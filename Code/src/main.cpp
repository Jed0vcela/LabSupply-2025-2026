/**
 * main.cpp  –  Lab power supply controller
 *
 * MCP4728 DAC channels:
 *   A (ch 0)  Isink-Hi – high-current sink reference  ┐ both written together
 *   B (ch 1)  Isink-Lo – low-current sink reference   ┘ as virtual "Isink" channel
 *   C (ch 2)  Vset     – output voltage setpoint
 *   D (ch 3)  Iset     – output current setpoint
 *
 *   Vref=internal 2.048V, Gain=x1 → full-scale 2.048V
 *   Channels are powered down (1kΩ to GND) when value=0 OR outputs disabled.
 *
 * Hardware GPIOs:
 *   GP12  LowCurrentSwitch   – hardwired 1 (manual mode)
 *   GP13  HighCurrentSwitch  – hardwired 1 (manual mode)
 *   GP20  HighCurrent-enable – active LOW; enabled when Isink≠0 and outputs on
 *   GP21  LowCurrent-enable  – active LOW; enabled when Isink≠0 and outputs on
 *
 * MCP23S17 buttons (active-low, inverted logic):
 *   GPA0  – toggle all outputs on/off (falling edge)
 *   GPB0  – select Vset  (DAC C)
 *   GPB1  – encoder speed ×100 while held
 *   GPB2  – select Iset  (DAC D)
 *   GPB3  – encoder speed ×10 while held
 *   GPB4  – select Isink (DAC A+B together)
 *
 * MCP23S17 outputs:
 *   GPA7  – output-enabled indicator LED (on when outputs active)
 *
 * Encoder step sizes:
 *   Normal:         ENC_STEP   =  5 DAC counts/detent
 *   GPB3 held:  ×10 = 50 counts/detent
 *   GPB1 held: ×100 = 500 counts/detent
 *   (GPB1 takes priority over GPB3 if both held)
 *
 * Display layout (scale-1, landscape 160×128, rotation=3)
 * ─────────────────────────────────────────────────────────
 *  y=  0  "LAB SUPPLY v1.0"           header (cyan)
 *  y=  9  ── separator ──────────────────────────────
 *  y= 11  LEFT: "Vset:" [X.XXXV]    RIGHT: "Vout:" [X.XXXV]
 *  y= 20  LEFT: "Iset:" [X.XXXA]    RIGHT: "Iout:" [X.XXXA]
 *  y= 29  LEFT: "Isink" [X.XXXV]    RIGHT: "HiI: " [X.XXXA]
 *  y= 38  LEFT: "Tcore" [XX.XC]     RIGHT: "Tfan:" [XX.XC]
 *  y= 47  LEFT:  ──────             RIGHT: "Vin: " [XX.XXV]
 *  y= 56  ── separator ──────────────────────────────
 *  y= 59  "OUT: " [ON /OFF ]          output state
 *  y= 68  "Step:" [xNNN  ]            current encoder step size
 *
 * EMA filter (alpha = 1/8, ~8 sample time constant ≈ 32ms):
 *   filtered += (raw - filtered) >> 3
 */

#include "mcp4728.h"
#include "adc_measurement.h"
#include "st7735.h"
#include "mcp23s17.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/critical_section.h"

/* ── DAC ────────────────────────────────────────────────────────────── */
#define DAC_CH_A      0   /* Isink-Hi – mirrors Isink-Lo */
#define DAC_CH_B      1   /* Isink-Lo – mirrors Isink-Hi */
#define DAC_CH_C      2   /* Vset   */
#define DAC_CH_D      3   /* Iset   */
#define DAC_NUM_CH    4

/*
 * Virtual channel indices used for encoder/button selection.
 * DAC_VIRT_ISINK writes both DAC A and DAC B together.
 * DAC_VIRT_VSET / DAC_VIRT_ISET map directly to DAC_CH_C / DAC_CH_D.
 */
#define DAC_VIRT_ISINK  0
#define DAC_VIRT_VSET   2
#define DAC_VIRT_ISET   3
#define DAC_NUM_VIRT    3   /* number of selectable virtual channels */

