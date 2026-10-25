/** E3V3SE: clear a stored XY probe offset.
 *
 * G28 moves the nozzle to Z_SAFE_HOMING_X/Y_POINT minus the probe's XY offset,
 * so an offset left in EEPROM parks the head beside the bed centre every time
 * the printer homes, and shifts every mesh point with it. This printer probes
 * with the nozzle itself, which has no XY offset to compensate; only the Z
 * offset is a real calibration value.
 */
#include "../inc/MarlinConfig.h"

#if ENABLED(E3V3SE_NOZZLE_AS_PROBE)

#include "ProbeXY.h"

#include "../module/probe.h"

void e3v3se_clear_probe_xy() {
  probe.offset.x = 0;
  probe.offset.y = 0;
}

#endif // E3V3SE_NOZZLE_AS_PROBE
