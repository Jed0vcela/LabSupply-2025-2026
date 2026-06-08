#include "psu_control.h"
#include "peripherals.h"
#include "flash_store.h"
#include "display.h"
#include "mcp23s17.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

/* ── DAC state ────────────────────────────────────────────────────────── */
int32_t g_dac_cnt[DAC_NUM_CH]  = {0, 0, 0, 0};
bool    g_vi_enabled            = false;
bool    g_isink_enabled         = false;
bool    g_pout_limiting         = false;
bool    g_psink_limiting        = false;

/* ── Channel select ───────────────────────────────────────────────────── */
uint8_t       g_active_ch                  = DAC_VIRT_VSET;
const uint8_t gpb_sel_mask[DAC_NUM_VIRT]   = {(1u<<0),(1u<<2),(1u<<4)};
const uint8_t gpb_sel_vch[DAC_NUM_VIRT]    = {DAC_VIRT_VSET,DAC_VIRT_ISET,DAC_VIRT_ISINK};

/* ── Helpers ──────────────────────────────────────────────────────────── */
static inline uint16_t cnt_to_raw(int32_t cnt)
{
    if (cnt <= 0)           return 0u;
    if (cnt >= DAC_MAX_CNT) return (uint16_t)DAC_MAX_COUNT;
    return (uint16_t)cnt;
}

/* ── Hardware GPIO init ───────────────────────────────────────────────── */
void hw_gpio_init(void)
{
    gpio_init(PIN_SW_HI); gpio_set_dir(PIN_SW_HI, GPIO_OUT); gpio_put(PIN_SW_HI, 1);
    gpio_init(PIN_SW_LO); gpio_set_dir(PIN_SW_LO, GPIO_OUT); gpio_put(PIN_SW_LO, 1);
    gpio_init(PIN_EN_HI); gpio_set_dir(PIN_EN_HI, GPIO_OUT); gpio_put(PIN_EN_HI, 1);
    gpio_init(PIN_EN_LO); gpio_set_dir(PIN_EN_LO, GPIO_OUT); gpio_put(PIN_EN_LO, 1);
}

/* ── DAC update ───────────────────────────────────────────────────────── */
void dac_update(mcp4728 &dac)
{
    bool isink_on = g_isink_enabled && (g_dac_cnt[DAC_CH_A] != 0);
    bool c_on     = g_vi_enabled    && (g_dac_cnt[DAC_CH_C] != 0);
    bool d_on     = g_vi_enabled    && (g_dac_cnt[DAC_CH_D] != 0);

    dac.setPowerDown(isink_on?0:1, isink_on?0:1, c_on?0:1, d_on?0:1);
    dac.analogWriteAll(
        isink_on ? cnt_to_raw(g_dac_cnt[DAC_CH_A]) : 0,
        isink_on ? cnt_to_raw(g_dac_cnt[DAC_CH_A]) : 0,
        c_on     ? cnt_to_raw(g_dac_cnt[DAC_CH_C]) : 0,
        d_on     ? cnt_to_raw(g_dac_cnt[DAC_CH_D]) : 0
    );
    dac.ldacPulse();
    gpio_put(PIN_EN_HI, isink_on ? 0 : 1);
    gpio_put(PIN_EN_LO, isink_on ? 0 : 1);
    mcp23s17_set_pin(MCP_OUT_GPA7, g_vi_enabled);
    mcp23s17_set_pin(MCP_OUT_GPA5, g_isink_enabled);
}

/* ── Power limiting ───────────────────────────────────────────────────── */
void power_limit_update(int32_t vout_mV, int32_t iout_mA, int32_t hii_mA)
{
    /* Power limiting disabled in settings → clear flags and return */
    if (!g_cal.plim_enabled) {
        g_pout_limiting  = false;
        g_psink_limiting = false;
        return;
    }

    int32_t pout_mW   = (vout_mV * iout_mA) / 1000;   /* raw Iout */
    int32_t psink_mW  = (vout_mV * hii_mA)  / 1000;

    /* Vin minimum: if input voltage is too low, ramp down both outputs */
    bool vin_low = (g_cal.vin_min_mV > 0);   /* vin not available here, checked in main */
    (void)vin_low;   /* vin_min applied in main loop via vin_mV */

    bool was_pout  = g_pout_limiting;
    bool was_psink = g_psink_limiting;

    /* Pout limit */
    if (g_cal.pout_max_mW > 0 && pout_mW > g_cal.pout_max_mW
        && g_vi_enabled && g_dac_cnt[DAC_CH_C] > 0) {
        g_dac_cnt[DAC_CH_C]--;
        g_pout_limiting = true;
    } else {
        g_pout_limiting = (g_cal.pout_max_mW > 0 &&
                           pout_mW > (g_cal.pout_max_mW - g_cal.pout_max_mW/10));
    }

    /* Psink limit */
    if (g_cal.psink_max_mW > 0 && psink_mW > g_cal.psink_max_mW
        && g_isink_enabled && g_dac_cnt[DAC_CH_A] > 0) {
        g_dac_cnt[DAC_CH_A]--;
        g_dac_cnt[DAC_CH_B] = g_dac_cnt[DAC_CH_A];
        g_psink_limiting = true;
    } else {
        g_psink_limiting = (g_cal.psink_max_mW > 0 &&
                            psink_mW > (g_cal.psink_max_mW - g_cal.psink_max_mW/10));
    }

    if (!was_pout  && g_pout_limiting)  { beep_special(); msg_post(MSG_WARN, "WARN: Pout limit active",  0u); }
    if (!was_psink && g_psink_limiting) { beep_special(); msg_post(MSG_WARN, "WARN: Psink limit active", 0u); }
    /* Clear warn when both limiters inactive */
    if (!g_pout_limiting && !g_psink_limiting && (was_pout || was_psink))
        msg_clear();
}

