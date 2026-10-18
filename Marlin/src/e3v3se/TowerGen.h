/** E3V3SE temperature tower: writes a ready-to-print G-code tower that steps the
 *  nozzle temperature once per band, so a filament can be tuned without a slicer. */
#pragma once

#include "../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_TOWER_GEN)

  #define TOWER_FILE       "TEMPTWR.GCO"  // 8.3 name in the card root
  #define TOWER_BLOCK_SIZE 384            // largest block Writer::next() emits

  struct TowerSettings {
    uint16_t start_temp;  // (°C) first band
    uint8_t  step;        // (°C) drop per band
    uint8_t  bands;       // number of bands
    uint8_t  bed_temp;    // (°C)
    uint8_t  band_mm;     // (mm) height of one band
  };

  namespace towerGen {
    /** Factory values: 230 °C, -5 °C per band, 5 bands of 10 mm, 60 °C bed. */
    void defaults();
    TowerSettings &settings();
    /** Pull the settings back into a printable range. */
    void clamp(TowerSettings &s);
    /** True when the settings describe a tower that can be printed safely. */
    bool valid(const TowerSettings &s);
    /** Temperature of the last band. */
    int16_t last_temp(const TowerSettings &s);

    /** Emits the G-code block by block. next() needs a TOWER_BLOCK_SIZE buffer. */
    class Writer {
      public:
        void begin(const TowerSettings &s);
        bool next(char *out, const uint16_t size);
      private:
        TowerSettings s;
        uint8_t stage, band, layer, contour, layers;
        int32_t z_um;
    };

    /** Write TOWER_FILE to the card. Refused while printing or without a card. */
    bool write_file();
    /** write_file() and start the job. */
    bool start();
    /** One line for the serial log and the LCD. */
    const char *result_text(const bool ok);
  }

#endif // DWIN_TOWER_GEN
