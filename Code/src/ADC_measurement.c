/**
 * adc_measurement.c
 *
 * Fixes vs. original:
 *   - adc_get_temp1_mC() was reading CHANNEL_TEMP2 (copy-paste bug) - FIXED
 *   - Both temp functions returned integer C, not milli-C as named - FIXED
 *   - LUT replaced with Beta-equation float calculation (faster, accurate)
 *   - Added adc_get_core_temp_mC() for RP2040 internal sensor
 */

#include "adc_measurement.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* NTC parameters - 10k NTC, B=3950, 10k pull-up to 3.3V */
#define NTC_R0     10000.0f
#define NTC_T0     298.15f
#define NTC_BETA   3950.0f
#define NTC_RPULL  10000.0f
#define NTC_VREF   3.3f

static adc_calibration_data_t calibration_data;

static inline void delay_nops(int n) {
    for (volatile int i = 0; i < n; ++i) __asm volatile ("nop");
}

static inline void set_mux_address(uint8_t mux1, uint8_t mux2) {
    gpio_put(ADC_MUX1_PIN, mux1);
    gpio_put(ADC_MUX2_PIN, mux2);
    delay_nops(ADC_SETTLING_NOPS);
}

static void init_default_calibration(void) {
    calibration_data.channels[CHANNEL_TEMP2].gain         = DEFAULT_TEMP2_GAIN;
    calibration_data.channels[CHANNEL_TEMP2].divisor      = DEFAULT_TEMP2_DIVISOR;
    calibration_data.channels[CHANNEL_TEMP2].offset       = DEFAULT_TEMP2_OFFSET;
    calibration_data.channels[CHANNEL_LOW_CURRENT_IN].gain    = DEFAULT_LOW_CURRENT_GAIN;
    calibration_data.channels[CHANNEL_LOW_CURRENT_IN].divisor = DEFAULT_LOW_CURRENT_DIVISOR;
    calibration_data.channels[CHANNEL_LOW_CURRENT_IN].offset  = DEFAULT_LOW_CURRENT_OFFSET;
    calibration_data.channels[CHANNEL_FREE].gain    = DEFAULT_FREE_GAIN;
    calibration_data.channels[CHANNEL_FREE].divisor = DEFAULT_FREE_DIVISOR;
    calibration_data.channels[CHANNEL_FREE].offset  = DEFAULT_FREE_OFFSET;
    calibration_data.channels[CHANNEL_VIN].gain    = DEFAULT_VIN_GAIN;
    calibration_data.channels[CHANNEL_VIN].divisor = DEFAULT_VIN_DIVISOR;
    calibration_data.channels[CHANNEL_VIN].offset  = DEFAULT_VIN_OFFSET;
    calibration_data.channels[CHANNEL_TEMP1].gain    = DEFAULT_TEMP1_GAIN;
    calibration_data.channels[CHANNEL_TEMP1].divisor = DEFAULT_TEMP1_DIVISOR;
    calibration_data.channels[CHANNEL_TEMP1].offset  = DEFAULT_TEMP1_OFFSET;
    calibration_data.channels[CHANNEL_VOUT].gain    = DEFAULT_VOUT_GAIN;
    calibration_data.channels[CHANNEL_VOUT].divisor = DEFAULT_VOUT_DIVISOR;
    calibration_data.channels[CHANNEL_VOUT].offset  = DEFAULT_VOUT_OFFSET;
    calibration_data.channels[CHANNEL_HIGH_CURRENT_IN].gain    = DEFAULT_HIGH_CURRENT_GAIN;
    calibration_data.channels[CHANNEL_HIGH_CURRENT_IN].divisor = DEFAULT_HIGH_CURRENT_DIVISOR;
    calibration_data.channels[CHANNEL_HIGH_CURRENT_IN].offset  = DEFAULT_HIGH_CURRENT_OFFSET;
    calibration_data.channels[CHANNEL_IOUT].gain    = DEFAULT_IOUT_GAIN;
    calibration_data.channels[CHANNEL_IOUT].divisor = DEFAULT_IOUT_DIVISOR;
    calibration_data.channels[CHANNEL_IOUT].offset  = DEFAULT_IOUT_OFFSET;
    calibration_data.magic    = CALIBRATION_MAGIC;
    calibration_data.checksum = adc_calculate_checksum(&calibration_data);
}

void adc_measurement_init(void) {
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);
    adc_set_temp_sensor_enabled(true);   /* enable RP2040 internal sensor */
    gpio_init(ADC_MUX1_PIN); gpio_set_dir(ADC_MUX1_PIN, GPIO_OUT); gpio_put(ADC_MUX1_PIN, 0);
    gpio_init(ADC_MUX2_PIN); gpio_set_dir(ADC_MUX2_PIN, GPIO_OUT); gpio_put(ADC_MUX2_PIN, 0);
    init_default_calibration();
}

