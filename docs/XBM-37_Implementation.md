# XBM-37 Protocol Implementation

## Executive Summary

This document records the current reverse-engineering status for the **T-Smart XBM-37** quadcopter protocol and explains why the implementation was added as an **`FQ777` subprotocol** instead of creating a brand-new top-level protocol.

**Status:** ⚠️ **Partially decoded / implemented for initial testing**

Current implementation goals:
- preserve what has already been learned from the stock transmitter captures
- explain the reasoning behind the current firmware integration
- provide a running log that can be extended in future commits as flight testing confirms or corrects behavior

---

## 1. Capture Set Used

The reverse engineering work used the capture files in:

- `Captures_XBM-37/01b-XBM-37_Quad_TX-PowerOn-NoRX.csv`
- `Captures_XBM-37/02b-XBM-37_Quad_TX-PowerOn-withRX-Bind.csv`
- `Captures_XBM-37/03b-XBM-37_Quad_Aileron-Center-Left-Center-Right-Center.csv`
- `Captures_XBM-37/04b-XBM-37_Quad_Elevator-Center-Back-Center-Forward-Center.csv`
- `Captures_XBM-37/05b-XBM-37_Quad_Throtle-Low-High-Low.csv`
- `Captures_XBM-37/06b-XBM-37_Quad_Rudder-Center-Left-Center-Right-Center.csv`
- `Captures_XBM-37/07b-XBM-37_Quad_RateModeSwitch-1-2-3.csv`
- `Captures_XBM-37/08b-XBM-37_Quad_FlipSwitch-PushButton_Off-On.csv`
- `Captures_XBM-37/09b-XBM-37_Quad_VideoSwitch-Off-On-Off-On.csv`
- `Captures_XBM-37/10b-XBM-37_Quad_PictureSwitch-PushButton-3X.csv`
- `Captures_XBM-37/11b-XBM-37_Quad_HeadlessSwitch-PushButton-Off-On.csv`
- `Captures_XBM-37/12b-XBM-37_Quad_ReturnToHomeSwitch-PushButton-Off-On.csv`
- `Captures_XBM-37/13b-XBM-37_Quad_LED-LightsSwitch-PushButton-On-Off.csv`
- `Captures_XBM-37/14b-XBM-37_Quad_OK-Switch-PushButton-Off-On.csv`

These captures were enough to identify the bind packet, hop sequence, stick byte locations, several feature bits, and the basic packet timing.

---

## 2. Why the Protocol Was Added Under FQ777

The original task was to first look for a similar existing protocol before creating a new one.

The XBM-37 captures matched the existing `FQ777` family more closely than `HONTAI` or other NRF24L01 paths for these reasons:

- **8-byte packet size**
- **4-channel hopping**
- **bind packet carries 3 address bytes plus a checksum**
- **~2ms transmit period**
- **plain nRF24L01+ style operation rather than XN297-scrambled traffic**

Because `FQ777` already existed and did not use all available subtype slots, the XBM-37 support was added as:

- `PROTO_FQ777`
- subprotocol `XBM-37`

This keeps the protocol list smaller and reuses an already similar transport structure.

---

## 3. Current Firmware Integration

### 3.1 Files Updated

- `Multiprotocol/FQ777_nrf24l01.ino`
- `Multiprotocol/Multi_Protos.ino`
- `Multiprotocol/Multiprotocol.h`
- `Protocols_Details.md`

### 3.2 Added Subprotocol

The XBM-37 support is currently exposed as:

- **Protocol:** `FQ777`
- **Subtype:** `XBM-37`

This means existing UI / model selection continues to use the FQ777 protocol entry while allowing a dedicated XBM-37 packet format.

---

## 4. Decoded RF Behavior

### 4.1 RF Chip / Air Format

Observed characteristics from the captures:

- **RF IC family:** nRF24L01+ compatible
- **Payload length:** 8 bytes
- **Dynamic payload enabled**
- **Bitrate:** 2Mbps (SV7241A/BK2425 RF_SETUP=0x26 bit5=RF_DR=1; nRF24L01+ uses RF_SETUP=0x07 bit0=RF_DR_HIGH=1)
- **Auto-ack:** disabled

### 4.2 Hop Sequence

Observed hop sequence:

```text
49, 34, 26, 07
```

The successful `02b` bind capture uses this same 4-channel hop sequence — but with one critical detail:

- **Bind packet #1** is sent on channel **0x00** (the universal bind-announce channel that the XBM-37 RX listens on at boot when in bind mode).
- **Bind packets #2 through #400** then cycle through 0x49 → 0x34 → 0x26 → 0x07, repeating.
- **Data packets** (after bind) continue cycling from where the bind sequence left off (0x07 → 0x49 → ...).

If the first bind packet is sent on 0x49 instead of 0x00, the receiver never sees the announce packet and bind fails.

Per-packet SPI cycle observed in capture:
```text
W_STATUS=0x70 → FLUSH_TX → W_RF_CH → W_TX_PAYLOAD
```

### 4.3 Packet Timing

Observed packet cadence is approximately:

```text
~2070µs
```

Deep comparison of `02a` (digital CE/SCK/CSN) and `02b` (decoded SPI) from the same bind capture shows this timing is intentionally non-uniform at startup and at bind transition:

- Packet #1 → #2: ~922µs
- Packet #2 → #3: ~1097µs
- Packet #3 onward (bind steady state): ~2070µs
- Packet #400 (last bind) → #401 (first data): ~17361µs pause
- Packet #401 → #402: ~918µs
- Packet #402 → #403: ~916µs
- Packet #403 → #404: ~1491µs
- Packet #404 onward (data steady state): ~2070µs

