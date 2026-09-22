/*
 * WeatherNerd — Interactive UI implementation
 *
 * Menu-driven OLED interface with encoder + button navigation.
 * Eight menu items: Live Data, Files, WiFi Portal, Sync Clock,
 * Format SD, Reboot, Sleep, Halt.
 * Auto-sleep after 60s inactivity.
 */

#include "ui.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "oled.h"
#include "input.h"
#include "storage.h"
#include "wifi.h"

static const char *TAG = "ui";

/* OLED MOSFET gate pin — used for power-off on exit.
 * Power-on is handled by caller (main.c) before ui_run() is called. */
#define OLED_MOSFET_GPIO  5

/* Auto-sleep timeout */
#define AUTO_SLEEP_MS  60000

/* Menu items */
typedef enum {
    MENU_LIVE_DATA = 0,
    MENU_FILES,
    MENU_WIFI,
    MENU_SYNC_CLOCK,
    MENU_FORMAT_SD,
    MENU_REBOOT,
    MENU_SLEEP,
    MENU_HALT,
    MENU_COUNT,
} menu_item_t;

static const char *menu_labels[MENU_COUNT] = {
    "Live Data",
    "Files",
    "WiFi Portal",
    "Sync Clock",
    "Format SD",
    "Reboot",
    "Sleep",
    "Halt",
};

/* ---- OLED power control ---- */

static void oled_power_off(void)
{
    gpio_set_level(OLED_MOSFET_GPIO, 1);  /* HIGH = MOSFET off */
}

/* ---- Rendering helpers ---- */

static void render_menu(i2c_master_dev_handle_t oled_dev, int selected)
{
    oled_clear();
    oled_set_cursor(0, 0);
    oled_puts("WeatherNerd");

    for (int i = 0; i < MENU_COUNT; i++) {
        oled_set_cursor(1, i + 1);
        if (i == selected) {
            oled_puts("> ");
        } else {
            oled_puts("  ");
        }
        oled_puts(menu_labels[i]);
    }

    oled_render(oled_dev);
}

static void render_status(i2c_master_dev_handle_t oled_dev, const char *line1, const char *line2)
{
    oled_clear();
    oled_set_cursor(0, 0);
    oled_puts(line1);
    oled_set_cursor(0, 1);
    oled_puts(line2);
    oled_render(oled_dev);
}

/* ---- Menu actions ---- */

static void show_live_data(i2c_master_dev_handle_t oled_dev,
                           i2c_master_dev_handle_t ds3231_dev,
                           i2c_master_dev_handle_t bme280_dev)
{
    while (1) {
        oled_clear();
        oled_set_cursor(0, 0);

        /* DS3231 time */
        uint32_t epoch = 0;
        if (ds3231_dev) ds3231_get_time(ds3231_dev, &epoch);
        char timebuf[17];
        if (epoch > 0) {
            /* Simple epoch → HH:MM:SS (UTC) */
            uint32_t s = epoch % 60;
            uint32_t m = (epoch / 60) % 60;
            uint32_t h = (epoch / 3600) % 24;
            snprintf(timebuf, sizeof(timebuf), "%02lu:%02lu:%02lu",
                     (unsigned long)h, (unsigned long)m, (unsigned long)s);
        } else {
            strcpy(timebuf, "Clock not set");
        }
        oled_puts(timebuf);

        /* BME280 data */
        if (bme280_dev) {
            bme280_data_t env;
            if (bme280_read(bme280_dev, &env) == ESP_OK) {
                char buf[17];
                oled_set_cursor(0, 2);
                snprintf(buf, sizeof(buf), "T=%.1fC H=%u%%",
                         env.temp_c_x100 / 100.0, env.rh_pct);
                oled_puts(buf);
                oled_set_cursor(0, 3);
                snprintf(buf, sizeof(buf), "P=%.1f hPa",
                         env.press_hpa_x10 / 10.0);
                oled_puts(buf);
            } else {
                oled_set_cursor(0, 2);
                oled_puts("BME280 error");
            }
        }

        oled_set_cursor(0, 7);
        oled_puts("BAK=back");
        oled_render(oled_dev);

        /* Check for input */
        input_event_t ev = input_wait_event(2000);
        if (ev == INPUT_BAK_PRESS || ev == INPUT_CON_PRESS || ev == INPUT_PSH_PRESS) {
            return;
        }
        if (input_idle_ms() > AUTO_SLEEP_MS) {
            return;
        }
    }
}

