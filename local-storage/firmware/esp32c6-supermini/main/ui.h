/*
 * WeatherNerd — Interactive UI module
 *
 * Menu-driven interface for the OLED + encoder + buttons board.
 * Activated when CON button wakes the main core from deep sleep.
 * Provides: live sensor readout, file browser, WiFi portal, clock sync,
 * format SD, reboot, sleep, and halt options.
 *
 * Menu structure:
 *   [Live Data]    → real-time sensor readout (updates every 2s)
 *   [Files]        → list CSV files on SD card
 *   [WiFi Portal]  → start soft-AP + HTTP server for phone retrieval
 *   [Sync Clock]   → show current RTC time, instructions for WiFi sync
 *   [Format SD]    → format SD card (confirm with CON)
 *   [Reboot]       → reboot ESP32-C6 (confirm with CON)
 *   [Sleep]        → return to low-power mode (confirm with CON)
 *   [Halt]         → full system stop (confirm with CON, power cycle to resume)
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "ds3231.h"
#include "bme280.h"

/* Start the interactive UI. Blocks until user selects Sleep or Halt,
 * or the 60s inactivity timeout fires.
 *
 * @param oled_dev   SH1106 device handle (must be initialized)
 * @param ds3231_dev DS3231 handle (may be NULL)
 * @param bme280_dev BME280 handle (may be NULL)
 *
 * Returns ESP_OK on normal sleep, ESP_ERR_INVALID_STATE on halt request. */
esp_err_t ui_run(i2c_master_dev_handle_t oled_dev,
                 i2c_master_dev_handle_t ds3231_dev,
                 i2c_master_dev_handle_t bme280_dev);
