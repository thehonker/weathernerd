# LTE Modem Options & Power Budget Impact

**Date:** 2026-09-27
**Author:** Coilette (for goos)
**Status:** Research — v2 uplink upgrade path
**Related:** [power-budget.md](power-budget.md), [hardware-design.md](hardware-design.md)

---

## Context

v1 is local-storage only — WiFi soft-AP for spring data retrieval. This document explores adding LTE uplink so the station can phone home during the 6-month deployment instead of waiting for physical retrieval.

**Constraints:**
- USA, AT&T is the only carrier with coverage at the deployment site
- Remote location — needs directional external antenna
- 6 months unattended, battery powered (2× ER34615 Li-SOCl2 D-cell, 38 Ah)
- Current system draws ~0.10 mA avg (without modem)
- Data payload is tiny — wind/rain/env CSV snippets, a few KB per upload

---

## AT&T Network Status (2026)

| Technology | Status on AT&T | Bands | Notes |
|---|---|---|---|
| **LTE-M (Cat-M1)** | ✅ Active, committed long-term | B2, B4, B5, B12, B17, B66 | AT&T's IoT strategy. Low power, PSM/eDRX support. |
| **NB-IoT** | ❌ Sunset Q1 2025 — dead | — | AT&T stopped certifying devices and selling data plans. Do not use. |
| **LTE Cat-1** | ✅ Active | Same as LTE | More power, more bandwidth than needed. |
| **LTE Cat-4** | ✅ Active | Same as LTE | Way too power-hungry for battery IoT. |
| **2G GSM** | ❌ Shutdown complete | — | Dead on AT&T. |
| **3G** | ❌ Shutdown Feb 2022 | — | Dead on AT&T. |

**Key takeaway:** LTE-M (Cat-M1) is the only viable low-power option on AT&T. NB-IoT is dead. Cat-1 and Cat-4 work but burn 10-100× more power.

### AT&T LTE Bands at the Site

AT&T's rural coverage backbone is **700 MHz (Band 12/17)** — longest range, best penetration. Secondary rural band is **850 MHz (Band 5)**. Higher bands (1900 MHz B2, 1700 MHz B4/66, 2300 MHz B30) are urban/suburban capacity layers with shorter range.

| Band | Frequency | Range | Penetration | AT&T rural? |
|---|---|---|---|---|
| B12/B17 | 700 MHz | Excellent | Excellent | ✅ Primary |
| B5 | 850 MHz | Very good | Very good | ✅ Secondary |
| B2 | 1900 MHz | Moderate | Moderate | ❌ Urban |
| B4/B66 | 1700 MHz (AWS) | Moderate | Moderate | ❌ Urban |
| B30 | 2300 MHz (WCS) | Short | Poor | ❌ Urban |

**Antenna must cover 700 MHz (B12/17) and ideally 850 MHz (B5).** Higher bands are bonus if the tower has them, but 700 MHz is what reaches remote sites.

---

## Modem Options Compared

### 1. SIM7080G — Cat-M1 + NB-IoT (BEST FIT)

| Spec | Value |
|---|---|
| LTE class | Cat-M1 (eMTC) + NB-IoT |
| Max downlink/uplink | 0.3 / 0.37 Mbps (Cat-M1) |
| Sleep current (PSM) | **~3 µA** |
| Active current (Cat-M1 TX) | ~200–600 mA peaks |
| Idle/connected current | ~3–10 mA |
| Bands (global "G" variant) | B1, B2, B3, B4, B5, B8, B12, B13, B18, B19, B20, B25, B26, B27, B28, B66, B71, B85 |
| AT&T bands supported | B2, B4, B5, B12, B17 (via B12), B66, B71 |
| GNSS | GPS, GLONASS, Galileo, BeiDou |
| Host interface | UART, USB |
| SIM voltage | 1.8 V only |
| Form factors | LGA module, Waveshare HAT, breakout boards |
| Price (Waveshare HAT) | ~$35–50 |

**Why it fits:** 3 µA PSM sleep is the lowest in the category. Cat-M1 is AT&T's committed IoT technology. NB-IoT is dead on AT&T but the modem falls back to Cat-M1 seamlessly. GNSS is used for daily DS3231 RTC sync — eliminates ±5 min drift over 6 months.

**Catch:** 1.8 V SIM only. Need a 1.8 V-compatible IoT SIM. Most modern M2M SIMs support 1.8 V, but verify before buying.

