#pragma once

/*
 * IMPORTANT: this file must contain ONLY declarations and #defines.
 * If you see errors about 'font5x7' or 'static' symbols being redefined,
 * you have an old st7735.h (with implementation) still in your project.
 * Delete it — only this file and st7735.c should exist.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Pin map */
#define ST7735_SPI_PORT   spi0
#define PIN_SCK           2
#define PIN_MOSI          3
#define PIN_MISO          4
#define PIN_LED           5
#define PIN_DC            6
#define PIN_RST           7
#define PIN_CS_TFT        8

/* Bus speeds */
#define SPI_BAUD_TFT      10000000u
#define SPI_BAUD_EEPROM    1000000u

/* Physical panel */
#define ST7735_WIDTH    128
#define ST7735_HEIGHT   160
#define COLSTART   0
#define ROWSTART   0

/* Colours */
#define ST7735_COLOR(r,g,b) ((uint16_t)((((r)&0xF8u)<<8u)|(((g)&0xFCu)<<3u)|((uint8_t)(b)>>3u)))
#define ST7735_BLACK    0x0000u
#define ST7735_WHITE    0xFFFFu
#define ST7735_RED      0xF800u
#define ST7735_GREEN    0x07E0u
#define ST7735_BLUE     0x001Fu
#define ST7735_CYAN     0x07FFu
#define ST7735_MAGENTA  0xF81Fu
#define ST7735_YELLOW   0xFFE0u
#define ST7735_ORANGE   0xFD20u
#define ST7735_DARKGREY 0x4208u
#define ST7735_LGREY    0xC618u

/*
 * FONT_W = 6: 5 active pixels + 1 pixel right gap
 * FONT_H = 8: 7 active pixels + 1 pixel bottom gap
 * These include the inter-character spacing so no extra gap is needed.
 */
#define FONT_W   6
#define FONT_H   8

/* API */
#include <stdint.h>
#include <stdbool.h>
void    st7735_init(void);
void    st7735_spi_acquire(void);
void    st7735_spi_release(void);
void    eeprom_spi_acquire(void);
void    st7735_set_backlight(uint8_t percent);
void    st7735_fill_screen(uint16_t colour);
void    st7735_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t colour);
void    st7735_draw_pixel(uint8_t x, uint8_t y, uint16_t colour);
void    st7735_draw_image(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint16_t *pixels);
void    st7735_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void    st7735_push_pixels(const uint16_t *data, uint32_t count);
void    st7735_draw_char(uint8_t x, uint8_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
uint8_t st7735_draw_string(uint8_t x, uint8_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale);
uint8_t st7735_draw_fixed(uint8_t x, uint8_t y, int32_t value, uint8_t decimals, uint8_t min_int, uint16_t fg, uint16_t bg, uint8_t scale);
void    st7735_set_rotation(uint8_t rot);
uint8_t st7735_width(void);
uint8_t st7735_height(void);

#ifdef __cplusplus
}
#endif