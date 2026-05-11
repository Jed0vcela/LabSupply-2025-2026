/**
 * mcp23s17.c  –  MCP23S17 16-bit SPI I/O expander driver
 *
 * SPI protocol (MCP23S17 in default HAEN=0 mode):
 *   Frame: [opcode] [register] [data]
 *   Opcode write: 0x40 | (addr<<1) | 0 = 0x40 for addr=0
 *   Opcode read:  0x40 | (addr<<1) | 1 = 0x41 for addr=0
 *   CPOL=0, CPHA=0 (Mode 0), MSB first
 *
 * Bugs fixed vs. previous version:
 *   1. spi_tx() wrote to TX FIFO but never read the corresponding RX byte.
 *      After 8 calls the RX FIFO (depth=8) was full, TX FIFO stalled,
 *      the chip received nothing and nothing ever worked.
 *      Fix: every TX byte is always paired with an RX drain — one function
 *      spi_byte() handles both, same approach as the ST7735 driver.
 *   2. set_outputs() and set_pin() called reg_write() without acquiring
 *      the SPI bus first — could run at 24 MHz (display speed) instead
 *      of 8 MHz, causing framing errors on the MCP23S17.
 *      Fix: acquire/release wrapped around every public write call.
 *   3. mcp_cs_high() waited for spi_is_busy() but the first two bytes
 *      of a 3-byte write had no such wait — CS could deassert mid-byte.
 *      Fix: spi_byte() waits for each byte to complete individually.
 */

#include "mcp23s17.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

/* ── Register addresses (IOCON.BANK=0, sequential layout) ──────────── */
#define REG_IODIRA  0x00u
#define REG_IODIRB  0x01u
#define REG_GPPUA   0x0Cu
#define REG_GPPUB   0x0Du
#define REG_GPIOA   0x12u
#define REG_GPIOB   0x13u
#define REG_OLATA   0x14u
#define REG_OLATB   0x15u

/* ── Opcodes (hardware address = 0b000) ─────────────────────────────── */
#define MCP_WR  0x40u
#define MCP_RD  0x41u

/* ── Internal state ─────────────────────────────────────────────────── */
static uint8_t _output_latch = 0x00u;

/* ── GPIO helpers ────────────────────────────────────────────────────── */
#define NOP3  __asm volatile("nop\nnop\nnop")

static inline void mcp_cs_low(void)
{
    NOP3; gpio_put(MCP_CS_PIN, 0); NOP3;
}

static inline void mcp_cs_high(void)
{
    NOP3; gpio_put(MCP_CS_PIN, 1); NOP3;
}

/* ── Core SPI byte: transmit b, wait for completion, drain RX ──────────
 * The RP2040 SPI is full-duplex: every TX byte produces an RX byte.
 * We must read it out each time or the RX FIFO fills up (depth=8) and
 * the TX FIFO stalls.  We also wait for the byte to fully clock out
 * before returning so the caller can safely deassert CS afterward.     */
static inline uint8_t spi_byte(uint8_t b)
{
    /* Wait for TX FIFO space */
    while (!spi_is_writable(ST7735_SPI_PORT)) tight_loop_contents();
    /* Write byte to shift register */
    spi_get_hw(ST7735_SPI_PORT)->dr = b;
    /* Wait until the byte has been fully clocked out AND RX byte arrived */
    while (!spi_is_readable(ST7735_SPI_PORT)) tight_loop_contents();
    /* Read and return the received byte (caller discards if not needed) */
    return (uint8_t)spi_get_hw(ST7735_SPI_PORT)->dr;
}

/* ── Register access ─────────────────────────────────────────────────── */

/* Write one register — CS is asserted/deasserted here */
static void reg_write(uint8_t reg, uint8_t value)
{
    mcp_cs_low();
    spi_byte(MCP_WR);
    spi_byte(reg);
    spi_byte(value);
    mcp_cs_high();
}

/* Read one register */
static uint8_t reg_read(uint8_t reg)
{
    mcp_cs_low();
    spi_byte(MCP_RD);       /* send opcode   */
    spi_byte(reg);          /* send register */
    uint8_t val = spi_byte(0x00u);  /* dummy TX, capture RX */
    mcp_cs_high();
    return val;
}

