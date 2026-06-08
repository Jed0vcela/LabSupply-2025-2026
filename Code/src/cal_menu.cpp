#include "cal_menu.h"
#include "display.h"
#include "peripherals.h"
#include "psu_control.h"
#include "mcp23s17.h"
#include "st7735.h"
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"

/* ── Cal item table ───────────────────────────────────────────────────── */
typedef enum { CI_ACT, CI_INT, CI_BEEP, CI_BL, CI_BOOL, CI_WATTS, CI_STEPMODE } cal_item_type_t;

typedef struct {
    const char       *label;
    cal_item_type_t   type;
    int32_t          *ptr;
    int32_t           step, min_val, max_val, def_val;
} cal_item_t;

/* Safe int32 backing for uint8_t flash fields */
static int32_t g_cal_beep_i32     = 0;
static int32_t g_cal_bl_i32       = 0;
static int32_t g_cal_plim_i32     = 0;
static int32_t g_cal_stepmode_i32 = 0;

static void cal_i32_to_cal(void)
{
    g_cal.beep_mode  = (uint8_t)g_cal_beep_i32;
    g_cal.backlight  = (uint8_t)g_cal_bl_i32;
    g_cal.plim_enabled = (uint8_t)g_cal_plim_i32;
    g_cal.step_mode  = (uint8_t)g_cal_stepmode_i32;
}
static void cal_cal_to_i32(void)
{
    g_cal_beep_i32     = (int32_t)g_cal.beep_mode;
    g_cal_bl_i32       = (int32_t)g_cal.backlight;
    g_cal_plim_i32     = (int32_t)g_cal.plim_enabled;
    g_cal_stepmode_i32 = (int32_t)g_cal.step_mode;
}

#define CAL_ITEMS_MAX  28
static cal_item_t g_cal_items[CAL_ITEMS_MAX];
static uint8_t    g_cal_item_count = 0;

static void cal_add(const char *lbl, cal_item_type_t t, int32_t *ptr,
                    int32_t step, int32_t mn, int32_t mx, int32_t def)
{
    if (g_cal_item_count >= CAL_ITEMS_MAX) return;
    g_cal_items[g_cal_item_count++] = {lbl, t, ptr, step, mn, mx, def};
}

