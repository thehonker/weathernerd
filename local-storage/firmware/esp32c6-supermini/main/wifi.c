/*
 * WeatherNerd — WiFi retrieval mode implementation
 *
 * Soft-AP + HTTP server for on-demand data retrieval and clock sync.
 * Serves a captive portal page that lists CSV files, allows individual
 * downloads, bulk download, and syncs the DS3231 RTC from the browser's clock.
 */

#include "wifi.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "storage.h"
#include "ds3231.h"

static const char *TAG = "wifi";

/* WiFi AP config */
#define WIFI_SSID       "WeatherNerd"
#define WIFI_PASS       ""          /* Open network — easiest for field retrieval */
#define WIFI_CHANNEL    1
#define WIFI_MAX_CONN   4

/* HTTP server state */
static httpd_handle_t s_server = NULL;
static i2c_master_dev_handle_t s_ds3231_dev = NULL;
static int s_con_gpio = -1;
static volatile bool s_exit_requested = false;

/* ---- Captive portal HTML ----
 * Single-page app: file list, download buttons, clock sync.
 * No external dependencies — all inline CSS/JS. */

static const char *PORTAL_HTML =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>WeatherNerd</title>"
"<style>"
"body{font-family:system-ui,sans-serif;max-width:800px;margin:0 auto;padding:16px;background:#1a1a2e;color:#e0e0e0}"
"h1{color:#0fbcf9;margin-bottom:4px}"
".sub{color:#888;margin-bottom:16px;font-size:14px}"
".card{background:#16213e;border-radius:8px;padding:16px;margin-bottom:16px}"
"table{width:100%;border-collapse:collapse}"
"th,td{text-align:left;padding:8px 12px;border-bottom:1px solid #333}"
"th{color:#0fbcf9;font-size:13px;text-transform:uppercase}"
"a{color:#0fbcf9;text-decoration:none}"
"a:hover{text-decoration:underline}"
"button,.btn{background:#0fbcf9;color:#000;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;font-size:14px}"
"button:hover,.btn:hover{background:#0ea5d9}"
".sync{display:flex;gap:8px;align-items:center}"
"#status{font-size:13px;color:#888;margin-top:8px}"
".file-size{color:#888;font-size:13px}"
"</style></head><body>"
"<h1>🌬️ WeatherNerd</h1>"
"<div class='sub'>Remote weather station — data retrieval &amp; clock sync</div>"

"<div class='card'>"
"<h2>🕐 Clock Sync</h2>"
"<p>Current station time: <span id='rtc-time'>loading...</span></p>"
"<div class='sync'>"
"<button onclick='syncClock()'>Sync from browser</button>"
"<span id='status'></span>"
"</div></div>"

"<div class='card'>"
"<h2>📁 Data Files</h2>"
"<p><button onclick='downloadAll()'>Download All (.zip not supported — downloads sequentially)</button></p>"
"<table><thead><tr><th>Filename</th><th>Size</th><th></th></tr></thead>"
"<tbody id='file-list'><tr><td colspan='3'>Loading...</td></tr></tbody></table>"
"</div>"

"<div class='card'>"
"<h2>💾 Format SD Card</h2>"
"<p>Erases all data on the SD card. This cannot be undone.</p>"
"<button onclick='formatSD()' style='background:#e74c3c'>Format SD Card</button>"
"<span id='format-status'></span>"
"</div>"

"<div class='card'>"
"<h2>⚙️ System</h2>"
"<p><button onclick='reboot()'>Reboot</button>"
"<button onclick='halt()' style='background:#e74c3c;margin-left:8px'>Halt</button></p>"
"<span id='sys-status'></span>"
"</div>"

