# Flash Space Optimization Options for 128KB STM32F103 MULTI_AIR Build

## Current State

The 128KB STM32F103 MULTI_AIR build (`build_release_stm32f1_4in1_no_debug`) currently disables
PPM mode and adds `MULTI_AIR` to exclude surface-only protocols. Despite these exclusions,
the build is tight on flash space and requires removal of additional protocols to fit.

### Already Excluded by MULTI_AIR (Validate.h lines 394-415)
These protocols are already `#undef`'d when `MULTI_AIR` is defined:

| Protocol | Type | Reason |
|----------|------|--------|
| JOYSWAY_A7105_INO | Surface | Boat protocol |
| LOSI_CYRF6936_INO | Surface | RC car (requires DSM) |
| TRAXXAS_CYRF6936_INO | Surface | RC car |
| EAZYRC_NRF24L01_INO | Surface | RC car |
| KYOSHO3_CYRF6936_INO | Surface | RC car |
| MOULDKG_NRF24L01_INO | Surface | RC car |
| SHENQI_NRF24L01_INO | Surface | RC car |
| SHENQI2_NRF24L01_INO | Surface | RC car |
| JIABAILE_NRF24L01_INO | Surface | RC car |
| UDIRC_CCNRF_INO | Surface | RC car |
| KAMTOM_NRF24L01_INO | Surface | RC car |
| WL91X_CCNRF_INO | Surface | RC car |
| WPL_NRF24L01_INO | Surface | RC car |
| RLINK_CC2500_INO | Flash saving | RadioLink |
| CABELL_NRF24L01_INO | Flash saving | Cabell |
| REDPINE_CC2500_INO | Flash saving | Redpine |

### Already Trimmed Within Protocol Files (sub-protocol level)
Several protocols already have internal `#ifndef MULTI_AIR` guards that strip
surface/niche sub-protocols:

- **DSM_cyrf6936.ino**: DSMR and DSM2_SFC sub-protocols disabled, DSMR_ID_FREQ table (594 bytes PROGMEM) excluded
- **Pelikan_a7105.ino**: SCX24 sub-protocol code trimmed
- **Kyosho_a7105.ino**: Hype sub-protocol disabled
- **RadioLink_cc2500.ino**: Surface sub-protocols disabled
- **AFHDS2A_a7105.ino**: PPM/SBUS modes reduced, Gyro sub-protocols trimmed
- **Hisky_nrf24l01.ino**: HK310 sub-protocol disabled

---

## Optimization Options

### Option 1: Disable Additional Niche/Rare Air Protocols (~2-6 KB each)

These protocols are still enabled in MULTI_AIR but serve niche/uncommon aircraft.
Each protocol removal saves the compiled code for that protocol plus its entry in
the `multi_protocols[]` table (~14 bytes per entry) and associated strings.

**Source-to-compiled ratio**: On ARM Thumb-2 (STM32F103), compiled code is typically
~40-60% of source size. A 10KB source file typically compiles to ~4-6KB of flash.

| Protocol | Source Size | Est. Flash | Use Case |
|----------|-----------|------------|----------|
| CFLIE_NRF24L01_INO | 31,151 B | **~5-6 KB** | Crazyflie (very niche) |
| PROPEL_NRF24L01_INO | 11,132 B | ~2-3 KB | Propel 74-Z |
| NCC1701_NRF24L01_INO | 8,161 B | ~2-3 KB | Star Trek toy |
| Q303_CCNRF_INO | 9,270 B | ~2-3 KB | Q303/CX35/CX10D |
| Q90C_CCNRF_INO | 5,494 B | ~1-2 KB | Q90C |
| BUMBLEB_CCNRF_INO | 5,110 B | ~1-2 KB | BumbleB |
| SCORPIO_CYRF6936_INO | 3,854 B | ~1-2 KB | Scorpio |
| BLUEFLY_CCNRF_INO | 3,013 B | ~1 KB | BlueFly |
| DM002_NRF24L01_INO | 3,860 B | ~1-2 KB | DM002 |
| GW008_NRF24L01_INO | 3,948 B | ~1-2 KB | GW008 |
| POTENSIC_NRF24L01_INO | 3,483 B | ~1-2 KB | Potensic A20 |
| REALACC_NRF24L01_INO | 5,647 B | ~1-2 KB | Realacc |
| XERALL_NRF24L01_INO | 7,315 B | ~2-3 KB | Xerall |
| ZSX_NRF24L01_INO | 2,605 B | ~1 KB | ZSX 280 JJRC |
| JJRC345_NRF24L01_INO | 5,430 B | ~1-2 KB | JJRC345/SkyTumbler |
| E016H_NRF24L01_INO | 3,990 B | ~1-2 KB | E016H |
| FQ777_NRF24L01_INO | 5,840 B | ~1-2 KB | FQ777 |
| FY326_NRF24L01_INO | 6,157 B | ~2 KB | FY326 |
| CG023_NRF24L01_INO | 4,360 B | ~1-2 KB | CG023/YD829 |
| H36_NRF24L01_INO | 3,234 B | ~1 KB | H36 |
| H8_3D_NRF24L01_INO | 7,459 B | ~2-3 KB | H8 3D |

