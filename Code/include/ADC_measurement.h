/**
 * adc_measurement.h
 * 
 * ADC voltage measurement with multiplexer support for RP2040
 * Uses 2 multiplexer control pins to select between 8 analog channels
 * 
 * Integer-only calibration system:
 * - All values in mV, mA, or mC (milli-units)
 * - No floating point math
 * - Per-channel gain and offset calibration
 */

#ifndef ADC_MEASUREMENT_H
#define ADC_MEASUREMENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Multiplexer control pins
#define ADC_MUX1_PIN         19
#define ADC_MUX2_PIN         18

// ADC hardware inputs (RP2040)
#define ADC_INPUT_0          0   // GPIO26
#define ADC_INPUT_1          1   // GPIO27
#define ADC_INPUT_2          2   // GPIO28

// ADC configuration
#define ADC_VREF_MV          3300     // Reference voltage in mV
#define ADC_RESOLUTION       4096     // 12-bit ADC (0-4095)
#define ADC_SETTLING_NOPS    2        // NOP delay for multiplexer settling

// ============================================================================
// DEFAULT CALIBRATION VALUES - CUSTOMIZE FOR YOUR HARDWARE
// ============================================================================
// These are the initial values loaded at startup
// You can modify these for your specific hardware, then each product can
// be fine-tuned individually after production if needed
//
// Formula: calibrated = ((raw_mv * gain) / divisor) - offset

#define DEFAULT_TEMP2_GAIN        100
#define DEFAULT_TEMP2_DIVISOR     1
#define DEFAULT_TEMP2_OFFSET      0

#define DEFAULT_LOW_CURRENT_GAIN      10
#define DEFAULT_LOW_CURRENT_DIVISOR   1
#define DEFAULT_LOW_CURRENT_OFFSET    50

#define DEFAULT_FREE_GAIN         1000
#define DEFAULT_FREE_DIVISOR      1000
#define DEFAULT_FREE_OFFSET       0

// Channel 3: VIN - Default for 10:1 voltage divider
#define DEFAULT_VIN_GAIN          18901
#define DEFAULT_VIN_DIVISOR       1703  
#define DEFAULT_VIN_OFFSET        250

#define DEFAULT_TEMP1_GAIN        100
#define DEFAULT_TEMP1_DIVISOR     1
#define DEFAULT_TEMP1_OFFSET      0

#define DEFAULT_VOUT_GAIN         11088
#define DEFAULT_VOUT_DIVISOR      1000
#define DEFAULT_VOUT_OFFSET       175

#define DEFAULT_HIGH_CURRENT_GAIN      16600
#define DEFAULT_HIGH_CURRENT_DIVISOR   1000
#define DEFAULT_HIGH_CURRENT_OFFSET    2500

#define DEFAULT_IOUT_GAIN         5405
#define DEFAULT_IOUT_DIVISOR      1000
#define DEFAULT_IOUT_OFFSET       50

// Generic defaults (used only if channel-specific defaults not defined)
#define DEFAULT_GAIN         1000     // Gain multiplier (1000 = 1.000x)
#define DEFAULT_DIVISOR      1000     // Divisor for gain (allows decimal precision)
#define DEFAULT_OFFSET       0        // DC offset to subtract (in mV/mA/mC)

int32_t adc_get_temp1_mC(void);
int32_t adc_get_temp2_mC(void);

int32_t adc_get_core_temp_mC(void);

// Channel indices in the readings structure
typedef enum {
    CHANNEL_TEMP2 = 0,
    CHANNEL_LOW_CURRENT_IN,
    CHANNEL_FREE,
    CHANNEL_VIN,
    CHANNEL_TEMP1,
    CHANNEL_VOUT,
    CHANNEL_HIGH_CURRENT_IN,
    CHANNEL_IOUT,
    ADC_NUM_CHANNELS
} adc_channel_t;

/**
 * Calibration parameters for a single channel
 * Formula: calibrated_value = ((raw_mv * gain) / divisor) - offset
 * 
 * Example: For a 10:1 voltage divider reading 12V as 1.2V
 *   gain = 10000, divisor = 1000, offset = 0
 *   Result: ((1200 * 10000) / 1000) - 0 = 12000 mV
 * 
 * Example: Current sensor with 100mV/A, 2.5V offset
 *   gain = 1000, divisor = 100, offset = 2500
 *   At 1A: ((2600 * 1000) / 100) - 2500 = 23500 mV = 1000 mA
 */
typedef struct {
    int32_t gain;       // Multiplier (e.g., 1000 = 1.000x, 2500 = 2.5x)
    int32_t divisor;    // Divisor (typically 1000 for 3 decimal places)
    int32_t offset;     // DC offset to subtract (in mV/mA/mC)
} adc_calibration_t;

/**
 * All calibration data for all channels
 * This struct can be stored in flash or EEPROM for persistence
 */
typedef struct {
    adc_calibration_t channels[ADC_NUM_CHANNELS];
    uint32_t magic;     // Magic number to verify valid calibration data
    uint32_t checksum;  // Simple checksum for data integrity
} adc_calibration_data_t;

