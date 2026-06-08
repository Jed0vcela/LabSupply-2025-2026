#pragma once
#include "psu_types.h"
#include "flash_store.h"
#include <stdint.h>
#include <stdbool.h>

#define DISPLAY_PERIOD_MS  100u

/* ── field_t helpers ─────────────────────────────────────────────────── */
bool update_field(field_t &f, const char *str);
void field_invalidate(field_t &f);

/* ── Formatting ──────────────────────────────────────────────────────── */
/* val in milli-units, dec = decimal places, unit appended */
void fmt_fp(char *b, int32_t val, uint8_t dec, const char *unit);
void fmt_temp(char *b, int32_t mC);

/* ── Moving average ──────────────────────────────────────────────────── */
void     mav_init(mav_t *m, int32_t v);
int32_t  mav_update(mav_t *m, int32_t s);

/* ── On-screen message system ────────────────────────────────────────
 * One message shown at a time in an overlay bar at Y_FOOT0.
 * Higher severity replaces lower. Timeout = 0 means sticky (until cleared).
 * Call msg_update() every display tick; call msg_clear() to dismiss.
 */
typedef enum {
    MSG_NONE = 0,
    MSG_INFO,    /* cyan   */
    MSG_WARN,    /* orange */
    MSG_ERROR    /* red    */
} msg_severity_t;

/* Post a message. severity, text (max 26 chars), timeout_ms (0=sticky) */
void msg_post(msg_severity_t sev, const char *text, uint32_t timeout_ms);
void msg_clear(void);
/* Call every display tick — draws/clears overlay, handles timeout */
void msg_update(void);

/* ── Continuity mode state (defined in display.cpp, used by main.cpp) ── */
#define CONT_VSET   0
#define CONT_ISET   1
#define CONT_THR    2
#define CONT_NUM_CH 3

extern int32_t g_cont_dac[2];   /* [0]=Vset counts, [1]=Iset counts */
extern int32_t g_cont_thr;      /* threshold in DAC counts           */
extern uint8_t g_cont_active;   /* 0=Vset, 1=Iset, 2=Threshold      */

/* ── Mode: Supply+Sink (MODE_SUPPLY_SINK) ────────────────────────────── */
void draw_supply_sink_static(void);
void draw_supply_sink_update(const adc_bundle_t &adc, int32_t mult);

/* ── Mode: Supply only (MODE_SUPPLY) ─────────────────────────────────── */
void draw_supply_static(void);
void draw_supply_update(const adc_bundle_t &adc, int32_t mult);

/* ── Mode: Continuity (MODE_CONTINUITY) ─────────────────────────────── */
void draw_continuity_static(void);
void draw_continuity_update(const adc_bundle_t &adc, int32_t mult);

/* ── IR overlay (shared across modes) ───────────────────────────────── */
void ir_overlay_update(void);