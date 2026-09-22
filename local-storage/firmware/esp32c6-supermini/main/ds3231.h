/*
 * WeatherNerd — DS3231 RTC driver
 *
 * Minimal driver for the DS3231 I2C precision RTC (±2 ppm, TCXO).
 * I2C address: 0x68 (shared bus with BME280 at 0x76 and OLED at 0x3C).
 *
 * Provides:
 *   - ds3231_init(): add device to existing I2C bus
 *   - ds3231_get_time(): read current time as Unix epoch
 *   - ds3231_set_time(): set time from Unix epoch
 *   - ds3231_get_temp(): read onboard temperature sensor
 *
 * Register map (BCD-encoded):
 *   0x00 Seconds  0x01 Minutes  0x02 Hours
 *   0x03 Day(week) 0x04 Date    0x05 Month/Century  0x06 Year
 *   0x11 Temp MSB  0x12 Temp LSB (2^-6 resolution)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* DS3231 I2C address */
#define DS3231_I2C_ADDR  0x68

/* DS3231 register addresses */
#define DS3231_REG_SECONDS   0x00
#define DS3231_REG_MINUTES   0x01
#define DS3231_REG_HOURS     0x02
#define DS3231_REG_DAY      0x03
#define DS3231_REG_DATE     0x04
#define DS3231_REG_MONTH    0x05
#define DS3231_REG_YEAR     0x06
#define DS3231_REG_CONTROL  0x0E
#define DS3231_REG_TEMP_MSB 0x11
#define DS3231_REG_TEMP_LSB 0x12

/* Initialize DS3231 on an existing I2C bus.
 * The bus must already be created with i2c_new_master_bus().
 * Returns a device handle via ret_dev. */
esp_err_t ds3231_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *ret_dev);

/* Read current time from DS3231 as Unix epoch timestamp.
 * Returns 0 if time appears unset (year < 2000). */
esp_err_t ds3231_get_time(i2c_master_dev_handle_t dev, uint32_t *epoch);

/* Set DS3231 time from Unix epoch timestamp. */
esp_err_t ds3231_set_time(i2c_master_dev_handle_t dev, uint32_t epoch);

/* Read DS3231 onboard temperature sensor.
 * Returns temperature in °C × 100 (e.g. 21.50°C = 2150). */
esp_err_t ds3231_get_temp(i2c_master_dev_handle_t dev, int16_t *temp_c_x100);

/* Check if DS3231 is present on the bus (probe). */
esp_err_t ds3231_probe(i2c_master_bus_handle_t bus);
