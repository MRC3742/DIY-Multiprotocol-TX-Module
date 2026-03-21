# AO-SEN-MA / CG022 capture analysis

This note records the conclusions from the Saleae CSV captures in this directory so the protocol implementation can be based on verified observations instead of redoing the same reverse-engineering work.

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

No write to `0x2C` appears in the captured startup/configuration traffic, so the device never leaves the LT8910 default data-rate of **1 Mbps**.

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

### TX-only bind persistence from `01b`

`01b-CG022_TX-PowerOn-NoRX.csv` shows the same bind-to-data handoff even when **no receiver is powered on**:

- the trace contains **1101** transmitted packets over about **3.129 s**
- packets **1..166** all carry the bind payload:
  - `0A 00 11 22 33 06 AB FC AD 00`
- at about **0.969762 s**, the transmitter again rewrites register `0x24` from `0x2211` to `0xAB06`
- packet **167** at about **0.971638 s** is already a normal data packet:
  - `0A 00 00 20 20 20 20 20 20 C0`
- the remaining **935** packets in the capture stay in that data-packet mode; the transmitter does **not** return to the earlier bind payload during the rest of the capture

So the original TX does **not** keep retrying bind indefinitely if no RX is present. It sends the original **166-packet bind burst**, then commits to normal data transmission anyway.

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

The steady-state capture repeatedly uses this 8-channel hop table:

`0A, 32, 14, 3C, 1E, 46, 00, 28`

In decimal:

`10, 50, 20, 60, 30, 70, 0, 40`

In the power-on bind trace, transmission begins at the `00` slot and then continues through that same cycle without resetting, which is why the observed startup order is `00, 28, 0A, 32, 14, 3C, 1E, 46`.

That is an LT89xx-style 1 MHz-spaced hop sequence and matches the existing LT89xx-over-NRF channel mapping approach used elsewhere in the project.

### Receiver-side bind findings from `51b` / `52b`

The new receiver-side SPI captures narrow the remaining bind problem much more than the TX-only traces did:

- `51b-CG022_RX-PowerOn-NoTX.csv` shows the receiver mostly staying in an RX/polling loop when no transmitter is present.
- `52b-CG022_RX-PowerOn-withTX-Bind.csv` shows the receiver **draining accepted packets from the radio FIFO**; there is no clear evidence of a required receiver-to-transmitter bind reply on the LT8910 SPI bus.
- A cleanly reconstructed **accepted data packet** from the receiver side is:
  - `0A 00 00 20 20 20 20 20 20 C0`
  - which matches the current low-throttle / centered-stick data-packet layout already implemented in `Multiprotocol/AOSENMA_nrf24l01.ino`.

The important mismatch was in the **bind packet**. One cleanly reconstructed **accepted bind packet** from the receiver side is:

`0A 00 11 22 33 07 00 FB 02 00`

That did **not** match the **earlier pre-fix TX-side-only bind model**:

`0A 00 11 22 33 06 AB FC AD 00`

So the earlier branch's most likely bind failure was **not** the normal data-packet format, but the fact that the bind packet tail was still modeled from TX-side FIFO writes only. The receiver-side evidence says the stock receiver is accepting a bind-phase payload with the same `0A 00 11 22 33` prefix but a **different last five bytes**.

The current repository code has already been updated to use **exactly that receiver-accepted bind tail** (`07 00 FB 02 00`). The mismatch above refers specifically to the **earlier TX-side-only implementation** that still used the tail (`06 AB FC AD 00`).

In other words:

- the current **data packet** model appears broadly correct
- the previous **bind packet** model was missing something important from the receiver's point of view
- the receiver traces do **not** support the earlier idea that a missing bidirectional bind handshake is the main problem

That bind-tail discrepancy was therefore the most likely reason the earlier MPM branch still would not bind/fly even though its later data packets were much closer to what the receiver accepts.

### RX no-TX state behavior from `51a` / `51b`

The no-transmitter receiver capture is now specific enough to answer whether the RX gives up on bind by itself.

After the initial power-on configuration writes, `51a-CG022_RX-PowerOn-NoTX.csv` / `51b-CG022_RX-PowerOn-NoTX.csv` settle into a **steady polling/search loop** and stay there for the full capture:

- capture length is about **3.144 s**
- after the startup sequence, `51a` shows **98** `PKT_flag` low pulses
- excluding the very first long startup pulse, every low pulse is about **30.764 ms** to **30.772 ms**
- `51a` shows **0** short `PKT_flag` pulses below **10 ms**
- `51b` shows repeated polling/status traffic such as:
  - **1191** first-byte `0xB0` transactions
  - **264** first-byte `0x07` transactions
  - **164** first-byte `0x87` transactions
- but `51b` shows **no** accepted-packet families at all:
  - **0** first-byte `0xB2`
  - **0** first-byte `0xF2`
  - **0** first-byte `0xB3`
  - **0** first-byte `0xBA`

Compared with `52a` / `52b`:

