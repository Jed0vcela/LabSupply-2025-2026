/**
 * main.cpp  –  Lab power supply controller  v3
 *
 * ── Mode overview ───────────────────────────────────────────────────────
 *  MODE_SUPPLY_SINK  Full display: supply + current sink
 *  MODE_SUPPLY       Supply only, large Vout/Iout readout
 *  MODE_CONTINUITY   Continuity/resistance checker with threshold beep
 *
 *  GPB7 cycles through modes on every falling edge.
 *
 * ── File map ────────────────────────────────────────────────────────────
 *  psu_types.h       Shared types, constants, colours, geometry
 *  flash_store.h/.cpp  Flash persistence (cal_flash_t, g_cal)
 *  peripherals.h/.cpp  Encoder, IR, fan, beeper, backlight
 *  psu_control.h/.cpp  DAC, power limiting, button handling
 *  display.h/.cpp      All mode display functions + IR overlay
 *  cal_menu.h/.cpp     Calibration menu (blocking, 3-column)
 *  main.cpp            Mode state machine + main loop  ← this file
 */

#include "psu_types.h"
#include "flash_store.h"
#include "peripherals.h"
#include "psu_control.h"
#include "display.h"
#include "cal_menu.h"
#include "mcp4728.h"
#include "adc_measurement.h"
#include "mcp23s17.h"
#include "st7735.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

/* ── Mode state ───────────────────────────────────────────────────────── */
static psu_mode_t g_mode = MODE_SUPPLY_SINK;

/* Called whenever the mode changes to set up the static screen and
 * adjust hardware state (e.g. disable Isink in supply-only mode). */
static void mode_enter(psu_mode_t m)
{
    g_mode = m;
    switch (m) {
    case MODE_SUPPLY_SINK:
        draw_supply_sink_static();
        break;
    case MODE_SUPPLY:
        g_isink_enabled = false;      /* auto-disable Isink in supply-only mode */
        g_active_ch     = DAC_VIRT_VSET;   /* always start on Vset */
        draw_supply_static();
        break;
    case MODE_CONTINUITY:
        g_isink_enabled = false;
        draw_continuity_static();
        break;
    default:
        break;
    }
}

/* Redraw current mode's static screen (passed to cal_menu_run as callback) */
static void mode_redraw_static(void)
{
    mode_enter(g_mode);
}

/* ── Moving average instances ─────────────────────────────────────────── */
static mav_t mav_vout, mav_iout, mav_hii, mav_tfan, mav_vin, mav_tcore;

static void mav_init_all(void)
{
    mav_init(&mav_vout,  0);  mav_init(&mav_iout, 0);  mav_init(&mav_hii,   0);
    mav_init(&mav_tfan,  25000); mav_init(&mav_vin, 0); mav_init(&mav_tcore, 25000);
}

/* ── Continuity encoder apply ─────────────────────────────────────────── */
static void apply_encoder_continuity(int32_t multiplier)
{
    int32_t delta = enc_consume();
    if (delta == 0) return;
    int32_t step = (int32_t)ENC_STEP * multiplier;

    int32_t *ptr = NULL;
    switch (g_cont_active) {
    case CONT_VSET: ptr = &g_cont_dac[0]; break;
    case CONT_ISET: ptr = &g_cont_dac[1]; break;
    case CONT_THR:  ptr = &g_cont_thr;    break;
    }
    if (!ptr) return;
    int32_t cnt = *ptr + delta * step;
    if (cnt < DAC_MIN_CNT) cnt = DAC_MIN_CNT;
    if (cnt > DAC_MAX_CNT) cnt = DAC_MAX_CNT;
    *ptr = cnt;
}

/* ── Continuity DAC update ────────────────────────────────────────────── */
static inline uint16_t ctr(int32_t v)
{
    if (v <= 0) return 0u;
    if (v >= DAC_MAX_CNT) return (uint16_t)DAC_MAX_COUNT;
    return (uint16_t)v;
}

static void dac_update_continuity(mcp4728 &dac)
{
    bool c_on = g_vi_enabled && (g_cont_dac[0] != 0);
    bool d_on = g_vi_enabled && (g_cont_dac[1] != 0);
    dac.setPowerDown(1, 1, c_on?0:1, d_on?0:1);
    dac.analogWriteAll(0, 0,
        c_on ? ctr(g_cont_dac[0]) : 0,
        d_on ? ctr(g_cont_dac[1]) : 0);
    dac.ldacPulse();
    gpio_put(PIN_EN_HI, 1); gpio_put(PIN_EN_LO, 1);
    mcp23s17_set_pin(MCP_OUT_GPA7, g_vi_enabled);
    mcp23s17_set_pin(MCP_OUT_GPA5, false);
}

