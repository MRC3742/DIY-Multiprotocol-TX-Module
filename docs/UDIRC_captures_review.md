# UDIRC Protocol Captures Review

## Overview

This document captures analysis of RF packet captures from a Pinecone Forest SG-1205
model running the UDIRC protocol (XN297 Enhanced, 250 Kbps, Scrambled, CRC enabled).
Source: the working firmware on the `add-Pinecone-UDIRC` branch cross-referenced
against capture files `Pinecone_SG-1205_TX-Bind_RX_Bound_02.txt` and
`Pinecone_SG-1205_TX-Bind_RX_Bound_at-each-CH-Start.txt`.

---

## Hardware / RF Parameters

| Parameter          | Value                     |
|--------------------|---------------------------|
| IC                 | XN297 (enhanced payload)  |
| Modulation         | 250 Kbps                  |
| Scrambling         | Enabled                   |
| CRC                | Enabled                   |
| Address length     | 5 bytes                   |
| Payload length     | 15 bytes (fixed)          |
| Hop channels       | 45, 52, 59, 67 MHz offset |
| Bind address       | 01:03:05:07:09            |
| Packet period      | ~20 ms                    |

---

## Two Addresses in Use

- **Bind address**: `01 03 05 07 09` — used for the entire bind phase.
- **TX (normal) address**: the 5-byte TX ID e.g. `C3 E4 04 00 81` — used during
  normal operation. The TX switches to this address once `bind_phase > 1`.

---

## Packet Types (Byte 0 Command Codes)

| Code | Direction | Description                                      |
|------|-----------|--------------------------------------------------|
| 0x01 | TX → RX   | Bind invite (TX ID in bytes [1..5])              |
| 0x02 | TX → RX   | Bind ack (TX ID in bytes [1..5], phase 2)        |
| 0x08 | TX → RX   | Normal control packet                            |
| 0x10 | RX → TX   | Telemetry / status packet                        |

---

## Captured Packet Details

### TX Bind Invite (0x01) — using bind address 01:03:05:07:09

```
C=45 Enhanced A=01:03:05:07:09
  Payload: 01 C3 E4 04 00 81 00 00 00 64 60 6C 00 00 5D
```

| Byte   | Value | Notes                          |
|--------|-------|--------------------------------|
| [0]    | 0x01  | Bind invite command            |
| [1..5] | TX ID | C3 E4 04 00 81                 |
| [9]    | 0x64  | Constant (100 decimal)         |
| [10]   | 0x60  | Constant (96 decimal)          |
| [11]   | 0x6C  | Constant (108 decimal)         |
| [14]   | 0x5D  | Checksum (sum of [0..13])      |

The TX hops to a new channel every 5 packets (`packet_count > 4`) while no RX
response is seen.

---

### TX Bind Ack (0x02) — using TX address after RX responds

Only one 0x02 packet was captured (at CH59):
```
C=59 Enhanced A=C3:E4:04:00:81
  Payload: 02 F8 00 00 30 00 00 00 00 64 60 6C 00 00 5A
```

Bytes [1..4] contain `F8 00 00 30` (the RX-assigned bytes) rather than the TX ID.
The working firmware sends `02 <TX_ID>` but binding still succeeds, suggesting the
RX only validates the command byte (0x02) and RF address, not the payload content.

---

### Normal Control Packet (0x08) — without F8/30 (before bind completes)

```
C=45 Enhanced A=C3:E4:04:00:81
  Payload: 08 64 64 64 00 00 00 00 00 64 60 6C 00 00 64
```

Seen on all 4 channels, always on TX address. Occurs interleaved with bind-address
0x01 packets before the RX powers up — the TX sends both packet types simultaneously.

---

### Normal Control Packet (0x08) — with F8/30 (after RX binds)

```
C=45/52/59/67 Enhanced A=C3:E4:04:00:81
  Payload: 08 64 64 64 00 F8 00 00 30 64 60 6C 00 00 8C
```

| Byte   | Value  | Notes                                           |
|--------|--------|-------------------------------------------------|
| [0]    | 0x08   | Normal control command                          |
| [1]    | 0x64   | Steering (0–200 range)                          |
| [2]    | 0x64   | Throttle (0–200 range)                          |
| [3]    | 0x64   | CH3/RATE (0–200 range)                          |
| [4]    | 0x00   | CH4/LIGHT (0–200 range)                         |
| [5]    | 0xF8   | **RX-assigned** — sourced from packet_in[11]    |
| [6]    | 0x00   | From packet_in[12]                              |
| [7]    | 0x00   | From packet_in[13]                              |
| [8]    | 0x30   | **RX-assigned** — sourced from packet_in[14]    |
| [9]    | 0x64   | Gyro (0–200 range)                              |
| [10]   | 0x60   | ST Trim (0–200 range)                           |
| [11]   | 0x6C   | ST DR (0–200 range)                             |
| [12]   | 0x00   | Flags: bit6=TH.REV, bit7=ST.REV                 |
| [13]   | 0x00   | Unknown                                         |
| [14]   | 0x8C   | Checksum (sum of [0..13])                       |

