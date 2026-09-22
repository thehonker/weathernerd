# WeatherNerd

[![WindNerd STM32G031F8 firmware](https://github.com/thehonker/weathernerd/actions/workflows/build-windnerd-stm32g031f8.yml/badge.svg)](https://github.com/thehonker/weathernerd/actions/workflows/build-windnerd-stm32g031f8.yml)
[![ESP32-C6 SuperMini firmware](https://github.com/thehonker/weathernerd/actions/workflows/build-esp32c6-supermini.yml/badge.svg)](https://github.com/thehonker/weathernerd/actions/workflows/build-esp32c6-supermini.yml)

Low-power, long-duration weather stations built around the [WindNerd Core](https://github.com/windnerd-labs/Windnerd-Core) anemometer and ESP32-C6 dataloggers.

## What is this?

Two station variants sharing the same wind sensor and datalogger platform:

### Remote Station (v1)

A battery-powered, 6-month-unattended weather station for remote deployment. No grid power, no WiFi infrastructure, no maintenance visits. Deploy in fall, retrieve in spring.

- **Wind:** WindNerd Core (STM32G031F8) with custom firmware — STOP mode sleep (~5 µA), woken on demand by ESP32-C6 every 5s via GPIO interrupt. Wind speed via rotor pulse counting, direction via TMAG5273 magnetic angle sensor.
- **Datalogger:** ESP32-C6 SuperMini, running **ESP-IDF v6.1** (not Arduino — the LP RISC-V coprocessor requires ESP-IDF's `ulp_embed_binary` build system). LP core handles the 5s wind sampling loop while the main core sleeps (~7 µA deep sleep). Main core wakes every 1 min for temp/RH/pressure, rain ADC, and SD card flush.
- **Sensors:** BME280 (temp/RH/pressure, I2C), DS3231 RTC (±2 ppm, I2C), DIY piezo disdrometer (ADC, rain rate).
- **Storage:** 8 GB industrial microSD via SPI. Two daily CSV files: `YYYY-MM-DD-wind.csv` (5s samples) and `YYYY-MM-DD-env.csv` (1 min samples). ~168 MB for 6 months.
- **Power:** 2× ER34615 Li-SOCl2 D-cells (38 Ah, -55°C rated). Total system draw ~0.10 mA. Battery lasts decades — power is a non-issue.
- **Retrieval:** Flip a switch on the enclosure → ESP32-C6 powers on OLED display + starts WiFi 6 soft-AP. Connect with phone/laptop, browse and download CSV files. Rotary encoder + OLED for on-device UI.
- **Cost:** ~$99-143 total BOM.

### Home Station (future)

A wall-powered variant for permanent installation at home. Same WindNerd Core + ESP32-C6 platform, but always-on WiFi with Prometheus-style metrics exposure:

- `/metrics` endpoint scraped by a Prometheus instance
- Wind speed, direction, temperature, RH, pressure, rain rate as gauge metrics
- DS3231 RTC for timestamp accuracy
- No battery, no SD card — data lives in Prometheus
- OLED + encoder UI always available
- Optional: push to a remote station's data archive for cross-site comparison

Design docs for the home station will be added when we start that phase. The remote station firmware and hardware are the priority — the home station reuses the same components and most of the same firmware.

## Repo layout

```none
weathernerd/
├── README.md              ← you are here
├── LICENSE
├── .github/workflows/
│   └── build-windnerd-stm32g031f8.yml  ← CI/CD for STM32 firmware
└── local-storage/
    ├── notes/
    │   ├── hardware-design.md   ← pin allocation, wiring, BOM, component selection
    │   ├── power-budget.md      ← current draw analysis, battery sizing
    │   └── storage-budget.md    ← data volume, SD card selection, file strategy
    └── firmware/
        ├── windnerd-stm32g031f8/  ← custom WindNerd Core firmware (⚠️ WIP, untested)
        │   ├── README.md           ← firmware docs, build & flash instructions
        │   ├── build.sh            ← build locally (Docker)
        │   ├── flash.sh            ← flash via ST-Link (Docker)
        │   ├── src/src.ino         ← firmware source
        │   ├── lib/Windnerd-Core/  ← git submodule
        │   └── build-env/          ← Dockerfile + compile/flash scripts
        └── esp32c6-supermini/     ← ESP32-C6 datalogger firmware (ESP-IDF, ✅ compiles)
            ├── README.md           ← firmware docs, build & flash instructions
            ├── CMakeLists.txt      ← ESP-IDF project root
            ├── sdkconfig.defaults  ← LP core, FAT/SD, WiFi AP, BT disabled
            ├── partitions.csv      ← nvs + phy + 3 MB factory
            ├── build.sh            ← build locally (Docker)
            ├── flash.sh            ← flash via USB serial (Docker)
            ├── main/
            │   ├── CMakeLists.txt  ← ulp_embed_binary() for LP core
            │   ├── main.c          ← main core firmware
            │   ├── storage.h/.c    ← SDSPI + CSV writing (wind + env)
            │   ├── i2c_bus.h/.c    ← shared I2C bus (GPIO6/7)
            │   ├── ds3231.h/.c     ← DS3231 RTC driver
            │   ├── bme280.h/.c     ← BME280 driver + compensation
            │   ├── wifi.h/.c         ← WiFi AP + HTTP server (files, clock, format, reboot, halt)
            │   ├── oled.h/.c       ← SH1106 OLED driver (128×64, text)
            │   ├── oled_font.h     ← 8×8 ASCII font
            │   ├── input.h/.c      ← EC11 encoder + CON/BAK/PSH buttons
            │   ├── ui.h/.c         ← Interactive menu UI (6 items)
            │   └── lp_core/main.c  ← LP core firmware
            └── build-env/          ← Dockerfile (espressif/idf:release-v6.1)
```

## Design docs

Start with [hardware-design.md](local-storage/notes/hardware-design.md) for the full system overview, pin allocation, and BOM.
[power-budget.md](local-storage/notes/power-budget.md) covers current draw and battery life.
[storage-budget.md](local-storage/notes/storage-budget.md) covers data volumes and SD card selection.

## Firmware

### WindNerd Core (STM32G031F8) — ⚠️ WIP, untested - compiles, sensors implemented

Custom firmware replacing the factory WindNerd Core firmware. Drops power from ~0.6 mA (SLEEP mode) to ~0.04 mA (STOP mode) by sleeping until the ESP32-C6 triggers a read via GPIO interrupt. Uses the TMAG5273 driver directly from the WindNerd Core library — the `WN_Core` class is not used.

- **Status:** Written, compiles clean (62% flash, 25% RAM), not yet tested on hardware
- **Build:** `./build.sh` (Docker, no host tooling needed) → `build/src.ino.bin`
- **Flash:** `./flash.sh` (ST-Link V2, openocd in Docker) or `./flash.sh --ci` (download from GitHub releases)
- **CI/CD:** Auto-builds on push to main, publishes env image to GHCR + GitHub release with firmware binaries
- **Details:** [windnerd-stm32g031f8/README.md](local-storage/firmware/windnerd-stm32g031f8/README.md)

### ESP32-C6 SuperMini — ⚠️ WIP, untested — compiles, all features implemented

ESP32-C6 datalogger firmware built with **ESP-IDF v6.1**. The LP RISC-V coprocessor handles the 5s wind sampling loop (WindNerd trigger + UART RX) while the main core sleeps. Main core wakes every 1 min for sensors + SD card flush. Interactive UI (OLED + encoder + buttons) triggered by CON button press — provides live data, file browser, WiFi portal, clock sync, format SD, reboot, sleep, and halt.

- **Status:** All firmware modules written and compiling clean. Full data flow implemented: LP core samples wind every 5s → main core flushes to SD every 1 min. Interactive UI with 8 menu items. WiFi portal with file browser, clock sync, format SD, reboot, halt. Only hardware testing remains.
- **Binary:** 1.15 MB (37% of 3 MB factory partition — WiFi stack adds ~800 KB)
- **Build:** `./build.sh` (Docker, no host toolchain needed) → `build/esp32c6_weathernerd.bin`
- **Flash:** `./flash.sh` (USB serial, esptool in Docker) or `./flash.sh --ci` (download from GitHub releases)
- **CI/CD:** Auto-builds on push, publishes env image to GHCR + GitHub release with firmware binaries
- **Details:** [esp32c6-supermini/README.md](local-storage/firmware/esp32c6-supermini/README.md)

**Why ESP-IDF, not Arduino?** The ESP32-C6's LP RISC-V coprocessor is the whole reason we chose the C6 over the C3. Arduino-esp32 does not support LP core programming — no `ulp_embed_binary()` build pipeline, no LP core API exposure. ESP-IDF is the official framework for LP core development.

## Key design decisions

- **WindNerd custom firmware** — factory firmware draws 0.6 mA. Custom STOP mode + on-demand sampling drops it to ~0.04 mA. 7× total system power reduction.
- **ESP32-C6 over ESP32-C3** — the C6's LP RISC-V coprocessor handles the 5s sampling loop while the main core sleeps. The C3 has no LP core, so the main core would wake every 5s, roughly doubling power consumption. This required switching from Arduino to ESP-IDF — Arduino-esp32 doesn't support LP core programming.
- **No solar** — at 0.10 mA average, the battery alone lasts decades. Solar adds complexity and failure modes for zero benefit.
- **Two data streams** — wind+rain at 5s (LP core) and temp/RH/pressure at 1 min (main core). Separate daily CSV files. Temp changes slowly; 1 min is plenty.
- **CON button + OLED** — the station sleeps with zero UI. A momentary button (CON) wakes the main core via GPIO interrupt, firmware powers on the OLED via MOSFET (GPIO5), and starts a WiFi AP for data retrieval. Zero power when off. BAK button at top-level menu returns to low-power mode. 60s inactivity timeout as backup. A future "Halt" option (full system stop, power cycle to resume) may be added to both the OLED menu and WiFi portal.
- **SPI over SDIO for SD card** — GPIO18–23 are the ESP32-C6's native SDIO peripheral, but 4-bit SDIO needs 6 pins and would eat GPIO20/21 (encoder) and GPIO22/23 (CON/BAK buttons). SPI mode uses 4 pins and the data rate is trivial (~838 KB/day, 70-byte appends once per minute). The SD card is asleep 55s out of every 60s — SDIO's speed advantage is irrelevant. The saved pins keep the encoder and buttons on safe, non-strapping GPIO.
- **DIY piezo disdrometer** — no moving parts, $2 in parts, logs raw ADC peaks. Calibrate later against a reference gauge. v1 has no op-amp (simplest); v2 adds OPA376 if drizzle sensitivity is needed.

## License

`AGPL-3.0-or-later`. See [LICENSE](LICENSE).
