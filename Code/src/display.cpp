#include "display.h"
#include "psu_control.h"
#include "peripherals.h"
#include "st7735.h"
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════
 * Utilities
 * ═══════════════════════════════════════════════════════════════════════ */
bool update_field(field_t &f, const char *str)
{
    char p[FIELD_BUF];
    uint8_t len = (uint8_t)strlen(str);
    if (len > f.max_chars) len = f.max_chars;
    memcpy(p, str, len);
    for (uint8_t i = len; i < f.max_chars; i++) p[i] = ' ';
    p[f.max_chars] = '\0';
    if (memcmp(p, f.prev, (size_t)(f.max_chars + 1)) == 0) return false;
    st7735_draw_string(f.x, f.y, p, f.fg, f.bg, UI_SCALE);
    memcpy(f.prev, p, (size_t)(f.max_chars + 1));
    return true;
}

void field_invalidate(field_t &f) { memset(f.prev, 0xFF, FIELD_BUF); }

/* val in milli-units, dec decimal places, unit appended.
 * Leading space for positives keeps column stable when sign changes. */
void fmt_fp(char *b, int32_t val, uint8_t dec, const char *unit)
{
    bool    neg = (val < 0); int32_t v = neg ? -val : val;
    int32_t div = 1; for (uint8_t i = 0; i < dec; i++) div *= 10;
    char fmt[20];
    snprintf(fmt, sizeof(fmt), "%%c%%ld.%%0%dld%%s", (int)dec);
    snprintf(b, FIELD_BUF, fmt, neg?'-':' ', (long)(v/div), (long)(v%div), unit);
}

void fmt_temp(char *b, int32_t mC) { fmt_fp(b, mC/100, 1, "C"); }

void mav_init(mav_t *m, int32_t v)
{
    for (uint8_t i = 0; i < MAV_LEN; i++) m->buf[i] = v;
    m->sum = v * (int32_t)MAV_LEN; m->idx = 0;
}
int32_t mav_update(mav_t *m, int32_t s)
{
    m->sum -= m->buf[m->idx];
    m->buf[m->idx] = s; m->sum += s;
    m->idx = (uint8_t)((m->idx + 1u) % MAV_LEN);
    return m->sum / (int32_t)MAV_LEN;
}

/* ── Shared virt-channel helpers ─────────────────────────────────────── */
static const uint8_t     virt_row_y[DAC_NUM_VIRT]  = { Y_ROW0, Y_ROW1, Y_ROW2 };
static const char *const virt_labels[DAC_NUM_VIRT]  = { "Vset:", "Iset:", "Isink" };

static inline uint8_t virt_arr_idx(uint8_t vch)
{
    if (vch == DAC_VIRT_VSET) return 0;
    if (vch == DAC_VIRT_ISET) return 1;
    return 2;
}

static uint16_t setpoint_colour(uint8_t vch, bool is_active)
{
    if (is_active) return ACT_COL;
    bool en = (vch == DAC_VIRT_ISINK) ? g_isink_enabled : g_vi_enabled;
    return en ? FG_COL : DIS_COL;
}

/* ── Footer pixel positions (shared across all modes) ─────────────────
 * Screen = 160 px, font = 6 px/char at scale 1.
 *
 * Row 0:  "source" (6ch=36px) + val(3ch)   "sink"(4ch=24px) + val(3ch)
 *         x=0 label, x=42 value            x=84 label, x=114 value
 * Row 1:  "step"(4ch=24px) + val(4ch)      "backlight"(9ch=54px) + val(4ch)
 *         x=0 label, x=30 value            x=66 label, x=120 value
 * ─────────────────────────────────────────────────────────────────────── */
#define FT_SRC_LBL_X    0u
#define FT_SRC_VAL_X    42u
#define FT_SNK_LBL_X    84u
#define FT_SNK_VAL_X    114u
#define FT_STEP_LBL_X   0u
#define FT_STEP_VAL_X   30u
#define FT_BL_LBL_X     66u
#define FT_BL_VAL_X     120u
#define FT_SRC_CHARS    3u
#define FT_SNK_CHARS    3u
#define FT_STEP_CHARS   4u
#define FT_BL_CHARS     4u

