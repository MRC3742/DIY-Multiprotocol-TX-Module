# XBM-37 Quad SPI Capture Analysis

**Manufacturer:** T-Smart  
**Model:** XBM-37 (toy quadcopter)  
**TX/RX RF Chip:** SV7241A (QFN-20, NRF24L01+ clone / BK2425 derivative)  
**Protocol Basis:** Closely related to FQ777 (same RF chip family, same bind address, same ssv ESB air encoding)  
**Capture Tool:** Logic analyzer – digital (02a) and SPI-decoded (all "b" files)

---

## Table of Contents

1. [Capture File Inventory](#1-capture-file-inventory)
2. [Key Protocol Parameters](#2-key-protocol-parameters)
3. [SPI Initialization Sequence](#3-spi-initialization-sequence)
4. [Bind Sequence – Deep Analysis (02a + 02b)](#4-bind-sequence--deep-analysis-02a--02b)
5. [Bind vs No-RX Comparison (01b vs 02b)](#5-bind-vs-no-rx-comparison-01b-vs-02b)
6. [Normal Data Packet Format](#6-normal-data-packet-format)
7. [Per-Channel Control Analysis (03b – 14b)](#7-per-channel-control-analysis-03b--14b)
8. [RF Timing Summary](#8-rf-timing-summary)
9. [MPM Implementation Notes](#9-mpm-implementation-notes)
10. [Testing](#10-testing)
   - [10.1 First test](#101-first-test)
   - [10.2 Test #2](#102-test-2)
   - [10.3 Test #3](#103-test-3)
   - [10.4 Test #4 Analysis and Review (No Build)](#104-test-4-analysis-and-review-no-build)

---

## 1. Capture File Inventory

All captures are located in `Captures_XBM-37/`. The **"b" files** are SPI-decoded exports
(columns: `Time [s], Packet ID, MOSI, MISO`). The **"a" file** is the raw digital export
(columns: `Time[s], MOSI, MISO, SCK, CSN, CE, IRQ`).

| File | Type | Description | SPI Transactions | TX Payloads | Duration |
|------|------|-------------|-----------------|-------------|----------|
| `01b-XBM-37_Quad_TX-PowerOn-NoRX.csv` | SPI | TX power-on, **no RX present** | 5,437 | 1,352 | 2.963 s |
| `02a-XBM-37_Quad_TX-PowerOn-withRX-Bind.csv` | **Digital** | TX power-on + bind **with RX** (all 6 lines) | 405,271 samples | 1,345 payloads | 4.380 s |
| `02b-XBM-37_Quad_TX-PowerOn-withRX-Bind.csv` | SPI | TX power-on + bind **with RX** | 5,409 | 1,345 | 2.949 s |
| `03b-XBM-37_Quad_Aileron-Center-Left-Center-Right-Center.csv` | SPI | Aileron stick: center → left → center → right → center | 6,039 | 1,509 | 3.127 s |
| `04b-XBM-37_Quad_Elevator-Center-Back-Center-Forward-Center.csv` | SPI | Elevator stick: center → back → center → fwd → center | 6,035 | 1,509 | 3.123 s |
| `05b-XBM-37_Quad_Throttle-Low-High-Low.csv` | SPI | Throttle: low → high → low | 6,034 | 1,509 | 3.122 s |
| `06b-XBM-37_Quad_Rudder-Center-Left-Center-Right-Center.csv` | SPI | Rudder (yaw): center → left → center → right → center | 6,034 | 1,509 | 3.122 s |
| `07b-XBM-37_Quad_RateModeSwitch-1-2-3.csv` | SPI | Rate/speed mode switch: 1 → 2 → 3 | 6,034 | 1,509 | 3.122 s |
| `08b-XBM-37_Quad_FlipSwitch-PushButton_Off-On.csv` | SPI | Flip trick button: off → on | 6,034 | 1,509 | 3.122 s |
| `09b-XBM-37_Quad_VideoSwitch-Off-On-Off-On.csv` | SPI | Video record toggle: off → on → off → on | 6,023 | 1,506 | 3.116 s |
| `10b-XBM-37_Quad_PictureSwitch-PushButton-3X.csv` | SPI | Photo/picture button: pressed 3 times | 6,019 | 1,505 | 3.113 s |
| `11b-XBM-37_Quad_HeadlessSwitch-PushButton-Off-On.csv` | SPI | Headless mode button: off → on | 6,022 | 1,506 | 3.116 s |
| `12b-XBM-37_Quad_ReturnToHomeSwitch-PushButton-Off-On.csv` | SPI | RTH button: off → on | 6,019 | 1,505 | 3.113 s |
| `13b-XBM-37_Quad_LED-LightsSwitch-PushButton-On-Off.csv` | SPI | LED lights toggle: on → off | 6,019 | 1,505 | 3.113 s |
| `14b-XBM-37_Quad_OK-Switch-PushButton-Off-On.csv` | SPI | OK button: off → on | 6,027 | 1,507 | 3.120 s |

### Notes
- The "b" SPI files each contain one SPI byte per row. Multiple rows sharing the same `Packet ID` belong to one CSN-low SPI transaction.
- Files 03b–14b were all captured in **normal (post-bind) flight mode**. The TX had previously
  completed a successful bind.
- File 01b and 02b are functionally **identical** captures (confirmed by direct payload comparison)
  except for a tiny time offset (~7 µs between corresponding packets). This confirms the TX
  transmits identically whether or not an RX is present; see Section 5.

---

## 2. Key Protocol Parameters

| Parameter | Value |
|-----------|-------|
| RF transceiver chip | SV7241A (QFN-20, NRF24L01+ / BK2425 derivative) |
| RF frequency band | 2.4 GHz ISM |
| Air data rate (SV7241A) | 2 Mbps (RF_SETUP = 0x26, bit5 = RF_DR = 1) |
| Air data rate (nRF24L01+ equivalent) | **250 kbps** via `ssv_pack_dpl()` method |
| Packet size (raw, pre-pack) | 8 bytes |
| Packet size (after ssv_pack_dpl) | 12 bytes |
| Enhanced ShockBurst | Yes (dynamic payload length, no auto-ACK) |
| SPI clock frequency | ~143 kHz (7 µs period) |
| Bind packet count | **400** |
| Bind address (broadcast) | `E7 E7 E7 E7 67` |
| Data address format | `[TX_ID₀ TX_ID₁ TX_ID₂ E7 67]` |
| Bind channel (first packet) | 0x00 (universal) |
| Hop channels (bind + data) | **0x49, 0x34, 0x26, 0x07** (4 channels) |
| Packet period (steady state) | **~2,070 µs** (~2.07 ms) |
| CE high pulse width | ~1,172 µs |
| CE low gap (inter-packet) | ~896 µs |
| TX-only mode | Yes — EN_RXADDR = 0x00; TX never listens |

---

## 3. SPI Initialization Sequence

The sequence below is from `02b` (PIDs 0–24), occurring over the first ~5 ms after the SPI
bus becomes active (~150–335 ms after TX power-on due to startup delays).

### 3.1 SV7241A Private Bank Registers

The SV7241A (like BK2425) has bank-switched extended registers at addresses 0x18–0x1B.
Register 0x1F selects the bank. These registers hold RF/analog calibration constants specific
to the chip and **do not exist on nRF24L01+** — they are ignored when implementing with MPM.

| PID | Register | Data Written | Purpose |
|-----|----------|-------------|---------|
| 0 | 0x1F | `00` | Select bank 0 |
| 1 | 0x1B | `10 E1 D3 3D` | Bank 0 – RF calibration reg 1 |
| 2 | 0x19 | `06 AA A2 DB` | Bank 0 – RF calibration reg 2 |
| 3 | 0x1F | `01` | Select bank 1 |
| 4 | 0x19 | `77 48 9A E8` | Bank 1 – internal reg |
| 5 | 0x1B | `76 87 CA 01` | Bank 1 – internal reg |
| 6 | 0x1F | `02` | Select bank 2 |
| 7 | 0x1B | `A0 00 18 A0` | Bank 2 – internal reg |
| 8 | 0x1F | `04` | Select bank 4 |
| 9 | 0x18 | `01 00 F0 00` | Bank 4 – internal reg |
| 10 | 0x1F | `05` | Select bank 5 |
| 11 | 0x18 | `84 03 2A 03` | Bank 5 – internal reg |
| 12 | 0x19 | `90 BF 00 00` | Bank 5 – internal reg |
| 13 | 0x1A | `A0 0F 00 00` | Bank 5 – internal reg |

### 3.2 Standard NRF24L01-Compatible Registers

Written immediately after the private registers (PIDs 14–24):

| PID | Register | Written Value | Decoded Meaning |
|-----|----------|--------------|-----------------|
| 14 | CONFIG (0x00) | `0x0C` | EN_CRC=1, CRCO=1 (2-byte CRC), PWR_UP=0, PRIM_RX=0 (TX) |
| 15 | CONFIG (0x00) | *read back* `0x0C` | Verify CONFIG |
| 16 | TX_ADDR (0x10) | `E7 E7 E7 E7 67` | Bind broadcast address |
| 17 | RX_ADDR_P0 (0x0A) | `E7 E7 E7 E7 67` | Matches TX_ADDR for bind |
| 18 | EN_AA (0x01) | `0x00` | Auto-ACK **disabled** on all pipes |
| 19 | EN_RXADDR (0x02) | `0x00` | All RX pipes **disabled** – TX only |
| 20 | RF_CH (0x05) | `0x49` | Initial RF channel (73 MHz offset) |
| 21 | FEATURE (0x1D) | `0x04` | EN_DPL = 1 (dynamic payload length) |
| 22 | DYNPD (0x1C) | `0x01` | DPL enabled on pipe 0 |
| 23 | RF_SETUP (0x06) | `0x26` | SV7241A: 2 Mbps, max power |
| 24 | CONFIG (0x00) | `0x0E` | PWR_UP=1 → TX powered up |

**RF_SETUP = 0x26 mapping:**

| Chip | Bit 5 meaning | Value 0x26 decodes as |
|------|--------------|----------------------|
| SV7241A | RF_DR (0=1Mbps, 1=2Mbps) | **2 Mbps**, max power |
| nRF24L01+ | RF_DR_LOW (0=normal, 1=250kbps) | **250 kbps**, max power |

→ On nRF24L01+, writing 0x26 would set **250 kbps** (not 2 Mbps). For MPM emulation the
correct approach (same as FQ777) is `NRF24L01_SetBitrate(NRF24L01_BR_250K)` combined with
`ssv_pack_dpl()` to produce a compatible air packet.

**Startup timing:**

The TX power-on to first SPI transaction takes ~150 ms (CE is held high from
t = –72 ms until t = 0, then the ~150 ms wait elapses before SPI activity at t ≈ +181 ms in the
02b file). The entire init register sequence completes in ~6 ms.

---

## 4. Bind Sequence – Deep Analysis (02a + 02b)

### 4.1 Overview

After the ~150 ms startup wait, the TX sends exactly **400 bind packets** before switching to
normal data mode.

| Property | Value |
|----------|-------|
| Total bind packets | **400** |
| Bind address | `E7 E7 E7 E7 67` (fixed broadcast) |
| First bind packet channel | `0x00` (universal channel) |
| Remaining 399 bind packet channels | Cycling: `0x49 → 0x34 → 0x26 → 0x07 → 0x49 → …` |
| Bind packet payload | `20 14 07 03 TX_ID₀ TX_ID₁ TX_ID₂ CKSUM` |
| Bind packet content | Identical across all 400 transmissions |

The first bind packet is sent on RF channel **0x00**, ensuring a newly powered-on RX (sitting
on its default channel) can hear the bind announcement regardless of any prior state. Subsequent
bind packets cycle through the four data-hopping channels.

### 4.2 Bind Packet Structure

```
Byte  0  1    2    3    4        5        6        7
     [20 14   07   03   TX_ID₀  TX_ID₁  TX_ID₂  CKSUM]
```

| Byte | Value (this TX) | Description |
|------|----------------|-------------|
| B0 | `0x20` | Bind identifier byte (constant — marks this as a bind packet) |
| B1 | `0x14` | Protocol variant constant (XBM-37 specific; FQ777 uses `0x15`) |
| B2 | `0x07` | Protocol variant constant (XBM-37 specific; FQ777 uses `0x05`) |
| B3 | `0x03` | Protocol variant constant (XBM-37 specific; FQ777 uses `0x06`) |
| B4 | `0x91` | TX_ID byte 0 — unique per TX unit |
| B5 | `0x05` | TX_ID byte 1 — unique per TX unit |
| B6 | `0x05` | TX_ID byte 2 — unique per TX unit |
| B7 | `0x9B` | Checksum = (TX_ID₀ + TX_ID₁ + TX_ID₂) & 0xFF = (0x91+0x05+0x05) & 0xFF |

**Checksum formula for bind packet:** `B7 = (B4 + B5 + B6) & 0xFF`
(This differs from the data packet checksum which sums B0–B6.)

### 4.3 TX ID

TX ID is **3 bytes** embedded in B4–B6 of the bind packet. This TX unit has:

```
TX_ID = [0x91, 0x05, 0x05]
```

After bind, the TX_ADDR is set to: `[TX_ID₀  TX_ID₁  TX_ID₂  0xE7  0x67]`
= `[91  05  05  E7  67]`

The RX extracts TX_ID from the bind packet and uses it to construct the matching address for
normal data reception.

### 4.4 Bind Channel Sequence (from 02b SPI decoded)

```
Packet #1:   ch=0x00  [20 14 07 03 91 05 05 9B]  ← universal announce
Packet #2:   ch=0x49  [20 14 07 03 91 05 05 9B]
Packet #3:   ch=0x34  [20 14 07 03 91 05 05 9B]
Packet #4:   ch=0x26  [20 14 07 03 91 05 05 9B]
Packet #5:   ch=0x07  [20 14 07 03 91 05 05 9B]
Packet #6:   ch=0x49  [20 14 07 03 91 05 05 9B]
...continues cycling 0x49→0x34→0x26→0x07...
Packet #400: ch=0x26  [20 14 07 03 91 05 05 9B]  ← last bind
```

Channel usage across all 400 bind packets:

| Channel | Count | Notes |
|---------|-------|-------|
| 0x00 | 1 | First packet only |
| 0x49 (73 MHz) | 100 | Regular cycle |
| 0x34 (52 MHz) | 100 | Regular cycle |
| 0x26 (38 MHz) | 100 | Regular cycle (includes last bind packet) |
| 0x07 (7 MHz) | 99 | Regular cycle |

### 4.5 Bind-to-Normal Transition

After the 400th bind packet the TX:

1. Completes the 400th transmission (CE pulse ~1,172 µs, then CE low).
2. Waits **~16.6 ms** (no SPI activity; firmware processing time).
3. Writes the new TX_ADDR: `W_TX_ADDR [91 05 05 E7 67]` (SPI pid=1625 at t=1.175882 s).
4. Immediately clears STATUS and starts normal data mode.
5. First normal packet: ch=**0x07** (continuation of the hop sequence from where bind ended).

```
Last bind (400th):  t=1.159258 s, ch=0x26
TX_ADDR change:     t=1.175882 s  (+16.6 ms gap)
First data packet:  t=1.176619 s, ch=0x07  [E1 70 70 70 20 20 00 71]
```

### 4.6 IRQ / STATUS Analysis (from 02a Digital Capture)

The IRQ line (active-low) is asserted by the SV7241A for every successfully transmitted packet
(TX_DS interrupt):

| Observation | Value |
|-------------|-------|
| Total IRQ assertions | 1,340 (matching 1,340 normal CE pulses) |
| IRQ pulse duration (typical) | ~600 µs |
| IRQ source | TX_DS only (TX complete) |
| RX_DR interrupts | **Zero** — no data ever received |

The SPI confirms there are **no `R_RX_PAYLOAD` (0x61) commands** in either 01b or 02b.
Combined with `EN_RXADDR = 0x00` (all RX pipes disabled), it is impossible for the TX to
receive data from the RX. This is confirmed by the STATUS register never showing bit6
(RX_DR) = 1.

One extended IRQ assertion of **15.9 ms** occurs at t=1.160476 s (immediately after the 400th
bind packet). This is not an RX event — it is simply the TX_DS interrupt remaining uncleared
while the firmware processes the bind-completion event and updates TX_ADDR. The IRQ is
cleared when `W_STATUS [70]` is written at t=1.176254 s (pid=1626).

### 4.7 CE Pulse Timing (from 02a)

| Measurement | Value |
|-------------|-------|
| Normal CE pulse duration | min 1,165 µs, max 1,178 µs, **avg 1,172 µs** |
| CE inter-pulse gap (end to start) | **~896 µs** |
| Total cycle (CE high + gap) | **~2,068 µs** |
| Pulse-to-pulse interval (high to high) | min 2.063 ms, max 20.686 ms, **avg 2.084 ms** |

The 20.686 ms outlier corresponds to the bind-to-normal transition pause (~16.6 ms).

---

## 5. Bind vs No-RX Comparison (01b vs 02b)

**Finding: The TX transmits identically whether or not an RX is present.**

| Metric | 01b (No RX) | 02b (With RX) |
|--------|------------|----------------|
| Total TX payloads | 1,352 | 1,345 |
| Bind packets | 400 | 400 |
| Bind packet content | `20 14 07 03 91 05 05 9B` | `20 14 07 03 91 05 05 9B` (identical) |
| TX_ADDR change time | t=1.175899 s | t=1.175882 s |
| TX_ADDR after bind | `[91 05 05 E7 67]` | `[91 05 05 E7 67]` (identical) |
| Normal data payload (idle) | `E1 70 70 70 20 20 00 71` | `E1 70 70 70 20 20 00 71` (identical) |
| Time offset between captures | — | ~7 µs per packet |

The TX performs an automatic timed bind sequence (400 packets) and then switches to data mode
unconditionally, regardless of RX acknowledgment. The TX **never knows** whether the RX
accepted the bind. Bind is one-sided: the RX passively listens for the bind packet on ch=0x00
(or the cycling channels), extracts the TX_ID and hop channels, and then follows the TX's
normal data transmissions.

---

## 6. Normal Data Packet Format

### 6.1 Packet Structure (8 bytes)

```
Byte  0        1      2       3        4       5       6       7
     [Throttle Rudder Aileron Elevator Flags1  Flags2  Flags3  CKSUM]
```

**Checksum (B7):** `B7 = (B0 + B1 + B2 + B3 + B4 + B5 + B6) & 0xFF`

All 8-byte checksum values have been verified against this formula across all capture files.

### 6.2 Stick Channels (B0–B3)

| Byte | Channel | Min | Center | Max | Notes |
|------|---------|-----|--------|-----|-------|
| B0 | Throttle | `0x00` | `0x70` | `0xE1` | Non-return throttle |
| B1 | Rudder (Yaw) | `0x00` | `0x70` | `0xE1` | |
| B2 | Aileron (Roll) | `0x00` | `0x70` | `0xE1` | |
| B3 | Elevator (Pitch) | `0x00` | `0x70` | `0xE1` | |

All analog channels use the same range: **0x00 – 0xE1** (0 – 225 decimal) with center at
**0x70** (112 decimal).

### 6.3 Flags Byte 1 (B4)

| Value | Meaning |
|-------|---------|
| `0x20` | Motors disarmed / idle (bit5=1, bit0=0) |
| `0x21` | Motors armed / in flight (bit5=1, bit0=1) |

Bit5 appears always set. Bit0 indicates the armed/flying state.

### 6.4 Flags Byte 2 (B5)

| Bit | Mask | Name | Description |
|-----|------|------|-------------|
| 7 | `0x80` | OK | OK button pressed (B5 = `0x20` → `0xA0`) |
| 5 | `0x20` | — | Always set (baseline = 0x20) |

Normal state: `B5 = 0x20`; OK button active: `B5 = 0xA0`

### 6.5 Flags Byte 3 (B6)

| Bits | Mask | Name | Description |
|------|------|------|-------------|
| [1:0] | `0x03` | Rate mode | `0x00`=rate 1 (slow), `0x01`=rate 2, `0x02`=rate 3 (fast) |
| 2 | `0x04` | LED off | `0`=LED lights ON, `1`=LED lights OFF |
| 4 | `0x10` | Headless | `1`=headless mode active |
| 5 | `0x20` | Video | `1`=video recording active |
| 6 | `0x40` | Picture | `1`=photo capture triggered |
| 7 | `0x80` | Flip | `1`=3D flip command |

Normal state: `B6 = 0x00` (rate 1, LED on, no special modes)

### 6.6 Return-to-Home (RTH)

The **RTH switch (file 12b)** produces **no change** in the 8-byte payload. Both the RTH-off
and RTH-on states show identical payload: `E1 70 70 70 20 20 00 71`. RTH does not appear
to be encoded in the normal 8-byte payload data structure.

### 6.7 Example Payloads

| Payload | Meaning |
|---------|---------|
| `E1 70 70 70 20 20 00 71` | Throttle max, all sticks center, motors disarmed, LED on |
| `82 70 70 70 21 20 00 13` | Throttle ~mid, all sticks center, motors armed |
| `00 70 70 70 21 20 00 91` | Throttle min, all sticks center, motors armed |
| `E1 70 00 70 21 20 00 02` | Throttle max, aileron full left, motors armed |
| `E1 70 E1 70 21 20 00 E3` | Throttle max, aileron full right, motors armed |
| `E1 70 70 00 21 20 00 02` | Throttle max, elevator full forward, motors armed |
| `82 70 70 70 21 20 80 93` | Throttle mid, flip command active |
| `82 70 70 70 21 20 10 23` | Throttle mid, headless mode active |
| `82 70 70 70 21 20 01 14` | Throttle mid, rate mode 2 |
| `82 70 70 70 21 20 02 15` | Throttle mid, rate mode 3 |
| `E1 70 70 70 20 A0 00 F1` | Throttle max, OK button pressed |
| `E1 70 70 70 20 20 04 75` | Throttle max, LED lights OFF |

---

## 7. Per-Channel Control Analysis (03b – 14b)

All control captures were taken in post-bind normal mode. Hopping channels confirmed active:
`0x07, 0x49, 0x34, 0x26` (cyclic). Average packet interval: **~2.07 ms** across all files.

### 03b – Aileron (Roll)

- **Affected byte:** B2  
- Range observed: `0x00` (full left) → `0x70` (center) → `0xE1` (full right)  
- B0=0xE1 (throttle at max during this capture), B4=0x21 (armed)

### 04b – Elevator (Pitch)

- **Affected byte:** B3  
- Range observed: `0x00` (full forward) → `0x70` (center) → `0xE1` (full back)

### 05b – Throttle

- **Affected byte:** B0  
- Range observed: `0x00` (min/low) → `0x70` (center/hover) → `0xE1` (max/high)  
- Non-return throttle (stick does not spring back to center)

### 06b – Rudder (Yaw)

- **Affected byte:** B1  
- Range observed: `0x00` (one endpoint) → `0x70` (center) → `0xE1` (other endpoint)  
- Note: B0 (throttle) also varies in this capture because left stick controls both
  throttle (up/down) and rudder (left/right) on a Mode 2 transmitter

### 07b – Rate Mode Switch

- **Affected byte:** B6 bits[1:0]  
- `B6 = 0x00`: Rate 1 (slowest/beginner)  
- `B6 = 0x01`: Rate 2 (intermediate)  
- `B6 = 0x02`: Rate 3 (fastest/expert)

### 08b – Flip Switch

- **Affected byte:** B6 bit7  
- `B6 = 0x00`: No flip; `B6 = 0x80`: 3D flip command active  
- While flip is held: throttle (B0) increments slightly (`0x82` → `0x83`)

### 09b – Video Switch

- **Affected byte:** B6 bit5  
- `B6 = 0x00`: Video off; `B6 = 0x20`: Video recording active  
- Toggle on/off: transitions between these two states

### 10b – Picture Switch (3×)

- **Affected byte:** B6 bit6  
- `B6 = 0x00`: Idle; `B6 = 0x40`: Photo capture triggered (momentary)

### 11b – Headless Switch

- **Affected byte:** B6 bit4  
- `B6 = 0x00`: Headless off; `B6 = 0x10`: Headless mode active

### 12b – Return to Home Switch

- **No payload change detected** — both RTH-off and RTH-on produce identical payload  
  `E1 70 70 70 20 20 00 71`  
- RTH is not encoded in the 8-byte data payload

### 13b – LED Lights Switch

- **Affected byte:** B6 bit2  
- `B6 = 0x00`: LED lights **ON** (default)  
- `B6 = 0x04`: LED lights **OFF**  
- Note: Inverted logic — bit2=1 means OFF

### 14b – OK Switch

- **Affected byte:** B5 bit7  
- `B5 = 0x20`: OK button not pressed  
- `B5 = 0xA0`: OK button pressed (0x20 | 0x80)

---

## 8. RF Timing Summary

### 8.1 Per-Packet SPI Cycle (02b decoded)

Each packet transmission consists of 4 SPI transactions:

```
1. W_STATUS [27] = 0x70    → Clear TX_DS / MAX_RT / RX_DR interrupt flags
2. FLUSH_TX [E1]           → Empty TX FIFO
3. W_RF_CH  [25] = <ch>    → Set hop channel
4. W_TX_PAYLOAD [A0] = 8B  → Write 8-byte payload to TX FIFO
                             [CE pulse ~1,172 µs to transmit]
                             [~896 µs gap before next cycle]
```

### 8.2 Bind Phase Timing

| Event | Timestamp (02b) |
|-------|----------------|
| Init register writes complete | t = 0.186 s |
| Startup wait completes | t = 0.335 s (~150 ms) |
| First bind packet (ch=0x00) | t = 0.335838 s |
| 400th (last) bind packet (ch=0x26) | t = 1.159258 s |
| TX_ADDR change to data address | t = 1.175882 s (+16.6 ms) |
| First normal data packet | t = 1.176619 s |

Total bind phase duration: **~0.824 s** (150 ms wait + ~674 ms for 400 packets at 2.07 ms each)

### 8.3 Normal Data Phase Timing

| Event | Interval |
|-------|---------|
| Packet 1 → 2 (startup) | 0.918 ms |
| Packet 2 → 3 (startup) | 0.916 ms |
| Packet 3 → 4 (startup) | 1.491 ms |
| Steady state (4+) | **~2.070 ms** per packet |

The first 3 packets after bind-to-data transition have shorter intervals, then settle into the
steady 2.07 ms period.

### 8.4 STATUS Byte Values Observed

| STATUS | Meaning | When seen |
|--------|---------|-----------|
| `0x0E` | TX FIFO not full, RX FIFO empty, all interrupts clear | Normal/idle |
| `0x2E` | TX_DS = 1 (TX complete interrupt), TX FIFO not full | After each TX packet |

RX_DR (bit6) is **never set** in any STATUS byte — confirming no data is ever received.

---

## 9. MPM Implementation Notes

### 9.1 Comparison with FQ777

The XBM-37 protocol is closely related to FQ777 but has the following differences:

| Property | FQ777 | XBM-37 |
|----------|-------|---------|
| Bind count | 1,000 | **400** |
| Bind packet B1 | `0x15` | `0x14` |
| Bind packet B2 | `0x05` | `0x07` |
| Bind packet B3 | `0x06` | `0x03` |
| Hop channels | `4D 43 27 07` | **`49 34 26 07`** |
| Data range | 0x00–0x64 | **0x00–0xE1** |
| Bind address | `E7 E7 E7 E7 67` | `E7 E7 E7 E7 67` (same) |
| Checksum method | Sum B0–B6 | Sum B0–B6 (same) |
| Bind checksum | Sum B4–B6 | Sum B4–B6 (same) |
| ssv_pack_dpl encoding | Yes | Yes (same) |
| Air bitrate (nRF24L01+) | 250 kbps | 250 kbps (same) |

### 9.2 Required Implementation Constants

```c
#define XBM37_INITIAL_WAIT      500     // ms startup wait
#define XBM37_PACKET_PERIOD    2000     // µs between packets
#define XBM37_PACKET_SIZE         8     // bytes (pre-pack)
#define XBM37_BIND_COUNT        400     // bind packets
#define XBM37_NUM_RF_CHANNELS     4

static const uint8_t XBM37_bind_addr[] = {0xE7, 0xE7, 0xE7, 0xE7, 0x67};
static const uint8_t XBM37_hop_channels[] = {0x49, 0x34, 0x26, 0x07};
```

### 9.3 Bind Packet Builder

```c
// Bind packet (bytes 4–6 = TX ID; B7 = checksum of B4+B5+B6)
packet[0] = 0x20;
packet[1] = 0x14;
packet[2] = 0x07;
packet[3] = 0x03;
packet[4] = rx_tx_addr[0];   // TX_ID byte 0
packet[5] = rx_tx_addr[1];   // TX_ID byte 1
packet[6] = rx_tx_addr[2];   // TX_ID byte 2
packet[7] = packet[4] + packet[5] + packet[6];
```

### 9.4 Data Packet Builder

```c
packet[0] = convert_channel_16b_limit(THROTTLE, 0x00, 0xE1);
packet[1] = convert_channel_16b_limit(RUDDER,   0x00, 0xE1);
packet[2] = convert_channel_16b_limit(AILERON,  0x00, 0xE1);
packet[3] = convert_channel_16b_limit(ELEVATOR, 0x00, 0xE1);
packet[4] = 0x21;   // armed state (or 0x20 = disarmed)
packet[5] = 0x20
          | GET_FLAG(CH_OK, 0x80);           // OK button
packet[6] = (rate_mode & 0x03)              // bits[1:0] = rate (0/1/2)
          | GET_FLAG(!LED_SW, 0x04)          // bit2=1 = LED off
          | GET_FLAG(HEADLESS_SW, 0x10)      // bit4 = headless
          | GET_FLAG(VIDEO_SW, 0x20)         // bit5 = video
          | GET_FLAG(PHOTO_SW, 0x40)         // bit6 = picture
          | GET_FLAG(FLIP_SW, 0x80);         // bit7 = flip
packet[7] = 0;
for (uint8_t i = 0; i < 7; i++) packet[7] += packet[i];  // checksum
```

### 9.5 Bind Channel Sequence

The first bind packet **must** be sent on ch=0x00, then continue cycling:

```c
// First bind packet: use channel 0x00
// Subsequent bind packets: cycle through XBM37_hop_channels[]
if (IS_BIND_IN_PROGRESS && bind_counter == XBM37_BIND_COUNT) {
    NRF24L01_WriteReg(NRF24L01_05_RF_CH, 0x00);   // first packet: ch 0
} else {
    NRF24L01_WriteReg(NRF24L01_05_RF_CH, XBM37_hop_channels[hop_idx]);
    hop_idx = (hop_idx + 1) % XBM37_NUM_RF_CHANNELS;
}
```

### 9.6 Address Management

```c
// TX init:
rx_tx_addr[2] = 0x00;
rx_tx_addr[3] = 0xE7;
rx_tx_addr[4] = 0x67;
// rx_tx_addr[0] and [1] are random/unique to the TX

// Bind address (used during bind):
NRF24L01_WriteRegisterMulti(NRF24L01_10_TX_ADDR, XBM37_bind_addr, 5);

// After 400th bind packet, switch to data address:
NRF24L01_WriteRegisterMulti(NRF24L01_10_TX_ADDR, rx_tx_addr, 5);
```

### 9.7 RF Init

```c
void XBM37_RF_init() {
    NRF24L01_Initialize();
    NRF24L01_WriteRegisterMulti(NRF24L01_10_TX_ADDR, XBM37_bind_addr, 5);
    NRF24L01_WriteReg(NRF24L01_01_EN_AA, 0x00);      // no auto-ACK
    NRF24L01_WriteReg(NRF24L01_02_EN_RXADDR, 0x00);  // no RX pipes
    NRF24L01_WriteReg(NRF24L01_1D_FEATURE, 0x04);    // EN_DPL
    NRF24L01_WriteReg(NRF24L01_1C_DYNPD, 0x01);      // DPL pipe 0
    NRF24L01_SetBitrate(NRF24L01_BR_250K);            // 250 kbps on nRF24L01+
}
```

### 9.8 Air Encoding

Because both the XBM-37 TX and RX use SV7241A (a BK2425 derivative), the air packet format is
the SV7241A Enhanced ShockBurst encoding. When implementing with nRF24L01+, the same
`ssv_pack_dpl()` function used in the FQ777 protocol must be applied to convert the raw 8-byte
payload into the 12-byte packed representation that nRF24L01+ transmits at 250 kbps to produce
a compatible on-air signal.

## 10. Testing

### 10.1 First test

For a minimum-change first bind/flight test, the existing `FQ777` protocol implementation was
modified in-place in `Multiprotocol/FQ777_nrf24l01.ino` without adding a new sub-protocol.

#### Code changes made for this first test

The requested minimal changes were applied while leaving `FQ777_BIND_COUNT` at `1000` and
leaving the rest of the FQ777 logic unchanged:

1. **Bind packet constants changed to XBM-37 values**
   - `B1: 0x15 -> 0x14`
   - `B2: 0x05 -> 0x07`
   - `B3: 0x06 -> 0x03`

2. **Hop channels changed to XBM-37 values**
   - `4D 43 27 07 -> 49 34 26 07`

3. **Basic control range changed to XBM-37 range**
   - throttle/rudder/elevator/aileron output range:
   - `0x00-0x64 -> 0x00-0xE1`

#### What this first test should answer

This test is intended to check whether the XBM-37 receiver will bind and accept the four basic
flight channels using only the major over-the-air differences already proven in the capture
analysis, while keeping the surrounding FQ777 implementation intact.

#### If this first test does not bind or does not control correctly, the next differences to try are:

1. **First bind packet on channel 0x00**
   - The captured XBM-37 transmitter sends the first bind packet on `0x00`, then hops on
     `49/34/26/07`.
   - Current FQ777 logic still starts bind hopping immediately on the four hop channels.
   - This is the most important remaining bind-path difference.

2. **Reduce bind count from 1000 to 400**
   - The real XBM-37 transmitter sends exactly `400` bind packets before switching to the data
     address.
   - For Test #1, leaving `1000` was useful for a minimal experiment, but it was a known mismatch.
   - Test #2 updates the implementation to `400`.

3. **Normal data packet layout still differs significantly from FQ777**
   - Real XBM-37 data packets are:
     - `B0 = throttle`
     - `B1 = rudder`
     - `B2 = aileron`
     - `B3 = elevator`
     - `B4/B5/B6 = state and feature flags`
   - Current FQ777 still sends its original byte meanings:
     - `B2 = elevator`
     - `B3 = aileron`
     - `B4 = rotating trim byte`
     - `B5 = FQ777-specific flags`
     - `B6 = 0x00`
   - Even if bind succeeds, this difference may prevent correct channel response.

4. **Armed/disarmed state byte may be required**
   - XBM-37 captures show `B4 = 0x20` or `0x21` instead of the rotating FQ777 trim byte.
   - The receiver may require the correct `B4` state semantics before motors or control are accepted.

5. **XBM-37 feature-bit mapping differs from FQ777**
   - XBM-37 uses:
     - `B5 bit7 = OK`
     - `B6 bits[1:0] = rate`
     - `B6 bit2 = LED off`
     - `B6 bit4 = headless`
     - `B6 bit5 = video`
     - `B6 bit6 = picture`
     - `B6 bit7 = flip`
   - Current FQ777 flag mapping is different, so auxiliary functions will not match yet.

6. **Possible channel order adjustment**
   - The real XBM-37 packet order for the two right-stick axes is `aileron` then `elevator`.
   - Current FQ777 code still emits `elevator` then `aileron`.
   - For this first test only the requested range change was made, so incorrect axis mapping remains possible.

### 10.2 Test #2

Result carried into this test:
- Test #1 successfully completed bind (RX LEDs changed from flashing to solid), but motors did not
  arm/run from throttle.

#### Code changes made for Test #2

For a minimum-change second test, the existing `FQ777` path in
`Multiprotocol/FQ777_nrf24l01.ino` was updated in-place:

1. **Bind count reduced to analyzed value**
   - `FQ777_BIND_COUNT: 1000 -> 400`

2. **Post-bind byte B4 changed from rotating trim to fixed armed state**
   - Previous Test #1 behavior used the original FQ777 rotating trim pattern on `B4`.
   - Test #2 now sets `B4 = 0x21` on every data packet.
   - Reason for choosing `0x21`: capture analysis indicates `0x21` is the armed-state value
     (while `0x20` corresponds to disarmed/idle state).

All other FQ777 logic remains unchanged for this focused test.

#### If Test #2 is still not successful, suggested next changes

1. **Implement throttle-gated arming transition (`0x20 -> 0x21`)**
   - Original TX behavior appears to be: bind at any throttle, then arm only after throttle reaches
     minimum.
   - Next step: start data phase at `B4=0x20`, switch to `B4=0x21` once a low-throttle condition is
     observed.

2. **Send first bind packet on channel `0x00`**
   - Captures indicate the first bind packet is on `0x00` before hopping on `49/34/26/07`.
   - Current code still starts hopping immediately, which can still block proper post-bind behavior.

3. **Replace remaining FQ777 data-byte semantics with XBM-37 mapping**
   - `B5/B6` flags and feature bits still follow FQ777 behavior.
   - If arming still fails, migrate `B5/B6` to the observed XBM-37 bit mapping.

4. **Swap right-stick axis order if control responses are incorrect**
   - XBM-37 uses `B2=aileron`, `B3=elevator`, while current code still emits the FQ777 order.
   - If movement appears swapped or unstable, this should be updated next.

### 10.3 Test #3

Result carried into this test:
- Test #2 still bound quickly (solid LEDs) but throttle still did not control motors.

#### Code changes made for Test #3

To follow the next suggested step, the existing `FQ777` implementation in
`Multiprotocol/FQ777_nrf24l01.ino` was updated to replace remaining FQ777 `B5/B6` data-byte
semantics with the observed XBM-37 mapping.

1. **B5 remapped to XBM-37 state + OK bit**
   - `B5` now sends:
     - base `0x20` baseline bit (bit5 always set in observed normal packets)
     - `bit7 (0x80)` for OK button
   - Implemented as:
     - `B5 = 0x20 | GET_FLAG(CH10_SW, 0x80)`

2. **B6 remapped to XBM-37 feature/rate bits**
   - `bits[1:0]` = rate mode (from CH11 three-position logic)
   - `bit2 (0x04)` = LED off (`!CH6_SW`)
   - `bit4 (0x10)` = headless (`CH7_SW`)
   - `bit5 (0x20)` = video (`CH8_SW`)
   - `bit6 (0x40)` = picture (`CH9_SW`)
   - `bit7 (0x80)` = flip (`CH5_SW`)

3. **Unchanged from Test #2**
   - Bind count remains `400`.
   - `B4` remains fixed at `0x21` (armed-state test behavior).
   - `B2/B3` order is still legacy FQ777 (`B2=elevator`, `B3=aileron`) for this test.

#### If Test #3 is still not successful, suggested next changes

1. **Implement throttle-gated arming transition (`B4: 0x20 -> 0x21`)**
   - Mirror original TX behavior: bind at any throttle, arm only after throttle reaches minimum.

2. **Send the first bind packet on RF channel `0x00`**
   - Captures show first bind frame on `0x00`, then hop sequence `49/34/26/07`.

3. **Swap right-stick axis order to full XBM-37 order**
   - Change to `B2=aileron`, `B3=elevator` if control response remains wrong.

4. **Adjust button/event behavior from level bits to edge/toggle semantics where needed**
   - If features react incorrectly, migrate selected bits (OK/video/picture/flip) to packetized
     press/toggle behavior matching capture timing patterns.

### 10.4 Test #4 Analysis and Review (No Build)

Result carried into this review:
- Bind still completes quickly (RX LEDs go solid).
- CH6 now toggles LED lights off/on.
- CH7 now toggles headless mode (fast LED flash pattern).
- Throttle still does not start motors.

#### Review findings

1. **B4 is still most consistent with arming-state signaling, not rotating trims**
   - In analyzed XBM-37 normal packets, `B4` behavior is stable at `0x20`/`0x21` and does not show
     the FQ777 rotating trim-byte pattern.
   - FQ777’s rotating trim semantics are protocol-specific legacy behavior and should not be assumed
     for XBM-37.
   - Current evidence still supports `B4=0x20` (idle/disarmed) and `B4=0x21` (armed-state packets).

2. **OK button mapping is not tied to B4 in captures**
   - In capture review, OK button activity appears on `B5 bit7` (`0x20 -> 0xA0`) while `B4` remains
     in its normal state value.
   - The “OK centers trims” hypothesis is plausible from UI perspective but is not supported by the
     currently captured packet deltas.

3. **Throttle low/high mapping decision for next arming test**
   - Keep the previously established byte mapping from Section 6.2:
     - `B0=0x00` = low throttle
     - `B0=0xE1` = high throttle
   - The `05b` file name and capture timing alone should not be treated as the authoritative low/high
     byte-direction source; the packet-byte mapping remains `0x00 -> 0x70 -> 0xE1` for
     low/center/high.
   - For implementation decisions, treat low-throttle detection as `B0` at/near the `0x00` endpoint.

#### Suggested next changes if throttle/motor arming still fails after Test #3

1. **Implement explicit throttle-gated `B4` transition**
   - Start data phase with `B4=0x20`, then switch to `B4=0x21` only after sustained low-throttle
     detection (`B0` at low-end encoding).
   - Keep a concrete hold time at low throttle before asserting armed state (recommended initial
     test window: `300-500 ms`).

2. **Align bind-open channel behavior exactly**
   - Ensure the first bind packet is always sent on channel `0x00` before hopping on
     `49/34/26/07` (if not already active in the test branch used on hardware).

3. **Apply full XBM-37 right-stick order**
   - Change data ordering to `B2=aileron`, `B3=elevator` to match capture-defined packet layout.

4. **Check for button edge/toggle semantics where required**
   - If feature bits are accepted but arming is blocked, emulate per-button event style
     (momentary edge vs. level) for control bytes as observed in capture transitions.

---

*Analysis completed using Python scripts against the raw CSV captures. All packet checksums,
channel sequences, bind counts, and register values verified programmatically.*