---

### RX Telemetry Packet (0x10) — from RX to TX

```
Payload: 10 00 00 00 30 00 00 00 00 00 00 F8 00 00 30
```

| Byte   | Value  | Notes                                               |
|--------|--------|-----------------------------------------------------|
| [0]    | 0x10   | Telemetry marker                                    |
| [1]    | 0x00   | 0x01 = low battery; 0x00 = OK (maps to v_lipo1)    |
| [4]    | 0x30   | RX-assigned byte (matches normal TX packet[8])      |
| [11]   | 0xF8   | RX-assigned byte (matches normal TX packet[5])      |
| [14]   | 0x30   | Duplicate of [4]                                    |

The telemetry provides the same F8/30 values at the same byte positions [11] and
[14] as the bind-phase reply — confirming the firmware's use of `packet_in[11..14]`
as the source for `packet[5..8]` works for both bind and normal phases.

---

## Bind Flow (Verified from Captures)

```
TX powers on:
  Sends 0x01 (bind address) + 0x08 (TX address) alternately on the same channel
  Hops channel every 5 packets of the 0x01 type

RX powers on:
  Flood of P(0)= zero-length enhanced ACKs appears (XN297 hardware auto-ack)
  These are NOT firmware-generated; hardware ACKs each received TX packet
  Timing: 15–40 µs after TX packet → confirms hardware origin

TX sees auto-ACKs → counts them → eventually reads full RX payload:
  If RX sends 0x01 payload → bind_phase=1
  (or if ACK alone is enough to trigger bind_phase in some firmware variants)

bind_phase=1: TX sends 0x02 (same structure but cmd byte incremented)
  TX switches to TX address for both TX and RX
  TX runs bind_counter down from 10 → 0 → BIND_DONE

Normal mode:
  TX stays on ONE fixed channel (the channel where bind completed)
  TX sends 0x08 with F8/30 bytes inserted at [5] and [8]
  RX sends 0x10 telemetry periodically
```

---

## P(0) Zero-Length Enhanced ACK Packets

Captures show `A=C3:E4:04:00:81 P(0)=` packets flooding in after RX powers up.
These are **XN297 hardware enhanced-mode ACKs** — sent automatically by the RX
chip for every packet it receives successfully, without RX firmware involvement.
Confirmed by timing: they appear 15–40 µs after the TX packet, far too fast for
firmware processing.

---

## Normal Operation — Fixed Channel

From `Pinecone_SG-1205_TX-Bind_RX_Bound_02.txt` (bound TX):
- TX locked onto **channel 67** and stayed there indefinitely.
- Alternating intervals: ~5 ms (TX send) and ~19 ms (RX window) ≈ 24 ms total.
- No channel hopping during normal operation.

From `Pinecone_SG-1205_TX-Bind_RX_Bound_at-each-CH-Start.txt` (per-channel):
- CH45: bind completed → TX stayed on CH45.
- CH52: bind completed → TX stayed on CH52.
- CH59: bind completed → TX stayed on CH59 (0x02 captured here).
- CH67: bind completed → TX stayed on CH67.

Which channel the TX locks onto is the channel where the bind handshake completes.

---

## RX-Assigned Bytes (F8 and 30)

`F8` (at packet[5]) and `0x30` (at packet[8]) are properties of the **specific RX
unit**, not the TX. The RX returns the same values regardless of TX ID — they are
likely derived from the RX's internal hardware ID or EEPROM. The TX must insert
these bytes into all normal control packets or the RX rejects them.

**How to obtain them**: read `packet_in[11]` and `packet_in[14]` from any valid
received packet (bind reply 0x01 or telemetry 0x10) and store them for the
lifetime of the session.

---

## Hopping Channel Order

**Correct order from captures**: 45, 52, 59, 67 (hex: `0x2D 0x34 0x3B 0x43`)

The original `add-Pinecone-UDIRC` branch had the order as `45, 59, 52, 67`
(`0x2D 0x3B 0x34 0x43`). This was corrected to match the capture sequence and the
original issue description. Since bind hops through all 4 channels regardless of
order, the mismatch does not break binding — but the correct order is documented here.

---

## Checksum Notes

Checksum = simple sum of bytes [0..13], truncated to 8 bits, stored in [14].

The firmware uses `packet[14] += packet[i]` (accumulation). This is **correct**
because `memset(&packet[3], 0x00, 12)` zeros `packet[14]` to 0 before the loop.

Verified against captured packets:
```
01 C3 E4 04 00 81 00 00 00 64 60 6C 00 00  → sum = 0x5D ✓
08 64 64 64 00 00 00 00 00 64 60 6C 00 00  → sum = 0x64 ✓
02 F8 00 00 30 00 00 00 00 64 60 6C 00 00  → sum = 0x5A ✓
08 64 64 64 00 F8 00 00 30 64 60 6C 00 00  → sum = 0x8C ✓
```

