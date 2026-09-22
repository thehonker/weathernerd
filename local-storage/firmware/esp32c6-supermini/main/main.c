/*
 * WeatherNerd ESP32-C6 SuperMini — Main core firmware
 *
 * Main core responsibilities:
 * - On boot: init I2C bus, LP UART, LP core, rain ADC, DS3231, BME280, enter deep sleep
 * - On LP core wakeup (every 1 min): read DS3231 + rain ADC + BME280, flush to SD
 * - On GPIO22 wakeup (CON button): power on OLED, run interactive UI menu
 *
 * Note: ESP32-C6 LP core has no LP ADC. Rain ADC is read by the main core
 * on its 1-minute wake cycle using the HP ADC peripheral.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "esp_sleep.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ulp_lp_core.h"
#include "lp_core_main.h"
#include "lp_core_uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "storage.h"
#include "i2c_bus.h"
#include "ds3231.h"
#include "bme280.h"
#include "oled.h"
#include "ui.h"

static const char *TAG = "weathernerd";

/* LP core binary symbols */
extern const uint8_t lp_core_main_bin_start[] asm("_binary_lp_core_main_bin_start");
extern const uint8_t lp_core_main_bin_end[]   asm("_binary_lp_core_main_bin_end");

/* Pin assignments */
#define CON_BUTTON_GPIO     22   /* CON button — wake interrupt + UI confirm */
#define OLED_MOSFET_GPIO    5    /* P-channel MOSFET gate for OLED power */
#define RAIN_ADC_UNIT       ADC_UNIT_1
#define RAIN_ADC_CHANNEL    ADC_CHANNEL_0  /* GPIO0 = ADC1_CH0 */
#define RAIN_RESET_GPIO     8              /* 2N7002 gate — discharges 470nF peak-hold cap */

/* Main core wake interval (1 minute) */
#define MAIN_WAKE_INTERVAL_US  (60 * 1000 * 1000)

/* ---- Peripherals ---- */
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static i2c_master_dev_handle_t s_ds3231_dev = NULL;
static i2c_master_dev_handle_t s_bme280_dev = NULL;
static i2c_master_dev_handle_t s_oled_dev = NULL;

/* ---- Rain ADC ---- */

static void init_rain_adc(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = RAIN_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, RAIN_ADC_CHANNEL, &chan_cfg));

    /* GPIO8 — 2N7002 gate to discharge 470nF peak-hold cap after each read.
     * LOW = MOSFET OFF (cap holds peak). HIGH = MOSFET ON (cap discharges).
     * 10kΩ pulldown ensures OFF during deep sleep. */
    gpio_set_direction(RAIN_RESET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(RAIN_RESET_GPIO, 0);

    ESP_LOGI(TAG, "Rain ADC initialized (ADC1_CH0 / GPIO0, reset on GPIO8)");
}

/* Read peak-hold cap: burst-read 16 samples (~2ms), take max, then discharge cap.
 * The 470nF film cap holds the highest raindrop impact voltage since the last
 * reset. 1N4148 reverse leakage (~5nA) gives a hold time well beyond 60s.
 * After reading, pulse GPIO8 HIGH for 5ms to discharge C2 for a clean window. */
static uint16_t read_rain_adc(void)
{
    int raw = 0, max_raw = 0;
    for (int i = 0; i < 16; i++) {
        esp_err_t ret = adc_oneshot_read(s_adc_handle, RAIN_ADC_CHANNEL, &raw);
        if (ret == ESP_OK && raw > max_raw) {
            max_raw = raw;
        }
    }

    /* Discharge peak-hold cap for next interval */
    gpio_set_level(RAIN_RESET_GPIO, 1);
    usleep(5000);   /* 5ms — full discharge of 470nF through 2N7002 */
    gpio_set_level(RAIN_RESET_GPIO, 0);

    return (uint16_t)max_raw;
}

/* ---- LP core init ---- */

static void lp_core_init(void)
{
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
        .lp_timer_sleep_duration_us = 5 * 1000 * 1000,
    };

    ESP_ERROR_CHECK(ulp_lp_core_load_binary(
        lp_core_main_bin_start,
        (lp_core_main_bin_end - lp_core_main_bin_start)));

    ESP_ERROR_CHECK(ulp_lp_core_run(&cfg));
    ESP_LOGI(TAG, "LP core started (5s timer wakeup)");
}

static void init_lp_uart(void)
{
    lp_core_uart_cfg_t cfg = LP_CORE_UART_DEFAULT_CONFIG();
    cfg.uart_proto_cfg.baud_rate = 9600;
    cfg.uart_pin_cfg.tx_io_num = -1;
    cfg.uart_pin_cfg.rx_io_num = 4;
    ESP_ERROR_CHECK(lp_core_uart_init(&cfg));
    ESP_LOGI(TAG, "LP UART initialized (RX=GPIO4, 9600 baud)");
}

