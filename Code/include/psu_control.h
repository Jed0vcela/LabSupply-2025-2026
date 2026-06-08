#pragma once
#include "psu_types.h"
#include "mcp4728.h"
#include <stdint.h>
#include <stdbool.h>

#define PIN_SW_HI    13
#define PIN_SW_LO    12
#define PIN_EN_HI    20
#define PIN_EN_LO    21

/* ── Virtual channel select tables ───────────────────────────────────── */
extern uint8_t       g_active_ch;
extern const uint8_t gpb_sel_mask[DAC_NUM_VIRT];
extern const uint8_t gpb_sel_vch[DAC_NUM_VIRT];

/* ── DAC setpoints ────────────────────────────────────────────────────── */
extern int32_t g_dac_cnt[DAC_NUM_CH];   /* main supply + sink setpoints   */

/* ── Output enables ───────────────────────────────────────────────────── */
extern bool g_vi_enabled;
extern bool g_isink_enabled;

/* ── Power limiting state ─────────────────────────────────────────────── */
extern bool g_pout_limiting;
extern bool g_psink_limiting;

/* ── Init & periodic ─────────────────────────────────────────────────── */
void hw_gpio_init(void);
void dac_update(mcp4728 &dac);
void power_limit_update(int32_t vout_mV, int32_t iout_mA, int32_t hii_mA);
void apply_encoder_main(int32_t multiplier);

/* Returns true if cal menu requested (GPB5), puts mode-cycle in gpb7_fell */
bool process_buttons_main(uint8_t gpa, uint8_t gpb, bool *gpb7_fell_out);
void process_buttons_main_reset(void);

int32_t encoder_step_multiplier(uint8_t gpb);

/* Utility: convert DAC counts to millivolts */
static inline int32_t cnt_to_mv(int32_t cnt)
{
    return (int32_t)((int64_t)cnt * DAC_VREF_MV / DAC_MAX_COUNT);
}

/* Compute setpoint display value via calibration */
static inline int32_t setpt_disp(int32_t cnt, int32_t gain, int32_t div, int32_t off)
{
    return cnt_to_mv(cnt) * gain / div - off;
}