bool adc_read_all_raw(adc_readings_t *readings) {
    if (!readings) return false;
    set_mux_address(0, 0);
    adc_select_input(ADC_INPUT_0); readings->raw_values[CHANNEL_TEMP2]          = adc_read();
    adc_select_input(ADC_INPUT_1); readings->raw_values[CHANNEL_LOW_CURRENT_IN] = adc_read();
    adc_select_input(ADC_INPUT_2); readings->raw_values[CHANNEL_FREE]           = adc_read();
    set_mux_address(0, 1);
    adc_select_input(ADC_INPUT_0); readings->raw_values[CHANNEL_VIN]            = adc_read();
    set_mux_address(1, 1);
    adc_select_input(ADC_INPUT_0); readings->raw_values[CHANNEL_TEMP1]          = adc_read();
    set_mux_address(1, 0);
    adc_select_input(ADC_INPUT_0); readings->raw_values[CHANNEL_VOUT]           = adc_read();
    adc_select_input(ADC_INPUT_1); readings->raw_values[CHANNEL_HIGH_CURRENT_IN]= adc_read();
    adc_select_input(ADC_INPUT_2); readings->raw_values[CHANNEL_IOUT]           = adc_read();
    readings->timestamp_ms = to_ms_since_boot(get_absolute_time());
    readings->valid = true;
    return true;
}

int32_t adc_raw_to_mv(uint16_t raw) {
    return ((int32_t)raw * ADC_VREF_MV) / ADC_RESOLUTION;
}

int32_t adc_apply_calibration(int32_t raw_mv, const adc_calibration_t *cal) {
    if (!cal || cal->divisor == 0) return raw_mv;
    int64_t t = ((int64_t)raw_mv * cal->gain) / cal->divisor;
    return (int32_t)t - cal->offset;
}

void adc_convert_and_calibrate(adc_readings_t *readings) {
    if (!readings) return;
    for (int i = 0; i < ADC_NUM_CHANNELS; i++) {
        readings->raw_mv[i]     = adc_raw_to_mv(readings->raw_values[i]);
        readings->calibrated[i] = adc_apply_calibration(readings->raw_mv[i],
                                      &calibration_data.channels[i]);
    }
}

bool adc_read_all_channels(adc_readings_t *readings) {
    if (!adc_read_all_raw(readings)) return false;
    adc_convert_and_calibrate(readings);
    return true;
}

uint16_t adc_read_channel_raw(adc_channel_t ch) {
    if (ch >= ADC_NUM_CHANNELS) return 0;
    switch (ch) {
        case CHANNEL_TEMP2:           set_mux_address(0,0); adc_select_input(ADC_INPUT_0); break;
        case CHANNEL_LOW_CURRENT_IN:  set_mux_address(0,0); adc_select_input(ADC_INPUT_1); break;
        case CHANNEL_FREE:            set_mux_address(0,0); adc_select_input(ADC_INPUT_2); break;
        case CHANNEL_VIN:             set_mux_address(0,1); adc_select_input(ADC_INPUT_0); break;
        case CHANNEL_TEMP1:           set_mux_address(1,1); adc_select_input(ADC_INPUT_0); break;
        case CHANNEL_VOUT:            set_mux_address(1,0); adc_select_input(ADC_INPUT_0); break;
        case CHANNEL_HIGH_CURRENT_IN: set_mux_address(1,0); adc_select_input(ADC_INPUT_1); break;
        case CHANNEL_IOUT:            set_mux_address(1,0); adc_select_input(ADC_INPUT_2); break;
        default: return 0;
    }
    return adc_read();
}

int32_t adc_read_channel_calibrated(adc_channel_t ch) {
    if (ch >= ADC_NUM_CHANNELS) return 0;
    return adc_apply_calibration(adc_raw_to_mv(adc_read_channel_raw(ch)),
                                 &calibration_data.channels[ch]);
}

/* ── NTC temperature – Beta equation, float ─────────────────────────────
 * Circuit: 3.3V → Rpull(10k) → ADC pin → NTC → GND  (pull-up)
 *   Rntc = Rpull * Vpin / (Vref - Vpin)
 *   1/T  = 1/T0 + ln(R/R0)/Beta
 * Returns milli-°C.  Returns INT32_MIN on bad input.
 */
static int32_t ntc_mv_to_mC(int32_t mv)
{
    if (mv <= 10 || mv >= (ADC_VREF_MV - 10)) return INT32_MIN;
    float vpin = (float)mv / 1000.0f;
    float rntc = NTC_RPULL * vpin / (NTC_VREF - vpin);
    if (rntc <= 0.0f) return INT32_MIN;
    float inv_T = 1.0f/NTC_T0 + logf(rntc / NTC_R0) / NTC_BETA;
    float T_C   = 1.0f/inv_T - 273.15f;
    return (int32_t)(T_C * 1000.0f);
}