#define DAC_VREF_MV   2048
#define DAC_MAX_COUNT 4095
#define DAC_MIN_MV    0
#define DAC_MAX_MV    2000
#define ENC_STEP      2     /* base DAC counts per detent */

static int32_t g_dac_mv[DAC_NUM_CH] = {0, 0, 0, 0};

/* Output enable toggle – true = outputs active */
static bool g_outputs_enabled = false;

static inline uint16_t mv_to_dac(int32_t mv)
{
    if (mv <= 0)           return 0;
    if (mv >= DAC_VREF_MV) return (uint16_t)DAC_MAX_COUNT;
    return (uint16_t)((int32_t)mv * DAC_MAX_COUNT / DAC_VREF_MV);
}

/* ── Hardware GPIOs ─────────────────────────────────────────────────── */
#define PIN_SW_HI   13   /* HighCurrentSwitch – hardwired 1 */
#define PIN_SW_LO   12   /* LowCurrentSwitch  – hardwired 1 */
#define PIN_EN_HI   20   /* HighCurrent-enable, active LOW  */
#define PIN_EN_LO   21   /* LowCurrent-enable,  active LOW  */

static void hw_gpio_init(void)
{
    gpio_init(PIN_SW_HI); gpio_set_dir(PIN_SW_HI, GPIO_OUT); gpio_put(PIN_SW_HI, 1);
    gpio_init(PIN_SW_LO); gpio_set_dir(PIN_SW_LO, GPIO_OUT); gpio_put(PIN_SW_LO, 1);
    gpio_init(PIN_EN_HI); gpio_set_dir(PIN_EN_HI, GPIO_OUT); gpio_put(PIN_EN_HI, 1);
    gpio_init(PIN_EN_LO); gpio_set_dir(PIN_EN_LO, GPIO_OUT); gpio_put(PIN_EN_LO, 1);
}

/**
 * Write DAC values, power-down states, enable pins, and indicator LED.
 * Isink: both ch A and ch B carry the same value and are enabled together.
 * When g_outputs_enabled=false: all channels powered down, enables high, LED off.
 * When g_outputs_enabled=true:  channel powered down only when value=0.
 */
static void dac_update(mcp4728 &dac)
{
    bool isink_on = g_outputs_enabled && (g_dac_mv[DAC_CH_A] != 0);
    bool c_on     = g_outputs_enabled && (g_dac_mv[DAC_CH_C] != 0);
    bool d_on     = g_outputs_enabled && (g_dac_mv[DAC_CH_D] != 0);

    /* Power-down: 0=normal, 1=1kΩ to GND */
    dac.setPowerDown(isink_on?0:1, isink_on?0:1, c_on?0:1, d_on?0:1);

    /* DAC values – B mirrors A for Isink */
    dac.analogWriteAll(
        isink_on ? mv_to_dac(g_dac_mv[DAC_CH_A]) : 0,
        isink_on ? mv_to_dac(g_dac_mv[DAC_CH_A]) : 0,
        c_on     ? mv_to_dac(g_dac_mv[DAC_CH_C]) : 0,
        d_on     ? mv_to_dac(g_dac_mv[DAC_CH_D]) : 0
    );
    dac.ldacPulse();

    /* Enable pins – active LOW; both sink enables track Isink */
    gpio_put(PIN_EN_HI, isink_on ? 0 : 1);
    gpio_put(PIN_EN_LO, isink_on ? 0 : 1);

    /* GPA7 LED – on when outputs enabled */
    mcp23s17_set_pin(MCP_OUT_GPA7, g_outputs_enabled);
}

/* ── Channel selection and button handling ──────────────────────────── */
/*
 * g_active_ch holds the virtual channel index:
 *   DAC_VIRT_ISINK (0) → encoder writes DAC A and DAC B together
 *   DAC_VIRT_VSET  (2) → encoder writes DAC C
 *   DAC_VIRT_ISET  (3) → encoder writes DAC D
 */
static uint8_t g_active_ch = DAC_VIRT_VSET;   /* default: Vset */

/*
 * Button → virtual channel map.
 * virt_ch_button_mask[i] is the GPB bit mask; virt_ch_index[i] is the
 * virtual channel that button selects.
 */