static void cal_menu_build(void)
{
    cal_cal_to_i32();
    g_cal_item_count = 0;
    /* ── Actions ─────────────────────────────────────────── */
    cal_add("Exit",         CI_ACT,      NULL,                0, 0, 0,  0);
    cal_add("Save+Exit",    CI_ACT,      NULL,                0, 0, 0,  0);
    /* ── Power limiting ──────────────────────────────────── */
    cal_add("Plim on",      CI_BOOL,     &g_cal_plim_i32,     1, 0, 1,  (int32_t)CAL_DEFAULTS.plim_enabled);
    cal_add("Pout max W",   CI_WATTS,    &g_cal.pout_max_mW,  1, 0, 200, CAL_DEFAULTS.pout_max_mW/1000);
    cal_add("Psink max W",  CI_WATTS,    &g_cal.psink_max_mW, 1, 0, 200, CAL_DEFAULTS.psink_max_mW/1000);
    cal_add("Vin min V",    CI_INT,      &g_cal.vin_min_mV,   100, 0, 60000, CAL_DEFAULTS.vin_min_mV);
    /* ── UI settings ─────────────────────────────────────── */
    cal_add("Beep",         CI_BEEP,     &g_cal_beep_i32,     1, 0, 2,  (int32_t)CAL_DEFAULTS.beep_mode);
    cal_add("Backlight",    CI_BL,       &g_cal_bl_i32,       1, 1, 100,(int32_t)CAL_DEFAULTS.backlight);
    cal_add("Step mode",    CI_STEPMODE, &g_cal_stepmode_i32, 1, 0, 1,  (int32_t)CAL_DEFAULTS.step_mode);
    /* ── ADC calibration ─────────────────────────────────── */
    cal_add("Vout gain",    CI_INT, &g_cal.vout_gain,    1, 1,    99999, CAL_DEFAULTS.vout_gain);
    cal_add("Vout offset",  CI_INT, &g_cal.vout_off,     1, -9999,9999,  CAL_DEFAULTS.vout_off);
    cal_add("Vin gain",     CI_INT, &g_cal.vin_gain,     1, 1,    99999, CAL_DEFAULTS.vin_gain);
    cal_add("Vin offset",   CI_INT, &g_cal.vin_off,      1, -9999,9999,  CAL_DEFAULTS.vin_off);
    cal_add("HiI gain",     CI_INT, &g_cal.hii_gain,     1, 1,    99999, CAL_DEFAULTS.hii_gain);
    cal_add("HiI offset",   CI_INT, &g_cal.hii_off,      1, -9999,9999,  CAL_DEFAULTS.hii_off);
    cal_add("Iout gain",    CI_INT, &g_cal.iout_gain,    1, 1,    99999, CAL_DEFAULTS.iout_gain);
    cal_add("Iout offset",  CI_INT, &g_cal.iout_off,     1, -9999,9999,  CAL_DEFAULTS.iout_off);
    /* ── Setpoint display calibration ────────────────────── */
    cal_add("Vset gain",    CI_INT, &g_cal.vset_gain,    1, 1,    99999, CAL_DEFAULTS.vset_gain);
    cal_add("Vset offset",  CI_INT, &g_cal.vset_off,     1, -9999,9999,  CAL_DEFAULTS.vset_off);
    cal_add("Iset gain",    CI_INT, &g_cal.iset_gain,    1, 1,    99999, CAL_DEFAULTS.iset_gain);
    cal_add("Iset offset",  CI_INT, &g_cal.iset_off,     1, -9999,9999,  CAL_DEFAULTS.iset_off);
    cal_add("Isink gain",   CI_INT, &g_cal.isink_gain,   1, 1,    99999, CAL_DEFAULTS.isink_gain);
    cal_add("Isink offset", CI_INT, &g_cal.isink_off,    1, -9999,9999,  CAL_DEFAULTS.isink_off);
}

/* ── Display constants ────────────────────────────────────────────────── */
#define CAL_ROWS_VISIBLE  10u
#define CAL_NAME_X        0u
#define CAL_NAME_CHARS    9u
#define CAL_VAL_X         60u
#define CAL_VAL_CHARS     6u
#define CAL_DEF_X         108u
#define CAL_DEF_CHARS     6u

static void cal_pad(char *dst, const char *src, uint8_t w)
{
    uint8_t i = 0;
    while (i < w && src[i]) { dst[i] = src[i]; i++; }
    while (i < w)            { dst[i++] = ' '; }
    dst[w] = '\0';
}

static void cal_fmt_value(char *buf, size_t vsz, const cal_item_t &it, int32_t v)
{
    buf[0] = '\0';
    switch (it.type) {
    case CI_ACT:      break;
    case CI_INT:      snprintf(buf, vsz, "%ld", (long)v); break;
    case CI_BOOL:     strncpy(buf, v ? "on " : "off", vsz); break;
    case CI_WATTS:    snprintf(buf, vsz, "%ldW", (long)(v/1000)); break;  /* stored mW, shown W */
    case CI_STEPMODE: strncpy(buf, v ? "toggle" : "moment", vsz); break;
    case CI_BEEP:
        switch ((beep_mode_t)(uint8_t)v) {
        case BEEP_ALL:     strncpy(buf, "ALL",   vsz); break;
        case BEEP_SPECIAL: strncpy(buf, "ALARM", vsz); break;  /* was "SPEC" */
        case BEEP_MUTE:    strncpy(buf, "MUTE",  vsz); break;
        }
        break;
    case CI_BL: snprintf(buf, vsz, "%d%%", (int)(uint8_t)v); break;
    }
}