### 2. SIM7000G — Cat-M1 + NB-IoT (ALTERNATIVE)

| Spec | Value |
|---|---|
| LTE class | Cat-M1 + NB-IoT |
| Sleep current (PSM) | ~7.4 µA (module only) |
| Board-level sleep (LilyGo T-SIM7000G) | **~1.2 mA** (board overhead!) |
| Active current | ~200–600 mA peaks |
| Bands (global "G" variant) | B1, B2, B3, B4, B5, B8, B12, B13, B18, B19, B20, B25, B26, B27, B28, B66, B71, B85 |
| AT&T bands | Same as SIM7080G |
| GNSS | GPS, GLONASS, Galileo, BeiDou |
| Form factors | Botletics Arduino shield, LilyGo all-in-one, breakout |
| Price (Botletics shield) | ~$40–60 |

**Why it's #2:** Same Cat-M1 capability, but module sleep is 2.5× higher than SIM7080G (7.4 µA vs 3 µA). On a breakout board (Botletics, LilyGo), board-level quiescent (LDOs, level shifters, SIM holder) pushes actual sleep to ~1.2 mA — 400× the module's PSM rating. The SIM7080G has the same board overhead problem but starts lower.

**Botletics advantage:** Best documentation and library support in the category. If you're learning, start here. For production low-power, SIM7080G wins.

### 3. A7670G — LTE Cat-1 (MORE POWER, MORE BANDWIDTH)

| Spec | Value |
|---|---|
| LTE class | Cat-1 (10/5 Mbps) |
| Sleep current | ~1–5 mA (module, no PSM) |
| Active current | ~300–800 mA peaks |
| 2G fallback | Yes (GSM/GPRS) |
| Form factors | LilyGo T-A7670G (ESP32 + modem + GPS + 18650) |
| Price (LilyGo board) | ~$25–40 |

**Why not:** Cat-1 has no PSM (Power Saving Mode). The modem can't drop to microamps — best case is ~1 mA sleep with DTR asserted. For a 6-month battery deployment, that's 4.4 Ah just for modem sleep. Doable with 38 Ah battery, but wasteful when Cat-M1 needs 0.003 Ah for the same job.

**Use case:** If you need more bandwidth (firmware OTA, images, frequent large uploads) and have solar or wall power. Not our case.

### 4. SIM7600G-H — LTE Cat-4 (OVERKILL)

| Spec | Value |
|---|---|
| LTE class | Cat-4 (150/50 Mbps) |
| Sleep current | ~1 mA (best case, DTR sleep) |
| Active current | Up to **2 A** transmit spikes |
| Board-level sleep (LilyGo) | ~1–5 mA (measured) |
| Price (Waveshare HAT) | ~$40–60 |

**Why not:** Cat-4 transmit spikes hit 2 A. The Li-SOCl2 D-cells can't deliver 2 A — they're rated for ~100 mA continuous max. You'd need a separate Li-ion battery + charger for the modem alone. Absurd overkill for sending a few KB of weather data.

### 5. nRF9151 / nRF9160 — Nordic Cat-M1 (ALT ECOSYSTEM)

| Spec | Value |
|---|---|
| LTE class | Cat-M1 + NB-IoT |
| Sleep current | ~3–9 µA (PSM) |
| Architecture | Nordic nRF91 SoC (ARM Cortex-M33 + LTE modem) |
| Use mode | Flash with "atclient" firmware → UART AT modem passthrough |
| Price | ~$30–50 (dev board) |

**Why not:** Different ecosystem from ESP32. The nRF91 is a combined MCU + modem — you'd either replace the ESP32-C6 entirely or use it as a dumb AT modem (wasteful, and the toolchain is completely different). Interesting for a clean-sheet design, but we already have ESP32-C6 + firmware investment.

---

## Power Budget With LTE

### Strategy: Infrequent Uploads with PSM Sleep

The modem stays in PSM (Power Saving Mode) between uploads. ESP32-C6 wakes it via GPIO, opens UART, sends buffered data as a compact packet (MQTT or HTTP POST), then commands the modem back to PSM. The modem stays registered to the network during PSM — reconnection takes seconds, not the 30-60s of a cold attach.

