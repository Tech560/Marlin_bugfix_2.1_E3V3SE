/** E3V3SE bed mesh snapshot store: keeps the last few meshes on the SD card. */
#include "../inc/MarlinConfig.h"

#if ENABLED(DWIN_MESH_HISTORY)

#include "MeshHistoryStore.h"

#include "../sd/cardreader.h"
#include "../module/printcounter.h"
#include "../feature/bedlevel/bedlevel.h"

#include <string.h>

namespace {

  // Four snapshots of ~274 bytes each. Static so the menu never puts this on
  // the stack, and small enough to leave the RAM budget alone.
  char log_buf[MESH_HISTORY_SLOTS * MESH_HISTORY_LINE_LEN + 1];

  /** Reads the log into log_buf and returns the number of complete lines. */
  uint8_t load_log() {
    log_buf[0] = '\0';
    if (!card.isMounted() || !card.fileExists(MESH_HISTORY_FILE)) return 0;

    card.openFileRead(MESH_HISTORY_FILE);
    const int16_t n = card.isFileOpen() ? card.read(log_buf, sizeof(log_buf) - 1) : -1;
    card.closefile();
    if (n <= 0) { log_buf[0] = '\0'; return 0; }
    log_buf[n] = '\0';

    uint8_t lines = 0;
    for (char *p = log_buf; *p; ++lines) {
      char * const nl = strchr(p, '\n');
      if (!nl) break;
      p = nl + 1;
    }
    return lines;
  }

  const char* line_at(const uint8_t index) {
    const char *p = log_buf;
    for (uint8_t i = 0; i < index && p; ++i) {
      p = strchr(p, '\n');
      if (p) ++p;
    }
    return p;
  }

  /** Parses the snapshot at index (0 = oldest); false when the line is invalid. */
  bool snapshot_at(const uint8_t index, MeshSnapshot &out) {
    const char * const line = line_at(index);
    return line && mesh_parse_line(line, out);
  }

} // namespace

bool meshHistory::save_snapshot() {
  if (!card.isMounted() || card.isPrinting()) return false;

  MeshSnapshot snap;
  snap.prints = uint32_t(print_job_timer.getStats().totalPrints);
  LOOP_L_N(x, GRID_MAX_POINTS_X)
    LOOP_L_N(y, GRID_MAX_POINTS_Y)
      snap.z[x * GRID_MAX_POINTS_Y + y] = mesh_quantize(bedlevel.z_values[x][y]);

  char line[MESH_HISTORY_LINE_LEN];
  if (!mesh_format_line(snap, line, sizeof(line))) return false;

  const uint8_t lines = load_log();

  // Keep room for the new snapshot by dropping the oldest line(s).
  const uint8_t keep = lines >= MESH_HISTORY_SLOTS ? MESH_HISTORY_SLOTS - 1 : lines;
  if (keep) {
    const char * const first = line_at(lines - keep);
    const size_t len = strlen(first);
    if (len) memmove(log_buf, first, len);
    log_buf[len] = '\0';
  }
  else log_buf[0] = '\0';

  const size_t used = strlen(log_buf), add = strlen(line);
  if (used + add >= sizeof(log_buf)) return false;
  memcpy(log_buf + used, line, add + 1);

  card.openFileWrite(MESH_HISTORY_FILE);
  if (!card.isFileOpen()) { card.closefile(); return false; }
  const int16_t written = card.write(log_buf, uint16_t(used + add));
  card.closefile();
  return written == int16_t(used + add);
}

uint8_t meshHistory::stored() { return load_log(); }

uint32_t meshHistory::last_prints() {
  const uint8_t lines = load_log();
  if (!lines) return 0;
  MeshSnapshot snap;
  return snapshot_at(lines - 1, snap) ? snap.prints : 0;
}

bool meshHistory::last_deltas(int8_t *out) {
  const uint8_t lines = load_log();
  if (lines < 2) return false;
  MeshSnapshot now, before;
  if (!snapshot_at(lines - 1, now) || !snapshot_at(lines - 2, before)) return false;
  mesh_delta(now.z, before.z, out, MESH_HISTORY_POINTS);
  return true;
}

bool meshHistory::drift(int16_t &max_um, int16_t &avg_um) {
  const uint8_t lines = load_log();
  if (lines < 2) return false;
  MeshSnapshot now, before;
  if (!snapshot_at(lines - 1, now) || !snapshot_at(lines - 2, before)) return false;

  int32_t peak = 0, total = 0;
  for (uint8_t i = 0; i < MESH_HISTORY_POINTS; ++i) {
    const int32_t d = int32_t(now.z[i]) - int32_t(before.z[i]);
    const int32_t mag = d < 0 ? -d : d;
    if (mag > peak) peak = mag;
    total += mag;
  }
  max_um = int16_t(peak);
  avg_um = int16_t(total / MESH_HISTORY_POINTS);
  return true;
}

bool meshHistory::clear() {
  if (!card.isMounted() || card.isPrinting() || !card.fileExists(MESH_HISTORY_FILE)) return false;
  card.removeFile(MESH_HISTORY_FILE);
  log_buf[0] = '\0';
  return true;
}

#endif // DWIN_MESH_HISTORY
