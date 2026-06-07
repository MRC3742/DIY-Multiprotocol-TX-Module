# CG022 vs SHENQI (LT89xx over NRF24L01) — What Matches and What Must Change

## Scope
This comparison is based on:
- CG022 TX captures starting from `02b` (plus `22b/32b/42b` for bind-ID comparison) in `/Captures_CG022`
- Current implementation: `/Multiprotocol/SHENQI_nrf24l01.ino`

---

## What is the same

1. **Both are LT89xx-family style protocols tunneled through NRF24L01 in MPM**
   - SHENQI uses the LT8900 emulation helpers (`LT8900_Config/SetAddress/SetChannel/WritePayload`).
   - CG022 captures show LT89xx register/FIFO style operation (channel register writes, FIFO control, FIFO words, TX-on trigger).

2. **Both are autobind-style at startup**
   - SHENQI starts in bind state (`BIND_IN_PROGRESS`).
   - CG022 starts with repeated bind packets immediately after init.

3. **Both use channel hopping and short packet periods**
   - SHENQI: 60-hop table, ~1750us data period.
   - CG022 (`02b`): 8-hop sequence with ~2309us mean frame period.

---

## Critical differences seen in `02b` captures

## 1) RF/init configuration is completely different
SHENQI does a small generic LT8900 setup:
- `LT8900_Config(4, 8, CRC_ON|PACKET_LENGTH_EN, 0xAA)`
- address init to `9A 9A 9A 9A`
- bind channel fixed at 2

CG022 in `02b` writes a **full 33-register init block** before packeting:
- Packet IDs 0..30: register writes from `00 6F E0` through `2B 00 0F`
- Packet ID 31: `32 00 00` (FIFO clear)
- Packet ID 32: `A8 FF FF` (CRC seed read)

So SHENQI init path is not sufficient for CG022; CG022 requires the stock init register set and sequence.

## 2) Bind model is TX-timed, not RX-response driven
SHENQI bind completion depends on receiving an RX payload:
- waits in RX mode, reads 3-byte packet, then sets `BIND_DONE`.

CG022 `02b` behavior:
- TX sends bind payload repeatedly for a fixed window, then transitions by itself.
- Same bind duration with and without RX (`01b` and `02b`): **165 bind frames**.
- Therefore CG022 bind handling should be timer/counter driven, not `LT8900_ReadPayload()` driven.

## 3) Packet size and content are very different
SHENQI payload is 3 bytes:
- bind: `[00, rx2, rx3]`
- data: `[00, rudder, throttle]`

CG022 payload is 10 bytes (`0A ...`), carried as 5 FIFO words per frame:
- bind payload in `02b`: `0A 00 11 22 33 06 AB FC AD 00`
- default post-bind payload in `02b`: `0A 00 00 20 20 20 20 20 20 C0`
- checksum style is payload-sum based (last byte changes with stick/flag bytes in other `*b` captures).

## 4) Hop plan is different
SHENQI:
- 60-entry hop table + nibble offset from TX ID

CG022 (`02b` packet stream):
- repeating 8-hop order: `0, 40, 10, 50, 20, 60, 30, 70`
- TX-on writes appear as `07 01 <ch>` and next-channel preload as `07 00 <next>`

## 5) Sync/bind identity handling differs
SHENQI bind identity comes from RX response bytes and 4-byte LT8900 address handling.

CG022 bind identity from captures:
- fixed bind prefix bytes `11 22 33`
- per-TX unique bytes in positions 5..7
- byte 8 = checksum of bytes 5..7
- confirmed across:
  - `02b`: `... 06 AB FC AD ...`
  - `22b`: `... FB E0 FC D7 ...`
  - `32b`: `... F4 B9 FA A7 ...`
  - `42b`: `... 14 D2 F9 DF ...`

Also, at bind->data transition in `02b`, extra sync-related writes occur before first data packet:
- `24 AB 06`
- `27 00 FC`
This behavior does not exist in SHENQI.

---

## Changes required to SHENQI_nrf24l01.ino to attempt CG022 bind

To make SHENQI bind with CG022, the SHENQI implementation must be reworked to CG022 behavior:

1. **Replace SHENQI LT8900 init with CG022 register-sequence init**
   - Implement the 33-register startup writes seen in `02b` (plus FIFO clear and CRC-seed read step).

2. **Change bind state machine from RX-driven to TX counter-driven**
   - Remove dependency on `LT8900_ReadPayload()` for bind completion.
   - Send bind payload for ~165 frames at ~2310us frame period.

3. **Expand packet generator from 3 bytes to 10 bytes**
   - Bind packet format: `0A 00 11 22 33 <id0> <id1> <id2> <sum> 00`.
   - Data packet format must follow CG022 byte map from `03b/04b/05b/06b/07b/08b/09b/11b/23b_60` captures.

4. **Replace SHENQI hop logic with CG022 8-hop sequence**
   - Use `0,40,10,50,20,60,30,70` with ~2310us frame cadence.

5. **Implement bind->data transition sync update writes**
   - Apply the transition writes equivalent to `24 AB 06` and `27 00 FC` (values tied to TX ID) before normal data packets.

6. **Adjust channel scaling and flags to CG022 semantics**
   - SHENQI only uses throttle/rudder mapping.
   - CG022 needs throttle/elevator/rudder/aileron plus mode/flags bytes and checksum.

---

## Practical conclusion
The overlap with SHENQI is only at a high level (LT89xx-like over NRF). For actual bind compatibility, CG022 is not a small SHENQI tweak; it is effectively a separate protocol implementation path that reuses only low-level LT8900/NRF transport primitives.
