#include "peripherals.h"
#include "st7735.h"
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include "hardware/irq.h"

/* ═══════════════════════════════════════════════════════════════════════
 * Encoder  (GP10 = A, GP11 = B)
 * Gray-code, 4 half-steps per detent, direction reversed (A/B swapped).
 * ═══════════════════════════════════════════════════════════════════════ */
#define ENC_A  10
#define ENC_B  11

volatile int32_t   g_enc_delta = 0;
critical_section_t g_enc_cs;

static const int8_t enc_table[16] = {
     0,-1,+1, 0,  +1, 0, 0,-1,  -1, 0, 0,+1,  0,+1,-1, 0
};
static uint8_t g_enc_state = 0;
static int8_t  g_enc_accum = 0;

/* ── IR state (needed by shared dispatcher) ── */
#define IR_BUF_LEN        68u
#define IR_SILENCE_US     10000u
#define IR_LEADER_MIN_US  8000u
#define IR_LEADER_MAX_US  10000u
#define IR_REPEAT_HIGH_US 2000u
#define IR_BIT1_MIN_US    1400u

static volatile uint64_t g_ir_times[IR_BUF_LEN];
static volatile uint8_t  g_ir_count = 0;
static volatile bool     g_ir_busy  = false;

uint32_t  g_ir_code       = 0;
bool      g_ir_repeat     = false;
bool      g_ir_new        = false;
uint64_t  g_ir_show_until = 0;

/* ── Shared GPIO IRQ dispatcher ──────────────────────────────────────── */
static void gpio_irq_dispatch(uint gpio, uint32_t /*events*/)
{
    if (gpio == ENC_A || gpio == ENC_B) {
        uint8_t a    = gpio_get(ENC_A) ? 1u : 0u;
        uint8_t b    = gpio_get(ENC_B) ? 1u : 0u;
        uint8_t ns   = (uint8_t)((a << 1u) | b);
        uint8_t ns_r = (uint8_t)(((ns & 1u) << 1u) | ((ns >> 1u) & 1u));
        int8_t  d    = enc_table[(g_enc_state << 2) | ns_r];
        g_enc_state  = ns_r;
        if (d == 0) return;
        g_enc_accum += d;
        if (g_enc_accum >= 4) {
            g_enc_accum = 0;
            critical_section_enter_blocking(&g_enc_cs);
            g_enc_delta++;
            critical_section_exit(&g_enc_cs);
        } else if (g_enc_accum <= -4) {
            g_enc_accum = 0;
            critical_section_enter_blocking(&g_enc_cs);
            g_enc_delta--;
            critical_section_exit(&g_enc_cs);
        }
    } else if (gpio == PIN_IR) {
        uint64_t t = time_us_64();
        if (!g_ir_busy) { g_ir_count = 0; g_ir_busy = true; }
        if (g_ir_count < IR_BUF_LEN)
            g_ir_times[g_ir_count++] = t;
    }
}