/* ---- DS3231 RTC ---- */

static void init_ds3231(void)
{
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();

    esp_err_t ret = ds3231_probe(bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 not found on I2C bus: %s", esp_err_to_name(ret));
        return;
    }

    ret = ds3231_init(bus, &s_ds3231_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 init failed: %s", esp_err_to_name(ret));
        return;
    }

    uint32_t epoch = 0;
    ds3231_get_time(s_ds3231_dev, &epoch);
    if (epoch == 0) {
        ESP_LOGW(TAG, "DS3231 time not set — timestamps will be 0 until configured");
    } else {
        ESP_LOGI(TAG, "DS3231 time: epoch %u", (unsigned)epoch);
    }
}

static uint32_t get_epoch(void)
{
    if (!s_ds3231_dev) return 0;
    uint32_t epoch = 0;
    ds3231_get_time(s_ds3231_dev, &epoch);
    return epoch;
}

/* ---- BME280 ---- */

static void init_bme280(void)
{
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();

    esp_err_t ret = bme280_probe(bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BME280 not found on I2C bus: %s", esp_err_to_name(ret));
        return;
    }

    ret = bme280_init(bus, &s_bme280_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BME280 init failed: %s", esp_err_to_name(ret));
        return;
    }

    bme280_data_t data;
    ret = bme280_read(s_bme280_dev, &data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BME280 test read: %.2f°C, %.1f hPa, %u%% RH",
                 data.temp_c_x100 / 100.0,
                 data.press_hpa_x10 / 10.0,
                 data.rh_pct);
    }
}

/* ---- OLED ---- */

static void init_oled(void)
{
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();

    esp_err_t ret = oled_probe(bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OLED not found on I2C bus: %s", esp_err_to_name(ret));
        return;
    }

    ret = oled_init(bus, &s_oled_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(ret));
        return;
    }

    /* Display is ON after oled_init() — leave it on, UI will use it immediately */
}

/* ---- LP core shared memory access ----
 *
 * The LP core defines a packed wind_sample_t (5 bytes: u16 + u16 + u8).
 * 12 packed samples = 60 bytes = 15 uint32_t words = ulp_sample_buffer[15].
 * Main core must use a matching packed struct to read the raw bytes correctly.
 */

typedef struct {
    uint16_t speed_mps_x10;
    uint16_t direction_deg;
    uint8_t  valid;
} __attribute__((packed)) lp_wind_sample_t;

/* ---- Sample flush ---- */

static void flush_samples_to_sd(void)
{
    uint16_t rain_adc = read_rain_adc();
    ESP_LOGI(TAG, "Rain ADC raw: %u", rain_adc);

    uint32_t epoch = get_epoch();
    if (epoch == 0) {
        ESP_LOGW(TAG, "DS3231 time not set — using 0 epoch");
    }

    ESP_LOGI(TAG, "Flushing samples to SD card (epoch=%u)", (unsigned)epoch);

    esp_err_t ret = storage_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card init failed: %s", esp_err_to_name(ret));
        return;
    }

    bme280_data_t env_data = {0};
    if (s_bme280_dev) {
        esp_err_t bret = bme280_read(s_bme280_dev, &env_data);
        if (bret != ESP_OK) {
            ESP_LOGW(TAG, "BME280 read failed: %s", esp_err_to_name(bret));
        } else {
            ESP_LOGI(TAG, "BME280: %.2f°C, %.1f hPa, %u%% RH",
                     env_data.temp_c_x100 / 100.0,
                     env_data.press_hpa_x10 / 10.0,
                     env_data.rh_pct);
        }
    }

    ret = storage_write_env_sample(epoch,
                                   env_data.temp_c_x100,
                                   env_data.rh_pct,
                                   env_data.press_hpa_x10);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write env sample: %s", esp_err_to_name(ret));
    }

    /* Read wind samples from LP core shared memory.
     * ulp_sample_buffer is 15 uint32_t words = 60 bytes = 12 packed samples.
     * ulp_sample_count is the number of valid samples (0-12).
     * ulp_buffer_ready is set by LP core when buffer is full. */
    uint16_t lp_count = (uint16_t)ulp_sample_count;
    if (lp_count > 0 && lp_count <= 12) {
        const lp_wind_sample_t *lp_samples = (const lp_wind_sample_t *)ulp_sample_buffer;

        /* Convert packed LP struct to storage struct (field-by-field, no padding issues) */
        storage_wind_sample_t storage_samples[12];
        int valid_count = 0;
        for (int i = 0; i < lp_count; i++) {
            if (lp_samples[i].valid) {
                storage_samples[valid_count].speed_mps_x10 = lp_samples[i].speed_mps_x10;
                storage_samples[valid_count].direction_deg = lp_samples[i].direction_deg;
                storage_samples[valid_count].valid = 1;
                valid_count++;
            }
        }

        if (valid_count > 0) {
            ESP_LOGI(TAG, "Writing %d wind samples to SD (of %d LP samples)",
                     valid_count, lp_count);
            ret = storage_write_wind_samples(epoch, storage_samples, valid_count, rain_adc);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to write wind samples: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGW(TAG, "No valid wind samples in LP buffer (count=%u)", lp_count);
        }

        /* Signal LP core that buffer has been consumed */
        ulp_buffer_ready = 0;
    } else if (lp_count == 0) {
        ESP_LOGD(TAG, "No wind samples in LP buffer");
    } else {
        ESP_LOGW(TAG, "LP sample count out of range: %u", lp_count);
    }

    storage_deinit();
    ESP_LOGI(TAG, "SD card flush complete");
}

