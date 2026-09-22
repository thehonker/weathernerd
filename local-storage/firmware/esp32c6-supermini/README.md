# esp32c6-supermini — WeatherNerd ESP32-C6 Firmware

[![ESP32-C6 SuperMini firmware](https://github.com/thehonker/weathernerd/actions/workflows/build-esp32c6-supermini.yml/badge.svg)](https://github.com/thehonker/weathernerd/actions/workflows/build-esp32c6-supermini.yml)

Firmware for the ESP32-C6 SuperMini datalogger — the main controller of the WeatherNerd remote station.

**⚠️ WIP — not yet started.** Placeholder sketch only.

## Planned functionality

- **LP RISC-V core:** 5s wind + rain sampling loop, triggers STM32G031F8 via GPIO interrupt, reads UART response
- **Main core:** 1 min temp/RH/pressure (BME280, I2C), SD card logging (SPI), WiFi retrieval
- **DS3231 RTC** for timestamp accuracy (I2C)
- **OLED + rotary encoder** UI (switch-activated, zero power when off)
- **WiFi 6 soft-AP** for on-demand data download from phone/laptop

## Hardware

- **Board:** ESP32-C6 SuperMini (ESP32C6 Dev Module)
- **Flash:** 4 MB QIO
- **Connections:** STM32G031F8 (GPIO trigger + UART), BME280 (I2C), DS3231 (I2C), microSD (SPI), OLED (I2C), rotary encoder (GPIO)

## Dependencies

- [esp32duino](https://github.com/espressif/arduino-esp32) 3.3.12 board package
- Docker (for containerized build/flash)

## Building locally

### Prerequisites

- Docker installed and running
- USB-C cable (for flashing)
- Clone with submodules (if any are added later):

```bash
git clone --recursive <repo-url>
```

### Compile

```bash
cd local-storage/firmware/esp32c6-supermini
./build.sh
```

This pulls the prebuilt env image from GHCR and compiles the firmware. Output:

```
build/src.ino.bin            ← flashable binary (USB serial)
build/src.ino.elf             ← debug symbols
build/src.ino.partitions.bin  ← partition table
build/src.ino.bootloader.bin  ← bootloader
```

To use a locally built image instead of the GHCR one:

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

This passes USB through to the container, which runs esptool to flash via USB serial. No host-side tooling required — esptool is baked into the Docker image.

Override the serial port if auto-detection fails:

```bash
ESP32_PORT=/dev/ttyACM1 ./flash.sh
```

Same `ESP32C6_BUILD_IMAGE` override works for `flash.sh`.

## CI/CD

GitHub Actions workflow: [`.github/workflows/build-esp32c6-supermini.yml`](../../../.github/workflows/build-esp32c6-supermini.yml)

**Triggers:** push to `main` or PR touching `local-storage/firmware/esp32c6-supermini/**`, plus manual dispatch.

**What it does:**
1. Builds the Docker env image and pushes it to `ghcr.io/thehonker/weathernerd-esp32c6-supermini-env` (tagged `:latest` and `:sha`). Image push happens only on push to `main`.
2. Compiles the firmware using the image.
3. Uploads `src.ino.bin`, `src.ino.elf`, `src.ino.partitions.bin`, and `src.ino.bootloader.bin` as workflow artifacts (90-day retention).
4. On push to `main`: creates a GitHub release tagged `esp32c6-supermini-<sha>` with the binaries as downloadable assets.

On PRs, the image builds but isn't pushed — firmware still compiles and artifacts are available for review. On `main`, the env image, workflow artifacts, and a GitHub release are all published.

**Env image:** `ghcr.io/thehonker/weathernerd-esp32c6-supermini-env:latest` — Ubuntu 24.04 + arduino-cli 1.5.1 + esp32duino 3.3.12 + esptool. Reusable for local builds if you don't want to build the image yourself:

```bash
docker pull ghcr.io/thehonker/weathernerd-esp32c6-supermini-env:latest
docker run --rm -v "$(pwd):/firmware" ghcr.io/thehonker/weathernerd-esp32c6-supermini-env:latest /usr/local/bin/compile.sh
```

**Releases:** Firmware binaries are published as GitHub releases at `https://github.com/thehonker/weathernerd/releases`. Download with `curl` or use `./flash.sh --ci`.
