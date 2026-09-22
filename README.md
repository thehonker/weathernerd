# WeatherNerd

Low-power, long-duration weather stations built around the [WindNerd Core](https://windnerd.net) anemometer and ESP32-C6 dataloggers.

## What is this?

Two station variants sharing the same wind sensor and datalogger platform:

### Remote Station (v1)

A battery-powered, 6-month-unattended weather station for remote deployment. No grid power, no WiFi infrastructure, no maintenance visits. Deploy in fall, retrieve in spring.

- **Wind:** WindNerd Core (STM32G031F8) with custom firmware — STOP mode sleep (~5 µA), woken on demand by ESP32-C6 every 5s via GPIO interrupt. Wind speed via rotor pulse counting, direction via TMAG5273 magnetic angle sensor.
- **Datalogger:** ESP32-C6 SuperMini. LP RISC-V coprocessor handles the 5s wind + rain sampling loop while the main core sleeps (~7 µA deep sleep). Main core wakes every 1 min for temp/RH/pressure and SD card flush.
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

```
weathernerd/
├── README.md              ← you are here
├── LICENSE
└── local-storage/
    └── notes/
        ├── hardware-design.md   ← pin allocation, wiring, BOM, component selection
        ├── power-budget.md      ← current draw analysis, battery sizing
        └── storage-budget.md    ← data volume, SD card selection, file strategy
```

Firmware directories will be added as development progresses:
- `windnerd-firmware/` — custom STM32G031F8 firmware (STOP mode + GPIO trigger)
- `esp32-c6-firmware/` — ESP32-C6 firmware (LP core sampling + main core logging + WiFi retrieval + OLED UI)

## Design docs

Start with [hardware-design.md](local-storage/notes/hardware-design.md) for the full system overview, pin allocation, and BOM. [power-budget.md](local-storage/notes/power-budget.md) covers current draw and battery life. [storage-budget.md](local-storage/notes/storage-budget.md) covers data volumes and SD card selection.

## Key design decisions

- **WindNerd custom firmware** — factory firmware draws 0.6 mA. Custom STOP mode + on-demand sampling drops it to ~0.04 mA. 7× total system power reduction.
- **ESP32-C6 over ESP32-C3** — the C6's LP RISC-V coprocessor handles the 5s sampling loop while the main core sleeps. The C3 has no LP core, so the main core would wake every 5s, roughly doubling power consumption.
- **No solar** — at 0.10 mA average, the battery alone lasts decades. Solar adds complexity and failure modes for zero benefit.
- **Two data streams** — wind+rain at 5s (LP core) and temp/RH/pressure at 1 min (main core). Separate daily CSV files. Temp changes slowly; 1 min is plenty.
- **WiFi switch + OLED** — the station sleeps with zero UI. A physical switch wakes the main core, powers on the OLED via MOSFET, and starts a WiFi AP for data retrieval. Zero power when off.
- **DIY piezo disdrometer** — no moving parts, $2 in parts, logs raw ADC peaks. Calibrate later against a reference gauge. v1 has no op-amp (simplest); v2 adds OPA376 if drizzle sensitivity is needed.

## License

See [LICENSE](LICENSE).
