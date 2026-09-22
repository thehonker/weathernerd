/*
 * WeatherNerd — BME280 driver implementation
 *
 * Minimal I2C driver for Bosch BME280 with forced-mode reads and
 * full software compensation per the BME280 datasheet (Appendix A).
 *
 * Calibration coefficients are read from ROM registers on init and
 * cached. Raw ADC values are compensated using the Bosch formulas
 * to produce temperature (°C), pressure (hPa), and humidity (%).
 */

#include "bme280.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bme280";

/* ---- Calibration coefficients (read from ROM on init) ---- */
typedef struct {
    /* Temperature */
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    /* Pressure */
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
    /* Humidity */
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
    /* Computed fine temperature (carried between T/P/H compensation) */
    int32_t  t_fine;
} bme280_calib_t;

static bme280_calib_t s_calib;

/* ---- I2C helpers ---- */

static esp_err_t bme280_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, 100);
}

static esp_err_t bme280_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, 100);
}

static esp_err_t bme280_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, 2, 100);
}

/* ---- Calibration coefficient reading ---- */

static esp_err_t read_calibration(i2c_master_dev_handle_t dev)
{
    uint8_t buf[26];

    /* Read first block: dig_T1-T3, dig_P1-P9 (0x88..0xA1, 26 bytes) */
    esp_err_t ret = bme280_read_regs(dev, BME280_REG_CALIB00, buf, 26);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration block 1: %s", esp_err_to_name(ret));
        return ret;
    }

    s_calib.dig_T1 = (uint16_t)(buf[1] << 8 | buf[0]);
    s_calib.dig_T2 = (int16_t)(buf[3] << 8 | buf[2]);
    s_calib.dig_T3 = (int16_t)(buf[5] << 8 | buf[4]);

    s_calib.dig_P1 = (uint16_t)(buf[7]  << 8 | buf[6]);
    s_calib.dig_P2 = (int16_t)(buf[9]  << 8 | buf[8]);
    s_calib.dig_P3 = (int16_t)(buf[11] << 8 | buf[10]);
    s_calib.dig_P4 = (int16_t)(buf[13] << 8 | buf[12]);
    s_calib.dig_P5 = (int16_t)(buf[15] << 8 | buf[14]);
    s_calib.dig_P6 = (int16_t)(buf[17] << 8 | buf[16]);
    s_calib.dig_P7 = (int16_t)(buf[19] << 8 | buf[18]);
    s_calib.dig_P8 = (int16_t)(buf[21] << 8 | buf[20]);
    s_calib.dig_P9 = (int16_t)(buf[23] << 8 | buf[22]);

    /* Read second block: dig_H1-H6 (0xE1..0xE7, 7 bytes) */
    uint8_t hbuf[7];
    ret = bme280_read_regs(dev, BME280_REG_CALIB26, hbuf, 7);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration block 2: %s", esp_err_to_name(ret));
        return ret;
    }

    s_calib.dig_H1 = hbuf[0];
    s_calib.dig_H2 = (int16_t)(hbuf[2] << 8 | hbuf[1]);
    s_calib.dig_H3 = hbuf[3];
    /* dig_H4: 12-bit signed, bits [11:4] in E4, [3:0] in upper nibble of E5 */
    s_calib.dig_H4 = (int16_t)((hbuf[4] << 4) | (hbuf[5] & 0x0F));
    /* dig_H5: 12-bit signed, bits [11:4] in E6, [3:0] in lower nibble of E5 */
    s_calib.dig_H5 = (int16_t)((hbuf[6] << 4) | (hbuf[5] >> 4));
    s_calib.dig_H6 = (int8_t)hbuf[6] & 0xFF;  /* Not right — H6 is at 0xE7 */

    /* Fix: dig_H6 is a signed 8-bit value at register 0xE7 (hbuf index 6 is E7,
     * but we already used it for H5 upper bits). Need to re-read 0xE7 separately. */
    uint8_t h6_raw;
    ret = bme280_read_reg(dev, 0xE7, &h6_raw);
    if (ret == ESP_OK) {
        s_calib.dig_H6 = (int8_t)h6_raw;
    }

    ESP_LOGD(TAG, "Calibration: T1=%u T2=%d T3=%d P1=%u..P9=%d H1=%u H2=%d H3=%u H4=%d H5=%d H6=%d",
             s_calib.dig_T1, s_calib.dig_T2, s_calib.dig_T3,
             s_calib.dig_P1, s_calib.dig_P9,
             s_calib.dig_H1, s_calib.dig_H2, s_calib.dig_H3,
             s_calib.dig_H4, s_calib.dig_H5, s_calib.dig_H6);

    return ESP_OK;
}

