# SPI Capture Comparison Charts

## Chart 1 — TX Comparison: 02b (Stock TX, Successful Bind) vs 61b (MPM TX, No Bind)

File 02b captures the **stock CG022 transmitter** (LT8900 chip) SPI bus during a
successful bind with the stock receiver.  File 61b captures the **MPM module**
(STM32 → NRF24L01) SPI bus running the CG022 protocol with ForceID, which
**does not** bind.

> The two transmitters use different RF chips (LT8900 vs NRF24L01), so the SPI
> commands differ.  What matters for binding is the **over-the-air (OTA)** signal
> they produce.  This chart compares the OTA-relevant parameters.

### 1.1 — Initialization / Register Configuration

| Parameter | 02b Stock TX (LT8900) | 61b MPM TX (NRF24L01) | Match? | Notes |
|-----------|----------------------|----------------------|--------|-------|
| **Chip** | LT8900 native | NRF24L01 emulating LT8900 | — | Different SPI protocols |
| **Data Rate** | 1 Mbps (R00 default) | 1 Mbps (RF_SETUP=0x01 → bit 3=0) | ✅ | Both 1 Mbps |
| **TX Power** | R09=0x4800 (normal) | RF_SETUP=0x01 (−18 dBm, lowest!) | ⚠️ **LOW** | MPM uses minimum power during bind |
| **CRC (chip level)** | LT8900 CRC enabled (R17=0x8005, R28=0x4402) | NRF CRC disabled (CONFIG=0x02) | ✅ | MPM computes CRC in software and appends to payload — correct approach |
| **Auto-Ack** | N/A (LT8900 has no auto-ack) | EN_AA=0x00 (disabled) | ✅ | Correct |
| **Retransmit** | N/A | SETUP_RETR=0x00 (none) | ✅ | Correct |
| **Address Width** | 5 bytes (LT8900 R23) | SETUP_AW=0x03 (5 bytes) | ✅ | Correct |
| **Dynamic Payload** | N/A | DYNPD=0x00, FEATURE=0x01 | ✅ | No dynamic payload, feature bit for W_TX_PAYLOAD_NOACK |
| **Startup Delay** | Power-on → first TX: ~19 ms | Power-on → first TX: ~1427 ms | ⚠️ **SLOW** | MPM has 1.3 s serial/module init before protocol starts |

### 1.2 — Bind Address / Sync Word

| Parameter | 02b Stock TX (LT8900) | 61b MPM TX (NRF24L01) | Match? |
|-----------|----------------------|----------------------|--------|
| **Bind Sync Word** | R24 = 0x2211 | NRF addr = [AA 88 44 55 55] (= bit-reversed 0x2211 + LT8900 framing) | ✅ |
| **TX_ADDR** | (set by sync word R24) | [AA 88 44 55 55] | ✅ |
| **RX_ADDR_P0** | (same as sync word) | [AA 88 44 55 55] (= TX_ADDR) | ✅ |

### 1.3 — RF Channel Hopping

| Hop Index | 02b Stock (LT8900 reg) | 02b Frequency | 61b MPM (NRF RF_CH) | 61b Frequency | Match? |
|-----------|------------------------|---------------|---------------------|---------------|--------|
| 0 | 0x00 | 2402 MHz | 0x02 | 2402 MHz | ✅ |
| 1 | 0x28 (40) | 2442 MHz | 0x2A (42) | 2442 MHz | ✅ |
| 2 | 0x0A (10) | 2412 MHz | 0x0C (12) | 2412 MHz | ✅ |
| 3 | 0x32 (50) | 2452 MHz | 0x34 (52) | 2452 MHz | ✅ |
| 4 | 0x14 (20) | 2422 MHz | 0x16 (22) | 2422 MHz | ✅ |
| 5 | 0x3C (60) | 2462 MHz | 0x3E (62) | 2462 MHz | ✅ |
| 6 | 0x1E (30) | 2432 MHz | 0x20 (32) | 2432 MHz | ✅ |
| 7 | 0x46 (70) | 2472 MHz | 0x48 (72) | 2472 MHz | ✅ |

> LT8900 channels map to NRF channels via `NRF_CH = LT8900_CH + 2`.  All 8
> channels match.

### 1.4 — Bind Packet Payload