/* FIXED: was reading CHANNEL_TEMP2 for both functions */
int32_t adc_get_temp1_mC(void) {
    int32_t mC = ntc_mv_to_mC(adc_raw_to_mv(adc_read_channel_raw(CHANNEL_TEMP1)));
    return (mC == INT32_MIN) ? 0 : mC;
}

int32_t adc_get_temp2_mC(void) {
    int32_t mC = ntc_mv_to_mC(adc_raw_to_mv(adc_read_channel_raw(CHANNEL_TEMP2)));
    return (mC == INT32_MIN) ? 0 : mC;
}

/* RP2040 internal temperature sensor on ADC channel 4.
 * Datasheet formula: T = 27 - (Vadc - 0.706) / 0.001721
 * Returns milli-°C.
 */
int32_t adc_get_core_temp_mC(void) {
    adc_select_input(4);
    float vadc = (float)adc_read() * (3.3f / 4096.0f);
    float T_C  = 27.0f - (vadc - 0.706f) / 0.001721f;
    return (int32_t)(T_C * 1000.0f);
}

/* ── Calibration management ──────────────────────────────────────────── */
void adc_set_calibration(adc_channel_t ch, int32_t gain, int32_t div, int32_t off) {
    if (ch >= ADC_NUM_CHANNELS) return;
    calibration_data.channels[ch].gain    = gain;
    calibration_data.channels[ch].divisor = div;
    calibration_data.channels[ch].offset  = off;
    calibration_data.checksum = adc_calculate_checksum(&calibration_data);
}
void adc_get_calibration(adc_channel_t ch, adc_calibration_t *cal) {
    if (ch < ADC_NUM_CHANNELS && cal) *cal = calibration_data.channels[ch];
}
void adc_reset_calibration(adc_channel_t ch) {
    if (ch >= ADC_NUM_CHANNELS) return;
    calibration_data.channels[ch].gain    = DEFAULT_GAIN;
    calibration_data.channels[ch].divisor = DEFAULT_DIVISOR;
    calibration_data.channels[ch].offset  = DEFAULT_OFFSET;
    calibration_data.checksum = adc_calculate_checksum(&calibration_data);
}
void adc_reset_all_calibration(void)                               { init_default_calibration(); }
const adc_calibration_data_t* adc_get_calibration_data(void)      { return &calibration_data; }

uint32_t adc_calculate_checksum(const adc_calibration_data_t *d) {
    if (!d) return 0;
    uint32_t cs = 0;
    const uint32_t *p = (const uint32_t *)d;
    size_t words = (sizeof(adc_calibration_data_t) - sizeof(uint32_t)) / sizeof(uint32_t);
    for (size_t i = 0; i < words; i++) cs ^= p[i];
    return cs;
}
bool adc_load_calibration_data(const adc_calibration_data_t *d) {
    if (!d || d->magic != CALIBRATION_MAGIC) return false;
    if (adc_calculate_checksum(d) != d->checksum) return false;
    memcpy(&calibration_data, d, sizeof(adc_calibration_data_t));
    return true;
}

/* ── Debug ───────────────────────────────────────────────────────────── */
const char* adc_get_channel_name(adc_channel_t ch) {
    switch (ch) {
        case CHANNEL_TEMP2:           return "TEMP2";
        case CHANNEL_LOW_CURRENT_IN:  return "LOW_I_IN";
        case CHANNEL_FREE:            return "FREE";
        case CHANNEL_VIN:             return "VIN";
        case CHANNEL_TEMP1:           return "TEMP1";
        case CHANNEL_VOUT:            return "VOUT";
        case CHANNEL_HIGH_CURRENT_IN: return "HIGH_I_IN";
        case CHANNEL_IOUT:            return "IOUT";
        default:                      return "UNKNOWN";
    }
}
void adc_print_readings(const adc_readings_t *r) {
    if (!r || !r->valid) { printf("Invalid ADC readings\n"); return; }
    printf("\n=== ADC (%lu ms) ===\n", r->timestamp_ms);
    for (int i = 0; i < ADC_NUM_CHANNELS; i++)
        printf("%-10s raw=%4d mv=%5ld cal=%6ld\n",
               adc_get_channel_name(i), r->raw_values[i], r->raw_mv[i], r->calibrated[i]);
    printf("T1=%ld mC  T2=%ld mC  Core=%ld mC\n",
           adc_get_temp1_mC(), adc_get_temp2_mC(), adc_get_core_temp_mC());
}
void adc_print_calibration(adc_channel_t ch) {
    if (ch >= ADC_NUM_CHANNELS) return;
    adc_calibration_t *c = &calibration_data.channels[ch];
    printf("Cal %s: g=%ld d=%ld o=%ld\n", adc_get_channel_name(ch), c->gain, c->divisor, c->offset);
}
void adc_print_all_calibration(void) {
    printf("\n=== Cal (magic=0x%08lX) ===\n", calibration_data.magic);
    for (int i = 0; i < ADC_NUM_CHANNELS; i++) adc_print_calibration(i);
}