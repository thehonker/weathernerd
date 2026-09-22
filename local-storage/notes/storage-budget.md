# Remote Wind Monitoring Station — Storage Napkin Math

**Date:** 2026-09-22
**Author:** Coilette (for goos)
**Status:** Updated — ESP32-C6 SuperMini, custom WindNerd firmware, on-demand sampling
**Related:** [power-budget.md](power-budget.md)

---

## Architecture (locked)

- **Wind sensing:** WindNerd Core (STM32G031F8) — **custom firmware**, ESP32 triggers reads on demand via GPIO interrupt
- **Datalogger:** ESP32-C6 SuperMini — triggers WindNerd every 5s, reads BME280 (I2C) + DS3231 RTC (I2C) + piezo rain (ADC), writes to SD card
- **RTC:** DS3231 (TCXO, ±2 ppm, CR1220 backup) on shared I2C with BME280
- **Storage:** MicroSD card (industrial, FAT32, two daily CSV files)
- **Sample rate:** 5 seconds (wind + rain), 1 minute (temp/RH/pressure)
- **Retrieval:** ESP32-C6 WiFi 6 soft-AP → connect phone/laptop → download CSV via WiFi switch

---

## Sensors

| Sensor | Values | Interface | Read by | Frequency |
|---|---|---|---|---|
| WindNerd Core | Wind speed + direction | UART (9600 baud, on-demand) | ESP32-C6 LP core | Every 5s |
| BME280 | Temperature, RH, pressure | I2C (addr 0x76/0x77) | ESP32-C6 main core | Every 1 min |
| DS3231 | RTC timestamp | I2C (addr 0x68) | ESP32-C6 main core | Every 1 min |
| Piezo rain | Rain rate (mm/hr) | ADC | ESP32-C6 LP core | Every 5s |

### WindNerd Core — custom firmware data flow

Unlike factory firmware (autonomous 3s output), the custom firmware is interrupt-driven:

1. ESP32-C6 LP core toggles a GPIO pin every 5s
2. WindNerd wakes from STOP mode, reads TMAG5273 (direction) + accumulated rotor pulse count (speed)
3. Outputs `WNI,<speed>,<direction>` via UART
4. Goes back to STOP mode

The ESP32-C6 LP core reads the UART response and stores it in RTC memory. Main core wakes every 1 min to read BME280 + DS3231 + flush all buffered data to SD.

### BME280 sampling note

