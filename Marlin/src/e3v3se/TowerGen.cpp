/** E3V3SE temperature tower: writes a ready-to-print G-code tower that steps the
 *  nozzle temperature once per band, so a filament can be tuned without a slicer. */
#include "../inc/MarlinConfig.h"

#if ENABLED(DWIN_TOWER_GEN)

#include "TowerGen.h"
#include "../sd/cardreader.h"
#include "../HAL/HAL.h"

#include <string.h>

namespace {

  // A 20 mm hollow square with two 0.45 mm walls. Everything is kept in
  // micrometres so the output is fixed-point and needs no float printf.
  constexpr int32_t TOWER_SIZE_UM  = 20000;   // 20.000 mm
  constexpr int32_t WALL_UM        =   450;   //  0.450 mm
  constexpr int32_t LAYER_UM       =   200;   //  0.200 mm
  constexpr int32_t ORIGIN_X_UM    = 100000;  // 100.000 mm
  constexpr int32_t ORIGIN_Y_UM    =  95000;  //  95.000 mm
  constexpr int32_t SKIRT_UM       =  3000;   // 3 mm clearance
  constexpr int32_t PRINT_FEED     =  2400;   // 40 mm/s walls
  constexpr int32_t TRAVEL_FEED    =  7200;   // 120 mm/s travel
  constexpr int32_t PRIME_LEN_UM   = 200000;  // 200 mm prime line
  constexpr int32_t TOWER_MAX_MM    = 100;    // tallest tower, keeps the file small

  // 0.45 mm wide, 0.2 mm high line out of 1.75 mm filament:
  //   0.45 * 0.2 / (pi * 1.75^2 / 4) = 0.03742 mm of filament per mm of path.
  // Scaled by 100000 so integer math stays exact to the last decimal.
  constexpr int32_t E_PER_MM_X100000 = 3742;

  int32_t extrude_um(const int32_t length_um) {
    return (length_um * E_PER_MM_X100000 + 50000) / 100000;
  }

