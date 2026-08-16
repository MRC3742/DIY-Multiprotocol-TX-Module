#!/usr/bin/env python3
"""
XN297DUMP Decoder - Parse and analyze raw OTA packet captures from CC2500 XN297DUMP

This script reads serial output from the Multi-Protocol TX Module running
the XN297DUMP protocol with CC2500 chip, parses the packets, and provides
analysis and visualization capabilities.

Usage:
    python xn297dump_decoder.py --port COM3 --baud 115200
    python xn297dump_decoder.py --file capture.txt --analyze
    python xn297dump_decoder.py --port /dev/ttyUSB0 --export packets.csv
"""

import serial
import argparse
import re
import csv
import json
import sys
from datetime import datetime
from collections import defaultdict, Counter
import time

class PacketParser:
    """Parse XN297DUMP packet format"""
    
    # Regex pattern to match packet output format:
    # [T:12345us][CH:54][RSSI:-45dBm][LQI:80] PKT: 01 02 03 ... FF
    # [CH:54][RSSI:-45dBm][LQI:80] PKT: 01 02 03 ... FF (no timestamp)
    # [CH:54] PKT: 01 02 03 ... FF (minimal)
    PACKET_PATTERN = re.compile(
        r'(?:\[T:(?P<timestamp>\d+)us\])?'
        r'\[CH:(?P<channel>\d+)\]'
        r'(?:\[RSSI:(?P<rssi>-?\d+)dBm\])?'
        r'(?:\[LQI:(?P<lqi>\d+)\])?\s+'
        r'PKT:\s+(?P<data>[0-9A-Fa-f\s]+)'
    )
    
    @staticmethod
    def parse_line(line):
        """Parse a single line of output"""
        match = PacketParser.PACKET_PATTERN.search(line)
        if not match:
            return None
        
        data_hex = match.group('data').strip().replace(' ', '')
        if not data_hex:
            return None
        
        # Convert hex string to bytes
        try:
            data_bytes = bytes.fromhex(data_hex)
        except ValueError:
            return None
        
        packet = {
            'timestamp': int(match.group('timestamp')) if match.group('timestamp') else None,
            'channel': int(match.group('channel')),
            'rssi': int(match.group('rssi')) if match.group('rssi') else None,
            'lqi': int(match.group('lqi')) if match.group('lqi') else None,
            'data': data_bytes,
            'hex': data_hex,
            'length': len(data_bytes),
            'raw_line': line.strip()
        }
        
        return packet

class PacketAnalyzer:
    """Analyze captured packets for patterns"""
    
    def __init__(self):
        self.packets = []
        self.patterns = defaultdict(int)
        self.channels = Counter()
        self.data_patterns = defaultdict(list)
        
    def add_packet(self, packet):
        """Add a packet to the analyzer"""
        self.packets.append(packet)
        self.channels[packet['channel']] += 1
        
        # Extract patterns from first few bytes
        if len(packet['data']) >= 4:
            pattern = packet['data'][:4].hex()
            self.patterns[pattern] += 1
            self.data_patterns[pattern].append(packet)
    
    def get_statistics(self):
        """Get packet statistics"""
        if not self.packets:
            return {}
        
        rssi_values = [p['rssi'] for p in self.packets if p['rssi'] is not None]
        lqi_values = [p['lqi'] for p in self.packets if p['lqi'] is not None]
        lengths = [p['length'] for p in self.packets]
        
        stats = {
            'total_packets': len(self.packets),
            'channels': dict(self.channels.most_common()),
            'unique_patterns': len(self.patterns),
            'packet_lengths': {
                'min': min(lengths) if lengths else 0,
                'max': max(lengths) if lengths else 0,
                'avg': sum(lengths) / len(lengths) if lengths else 0
            }
        }
        
        if rssi_values:
            stats['rssi'] = {
                'min': min(rssi_values),
                'max': max(rssi_values),
                'avg': sum(rssi_values) / len(rssi_values)
            }
        
        if lqi_values:
            stats['lqi'] = {
                'min': min(lqi_values),
                'max': max(lqi_values),
                'avg': sum(lqi_values) / len(lqi_values)
            }
        
        return stats
    
    def find_repeating_patterns(self, min_occurrences=3):
        """Find repeating patterns in the data"""
        repeated = {}
        for pattern, count in self.patterns.items():
            if count >= min_occurrences:
                repeated[pattern] = {
                    'count': count,
                    'percentage': (count / len(self.packets)) * 100
                }
        return repeated
    
    def analyze_timing(self):
        """Analyze packet timing intervals"""
        if len(self.packets) < 2:
            return None
        
        intervals = []
        for i in range(1, len(self.packets)):
            if self.packets[i]['timestamp'] and self.packets[i-1]['timestamp']:
                interval = self.packets[i]['timestamp'] - self.packets[i-1]['timestamp']
                intervals.append(interval)
        
        if not intervals:
            return None
        
        return {
            'min_interval': min(intervals),
            'max_interval': max(intervals),
            'avg_interval': sum(intervals) / len(intervals),
            'intervals': intervals
        }