"<script>"
"async function loadFiles(){"
"  const r=await fetch('/api/files');"
"  const files=await r.json();"
"  const tb=document.getElementById('file-list');"
"  if(!files.length){tb.innerHTML='<tr><td colspan=3>No data files found</td></tr>';return;}"
"  tb.innerHTML=files.map(f=>"
"    `<tr><td>${f.name}</td><td class='file-size'>${fmtSize(f.size)}</td>`+"
"    `<td><a class='btn' href='/api/files/${f.name}' download>Download</a></td></tr>`"
"  ).join('');"
"}"
"function fmtSize(b){if(b<1024)return b+' B';if(b<1048576)return(b/1024).toFixed(1)+' KB';return(b/1048576).toFixed(1)+' MB'}"
"async function loadRtcTime(){"
"  const r=await fetch('/api/rtc-time');"
"  const d=await r.json();"
"  document.getElementById('rtc-time').textContent=d.epoch?new Date(d.epoch*1000).toLocaleString():'Not set';"
"}"
"async function syncClock(){"
"  const now=Math.floor(Date.now()/1000);"
"  const r=await fetch('/api/sync-clock',{method:'POST',body:String(now)});"
"  const d=await r.json();"
"  document.getElementById('status').textContent=d.ok?'Synced!':'Failed';"
"  setTimeout(loadRtcTime,500);"
"}"
"function downloadAll(){"
"  fetch('/api/files').then(r=>r.json()).then(files=>{"
"    files.forEach((f,i)=>setTimeout(()=>{"
"      const a=document.createElement('a');a.href='/api/files/'+f.name;a.download=f.name;a.click()"
"    },i*500))"
"  })"
"}"
"async function formatSD(){"
"  if(!confirm('Format SD card? ALL DATA WILL BE ERASED!'))return"
"  document.getElementById('format-status').textContent='Formatting...'"
"  const r=await fetch('/api/format-sd',{method:'POST'})"
"  const d=await r.json()"
"  document.getElementById('format-status').textContent=d.ok?'Done':'Failed: '+(d.error||'')"
"  if(d.ok)setTimeout(loadFiles,1000)"
"}"
"async function reboot(){"
"  if(!confirm('Reboot the station? Sampling stops briefly during reboot.'))return"
"  document.getElementById('sys-status').textContent='Rebooting...'"
"  try{await fetch('/api/reboot',{method:'POST'})}catch(e){}"
"  document.getElementById('sys-status').textContent='Rebooting... WiFi will disconnect'"
"}"
"async function halt(){"
"  if(!confirm('Halt the station? It will enter permanent deep sleep. Power cycle to resume.'))return"
"  document.getElementById('sys-status').textContent='Halting...'"
"  try{await fetch('/api/halt',{method:'POST'})}catch(e){}"
"  document.getElementById('sys-status').textContent='Halting... WiFi will disconnect'"
"}"
"loadFiles();loadRtcTime();"
"</script></body></html>";

/* ---- HTTP handlers ---- */

/* GET / — serve portal page */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PORTAL_HTML);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* GET /api/files — JSON list of CSV files on SD card */
static esp_err_t handler_file_list(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    /* Build JSON array */
    httpd_resp_sendstr_chunk(req, "[");
    bool first = true;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue;
        /* Only list .csv files */
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || strcmp(dot, ".csv") != 0) continue;

        char path[SD_MAX_PATH_LEN + 256];
        snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (!first) httpd_resp_sendstr_chunk(req, ",");
        first = false;

        httpd_resp_sendstr_chunk(req, "{\"name\":\"");
        httpd_resp_sendstr_chunk(req, entry->d_name);
        httpd_resp_sendstr_chunk(req, "\",\"size\":");
        char sizebuf[20];
        snprintf(sizebuf, sizeof(sizebuf), "%ld", (long)st.st_size);
        httpd_resp_sendstr_chunk(req, sizebuf);
        httpd_resp_sendstr_chunk(req, "}");
    }
    closedir(dir);
    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* GET /api/files/<name> — download individual file */
static esp_err_t handler_file_download(httpd_req_t *req)
{
    /* Extract filename from URI: /api/files/<name> */
    const char *uri = req->uri;
    const char *prefix = "/api/files/";
    size_t prefix_len = strlen(prefix);
    if (strlen(uri) <= prefix_len) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No filename");
        return ESP_FAIL;
    }
    const char *filename = uri + prefix_len;

    /* Sanitize: no path separators allowed (prevent directory traversal) */
    if (strchr(filename, '/') || strchr(filename, '\\') || strstr(filename, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    char path[SD_MAX_PATH_LEN + 256];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, filename);

    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    /* Set content type for CSV */
    httpd_resp_set_type(req, "text/csv");
    /* Force download with Content-Disposition */
    char disp[128];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    /* Stream file in chunks */
    char buf[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, bytes_read) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);  /* end response */
    return ESP_OK;
}

