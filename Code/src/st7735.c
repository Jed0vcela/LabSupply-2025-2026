/**
 * st7735.c - ST7735 driver for Raspberry Pi Pico
 *
 * Directly ported from bablokb/pico-st7735 (Bernhard Bablok) which itself
 * is based on Gavin Lyons' PIC library.  The SPI write pattern (CS pulse
 * per byte, DC before CS) and rotation logic (_xstart/_ystart swap) are
 * taken verbatim from that working reference.
 *
 * Key differences from previous attempts:
 *  - _colstart=2, _rowstart=1 set during init (green-tab offsets)
 *  - _xstart/_ystart SWAP on rotation 1 and 3 (reference lines 715-716)
 *  - tft_dc_low() called once before Rcmd1() as reference does
 *  - drawPixel uses x,y,x+1,y+1 window (reference line 288)
 *  - pixel streaming always: setAddrWindow → tft_dc_high → tft_cs_low
 *    → stream bytes → tft_cs_high  (never holding CS across commands)
 */

#include "st7735.h"
#include "hardware/spi.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

/* ── ST7735 commands ──────────────────────────────────────────────────── */
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT  0x11
#define ST7735_NORON   0x13
#define ST7735_INVOFF  0x20
#define ST7735_DISPON  0x29
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C
#define ST7735_COLMOD  0x3A
#define ST7735_MADCTL  0x36
#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR  0xB4
#define ST7735_PWCTR1  0xC0
#define ST7735_PWCTR2  0xC1
#define ST7735_PWCTR3  0xC2
#define ST7735_PWCTR4  0xC3
#define ST7735_PWCTR5  0xC4
#define ST7735_VMCTR1  0xC5
#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1

#define ST7735_MADCTL_MY  0x80
#define ST7735_MADCTL_MX  0x40
#define ST7735_MADCTL_MV  0x20
#define ST7735_MADCTL_RGB 0x00

/* ── 5x7 font (column-major, bit0=top) ───────────────────────────────── */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20 ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* 0x21 '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* 0x22 '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23 '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24 '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* 0x25 '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* 0x26 '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* 0x27 ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 0x28 '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* 0x29 ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* 0x2A '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* 0x2C ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* 0x2D '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* 0x2E '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* 0x2F '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30 '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* 0x31 '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* 0x32 '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* 0x33 '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* 0x34 '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* 0x35 '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36 '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* 0x37 '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* 0x38 '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* 0x39 '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* 0x3A ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* 0x3B ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* 0x3C '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* 0x3D '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* 0x3E '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* 0x3F '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* 0x40 '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 0x42 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 0x43 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 0x45 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 0x46 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 0x49 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 0x4D 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 0x50 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 0x52 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 0x53 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 0x54 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 0x58 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 0x59 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 0x5A 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* 0x5C '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* 0x5E '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* 0x5F '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* 0x60 '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 0x61 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 0x62 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 0x63 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 0x64 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 0x65 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 0x66 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 0x68 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 0x69 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 0x6F 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 0x70 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 0x71 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 0x72 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 0x73 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 0x74 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 0x75 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 0x78 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 0x79 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* 0x7B '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* 0x7D '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* 0x7E '~' */
    {0x78,0x46,0x41,0x46,0x78}, /* 0x7F DEL  */
};

/* ── Module state ─────────────────────────────────────────────────────── */
static uint8_t _rotation = 0;
static uint8_t _colstart = 2;   /* green-tab offset */
static uint8_t _rowstart = 1;   /* green-tab offset */
static uint8_t _xstart   = 2;   /* active x offset (swaps on rotation) */
static uint8_t _ystart   = 1;   /* active y offset (swaps on rotation) */
static uint8_t _width    = ST7735_WIDTH;
static uint8_t _height   = ST7735_HEIGHT;
static uint    _pwm_slice;
static uint    _pwm_chan;

/* ── GPIO helpers (reference hw.h style with NOPs) ───────────────────── */
#define NOP3  __asm volatile("nop\nnop\nnop")

