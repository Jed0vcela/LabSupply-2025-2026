#include "cal_wizard.h"
#include "cal_fit.h"
#include "psu_types.h"
#include "flash_store.h"
#include "psu_control.h"
#include "peripherals.h"
#include "display.h"
#include "ADC_measurement.h"
#include "mcp23s17.h"
#include "st7735.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"

/* ── Tunables ─────────────────────────────────────────────────────────── */
/* Calibrated Vin is in units of 10 mV (like g_cal.vin_min_mV and the main display) */
#define WIZ_VIN_UNIT_MV      10
#define WIZ_VIN_MIN          1200    /* 12 V, lower limit of the supply input  */
#define WIZ_VIN_DROP_PCT     10      /* abort step if Vin drops by more than   */
#define WIZ_VIN_BAD_TICKS    3       /* consecutive bad samples before abort   */
#define WIZ_VSET_I_MA        20      /* Iset during the voltage part (gentle inrush) */
#define WIZ_RAMP_MS          1500    /* V3: Vset ramps up over this time       */
#define WIZ_ISET_V_MV        3000    /* Vset during the current part           */
#define WIZ_AVG_SAMPLES      256
#define WIZ_I4_SETTLE_MS     2000    /* I4: wait after output on               */
#define WIZ_I4_AVG_END_MS    3000    /* I4: averaging window ends              */
#define WIZ_I4_OFF_MS        5000    /* I4: output off no later than this      */
#define WIZ_MAX_PTS          4

/* ── Screen: 26 x 14 characters at scale 1, with a per-row cache ──────── */
#define W_COLS  26
#define W_ROWS  14
#define W_FOOT  (W_ROWS - 1)

static char     s_shown[W_ROWS][W_COLS + 1];
static uint16_t s_color[W_ROWS];

static void scr_clear(void)
{
    st7735_fill_screen(BG_COL);
    for (uint8_t r = 0; r < W_ROWS; r++) {
        memset(s_shown[r], ' ', W_COLS);
        s_shown[r][W_COLS] = '\0';
        s_color[r] = FG_COL;
    }
}

