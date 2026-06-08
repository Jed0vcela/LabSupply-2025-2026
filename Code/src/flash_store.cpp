#include "flash_store.h"
#include "adc_measurement.h"
#include <string.h>
#include "hardware/flash.h"
#include "hardware/sync.h"

/* ── CRC-32 ───────────────────────────────────────────────────────────── */
static uint32_t crc32(const uint8_t *d, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

/* ── Defaults ─────────────────────────────────────────────────────────── */
const cal_flash_t CAL_DEFAULTS = {
    CAL_MAGIC,
    /* vout */ 11088, 1000, 175,
    /* vin  */ 18551, 1703, 0,
    /* hii  */ 10000, 1000, 2500,
    /* iout */ 5405,  1000, 2500,
    /* vset */ 1000,  1000, 0,
    /* iset */ 1000,  1000, 0,
    /* isink*/ 1000,  1000, 0,
    /* dac  */ {0, 0, 0, 0},
    /* cont */ {0, 0, 200},
    /* pout_max_mW  */ 30000,
    /* psink_max_mW */ 20000,
    /* vin_min_mV   */ 0,
    /* plim_enabled */ 1,
    /* beep_mode    */ BEEP_ALL,
    /* backlight    */ 80,
    /* step_mode    */ 0,   /* 0 = momentary */
    /* crc          */ 0
};

cal_flash_t g_cal;

/* ── Flash address ────────────────────────────────────────────────────── */
#define FLASH_CAL_OFFSET   (FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_CAL_ADDR     (XIP_BASE + FLASH_CAL_OFFSET)

void flash_load(void)
{
    const cal_flash_t *p = (const cal_flash_t *)FLASH_CAL_ADDR;
    if (p->magic != CAL_MAGIC) { g_cal = CAL_DEFAULTS; return; }
    uint32_t expected = crc32((const uint8_t *)p,
                               sizeof(cal_flash_t) - sizeof(uint32_t));
    if (p->crc != expected) { g_cal = CAL_DEFAULTS; return; }
    g_cal = *p;
}

void flash_save(void)
{
    g_cal.magic = CAL_MAGIC;
    g_cal.crc   = crc32((const uint8_t *)&g_cal,
                         sizeof(cal_flash_t) - sizeof(uint32_t));
    static uint8_t buf[FLASH_SECTOR_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &g_cal, sizeof(cal_flash_t));
    uint32_t st = save_and_disable_interrupts();
    flash_range_erase(FLASH_CAL_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CAL_OFFSET, buf, FLASH_SECTOR_SIZE);
    restore_interrupts(st);
}

void cal_apply(void)
{
    adc_set_calibration(CHANNEL_VOUT,            g_cal.vout_gain,  g_cal.vout_div,  g_cal.vout_off);
    adc_set_calibration(CHANNEL_VIN,             g_cal.vin_gain,   g_cal.vin_div,   g_cal.vin_off);
    adc_set_calibration(CHANNEL_HIGH_CURRENT_IN, g_cal.hii_gain,   g_cal.hii_div,   g_cal.hii_off);
    adc_set_calibration(CHANNEL_IOUT,            g_cal.iout_gain,  g_cal.iout_div,  g_cal.iout_off);
}