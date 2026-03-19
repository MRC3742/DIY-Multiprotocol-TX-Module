# CG022 LT8900 Emulation CRC Analysis

## Summary

Analysis of TX captures (01-11) and RX captures (51-53) identified a CRC byte
ordering bug in the LT8900/LT8910 emulation layer (`NRF24l01_SPI.ino`) that
prevents the MPM module from binding with the CG022 receiver.

## Key Evidence

### Capture 52b: Stock TX → Stock RX (SUCCESS)
- **1690 FIFO reads** observed - receiver accepted many packets
- First accepted packet at t=2.934s with FIFO data matching bind format
- Register 0x38 status shows PKT_FLAG (bit 6) set, confirming valid reception
- After bind, data packets received with centered-stick idle values

### Capture 53b: MPM ForceID → Stock RX (FAILURE)  
- **ZERO FIFO reads** - receiver never accepted ANY packet from MPM
- LT8910 correlator may have detected sync words but CRC check rejected all
- No PKT_FLAG assertion observed during MPM transmission window

### Capture 51b: No TX present (BASELINE)
- Receiver scans channels indefinitely with no FIFO reads (expected)

## Register Configuration Comparison

| Register | TX (01b/02b) | RX (51b/52b) | Description |
|----------|-------------|--------------|-------------|
| R17      | 0x8005      | 0xC005       | CRC polynomial (bit 14 differs) |
| R20      | 0x4800      | 0x4C00       | Preamble=3, Trailer=8(TX)/12(RX) |
| R24      | 0x2211      | 0x2211       | Bind sync word (MATCH) |
| R28      | 0x4402      | 0x4401       | CRC initial value (differs) |
| R09      | 0x4800      | 0x6800       | PA/power config (differs) |

Note: TX and RX register differences in R17/R20/R28 are normal — the TX and RX
sides can use different configurations since the CRC polynomial/init control
only the local computation and the receiver detects/validates independently.
The MPM emulates the TX side, so TX register values are the correct reference.

## Root Cause: CRC Byte Bit-Reversal

### LT8900/LT8910 OTA Format
The LT8910 sends ALL bytes **LSBit-first** on the air:
- Preamble: alternating bits (3 bytes)
- Sync word: register R24 value sent LSBit-first per byte
- Trailer: alternating bits (8 bits for TX)
- **Data**: each byte sent LSBit-first
- **CRC**: each byte sent LSBit-first

### NRF24L01 Emulation
The NRF24L01 sends ALL bytes **MSBit-first**. To produce the correct OTA
format, the emulation layer bit-reverses each data byte before placing it
in the NRF24L01 payload buffer:

```
Data byte 0x0A (00001010) → bit_reverse → 0x50 (01010000)
NRF sends 0x50 MSBit-first → air bits: 01010000
LT8910 would send 0x0A LSBit-first → air bits: 01010000 ✓ (MATCH)
```

### The Bug
The CRC bytes were **NOT** bit-reversed in the emulation, unlike data bytes:

```
CRC high byte 0x46 → NOT reversed → NRF sends 0x46 MSBit-first → air: 01000110
LT8910 would send 0x46 LSBit-first → air: 01100010 ✗ (MISMATCH!)
```

This caused every packet's CRC to fail validation at the receiver, explaining
the zero FIFO reads in capture 53b.

### The Fix
Bit-reverse CRC bytes in both `LT8900_WritePayload` and `LT8900_ReadPayload`:

```c
// WritePayload: bit-reverse CRC bytes for LSBit-first OTA
buffer[pos++] = bit_reverse(crc >> 8);
buffer[pos++] = bit_reverse(crc);

// ReadPayload: reverse received CRC bytes back before comparing
if(bit_reverse(buffer[pos++]) != ((crc>>8)&0xFF)) return 0;
if(bit_reverse(buffer[pos]) != (crc&0xFF)) return 0;
```

## TX Capture Analysis Summary

### Bind Phase (captures 01b, 02b)
- TX ID: `11 22 33 06 AB` (5 bytes from rx_tx_addr[0..4])
- Bind FIFO data: `0A 00 11 22 33 06 AB FC AD 00` (10 bytes)
- Bind sync word R24 = 0x2211
- Bind count: 166 packets (~0.38s)
- Channel hopping: 0, 40, 10, 50, 20, 60, 30, 70
- Packet period: ~2310µs per channel hop

### Data Phase (after bind)
- Sync word changes: R24 = 0xAB06 (derived from TX ID bytes [4],[3])
- R27 also changes: 0x0033 → 0x00FC
- First data FIFO: `0A 00 00 20 20 20 20 20 20 C0` (idle sticks)
- Channel hopping continues without reset

### Register R27 Change
At bind→data transition, the TX writes R27 = 0x00FC (was 0x0033 during bind).
This register may control preamble detection threshold or other RF parameters.
The current code does not replicate this R27 change — this could be a secondary
issue if the CRC fix alone doesn't fully resolve binding.