- stock-with-TX `52a` develops **581** short `PKT_flag` pulses below **10 ms**, first appearing at about **3.185052 s**
- stock-with-TX `52b` first shows the accepted-packet families `0xB2` / `0xF2` at about **2.934 s**
- no such late transition appears anywhere in the `51*` no-TX capture

So the receiver **does** change from startup/configuration into a stable listen/search state shortly after power-on, but within the full **3.144 s** no-TX capture it does **not** switch into any different later autonomous state. It simply keeps looking for valid bind traffic.

### Receiver-side bind-to-data sync transition findings from `52b`

The next question after fixing the bind payload tail is whether the **bind-to-data sync transition** itself is wrong. Comparing the TX and RX traces gives a more limited answer than the bind-tail comparison:

- On the **transmitter** side, `02b-CG022_TX-PowerOn-withRX-Bind.csv` still shows the clean expected transition:
  - bind lasts for **166 transmitted packets**
  - then the sync-related register write changes from `0x2211` to `0xAB06`
  - the first data packet follows immediately on the next packet slot
- On the **receiver** side, `52b-CG022_RX-PowerOn-withTX-Bind.csv` shows:
  - one clean accepted bind packet: `0A 00 11 22 33 07 00 FB 02 00`
  - then, about **0.378 s** later, clean accepted data packets beginning with `0A 00 00 20 20 20 20 20 20 C0`

What the receiver trace does **not** show is just as important:

- there is **no visible receiver-side SPI write** matching a bind-to-data sync reprogram such as `0xAB06` / `0x06AB`
- around the first accepted data packets, the receiver only shows FIFO/status housekeeping (`0x34`, `0x07`, and related polling/clear operations), not an obvious sync-register rewrite

So the receiver captures do **not** support the idea that the missing piece is a second, receiver-visible SPI-level sync transition or post-bind handshake. The current code's TX-side behavior:

- send **166** bind packets
- then call `AOSENMA_set_data_sync()`
- then start sending normal data packets

is still the closest match to the stock transmitter trace.

In short:

- the receiver captures **do not contradict** the current TX-side bind-to-data handoff
- they also do **not** provide a new receiver-side SPI clue for a different sync-switch moment
- the remaining uncertainty after the bind-tail fix is therefore more likely in the **OTA transition behavior** than in a missing MCU-visible sync-register change on the receiver

### Receiver-side MPM forced-ID comparison from `53a` / `53b`

The next test was the most useful one still available without OTA capture: power up the **receiver** against the **MPM AO-SEN-MA implementation** while forcing the original captured TX ID (`11 22 33 06 AB`), then compare that receiver-side trace directly against the stock `52a` / `52b` bind trace.

That comparison rules out several earlier suspects and narrows the remaining problem further.

#### What still looks similar between stock `52*` and MPM `53*`

- The captures are almost the same length:
  - `52b-CG022_RX-PowerOn-withTX-Bind.csv`: about **6.259 s**
  - `53b-CG022_RX-PowerOn-withMPM-ForceID-Bind.csv`: about **6.250 s**
- The receiver keeps running the same **basic polling loop** in both traces:
  - `52b-CG022_RX-PowerOn-withTX-Bind.csv` has **2357** first-byte `0xB0` transactions
  - `53b-CG022_RX-PowerOn-withMPM-ForceID-Bind.csv` has **2351** first-byte `0xB0` transactions
- The early `PKT_flag` cadence also starts out the same in both digital traces:
  - repeated low pulses of about **30.77 ms**
  - first rising edges near **0.0588 s**, **0.0906 s**, **0.1224 s**, ...

So the forced-ID MPM signal is not completely invisible to the receiver. The receiver is still running through roughly the same polling cadence and timing windows as it does with the stock TX.

### Requested next firmware experiment

Given the receiver-side baseline above:

- `51*` shows the RX can stay in a pure polling/search loop for at least **3.144 s** when no valid transmitter is present
- stock `01b` shows the original TX stops bind after only **166 packets** (about **0.383 s**) even with no RX present

the next practical experiment is to deliberately deviate from the stock transmitter timing and see whether the receiver will accept a **much longer bind-only transmission window** from MPM.

The current test branch therefore makes two intentional firmware changes for capture-driven debugging:

1. **always force the original stock TX ID**
   - `11 22 33 06 AB`
2. **keep sending bind packets for about 3 seconds**
   - `AOSENMA_PACKET_PERIOD = 2310 us`
   - `AOSENMA_BIND_COUNT = 1299`
   - effective bind duration ≈ **3.00069 s**

This is not meant to model the stock TX more closely. It is a targeted debug build intended to answer a different question:

- if the receiver is still polling for a valid bind after power-on, will it finally enter the accepted-packet path if MPM keeps transmitting the bind packet and stock TX ID long enough for the receiver to latch onto it?

The imported `51*`, `52*`, and `53*` captures in this directory provide the receiver-side baseline for comparing that longer-bind firmware against both:

