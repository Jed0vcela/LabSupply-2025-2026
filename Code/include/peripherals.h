#pragma once
#include "psu_types.h"
#include "flash_store.h"
#include <stdint.h>
#include <stdbool.h>
#include "pico/critical_section.h"

/* ── Encoder ──────────────────────────────────────────────────────────── */
extern volatile int32_t   g_enc_delta;
extern critical_section_t g_enc_cs;

void    encoder_init(void);
int32_t enc_consume(void);

/* ── IR receiver ──────────────────────────────────────────────────────── */
#define PIN_IR           1
#define IR_DISPLAY_US    3000000u

/* Placeholder NEC command bytes — fill in after testing with IR printer */
#define IR_CMD_VI_TOGGLE  0xFF
#define IR_CMD_IS_TOGGLE  0xFF
#define IR_CMD_SEL_VSET   0xFF
#define IR_CMD_SEL_ISET   0xFF
#define IR_CMD_SEL_ISINK  0xFF
#define IR_CMD_ENC_UP     0xFF
#define IR_CMD_ENC_DOWN   0xFF

extern uint32_t  g_ir_code;
extern bool      g_ir_repeat;
extern bool      g_ir_new;
extern uint64_t  g_ir_show_until;

void ir_init(void);          /* must be called AFTER encoder_init() */
bool ir_decode(void);

/* ── Fan ──────────────────────────────────────────────────────────────── */
#define PIN_FAN         22
#define FAN_STARTUP_MS  2000
#define FAN_ON_TEMP_MC  50000
#define FAN_OFF_TEMP_MC 47000

void fan_init(void);
void fan_update(int32_t tfan_mC);

/* ── Beeper ───────────────────────────────────────────────────────────── */
#define PIN_BEEP        14
#define BEEP_DUR_US     100000u

void beeper_init(void);
void beep(void);            /* button click — BEEP_ALL only */
void beep_special(void);    /* errors/continuity — all except MUTE */
void beep_continuous_start(void);   /* continuity tone: start */
void beep_continuous_stop(void);    /* continuity tone: stop  */

/* ── Backlight ────────────────────────────────────────────────────────── */
void backlight_set(uint8_t pct);    /* 1–100 %, clamps automatically */
