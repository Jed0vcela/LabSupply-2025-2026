#pragma once

/**
 * mcp23s17.h  –  MCP23S17 16-bit SPI I/O expander driver
 *
 * Hardwired to SPI0 (shared with ST7735 display).
 * CS pin: GP09  (active LOW)
 * Address pins A2,A1,A0 all tied to GND → device address = 0x00
 *
 * Pin assignment (hardwired, not configurable at runtime):
 *   Outputs: GPA2, GPA3, GPA4, GPA5, GPA6, GPA7
 *   Inputs:  GPA0, GPA1, GPB0-GPB7  (all with internal pull-ups)
 *
 * SPI bus sharing with ST7735:
 *   eeprom_spi_acquire() / st7735_spi_acquire() pattern is reused.
 *   Call mcp23s17_spi_acquire() before any MCP operation.
 *   Call mcp23s17_spi_release() when done.
 *   The display driver's st7735_spi_acquire() restores display speed.
 */

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "st7735.h"   /* for ST7735_SPI_PORT and SPI_BAUD_TFT */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Pin ──────────────────────────────────────────────────────────────── */
#define MCP_CS_PIN       9

/* ── SPI speed ────────────────────────────────────────────────────────── */
#define MCP_SPI_BAUD  8000000u   /* 8 MHz – safe margin below 10 MHz max */

/* ── Output/input masks (compile-time constants matching wiring) ──────── */
/* GPA: bits 7-2 = outputs, bits 1-0 = inputs
 * IODIRA value: 0 = output, 1 = input
 * Outputs = GPA2..GPA7 → IODIRA bits 2-7 = 0 → mask 0x03 (only GPA0,1 input) */
#define MCP_IODIRA   0x03u   /* GPA0=in, GPA1=in, GPA2-7=out */
#define MCP_IODIRB   0xFFu   /* GPB0-7 all inputs             */
#define MCP_GPPUA    0x03u   /* pull-up on GPA0, GPA1 (inputs only) */
#define MCP_GPPUB    0xFFu   /* pull-up on all GPB inputs           */

/* ── Output bit positions within GPIOA (for use with mcp23s17_set_outputs) */
#define MCP_OUT_GPA2   (1u << 2)
#define MCP_OUT_GPA3   (1u << 3)
#define MCP_OUT_GPA4   (1u << 4)
#define MCP_OUT_GPA5   (1u << 5)
#define MCP_OUT_GPA6   (1u << 6)
#define MCP_OUT_GPA7   (1u << 7)

/* ── Public API ───────────────────────────────────────────────────────── */

/**
 * Initialise the MCP23S17: configure directions, pull-ups, and set all
 * outputs LOW.  Call once after st7735_init().
 */
void mcp23s17_init(void);

/**
 * Acquire the SPI bus for MCP23S17 operations.
 * Deasserts TFT CS and sets SPI to MCP speed.
 * Call before any read/write operation; release afterward.
 */
void mcp23s17_spi_acquire(void);

/**
 * Release the SPI bus after MCP23S17 operations.
 * Deasserts MCP CS and restores SPI to display speed.
 * Call st7735_spi_acquire() before the next display operation.
 */
void mcp23s17_spi_release(void);

/**
 * Write the output pins on port A.
 * Only bits corresponding to outputs (GPA2-GPA7) are applied;
 * bits for input pins (GPA0, GPA1) are masked out automatically.
 *
 * @param gpa_value   Byte where bit N corresponds to GPAN.
 *                    e.g. MCP_OUT_GPA3 | MCP_OUT_GPA5 sets GPA3 and GPA5 HIGH.
 */
void mcp23s17_set_outputs(uint8_t gpa_value);

/**
 * Set a single output pin HIGH or LOW.
 *
 * @param pin    One of MCP_OUT_GPAx
 * @param value  true = HIGH, false = LOW
 */
void mcp23s17_set_pin(uint8_t pin, bool value);

/**
 * Read all input pins.
 *
 * @param gpa_inputs  Pointer filled with GPA byte (bits 0-1 = GPA0,GPA1)
 * @param gpb_inputs  Pointer filled with GPB byte (bits 0-7 = GPB0-GPB7)
 */
void mcp23s17_read_inputs(uint8_t *gpa_inputs, uint8_t *gpb_inputs);

/**
 * Read a single input pin by name.
 * Use the register bit position: GPA0=0x01, GPA1=0x02,
 * GPB0=0x100 … GPB7=0x8000 (16-bit mask).
 *
 * Convenience: returns the full 16-bit GPIO state (GPA in low byte,
 * GPB in high byte) if both pointers are NULL.
 */
uint16_t mcp23s17_read_all(void);

/**
 * Get the current latched output value (what was last written, not
 * what the pin is actually doing — same as reading OLATA).
 */
uint8_t mcp23s17_get_outputs(void);

#ifdef __cplusplus
}
#endif