class SerialReader:
    """Read packets from serial port"""
    
    def __init__(self, port, baudrate=115200, timeout=1):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None
        
    def open(self):
        """Open serial connection"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
            print(f"Connected to {self.port} at {self.baudrate} baud")
            return True
        except serial.SerialException as e:
            print(f"Error opening serial port: {e}")
            return False
    
    def close(self):
        """Close serial connection"""
        if self.ser and self.ser.is_open:
            self.ser.close()
    
    def read_packets(self, callback, duration=None):
        """Read packets and call callback for each one"""
        if not self.ser or not self.ser.is_open:
            return
        
        start_time = time.time()
        packet_count = 0
        
        try:
            while True:
                if duration and (time.time() - start_time) > duration:
                    break
                
                line = self.ser.readline().decode('utf-8', errors='ignore')
                if line:
                    packet = PacketParser.parse_line(line)
                    if packet:
                        packet_count += 1
                        callback(packet)
                    else:
                        # Print non-packet lines for debugging
                        print(line.strip())
        except KeyboardInterrupt:
            print(f"\nStopped. Captured {packet_count} packets.")
        except Exception as e:
            print(f"Error reading serial: {e}")

class FileReader:
    """Read packets from file"""
    
    @staticmethod
    def read_packets(filename, callback):
        """Read packets from file and call callback for each one"""
        packet_count = 0
        try:
            with open(filename, 'r') as f:
                for line in f:
                    packet = PacketParser.parse_line(line)
                    if packet:
                        packet_count += 1
                        callback(packet)
            print(f"Processed {packet_count} packets from {filename}")
        except FileNotFoundError:
            print(f"Error: File {filename} not found")
        except Exception as e:
            print(f"Error reading file: {e}")

class Exporter:
    """Export packets to various formats"""
    
    @staticmethod
    def to_csv(packets, filename):
        """Export packets to CSV"""
        if not packets:
            print("No packets to export")
            return
        
        with open(filename, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=[
                'timestamp', 'channel', 'rssi', 'lqi', 'length', 'hex'
            ])
            writer.writeheader()
            for packet in packets:
                writer.writerow({
                    'timestamp': packet['timestamp'],
                    'channel': packet['channel'],
                    'rssi': packet['rssi'],
                    'lqi': packet['lqi'],
                    'length': packet['length'],
                    'hex': packet['hex']
                })
        print(f"Exported {len(packets)} packets to {filename}")
    
    @staticmethod
    def to_json(packets, filename):
        """Export packets to JSON"""
        if not packets:
            print("No packets to export")
            return
        
        # Convert bytes to hex strings for JSON
        json_packets = []
        for packet in packets:
            p = packet.copy()
            p['data'] = packet['hex']
            del p['raw_line']
            json_packets.append(p)
        
        with open(filename, 'w') as f:
            json.dump(json_packets, f, indent=2)
        print(f"Exported {len(packets)} packets to {filename}")

def print_packet(packet, index=None):
    """Pretty print a packet"""
    prefix = f"[{index:04d}] " if index is not None else ""
    
    print(f"{prefix}Channel: {packet['channel']}", end='')
    if packet['timestamp'] is not None:
        print(f" | Time: {packet['timestamp']:6d}us", end='')
    if packet['rssi'] is not None:
        print(f" | RSSI: {packet['rssi']:4d}dBm", end='')
    if packet['lqi'] is not None:
        print(f" | LQI: {packet['lqi']:3d}", end='')
    print(f" | Len: {packet['length']:2d}")
    
    # Print hex dump
    hex_str = ' '.join(packet['hex'][i:i+2] for i in range(0, len(packet['hex']), 2))
    print(f"      Data: {hex_str}")

def print_analysis(analyzer):
    """Print analysis results"""
    stats = analyzer.get_statistics()
    
    print("\n" + "="*60)
    print("PACKET ANALYSIS")
    print("="*60)
    
    print(f"\nTotal packets: {stats['total_packets']}")
    
    print(f"\nPacket lengths: min={stats['packet_lengths']['min']}, "
          f"max={stats['packet_lengths']['max']}, "
          f"avg={stats['packet_lengths']['avg']:.1f}")
    
    if 'rssi' in stats:
        print(f"\nRSSI: min={stats['rssi']['min']}dBm, "
              f"max={stats['rssi']['max']}dBm, "
              f"avg={stats['rssi']['avg']:.1f}dBm")
    
    if 'lqi' in stats:
        print(f"LQI:  min={stats['lqi']['min']}, "
              f"max={stats['lqi']['max']}, "
              f"avg={stats['lqi']['avg']:.1f}")
    
    print(f"\nChannels used ({len(stats['channels'])}):")
    for ch, count in sorted(stats['channels'].items())[:10]:
        print(f"  CH{ch:3d}: {count:4d} packets ({(count/stats['total_packets'])*100:.1f}%)")
    
    print(f"\nUnique patterns (first 4 bytes): {stats['unique_patterns']}")
    
    # Find repeating patterns
    repeated = analyzer.find_repeating_patterns(min_occurrences=3)
    if repeated:
        print(f"\nRepeating patterns (≥3 occurrences):")
        for pattern, info in sorted(repeated.items(), key=lambda x: x[1]['count'], reverse=True)[:10]:
            print(f"  {pattern}: {info['count']} times ({info['percentage']:.1f}%)")
    
    # Timing analysis
    timing = analyzer.analyze_timing()
    if timing:
        print(f"\nTiming intervals:")
        print(f"  Min: {timing['min_interval']}us")
        print(f"  Max: {timing['max_interval']}us")
        print(f"  Avg: {timing['avg_interval']:.1f}us")

def main():
    parser = argparse.ArgumentParser(
        description='XN297DUMP Packet Decoder and Analyzer',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Read from serial port and display packets:
    %(prog)s --port COM3 --baud 115200
    
  Read from file and analyze:
    %(prog)s --file capture.txt --analyze
    
  Capture for 60 seconds and export to CSV:
    %(prog)s --port /dev/ttyUSB0 --duration 60 --export capture.csv
    
  Real-time display with analysis every 10 packets:
    %(prog)s --port COM3 --display --analyze --interval 10
        """
    )
    
    parser.add_argument('--port', help='Serial port (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--file', help='Read from file instead of serial port')
    parser.add_argument('--export', help='Export packets to CSV file')
    parser.add_argument('--json', help='Export packets to JSON file')
    parser.add_argument('--analyze', action='store_true', help='Show analysis after capture')
    parser.add_argument('--display', action='store_true', help='Display packets as they arrive')
    parser.add_argument('--duration', type=int, help='Capture duration in seconds (serial only)')
    parser.add_argument('--interval', type=int, default=0, 
                       help='Show analysis every N packets during capture')
    
    args = parser.parse_args()
    
    # Validate arguments
    if not args.port and not args.file:
        parser.error("Either --port or --file must be specified")
    
    # Initialize analyzer
    analyzer = PacketAnalyzer()
    packet_count = [0]  # Use list to allow modification in callback
    last_analysis = [0]
    
    def packet_callback(packet):
        """Callback for each packet"""
        packet_count[0] += 1
        analyzer.add_packet(packet)
        
        if args.display:
            print_packet(packet, packet_count[0])
        
        # Show periodic analysis if requested
        if args.interval > 0 and (packet_count[0] - last_analysis[0]) >= args.interval:
            print_analysis(analyzer)
            last_analysis[0] = packet_count[0]
    
    # Read packets
    if args.file:
        FileReader.read_packets(args.file, packet_callback)
    else:
        reader = SerialReader(args.port, args.baud)
        if reader.open():
            reader.read_packets(packet_callback, args.duration)
            reader.close()
    
    # Export if requested
    if args.export:
        Exporter.to_csv(analyzer.packets, args.export)
    
    if args.json:
        Exporter.to_json(analyzer.packets, args.json)
    
    # Show final analysis if requested
    if args.analyze:
        print_analysis(analyzer)

if __name__ == '__main__':
    main()
