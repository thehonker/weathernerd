/*
 * WeatherNerd — Shared I2C bus implementation
 */

#include "i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus_handle = NULL;

esp_err_t i2c_bus_init(void)
{
    if (s_bus_handle) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
            .allow_pd = false,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d, %d Hz)",
             I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return s_bus_handle;
}

esp_err_t i2c_bus_deinit(void)
{
    if (!s_bus_handle) {
        return ESP_OK;
    }

    /* ESP-IDF v6.1 doesn't have a dedicated bus delete — the bus handle
     * is valid for the lifetime of the process. On deep sleep, the I2C
     * peripheral powers down automatically. We just clear our reference. */
    s_bus_handle = NULL;
    ESP_LOGI(TAG, "I2C bus handle cleared");
    return ESP_OK;
}
