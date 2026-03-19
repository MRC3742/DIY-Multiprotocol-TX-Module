# MPM TX Module SPI Capture Guide

## Purpose

Capture the SPI traffic between the STM32 MCU and the NRF24L01 chip on the
MPM TX module.  This reveals exactly what the LT8900 emulation layer sends,
allowing direct comparison with the stock AO-SEN-MA transmitter captures.

## Why This Capture Is Needed

Receiver-side captures (52b vs 53b) confirmed that the CG022 receiver accepts
1690 FIFO reads from the stock TX but zero from the MPM module.  The CRC
byte-reversal fix is mathematically verified correct (see CRC Verification
below), yet binding still fails.  The remaining unknown is whether the
NRF24L01 chip produces the expected on-air waveform.  Possible causes
that only an MPM TX-side capture can diagnose:

1. **NRF24L01 register misconfiguration** – Wrong address width, data rate,
   CRC not fully disabled, unexpected FEATURE/DYNPD state.
2. **Payload byte mismatch** – Emulation buffer differs from expected
   bit-reversed packet due to a shift/alignment bug.
3. **Packet Control Field (PCF) insertion** – Some NRF24L01 clones always
   insert 9 PCF bits even in non-enhanced ShockBurst mode, which would
   shift all payload data and break CRC at the LT8910 receiver.
4. **CE pin or power-up sequencing** – NRF24L01 not entering TX mode or
   not transmitting packets.

## Capture Setup

### Saleae Pin Assignment (4 channels minimum)

| Saleae Channel | MPM Module Signal | Description |
|----------------|-------------------|-------------|
| D0 | SPI_MOSI | STM32 → NRF24L01 data |
| D1 | SPI_MISO | NRF24L01 → STM32 data |
| D2 | SPI_CLK  | SPI clock |
| D3 | SPI_CS   | Chip select (active low) |

If additional channels are available:

| D4 | CE | NRF24L01 Chip Enable (TX trigger) |
| D5 | IRQ | NRF24L01 interrupt (active low) |

### Saleae SPI Analyzer Settings

- **MOSI**: D0
- **MISO**: D1
- **Clock**: D2
- **Enable**: D3 (active low)
- **CPOL**: 0 (clock idle low)
- **CPHA**: 0 (sample on leading edge)
- **Bit Order**: MSB first
- **Bits per Transfer**: 8

### Sample Rate

- Minimum: 12 MHz (for 8 MHz SPI clock)
- Recommended: 24 MHz for clean decoding

### Trigger

- Trigger on D3 (SPI_CS) falling edge
- Start capture **before** powering on the MPM module

## Recommended Captures

### Priority 1: FORCE_ID Bind Capture

Uncomment `#define FORCE_CG022_ORIGINAL_ID` in `CG022_nrf24l01.ino`,
rebuild, and flash the MPM module.  This forces the TX ID to match the
stock TX (11 22 33 06 AB), allowing direct comparison of every byte.

1. Power on the MPM module with CG022 protocol selected
2. Capture at least 3.5 seconds of SPI traffic from power-on
3. Export two files:
   - **Digital export** (raw pin levels): `61a-MPM_TX-ForceID-Bind.csv`
   - **SPI decoded export**: `61b-MPM_TX-ForceID-Bind.csv`

### Priority 2: Default ID Bind Capture (Optional)

Keep `FORCE_CG022_ORIGINAL_ID` commented out.  Same capture procedure.

- `62a-MPM_TX-DefaultID-Bind.csv`
- `62b-MPM_TX-DefaultID-Bind.csv`

## What to Look For

### 1. NRF24L01 Register Configuration (Init Phase)

After power-on, the STM32 configures the NRF24L01.  Verify these registers:

| Register | Expected Value | Purpose |
|----------|---------------|---------|
| 0x00 CONFIG | 0x02 | PWR_UP=1, CRC disabled, TX mode |
| 0x01 EN_AA | 0x00 | Auto-ack disabled (all pipes) |
| 0x03 SETUP_AW | 0x03 | 5-byte address |
| 0x04 SETUP_RETR | 0x00 | No retransmits |
| 0x05 RF_CH | varies | 2 + channel (first bind channel = 2) |
| 0x06 RF_SETUP | varies | 1 Mbps (bits 5,3 = 00), power level |
| 0x0A RX_ADDR_P0 | AA 88 44 55 55 | NRF address (bind, LSByte first) |
| 0x10 TX_ADDR | AA 88 44 55 55 | Same as RX_ADDR_P0 |
| 0x1C DYNPD | 0x00 | Dynamic payload disabled |
| 0x1D FEATURE | 0x01 | EN_DYN_ACK enabled |