/* ── Encoder step multiplier ──────────────────────────────────────────
 * Two modes controlled by g_cal.step_mode:
 *
 * MOMENTARY (step_mode = 0):
 *   GPB3 held → ×10, GPB1 held → ×100, both held → ×1000
 *   Reads GPB level each call (no state).
 *
 * TOGGLE (step_mode = 1):
 *   GPB3 falling edge cycles ×1 → ×10 → ×100 → ×1
 *   GPB1 falling edge resets to ×1
 *   g_step_mult holds persistent state.
 */
static int32_t g_step_mult    = 1;
static bool    g_buttons_primed = false;
static bool    g_step_primed    = false;

static void step_mult_update(uint8_t gpb)
{
    if (g_cal.step_mode == 0) {
        /* Momentary: level-based, no state needed */
        int32_t m = 1;
        if ((gpb & (1u<<3)) == 0) m *= 10;
        if ((gpb & (1u<<1)) == 0) m *= 100;
        g_step_mult = m;
        return;
    }

    /* Toggle: falling-edge based */
    static uint8_t gpb_prev = 0xFFu;
    if (!g_step_primed) { gpb_prev = gpb; g_step_primed = true; return; }

    uint8_t fell = (uint8_t)(gpb_prev & ~gpb);
    if (fell & (1u<<3)) {   /* GPB3: cycle 1→10→100→1 */
        if      (g_step_mult == 1)   g_step_mult = 10;
        else if (g_step_mult == 10)  g_step_mult = 100;
        else                         g_step_mult = 1;
    }
    if (fell & (1u<<1))     /* GPB1: reset to ×1 */
        g_step_mult = 1;
    gpb_prev = gpb;
}

int32_t encoder_step_multiplier(uint8_t gpb)
{
    step_mult_update(gpb);
    return g_step_mult;
}

void apply_encoder_main(int32_t multiplier)
{
    int32_t delta = enc_consume();
    if (delta == 0) return;
    int32_t step = (int32_t)ENC_STEP * multiplier;
    if (g_active_ch == DAC_VIRT_ISINK) {
        int32_t cnt = g_dac_cnt[DAC_CH_A] + delta * step;
        if (cnt < DAC_MIN_CNT) cnt = DAC_MIN_CNT;
        if (cnt > DAC_MAX_CNT) cnt = DAC_MAX_CNT;
        g_dac_cnt[DAC_CH_A] = g_dac_cnt[DAC_CH_B] = cnt;
    } else {
        int32_t cnt = g_dac_cnt[g_active_ch] + delta * step;
        if (cnt < DAC_MIN_CNT) cnt = DAC_MIN_CNT;
        if (cnt > DAC_MAX_CNT) cnt = DAC_MAX_CNT;
        g_dac_cnt[g_active_ch] = cnt;
    }
}

/* ── Button handling ──────────────────────────────────────────────────── */

void process_buttons_main_reset(void)
{
    g_buttons_primed = false;
    g_step_primed    = false;
}

/*
 * Returns true  → enter cal menu (GPB5 fell)
 * gpb7_fell_out → caller uses to cycle psu_mode
 */
bool process_buttons_main(uint8_t gpa, uint8_t gpb, bool *gpb7_fell_out)
{
    static uint8_t gpa_prev = 0xFFu;
    static uint8_t gpb_prev = 0xFFu;
    if (gpb7_fell_out) *gpb7_fell_out = false;

    if (!g_buttons_primed) {
        gpa_prev = gpa; gpb_prev = gpb;
        g_buttons_primed = true;
        return false;
    }

    uint8_t gpa_fell = (uint8_t)(gpa_prev & ~gpa);
    uint8_t gpb_fell = (uint8_t)(gpb_prev & ~gpb);

    if (gpa_fell || gpb_fell) beep();

    if (gpa_fell & (1u<<0)) g_vi_enabled    = !g_vi_enabled;
    if (gpb_fell & (1u<<6)) g_isink_enabled = !g_isink_enabled;

    for (uint8_t i = 0; i < DAC_NUM_VIRT; i++)
        if (gpb_fell & gpb_sel_mask[i]) g_active_ch = gpb_sel_vch[i];

    if (gpb7_fell_out && (gpb_fell & (1u<<7))) *gpb7_fell_out = true;

    gpa_prev = gpa; gpb_prev = gpb;
    return (bool)(gpb_fell & (1u<<5));
}