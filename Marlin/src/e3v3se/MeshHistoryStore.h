/** E3V3SE bed mesh snapshot store: keeps the last few meshes on the SD card. */
#pragma once

#include "../inc/MarlinConfigPre.h"

#if ENABLED(DWIN_MESH_HISTORY)

  #include "MeshHistory.h"

  #define MESH_HISTORY_FILE "mesh_hist.csv"

  namespace meshHistory {
    /** Capture bedlevel.z_values and append it to the log. Refused while printing. */
    bool save_snapshot();
    /** Number of stored snapshots in the log. */
    uint8_t stored();
    /** Average and peak drift (µm) between the newest and the previous snapshot. */
    bool drift(int16_t &max_um, int16_t &avg_um);
    /** Newest minus previous snapshot per point, clamped for display. */
    bool last_deltas(int8_t *out);
    /** Print count recorded with the newest snapshot. */
    uint32_t last_prints();
    /** Remove the log from the SD card. */
    bool clear();
  }

#endif // DWIN_MESH_HISTORY