/* ---- Compensation (Bosch datasheet Appendix A) ---- */

/* Returns temperature in °C × 100 (e.g. 21.50°C = 2150).
 * Also sets t_fine for pressure and humidity compensation. */
static int32_t compensate_temperature(int32_t adc_T)
{
    int32_t var1, var2;

    var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) *
            ((int32_t)s_calib.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)s_calib.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)s_calib.dig_T1))) >>
             12) *
            ((int32_t)s_calib.dig_T3)) >> 14;

    s_calib.t_fine = var1 + var2;

    return (s_calib.t_fine * 5 + 128) >> 8;
}

/* Returns pressure in Pa (unsigned 32-bit).
 * Convert to hPa × 10: divide by 10 (Pa → hPa) then multiply by 10 → just use Pa directly. */
static uint32_t compensate_pressure(int32_t adc_P)
{
    int32_t var1, var2;
    uint32_t p;

    var1 = (((int32_t)s_calib.t_fine) >> 1) - 64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) *
           ((int32_t)s_calib.dig_P6);
    var2 = var2 + ((var1 * ((int32_t)s_calib.dig_P5)) << 1);
    var2 = (var2 >> 2) + (((int32_t)s_calib.dig_P4) << 16);
    var1 = (((s_calib.dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) +
            ((((int32_t)s_calib.dig_P2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((int32_t)s_calib.dig_P1)) >> 15);

    if (var1 == 0) {
        return 0;  /* avoid division by zero */
    }

    p = (((uint32_t)(((1048576) - adc_P) - (var2 >> 12))) << 10) / (uint32_t)var1;
    var1 = (((int32_t)s_calib.dig_P9) * (int32_t)((p >> 13) * (p >> 13))) >> 25;
    var2 = (((int32_t)(p >> 2)) * ((int32_t)s_calib.dig_P8)) >> 12;
    p = (uint32_t)((int32_t)p + ((var1 + var2 + s_calib.dig_P7) >> 4));

    return p;  /* Pa */
}

/* Returns humidity in %RH (unsigned 32-bit, 0-102400 → divide by 1024 for %). */
static uint32_t compensate_humidity(int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = (s_calib.t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)s_calib.dig_H4) << 20) -
                    (((int32_t)s_calib.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                  (((((((v_x1_u32r * ((int32_t)s_calib.dig_H6)) >> 10) *
                       (((v_x1_u32r * ((int32_t)s_calib.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
                     ((int32_t)2097152)) * ((int32_t)s_calib.dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                               ((int32_t)s_calib.dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;

    return (uint32_t)(v_x1_u32r >> 12);  /* 0-102400, /1024 = %RH */
}

/* ---- Public API ---- */

esp_err_t bme280_probe(i2c_master_bus_handle_t bus)
{
    /* Try 0x76 first, then 0x77 */
    esp_err_t ret = i2c_master_probe(bus, BME280_I2C_ADDR_LOW, 100);
    if (ret == ESP_OK) return ESP_OK;
    return i2c_master_probe(bus, BME280_I2C_ADDR_HIGH, 100);
}

esp_err_t bme280_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t *ret_dev)
{
    /* Try address 0x76 first, fall back to 0x77 */
    uint8_t addr = BME280_I2C_ADDR_LOW;
    esp_err_t ret = i2c_master_probe(bus, addr, 100);
    if (ret != ESP_OK) {
        addr = BME280_I2C_ADDR_HIGH;
        ret = i2c_master_probe(bus, addr, 100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "BME280 not found at 0x76 or 0x77");
            return ret;
        }
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };

    ret = i2c_master_bus_add_device(bus, &dev_cfg, ret_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add BME280 to I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_master_dev_handle_t dev = *ret_dev;

    /* Verify chip ID */
    uint8_t chip_id;
    ret = bme280_read_reg(dev, BME280_REG_ID, &chip_id);
    if (ret != ESP_OK || chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Invalid chip ID: 0x%02X (expected 0x%02X)", chip_id, BME280_CHIP_ID);
        i2c_master_bus_rm_device(dev);
        *ret_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    /* Soft reset */
    bme280_write_reg(dev, BME280_REG_RESET, 0xB6);
    vTaskDelay(pdMS_TO_TICKS(10));  /* 2ms reset time, give 10ms to be safe */

    /* Read calibration coefficients */
    ret = read_calibration(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set default oversampling: 1x for all (lowest power, ~7ms conversion) */
    bme280_set_oversampling(dev, BME280_OSR_1X, BME280_OSR_1X, BME280_OSR_1X);

    ESP_LOGI(TAG, "BME280 initialized at I2C 0x%02X (chip ID 0x%02X)", addr, chip_id);
    return ESP_OK;
}

esp_err_t bme280_set_oversampling(i2c_master_dev_handle_t dev,
                                   bme280_oversampling_t temp,
                                   bme280_oversampling_t press,
                                   bme280_oversampling_t hum)
{
    /* Set humidity oversampling first (must be written before ctrl_meas) */
    esp_err_t ret = bme280_write_reg(dev, BME280_REG_CTRL_HUM, hum);
    if (ret != ESP_OK) return ret;

    /* Set temperature + pressure oversampling and mode (sleep mode) */
    uint8_t ctrl_meas = (temp << 5) | (press << 2) | BME280_MODE_SLEEP;
    return bme280_write_reg(dev, BME280_REG_CTRL_MEAS, ctrl_meas);
}

esp_err_t bme280_read(i2c_master_dev_handle_t dev, bme280_data_t *data)
{
    /* Trigger forced-mode conversion */
    /* Read current ctrl_meas, set mode bits to 0b01 (forced) */
    uint8_t ctrl;
    esp_err_t ret = bme280_read_reg(dev, BME280_REG_CTRL_MEAS, &ctrl);
    if (ret != ESP_OK) return ret;

    ctrl = (ctrl & 0xFC) | BME280_MODE_FORCED;
    ret = bme280_write_reg(dev, BME280_REG_CTRL_MEAS, ctrl);
    if (ret != ESP_OK) return ret;

    /* Wait for conversion to complete.
     * With 1x oversampling: temp ~7ms, pressure ~7ms, humidity ~2ms ≈ 16ms total.
     * Poll the status register measuring bit, with timeout. */
    uint8_t status;
    for (int i = 0; i < 50; i++) {  /* up to 50ms timeout */
        ret = bme280_read_reg(dev, BME280_REG_STATUS, &status);
        if (ret != ESP_OK) return ret;
        if (!(status & 0x08)) break;  /* bit 3 = measuring */
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    /* Read raw data: 8 bytes starting at 0xF7
     *   press_msb, press_lsb, press_xlsb (3 bytes)
     *   temp_msb,  temp_lsb,  temp_xlsb  (3 bytes)
     *   hum_msb,   hum_lsb               (2 bytes) */
    uint8_t raw[8];
    ret = bme280_read_regs(dev, BME280_REG_DATA, raw, 8);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data: %s", esp_err_to_name(ret));
        return ret;
    }

    int32_t adc_P = (int32_t)((raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4));
    int32_t adc_T = (int32_t)((raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4));
    int32_t adc_H = (int32_t)((raw[6] << 8)  | raw[7]);

    /* Compensate (order matters: temperature first to set t_fine) */
    int32_t temp_c_x100 = compensate_temperature(adc_T);
    uint32_t press_pa   = compensate_pressure(adc_P);
    uint32_t hum_raw    = compensate_humidity(adc_H);

    /* Convert to output units */
    data->temp_c_x100   = (int16_t)temp_c_x100;
    data->press_hpa_x10 = (uint16_t)(press_pa / 10);  /* Pa → hPa×10 (1 hPa = 100 Pa, so /10 gives hPa×10) */
    data->rh_pct        = (uint8_t)(hum_raw / 1024);   /* 0-102400 → 0-100 */

    ESP_LOGD(TAG, "BME280: T=%.2f°C P=%.1f hPa RH=%u%%",
             data->temp_c_x100 / 100.0,
             data->press_hpa_x10 / 10.0,
             data->rh_pct);

    return ESP_OK;
}