/* ---- Interactive UI mode ---- */

static void handle_con_button(void)
{
    ESP_LOGI(TAG, "CON button pressed — entering interactive UI");

    /* Power on OLED via MOSFET before initializing it.
     * The MOSFET gate has a 10k pull-up so OLED is OFF during sleep. */
    gpio_config_t mosfet_cfg = {
        .pin_bit_mask = (1ULL << OLED_MOSFET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&mosfet_cfg);
    gpio_set_level(OLED_MOSFET_GPIO, 0);  /* LOW = MOSFET on */
    vTaskDelay(pdMS_TO_TICKS(50));  /* OLED power-up time */

    /* Init OLED now that it has power */
    init_oled();
    if (!s_oled_dev) {
        ESP_LOGE(TAG, "OLED init failed — cannot start UI");
        gpio_set_level(OLED_MOSFET_GPIO, 1);  /* Power off OLED */
        return;
    }

    /* Run UI — blocks until user selects Sleep/Halt or auto-sleep timeout */
    esp_err_t result = ui_run(s_oled_dev, s_ds3231_dev, s_bme280_dev);

    if (result == ESP_ERR_INVALID_STATE) {
        /* Halt requested — stop everything, enter permanent deep sleep.
         * Power cycle is the only way out. */
        ESP_LOGI(TAG, "Halt requested — stopping LP core and entering permanent deep sleep");

        /* Stop the LP core so it stops triggering ULP wakeups */
        ulp_lp_core_stop();

        /* Disable ALL wakeup sources — no button, no timer, no ULP */
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ULP);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

        /* Enter deep sleep — nothing can wake us except power cycle */
        esp_deep_sleep_start();
        /* esp_deep_sleep_start() does not return */
    }

    /* Normal sleep — fall through to deep sleep with all wakeup sources */
}

/* Wait for CON button to be released before sleeping.
 * Prevents immediate re-wakeup if user holds the button too long. */
static void wait_for_con_release(void)
{
    while (gpio_get_level(CON_BUTTON_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---- Main ---- */

void app_main(void)
{
    /* Set timezone to UTC so mktime() produces correct epoch values.
     * Without this, mktime() applies system TZ which defaults to UTC
     * but could be changed by SNTP or setenv elsewhere. */
    setenv("TZ", "UTC0", 1);
    tzset();

    uint32_t wakeup_causes = esp_sleep_get_wakeup_causes();

    /* I2C bus + sensors + ADC lose state in deep sleep (CPU reset on wake).
     * Must re-init on every wake, not just cold boot. */
    i2c_bus_init();
    init_ds3231();
    init_bme280();
    init_rain_adc();

    if (wakeup_causes == 0) {
        /* Cold boot — init LP core + UART (only once; LP core persists across deep sleep) */
        ESP_LOGI(TAG, "Cold boot — initializing");
        init_lp_uart();
        lp_core_init();

        /* Configure GPIO22 (CON) as wakeup source — active low (falling edge) */
        gpio_set_direction(CON_BUTTON_GPIO, GPIO_MODE_INPUT);
        gpio_pullup_en(CON_BUTTON_GPIO);
        gpio_wakeup_enable(CON_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);

    } else if (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_ULP)) {
        ESP_LOGI(TAG, "Woken by LP core");
        flush_samples_to_sd();

    } else if (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_GPIO)) {
        ESP_LOGI(TAG, "Woken by CON button");
        handle_con_button();
    }

    /* Wait for CON button to be released before sleeping (GPIO wake only).
     * Prevents immediate re-wakeup if user holds the button too long. */
    if (wakeup_causes & BIT(ESP_SLEEP_WAKEUP_GPIO)) {
        wait_for_con_release();
    }

    /* Enable wakeup sources for next sleep cycle */
    esp_sleep_enable_ulp_wakeup();
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(MAIN_WAKE_INTERVAL_US);

    ESP_LOGI(TAG, "Entering deep sleep");
    esp_deep_sleep_start();
}
