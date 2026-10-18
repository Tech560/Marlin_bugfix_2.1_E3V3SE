#!/usr/bin/env python3
"""Run the temperature tower generator that ships in the firmware. The SD card is
stubbed, so the checks cover the emitted G-code itself, the settings clamping and
the guards around writing the file."""
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'Marlin/src/e3v3se/TowerGen.cpp').read_text()
HDR = (ROOT / 'Marlin/src/e3v3se/TowerGen.h').read_text()

CXX = shlex.split(os.environ.get('E3V3SE_TEST_CXX', 'g++'))

PROLOGUE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define DWIN_TOWER_GEN 1
#define USE_WATCHDOG 0
#define ENABLED(V) (V)
#define HEATER_0_MAXTEMP 310
#define BED_MAXTEMP 125
#define constrain(V,L,H) ((V) < (L) ? (L) : ((V) > (H) ? (H) : (V)))
struct CardStub {
  static bool mounted, printing, open;
  static std::string name, data;
  static bool isMounted() { return mounted; }
  static bool isPrinting() { return printing; }
  static void removeFile(const char *) {}
  static void openFileWrite(const char *n) { open = true; name = n; data.clear(); }
  static bool isFileOpen() { return open; }
  static void closefile() { open = false; }
  static int16_t write(void *buf, uint16_t n) { data.append(static_cast<char *>(buf), n); return int16_t(n); }
  static void openAndPrintFile(const char *n) { name = n; }
};
bool CardStub::mounted = true;
bool CardStub::printing = false;
bool CardStub::open = false;
std::string CardStub::name, CardStub::data;
static CardStub card;
'''

MAIN = r'''
static float number_after(const std::string &line, const char axis) {
  const size_t at = line.find(std::string(" ") + axis);
  if (at == std::string::npos) return -1;
  return std::strtof(line.c_str() + at + 2, nullptr);
}

int main() {
  TowerSettings &s = towerGen::settings();

  // The defaults describe a printable tower and report its bottom band.
  towerGen::defaults();
  assert(s.start_temp == 230 && s.step == 5 && s.bands == 5 && s.bed_temp == 60 && s.band_mm == 10);
  assert(towerGen::last_temp(s) == 210);
  assert(towerGen::valid(s));

  // Nonsense is pulled back into a printable range instead of overflowing.
  s.start_temp = 400; s.step = 255; s.bands = 200; s.bed_temp = 200; s.band_mm = 0;
  towerGen::clamp(s);
  assert(s.start_temp == 310 && s.step == 20 && s.bed_temp == 125 && s.band_mm == 5);
  assert(towerGen::last_temp(s) >= 170);
  assert(towerGen::valid(s));

  // A tall setting set may not grow the file without bound.
  s.start_temp = 230; s.step = 0; s.bands = 10; s.band_mm = 30;
  towerGen::clamp(s);
  assert(s.bands * s.band_mm <= 100);

  // Generating the default tower.
  towerGen::defaults();
  std::string file;
  towerGen::Writer w;
  w.begin(s);
  char block[TOWER_BLOCK_SIZE];
  uint16_t blocks = 0;
  while (w.next(block, sizeof(block))) {
    assert(std::strlen(block) < TOWER_BLOCK_SIZE);      // never overruns the caller's buffer
    file += block;
    ++blocks;
  }
  assert(blocks > 500);                                  // header + skirt + bands + layers + footer
  assert(!w.next(block, sizeof(block)));                 // and it stays finished

  // Header and footer.
  assert(file.rfind("; E3V3SE temperature tower", 0) == 0);
  assert(file.find("G21\nG90\n") != std::string::npos);
  assert(file.find("M109 S230\n") != std::string::npos); // heat before the first layer
  assert(file.find("G28\n") != std::string::npos);
  assert(file.find("M83\n") != std::string::npos);       // relative extrusion
  assert(file.size() > 4);
  assert(file.compare(file.size() - 4, 4, "M84\n") == 0);

  // One temperature change per band, drilled down to the last band.
  // Header preheat, then one per band, and a final cooldown.
  size_t pos = 0;
  int m104 = 0;
  const int expect[6] = { 230, 230, 225, 220, 215, 210 };
  while ((pos = file.find("M104 S", pos)) != std::string::npos) {
    const int temp = std::atoi(file.c_str() + pos + 6);
    if (m104 < 6) assert(temp == expect[m104]);
    ++pos;
    ++m104;
  }
  assert(m104 == 7);

  // Every move stays inside the bed and every extrusion is positive.
  std::vector<float> zs;
  float max_x = 0, max_y = 0, total_e = 0;
  size_t line_start = 0;
  while (line_start < file.size()) {
    const size_t line_end = file.find('\n', line_start);
    const std::string line = file.substr(line_start, line_end - line_start);
    line_start = line_end + 1;
    if (line.rfind("G1 ", 0) != 0) continue;
    const float x = number_after(line, 'X'), y = number_after(line, 'Y'), e = number_after(line, 'E');
    if (x >= 0) { assert(x <= 220); if (x > max_x) max_x = x; }
    if (y >= 0) { assert(y <= 227); if (y > max_y) max_y = y; }
    if (e >= 0) { assert(e > 0); total_e += e; }
    if (line.rfind("G1 Z", 0) == 0)
      zs.push_back(std::strtof(line.c_str() + 4, nullptr));
  }
  assert(max_x > 100 && max_y > 90);                     // the tower is on the bed
  assert(total_e > 1000 && total_e < 2000);              // about 12 g of filament

  // 253 Z moves: two priming moves, one per layer of five 10 mm bands, and the
  // final relative lift. Each layer is exactly 0.2 mm above the previous one.
  assert(zs.size() == 2 + 250 + 1);
  assert(zs[2] == 0.2f);                                // first tower layer
  float tallest = zs[2];
  for (size_t i = 3; i < 2 + 250; ++i) {
    const float step = zs[i] - tallest;
    assert(step > 0.199f && step < 0.201f);
    tallest = zs[i];
  }
  assert(tallest > 49.99f && tallest < 50.01f);          // five bands of 10 mm
  assert(zs[2 + 250] > 9.99f && zs[2 + 250] < 10.01f);   // relative lift at the end

  // Writing the file guards on the card state and keeps the 8.3 name.
  CardStub::printing = true;
  assert(!towerGen::write_file());
  CardStub::printing = false;
  CardStub::mounted = false;
  assert(!towerGen::write_file());
  CardStub::mounted = true;
  assert(towerGen::write_file());
  assert(CardStub::name == TOWER_FILE);
  assert(CardStub::data == file);                        // the file is exactly what Writer emits
  assert(!CardStub::open);                               // and the file was closed again

  std::printf("PASS tower generator: %u blocks, %zu bytes, %zu Z moves, %.1f mm of filament\n",
              unsigned(blocks), file.size(), zs.size(), double(total_e));
  return 0;
}
'''


def strip_includes(text):
    skip = ('#include', '#pragma once')
    return '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith(skip)) + '\n'


def main():
    cpp = PROLOGUE + strip_includes(HDR) + strip_includes(SRC) + MAIN
    cpp = '#include <cstdlib>\n' + cpp
    with tempfile.TemporaryDirectory(prefix='tower-gen-') as folder:
        source, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
        source.write_text(cpp)
        subprocess.run([*CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
