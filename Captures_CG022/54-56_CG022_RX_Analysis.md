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

## Can the existing captures still debug the LT89xx emulation layer?

**Yes, partially.**

The captures already taken are still useful for debugging several parts of the LT89xx emulation path, even without OTA equipment.

### What can still be debugged from the existing captures

Using the TX-side FIFO captures plus the receiver-side stock-vs-MPM comparisons, it is still possible to check whether the emulation layer is doing the **right logical transformations** before transmission:

1. **sync/address handling**
   - confirm the bind sync uses `txid[0..2]`
   - confirm the data sync uses `{ txid[3], txid[4], 0xFC }`
   - verify the byte-reversal and address-order behavior in `LT8900_SetAddress()`

2. **packet-length handling**
   - confirm AO-SEN-MA passes **9 payload bytes**
   - confirm `LT8900_WritePayload()` is adding the LT89xx length byte on-air when packet-length mode is enabled

3. **CRC and payload bit reversal**
   - verify the exact bytes being fed into the CRC
   - verify where payload bytes are bit-reversed and where they are not

4. **preamble / trailer construction**
   - verify the preamble pattern selected from the sync LSB
   - verify the configured trailer length and the trailer-bit shift logic

5. **channel and bind/data sequencing**
   - verify channel order and no-reset hop continuation across bind completion
   - verify the exact moment `AOSENMA_set_data_sync()` is called relative to bind count completion

6. **whether a candidate emulation change moves MPM behavior closer to stock**
   - the receiver-side captures are still good enough to tell whether a change starts producing the later receiver active/bound-state behavior
   - even without OTA, that is still a meaningful pass/fail signal

### What the existing captures cannot prove

The current captures do **not** directly show the final over-the-air waveform emitted by the NRF24L01. So they cannot conclusively prove:

1. the exact **radiated preamble/sync/trailer bitstream**
2. whether the receiver is rejecting packets because of some subtle **Manchester / symbol timing** mismatch
3. whether the effective on-air result of the NRF24L01 path is bit-exact to the original LT8910-compatible transmitter
4. any mismatch caused by RF behavior that is **after** buffer construction and register programming

### Practical implication

So the answer is:

- **yes**, the existing captures are still enough to debug a large part of the LT89xx emulation layer
- **no**, they are not enough to fully prove final OTA correctness

Without OTA gear, the best remaining workflow is:

1. audit `LT8900_SetAddress()`, `LT8900_BuildOverhead()`, and `LT8900_WritePayload()`
2. make one small emulation-layer change at a time
3. use the same receiver-side capture method to see whether MPM starts entering the stock late active/bound state

That will not prove the exact waveform, but it can still narrow the fault further and may still expose the specific emulation-layer mistake.

## First emulation-layer change to try from the current evidence

The most suspicious single bug in the current repository code is that AO-SEN-MA requests an LT89xx **Manchester** packet type, but the shared LT89xx-over-NRF24L01 path originally did not apply any Manchester transform to the payload stream at all.

So the first committed emulation-layer fix should be:

- make `LT8900_WritePayload()` actually honor the Manchester packet-type flag
- make `LT8900_ReadPayload()` decode the same Manchester format symmetrically

That is a better first fix than blind payload edits because it directly targets a real transport-layer mismatch between the intended LT89xx configuration and the bytes actually sent by the NRF24L01 emulation path.

## How many practical emulation-layer variations are worth testing?

There are **30** practical first-pass variations in the most relevant framing matrix.

If testing is limited to the framing knobs most strongly implicated by the captures, the practical first-pass space is:

1. **packet type**
   - **NRZ**
   - **Manchester**

2. **trailer length**
   - any LT89xx-legal value from **4** to **18** bits
   - that is **15** possible trailer-length settings

So the practical first-pass matrix is:

- **2 packet-type choices × 15 trailer-length choices = 30 variations**

That is the useful number to keep in mind if testing is done systematically while holding the already-matched items fixed:

- payload bytes
- TX ID
- bind count
- sync-width choice
- hop sequence
- packet period

If even that 30-case matrix fails, the next remaining variables are no longer the obvious configuration knobs but deeper waveform details such as exact Manchester polarity/timing, preamble shape, or other NRF24L01-vs-LT89xx RF behavior.
