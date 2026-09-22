/*
 * WeatherNerd — SH1106 OLED driver
 *
 * Minimal I2C driver for the SH1106 1.3" 128×64 monochrome OLED.
 * I2C address: 0x3C (shared bus with BME280 0x76 + DS3231 0x68).
 * Text-only rendering with embedded 8×8 font (16 cols × 8 rows).
 *
 * Provides:
 *   - oled_init(): add device to existing I2C bus, send init sequence
 *   - oled_clear(): clear display buffer
 *   - oled_set_cursor(): move text cursor
 *   - oled_puts(): write a string at cursor position
 *   - oled_render(): flush buffer to display
 *   - oled_on() / oled_off(): power control (display on/off, not VCC gating)
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/* SH1106 I2C address */
#define OLED_I2C_ADDR   0x3C

/* Display dimensions */
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_FONT_WIDTH 8
#define OLED_FONT_HEIGHT 8
#define OLED_COLS       (OLED_WIDTH / OLED_FONT_WIDTH)   /* 16 */
#define OLED_ROWS       (OLED_HEIGHT / OLED_FONT_HEIGHT) /* 8 */

/* Initialize SH1106 on an existing I2C bus. */
esp_err_t oled_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *ret_dev);

/* Clear the display buffer (does not render). */
void oled_clear(void);

/* Set text cursor (col 0-15, row 0-7). */
void oled_set_cursor(int col, int row);

/* Write a string at the current cursor position.
 * Advances cursor. Wraps to next row at end of line. */
void oled_puts(const char *str);

/* Write a single character at cursor position. Advances cursor. */
void oled_putc(char c);

/* Render the display buffer to the OLED. */
esp_err_t oled_render(i2c_master_dev_handle_t dev);

/* Turn display on (show pixels). */
esp_err_t oled_display_on(i2c_master_dev_handle_t dev);

/* Turn display off (black screen, keeps buffer). */
esp_err_t oled_display_off(i2c_master_dev_handle_t dev);

/* Probe for SH1106 presence on the bus. */
esp_err_t oled_probe(i2c_master_bus_handle_t bus);