static inline void tft_cs_low(void)  { NOP3; gpio_put(PIN_CS_TFT,0); NOP3; }
static inline void tft_cs_high(void) { NOP3; gpio_put(PIN_CS_TFT,1); NOP3; }
static inline void tft_dc_low(void)  { NOP3; gpio_put(PIN_DC,     0); NOP3; }
static inline void tft_dc_high(void) { NOP3; gpio_put(PIN_DC,     1); NOP3; }
static inline void tft_rst_low(void) { NOP3; gpio_put(PIN_RST,    0); NOP3; }
static inline void tft_rst_high(void){ NOP3; gpio_put(PIN_RST,    1); NOP3; }

/* spiwrite: write-only, never reads MISO.
 * spi_write_blocking() reads back one RX byte per TX byte — if MISO is
 * not wired to the SPI peripheral it waits forever for RX data.
 * We write directly to the TX FIFO and wait only for TX completion. */
static inline void spiwrite(uint8_t b)
{
    /* Wait for TX FIFO space */
    while (!spi_is_writable(ST7735_SPI_PORT)) tight_loop_contents();
    /* Write the byte */
    spi_get_hw(ST7735_SPI_PORT)->dr = b;
    /* Wait until shift register is idle (byte fully clocked out) */
    while (spi_is_busy(ST7735_SPI_PORT)) tight_loop_contents();
    /* Drain RX FIFO if anything arrived (prevents overflow stall) */
    while (spi_is_readable(ST7735_SPI_PORT))
        (void)spi_get_hw(ST7735_SPI_PORT)->dr;
}

/* write_command / write_data: exact copy of reference library */
static void write_command(uint8_t cmd)
{
    tft_dc_low();
    tft_cs_low();
    spiwrite(cmd);
    tft_cs_high();
}

static void write_data(uint8_t data)
{
    tft_dc_high();
    tft_cs_low();
    spiwrite(data);
    tft_cs_high();
}

/* ── Init sequences: exact copy of reference Rcmd1/Rcmd2green/Rcmd3 ──── */
static void Rcmd1(void)
{
    write_command(ST7735_SWRESET); sleep_ms(150);
    write_command(ST7735_SLPOUT);  sleep_ms(500);
    write_command(ST7735_FRMCTR1);
      write_data(0x01); write_data(0x2C); write_data(0x2D);
    write_command(ST7735_FRMCTR2);
      write_data(0x01); write_data(0x2C); write_data(0x2D);
    write_command(ST7735_FRMCTR3);
      write_data(0x01); write_data(0x2C); write_data(0x2D);
      write_data(0x01); write_data(0x2C); write_data(0x2D);
    write_command(ST7735_INVCTR);  write_data(0x07);
    write_command(ST7735_PWCTR1);  write_data(0xA2); write_data(0x02); write_data(0x84);
    write_command(ST7735_PWCTR2);  write_data(0xC5);
    write_command(ST7735_PWCTR3);  write_data(0x0A); write_data(0x00);
    write_command(ST7735_PWCTR4);  write_data(0x8A); write_data(0x2A);
    write_command(ST7735_PWCTR5);  write_data(0x8A); write_data(0xEE);
    write_command(ST7735_VMCTR1);  write_data(0x0E);
    write_command(ST7735_INVOFF);
    write_command(ST7735_MADCTL);  write_data(0xC8);
    write_command(ST7735_COLMOD);  write_data(0x05);
}

static void Rcmd2green(void)
{
    write_command(ST7735_CASET);
      write_data(0x00); write_data(0x02);
      write_data(0x00); write_data(0x7F + 0x02);
    write_command(ST7735_RASET);
      write_data(0x00); write_data(0x01);
      write_data(0x00); write_data(0x9F + 0x01);
}

