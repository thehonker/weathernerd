# Remote Wind Monitoring Station — Power Budget

**Date:** 2026-09-22
**Author:** Coilette (for goos)
**Status:** Updated — SIM7080G LTE-M uplink folded into v1
**Related:** [storage-budget.md](storage-budget.md)

---

## Architecture (locked)

- **Wind sensing:** WindNerd Core (STM32G031F8) — **custom firmware**, sleeps until ESP32 requests data
- **Datalogger + sensors:** ESP32-C6 SuperMini — triggers WindNerd on demand, reads BME280 (I2C) + DS3231 RTC (I2C) + piezo rain (ADC), writes to SD card
- **RTC:** DS3231 (TCXO, ±2 ppm, CR1220 backup) on shared I2C with BME280
- **Storage:** MicroSD card (industrial, FAT32, daily CSV files — two streams: wind/rain at 5s, temp/RH/pressure at 1 min)
- **Sample rate:** 5 seconds (wind + rain), 1 minute (temp/RH/pressure)
- **Data retrieval:** SIM7080G LTE-M MQTT uplink (30-min intervals) + ESP32 WiFi soft-AP for full CSV download on retrieval
- **Display + UI:** SH1106 1.3" OLED + EC11 rotary encoder + CON/BAK buttons on shared I2C + GPIO. Power-gated via MOSFET (GPIO5) — zero current in sleep. Only active when CON button wakes the main core.
- **No chip swaps.** STM32G031F8 stays as-is with custom firmware. ESP32-C6 handles everything beyond wind.

### Board: ESP32-C6 SuperMini

Selected from available boards (ESP32-C3 SuperMini, ESP32-C6 SuperMini, Xiao ESP32-C3).

| Factor | ESP32-C6 SuperMini | ESP32-C3 SuperMini | Xiao ESP32-C3 |
|---|---|---|---|
| Low-power coprocessor | ✅ LP RISC-V core | ❌ None | ❌ None |
| Deep sleep current | ~5-7 µA | ~5 µA | ~5 µA |
| WiFi | WiFi 6 (better range, lower power) | WiFi 4 | WiFi 4 |
| BLE | BLE 5.3 | BLE 5.0 | BLE 5.0 |
| Flash | 4 MB | 4 MB | 4 MB |
| GPIO available | 22 (silkscreen 0–9, 12–15, 16/TX, 17/RX, 18–23) | ~11 | More (Xiao form factor) |
| Onboard LiPo charging | ❌ | ❌ | ✅ (adds quiescent) |

**The LP core is the deciding factor.** Our entire power budget relies on a low-power coprocessor handling the 5s sampling loop while the main core sleeps. Without it (C3), the main core must wake every 5s, roughly doubling ESP32 power consumption.

| | ESP32-C6 (LP core) | ESP32-C3 (no LP core) |
|---|---|---|
| Main core wake frequency | Every 1 min | Every 5s |
| ESP32 avg current | ~0.155 mA | ~0.4–0.5 mA |
| Total system avg | ~0.24 mA | ~0.5–0.6 mA |

WiFi 6 also gives better range for the spring data-retrieval workflow — press CON button, connect, download CSVs.

### GPIO allocation (18 pins used, 22 available, 3 free)

| Function | Pins | Silkscreen |
|---|---|---|
| ADC (piezo rain) | 1 | 0 |
| WindNerd trigger (output) | 1 | 1 |
| UART RX from WindNerd | 1 | 4 |
| I2C (BME280 + DS3231 + OLED: SDA + SCL) | 2 | 6, 7 |
| SPI (SD card: MOSI + MISO + SCK + CS) | 4 | 2, 3, 18, 19 |
| MCP23008 I2C expander INT | 1 | 14 |
| OLED VCC MOSFET gate | 1 | 5 |
| Rain peak-hold reset (2N7002 gate) | 1 | 8 |
| SIM7080G UART (TX + RX) | 2 | 16, 17 |
| SIM7080G DTR (PSM control) | 1 | 15 |
| SIM7080G PWR (power key) | 1 | 9 |
| **Total** | **18** | |

