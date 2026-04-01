# CG022 LT8910 Bind Capture Summary (02/52)

This summary consolidates the LT8910 PHY configuration and bind-flow timing derived from the successful bind captures:

- **02b_1-CG022_TX-PowerOn-withRX-Bind.logicdata.csv** (original TX LT8910, successful bind to RX)
- **52b-CG022_RX-PowerOn-withTX-Bind.csv** (model RX LT8910, successful bind to original TX)

All LT8910 SPI decode uses **CPHA=1** (mode 1). Each SPI packet is 3 bytes: `REG`, `MSB`, `LSB`.

## Bind payload comparison

Bind payload bytes (LT8910 TX `02b_1` payload decoded from `0x32` writes after the `0x00 0x00` marker vs NRF24 `21b` `0xA0` payloads):

| Source | Payload bytes |
| --- | --- |
| LT8910 `02b_1` | `0x0A 00 11 22 33 06 AB FC AD 00` |
| NRF24 `21b` (`0xA0`) | `0x0A 00 11 22 33 06 AB FC AD 00` |

## Channel hop comparison

LT8910 `02b_1` uses `0x07 00 <CH>` then `0x07 01 <CH>` per hop; NRF24 `21b` writes `RF_CH` via command `0x25`:

| Source | Hop sequence (one cycle) |
| --- | --- |
| LT8910 `02b_1` | `0x00 0x28 0x0A 0x32 0x14 0x3C 0x1E 0x46` |
| NRF24 `21b` (`0x25`) | `0x0A 0x32 0x14 0x3C 0x1E 0x46 0x00 0x28` |

(Same 8-channel set; LT8910 trace is phase-shifted because it begins with the `0x00/0x28` prelude.)

## LT8910 PHY configuration (init writes)

First write to each register before the first payload marker (`0x32 00 00`):

| Reg | TX init (02b_1) | RX init (52b) |
| --- | --- | --- |
| 0x00 | 0x6F 0xE0 | 0x6F 0xF0 |
| 0x01 | 0x56 0x81 | 0x16 0x81 |
| 0x02 | 0x66 0x17 | 0x66 0x17 |
| 0x04 | 0x9C 0xC9 | — |
| 0x05 | 0x66 0x37 | 0x66 0x37 |
| 0x07 | 0x00 0x00 | 0x00 0x00 |
| 0x08 | 0x6C 0x90 | 0x6C 0x10 |
| 0x09 | 0x48 0x00 | 0x48 0x00 |
| 0x0A | 0x7F 0xFD | — |
| 0x0B | 0x00 0x08 | 0x7F 0xFD |
| 0x0C | 0x00 0x00 | 0x00 0x00 |
| 0x0D | 0x48 0xBD | 0x40 0xBD |
| 0x16 | 0x00 0xFF | — |
| 0x17 | 0x80 0x05 | 0x00 0xFF |
| 0x18 | 0x00 0x67 | 0x00 0x67 |
| 0x19 | 0x16 0x59 | 0x16 0x59 |
| 0x1A | 0x19 0xE0 | — |
| 0x1B | 0x13 0x00 | 0x19 0xF0 |
| 0x1C | 0x18 0x00 | 0x18 0x00 |
| 0x20 | 0x48 0x00 | 0x40 0x00 |
| 0x21 | 0x3F 0xC7 | 0x3F 0xC7 |
| 0x22 | 0x20 0x00 | — |
| 0x23 | 0x03 0x00 | 0x03 0x00 |
| 0x24 | 0x22 0x11 | — |
| 0x25 | 0x06 0x8C | 0x06 0x8C |
| 0x26 | 0x5A 0x5A | 0x5A 0x4A |
| 0x27 | 0x00 0x33 | 0x00 0x33 |
| 0x28 | 0x44 0x02 | 0x44 0x01 |
| 0x29 | 0xB0 0x00 | — |
| 0x2A | 0xFD 0xB0 | 0xFD 0xB0 |
| 0x2B | 0x00 0x0F | — |
| 0x32 | 0x00 0x00 | 0x00 0x00 |

