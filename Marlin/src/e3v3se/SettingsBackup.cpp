/** E3V3SE settings backup: the Z-offset, bed mesh and machine settings as a
 *  file on the SD card, restorable after a firmware update.
 *
 * The file holds the commands that set the values instead of a copy of the
 * EEPROM block, so a firmware that moved a setting to another address can
 * still restore it. It is a readable text file as well.
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(DWIN_SETTINGS_BACKUP)

#include "SettingsBackup.h"
#include "SDFiles.h"

#include "../MarlinCore.h"
#include "../gcode/gcode.h"
#include "../sd/cardreader.h"
#include "../module/motion.h"
#include "../module/planner.h"
#include "../module/settings.h"
#include "../feature/bedlevel/bedlevel.h"
#include "../module/probe.h"
#include "../module/temperature.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

  // One line, built in place. The longest is the four-axis line, and every
  // number is capped below, so this cannot be overrun by a bad EEPROM value.
  constexpr uint8_t LINE_LEN = 64;
  constexpr int32_t MAX_SCALED = 999999999L;   // nine digits plus sign
  char line[LINE_LEN];

  char* put_text(char *p, const char *text) {
    while (*text) *p++ = *text++;
    *p = '\0';
    return p;
  }

  char* put_uint(char *p, uint32_t value) {
    if (value > uint32_t(MAX_SCALED)) value = MAX_SCALED;
    char digits[10];
    uint8_t n = 0;
    do { digits[n++] = char('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = digits[--n];
    *p = '\0';
    return p;
  }

  // "%.3f" and friends. This build has no float printf, and a fixed point
  // number with the decimal point put in is shorter than one anyway.
  char* put_fixed(char *p, const float value, const uint8_t decimals) {
    uint32_t scale = 1;
    LOOP_L_N(i, decimals) scale *= 10;
    // Keep the scaled number under the cap, so a damaged EEPROM value can
    // neither overflow the arithmetic nor walk off the line buffer. A NaN
    // fails both comparisons and becomes zero.
    const float limit = float(MAX_SCALED) / float(scale);
    const float bounded = (value > -limit && value < limit) ? value : 0;
    int32_t scaled = lroundf(bounded * float(scale));
    if (scaled < 0) { *p++ = '-'; scaled = -scaled; }
    p = put_uint(p, uint32_t(scaled) / scale);
    if (decimals) {
      *p++ = '.';
      uint32_t frac = uint32_t(scaled) % scale;
      for (uint32_t div = scale / 10; div; div /= 10) { *p++ = char('0' + frac / div); frac %= div; }
      *p = '\0';
    }
    return p;
  }

  /// Terminate the line that ends at \p end with a newline and hand it to the
  /// card, reporting whether all of it went.
  bool write_line(const char * const end) {
    const uint16_t len = end - line;
    if (len >= LINE_LEN - 1) return false;        // the newline needs a slot too
    line[len] = '\n';
    return card.write((void*)line, len + 1) == int16_t(len + 1);
  }

  /// Whole millimetres per second squared and similar, rounded not truncated.
  uint32_t whole(const float value) {
    if (!(value > 0) || value > float(MAX_SCALED)) return 0;
    return uint32_t(value + 0.5f);
  }

  /// Write every setting as the command that sets it.
  bool write_values() {
    bool ok = write_line(put_text(line, "; E3V3SE settings"));
    char *p;

    #if HAS_BED_PROBE
      p = put_text(line, "M851 X");
      p = put_fixed(p, probe.offset.x, 2);
      p = put_text(p, " Y");
      p = put_fixed(p, probe.offset.y, 2);
      p = put_text(p, " Z");
      p = put_fixed(p, probe.offset.z, 3);
      ok = ok && write_line(p);
    #endif

    p = put_text(line, "M206 X");
    p = put_fixed(p, motion.home_offset.x, 2);
    p = put_text(p, " Y");
    p = put_fixed(p, motion.home_offset.y, 2);
    p = put_text(p, " Z");
    p = put_fixed(p, motion.home_offset.z, 2);
    ok = ok && write_line(p);

    p = put_text(line, "M92 X");
    p = put_fixed(p, planner.settings.axis_steps_per_mm[X_AXIS], 2);
    p = put_text(p, " Y");
    p = put_fixed(p, planner.settings.axis_steps_per_mm[Y_AXIS], 2);
    p = put_text(p, " Z");
    p = put_fixed(p, planner.settings.axis_steps_per_mm[Z_AXIS], 2);
    p = put_text(p, " E");
    p = put_fixed(p, planner.settings.axis_steps_per_mm[E_AXIS], 2);
    ok = ok && write_line(p);

    p = put_text(line, "M201 X");
    p = put_uint(p, planner.settings.max_acceleration_mm_per_s2[X_AXIS]);
    p = put_text(p, " Y");
    p = put_uint(p, planner.settings.max_acceleration_mm_per_s2[Y_AXIS]);
    p = put_text(p, " Z");
    p = put_uint(p, planner.settings.max_acceleration_mm_per_s2[Z_AXIS]);
    p = put_text(p, " E");
    p = put_uint(p, planner.settings.max_acceleration_mm_per_s2[E_AXIS]);
    ok = ok && write_line(p);

    p = put_text(line, "M203 X");
    p = put_fixed(p, planner.settings.max_feedrate_mm_s[X_AXIS], 1);
    p = put_text(p, " Y");
    p = put_fixed(p, planner.settings.max_feedrate_mm_s[Y_AXIS], 1);
    p = put_text(p, " Z");
    p = put_fixed(p, planner.settings.max_feedrate_mm_s[Z_AXIS], 1);
    p = put_text(p, " E");
    p = put_fixed(p, planner.settings.max_feedrate_mm_s[E_AXIS], 1);
    ok = ok && write_line(p);

    p = put_text(line, "M204 P");
    p = put_uint(p, whole(planner.settings.acceleration));
    p = put_text(p, " R");
    p = put_uint(p, whole(planner.settings.retract_acceleration));
    p = put_text(p, " T");
    p = put_uint(p, whole(planner.settings.travel_acceleration));
    ok = ok && write_line(p);

    #if ENABLED(SKEW_CORRECTION_GCODE)
      p = put_text(line, "M852 I");
      p = put_fixed(p, planner.skew_factor.xy, 6);
      ok = ok && write_line(p);
    #endif

    #if ENABLED(PIDTEMP)
      p = put_text(line, "M301 P");
      p = put_fixed(p, thermalManager.temp_hotend[0].pid.Kp, 2);
      p = put_text(p, " I");
      p = put_fixed(p, thermalManager.temp_hotend[0].pid.Ki, 2);
      p = put_text(p, " D");
      p = put_fixed(p, thermalManager.temp_hotend[0].pid.Kd, 2);
      ok = ok && write_line(p);
    #endif

    #if ENABLED(PIDTEMPBED)
      p = put_text(line, "M304 P");
      p = put_fixed(p, thermalManager.temp_bed.pid.Kp, 2);
      p = put_text(p, " I");
      p = put_fixed(p, thermalManager.temp_bed.pid.Ki, 2);
      p = put_text(p, " D");
      p = put_fixed(p, thermalManager.temp_bed.pid.Kd, 2);
      ok = ok && write_line(p);
    #endif

    #if ENABLED(AUTO_BED_LEVELING_BILINEAR)
      // One command per point, so a single bad value cannot shift the rest.
      LOOP_L_N(x, GRID_MAX_POINTS_X) {
        LOOP_L_N(y, GRID_MAX_POINTS_Y) {
          p = put_text(line, "M421 I");
          p = put_uint(p, x);
          p = put_text(p, " J");
          p = put_uint(p, y);
          p = put_text(p, " Z");
          p = put_fixed(p, bedlevel.z_values[x][y], 3);
          ok = ok && write_line(p);
        }
      }
    #endif

    return ok;
  }

  /// True when the line is one of the commands this module writes. Anything
  /// else is skipped, so a hand-edited file cannot make the printer move.
  bool apply_line() {
    if (line[0] != 'M') return false;
    switch (atoi(&line[1])) {
      case 92: case 201: case 203: case 204: case 206:
      case 301: case 304: case 421: case 851: case 852:
        gcode.process_subcommands_now(line);
        return true;
      default:
        return false;
    }
  }

} // namespace

settingsBackup::Result settingsBackup::save() {
  if (!card.isMounted()) return Result::NotMounted;
  if (!sdFiles::deletable_now()) return Result::Busy;

  card.openFileWrite(SETTINGS_BACKUP_FILE);
  if (!card.isFileOpen()) { card.closefile(); return Result::Failed; }

  // The file is replaced from the start, so an old backup cannot survive in
  // the new one.
  const bool wrote = write_values();

  card.closefile();
  return wrote ? Result::Saved : Result::Failed;
}

settingsBackup::Result settingsBackup::restore() {
  if (!card.isMounted()) return Result::NotMounted;
  if (!sdFiles::deletable_now()) return Result::Busy;
  if (!card.fileExists(SETTINGS_BACKUP_FILE)) return Result::NoBackup;

  card.openFileRead(SETTINGS_BACKUP_FILE);
  if (!card.isFileOpen()) { card.closefile(); return Result::Failed; }

  // Read a line at a time: a whole file would need a buffer of well over a
  // kilobyte, and the settings fit in a handful of lines.
  bool applied = false, read_ok = true;
  uint8_t n = 0;
  for (;;) {
    char ch;
    const int16_t got = card.read(&ch, 1);
    if (got < 0) { read_ok = false; break; }
    if (!got) break;                            // end of the file
    if (ch == '\n') {
      line[n] = '\0';
      applied = apply_line() || applied;
      n = 0;
    }
    else if (ch != '\r' && n < LINE_LEN - 1)
      line[n++] = ch;
  }
  if (n) {
    line[n] = '\0';
    applied = apply_line() || applied;           // last line without a newline
  }
  card.closefile();

  if (!read_ok) return Result::Failed;
  if (!applied) return Result::Empty;

  TERN_(EEPROM_SETTINGS, settings.save());
  TERN_(HAS_LEVELING, bedlevel.refresh_bed_level());
  return Result::Restored;
}

bool settingsBackup::ok(const Result result) {
  return result == Result::Saved || result == Result::Restored;
}

const char *settingsBackup::result_text(const Result result) {
  switch (result) {
    case Result::Saved:      return "Backup saved";
    case Result::Restored:   return "Restored";
    case Result::NotMounted: return "No SD card";
    case Result::Busy:       return "Printing";
    case Result::NoBackup:   return "No settings.bak";
    case Result::Empty:      return "Not a backup";
    default:                 return "Backup failed";
  }
}

#endif // DWIN_SETTINGS_BACKUP
