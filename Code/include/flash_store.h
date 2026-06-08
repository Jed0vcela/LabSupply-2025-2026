#pragma once
#include "psu_types.h"
#include <stdint.h>

#define FLASH_SIZE_BYTES   (2u * 1024u * 1024u)
#define CAL_MAGIC          0xCAFE5A81u   /* bumped: step_mode field added */

/* Continuity tester stored state */
typedef struct {
    int32_t vset_cnt;
    int32_t iset_cnt;
    int32_t thr_cnt;
} cont_flash_t;

typedef struct {
    uint32_t magic;
    /* ADC calibration */
    int32_t vout_gain, vout_div, vout_off;
    int32_t vin_gain,  vin_div,  vin_off;
    int32_t hii_gain,  hii_div,  hii_off;
    int32_t iout_gain, iout_div, iout_off;
    /* Setpoint display calibration */
    int32_t vset_gain, vset_div, vset_off;
    int32_t iset_gain, iset_div, iset_off;
    int32_t isink_gain,isink_div,isink_off;
    /* DAC setpoints */
    int32_t dac_cnt[DAC_NUM_CH];
    /* Continuity mode */
    cont_flash_t cont;
    /* Power limiting — stored in mW, edited in W in cal menu */
    int32_t pout_max_mW;    /* max load power,  mW  (0 = unlimited) */
    int32_t psink_max_mW;   /* max sink power,  mW  (0 = unlimited) */
    int32_t vin_min_mV;     /* min input voltage, mV (0 = disabled) */
    uint8_t plim_enabled;   /* 0 = off, 1 = on                      */
    /* UI */
    uint8_t beep_mode;      /* beep_mode_t                           */
    uint8_t backlight;      /* 1–100 %                               */
    uint8_t step_mode;      /* 0 = momentary, 1 = toggle             */
    uint32_t crc;
} cal_flash_t;

extern cal_flash_t       g_cal;
extern const cal_flash_t CAL_DEFAULTS;

void flash_load(void);
void flash_save(void);
void cal_apply(void);   /* apply g_cal to ADC subsystem */