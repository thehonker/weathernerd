/*
 * WeatherNerd — Shared I2C bus module
 *
 * Initializes the ESP32-C6 I2C master bus on GPIO6 (SDA) / GPIO7 (SCL).
 * Shared by DS3231 (0x68), BME280 (0x76), and OLED (0x3C).
 *
 * The bus is initialized once on cold boot and kept alive across wake cycles.
 * Devices are added by their respective drivers via i2c_bus_get_handle().
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

/* I2C pin assignments (from hardware-design.md) */
#define I2C_SDA_PIN   6
#define I2C_SCL_PIN   7
#define I2C_FREQ_HZ   100000  /* 100 kHz — safe for all devices */

/* Initialize the shared I2C bus. Call once on cold boot. */
esp_err_t i2c_bus_init(void);

/* Get the shared I2C bus handle. Returns NULL if not initialized. */
i2c_master_bus_handle_t i2c_bus_get_handle(void);

/* Deinitialize the I2C bus (before deep sleep). */
esp_err_t i2c_bus_deinit(void);