static void show_files(i2c_master_dev_handle_t oled_dev)
{
    /* Mount SD, list files, unmount */
    if (storage_init() != ESP_OK) {
        render_status(oled_dev, "SD card error", "BAK=back");
        input_wait_event(5000);
        return;
    }

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        storage_deinit();
        render_status(oled_dev, "No SD card", "BAK=back");
        input_wait_event(5000);
        return;
    }

    /* Collect file names + sizes (max 20 files) */
    #define MAX_FILES 20
    char names[MAX_FILES][32];
    long sizes[MAX_FILES];
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < MAX_FILES) {
        if (entry->d_type != DT_REG) continue;
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || strcmp(dot, ".csv") != 0) continue;
        strncpy(names[count], entry->d_name, 31);
        names[count][31] = '\0';
        char path[SD_MAX_PATH_LEN + 256];
        snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, entry->d_name);
        struct stat st;
        sizes[count] = (stat(path, &st) == 0) ? st.st_size : 0;
        count++;
    }
    closedir(dir);
    storage_deinit();

    if (count == 0) {
        render_status(oled_dev, "No CSV files", "BAK=back");
        input_wait_event(5000);
        return;
    }

    /* Scrollable file list */
    int scroll = 0;
    while (1) {
        oled_clear();
        oled_set_cursor(0, 0);
        oled_puts("Files on SD:");
        for (int i = 0; i < 6 && (scroll + i) < count; i++) {
            oled_set_cursor(0, i + 1);
            char buf[17];
            snprintf(buf, sizeof(buf), "%.16s", names[scroll + i]);
            oled_puts(buf);
        }
        oled_set_cursor(0, 7);
        char nav[17];
        int n = snprintf(nav, sizeof(nav), "%d/%d BAK=back", scroll + 1, count);
        if (n < 0) n = 0;
        if (n > 16) n = 16;
        oled_puts(nav);
        oled_render(oled_dev);

        input_event_t ev = input_wait_event(10000);
        if (ev == INPUT_BAK_PRESS || ev == INPUT_CON_PRESS || ev == INPUT_PSH_PRESS) {
            return;
        }
        if (ev == INPUT_ROT_CW && scroll + 6 < count) scroll++;
        if (ev == INPUT_ROT_CCW && scroll > 0) scroll--;
        if (input_idle_ms() > AUTO_SLEEP_MS) return;
    }
}

static void start_wifi_portal(i2c_master_dev_handle_t oled_dev,
                               i2c_master_dev_handle_t ds3231_dev)
{
    render_status(oled_dev, "Starting WiFi...", "Connect to:");
    oled_set_cursor(0, 2);
    oled_puts("SSID:WeatherNerd");
    oled_set_cursor(0, 4);
    oled_puts("CON/BAK=exit");
    oled_render(oled_dev);

    /* wifi_start blocks until CON/BAK press or 5min timeout */
    wifi_start(ds3231_dev, 22);  /* GPIO22 = CON button */

    render_status(oled_dev, "WiFi stopped", "BAK=back");
    input_wait_event(3000);
}

static void sync_clock(i2c_master_dev_handle_t oled_dev,
                       i2c_master_dev_handle_t ds3231_dev)
{
    if (!ds3231_dev) {
        render_status(oled_dev, "No RTC found", "BAK=back");
        input_wait_event(3000);
        return;
    }

    /* Read current RTC time */
    uint32_t epoch = 0;
    ds3231_get_time(ds3231_dev, &epoch);

    oled_clear();
    oled_set_cursor(0, 0);
    oled_puts("Clock Sync");
    oled_set_cursor(0, 2);
    if (epoch > 0) {
        /* Show UTC time from epoch */
        uint32_t s = epoch % 60;
        uint32_t m = (epoch / 60) % 60;
        uint32_t h = (epoch / 3600) % 24;
        char buf[17];
        snprintf(buf, sizeof(buf), "UTC %02lu:%02lu:%02lu",
                 (unsigned long)h, (unsigned long)m, (unsigned long)s);
        oled_puts(buf);
    } else {
        oled_puts("RTC not set");
    }
    oled_set_cursor(0, 4);
    oled_puts("Use WiFi portal");
    oled_set_cursor(0, 5);
    oled_puts("to sync clock");
    oled_set_cursor(0, 7);
    oled_puts("BAK=back");
    oled_render(oled_dev);

    input_wait_event(10000);
}

/* ---- Main UI loop ---- */