**Upload payload:** Compressed CSV snippet. 5s wind + rain for 10 min = 120 rows × ~30 bytes = ~3.6 KB. Env data for 10 min = 10 rows × ~40 bytes = 400 bytes. Total per upload: ~4 KB. Trivial even for Cat-M1's 0.3 Mbps.

### Power Calculation: SIM7080G at Various Upload Intervals

**Module-only sleep (3 µA PSM):**

| Upload interval | Sleep energy | Upload energy | Avg current | 6-month Ah | % of 38 Ah battery |
|---|---|---|---|---|---|
| 10 min | 0.003 mA × 9.5 min | ~300 mA × 30s attach+TX | ~1.0 mA | 4.4 Ah | 12% |
| 30 min | 0.003 mA × 29.5 min | ~300 mA × 30s | ~0.35 mA | 1.5 Ah | 4% |
| 1 hour | 0.003 mA × 59.5 min | ~300 mA × 30s | ~0.18 mA | 0.79 Ah | 2% |
| 2 hours | 0.003 mA × 119.5 min | ~300 mA × 30s | ~0.09 mA | 0.40 Ah | 1% |
| 6 hours | 0.003 mA × 359.5 min | ~300 mA × 30s | ~0.03 mA | 0.13 Ah | 0.3% |

**Upload energy model:** 30s per upload = ~5s network wake from PSM + ~10s attach + ~10s TLS+MQTT publish + ~5s teardown. Average current during upload ~300 mA (Cat-M1 TX burst averaged over the session, not peak).

### Realistic Board-Level Sleep (the gotcha)

Module PSM is 3 µA. But breakout boards add:
- LDO quiescent: ~1–5 µA
- SIM holder leakage: ~1 µA
- Level shifters: ~1–5 µA
- LED indicators (if not removed): ~200–500 µA each
- Board trace leakage: negligible

**Realistic board sleep: ~10–50 µA** (with LEDs removed). Still excellent.

| Scenario | Board sleep | Upload interval | Total avg current (system + modem) | 6-month Ah |
|---|---|---|---|---|
| Optimistic (10 µA board sleep) | 10 µA | 30 min | 0.10 + 0.36 = **0.46 mA** | 2.0 Ah |
| Realistic (50 µA board sleep) | 50 µA | 30 min | 0.10 + 0.40 + 0.052 = **0.55 mA** | 2.4 Ah |
| Realistic (50 µA board sleep) | 50 µA | 1 hour | 0.10 + 0.23 = **0.33 mA** | 1.4 Ah |
| Realistic (50 µA board sleep) | 50 µA | 2 hours | 0.10 + 0.14 = **0.24 mA** | 1.1 Ah |
| Pessimistic (1.2 mA board sleep, LilyGo-level) | 1.2 mA | 1 hour | 0.10 + 1.38 = **1.48 mA** | 6.5 Ah |

**Even the pessimistic case (1.2 mA board sleep, hourly uploads) fits in 38 Ah with 5.8× headroom.** The realistic case (50 µA sleep, 30-min uploads) uses 2.2 Ah — 5.7% of battery capacity over 6 months.

### Comparison: All Modems at 30-min Upload Interval

| Modem | Board sleep | Avg current (system + modem) | 6-month Ah | Verdict |
|---|---|---|---|---|
| SIM7080G (realistic board) | 50 µA | 0.55 mA | 2.4 Ah | ✅ Best |
| SIM7080G (pessimistic board) | 1.2 mA | 1.65 mA | 7.2 Ah | ✅ OK |
| SIM7000G (Botletics, measured) | 1.2 mA | 1.65 mA | 7.2 Ah | ✅ OK |
| A7670G (Cat-1, no PSM) | ~1 mA | 1.45 mA | 6.3 Ah | ⚠️ Wasteful |
| SIM7600G-H (Cat-4) | ~1 mA | 1.45 mA | 6.3 Ah | ❌ 2A TX spikes kill it |

All Cat-M1 options fit the battery. The difference is headroom — SIM7080G gives 16× headroom (including daily GPS sync), SIM7000G gives 5×. Cat-1/Cat-4 technically fit but TX spikes are a battery-delivery problem, not just capacity.

---

## Antenna Selection

### Requirements