**Recommended high-impact removals** (combined ~12-16 KB savings):

| Protocol | Est. Flash Saving | Impact |
|----------|------------------|--------|
| CFLIE_NRF24L01_INO | ~5-6 KB | Very niche (Crazyflie research drone) |
| PROPEL_NRF24L01_INO | ~2-3 KB | Discontinued toy |
| NCC1701_NRF24L01_INO | ~2-3 KB | Star Trek toy |
| XERALL_NRF24L01_INO | ~2-3 KB | Niche |
| SCORPIO_CYRF6936_INO | ~1-2 KB | Niche |

**Total estimated savings: ~12-16 KB**

### Option 2: Disable Additional Sub-Protocol Features Within Existing Protocols

These are internal code blocks that can be conditionally compiled out under MULTI_AIR
with minimal impact on core air functionality:

| Feature | Location | Est. Savings | Notes |
|---------|----------|-------------|-------|
| DSM_FWD_PGM | DSM_cyrf6936.ino + Multiprotocol.ino | ~200-400 B | Forward programming feature; useful but optional |
| SCANNER_CC2500_INO | Scanner_cc2500.ino | ~2-3 KB | Diagnostic tool, not flight protocol |
| DSM_RX_CYRF6936_INO | DSM_Rx_cyrf6936.ino + DSM.ino | ~3-4 KB | DSM RX mode (trainer) |
| AFHDS2A_RX_A7105_INO | AFHDS2A_Rx_a7105.ino | ~1.5-2 KB | AFHDS2A RX mode (trainer) |
| BAYANG_RX_NRF24L01_INO | Bayang_Rx_nrf24l01.ino | ~1.5-2 KB | Bayang RX mode (trainer) |
| FRSKY_RX_CC2500_INO | FrSky_Rx_cc2500.ino | ~4-5 KB | FrSky RX mode (trainer) |
| SEND_CPPM | Multiprotocol.ino | ~200-400 B | CPPM trainer output |

**Total estimated savings: ~13-17 KB** (if all RX/trainer/scanner features removed)

### Option 3: Optimize Large PROGMEM Data Tables

| Table | File | Current Size | Optimization | Est. Savings |
|-------|------|-------------|-------------|-------------|
| HOTT_hop[][] | HOTT_cc2500.ino | 1,200 B | Compute at runtime from seed/algorithm instead of lookup | ~800-1,000 B |
| DSM_pncodes[][] | DSM.ino | ~480 B | Already in PROGMEM; could compress with runtime decompression | ~200-300 B |
| BUGS_most_popular_67_cycle[] + BUGS_most_popular_01[] + BUGS_hop[] | Bugs_a7105.ino | ~300+ B | Already in PROGMEM; minimal savings |~100-200 B |
| BUGSMINI_RF_chans[][] | BUGSMINI_nrf24l01.ino | ~200 B | Already in PROGMEM; minimal | ~50-100 B |
| A7105 register tables (10 tables) | A7105_SPI.ino | ~480 B total | Could deduplicate common values | ~100-200 B |
| FrSkyX_CRC_Short[] | FrSkyDVX_common.ino | ~128 B | Compute at runtime | ~100 B |
| CC2500 config tables (6 tables) | FrSkyDVX_common.ino | ~300 B | Deduplicate common registers | ~100-150 B |

**Total estimated savings: ~1.5-2.5 KB**

**Note**: These optimizations involve refactoring existing code, which carries risk of
introducing bugs. The savings are modest compared to protocol removal.

### Option 4: Reduce Protocol String Table

The `Multi_Protos.ino` file contains ~120 protocol name strings and ~50 sub-protocol
string tables. These are stored in flash memory.

| Item | Est. Size | Optimization |
|------|----------|-------------|
| Protocol name strings (STR_*) | ~900 B | Could use shorter names or indices |
| Sub-protocol string tables (STR_SUBTYPE_*) | ~2-3 KB | Could conditionally exclude unused sub-protocol strings |
| multi_protocols[] array entries | ~14 B/entry × ~80 entries = ~1.1 KB | Entries auto-excluded when protocol disabled |

Strings for disabled protocols are already excluded by the compiler (dead code
elimination) since they're only referenced in conditionally-compiled blocks.

**Potential savings**: Already handled by protocol disable. No additional action needed
unless strings are referenced elsewhere unconditionally.

### Option 5: Compiler Optimization Flags

| Flag | Description | Est. Savings |
|------|------------|-------------|
| `-Os` | Optimize for size (likely already used by Arduino STM32 core) | 0 (already used) |
| `-flto` | Link-Time Optimization - enables cross-file dead code elimination | ~2-5 KB |
| `-ffunction-sections -fdata-sections --gc-sections` | Remove unused functions/data at link time | ~1-3 KB |