RX pre-payload updates observed in `52b` (order in capture): `0x00 9C C9`, `0x0B 00 08`, `0x17 C0 05`, `0x1B 13 00`, `0x02 20 00`, `0x20 22 11`, `0x28 B0 00`, `0x23 00 0F`.

## Bind flow chart (with timestamps)

Timestamps are taken directly from the `Time [s]` column of the captures.

| Step | TX time (02b_1) | RX time (52b) | Details |
| --- | --- | --- | --- |
| First SPI write | 0.567179875 | 0.000002125 | `0x00 6F E0` (TX), `0x00 6F F0` (RX) |
| Last init write before payload marker | 0.574002625 | 0.001673625 | `0x2B 00 0F` (TX), `0x23 00 0F` (RX) |
| Payload marker | 0.574229375 | 0.001729375 | `0x32 00 00` |
| Payload latch | 0.586422125 | — | `0x34 80 80` |
| Payload bytes start | 0.586652750 | — | `0x32 0A 00` |
| Payload bytes end | 0.587564000 | — | `0x32 AD 00` |
| Hop loop start | 0.587793625 | — | First `0x07 01 <CH>` after payload |
| First status read after payload | — | 0.012917125 | First `0xA2/0xA8/0xC7/0xB0/0xF0/0x87` read |
| First scan write after payload | — | 0.027937875 | `0x07 00 AA` |
| First base hop channel scan | — | 0.059615750 | `0x03 00 28` |

### TX hop channel timing (first cycle)

First occurrence of each hop channel in the TX loop (`0x07 00 <CH>` after payload marker):

| Channel | Time [s] |
| --- | --- |
| 0x00 | 0.604439500 |
| 0x28 | 0.588272000 |
| 0x0A | 0.590579750 |
| 0x32 | 0.592888000 |
| 0x14 | 0.595197250 |
| 0x3C | 0.597507500 |
| 0x1E | 0.599821625 |
| 0x46 | 0.602134250 |

## RX scan values

Channel scan writes on `0x03` and `0x07` include the hop set `00 28 0A 32 14 3C 1E 46`, extra scan values `06 08 16 30`, plus offset values `80 88 8A 94 96 9E A8 B0 B2 C6 CA D4 DE E8`.

## PHY traceability checklist († requires SDR/sniffer capture)

- Confirm LT8910 SPI readback works (MISO not stuck at `0xFF`) so register values are trustworthy.
- Record LT8910 register config for data rate/modulation, preamble + sync, CRC width/polynomial, whitening seed, and address width; compare to NRF24 feature set.
- Verify packet timing (bind burst length, dwell time, inter-packet spacing) with logic analyzer captures.
- † Compare on-air preamble/sync/whitening/CRC behavior against NRF24 output.
- † Validate actual RF center frequency and deviation during a hop cycle.
- If you do not have SDR/sniffer hardware, the next step is confirming LT8910 register readback and checking those settings fall within NRF24 capabilities; SDR is then the remaining proof point for PHY-level equivalence.
- Status (no SDR): LT8910 register readback is not confirmed in the MPM traces yet; resolve any `0xFF` readback before mapping the PHY fields.
- What `0xFF` readback means: MISO is staying high/undriven during read transactions, so the LT8910 is not responding and the returned register bytes are not trustworthy.
- What is required to fix readback:
  - Ensure the LT8910 is powered and has a reset pulse (RET low → high) before SPI reads.
  - Confirm the MISO wiring path is correct and not blocked by bus contention (only the LT8910 should drive MISO during its reads).
  - Match the stock SPI mode (CPHA=1, as noted above) and keep the SPI clock conservative if reads are still `0xFF`.
- Scope clarification: this readback refers to SPI register reads from an LT8910 device (original TX or RX board) captured with a logic analyzer; NRF24 emulation has no LT8910 to read back.
- If you already have successful-bind captures from the model’s RX (e.g., files 52+), those are valid inputs as long as they include LT8910 register writes/reads; they do not require the MPM to bind.
- NRF24 capability reference for the mapping: GFSK modulation, data rate 250 kbps/1 Mbps/2 Mbps, CRC 1 or 2 bytes, address width 3–5 bytes, payload 1–32 bytes, built-in data whitening (not user-disableable).
