# CG022 receiver capture analysis for sets 54 / 55 / 56

This note summarizes what the newer receiver-side Saleae captures show about the remaining AO-SEN-MA / CG022 bind failure.

## Capture sets

- **54** = `CG022_RX-PowerOn-withTX-Bind`
- **55** = `CG022_RX-PowerOn-withMPM-Bind`
- **56** = `CG022_RX-PowerOn-withMPM-ForceID-Bind`

Each set contains:

- the original `.sal` capture
- an **`a`** digital CSV export
- a **`b`** SPI analyzer CSV export

## Main conclusion

The new captures show that the current **MPM** implementation still fails in essentially the **same way** as the **forced-ID MPM** implementation.

That means the remaining blocker is **not TX ID selection**.

The stock transmitter trace (`54`) still reaches a later receiver **bound/active** state that neither `55` nor `56` reaches. The remaining problem is therefore much more likely to be an **LT89xx OTA acceptance / emulation mismatch** than a payload-content or TX-ID problem.

## Evidence

### 1) Stock `54` reaches the late active state

The digital trace `54a-CG022_RX-PowerOn-withTX-Bind.csv` develops a large number of short `PKT_flag` low pulses after the receiver enters its later active state:

- **621** short pulses below **10 ms**
- first short pulse at about **3.504385 s**

That behavior matches the earlier stock-vs-MPM conclusion from the `52*` / `53*` captures: the stock receiver eventually leaves the coarse polling-only state and enters a much denser accepted-packet / active-receive path.

The reconstructed SPI transaction stream from `54b-CG022_RX-PowerOn-withTX-Bind.csv` shows the same late-state shift. After about **3.5 s**, the counts of several transaction families rise sharply compared with the earlier part of the capture, especially:

- first-byte `0x07`
- first-byte `0x30`
- first-byte `0x18`

## 2) Current MPM `55` does not reach that state

The digital trace `55a-CG022_RX-PowerOn-withMPM-Bind.csv` never develops the short-pulse pattern seen with the stock TX:

- **0** short `PKT_flag` low pulses below **10 ms**
- the trace stays in the coarse roughly **30.77 ms** polling rhythm through the full capture

The reconstructed SPI transaction mix in `55b-CG022_RX-PowerOn-withMPM-Bind.csv` also stays in the polling-only regime. Unlike stock `54`, it never shows the late large increase in the `0x07` / `0x30` / `0x18` families that goes with the receiver entering the later bound/active state.

## 3) Forced-ID MPM `56` fails the same way

The digital trace `56a-CG022_RX-PowerOn-withMPM-ForceID-Bind.csv` also fails to enter the stock late active state:

- only **4** tiny sub-10 ms pulses appear
- they occur very early and look like startup glitches, not a sustained late-state transition

The reconstructed SPI transaction mix in `56b-CG022_RX-PowerOn-withMPM-ForceID-Bind.csv` is extremely close to `55b`, and again does **not** develop the stock late-state behavior.

So:

- **55** (current MPM) fails
- **56** (forced-ID MPM) fails in the same way

This rules out TX ID as the remaining primary cause.

## What this rules out

These capture sets make several earlier suspects much less likely:

- **wrong TX ID**
- **missing forced-ID option**
- **bind payload tail only**
- **gross packet-period error**
- **gross hop-sequence error**

The receiver is still reacting enough to stay on roughly the same broad polling cadence, but it is **not** accepting the MPM traffic into the same later receive path that the stock TX reaches.

## Most likely remaining cause

The remaining failure is now most likely in the **LT89xx-on-NRF24L01 emulation details**, not in the visible payload bytes alone.

Most likely categories:

1. **on-air framing fidelity**
2. **sync/correlation acceptance details**
3. **CRC / trailer / Manchester / packet-type behavior**
4. another **bitstream-level LT89xx emulation mismatch** that still lets the receiver see activity timing without treating the packet as valid

In repository terms, the next place to focus is no longer just `Multiprotocol/AOSENMA_nrf24l01.ino`, but the underlying LT89xx emulation path in:

- `Multiprotocol/NRF24l01_SPI.ino`

## Practical next step

The new captures do **not** justify another TX-ID experiment.

The highest-value next investigation is:

- compare the AO-SEN-MA LT89xx emulation behavior against what the receiver accepts on-air
- focus on the shared LT89xx-over-NRF24L01 emulation layer rather than only changing payload bytes in `AOSENMA_nrf24l01.ino`

