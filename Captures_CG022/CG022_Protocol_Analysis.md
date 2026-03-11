# CG022 Protocol Analysis

Analysis of the AO-SEN-MA transmitter protocol used with the CG022 quadcopter,
decoded from Saleae logic capture CSV files in `Captures_CG022/`.

## SPI Capture CPHA Validation

The raw captures ("a" files) store logic-level pin transitions. Re-decoding
with both CPHA=0 and CPHA=1 produces **identical** register values because
MOSI is stable at both clock edges. The LT89xx uses CPHA=1 (SPI Mode 1),
but CPHA=0 exports are valid for MOSI data analysis since the MCU sets up
data well before the first clock edge.

## RF Chip Identification

- **Chip**: LT89xx series (likely LT8910 in SSOP16 package)
- **Confirmation**: Register 0x00 reset value = 0x6FE0 (LT89xx family signature)
- **CRC Polynomial**: Register 0x17 = 0x8005 (standard CRC-16)

## Data Rate

- **Register 0x2C (Data Rate)**: NOT written in initialization → uses default value
- **Default data rate**: **1 Mbps**
- **Verified by timing**: ~2.31ms packet interval with 10-byte payload + overhead confirms 1 Mbps

## Emulation Recommendation

**NRF24L01** is the best emulation chip because:
1. 1 Mbps is natively supported by NRF24L01
2. Existing LT8900 emulation layer already handles protocol framing
3. Frequency range (2402-2472 MHz) is within NRF24L01 capabilities
4. SHENQI protocol demonstrates a working LT89xx → NRF24L01 emulation

CC2500 is **not recommended** because no existing LT89xx emulation layer exists for it,
and the 1 Mbps rate is already handled by NRF24L01.

## LT89xx Register Configuration

| Register | Value  | Description |
|----------|--------|-------------|
| 0x00     | 0x6FE0 | Chip ID / Reset |
| 0x01     | 0x5681 | Clock configuration |
| 0x02     | 0x6617 | AGC / Crystal settings |
| 0x04     | 0x9CC9 | RSSI threshold |
| 0x05     | 0x6637 | RSSI level |
| 0x07     | 0x0000 | RF channel (initially off) |
| 0x08     | 0x6C90 | Reserved |
| 0x09     | 0x4800 | PA current control |
| 0x0A     | 0x7FFD | Crystal oscillator |
| 0x0B     | 0x0008 | RSSI configuration |
| 0x0C     | 0x0000 | Reserved |
| 0x0D     | 0x48BD | Reserved |
| 0x16     | 0x00FF | Threshold |
| 0x17     | 0x8005 | CRC polynomial (CRC-16) |
| 0x18     | 0x0067 | Reserved |
| 0x19     | 0x1659 | AGC |
| 0x1A     | 0x19E0 | AGC |
| 0x1B     | 0x1300 | AGC |
| 0x1C     | 0x1800 | AGC |
| 0x20     | 0x4800 | Preamble=3 bytes (bits 15:13=010, val+1), Trailer=8 bits, SyncWord=2 bytes |
| 0x21     | 0x3FC7 | Preamble pattern / demod threshold |
| 0x22     | 0x2000 | FIFO control |
| 0x23     | 0x0300 | Communication control |
| 0x24     | 0x2211 | Sync Word bytes 0-1 (bind phase; changes to TX-ID after bind, see below) |
| 0x25     | 0x068C | Sync Word bytes 2-3 (not used with 2-byte sync) |
| 0x26     | 0x5A5A | Sync Word bytes 4-5 (not used with 2-byte sync) |
| 0x27     | 0x0033 | Sync Word bytes 6-7 (not used with 2-byte sync) |
| 0x28     | 0x4402 | CRC initial data (16-bit CRC seed) |
| 0x29     | 0xB000 | TX power / packet config (CRC enabled) |
| 0x2A     | 0xFDB0 | Calibration |
| 0x2B     | 0x000F | Calibration |
| 0x34     | 0x8080 | TX power / FIFO control |

## RF Channel Configuration

- **Channels used**: 0, 10, 20, 30, 40, 50, 60, 70 (8 unique channels)
  - In frequency: 2402, 2412, 2422, 2432, 2442, 2452, 2462, 2472 MHz
- **Hopping order**: 0, 40, 10, 50, 20, 60, 30, 70 (interleaved pattern)
- **TX interval**: ~2.31 ms per hop
- **Full cycle**: ~18.5 ms through all 8 channels (~54 Hz frame rate)

## Packet Format

### Over-the-air Frame Structure
```
[Preamble 3 bytes] [Sync Word 2 bytes] [Trailer 8 bits] [Payload 10 bytes] [CRC-16 2 bytes]
```

### Sync Word (CRITICAL: Changes After Bind)

**Bind phase**: Sync word from register 0x24 = `0x2211` (fixed for all TXes)

**Data phase**: After exactly 166 bind packets, the TX rewrites register 0x24
with a TX-ID-derived sync word so the receiver only hears its paired TX:

```
New reg 0x24 = (bind_pkt[6] << 8) | bind_pkt[5]
             = (rx_tx_addr[4] << 8) | rx_tx_addr[3]
```

Example from capture: TX_ID = `11 22 33 06 AB FC AD`
- Bind sync: reg 0x24 = `0x2211`
- Data sync: reg 0x24 = `0xAB06` (from TX_ID bytes [4]=0xAB, [3]=0x06)

This sync word change is verified in both capture 01b (no RX) and 02b (with RX).

### Bind Packet (10 bytes)
Sent repeatedly on all 8 channels for exactly 166 packets (~383ms) after power-on.
Uses fixed sync word `0x2211`.
```
Byte 0: 0x0A    - Packet marker (constant)
Byte 1: 0x00    - Bind indicator
Byte 2: TX_ID[0]
Byte 3: TX_ID[1]
Byte 4: TX_ID[2]
Byte 5: TX_ID[3]
Byte 6: TX_ID[4]
Byte 7: TX_ID[5]
Byte 8: TX_ID[6]
Byte 9: 0x00    - Bind packets always use 0x00
```

Example from capture: `0A 00 11 22 33 06 AB FC AD 00`

### Data Packet (10 bytes)
Sent after bind period on all 8 channels continuously.
Uses TX-ID-derived sync word (see above).
```
Byte 0: 0x0A    - Packet marker (constant)
Byte 1: TX_ID   - TX address byte for receiver identification
Byte 2: Throttle - 0x00 (low) to 0x3F (max)
Byte 3: Elevator - 0x00 (back) to 0x20 (center) to 0x3F (forward)
Byte 4: Rudder   - 0x00 (left) to 0x20 (center) to 0x3F (right)
Byte 5: Aileron  - 0x00 (left) to 0x20 (center) to 0x3F (right)
Byte 6: Flags    - 0x20 (default), 0xA0 (LEDs off)
Byte 7: Flags    - 0x20 (default), 0x60 (flip mode), 0xE0 (headless mode)
Byte 8: 0x20     - Constant
Byte 9: Checksum - Sum of bytes 2-8 (mod 256)
```

### Checksum Verification
```
Center stick:  0A xx 00 20 20 20 20 20 20 → sum(00+20+20+20+20+20+20) = 0xC0 ✓
Headless mode: 0A 00 00 20 20 20 20 E0 20 → sum(00+20+20+20+20+E0+20) = 0x80 ✓
LEDs off:      0A 00 00 20 20 20 A0 60 20 → sum(00+20+20+20+A0+60+20) = 0x80 ✓
```

## Flag Byte Details

### Byte 6 Flags
| Bit | Value | Function |
|-----|-------|----------|
| 7   | 0x80  | LEDs off |
| 6-0 | 0x20  | Default base value |

### Byte 7 Flags
| Bits | Value | Function |
|------|-------|----------|
| 7+6  | 0xC0  | Headless mode |
| 6    | 0x40  | Flip mode |
| 5-0  | 0x20  | Default base value |

## Capture File Summary

| File | Description | Key Observations |
|------|-------------|------------------|
| 01a/b | TX power-on, no RX | 166 bind packets → sync word change → data packets |
| 02a/b | TX power-on, with RX bind | Same: 166 bind → sync change → data |
| 03a/b | Aileron full range | Mid-session, byte 5 varies 0x00-0x3F |
| 04a/b | Elevator full range | Mid-session, byte 3 varies 0x00-0x3F |
| 05a/b | Throttle low-high-low | Mid-session, byte 2 varies 0x00-0x3F |
| 06a/b | Rudder full range | Mid-session, byte 4 varies 0x00-0x3F |
| 07a/b | Headless mode switch | Mid-session, byte 7: 0x60 ↔ 0xE0 |
| 08a/b | Flip switch (6 presses) | Mid-session, byte 7: 0x60 ↔ 0x20 |
| 09a/b | LEDs out switch | Mid-session, byte 6: 0x20 ↔ 0xA0 |
| 11a/b | Gyro calibration | Mid-session, byte 4 sweeps 0x00-0x20 |

## Bind Timing Analysis

From captures 01b and 02b (power-on):
- Original TX sends exactly **166 bind packets** before transitioning to data
- Each packet is sent **once per channel** (no retransmit)
- Bind duration: ~383ms (166 × 2.31ms)
- Bind covers ~20.75 full channel cycles
- Sync word change occurs at SPI packet 1527 (immediately after 166th TX)

## TX Cycle Per Packet (from SPI captures)

Each packet transmission follows this exact sequence:
```
1. Write FIFO reg 0x32 = 0x0000       (clear/reset FIFO)
2. Write reg 0x34 = 0x8080            (FIFO control / TX power)
3. Write FIFO reg 0x32 × 5 words      (5 × 16-bit = 10 bytes payload)
4. Write reg 0x07 = 0x01xx            (TX enable + channel)
5. Write reg 0x07 = 0x00yy            (set next channel, TX disabled)
```

The LT8900 hardware automatically appends preamble, sync word, trailer, and CRC
to the FIFO data when transmitting.
