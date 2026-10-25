#!/usr/bin/env python3
"""Run the E3V3SE probe-offset guard that ships in the firmware.

G28 moves the nozzle to Z_SAFE_HOMING_X/Y_POINT minus the probe's XY offset, so
a stale offset in EEPROM parks the head beside the bed centre. The nozzle does
the probing on this machine, so the guard may zero X and Y, but it has to leave
the Z offset alone: that one calibrates the first layer.
"""
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'Marlin/src/e3v3se/ProbeXY.cpp').read_text()
HDR = (ROOT / 'Marlin/src/e3v3se/ProbeXY.h').read_text()

CXX = shlex.split(os.environ.get('E3V3SE_TEST_CXX', 'g++'))

PROLOGUE = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#define E3V3SE_NOZZLE_AS_PROBE 1
#define ENABLED(V) (V)
struct ProbeOffsetStub { float x, y, z; };
struct ProbeStub { ProbeOffsetStub offset; } probe;
'''

MAIN = r'''
static bool same(const float a, const float b) { return std::fabs(a - b) < 1e-6f; }

int main() {
  // The values read off the printer: Auto Home went to X 134.2 / Y 125.0
  // instead of 110 / 110, which is 110 - (-24.2) and 110 - (-15.0).
  probe.offset = { -24.2f, -15.0f, -1.234f };
  e3v3se_clear_probe_xy();
  assert(same(probe.offset.x, 0.0f));
  assert(same(probe.offset.y, 0.0f));

  // The Z offset is the first-layer calibration and must survive.
  assert(same(probe.offset.z, -1.234f));

  // A second call (settings reloaded) is harmless and keeps Z.
  probe.offset = { 3.5f, 7.25f, 0.42f };
  e3v3se_clear_probe_xy();
  assert(same(probe.offset.x, 0.0f));
  assert(same(probe.offset.y, 0.0f));
  assert(same(probe.offset.z, 0.42f));

  std::printf("PASS clears the XY probe offset, keeps the Z offset\n");
  return 0;
}
'''


def strip_includes(text):
    skip = ('#include', '#pragma once')
    return '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith(skip))


def main():
    cpp = PROLOGUE + strip_includes(HDR) + strip_includes(SRC) + MAIN
    with tempfile.TemporaryDirectory(prefix='probe-xy-') as folder:
        source, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
        source.write_text(cpp)
        subprocess.run([*CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
