#!/usr/bin/env python3
"""Run the settings backup module that ships in the firmware. The SD card, the
G-code handlers and the machine state are stubbed, so the checks cover the
lines written, the commands a restore runs, the refusal to touch the card while
printing, and every reported result.

The file is the interface: a backup written by this build is read back as the
G-code that sets the same values, which is what makes it survive a firmware
update that moved a setting in EEPROM.
"""
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'Marlin/src/e3v3se/SettingsBackup.cpp').read_text()
HDR = (ROOT / 'Marlin/src/e3v3se/SettingsBackup.h').read_text()

CXX = shlex.split(os.environ.get('E3V3SE_TEST_CXX', 'g++'))

PROLOGUE = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define DWIN_SETTINGS_BACKUP 1
#define HAS_BED_PROBE 1
#define AUTO_BED_LEVELING_BILINEAR 1
#define PIDTEMP 1
#define PIDTEMPBED 1
#define SKEW_CORRECTION_GCODE 1
#define EEPROM_SETTINGS 1
#define HAS_LEVELING 1
#define ENABLED(V) (V)
#define DISABLED(V) (!(V))
#define TERN_(C, V) V
#define LOOP_L_N(N, C) for (uint8_t N = 0, _s_##N = uint8_t(C); N < _s_##N; ++N)
#define GRID_MAX_POINTS_X 4
#define GRID_MAX_POINTS_Y 4
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
#define E_AXIS 3

struct CardStub {
  static bool mounted, printing, open, exists, fail_writes;
  static size_t pos;
  static std::string written, input;
  static bool isMounted() { return mounted; }
  static void openFileWrite(const char *) { open = true; written.clear(); }
  static void openFileRead(const char *) { open = true; pos = 0; }
  static bool isFileOpen() { return open; }
  static void closefile() { open = false; }
  static bool fileExists(const char *) { return exists; }
  static int16_t write(void *buf, uint16_t n) {
    if (fail_writes) return -1;
    written.append(static_cast<char *>(buf), n);
    return int16_t(n);
  }
  static int16_t read(void *buf, uint16_t n) {
    if (pos >= input.size()) return 0;
    const size_t take = std::min<size_t>(n, input.size() - pos);
    std::memcpy(buf, input.data() + pos, take);
    pos += take;
    return int16_t(take);
  }
} card;
bool CardStub::mounted = true;
bool CardStub::printing = false;
bool CardStub::open = false;
bool CardStub::exists = false;
bool CardStub::fail_writes = false;
size_t CardStub::pos = 0;
std::string CardStub::written;
std::string CardStub::input;

namespace sdFiles {
  bool deletable_now() { return CardStub::mounted && !CardStub::printing; }
}

struct GcodeStub {
  std::string log;
  void process_subcommands_now(char *g) { log.append(g).append("\n"); }
} gcode;

struct PlannerSettings {
  float axis_steps_per_mm[4];
  uint32_t max_acceleration_mm_per_s2[4];
  float max_feedrate_mm_s[4];
  float acceleration, retract_acceleration, travel_acceleration;
};
struct SkewFactor { float xy, xz, yz; };
struct PlannerStub {
  PlannerSettings settings;
  SkewFactor skew_factor;
} planner;

struct MotionStub {
  struct { float x, y, z; } home_offset;
} motion;

struct ProbeStub {
  struct { float x, y, z; } offset;
} probe;

struct BedLevelStub {
  float z_values[GRID_MAX_POINTS_X][GRID_MAX_POINTS_Y];
  int refreshes = 0;
  void refresh_bed_level() { ++refreshes; }
} bedlevel;

struct PidStub { float Kp, Ki, Kd; };
struct ThermalStub {
  struct { PidStub pid; } temp_hotend[1];
  struct { PidStub pid; } temp_bed;
} thermalManager;

struct SettingsStub {
  int saves = 0;
  bool save() { ++saves; return true; }
} settings;

static void machine_defaults() {
  const float steps[4] = { 80.0f, 80.0f, 400.0f, 424.9f };
  const uint32_t accel[4] = { 1000, 1000, 100, 5000 };
  const float feed[4] = { 300.0f, 300.0f, 5.0f, 25.0f };
  for (int i = 0; i < 4; ++i) {
    planner.settings.axis_steps_per_mm[i] = steps[i];
    planner.settings.max_acceleration_mm_per_s2[i] = accel[i];
    planner.settings.max_feedrate_mm_s[i] = feed[i];
  }
  planner.settings.acceleration = 1000.0f;
  planner.settings.retract_acceleration = 1000.0f;
  planner.settings.travel_acceleration = 1500.0f;
  planner.skew_factor = { 0.000123f, 0.0f, 0.0f };
  motion.home_offset = { 0.0f, 0.0f, 1.25f };
  probe.offset = { -31.0f, -38.0f, -2.35f };
  thermalManager.temp_hotend[0].pid = { 21.73f, 1.66f, 116.29f };
  thermalManager.temp_bed.pid = { 62.61f, 2.05f, 806.93f };
  for (int x = 0; x < GRID_MAX_POINTS_X; ++x)
    for (int y = 0; y < GRID_MAX_POINTS_Y; ++y)
      bedlevel.z_values[x][y] = 0.125f * float(x) - 0.25f * float(y);
  bedlevel.z_values[1][2] = -1.5f;
}

