// Host test for the calibration fit: g++ -I../include test_cal_fit.cpp ../src/cal_fit.cpp -o /tmp/t && /tmp/t
#include "cal_fit.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static double frand() { return (double)rand() / RAND_MAX * 2.0 - 1.0; }

int main()
{
    int32_t g = -1, o = -1;

    // no points / only unusable points -> unchanged, outputs untouched
    CHECK(cal_fit(NULL, 0, 5, &g, &o) == CAL_FIT_UNCHANGED && g == -1 && o == -1);
    cal_pt_t bad[3] = {{0, 100}, {100, 0}, {-5, 10}};
    CHECK(cal_fit(bad, 3, 5, &g, &o) == CAL_FIT_UNCHANGED);

    // exact line, known a=11.1, b=-170 (gain 111000, off 170)
    {
        double a = 11.1, b = -170.0;
        cal_pt_t p[3];
        double xs[3] = {200, 1700, 3000};
        for (int i = 0; i < 3; i++) { p[i].x = xs[i]; p[i].truth = a * xs[i] + b; }
        CHECK(cal_fit(p, 3, 0, &g, &o) == CAL_FIT_FULL);
        CHECK(g == 111000 && o == 170);
        for (int i = 0; i < 3; i++) CHECK(fabs(cal_fit_error_pct(p[i], g, o)) < 0.01);
    }

    // noisy data: recovered parameters close, residuals small
    {
        srand(1);
        double a = 2.119, b = -17.0;
        cal_pt_t p[4];
        double xs[4] = {50, 400, 1500, 2900};
        for (int i = 0; i < 4; i++) { p[i].x = xs[i]; p[i].truth = (a * xs[i] + b) * (1.0 + 0.001 * frand()); }
        CHECK(cal_fit(p, 4, 0, &g, &o) == CAL_FIT_FULL);
        CHECK(abs(g - 21190) < 60);
        CHECK(abs(o - 17) < 4);
        for (int i = 0; i < 4; i++) CHECK(fabs(cal_fit_error_pct(p[i], g, o)) < 0.5);
    }

    // weighting: the low point must be matched better than the high one in relative terms
    {
        cal_pt_t p[3] = {{10, 10}, {1000, 1010}, {2000, 1990}};
        CHECK(cal_fit(p, 3, 0, &g, &o) == CAL_FIT_FULL);
        double e0 = fabs(cal_fit_error_pct(p[0], g, o));
        double e2 = fabs(cal_fit_error_pct(p[2], g, o));
        CHECK(e0 < 5.0 && e2 < 5.0);
    }

    // single point: gain only, old offset kept (off=100 -> b=-100): truth=1900 at x=1000 -> a=2.0
    {
        cal_pt_t p = {1900, 1000};
        CHECK(cal_fit(&p, 1, 100, &g, &o) == CAL_FIT_GAIN_ONLY);
        CHECK(g == 20000 && o == 100);
    }

    // single point plus unusable ones behaves like one point
    {
        cal_pt_t p[2] = {{0, 5}, {1000, 500}};
        CHECK(cal_fit(p, 2, 0, &g, &o) == CAL_FIT_GAIN_ONLY);
        CHECK(g == 20000 && o == 0);
    }

    // degenerate: two points with identical x
    {
        cal_pt_t p[2] = {{100, 50}, {200, 50}};
        CHECK(cal_fit(p, 2, 0, &g, &o) == CAL_FIT_ERROR);
    }

    // negative gain (truth falls while x rises)
    {
        cal_pt_t p[2] = {{1000, 100}, {100, 1000}};
        CHECK(cal_fit(p, 2, 0, &g, &o) == CAL_FIT_ERROR);
    }

    // gain overflow: gain/div >= 100 would overflow int32 in setpt_disp
    {
        cal_pt_t p = {100000, 100};
        CHECK(cal_fit(&p, 1, 0, &g, &o) == CAL_FIT_ERROR);
        cal_pt_t q = {99000, 1000};   // a = 99 -> gain 990000, still fine
        CHECK(cal_fit(&q, 1, 0, &g, &o) == CAL_FIT_GAIN_ONLY && g == 990000);
    }

    // offset out of range
    {
        cal_pt_t p[2] = {{100, 1000}, {40000, 3000}};   // b ~ -19850 -> off 19850
        CHECK(cal_fit(p, 2, 0, &g, &o) == CAL_FIT_ERROR);
    }

    // non-finite input ignored
    {
        cal_pt_t p[2] = {{NAN, 10}, {INFINITY, 10}};
        CHECK(cal_fit(p, 2, 0, &g, &o) == CAL_FIT_UNCHANGED);
    }

    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails ? 1 : 0;
}
