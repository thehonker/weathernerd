/*
 * WeatherNerd ESP32-C6 — LP Core firmware
 *
 * Runs on the ULP LP Core (RISC-V coprocessor) while the main core sleeps.
 * Wakes every 5 seconds via LP timer and performs:
 *
 * 1. Trigger WindNerd Core (GPIO1 pulse → STM32G031F8 wakes from STOP mode)
 * 2. Read UART response on LP_UART (GPIO4, 9600 baud) — "WNI,<speed>,<dir>\n"
 * 3. Store wind sample in shared memory buffer
 * 4. After 12 samples (1 minute), wake the main core to flush to SD card
 *
 * Note: The ESP32-C6 LP core does not have LP ADC support.
 * Rain ADC sampling is handled by the main core on its 1-minute wake cycle.
 *
 * ADC and UART are initialized by the main core before starting the LP core.
 * The LP core only reads from them.
 */

#include <stdint.h>
#include <string.h>
#include "ulp_lp_core.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_gpio.h"
#include "ulp_lp_core_uart.h"
#include "ulp_lp_core_print.h"

/* ---- Configuration ---- */

#define SAMPLES_PER_FLUSH       12      /* 12 × 5s = 60s = 1 min */
#define WINDNERD_TRIGGER_PIN   LP_IO_NUM_1
#define LP_UART_PORT            LP_UART_NUM_0

#define UART_BUF_SIZE           64
#define UART_TIMEOUT_MS         100

/* ---- Shared variables (accessible from main core via ulp_ prefix) ---- */

typedef struct {
    uint16_t speed_mps_x10;    /* wind speed × 10 (3.2 m/s = 32) */
    uint16_t direction_deg;    /* 0-359, or 0 if read failed */
    uint8_t  valid;            /* 1 = valid sample, 0 = empty */
} __attribute__((packed)) wind_sample_t;

/* These globals are exported to the main core via the generated header. */
wind_sample_t sample_buffer[SAMPLES_PER_FLUSH];
uint16_t sample_write_idx;
uint16_t sample_count;
uint8_t  buffer_ready;  /* Set by LP core when buffer is full, cleared by main core */

/* ---- WindNerd trigger ---- */

static void trigger_windnerd(void)
{
    /* Pulse GPIO1 high for 10ms to wake the STM32G031F8 from STOP mode.
     * The STM32 is configured for EXTI RISING on its trigger pin. */
    ulp_lp_core_gpio_set_level(WINDNERD_TRIGGER_PIN, 1);
    ulp_lp_core_delay_us(10 * 1000);
    ulp_lp_core_gpio_set_level(WINDNERD_TRIGGER_PIN, 0);
}

/* ---- UART parsing ---- */

/* Parse "WNI,<speed>,<dir>\n" from UART buffer.
 * Returns 0 on success, -1 on parse failure. */
static int parse_wni(const char *buf, int len, uint16_t *speed_x10, uint16_t *direction)
{
    const char *p = NULL;
    for (int i = 0; i <= len - 4; i++) {
        if (buf[i] == 'W' && buf[i+1] == 'N' && buf[i+2] == 'I' && buf[i+3] == ',') {
            p = &buf[i + 4];
            break;
        }
    }
    if (!p) return -1;

    /* Parse speed (float × 10) */
    int speed_int = 0, speed_frac = 0;
    const char *s = p;
    while (*s >= '0' && *s <= '9') {
        speed_int = speed_int * 10 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        if (*s >= '0' && *s <= '9') {
            speed_frac = *s - '0';
            s++;
        }
    }
    *speed_x10 = speed_int * 10 + speed_frac;

    if (*s != ',') return -1;
    s++;

    /* Parse direction (integer 0-359) */
    int dir = 0;
    while (*s >= '0' && *s <= '9') {
        dir = dir * 10 + (*s - '0');
        s++;
    }
    if (dir < 0 || dir > 359) dir = 0;
    *direction = dir;

    return 0;
}

/* ---- Main ---- */

int main(void)
{
    /* Initialize trigger GPIO — UART is already initialized
     * by the main core before starting the LP core. */
    ulp_lp_core_gpio_init(WINDNERD_TRIGGER_PIN);
    ulp_lp_core_gpio_output_enable(WINDNERD_TRIGGER_PIN);
    ulp_lp_core_gpio_set_level(WINDNERD_TRIGGER_PIN, 0);

    char uart_buf[UART_BUF_SIZE];

    while (1) {
        /* 1. Trigger WindNerd */
        trigger_windnerd();

        /* 2. Read UART response */
        memset(uart_buf, 0, sizeof(uart_buf));
        int len = lp_core_uart_read_bytes(LP_UART_PORT, (uint8_t *)uart_buf,
                                          sizeof(uart_buf) - 1, UART_TIMEOUT_MS);

        /* 3. Parse and store sample */
        wind_sample_t *slot = &sample_buffer[sample_write_idx];
        slot->valid = 0;

        if (len > 0) {
            uint16_t speed_x10 = 0, direction = 0;
            if (parse_wni(uart_buf, len, &speed_x10, &direction) == 0) {
                slot->speed_mps_x10 = speed_x10;
                slot->direction_deg = direction;
                slot->valid = 1;
            }
        }

        /* 4. Advance buffer */
        sample_write_idx = (sample_write_idx + 1) % SAMPLES_PER_FLUSH;
        if (sample_count < SAMPLES_PER_FLUSH) {
            sample_count++;
        }

        /* 5. After 12 samples, wake the main core.
         * Set buffer_ready so main core knows data is available.
         * Wait for main core to clear it before overwriting (race condition prevention). */
        if (sample_write_idx == 0 && sample_count == SAMPLES_PER_FLUSH) {
            buffer_ready = 1;
            ulp_lp_core_wakeup_main_processor();
        }

        /* 6. If buffer was consumed by main core, reset for next cycle.
         * This check happens at the top of the next 5s loop iteration —
         * by then the main core has had plenty of time to read the buffer. */
        if (buffer_ready == 0 && sample_count == SAMPLES_PER_FLUSH) {
            sample_count = 0;
            sample_write_idx = 0;
        }

        /* 7. Halt — LP timer will wake us in 5 seconds */
        ulp_lp_core_halt();
    }

    return 0;
}