static const uint8_t virt_ch_button_mask[DAC_NUM_VIRT] = {
    (1u << 0),   /* GPB0 → Vset  */
    (1u << 2),   /* GPB2 → Iset  */
    (1u << 4),   /* GPB4 → Isink */
};
static const uint8_t virt_ch_index[DAC_NUM_VIRT] = {
    DAC_VIRT_VSET,
    DAC_VIRT_ISET,
    DAC_VIRT_ISINK,
};

/* Returns the current encoder step multiplier based on held GPB buttons.
 * GPB1=×100, GPB3=×10 (GPB1 takes priority). Active-low. */
static int32_t encoder_step_multiplier(uint8_t gpb)
{
    if ((gpb & (1u << 1)) == 0) return 100;
    if ((gpb & (1u << 3)) == 0) return 10;
    return 1;
}

/**
 * Process all button inputs from GPA and GPB.
 * gpa: GPA byte (bit 0 = output toggle)
 * gpb: GPB byte (channel select + speed modifier)
 */
static void process_buttons(uint8_t gpa, uint8_t gpb)
{
    static uint8_t gpa_prev = 0xFFu;
    static uint8_t gpb_prev = 0xFFu;

    /* GPA0 falling edge → toggle output enable */
    if ((gpa_prev & 1u) && !(gpa & 1u))
        g_outputs_enabled = !g_outputs_enabled;

    /* GPB channel select – falling edge only */
    for (uint8_t i = 0; i < DAC_NUM_VIRT; i++) {
        uint8_t mask = virt_ch_button_mask[i];
        if ((gpb_prev & mask) && !(gpb & mask))
            g_active_ch = virt_ch_index[i];
    }

    gpa_prev = gpa;
    gpb_prev = gpb;
}

/* ── Encoder ────────────────────────────────────────────────────────── */
#define ENC_A  10
#define ENC_B  11

static volatile int32_t   g_enc_delta = 0;
static critical_section_t g_enc_cs;

static const int8_t enc_table[16] = {
    0,-1,+1, 0,
   +1, 0, 0,-1,
   -1, 0, 0,+1,
    0,+1,-1, 0,
};
static uint8_t g_enc_state = 0;
static int8_t  g_enc_accum = 0;

static void encoder_isr(uint gpio, uint32_t /*events*/)
{
    (void)gpio;
    uint8_t a  = gpio_get(ENC_A) ? 1u : 0u;
    uint8_t b  = gpio_get(ENC_B) ? 1u : 0u;
    uint8_t ns = (uint8_t)((a << 1u) | b);
    /* Swap A/B to reverse direction */
    uint8_t ns_r = (uint8_t)(((ns & 1u) << 1u) | ((ns >> 1u) & 1u));
    int8_t  d  = enc_table[(g_enc_state << 2) | ns_r];
    g_enc_state = ns_r;
    if (d == 0) return;
    g_enc_accum += d;
    if (g_enc_accum >= 4) {
        g_enc_accum = 0;
        critical_section_enter_blocking(&g_enc_cs);
        g_enc_delta++;
        critical_section_exit(&g_enc_cs);
    } else if (g_enc_accum <= -4) {
        g_enc_accum = 0;
        critical_section_enter_blocking(&g_enc_cs);
        g_enc_delta--;
        critical_section_exit(&g_enc_cs);
    }
}