#define CALIBRATION_MAGIC 0xCAFEBABE  // Magic number for valid calibration

/**
 * Structure to hold voltage measurements from all ADC channels
 */
typedef struct {
    uint16_t raw_values[ADC_NUM_CHANNELS];      // Raw ADC values (0-4095)
    int32_t  raw_mv[ADC_NUM_CHANNELS];          // Raw values in mV (before calibration)
    int32_t  calibrated[ADC_NUM_CHANNELS];      // Calibrated values (mV/mA/mC)
    uint32_t timestamp_ms;                      // Timestamp of measurement
    bool     valid;                             // True if measurement is valid
} adc_readings_t;

/**
 * Initialize ADC subsystem and multiplexer control pins
 * Loads default calibration values
 */
void adc_measurement_init(void);

/**
 * Read all 8 ADC channels through multiplexer
 * Reads raw values, converts to mV, and applies calibration
 * 
 * @param readings Pointer to struct where results will be stored
 * @return true if successful, false on error
 */
bool adc_read_all_channels(adc_readings_t *readings);

/**
 * Read all channels (raw values only, no conversion or calibration)
 * Fastest version - only reads ADC hardware
 * 
 * @param readings Pointer to struct where raw values will be stored
 * @return true if successful, false on error
 */
bool adc_read_all_raw(adc_readings_t *readings);

/**
 * Read a single channel (raw ADC value)
 * 
 * @param channel Channel to read (use adc_channel_t enum)
 * @return Raw ADC value (0-4095)
 */
uint16_t adc_read_channel_raw(adc_channel_t channel);

/**
 * Read a single channel and return calibrated value
 * 
 * @param channel Channel to read (use adc_channel_t enum)
 * @return Calibrated value in mV/mA/mC
 */
int32_t adc_read_channel_calibrated(adc_channel_t channel);

/**
 * Convert raw ADC value to millivolts (integer only)
 * Formula: mv = (raw * VREF_MV) / ADC_RESOLUTION
 * 
 * @param raw_value Raw ADC reading (0-4095)
 * @return Voltage in millivolts
 */
int32_t adc_raw_to_mv(uint16_t raw_value);

/**
 * Apply calibration to a raw mV value
 * Formula: calibrated = ((raw_mv * gain) / divisor) - offset
 * 
 * @param raw_mv Raw millivolt value
 * @param cal Pointer to calibration parameters
 * @return Calibrated value in mV/mA/mC
 */
int32_t adc_apply_calibration(int32_t raw_mv, const adc_calibration_t *cal);

/**
 * Convert all raw values to mV and apply calibration
 * 
 * @param readings Pointer to readings struct (raw_values must be populated)
 */
void adc_convert_and_calibrate(adc_readings_t *readings);

/**
 * Set calibration for a specific channel
 * 
 * @param channel Channel to calibrate
 * @param gain Gain multiplier (e.g., 1000 = 1.000x)
 * @param divisor Divisor (typically 1000)
 * @param offset DC offset in mV/mA/mC to subtract
 */
void adc_set_calibration(adc_channel_t channel, int32_t gain, int32_t divisor, int32_t offset);

/**
 * Get current calibration for a channel
 * 
 * @param channel Channel index
 * @param cal Pointer to calibration struct to fill
 */
void adc_get_calibration(adc_channel_t channel, adc_calibration_t *cal);

/**
 * Reset calibration for a channel to defaults
 * 
 * @param channel Channel to reset
 */
void adc_reset_calibration(adc_channel_t channel);

/**
 * Reset all channels to default calibration
 */
void adc_reset_all_calibration(void);

/**
 * Get pointer to current calibration data (for reading/saving)
 * 
 * @return Pointer to calibration data structure
 */
const adc_calibration_data_t* adc_get_calibration_data(void);

/**
 * Load calibration data (e.g., from flash/EEPROM)
 * Validates magic number and checksum
 * 
 * @param cal_data Pointer to calibration data to load
 * @return true if data is valid and loaded, false otherwise
 */
bool adc_load_calibration_data(const adc_calibration_data_t *cal_data);

/**
 * Calculate checksum for calibration data
 * 
 * @param cal_data Pointer to calibration data
 * @return Calculated checksum
 */
uint32_t adc_calculate_checksum(const adc_calibration_data_t *cal_data);

/**
 * Print ADC readings to console (for debugging)
 * 
 * @param readings Pointer to readings struct
 */
void adc_print_readings(const adc_readings_t *readings);

/**
 * Print calibration data for a channel
 * 
 * @param channel Channel to print
 */
void adc_print_calibration(adc_channel_t channel);

/**
 * Print all calibration data
 */
void adc_print_all_calibration(void);

/**
 * Get channel name as string (for debugging/display)
 * 
 * @param channel Channel index
 * @return Channel name string
 */
const char* adc_get_channel_name(adc_channel_t channel);

#ifdef __cplusplus
}
#endif

#endif // ADC_MEASUREMENT_H