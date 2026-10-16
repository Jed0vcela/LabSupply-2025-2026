#pragma once
#include <stdint.h>

/* Pure calibration maths (no Pico SDK dependency, unit-tested on the host).
 *
 * Model: truth = a * x + b   with   a = gain / CAL_FIT_DIV,  b = -off
 * which is the same form as the firmware uses: value = x * gain / div - off. */

#define CAL_FIT_DIV        10000
#define CAL_FIT_GAIN_MAX   999999     /* gain/div must stay < 100 (int32 overflow in setpt_disp) */
#define CAL_FIT_OFF_LIMIT  9999       /* |off| must be < this                                    */

typedef struct {
    double truth;   /* true value (mV or mA)                       */
    double x;       /* uncalibrated value (raw mV or DAC output mV) */
} cal_pt_t;

typedef enum {
    CAL_FIT_UNCHANGED = 0,   /* no usable point - channel left as it was      */
    CAL_FIT_GAIN_ONLY,       /* one point - gain only, old offset kept         */
    CAL_FIT_FULL,            /* two or more points - gain and offset           */
    CAL_FIT_ERROR            /* degenerate points or result out of range       */
} cal_fit_status_t;

/* Weighted least squares with weight 1/truth^2 (minimises relative error).
 * Points with truth <= 0 or x <= 0 or non-finite values are ignored.
 * old_off is kept when only one point is usable.
 * *gain / *off are written only for CAL_FIT_GAIN_ONLY and CAL_FIT_FULL. */
cal_fit_status_t cal_fit(const cal_pt_t *pts, int n, int32_t old_off,
                         int32_t *gain, int32_t *off);

/* Relative error in percent of one point under the given gain/off (div = CAL_FIT_DIV). */
double cal_fit_error_pct(const cal_pt_t &p, int32_t gain, int32_t off);
