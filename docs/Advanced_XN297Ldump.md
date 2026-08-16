# XN297L dump feature

To get the XN297L dump feature working on your module you must know:
1. How to buid the firmware from the source code available on this GitHub. To do so follow this page: [Compiling and programming the STM32 module](Compiling_STM32.md).
1. How to enable serial debug [MULTI-Module Serial Debug](Advanced_Debug.md).

## Quick Start

Procedure to use the XN297L dump feature:
1. Start the Multi module in serial debug mode with the Arduion IDE Serial Monitor open<br> <img src="images/Serial_Monitor_2.png" />
1. Select the protocol XN297DP, 63 or "Custom 63" to enable the XN297L Dump protocol
1. This protocol parameters are:
   * sub_protocol or type or the second number after "Custom 63" is used to set the transmission speed: 0=250Kbps, 1=1Mbps, 2=2Mbps and 3=Auto.
     * Auto is the recommended mode since it gives many information like channels, timing, order as well as finding bytes meaning
   * RX_num or Receiver number sets the address length 3, 4 or 5 bytes. Any other value will default to an address length of 5 bytes.
   * option sets the RF channel number used to receive packets between 0..84 . A value of -1 will automatically scan all channels one by one. Any other value will default to the RF channel 0.

## Sub-Protocols

The XN297DUMP protocol supports multiple sub-protocols:

| Sub-Protocol | Value | Description |
|--------------|-------|-------------|
| 250K | 0 | NRF24L01 at 250Kbps |
| 1M | 1 | NRF24L01 at 1Mbps |
| 2M | 2 | NRF24L01 at 2Mbps |
| AUTO | 3 | Automatic detection |
| NRF | 4 | Direct NRF24L01 mode |
| **CC2500** | **5** | **CC2500 raw capture mode (NEW!)** |
| XN297 | 6 | XN297 emulation mode |

## CC2500 Raw Capture Mode

**NEW**: Enhanced CC2500 sub-protocol for raw OTA packet sniffing with configurable parameters!

For complete documentation on the CC2500 feature, see: [CC2500 XN297DUMP Feature Guide](CC2500_XN297DUMP_Feature.md)

Key features:
- **Variable packet length** (1-64 bytes)
- **Raw mode** (no address filtering)
- **RSSI and LQI** in output
- **Timestamp** for each packet
- **Python decoder** for analysis and export
- **Configurable data rates** (250K, 500K, custom)

Example usage:
```
Protocol: XN297DP (63)
Sub-Protocol: CC2500 (5)
RX_num: 0 (for raw mode)
Option: 54 (RF channel) or -1 (auto)
```

Output format:
```
[T:12345us][CH:54][RSSI:-45dBm][LQI:80] PKT: AE D2 71 79 46 10 54 64 25 ...
```

Examples:
TBC