- **no TX present**
- **stock TX present**
- **earlier forced-ID MPM present**

#### What is missing in `53b`

The important difference is that `53b` never shows the same **accepted-packet / later bound-state SPI activity** that appears in the stock trace.

Across the full 6.25 s capture:

- `52b` contains:
  - **1690** first-byte `0xB2` transactions
  - **264** first-byte `0xB3` transactions
  - **261** first-byte `0xBA` transactions
  - **217** first-byte `0xF2` transactions
- `53b` contains:
  - **0** first-byte `0xB2` transactions
  - **0** first-byte `0xB3` transactions
  - **0** first-byte `0xBA` transactions
  - **0** first-byte `0xF2` transactions

Those stock-only transaction families do not appear immediately at power-up. In `52b` they begin around **2.934 s** and then dominate the later part of the trace:

- `0xB2` first appears at about **2.934349 s**
- `0xF2` first appears at about **2.934464 s**
- `0xB3` first appears at about **3.325888 s**
- `0xBA` first appears at about **3.328927 s**

From about **3.5 s onward**, the stock trace stays in that much heavier activity pattern, while `53b` never leaves the simpler polling-only regime.

The digital trace shows the same split:

- `52a` eventually develops **581 short `PKT_flag` low pulses** below **10 ms**
  - first short pulse starts at about **3.185052 s**
  - typical short widths are about **1.5 ms** to **3.8 ms**
- `53a` develops **no short `PKT_flag` pulses at all**
  - it stays in the original roughly **30.78 ms** low-pulse pattern for the whole capture

So the receiver does **not** reach the same later bound/active receive state with MPM, even when the forced ID matches the stock transmitter.

#### Earliest divergence near the stock bind-to-data acceptance window

The earlier `52b` analysis already showed the first clean accepted data window beginning around **0.378 s** after the clean accepted bind packet. In that same time region, stock and MPM no longer behave quite the same even though both are still polling:

- Stock `52b` at about **0.377704 s**:
  - `C7 FF FF | 01 80 32`
- MPM `53b` at about **0.377852 s**:
  - `83 FF FF | 01 00 32`

Later in the same repeating window:

- Stock `52b` at about **0.441292 s**:
  - `C7 FF FF | 01 80 1C`
- MPM `53b` at about **0.441473 s**:
  - `87 FF FF | 01 00 3C`

And near **0.472 s**:

- Stock `52b`:
  - `87 FF FF | 12 00 BC`
  - `87 00 1E | 12 12 12`
- MPM `53b`:
  - `87 FF FF | 12 00 BC`
  - then only `34 80 80 | 01 01 01`
  - followed by `87 FF FF | 01 00 1E`

So the MPM trace gets into some of the **same timing slots** and even some of the **same returned status values**, but it does **not** trigger the same follow-on receiver behavior that the stock TX does.

#### What `53a` / `53b` rule out

This new capture is useful because it eliminates several remaining "easy" explanations:

- It is **not** just the random TX ID, because `53*` used the **original forced ID**
- It is **not** just the gross packet cadence, because the receiver still follows the same overall polling rhythm
- It is **not** just the broad bind-to-data schedule, because the receiver still reaches the same approximate timing windows where stock traffic is handled differently

#### Most likely remaining cause

The receiver is clearly **seeing something close enough to stock to keep polling on the same schedule**, but it is **not accepting those MPM packets into the same receive path** and never reaches the later bound/active state.

That means the remaining bind failure is now much more likely to be in the **actual OTA packet acceptance details**, not in the visible TX ID bytes or the already-corrected bind-tail payload model.

The most likely remaining categories are:

1. **LT89xx on-air framing details** that do not show up in the SPI payload bytes alone
2. **sync/correlation acceptance behavior** that is still not bit-exact enough
3. **CRC / whitening / trailer / packet-format behavior** inside the LT8910-compatible receive path
4. some other **NRF24L01 LT89xx-emulation mismatch** that allows rough activity detection but prevents the receiver from treating the packet as valid and entering the normal accepted-packet / bound-state flow

In short:

- `53a` / `53b` show that **forcing the original ID is not enough**
- the receiver still does **not** behave like it does with the stock TX
- the failure is now much more likely to be a **bitstream-level LT89xx emulation problem** than a remaining bind-payload-content problem

### Long-bind debug capture comparison from `70*` / `71*`

The next test was to keep the MPM in forced-ID bind mode for about **3 seconds** and then compare:

- **MPM TX SPI** from `71b-CG022_MPM_TX-ForceID-Bind.csv`
- against **stock TX SPI** from `02b-CG022_TX-PowerOn-withRX-Bind.csv`

and also:

- **receiver-side SPI with long-bind MPM** from `70b-CG022_RX-PowerOn_MPM_ForceID-Bind.csv`
- against **receiver-side SPI with stock TX** from `52b-CG022_RX-PowerOn-withTX-Bind.csv`

That longer-bind debug build answers one specific question very clearly:

