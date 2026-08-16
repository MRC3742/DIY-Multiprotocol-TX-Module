# CC2500 XN297DUMP Feature Documentation

## Overview

The CC2500 XN297DUMP feature is an enhanced version of the XN297DUMP protocol (Protocol #63) that enables raw Over-The-Air (OTA) packet sniffing using the CC2500 RF chip. This tool is designed for analyzing and reverse-engineering unknown SOC chips in toy-grade transmitters by capturing raw RF transmissions in the 2.4GHz band.

### Key Features

- **Variable Data Rates**: Support for 250Kbps, 500Kbps, and custom data rates
- **Configurable Packet Length**: Capture packets from 1 to 64 bytes
- **Raw Mode**: Option to capture all packets without address filtering
- **Rich Metadata**: Includes RSSI, LQI, and timestamp for each packet
- **Channel Scanning**: Automatic or manual RF channel selection (0-255)
- **Python Decoder**: Comprehensive analysis and export tools

## Hardware Requirements

- Multi-Protocol TX Module with CC2500 chip installed
- STM32-based module (recommended for serial debug output)
- USB connection for serial monitor output

## Sub-Protocol Configuration

The XN297DUMP protocol supports multiple sub-protocols:

| Sub-Protocol | Value | Description |
|--------------|-------|-------------|
| 250K | 0 | NRF24L01 at 250Kbps |
| 1M | 1 | NRF24L01 at 1Mbps |
| 2M | 2 | NRF24L01 at 2Mbps |
| AUTO | 3 | Automatic detection |
| NRF | 4 | Direct NRF24L01 mode |
| **CC2500** | **5** | **CC2500 raw capture mode** |
| XN297 | 6 | XN297 emulation mode |

**To use the CC2500 feature**: Select sub-protocol 5 (CC2500) when configuring the XN297DUMP protocol.

## Configuration Parameters

### Compile-Time Configuration

Edit the following defines in `XN297Dump_nrf24l01.ino` (lines 20-49):

```cpp
//***************************************************
// CC2500 DUMP Configuration - Modify these for different capture scenarios
//***************************************************

// Data rate configuration: 0=250K (default), 1=500K, 2=Custom
#define CC2500_DUMP_DATA_RATE       0

// Packet length: 1-64 bytes (default 38)
#define CC2500_DUMP_PACKET_LEN      38

// Address/Sync word length: 0-5 bytes (0=no sync, 3-5=sync+addr)
#define CC2500_DUMP_ADDR_LEN        5

// Raw mode: 1=capture all packets (no filtering), 0=use address filtering
#define CC2500_DUMP_RAW_MODE        1

// Custom sync word (used when ADDR_LEN > 0)
#define CC2500_DUMP_SYNC1           0xD3
#define CC2500_DUMP_SYNC0           0x91

// RF channel configuration
// If option == -1 (0xFF), scanner will cycle through channels
// Otherwise, option sets the fixed RF channel (0-255)
#define CC2500_DUMP_DEFAULT_CHANNEL 54

// Output configuration
#define CC2500_DUMP_SHOW_RSSI       1   // Include RSSI in output
#define CC2500_DUMP_SHOW_LQI        1   // Include LQI in output  
#define CC2500_DUMP_SHOW_TIMESTAMP  1   // Include timestamp in output
```

### Runtime Configuration

#### RX_num (Receiver Number)

- **Raw Mode (CC2500_DUMP_RAW_MODE = 1)**: RX_num is ignored
- **Filtered Mode (CC2500_DUMP_RAW_MODE = 0)**: RX_num selects predefined address (0-7)

| RX_num | Predefined Address (5 bytes) |
|--------|------------------------------|
| 0 | AE D2 71 79 46 |
| 1 | 5D A4 E2 F2 8C |
| 2 | BB 49 C5 E5 18 |
| 3 | 76 93 8B CA 30 |
| 4 | ED 27 17 94 61 |
| 5 | DA 4E 2F 28 C2 |
| 6 | AB B4 9C 5E 51 |
| 7 | 57 69 38 BC A3 |

#### Option Field

- **Value 0-255**: Sets fixed RF channel
- **Value -1 (0xFF)**: Uses default channel (CC2500_DUMP_DEFAULT_CHANNEL)

## Usage Instructions

### 1. Compile and Upload Firmware

1. Follow the [STM32 Compilation Guide](Compiling_STM32.md)
2. Ensure `CC2500_INSTALLED` is defined in `_Config.h`
3. Enable serial debug mode by defining `DEBUG_SERIAL` or `ARDUINO_MULTI_DEBUG`
4. Compile and upload to your module

### 2. Configure Protocol

Using your transmitter (e.g., OpenTX, EdgeTX):

1. Create a new model
2. Set Protocol: **XN297DP** (Protocol 63)
3. Set Sub-Protocol: **CC2500** (5)
4. Set RX_num: 0 (for raw mode) or 0-7 (for filtered mode with predefined addresses)
5. Set Option: Your target RF channel (e.g., 54) or -1 for auto

### 3. Connect Serial Monitor

1. Connect USB cable to module
2. Open Arduino IDE Serial Monitor or any serial terminal
3. Set baud rate to **115200**
4. You should see initialization messages

### 4. Start Capturing

Power on the module and you should see output like:

```
CC2500 dump, len=38, rf=54, addr_len=5, bitrate=250K
Mode: RAW (no filtering)
[T:12345us][CH:54][RSSI:-45dBm][LQI:80] PKT: AE D2 71 79 46 10 54 64 25 C6 E7 50 02 AA 49 ...
[T:15678us][CH:54][RSSI:-43dBm][LQI:85] PKT: 5D A4 E2 F2 8C 05 46 41 50 6E 75 00 2A A4 94 ...
```

## Output Format

Each captured packet is displayed with the following format:

```
[T:<timestamp>us][CH:<channel>][RSSI:<rssi>dBm][LQI:<lqi>] PKT: <hex_data>
```

### Field Descriptions

| Field | Description | Range/Units |
|-------|-------------|-------------|
| T: | Timestamp | Microseconds (us) |
| CH: | RF Channel | 0-255 |
| RSSI: | Received Signal Strength Indicator | -100 to 0 dBm |
| LQI: | Link Quality Indicator | 0-127 (higher is better) |
| PKT: | Packet data | Hex bytes (space-separated) |

### RSSI Interpretation

| RSSI Range | Signal Quality |
|------------|----------------|
| > -50 dBm | Excellent (very close range) |
| -50 to -70 dBm | Good |
| -70 to -85 dBm | Fair |
| < -85 dBm | Poor (long range or obstacles) |

### LQI Interpretation

| LQI Range | Link Quality |
|-----------|--------------|
| 100-127 | Excellent |
| 80-99 | Good |
| 60-79 | Fair |
| < 60 | Poor |

## Python Decoder Tool

A comprehensive Python script is provided for analyzing captured packets.

### Installation

1. Ensure Python 3.6+ is installed
2. Install required package:
   ```bash
   pip install pyserial
   ```

### Basic Usage

#### Real-Time Capture from Serial Port

```bash
# Windows
python tools/xn297dump_decoder.py --port COM3 --display --analyze

# Linux/Mac
python tools/xn297dump_decoder.py --port /dev/ttyUSB0 --display --analyze
```

#### Analyze Saved Capture File

```bash
python tools/xn297dump_decoder.py --file capture.txt --analyze
```

#### Export to CSV

```bash
python tools/xn297dump_decoder.py --port COM3 --duration 60 --export packets.csv
```

#### Export to JSON

```bash
python tools/xn297dump_decoder.py --file capture.txt --json packets.json
```

### Command-Line Options

| Option | Description |
|--------|-------------|
| `--port PORT` | Serial port (e.g., COM3, /dev/ttyUSB0) |
| `--baud BAUD` | Baud rate (default: 115200) |
| `--file FILE` | Read from file instead of serial port |
| `--export FILE` | Export packets to CSV file |
| `--json FILE` | Export packets to JSON file |
| `--analyze` | Show analysis after capture |
| `--display` | Display packets as they arrive |
| `--duration SEC` | Capture duration in seconds (serial only) |
| `--interval N` | Show analysis every N packets during capture |

### Analysis Features

The decoder provides:

1. **Packet Statistics**
   - Total packets captured
   - Packet length statistics (min/max/avg)
   - RSSI statistics
   - LQI statistics

2. **Channel Analysis**
   - List of channels used
   - Packet count per channel
   - Percentage distribution

3. **Pattern Detection**
   - Unique patterns in first 4 bytes
   - Repeating patterns (frequency analysis)
   - Pattern correlation across packets

4. **Timing Analysis**
   - Minimum/maximum/average intervals between packets
   - Helps identify packet timing patterns
   - Useful for determining transmission rates

### Example Analysis Output

```
============================================================
PACKET ANALYSIS
============================================================

Total packets: 245

Packet lengths: min=38, max=38, avg=38.0

RSSI: min=-65dBm, max=-42dBm, avg=-51.2dBm
LQI:  min=75, max=110, avg=92.4

Channels used (3):
  CH 54: 198 packets (80.8%)
  CH 55:  32 packets (13.1%)
  CH 56:  15 packets (6.1%)

Unique patterns (first 4 bytes): 8

Repeating patterns (≥3 occurrences):
  aed27179: 198 times (80.8%)
  5da4e2f2: 32 times (13.1%)
  bb49c5e5: 15 times (6.1%)

Timing intervals:
  Min: 1850us
  Max: 2150us
  Avg: 2000.3us
```

## Common Use Cases

### Scenario 1: Unknown Transmitter Analysis

**Goal**: Capture and analyze transmissions from an unknown toy-grade transmitter.

**Steps**:
1. Set `CC2500_DUMP_RAW_MODE = 1` (capture all packets)
2. Set `CC2500_DUMP_PACKET_LEN` to maximum expected size (e.g., 64)
3. Set option to -1 or known channel
4. Power on the unknown transmitter
5. Use Python decoder to analyze:
   ```bash
   python tools/xn297dump_decoder.py --port COM3 --duration 30 --analyze --export unknown_tx.csv
   ```
6. Review analysis for:
   - Active channels
   - Packet lengths
   - Repeating patterns
   - Timing characteristics

### Scenario 2: Specific Protocol Reverse Engineering

**Goal**: Decode a specific protocol with known sync word.

**Steps**:
1. Set `CC2500_DUMP_RAW_MODE = 0` (filtered mode)
2. Configure sync word: `CC2500_DUMP_SYNC1` and `CC2500_DUMP_SYNC0`
3. Set known packet length
4. Capture packets and export to JSON:
   ```bash
   python tools/xn297dump_decoder.py --port COM3 --json protocol_data.json
   ```
5. Analyze JSON file to understand packet structure

### Scenario 3: Multi-Channel Hopping Analysis

**Goal**: Identify frequency hopping sequence.

**Steps**:
1. Set `CC2500_DUMP_RAW_MODE = 1`
2. Set option to -1 (will use default channel, but can be changed dynamically)
3. Capture while cycling through channels
4. Use Python decoder with `--analyze` to see channel distribution
5. Review timing analysis to determine hop rate

## Troubleshooting

### No Packets Captured

**Possible Causes**:
- Incorrect RF channel
- Packet length mismatch
- Signal too weak (check RSSI)
- Sync word mismatch (in filtered mode)

**Solutions**:
- Enable RAW mode (`CC2500_DUMP_RAW_MODE = 1`)
- Increase packet length to maximum (64)
- Move transmitter closer to module
- Try different RF channels

### Garbled/Invalid Packets

**Possible Causes**:
- Incorrect data rate
- Packet length too short
- Interference on channel

**Solutions**:
- Try different data rates (250K, 500K)
- Increase packet length
- Change to a different RF channel
- Check for nearby 2.4GHz devices (WiFi, Bluetooth)

### Serial Output Not Showing

**Possible Causes**:
- Serial debug not enabled
- Wrong baud rate
- USB connection issue

**Solutions**:
- Ensure `DEBUG_SERIAL` is defined in compilation
- Verify serial monitor baud rate is 115200
- Try different USB port or cable

### High Packet Loss

**Possible Causes**:
- Weak signal
- Fast hopping rate
- Module overload

**Solutions**:
- Move transmitter closer
- Use fixed channel instead of scanning
- Reduce packet length if possible

## Technical Details

### CC2500 Configuration

The CC2500 is configured with the following key settings for 250K mode:

- **Frequency**: 2.400GHz base + (channel × 333.25kHz)
- **Data Rate**: 250Kbps (configurable)
- **Modulation**: GFSK (Gaussian Frequency Shift Keying)
- **Deviation**: 126.953125 kHz
- **RX Filter BW**: 203.125 kHz
- **Packet Mode**: Fixed length or variable (based on configuration)
- **CRC**: Optional (configurable)
- **Address Check**: Optional (based on RAW_MODE setting)

### Frequency Calculation

```
Frequency (MHz) = 2400.0 + (channel × 0.33325)
```

Example channels:
- Channel 0: 2400.00 MHz
- Channel 54: 2417.955 MHz (near WiFi channel 3)
- Channel 84: 2427.99 MHz (near WiFi channel 4)
- Channel 255: 2484.98 MHz (above WiFi band)

### Packet Structure (Raw Mode)

In raw mode, packets are captured as-is from the air:

```
┌─────────────┬──────────────┬──────────┬──────┬─────┐
│ Sync Word   │ Address/Data │ Payload  │ RSSI │ LQI │
│ (2-5 bytes) │ (0-3 bytes)  │ (N bytes)│(1 B) │(1 B)│
└─────────────┴──────────────┴──────────┴──────┴─────┘
```

RSSI and LQI are appended by CC2500 hardware automatically.

### Performance Characteristics

- **Maximum Packet Rate**: ~500 packets/second (250Kbps, 38-byte packets)
- **Channel Switch Time**: ~1ms (with calibration)
- **RX Sensitivity**: ~-100 dBm (typical)
- **Maximum Range**: 50-100m (depending on TX power and environment)

## Advanced Features

### Custom Data Rates

To use custom data rates, modify CC2500 modem configuration registers:

```cpp
#define CC2500_DUMP_DATA_RATE 2  // Enable custom mode

// In CC2500 initialization, add:
CC2500_WriteReg(CC2500_10_MDMCFG4, 0xXX);  // Configure for desired rate
CC2500_WriteReg(CC2500_11_MDMCFG3, 0xXX);  // Data rate mantissa
```

Consult CC2500 datasheet for register values.

### Variable Packet Length Mode

To enable variable packet length:

```cpp
CC2500_WriteReg(CC2500_08_PKTCTRL0, 0x01);  // Variable packet length mode
CC2500_WriteReg(CC2500_06_PKTLEN, 0xFF);    // Max packet length
```

First byte received will indicate packet length.

### Custom Preamble/Sync Detection

Modify modem configuration register:

```cpp
// 16/16 sync word detection (stricter)
CC2500_WriteReg(CC2500_12_MDMCFG2, 0x12);

// 15/16 sync word detection (more lenient)
CC2500_WriteReg(CC2500_12_MDMCFG2, 0x11);

// No preamble/sync (raw mode)
CC2500_WriteReg(CC2500_12_MDMCFG2, 0x10);
```

## References

### Related Documentation

- [Advanced XN297Ldump](Advanced_XN297Ldump.md) - Original XN297DUMP feature
- [Compiling STM32](Compiling_STM32.md) - How to compile firmware
- [Advanced Debug](Advanced_Debug.md) - Serial debug setup

### External Resources

- [CC2500 Datasheet](http://www.ti.com/lit/ds/symlink/cc2500.pdf) - Texas Instruments
- [2.4GHz ISM Band](https://en.wikipedia.org/wiki/ISM_band) - Wikipedia
- [GFSK Modulation](https://en.wikipedia.org/wiki/Frequency-shift_keying#Gaussian_frequency-shift_keying) - Wikipedia

## Changelog

### Version 1.0 (2024)
- Initial implementation of CC2500 XN297DUMP feature
- Added configurable data rate, packet length, and address
- Implemented RAW mode for unfiltered packet capture
- Added RSSI, LQI, and timestamp to output
- Created Python decoder with analysis capabilities
- Comprehensive documentation

## Contributing

If you find bugs or have suggestions for improvements:

1. Open an issue on the GitHub repository
2. Submit a pull request with your changes
3. Include test results and documentation updates

## License

This feature is part of the DIY Multiprotocol TX Module project and is licensed under the GNU General Public License v3.0.

## Support

For questions and support:
- GitHub Issues: https://github.com/pascallanger/DIY-Multiprotocol-TX-Module/issues
- RC Groups Thread: [Multi-Protocol TX Module](https://www.rcgroups.com/forums/showthread.php?2165676)

---

**Document Version**: 1.0  
**Last Updated**: 2024-08-16  
**Author**: Multi-Protocol TX Module Team
