/*
 * WeatherNerd — DS3231 RTC driver implementation
 *
 * Minimal I2C driver for the Maxim DS3231 precision RTC.
 * Uses ESP-IDF v6.1 I2C master driver (i2c_master_transmit_receive / i2c_master_transmit).
 *
 * The DS3231 stores time in BCD format. This driver converts between
 * BCD and binary, and between the DS3231's calendar fields and Unix epoch.
 */

#include "ds3231.h"

#include <string.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "ds3231";

/* ---- BCD helpers ---- */

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static uint8_t bin_to_bcd(uint8_t bin)
{
    return ((bin / 10) << 4) | (bin % 10);
}

/* ---- I2C register access ---- */

static esp_err_t ds3231_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, 100);
}

static esp_err_t ds3231_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, 2, 100);
}

static esp_err_t ds3231_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, 100);
}

/* ---- Public API ---- */

esp_err_t ds3231_probe(i2c_master_bus_handle_t bus)
{
    return i2c_master_probe(bus, DS3231_I2C_ADDR, 100);
}

esp_err_t ds3231_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *ret_dev)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDR,
        .scl_speed_hz = 100000,  /* 100 kHz — DS3231 max is 400 kHz, 100 is safe */
    };

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, ret_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add DS3231 to I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "DS3231 initialized at I2C 0x%02X", DS3231_I2C_ADDR);
    return ESP_OK;
}

esp_err_t ds3231_get_time(i2c_master_dev_handle_t dev, uint32_t *epoch)
{
    /* Read 7 bytes: seconds, minutes, hours, day(week), date, month, year */
    uint8_t regs[7];
    esp_err_t ret = ds3231_read_regs(dev, DS3231_REG_SECONDS, regs, 7);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Parse BCD → binary */
    int sec   = bcd_to_bin(regs[0] & 0x7F);  /* bit 7 = CH (clock halt), mask it */
    int min   = bcd_to_bin(regs[1] & 0x7F);
    int hour  = bcd_to_bin(regs[2] & 0x3F);   /* 24-hour mode (bit 6=0) */
    int mday  = bcd_to_bin(regs[4] & 0x3F);
    int month = bcd_to_bin(regs[5] & 0x1F);   /* bit 7 = century, mask it */
    int year  = bcd_to_bin(regs[6]);          /* 00-99 */

    /* DS3231 century bit (bit 7 of month register) indicates 2000s vs 1900s.
     * We assume 2000s: year 00-99 → 2000-2099. */
    int full_year = 2000 + year;

    /* Sanity check — if year < 2000, time is probably unset */
    if (full_year < 2000 || full_year > 2099) {
        ESP_LOGW(TAG, "DS3231 time appears unset (year=%d)", full_year);
        *epoch = 0;
        return ESP_OK;
    }

    /* Convert to Unix epoch via struct tm */
    struct tm tm = {
        .tm_sec  = sec,
        .tm_min  = min,
        .tm_hour = hour,
        .tm_mday = mday,
        .tm_mon  = month - 1,    /* tm_mon is 0-based */
        .tm_year = full_year - 1900,
        .tm_isdst = 0,
    };

    time_t t = mktime(&tm);
    *epoch = (uint32_t)t;

    ESP_LOGD(TAG, "DS3231 time: %04d-%02d-%02d %02d:%02d:%02d → epoch %u",
             full_year, month, mday, hour, min, sec, (unsigned)*epoch);
    return ESP_OK;
}

esp_err_t ds3231_set_time(i2c_master_dev_handle_t dev, uint32_t epoch)
{
    time_t t = (time_t)epoch;
    struct tm tm;
    gmtime_r(&t, &tm);

    int full_year = tm.tm_year + 1900;
    if (full_year < 2000 || full_year > 2099) {
        ESP_LOGE(TAG, "Year %d out of DS3231 range (2000-2099)", full_year);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t regs[7];
    regs[0] = bin_to_bcd(tm.tm_sec);           /* seconds (CH=0, running) */
    regs[1] = bin_to_bcd(tm.tm_min);           /* minutes */
    regs[2] = bin_to_bcd(tm.tm_hour);          /* hours (24-hour mode, bit 6=0) */
    regs[3] = bin_to_bcd(tm.tm_wday ? tm.tm_wday : 7);  /* day of week (1=Sunday) */
    regs[4] = bin_to_bcd(tm.tm_mday);          /* date */
    regs[5] = bin_to_bcd(tm.tm_mon + 1);       /* month (century bit=0, we're in 2000s) */
    regs[6] = bin_to_bcd(full_year - 2000);    /* year (00-99) */

    /* Write all 7 time registers starting from seconds */
    uint8_t buf[8];
    buf[0] = DS3231_REG_SECONDS;
    memcpy(&buf[1], regs, 7);

    esp_err_t ret = i2c_master_transmit(dev, buf, 8, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set time: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "DS3231 time set: %04d-%02d-%02d %02d:%02d:%02d",
             full_year, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    return ESP_OK;
}

esp_err_t ds3231_get_temp(i2c_master_dev_handle_t dev, int16_t *temp_c_x100)
{
    uint8_t msb, lsb;

    esp_err_t ret = ds3231_read_reg(dev, DS3231_REG_TEMP_MSB, &msb);
    if (ret != ESP_OK) return ret;
    ret = ds3231_read_reg(dev, DS3231_REG_TEMP_LSB, &lsb);
    if (ret != ESP_OK) return ret;

    /* Temperature is signed 10-bit, MSB is integer part (2's complement),
     * LSB bits 7-6 are fractional (resolution 0.25°C, but we use 0.01°C) */
    int8_t temp_int = (int8_t)msb;  /* signed integer part */
    int frac = (lsb >> 6) & 0x03;   /* 0-3, each step = 0.25°C */

    *temp_c_x100 = temp_int * 100 + frac * 25;

    ESP_LOGD(TAG, "DS3231 temp: %d.%02d°C", *temp_c_x100 / 100, *temp_c_x100 % 100);
    return ESP_OK;
}