- does the receiver start accepting packets if MPM keeps transmitting the forced-ID bind packet long enough?

The answer from `70*` / `71*` is **no**.

#### TX-side comparison: stock `02b` vs long-bind MPM `71b`

A direct byte-for-byte SPI comparison is not possible because the buses are different:

- `02b` is the **original transmitter MCU talking to its LT8910-compatible radio**
- `71b` is the **MPM MCU talking to the NRF24L01** that is emulating LT89xx behavior

So the useful comparison is at the **functional** level:

- what packets are transmitted
- how long bind lasts
- whether the TX ever makes the bind-to-data transition

From the earlier stock analysis, `02b` shows:

- **166 bind packets**
- then the LT8910 sync-related write changes register `0x24` from `0x2211` to `0xAB06`
- then the first normal data packet follows immediately

The new long-bind MPM trace `71b` shows the debug firmware doing exactly what it was supposed to do:

- after the short startup/config sequence, the first regular bind burst starts at about **1.557768 s**
- there are **1299** repeated bind bursts using the expected 8-channel NRF hop sequence:
  - `0x02, 0x2A, 0x0C, 0x34, 0x16, 0x3E, 0x20, 0x48`
  - i.e. decimal **2, 42, 12, 52, 22, 62, 32, 72**
- those bind bursts continue until about **4.556945 s**
- the first data-mode burst appears at about **4.559260 s**

So the effective MPM bind-only transmission window is about:

- **4.559260 s - 1.557768 s = 3.00149 s**

which matches the intended debug behavior.

The `71b` trace also shows that MPM is **not** getting stuck in bind forever:

- the repeated bind payload occupies **1299** transmitted bursts
- then the transmitted payload changes to the normal idle/control-packet form
- the remaining **840** bursts in the capture stay in data mode

In other words, the new firmware is doing the requested experiment correctly:

- **force original TX ID**
- **transmit bind for about 3 seconds**
- **then switch to data mode**

That matters because it means the failed bind in `70*` is **not** caused by the old short 166-packet bind timeout anymore.

Using **stock `02*`** as the behavior to replicate, the TX-side SPI results for files `01*`, `02*`, and `71*` compare as follows:

| TX-side check | `01*` TX-PowerOn-NoRX | `02*` TX-PowerOn-withRX-Bind (**target**) | `71*` MPM_TX-ForceID-Bind | Relative to `02*` |
| --- | --- | --- | --- | --- |
| Bind payload shape | **Present** — packets `1..166` carry `0A 00 11 22 33 06 AB FC AD 00` | **Present** — stock bind payload to replicate | **Present** — repeated bind bursts use the expected forced-ID bind payload model | `01*` = **correct**; `71*` = **correct** at the functional bind-payload level |
| Bind duration before switching to data | **166 packets** | **166 packets** | **1299** bind bursts over about **3.00149 s** | `01*` = **correct** match to stock; `71*` = **intentionally different** for debug, so **not matching** `02*` timing even though the longer bind experiment itself worked as designed |
| Bind-to-data transition occurs | **Yes** | **Yes** | **Yes** | `01*` = **correct**; `71*` = **correct** in principle because MPM still exits bind cleanly instead of getting stuck |
| Sync-related handoff at transition | Register `0x24` rewrites from `0x2211` to `0xAB06` at about **0.969762 s** | Register `0x24` rewrites from `0x2211` to `0xAB06` immediately after bind | Functional equivalent handoff is present: bind bursts stop and data bursts begin at about **4.559260 s** | `01*` = **correct** direct stock match; `71*` = **functionally correct**, though not byte-for-byte comparable because the radio bus is NRF24L01 emulating LT89xx |
| First data packet after bind | Packet **167** is already normal data: `0A 00 00 20 20 20 20 20 20 C0` | First normal data packet follows immediately after the bind phase | First data-mode burst appears immediately after the long bind window, at about **4.559260 s** | `01*` = **correct**; `71*` = **correct** at the handoff level |
| Stays in data mode after transition | **Yes** — remaining **935** packets stay in data mode | **Yes** — stock TX commits to data mode | **Yes** — remaining **840** bursts stay in data mode | `01*` = **correct**; `71*` = **correct** |
| Dependence on RX presence for the TX-side handoff | **No RX present**, but TX still performs the full stock handoff | **RX present**, but the TX-side handoff behavior is the same | MPM also completes its programmed handoff without needing receiver-side feedback | `01*` = **correct evidence** that stock handoff is self-timed; `71*` = **correct evidence** that current failure is not “MPM stuck in bind forever” |
| Overall TX-side verdict relative to stock bind | **Essentially the same stock TX behavior as `02*`** | **Correct target behavior** to replicate | **Functionally correct experiment**, but **not a timing match** to `02*` because bind was intentionally extended from 166 packets to ~3 s | `71*` is **good evidence that TX-side bind/data sequencing works**, but it is **not** a strict stock-timing match to `02*` |