**Critical check**: CONFIG must have EN_CRC=0 (bit 3 = 0).  If the NRF24L01
appends its own CRC, the on-air packet will have extra bytes that the
LT8910 receiver does not expect.

### 2. NRF24L01 TX Address (Bind Phase)

The 5-byte NRF address encodes the LT8900 preamble + sync + trailer:

```
NRF address bytes (LSByte first on SPI): AA 88 44 55 55

On air (MSByte first): 55 55 44 88 AA
                       [preamble] [sync 0x2211] [trailer]
```

With the NRF auto-preamble (0x55), the full on-air overhead is:
```
55 | 55 55 | 44 88 | AA | payload...
NRF  preamble  sync    trail
```

### 3. Payload Data (Bind Packets)

Each W_TX_PAYLOAD (0xA0) SPI command should be followed by 12 bytes:
10 data bytes (bit-reversed) + 2 CRC bytes (bit-reversed).

**Expected bind packet payload with FORCE_ID:**

```
Original:  0A 00 11 22 33 06 AB FC AD 00
Reversed:  50 00 88 44 CC 60 D5 3F B5 00
CRC:       62 C6
Full NRF:  50 00 88 44 CC 60 D5 3F B5 00 62 C6  (12 bytes)
```

**Expected data packet payload (idle sticks):**

```
Original:  0A 00 00 20 20 20 20 20 20 C0
Reversed:  50 00 00 04 04 04 04 04 04 03
CRC raw:   0xA03D → bit-reversed bytes: 05 BC
Full NRF:  50 00 00 04 04 04 04 04 04 03 05 BC  (12 bytes)
```

### 4. Channel Hopping

After each packet, the STM32 writes a new RF_CH value.  The sequence should
cycle through (register 0x05 values = channel + 2):

```
Channel: 0  40  10  50  20  60  30  70
RF_CH:   2  42  12  52  22  62  32  72
```

### 5. Packet Timing

Packets should be spaced approximately 2310 µs apart.  Measure the time
between consecutive W_TX_PAYLOAD commands.

## CRC Verification

The CRC-16 computation has been verified independently:

- **Polynomial**: 0x8005 (CRC-16/ARC)
- **Initial value**: 0x4402
- **Input processing**: Each payload byte is bit-reversed, then processed
  MSBit-first through the CRC register (equivalent to the LT8910's native
  LSBit-first processing of non-reversed bytes)
- **Output**: CRC bytes are bit-reversed before placement in the NRF buffer

**Bind packet CRC verification:**
```
Payload: 0A 00 11 22 33 06 AB FC AD 00
CRC (computed): 0x4663
CRC hi reversed: 0x62
CRC lo reversed: 0xC6
On-air bits: 01100010 11000110
LT8910 equivalent: 0x46 LSBfirst 0x63 LSBfirst = 01100010 11000110 ✓
```

## Comparison Checklist

After capturing, compare with the stock TX captures (02b) to identify
any discrepancy:

- [ ] NRF24L01 CONFIG register = 0x02 (no CRC)
- [ ] NRF24L01 address = AA 88 44 55 55 (bind phase)
- [ ] RF_CH values follow the expected hopping pattern
- [ ] Data rate is 1 Mbps (RF_SETUP bits 5,3 = 00)
- [ ] W_TX_PAYLOAD uses 0xA0 command (no extra PCF bits)
- [ ] Payload length is exactly 12 bytes per W_TX_PAYLOAD
- [ ] First payload byte is 0x50 (bit_reverse of 0x0A)
- [ ] Last two payload bytes match expected CRC (0x62 0xC6 for bind)
- [ ] Packet spacing is ~2310 µs
- [ ] No unexpected SPI commands between channel hop and payload write
