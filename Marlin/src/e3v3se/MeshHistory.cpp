/** E3V3SE bed mesh snapshots: quantisation, deltas and CSV lines. */
#include "../inc/MarlinConfig.h"

#if ENABLED(DWIN_MESH_HISTORY)

#include "MeshHistory.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

static char* put_uint(char *p, uint32_t v) {
  char tmp[11];
  uint8_t n = 0;
  do { tmp[n++] = char('0' + v % 10); v /= 10; } while (v);
  while (n) *p++ = tmp[--n];
  return p;
}

static char* put_int(char *p, const int32_t v) {
  if (v < 0) { *p++ = '-'; return put_uint(p, uint32_t(-v)); }
  return put_uint(p, uint32_t(v));
}

int16_t mesh_quantize(const float mm) {
  const float um = mm * 1000.0f;
  if (um >= 32767.0f) return 32767;
  if (um <= -32768.0f) return -32768;
  return int16_t(lroundf(um));
}

float mesh_dequantize(const int16_t um) { return float(um) * 0.001f; }

uint16_t mesh_crc16(const char *data, const uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; ++i) {
    crc ^= uint16_t(uint8_t(data[i])) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}

void mesh_delta(const int16_t *now, const int16_t *before, int8_t *out, const uint8_t count) {
  for (uint8_t i = 0; i < count; ++i) {
    const int32_t d = int32_t(now[i]) - int32_t(before[i]);
    out[i] = d > 127 ? 127 : (d < -127 ? -127 : int8_t(d));
  }
}

void mesh_format_row(const int8_t *delta, const uint8_t count, char *out, const size_t size) {
  if (!size) return;
  char *p = out;
  const char * const end = out + size - 1;   // keep room for the NUL
  for (uint8_t i = 0; i < count; ++i) {
    if (i) *p++ = ' ';
    const int16_t v = delta[i];
    const uint8_t mag = uint8_t(v < 0 ? -v : v);
    *p++ = v < 0 ? '-' : (v ? '+' : ' ');
    if (mag < 100) *p++ = ' ';
    if (mag < 10) *p++ = ' ';
    if (mag >= 100) *p++ = char('0' + mag / 100);
    if (mag >= 10) *p++ = char('0' + (mag / 10) % 10);
    *p++ = char('0' + mag % 10);
    if (p >= end) break;
  }
  *p = '\0';
}

uint16_t mesh_format_line(const MeshSnapshot &in, char *out, const size_t size) {
  if (!out || size < MESH_HISTORY_LINE_LEN) return 0;

  char *p = out;
  memcpy(p, "MH1;", 4); p += 4;
  p = put_uint(p, in.prints);
  for (uint8_t i = 0; i < MESH_HISTORY_POINTS; ++i) {
    *p++ = ';';
    p = put_int(p, in.z[i]);
  }

  const uint16_t crc = mesh_crc16(out, uint16_t(p - out));
  static const char hex[] = "0123456789ABCDEF";
  *p++ = ';';
  *p++ = hex[(crc >> 12) & 0x0F];
  *p++ = hex[(crc >> 8) & 0x0F];
  *p++ = hex[(crc >> 4) & 0x0F];
  *p++ = hex[crc & 0x0F];
  *p++ = '\n';
  *p = '\0';
  return uint16_t(p - out);
}

bool mesh_parse_line(const char *line, MeshSnapshot &out) {
  if (!line || strncmp(line, "MH1;", 4)) return false;

  const char *p = line + 4;
  char *end = nullptr;

  const unsigned long prints = strtoul(p, &end, 10);
  if (end == p) return false;
  p = end;

  for (uint8_t i = 0; i < MESH_HISTORY_POINTS; ++i) {
    if (*p != ';') return false;
    ++p;
    const long v = strtol(p, &end, 10);
    if (end == p || v < -32768L || v > 32767L) return false;
    out.z[i] = int16_t(v);
    p = end;
  }

  if (*p != ';') return false;                       // checksum separator
  const uint16_t want = uint16_t(strtoul(p + 1, &end, 16));
  if (end == p + 1) return false;                    // no checksum digits
  if (mesh_crc16(line, uint16_t(p - line)) != want) return false;

  out.prints = uint32_t(prints);
  return true;
}

#endif // DWIN_MESH_HISTORY