BME280 and DS3231 are read every 1 minute by the ESP32-C6 main core (LP core can't easily do I2C). Temp/RH/pressure change slowly — 1-minute resolution is more than adequate. These are stored as a **separate data stream** from the wind/rain data, not stamped onto every 5s sample. Two daily CSV files: one for wind+rain (5s), one for temp/RH/pressure (1 min).

---

## Sample Rates

### Wind + rain: 5 seconds

- 5s interval → 12 samples/minute → 17,280 samples/day
- 6 months = 182 days → **3,149,760 samples total**

### Temp + RH + pressure: 1 minute

- 1 min interval → 1,440 samples/day
- 6 months = 182 days → **262,080 samples total**

---

## Data Formats

### CSV (recommended — SD card is PC-readable)

```
epoch,speed_ms,dir_deg,temp_c,rh_pct,press_hpa,rain_mmh
1774030400,12.3,245,18.5,65,1013.2,0.00
```

~70 bytes/line including newline (ISO epoch timestamp + 6 float fields + commas).

### Binary (if CSV is too large — it won't be)

| Field | Bytes | Encoding |
|---|---|---|
| Timestamp | 4 | uint32 Unix epoch |
| Wind speed | 2 | uint16 ×100 (0.01 m/s res) |
| Wind direction | 2 | uint16 degrees (0–359) |
| Temperature | 2 | int16 ×100 (0.01°C res) |
| RH | 1 | uint8 (0–100%) |
| Pressure | 2 | uint16 ×10 offset from 800 hPa |
| Rain rate | 2 | uint16 ×100 (0.01 mm/hr res) |
| Reserved | 1 | alignment |
| **Total** | **16 bytes** | |

---

## Storage Calculation

### Wind + rain CSV (5s samples)

```
epoch,speed_ms,dir_deg,rain_mmh
1774030400,12.3,245,0.00
```
~45 bytes/line (epoch + 3 fields + commas + newline)

| Format | Per-sample | Samples (6 mo) | Total | With FAT overhead |
|---|---|---|---|---|
| **CSV** | ~45 bytes | 3,149,760 | **142 MB** | **~156 MB** |

### Temp/RH/pressure CSV (1 min samples)

```
epoch,temp_c,rh_pct,press_hpa
1774030400,18.5,65,1013.2
```
~40 bytes/line (epoch + 3 fields + commas + newline)

| Format | Per-sample | Samples (6 mo) | Total | With FAT overhead |
|---|---|---|---|---|
| **CSV** | ~40 bytes | 262,080 | **10.5 MB** | **~12 MB** |

### Combined storage

| Stream | Per-sample | 6-month total |
|---|---|---|
| Wind + rain (5s CSV) | ~45 bytes | ~156 MB |
| Temp/RH/pressure (1 min CSV) | ~40 bytes | ~12 MB |
| **Total** | | **~168 MB** |

Splitting into two streams actually saves storage — the 5s wind/rain file no longer carries redundant temp/RH/pressure columns that only change every 1 min. Total drops from ~242 MB (single file) to ~168 MB (two files).

---

## SD Card Selection

| Factor | Recommendation |
|---|---|
| Capacity | 1 GB is overkill (168 MB total). 8 GB is the sweet spot — cheap, common, reliable. |
| Type | **Industrial SD** (SLC or pSLC) if budget allows — survives cold, power-loss, high write endurance. Consumer SD (TLC/QLC) works but risks corruption on power loss. |
| Speed class | Class 4 is fine — we write 70 bytes every 5 seconds. Speed is irrelevant. |
| Cold rating | Most SD cards are rated -25°C to 85°C. Industrial cards go to -40°C. |
| Wear | 168 MB over 6 months, written in 70-byte appends. SD wear leveling handles this trivially — even a cheap card's write endurance is ~100 TBW. We're writing 0.168 GB total. |
| Power loss risk | Mitigate by buffering in ESP32 RAM and flushing to SD every 1 minute. One write per minute = 262,800 writes in 6 months. Nothing. |

### Recommended cards

| Card | Capacity | Temp range | Endurance | Cost |
|---|---|---|---|---|
| Kingston Industrial SD | 8 GB | -25°C | High (pSLC) | $8-12 |
| Transcend Industrial SD | 8 GB | -40°C | High (pSLC) | $10-15 |
| SanDisk High Endurance | 32 GB | -25°C | High (dashcam-rated) | $8-12 |
| Consumer microSD (any brand) | 8-32 GB | -25°C | OK for this use case | $3-5 |

**Recommendation:** Transcend Industrial 8GB if the station sees real cold. SanDisk High Endurance if -25°C is acceptable. Either way, $10-15.

---

## File Strategy

**Daily CSV files, two streams.** Simple, robust, PC-readable, one-day loss risk maximum. 838 KB/day is nothing.

- Wind + rain: `YYYY-MM-DD-wind.csv` — 45 bytes × 17,280 = ~780 KB/day
- Temp/RH/pressure: `YYYY-MM-DD-env.csv` — 40 bytes × 1,440 = ~58 KB/day
- 364 files over 6 months (2 per day)
- If one file corrupts, you only lose one day of one stream
- Easy to grep/parse per-day
- ESP32-C6 handles file rotation trivially

---

## Write Frequency & Power

Writing to SD every 5 seconds (every sample) is wasteful. Better approach:

| Strategy | SD writes/day | Power | Risk |
|---|---|---|---|
| Write every 5s (every sample) | 17,280 | Higher (SD always active) | Fine, but unnecessary |
| **Buffer in RAM, flush every 1 min** | 1,440 | Low (SD sleeps 55s/min) | Lose 1 min on power loss |
| Buffer in RAM, flush every 5 min | 288 | Lower | Lose 5 min on power loss |

**Recommendation:** Buffer in ESP32-C6 RTC/RAM, flush to SD every 1 minute. The C6 has ample RAM — a 1-minute buffer of 5s wind/rain samples is 12 × 45 bytes = 540 bytes. Trivial. SD card sleeps between flushes, saving power.

This aligns naturally with the architecture: LP core collects 12 wind + rain samples (5s each) in RTC memory, main core wakes every 1 min to read BME280 + DS3231, and flushes both streams to their respective daily CSV files.

---

## Data Flow Summary

```
Every 5s:
  ESP32-C6 LP core → GPIO trigger → WindNerd wakes from STOP
  WindNerd → UART: WNI,<speed>,<dir> → ESP32-C6 LP core reads + stores in RTC mem
  ESP32-C6 LP core → ADC read piezo rain → stores in RTC mem

Every 1 min:
  ESP32-C6 main core wakes
  → Read DS3231 (I2C): accurate epoch timestamp
  → Read BME280 (I2C): temp, RH, pressure
  → Flush 12 buffered wind+rain rows to YYYY-MM-DD-wind.csv on SD
  → Append 1 row to YYYY-MM-DD-env.csv on SD
  → Go back to deep sleep

On demand (spring retrieval):
  Phone connects to ESP32-C6 WiFi 6 soft-AP
  → Browse/download daily CSV files (wind + env)
  → Download CSV files via WiFi
```

---

## Bottom Line

| Metric | Value |
|---|---|
| Wind + rain sample rate | 5 seconds |
| Temp/RH/pressure sample rate | 1 minute |
| Format | CSV (two daily files: wind + env) |
| Wind CSV per-sample | ~45 bytes |
| Env CSV per-sample | ~40 bytes |
| Daily storage (wind) | ~780 KB/day |
| Daily storage (env) | ~58 KB/day |
| 6-month storage (total) | ~168 MB |
| SD card | 8 GB industrial microSD ($10-15) |
| Write frequency | Flush to SD every 1 minute |
| Data retrieval | ESP32-C6 WiFi 6 soft-AP → download CSV files via phone |
| Power loss risk | Lose at most 1 minute of data |
| RTC | DS3231 (±2 ppm, ±5 min over 6 months, CR1220 backup) |

**Storage is a non-issue.** A $10 SD card holds 36× what you need. The real work is firmware.