static void put(uint8_t row, uint16_t col, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void put(uint8_t row, uint16_t col, const char *fmt, ...)
{
    if (row >= W_ROWS) return;
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    size_t n = strlen(tmp);
    if (n > W_COLS) n = W_COLS;
    memset(tmp + n, ' ', W_COLS - n);
    tmp[W_COLS] = '\0';
    if (col == s_color[row] && strcmp(tmp, s_shown[row]) == 0) return;
    memcpy(s_shown[row], tmp, W_COLS + 1);
    s_color[row] = col;
    st7735_draw_string(0, (uint8_t)(row * ROW_STEP), tmp, col, BG_COL, UI_SCALE);
}

static void put_sep(uint8_t row)
{
    put(row, SEP_COL, "--------------------------");
}

/* value in milli-units (or mA) -> "18.03 V" */
static void fmt_val(char *b, size_t n, int32_t v, int32_t div, uint8_t dec, const char *unit)
{
    int32_t a  = v < 0 ? -v : v;
    int32_t ip = a / div;
    if (dec == 0) {
        snprintf(b, n, "%s%ld %s", v < 0 ? "-" : "", (long)ip, unit);
        return;
    }
    int32_t p10 = 1;
    for (uint8_t i = 0; i < dec; i++) p10 *= 10;
    int32_t frac = (int32_t)((int64_t)(a % div) * p10 / div);
    snprintf(b, n, "%s%ld.%0*ld %s", v < 0 ? "-" : "", (long)ip, (int)dec, (long)frac, unit);
}

/* ── Buttons & encoder ────────────────────────────────────────────────── */
#define BTN_OUT   (1u << 0)   /* GPA0 */
#define BTN_ENC   (1u << 1)   /* GPA1 */
#define BTN_MENU  (1u << 5)   /* GPB5 */
#define BTN_MODE  (1u << 7)   /* GPB7 */

typedef struct {
    int32_t delta;
    uint8_t gpa_fell, gpb_fell, gpb;
    bool enc()  const { return gpa_fell & BTN_ENC; }
    bool out()  const { return gpa_fell & BTN_OUT; }
    bool menu() const { return gpb_fell & BTN_MENU; }
    bool mode() const { return gpb_fell & BTN_MODE; }
} ev_t;

static uint8_t s_gpa_prev = 0xFF, s_gpb_prev = 0xFF;

static void ev_prime(void)
{
    mcp23s17_read_inputs(&s_gpa_prev, &s_gpb_prev);
    enc_consume();
}

/* Wait for the buttons used in the wizard to be released, then re-arm edge detection */
static void wait_release(void)
{
    uint8_t ga, gb;
    do {
        sleep_ms(10);
        mcp23s17_read_inputs(&ga, &gb);
    } while ((ga & (BTN_OUT | BTN_ENC)) != (BTN_OUT | BTN_ENC) ||
             (gb & (BTN_MENU | BTN_MODE)) != (BTN_MENU | BTN_MODE));
    sleep_ms(30);
    ev_prime();
}

static ev_t ev_poll(void)
{
    ev_t e;
    uint8_t ga, gb;
    mcp23s17_read_inputs(&ga, &gb);
    e.gpa_fell = (uint8_t)(s_gpa_prev & ~ga);
    e.gpb_fell = (uint8_t)(s_gpb_prev & ~gb);
    e.gpb      = gb;
    s_gpa_prev = ga;
    s_gpb_prev = gb;
    e.delta    = enc_consume();
    if (e.gpa_fell || e.gpb_fell) beep();
    return e;
}

/* ── ADC helpers ──────────────────────────────────────────────────────── */
typedef struct {
    double  sum[ADC_NUM_CHANNELS];
    int32_t n;
    int32_t vin_cal_min;     /* lowest calibrated Vin seen, mV */
} acc_t;

static void acc_reset(acc_t *a)
{
    memset(a, 0, sizeof(*a));
    a->vin_cal_min = INT32_MAX;
}

static void acc_add(acc_t *a, const adc_readings_t &r)
{
    for (int c = 0; c < ADC_NUM_CHANNELS; c++) a->sum[c] += r.raw_mv[c];
    a->n++;
    if (r.calibrated[CHANNEL_VIN] < a->vin_cal_min) a->vin_cal_min = r.calibrated[CHANNEL_VIN];
}

static double acc_mean(const acc_t &a, adc_channel_t ch)
{
    return a.n ? a.sum[ch] / a.n : 0.0;
}

/* Blocking average of n samples, 1 ms apart */
static bool sample_avg(acc_t *a, int n)
{
    adc_readings_t r;
    acc_reset(a);
    for (int i = 0; i < n; i++) {
        if (!adc_read_all_channels(&r)) return false;
        acc_add(a, r);
        sleep_ms(1);
    }
    return true;
}

/* Calibrated Vin (10 mV units) averaged over a few samples, with current g_cal */
static int32_t vin_now(void)
{
    acc_t a;
    if (!sample_avg(&a, 16)) return 0;
    adc_calibration_t c;
    adc_get_calibration(CHANNEL_VIN, &c);
    return adc_apply_calibration((int32_t)lround(acc_mean(a, CHANNEL_VIN)), &c);
}

/* ── DAC helpers ──────────────────────────────────────────────────────── */
static int32_t clamp_cnt(int32_t c)
{
    if (c < DAC_MIN_CNT) return DAC_MIN_CNT;
    if (c > DAC_MAX_CNT) return DAC_MAX_CNT;
    return c;
}

/* DAC count that should display `value` for the given setpoint calibration */
static int32_t cnt_for(int32_t value, int32_t gain, int32_t div, int32_t off)
{
    if (gain <= 0 || div <= 0) return 0;
    int64_t mv = ((int64_t)value + off) * div / gain;
    return clamp_cnt((int32_t)(mv * DAC_MAX_COUNT / DAC_VREF_MV));
}

static void outputs_off(mcp4728 &dac)
{
    g_vi_enabled = false;
    g_isink_enabled = false;
    dac_update(dac);
}

/* ── Step table ───────────────────────────────────────────────────────── */
typedef enum { ST_ADJUST, ST_MAX } step_kind_t;
typedef enum { CH_V, CH_I, CH_VIN } step_ch_t;

typedef struct {
    const char  *id;
    step_kind_t  kind;
    step_ch_t    ch;
    const char  *meter;        /* meter function and range, shown to the user  */
    int32_t      target;       /* ADJUST: mV or mA                              */
    int32_t      unit_step;    /* MAX: entry step per encoder click (mV / mA)   */
    int32_t      disp_div;     /* value / disp_div = displayed number           */
    uint8_t      disp_dec;     /* decimals shown                                */
    const char  *unit;
    const char  *pre[4];       /* optional prompt shown before the step         */
} step_t;

static const step_t STEPS[] = {
    {"V1", ST_ADJUST, CH_V,   "DC V, range 2V",    1800,  0, 1000, 3, "V",
        {"PART 1: VOLTAGE", "Terminals open. Connect", "a DC voltmeter across", "the output terminals."}},
    {"V2", ST_ADJUST, CH_V,   "DC V, range 20V",  18000,  0, 1000, 2, "V",  {NULL}},
    {"V3", ST_MAX,    CH_V,   "DC V, range 200V",    0,  10, 1000, 2, "V",  {NULL}},
    {"I1", ST_ADJUST, CH_I,   "DC mA, range 20mA",  18,   0,    1, 1, "mA",
        {"PART 2: CURRENT", "Short the terminals with", "an ammeter (mA jack).", "Output is off meanwhile."}},
    {"I2", ST_ADJUST, CH_I,   "DC mA, range 200mA", 180,  0,    1, 0, "mA", {NULL}},
    {"I3", ST_ADJUST, CH_I,   "DC A, range 2A/20A", 1800, 0, 1000, 3, "A",
        {"Move the meter lead to", "the A jack (2A or 20A).", "Output is off meanwhile."}},
    {"I4", ST_MAX,    CH_I,   "DC A, range 20A",     0,   1, 1000, 3, "A",
        {"Move the meter lead to", "the 20A jack.", "Output turns on for 5 s", "only. Read meter fast!"}},
    {"Vin", ST_MAX,   CH_VIN, "DC V, range 200V",    0,  10, 1000, 2, "V",
        {"PART 3: INPUT VOLTAGE", "Optional. Measure Vin at", "XT60 / USB-C input.", "MODE=skip this part."}},
};
#define NUM_STEPS  ((int)(sizeof(STEPS) / sizeof(STEPS[0])))

/* ── Collected points ─────────────────────────────────────────────────── */
typedef struct {
    double truth;
    double x_set;     /* DAC output, mV (V and I steps)    */
    double x_meas;    /* raw ADC mean, mV                  */
    const char *id;
} wpoint_t;

typedef struct {
    wpoint_t v[WIZ_MAX_PTS];  int nv;
    wpoint_t i[WIZ_MAX_PTS];  int ni;
    wpoint_t vin;             int nvin;
} points_t;

/* ── Step execution ───────────────────────────────────────────────────── */
typedef enum { A_OK, A_SKIP, A_QUIT, A_FAIL } attempt_t;

typedef struct {
    mcp4728     &dac;
    int          vin_ref;     /* Vin before the output was turned on */
    const char  *fail_reason;
} ctx_t;

static void header(const step_t &st, int idx)
{
    char tag[12];
    snprintf(tag, sizeof(tag), "%s %d/%d", st.id, idx + 1, NUM_STEPS);
    put(0, HDR_COL, "%-17s%9s", "CAL WIZARD", tag);
}

/* Returns true when Vin has been bad for several consecutive checks */
static bool vin_bad_tick(ctx_t &c, int32_t vin_cal_mV, int *bad_ticks)
{
    bool bad = (vin_cal_mV < c.vin_ref - c.vin_ref * WIZ_VIN_DROP_PCT / 100) ||
               (g_cal.vin_min_mV > 0 && vin_cal_mV < g_cal.vin_min_mV);
    *bad_ticks = bad ? *bad_ticks + 1 : 0;
    return *bad_ticks >= WIZ_VIN_BAD_TICKS;
}

/* Runs Plim; returns true if any limiter reacted */
static bool plim_hit(const adc_readings_t &r)
{
    int32_t before = g_dac_cnt[DAC_CH_C];
    power_limit_update(r.calibrated[CHANNEL_VOUT], r.calibrated[CHANNEL_IOUT],
                       r.calibrated[CHANNEL_HIGH_CURRENT_IN]);
    return g_pout_limiting || g_psink_limiting || g_dac_cnt[DAC_CH_C] != before;
}

static void show_pre(const step_t &st, int idx)
{
    scr_clear();
    header(st, idx);
    put_sep(1);
    uint8_t row = 2;
    for (int i = 0; i < 4 && st.pre[i]; i++) put(row++, i == 0 ? ACT_COL : FG_COL, "%s", st.pre[i]);
    /* Every step needs a (possibly) different meter range - always say so */
    put(8, WARN_COL, "!! CHANGE METER RANGE !!");
    put(9, FG_COL, "Set meter to:");
    put(10, ACT_COL, "%s", st.meter);
    put(11, FG_COL, "Output is off meanwhile.");
    put(W_FOOT, DIS_COL, "ENC=OK MODE=skip MENU=quit");
}

/* Pre-step prompt (shown before every step). Output is off. */
static attempt_t prompt_pre(const step_t &st, int idx)
{
    show_pre(st, idx);
    beep();
    while (true) {
        sleep_ms(5);
        ev_t e = ev_poll();
        if (e.menu()) return A_QUIT;
        if (e.mode()) return A_SKIP;
        if (e.enc())  return A_OK;
    }
}

/* Number entry. Output state is whatever the caller set. Returns A_OK with *value set. */
static attempt_t enter_value(ctx_t &c, const step_t &st, int idx, int32_t *value,
                             bool out_toggle, bool timed_done)
{
    mav_t mv;
    mav_init(&mv, 0);
    uint64_t last_disp = 0;
    char b[24];
    const adc_channel_t ch = st.ch == CH_V ? CHANNEL_VOUT : st.ch == CH_I ? CHANNEL_IOUT : CHANNEL_VIN;
    int bad = 0;
    bool dirty = true;

    scr_clear();
    header(st, idx);
    put(1, FG_COL, "Meter: %s", st.meter);
    put(2, ACT_COL, "Enter measured value:");
    put_sep(5);
    put(W_FOOT, DIS_COL, "ENC=OK MODE=skip MENU=quit");
    if (timed_done) put(7, ON_COL, "Output is off.");

    while (true) {
        sleep_ms(4);
        ev_t e = ev_poll();
        if (e.menu()) return A_QUIT;
        if (e.mode()) return A_SKIP;

        if (e.out() && out_toggle) { g_vi_enabled = !g_vi_enabled; dirty = true; }
        if (e.out() && !out_toggle && g_vi_enabled) { g_vi_enabled = false; dirty = true; }

        int32_t mult = encoder_step_multiplier(e.gpb);
        if (e.delta) {
            int64_t nv = (int64_t)*value + (int64_t)e.delta * st.unit_step * mult;
            if (nv < 0) nv = 0;
            if (nv > 999999) nv = 999999;
            *value = (int32_t)nv;
        }

        if (e.enc()) {
            if (out_toggle && !g_vi_enabled) { put(8, OFF_COL, "Output is off - press OUT"); continue; }
            return A_OK;
        }

        adc_readings_t r;
        if (!adc_read_all_channels(&r)) continue;
        int32_t shown = mav_update(&mv, r.calibrated[ch]);

        if (g_vi_enabled && st.ch != CH_VIN) {
            if (vin_bad_tick(c, r.calibrated[CHANNEL_VIN], &bad)) {
                c.fail_reason = "Input supply too weak";
                outputs_off(c.dac);
                return A_FAIL;
            }
            if (plim_hit(r)) {
                c.fail_reason = "Power limit hit!";
                outputs_off(c.dac);
                return A_FAIL;
            }
        }

        if (dirty) { dac_update(c.dac); dirty = false; }

        uint64_t now = time_us_64();
        if (now - last_disp >= 100000u) {
            last_disp = now;
            fmt_val(b, sizeof(b), *value, st.disp_div, st.disp_dec, st.unit);
            put(4, ACT_COL, "%s", b);
            put(6, FG_COL, "step x%ld", (long)mult);
            if (!timed_done) {
                fmt_val(b, sizeof(b), shown, st.disp_div, st.disp_dec, st.unit);
                put(7, FG_COL, "ADC %s (now)", b);
                put(8, g_vi_enabled || st.ch == CH_VIN ? ON_COL : OFF_COL, "%s",
                    st.ch == CH_VIN ? "Output stays off." : g_vi_enabled ? "OUTPUT ON" : "OUTPUT OFF (OUT=on)");
            }
        }
    }
}

/* ADJUST step: user turns the knob until the meter shows the target */
static attempt_t adjust_loop(ctx_t &c, const step_t &st, int idx, wpoint_t *out)
{
    const uint8_t ch_dac = st.ch == CH_V ? DAC_CH_C : DAC_CH_D;
    const adc_channel_t ch_adc = st.ch == CH_V ? CHANNEL_VOUT : CHANNEL_IOUT;
    char b[24];
    mav_t mv;
    mav_init(&mv, 0);
    int bad = 0;
    bool dirty = true;
    uint64_t last_disp = 0;

    fmt_val(b, sizeof(b), st.target, st.disp_div, st.disp_dec, st.unit);

    scr_clear();
    header(st, idx);
    put(1, FG_COL, "Meter: %s", st.meter);
    put(2, FG_COL, "Turn knob until meter");
    put(3, ACT_COL, "shows:   %s", b);
    put_sep(4);
    put(W_FOOT, DIS_COL, "ENC=OK MODE=skip MENU=quit");

    g_vi_enabled = true;

    while (true) {
        sleep_ms(4);
        ev_t e = ev_poll();
        if (e.menu()) { outputs_off(c.dac); return A_QUIT; }
        if (e.mode()) { outputs_off(c.dac); return A_SKIP; }
        if (e.out())  { g_vi_enabled = !g_vi_enabled; dirty = true; }

        int32_t mult = encoder_step_multiplier(e.gpb);
        if (e.delta) {
            g_dac_cnt[ch_dac] = clamp_cnt(g_dac_cnt[ch_dac] + e.delta * mult);
            dirty = true;
        }

        adc_readings_t r;
        if (!adc_read_all_channels(&r)) continue;
        int32_t shown = mav_update(&mv, r.calibrated[ch_adc]);

        if (g_vi_enabled) {
            if (vin_bad_tick(c, r.calibrated[CHANNEL_VIN], &bad)) {
                c.fail_reason = "Input supply too weak";
                outputs_off(c.dac);
                return A_FAIL;
            }
            if (plim_hit(r)) {
                c.fail_reason = "Power limit hit!";
                outputs_off(c.dac);
                return A_FAIL;
            }
        }

        if (dirty) { dac_update(c.dac); dirty = false; }

        if (e.enc()) {
            if (!g_vi_enabled) {
                put(8, OFF_COL, "Output is off - press OUT");
            } else {
                acc_t a;
                if (!sample_avg(&a, WIZ_AVG_SAMPLES)) { c.fail_reason = "ADC read error"; outputs_off(c.dac); return A_FAIL; }
                /* a single low sample in the averaged block is enough to reject it */
                bool weak = (a.vin_cal_min < c.vin_ref - c.vin_ref * WIZ_VIN_DROP_PCT / 100) ||
                            (g_cal.vin_min_mV > 0 && a.vin_cal_min < g_cal.vin_min_mV);
                if (weak) { c.fail_reason = "Input supply too weak"; outputs_off(c.dac); return A_FAIL; }
                out->truth  = (double)st.target;
                out->x_set  = (double)cnt_to_mv(g_dac_cnt[ch_dac]);
                out->x_meas = acc_mean(a, ch_adc);
                out->id     = st.id;
                outputs_off(c.dac);
                return A_OK;
            }
        }

        uint64_t now = time_us_64();
        if (now - last_disp >= 100000u) {
            last_disp = now;
            put(6, FG_COL, "DAC %-5ld  step x%ld", (long)g_dac_cnt[ch_dac], (long)mult);
            fmt_val(b, sizeof(b), shown, st.disp_div, st.disp_dec, st.unit);
            put(7, FG_COL, "ADC  %s (now)", b);
            put(9, g_vi_enabled ? ON_COL : OFF_COL, "%s", g_vi_enabled ? "OUTPUT ON" : "OUTPUT OFF (OUT=on)");
        }
    }
}

/* V3 and I4: output on for at most 5 s, ADC averaged automatically, value typed in afterwards */
static attempt_t max_timed(ctx_t &c, const step_t &st, int idx, wpoint_t *out)
{
    char b[24];
    const bool isv = (st.ch == CH_V);
    const uint8_t ch_dac = isv ? DAC_CH_C : DAC_CH_D;
    const adc_channel_t ch_adc = isv ? CHANNEL_VOUT : CHANNEL_IOUT;
    int32_t value = isv ? setpt_disp(DAC_MAX_CNT, g_cal.vset_gain, g_cal.vset_div, g_cal.vset_off)
                        : setpt_disp(DAC_MAX_CNT, g_cal.iset_gain, g_cal.iset_div, g_cal.iset_off);

    scr_clear();
    header(st, idx);
    put(1, FG_COL, "Meter: %s", st.meter);
    put(2, ACT_COL, isv ? "Max voltage test" : "Max current test");
    put(3, FG_COL, "Output will run for 5 s.");
    put(4, FG_COL, "Read the meter while the");
    put(5, FG_COL, "screen says READ NOW.");
    put(W_FOOT, DIS_COL, "ENC=start MODE=skip MENU=quit");
    while (true) {
        sleep_ms(5);
        ev_t e = ev_poll();
        if (e.menu()) return A_QUIT;
        if (e.mode()) return A_SKIP;
        if (e.enc())  break;
    }

    scr_clear();
    header(st, idx);
    put(1, FG_COL, "Meter: %s", st.meter);
    put(W_FOOT, DIS_COL, "OUT=stop MENU=quit");

    acc_t a;
    acc_reset(&a);
    int bad = 0;
    bool stopped = false;
    /* Voltage: soft start from 0 instead of a jump to maximum (output capacitors) */
    if (isv) g_dac_cnt[ch_dac] = 0;
    const uint64_t t0 = time_us_64();
    g_vi_enabled = true;
    dac_update(c.dac);

    while (true) {
        sleep_ms(2);
        uint32_t t = (uint32_t)((time_us_64() - t0) / 1000u);
        ev_t e = ev_poll();
        if (e.menu()) { outputs_off(c.dac); return A_QUIT; }
        if (e.out() || e.enc()) { stopped = true; break; }

        if (isv && t <= WIZ_RAMP_MS + 100u) {
            int32_t cnt = t >= WIZ_RAMP_MS ? DAC_MAX_CNT : (int32_t)((int64_t)DAC_MAX_CNT * t / WIZ_RAMP_MS);
            if (cnt != g_dac_cnt[ch_dac]) { g_dac_cnt[ch_dac] = cnt; dac_update(c.dac); }
        }

        adc_readings_t r;
        if (adc_read_all_channels(&r)) {
            if (vin_bad_tick(c, r.calibrated[CHANNEL_VIN], &bad)) {
                c.fail_reason = "Input supply too weak";
                outputs_off(c.dac);
                return A_FAIL;
            }
            if (plim_hit(r)) {
                c.fail_reason = "Power limit hit!";
                outputs_off(c.dac);
                return A_FAIL;
            }
            if (t >= WIZ_I4_SETTLE_MS && t < WIZ_I4_OFF_MS - 500u &&
                (t < WIZ_I4_AVG_END_MS || a.n < WIZ_AVG_SAMPLES))
                acc_add(&a, r);
            fmt_val(b, sizeof(b), r.calibrated[ch_adc], st.disp_div, st.disp_dec, st.unit);
            put(7, FG_COL, "ADC  %s (now)", b);
        }

        if (t < WIZ_I4_SETTLE_MS) {
            put(3, FG_COL, "Settling...");
        } else if (t < WIZ_I4_AVG_END_MS || a.n < WIZ_AVG_SAMPLES) {
            put(3, FG_COL, "Averaging...");
        } else {
            put(3, ACT_COL, "READ METER NOW!");
        }
        if (t >= WIZ_I4_OFF_MS) break;
        put(5, WARN_COL, "Output off in %lu s", (unsigned long)((WIZ_I4_OFF_MS - t + 999u) / 1000u));
    }
    outputs_off(c.dac);

    if (a.n < 64) {
        c.fail_reason = stopped ? "Stopped too early" : "Too few ADC samples";
        return A_FAIL;
    }

    out->x_set  = (double)cnt_to_mv(g_dac_cnt[ch_dac]);
    out->x_meas = acc_mean(a, ch_adc);
    out->id     = st.id;
    attempt_t r = enter_value(c, st, idx, &value, false, true);
    if (r != A_OK) return r;
    out->truth = (double)value;
    return A_OK;
}

static attempt_t attempt(ctx_t &c, const step_t &st, int idx, wpoint_t *out)
{
    for (uint8_t i = 0; i < DAC_NUM_CH; i++) g_dac_cnt[i] = 0;
    outputs_off(c.dac);
    c.fail_reason = NULL;

    if (st.ch != CH_VIN) {
        c.vin_ref = vin_now();
        if (c.vin_ref < WIZ_VIN_MIN) {
            c.fail_reason = "Input supply too weak";
            return A_FAIL;
        }
    }

    if (st.ch == CH_V) {
        g_dac_cnt[DAC_CH_D] = cnt_for(WIZ_VSET_I_MA, g_cal.iset_gain, g_cal.iset_div, g_cal.iset_off);
        g_dac_cnt[DAC_CH_C] = st.kind == ST_MAX ? DAC_MAX_CNT
            : cnt_for(st.target, g_cal.vset_gain, g_cal.vset_div, g_cal.vset_off);
    } else if (st.ch == CH_I) {
        g_dac_cnt[DAC_CH_C] = cnt_for(WIZ_ISET_V_MV, g_cal.vset_gain, g_cal.vset_div, g_cal.vset_off);
        g_dac_cnt[DAC_CH_D] = st.kind == ST_MAX ? DAC_MAX_CNT
            : cnt_for(st.target, g_cal.iset_gain, g_cal.iset_div, g_cal.iset_off);
    }

    if (st.kind == ST_ADJUST)
        return adjust_loop(c, st, idx, out);

    /* Maximum voltage / current: output runs only briefly, value typed in afterwards */
    if (st.ch != CH_VIN)
        return max_timed(c, st, idx, out);

    /* Vin (output stays off): type in the meter reading */
    int32_t value = vin_now() * WIZ_VIN_UNIT_MV;   /* entry is in mV */
    attempt_t r = enter_value(c, st, idx, &value, false, false);
    if (r != A_OK) { outputs_off(c.dac); return r; }

    acc_t a;
    if (!sample_avg(&a, WIZ_AVG_SAMPLES)) { outputs_off(c.dac); c.fail_reason = "ADC read error"; return A_FAIL; }
    out->x_set  = 0.0;
    out->x_meas = acc_mean(a, CHANNEL_VIN);
    /* Vin is calibrated in 10 mV units, the entry is in mV */
    out->truth = st.ch == CH_VIN ? (double)value / WIZ_VIN_UNIT_MV : (double)value;
    out->id    = st.id;
    outputs_off(c.dac);
    return A_OK;
}

static attempt_t run_step(ctx_t &c, const step_t &st, int idx, wpoint_t *out)
{
    for (uint8_t i = 0; i < DAC_NUM_CH; i++) g_dac_cnt[i] = 0;
    outputs_off(c.dac);

    attempt_t p = prompt_pre(st, idx);
    if (p != A_OK) return p;

    while (true) {
        attempt_t r = attempt(c, st, idx, out);
        if (r != A_FAIL) return r;

        outputs_off(c.dac);
        beep_special();
        scr_clear();
        header(st, idx);
        put(2, OFF_COL, "STEP ABORTED");
        put(3, OFF_COL, "%s", c.fail_reason ? c.fail_reason : "Error");
        put(5, FG_COL, "Output is off.");
        put(6, FG_COL, "This point was not counted.");
        if (c.fail_reason && strstr(c.fail_reason, "supply"))
            put(8, FG_COL, "Use a stronger supply.");
        put(W_FOOT, DIS_COL, "ENC=retry MODE=skip MENU=quit");
        while (true) {
            sleep_ms(5);
            ev_t e = ev_poll();
            if (e.menu()) return A_QUIT;
            if (e.mode()) return A_SKIP;
            if (e.enc())  break;
        }
    }
}

/* ── Intro / warning ──────────────────────────────────────────────────── */
static bool intro(ctx_t &c)
{
    scr_clear();
    put(0, WARN_COL, "! WARNING !");
    put(1, FG_COL, "Use a strong input supply");
    put(2, FG_COL, "(XT60 / USB-C PD, >=60 W).");
    put(3, FG_COL, "Weak supply = wrong cal!");
    put(4, FG_COL, "Do not power off during");
    put(5, FG_COL, "calibration.");
    put(W_FOOT, DIS_COL, "ENC=continue MENU=quit");

    uint64_t last = 0;
    bool ok = false;
    while (true) {
        sleep_ms(5);
        ev_t e = ev_poll();
        if (e.menu()) return false;
        uint64_t now = time_us_64();
        if (now - last >= 200000u) {
            last = now;
            int32_t vin = vin_now();
            char b[24];
            fmt_val(b, sizeof(b), vin, 100, 2, "V");   /* 10 mV units */
            ok = vin >= WIZ_VIN_MIN;
            put(7, ok ? ON_COL : OFF_COL, "Vin: %s", b);
            put(8, OFF_COL, "%s", ok ? "" : "Vin < 12 V - cannot start");
        }
        if (e.enc() && ok) return true;
        (void)c;
    }
}

/* ── Fit + results ────────────────────────────────────────────────────── */
typedef struct {
    const char       *name;
    cal_fit_status_t  st;
    int32_t          *p_gain, *p_div, *p_off;     /* live calibration (read-only until save) */
    int32_t           og, od, oo, ng, no;
    cal_pt_t          pt[WIZ_MAX_PTS];
    const char       *pid[WIZ_MAX_PTS];
    int               n;
} res_t;

#define NUM_RES 5

static void make_res(res_t *r, const char *name, const wpoint_t *pts, int n, bool use_set,
                     int32_t *pg, int32_t *pd, int32_t *po)
{
    memset(r, 0, sizeof(*r));
    r->name = name;
    r->p_gain = pg; r->p_div = pd; r->p_off = po;
    r->og = *pg; r->od = *pd; r->oo = *po;
    r->n = n;
    for (int i = 0; i < n; i++) {
        r->pt[i].truth = pts[i].truth;
        r->pt[i].x     = use_set ? pts[i].x_set : pts[i].x_meas;
        r->pid[i]      = pts[i].id;
    }
    r->st = cal_fit(r->pt, n, r->oo, &r->ng, &r->no);
}

static bool res_changed(const res_t &r)
{
    return r.st == CAL_FIT_FULL || r.st == CAL_FIT_GAIN_ONLY;
}

static void draw_result_page(const res_t &r, int page)
{
    scr_clear();
    put(0, HDR_COL, "RESULT %-12s%3d/%d", r.name, page + 1, NUM_RES + 1);
    put_sep(1);
    put(2, FG_COL, "Old g%ld/%ld o%ld", (long)r.og, (long)r.od, (long)r.oo);
    if (res_changed(r))
        put(3, ACT_COL, "New g%ld/%ld o%ld", (long)r.ng, (long)CAL_FIT_DIV, (long)r.no);
    switch (r.st) {
    case CAL_FIT_UNCHANGED: put(3, DIS_COL, "No points - unchanged"); break;
    case CAL_FIT_ERROR:     put(3, OFF_COL, "FIT ERROR - unchanged"); break;
    case CAL_FIT_GAIN_ONLY: put(4, WARN_COL, "1 point: offset kept"); break;
    default: break;
    }
    if (res_changed(r)) {
        put(6, DIS_COL, "Error at each point:");
        for (int i = 0; i < r.n; i += 2) {
            char col[2][14];
            for (int k = 0; k < 2; k++) {
                col[k][0] = '\0';
                if (i + k >= r.n) continue;
                long h = lround(cal_fit_error_pct(r.pt[i + k], r.ng, r.no) * 100.0);
                snprintf(col[k], sizeof(col[k]), "%-3s%c%ld.%02ld%%", r.pid[i + k],
                         h < 0 ? '-' : '+', labs(h) / 100, labs(h) % 100);
            }
            put((uint8_t)(7 + i / 2), FG_COL, "%-13s%s", col[0], col[1]);
        }
    }
    put(W_FOOT, DIS_COL, "turn=page ENC=next MENU=no");
}

/* Shows the results; returns true if the user confirmed saving */
static bool results(mcp4728 &dac, const points_t &pts, const int32_t *orig_dac, bool *saved)
{
    res_t res[NUM_RES];
    make_res(&res[0], "Vout", pts.v,  pts.nv,  false, &g_cal.vout_gain, &g_cal.vout_div, &g_cal.vout_off);
    make_res(&res[1], "Vset", pts.v,  pts.nv,  true,  &g_cal.vset_gain, &g_cal.vset_div, &g_cal.vset_off);
    make_res(&res[2], "Iout", pts.i,  pts.ni,  false, &g_cal.iout_gain, &g_cal.iout_div, &g_cal.iout_off);
    make_res(&res[3], "Iset", pts.i,  pts.ni,  true,  &g_cal.iset_gain, &g_cal.iset_div, &g_cal.iset_off);
    make_res(&res[4], "Vin",  &pts.vin, pts.nvin, false, &g_cal.vin_gain, &g_cal.vin_div, &g_cal.vin_off);

    bool any = false;
    for (int i = 0; i < NUM_RES; i++) any = any || res_changed(res[i]);

    int page = 0, drawn = -1;
    while (true) {
        if (page != drawn) {
            drawn = page;
            if (page < NUM_RES) {
                draw_result_page(res[page], page);
            } else {
                scr_clear();
                put(0, HDR_COL, "%-21s%3d/%d", "RESULT", page + 1, NUM_RES + 1);
                put_sep(1);
                if (any) {
                    put(3, ACT_COL, "Save calibration?");
                    put(5, FG_COL, "Old values in flash will");
                    put(6, FG_COL, "be overwritten.");
                    put(W_FOOT, DIS_COL, "Save? ENC=yes MENU=no");
                } else {
                    put(3, WARN_COL, "No valid points.");
                    put(4, FG_COL, "Nothing to save.");
                    put(W_FOOT, DIS_COL, "ENC=exit MENU=exit");
                }
            }
        }
        sleep_ms(5);
        ev_t e = ev_poll();
        if (e.menu()) return false;
        if (e.delta) {
            int np = page + (e.delta > 0 ? 1 : -1);
            if (np >= 0 && np <= NUM_RES) page = np;
        }
        if (e.enc()) {
            if (page < NUM_RES) { page++; continue; }
            if (!any) return false;
            break;
        }
    }

    /* Commit: g_cal changes only now, immediately before flash_save() */
    for (int i = 0; i < NUM_RES; i++) {
        if (!res_changed(res[i])) continue;
        *res[i].p_gain = res[i].ng;
        *res[i].p_div  = CAL_FIT_DIV;
        *res[i].p_off  = res[i].no;
    }
    for (uint8_t i = 0; i < DAC_NUM_CH; i++) g_cal.dac_cnt[i] = orig_dac[i];
    g_cal.cont.vset_cnt = g_cont_dac[0];
    g_cal.cont.iset_cnt = g_cont_dac[1];
    g_cal.cont.thr_cnt  = g_cont_thr;
    flash_save();
    cal_apply();
    *saved = true;
    (void)dac;

    scr_clear();
    put(3, ON_COL, "Calibration saved.");
    sleep_ms(1200);
    return true;
}

/* ── Entry point ──────────────────────────────────────────────────────── */
bool cal_wizard_run(mcp4728 &dac)
{
    int32_t orig_dac[DAC_NUM_CH];
    for (uint8_t i = 0; i < DAC_NUM_CH; i++) orig_dac[i] = g_dac_cnt[i];

    process_buttons_main_reset();
    outputs_off(dac);
    wait_release();

    ctx_t c = {dac, 0, NULL};
    bool saved = false;

    if (intro(c)) {
        points_t pts;
        memset(&pts, 0, sizeof(pts));
        bool quit = false;

        for (int i = 0; i < NUM_STEPS && !quit; i++) {
            wpoint_t p;
            memset(&p, 0, sizeof(p));
            attempt_t r = run_step(c, STEPS[i], i, &p);
            if (r == A_QUIT) { quit = true; break; }
            if (r != A_OK)   continue;
            switch (STEPS[i].ch) {
            case CH_V:   if (pts.nv < WIZ_MAX_PTS) pts.v[pts.nv++] = p; break;
            case CH_I:   if (pts.ni < WIZ_MAX_PTS) pts.i[pts.ni++] = p; break;
            case CH_VIN: pts.vin = p; pts.nvin = 1; break;
            }
        }
        if (!quit) results(dac, pts, orig_dac, &saved);
    }

    /* Cleanup (saved or cancelled): outputs off, setpoints and ADC cal as before */
    for (uint8_t i = 0; i < DAC_NUM_CH; i++) g_dac_cnt[i] = orig_dac[i];
    outputs_off(dac);
    cal_apply();
    g_pout_limiting = g_psink_limiting = false;
    msg_clear();
    process_buttons_main_reset();
    enc_consume();
    return saved;
}
