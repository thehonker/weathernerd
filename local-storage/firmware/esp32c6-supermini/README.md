# esp32c6-supermini — WeatherNerd ESP32-C6 Firmware

[![ESP32-C6 SuperMini firmware](https://github.com/thehonker/weathernerd/actions/workflows/build-esp32c6-supermini.yml/badge.svg)](https://github.com/thehonker/weathernerd/actions/workflows/build-esp32c6-supermini.yml)

Firmware for the ESP32-C6 SuperMini datalogger — the main controller of the WeatherNerd remote station.

Built with **ESP-IDF v6.1** (not Arduino). The ESP32-C6's LP RISC-V coprocessor requires ESP-IDF's `ulp_embed_binary` build system — Arduino-esp32 does not support LP core programming.

## Architecture

```
┌─────────────────────────────────────────────────┐
│  ESP32-C6 SuperMini                              │
│                                                  │
│  ┌──────────┐   shared RTC    ┌──────────────┐   │
│  │ LP Core  │───memory───────▶│  Main Core   │   │
│  │ (RISC-V) │                 │  (RISC-V)    │   │
│  │          │                 │              │   │
│  │ 5s loop: │                 │ 1 min wake:  │   │
│  │ trigger  │                 │ read BME280  │   │
│  │ WindNerd │                 │ read DS3231  │   │
│  │ read UART │                │ read rain ADC │   │
│  │ store    │                 │ flush to SD   │   │
│  │ sample   │                 │ deep sleep    │   │
│  │ halt     │                 │              │   │
│  └──────────┘                 └──────────────┘   │
│       │                              │           │
│       │ GPIO1 (trigger)              │ GPIO22    │
│       │ GPIO4 (LP_UART RX)           │ (WiFi SW) │
│       ▼                              ▼           │
│  ┌──────────┐                 ┌──────────────┐   │
│  │ WindNerd │                 │ WiFi AP +    │   │
│  │ Core     │                 │ OLED UI      │   │
│  │ (STM32)  │                 │ (on demand)  │   │
│  └──────────┘                 └──────────────┘   │
└─────────────────────────────────────────────────┘
```

### LP Core (ULP coprocessor)

Wakes every 5 seconds via LP timer while main core sleeps:
1. Pulse GPIO1 high for 10ms → wakes WindNerd Core (STM32G031F8) from STOP mode
2. Read UART response on LP_UART (GPIO4, 9600 baud) — `WNI,<speed>,<dir>\n`
3. Parse and store wind sample in shared RTC memory buffer
4. After 12 samples (1 minute), wake the main core
5. Halt — LP timer wakes again in 5s

### Main Core

**Cold boot:** Initialize LP UART (9600 baud, RX=GPIO4), load LP core binary, configure GPIO22 wakeup, enter deep sleep.

**LP core wakeup (every 1 min):** Read BME280 (temp/RH/pressure, I2C), read DS3231 (timestamp, I2C), read rain ADC (HP ADC, GPIO0), flush 12 wind samples + env data to SD card (SPI), enter deep sleep.

**GPIO22 wakeup (CON button):** Power on OLED via MOSFET (GPIO5), start WiFi 6 soft-AP, serve CSV files via HTTP, OLED + rotary encoder UI. Returns to low-power mode on 60s timeout, menu action, or BAK button at top level.

### Known limitation: rain ADC resolution

The ESP32-C6 LP core does **not** have an LP ADC peripheral (`SOC_LP_ADC_SUPPORTED` is not defined). Rain ADC sampling moved to the main core's 1-minute wake cycle using the HP ADC. Rain resolution is 1 min instead of 5s — acceptable for v1 since rain rate doesn't change much in 60 seconds.

## Project structure

```
esp32c6-supermini/
├── CMakeLists.txt           ← ESP-IDF project root
├── sdkconfig.defaults       ← LP core, FAT/SD, WiFi AP enabled, BT disabled, 4 MB flash
├── partitions.csv           ← nvs (16K) + phy (4K) + factory (3 MB)
├── build.sh                 ← Docker build helper
├── flash.sh                 ← Docker flash helper (--ci mode)
├── .gitignore
├── main/
│   ├── CMakeLists.txt       ← idf_component_register + ulp_embed_binary()
│   ├── main.c               ← main core firmware (deep sleep, wakeup dispatch)
│   ├── storage.h / .c       ← SDSPI init, FAT mount, CSV file writing (wind + env)
│   ├── i2c_bus.h / .c       ← shared I2C bus (GPIO6/7, 100 kHz)
│   ├── ds3231.h / .c        ← DS3231 RTC driver (probe, get/set time, temp)
│   ├── bme280.h / .c        ← BME280 driver (probe, init, forced read, compensation)
│   ├── wifi.h / .c           ← WiFi soft-AP + HTTP server (files, clock sync, format, reboot, halt)
│   ├── oled.h / .c          ← SH1106 OLED driver (I2C, 128×64, text rendering)
│   ├── oled_font.h          ← 8×8 ASCII font (96 chars, 768 bytes)
│   ├── input.h / .c         ← EC11 encoder + CON/BAK/PSH buttons (GPIO ISR + debounce)
│   ├── ui.h / .c            ← Interactive menu UI (live data, files, WiFi, clock, sleep, halt)
│   └── lp_core/
│       └── main.c           ← LP core firmware (5s wind sampling loop)
└── build-env/
    ├── Dockerfile           ← espressif/idf:release-v6.1 base
    ├── compile.sh           ← container: idf.py set-target + build
    └── flash.sh             ← container: idf.py flash
```

## Hardware

- **Board:** ESP32-C6 SuperMini (ESP32-C6FH4)
- **Flash:** 4 MB QIO
- **LP core peripherals used:** LP IO (GPIO1 trigger), LP UART (GPIO4 RX, 9600 baud)
- **Main core peripherals:** I2C (BME280 + DS3231 + OLED on GPIO6/GPIO7), SPI (microSD on GPIO2/3/18/19), ADC (GPIO0 rain), GPIO5 (OLED MOSFET gate), GPIO14/20/21 (encoder TRA/TRB/PSH), GPIO22 (CON button), GPIO23 (BAK button)

See [hardware-design.md](../../notes/hardware-design.md) for full pin allocation and wiring.

## Dependencies

- **ESP-IDF v6.1** (bundled in Docker image — no host install needed)
- Docker (for containerized build/flash)
- USB-C cable (for flashing)

## Building locally

### Compile

```bash
cd local-storage/firmware/esp32c6-supermini
./build.sh
```

Pulls the prebuilt env image from GHCR and compiles the firmware. Output:

```
build/esp32c6_weathernerd.bin            ← flashable application binary
build/esp32c6_weathernerd.elf            ← debug symbols
build/bootloader/bootloader.bin          ← ESP32-C6 bootloader
build/partition_table/partition-table.bin ← partition table
```

To use a locally built image instead of GHCR:

```bash
docker build -t esp32c6-build ./build-env
ESP32C6_BUILD_IMAGE=esp32c6-build ./build.sh
```

### Flash

Connect ESP32-C6 SuperMini via USB-C, then:

```bash
./flash.sh                # flash locally built binary
./flash.sh --ci           # download latest CI release and flash that
./flash.sh --ci <sha>     # download release for a specific commit and flash
```

Override the serial port if auto-detection fails:

```bash
ESP32_PORT=/dev/ttyACM1 ./flash.sh
```

## CI/CD

GitHub Actions workflow: [`.github/workflows/build-esp32c6-supermini.yml`](../../../.github/workflows/build-esp32c6-supermini.yml)

**Triggers:** push to `main` or PR touching `local-storage/firmware/esp32c6-supermini/**`, plus manual dispatch.

**What it does:**
1. Builds the Docker env image and pushes it to `ghcr.io/thehonker/weathnerd-esp32c6-supermini-env` (tagged `:latest` and `:sha`). Image push happens only on push to `main`.
2. Compiles the firmware using the image (`idf.py set-target esp32c6 && idf.py build`).
3. Uploads `esp32c6_weathernerd.bin`, `esp32c6_weathernerd.elf`, `bootloader.bin`, and `partition-table.bin` as workflow artifacts (90-day retention).
4. On push to `main`: creates a GitHub release tagged `esp32c6-supermini-<sha>` with the binaries as downloadable assets.

**Env image:** `ghcr.io/thehonker/weathnerd-esp32c6-supermini-env:latest` — based on `espressif/idf:release-v6.1` (ESP-IDF v6.1 + RISC-V toolchain + esptool). Reusable for local builds:

```bash
docker pull ghcr.io/thehonker/weathnerd-esp32c6-supermini-env:latest
docker run --rm -v "$(pwd):/project" -w /project -u $(id -u) -e HOME=/tmp \
  ghcr.io/thehonker/weathnerd-esp32c6-supermini-env:latest /usr/local/bin/compile.sh
```

## Implementation status

| Component | Status |
|-----------|--------|
| ESP-IDF project + LP core build | ✅ Compiles clean |
| LP core: WindNerd trigger (GPIO1) | ✅ Written |
| LP core: UART RX + parse | ✅ Written |
| LP core: sample buffer + main wakeup | ✅ Written |
| Main core: deep sleep + wakeup dispatch | ✅ Written |
| Main core: LP UART init | ✅ Written |
| Main core: I2C bus (shared) | ✅ Written |
| Main core: DS3231 RTC (I2C) | ✅ Written |
| Main core: BME280 sensor (I2C) | ✅ Written |
| Main core: rain ADC reads (HP ADC) | ✅ Written |
| Main core: SD card logging (SPI) | ✅ Written |
| Main core: env CSV writing (temp/RH/press) | ✅ Written |
| WiFi retrieval mode (soft-AP + HTTP) | ✅ Written |
| Clock sync from browser | ✅ Written |
| File browser + download | ✅ Written |
| OLED driver (SH1106, I2C) | ✅ Written |
| Encoder + button input (GPIO ISR) | ✅ Written |
| Interactive menu UI (6 items) | ✅ Written |
| OLED power gating (MOSFET on GPIO5) | ✅ Written |
| Halt option (full system stop) | ✅ Written |
| Wind samples → SD card (ulp_ variable access) | 🔲 TODO |

## Why ESP-IDF, not Arduino?

The ESP32-C6's LP RISC-V coprocessor is the whole reason we chose the C6 over the C3. Arduino-esp32 (3.3.12) does not support LP core programming — there's no build pipeline for `ulp_embed_binary()`, no `ulp_lp_core_load_binary()` / `ulp_lp_core_run()` API exposure, and no Kconfig support for `CONFIG_ULP_COPROC_TYPE_LP_CORE`. The feature has been requested ([arduino-esp32#9721](https://github.com/espressif/arduino-esp32/issues/9721)) since May 2024 with no progress.

ESP-IDF is the official framework for LP core development. The build system (`ulp_embed_binary` / `ulp_add_project`), Kconfig options, and LP core APIs (`ulp_lp_core.h`, `ulp_lp_core_uart.h`, `ulp_lp_core_gpio.h`) are all first-class. The Docker build pattern still works — just `espressif/idf:release-v6.1` instead of arduino-cli.
