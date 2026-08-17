# Multi-Protocol TX Module Tools

This directory contains utility scripts for working with the Multi-Protocol TX Module.

## xn297dump_decoder.py

Python script for decoding and analyzing XN297DUMP packet captures from the CC2500 chip.

### Requirements

- Python 3.6+
- pyserial (for real-time serial capture)

Install dependencies:
```bash
pip install pyserial
```

### Usage

#### Real-time capture from serial port
```bash
python xn297dump_decoder.py --port COM3 --display --analyze
```

#### Analyze saved capture file
```bash
python xn297dump_decoder.py --file capture.txt --analyze
```

#### Export to CSV
```bash
python xn297dump_decoder.py --port /dev/ttyUSB0 --duration 60 --export packets.csv
```

For complete documentation, see [CC2500 XN297DUMP Feature](../docs/CC2500_XN297DUMP_Feature.md).

### Features

- Real-time packet parsing
- RSSI, LQI, and timing analysis
- Pattern detection
- CSV and JSON export
- Statistical analysis
- Channel distribution analysis

## License

These tools are part of the DIY Multiprotocol TX Module project and are licensed under the GNU General Public License v3.0.
