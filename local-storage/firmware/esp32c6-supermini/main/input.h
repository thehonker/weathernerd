/*
 * WeatherNerd — Rotary encoder + button input handler
 *
 * Handles the EC11 rotary encoder (TRA/TRB/PSH) and two pushbuttons (CON/BAK)
 * on the OLED+encoder board. Uses GPIO interrupts for encoder rotation
 * and button presses. Debounced in software.
 *
 * Pin assignments:
 *   GPIO14 = TRA (encoder A, interrupt)
 *   GPIO20 = TRB (encoder B)
 *   GPIO21 = PSH (encoder push, active low)
 *   GPIO22 = CON (confirm, active low, also deep-sleep wake source)
 *   GPIO23 = BAK (back, active low)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Input event types */
typedef enum {
    INPUT_NONE = 0,
    INPUT_ROT_CW,      /* encoder rotated clockwise */
    INPUT_ROT_CCW,     /* encoder rotated counter-clockwise */
    INPUT_PSH_PRESS,   /* encoder push button pressed */
    INPUT_CON_PRESS,   /* CON button pressed */
    INPUT_BAK_PRESS,   /* BAK button pressed */
} input_event_t;

/* Initialize encoder + button GPIO pins and interrupts.
 * Idempotent — safe to call multiple times. */
void input_init(void);

/* Drain all pending events from the queue.
 * Call before entering a new UI context to avoid stale events. */
void input_flush(void);

/* Get the next input event (non-blocking).
 * Returns INPUT_NONE if no event pending. */
input_event_t input_get_event(void);

/* Wait for an input event (blocking, with timeout in ms).
 * Returns INPUT_NONE on timeout. */
input_event_t input_wait_event(uint32_t timeout_ms);

/* Reset the inactivity timer (call on any user interaction). */
void input_reset_activity(void);

/* Get milliseconds since last input activity.
 * Used for auto-sleep timeout. */
uint32_t input_idle_ms(void);
