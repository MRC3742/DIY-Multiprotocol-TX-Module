# MPM TX SPI Capture Analysis (Captures 60–62)

## Captures Analyzed
| File | Description |
|------|-------------|
| 60a/60b | CG022 RX (LT8910) SPI, MPM ForceID TX attempting bind |
| 61a/61b | MPM TX (STM32→NRF24L01) SPI, ForceID firmware |
| 62a/62b | MPM TX (STM32→NRF24L01) SPI, DefaultID firmware |

## Critical Finding: CG022 Protocol Is NOT Running

The SPI captures from the MPM TX module (61b, 62b) show that the CG022
protocol code is **not being executed**. A different protocol is running
instead. This explains why the model receiver never binds.

### Evidence

**1. ForceID and DefaultID captures are IDENTICAL**

Both 61b (FORCE_CG022_ORIGINAL_ID uncommented) and 62b (default ID)
produce the exact same NRF24L01 register writes: same TX address, same
RX address, same payload data, same RF channels, same timing. If the
CG022 code were running, uncommenting FORCE_CG022_ORIGINAL_ID would
change the TX ID, sync word, bind packet contents, and hopping channels.

**2. NRF24L01 addresses do not match CG022**

| Parameter | Capture (61b/62b) | Expected CG022 (ForceID) |
|-----------|-------------------|--------------------------|
| TX_ADDR   | `55 0F 71 0C 00`  | `AA 88 44 55 55`         |
| RX_ADDR_P0| `D7 74 2D 9A A8`  | `AA 88 44 55 55` (same as TX) |

CG022's LT8900 emulation always sets TX_ADDR = RX_ADDR_P0 because it does
not use bidirectional communication. The capture shows different TX and RX
addresses, which is characteristic of a protocol that uses telemetry.

**3. Payload size is wrong**

| Parameter | Capture | Expected CG022 |
|-----------|---------|-----------------|
| NRF payload length | 31 bytes | 12 bytes |

CG022's LT8900 emulation produces a 12-byte NRF payload containing:
10 bit-reversed data bytes + 2 bit-reversed CRC bytes.

**4. RF channels are wrong**

| Parameter | Capture | Expected CG022 |
|-----------|---------|-----------------|
| First RF_CH | 0x34 (52) | 0x02 (2) |
| Channel sequence | 52, 43, 34, 25, 64, 55, 46, 37 | 2, 42, 12, 52, 22, 62, 32, 72 |
| Number of channels | 16+ | 8 |

CG022 uses 8 LT8900 channels {0, 40, 10, 50, 20, 60, 30, 70} which map
to NRF24L01 channels {2, 42, 12, 52, 22, 62, 32, 72} via the +2 offset.

**5. Protocol uses TX/RX switching (CG022 does not)**

The capture shows the module switching between TX mode (CONFIG=0x02) and
RX mode (CONFIG=0x03) after every packet transmission. CG022 is a TX-only
protocol that never enters RX mode.

**6. RX_PW_P0 register is written (CG022 never writes it)**

The capture shows W_RX_PW_P0 = 0x14 (20 bytes). CG022's code path never
writes the RX payload width register because it never receives data.

### RX Capture (60b) Confirmation

The receiver-side capture (60b) shows only 3 FIFO reads during the
entire 6.3-second capture, confirming the receiver did not detect any
valid packets from the MPM module (the stock TX produces ~1690 FIFO reads
in the same time window when binding succeeds).

## Root Cause: Protocol Selection

CG022 is registered as protocol number **109** in the multiprotocol
firmware. The protocol must be correctly selected on the radio/transmitter.

### If Using PPM Mode (Rotary Switch)

CG022 was **not included** in the default PPM protocol table in
`_Config.h`. The PPM table only has 14 positions, and CG022 was not
assigned to any of them. This has now been fixed — CG022 is assigned to
PPM **switch position 6** (replacing a duplicate AFHDS2A entry).

After rebuilding and reflashing with the updated `_Config.h`, set the
rotary switch to position 6 to select CG022.

### If Using Serial Mode (OpenTX/EdgeTX)

The module advertises CG022 in its protocol table. In your radio:
1. Go to Model Setup → Internal/External Module
2. Select "Multi" as the module type
3. Scroll the protocol list and look for **"CG022"**
4. Sub-protocol should be **"Std"**
5. The module will auto-bind (CG022 uses autobind)

If "CG022" does not appear in the protocol list:
- Ensure you rebuilt the firmware with `CG022_NRF24L01_INO` defined in
  `_Config.h` (line 234) — it is enabled by default.