static void Rcmd3(void)
{
    write_command(ST7735_GMCTRP1);
      write_data(0x02); write_data(0x1C); write_data(0x07); write_data(0x12);
      write_data(0x37); write_data(0x32); write_data(0x29); write_data(0x2D);
      write_data(0x29); write_data(0x25); write_data(0x2B); write_data(0x39);
      write_data(0x00); write_data(0x01); write_data(0x03); write_data(0x10);
    write_command(ST7735_GMCTRN1);
      write_data(0x03); write_data(0x1D); write_data(0x07); write_data(0x06);
      write_data(0x2E); write_data(0x2C); write_data(0x29); write_data(0x2D);
      write_data(0x2E); write_data(0x2E); write_data(0x37); write_data(0x3F);
      write_data(0x00); write_data(0x00); write_data(0x02); write_data(0x10);
    write_command(ST7735_NORON);  sleep_ms(10);
    write_command(ST7735_DISPON); sleep_ms(100);
}

/* ── setAddrWindow: exact copy of reference ──────────────────────────── */
static void setAddrWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    write_command(ST7735_CASET);
      write_data(0); write_data((uint8_t)(x0 + _xstart));
      write_data(0); write_data((uint8_t)(x1 + _xstart));
    write_command(ST7735_RASET);
      write_data(0); write_data((uint8_t)(y0 + _ystart));
      write_data(0); write_data((uint8_t)(y1 + _ystart));
    write_command(ST7735_RAMWR);
}

/* ── Public API ───────────────────────────────────────────────────────── */

void st7735_spi_acquire(void)
{
    spi_set_baudrate(ST7735_SPI_PORT, SPI_BAUD_TFT);
    tft_cs_low();
}

void st7735_spi_release(void)
{
    tft_cs_high();
}

void eeprom_spi_acquire(void)
{
    tft_cs_high();
    spi_set_baudrate(ST7735_SPI_PORT, SPI_BAUD_EEPROM);
}

void st7735_set_backlight(uint8_t percent)
{
    if (percent > 100u) percent = 100u;
    pwm_set_chan_level(_pwm_slice, _pwm_chan, (uint16_t)percent * 100u);
}

