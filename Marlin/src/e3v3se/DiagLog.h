/** E3V3SE diagnostics: what happened before the printer stopped. */
#pragma once

#include "../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_DIAG_LOG)

  #define DIAG_LINES     8
  #define DIAG_LINE_LEN  40

  /** Record a message (repeat of the newest line is ignored). */
  void diag_note(PGM_P msg);
  /** Record "msg value", for counts and temperatures. */
  void diag_note_i(PGM_P msg, const int32_t value);
  /** Number of stored lines, newest first. */
  uint8_t diag_count();
  /** Line by index, 0 = newest, or an empty string when out of range. */
  const char* diag_line(const uint8_t index);
  /** Write the ring to diag.log. Refused while printing or without a card. */
  bool diag_flush();
  /** Empty the ring (the file on the card is left alone). */
  void diag_clear();

#endif // DWIN_DIAG_LOG