3 free (IO20, IO22, IO23). Encoder + buttons moved to MCP23008 I2C expander (address 0x20) on shared I2C bus. MCP23008 INT → GPIO14 wakes ESP32 on any pin change. GPIO10/GPIO11 don't exist on SuperMini. GPIO21 does not exist as a pad on the SuperMini board.

---

## WindNerd Core — Custom Firmware

### What it is
- **MCU:** STM32G031F8Px (Cortex-M0+, 64MHz — custom firmware underclocks to 8MHz)
- **Wind speed:** Rotor pulse counting via interrupts
- **Wind direction:** TMAG5273 magnetic angle sensor via I2C
- **Serial output:** USART2 @ 9600 baud (TX2, yellow wire)
- **Programming:** ST-Link SWD or serial bootloader
- **Framework:** Arduino (stm32duino), WindNerd Core Arduino library
- **License:** BSD 3-Clause

### Firmware rewrite: on-demand sampling

Instead of the factory firmware autonomously outputting every 3s, the custom firmware:

1. **Sleeps in STOP mode** (~1-5 µA) between samples — rotor pulse counting still runs via interrupt
2. **ESP32 triggers a read** every 5s via GPIO interrupt (or UART break / RTS line)
3. WindNerd wakes, reads TMAG5273 for direction, counts accumulated rotor pulses for speed
4. Outputs a single `WNI,<speed>,<direction>` line via UART
5. Goes back to STOP mode

### Why this is better than factory firmware

| Factor | Factory firmware | Custom firmware (on-demand) |
|---|---|---|
| Sleep mode | SLEEP (CPU stops, peripherals clocked) | STOP (most clocks off, ~1-5 µA) |
| Vane sampling | Every 500ms (low power mode) | Only when triggered (every 5s) |
| UART output | Continuous (every 3s, unsolicited) | Only on request |
| LEDs | Off in low power mode | Off always (we control firmware) |
| Est. avg current | 0.6 mA | **~0.05–0.1 mA** (see breakdown below) |

The factory firmware's 0.6 mA is dominated by:
- TMAG5273 polling every 500ms (~0.3 mA of the total)
- SLEEP mode quiescent (~0.2 mA)
- UART always active (~0.05 mA)

Custom firmware eliminates all three: vane only read on demand, STOP mode instead of SLEEP, UART off between samples.

### Rotor pulse counting during STOP mode

The STM32G0 can count external interrupts during STOP mode. The anemometer rotor generates pulses (reed switch / hall effect) — these increment a counter via GPIO interrupt. When the ESP32 triggers a read, the WindNerd reports the accumulated pulse count since last read → speed = pulses / time interval. This means wind speed is effectively measured continuously even while the MCU sleeps.

---

## Power Budget — WindNerd Core (custom firmware) + ESP32

### WindNerd Core current draw (custom firmware, per 5s cycle)

| State | Current | Duration | Per-5s-cycle avg |
|---|---|---|---|
| STOP mode (MCU off, rotor interrupt counting) | ~5 µA | 4,940 ms | 0.0049 mAs |
| Active (wake, read TMAG5273, compute, UART output) | ~3 mA | 60 ms | 0.036 mAs |
| **WindNerd avg** | | | **~0.041 mA** |

### ESP32-C6 current draw (per 5s cycle)

The C6's LP RISC-V core handles the 5s sampling loop. Main core sleeps in deep sleep (~7 µA) and only wakes every 1 min.

| State | Current | Duration | Per-cycle avg |
|---|---|---|---|
| Deep sleep + LP core (triggers WindNerd every 5s, reads UART) | ~7 µA | continuous | 0.007 mA |
| LP core active (GPIO toggle + UART read + ADC) | ~3 mA | 10 ms / 5s | 0.006 mA |
| Main core wake (every 1 min: read BME280 + flush SD) | 20 mA | 15 ms | 0.005 mA |
| **ESP32-C6 avg** | | | **~0.018 mA** |