static bool has_line(const std::string &text, const char *line) {
  const std::string wanted = std::string(line) + "\n";
  size_t at = text.find(wanted);
  while (at != std::string::npos) {
    if (at == 0 || text[at - 1] == '\n') return true;
    at = text.find(wanted, at + 1);
  }
  return false;
}
'''

MAIN = r'''
int main() {
  using settingsBackup::Result;

  machine_defaults();

  // Nothing is written without a card, and nothing while a job runs.
  CardStub::mounted = false;
  assert(settingsBackup::save() == Result::NotMounted);
  assert(CardStub::written.empty());
  CardStub::mounted = true;
  CardStub::printing = true;
  assert(settingsBackup::save() == Result::Busy);
  assert(CardStub::written.empty());
  CardStub::printing = false;

  // A card that refuses the write is reported as a failure, not as saved.
  CardStub::fail_writes = true;
  assert(settingsBackup::save() == Result::Failed);
  CardStub::fail_writes = false;

  // The values, written as the commands that set them.
  assert(settingsBackup::save() == Result::Saved);
  const std::string &text = CardStub::written;
  assert(text.rfind("; E3V3SE settings\n", 0) == 0);
  assert(has_line(text, "M851 X-31.00 Y-38.00 Z-2.350"));
  assert(has_line(text, "M206 X0.00 Y0.00 Z1.25"));
  assert(has_line(text, "M92 X80.00 Y80.00 Z400.00 E424.90"));
  assert(has_line(text, "M201 X1000 Y1000 Z100 E5000"));
  assert(has_line(text, "M203 X300.0 Y300.0 Z5.0 E25.0"));
  assert(has_line(text, "M204 P1000 R1000 T1500"));
  assert(has_line(text, "M852 I0.000123"));
  assert(has_line(text, "M301 P21.73 I1.66 D116.29"));
  assert(has_line(text, "M304 P62.61 I2.05 D806.93"));
  assert(has_line(text, "M421 I0 J0 Z0.000"));
  assert(has_line(text, "M421 I1 J2 Z-1.500"));
  assert(has_line(text, "M421 I3 J3 Z-0.375"));

  // One command per mesh point, every line terminated, none over the buffer.
  size_t mesh_lines = 0;
  for (size_t at = 0; at < text.size();) {
    const size_t end = text.find('\n', at);
    assert(end != std::string::npos);
    const size_t len = end - at;
    assert(len > 0 && len <= LINE_LEN - 1);   // as long as the line buffer allows
    if (text.compare(at, 4, "M421") == 0) ++mesh_lines;
    at = end + 1;
  }
  assert(mesh_lines == GRID_MAX_POINTS_X * GRID_MAX_POINTS_Y);

  // Restore without a card, while printing, and without a file.
  settings.saves = 0;
  CardStub::mounted = false;
  assert(settingsBackup::restore() == Result::NotMounted);
  CardStub::mounted = true;
  CardStub::printing = true;
  assert(settingsBackup::restore() == Result::Busy);
  CardStub::printing = false;
  CardStub::exists = false;
  assert(settingsBackup::restore() == Result::NoBackup);

  // Only the settings commands run: a comment, an unknown command and a move
  // in the file are skipped, so a hand-edited file cannot move the printer.
  CardStub::exists = true;
  CardStub::input =
    "; E3V3SE settings\n"
    "M851 X-31.00 Y-38.00 Z-2.350\n"
    "G1 X10 Y10\n"
    "M104 S200\n"
    "M92 X80.00 Y80.00 Z400.00 E424.90\n"
    "M421 I0 J0 Z0.125\n"
    "M852 I0.000123\n";
  gcode.log.clear();
  bedlevel.refreshes = 0;
  assert(settingsBackup::restore() == Result::Restored);
  assert(gcode.log ==
    "M851 X-31.00 Y-38.00 Z-2.350\n"
    "M92 X80.00 Y80.00 Z400.00 E424.90\n"
    "M421 I0 J0 Z0.125\n"
    "M852 I0.000123\n");
  assert(settings.saves == 1);            // the restored values are stored
  assert(bedlevel.refreshes == 1);

  // A last line without a newline still counts.
  CardStub::input = "M851 Z-2.200";
  gcode.log.clear();
  assert(settingsBackup::restore() == Result::Restored);
  assert(gcode.log == "M851 Z-2.200\n");
  assert(settings.saves == 2);

  // CRLF, and a file that holds no settings at all.
  CardStub::input = "M206 X1.00 Y2.00 Z3.00\r\n";
  gcode.log.clear();
  assert(settingsBackup::restore() == Result::Restored);
  assert(gcode.log == "M206 X1.00 Y2.00 Z3.00\n");

  CardStub::input = "; nothing to do here\nG28\n";
  gcode.log.clear();
  const int saves_before = settings.saves;
  assert(settingsBackup::restore() == Result::Empty);
  assert(gcode.log.empty());
  assert(settings.saves == saves_before);

  // Every result has a short line for the LCD, and only two mean success.
  const Result all[] = { Result::Saved, Result::Restored, Result::NotMounted,
                         Result::Busy, Result::NoBackup, Result::Empty, Result::Failed };
  for (const Result r : all) {
    const char *line = settingsBackup::result_text(r);
    assert(line && *line && std::strlen(line) <= 20);
    assert(settingsBackup::ok(r) == (r == Result::Saved || r == Result::Restored));
  }

  std::printf("PASS settings backup write, restore, filters and guards\n");
  return 0;
}
'''


def strip_includes(text):
    skip = ('#include', '#pragma once')
    return '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith(skip))


def main():
    cpp = '\n'.join([PROLOGUE, strip_includes(HDR), strip_includes(SRC), MAIN])
    with tempfile.TemporaryDirectory(prefix='settings-backup-') as folder:
        source, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
        source.write_text(cpp)
        subprocess.run([*CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