void encoder_init(void)
{
    critical_section_init(&g_enc_cs);
    gpio_init(ENC_A); gpio_set_dir(ENC_A, GPIO_IN); gpio_pull_up(ENC_A);
    gpio_init(ENC_B); gpio_set_dir(ENC_B, GPIO_IN); gpio_pull_up(ENC_B);
    uint8_t a = gpio_get(ENC_A) ? 1u : 0u;
    uint8_t b = gpio_get(ENC_B) ? 1u : 0u;
    uint8_t s = (uint8_t)((a << 1u) | b);
    g_enc_state = (uint8_t)(((s & 1u) << 1u) | ((s >> 1u) & 1u));
    gpio_set_irq_enabled_with_callback(ENC_A,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, gpio_irq_dispatch);
    gpio_set_irq_enabled(ENC_B,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
}

int32_t enc_consume(void)
{
    int32_t d;
    critical_section_enter_blocking(&g_enc_cs);
    d = g_enc_delta; g_enc_delta = 0;
    critical_section_exit(&g_enc_cs);
    return d;
}

/* ═══════════════════════════════════════════════════════════════════════
 * IR receiver  (GP1, NEC protocol, HW pull-up)
 * Must be called AFTER encoder_init() (shared IRQ handler).
 * ═══════════════════════════════════════════════════════════════════════ */
void ir_init(void)
{
    gpio_init(PIN_IR);
    gpio_set_dir(PIN_IR, GPIO_IN);
    gpio_set_irq_enabled(PIN_IR,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
}

bool ir_decode(void)
{
    if (!g_ir_busy) return false;
    uint8_t cnt = g_ir_count;
    if (cnt == 0) return false;
    if ((time_us_64() - g_ir_times[cnt - 1]) < IR_SILENCE_US) return false;

    uint64_t times[IR_BUF_LEN];
    memcpy(times, (const void *)g_ir_times, cnt * sizeof(uint64_t));
    g_ir_busy = false; g_ir_count = 0;

    if (cnt < 3) return false;

    uint32_t widths[IR_BUF_LEN - 1];
    for (uint8_t i = 0; i < cnt - 1; i++)
        widths[i] = (uint32_t)(times[i + 1] - times[i]);

    if (widths[0] < IR_LEADER_MIN_US || widths[0] > IR_LEADER_MAX_US) return false;
    if (cnt < 4) return false;

    if (widths[1] < IR_REPEAT_HIGH_US) {
        g_ir_repeat = true; g_ir_new = true; return true;
    }
    if (cnt < 67) return false;

    uint32_t code = 0;
    for (uint8_t bit = 0; bit < 32; bit++) {
        uint8_t idx = (uint8_t)(2 + bit * 2 + 1);
        if (idx >= cnt - 1) return false;
        uint32_t total = widths[2 + bit*2] + widths[idx];
        if (total > IR_BIT1_MIN_US) code |= (1u << bit);
    }

    uint8_t addr  = (uint8_t)(code & 0xFF);
    uint8_t naddr = (uint8_t)((code >> 8) & 0xFF);
    uint8_t cmd   = (uint8_t)((code >> 16) & 0xFF);
    uint8_t ncmd  = (uint8_t)((code >> 24) & 0xFF);
    if ((addr ^ naddr) != 0xFF || (cmd ^ ncmd) != 0xFF) return false;

    g_ir_code = code; g_ir_repeat = false; g_ir_new = true;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Fan  (GP22, active HIGH)
 * ═══════════════════════════════════════════════════════════════════════ */
static uint64_t g_fan_startup_end_us = 0;

void fan_init(void)
{
    gpio_init(PIN_FAN);
    gpio_set_dir(PIN_FAN, GPIO_OUT);
    gpio_put(PIN_FAN, 1);
    g_fan_startup_end_us = time_us_64() + (uint64_t)FAN_STARTUP_MS * 1000u;
}

void fan_update(int32_t tfan_mC)
{
    static bool fan_on = true;
    if (time_us_64() < g_fan_startup_end_us) { gpio_put(PIN_FAN, 1); return; }
    if (!fan_on && tfan_mC >= FAN_ON_TEMP_MC)    fan_on = true;
    else if (fan_on && tfan_mC < FAN_OFF_TEMP_MC) fan_on = false;
    gpio_put(PIN_FAN, fan_on ? 1 : 0);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Beeper  (GP14, PWM ~2700 Hz)
 * beep()             – 100 ms click, BEEP_ALL only
 * beep_special()     – 100 ms, BEEP_ALL + BEEP_SPECIAL
 * beep_continuous_*  – continuous tone (continuity checker), stops on call
 * ═══════════════════════════════════════════════════════════════════════ */
#define BEEP_PWM_DIV    8u
#define BEEP_PWM_WRAP   5786u
#define BEEP_PWM_LEVEL  (BEEP_PWM_WRAP / 2u)
#define BEEP_ALARM_NUM  1

static uint g_beep_slice    = 0;
static bool g_beep_cont_on  = false;

static void beep_hw_start(void)
{
    hw_clear_bits(&timer_hw->inte, 1u << BEEP_ALARM_NUM);
    hw_clear_bits(&timer_hw->intr, 1u << BEEP_ALARM_NUM);
    gpio_set_function(PIN_BEEP, GPIO_FUNC_PWM);
    pwm_set_chan_level(g_beep_slice, PWM_CHAN_A, BEEP_PWM_LEVEL);
    pwm_set_enabled(g_beep_slice, true);
}

static void beep_hw_stop(void)
{
    pwm_set_chan_level(g_beep_slice, PWM_CHAN_A, 0);
    pwm_set_enabled(g_beep_slice, false);
    gpio_set_function(PIN_BEEP, GPIO_FUNC_SIO);
    gpio_put(PIN_BEEP, 0);
}

static void beep_alarm_irq(void)
{
    hw_clear_bits(&timer_hw->intr, 1u << BEEP_ALARM_NUM);
    if (!g_beep_cont_on) beep_hw_stop();
}

void beeper_init(void)
{
    gpio_set_function(PIN_BEEP, GPIO_FUNC_PWM);
    g_beep_slice = pwm_gpio_to_slice_num(PIN_BEEP);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, BEEP_PWM_DIV);
    pwm_config_set_wrap(&cfg, BEEP_PWM_WRAP);
    pwm_init(g_beep_slice, &cfg, false);
    pwm_set_chan_level(g_beep_slice, PWM_CHAN_A, 0);
    hw_set_bits(&timer_hw->inte, 1u << BEEP_ALARM_NUM);
    irq_set_exclusive_handler(TIMER_IRQ_1, beep_alarm_irq);
    irq_set_enabled(TIMER_IRQ_1, true);
    gpio_set_function(PIN_BEEP, GPIO_FUNC_SIO);
    gpio_init(PIN_BEEP);
    gpio_set_dir(PIN_BEEP, GPIO_OUT);
    gpio_put(PIN_BEEP, 0);
}

static void beep_timed(void)
{
    g_beep_cont_on = false;
    beep_hw_start();
    hw_set_bits(&timer_hw->inte, 1u << BEEP_ALARM_NUM);
    timer_hw->alarm[BEEP_ALARM_NUM] = (uint32_t)(time_us_64() + BEEP_DUR_US);
}

void beep(void)
{
    if ((beep_mode_t)g_cal.beep_mode == BEEP_ALL) beep_timed();
}

void beep_special(void)
{
    if ((beep_mode_t)g_cal.beep_mode != BEEP_MUTE) beep_timed();
}

void beep_continuous_start(void)
{
    if ((beep_mode_t)g_cal.beep_mode == BEEP_MUTE) return;
    if (g_beep_cont_on) return;
    g_beep_cont_on = true;
    /* Cancel any pending timed alarm */
    hw_clear_bits(&timer_hw->inte, 1u << BEEP_ALARM_NUM);
    hw_clear_bits(&timer_hw->intr, 1u << BEEP_ALARM_NUM);
    beep_hw_start();
}

void beep_continuous_stop(void)
{
    if (!g_beep_cont_on) return;
    g_beep_cont_on = false;
    beep_hw_stop();
    /* Re-enable alarm IRQ for future timed beeps */
    hw_set_bits(&timer_hw->inte, 1u << BEEP_ALARM_NUM);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Backlight  (GP5, driven by st7735 driver)
 * ═══════════════════════════════════════════════════════════════════════ */
void backlight_set(uint8_t pct)
{
    if (pct < 1)   pct = 1;
    if (pct > 100) pct = 100;
    st7735_set_backlight(pct);
}
