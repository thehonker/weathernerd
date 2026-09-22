/*
 * WeatherNerd — SD card storage implementation
 *
 * SDSPI init, FAT mount, CSV file writing for wind + env data streams.
 * Uses esp_vfs_fat_sdspi_mount for all-in-one bus init + filesystem mount.
 *
 * SPI pins (from hardware-design.md):
 *   SCK=GPIO2  CS=GPIO3  MOSI=GPIO18  MISO=GPIO19
 */

#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_log.h"
#include <inttypes.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"

static const char *TAG = "storage";

/* SPI pin assignments */
#define PIN_NUM_MISO  19
#define PIN_NUM_MOSI  18
#define PIN_NUM_CLK   2
#define PIN_NUM_CS    3

/* Mount state */
static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;

/* ---- File helpers ---- */

/* Build a daily CSV filename from a Unix epoch timestamp.
 * wind: "/sdcard/YYYY-MM-DD-wind.csv"
 * env:  "/sdcard/YYYY-MM-DD-env.csv"
 *
 * Uses localtime_r — the ESP32-C6 RTC + DS3231 provides real time.
 * If time is not synced (epoch < 1700000000), uses "1970-01-01" fallback. */
static void build_daily_path(char *buf, size_t buflen, uint32_t epoch, bool is_wind)
{
    if (epoch < 1700000000) {
        /* Time not set yet — use fallback filename */
        snprintf(buf, buflen, "%s/1970-01-01-%s.csv",
                 SD_MOUNT_POINT, is_wind ? "wind" : "env");
        return;
    }

    time_t t = (time_t)epoch;
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(buf, buflen, "%s/%04d-%02d-%02d-%s.csv",
             SD_MOUNT_POINT,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             is_wind ? "wind" : "env");
}

/* Ensure the CSV file has a header row.
 * Called after opening a file for append — if file is empty (size 0),
 * writes the header. */
static void ensure_header(const char *path, const char *header)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        /* File doesn't exist yet — header will be written by first append */
        return;
    }
    if (st.st_size == 0) {
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%s\n", header);
            fclose(f);
        }
    }
}

/* ---- Public API ---- */

esp_err_t storage_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD card (SDSPI)");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    /* SDSPI host config — use SPI2_HOST (first available SPI bus on C6) */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.unaligned_multi_block_rw_max_chunk_size = 8;

    /* SPI bus config */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    /* SDSPI device config */
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount FAT filesystem (card not formatted?)");
        } else {
            ESP_LOGE(TAG, "Failed to init SD card: %s", esp_err_to_name(ret));
        }
        spi_bus_free(host.slot);
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t storage_deinit(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }

    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_mounted = false;
    s_card = NULL;

    /* Free the SPI bus */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_free(host.slot);

    ESP_LOGI(TAG, "SD card unmounted");
    return ESP_OK;
}

bool storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t storage_format(void)
{
    if (!s_mounted || !s_card) {
        ESP_LOGE(TAG, "SD card not mounted — cannot format");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Formatting SD card — all data will be erased!");

    /* Unmount first, format, then re-mount */
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_mounted = false;

    esp_err_t ret = esp_vfs_fat_sdcard_format(SD_MOUNT_POINT, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
        /* Try to re-mount even if format failed */
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024,
        };
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = PIN_NUM_CS;
        slot_config.host_id = host.slot;
        esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
        s_mounted = true;
        return ret;
    }

    /* Re-mount after format */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret == ESP_OK) {
        s_mounted = true;
        ESP_LOGI(TAG, "SD card formatted and re-mounted successfully");
    } else {
        ESP_LOGE(TAG, "Re-mount after format failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t storage_write_wind_samples(uint32_t epoch, const storage_wind_sample_t *samples, int count, uint16_t rain_adc)
{
    if (!s_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    char path[SD_MAX_PATH_LEN];
    build_daily_path(path, sizeof(path), epoch, true);

    /* Ensure header exists for new files */
    ensure_header(path, "epoch,speed_ms,dir_deg,rain_mmh");

    /* Open for append */
    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for append", path);
        return ESP_FAIL;
    }

    /* Write each valid sample. Timestamps are interpolated from the base epoch:
     * sample[i] was taken at epoch - (count - 1 - i) * 5 seconds */
    for (int i = 0; i < count; i++) {
        if (!samples[i].valid) {
            continue;
        }
        uint32_t sample_epoch = epoch - (uint32_t)(count - 1 - i) * 5;
        /* rain_mmh: raw ADC value for now — calibration applied in post-processing */
        fprintf(f, "%" PRIu32 ",%.1f,%u,%u\n",
                sample_epoch,
                samples[i].speed_mps_x10 / 10.0,
                samples[i].direction_deg,
                rain_adc);
    }

    fclose(f);
    ESP_LOGD(TAG, "Wrote %d wind samples to %s", count, path);
    return ESP_OK;
}

esp_err_t storage_write_env_sample(uint32_t epoch, int16_t temp_c_x100, uint8_t rh_pct, uint16_t press_hpa_x10)
{
    if (!s_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    char path[SD_MAX_PATH_LEN];
    build_daily_path(path, sizeof(path), epoch, false);

    /* Ensure header exists for new files */
    ensure_header(path, "epoch,temp_c,rh_pct,press_hpa");

    FILE *f = fopen(path, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for append", path);
        return ESP_FAIL;
    }

    fprintf(f, "%" PRIu32 ",%.2f,%u,%.1f\n",
            epoch,
            temp_c_x100 / 100.0,
            rh_pct,
            press_hpa_x10 / 10.0);

    fclose(f);
    ESP_LOGD(TAG, "Wrote env sample to %s", path);
    return ESP_OK;
}