/* GET /api/rtc-time — return current DS3231 time as JSON */
static esp_err_t handler_rtc_time(httpd_req_t *req)
{
    uint32_t epoch = 0;
    if (s_ds3231_dev) {
        ds3231_get_time(s_ds3231_dev, &epoch);
    }

    char json[64];
    snprintf(json, sizeof(json), "{\"epoch\":%" PRIu32 "}", epoch);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

/* POST /api/sync-clock — set DS3231 time from browser (body: epoch seconds as string) */
static esp_err_t handler_sync_clock(httpd_req_t *req)
{
    char body[16] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    uint32_t epoch = (uint32_t)strtoul(body, NULL, 10);
    if (epoch < 1700000000) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid epoch\"}");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    if (s_ds3231_dev) {
        ret = ds3231_set_time(s_ds3231_dev, epoch);
    }

    httpd_resp_set_type(req, "application/json");
    if (ret == ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":true}");
        ESP_LOGI(TAG, "Clock synced to epoch %" PRIu32, epoch);
    } else {
        httpd_resp_sendstr(req, "{\"ok\":false}");
    }
    return ESP_OK;
}

/* POST /api/format-sd — format the SD card filesystem (erases all data!) */
static esp_err_t handler_format_sd(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Format SD card requested via web UI");

    esp_err_t ret = storage_format();

    httpd_resp_set_type(req, "application/json");
    if (ret == ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"SD card formatted successfully\"}");
        ESP_LOGI(TAG, "SD card formatted via web UI");
    } else {
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"ok\":false,\"error\":\"%s\"}", esp_err_to_name(ret));
        httpd_resp_sendstr(req, json);
        ESP_LOGE(TAG, "SD card format failed: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

/* POST /api/reboot — reboot the ESP32-C6 */
static esp_err_t handler_reboot(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Reboot requested via web UI");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    /* Small delay so the HTTP response is sent before reboot */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;  /* not reached */
}

/* POST /api/halt — halt the system (stop LP core, permanent deep sleep, power cycle to resume) */
static esp_err_t handler_halt(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Halt requested via web UI");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Stop LP core */
    extern void ulp_lp_core_stop(void);
    ulp_lp_core_stop();

    /* Disable all wakeup sources */
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ULP);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

    /* Permanent deep sleep — power cycle to resume */
    esp_deep_sleep_start();
    return ESP_OK;  /* not reached */
}

/* 404 handler — redirect to portal (captive portal behavior) */
static esp_err_t handler_404(httpd_req_t *req, httpd_err_code_t error)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "Redirect to portal");
    return ESP_OK;
}

/* ---- WiFi soft-AP ---- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = event_data;
        ESP_LOGI(TAG, "Station connected: " MACSTR, MAC2STR(event->mac));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = event_data;
        ESP_LOGI(TAG, "Station disconnected: " MACSTR, MAC2STR(event->mac));
    }
}

static esp_err_t wifi_init_ap(void)
{
    /* NVS is needed for WiFi */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = WIFI_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Get AP IP address */
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);
    ESP_LOGI(TAG, "WiFi AP started: SSID='%s', IP=" IPSTR, WIFI_SSID, IP2STR(&ip_info.ip));

    return ESP_OK;
}

static void wifi_deinit_ap(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    ESP_LOGI(TAG, "WiFi AP stopped");
}