#### Receiver-side comparison: stock `52*` vs long-bind MPM `70*`

The receiver-side comparison is the most important result from this test.

Using **stock `52*`** as the behavior to replicate, the receiver-side SPI results for files `51*`, `52*`, and `70*` compare as follows:

| Receiver-side check | `51*` RX-PowerOn-NoTX | `52*` RX-PowerOn-withTX-Bind (**target**) | `70*` RX-PowerOn_MPM_ForceID-Bind | Relative to `52*` |
| --- | --- | --- | --- | --- |
| Basic polling/search loop appears after startup | **Yes** — steady search loop only | **Yes** — same early polling before bind is accepted | **Yes** — same basic early polling loop is still present | `51*` = baseline only; `70*` = **partly correct** because the receiver is at least being perturbed on the right general schedule |
| Short `PKT_flag` pulses below 10 ms | **0** | **581** short pulses, first at about **3.185052 s** | **0** | `51*` = **incorrect** for bind; `70*` = **incorrect**, because it never develops the stock accepted-packet pulse pattern |
| Accepted FIFO-drain sequence (`0xB2` / `0xF2`) | **Absent** | **Present** first at about **2.934349 s** / **2.934464 s** | **Absent** (`0xB2` count = **0**) | `51*` = **incorrect**; `70*` = **incorrect** and still missing the key stock acceptance path |
| Later active/bound-state families (`0xB3` / `0xBA`) | **Absent** | **Present** at about **3.325888 s** / **3.328927 s** | **Absent** (`0xBA` count = **0**) | `51*` = **incorrect**; `70*` = **incorrect**, because it never reaches the late bound-state behavior seen in stock |
| Around the stock first-accept time near **2.934 s** | Still only search/polling traffic | Immediately starts draining accepted bytes: `0xB2 -> 0x0A 0x00`, `0xB2 -> 0x11 0x22`, `0xF2 -> 0x33 0x07`, `0xB2 -> 0x00 0xFB` | Still only status/polling traffic such as `0x90`, `0x98` | `70*` is **wrong at exactly the moment that matters most for bind** |
| Around the stock late bound-state window near **3.312 s** to **3.329 s** | No transition at all | Already in accepted-data / `0xB3` / `0xBA` behavior | Still only housekeeping traffic such as `0xB0`, `0xF0`, `0x98`, `0x87`, `0x07` | `70*` remains **wrong late**, not just early |
| Overall receiver verdict relative to stock bind | Receiver never sees valid bind traffic | **Correct target behavior**: bind is accepted and the receiver transitions into the later active/bound path | Receiver sees **enough** to alter polling/status mix, but never enough to accept a packet into FIFO | `70*` is **closer than `51*`**, but still **fundamentally incorrect** at the accepted-packet stage |

With the **stock TX**, `52a` / `52b` show the receiver eventually entering the accepted-packet path:

- `52b` first shows FIFO-drain reads `0xB2` / `0xF2` at about **2.934349 s** / **2.934464 s**
- `52b` later shows the bound/active-state families `0xB3` / `0xBA` at about **3.325888 s** / **3.328927 s**
- `52a` shows **581** short `PKT_flag` low pulses below **10 ms**
  - first short pulse starts at about **3.185052 s**

With the **long-bind MPM firmware**, `70a` / `70b` still do **not** show that stock accepted-packet behavior:

- `70a` contains **196** `PKT_flag` low pulses
- `70a` contains **0** short `PKT_flag` pulses below **10 ms**
- `70b` contains **0** `0xB2` FIFO-read transactions anywhere in the capture
- `70b` contains **0** `0xBA` transactions anywhere in the capture

There are a few isolated raw `0xF2` and `0xB3` bytes in `70b`, but they do **not** form the same stock accepted-packet sequences seen in `52b`, and they do **not** coincide with any receiver transition into the later active/bound state.

Around the exact time where stock `52b` first starts draining accepted packets, the difference is stark:

- stock `52b` at about **2.934 s** immediately begins:
  - `0xB2 FF FF -> 0x0A 0x00`
  - `0xB2 FF FF -> 0x11 0x22`
  - `0xF2 FF FF -> 0x33 0x07`
  - `0xB2 FF FF -> 0x00 0xFB`
- long-bind MPM `70b` at the same time region shows only status/polling traffic such as:
  - `0x90 FF FF`
  - `0x98 FF FF`

And around the later stock active-state window near **3.312 s** to **3.329 s**:

- stock `52b` is already reading accepted data and then entering the `0xB3` / `0xBA` families
- long-bind MPM `70b` still shows only polling / housekeeping traffic such as:
  - `0xB0`
  - `0xF0`
  - `0x98`
  - `0x87`
  - `0x07`

but never the stock receiver's accepted-packet flow.

#### What changed vs the earlier forced-ID-only `53*` test

The longer-bind firmware does change the receiver-side trace somewhat:

- `70b` shows a noticeably different polling/status mix than `53b`
- in particular, `70b` has many more `0x98` / `0xF0` style status bursts than the earlier forced-ID-only capture

