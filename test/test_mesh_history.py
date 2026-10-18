#!/usr/bin/env python3
"""Run the mesh snapshot serialisation, delta and formatting code that the LCD
history screen uses. Compiles the production sources with a small shim, so the
checks apply to the code that ships."""
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

# E3V3SE_TEST_CXX lets a host without a C++17 g++ point the test at another
# compiler (for example "zig c++"); the default keeps the repo convention.
CXX = shlex.split(os.environ.get('E3V3SE_TEST_CXX', 'g++'))

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'Marlin/src/e3v3se/MeshHistory.cpp').read_text()
HDR = (ROOT / 'Marlin/src/e3v3se/MeshHistory.h').read_text()

PROLOGUE = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define DWIN_MESH_HISTORY 1
#define GRID_MAX_POINTS_X 6
#define GRID_MAX_POINTS_Y 6
#define ENABLED(V) (V)
'''

MAIN = r'''
static bool close_enough(const float a, const float b) { return std::fabs(a - b) <= 0.001f; }

int main() {
  // Quantisation keeps a thousandth of a millimetre and clamps the extremes.
  for (float mm : {0.0f, 0.05f, -0.237f, 1.234f, -1.999f}) {
    const int16_t um = mesh_quantize(mm);
    assert(std::labs(long(um) - long(mm * 1000.0f)) <= 1);
    assert(close_enough(mesh_dequantize(um), mm));
  }
  assert(mesh_quantize(40.0f) == 32767);
  assert(mesh_quantize(-40.0f) == -32768);

  // Deltas carry the sign and clamp to what four LCD columns can show.
  const int16_t before[MESH_HISTORY_POINTS] = {0};
  int16_t now[MESH_HISTORY_POINTS] = {0};
  now[0] = 40; now[1] = -200; now[2] = 200; now[3] = 0;
  int8_t delta[MESH_HISTORY_POINTS];
  mesh_delta(now, before, delta, MESH_HISTORY_POINTS);
  assert(delta[0] == 40 && delta[1] == -127 && delta[2] == 127 && delta[3] == 0);

  // Every value occupies four columns, so six of them always fit one LCD row.
  const int8_t row[6] = {-12, 5, 0, 123, -99, -7};
  char text[64];
  mesh_format_row(row, 6, text, sizeof(text));
  assert(std::strlen(text) == 6 * 4 + 5);
  assert(std::strcmp(text, "- 12 +  5    0 +123 - 99 -  7") == 0);

  // A snapshot survives a format/parse round trip, including the print counter.
  MeshSnapshot in;
  in.prints = 42;
  for (uint8_t i = 0; i < MESH_HISTORY_POINTS; ++i) in.z[i] = int16_t(i * 7 - 150);
  char line[MESH_HISTORY_LINE_LEN];
  const uint16_t n = mesh_format_line(in, line, sizeof(line));
  assert(n > 0 && n < sizeof(line));
  assert(line[n - 1] == '\n');
  MeshSnapshot out;
  std::memset(&out, 0, sizeof(out));
  assert(mesh_parse_line(line, out));
  assert(out.prints == in.prints);
  assert(std::memcmp(out.z, in.z, sizeof(in.z)) == 0);

  // A buffer too small is refused rather than overrun.
  char tiny[16];
  assert(mesh_format_line(in, tiny, sizeof(tiny)) == 0);

  // Torn or edited lines are rejected, not silently accepted.
  MeshSnapshot bad;
  line[n - 6] ^= 0x01;                       // flip a bit inside the checksum
  assert(!mesh_parse_line(line, bad));
  line[n - 6] ^= 0x01;
  assert(mesh_parse_line(line, bad));
  std::memmove(line + 8, line + 9, n - 8);    // drop one digit: wrong point count
  assert(!mesh_parse_line(line, bad));
  assert(!mesh_parse_line("XX1;1;0;0", bad));
  assert(!mesh_parse_line("", bad));

  std::printf("PASS mesh quantise/delta/format/parse with CRC rejection\n");
  return 0;
}
'''


def strip_includes(text):
    skip = ('#include', '#pragma once')
    return '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith(skip))


def main():
    cpp = PROLOGUE + strip_includes(HDR) + strip_includes(SRC) + MAIN
    with tempfile.TemporaryDirectory(prefix='mesh-history-') as folder:
        source, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
        source.write_text(cpp)
        subprocess.run([*CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