**Note**: The Arduino STM32 board definitions control these flags. Changing them requires
modifying the board platform definition, not the sketch code. The Multi STM32 board
package may already use some of these. LTO in particular can provide significant savings
but may increase compile time and can occasionally cause issues with interrupt handlers.

### Option 6: Disable Telemetry Features Selectively

The Telemetry.ino file (~34 KB source, est. ~6-8 KB compiled) contains many optional
telemetry backends:

| Feature | Controlled By | Est. Savings |
|---------|--------------|-------------|
| SPORT_TELEMETRY + SPORT_SEND | Used by FrSky X/R9 | ~1-2 KB (core protocols, keep) |
| HUB_TELEMETRY | Used by FrSky D + many protocols | ~0.5-1 KB (widely used, keep) |
| DSM_TELEMETRY | Used by DSM protocols | ~0.5-1 KB |
| HOTT_FW_TELEMETRY | Used by HoTT only | ~0.5-1 KB |
| MLINK_FW_TELEMETRY | Used by M-Link only | ~0.3-0.5 KB |
| MULTI_TELEMETRY / MULTI_STATUS | Multi protocol status reporting | ~0.5-1 KB |

Selectively disabling telemetry for less-used protocols saves modest flash but reduces
functionality. The telemetry code is already well-guarded with `#ifdef` blocks.

**Potential savings: ~1-3 KB** (if niche telemetry features disabled)

---

## Summary: Recommended Options by Impact

### Tier 1 - Highest Impact, Lowest Risk (~12-16 KB)
**Disable niche/rare air protocols in Validate.h under MULTI_AIR:**
- `CFLIE_NRF24L01_INO` (~5-6 KB) - Crazyflie research drone
- `PROPEL_NRF24L01_INO` (~2-3 KB) - Discontinued toy
- `NCC1701_NRF24L01_INO` (~2-3 KB) - Star Trek toy
- `XERALL_NRF24L01_INO` (~2-3 KB) - Niche drone
- `SCORPIO_CYRF6936_INO` (~1-2 KB) - Niche

### Tier 2 - High Impact, Low Risk (~8-12 KB)
**Disable trainer/RX protocols and diagnostic tools:**
- `SCANNER_CC2500_INO` (~2-3 KB) - Diagnostic, not flight
- `DSM_RX_CYRF6936_INO` (~3-4 KB) - Trainer mode
- `FRSKY_RX_CC2500_INO` (~4-5 KB) - Trainer mode
- `AFHDS2A_RX_A7105_INO` (~1.5-2 KB) - Trainer mode
- `BAYANG_RX_NRF24L01_INO` (~1.5-2 KB) - Trainer mode

### Tier 3 - Moderate Impact, Low Risk (~5-10 KB)
**Disable more niche protocols:**
- `Q303_CCNRF_INO` (~2-3 KB)
- `Q90C_CCNRF_INO` (~1-2 KB)
- `BUMBLEB_CCNRF_INO` (~1-2 KB)
- `BLUEFLY_CCNRF_INO` (~1 KB)
- `DM002_NRF24L01_INO` (~1-2 KB)
- `GW008_NRF24L01_INO` (~1-2 KB)
- `POTENSIC_NRF24L01_INO` (~1-2 KB)
- `REALACC_NRF24L01_INO` (~1-2 KB)
- `H36_NRF24L01_INO` (~1 KB)
- `ZSX_NRF24L01_INO` (~1 KB)

### Tier 4 - Moderate Impact, Moderate Risk (~1.5-5 KB)
**Optimize data tables and compiler settings:**
- HOTT hop table runtime generation (~0.8-1 KB)
- LTO compiler flag (~2-5 KB, needs testing with board package)
- Data table deduplication (~0.5-1 KB)

### Tier 5 - Low Impact (~1-3 KB)
**Selective telemetry and feature reduction:**
- Disable DSM_FWD_PGM (~200-400 B)
- Disable niche telemetry features (~1-2 KB)
- Disable SEND_CPPM (~200-400 B)

---

## Implementation Notes

All protocol-level changes (Tiers 1-3) are implemented by adding `#undef` lines to the
`#ifdef MULTI_AIR` block in `Validate.h` (lines 394-415). This is the same pattern used
by the existing exclusions and requires no changes to protocol source files.

Example addition to `Validate.h`:
```c
#ifdef MULTI_AIR
    // Existing exclusions...
    #undef JOYSWAY_A7105_INO
    // ...

    // New exclusions for flash savings
    #undef CFLIE_NRF24L01_INO
    #undef PROPEL_NRF24L01_INO
    #undef NCC1701_NRF24L01_INO
    #undef XERALL_NRF24L01_INO
    #undef SCORPIO_CYRF6936_INO
    // etc.
#endif
```

Sub-protocol optimizations (Option 2 items) require adding `#ifndef MULTI_AIR` / `#endif`
guards around code blocks within the specific protocol files, following the existing
pattern used in DSM_cyrf6936.ino, Pelikan_a7105.ino, etc.

Data table optimizations (Option 3) require code refactoring and should be thoroughly
tested to avoid introducing protocol-breaking bugs.