- Reflash the multiprotocol module with the updated firmware.
- On some radios, you may need to re-scan the module's protocol list.

## How to Verify CG022 Is Running

After correctly selecting CG022, an SPI capture of the NRF24L01 bus
should show these distinctive features:

### Init Sequence
```
NRF24L01_Initialize:
  FLUSH_TX, FLUSH_RX
  EN_AA = 0x00, EN_RXADDR = 0x01, SETUP_AW = 0x03
  SETUP_RETR = 0x00, RF_SETUP = 0x01, DYNPD = 0x00, FEATURE = 0x01
  RF_SETUP = 0x07, STATUS = 0x70, CONFIG = 0x0E

LT8900_SetAddress (bind sync word 0x2211):
  SETUP_AW = 0x03
  RX_ADDR_P0 = [AA 88 44 55 55]    ← KEY IDENTIFIER
  TX_ADDR    = [AA 88 44 55 55]    ← MUST match RX_ADDR_P0

LT8900_SetTxRxMode(TX_EN):
  STATUS = 0x70, CONFIG = 0x08     ← TXRX_OFF
  STATUS = 0x70, CONFIG = 0x0E     ← TX_EN
  CONFIG = 0x02                     ← PWR_UP only, NRF CRC disabled
```

### Bind Packet (with FORCE_CG022_ORIGINAL_ID)
```
RF_CH = 0x02 (first channel = LT8900 ch 0 + 2)
FLUSH_TX, STATUS = 0x70
W_TX_PAYLOAD [12 bytes] = [50 00 88 44 CC 60 D5 3F B5 00 62 C6]
```

Payload breakdown (bit-reversed LT8900 data + CRC):
```
  50 = bit_reverse(0x0A)  ← packet length byte
  00 = bit_reverse(0x00)  ← bind marker
  88 = bit_reverse(0x11)  ← rx_tx_addr[0]
  44 = bit_reverse(0x22)  ← rx_tx_addr[1]
  CC = bit_reverse(0x33)  ← rx_tx_addr[2]
  60 = bit_reverse(0x06)  ← rx_tx_addr[3]
  D5 = bit_reverse(0xAB)  ← rx_tx_addr[4]
  3F = bit_reverse(0xFC)  ← extended ID byte 7
  B5 = bit_reverse(0xAD)  ← extended ID byte 8
  00 = bit_reverse(0x00)  ← bind terminator
  62 C6 = CRC-16 (bit-reversed), poly=0x8005, init=0x4402
```

### Channel Hopping (NRF24L01 RF_CH values)
```
Channel 0: RF_CH = 0x02 (2)    → 2402 MHz
Channel 1: RF_CH = 0x2A (42)   → 2442 MHz
Channel 2: RF_CH = 0x0C (12)   → 2412 MHz
Channel 3: RF_CH = 0x34 (52)   → 2452 MHz
Channel 4: RF_CH = 0x16 (22)   → 2422 MHz
Channel 5: RF_CH = 0x3E (62)   → 2462 MHz
Channel 6: RF_CH = 0x20 (32)   → 2432 MHz
Channel 7: RF_CH = 0x48 (72)   → 2472 MHz
  (then repeats from channel 0)
```

### Key Differences From the Captured Protocol

| Feature | CG022 (Expected) | Captured Protocol |
|---------|-------------------|-------------------|
| TX_ADDR == RX_ADDR_P0 | Yes (always identical) | No (different) |
| NRF payload size | 12 bytes | 31 bytes |
| First RF_CH | 0x02 | 0x34 |
| TX/RX mode switching | Never (TX only) | After every packet |
| RX_PW_P0 written | Never | Yes (0x14) |
| CONFIG during TX | 0x02 (no CRC) | 0x02 (matches) |
| Retransmit pattern | Same payload twice | Single TX then RX |

## Recommended Next Steps

1. **Rebuild and reflash** the multiprotocol module firmware using the
   latest code from this branch (which now includes CG022 in PPM
   position 6).

2. **Select CG022** on your radio:
   - PPM mode: rotary switch position 6
   - Serial mode: select "CG022" / "Std" in the protocol list

3. **Verify with SPI capture** (optional): Take a new capture and confirm
   the NRF24L01 TX_ADDR is `[AA 88 44 55 55]` and the first RF_CH is
   `0x02`. These two values uniquely identify CG022 bind mode.

4. **Power cycle the receiver** after starting the MPM module with CG022
   selected, as the CG022 RX expects to see bind packets within the first
   few seconds after power-on.
