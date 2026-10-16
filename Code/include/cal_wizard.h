#pragma once
#include "mcp4728.h"

/* Guided calibration (Vout+Vset, Iout+Iset, optional Vin).
 *
 * Blocking, like cal_menu_run(). Nothing is written to flash until the final
 * "Save?" confirmation; g_cal is only modified immediately before flash_save().
 * On exit (saved or not) g_dac_cnt[] is restored and all outputs are off.
 *
 * Returns true if the new calibration was saved to flash. */
bool cal_wizard_run(mcp4728 &dac);
