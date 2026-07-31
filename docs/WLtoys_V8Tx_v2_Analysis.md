# WLtoys 284019A (V8 TX v2) OTA Protocol Analysis

**Device:** WLtoys 284019A RC car transmitter (V8 TX hardware, firmware v2)  
**Capture branch:** [wl-284019a](https://github.com/MRC3742/DIY-Multiprotocol-TX-Module/tree/wl-284019a/Captures_wl284019A)  
**Capture tool:** DIY Multiprotocol Module running XN297 dump (Proto=XN297DP, Sub=Auto / 1Mbps)  
**Multiprotocol firmware version:** 1.3.4.31

---

## Table of Contents

1. [RF Physical Layer](#1-rf-physical-layer)
2. [Bind Procedure](#2-bind-procedure)
3. [Normal Operation – RF Channel Hopping](#3-normal-operation--rf-channel-hopping)
4. [Packet Structure](#4-packet-structure)
5. [Channel Encoding](#5-channel-encoding)
6. [Trim Encoding](#6-trim-encoding)
7. [Per-Channel ID Byte (P\[9\])](#7-per-channel-id-byte-p9)
8. [RX Telemetry / Acknowledgment Packets](#8-rx-telemetry--acknowledgment-packets)
9. [TX Power-Off / RX Failsafe Behaviour](#9-tx-power-off--rx-failsafe-behaviour)
10. [Summary of Key Values](#10-summary-of-key-values)
11. [Open Questions](#11-open-questions)

---

## 1. RF Physical Layer

| Parameter | Value |
|-----------|-------|
| Radio IC | NRF24L01+ compatible (XN297 Enhanced Shockburst) |
| Data rate | 1 Mbps |
| Address width | 4 bytes |
| Packet format | Enhanced Shockburst with ACK |
| CRC | Enabled (hardware CRC) |
| Band | 2.4 GHz (channels map to 2400 + N MHz) |

> **Auto-detection note:** During the initial scan the sniffer first tried a 5-byte address and then correctly detected 4-byte addresses. The radio uses NRF-mode Enhanced Shockburst (not the software-decoded XN297 whitening mode).

---

## 2. Bind Procedure

### 2.1 Bind Channel and Address

| Property | Value |
|----------|-------|
| Bind RF channel | **80** (2480 MHz) |
| Bind address | `4D 41 49 4E` → ASCII **"MAIN"** (constant for all 284019A units) |
| Bind packet rate | Every **~16.27 ms** (≈ 61.5 Hz) |
| Bind packet length | 10 bytes |

### 2.2 Bind Packet

TX broadcasts the following 10-byte payload to address `MAIN` on channel 80:

```
B1  32 E9 DE  0A 45 42 38 3C 4A
│   └────────┘ └────────────────┘
│   TX ID[0:2]  TX ID[3] + channel map / extra bytes
└── Bind command (0xB1 = bind request)
```

- **Byte 0:** `0xB1` – Bind command / packet type identifier  
- **Bytes 1–3:** `32 E9 DE` – First 3 bytes of the TX unique identifier  
- **Bytes 4–9:** `0A 45 42 38 3C 4A` – Additional TX identifier / channel table data

### 2.3 Bind Handshake State Machine

The following sequence was captured when the RX was powered on while the TX was already broadcasting bind packets:

| Step | Direction | Address | Payload | Description |
|------|-----------|---------|---------|-------------|
| 1 | TX→RX | `4D 41 49 4E` (MAIN) | `B1 32 E9 DE 0A 45 42 38 3C 4A` | TX broadcasts bind request (repeating) |
| 2 | RX→TX | `32 E9 DE 8A` | `B3 D4 E9` | RX found TX; `B3` = "waiting for TX ACK" |
| 3 | TX→RX | `4D 41 49 4E` (MAIN) | `B4 D4 E9 32 E9 DE 0A 45 42 38 3C 4A` (12 bytes) | TX acknowledges RX; `B4` = "TX found RX"; embeds RX info |
| 4 | RX→TX | `32 E9 DE 8A` | `B5 D4 E9` | RX confirms bind complete; `B5` = "bind complete" |
| 5 | TX→RX | data addr | 13-byte control packet | Normal operation begins |

**Key bind state bytes (Byte 0 of RX response):**

| Value | Meaning |
|-------|---------|
| `0xB3` | RX detected TX, waiting for confirmation |
| `0xB4` | TX has acknowledged RX (embedded in TX→RX packet) |
| `0xB5` | Bind sequence complete |

### 2.4 Derived Addresses (Post-Bind)

After binding, two addresses are used for normal operation:

| Direction | Address | Description |
|-----------|---------|-------------|
| TX → RX (control) | `32 E9 0A E3` | 4-byte data address (derived from TX ID) |
| RX → TX (ACK/telemetry) | `32 E9 DE 8A` | 4-byte RX response address |

---

## 3. Normal Operation – RF Channel Hopping

### 3.1 Active RF Channels

Five channels are used for control packet hopping:

| Channel (decimal) | Frequency (MHz) | Time offset from CH56 | Capture count |
|-------------------|-----------------|----------------------|---------------|
| **56** | 2456 | 0 µs (reference) | 6 packets |
| **60** | 2460 | ~16,260 µs | 6 packets |
| **74** | 2474 | ~32,535 µs | 6 packets |
| **69** | 2469 | ~48,800 µs | 6 packets |
| **66** | 2466 | ~65,070 µs | 5 packets |

**Hopping order:** `56 → 60 → 74 → 69 → 66 → 56 → …`

### 3.2 Timing

| Parameter | Value |
|-----------|-------|
| Channel dwell time | ~16,260–16,270 µs |
| Total frame period | ~81,350–81,400 µs |
| Update rate (all channels) | **~12.3 Hz per channel / ~61.5 Hz total** |
| Packet interval on single channel | ~81,370 µs (when parked on one channel) |

### 3.3 Channel Scan Notes

During auto-detection, 8 channels were initially observed (53, 55, 56, 60, 66, 69, 74, 80). Channels 53, 55, and 80 had only 1 packet each (likely from a single hop pass), while 56, 60, 66, 69, and 74 showed 5–6 packets each and were confirmed as the active hopping set.

---

## 4. Packet Structure

All control packets are **13 bytes** long, transmitted in Enhanced Shockburst mode to address `32 E9 0A E3`.

```
Offset │ Byte  │ Name        │ Range      │ Notes
───────┼───────┼─────────────┼────────────┼─────────────────────────────────────────
  [0]  │  DC   │ Header      │ 0xDC       │ Constant
  [1]  │  80   │ (unused)    │ 0x80       │ Always 0x80 – AETR Ch1 placeholder
  [2]  │  80   │ (unused)    │ 0x80       │ Always 0x80 – AETR Ch2 placeholder
  [3]  │ var   │ Throttle    │ 0x0F–0xF7  │ 0x80 = center; see §5
  [4]  │ var   │ Steering    │ 0x1F–0xE7  │ 0x80 = center; see §5
  [5]  │ var   │ GY Trim     │ 0x02–0x3D  │ Gyro sensitivity trim; see §6
  [6]  │  20   │ Separator   │ 0x20       │ Constant
  [7]  │ var   │ TH Trim     │ 0x02–0x3D  │ Throttle trim; see §6
  [8]  │ var   │ ST Trim     │ (encoded)  │ Steering trim with direction flag; see §6
  [9]  │ var   │ CH ID       │ (varies)   │ Per-channel/session byte; see §7
 [10]  │  0C   │ Constant    │ 0x0C       │ Constant
 [11]  │  00   │ Constant    │ 0x00       │ Constant
 [12]  │  00   │ Constant    │ 0x00       │ Constant
```

**Example idle packet (all controls at center, trims at minimum):**
```
DC 80 80 80 80 02 20 02 20 89 0C 00 00
```

---

## 5. Channel Encoding

### 5.1 Throttle (P\[3\])

| TX Stick Position | Byte Value | Notes |
|-------------------|-----------|-------|
| Center / neutral | `0x80` | |
| Full forward (stick up) | `0x0F` | Car moves **backwards** |
| Full back (stick down) | `0xF7` | Car moves **forwards** |

> The car's forward/backward direction relative to stick direction depends on motor/ESC wiring. The TX sends a linear 8-bit value centred at 0x80.

**Approximate range:** 0x0F–0xF7 (~240 steps, symmetric about 0x80)

### 5.2 Steering (P\[4\])

| TX Stick Position | Byte Value | Notes |
|-------------------|-----------|-------|
| Center | `0x80` | |
| Full right | `0x1F` | |
| Full left | `0xE7` | |

**Approximate range:** 0x1F–0xE7 (~200 steps, symmetric about 0x80)

> The steering range is slightly asymmetric around 0x80: right limit is 0x61 below center (0x80 − 0x1F = 0x61), left limit is 0x67 above center (0xE7 − 0x80 = 0x67).

---

## 6. Trim Encoding

### 6.1 GY Trim (P\[5\]) – Gyro Sensitivity

Simple linear 6-bit value. Increasing values correspond to increasing gyro sensitivity.

| Trim Position | Value |
|---------------|-------|
| Minimum | `0x02` |
| Maximum | `0x3D` |

**Steps:** 0x02 to 0x3D (59 steps)

### 6.2 TH Trim (P\[7\]) – Throttle Trim

Identical encoding to GY Trim.

| Trim Position | Value |
|---------------|-------|
| Minimum | `0x02` |
| Maximum | `0x3D` |

### 6.3 ST Trim (P\[8\]) – Steering Trim

The steering trim uses an unusual **alternating dual-value encoding** where each trim click generates a pair of consecutive packet values:

- **Even packets:** trim value without direction flag  
- **Odd packets:** (trim value + 1) **OR**'ed with `0x80` (direction flag set)

**At center:** `0x20`

**Clicking left (up to 31 clicks from center):**
```
Packet sequence: 20 → A1 → 22 → A3 → 24 → A5 → … → 3E → BF
```
- Odd packets:  `0x80 | (0x20 + step)` where step increments by 1 per click
- Even packets: `0x20 + step` (no flag)

**Clicking right (up to 31 clicks from center):**
```
Packet sequence: 20 → 9F → DE → 9D → DC → … → C2 → 81
```

**Observed extremes:**

| Position | P\[8\] value |
|----------|-------------|
| Max left (31 clicks) | `0xBF` |
| Center | `0x20` |
| Max right (31 clicks) | `0x81` |

> The exact purpose of the alternating `0x80` flag pattern is not fully understood. It may indicate direction to the RX or be an artefact of how the TX firmware cycles through trim sub-states.

---

## 7. Per-Channel ID Byte (P\[9\])

Byte P\[9\] changes value on each TX power-on cycle and takes a **different value per RF channel**. Observed values from the captures:

| RF Channel | P\[9\] value | Notes |
|------------|-------------|-------|
| 56 | `0xE4` | Second capture session |
| 66 | `0xA5` | Second capture session |
| 69 | `0x25` | Second capture session |
| (unknown) | `0x89` | First capture (auto-detected, channel unknown at time of log) |

The values on channels 56 (`0xE4`) and 66 (`0xA5`) differ by `0x80` from channel 69 (`0x25`) and 66 (`0xA5`), suggesting the bit 7 of this byte may also encode a **per-channel flag** (similar to the ST Trim pattern). The upper nibble `0xE` vs `0x2` vs `0xA` may encode the channel index or a session token.

This byte appears to be **derived from the TX's unique ID** and/or **the channel index** within the hopping table.

---

## 8. RX Telemetry / Acknowledgment Packets

The RX transmits short response packets to address `32 E9 DE 8A`. Two types were observed:

### 8.1 Status Packets (3 bytes)

```
B5 D4 E9
│  └──── Fixed bytes (RX identifier / session token)
└─────── Status byte
```

| Status byte | Meaning |
|-------------|---------|
| `0xB3` | Waiting for TX acknowledgment (during bind) |
| `0xB5` | Bound and operating normally |

### 8.2 Telemetry / Extended Packets (6 bytes)

Occasionally observed during normal operation on data channels:
```
40 83 00 00 00 00
```
These 6-byte packets appear on the RX response address (`32 E9 DE 8A`) interleaved with the normal 3-byte status packets. Their content is not yet decoded but may carry battery voltage or RSSI telemetry.

---

## 9. TX Power-Off / RX Failsafe Behaviour

From the captures:

1. **TX turned off while RX is bound:** The RX continues transmitting `B5 D4 E9` status packets to `32 E9 DE 8A` at intervals that progressively lengthen (~4 ms, ~230 ms, ~475–490 ms, then ~960 ms gaps), indicating the RX is searching for the TX. There is no explicit failsafe output observed; the car presumably holds last position or stops.

2. **TX powered back on without rebind:** The TX begins sending control packets immediately on its first hopping channel. After a few irregular intervals (Bad CRC packets during re-synchronisation), the RX locks back on and resumes normal ~81 ms cycle timing without requiring a new bind sequence.

3. **RX powered off while TX is bound:** TX continues sending control packets; no change in packet format or timing.

---

## 10. Summary of Key Values

```
Protocol family:   XN297 Enhanced Shockburst (NRF mode), 1 Mbps, 4-byte address
Bind channel:      80 (2480 MHz)
Bind address:      4D 41 49 4E  ("MAIN")
Data channels:     56, 60, 74, 69, 66  (hop order)
Data address TX:   32 E9 0A E3
Data address RX:   32 E9 DE 8A
Frame period:      ~81.37 ms  (~12.3 Hz per channel)
Packet size:       13 bytes
Header byte:       0xDC  (P[0], constant)
Separator byte:    0x20  (P[6], constant)
Throttle:          P[3], 0x0F (full fwd) .. 0x80 (center) .. 0xF7 (full rev)
Steering:          P[4], 0x1F (full right) .. 0x80 (center) .. 0xE7 (full left)
GY Trim:           P[5], 0x02 (min) .. 0x3D (max)
TH Trim:           P[7], 0x02 (min) .. 0x3D (max)
ST Trim:           P[8], alternating encoded, center=0x20, ±31 clicks
CH ID byte:        P[9], channel/session-dependent, changes per TX power cycle
Tail:              P[10]=0x0C, P[11]=0x00, P[12]=0x00  (constant)
```

---

## 11. Open Questions

1. **ST Trim alternating pattern (P\[8\]):** The exact semantics of the alternating `0x80` flag bit are unclear. Is it a direction indicator consumed by the RX, or is it a TX-side artefact of the trim state machine?

2. **P\[9\] derivation:** How exactly is the per-channel ID byte computed from the TX session / ID? Is it a simple XOR/shift of the TX address bytes with the channel index, or a more complex function?

3. **P\[1\] and P\[2\]:** Always `0x80`. Are these truly unused AETR placeholders (the 284019A is a 2-channel car controller), or could they carry additional data in other operating modes (e.g., a different sub-model)?

4. **Bind packet bytes 4–9:** The bytes `0A 45 42 38 3C 4A` in the bind payload need further analysis. They may represent a channel hopping table, a TX serial number, or calibration data.

5. **6-byte RX telemetry packets (`40 83 00 00 00 00`):** Content not decoded. Possible candidates: battery voltage, RSSI, motor current sense.

6. **Failsafe output:** The exact failsafe behaviour of the ESC/motor driver when the RX loses signal for extended periods was not captured.

---

*Analysis based on captures in [Captures_wl284019A](https://github.com/MRC3742/DIY-Multiprotocol-TX-Module/tree/wl-284019a/Captures_wl284019A) on branch `wl-284019a`.*
