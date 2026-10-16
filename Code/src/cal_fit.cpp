#include "cal_fit.h"
#include <math.h>

static bool pt_usable(const cal_pt_t &p)
{
    return isfinite(p.truth) && isfinite(p.x) && p.truth > 0.0 && p.x > 0.0;
}

static bool to_int(double a, double b, int32_t *gain, int32_t *off)
{
    double g = round(a * CAL_FIT_DIV);
    double o = round(-b);
    if (!isfinite(g) || !isfinite(o)) return false;
    if (g < 1.0 || g > (double)CAL_FIT_GAIN_MAX) return false;
    if (fabs(o) >= (double)CAL_FIT_OFF_LIMIT)    return false;
    *gain = (int32_t)g;
    *off  = (int32_t)o;
    return true;
}

cal_fit_status_t cal_fit(const cal_pt_t *pts, int n, int32_t old_off,
                         int32_t *gain, int32_t *off)
{
    double sw = 0, swx = 0, swxx = 0, swt = 0, swxt = 0;
    int used = 0;
    const cal_pt_t *last = NULL;
    for (int i = 0; i < n; i++) {
        if (!pt_usable(pts[i])) continue;
        double w = 1.0 / (pts[i].truth * pts[i].truth);
        sw   += w;
        swx  += w * pts[i].x;
        swxx += w * pts[i].x * pts[i].x;
        swt  += w * pts[i].truth;
        swxt += w * pts[i].x * pts[i].truth;
        used++;
        last = &pts[i];
    }
    if (used == 0) return CAL_FIT_UNCHANGED;

    if (used == 1) {
        double b = -(double)old_off;
        double a = (last->truth - b) / last->x;
        return to_int(a, b, gain, off) ? CAL_FIT_GAIN_ONLY : CAL_FIT_ERROR;
    }

    double den = sw * swxx - swx * swx;
    if (!(den > 1e-9 * sw * swxx)) return CAL_FIT_ERROR;   /* all x (nearly) equal */
    double a = (sw * swxt - swx * swt) / den;
    double b = (swt - a * swx) / sw;
    return to_int(a, b, gain, off) ? CAL_FIT_FULL : CAL_FIT_ERROR;
}

double cal_fit_error_pct(const cal_pt_t &p, int32_t gain, int32_t off)
{
    if (p.truth == 0.0) return 0.0;
    double v = p.x * (double)gain / CAL_FIT_DIV - (double)off;
    return (v - p.truth) / p.truth * 100.0;
}
