/** E3V3SE bed mesh snapshots: quantisation, deltas and CSV lines. */
#pragma once

#include "../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_MESH_HISTORY)

  #define MESH_HISTORY_POINTS (GRID_MAX_POINTS_X * GRID_MAX_POINTS_Y)
  #define MESH_HISTORY_SLOTS  4
  // "MH1;" + prints + one ";<value>" per point + ";<crc4>" + "\n"
  #define MESH_HISTORY_LINE_LEN (4 + 11 + MESH_HISTORY_POINTS * 7 + 5 + 2)
  #define MESH_HISTORY_MAX_DRIFT 999  // (µm) display ceiling for the delta screen

  struct MeshSnapshot {
    uint32_t prints;
    int16_t z[MESH_HISTORY_POINTS];   // micrometres, row-major like bedlevel.z_values
  };

  /** mm -> µm, clamped to the int16 range. */
  int16_t mesh_quantize(const float mm);
  /** µm -> mm. */
  float mesh_dequantize(const int16_t um);
  /** CRC-16/CCITT over len bytes, used to reject torn or edited log lines. */
  uint16_t mesh_crc16(const char *data, const uint16_t len);
  /** now - before in µm, clamped to int8 so the LCD can print it in four columns. */
  void mesh_delta(const int16_t *now, const int16_t *before, int8_t *out, const uint8_t count);
  /** One LCD row: "-12  +5   0 ...", count values, always NUL terminated. */
  void mesh_format_row(const int8_t *delta, const uint8_t count, char *out, const size_t size);
  /** Serialises a snapshot as "MH1;<prints>;<z0>;...;<crc><\n>". Returns the length. */
  uint16_t mesh_format_line(const MeshSnapshot &in, char *out, const size_t size);
  /** Parses one line produced by mesh_format_line; false on prefix, count or CRC mismatch. */
  bool mesh_parse_line(const char *line, MeshSnapshot &out);

#endif // DWIN_MESH_HISTORY