- **Must cover:** 700 MHz (AT&T B12/17) — primary rural coverage
- **Should cover:** 850 MHz (AT&T B5) — secondary rural
- **Nice to have:** 1700/1900/2300 MHz (AT&T B2/B4/B66/B30) — if tower has them
- **Type:** Directional (Yagi or log-periodic) — aim at nearest AT&T tower
- **Gain:** 9–11 dBi (enough to pull weak signal without overdriving the modem)
- **Mounting:** Outdoor, mast-mounted, weatherproof
- **Connector:** SMA (most common on modem breakouts) or TS-9 (some boards)

### Recommended Antennas

| Antenna | Type | Freq range | Gain | Connector | Price | Notes |
|---|---|---|---|---|---|---|
| **Proxicast 11 dBi Yagi** | Log-periodic Yagi | 698–2700 MHz | 11 dBi | SMA male | ~$35 | Covers all AT&T LTE bands. Amazon. Best all-rounder. |
| **RFMAX RY-4-14-SNF** | 9-element Yagi | 615–6100 MHz | ~9 dBi | N-female | ~$60 | Rugged outdoor, covers B71 (600MHz) too. Overkill but future-proof. |
| **Tupavco TP514** | Log-periodic Yagi | 806–960 MHz + 1.7–2.5 GHz | 9 dBi | SMA + TS-9 adapter | ~$25 | Cheapest option. Covers 850+1700/1900 but **misses 700 MHz**. Only buy if site has strong B5 signal. |
| **Generic 700/800/850/900 Yagi** (eBay) | Yagi | 700–960 MHz | ~8–10 dBi | SMA | ~$20 | Covers 700+850 MHz. Cheap. Misses higher bands but those don't reach remote sites anyway. |

**Recommendation:** Proxicast 11 dBi (698–2700 MHz). Covers 700 MHz (critical) through 2700 MHz (all AT&T bands), $35, well-reviewed, outdoor rated. If on a budget, the generic 700–960 MHz Yagi from eBay works — 700 MHz is what matters at remote sites.

### Antenna Installation Notes