So extending bind to ~3 seconds is **not invisible** to the receiver.

But the important thing is what still does **not** happen:

- no receiver FIFO-drain sequence like stock `52b`
- no short `PKT_flag` pulses like stock `52a`
- no transition into the late active/bound state

So the longer bind window changes the receiver's polling behavior, but it still does **not** make the receiver treat the MPM packets as valid bind packets.

#### Conclusion from `70*` / `71*`

These two new capture sets narrow the problem further:

1. `71b` proves the new debug firmware is actually doing the requested test:
   - original forced TX ID
   - about **3 seconds** of bind packets
   - then a clean change into data mode
2. `70b` proves the receiver still never reaches the stock accepted-packet path while that long bind is being transmitted
3. therefore the remaining bind failure is **not** just that MPM was previously leaving bind too early

So after `70*` / `71*`, the most likely remaining blocker is still:

in ranked order, the LT89xx emulation fields most likely still wrong for the specific failure mode

- **receiver polling is perturbed**
- but the receiver **never** promotes the packet into FIFO / accepted-packet handling

are:

1. **sync-word / preamble / correlator alignment on the air**
   - This is the best fit for "the RX clearly notices something changed" while still never producing any `0xB2` FIFO-drain sequence.
   - If the RF burst lands on the right channel with roughly the right timing, the receiver's polling / status cadence can still be perturbed.
   - But if the LT89xx sync pattern, its bit order, or its exact correlation alignment is still wrong, the packet can be rejected before the receiver ever treats it as a valid FIFO candidate.
2. **trailer / final-bit alignment / packet framing after the LT89xx length byte**
   - This is the next most likely class because it can preserve a mostly correct packet body while still making the full OTA frame invalid.
   - The captures already show that stock `52b` reaches accepted-packet reads while MPM `70b` never does, so a framing error late enough to keep the packet from closing cleanly is still a strong candidate.
   - In practical terms this means the remaining bug could still be in the LT89xx-over-NRF24L01 framing details such as the inserted length byte, trailer-bit count, or the final bit shift into the NRF payload.
3. **CRC acceptance as seen by the LT89xx receiver, not just the nominal CRC math**
   - The polynomial and nominal bytes are already verified, so this ranks below sync/framing.
   - But a CRC can still fail on the real OTA bitstream if the CRC input bits, byte order on the air, or the post-data trailer alignment differ from what the LT89xx receiver expects.
   - This would also fit the observed failure mode: packets that are "close enough" to disturb receiver behavior, but never valid enough to enter the accepted FIFO path.
4. **address / sync-field bit reversal or byte packing at the LT89xx boundary**
   - This overlaps somewhat with item 1, but it is worth calling out separately because the repository already had one LT89xx byte-order bug in the CRC path.
   - If payload bytes are now correct but the sync/address field is still packed or reversed incorrectly for OTA transmission, the receiver can remain stuck in pure polling / housekeeping traffic forever.
5. **packet-type sequencing details that affect RF validity but not the logical bind payload**
   - This includes lower-level transport details such as the exact duplicate-send pattern, TX completion timing, and possibly other LT89xx framing-state assumptions around repeated packets.
   - These rank below the items above because `71b` shows the long-bind firmware is sending the intended burst pattern and `70b` still never reaches even the first accepted FIFO read.
   - So these details still matter, but they are less likely than a fundamental correlator / framing / CRC-validity mismatch.

- **bit-exact LT89xx emulation / OTA validity**
- not TX ID selection
- not bind duration
- and not simply the lack of a long enough bind retry window

## Best emulation choice in this repository

### Recommended: NRF24L01

The **NRF24L01** is the best fit here because:

1. The captured protocol is using **1 Mbps**, which the NRF24L01 can already do.
2. The project already has an **LT8900 emulation layer** in `Multiprotocol/NRF24L01_SPI.ino`.
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
5. The two protocols mainly share the **LT89xx-over-NRF24L01 transport layer**, which is already factored into `Multiprotocol/NRF24L01_SPI.ino`; that shared transport is not, by itself, a strong reason to merge them into one protocol entry.

So the clean repository-style approach is:

- keep using the existing **NRF24L01 LT89xx emulation layer**
- implement AO-SEN-MA / CG022 in its **own protocol file**
- register it as its **own protocol entry**, rather than expanding `SHENQI` into a loosely related subtype family

## Practical implementation direction

For this CG022/AO-SEN-MA work, the most promising next step is:

1. keep the implementation on the **NRF24L01**
2. extend the existing LT89xx emulation logic as needed for the LT8910-style framing/control flow used here
3. model the new protocol from these verified packet contents, hop sequence, and bind-to-data transition behavior

## What additional evidence would help beyond TX-side SPI captures

The SPI captures are still valuable because they show the FIFO bytes, hop timing, and sync-word transition. But they do **not** directly show the final over-the-air bitstream after LT89xx framing, nor do they prove whether the receiver answers during bind. If the protocol still will not bind or fly, the most useful next evidence is:

