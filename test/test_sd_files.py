#!/usr/bin/env python3
"""Run the SD file deletion guards that ship in the firmware. The card reader is
stubbed, so the checks cover the refusals, the 8.3 name that is passed on and the
verification after the removal."""
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'Marlin/src/e3v3se/SDFiles.cpp').read_text()
HDR = (ROOT / 'Marlin/src/e3v3se/SDFiles.h').read_text()

CXX = shlex.split(os.environ.get('E3V3SE_TEST_CXX', 'g++'))

PROLOGUE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define SDSUPPORT 1
#define FILENAME_LENGTH 13
struct CardFlags { bool filenameIsDir; };
struct CardStub {
  static bool mounted, printing, exists_after_remove;
  static CardFlags flag;
  static char filename[FILENAME_LENGTH];
  static char removed_name[FILENAME_LENGTH];
  static int removed;
  static bool isMounted() { return mounted; }
  static bool isPrinting() { return printing; }
  static void removeFile(const char *name) {
    ++removed;
    std::strncpy(removed_name, name, sizeof(removed_name));
    removed_name[sizeof(removed_name) - 1] = 0;
  }
  static bool fileExists(const char *) { return exists_after_remove; }
};
bool CardStub::mounted = true;
bool CardStub::printing = false;
bool CardStub::exists_after_remove = false;
CardFlags CardStub::flag = { false };
char CardStub::filename[FILENAME_LENGTH] = { 0 };
char CardStub::removed_name[FILENAME_LENGTH] = { 0 };
int CardStub::removed = 0;
static CardStub card;
struct MarlinStub { bool active; bool printingIsActive() const { return active; } };
static MarlinStub marlin;
'''

MAIN = r'''
int main() {
  using R = sdFiles::DeleteResult;

  // No card: nothing to delete, and the card is not touched.
  CardStub::mounted = false;
  CardStub::removed = 0;
  assert(!sdFiles::deletable_now());
  assert(sdFiles::remove_selected() == R::NotMounted);
  assert(CardStub::removed == 0);
  CardStub::mounted = true;

  // A running job blocks deletion, both the SD job and a host job.
  CardStub::printing = true;
  assert(!sdFiles::deletable_now());
  assert(sdFiles::remove_selected() == R::Busy);
  assert(CardStub::removed == 0);
  CardStub::printing = false;
  marlin.active = true;
  assert(sdFiles::remove_selected() == R::Busy);
  assert(CardStub::removed == 0);
  marlin.active = false;
  assert(sdFiles::deletable_now());

  // Folders are left alone.
  std::strcpy(CardStub::filename, "FOLDER~1");
  CardStub::flag.filenameIsDir = true;
  assert(sdFiles::remove_selected() == R::IsFolder);
  assert(CardStub::removed == 0);
  CardStub::flag.filenameIsDir = false;

  // Without a selection there is nothing to remove.
  CardStub::filename[0] = 0;
  assert(sdFiles::remove_selected() == R::NoFile);
  assert(CardStub::removed == 0);

  // A normal delete passes the 8.3 name on and reports success once it is gone.
  std::strcpy(CardStub::filename, "BENCHY~1.GCO");
  CardStub::exists_after_remove = false;
  assert(sdFiles::remove_selected() == R::Deleted);
  assert(CardStub::removed == 1);
  assert(std::strcmp(CardStub::removed_name, "BENCHY~1.GCO") == 0);
  assert(sdFiles::deleted(R::Deleted));

  // If the entry is still there, the card refused it.
  CardStub::exists_after_remove = true;
  assert(sdFiles::remove_selected() == R::Failed);
  assert(CardStub::removed == 2);
  assert(!sdFiles::deleted(R::Failed));

  // Every result has a short line for the LCD.
  for (const R r : { R::Deleted, R::NotMounted, R::Busy, R::IsFolder, R::NoFile, R::Failed })
    assert(std::strlen(sdFiles::result_text(r)) > 3);
  assert(std::strcmp(sdFiles::result_text(R::Busy), "Printing: not deleted") == 0);

  std::printf("PASS SD file delete: refused while busy, folders and empty names skipped, "
              "8.3 name passed on and removal verified (%u calls)\n", unsigned(CardStub::removed));
  return 0;
}
'''


def strip_includes(text):
    skip = ('#include', '#pragma once')
    return '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith(skip)) + '\n'


def main():
    cpp = PROLOGUE + strip_includes(HDR) + strip_includes(SRC) + MAIN
    cpp = '#include <cstdlib>\n' + cpp
    with tempfile.TemporaryDirectory(prefix='sd-files-') as folder:
        source, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
        source.write_text(cpp)
        subprocess.run([*CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
