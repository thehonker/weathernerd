/*
 * WeatherNerd — Custom WindNerd Core Firmware
 * Target: STM32G031F8Px (WindNerd Core board)
 *
 * Replaces factory firmware for ESP32-C6 triggered on-demand sampling.
 *
 * Factory firmware: SLEEP mode (~0.6 mA), TIM3 timer drives 3s sampling
 * This firmware:   STOP mode (~5 µA), ESP32-C6 GPIO trigger drives 5s sampling
 *
 * Flow:
 *   1. Boot → init UART, TMAG5273, pulse ISR, trigger ISR
 *   2. Enter STOP mode (~5 µA, SRAM retained)
 *   3. Rotor pulses increment counter via EXTI (works in STOP)
 *   4. ESP32-C6 raises trigger pin → EXTI wakes STM32
 *   5. Restore 8 MHz clock (STOP mode drops it)
 *   6. Read pulse count + TMAG5273 angle
 *   7. Output "WNI,<speed_mps>,<direction_deg>\n" via USART2
 *   8. Return to STOP mode
 *
 * Dependencies:
 *   - WindNerd Core library (for TMAG5273 + I2C code only, not WN_Core class)
 *   - stm32duino board package
 *
 * Build:
 *   Arduino IDE: Generic STM32G031F8Px, upload via ST-Link SWD
 *   See WindNerd Core docs/PROGRAM.md for setup instructions
 */

#include "Arduino.h"
#include "Windnerd_TMAG5273.h"   // TMAG5273 angle sensor + WN_I2C (no WN_Core needed)
#include "stm32g0xx_hal.h"       // STOP mode + clock config

// === Pin assignments (WindNerd Core board) ===

// These match the WindNerd Core library defaults. The TMAG5273 functions
// use them internally via wn_init_angle_sensor().
#define I2C_SCL_PIN 22       // PA22 — TMAG5273 I2C SCL
#define I2C_SDA_PIN 23       // PA23 — TMAG5273 I2C SDA

// Rotor pulse input — same physical pin as factory firmware.
// We attach our own ISR here (WN_Core::begin() is never called).
#define SPEED_INPUT_PIN 25   // PA25 — reed switch rotor pulse

// ESP32-C6 trigger input — PA0 is on the WindNerd Core expansion header.
// Other header pins (PA1, PA3, PA4, PA5, PA6) also work if PA0 is needed
// for something else. All are EXTI-capable.
#define TRIGGER_PIN PA0

// UART output to ESP32-C6 (TX2 pin, yellow wire — same as factory)
#define UART_BAUD 9600

// === Calibration ===

// Standard WindNerd rotor: 1 Hz = 1.31 m/s
#define HZ_TO_MS 1.31f

// Expected seconds between ESP32-C6 triggers. Used for speed calculation
// since we can't use millis()/micros() after STOP mode (SysTick stops).
// The ESP32-C6 controls timing, so this is accurate. If the interval
// changes, update this constant and reflash.
#define SAMPLE_INTERVAL_SEC 5

// === State ===

// Pulse counter — incremented by EXTI ISR. Works in STOP mode because
// EXTI interrupts wake the CPU briefly to run the ISR.
static volatile uint32_t pulse_count = 0;

// Trigger flag — set by trigger ISR, checked in loop()
static volatile bool triggered = false;

// UART output (USART2 = TX2 pin, yellow wire)
// stm32duino 3.0.0: Uart is the concrete class (HardwareSerial is abstract)
Uart SerialOutput(USART2);

// === Interrupt handlers ===

// Rotor pulse ISR — minimal, no debounce.
//
// The factory firmware uses micros() for 10ms debounce, but micros() is
// unreliable in STOP mode (SysTick is stopped). We skip debounce here.
// A few extra pulses from reed switch bounce won't significantly affect
// speed over a 5s window — the factory's 10ms filter mainly rejects EMI
// interference, which is a non-issue for a short wire run inside the
// anemometer housing.
void onPulseISR() {
  pulse_count++;
}

// ESP32-C6 trigger ISR — wakes us from STOP mode via EXTI
void onTriggerISR() {
  triggered = true;
}

// === Setup ===

void setup() {
  // UART output to ESP32-C6
  SerialOutput.begin(UART_BAUD);

  // TMAG5273 magnetic angle sensor (wind direction)
  // Initializes I2C bus and puts sensor in sleep mode
  wn_init_angle_sensor(I2C_SCL_PIN, I2C_SDA_PIN);

  // Rotor pulse input (reed switch)
  pinMode(SPEED_INPUT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(SPEED_INPUT_PIN), onPulseISR, RISING);

  // ESP32-C6 trigger input
  // Pulldown so the pin stays low when ESP32-C6 is asleep or disconnected
  pinMode(TRIGGER_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(TRIGGER_PIN), onTriggerISR, RISING);
}

// === Main loop ===

void loop() {
  if (!triggered) {
    // Enter STOP mode — ~5 µA current, SRAM and registers retained
    // Rotor pulse EXTI still fires, incrementing pulse_count
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    // ↑ CPU halts here. Resumes after trigger ISR fires.
    SystemClock_Config();  // Restore 8 MHz clock (STOP drops to HSI 16 MHz)
    return;
  }

  triggered = false;

  // Read and reset pulse count atomically
  noInterrupts();
  uint32_t pulses = pulse_count;
  pulse_count = 0;
  interrupts();

  // Read wind direction from TMAG5273
  // Wakes sensor, measures, puts back to sleep (~11 ms)
  uint16_t angle = 0;
  bool dir_ok = wn_read_then_make_angle_sensor_sleep(&angle);

  // Compute wind speed (m/s)
  // speed = pulses * calibration / time_interval
  float speed_ms = (pulses * HZ_TO_MS) / SAMPLE_INTERVAL_SEC;

  // Output: WNI,<speed>,<direction>
  // Same format as factory firmware instant wind message
  SerialOutput.print("WNI,");
  SerialOutput.print(speed_ms, 1);
  SerialOutput.print(",");
  SerialOutput.println(dir_ok ? angle : 0);
}

// === System clock — 8 MHz for low power ===
// Override of stm32duino weak function. Called at startup and after
// STOP mode wakeup. STOP mode resets the system clock to HSI 16 MHz;
// this restores the 8 MHz configuration (HSI/2, no PLL).
// Generated by STM32CubeMX, same as factory firmware.
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV2;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
    Error_Handler();
  }

  SystemCoreClockUpdate();
}