**Deep sleep caveat:** Per [measurements on this exact board](https://dmelo.eu/blog/esp32c6_deepsleep/), deep sleep current is in the **tens of microamps** — higher than the ESP32-C6 datasheet's ~7 µA figure. The onboard LTH7R charger IC and passive components add overhead. If actual sleep current is 30 µA instead of 7 µA, ESP32 avg rises to ~0.041 mA and total system to ~0.12 mA. Still well within the 38 Ah battery budget. **Measure actual current on the specific board** and update accordingly.

LP RISC-V coprocessor can:
- Toggle a GPIO to wake the WindNerd every 5s
- Read the UART response (small packet, ~20 bytes)
- Read piezo rain ADC
- Store wind + rain data in RTC memory
- Wake main core once per minute for BME280 + DS3231 read + SD flush

Note: the C6's deep sleep current (~7 µA) is significantly lower than older ESP32 variants (~150 µA) because the LP core replaces the ULP and the RTC/peripheral retention is more efficient. This is a major win for the power budget.

### Full system current draw

| Component | v1 (no op-amp) | v2 (with op-amp) | Notes |
|---|---|---|---|
| WindNerd Core (custom firmware, STOP mode, on-demand) | ~0.04 mA | ~0.04 mA | STOP + rotor interrupts + 5s wake |
| ESP32-C6 SuperMini (deep sleep + LP core, main core wake 1/min) | ~0.018 mA | ~0.018 mA | LP core handles 5s WindNerd trigger + UART read + ADC. See deep sleep caveat above. |
| BME280 (I2C, read every 1 min by main core) | ~0.005 mA | ~0.005 mA | 3.6 µA sleep + active avg |
| DS3231 RTC (I2C, read every 1 min by main core) | ~0.001 mA | ~0.001 mA | 0.8 µA battery-backup mode |
| MCP23008 I2C expander (standby) | ~0.001 mA | ~0.001 mA | ~1 µA standby, only active during interactive mode |
| Piezo rain v1 (bias divider only, sample every 5s via LP core) | ~0.0002 mA | — | 2× 10MΩ divider (~0.16 µA) |
| Piezo rain v2 (OPA376 op-amp, sample every 5s via LP core) | — | ~0.0011 mA | 0.9 µA op-amp + 0.16 µA divider |
| SD card (write every 1 min, sleep otherwise) | ~0.01 mA | ~0.01 mA | Flush buffered 12 samples |
| Inter-board GPIO + UART pull-ups | ~0.01 mA | ~0.01 mA | Trigger line + UART idle |
| Regulator/quiescent (LDO) | ~0.001 mA | ~0.001 mA | HT7333 quiescent |
| SIM7080G modem (PSM sleep, 30-min uploads) | ~0.40 mA | ~0.40 mA | 3 µA PSM + board overhead + 30s TX every 30 min. See [lte-uplink.md](lte-uplink.md). |
| SIM7080G GNSS (daily GPS RTC sync, ~45s fix) | ~0.052 mA | ~0.052 mA | 100 mA × 45s / 86400s averaged. Corrects DS3231 drift daily. |
| **Total estimated** | **~0.55 mA** | **~0.55 mA** | |

OLED, encoder, and buttons draw zero current in sleep (OLED power-gated via MOSFET on GPIO5, encoder/buttons are on MCP23008 I2C expander which sleeps at ~1 µA, INT line on GPIO14 only wakes main core on pin change).

### Comparison: factory vs custom WindNerd firmware

| Scenario | WindNerd current | Total system | 6-month Ah |
|---|---|---|---|
| Factory firmware (low power, 0.6 mA) | 0.6 mA | 0.81 mA | 3.55 Ah |
| **Custom firmware (STOP, on-demand)** | **~0.04 mA** | **~0.10 mA** | **0.44 Ah** |

Custom firmware + C6 LP core cuts total system power by **~7×** (without modem). The WindNerd goes from being the dominant power consumer (74% of total without modem) to the largest at 36%. With the SIM7080G modem added, the modem dominates the power budget at ~73% of total, but absolute draw is still tiny — 0.55 mA avg, 16× battery headroom.

### Realistic estimates

| Scenario | Est. avg current | 6-month Ah |
|---|---|---|
| v1 without modem (sensors only) | 0.10 mA | **0.44 Ah** |
| **v1 with SIM7080G, 30-min uploads + daily GPS sync** | **0.55 mA** | **2.4 Ah** |
| v1 with SIM7080G, 1-hour uploads + daily GPS sync | 0.38 mA | **1.5 Ah** |
| v1 with SIM7080G, 2-hour uploads + daily GPS sync | 0.29 mA | **1.2 Ah** |
| Pessimistic (ESP32-C6 at 30 µA + modem board at 1.2 mA + GPS) | 1.70 mA | **7.5 Ah** |

---

## Battery Sizing

### No solar (6 months, pure battery)

| Battery | Capacity | Cold rating | 6-month draw (0.55 mA) | Verdict |
|---|---|---|---|---|
| 2× ER34615 Li-SOCl2 D-cell (parallel) | ~38 Ah | -55°C ✅ | 2.4 Ah | ✅ **16× headroom** |
| 1× ER34615 Li-SOCl2 D-cell | ~19 Ah | -55°C ✅ | 2.4 Ah | ✅ **7.9× headroom** |
| Li-SOCl2 AA-cell | ~2.4 Ah | -55°C ✅ | 2.4 Ah | ⚠️ 175 days — too tight for cold derating. |

With SIM7080G at 30-min uploads + daily GPS sync, the 2× D-cell battery (38 Ah) has 16× headroom. A single D-cell (19 Ah) still has 7.9×. The AA-cell is too tight once cold derating is considered.

**Recommendation:** 2× ER34615 Li-SOCl2 D-cell in parallel (38 Ah total, -55°C rated). ~$20-30. The oversizing is now justified by the LTE modem power draw + Cat-M1 TX spike delivery (LC filter + 300µF ceramic cap bank handles spikes, battery handles average).

### With small solar (optional)

At 0.55 mA, a tiny 0.5W panel is still overkill:
- 0.5W panel in poor winter (1 hr effective sun → 0.5 Wh/day = ~42 mAh/day at 3.3V/12V)
- Station needs ~13 mAh/day
- Panel provides 3.2× headroom

**Skip solar for v1.** Battery alone has 16× headroom. Solar adds complexity (charge controller, panel mount) for no benefit.

---

## Sample Rate: 5 seconds

The ESP32-C6 LP core triggers the WindNerd every 5s via GPIO. The WindNerd wakes from STOP, reads direction, reports accumulated rotor pulse count + direction via UART, goes back to STOP.

Two data streams:
- **Wind + rain:** 5s, handled by LP core (WindNerd trigger + UART read + piezo ADC), stored in RTC memory
- **Temp + RH + pressure:** 1 min, handled by main core (BME280 I2C read + DS3231 timestamp), stored directly

BME280 and DS3231 are on the same I2C bus (different addresses, no conflict). The LP core can't easily do I2C, so BME280/DS3231 are read by the main core every 1 minute. Temp/RH/pressure change slowly — 1-minute resolution is more than adequate. These are stored as a separate daily CSV from the wind/rain data.

Storage: ~156 MB wind/rain CSV + ~12 MB env CSV for 6 months. See [storage-budget.md](storage-budget.md).

---

## Triggering Mechanism Options

How the ESP32-C6 tells the WindNerd "give me a reading":

| Method | Pins | Latency | Notes |
|---|---|---|---|
| **GPIO interrupt** | 1 (ESP32 GPIO → WindNerd GPIO) | Instant | Simplest. WindNerd configured with external interrupt on this pin. |
| UART break | 0 (uses existing UART) | ~1ms | Send a UART break condition. WindNerd detects it as an interrupt. No extra pin. |
| RTS/CTS hardware flow | 1-2 | Instant | Classic but needs extra pins. |
| UART poll command | 0 (uses existing UART) | ~10ms | ESP32 sends `?` or `READ\n`, WindNerd responds. Simple but WindNerd must monitor UART (prevents deep STOP). |

**Recommendation:** GPIO interrupt. One extra wire, instant wake from STOP mode, no UART monitoring needed. The WindNerd's UART peripheral can be off during STOP — only the GPIO exti wakes it.

---

## LTE Uplink (v1 — SIM7080G)

The ESP32-C6 drives a SIM7080G Cat-M1 modem via UART0 (GPIO16/17). The WindNerd's custom firmware is unaffected — the LP core still triggers wind reads at 5s. The main core wakes every 30 min, brings the modem out of PSM via DTR (GPIO15), sends buffered data via MQTT, then puts the modem back to PSM and returns to deep sleep.

| Component | Current | Notes |
|---|---|---|
| SIM7080G modem (PSM sleep, 30-min uploads) | ~0.40 mA avg | 3 µA PSM + board overhead (~50 µA) + 30s TX every 30 min |
| SIM7080G GNSS (daily GPS RTC sync) | ~0.052 mA avg | 100 mA × 45s / 86400s. Corrects DS3231 drift. |
| Total system with LTE + GPS | ~0.55 mA | 16× battery headroom over 6 months |

See [lte-uplink.md](lte-uplink.md) for full modem analysis, antenna selection, SIM plan, and power calculations at various upload intervals.

---

## Verdict

| Factor | Decision |
|---|---|
| **Wind sensing** | WindNerd Core, **custom firmware** (STOP mode, on-demand via GPIO trigger) |
| **WindNerd power** | ~0.04 mA (down from 0.6 mA factory) |
| **Datalogger** | ESP32-C6 SuperMini (deep sleep + LP core, ~0.018 mA) |
| **Sensors** | BME280 (I2C) + DS3231 RTC (I2C) + piezo rain (ADC), all on ESP32-C6 |
| **Storage** | MicroSD card (8 GB industrial, FAT32, two daily CSV files: wind/rain 5s + temp/RH/pressure 1 min) |
| **Sample rate** | 5s (wind + rain), 1 min (temp/RH/pressure) |
| **Battery** | 2× ER34615 Li-SOCl2 D-cell (~38 Ah, -55°C, $20-30) — 16× headroom |
| **Solar** | Skip for v1. Battery has 16× headroom. |
| **Data retrieval** | SIM7080G LTE-M MQTT uplink (30-min) + WiFi soft-AP for full CSV download |
| **LTE modem** | SIM7080G Cat-M1, PSM sleep, 30-min MQTT uploads + daily GPS RTC sync. See [lte-uplink.md](lte-uplink.md). |
| **Total avg current** | **~0.55 mA** (16× battery headroom) |
| **6-month Ah** | 2.4 Ah (2× D-cell has 16× headroom) |

---

## Next Steps

1. **Clone WindNerd Core repo** — study library source, understand STOP mode + GPIO interrupt hooks
2. **Write WindNerd custom firmware** — STOP mode sleep, GPIO wake, UART output on demand
3. **Wire up ESP32-C6 SuperMini** — GPIO trigger to WindNerd, UART RX from WindNerd TX2, BME280 + DS3231 + OLED + MCP23008 on shared I2C, piezo on ADC, SD card on SPI, EC11 encoder + CON/BAK buttons via MCP23008 I2C expander (INT on GPIO14), OLED MOSFET gate, SIM7080G on UART0 + DTR + PWR (18 pins, 22 available, 3 free)
4. **Write ESP32-C6 firmware** — LP core triggers WindNerd every 5s + reads UART + ADC, main core reads BME280/DS3231 + flushes SD every 1 min, SIM7080G MQTT uplink every 30 min + daily GPS RTC sync, WiFi 6 soft-AP for data retrieval
5. **Measure actual sleep currents** — both boards + SIM7080G PSM. SuperMini dev board overhead (LEDs, LDOs) will inflate numbers. Desolder LEDs.
6. **Test SIM7080G** — AT&T Cat-M1 coverage at site, PSM negotiation, VBAT sag with LC filter, 1.8V SIM, GNSS fix
7. **Enclosure design** — 3D-printed anemometer (WindNerd files) + weatherproof box + Stevenson screen + LTE antenna mast + GNSS patch + lightning arrestor
8. **Cold weather plan** — Li-SOCl2 battery, condensation management, icing, modem temp range
9. **Field test** — deploy, verify LTE-M uplink + GPS sync + WiFi retrieval