static void encoder_init(void)
{
    critical_section_init(&g_enc_cs);
    gpio_init(ENC_A); gpio_set_dir(ENC_A, GPIO_IN); gpio_pull_up(ENC_A);
    gpio_init(ENC_B); gpio_set_dir(ENC_B, GPIO_IN); gpio_pull_up(ENC_B);
    uint8_t a = gpio_get(ENC_A) ? 1u : 0u;
    uint8_t b = gpio_get(ENC_B) ? 1u : 0u;
    uint8_t s = (uint8_t)((a << 1u) | b);
    g_enc_state = (uint8_t)(((s & 1u) << 1u) | ((s >> 1u) & 1u));
    gpio_set_irq_enabled_with_callback(ENC_A,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, encoder_isr);
    gpio_set_irq_enabled(ENC_B,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
}

/* Apply encoder delta with speed multiplier to the active virtual channel */
static void apply_encoder(int32_t multiplier)
{
    int32_t delta;
    critical_section_enter_blocking(&g_enc_cs);
    delta = g_enc_delta;
    g_enc_delta = 0;
    critical_section_exit(&g_enc_cs);
    if (delta == 0) return;

    /* Convert step (in DAC counts) to mV */
    int32_t step_mv = (int32_t)ENC_STEP * multiplier * DAC_VREF_MV / DAC_MAX_COUNT;

    if (g_active_ch == DAC_VIRT_ISINK) {
        /* Isink: write ch A; ch B mirrors A in dac_update() */
        int32_t mv = g_dac_mv[DAC_CH_A] + delta * step_mv;
        if (mv < DAC_MIN_MV) mv = DAC_MIN_MV;
        if (mv > DAC_MAX_MV) mv = DAC_MAX_MV;
        g_dac_mv[DAC_CH_A] = mv;
        g_dac_mv[DAC_CH_B] = mv;   /* keep in sync so display is consistent */
    } else {
        /* Vset (ch C) or Iset (ch D) */
        int32_t mv = g_dac_mv[g_active_ch] + delta * step_mv;
        if (mv < DAC_MIN_MV) mv = DAC_MIN_MV;
        if (mv > DAC_MAX_MV) mv = DAC_MAX_MV;
        g_dac_mv[g_active_ch] = mv;
    }
}

/* ── Display ────────────────────────────────────────────────────────── */
#define UI_SCALE  1
#define CW        ((uint8_t)(FONT_W * UI_SCALE))
#define CH        ((uint8_t)(FONT_H * UI_SCALE))
#define ROW_STEP  ((uint8_t)(CH + 1))
#define HALF      ((uint8_t)80)
#define LABEL_W   ((uint8_t)(5 * CW))   /* 5-char labels = 30px */
#define VAL_CHARS 7
#define VAL_X_L   LABEL_W
#define VAL_X_R   ((uint8_t)(HALF + LABEL_W))

#define BG_COL   0x0000u
#define FG_COL   0xFFFFu
#define HDR_COL  0x07FFu   /* cyan        */
#define SEP_COL  0x4208u   /* dark grey   */
#define ACT_COL  0xFFE0u   /* yellow – active channel */
#define DIS_COL  0x7BEFu   /* grey – disabled outputs */
#define ON_COL   0x07E0u   /* green – output enabled  */
#define OFF_COL  0xF800u   /* red   – output disabled */

#define Y_HEADER  0
#define Y_SEP1    (Y_HEADER + CH + 1)
#define Y_ROW0    (Y_SEP1   + 2)
#define Y_ROW1    (Y_ROW0   + ROW_STEP)
#define Y_ROW2    (Y_ROW1   + ROW_STEP)
#define Y_ROW3    (Y_ROW2   + ROW_STEP)
#define Y_ROW4    (Y_ROW3   + ROW_STEP)
#define Y_SEP2    (Y_ROW4   + ROW_STEP + 1)
#define Y_STATUS  (Y_SEP2   + 3)
#define Y_STEP    (Y_STATUS + ROW_STEP)

/*
 * Left-column label/row/field for each virtual setpoint channel.
 * Array indexed 0..DAC_NUM_VIRT-1, order matches virt_ch_index[].
 */
static const uint8_t     virt_row_y[DAC_NUM_VIRT]  = { Y_ROW0, Y_ROW1, Y_ROW2 };
static const char *const virt_labels[DAC_NUM_VIRT]  = { "Vset:", "Iset:", "Isink" };
/* virt_ch_index[] already defined above: { VSET=2, ISET=3, ISINK=0 } */

/* Returns the virt_labels/virt_row_y array index for a given virtual ch */
static inline uint8_t virt_arr_idx(uint8_t vch)
{
    if (vch == DAC_VIRT_VSET)  return 0;
    if (vch == DAC_VIRT_ISET)  return 1;
    return 2;   /* DAC_VIRT_ISINK */
}

#define FIELD_BUF  (VAL_CHARS + 1)

typedef struct {
    uint8_t  x, y, max_chars;
    uint16_t fg, bg;
    char     prev[FIELD_BUF];
} field_t;

static bool update_field(field_t &f, const char *str)
{
    char p[FIELD_BUF];
    uint8_t len = (uint8_t)strlen(str);
    if (len > f.max_chars) len = f.max_chars;
    memcpy(p, str, len);
    for (uint8_t i = len; i < f.max_chars; i++) p[i] = ' ';
    p[f.max_chars] = '\0';
    if (memcmp(p, f.prev, f.max_chars + 1) == 0) return false;
    st7735_draw_string(f.x, f.y, p, f.fg, f.bg, UI_SCALE);
    memcpy(f.prev, p, f.max_chars + 1);
    return true;
}

enum FID {
    /* left column – virtual setpoint fields, order matches virt_arr_idx() */
    F_VSET=0,                   /* left row 0 – Vset  (DAC C)   */
    F_ISET,                     /* left row 1 – Iset  (DAC D)   */
    F_ISINK,                    /* left row 2 – Isink (DAC A+B) */
    F_TCORE,                    /* left row 3 */
    /* right column – measurements */
    F_VOUT,                     /* right row 0 */
    F_IOUT,                     /* right row 1 */
    F_HII,                      /* right row 2 */
    F_TFAN,                     /* right row 3 – NTC temp 2 (fan) */
    F_VIN,                      /* right row 4 */
    /* footer */
    F_STATUS, F_STEP,
    NUM_F
};

static field_t fields[NUM_F] = {
    /* left: virtual setpoint rows 0-2, Tcore row 3 */
    {VAL_X_L, Y_ROW0, VAL_CHARS, ACT_COL, BG_COL, {}},  /* F_VSET  (default active) */
    {VAL_X_L, Y_ROW1, VAL_CHARS, FG_COL,  BG_COL, {}},  /* F_ISET  */
    {VAL_X_L, Y_ROW2, VAL_CHARS, FG_COL,  BG_COL, {}},  /* F_ISINK */
    {VAL_X_L, Y_ROW3, VAL_CHARS, FG_COL,  BG_COL, {}},  /* F_TCORE */
    /* right: measurements */
    {VAL_X_R, Y_ROW0, VAL_CHARS, FG_COL,  BG_COL, {}},  /* F_VOUT  */
    {VAL_X_R, Y_ROW1, VAL_CHARS, FG_COL,  BG_COL, {}},  /* F_IOUT  */
    {VAL_X_R, Y_ROW2, VAL_CHARS, FG_COL,  BG_COL, {}},  /* F_HII   */
    {VAL_X_R, Y_ROW3, VAL_CHARS, FG_COL,  BG_COL, {}},  /* F_TFAN  */
    {VAL_X_R, Y_ROW4, VAL_CHARS, FG_COL,  BG_COL, {}},  /* F_VIN   */
    /* footer */
    {LABEL_W, Y_STATUS, VAL_CHARS, OFF_COL, BG_COL, {}}, /* F_STATUS */
    {LABEL_W, Y_STEP,   VAL_CHARS, FG_COL,  BG_COL, {}}, /* F_STEP   */
};

/* Map virtual channel → left-column field ID */
static inline uint8_t virt_ch_to_field(uint8_t vch)
{
    return (uint8_t)(F_VSET + virt_arr_idx(vch));
}

static void fmt_fp(char *b, int32_t val, uint8_t dec, const char *unit)
{
    bool    neg = (val < 0); int32_t v = neg ? -val : val;
    int32_t div = 1; for (uint8_t i = 0; i < dec; i++) div *= 10;
    char fmt[20];
    snprintf(fmt, sizeof(fmt), "%%s%%ld.%%0%dld%%s", (int)dec);
    snprintf(b, FIELD_BUF, fmt, neg?"-":"", (long)(v/div), (long)(v%div), unit);
}

static void fmt_temp(char *b, int32_t mC)
{
    fmt_fp(b, mC / 100, 1, "C");
}

static void draw_static_ui(void)
{
    st7735_fill_screen(BG_COL);
    st7735_draw_string(0, Y_HEADER, "LAB SUPPLY v1.0", HDR_COL, BG_COL, UI_SCALE);
    st7735_fill_rect(0, Y_SEP1, st7735_width(), 1, SEP_COL);
    st7735_fill_rect(0, Y_SEP2, st7735_width(), 1, SEP_COL);
    st7735_fill_rect(HALF-1, Y_SEP1+1, 1,
                     (uint8_t)(Y_SEP2 - Y_SEP1 - 1), SEP_COL);

    /* Left: virtual setpoint labels (colour updated by update_channel_labels) */
    for (uint8_t i = 0; i < DAC_NUM_VIRT; i++) {
        uint16_t col = (virt_ch_index[i] == g_active_ch) ? ACT_COL : FG_COL;
        st7735_draw_string(0, virt_row_y[i], virt_labels[i], col, BG_COL, UI_SCALE);
    }
    st7735_draw_string(0, Y_ROW3, "Tcore", FG_COL, BG_COL, UI_SCALE);

    /* Right: measurement labels */
    st7735_draw_string(HALF, Y_ROW0, "Vout:", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW1, "Iout:", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW2, "HiI: ", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW3, "Tfan:", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW4, "Vin: ", FG_COL, BG_COL, UI_SCALE);

    st7735_draw_string(0, Y_STATUS, "OUT: ", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(0, Y_STEP,   "Step:", FG_COL, BG_COL, UI_SCALE);
}

/* Redraw setpoint labels when active channel or output state changes */
static void update_channel_labels(void)
{
    static uint8_t prev_active  = 0xFFu;
    static bool    prev_enabled = false;

    bool changed = (g_active_ch != prev_active) ||
                   (g_outputs_enabled != prev_enabled);
    if (!changed) return;

    for (uint8_t i = 0; i < DAC_NUM_VIRT; i++) {
        bool is_active = (virt_ch_index[i] == g_active_ch);
        uint16_t col;
        if (!g_outputs_enabled)
            col = is_active ? ACT_COL : DIS_COL;
        else
            col = is_active ? ACT_COL : FG_COL;
        st7735_draw_string(0, virt_row_y[i], virt_labels[i], col, BG_COL, UI_SCALE);
    }

    /* Invalidate value fields whose colour changed */
    if (prev_active != 0xFFu) {
        uint8_t prev_fid = virt_ch_to_field(prev_active);
        fields[prev_fid].fg = g_outputs_enabled ? FG_COL : DIS_COL;
        memset(fields[prev_fid].prev, 0xFF, FIELD_BUF);
    }
    uint8_t cur_fid = virt_ch_to_field(g_active_ch);
    fields[cur_fid].fg = ACT_COL;
    memset(fields[cur_fid].prev, 0xFF, FIELD_BUF);

    prev_active  = g_active_ch;
    prev_enabled = g_outputs_enabled;
}

/* ═══════════════════════════════════════════════════════════════════════
 * ADC measurement calibration
 * ═══════════════════════════════════════════════════════════════════════
 * Formula:  calibrated = (raw_mv * gain / divisor) - offset
 *
 * To calibrate:
 *   1. Measure the real value with a reference meter.
 *   2. Note the raw_mv from debug output (adc_print_readings).
 *   3. Adjust gain/divisor for scaling errors (e.g. resistor divider ratio).
 *   4. Adjust offset for DC zero errors (same units as output: mV or mA).
 *
 * Leave gain=1000, divisor=1000 (=×1.000) if no scaling is needed.
 */

/* Vout – resistor divider ratio */
#define CAL_VOUT_GAIN      11088
#define CAL_VOUT_DIVISOR   1000
#define CAL_VOUT_OFFSET    175      /* mV DC offset trim */

/* Vin – resistor divider ratio */
#define CAL_VIN_GAIN       18551
#define CAL_VIN_DIVISOR    1703
#define CAL_VIN_OFFSET     0

/* HiI – high-current shunt amplifier */
#define CAL_HII_GAIN       10000
#define CAL_HII_DIVISOR    1000
#define CAL_HII_OFFSET     0     /* mA: subtract mid-rail offset */

/* LoI – low-current measurement (kept for adc_set_calibration; not displayed) */
#define CAL_LOI_GAIN       10
#define CAL_LOI_DIVISOR    1
#define CAL_LOI_OFFSET     0

/* Iout */
#define CAL_IOUT_GAIN      5405
#define CAL_IOUT_DIVISOR   1000
#define CAL_IOUT_OFFSET    0

/* ═══════════════════════════════════════════════════════════════════════
 * Setpoint display calibration  (DAC mV → displayed value)
 * ═══════════════════════════════════════════════════════════════════════
 * Formula:  display_val_milli = (dac_mv * GAIN / DIVISOR) - OFFSET
 * The result is divided by 1000 before display (mV→V, mA→A).
 *
 * Example: Vset op-amp has 3× gain → set VSET_GAIN=3000, VSET_DIVISOR=1000
 *   → displayed value = dac_mv × 3
 *
 * Leave GAIN=1000, DIVISOR=1000, OFFSET=0 for raw DAC mV pass-through.
 */
#define CAL_VSET_GAIN      15000
#define CAL_VSET_DIVISOR   1000
#define CAL_VSET_OFFSET    0        /* mV */

#define CAL_ISET_GAIN      3000
#define CAL_ISET_DIVISOR   1000
#define CAL_ISET_OFFSET    0        /* mA */

#define CAL_ISINK_GAIN     2000
#define CAL_ISINK_DIVISOR  1000
#define CAL_ISINK_OFFSET   0        /* mV */

/* ═══════════════════════════════════════════════════════════════════════
 * EMA (Exponential Moving Average) filter
 * ═══════════════════════════════════════════════════════════════════════
 * filtered += (new_sample - filtered) >> EMA_SHIFT
 * EMA_SHIFT=3 → alpha=1/8 → ~8 sample time constant ≈ 32ms at 4ms loop
 * EMA_SHIFT=4 → alpha=1/16 → smoother, ~64ms time constant
 * Initialised with INT32_MIN as sentinel so the first sample loads directly.
 */
#define EMA_SHIFT  2

typedef struct { int32_t v; } ema_t;

static inline void ema_init(ema_t *f) { f->v = INT32_MIN; }

static inline int32_t ema_update(ema_t *f, int32_t sample)
{
    if (f->v == INT32_MIN) f->v = sample;
    else f->v += (sample - f->v) >> EMA_SHIFT;
    return f->v;
}

/* One filter instance per displayed measurement */
static ema_t ema_vout, ema_iout, ema_hii, ema_tfan, ema_vin, ema_tcore;

static void ema_init_all(void)
{
    ema_init(&ema_vout);
    ema_init(&ema_iout);
    ema_init(&ema_hii);
    ema_init(&ema_tfan);
    ema_init(&ema_vin);
    ema_init(&ema_tcore);
}

/* ── Main ───────────────────────────────────────────────────────────── */
int main(void)
{
    stdio_init_all();

    const uint led = 25;
    gpio_init(led); gpio_set_dir(led, GPIO_OUT);

    /* DAC */
    mcp4728 dac(0x00);
    dac.begin();
    dac.setVref(1, 1, 1, 1);
    dac.setGain(0, 0, 0, 0);
    dac.setPowerDown(1, 1, 1, 1);
    dac.analogWriteAll(0, 0, 0, 0);
    dac.ldacPulse();

    hw_gpio_init();
    adc_measurement_init();

    /* Apply ADC calibration */
    adc_set_calibration(CHANNEL_VOUT,            CAL_VOUT_GAIN, CAL_VOUT_DIVISOR, CAL_VOUT_OFFSET);
    adc_set_calibration(CHANNEL_VIN,             CAL_VIN_GAIN,  CAL_VIN_DIVISOR,  CAL_VIN_OFFSET);
    adc_set_calibration(CHANNEL_HIGH_CURRENT_IN, CAL_HII_GAIN,  CAL_HII_DIVISOR,  CAL_HII_OFFSET);
    adc_set_calibration(CHANNEL_LOW_CURRENT_IN,  CAL_LOI_GAIN,  CAL_LOI_DIVISOR,  CAL_LOI_OFFSET);
    adc_set_calibration(CHANNEL_IOUT,            CAL_IOUT_GAIN, CAL_IOUT_DIVISOR, CAL_IOUT_OFFSET);

    ema_init_all();
    encoder_init();

    st7735_init();
    st7735_set_rotation(3);
    st7735_set_backlight(85);

    mcp23s17_init();

    draw_static_ui();

    char buf[FIELD_BUF];

    while (true)
    {
        gpio_put(led, true); sleep_ms(4); gpio_put(led, false);

        /* ── Buttons ───────────────────────────────────────────────── */
        uint8_t gpa_in, gpb_in;
        mcp23s17_read_inputs(&gpa_in, &gpb_in);
        process_buttons(gpa_in, gpb_in);

        /* ── Encoder with speed modifier ───────────────────────────── */
        int32_t mult = encoder_step_multiplier(gpb_in);
        apply_encoder(mult);

        /* ── DAC + enables + LED ────────────────────────────────────── */
        dac_update(dac);

        /* ── ADC + EMA filter ──────────────────────────────────────── */
        adc_readings_t r;
        if (!adc_read_all_channels(&r)) printf("ADC err\n");

        int32_t vout  = ema_update(&ema_vout,  r.calibrated[CHANNEL_VOUT]);
        int32_t iout  = ema_update(&ema_iout,  r.calibrated[CHANNEL_IOUT]);
        int32_t hii   = ema_update(&ema_hii,   r.calibrated[CHANNEL_HIGH_CURRENT_IN]);
        int32_t tfan  = ema_update(&ema_tfan,  adc_get_temp2_mC());
        int32_t vin   = ema_update(&ema_vin,   r.calibrated[CHANNEL_VIN]);
        int32_t tcore = ema_update(&ema_tcore, adc_get_core_temp_mC());

        /* ── Display ───────────────────────────────────────────────── */
        update_channel_labels();

        /* Left: setpoint values with calibration and unit suffix.
         * Isink unit = "V" (DAC voltage reference).
         * Vset  unit = "V", Iset unit = "A". */
        {
            int32_t vset_disp = g_dac_mv[DAC_CH_C] * CAL_VSET_GAIN  / CAL_VSET_DIVISOR  - CAL_VSET_OFFSET;
            int32_t iset_disp = g_dac_mv[DAC_CH_D] * CAL_ISET_GAIN  / CAL_ISET_DIVISOR  - CAL_ISET_OFFSET;
            int32_t isink_disp= g_dac_mv[DAC_CH_A] * CAL_ISINK_GAIN / CAL_ISINK_DIVISOR - CAL_ISINK_OFFSET;

            fmt_fp(buf, vset_disp,  3, "V"); update_field(fields[F_VSET],  buf);
            fmt_fp(buf, iset_disp,  3, "A"); update_field(fields[F_ISET],  buf);
            fmt_fp(buf, isink_disp, 3, "A"); update_field(fields[F_ISINK], buf);
        }

        fmt_temp(buf, tcore);
        update_field(fields[F_TCORE], buf);

        /* Right: measurements */
        fmt_fp(buf, vout, 3, "V"); update_field(fields[F_VOUT], buf);
        fmt_fp(buf, iout, 3, "A"); update_field(fields[F_IOUT], buf);
        fmt_fp(buf, hii,  3, "A"); update_field(fields[F_HII],  buf);
        fmt_temp(buf, tfan);       update_field(fields[F_TFAN], buf);
        fmt_fp(buf, vin,  3, "V"); update_field(fields[F_VIN],  buf);

        /* Status: OUT ON (green) / OFF (red) */
        {
            static bool prev_en = false;
            if (g_outputs_enabled != prev_en) {
                fields[F_STATUS].fg = g_outputs_enabled ? ON_COL : OFF_COL;
                memset(fields[F_STATUS].prev, 0xFF, FIELD_BUF);
                prev_en = g_outputs_enabled;
            }
            update_field(fields[F_STATUS], g_outputs_enabled ? "ON " : "OFF");
        }

        /* Step size indicator */
        snprintf(buf, sizeof(buf), "x%ld", (long)mult);
        update_field(fields[F_STEP], buf);
    }
}