/* ═══════════════════════════════════════════════════════════════════════
 * MODE_SUPPLY_SINK
 *
 *  Left  x=0..79          Right x=80..159
 *  Vset: value            Vout: value
 *  Iset: value            Iout: value
 *  Isink value            Pout: value   ← moved here
 *  Tcore value            Psink value
 *  Tfan: value            Isink value   (measured)
 *                         Vin:  value   (XX.XXV — 2 decimals)
 *  ── sep ──────────────────────────────────────
 *  source ON/OFF          sink  ON/OFF
 *  step   xN              backlight XX%
 * ═══════════════════════════════════════════════════════════════════════ */
enum SS_FID {
    SS_VSET=0, SS_ISET, SS_ISINK_SET,
    SS_TCORE, SS_TFAN,                   /* left rows 3,4 */
    SS_VOUT, SS_IOUT, SS_POUT,           /* right rows 0,1,2 */
    SS_PSINK, SS_ISINK_MEAS, SS_VIN,     /* right rows 3,4,5 */
    SS_STATUS_VI, SS_STATUS_IS,
    SS_STEP, SS_BL,
    SS_NUM_F
};

static field_t ss_fields[SS_NUM_F] = {
    {VAL_X_L, Y_ROW0, VAL_CHARS, ACT_COL, BG_COL, {}},  /* SS_VSET      */
    {VAL_X_L, Y_ROW1, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_ISET      */
    {VAL_X_L, Y_ROW2, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_ISINK_SET */
    {VAL_X_L, Y_ROW3, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_TCORE     */
    {VAL_X_L, Y_ROW4, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_TFAN      */
    {VAL_X_R, Y_ROW0, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_VOUT      */
    {VAL_X_R, Y_ROW1, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_IOUT      */
    {VAL_X_R, Y_ROW2, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_POUT      */
    {VAL_X_R, Y_ROW3, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_PSINK     */
    {VAL_X_R, Y_ROW4, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_ISINK_MEAS*/
    {VAL_X_R, Y_ROW5, VAL_CHARS, FG_COL,  BG_COL, {}},  /* SS_VIN       */
    {FT_SRC_VAL_X, Y_FOOT0, FT_SRC_CHARS,  OFF_COL, BG_COL, {}},
    {FT_SNK_VAL_X, Y_FOOT0, FT_SNK_CHARS,  OFF_COL, BG_COL, {}},
    {FT_STEP_VAL_X,Y_FOOT1, FT_STEP_CHARS, FG_COL,  BG_COL, {}},
    {FT_BL_VAL_X,  Y_FOOT1, FT_BL_CHARS,   FG_COL,  BG_COL, {}},
};

static void ss_update_labels(void)
{
    static uint8_t prev_active = 0xFFu;
    static bool prev_vi = false, prev_is = false;
    if (g_active_ch == prev_active && g_vi_enabled == prev_vi &&
        g_isink_enabled == prev_is) return;
    for (uint8_t i = 0; i < DAC_NUM_VIRT; i++) {
        uint8_t  vch = gpb_sel_vch[i];
        bool     act = (virt_arr_idx(vch) == virt_arr_idx(g_active_ch));
        uint16_t col = setpoint_colour(vch, act);
        st7735_draw_string(0, virt_row_y[i], virt_labels[i], col, BG_COL, UI_SCALE);
        uint8_t fid = (uint8_t)(SS_VSET + virt_arr_idx(vch));
        ss_fields[fid].fg = col;
        field_invalidate(ss_fields[fid]);
    }
    prev_active = g_active_ch; prev_vi = g_vi_enabled; prev_is = g_isink_enabled;
}

void draw_supply_sink_static(void)
{
    st7735_fill_screen(BG_COL);
    st7735_draw_string(0, Y_HEADER, "SUPPLY + SINK", HDR_COL, BG_COL, UI_SCALE);
    st7735_fill_rect(0, Y_SEP1, st7735_width(), 1, SEP_COL);
    st7735_fill_rect(0, Y_SEP2, st7735_width(), 1, SEP_COL);
    st7735_fill_rect((uint8_t)(HALF-1),(uint8_t)(Y_SEP1+1),1,(uint8_t)(Y_SEP2-Y_SEP1-1),SEP_COL);
    /* Left labels */
    for (uint8_t i=0;i<DAC_NUM_VIRT;i++)
        st7735_draw_string(0,virt_row_y[i],virt_labels[i],FG_COL,BG_COL,UI_SCALE);
    st7735_draw_string(0,    Y_ROW3, "Tcore",  FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(0,    Y_ROW4, "Tfan:",  FG_COL, BG_COL, UI_SCALE);
    /* Right labels */
    st7735_draw_string(HALF, Y_ROW0, "Vout:",  FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW1, "Iout:",  FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW2, "Pout:",  FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW3, "Psink",  FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW4, "Isink",  FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW5, "Vin: ",  FG_COL, BG_COL, UI_SCALE);
    /* Footer */
    st7735_draw_string(FT_SRC_LBL_X, Y_FOOT0, "source",    FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(FT_SNK_LBL_X, Y_FOOT0, "sink",      FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(FT_STEP_LBL_X,Y_FOOT1, "step",      FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(FT_BL_LBL_X,  Y_FOOT1, "backlight", FG_COL, BG_COL, UI_SCALE);
    for (uint8_t i=0;i<SS_NUM_F;i++) field_invalidate(ss_fields[i]);
}

void draw_supply_sink_update(const adc_bundle_t &adc, int32_t mult)
{
    char buf[FIELD_BUF];
    ss_update_labels();

    int32_t vset_d  = setpt_disp(g_dac_cnt[DAC_CH_C],g_cal.vset_gain, g_cal.vset_div, g_cal.vset_off);
    int32_t iset_d  = setpt_disp(g_dac_cnt[DAC_CH_D],g_cal.iset_gain, g_cal.iset_div, g_cal.iset_off);
    int32_t isink_d = setpt_disp(g_dac_cnt[DAC_CH_A],g_cal.isink_gain,g_cal.isink_div,g_cal.isink_off);

    uint16_t sv = ss_fields[SS_VSET].fg, si = ss_fields[SS_ISINK_SET].fg;
    if (g_pout_limiting)  ss_fields[SS_VSET].fg      = WARN_COL;
    if (g_psink_limiting) ss_fields[SS_ISINK_SET].fg = WARN_COL;

    fmt_fp(buf, vset_d,  3, "V"); update_field(ss_fields[SS_VSET],      buf);
    fmt_fp(buf, iset_d,  3, "A"); update_field(ss_fields[SS_ISET],      buf);
    fmt_fp(buf, isink_d, 3, "A"); update_field(ss_fields[SS_ISINK_SET], buf);

    if (g_pout_limiting)  { ss_fields[SS_VSET].fg      = sv; field_invalidate(ss_fields[SS_VSET]);      }
    if (g_psink_limiting) { ss_fields[SS_ISINK_SET].fg = si; field_invalidate(ss_fields[SS_ISINK_SET]); }

    /* Left: temperatures */
    fmt_temp(buf, adc.tcore_mC);            update_field(ss_fields[SS_TCORE],      buf);
    fmt_temp(buf, adc.tfan_mC);             update_field(ss_fields[SS_TFAN],       buf);

    /* Right: measurements — Vin uses 2 decimal places to show XX.XXV */
    fmt_fp(buf, adc.vout_mV,      3, "V"); update_field(ss_fields[SS_VOUT],       buf);
    fmt_fp(buf, adc.iout_mA,      3, "A"); update_field(ss_fields[SS_IOUT],       buf);
    fmt_fp(buf, adc.pout_mW,      3, "W"); update_field(ss_fields[SS_POUT],       buf);
    fmt_fp(buf, adc.psink_mW,     3, "W"); update_field(ss_fields[SS_PSINK],      buf);
    fmt_fp(buf, adc.hii_mA,       3, "A"); update_field(ss_fields[SS_ISINK_MEAS], buf);
    fmt_fp(buf, adc.vin_mV,       2, "V"); update_field(ss_fields[SS_VIN],        buf);

    static bool prev_vi=false, prev_is=false;
    if (g_vi_enabled    != prev_vi) { ss_fields[SS_STATUS_VI].fg = g_vi_enabled    ? ON_COL:OFF_COL; field_invalidate(ss_fields[SS_STATUS_VI]); prev_vi=g_vi_enabled;    }
    if (g_isink_enabled != prev_is) { ss_fields[SS_STATUS_IS].fg = g_isink_enabled ? ON_COL:OFF_COL; field_invalidate(ss_fields[SS_STATUS_IS]); prev_is=g_isink_enabled; }
    update_field(ss_fields[SS_STATUS_VI], g_vi_enabled    ? "ON " : "OFF");
    update_field(ss_fields[SS_STATUS_IS], g_isink_enabled ? "ON " : "OFF");

    snprintf(buf, sizeof(buf), "x%ld",  (long)mult);           update_field(ss_fields[SS_STEP], buf);
    snprintf(buf, sizeof(buf), "%3d%%", (int)g_cal.backlight); update_field(ss_fields[SS_BL],   buf);
}

/* ═══════════════════════════════════════════════════════════════════════
 * MODE_SUPPLY  –  large Vout/Iout
 * ═══════════════════════════════════════════════════════════════════════ */
#define SP_BIG_SCALE   3
#define SP_BIG_CH      ((uint8_t)(FONT_H * SP_BIG_SCALE))
#define SP_BIG_CHARS   7
#define SP_BIG_BUF     (SP_BIG_CHARS + 1)

#define SP_Y_SETPTS    Y_ROW0
#define SP_Y_VOUT      ((uint8_t)(Y_SEP1 + 18u))
#define SP_Y_IOUT      ((uint8_t)(SP_Y_VOUT + SP_BIG_CH + 4u))
#define SP_Y_SMALL     ((uint8_t)(SP_Y_IOUT + SP_BIG_CH + 4u))
#define SP_Y_SEP2      ((uint8_t)(SP_Y_SMALL + ROW_STEP + 2u))
#define SP_Y_FOOT0     ((uint8_t)(SP_Y_SEP2 + 3u))
#define SP_Y_FOOT1     ((uint8_t)(SP_Y_FOOT0 + ROW_STEP))

enum SP_FID {
    SP_VSET=0, SP_ISET,
    SP_VOUT_BIG, SP_IOUT_BIG,
    SP_POUT, SP_TFAN, SP_VIN,
    SP_STATUS_VI, SP_STEP, SP_BL,
    SP_NUM_F
};

static field_t sp_fields[SP_NUM_F] = {
    {VAL_X_L,              SP_Y_SETPTS, VAL_CHARS,   ACT_COL, BG_COL, {}},
    {(uint8_t)(HALF+LABEL_W), SP_Y_SETPTS, VAL_CHARS, FG_COL, BG_COL, {}},
    {0u, SP_Y_VOUT, SP_BIG_CHARS, FG_COL, BG_COL, {}},
    {0u, SP_Y_IOUT, SP_BIG_CHARS, FG_COL, BG_COL, {}},
    {0u,   SP_Y_SMALL, VAL_CHARS, FG_COL, BG_COL, {}},
    {55u,  SP_Y_SMALL, VAL_CHARS, FG_COL, BG_COL, {}},
    {110u, SP_Y_SMALL, VAL_CHARS, FG_COL, BG_COL, {}},
    {FT_SRC_VAL_X, SP_Y_FOOT0, FT_SRC_CHARS,  OFF_COL, BG_COL, {}},
    {FT_STEP_VAL_X,SP_Y_FOOT1, FT_STEP_CHARS, FG_COL,  BG_COL, {}},
    {FT_BL_VAL_X,  SP_Y_FOOT1, FT_BL_CHARS,   FG_COL,  BG_COL, {}},
};

static void sp_update_active(void)
{
    static uint8_t prev_act = 0xFFu;
    static bool    prev_vi  = false;
    if (g_active_ch == prev_act && g_vi_enabled == prev_vi) return;
    /* Only Vset/Iset selectable in supply mode */
    const struct { uint8_t vch; uint8_t fid; const char *lbl; uint8_t x; } rows[2] = {
        { DAC_VIRT_VSET, SP_VSET, "Vset:", 0u },
        { DAC_VIRT_ISET, SP_ISET, "Iset:", HALF },
    };
    for (int i = 0; i < 2; i++) {
        bool     act = (virt_arr_idx(rows[i].vch) == virt_arr_idx(g_active_ch));
        uint16_t col = act ? ACT_COL : (g_vi_enabled ? FG_COL : DIS_COL);
        st7735_draw_string(rows[i].x, SP_Y_SETPTS, rows[i].lbl, col, BG_COL, UI_SCALE);
        sp_fields[rows[i].fid].fg = col;
        field_invalidate(sp_fields[rows[i].fid]);
    }
    prev_act = g_active_ch; prev_vi = g_vi_enabled;
}

static void sp_draw_big(field_t &f, int32_t val_milli, uint8_t dec,
                         const char *unit, uint8_t scale)
{
    char raw[SP_BIG_BUF], padded[SP_BIG_BUF];
    bool neg = (val_milli < 0); int32_t v = neg ? -val_milli : val_milli;
    int32_t div = 1; for (uint8_t i = 0; i < dec; i++) div *= 10;
    char fmt[24];
    snprintf(fmt, sizeof(fmt), "%%c%%ld.%%0%dld%%s", (int)dec);
    snprintf(raw, sizeof(raw), fmt, neg?'-':' ', (long)(v/div), (long)(v%div), unit);
    uint8_t len = (uint8_t)strlen(raw);
    if (len > SP_BIG_CHARS) len = SP_BIG_CHARS;
    memcpy(padded, raw, len);
    for (uint8_t i = len; i < SP_BIG_CHARS; i++) padded[i] = ' ';
    padded[SP_BIG_CHARS] = '\0';
    if (memcmp(padded, f.prev, SP_BIG_CHARS + 1) == 0) return;
    st7735_draw_string(f.x, f.y, padded, f.fg, BG_COL, scale);
    memcpy(f.prev, padded, SP_BIG_CHARS + 1);
}

void draw_supply_static(void)
{
    st7735_fill_screen(BG_COL);
    st7735_draw_string(0, Y_HEADER, "SUPPLY", HDR_COL, BG_COL, UI_SCALE);
    st7735_fill_rect(0, Y_SEP1, st7735_width(), 1, SEP_COL);
    st7735_draw_string(0,    SP_Y_SETPTS, "Vset:", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, SP_Y_SETPTS, "Iset:", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(0u,   SP_Y_SMALL, "Pout:",  FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(55u,  SP_Y_SMALL, "Tfan:",  FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(110u, SP_Y_SMALL, "Vin:",   FG_COL, BG_COL, UI_SCALE);
    st7735_fill_rect(0, SP_Y_SEP2, st7735_width(), 1, SEP_COL);
    st7735_draw_string(FT_SRC_LBL_X, SP_Y_FOOT0, "source",    FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(FT_STEP_LBL_X,SP_Y_FOOT1, "step",      FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(FT_BL_LBL_X,  SP_Y_FOOT1, "backlight", FG_COL, BG_COL, UI_SCALE);
    for (uint8_t i=0;i<SP_NUM_F;i++) field_invalidate(sp_fields[i]);
}

void draw_supply_update(const adc_bundle_t &adc, int32_t mult)
{
    char buf[FIELD_BUF];
    sp_update_active();

    uint16_t sv = sp_fields[SP_VSET].fg;
    if (g_pout_limiting) sp_fields[SP_VSET].fg = WARN_COL;
    int32_t vset_d = setpt_disp(g_dac_cnt[DAC_CH_C],g_cal.vset_gain,g_cal.vset_div,g_cal.vset_off);
    int32_t iset_d = setpt_disp(g_dac_cnt[DAC_CH_D],g_cal.iset_gain,g_cal.iset_div,g_cal.iset_off);
    fmt_fp(buf, vset_d, 3, "V"); update_field(sp_fields[SP_VSET], buf);
    fmt_fp(buf, iset_d, 3, "A"); update_field(sp_fields[SP_ISET], buf);
    if (g_pout_limiting) { sp_fields[SP_VSET].fg = sv; field_invalidate(sp_fields[SP_VSET]); }

    sp_draw_big(sp_fields[SP_VOUT_BIG], adc.vout_mV,      3, "V", SP_BIG_SCALE);
    sp_draw_big(sp_fields[SP_IOUT_BIG], adc.iout_mA, 3, "A", SP_BIG_SCALE);

    fmt_fp(buf, adc.pout_mW,  3, "W"); update_field(sp_fields[SP_POUT], buf);
    fmt_temp(buf, adc.tfan_mC);         update_field(sp_fields[SP_TFAN], buf);
    fmt_fp(buf, adc.vin_mV,   3, "V"); update_field(sp_fields[SP_VIN],  buf);

    static bool prev_vi = false;
    if (g_vi_enabled != prev_vi) {
        sp_fields[SP_STATUS_VI].fg = g_vi_enabled ? ON_COL : OFF_COL;
        field_invalidate(sp_fields[SP_STATUS_VI]); prev_vi = g_vi_enabled;
    }
    update_field(sp_fields[SP_STATUS_VI], g_vi_enabled ? "ON " : "OFF");
    snprintf(buf, sizeof(buf), "x%ld",  (long)mult);           update_field(sp_fields[SP_STEP], buf);
    snprintf(buf, sizeof(buf), "%3d%%", (int)g_cal.backlight); update_field(sp_fields[SP_BL],   buf);
}

/* ═══════════════════════════════════════════════════════════════════════
 * MODE_CONTINUITY
 * ═══════════════════════════════════════════════════════════════════════ */
int32_t  g_cont_dac[2]  = {0, 0};
int32_t  g_cont_thr     = 200;
uint8_t  g_cont_active  = 0;

#define CT_Y_RESULT   ((uint8_t)(Y_SEP2 + 5u))
#define CT_Y_FOOT0    ((uint8_t)(CT_Y_RESULT + ROW_STEP + 2u))
#define CT_Y_FOOT1    ((uint8_t)(CT_Y_FOOT0 + ROW_STEP))

enum CT_FID {
    CT_VSET=0, CT_ISET, CT_THR,
    CT_VOUT, CT_IOUT,
    CT_RESULT,
    CT_STATUS_VI, CT_STEP, CT_BL,
    CT_NUM_F
};

static field_t ct_fields[CT_NUM_F] = {
    {VAL_X_L, Y_ROW0, VAL_CHARS, ACT_COL, BG_COL, {}},
    {VAL_X_L, Y_ROW1, VAL_CHARS, FG_COL,  BG_COL, {}},
    {VAL_X_L, Y_ROW2, VAL_CHARS, FG_COL,  BG_COL, {}},
    {VAL_X_R, Y_ROW0, VAL_CHARS, FG_COL,  BG_COL, {}},
    {VAL_X_R, Y_ROW1, VAL_CHARS, FG_COL,  BG_COL, {}},
    {0u, CT_Y_RESULT, VAL_CHARS, ON_COL,  BG_COL, {}},
    {FT_SRC_VAL_X, CT_Y_FOOT0, FT_SRC_CHARS,  OFF_COL, BG_COL, {}},
    {FT_STEP_VAL_X,CT_Y_FOOT1, FT_STEP_CHARS, FG_COL,  BG_COL, {}},
    {FT_BL_VAL_X,  CT_Y_FOOT1, FT_BL_CHARS,   FG_COL,  BG_COL, {}},
};

static const char *cont_ch_labels[CONT_NUM_CH] = { "Vset:", "Iset:", "Thr: " };

static void ct_update_labels(void)
{
    static uint8_t prev = 0xFFu;
    if (g_cont_active == prev) return;
    for (uint8_t i = 0; i < CONT_NUM_CH; i++) {
        uint16_t col = (i == g_cont_active) ? ACT_COL : FG_COL;
        st7735_draw_string(0,(uint8_t)(Y_ROW0+i*ROW_STEP),cont_ch_labels[i],col,BG_COL,UI_SCALE);
        ct_fields[CT_VSET + i].fg = col;
        field_invalidate(ct_fields[CT_VSET + i]);
    }
    prev = g_cont_active;
}

void draw_continuity_static(void)
{
    st7735_fill_screen(BG_COL);
    st7735_draw_string(0, Y_HEADER, "CONTINUITY", HDR_COL, BG_COL, UI_SCALE);
    st7735_fill_rect(0, Y_SEP1, st7735_width(), 1, SEP_COL);
    st7735_fill_rect(0, Y_SEP2, st7735_width(), 1, SEP_COL);
    st7735_fill_rect((uint8_t)(HALF-1),(uint8_t)(Y_SEP1+1),1,(uint8_t)(Y_SEP2-Y_SEP1-1),SEP_COL);
    for (uint8_t i=0;i<CONT_NUM_CH;i++)
        st7735_draw_string(0,(uint8_t)(Y_ROW0+i*ROW_STEP),cont_ch_labels[i],FG_COL,BG_COL,UI_SCALE);
    st7735_draw_string(HALF, Y_ROW0, "Vout:", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(HALF, Y_ROW1, "Iout:", FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(FT_SRC_LBL_X, CT_Y_FOOT0, "source",    FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(FT_STEP_LBL_X,CT_Y_FOOT1, "step",      FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(FT_BL_LBL_X,  CT_Y_FOOT1, "backlight", FG_COL, BG_COL, UI_SCALE);
    for (uint8_t i=0;i<CT_NUM_F;i++) field_invalidate(ct_fields[i]);
}

void draw_continuity_update(const adc_bundle_t &adc, int32_t mult)
{
    char buf[FIELD_BUF];
    ct_update_labels();

    int32_t vset_d = setpt_disp(g_cont_dac[CONT_VSET],g_cal.vset_gain,g_cal.vset_div,g_cal.vset_off);
    int32_t iset_d = setpt_disp(g_cont_dac[CONT_ISET],g_cal.iset_gain,g_cal.iset_div,g_cal.iset_off);
    int32_t thr_d  = setpt_disp(g_cont_thr,g_cal.vout_gain,g_cal.vout_div,g_cal.vout_off);

    fmt_fp(buf, vset_d, 3, "V"); update_field(ct_fields[CT_VSET], buf);
    fmt_fp(buf, iset_d, 3, "A"); update_field(ct_fields[CT_ISET], buf);
    fmt_fp(buf, thr_d,  3, "V"); update_field(ct_fields[CT_THR],  buf);
    fmt_fp(buf, adc.vout_mV,      3, "V"); update_field(ct_fields[CT_VOUT], buf);
    fmt_fp(buf, adc.iout_mA, 3, "A"); update_field(ct_fields[CT_IOUT], buf);

    bool below = (adc.vout_mV < thr_d);
    uint16_t res_col = below ? OFF_COL : ON_COL;
    if (ct_fields[CT_RESULT].fg != res_col) {
        ct_fields[CT_RESULT].fg = res_col;
        field_invalidate(ct_fields[CT_RESULT]);
    }
    update_field(ct_fields[CT_RESULT], below ? "CLOSED" : "OPEN  ");

    static bool prev_vi = false;
    if (g_vi_enabled != prev_vi) {
        ct_fields[CT_STATUS_VI].fg = g_vi_enabled ? ON_COL : OFF_COL;
        field_invalidate(ct_fields[CT_STATUS_VI]); prev_vi = g_vi_enabled;
    }
    update_field(ct_fields[CT_STATUS_VI], g_vi_enabled ? "ON " : "OFF");
    snprintf(buf, sizeof(buf), "x%ld",  (long)mult);           update_field(ct_fields[CT_STEP], buf);
    snprintf(buf, sizeof(buf), "%3d%%", (int)g_cal.backlight); update_field(ct_fields[CT_BL],   buf);
}

/* ═══════════════════════════════════════════════════════════════════════
 * On-screen message system
 * ═══════════════════════════════════════════════════════════════════════
 * One overlay bar at Y_MSG (bottom row) across the full screen width (26 chars).
 * Higher severity replaces lower. timeout_ms=0 means sticky.
 * IR received codes are posted as INFO messages (3 s timeout).
 */
#define MSG_MAX_LEN  26u

static msg_severity_t g_msg_sev       = MSG_NONE;
static char           g_msg_text[MSG_MAX_LEN + 1] = {};
static uint64_t       g_msg_expire_us  = 0u;
static bool           g_msg_drawn      = false;

static uint16_t msg_colour(msg_severity_t s)
{
    switch (s) {
    case MSG_INFO:  return HDR_COL;   /* cyan   */
    case MSG_WARN:  return WARN_COL;  /* orange */
    case MSG_ERROR: return OFF_COL;   /* red    */
    default:        return FG_COL;
    }
}

void msg_post(msg_severity_t sev, const char *text, uint32_t timeout_ms)
{
    if (sev < g_msg_sev) return;   /* don't downgrade severity */
    g_msg_sev = sev;
    uint8_t i = 0;
    while (i < MSG_MAX_LEN && text[i]) { g_msg_text[i] = text[i]; i++; }
    while (i < MSG_MAX_LEN) g_msg_text[i++] = ' ';   /* pad to erase old text */
    g_msg_text[MSG_MAX_LEN] = '\0';
    g_msg_expire_us = timeout_ms ? (time_us_64() + (uint64_t)timeout_ms * 1000u) : 0u;
    g_msg_drawn = false;
}

void msg_clear(void)
{
    if (g_msg_sev == MSG_NONE) return;
    g_msg_sev   = MSG_NONE;
    g_msg_drawn = false;
}

void msg_update(void)
{
    /* IR received: post once as INFO with a real timeout, then let it expire normally */
    if (g_ir_show_until != 0u) {
        uint64_t now = time_us_64();
        if (now >= g_ir_show_until) {
            g_ir_show_until = 0u;
            if (g_msg_sev == MSG_INFO) msg_clear();
        } else {
            static uint32_t last_ir_posted = 0xFFFFFFFFu;
            if (g_ir_code != last_ir_posted) {
                char buf[MSG_MAX_LEN + 1];
                uint8_t cmd = (uint8_t)((g_ir_code >> 16) & 0xFF);
                snprintf(buf, sizeof(buf), "IR: cmd=0x%02X%s",
                         cmd, g_ir_repeat ? " (repeat)" : "");
                uint32_t remaining_ms = (uint32_t)((g_ir_show_until - now) / 1000u);
                msg_post(MSG_INFO, buf, remaining_ms);
                last_ir_posted = g_ir_code;
            }
        }
    }

    /* Check timeout expiry for all messages */
    if (g_msg_sev != MSG_NONE && g_msg_expire_us != 0u &&
        time_us_64() >= g_msg_expire_us) {
        g_msg_sev   = MSG_NONE;
        g_msg_drawn = false;
    }

    if (g_msg_sev == MSG_NONE) {
        if (g_msg_drawn) {
            st7735_fill_rect(0, Y_MSG, st7735_width(), CH_PX, BG_COL);
            g_msg_drawn = false;
        }
        return;
    }

    if (!g_msg_drawn) {
        st7735_fill_rect(0, Y_MSG, st7735_width(), CH_PX, BG_COL);
        st7735_draw_string(0, Y_MSG, g_msg_text, msg_colour(g_msg_sev), BG_COL, UI_SCALE);
        g_msg_drawn = true;
    }
}

void ir_overlay_update(void) { /* kept for link compat — functionality moved to msg_update */ }