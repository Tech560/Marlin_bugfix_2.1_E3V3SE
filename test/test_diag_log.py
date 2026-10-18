#!/usr/bin/env python3
"""Run the diagnostics ring buffer and the diag.log writer that ship in the
firmware. The SD card is stubbed, so the checks cover ordering, de-duplication,
truncation and the refusal to write during a print."""
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'Marlin/src/e3v3se/DiagLog.cpp').read_text()
HDR = (ROOT / 'Marlin/src/e3v3se/DiagLog.h').read_text()

CXX = shlex.split(os.environ.get('E3V3SE_TEST_CXX', 'g++'))

PROLOGUE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define DWIN_DIAG_LOG 1
#define ENABLED(V) (V)
#define PGM_P const char*
#define PSTR(x) (x)
static inline void strncpy_P(char *dst, const char *src, size_t n) { std::strncpy(dst, src, n); }
// Marlin's printf understands %S for program-memory strings; the host one does not.
static int host_snprintf_P(char *out, size_t n, const char *fmt, ...) {
  std::string f(fmt);
  for (size_t i = f.find("%S"); i != std::string::npos; i = f.find("%S"))
    f.replace(i, 2, "%s");
  va_list args;
  va_start(args, fmt);
  const int written = std::vsnprintf(out, n, f.c_str(), args);
  va_end(args);
  return written;
}
#define snprintf_P host_snprintf_P
struct CardStub {
  static bool mounted, printing, open;
  static std::string data;
  static bool isMounted() { return mounted; }
  static bool isPrinting() { return printing; }
  static void openFileWrite(const char *) { open = true; data.clear(); }
  static bool isFileOpen() { return open; }
  static void closefile() { open = false; }
  static int16_t write(void *buf, uint16_t n) { data.append(static_cast<char *>(buf), n); return int16_t(n); }
} card;
bool CardStub::mounted = true;
bool CardStub::printing = false;
bool CardStub::open = false;
std::string CardStub::data;
'''

MAIN = r'''
int main() {
  assert(diag_count() == 0);
  assert(std::strcmp(diag_line(0), "") == 0);

  // Newest first, oldest last.
  diag_note(PSTR("power on"));
  diag_note(PSTR("thermal runaway"));
  diag_note(PSTR("printer halted"));
  assert(diag_count() == 3);
  assert(std::strcmp(diag_line(0), "printer halted") == 0);
  assert(std::strcmp(diag_line(1), "thermal runaway") == 0);
  assert(std::strcmp(diag_line(2), "power on") == 0);
  assert(std::strcmp(diag_line(3), "") == 0);

  // The same event twice in a row is one line, a different one is not.
  diag_note(PSTR("printer halted"));
  assert(diag_count() == 3);
  diag_note(PSTR("power on"));
  assert(diag_count() == 4);

  // A line never exceeds the display width and stays terminated.
  char long_msg[80];
  std::memset(long_msg, 'x', sizeof(long_msg) - 1);
  long_msg[sizeof(long_msg) - 1] = '\0';
  diag_note(long_msg);
  assert(std::strlen(diag_line(0)) == DIAG_LINE_LEN - 1);

  // Values are appended with a space.
  diag_note_i(PSTR("bed target"), 110);
  assert(std::strcmp(diag_line(0), "bed target 110") == 0);

  // The ring keeps the newest entries only.
  for (int i = 0; i < DIAG_LINES + 2; ++i) diag_note_i(PSTR("event"), i);
  assert(diag_count() == DIAG_LINES);
  char expected[DIAG_LINE_LEN];
  std::snprintf(expected, sizeof(expected), "event %d", DIAG_LINES + 1);
  assert(std::strcmp(diag_line(0), expected) == 0);

  // Nothing is written while printing or without a card.
  CardStub::printing = true;
  assert(!diag_flush() && CardStub::data.empty());
  CardStub::printing = false;
  CardStub::mounted = false;
  assert(!diag_flush() && CardStub::data.empty());

  // Idle and mounted: oldest first, one CRLF per line.
  CardStub::mounted = true;
  assert(diag_flush());
  // Ten pushes into an eight-line ring leave events 2 through 9, oldest first.
  assert(CardStub::data.rfind("event 2", 0) == 0);
  const size_t lines = std::count(CardStub::data.begin(), CardStub::data.end(), '\n');
  assert(lines == DIAG_LINES);
  assert(CardStub::data.find("event 9") != std::string::npos);

  diag_clear();
  assert(diag_count() == 0);
  assert(!diag_flush());                    // nothing to write
  std::printf("PASS diagnostics ring, truncation, de-duplication and flush guards\n");
  return 0;
}
'''


def strip_includes(text):
    skip = ('#include', '#pragma once')
    return '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith(skip))


def main():
    cpp = PROLOGUE + strip_includes(HDR) + strip_includes(SRC) + MAIN
    cpp = cpp.replace('#include <cstdarg>', '')
    cpp = '#include <cstdarg>\n#include <algorithm>\n' + cpp
    with tempfile.TemporaryDirectory(prefix='diag-log-') as folder:
        source, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
        source.write_text(cpp)
        subprocess.run([*CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
