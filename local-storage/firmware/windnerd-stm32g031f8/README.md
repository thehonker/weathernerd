# windnerd-stm32g031f8 — Custom WindNerd Core Firmware

[![WindNerd STM32G031F8 firmware](https://github.com/thehonker/weathernerd/actions/workflows/build-windnerd-stm32g031f8.yml/badge.svg)](https://github.com/thehonker/weathernerd/actions/workflows/build-windnerd-stm32g031f8.yml)

Custom firmware for the [WindNerd Core](https://github.com/windnerd-labs/Windnerd-Core) board (STM32G031F8Px), replacing the factory firmware for ESP32-C6 triggered on-demand sampling.

## Why custom firmware?

The factory firmware is designed for standalone operation — it runs a 10 Hz timer (TIM3) that drives 3-second sampling windows, outputs `WNI`/`WNA` messages on UART, and enters SLEEP mode between samples. Current draw: ~0.6 mA in low power mode.

Our station doesn't need continuous sampling. The ESP32-C6 LP core triggers a read every 5 seconds. Between triggers, the WindNerd Core has nothing to do. STOP mode (~5 µA) drops power consumption by 120× — from 0.6 mA to 0.04 mA.

| | Factory firmware | This firmware |
|---|---|---|
| Sleep mode | SLEEP (~0.6 mA) | STOP (~5 µA) |
| Sampling trigger | TIM3 timer (10 Hz) | ESP32-C6 GPIO (EXTI) |
| Sample interval | 3 s (fixed) | 5 s (configurable, set by ESP32-C6) |
| Output | `WNI` + `WNA` messages | `WNI` only (on demand) |
| TMAG5273 | Read every 100-500 ms | Read only on trigger |
| LEDs | Active (speed + north) | Off (not initialized) |
| Power | ~0.6 mA | ~0.04 mA (estimated) |

## How it works

```
                    ESP32-C6 GPIO1 (trigger)
                           │ RISING edge
                           ▼
    ┌─────────────────────────────────────┐
    │  STM32G031F8 in STOP mode (~5 µA)   │
    │                                     │
    │  EXTI wakes CPU                     │
    │  → restore 8 MHz clock              │
    │  → read pulse count (reset to 0)    │
    │  → read TMAG5273 angle (I2C)        │
    │  → compute speed = pulses × 1.31 / 5│
    │  → UART TX2: "WNI,<speed>,<dir>\n"   │
    │  → return to STOP mode              │
    └─────────────────────────────────────┘
```

Rotor pulses (reed switch) increment a counter via EXTI. This works in STOP mode — the EXTI interrupt briefly wakes the CPU, the ISR increments the counter, and the CPU returns to STOP. The ESP32-C6 doesn't need to know about individual pulses; it just triggers every 5 seconds and gets the accumulated count.

## Output format

```
WNI,<speed_mps>,<direction_deg>
```

- `speed_mps`: wind speed in m/s, one decimal place (e.g. `3.2`)
- `direction_deg`: wind direction 0–359 degrees, or `0` if TMAG5273 read failed

Example: `WNI,3.2,247`

Same `WNI` format as factory firmware. No `WNA` (averaged) messages — averaging is done on the ESP32-C6 side from the 5s samples.

## Dependencies

- [WindNerd Core library](https://github.com/windnerd-labs/Windnerd-Core) — only for `Windnerd_TMAG5273.h/.cpp` and `Windnerd_I2C.h/.cpp` (TMAG5273 driver + I2C wrapper). The `WN_Core` class is **not** used. Included as a git submodule at `lib/Windnerd-Core/`.
- [stm32duino](https://github.com/stm32duino) 3.0.0 board package
- Docker (for containerized build/flash)

## Building locally

### Prerequisites

- Docker installed and running
- ST-Link V2 dongle (for flashing)
- Clone with submodules:

```bash
git clone --recursive <repo-url>
# or if already cloned:
git submodule update --init
```

### Compile

```bash
cd local-storage/firmware/windnerd-stm32g031f8
./build.sh
```

This pulls the prebuilt env image from GHCR and compiles the firmware. Output:

```
build/src.ino.bin   ← flashable binary (ST-Link SWD)
build/src.ino.elf    ← debug symbols
```

To use a locally built image instead of the GHCR one:

```bash
docker build -t windnerd-build ./build-env
WINDNERD_BUILD_IMAGE=windnerd-build ./build.sh
```

### Flash

Connect ST-Link V2 (CLK, DIO, RST, GND) to the WindNerd Core board, then:

```bash
./flash.sh                # flash locally built binary
./flash.sh --ci           # download latest CI release and flash that
./flash.sh --ci <sha>     # download release for a specific commit and flash
```

This passes USB through to the container, which runs openocd to flash via ST-Link SWD. No host-side tooling required — openocd is baked into the Docker image. The `--ci` mode downloads prebuilt firmware from GitHub releases using `curl` (no `gh` CLI needed).

Same `WINDNERD_BUILD_IMAGE` override works for `flash.sh`.

### Build without Docker (Arduino IDE)

If you prefer the Arduino IDE:

1. Follow the [WindNerd Core programming guide](https://github.com/windnerd-labs/Windnerd-Core/blob/main/docs/PROGRAM.md) to install STM32CubeProgrammer and stm32duino.
2. Install the WindNerd Core library into your Arduino `libraries/` folder (symlink or copy from `lib/Windnerd-Core/`).
3. Open `src/src.ino` in Arduino IDE.
4. Board: **Generic STM32G031F8Px**
5. Upload method: **STM32CubeProgrammer (SWD)** via ST-Link V2
6. Upload

## Pin assignments

| Pin | Function | Notes |
|---|---|---|
| PA25 | Rotor pulse input | Reed switch, EXTI RISING |
| PA22 | I2C SCL | TMAG5273 |
| PA23 | I2C SDA | TMAG5273 |
| PA0 | ESP32-C6 trigger | EXTI RISING, INPUT_PULLDOWN, expansion header |
| USART2 TX2 | UART output | 9600 baud, expansion header, to ESP32-C6 GPIO4 |

**Expansion header pinout:**

```
VCC
GND
PA6
PA5
PA4
PA3
TX2  (USART2 TX — UART output to ESP32-C6)
PA1
PA0  (ESP32-C6 trigger input)
3V3
TX   (bootloader)
RX   (bootloader)
GND
```

PA0 is confirmed on the header. If PA0 is needed for something else, PA1/PA3/PA4/PA5/PA6 are also available — all are EXTI-capable. Update `TRIGGER_PIN` in `src/src.ino`.

## Configuration

All configuration is in `src/src.ino`:

| Constant | Default | Description |
|---|---|---|
| `SAMPLE_INTERVAL_SEC` | 5 | Seconds between ESP32-C6 triggers. Must match ESP32-C6 LP core loop. |
| `HZ_TO_MS` | 1.31 | Rotor calibration. Standard WindNerd rotor = 1.31 m/s per Hz. |
| `UART_BAUD` | 9600 | Must match ESP32-C6 UART RX config. |

If the ESP32-C6 sample interval changes, update `SAMPLE_INTERVAL_SEC` and reflash. The STM32 uses this constant for speed calculation since `millis()`/`micros()` are unreliable after STOP mode (SysTick stops).

## STOP mode notes

- **SRAM retained** — `pulse_count` and all variables survive STOP mode
- **SysTick stopped** — `millis()` and `micros()` don't work after wakeup until `SystemClock_Config()` restores the clock
- **TIM3 stopped** — we don't use the WindNerd Core's timer-driven sampling
- **EXTI works** — rotor pulse ISR and trigger ISR both fire in STOP mode
- **I2C works after wakeup** — TMAG5273 read happens after clock restore
- **Clock restore** — STOP mode resets SYSCLK to HSI 16 MHz; `SystemClock_Config()` restores 8 MHz (HSI/2)

## Power estimate

| State | Current | Duration | Per-5s avg |
|---|---|---|---|
| STOP mode | ~5 µA | ~4.99 s | 4.99 µA |
| Wake + read + UART | ~3 mA | ~10 ms | 6 µA |
| **Average** | | | **~11 µA (~0.011 mA)** |

Factory firmware low power mode: ~600 µA. This firmware: ~11 µA. **55× reduction.**

Actual current needs to be measured on the physical board. The TMAG5273 sleep current (<1 µA) and I2C bus pull-ups may add a few µA.

## CI/CD

GitHub Actions workflow: [`.github/workflows/build-windnerd-stm32g031f8.yml`](../../../.github/workflows/build-windnerd-stm32g031f8.yml)

**Triggers:** push to `main` or PR touching `local-storage/firmware/windnerd-stm32g031f8/**`, plus manual dispatch.

**What it does:**
1. Builds the Docker env image and pushes it to `ghcr.io/thehonker/weathnerd-windnerd-stm32g031f8-env` (tagged `:latest` and `:sha`). Image push happens only on push to `main`.
2. Compiles the firmware using the image.
3. Uploads `src.ino.bin` and `src.ino.elf` as workflow artifacts (90-day retention).
4. On push to `main`: creates a GitHub release tagged `windnerd-stm32g031f8-<sha>` with the `.bin` and `.elf` as downloadable assets.

On PRs, the image builds but isn't pushed — firmware still compiles and artifacts are available for review. On `main`, the env image, workflow artifacts, and a GitHub release are all published.

**Env image:** `ghcr.io/thehonker/weathnerd-windnerd-stm32g031f8-env:latest` — Ubuntu 24.04 + arduino-cli 1.5.1 + stm32duino 3.0.0 + openocd. Reusable for local builds if you don't want to build the image yourself:

```bash
docker pull ghcr.io/thehonker/weathnerd-windnerd-stm32g031f8-env:latest
docker run --rm -v "$(pwd):/firmware" ghcr.io/thehonker/weathnerd-windnerd-stm32g031f8-env:latest /usr/local/bin/compile.sh
```

**Releases:** Firmware binaries are published as GitHub releases at `https://github.com/thehonker/weathernerd/releases`. Download with `curl` or use `./flash.sh --ci`.