- **Aim at nearest AT&T tower** — use [CellMapper.net](https://www.cellmapper.net) or OpenSignal to find tower location and AT&T band info
- **Height matters** — mount on mast above tree line if possible. 700 MHz bends over terrain better than higher freqs, but line-of-sight still wins
- **Cable loss** — keep coax short. At 700 MHz, RG58 loses ~3 dB per 10m. Use LMR-240 or LMR-400 for runs >3m. Or mount a preamp near the antenna
- **Polarity** — LTE uses ±45° slant polarization (X-pol). Match the antenna's polarity or use a dual-pol antenna. Most Yagis are vertical; that's close enough for a fixed installation
- **Lightning protection** — outdoor antenna = lightning target. Use a coaxial lightning arrestor at the entry point, bond to ground

---

## SIM Card & Data Plan

### Requirements
- **Must support LTE-M (Cat-M1)** on AT&T
- **1.8 V SIM** (SIM7080G requirement) — most modern IoT SIMs support this
- **IoT/M2M plan** — not a voice SIM. Data-only.

### Recommended Providers

| Provider | Plan | Price | Notes |
|---|---|---|---|
| **Hologram.io** | IoT SIM, pay per MB | $0.60/MB, $5/mo base | Global roaming, LTE-M supported. Best for prototyping. ~4 KB/upload × 144 uploads/day = 576 KB/day ≈ $0.35/day. |
| **1NCE** | 10-year IoT SIM | ~$15 one-time for 500 MB | Flat rate, no monthly. 500 MB lasts years at our data rate. Best value. |
| **AT&T IoT** | AT&T IoT Data Plans | Varies ($1–25/mo) | Direct from AT&T. Best coverage guarantee but more expensive. |
| **Twilio Super SIM** | Pay per MB | $0.50/MB | Developer-friendly API. LTE-M supported. |

**Recommendation:** 1NCE 10-year SIM. 500 MB for ~$15 one-time. At 4 KB/upload every 30 min, that's 576 KB/day = 105 MB/year. 500 MB lasts ~4.7 years. No monthly fee. Perfect for a 6-month deployment.

### Data Volume Check

| Upload interval | Data per upload | Uploads/day | Data/day | Data/6 months |
|---|---|---|---|---|
| 10 min | ~4 KB | 144 | 576 KB | 103 MB |
| 30 min | ~4 KB | 48 | 192 KB | 34 MB |
| 1 hour | ~4 KB | 24 | 96 KB | 17 MB |
| 2 hours | ~4 KB | 12 | 48 KB | 8.6 MB |

Even 10-minute uploads fit in 500 MB. 30-minute uploads use 34 MB over 6 months — nothing.

---

## Integration with ESP32-C6

### Wiring (SIM7080G breakout → ESP32-C6)

| SIM7080G pin | ESP32-C6 pin | Function |
|---|---|---|
| VBAT | Raw battery (before LDO, via LC filter) | Power — 3.6V Li-SOCl2, L1 1µH + 3× 100µF |
| VCC | 3.3V (LDO output) | Logic level reference |
| GND (×2) | GND | Ground — tie both to common ground |
| RXD | GPIO16 (UART0 TX) | ESP32 TX → Modem RX |
| TXD | GPIO17 (UART0 RX) | Modem TX → ESP32 RX |
| DTR | GPIO15 | Sleep/wake control (PSM). 10kΩ pull-up. |
| PWR | GPIO9 | Power key. 10kΩ pull-up. |
| ANT | SMA connector → coax → Yagi | External antenna |

**Free pins after allocation:** 20, 22, 23 (3 spare). The modem uses GPIO9 (PWR), GPIO15 (DTR), GPIO16 (UART0 TX), GPIO17 (UART0 RX).

**Note:** SIM7080G wants 3.8V VBAT typically. The Li-SOCl2 battery is 3.6V nominal. A boost converter or separate LDO from the raw battery may be needed. Check the specific breakout board's input range — some accept 3.3–5V and regulate internally.

### Firmware Flow

```
Every 30 min (main core wake):
1. Wake modem from PSM (assert DTR)
2. Wait for "OK" on UART
3. Open MQTT connection (or HTTP POST)
4. Send buffered CSV data from SD card
5. Close connection
6. Command modem to PSM (AT+CFUN=0 or AT+CPSMS=1)
7. Deassert DTR
8. Main core back to deep sleep
```

The LP core continues 5s wind sampling during modem activity — the two are independent. The modem upload happens on the main core's 1-minute wake cycle (piggyback on the BME280/SD flush wake).

### Upload Protocol

| Protocol | Pros | Cons | Use for v2? |
|---|---|---|---|
| **MQTT** | Lightweight, persistent session, QoS levels | Needs broker (self-hosted or cloud) | ✅ Best — publish to a broker, home station subscribes |
| HTTP POST | Simple, RESTful | Heavier (TLS handshake overhead) | Fallback |
| CoAP | Ultra-lightweight | Less tooling, NAT issues | Skip |
| SMS | No data connection needed | 160 chars, AT&T SMS fees, unreliable delivery | Emergency alerts only |

**Recommendation:** MQTT to a self-hosted broker (Mosquitto on the home station or a $5 VPS). ESP32-C6 publishes to `weathernerd/station-id/wind`, `weathernerd/station-id/env`, `weathernerd/station-id/rain`. Home station or VPS subscribes and stores to database.

---

## Impact on v1 Power Budget

| Scenario | Avg current | 6-month Ah | % of 38 Ah | Headroom |
|---|---|---|---|---|
| **v1 (no LTE)** | 0.10 mA | 0.44 Ah | 1.2% | 86× |
| **v2 + SIM7080G, 30-min upload + daily GPS sync (realistic)** | 0.55 mA | 2.4 Ah | 6.3% | 16× |
| **v2 + SIM7080G, 1-hour upload (realistic)** | 0.33 mA | 1.4 Ah | 3.7% | 27× |
| **v2 + SIM7080G, 2-hour upload (realistic)** | 0.24 mA | 1.1 Ah | 2.9% | 35× |
| **v2 + SIM7000G, 30-min upload (pessimistic board)** | 1.65 mA | 7.2 Ah | 19% | 5× |
| **v2 + A7670G Cat-1, 30-min upload** | 1.45 mA | 6.3 Ah | 17% | 6× |

**Verdict:** LTE-M with SIM7080G at 30-min or 1-hour uploads + daily GPS sync is comfortably within the 38 Ah battery. Even with pessimistic board overhead, 6-month runtime is fine. The 2× D-cell battery was sized for 80× headroom without modem — LTE + GPS eats into that headroom but 16× is still massive.

**Solar still not needed.** Even the worst realistic case (7.2 Ah) leaves 30.8 Ah unused. The battery was massively oversized for v1; LTE is what makes the oversizing justified.

---

## Bill of Materials (v2 LTE Add-on)

| Item | Part | Price | Notes |
|---|---|---|---|
| Modem board | Waveshare SIM7080G Cat-M/NB-IoT HAT (or equivalent breakout) | ~$35–50 | UART-controlled, PSM capable |
| Antenna | Proxicast 11 dBi Yagi (698–2700 MHz) | ~$35 | Outdoor directional, covers all AT&T bands |
| Coax cable | LMR-240, 3–5m, SMA male → SMA male | ~$10–15 | Keep short to minimize loss |
| Lightning arrestor | SMA coaxial lightning arrestor | ~$15 | Outdoor install safety |
| SIM card | 1NCE 10-year IoT SIM (500 MB) | ~$15 | One-time, no monthly fee |
| Mounting hardware | Mast, U-bolts, weatherproofing | ~$15 | |
| **Total** | | **~$125–140** | |

---

## Recommendation

| Decision | Choice | Reason |
|---|---|---|
| **Modem** | SIM7080G | 3 µA PSM sleep, Cat-M1 on AT&T, global bands, GNSS for RTC sync |
| **Upload interval** | 30 min (default), configurable down to 10 min | 30 min = 2.4 Ah/6mo, 16× battery headroom. 10 min if real-time matters. |
| **Antenna** | Proxicast 11 dBi Yagi (698–2700 MHz) | Covers 700 MHz (critical) + all AT&T bands, $35, outdoor rated |
| **GNSS antenna** | Passive patch, 25×25mm, ~$2 | For daily GPS RTC sync. Mounted with sky view. |
| **SIM** | 1NCE 10-year (500 MB, ~$15) | No monthly fee, 500 MB lasts years at our data rate |
| **Protocol** | MQTT to self-hosted broker | Lightweight, persistent, QoS. Home station or VPS subscribes. |
| **RTC sync** | Daily GPS fix via SIM7080G GNSS | Eliminates DS3231 drift. ~45s fix once/day, negligible power cost. |
| **v1 or v2?** | v1 — folded in | SIM7080G is part of v1 build. |

### What This Does NOT Change

- WindNerd custom firmware — unaffected, still STOP mode + on-demand
- ESP32-C6 LP core — still handles 5s wind sampling, modem is main-core only
- SD card — still primary storage, LTE is secondary uplink
- WiFi soft-AP — still used for spring data retrieval (full CSV download)
- Battery — 2× ER34615 Li-SOCl2 D-cell (38 Ah), no solar needed
- OLED + encoder + buttons — still for interactive mode, unaffected. Encoder + buttons now on MCP23008 I2C expander, only INT line uses GPIO14.
- DS3231 RTC — still the always-on clock; GPS corrects drift daily, CR1220 backup still needed

### What Changes

- 4 GPIO pins for modem UART + control (GPIO 9, 15, 16, 17)
- Firmware: main core wakes modem every 30 min, sends MQTT, sleeps. Daily GPS sync cycle corrects DS3231.
- Power budget: 0.10 → 0.55 mA avg (still 16× battery headroom)
- Enclosure: LTE antenna feed-through + lightning arrestor + GNSS patch antenna with sky view
- BOM: +$127 for modem + LTE antenna + GNSS antenna + SIM

---

## Open Questions

1. **SIM7080G breakout board availability** — Waveshare HAT is Pi-oriented. Need a bare breakout or SIMCom dev board with UART + SMA. Check availability of SIM7080G on a simple carrier.
2. **Modem power supply** — SIM7080G wants 3.8V VBAT. Li-SOCl2 is 3.6V. Boost converter adds quiescent current. Or run from the 3.3V LDO and accept slightly reduced TX power. Test signal strength.
3. **AT&T Cat-M1 coverage at the exact site** — AT&T's LTE-M footprint is smaller than their full LTE footprint. Verify with an AT&T IoT SIM + phone test before committing. [AT&T IoT coverage map](https://www.business.att.com/products/lpwa.html).
4. **PSM negotiation** — AT&T must support PSM for the modem to enter 3 µA sleep. AT&T LTE-M network supports PSM but the negotiated T3324/T3412 timers may limit sleep duration. Test with actual SIM.
5. **Cold weather** — modem spec is typically -30°C to +80°C. Li-SOCl2 is rated -55°C. The modem should survive winter, but verify the specific breakout board's temperature rating.