CE pulses in the digital capture also align with this sequence, with most packet transmit windows holding CE high for roughly ~1.17ms before returning low for the inter-packet gap.

### 4.4 Bind Addressing

The captured stock transmitter used:

```text
E7 E7 E7 E7 67
```

for the initial bind address, then moved to a captured data address pattern including:

```text
91 05 05 E7 67
```

The current implementation uses those captured bytes directly for the initial XBM-37 subtype work.
In `02b`, the transmitter switches `TX_ADDR` to `91 05 05 E7 67` immediately after the 400th bind payload and before the first data payload.

---

## 5. Decoded Packet Behavior

### 5.1 Bind Packet

The captured bind packet format is currently treated as:

```text
20 14 07 03 aa bb cc sum
```

Where:

- `aa bb cc` are the XBM-37 address bytes
- `sum` is the checksum of the address bytes (`4..6`)

### 5.2 Data Packet

The current XBM-37 data packet implementation uses:

```text
[0] throttle
[1] rudder
[2] aileron
[3] elevator
[4] fixed base byte (`0x20` in the successful `02b` bind capture; `0x21` in later control captures)
[5] fixed base byte (`0x20`) + OK bit
[6] rate/feature flags
[7] checksum
```

### 5.3 Checksum

The checksum currently matches the observed stock behavior:

- bind packet: byte `7` = sum of bytes `4..6`
- data packet: byte `7` = sum of bytes `0..6`

---

## 6. Current Channel / Feature Mapping

Based on the captures reviewed so far, the current mapping is:

| CH | Function | Status |
| --- | --- | --- |
| 1 | Aileron | decoded |
| 2 | Elevator | decoded |
| 3 | Throttle | decoded |
| 4 | Rudder | decoded |
| 5 | Rate | decoded from `07b` |
| 6 | Flip | decoded from `08b` |
| 7 | Picture | decoded from `10b` |
| 8 | Video | decoded from `09b` |
| 9 | Headless | decoded from `11b` |
| 10 | Unused | not assigned |
| 11 | LED | decoded from `13b` |
| 12 | OK | separate bit observed in `14b`; purpose still unknown |

### Notes

- `07b` showed a 3-state rate value change in the flags byte.
- `08b` showed the flip bit.
- `09b` showed the video bit.
- `10b` showed the picture bit.
- `11b` showed the headless bit.
- `13b` showed the LED bit.
- `14b` showed a change in byte 5, which is currently mapped to the **OK** switch.
- The **OK** button does **not** match the currently decoded rate / flip / picture / video / headless / LED flags. It appears as its own separate bit by changing byte `5` from `0x20` to `0xA0` while the other feature flags stay unchanged.

---

## 7. What Is Still Uncertain

The following items are **not yet fully confirmed** and should be revisited during live testing:

1. **RTH mapping**
   - `12b` still does not show a clear packet-byte change in the currently decoded 8-byte payload.
   - No separate RTH channel/bit is assigned in the implementation yet.
   - Flight testing now suggests the stock transmitter's RTH button behaves more like a **reverse-from-bind-heading** command than a true return-to-home function.
   - The behavior can reportedly be canceled by pushing forward elevator / pitch.

2. **Exact stick scaling**
   - The current implementation follows the captured endpoints and direction assumptions, but live testing may require inversion or endpoint refinement.

3. **Meaning of fixed bytes**
   - Data bytes `4` and `5` include fixed base values observed in the stock traffic.
   - The successful `02b` bind capture keeps byte `4` at `0x20` through the first post-bind data packets, while later control captures show `0x21`.
   - Their full meaning is not yet known beyond the confirmed OK-button effect in byte `5`.
   - The OK button's operational purpose is still unknown; it may be a calibration or mode command, but this has not been confirmed.

4. **Any hidden bind/model-match logic**
   - The current implementation is based on the captured address behavior and may need refinement if additional transmitters or receiver variants are tested.

---

## 8. Testing Updates

### 2026-05-07

- Additional live testing suggests the **OK** button is **separate from the other decoded feature flags**.
- In the stock transmitter capture, pressing **OK** changes byte `5` from `0x20` to `0xA0`, while byte `6` stays unchanged.
- This means **OK does not replicate flip, picture, video, headless, LED, or rate** in the currently decoded payload.
- The actual flight purpose of **OK** is still unknown. A calibration-style function is possible, but not yet verified.
- Additional live testing suggests the **RTH** button is not a normal position-aware return-to-home mode.
- Reported behavior is that the model flies back in the **opposite direction from the original bind orientation**, and pushing forward elevator / pitch cancels it.
- The existing `12b` capture still does **not** show a clean separate payload-byte change for RTH, so the command may depend on a state or encoding detail that has not been isolated yet.

---

## 9. Why This Document Exists

This file is intended to be a **living implementation log** for the XBM-37 work.

It should be updated in future commits whenever:

- a control is confirmed working on real hardware
- a bit mapping is corrected
- a checksum/address rule changes
- bind behavior is improved
- a feature is added or intentionally left unsupported

Suggested future update pattern:

- add a short dated note in a new section such as **Testing Updates**
- record what was tested
- record what changed in the code
- record what is now confirmed vs still uncertain

---

## 10. Suggested Future Sections

As XBM-37 support matures, extend this document with:

- **Testing Updates**
- **Known Working Features**
- **Known Non-Working Features**
- **Open Questions**
- **Packet Examples**
- **Receiver / board photos**

---

*Document updated: May 7, 2026*  
*Repository: MRC3742/DIY-Multiprotocol-TX-Module*
