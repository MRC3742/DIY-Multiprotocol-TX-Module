/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Multiprotocol is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Multiprotocol.  If not, see <http://www.gnu.org/licenses/>.
 */
// Compatible with CG022 quadcopter using AO-SEN-MA transmitter protocol
// LT89xx (LT8910) chip emulated via NRF24L01 at 1 Mbps

#if defined(CG022_NRF24L01_INO)

#include "iface_nrf24l01.h"

// LT8900 emulation flag - defined here because NRF24l01_SPI.ino is compiled after this file
#ifndef LT8900_CRC_ON
	#define LT8900_CRC_ON 6
#endif

//#define FORCE_CG022_ORIGINAL_ID

// Protocol constants derived from SPI capture analysis
#define CG022_PACKET_PERIOD		2310	// ~2.31ms per channel hop
#define CG022_PACKET_SIZE		10		// 10-byte payload (5 x 16-bit FIFO words)
#define CG022_NUM_CHANNELS		8		// 8 RF channels
#define CG022_BIND_COUNT		200		// ~0.46 seconds of bind packets (original TX sends ~166)
#define CG022_INITIAL_WAIT		500

// Channel hopping pattern from capture analysis: 0, 40, 10, 50, 20, 60, 30, 70
static const uint8_t PROGMEM CG022_Channels[] = { 0, 40, 10, 50, 20, 60, 30, 70 };

// Flag definitions for packet byte 6
#define CG022_FLAG_LED_OFF		0x80	// Bit 7: LEDs off (0xA0 = 0x20 | 0x80)

// Flag definitions for packet byte 7
#define CG022_FLAG_FLIP			0x40	// Bit 6: Flip mode (0x60 = 0x20 | 0x40)
#define CG022_FLAG_HEADLESS		0xC0	// Bits 7+6: Headless mode (0xE0 = 0x20 | 0xC0)

static void __attribute__((unused)) CG022_initialize_txid()
{
	// rx_tx_addr[0..3] are set from MProtocol_id by the framework
	// RX_num provides model match capability
	rx_tx_addr[3] = (rx_tx_addr[3] & 0xF0) | (RX_num & 0x0F);
	// Bind packet uses 7 TX ID bytes (rx_tx_addr[0..3] + rx_tx_addr[4] extended)
	// rx_tx_addr[4] is already set by the framework
	#ifdef FORCE_CG022_ORIGINAL_ID
		// Full 7-byte TX ID from capture: bind packet 0A 00 11 22 33 06 AB FC AD 00
		rx_tx_addr[0] = 0x11;
		rx_tx_addr[1] = 0x22;
		rx_tx_addr[2] = 0x33;
		rx_tx_addr[3] = 0x06;
		rx_tx_addr[4] = 0xAB;
	#endif
}

