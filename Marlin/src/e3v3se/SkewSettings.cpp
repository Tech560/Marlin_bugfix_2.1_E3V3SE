/** E3V3SE LCD-editable XY skew measurements (declared in Configuration.h). */
#include "../inc/MarlinConfig.h"

#if ENABLED(DWIN_SKEW_MENU)

// Defaults come from the measured diagonals in Configuration.h and are
// overwritten by the values stored in EEPROM (see settings.cpp).
float xyskew_d_ac = XY_DIAG_AC,
      xyskew_d_bd = XY_DIAG_BD,
      xyskew_s_ad = XY_SIDE_AD;

#endif
