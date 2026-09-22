/*
 * WeatherNerd — WiFi soft-AP + HTTP server
 *
 * When the CON button (GPIO22) wakes the main core, the WiFi module starts
 * a soft-AP and HTTP portal for browsing/downloading CSV files, syncing the
 * DS3231 clock, formatting the SD card, rebooting, or halting the system.
 *
 * Endpoints:
 *   GET  /                — captive portal page (HTML + inline JS)
 *   GET  /api/files       — JSON list of CSV files on SD card
 *   GET  /api/files/<name> — download individual CSV file
 *   GET  /api/rtc-time    — current DS3231 time as JSON
 *   POST /api/sync-clock  — sync DS3231 from browser time (body: epoch seconds)
 *   POST /api/format-sd   — format SD card filesystem (erases all data)
 *   POST /api/reboot     — reboot the ESP32-C6
 *   POST /api/halt        — halt system (permanent deep sleep, power cycle to resume)
 *
 * Blocks until the CON button is released or auto-sleep timeout fires,
 * then shuts down HTTP server + WiFi and returns to sleep.
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "ds3231.h"

/* Start WiFi soft-AP + HTTP server.
 * - Inits NVS, netif, WiFi soft-AP
 * - Mounts SD card
 * - Starts HTTP server with file browser, clock sync, format, reboot, halt
 * - Blocks until the CON button is released (checked via GPIO polling)
 *
 * @param ds3231_dev  DS3231 handle for clock sync (may be NULL)
 * @param con_gpio    GPIO pin for CON button (active low, polled for release)
 *
 * Returns ESP_OK when CON button is released and sleep is complete. */
esp_err_t wifi_start(i2c_master_dev_handle_t ds3231_dev, int con_gpio);