| Parameter | 02b Stock TX | 61b MPM TX | Match? |
|-----------|-------------|-----------|--------|
| **Payload size** | 10 data + LT8900 hardware CRC | 12 NRF bytes (10 data + 2 software CRC) | ✅ |
| **Byte order** | LT8900 sends LSBit-first natively | NRF sends MSBit-first; all bytes bit-reversed | ✅ |
| **Bind data (LT8900 domain)** | `0A 00 11 22 33 06 AB FC AD 00` | Bit-reverse of `[50 00 88 44 CC 60 D5 3F B5 00]` = `0A 00 11 22 33 06 AB FC AD 00` | ✅ |
| **CRC-16 (LT8900 domain)** | Hardware: poly=0x8005, init=0x4402 | Software: `[62 C6]` → bit-reverse → `[46 63]` ← CRC value | ✅ |
| **Packet sent twice per channel** | Yes (R21 retransmit) | Yes (STATUS polling loop) | ✅ |

### 1.5 — Bind Timing & Duration

| Parameter | 02b Stock TX | 61b MPM TX | Match? | Action Needed |
|-----------|-------------|-----------|--------|---------------|
| **Bind packet count** | 166 packets | ~853 packets (1706 payloads ÷ 2 per channel) | ❌ | MPM sends ~5× more bind packets; not harmful but different |
| **Bind duration** | ~384 ms | ~1960 ms (~2.0 s) | ❌ | MPM bind window is ~5× longer |
| **Per-channel dwell** | ~2.31 ms (2 packets) | ~2.31 ms (2 packets) | ✅ | Timing per hop matches |
| **Bind-to-data transition** | Sync word R24: 0x2211 → 0xAB06 | Address changes at ~3.42 s | ✅ | Both transition to data mode |
| **Data mode sync word** | R24 = 0xAB06 | New NRF address (derived from TX ID) | ✅ | Correct |

### 1.6 — OTA Framing (NRF24L01 vs LT8900)

This is the **critical difference** — the NRF24L01 and LT8900 produce different
over-the-air framing even when the payload data is identical:

| OTA Element | LT8900 (02b) | NRF24L01 (61b) | Match? | Action Needed |
|-------------|-------------|----------------|--------|---------------|
| **Preamble** | 3 bytes alternating (R20 bits 15:8 = 0x48 → 3 bytes) | 1 byte (0xAA or 0x55, NRF hardware) | ❌ **MISMATCH** | NRF preamble is shorter; LT8900 RX expects 3 bytes |
| **Sync Word** | 2 bytes (R24), sent LSBit-first | 5-byte NRF address encodes sync + framing bits | ⚠️ | Address bytes selected to reproduce LT8900 sync on-air |
| **Trailer** | 8 bits (R20 bits 7:2 = trailer length) | No trailer concept in NRF24L01 | ❌ **MISMATCH** | Trailer bits must be encoded into the NRF payload |
| **Data bits** | LSBit-first | MSBit-first (bit-reversed by firmware) | ✅ | Handled correctly |
| **CRC** | 2 bytes, LSBit-first by hardware | 2 bytes, bit-reversed by firmware, NRF CRC disabled | ✅ | Handled correctly |

### 1.7 — Summary of Differences to Address

| # | Issue | Severity | Status |
|---|-------|----------|--------|
| 1 | **OTA preamble length mismatch** — NRF sends 1 byte, LT8900 expects 3 bytes | 🔴 Critical | Open — framing experiments (captures 72–98) show progress but not solved |
| 2 | **OTA trailer bits missing** — LT8900 expects 8 trailer bits after sync word | 🔴 Critical | Open — trailer length experiments ongoing |
| 3 | **TX power too low during bind** — RF_SETUP=0x01 = −18 dBm (minimum) | 🟡 Medium | Check if RF_SETUP increases before TX; data mode uses 0x07 (0 dBm) |
| 4 | **Bind count is different** (853 vs 166) | 🟢 Low | Not harmful — more packets gives RX more chances |
| 5 | **Startup delay** (~1.4 s vs ~19 ms) | 🟢 Low | MPM serial init overhead; RX can wait |

---

## Chart 2 — RX Comparison: 52b (RX + Stock TX, Bind OK) vs 60b (RX + MPM TX, No Bind)

Both files capture the **same CG022 receiver** (LT8910 chip) SPI bus.  The
difference is the transmitter: stock LT8900 TX (02b, binds) vs MPM NRF24L01 TX
(61b, does not bind).

### 2.1 — Overall Activity

| Metric | 52b (Stock TX → Bind OK) | 60b (MPM TX → No Bind) | Ratio |
|--------|-------------------------|------------------------|-------|
| **Total SPI bytes** | 33,603 | 13,131 | 2.6× |
| **Capture duration** | ~6.26 s | ~6.33 s | ≈same |
| **Unique Packet IDs** | 11,201 | 1 (all ID=0) | — |
| **FIFO reads (MOSI=0xB2)** | **1,707** | **16** | **107×** |
| **FIFO status (MOSI=0xB3)** | 266 | 4 | 67× |
| **Register polls** | Normal + FIFO activity | Almost all polling, no FIFO | — |

