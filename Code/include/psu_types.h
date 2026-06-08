#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ── DAC virtual channels ─────────────────────────────────────────────── */
#define DAC_CH_A        0   /* Isink-Hi – always mirrors B */
#define DAC_CH_B        1   /* Isink-Lo – always mirrors A */
#define DAC_CH_C        2   /* Vset  */
#define DAC_CH_D        3   /* Iset  */
#define DAC_NUM_CH      4

#define DAC_VIRT_ISINK  0   /* virtual → writes DAC A+B */
#define DAC_VIRT_VSET   2   /* virtual → writes DAC C   */
#define DAC_VIRT_ISET   3   /* virtual → writes DAC D   */
#define DAC_NUM_VIRT    3

#define DAC_VREF_MV     2048u
#define DAC_MAX_COUNT   4095u
#define DAC_MIN_CNT     0
#define DAC_MAX_CNT     ((int32_t)DAC_MAX_COUNT)
#define ENC_STEP        1

/* ── Display colours (RGB565) ─────────────────────────────────────────── */
#define BG_COL     0x0000u
#define FG_COL     0xFFFFu
#define HDR_COL    0x07FFu   /* cyan       */
#define SEP_COL    0x4208u   /* dark grey  */
#define ACT_COL    0xFFE0u   /* yellow     */
#define DIS_COL    0x7BEFu   /* grey       */
#define ON_COL     0x07E0u   /* green      */
#define OFF_COL    0xF800u   /* red        */
#define WARN_COL   0xFD20u   /* orange     */
#define IR_COL     0xF81Fu   /* magenta    */
#define CAL_HDR    0x07FFu
#define CAL_SEL    0xFFE0u
#define CAL_EDT    0xFD20u

/* ── Display geometry ─────────────────────────────────────────────────── */
#define UI_SCALE   1
#define FONT_W     6    /* font glyph width  in pixels at scale 1 */
#define FONT_H     8    /* font glyph height in pixels at scale 1 */
#define CW         ((uint8_t)(FONT_W * UI_SCALE))
#define CH_PX      ((uint8_t)(FONT_H * UI_SCALE))
#define ROW_STEP   ((uint8_t)(CH_PX + 1u))
#define HALF       ((uint8_t)80)
#define LABEL_W    ((uint8_t)(5 * CW))
#define VAL_CHARS  7
#define VAL_X_L    LABEL_W
#define VAL_X_R    ((uint8_t)(HALF + LABEL_W))
#define FIELD_BUF  (VAL_CHARS + 1)

#define Y_HEADER  0u
#define Y_SEP1    ((uint8_t)(Y_HEADER + CH_PX + 1u))
#define Y_ROW0    ((uint8_t)(Y_SEP1   + 2u))
#define Y_ROW1    ((uint8_t)(Y_ROW0   + ROW_STEP))
#define Y_ROW2    ((uint8_t)(Y_ROW1   + ROW_STEP))
#define Y_ROW3    ((uint8_t)(Y_ROW2   + ROW_STEP))
#define Y_ROW4    ((uint8_t)(Y_ROW3   + ROW_STEP))
#define Y_ROW5    ((uint8_t)(Y_ROW4   + ROW_STEP))
#define Y_SEP2    ((uint8_t)(Y_ROW5   + ROW_STEP + 1u))
#define Y_FOOT0   ((uint8_t)(Y_SEP2   + 3u))
#define Y_FOOT1   ((uint8_t)(Y_FOOT0  + ROW_STEP))
#define Y_MSG     ((uint8_t)(128u - FONT_H - 3u)) /* bottom row; -3 for ST7735 rowstart offset */

/* ── Supply modes ─────────────────────────────────────────────────────── */
typedef enum {
    MODE_SUPPLY_SINK = 0,  /* full display: supply + sink */
    MODE_SUPPLY,           /* supply only, large Vout/Iout */
    MODE_CONTINUITY,       /* continuity checker */
    MODE_COUNT
} psu_mode_t;

/* ── Beep mode ────────────────────────────────────────────────────────── */
typedef enum {
    BEEP_ALL = 0,
    BEEP_SPECIAL,
    BEEP_MUTE
} beep_mode_t;

/* ── Field (dirty-caching display cell) ──────────────────────────────── */
typedef struct {
    uint8_t  x, y, max_chars;
    uint16_t fg, bg;
    char     prev[FIELD_BUF];
} field_t;

/* ── Moving average filter ────────────────────────────────────────────── */
#define MAV_LEN  8u
typedef struct { int32_t buf[MAV_LEN]; int32_t sum; uint8_t idx; } mav_t;

/* ── ADC measurements bundle (passed between modules) ────────────────── */
typedef struct {
    int32_t vout_mV;
    int32_t iout_mA;       /* raw calibrated Iout measurement */
    int32_t hii_mA;        /* raw calibrated HiI (Isink) measurement */
    int32_t vin_mV;
    int32_t tfan_mC;
    int32_t tcore_mC;
    int32_t pout_mW;       /* Vout * (Iout - HiI) / 1000 — load power  */
    int32_t psink_mW;      /* Vout * HiI / 1000          — sink power   */
} adc_bundle_t;