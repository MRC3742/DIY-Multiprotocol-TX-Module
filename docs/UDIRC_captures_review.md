# UDIRC Protocol Captures Review

## Overview

This document captures analysis of RF packet captures from a Pinecone Forest SG-1205
model running the UDIRC protocol (XN297 Enhanced, 250K, Scrambled, CRC enabled).
The goal is to document the full bind handshake and normal operation flow so the
MPM firmware can faithfully reproduce it.

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

---

## Bind ID vs TX ID

Two distinct addresses are used:

- **Bind address**: `01 03 05 07 09` — used during the entire bind handshake.
- **TX (normal) address**: the 5-byte TX ID e.g. `C3 E4 04 00 81` — used during
  normal operation after bind completes.

---

## Packet Types

### Byte 0 Command Codes

| Code | Direction | Description                           |
|------|-----------|---------------------------------------|
| 0x01 | TX → RX   | Bind invite (TX ID in bytes [1..5])   |
| 0x01 | RX → TX   | Bind reply (RX-assigned bytes F8/30)  |
| 0x02 | TX → RX   | Bind ACK (echoes RX F8/30 bytes)      |
| 0x02 | RX → TX   | Bind ACK echo (bind confirmed)        |
| 0x08 | TX → RX   | Normal control packet                 |
| 0x10 | RX → TX   | Telemetry / status packet             |

---

## Bind Handshake Flow

### Step 1 — TX sends bind invite (0x01) while hopping all 4 channels

TX hops channels 45→52→59→67→45… using bind address `01:03:05:07:09`.

```
C=45  pid=0 A=01:03:05:07:09
  Payload: 01 C3 E4 04 00 81 00 00 00 64 60 6C 00 00 <csum>
           ^^ TX ID (5 bytes)  ^^^^^^^^^^^^^^ constant bytes
```

- `packet[0]` = `0x01`
- `packet[1..5]` = TX ID (`C3 E4 04 00 81`)
- `packet[9..11]` = `64 60 6C` (constant, likely model flags)
- `packet[14]` = checksum (sum of bytes [0..13])

---

### Step 2 — RX sends bind reply (0x01) with its assigned bytes

Once the RX powers up and hears the bind invite it responds on the same channel
using the bind address:

```
RX Payload: 01 F8 00 00 30 00 00 00 00 00 00 F8 00 00 30
            ^^ ^^             ^^             ^^ (duplicated at [11] and [14])
```

- `packet_in[0]`  = `0x01`  — RX bind reply identifier
- `packet_in[1]`  = **`0xF8`** — RX-assigned byte (inserted at TX packet[5] going forward)
- `packet_in[4]`  = **`0x30`** — RX-assigned byte (inserted at TX packet[8] going forward)
- `packet_in[11]` = `0xF8` (duplicate of [1])
- `packet_in[14]` = `0x30` (duplicate of [4])