  char* put_text(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

  char* put_uint(char *p, uint32_t value) {
    char tmp[10];
    uint8_t n = 0;
    do { tmp[n++] = char('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = tmp[--n];
    return p;
  }

  /** Fixed-point micrometres: 12345 with 3 decimals becomes "12.345". */
  char* put_mm(char *p, const int32_t um, const uint8_t decimals) {
    uint32_t scale = 1;
    for (uint8_t i = 0; i < decimals; ++i) scale *= 10;
    const uint32_t magnitude = uint32_t(um < 0 ? -um : um);
    if (um < 0) *p++ = '-';
    p = put_uint(p, magnitude / scale);
    *p++ = '.';
    const uint32_t fraction = magnitude % scale;
    for (uint32_t d = scale / 10; d >= 1; d /= 10)
      *p++ = char('0' + (fraction / d) % 10);
    return p;
  }

  char* put_edge(char *p, const char axis, const int32_t to_um, const int32_t length_um, const int32_t feed) {
    p = put_text(p, "G1 ");
    *p++ = axis;
    p = put_mm(p, to_um, 3);
    // E is micrometres of filament: three decimals give 0.001 mm resolution.
    p = put_text(p, " E");
    p = put_mm(p, extrude_um(length_um), 3);
    if (feed) {
      p = put_text(p, " F");
      p = put_uint(p, uint32_t(feed));
    }
    *p++ = '\n';
    return p;
  }

  /** Travel to the start corner and draw one square contour. */
  char* put_square(char *p, const int32_t x0, const int32_t y0, const int32_t side,
                   const bool ccw, const bool first_edge_feed) {
    p = put_text(p, "G1 X");
    p = put_mm(p, x0, 3);
    p = put_text(p, " Y");
    p = put_mm(p, y0, 3);
    p = put_text(p, " F");
    p = put_uint(p, uint32_t(TRAVEL_FEED));
    *p++ = '\n';

    // Clockwise keeps the nozzle on the inside of the inner wall.
    const int32_t x1 = x0 + side, y1 = y0 + side;
    if (ccw) {
      p = put_edge(p, 'X', x1, side, first_edge_feed ? PRINT_FEED : 0);
      p = put_edge(p, 'Y', y1, side, 0);
      p = put_edge(p, 'X', x0, side, 0);
      p = put_edge(p, 'Y', y0, side, 0);
    }
    else {
      p = put_edge(p, 'Y', y1, side, first_edge_feed ? PRINT_FEED : 0);
      p = put_edge(p, 'X', x1, side, 0);
      p = put_edge(p, 'Y', y0, side, 0);
      p = put_edge(p, 'X', x0, side, 0);
    }
    return p;
  }

  constexpr uint8_t TOWER_HEADER = 0, TOWER_SKIRT = 1, TOWER_BAND = 2,
                    TOWER_LAYER = 3, TOWER_FOOTER = 4, TOWER_DONE = 5;

  TowerSettings tower_settings = { 230, 5, 5, 60, 10 };

} // namespace

namespace towerGen {

  void defaults() {
    tower_settings.start_temp = 230;
    tower_settings.step = 5;
    tower_settings.bands = 5;
    tower_settings.bed_temp = 60;
    tower_settings.band_mm = 10;
  }

  TowerSettings &settings() { return tower_settings; }

  void clamp(TowerSettings &s) {
    s.start_temp = constrain(s.start_temp, 170, HEATER_0_MAXTEMP);
    s.step = constrain(s.step, 0, 20);
    s.bands = constrain(s.bands, 1, 10);
    s.bed_temp = constrain(s.bed_temp, 0, BED_MAXTEMP);
    s.band_mm = constrain(s.band_mm, 5, 30);
    // Never schedule a band below a printable temperature...
    while (s.bands > 1 && int16_t(s.start_temp) - int16_t(s.bands - 1) * int16_t(s.step) < 170)
      --s.bands;
    // ...and keep the file small enough to write in a few seconds.
    while (s.bands > 1 && s.bands * s.band_mm > TOWER_MAX_MM)
      --s.bands;
  }

  bool valid(const TowerSettings &s) {
    return s.start_temp >= 170 && s.start_temp <= HEATER_0_MAXTEMP
        && s.step <= 20 && s.bands >= 1 && s.bands <= 10
        && s.bed_temp <= BED_MAXTEMP
        && s.band_mm >= 5 && s.band_mm <= 30
        && s.bands * s.band_mm <= TOWER_MAX_MM
        && int16_t(s.start_temp) - int16_t(s.bands - 1) * int16_t(s.step) >= 170;
  }

  int16_t last_temp(const TowerSettings &s) {
    return int16_t(s.start_temp) - int16_t(s.bands - 1) * int16_t(s.step);
  }

  void Writer::begin(const TowerSettings &in) {
    s = in;
    stage = TOWER_HEADER;
    band = 0; layer = 0; contour = 0;
    layers = uint8_t(s.band_mm * (1000 / LAYER_UM));      // 0.2 mm layers per band
    z_um = LAYER_UM;
  }

  bool Writer::next(char *out, const uint16_t size) {
    if (!out || size < TOWER_BLOCK_SIZE || stage == TOWER_DONE) return false;

    char *p = out;
    switch (stage) {

      case TOWER_HEADER:
        p = put_text(p, "; E3V3SE temperature tower\n; ");
        p = put_uint(p, s.start_temp);
        p = put_text(p, " C down to ");
        p = put_uint(p, uint16_t(last_temp(s)));
        p = put_text(p, " C in ");
        p = put_uint(p, s.bands);
        p = put_text(p, " bands of ");
        p = put_uint(p, s.band_mm);
        p = put_text(p, " mm\n");
        p = put_text(p, "G21\nG90\nM140 S");
        p = put_uint(p, s.bed_temp);
        p = put_text(p, "\nM104 S");
        p = put_uint(p, s.start_temp);
        p = put_text(p, "\nM190 S");
        p = put_uint(p, s.bed_temp);
        p = put_text(p, "\nM109 S");
        p = put_uint(p, s.start_temp);
        p = put_text(p, "\nM83\nG28\nM420 S1\nG92 E0\n");
        // Prime along the front edge, then step sideways and back.
        p = put_text(p, "G1 Z0.200 F600\nG1 X10.000 Y10.000 F");
        p = put_uint(p, uint32_t(TRAVEL_FEED));
        p = put_text(p, "\n");
        p = put_edge(p, 'Y', 210000, PRIME_LEN_UM, 1200);
        p = put_edge(p, 'X', 10600, 600, 0);
        p = put_edge(p, 'Y', 10000, PRIME_LEN_UM, 0);
        p = put_text(p, "M106 S255\nG1 Z0.200 F600\n");
        stage = TOWER_SKIRT;
        break;

      case TOWER_SKIRT: {
        // Two loops 3 mm outside the tower, on the first layer.
        const int32_t half = TOWER_SIZE_UM / 2 + SKIRT_UM;
        const int32_t cx = ORIGIN_X_UM + TOWER_SIZE_UM / 2;
        const int32_t cy = ORIGIN_Y_UM + TOWER_SIZE_UM / 2;
        const int32_t inset = int32_t(contour) * WALL_UM;
        const int32_t x0 = cx - half + inset;
        const int32_t y0 = cy - half + inset;
        p = put_square(p, x0, y0, 2 * (half - inset), true, contour == 0);
        if (++contour > 1) { contour = 0; stage = TOWER_BAND; }
        break;
      }

      case TOWER_BAND: {
        const int16_t temp = int16_t(s.start_temp) - int16_t(band) * int16_t(s.step);
        p = put_text(p, "; band ");
        p = put_uint(p, uint16_t(band + 1));
        p = put_text(p, "/");
        p = put_uint(p, s.bands);
        p = put_text(p, " at ");
        p = put_uint(p, uint16_t(temp));
        p = put_text(p, " C\nM104 S");
        p = put_uint(p, uint16_t(temp));
        p = put_text(p, "\nM109 S");
        p = put_uint(p, uint16_t(temp));
        p = put_text(p, "\n");
        layer = 0;
        contour = 0;
        stage = TOWER_LAYER;
        break;
      }

      case TOWER_LAYER: {
        if (contour == 0) {
          p = put_text(p, "G1 Z");
          p = put_mm(p, z_um, 3);
          p = put_text(p, " F600\n");
        }
        const int32_t inset = int32_t(contour) * WALL_UM;
        // The inner contour runs the other way so the nozzle stays inside the wall.
        p = put_square(p, ORIGIN_X_UM + inset, ORIGIN_Y_UM + inset,
                       TOWER_SIZE_UM - 2 * inset, contour == 0, true);
        if (++contour > 1) {
          contour = 0;
          z_um += LAYER_UM;
          if (++layer >= layers) {
            layer = 0;
            if (++band >= s.bands) stage = TOWER_FOOTER;
            else stage = TOWER_BAND;
          }
        }
        break;
      }

      case TOWER_FOOTER:
        p = put_text(p, "M104 S0\nM140 S0\nM106 S0\nG91\nG1 Z10.000 F600\nG90\n");
        p = put_text(p, "G1 X0.000 Y210.000 F3000\nM84\n");
        stage = TOWER_DONE;
        break;

      default:
        return false;
    }

    *p = '\0';
    return true;
  }

  bool write_file() {
    if (!card.isMounted() || card.isPrinting()) return false;
    TowerSettings &s = settings();
    clamp(s);
    if (!valid(s)) return false;

    card.removeFile(TOWER_FILE);
    card.openFileWrite(TOWER_FILE);
    if (!card.isFileOpen()) { card.closefile(); return false; }

    char block[TOWER_BLOCK_SIZE];
    Writer w;
    w.begin(s);
    bool ok = true;
    #if ENABLED(USE_WATCHDOG)
      uint16_t count = 0;
    #endif
    while (w.next(block, sizeof(block))) {
      const uint16_t len = uint16_t(strlen(block));
      if (len && card.write((void*)block, len) != int16_t(len)) { ok = false; break; }
      // Writing a few hundred blocks blocks the main loop; keep the watchdog fed.
      #if ENABLED(USE_WATCHDOG)
        if ((++count & 0x1F) == 0) hal.watchdog_refresh();
      #endif
    }
    card.closefile();
    return ok;
  }

  bool start() {
    if (!write_file()) return false;
    card.openAndPrintFile(TOWER_FILE);    // queues M23 + M24
    return true;
  }

  const char *result_text(const bool ok) {
    return ok ? "tower written to " TOWER_FILE : "could not write the tower";
  }

} // namespace towerGen

#endif // DWIN_TOWER_GEN