/* ── Continuity button handling ───────────────────────────────────────── */
static void process_buttons_continuity(uint8_t gpa, uint8_t gpb)
{
    static uint8_t gpa_prev = 0xFFu, gpb_prev = 0xFFu;
    static bool primed = false;
    if (!primed) { gpa_prev = gpa; gpb_prev = gpb; primed = true; return; }

    uint8_t gpa_fell = (uint8_t)(gpa_prev & ~gpa);
    uint8_t gpb_fell = (uint8_t)(gpb_prev & ~gpb);
    if (gpa_fell || gpb_fell) beep();

    /* GPA0: toggle VI enable */
    if (gpa_fell & (1u<<0)) g_vi_enabled = !g_vi_enabled;

    /* GPB0/2/4: cycle continuity channel (Vset/Iset/Threshold) */
    if (gpb_fell & (1u<<0)) g_cont_active = CONT_VSET;
    if (gpb_fell & (1u<<2)) g_cont_active = CONT_ISET;
    if (gpb_fell & (1u<<4)) g_cont_active = CONT_THR;

    gpa_prev = gpa; gpb_prev = gpb;
}

/* ── Main ─────────────────────────────────────────────────────────────── */
int main(void)
{
    stdio_init_all();
    const uint led = 25;
    gpio_init(led); gpio_set_dir(led, GPIO_OUT);

    flash_load();

    mcp4728 dac(0x00);
    dac.begin();
    dac.setVref(1,1,1,1);
    dac.setGain(0,0,0,0);
    dac.setPowerDown(1,1,1,1);
    dac.analogWriteAll(0,0,0,0);
    dac.ldacPulse();

    hw_gpio_init();
    adc_measurement_init();
    cal_apply();

    /* Restore setpoints; outputs start DISABLED */
    for (uint8_t i = 0; i < DAC_NUM_CH; i++) g_dac_cnt[i] = g_cal.dac_cnt[i];
    g_cont_dac[0] = g_cal.cont.vset_cnt;
    g_cont_dac[1] = g_cal.cont.iset_cnt;
    g_cont_thr    = g_cal.cont.thr_cnt;
    g_vi_enabled = g_isink_enabled = false;

    mav_init_all();
    encoder_init();
    ir_init();
    fan_init();
    beeper_init();

    st7735_init();
    st7735_set_rotation(3);
    backlight_set(g_cal.backlight);

    mcp23s17_init();
    mode_enter(MODE_SUPPLY_SINK);

    uint64_t last_display_us = 0;

    while (true) {
        gpio_put(led, true); sleep_ms(1); gpio_put(led, false);

        /* ── IR decode ──────────────────────────────────────────────── */
        if (ir_decode() && g_ir_new) {
            g_ir_new = false;
            g_ir_show_until = time_us_64() + IR_DISPLAY_US;
            if (!g_ir_repeat) {
                uint8_t cmd = (uint8_t)((g_ir_code >> 16) & 0xFF);
                /* IR dispatch: expand as needed */
                if (cmd == IR_CMD_VI_TOGGLE)  g_vi_enabled    = !g_vi_enabled;
                if (cmd == IR_CMD_IS_TOGGLE)  g_isink_enabled = !g_isink_enabled;
            }
        }

        /* ── Buttons ────────────────────────────────────────────────── */
        uint8_t gpa_in, gpb_in;
        mcp23s17_read_inputs(&gpa_in, &gpb_in);

        bool gpb7_fell = false;
        bool enter_cal = false;

        if (g_mode == MODE_CONTINUITY) {
            process_buttons_continuity(gpa_in, gpb_in);
            /* GPB5 and GPB7 handled here with proper priming */
            static uint8_t gpb_prev_c = 0xFFu;
            static bool    gpb_c_primed = false;
            if (!gpb_c_primed) { gpb_prev_c = gpb_in; gpb_c_primed = true; }
            uint8_t gpb_fell_c = (uint8_t)(gpb_prev_c & ~gpb_in);
            gpb_prev_c = gpb_in;
            enter_cal = (bool)(gpb_fell_c & (1u<<5));
            gpb7_fell  = (bool)(gpb_fell_c & (1u<<7));
        } else {
            enter_cal = process_buttons_main(gpa_in, gpb_in, &gpb7_fell);
        }

        /* GPB7: cycle mode */
        if (gpb7_fell) {
            psu_mode_t next = (psu_mode_t)((g_mode + 1) % MODE_COUNT);
            mode_enter(next);
            process_buttons_main_reset();
            last_display_us = 0;
            continue;
        }

        /* GPB5: enter cal menu */
        if (enter_cal) {
            cal_menu_run(mode_redraw_static);
            process_buttons_main_reset();
            backlight_set(g_cal.backlight);
            last_display_us = 0;
            continue;
        }

        /* ── Encoder + DAC ──────────────────────────────────────────── */
        int32_t mult = encoder_step_multiplier(gpb_in);

        if (g_mode == MODE_CONTINUITY) {
            apply_encoder_continuity(mult);
            dac_update_continuity(dac);
        } else {
            apply_encoder_main(mult);
            dac_update(dac);
        }

        /* ── ADC + filter ───────────────────────────────────────────── */
        adc_readings_t r;
        if (!adc_read_all_channels(&r)) printf("ADC err\n");

        adc_bundle_t adc;
        adc.vout_mV  = mav_update(&mav_vout,  r.calibrated[CHANNEL_VOUT]);
        adc.iout_mA  = mav_update(&mav_iout,  r.calibrated[CHANNEL_IOUT]);
        adc.hii_mA   = mav_update(&mav_hii,   r.calibrated[CHANNEL_HIGH_CURRENT_IN]);
        adc.tfan_mC  = mav_update(&mav_tfan,  adc_get_temp2_mC());
        adc.vin_mV   = mav_update(&mav_vin,   r.calibrated[CHANNEL_VIN]);
        adc.tcore_mC = mav_update(&mav_tcore, adc_get_core_temp_mC());
        /* Power: Pout uses raw Iout; Psink uses HiI (Isink) directly */
        adc.pout_mW  = (adc.vout_mV * adc.iout_mA) / 1000;
        adc.psink_mW = (adc.vout_mV * adc.hii_mA)  / 1000;

        /* ── Fan ────────────────────────────────────────────────────── */
        fan_update(adc.tfan_mC);

        if (g_mode != MODE_CONTINUITY) {
            power_limit_update(adc.vout_mV, adc.iout_mA, adc.hii_mA);

            /* Vin minimum check */
            if (g_cal.plim_enabled && g_cal.vin_min_mV > 0 &&
                adc.vin_mV < g_cal.vin_min_mV && g_vi_enabled &&
                g_dac_cnt[DAC_CH_C] > 0) {
                g_dac_cnt[DAC_CH_C]--;
                msg_post(MSG_ERROR, "ERR: Vin too low - reducing Vset", 0u);
                if (!g_pout_limiting) beep_special();
                g_pout_limiting = true;
            } else if (g_cal.vin_min_mV > 0 && adc.vin_mV >= g_cal.vin_min_mV
                       && g_pout_limiting && !g_psink_limiting) {
                /* Vin recovered and no other limiter active */
                msg_clear();
            }
        }

        /* ── Continuity beep + message ─────────────────────────────── */
        if (g_mode == MODE_CONTINUITY && g_vi_enabled) {
            int32_t thr_mV = cnt_to_mv(g_cont_thr)
                             * g_cal.vout_gain / g_cal.vout_div - g_cal.vout_off;
            if (adc.vout_mV < thr_mV) {
                beep_continuous_start();
                msg_post(MSG_WARN, "CONTINUITY: closed", 0u);
            } else {
                beep_continuous_stop();
                msg_clear();
            }
        } else {
            beep_continuous_stop();
            if (g_mode == MODE_CONTINUITY) msg_clear();
        }

        /* ── Display (rate-limited) ─────────────────────────────────── */
        uint64_t now_us = time_us_64();
        if ((now_us - last_display_us) < (uint64_t)DISPLAY_PERIOD_MS * 1000u)
            continue;
        last_display_us = now_us;

        switch (g_mode) {
        case MODE_SUPPLY_SINK: draw_supply_sink_update(adc, mult); break;
        case MODE_SUPPLY:      draw_supply_update(adc, mult);      break;
        case MODE_CONTINUITY:  draw_continuity_update(adc, mult);  break;
        default: break;
        }

        ir_overlay_update();   /* kept for link compat */
        msg_update();          /* handles all overlays including IR */
    }
}