static void __attribute__((unused)) CG022_send_packet()
{
	// Byte 0: Packet marker (always 0x0A)
	packet[0] = 0x0A;

	if(IS_BIND_IN_PROGRESS)
	{
		// Bind packet: byte 1 = 0x00, bytes 2-8 = 7-byte TX ID
		packet[1] = 0x00;
		packet[2] = rx_tx_addr[0];
		packet[3] = rx_tx_addr[1];
		packet[4] = rx_tx_addr[2];
		packet[5] = rx_tx_addr[3];
		packet[6] = rx_tx_addr[4];
		#ifdef FORCE_CG022_ORIGINAL_ID
			packet[7] = 0xFC;
			packet[8] = 0xAD;
		#else
			// Bytes 7-8 are additional TX ID bytes; exact derivation from
			// original TX is unknown, so generate unique values from addr
			packet[7] = ~rx_tx_addr[1];
			packet[8] = ~rx_tx_addr[2];
		#endif
		// Byte 9: Bind packets use 0x00
		packet[9] = 0x00;
	}
	else
	{
		// Data packet
		// Byte 1: TX address byte (for receiver identification)
		packet[1] = rx_tx_addr[1];

		// Byte 2: Throttle (0x00 = low, 0x3F = max)
		packet[2] = convert_channel_16b_limit(THROTTLE, 0x00, 0x3F);

		// Byte 3: Elevator (0x00 = back, 0x20 = center, 0x3F = forward)
		packet[3] = convert_channel_16b_limit(ELEVATOR, 0x00, 0x3F);

		// Byte 4: Rudder (0x00 = left, 0x20 = center, 0x3F = right)
		packet[4] = convert_channel_16b_limit(RUDDER, 0x00, 0x3F);

		// Byte 5: Aileron (0x00 = left, 0x20 = center, 0x3F = right)
		packet[5] = convert_channel_16b_limit(AILERON, 0x00, 0x3F);

		// Byte 6: Flags (0x20 default)
		packet[6] = 0x20;
		if(CH6_SW)
			packet[6] |= CG022_FLAG_LED_OFF;		// LEDs off

		// Byte 7: Flags (0x20 default)
		packet[7] = 0x20;
		if(CH7_SW)
			packet[7] |= CG022_FLAG_HEADLESS;		// Headless mode
		else if(CH5_SW)
			packet[7] |= CG022_FLAG_FLIP;			// Flip mode

		// Byte 8: Constant 0x20
		packet[8] = 0x20;

		// Byte 9: Checksum = sum of bytes 2-8
		packet[9] = 0;
		for(uint8_t i = 2; i <= 8; i++)
			packet[9] += packet[i];
	}

	// Set channel frequency from hopping pattern
	LT8900_SetChannel(pgm_read_byte_near(&CG022_Channels[hopping_frequency_no]));

	// Clear status flags from previous TX before sending new packet
	NRF24L01_WriteReg(NRF24L01_07_STATUS, _BV(NRF24L01_07_TX_DS) | _BV(NRF24L01_07_MAX_RT));

	// Send packet + 1 retransmit (needed for LT8900 emulation via NRF24L01,
	// same pattern used by SHENQI protocol which also emulates LT8900)
	LT8900_WritePayload(packet, CG022_PACKET_SIZE);
	while(NRF24L01_packet_ack() != PKT_ACKED);
	LT8900_WritePayload(packet, CG022_PACKET_SIZE);

	// Advance to next hop channel
	hopping_frequency_no++;
	if(hopping_frequency_no >= CG022_NUM_CHANNELS)
		hopping_frequency_no = 0;

	// Set power
	NRF24L01_SetPower();
}

static void __attribute__((unused)) CG022_RF_init()
{
	NRF24L01_Initialize();

	// CRC-16 polynomial from register 0x17 = 0x8005
	crc16_polynomial = 0x8005;

	// Configure LT8900 emulation layer from register 0x20 = 0x4800:
	// Preamble: 3 bytes (bits 15:13 = 010, value+1), Trailer: 8 bits (bits 12:8 = 01000)
	// SyncWord: 2 bytes (bits 7:6 = 00), PACKET_LENGTH_EN: OFF (bit 5 = 0)
	// CRC enabled, NRZ encoding
	// CRC init from register 0x28 = 0x4402
	LT8900_Config(3, 8, _BV(LT8900_CRC_ON), 0x4402);

	// Set 2-byte sync word from register 0x24 = 0x2211
	// Register 0x25 (0x068C) is NOT used with 2-byte sync word
	// LT8900_SetAddress reverses byte order internally
	LT8900_SetAddress((uint8_t *)"\x11\x22", 2);

	// Set to TX mode
	LT8900_SetTxRxMode(TX_EN);
}

uint16_t CG022_callback()
{
	if(IS_BIND_IN_PROGRESS)
	{
		if(bind_counter == 0)
			BIND_DONE;
		else
			bind_counter--;
	}
	CG022_send_packet();
	return CG022_PACKET_PERIOD;
}

void CG022_init()
{
	BIND_IN_PROGRESS;	// autobind protocol
	CG022_initialize_txid();
	CG022_RF_init();
	hopping_frequency_no = 0;
	bind_counter = CG022_BIND_COUNT;
}

#endif