> **Key finding**: `0xF8` and `0x30` are assigned by the RX and must be echoed
> back in the ACK and embedded in all subsequent normal control packets.
> Changing the TX ID does **not** change these values — they appear to be
> an RX-internal identifier (possibly derived from the RX's own serial number).

---

### Step 3 — TX sends bind ACK (0x02) echoing F8/30

TX sends a 0x02 packet echoing the RX-assigned bytes back to confirm it received them:

```
C=45 pid=3 A=C3:E4:04:00:81
  Payload: 02 F8 00 00 30 00 00 00 00 64 60 6C 00 00 <csum>
           ^^ ^^             ^^
```

- `packet[0]`    = `0x02`
- `packet[1]`    = `0xF8` (echoed from RX reply [1])
- `packet[4]`    = `0x30` (echoed from RX reply [4])
- `packet[9..11]`= `64 60 6C`

> **Note**: At this point the TX switches from bind address to TX address
> (`C3 E4 04 00 81`) for both TX and RX.

---

### Step 4 — RX echoes 0x02 to confirm bind complete

```
RX Payload: 02 F8 00 00 30 00 00 00 00 00 00 F8 00 00 30
```

Once the TX receives this 0x02 echo from the RX, binding is complete and both
sides switch to normal operation.

---

## Normal Operation

After bind, the TX stays on **one fixed hop channel** (the channel where bind
completed — no more channel hopping). The TX uses its own address (`C3 E4 04 00 81`).

### Normal TX Control Packet (0x08)

```
C=45 pid=0 A=C3:E4:04:00:81
  Payload: 08 64 64 64 00 F8 00 00 30 64 60 6C 00 00 8C
           ^^ ST TH CH CH ^^             ^^
           |              |              |
           0x08           F8 (from bind) 30 (from bind)
```

| Byte | Content            |
|------|--------------------|
| [0]  | `0x08` (cmd)       |
| [1]  | Steering (0–200)   |
| [2]  | Throttle (0–200)   |
| [3]  | CH3/RATE (0–200)   |
| [4]  | CH4/LIGHT (0–200)  |
| [5]  | `udirc_rx_byte5` (e.g. `0xF8`) — from bind |
| [6]  | `0x00`             |
| [7]  | `0x00`             |
| [8]  | `udirc_rx_byte8` (e.g. `0x30`) — from bind |
| [9]  | Gyro (0–200)       |
| [10] | ST Trim (0–200)    |
| [11] | ST DR (0–200)      |
| [12] | Flags: bit6=TH.REV, bit7=ST.REV |
| [13] | `0x00` (unknown)   |
| [14] | Checksum (sum of [0..13]) |

---

### RX Telemetry Packet (0x10)

```
RX Payload: 10 00 00 00 30 00 00 00 00 00 00 F8 00 00 30
```

| Byte  | Content                               |
|-------|---------------------------------------|
| [0]   | `0x10` (telemetry marker)             |
| [4]   | `0x30` (same as `udirc_rx_byte8`)     |
| [11]  | `0xF8` (same as `udirc_rx_byte5`)     |
| [14]  | `0x30` (duplicate of [4])             |

The firmware should monitor telemetry packets and may update `udirc_rx_byte5`
and `udirc_rx_byte8` if they change (though in practice they appear fixed).

---

## Bugs Found in Original Firmware

### 1. Checksum accumulation bug

```c
// BUG: += accumulates across packets instead of computing a fresh sum
for(uint8_t i=0;i<UDIRC_PAYLOAD_SIZE-1;i++)
    packet[14] += packet[i];
```

Should be:
```c
uint8_t sum = 0;
for(uint8_t i=0;i<UDIRC_PAYLOAD_SIZE-1;i++)
    sum += packet[i];
packet[14] = sum;
```

### 2. Missing bind handshake ACK (0x02 step)

The original code never sends the 0x02 ACK packet after receiving the RX 0x01
reply, so binding never completes properly.

### 3. Missing F8/30 bytes in normal packets

The original code zeroed out `packet[5..8]` instead of inserting the RX-assigned
`F8` and `30` bytes. Without these bytes the RX rejects all normal packets.

### 4. Wrong TX ID in FORCE_UDIRC_ORIGINAL_ID

The original `else` branch had TX ID `F6 96 01 00 81` instead of the captured
`C3 E4 04 00 81`.

### 5. Hopping channel order mismatch

Channels were ordered `45, 59, 52, 67` but the capture shows `45, 52, 59, 67`.

---

## Unanswered Questions

1. How does the RX determine which single channel to lock onto after bind?
   (It may be the channel where the 0x02 ACK was successfully exchanged.)
2. Are `F8` and `0x30` always the same for a given RX, or do they vary per power cycle?
3. Does the TX hop channels during normal mode if telemetry is lost for an extended period?
4. What are bytes `64 60 6C` in positions [9..11] during bind and normal packets?
   Possibly: `64`=100 (midpoint 0–200), `60`=96, `6C`=108 — or model-specific constants.

---

## Captured Files Referenced

- `Pinecone_SG-1205_TX-Bind_RX_Bound_02.txt`
- `Pinecone_SG-1205_TX-Bind_RX_Bound_at-each-CH-Start.txt`
