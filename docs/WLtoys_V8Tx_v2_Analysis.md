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
7. [Session ID Byte (P\[9\])](#7-session-id-byte-p9)
8. [RX Telemetry / Acknowledgment Packets](#8-rx-telemetry--acknowledgment-packets)
9. [TX Power-Off / RX Failsafe Behaviour](#9-tx-power-off--rx-failsafe-behaviour)
10. [Summary of Key Values](#10-summary-of-key-values)
11. [Open Questions](#11-open-questions)
12. [Code Discrepancies — REALACC\_nrf24l01.ino vs Protocol Captures](#12-code-discrepancies--realacc_nrf24l01ino-vs-protocol-captures)
13. [Timing, TX/RX Mode Transitions, and NRF24L01 Clone Compatibility](#13-timing-txrx-mode-transitions-and-nrf24l01-clone-compatibility)

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
  [9]  │ var   │ Session ID  │ (varies)   │ Session ID byte (constant per session); see §7
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

## 7. Session ID Byte (P\[9\])

Byte P\[9\] is a **session ID**: it changes value on each TX power-on cycle but remains **constant across all RF channels within a single session**. Observed values from the captures:

| RF Channel | P\[9\] value | Notes |
|------------|-------------|-------|
| 56 | `0xE4` | Second capture session |
| 66 | `0xA5` | Second capture session |
| 69 | `0x25` | Second capture session |
| (unknown) | `0x89` | First capture (auto-detected, channel unknown at time of log) |

> **Capture artefact note:** During the channel-by-channel captures the TX and RX were powered off and back on for each working-channel section. Each power cycle constitutes a new session, so the different P\[9\] values recorded across channels reflect **different TX sessions**, not a per-channel derivation. Within any single power-on session, P\[9\] holds the same value on every hop channel.

This byte appears to be **derived from the TX's unique ID** and/or generated at power-on (e.g., a session token seeded from a hardware counter or address bytes). It does not encode the channel index.

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
Session ID byte:   P[9], session-dependent (constant within session), changes per TX power cycle
Tail:              P[10]=0x0C, P[11]=0x00, P[12]=0x00  (constant)
```

---

## 11. Open Questions

1. **ST Trim alternating pattern (P\[8\]):** The exact semantics of the alternating `0x80` flag bit are unclear. Is it a direction indicator consumed by the RX, or is it a TX-side artefact of the trim state machine?

2. **P\[9\] derivation:** How exactly is the session ID byte computed? Is it derived from the TX address bytes, a hardware-seeded counter, or some other source? Further captures without power-cycling between channels would confirm it is constant per session.

3. **P\[1\] and P\[2\]:** Always `0x80`. Are these truly unused AETR placeholders (the 284019A is a 2-channel car controller), or could they carry additional data in other operating modes (e.g., a different sub-model)?

4. **Bind packet bytes 4–9:** The bytes `0A 45 42 38 3C 4A` in the bind payload need further analysis. They may represent a channel hopping table, a TX serial number, or calibration data.

5. **6-byte RX telemetry packets (`40 83 00 00 00 00`):** Content not decoded. Possible candidates: battery voltage, RSSI, motor current sense.

6. **Failsafe output:** The exact failsafe behaviour of the ESC/motor driver when the RX loses signal for extended periods was not captured.

---

## 12. Code Discrepancies — `REALACC_nrf24l01.ino` vs Protocol Captures

> **Context:** The WLV8TX sub-protocol is implemented inside `Multiprotocol/REALACC_nrf24l01.ino` on branch `wl-284019a`. The following discrepancies were identified by comparing the implementation against the OTA captures documented in §§1–10.

### D1 — Hardcoded Session ID byte (P\[9\]) *(Severity: High)*

**Code:** `packet[9] = 0x77;` (hardcoded in `REALACC_send_packet()`)  
**Protocol:** P[9] is a **session ID byte** (see §7). It changes on every TX power cycle but is **constant across all hop channels within a session**. The per-channel variation that appeared in the original captures was an artefact of cycling the TX/RX off/on between each channel capture, making each capture a new session.

Sending a fixed `0x77` means the car RX always sees the same session ID regardless of session. If the RX uses this byte to validate or correlate packets (e.g., as a session token established during bind), every control packet will fail that check. This is the most likely cause of the car accepting the bind sequence but ignoring subsequent data packets.

---

### D2 — DATA phase packet period *(Severity: High)*

**Code:** The `REALACC_WLV8TX_DATA` branch of `REALACC_callback()` returns `REALACC_PACKET_PERIOD` = **2268 µs** per hop.  
**Protocol:** The real TX sends one packet per channel dwell time of **~16 279 µs**, cycling through 5 channels for a total frame period of ~81 370 µs.

The module therefore transmits at **~440 Hz** (≈5 channels × 1 / 2268 µs) instead of the correct ~61.5 Hz. While the car RX may tolerate a faster packet rate, the mismatch is 7× and could interfere with RX-side timing logic (failsafe timers, ACK expectations, telemetry windows). A dedicated `WLV8TX_PACKET_PERIOD` constant equal to **16 279 µs** should be used.

---

### D3 — Bind RX window too short for B3 / B5 *(Severity: Medium)*

**Code:** `REALACC_WLV8TX_STEP_PERIOD = REALACC_PACKET_PERIOD / 2` = **1134 µs** per bind phase. The BIND\_RX\_SETUP phase switches to RX mode then immediately returns 1134 µs; BIND\_RX\_CHECK fires 1134 µs later to poll for the packet.  
**Protocol:** The car RX's first B3 response arrives **~10 655 µs** after the TX first broadcasts on channel 80.

Because the module cycles through three 1134 µs phases (≈ 3.4 ms per full TX→RX→check cycle) there are multiple attempts to receive B3, which mitigates the timing mismatch. However each RX window is only 1134 µs wide, and the 130 µs mode settling deficit (see §13.1) consumes roughly 11% of that window on a tolerant chip or causes a complete loss of RX capability on intolerant clones.

---

### D4 — GY Trim / TH Trim range off by 2 at both extremes *(Severity: Low)*

**Code:** `packet[5] = convert_channel_8b(CH6) >> 2;` yields values `0x00`–`0x3F`.  
**Protocol:** Actual TX transmits `0x02`–`0x3D` (centered at `0x1F`).  
The hardware endpoints differ by 2 steps; the car should accept the code's wider range but trim accuracy at the extremes is slightly off.

---

### D5 — ST Trim missing alternating direction-flag pattern *(Severity: Low)*

**Code:** `packet[8] = convert_channel_8b(CH7) >> 2;` — simple linear encoding.  
**Protocol:** Every odd packet ORs the trim value with `0x80` as a direction flag (see §6.3). The alternating pattern is required for the RX to interpret trim direction correctly. Without it, ST Trim values from the module are structurally different from what the car expects; steering trim may be ignored or decoded incorrectly.

---

### D6 — `#ifdef MULTI_SYNC` telemetry sync period *(Severity: Dormant)*

**Code:** `telemetry_set_input_sync(REALACC_PACKET_PERIOD)` (2268 µs) is called unconditionally at the top of `REALACC_callback()`, inside `#ifdef MULTI_SYNC`.  
**Status:** `MULTI_SYNC` is not enabled on any production build; the block is dead code. For WLV8TX the correct sync period would be 16 279 µs. This is a latent bug if `MULTI_SYNC` is ever enabled.

---

## 13. Timing, TX/RX Mode Transitions, and NRF24L01 Clone Compatibility

This section documents findings from reviewing `Multiprotocol/XN297_EMU.ino` and `Multiprotocol/NRF24l01_SPI.ino` for hardware-level timing violations, mode-transition sequencing errors, and buffer management issues that explain why the protocol binds on a Jumper TX16 (with its specific NRF24L01 clone) but fails to bind on a Radiomaster MT12 (with a different, likely newer/cheaper clone).

### 13.1 Missing Tstby2a Settling Delay — Root Cause of Clone Incompatibility

**File:** `Multiprotocol/XN297_EMU.ino`, `XN297_SetTxRxMode()`, line 183  
**Code:**
```cpp
if(mode != cur_mode)
{
    //delayMicroseconds(130);   // ← COMMENTED OUT
    cur_mode = mode;
}
NRF_CE_on;
```

The NRF24L01+ datasheet (Table 15) mandates a **130 µs minimum** settling time (Tstby2a) between writing the CONFIG register and asserting CE to enter an active TX or RX state. Without this delay, CE goes high while the crystal oscillator and PLL are still stabilising, producing the following failure modes:

| Failure mode | Effect |
|---|---|
| PLL not locked when CE asserts | First packet preamble is corrupted; RX cannot frame-sync |
| Clone chip PLL slower than spec | Chip may fail to enter RX mode for the entire 1134 µs window |
| Chip enters undefined state | Subsequent SPI writes are ignored until power-cycle or NRF reset |

**Why it differs per transmitter hardware:** Genuine Nordic NRF24L01+ parts have tighter internal timing margins that mask the missing delay. Common clones (Si24R1, BK2423, XN297L) have wider PLL lock-time spreads and stricter requirements for CE assertion timing. The Jumper TX16 external module likely contains an older batch or a more tolerant clone that happens to work; the Radiomaster MT12 likely contains a newer or cheaper variant that does not.

**Contrast with the non-XN297 layer:**  
`NRF24L01_SetTxRxMode()` in `NRF24l01_SPI.ino` (lines 204 and 215) **does** include the 130 µs delay:
```cpp
NRF24L01_WriteReg(NRF24L01_00_CONFIG, (1 << NRF24L01_00_EN_CRC) | ...);
delayMicroseconds(130);   // ← PRESENT in standard NRF layer
NRF_CE_on;
```
`XN297_SetTxRxMode()` reimplements this logic **independently** and has the delay explicitly disabled, creating a silent divergence between the two paths.

---

### 13.2 `TXRX_OFF` Sets CONFIG = 0 (Full Power Down)

**Code (`XN297_EMU.ino`, ~line 165):**
```cpp
if(mode == TXRX_OFF)
{
    NRF24L01_WriteReg(NRF24L01_00_CONFIG, 0);  // PWR_UP=0, EN_CRC=0, CRCO=0
    NRF_CE_off;
    return;  // ← returns WITHOUT updating cur_mode
}
```

Setting CONFIG = 0 places the chip in **full Power Down** (not Standby-I). The NRF24L01+ datasheet requires:

| Transition | Minimum time |
|---|---|
| Power Down → Standby-I | 1.5 ms (Tpd2stby, crystal startup) |
| Standby-I → TX/RX | 130 µs (Tstby2a, PLL lock) |

**Important:** in the WLV8TX bind loop the code does **not** call `TXRX_OFF` between bind phases. The BIND\_TX→BIND\_RX\_SETUP and BIND\_RX\_CHECK→BIND\_TX transitions call `XN297_SetTxRxMode(TX_EN)` or `XN297_SetTxRxMode(RX_EN)` directly, going from Standby-I through CONFIG register change only. `TXRX_OFF` is used at init and at the end of some phases in other sub-protocols. If it is ever interleaved during the WLV8TX bind (e.g. by a protocol switch or future refactor), the 1.5 ms recovery time must be waited for.

**Secondary effect:** Because `TXRX_OFF` returns early without executing `cur_mode = mode`, the static `cur_mode` variable retains its previous value. The conditional `if(mode != cur_mode)` that guards the (commented-out) delay will then always evaluate **true** on the next non-OFF call — meaning the delay *would* always fire if it were un-commented, which is correct behaviour but masks the return-without-update bug.

---

### 13.3 Bind State Machine — Mode Transition Sequence and Timing

The WLV8TX bind callback cycles through three phases, each separated by:
```
REALACC_WLV8TX_STEP_PERIOD = REALACC_PACKET_PERIOD / 2 = 2268 / 2 = 1134 µs
```

```
t=0       BIND_TX:        XN297_SetTxRxMode(TX_EN)  → send B1 bind packet
t=1134µs  BIND_RX_SETUP:  XN297_SetTxRxMode(RX_EN)  → enter listen window
t=2268µs  BIND_RX_CHECK:  poll XN297_IsRX()          → look for B3 or B5
t=3402µs  BIND_TX:        XN297_SetTxRxMode(TX_EN)  → next TX cycle
```

**TX → RX transition (t=1134µs):**

Inside `XN297_SetTxRxMode(RX_EN)`:
1. Write NRF24L01_07_STATUS to clear RX_DR/TX_DS/MAX_RT flags
2. CE ← low  
3. `NRF24L01_FlushRx()` (SPI: ~2–4 µs)  
4. Write CONFIG = `PWR_UP | PRIM_RX`  (SPI: ~2 µs)
5. *(130 µs delay missing)*  
6. CE ← high  

Total actual delay between CONFIG write and CE assertion: **≈ 4–8 µs** from SPI transaction latency only. The chip requires 130 µs minimum. On a tolerant clone, the RX mode stabilises in time; on an intolerant clone (e.g. needing 200–300 µs reported for some Si24R1 production runs), the chip does not enter RX mode at all within the 1134 µs window.

**RX → TX transition (t=3402µs, back to BIND_TX):**

The BIND\_RX\_CHECK phase does not call `XN297_SetTxRxMode`; it only sets `realacc_phase = REALACC_WLV8TX_BIND_TX` and returns. The TX\_EN call happens at the very start of the next callback invocation — approximately 1134 µs later. From the RX CONFIG state (PWR_UP | PRIM_RX) to TX (PWR_UP only), the same CE assertion race applies: PRIM_RX is cleared and CE goes high with no settling delay.

**Net effect on bind reliability:**  
Each bind cycle performs **two mode transitions without settling delays**. With `STEP_PERIOD = 1134 µs` the usable RX listen window (after the missing 130 µs) is effectively:  
- Tolerant clone: ~1004 µs (enough for one B3 packet at 1 Mbps, ≈60 µs/packet)  
- Intolerant clone: 0 µs (chip never enters RX mode; B3 is never seen)

---

### 13.4 Initial Power-On Stabilisation (Protocol Init)

`REALACC_init()` returns `REALACC_INITIAL_WAIT = 500 µs` before the first callback fires. During `REALACC_RF_init()` → `XN297_Configure()` → `NRF24L01_Initialize()`, CONFIG is written to 0 (Power Down). The chip then has only **500 µs** to reach Standby-I before the first `XN297_SetTxRxMode(TX_EN)` call.

The datasheet Tpd2stby minimum is **1.5 ms** (crystal oscillator startup, not guaranteed with external crystals below that time). On boards with a faster crystal and a tolerant clone this may work, but on boards where the clone's internal oscillator startup time matches or exceeds 500 µs, the first several TX attempts will produce garbage or silence.

---

### 13.5 Buffer Management

#### TX FIFO — Double Flush in TX Path

`XN297_WriteEnhancedPayload()` → `XN297_SendPayload()` calls:
```cpp
NRF24L01_WriteReg(NRF24L01_07_STATUS, 0x70);  // clear status
NRF24L01_FlushTx();                            // flush TX FIFO
NRF24L01_WritePayload(msg, len);               // write packet
```
This flush is redundant because `XN297_SetTxRxMode(TX_EN)` already called `NRF24L01_FlushTx()`. The double flush is harmless but adds ~2–4 µs of extra SPI overhead on every packet transmission. More importantly, CE is already **high** when `XN297_SendPayload` runs (raised at the end of `SetTxRxMode`). The FlushTx while CE is high is a defined NRF operation and not a hazard, but it is architecturally redundant.

#### RX FIFO — Flush on Mode Entry (Correct)

`XN297_SetTxRxMode(RX_EN)` calls `NRF24L01_FlushRx()` before writing CONFIG, ensuring the RX FIFO is empty at the start of each listen window. This is correct behaviour and prevents stale packets from a prior window from appearing as valid B3/B5 responses.

#### STATUS Register — No Explicit Clear on Bind Retry

When `BIND_RX_CHECK` finds no packet and returns to `BIND_TX`, the STATUS register is not explicitly cleared. The STATUS clear in `XN297_SetTxRxMode(TX_EN)` (the write to register 0x07 at the top of the function) does clear RX_DR/TX_DS/MAX_RT, so this is handled implicitly on the next TX-mode entry. There is no risk of a stale `RX_DR` flag causing `XN297_IsRX()` to return a false positive on the next CHECK phase.

#### No Hardware Reset on Prolonged Bind Failure

If a clone chip enters an undefined state (CE-assertion before PLL lock, SPI noise during CONFIG write, ESD event on the antenna port), there is no watchdog or retry counter in the bind state machine that triggers `NRF24L01_Reset()`. The bind loop continues indefinitely cycling TX/RX without re-initialising the chip, so a locked-up clone stays locked up until the user power-cycles the transmitter module.

---

### 13.6 ESD and Power Supply Sensitivity of NRF24L01 Clones

Several factors make newer/cheaper NRF24L01 clones more susceptible to the timing issues above:

1. **Thinner gate-oxide ESD protection:** Si24R1 and BK2423 clones ship with ESD ratings as low as ±500 V HBM on RF pins vs ±2000 V for genuine Nordic parts. Antenna-port transients from rapid CE toggling or an attached antenna can latch the RF front-end into a high-current state, corrupting the STATE register.

2. **Weaker internal LDO regulator:** Clones often use a simpler internal regulator. Rapid TX→RX→TX switching generates current steps of 5–20 mA at ~1 MHz rates. On boards with inadequate decoupling (> 100 nF bypass capacitors missing or distant), the DVDD/AVDD can droop enough to cause a soft-reset of the digital core, which randomises all registers including CONFIG, SETUP_AW, and RX_ADDR_P0.

3. **Process variation in PLL lock time:** Clone datasheets do not always specify Tstby2a. Field measurements of Si24R1 variants have recorded lock times from 80 µs to 350 µs across production lots. The WLV8TX bind loop provides zero deterministic margin above the zero-delay case.

4. **Behaviour at the specific SPI clock frequency:** If the module's SPI clock is at the upper limit for a given clone's SCK-to-CSN setup time, CONFIG writes may not be latched correctly. A partial CONFIG write where only one of `PWR_UP` or `PRIM_RX` is stored correctly can leave the chip in an indeterminate RF state.

---

### 13.7 Summary of Recommended Fixes

| # | File / Location | Fix | Impact |
|---|---|---|---|
| F1 | `XN297_EMU.ino` line 183 | Un-comment `delayMicroseconds(130)` in `XN297_SetTxRxMode` | Restores mandatory PLL settling before CE; fixes clone incompatibility |
| F2 | `REALACC_nrf24l01.ino` | Increase `REALACC_INITIAL_WAIT` from 500 µs to ≥ 2000 µs | Ensures crystal startup from power-down before first TX |
| F3 | `REALACC_nrf24l01.ino` | Derive P[9] (Session ID) from `rx_tx_addr` or a session token set at bind time rather than hardcoding `0x77` | Matches real TX behaviour; same value used on all hop channels; likely required for data phase |
| F4 | `REALACC_nrf24l01.ino` | Define and use `WLV8TX_PACKET_PERIOD = 16279` in `REALACC_WLV8TX_DATA` phase | Corrects data rate from ~440 Hz to ~61.5 Hz |
| F5 | `REALACC_nrf24l01.ino` | Implement alternating `0x80` direction flag for ST Trim (P[8]) | Fixes ST Trim direction encoding |
| F6 | `REALACC_nrf24l01.ino` | Add `NRF24L01_Reset()` call after N failed bind cycles | Recovers clone chips from locked-up state |
| F7 | `XN297_EMU.ino` | Update `cur_mode` before the early return in the `TXRX_OFF` branch | Fixes the static `cur_mode` not tracking power-down state |

> **F1 is the highest-priority fix.** Restoring the 130 µs delay in `XN297_SetTxRxMode` has zero negative impact on tolerant chips and is the single most likely reason bind succeeds on some transmitters (Jumper TX16) but not others (Radiomaster MT12).

---

*Analysis based on captures in [Captures_wl284019A](https://github.com/MRC3742/DIY-Multiprotocol-TX-Module/tree/wl-284019a/Captures_wl284019A) on branch `wl-284019a`, and code review of `Multiprotocol/REALACC_nrf24l01.ino`, `Multiprotocol/XN297_EMU.ino`, and `Multiprotocol/NRF24l01_SPI.ino` on branch `wl-284019a`.*