### 2.2 — RX Startup Phase (Both Files)

| Phase | 52b | 60b | Match? |
|-------|-----|-----|--------|
| **Power-on init** | LT8910 register writes (R00–R32) | Same register writes | ✅ |
| **Search/poll loop** | ~1.95 ms cycle (status reads) | ~1.95 ms cycle (status reads) | ✅ |
| **Init complete at** | ~0.16 s | ~0.17 s | ✅ |

Both receivers start identically — the difference emerges only when the TX
signal is (or isn't) received.

### 2.3 — Packet Reception Events

| Event | 52b (Stock TX) | 60b (MPM TX) | Notes |
|-------|---------------|-------------|-------|
| **First FIFO read (0xB2)** | t ≈ 0.378 s (Packet ID 278) | t ≈ 0.320 s (sporadic) | 60b has occasional 0xB2 but no sustained reads |
| **Sustained FIFO drain** | Yes — 1,707 reads over ~5.88 s | **Never** — only 16 scattered reads | RX never accepts MPM packets |
| **PKT_FLAG assertion** | Yes — short pulses indicating valid CRC | **Never observed** | LT8910 correlator rejects MPM frames |
| **FIFO payload data** | Real payload bytes (0x12, 0x20, etc.) | Only status bytes (0x00, 0x01) | No actual data recovered |
| **State transition to bound** | Yes — at ~0.378 s, new register writes appear | **Never** | RX stays in search mode |

### 2.4 — Receiver State Machine Comparison

| State | 52b Behavior | 60b Behavior |
|-------|-------------|-------------|
| **1. Search** | Polls status registers every ~1.95 ms | Same polling pattern |
| **2. Sync detect** | LT8910 correlator fires → PKT_FLAG → FIFO fills | Correlator **never** fires (or fires but CRC fails) |
| **3. FIFO drain** | Firmware reads 0xB2 repeatedly, extracts payload | **Never reached** — no valid packets to read |
| **4. Bind accept** | Bind data extracted, sync word updated, enters data mode | **Never reached** |
| **5. Data receive** | Continuous 0xB2 reads at channel-hop rate | **Never reached** — stays in state 1 forever |

### 2.5 — Why the Receiver Rejects MPM Packets

The 60b capture confirms the receiver **does not detect valid packets** from the
MPM transmitter.  Based on the TX comparison (Chart 1), the most likely causes
are:

| # | Cause | Evidence from 60b |
|---|-------|-------------------|
| 1 | **Preamble too short** — NRF sends 1 byte, LT8910 expects 3 bytes to lock its bit synchronizer | RX never asserts PKT_FLAG → correlator doesn't lock |
| 2 | **Missing trailer bits** — LT8910 expects 8 trailer bits between sync word and data | Even if sync is detected, data alignment would be wrong |
| 3 | **Frame alignment** — without correct preamble + trailer, the LT8910's CRC computation starts at the wrong bit position | The 16 sporadic 0xB2 reads may be noise-triggered, not real packets |

### 2.6 — Key Diagnostic Indicators

| Indicator | 52b (Success) | 60b (Failure) | What It Means |
|-----------|--------------|--------------|---------------|
| FIFO reads > 100 | ✅ 1,707 | ❌ 16 | Receiver is/isn't accepting packets |
| Sustained 0xB2 bursts | ✅ Yes | ❌ No | Receiver is/isn't in bound state |
| Register writes after init | ✅ Sync word change | ❌ None | Receiver did/didn't extract bind data |
| MISO contains payload data | ✅ Various bytes | ❌ Only 0x00/0x01 | Real/no data in FIFO |

---

## Overall Conclusion

The MPM CG022 protocol implementation (61b) has **correct**:
- ✅ Bind address / sync word
- ✅ Channel hopping sequence and frequencies
- ✅ Payload content (bind data, bit-reversal, CRC)
- ✅ Per-channel timing (~2.31 ms dwell)

The MPM CG022 protocol implementation has **incorrect or unresolved**:
- ❌ **OTA preamble** — NRF24L01 sends 1-byte preamble vs LT8900's 3-byte preamble
- ❌ **OTA trailer** — LT8900 inserts 8 trailer bits that NRF24L01 does not produce
- ⚠️ **TX power during bind** — RF_SETUP=0x01 is minimum power (−18 dBm)

The **preamble/trailer framing mismatch** is the primary reason the LT8910
receiver (60b) never detects valid packets from the MPM transmitter (61b).
This is confirmed by the ongoing framing experiments (captures 72–98) which
show that adjusting preamble and trailer lengths in the NRF24L01 payload
progressively improves RX behavior but has not yet achieved a complete bind.