1. **Over-the-air RF waveform capture during bind and the first data packets**
   - Use a spectrum analyzer, SDR, or oscilloscope setup that can show the actual 2.4 GHz burst timing and frequency placement.
   - This would confirm whether the LT89xx-on-NRF24L01 emulation is really producing the expected preamble/sync/trailer shape, symbol rate, and hop channels instead of only the expected FIFO writes.

2. **Receiver-side observation during a stock-TX bind**
   - If possible, capture the receiver board's SPI/UART/GPIO activity or even just its LED/state transition while binding to the original transmitter.
   - That would show whether the receiver sends any bind response, how long it waits before accepting a transmitter, and whether there is a post-bind acknowledgment or mode change that is invisible from the TX SPI log alone.

3. **A synchronized capture of the bind-to-data transition**
   - Record the last few bind packets and first few data packets both on SPI and OTA.
   - This is the cleanest way to verify that the sync-word change, hop continuation, and timing transition all happen at the right instant from the receiver's point of view.

4. **Register/state verification from the emulated transmitter**
   - Debug output or read-back of the programmed NRF24L01 state after `LT8900_SetAddress()`, `LT8900_SetChannel()`, and the bind-to-data transition would help confirm that the emulation is really in the state we think it is.
   - This is especially useful because TX-side SPI write logs alone do not prove that the effective on-air address/channel state matches the original LT8910-class transmitter at each step.

In short: if FIFO bytes, hop order, sync change, and packet timing all look right but the model still will not bind, the remaining unknowns are most likely in the **actual OTA waveform** or the **receiver-side bind behavior**, not in the SPI payload bytes themselves.

## Receiver-board Saleae hookup guidance from the photographed CG022 board

With the sharper receiver-board photo and follow-up continuity tracing, the 32-pin MCU can now be read as **`MINI54ZAN`**, matching the Nuvoton Mini54 family.

The exposed receiver-board test pads are now understood to be:

- `DATA` -> **Mini54 pin 20 / `P4.7` / `ICE_DAT`**
- `CLK` -> **Mini54 pin 19 / `P4.6` / `ICE_CLK`**
- `VDD` -> board power
- `GND` -> ground

The nearby four vias also do **not** appear to be useful LT8910 bus probes:

- top via -> tied to a motor `+` trace / supply rail
- second via -> ground
- lower two vias -> tied to **Mini54 pins 10 and 11**, which are the external crystal pins (`XTAL1`, `XTAL2`)

That means the earlier "start on the exposed `DATA`/`CLK` pads and four vias" guidance is no longer the best answer for this board. Those points are useful for confirming the MCU family and board power, but they are **not** the direct LT8910 control interface.

For receiver-side logic capture, the Saleae should instead be placed on the **LT8910-side digital bus**.

### Best LT8910 pins to capture first

From the current board tracing, the most useful LT8910 pins are:

1. **LT8910 pin 16 = `SPI_CLK`**
   - This is the most important reference for decoding the synchronous bus.
2. **LT8910 pin 1 = `SPI_MOSI`**
   - Captures MCU-to-radio register writes and FIFO payloads.
3. **LT8910 pin 2 = `SPI_MISO`**
   - Captures any radio-to-MCU readback/status traffic.
4. **LT8910 pin 14 = `SPI_SS` / chip-select** if available
   - Strongly recommended because it gives clean frame boundaries for the Saleae SPI decoder.

Those four signals are the best "minimum useful" capture set.

### Secondary LT8910 candidates if you have spare channels

- **LT8910 pin 13 = `PKT_flag`**
  - Very useful as a timing marker because it should show when the radio reports packet/buffer state changes.
- **LT8910 pin 4 = `RESET_n`**
  - Usually much less active, but it can confirm radio reset timing during power-up.

### Recommended hookup order

For a first meaningful LT8910 capture, hook the Saleae up in this order:

1. **board `GND`**
2. **LT8910 `SPI_CLK` (pin 16)**
3. **LT8910 `SPI_MOSI` (pin 1)**
4. **LT8910 `SPI_MISO` (pin 2)**
5. **LT8910 `SPI_SS` (pin 14)** if reachable
6. **LT8910 `PKT_flag` (pin 13)** if you still have channels
7. **LT8910 `RESET_n` (pin 4)** only after the above are covered

Recommended first capture set on an 8-channel Saleae:

- `GND` -> Saleae ground clip
- LT8910 `SPI_CLK` -> digital channel
- LT8910 `SPI_MOSI` -> digital channel
- LT8910 `SPI_MISO` -> digital channel
- LT8910 `SPI_SS` -> digital channel
- LT8910 `PKT_flag` -> digital channel
- LT8910 `RESET_n` -> digital channel

An exact 8-channel assignment that should work well is:

- **ground clip** -> board `GND`
- **D0** -> LT8910 **`SPI_MOSI`** (pin 1)
- **D1** -> LT8910 **`SPI_MISO`** (pin 2)
- **D2** -> LT8910 **`SPI_CLK`** (pin 16)
- **D3** -> LT8910 **`SPI_SS`** (pin 14)
- **D4** -> LT8910 **`PKT_flag`** (pin 13)
- **D5** -> LT8910 **`RESET_n`** (pin 4)
- **D6** -> exposed pad **`DATA` / Mini54 `ICE_DAT`** only as a low-priority reference channel
- **D7** -> exposed pad **`CLK` / Mini54 `ICE_CLK`** only as a low-priority reference channel

That gives one clean SPI decoder group plus two spare "sanity-check" channels on the exposed Mini54 pads without sacrificing any of the important LT8910 lines.

If you use the Saleae SPI analyzer, set it up as:

- **Enable** = `D3` (`SPI_SS`)
- **Clock** = `D2` (`SPI_CLK`)
- **MOSI** = `D0` (`SPI_MOSI`)
- **MISO** = `D1` (`SPI_MISO`)

For the actual Saleae capture trigger, use:

- **Trigger channel** = `D3` (`SPI_SS`)
- **Trigger condition** = **falling edge**

In other words, start the capture when **`SPI_SS` goes from high to low**, because that is the normal start of an SPI transaction and gives the cleanest frame boundary for the LT8910 register/FIFO traffic.

If `SPI_SS` is too hard to reach physically, keep the same order but move:

- **D3** -> LT8910 `PKT_flag`
- **D4** -> LT8910 `RESET_n`
- **D5** -> exposed `DATA` / `ICE_DAT`
- **D6** -> exposed `CLK` / `ICE_CLK`
- **D7** -> leave unused or put on any other suspected LT8910 control/status trace

If you do not have `SPI_SS`, the next-best trigger is usually **`D2` / `SPI_CLK`** on a **rising edge**, but that is less clean because it does not uniquely mark the start of a complete SPI frame the way chip-select does.

Important practical notes:

- **Do not spend Saleae data channels on the exposed `DATA` / `CLK` pads first.** On this board they are Mini54 ICE/programming pins, not the LT8910 SPI bus.
- **Do not spend channels on the two crystal vias** unless you specifically want to examine the oscillator; they are not useful for register/FIFO decode.
- **Do not use `VDD` as a logic-data input.** It is only useful as a voltage reference check.
- If you only have room for three LT8910 signals besides ground, use **`SPI_CLK` + `SPI_MOSI` + `SPI_SS`** first. Add `SPI_MISO` next if you can.

For this specific board, the best "what should I hook up first?" answer is therefore:

- **must-have:** `GND`, LT8910 `SPI_CLK`, LT8910 `SPI_MOSI`
- **strongly recommended at the same time:** LT8910 `SPI_MISO`, LT8910 `SPI_SS`
- **good extra timing channels:** LT8910 `PKT_flag`, then `RESET_n`

That setup gives the best chance of decoding the actual Mini54-to-LT8910 command traffic during bind and verifying exactly which register writes, FIFO loads, and timing events the stock receiver board uses.

### Bench-side 10-step Saleae receiver capture procedure

If you just want the short version to follow at the bench, use this exact procedure:

1. Connect the Saleae ground clip to board **`GND`**.
2. Connect **D0=`SPI_MOSI`**, **D1=`SPI_MISO`**, **D2=`SPI_CLK`**, **D3=`SPI_SS`**, **D4=`PKT_flag`**, and **D5=`RESET_n`**. Leave **D6/D7** unused unless you also want the optional **`ICE_DAT` / `ICE_CLK`** reference channels.
3. In the Saleae SPI analyzer, set **Enable=D3**, **Clock=D2**, **MOSI=D0**, **MISO=D1**.
4. Set the trigger to **D3 / `SPI_SS` falling edge**. If `SPI_SS` is unreachable, fall back to **D2 / `SPI_CLK` rising edge**.
5. Set the digital sample rate to **24 MS/s or higher** and the capture length to **6.5 s** so the trace includes both the early bind/data window and the later stock-only active-state window.
6. Turn the receiver **off** before arming the Saleae capture.
7. Prepare the transmitter for the specific run: first take a **stock-TX bind** capture, then repeat with the **latest MPM bind** capture. Keep transmitter distance, orientation, and bind procedure the same between runs.
8. Click **Arm** in Saleae, then power **on** the receiver and let the full **6.5 s** capture complete without moving sticks or pressing extra buttons beyond the normal bind action.
9. Save the full **`.sal`** capture, then export both the **digital CSV** and the **SPI CSV**. Keep the existing naming pattern such as `54a-...` for digital and `54b-...` for SPI.
10. Before sending the files, do a quick sanity check that **D2 shows clock activity**, **D3 toggles for SPI frames**, **D0/D1 are not flat**, and **D4 `PKT_flag` is alive**. If any of those are missing, fix the hookup and repeat the capture.