/* Read two consecutive registers in one transaction (SEQOP=0 default) */
static uint16_t reg_read16(uint8_t start_reg)
{
    mcp_cs_low();
    spi_byte(MCP_RD);
    spi_byte(start_reg);
    uint8_t lo = spi_byte(0x00u);
    uint8_t hi = spi_byte(0x00u);
    mcp_cs_high();
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

/* ── Bus arbitration ─────────────────────────────────────────────────── */
void mcp23s17_spi_acquire(void)
{
    gpio_put(PIN_CS_TFT, 1);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    spi_set_baudrate(ST7735_SPI_PORT, MCP_SPI_BAUD);
    /* Drain any stale bytes that arrived in the RX FIFO while MISO was
     * disconnected (the SPI peripheral may have clocked in garbage). */
    while (spi_is_readable(ST7735_SPI_PORT))
        (void)spi_get_hw(ST7735_SPI_PORT)->dr;
}

void mcp23s17_spi_release(void)
{
    spi_set_baudrate(ST7735_SPI_PORT, SPI_BAUD_TFT);  /* restore display speed */
    /* Disconnect MISO from SPI and pull it down so the MCP23S17's SDO
     * pin cannot load the bus or cause noise during display writes.     */
    gpio_init(PIN_MISO);
    gpio_set_dir(PIN_MISO, GPIO_IN);
    gpio_pull_down(PIN_MISO);
}

/* ── Public API ──────────────────────────────────────────────────────── */
void mcp23s17_init(void)
{
    gpio_init(MCP_CS_PIN);
    gpio_set_dir(MCP_CS_PIN, GPIO_OUT);
    gpio_put(MCP_CS_PIN, 1);   /* deasserted */

    mcp23s17_spi_acquire();

    /* Direction registers: GPA0-1=input, GPA2-7=output; GPB all input */
    reg_write(REG_IODIRA, MCP_IODIRA);
    reg_write(REG_IODIRB, MCP_IODIRB);

    /* Enable pull-ups on all input pins */
    reg_write(REG_GPPUA, MCP_GPPUA);
    reg_write(REG_GPPUB, MCP_GPPUB);

    /* Outputs start LOW */
    _output_latch = 0x00u;
    reg_write(REG_OLATA, _output_latch);

    mcp23s17_spi_release();
}

void mcp23s17_set_outputs(uint8_t gpa_value)
{
    /* Mask off input bits so we never accidentally drive GPA0/GPA1 */
    _output_latch = (uint8_t)(gpa_value & ~MCP_IODIRA);
    mcp23s17_spi_acquire();
    reg_write(REG_OLATA, _output_latch);
    mcp23s17_spi_release();
}

void mcp23s17_set_pin(uint8_t pin_mask, bool value)
{
    if (value)
        _output_latch |=  (uint8_t)(pin_mask & ~MCP_IODIRA);
    else
        _output_latch &= (uint8_t)~(pin_mask & ~MCP_IODIRA);
    mcp23s17_spi_acquire();
    reg_write(REG_OLATA, _output_latch);
    mcp23s17_spi_release();
}

void mcp23s17_read_inputs(uint8_t *gpa_inputs, uint8_t *gpb_inputs)
{
    mcp23s17_spi_acquire();
    uint16_t v = reg_read16(REG_GPIOA);
    mcp23s17_spi_release();
    /* Return only the input bits (mask out output pins on GPA) */
    if (gpa_inputs) *gpa_inputs = (uint8_t)(v & 0xFFu) & MCP_IODIRA;
    if (gpb_inputs) *gpb_inputs = (uint8_t)(v >> 8);
}

uint16_t mcp23s17_read_all(void)
{
    mcp23s17_spi_acquire();
    uint16_t v = reg_read16(REG_GPIOA);
    mcp23s17_spi_release();
    return v;
}

uint8_t mcp23s17_get_outputs(void)
{
    return _output_latch;
}