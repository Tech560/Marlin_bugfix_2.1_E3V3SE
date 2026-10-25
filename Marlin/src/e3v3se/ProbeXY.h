/** E3V3SE: the nozzle is the Z probe, so a stored XY probe offset only moves
 *  Auto Home and the mesh points away from where they belong. */
#pragma once

#include "../inc/MarlinConfigPre.h"

#if ENABLED(E3V3SE_NOZZLE_AS_PROBE)

  /**
   * Zero the probe's X and Y offset, keeping the Z offset that calibrates the
   * first layer. Called once after the settings are loaded, so a value left in
   * EEPROM (or restored from a backup) cannot shift homing again.
   */
  void e3v3se_clear_probe_xy();

#endif