void st7735_init(void)
{
    /* SPI */
    spi_init(ST7735_SPI_PORT, SPI_BAUD_TFT);
    spi_set_format(ST7735_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    /* MISO is NOT configured as SPI - ST7735 is write-only.
     * Leaving it as a pulled-down GPIO prevents the floating pin from
     * coupling noise into the SPI bus or stalling spi_write_blocking. */
    gpio_init(PIN_MISO);
    gpio_set_dir(PIN_MISO, GPIO_IN);
    gpio_pull_down(PIN_MISO);

    /* Control GPIOs */
    gpio_init(PIN_CS_TFT); gpio_set_dir(PIN_CS_TFT, GPIO_OUT); tft_cs_high();
    gpio_init(PIN_DC);     gpio_set_dir(PIN_DC,     GPIO_OUT); tft_dc_low();
    gpio_init(PIN_RST);    gpio_set_dir(PIN_RST,    GPIO_OUT);

    /* PWM backlight */
    gpio_set_function(PIN_LED, GPIO_FUNC_PWM);
    _pwm_slice = pwm_gpio_to_slice_num(PIN_LED);
    _pwm_chan  = pwm_gpio_to_channel(PIN_LED);
    pwm_set_wrap(_pwm_slice, 10000u);
    pwm_set_chan_level(_pwm_slice, _pwm_chan, 0u);
    pwm_set_enabled(_pwm_slice, true);

    /* Hardware reset */
    tft_rst_high(); sleep_ms(10);
    tft_rst_low();  sleep_ms(10);
    tft_rst_high(); sleep_ms(120);

    /* Init: reference calls tft_dc_low() THEN Rcmd1/2/3 */
    tft_dc_low();
    Rcmd1();
    Rcmd2green();
    Rcmd3();

    /* Clear the ENTIRE physical GRAM (132x162) before setting rotation.
     * The ST7735 chip has more physical pixels than the logical display.
     * Any physical pixel not covered by our logical window retains its
     * power-on GRAM value (random noise) and stays visible at the edges.
     * Writing black to every physical pixel eliminates this.
     * We do this in portrait mode with zero offsets so the window covers
     * the full 132x162 chip, then set the real rotation afterward. */
    _xstart = 0; _ystart = 0;
    _width  = 132; _height = 162;
    st7735_fill_screen(0x0000);   /* black */

    _colstart = 2;
    _rowstart = 1;

    st7735_set_rotation(0);
    st7735_set_backlight(80);
}

/* setRotation: exact copy of reference lines 697-735 */
void st7735_set_rotation(uint8_t m)
{
    uint8_t madctl = 0;
    _rotation = m % 4;
    switch (_rotation) {
    case 0:
        madctl   = ST7735_MADCTL_MX | ST7735_MADCTL_MY | ST7735_MADCTL_RGB;
        _height  = ST7735_HEIGHT; _width = ST7735_WIDTH;
        _xstart  = _colstart;    _ystart = _rowstart;
        break;
    case 1:
        madctl   = ST7735_MADCTL_MY | ST7735_MADCTL_MV | ST7735_MADCTL_RGB;
        _width   = ST7735_HEIGHT; _height = ST7735_WIDTH;
        _ystart  = _colstart;    _xstart = _rowstart;  /* NOTE: swapped */
        break;
    case 2:
        madctl   = ST7735_MADCTL_RGB;
        _height  = ST7735_HEIGHT; _width = ST7735_WIDTH;
        _xstart  = _colstart;    _ystart = _rowstart;
        break;
    case 3:
        madctl   = ST7735_MADCTL_MX | ST7735_MADCTL_MV | ST7735_MADCTL_RGB;
        _width   = ST7735_HEIGHT; _height = ST7735_WIDTH;
        _ystart  = _colstart;    _xstart = _rowstart;  /* NOTE: swapped */
        break;
    }
    write_command(ST7735_MADCTL);
    write_data(madctl);
}

uint8_t st7735_width(void)  { return _width;  }
uint8_t st7735_height(void) { return _height; }

/* fillRectangle: exact copy of reference lines 219-238 */
void st7735_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
    uint8_t hi, lo, xi, yi;
    if (x >= _width || y >= _height) return;
    if ((x + w - 1) >= _width)  w = _width  - x;
    if ((y + h - 1) >= _height) h = _height - y;
    setAddrWindow(x, y, (uint8_t)(x+w-1), (uint8_t)(y+h-1));
    hi = (uint8_t)(color >> 8); lo = (uint8_t)(color & 0xFF);
    tft_dc_high();
    tft_cs_low();
    for (yi = h; yi > 0; yi--) {
        for (xi = w; xi > 0; xi--) {
            spiwrite(hi);
            spiwrite(lo);
        }
    }
    tft_cs_high();
}

void st7735_fill_screen(uint16_t color)
{
    st7735_fill_rect(0, 0, _width, _height, color);
}

/* drawPixel: exact copy of reference lines 285-291
   Note: reference uses x+1,y+1 as end of window */
void st7735_draw_pixel(uint8_t x, uint8_t y, uint16_t color)
{
    if (x >= _width || y >= _height) return;
    setAddrWindow(x, y, (uint8_t)(x+1), (uint8_t)(y+1));
    write_data((uint8_t)(color >> 8));
    write_data((uint8_t)(color & 0xFF));
}

void st7735_fill_rect_fast(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                            uint16_t color)
{
    st7735_fill_rect(x, y, w, h, color);
}

void st7735_draw_image(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                       const uint16_t *pixels)
{
    if (x >= _width || y >= _height) return;
    if ((x + w - 1) >= _width)  w = _width  - x;
    if ((y + h - 1) >= _height) h = _height - y;
    setAddrWindow(x, y, (uint8_t)(x+w-1), (uint8_t)(y+h-1));
    tft_dc_high();
    tft_cs_low();
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        spiwrite((uint8_t)(pixels[i] >> 8));
        spiwrite((uint8_t)(pixels[i] & 0xFF));
    }
    tft_cs_high();
}

void st7735_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    setAddrWindow(x0, y0, x1, y1);
    tft_dc_high();
    tft_cs_low();
}

void st7735_push_pixels(const uint16_t *data, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        spiwrite((uint8_t)(data[i] >> 8));
        spiwrite((uint8_t)(data[i] & 0xFF));
    }
}

/* ── Text rendering ───────────────────────────────────────────────────── */
/* One address window per glyph row; CS released between rows.
   Font: column-major, bit0=top, 5 active cols + 1 gap, 7 active rows + 1 gap */
