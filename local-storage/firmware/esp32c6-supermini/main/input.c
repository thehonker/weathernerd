/*
 * WeatherNerd — Rotary encoder + button input implementation
 *
 * EC11 encoder with GPIO interrupt on TRA (A pin), sampled TRB for direction.
 * Three momentary buttons (PSH, CON, BK) with software debounce.
 * Events queued in a small ring buffer for consumption by the UI task.
 */

#include "input.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "input";

/* Pin assignments */
#define PIN_TRA   14
#define PIN_TRB   20
#define PIN_PSH   21
#define PIN_CON   22
#define PIN_BAK   23

/* Debounce time (ms) */
#define DEBOUNCE_MS  50

/* Event queue */
#define QUEUE_LEN 16
static QueueHandle_t s_event_queue = NULL;
static bool s_isr_installed = false;

/* Inactivity tracking */
static uint32_t s_last_activity_ms = 0;

/* Debounce state */
static int64_t s_last_rot_time = 0;
static int64_t s_last_psh_time = 0;
static int64_t s_last_con_time = 0;
static int64_t s_last_bak_time = 0;

/* ---- ISR handlers ---- */

static void IRAM_ATTR encoder_isr(void *arg)
{
    int64_t now = esp_timer_get_time() / 1000;  /* ms */
    if (now - s_last_rot_time < DEBOUNCE_MS) return;
    s_last_rot_time = now;

    /* Read B pin to determine direction */
    int b = gpio_get_level(PIN_TRB);
    input_event_t ev = b ? INPUT_ROT_CCW : INPUT_ROT_CW;
    /* On a common EC11: if A falls and B is high → CCW, B low → CW.
     * This may need to be swapped depending on encoder wiring. */
    xQueueSendFromISR(s_event_queue, &ev, NULL);
}

static void IRAM_ATTR button_isr(void *arg)
{
    int pin = (int)arg;
    int64_t now = esp_timer_get_time() / 1000;
    input_event_t ev = INPUT_NONE;

    switch (pin) {
    case PIN_PSH:
        if (now - s_last_psh_time < DEBOUNCE_MS) return;
        s_last_psh_time = now;
        ev = INPUT_PSH_PRESS;
        break;
    case PIN_CON:
        if (now - s_last_con_time < DEBOUNCE_MS) return;
        s_last_con_time = now;
        ev = INPUT_CON_PRESS;
        break;
    case PIN_BAK:
        if (now - s_last_bak_time < DEBOUNCE_MS) return;
        s_last_bak_time = now;
        ev = INPUT_BAK_PRESS;
        break;
    }

    if (ev != INPUT_NONE) {
        xQueueSendFromISR(s_event_queue, &ev, NULL);
    }
}

/* ---- Public API ---- */

void input_init(void)
{
    /* Create queue if not already created */
    if (!s_event_queue) {
        s_event_queue = xQueueCreate(QUEUE_LEN, sizeof(input_event_t));
    }

    /* Configure encoder pins */
    gpio_config_t enc_cfg = {
        .pin_bit_mask = (1ULL << PIN_TRA) | (1ULL << PIN_TRB),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  /* A pin falling edge */
    };
    gpio_config(&enc_cfg);

    /* Configure button pins */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PIN_PSH) | (1ULL << PIN_CON) | (1ULL << PIN_BAK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_cfg);

    /* Install ISR service once — idempotent */
    if (!s_isr_installed) {
        gpio_install_isr_service(0);
        s_isr_installed = true;
    }

    /* Add ISR handlers — remove first if already added (idempotent) */
    gpio_isr_handler_remove(PIN_TRA);
    gpio_isr_handler_remove(PIN_PSH);
    gpio_isr_handler_remove(PIN_CON);
    gpio_isr_handler_remove(PIN_BAK);

    gpio_isr_handler_add(PIN_TRA, encoder_isr, NULL);
    gpio_isr_handler_add(PIN_PSH, button_isr, (void *)PIN_PSH);
    gpio_isr_handler_add(PIN_CON, button_isr, (void *)PIN_CON);
    gpio_isr_handler_add(PIN_BAK, button_isr, (void *)PIN_BAK);

    /* Drain any stale events from previous UI context */
    input_flush();

    s_last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Input initialized (TRA=%d TRB=%d PSH=%d CON=%d BAK=%d)",
             PIN_TRA, PIN_TRB, PIN_PSH, PIN_CON, PIN_BAK);
}

void input_flush(void)
{
    if (!s_event_queue) return;
    input_event_t ev;
    while (xQueueReceive(s_event_queue, &ev, 0) == pdTRUE) {
        /* discard */
    }
}

input_event_t input_get_event(void)
{
    input_event_t ev = INPUT_NONE;
    if (xQueueReceive(s_event_queue, &ev, 0) == pdTRUE) {
        s_last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    return ev;
}

input_event_t input_wait_event(uint32_t timeout_ms)
{
    input_event_t ev = INPUT_NONE;
    if (xQueueReceive(s_event_queue, &ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        s_last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }
    return ev;
}

void input_reset_activity(void)
{
    s_last_activity_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

uint32_t input_idle_ms(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    return now - s_last_activity_ms;
}
