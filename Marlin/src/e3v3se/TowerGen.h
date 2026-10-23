/** E3V3SE calibration towers: writes a ready-to-print G-code tower that changes
 *  one variable per band, so a filament or shaper can be tuned without a slicer.
 *
 *  Types:
 *    TEMP     - nozzle temperature drops by `step` °C per band.
 *    RETRACT  - retraction length grows by `step` tenths of a mm per band.
 *    RING     - shaper frequency (M493) grows by `step` Hz per band.
 */
#pragma once

#include "../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_TOWER_GEN)

  #define TOWER_FILE_TEMP    "TEMPTWR.GCO"  // 8.3 names in the card root
  #define TOWER_FILE_RETRACT "RETRTWR.GCO"
  #define TOWER_FILE_RING    "RINGTWR.GCO"

  #define TOWER_BLOCK_SIZE 384              // largest block Writer::next() emits

  enum TowerType : uint8_t {
    TOWER_TYPE_TEMP,                      // nozzle temperature per band
    TOWER_TYPE_RETRACT,                   // retraction length per band
    TOWER_TYPE_RING                       // shaper frequency per band
  };

  struct TowerSettings {
    uint8_t  type;        // (TowerType)
    uint16_t start_temp;  // (°C) first band, or the fixed print temperature
    uint8_t  start;       // (0.1 mm) first retraction length / (Hz) first frequency
    uint8_t  step;        // (°C) drop / (0.1 mm) growth / (Hz) growth per band
    uint8_t  bands;       // number of bands
    uint8_t  bed_temp;    // (°C)
    uint8_t  band_mm;     // (mm) height of one band
  };

  namespace towerGen {
    /** Factory values: temperature tower, 230 °C, -5 °C per band, 5 bands of 10 mm, 60 °C bed. */
    void defaults();
    /** Factory values for one tower type; keeps the common print settings out of it. */
    void type_defaults(const uint8_t type);
    TowerSettings &settings();
    /** Pull the settings back into a printable range. */
    void clamp(TowerSettings &s);
    /** True when the settings describe a tower that can be printed safely. */
    bool valid(const TowerSettings &s);
    /** Temperature of the last band (temperature towers only). */
    int16_t last_temp(const TowerSettings &s);
    /** Value of one band: °C, tenths of a mm, or Hz, depending on the type. */
    int16_t band_value(const TowerSettings &s, const uint8_t band);
    /** Value of the last band, in the same unit as band_value(). */
    int16_t last_band(const TowerSettings &s);
    /** 8.3 file name for the tower type. */
    const char *file_name(const TowerSettings &s);
    /** Short type name for the LCD and the serial log. */
    const char *type_name(const uint8_t type);

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

    /** Write the tower for the current type to the card. Refused while printing or without a card. */
    bool write_file();
    /** write_file() and start the job. */
    bool start();
    /** One line for the serial log and the LCD. */
    const char *result_text(const bool ok);
  }

#endif // DWIN_TOWER_GEN
