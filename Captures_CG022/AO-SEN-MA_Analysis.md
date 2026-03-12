# AO-SEN-MA / CG022 capture analysis

This note records the conclusions from the Saleae CSV captures in this directory so the future protocol implementation can be based on verified observations instead of redoing the same reverse-engineering work.

## Summary

- The unknown 16-pin RF IC used by the CG022 transmitter behaves like an **LT8910 or a very close LT8910-compatible variant**, not a plain LT8900.
- The captures do **not** show any data-rate override write, so the radio stays at the LT8910 default **1 Mbps** on-air rate.
- Because this protocol is running at **1 Mbps GFSK** and the project already has an **LT89xx-on-NRF24L01 emulation layer** used by `SHENQI_nrf24l01.ino`, the best emulation target in this repository is still the **NRF24L01**, not the CC2500.
- The **CC2500** only becomes attractive if a future LT8910-based protocol actually uses the LT8910 low-rate modes (`250 Kbps`, `125 Kbps`, or `62.5 Kbps`). These captures do not.

## Why the chip looks like LT8910

The SPI traffic matches the LT8910 register map closely:

- Repeated accesses to `0x32` are consistent with **LT8910 register 50**, the TX/RX FIFO register.
- Repeated accesses to `0x34` with `0x8080` are consistent with **LT8910 register 52**, used to clear FIFO pointers.
- The initialization sequence writes registers through `0x2B` and then uses the higher FIFO-related addresses (`0x32`, `0x34`), which matches the LT8910 datasheet layout much better than an LT8900-only interpretation.

That makes the original “LT8910 in SSOP16” hypothesis plausible.

## Why the air rate is 1 Mbps

### 1) No LT8910 data-rate register write was captured

The LT8910 datasheet exposes the variable data-rate control in **register 44 (`0x2C`)**:

- `0x01` = `1 Mbps`
- `0x04` = `250 Kbps`
- `0x08` = `125 Kbps`
- `0x10` = `62.5 Kbps`

Across the CG022 captures, the transmitter writes:

- initialization registers up to `0x2B`
- FIFO data via `0x32`
- FIFO pointer control via `0x34`

No write to `0x2C` appears in the captured startup/configuration traffic, so the device never leaves the LT8910 default data rate of **1 Mbps**.

### 2) The observed packet timing is too short for the LT8910 low-rate modes

From `01a-CG022_TX-PowerOn-NoRX.csv`:

- the transmitter starts a packet with a `0x07 0x01 xx` transaction
- `PKT` rises about **0.35 ms** later

That timing is consistent with a short **1 Mbps** LT89xx packet plus radio turnaround/housekeeping. It is not consistent with `250 Kbps`, `125 Kbps`, or `62.5 Kbps`, which would take much longer on-air for the same framed packet.

## Bind/data observations useful for future implementation

### Bind packet

The bind FIFO payload written after the initial setup is:

`0A 00 11 22 33 06 AB FC AD 00`

- `0x0A` is the LT89xx length byte
- bind payload bytes are:
  - `00 11 22 33 06 AB FC AD 00`

### Bind completion

From `02b-CG022_TX-PowerOn-withRX-Bind.csv`:

- bind lasts for **166 transmitted packets**
- immediately after that, the transmitter changes the sync-related configuration and starts sending normal control packets
- one captured transition is:
  - register `0x24` changes from `0x2211` to `0xAB06`

### Normal data packet shape

A centered control packet appears as:

`0A 22 00 20 20 20 20 20 20 C0`

Other captures show that, after the leading `0x0A` length byte:

- the first control byte changes with **throttle**
- the second control byte changes with **elevator**
- the fourth control byte changes with **rudder**
- the fifth control byte changes with **aileron**
- later bytes carry **button/special-function flags**

### Channel hopping sequence

The capture repeatedly uses this 8-channel hop table, although the power-on bind trace enters it at the `00` slot before continuing through the same cycle:

`0A, 32, 14, 3C, 1E, 46, 00, 28`

In decimal:

`10, 50, 20, 60, 30, 70, 0, 40`

That is an LT89xx-style 1 MHz-spaced hop sequence and matches the existing LT89xx-over-NRF channel mapping approach used elsewhere in the project.

## Best emulation choice in this repository

### Recommended: NRF24L01

The **NRF24L01** is the best fit here because:

1. The captured protocol is using **1 Mbps**, which the NRF24L01 can already do.
2. The project already has an **LT8900 emulation layer** in `Multiprotocol/NRF24l01_SPI.ino`.
3. `Multiprotocol/SHENQI_nrf24l01.ino` already proves the repository can emulate an LT89xx-family protocol over NRF24L01.
4. Reusing and extending that path is much less invasive than starting a new LT8910 emulation path on a different RF chip.

### Not recommended for this capture set: CC2500

The **CC2500** would only be a better fit if the captured LT8910 protocol was actually using one of the low-rate LT8910 modes that the NRF24L01 cannot emulate directly. Since these captures stay at **1 Mbps**, CC2500 adds complexity without solving a real problem for AO-SEN-MA.

## Protocol registration recommendation

This should be added as a **new protocol**, not as a **SHENQI subprotocol**.

Why:

1. `Multiprotocol/SHENQI_nrf24l01.ino` is a very small **3-byte** LT8900-style protocol, while AO-SEN-MA / CG022 uses **9-byte LT8910-class payloads plus a leading LT89xx length byte** with different control-byte placement.
2. SHENQI binds through a short RX/TX handshake and then sends a repeating 7-packet cycle, while CG022 uses a **166-packet bind phase** followed by a sync-word change and a different data phase.
3. SHENQI uses its own 60-entry hop table with TXID-based offsetting, while CG022 uses the fixed 8-channel sequence `10, 50, 20, 60, 30, 70, 0, 40`.
4. In the current repository structure, `SHENQI` has **no existing subtypes** in `Multiprotocol/Multi_Protos.ino`, and adding AO-SEN-MA as a subtype would force most of `SHENQI_send_packet()` and `SHENQI_callback()` to become special-case branches.
5. The two protocols mainly share the **LT89xx-over-NRF24L01 transport layer**, which is already factored into `Multiprotocol/NRF24l01_SPI.ino`; that shared transport is not, by itself, a strong reason to merge them into one protocol entry.

So the clean repository-style approach is:

- keep using the existing **NRF24L01 LT89xx emulation layer**
- implement AO-SEN-MA / CG022 in its **own protocol file**
- register it as its **own protocol entry**, rather than expanding `SHENQI` into a loosely related subtype family

## Practical implementation direction

For this CG022/AO-SEN-MA work, the most promising next step is:

1. keep the implementation on the **NRF24L01**
2. extend the existing LT89xx emulation logic as needed for the LT8910-style framing/control flow used here
3. model the new protocol from these verified packet contents, hop sequence, and bind-to-data transition behavior
