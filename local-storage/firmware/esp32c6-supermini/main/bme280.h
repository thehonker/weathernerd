/*
 * WeatherNerd — BME280 driver
 *
 * Minimal I2C driver for the Bosch BME280 pressure/humidity/temperature sensor.
 * I2C address: 0x76 (SDO pin low) or 0x77 (SDO pin high).
 * Uses forced-mode reads (one-shot) — lowest power for battery operation.
 *
 * Provides:
 *   - bme280_init(): probe + read calibration coefficients
 *   - bme280_read(): one-shot forced mode read → compensated T/H/P
 *
 * The BME280 has factory calibration coefficients stored in ROM.
 * Raw ADC values must be compensated using these coefficients per the
 * Bosch datasheet (Appendix A) to produce real units.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* BME280 I2C addresses */
#define BME280_I2C_ADDR_LOW   0x76  /* SDO pin = GND */
#define BME280_I2C_ADDR_HIGH  0x77  /* SDO pin = VCC */

/* BME280 chip ID */
#define BME280_CHIP_ID  0x60

/* Register addresses */
#define BME280_REG_ID           0xD0
#define BME280_REG_RESET        0xE0
#define BME280_REG_CTRL_HUM     0xF2
#define BME280_REG_STATUS       0xF3
#define BME280_REG_CTRL_MEAS    0xF4
#define BME280_REG_CONFIG       0xF5
#define BME280_REG_DATA         0xF7  /* pressure MSB (8 bytes: P3 + T3 + H2) */

/* Calibration coefficient registers */
#define BME280_REG_CALIB00      0x88  /* dig_T1-T3, dig_P1-P9 (26 bytes) */
#define BME280_REG_CALIB26      0xE1  /* dig_H1-H6 (7 bytes) */

/* Oversampling settings */
typedef enum {
    BME280_OSR_SKIP   = 0x00,
    BME280_OSR_1X     = 0x01,
    BME280_OSR_2X     = 0x02,
    BME280_OSR_4X     = 0x03,
    BME280_OSR_8X     = 0x04,
    BME280_OSR_16X    = 0x05,
} bme280_oversampling_t;

/* Power mode */
#define BME280_MODE_SLEEP   0x00
#define BME280_MODE_FORCED  0x01
#define BME280_MODE_NORMAL  0x03

/* Measurement result */
typedef struct {
    int16_t  temp_c_x100;     /* temperature in °C × 100 (e.g. 21.50°C = 2150) */
    uint16_t press_hpa_x10;   /* pressure in hPa × 10 (e.g. 1013.2 hPa = 10132) */
    uint8_t  rh_pct;          /* relative humidity in % (0-100) */
} bme280_data_t;

/* Initialize BME280: probe, reset, read calibration coefficients.
 * Uses address 0x76 first, falls back to 0x77.
 * Oversampling defaults: temp 1x, pressure 1x, humidity 1x (lowest power). */
esp_err_t bme280_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *ret_dev);

/* Perform a forced-mode one-shot read.
 * Wakes the sensor, waits for conversion, reads + compensates T/H/P.
 * Takes ~10ms with 1x oversampling. */
esp_err_t bme280_read(i2c_master_dev_handle_t dev, bme280_data_t *data);

/* Set oversampling for next read. Higher = more accurate but slower + more power. */
esp_err_t bme280_set_oversampling(i2c_master_dev_handle_t dev,
                                   bme280_oversampling_t temp,
                                   bme280_oversampling_t press,
                                   bme280_oversampling_t hum);

/* Probe for BME280 presence on the bus. */
esp_err_t bme280_probe(i2c_master_bus_handle_t bus);
