/*
 * WeatherNerd — SD card storage module
 *
 * Handles SDSPI initialization, FAT filesystem mount, and CSV file writing
 * for the two daily data streams:
 *   - YYYY-MM-DD-wind.csv  (5s wind + rain samples, flushed every 1 min)
 *   - YYYY-MM-DD-env.csv   (1 min temp/RH/pressure samples)
 *
 * SPI pin assignments (from hardware-design.md):
 *   SCK  = GPIO2
 *   CS   = GPIO3
 *   MOSI = GPIO18
 *   MISO = GPIO19
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Mount point for the SD card filesystem */
#define SD_MOUNT_POINT "/sdcard"

/* Maximum path length for file paths on SD card */
#define SD_MAX_PATH_LEN 64

/* Initialize SDSPI bus, mount FAT filesystem.
 * Returns ESP_OK on success, error code on failure.
 * Must be called once after boot, before any file operations. */
esp_err_t storage_init(void);

/* Unmount filesystem and free SPI bus.
 * Called before entering deep sleep to ensure clean unmount. */
esp_err_t storage_deinit(void);

/* Append wind sample rows to today's wind CSV file.
 * Each row: epoch,speed_ms,dir_deg,rain_mmh
 *
 * @param epoch       Unix timestamp from DS3231
 * @param samples     Array of wind samples (from LP core shared memory)
 * @param count       Number of valid samples
 * @param rain_adc    Raw rain ADC peak value (main core reads HP ADC)
 *
 * Returns ESP_OK on success, error code on failure. */
typedef struct {
    uint16_t speed_mps_x10;    /* wind speed × 10 */
    uint16_t direction_deg;    /* 0-359 */
    uint8_t  valid;            /* 1 = valid */
} storage_wind_sample_t;

esp_err_t storage_write_wind_samples(uint32_t epoch, const storage_wind_sample_t *samples, int count, uint16_t rain_adc);

/* Append a single environment sample to today's env CSV file.
 * Row: epoch,temp_c,rh_pct,press_hpa
 *
 * @param epoch   Unix timestamp from DS3231
 * @param temp_c  Temperature in °C × 100 (e.g. 18.50°C = 1850)
 * @param rh_pct  Relative humidity in % (0-100)
 * @param press_hpa Pressure in hPa × 10 (e.g. 1013.2 hPa = 10132)
 *
 * Returns ESP_OK on success, error code on failure. */
esp_err_t storage_write_env_sample(uint32_t epoch, int16_t temp_c_x100, uint8_t rh_pct, uint16_t press_hpa_x10);

/* Check if SD card is mounted and ready.
 * Returns true if mounted, false otherwise. */
bool storage_is_mounted(void);

/* Format the SD card filesystem (FAT32).
 * Card must be initialized (storage_init) before calling.
 * WARNING: Erases all data on the card.
 * Returns ESP_OK on success. */
esp_err_t storage_format(void);
