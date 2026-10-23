/**
 * M8025: calibration tower generator.
 *
 *   M8025                                   Report the settings and the file name
 *   M8025 R<0|1|2>                          Tower type: 0 temperature, 1 retraction, 2 ringing
 *   M8025 S230 T5 B5 H10 D60                Start temp (or print temp), step, bands,
 *                                           band height and bed temp (values are clamped)
 *   M8025 A3                                Start of the retraction length (0.1 mm) or frequency (Hz)
 *   M8025 W                                 Write the tower file to the card
 *   M8025 P                                 Write it and start the print
 */
#include "../gcode/gcode.h"

#if ENABLED(DWIN_TOWER_GEN)

#include "TowerGen.h"

void GcodeSuite::M8025() {
  TowerSettings &s = towerGen::settings();

  // Every value is read as a signed integer first: a negative or huge number
  // must never wrap around before clamp() sees it.
  bool changed = false;
  if (parser.seenval('R')) {
    const int type = constrain(parser.value_int(), 0L, 2L);
    towerGen::type_defaults(uint8_t(type));   // keeps the print and size settings
    changed = true;
  }
  if (parser.seenval('S')) { s.start_temp = uint16_t(constrain(parser.value_int(), 0L, 1000L)); changed = true; }
  if (parser.seenval('A')) { s.start = uint8_t(constrain(parser.value_int(), 0L, 255L)); changed = true; }
  if (parser.seenval('T')) { s.step = uint8_t(constrain(parser.value_int(), 0L, 255L)); changed = true; }
  if (parser.seenval('B')) { s.bands = uint8_t(constrain(parser.value_int(), 0L, 255L)); changed = true; }
  if (parser.seenval('H')) { s.band_mm = uint8_t(constrain(parser.value_int(), 0L, 255L)); changed = true; }
  if (parser.seenval('D')) { s.bed_temp = uint8_t(constrain(parser.value_int(), 0L, 255L)); changed = true; }
  if (changed) towerGen::clamp(s);

  if (parser.seen('P') || parser.seen('W')) {
    const bool print = parser.seen('P');
    const bool ok = print ? towerGen::start() : towerGen::write_file();
    SERIAL_ECHOLNPGM("M8025: ", towerGen::result_text(ok));
    if (!ok) return;
  }

  switch (s.type) {
    case TOWER_TYPE_RETRACT:
      SERIAL_ECHOLNPGM("Retraction tower: ", s.start_temp, " C, ",
                       s.start, " to ", towerGen::last_band(s), " (0.1 mm) per band, ",
                       s.bands, " bands of ", s.band_mm, " mm, bed ", s.bed_temp, " C");
      break;
    case TOWER_TYPE_RING:
      SERIAL_ECHOLNPGM("Ringing tower: ", s.start_temp, " C, ",
                       s.start, " to ", towerGen::last_band(s), " Hz per band, ",
                       s.bands, " bands of ", s.band_mm, " mm, bed ", s.bed_temp, " C");
      break;
    default:
      SERIAL_ECHOLNPGM("Temperature tower: ", s.start_temp, " C, -", s.step,
                       " C per band, ", s.bands, " bands of ", s.band_mm,
                       " mm, bed ", s.bed_temp, " C");
      break;
  }
  SERIAL_ECHOLNPGM("  last band: ", towerGen::last_band(s),
                   s.type == TOWER_TYPE_TEMP ? " C, file: " : " (see above), file: ",
                   towerGen::file_name(s));
}

#endif // DWIN_TOWER_GEN