---

## Changes Applied to Working Code

Based on capture analysis, the following changes were made to the `add-Pinecone-UDIRC`
working firmware before merging:

1. **Hopping order corrected**: `"\x2D\x3B\x34\x43"` → `"\x2D\x34\x3B\x43"`
   (45,59,52,67 → 45,52,59,67)

2. **`packet_in` initialized**: `memset(packet_in, 0x00, UDIRC_PAYLOAD_SIZE)` added
   to `UDIRC_init()` so the `packet_in[0] != 0` guard in normal mode starts safe.

3. **`packet_in[0]` guard changed**: from `>= 0x01` (passes on garbage data) to
   `!= 0` (same behaviour but explicit intent).

4. **Debug output improved**: added bind_phase, channel, and state labels to all
   debug output for easier protocol decoding.

5. **Comment on checksum clarified**: documented why `+=` is safe here.

---

## Unanswered Questions

1. Why does the RX assign specific F8/30 bytes — are they stored in the RX EEPROM?
2. Does the RX hop channels in normal mode if it misses too many control packets?
3. What are the exact conditions that trigger the RX to send a full 15-byte 0x01
   payload vs. only P(0)= hardware ACKs?
4. What do bytes [9..11] (`64 60 6C`) represent physically?
   `64`=100, `60`=96, `6C`=108 — possibly gyro sensitivity, trim presets, or
   model-specific hardware parameters.

---

## Captured Files Referenced

- `Pinecone_SG-1205_TX-Bind_RX_Bound_02.txt`
- `Pinecone_SG-1205_TX-Bind_RX_Bound_at-each-CH-Start.txt`

---

## TX-ID Binding Limitation (from PR #31 debug captures)

### Observation

Three TX IDs were tested:
- **TX-ID_0** (`C3 E4 04 00 81`): binds and receives 0x10 telemetry → model responds to controls ✓
- **TX-ID_1** (`D0 06 00 00 81`): firmware reports bind complete, RX sends 0x02 but NO 0x10 telemetry → model ignores controls ✗
- **TX-ID_2** (`F6 96 01 00 81`): same as TX-ID_1 ✗

### Root Cause

The RX **stores the paired TX ID in internal EEPROM**. It responds to the bind
handshake from any TX ID (sends 0x01 reply, receives 0x02 ack) but will only
accept control packets and send 0x10 telemetry from the TX ID that is stored in
its EEPROM. This is a hardware security feature, not a firmware issue.

### RX Packet Interpretation

| RX Packet[0] | Meaning                                      |
|--------------|----------------------------------------------|
| 0x01         | Bind reply — RX heard TX invite              |
| 0x02         | **Rebind request** — RX has a stored ID that does not match current TX |
| 0x10         | Telemetry — RX accepts control (TX ID matches stored ID) |

The 0x02 packet from RX is NOT a bind confirmation. It is sent when the RX is
already paired to a different TX ID and is asking to re-pair.

### How to Use a New TX ID

**No user-accessible reset procedure has been found for the Pinecone SG-1205.**
The model has only a red power button (hold ~1 second to power on/off); there is
no documented bind button or factory-reset sequence in any available manual or
online forum. The TX ID stored in the RX EEPROM appears to be **permanently set
at the factory** and is not user-reprogrammable.

**Practical consequence:** To control the Pinecone SG-1205 with MPM, use
`FORCE_UDIRC_ORIGINAL_ID` in the firmware and set `rx_tx_addr` to match the
TX ID that was programmed into your specific model at the factory. For the
reference model this is `C3:E4:04:00:81`.

If you acquire a new model with a different factory TX ID, you will need to
capture that ID from a sniffer trace and add it to the `FORCE_UDIRC_ORIGINAL_ID`
block for the appropriate `RX_num`.

### Telemetry Flapping Fix

`telemetry_link = 1` was set on every 0x10 packet but `telemetry_lost` was never
managed, causing the radio to toggle telemetry lost/found approximately once per
second. Fixed by adopting the SGF22 pattern:

- Added `telem_count` counter incremented each packet.
- When a 0x10 packet arrives: `telemetry_lost = 0`, `telem_count = 0`.
- If `telem_count > UDIRC_TELEM_TIMEOUT` (~5 s): `telemetry_lost = 1`.
- While not lost, send `telemetry_link = 1` every 64 packets (~1.3 s) to keep the radio alive.

### bind_phase > 1 Address Switch Bug Fixed

The `XN297_SetTXAddr` / `XN297_SetRXAddr` calls were inside `UDIRC_send_packet()`
under `if(bind_phase > 1)`, which caused them to be called on **every packet** during
normal operation (since `bind_phase` stays at 3). This was inefficient and potentially
caused RF instability. Fixed by moving the address switch to happen exactly once at
the moment `bind_counter` reaches 0.