esp_err_t ui_run(i2c_master_dev_handle_t oled_dev,
                 i2c_master_dev_handle_t ds3231_dev,
                 i2c_master_dev_handle_t bme280_dev)
{
    /* OLED is already powered on and initialized by caller (main.c).
     * We just init input and run the menu loop. */
    input_init();
    input_reset_activity();

    int selected = 0;
    bool running = true;
    esp_err_t result = ESP_OK;

    render_menu(oled_dev, selected);

    while (running) {
        input_event_t ev = input_wait_event(1000);

        switch (ev) {
        case INPUT_ROT_CW:
            selected = (selected + 1) % MENU_COUNT;
            render_menu(oled_dev, selected);
            break;
        case INPUT_ROT_CCW:
            selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
            render_menu(oled_dev, selected);
            break;
        case INPUT_CON_PRESS:
        case INPUT_PSH_PRESS:  /* Encoder push also works as confirm */
            switch (selected) {
            case MENU_LIVE_DATA:
                show_live_data(oled_dev, ds3231_dev, bme280_dev);
                break;
            case MENU_FILES:
                show_files(oled_dev);
                break;
            case MENU_WIFI:
                start_wifi_portal(oled_dev, ds3231_dev);
                break;
            case MENU_SYNC_CLOCK:
                sync_clock(oled_dev, ds3231_dev);
                break;
            case MENU_FORMAT_SD:
                /* Confirm before formatting */
                render_status(oled_dev, "Format SD card?", "CON=yes BAK=no");
                {
                    input_event_t fmt_ev = input_wait_event(10000);
                    if (fmt_ev == INPUT_CON_PRESS || fmt_ev == INPUT_PSH_PRESS) {
                        render_status(oled_dev, "Formatting...", "Please wait");
                        if (storage_init() == ESP_OK) {
                            esp_err_t fmt_ret = storage_format();
                            if (fmt_ret == ESP_OK) {
                                render_status(oled_dev, "Format OK", "BAK=back");
                            } else {
                                render_status(oled_dev, "Format failed", "BAK=back");
                            }
                            storage_deinit();
                        } else {
                            render_status(oled_dev, "No SD card", "BAK=back");
                        }
                        input_wait_event(5000);
                    }
                }
                break;
            case MENU_REBOOT:
                render_status(oled_dev, "Reboot?", "CON=yes BAK=no");
                {
                    input_event_t reb_ev = input_wait_event(10000);
                    if (reb_ev == INPUT_CON_PRESS || reb_ev == INPUT_PSH_PRESS) {
                        render_status(oled_dev, "Rebooting...", "");
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        esp_restart();
                    }
                }
                break;
            case MENU_SLEEP:
                render_status(oled_dev, "Sleep?", "CON=yes BAK=no");
                {
                    input_event_t slp_ev = input_wait_event(10000);
                    if (slp_ev == INPUT_CON_PRESS || slp_ev == INPUT_PSH_PRESS) {
                        render_status(oled_dev, "Entering", "low-power mode");
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        running = false;
                        result = ESP_OK;
                    }
                }
                break;
            case MENU_HALT:
                render_status(oled_dev, "Halt?", "CON=yes BAK=no");
                {
                    input_event_t hlt_ev = input_wait_event(10000);
                    if (hlt_ev == INPUT_CON_PRESS || hlt_ev == INPUT_PSH_PRESS) {
                        render_status(oled_dev, "System halt.", "Power cycle");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        running = false;
                        result = ESP_ERR_INVALID_STATE;
                    }
                }
                break;
            }
            if (running) {
                render_menu(oled_dev, selected);
            }
            break;
        case INPUT_BAK_PRESS:
            /* BAK at top-level menu = sleep */
            render_status(oled_dev, "Entering", "low-power mode");
            vTaskDelay(pdMS_TO_TICKS(1000));
            running = false;
            result = ESP_OK;
            break;
        default:
            /* Timeout or no event — check auto-sleep */
            if (input_idle_ms() > AUTO_SLEEP_MS) {
                ESP_LOGI(TAG, "Auto-sleep after %dms inactivity", AUTO_SLEEP_MS);
                running = false;
                result = ESP_OK;
            }
            break;
        }
    }

    /* Power off OLED */
    oled_display_off(oled_dev);
    vTaskDelay(pdMS_TO_TICKS(10));
    oled_power_off();

    ESP_LOGI(TAG, "UI exited (result=%s)", esp_err_to_name(result));
    return result;
}