/* ---- HTTP server ---- */

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    /* Register handlers */
    static const httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = handler_root,
    };
    static const httpd_uri_t uri_files = {
        .uri = "/api/files", .method = HTTP_GET, .handler = handler_file_list,
    };
    static const httpd_uri_t uri_file_dl = {
        .uri = "/api/files/*", .method = HTTP_GET, .handler = handler_file_download,
    };
    static const httpd_uri_t uri_rtc_time = {
        .uri = "/api/rtc-time", .method = HTTP_GET, .handler = handler_rtc_time,
    };
    static const httpd_uri_t uri_sync_clock = {
        .uri = "/api/sync-clock", .method = HTTP_POST, .handler = handler_sync_clock,
    };
    static const httpd_uri_t uri_format_sd = {
        .uri = "/api/format-sd", .method = HTTP_POST, .handler = handler_format_sd,
    };
    static const httpd_uri_t uri_reboot = {
        .uri = "/api/reboot", .method = HTTP_POST, .handler = handler_reboot,
    };
    static const httpd_uri_t uri_halt = {
        .uri = "/api/halt", .method = HTTP_POST, .handler = handler_halt,
    };

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_files);
    httpd_register_uri_handler(server, &uri_file_dl);
    httpd_register_uri_handler(server, &uri_rtc_time);
    httpd_register_uri_handler(server, &uri_sync_clock);
    httpd_register_uri_handler(server, &uri_format_sd);
    httpd_register_uri_handler(server, &uri_reboot);
    httpd_register_uri_handler(server, &uri_halt);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, handler_404);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return server;
}

/* ---- Exit monitor task ----
 * With a momentary button, the wake press is already released by the time
 * code runs. So we wait for a *press* (CON or BAK) to signal exit, plus
 * a timeout in case the user walks away. */

#define WIFI_PORTAL_TIMEOUT_MS  (5 * 60 * 1000)  /* 5 minutes */

static void exit_monitor_task(void *arg)
{
    uint32_t elapsed = 0;
    const uint32_t poll_ms = 200;

    while (elapsed < WIFI_PORTAL_TIMEOUT_MS) {
        /* Check if CON or BAK button is pressed (active low) */
        if (gpio_get_level(s_con_gpio) == 0) {
            /* CON pressed again — user wants to exit */
            vTaskDelay(pdMS_TO_TICKS(50));  /* debounce */
            if (gpio_get_level(s_con_gpio) == 0) {
                s_exit_requested = true;
                break;
            }
        }
        /* Check BAK button (GPIO23) if it's configured */
        if (gpio_get_level(23) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(23) == 0) {
                s_exit_requested = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
        elapsed += poll_ms;
    }

    if (!s_exit_requested) {
        ESP_LOGI(TAG, "WiFi portal timeout (%dms) — auto-sleep", WIFI_PORTAL_TIMEOUT_MS);
        s_exit_requested = true;
    }
    vTaskDelete(NULL);
}

/* ---- Public API ---- */

esp_err_t wifi_start(i2c_master_dev_handle_t ds3231_dev, int con_gpio)
{
    s_ds3231_dev = ds3231_dev;
    s_con_gpio = con_gpio;
    s_exit_requested = false;

    ESP_LOGI(TAG, "Starting WiFi soft-AP + HTTP portal");

    /* Mount SD card so files are accessible */
    esp_err_t ret = storage_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        /* Continue anyway — clock sync still works */
    }

    /* Start WiFi AP */
    ret = wifi_init_ap();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        storage_deinit();
        return ret;
    }

    /* Start HTTP server */
    s_server = start_webserver();
    if (!s_server) {
        wifi_deinit_ap();
        storage_deinit();
        return ESP_FAIL;
    }

    /* Start exit monitor task — waits for CON/BAK press or 5min timeout */
    xTaskCreate(exit_monitor_task, "exit_mon", 2048, NULL, 1, NULL);

    /* Wait for exit signal */
    while (!s_exit_requested) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Exiting WiFi portal — entering low-power mode");

    /* Sleep — stop HTTP server, WiFi, unmount SD */
    httpd_stop(s_server);
    s_server = NULL;
    wifi_deinit_ap();
    storage_deinit();

    /* Drain any stale button events that fired during WiFi mode */
    extern void input_flush(void);
    input_flush();

    ESP_LOGI(TAG, "WiFi portal sleep complete");
    return ESP_OK;
}