static void cal_draw_row(uint8_t screen_row, uint8_t item_idx,
                          bool in_left, bool in_right)
{
    if (screen_row >= CAL_ROWS_VISIBLE) return;
    uint8_t y = (uint8_t)(Y_SEP1 + 2u + screen_row * ROW_STEP);
    const cal_item_t &it = g_cal_items[item_idx];

    char nb[CAL_NAME_CHARS+1], vb[CAL_VAL_CHARS+1], db[CAL_DEF_CHARS+1];
    char vraw[CAL_VAL_CHARS+1], draw[CAL_DEF_CHARS+1];

    cal_pad(nb, it.label, CAL_NAME_CHARS);
    cal_fmt_value(vraw, sizeof(vraw), it, it.ptr ? *it.ptr : 0);
    cal_pad(vb, vraw, CAL_VAL_CHARS);
    cal_fmt_value(draw, sizeof(draw), it, it.def_val);
    cal_pad(db, draw, CAL_DEF_CHARS);

    st7735_draw_string(CAL_NAME_X, y, nb, in_left  ? CAL_SEL : FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(CAL_VAL_X,  y, vb, in_right ? CAL_EDT : FG_COL, BG_COL, UI_SCALE);
    st7735_draw_string(CAL_DEF_X,  y, db, DIS_COL,                     BG_COL, UI_SCALE);
}

static void cal_draw_update(uint8_t scroll, uint8_t sel, bool in_right,
                             uint8_t prev_sel)
{
    for (uint8_t r = 0; r < CAL_ROWS_VISIBLE; r++) {
        uint8_t idx = (uint8_t)(scroll + r);
        if (idx >= g_cal_item_count) {
            st7735_fill_rect(0, (uint8_t)(Y_SEP1+2u+r*ROW_STEP),
                             st7735_width(), CH_PX, BG_COL);
            continue;
        }
        if (prev_sel != 0xFF && idx != prev_sel && idx != sel) continue;
        cal_draw_row(r, idx, (idx==sel && !in_right), (idx==sel && in_right));
    }
}

static void cal_draw_full(uint8_t scroll, uint8_t sel, bool in_right)
{
    st7735_fill_screen(BG_COL);
    st7735_draw_string(CAL_NAME_X, Y_HEADER, "CAL MENU", CAL_HDR, BG_COL, UI_SCALE);
    st7735_draw_string(CAL_VAL_X,  Y_HEADER, "value",   DIS_COL, BG_COL, UI_SCALE);
    st7735_draw_string(CAL_DEF_X,  Y_HEADER, "default", DIS_COL, BG_COL, UI_SCALE);
    st7735_fill_rect(0, Y_SEP1, st7735_width(), 1, SEP_COL);
    st7735_fill_rect(0, Y_SEP2, st7735_width(), 1, SEP_COL);
    st7735_fill_rect((uint8_t)(CAL_VAL_X-2u), (uint8_t)(Y_SEP1+1u),
                     1, (uint8_t)(Y_SEP2-Y_SEP1-1u), SEP_COL);
    st7735_fill_rect((uint8_t)(CAL_DEF_X-2u), (uint8_t)(Y_SEP1+1u),
                     1, (uint8_t)(Y_SEP2-Y_SEP1-1u), SEP_COL);
    cal_draw_update(scroll, sel, in_right, 0xFF);
}

/* ── Menu run ─────────────────────────────────────────────────────────── */
bool cal_menu_run(draw_static_fn_t redraw_fn)
{
    cal_menu_build();
    uint8_t scroll = 0, sel = 0;
    bool in_right = false;

    cal_draw_full(scroll, sel, in_right);
    enc_consume();

    /* Wait for entry button(s) to be released */
    {
        uint8_t ga, gb;
        do {
            sleep_ms(10);
            mcp23s17_read_inputs(&ga, &gb);
        } while (!(ga & (1u<<1)) || !(gb & (1u<<5)));
        sleep_ms(30);
    }

    uint8_t gpa_prev, gpb_prev;
    mcp23s17_read_inputs(&gpa_prev, &gpb_prev);
    enc_consume();

    while (true) {
        sleep_ms(4);
        uint8_t gpa, gpb;
        mcp23s17_read_inputs(&gpa, &gpb);
        uint8_t gpa_fell = (uint8_t)(gpa_prev & ~gpa);
        uint8_t gpb_fell = (uint8_t)(gpb_prev & ~gpb);
        gpa_prev = gpa; gpb_prev = gpb;
        int32_t delta = enc_consume();

        if (gpa_fell || gpb_fell) beep();

        /* GPB5 falling edge: exit without saving.
         * Entry wait ensures the button was released before loop started,
         * so this is always a deliberate new press. */
        if (gpb_fell & (1u<<5)) { redraw_fn(); return false; }

        /* GPA1: execute action or toggle column */
        if (gpa_fell & (1u<<1)) {
            cal_item_t &it = g_cal_items[sel];
            if (!in_right) {
                if (it.type == CI_ACT) {
                    if (sel == 0) { redraw_fn(); return false; }
                    if (sel == 1) {
                        cal_i32_to_cal();   /* syncs beep, backlight, plim_enabled */
                        for (uint8_t i = 0; i < DAC_NUM_CH; i++)
                            g_cal.dac_cnt[i] = g_dac_cnt[i];
                        /* Also save continuity state */
                        g_cal.cont.vset_cnt = g_cont_dac[0];
                        g_cal.cont.iset_cnt = g_cont_dac[1];
                        g_cal.cont.thr_cnt  = g_cont_thr;
                        flash_save();
                        redraw_fn();
                        return true;
                    }
                } else {
                    uint8_t prev = sel;
                    in_right = true;
                    cal_draw_update(scroll, sel, in_right, prev);
                }
            } else {
                in_right = false;
                cal_apply();
                cal_draw_update(scroll, sel, in_right, sel);
            }
        }

        if (delta == 0) continue;

        if (!in_right) {
            uint8_t prev = sel;
            int32_t ns = (int32_t)sel + delta;
            if (ns < 0) ns = 0;
            if (ns >= (int32_t)g_cal_item_count) ns = (int32_t)g_cal_item_count - 1;
            sel = (uint8_t)ns;
            bool win_changed = false;
            if (sel < scroll) { scroll = sel; win_changed = true; }
            else if (sel >= scroll + CAL_ROWS_VISIBLE) {
                scroll = (uint8_t)(sel - CAL_ROWS_VISIBLE + 1u); win_changed = true;
            }
            if (win_changed) cal_draw_full(scroll, sel, in_right);
            else             cal_draw_update(scroll, sel, in_right, prev);
        } else {
            cal_item_t &it = g_cal_items[sel];
            switch (it.type) {
            case CI_INT: {
                int32_t nv = *it.ptr + delta * it.step;
                if (nv < it.min_val) nv = it.min_val;
                if (nv > it.max_val) nv = it.max_val;
                *it.ptr = nv; break;
            }
            case CI_WATTS: {
                /* Stored as mW, edited in whole W (step = 1 W = 1000 mW) */
                int32_t w  = *it.ptr / 1000 + delta;
                if (w < it.min_val) w = it.min_val;
                if (w > it.max_val) w = it.max_val;
                *it.ptr = w * 1000; break;
            }
            case CI_BOOL:
            case CI_STEPMODE: {
                *it.ptr = (*it.ptr) ? 0 : 1; break;
            }
            case CI_BEEP: {
                int32_t m = ((*it.ptr) + delta % 3 + 3) % 3;
                *it.ptr = m; break;
            }
            case CI_BL: {
                int32_t nv = *it.ptr + delta;
                if (nv < 1) nv=1; if (nv>100) nv=100;
                *it.ptr = nv;
                backlight_set((uint8_t)nv);
                break;
            }
            default: break;
            }
            cal_draw_row((uint8_t)(sel-scroll), sel, false, true);
        }
    }
}