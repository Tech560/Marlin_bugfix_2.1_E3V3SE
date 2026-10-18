/** E3V3SE diagnostics: what happened before the printer stopped. */
#include "../inc/MarlinConfig.h"

#if ENABLED(DWIN_DIAG_LOG)

#include "DiagLog.h"
#include "../sd/cardreader.h"

#include <string.h>

namespace {
  char ring[DIAG_LINES][DIAG_LINE_LEN];
  uint8_t newest = 0;   // index of the most recent line
  uint8_t used = 0;     // number of valid lines

  void push(const char * const msg) {
    const uint8_t next = used ? uint8_t((newest + 1) % DIAG_LINES) : 0;
    if (used && !strncmp(ring[newest], msg, DIAG_LINE_LEN - 1)) return;   // same event again
    strncpy(ring[next], msg, DIAG_LINE_LEN - 1);
    ring[next][DIAG_LINE_LEN - 1] = '\0';
    newest = next;
    if (used < DIAG_LINES) ++used;
  }
}

void diag_note(PGM_P msg) {
  char buf[DIAG_LINE_LEN];
  strncpy_P(buf, msg, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  push(buf);
}

void diag_note_i(PGM_P msg, const int32_t value) {
  char buf[DIAG_LINE_LEN];
  snprintf_P(buf, sizeof(buf), PSTR("%S %ld"), msg, (long)value);
  push(buf);
}

uint8_t diag_count() { return used; }

const char* diag_line(const uint8_t index) {
  if (index >= used) return "";
  return ring[(newest + DIAG_LINES - index) % DIAG_LINES];
}

void diag_clear() {
  used = 0;
  newest = 0;
  ring[0][0] = '\0';
}

bool diag_flush() {
  if (!used || !card.isMounted() || card.isPrinting()) return false;

  card.openFileWrite("diag.log");
  if (!card.isFileOpen()) { card.closefile(); return false; }

  bool ok = true;
  char eol[2] = { '\r', '\n' };
  // Oldest first, so the file reads chronologically. card.write() takes a
  // void* for the SdFat layer; nothing here modifies the buffer.
  for (uint8_t i = 0; i < used; ++i) {
    const char * const line = diag_line(uint8_t(used - 1 - i));
    if (card.write((void*)line, uint16_t(strlen(line))) < 0) { ok = false; break; }
    if (card.write(eol, 2) < 0) { ok = false; break; }
  }
  card.closefile();
  return ok;
}

#endif // DWIN_DIAG_LOG