static void draw_char(uint8_t x, uint8_t y, char c,
                      uint16_t fg, uint16_t bg, uint8_t scale)
{
    if ((uint8_t)c < 0x20u || (uint8_t)c > 0x7Fu) c = '?';
    const uint8_t *glyph = font5x7[(uint8_t)c - 0x20u];
    uint8_t gw = (uint8_t)(FONT_W * scale);
    if ((uint16_t)x + gw > _width) return;

    uint8_t fhi = (uint8_t)(fg >> 8), flo = (uint8_t)(fg & 0xFFu);
    uint8_t bhi = (uint8_t)(bg >> 8), blo = (uint8_t)(bg & 0xFFu);

    for (uint8_t row = 0; row < FONT_H; row++) {
        for (uint8_t sr = 0; sr < scale; sr++) {
            uint8_t py = (uint8_t)(y + row * scale + sr);
            if (py >= _height) return;
            /* one 1-row window, width = gw */
            setAddrWindow(x, py, (uint8_t)(x + gw - 1u), py);
            tft_dc_high();
            tft_cs_low();
            for (uint8_t col = 0; col < FONT_W; col++) {
                uint8_t hi, lo;
                if (col < 5u && row < 7u) {
                    uint8_t bit = (glyph[col] >> row) & 1u;
                    hi = bit ? fhi : bhi;
                    lo = bit ? flo : blo;
                } else {
                    hi = bhi; lo = blo;
                }
                for (uint8_t sc = 0; sc < scale; sc++) {
                    spiwrite(hi);
                    spiwrite(lo);
                }
            }
            tft_cs_high();
        }
    }
}

void st7735_draw_char(uint8_t x, uint8_t y, char c,
                      uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (scale == 0) scale = 1;
    draw_char(x, y, c, fg, bg, scale);
}

uint8_t st7735_draw_string(uint8_t x, uint8_t y, const char *str,
                           uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (!str || scale == 0) return x;
    uint8_t x0 = x;
    uint8_t cw = (uint8_t)(FONT_W * scale);
    uint8_t ch = (uint8_t)(FONT_H * scale);
    while (*str) {
        if (*str == '\n') {
            x = x0; y = (uint8_t)(y + ch);
        } else {
            if ((uint16_t)x + cw > _width) { x = x0; y = (uint8_t)(y + ch); }
            if ((uint16_t)y + ch <= _height)
                draw_char(x, y, *str, fg, bg, scale);
            x = (uint8_t)(x + cw);
        }
        str++;
    }
    return x;
}

uint8_t st7735_draw_fixed(uint8_t x, uint8_t y,
                          int32_t value, uint8_t decimals, uint8_t min_int,
                          uint16_t fg, uint16_t bg, uint8_t scale)
{
    char buf[16];
    uint8_t pos = 0;
    bool neg = (value < 0);
    if (neg) value = -value;
    int32_t div = 1;
    for (uint8_t i = 0; i < decimals; i++) div *= 10;
    long ip = (long)(value / div);
    long fp = (long)(value % div);
    char tmp[12];
    int ilen = snprintf(tmp, sizeof(tmp), "%ld", ip);
    if (min_int > 0 && (uint8_t)ilen < min_int)
        for (uint8_t p = 0; p < (uint8_t)(min_int - ilen) && pos < 14; p++)
            buf[pos++] = ' ';
    if (neg && pos < 14) buf[pos++] = '-';
    for (int i = 0; i < ilen && pos < 14; i++) buf[pos++] = tmp[i];
    if (decimals > 0 && pos < 14) {
        buf[pos++] = '.';
        char fmt[8]; snprintf(fmt, sizeof(fmt), "%%0%dld", (int)decimals);
        char frac[8]; snprintf(frac, sizeof(frac), fmt, fp);
        for (uint8_t i = 0; i < decimals && pos < 14; i++) buf[pos++] = frac[i];
    }
    buf[pos] = '\0';
    return st7735_draw_string(x, y, buf, fg, bg, scale);
}