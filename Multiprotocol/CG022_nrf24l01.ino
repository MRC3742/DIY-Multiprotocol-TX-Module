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
// LT89xx (LT8910) chip emulated via NRF24L01 at 1 Mbps using the
// standard LT8900 emulation layer in NRF24l01_SPI.ino.
//
// Protocol decoded from SPI captures of original AO-SEN-MA TX hardware.
// Key findings from capture analysis:
//   - Bind phase uses fixed sync word 0x2211 (LT8900 register 0x24)
//   - After 166 bind packets, TX switches to TX-ID-derived sync word
//     New reg 0x24 = (bind_pkt[6] << 8) | bind_pkt[5]  (i.e. rx_tx_addr[4], rx_tx_addr[3])
//   - Data packets use this new sync word so receiver only hears its paired TX
//   - CRC-16 poly=0x8005, init=0x4402, 3-byte preamble, 8-bit trailer
//
// Framing (captures 60-69 analysis):
//   The LT8900 emulation layer packs [preamble][sync][trailer] into the
//   NRF24L01 address field.  With preamble_len=3, addr_size=2, trailer=8:
//     NRF address (5 bytes) = [pre pre sync_hi sync_lo trailer]
//     NRF payload = [data 10B bit-reversed] [CRC 2B bit-reversed]
//   On air: [pre pre pre] [sync 2B] [trailer 1B] [data 10B] [CRC 2B]
//   This exactly matches the stock LT8900 TX on-air frame.

#if defined(CG022_NRF24L01_INO)

#include "iface_nrf24l01.h"

#define FORCE_CG022_ORIGINAL_ID

// Protocol constants derived from SPI capture analysis
#define CG022_PACKET_PERIOD		2310	// ~2.31ms per channel hop
#define CG022_PACKET_SIZE		10		// 10-byte payload (5 x 16-bit FIFO words)
#define CG022_NUM_CHANNELS		8		// 8 RF channels
#define CG022_BIND_COUNT		166		// ~0.38 seconds of bind packets (matches original TX)
#define CG022_INITIAL_WAIT		500

// CRC-16 parameters from stock LT8900 registers
#define CG022_CRC_INIT			0x4402	// register 0x28
#define CG022_CRC_POLY			0x8005	// register 0x17

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

static void __attribute__((unused)) CG022_set_bind_sync()
{
	// Set bind-phase sync word (LT8900 register 0x24 = 0x2211).
	// LT8900_SetAddress expects bytes in LSByte-first order (the function
	// reverses them internally to match the LT8900 on-air byte order).
	uint8_t sync[] = {0x11, 0x22};
	LT8900_SetAddress(sync, 2);
}

static void __attribute__((unused)) CG022_set_data_sync()
{
	// After bind, the original TX changes the LT8900 sync word from the fixed
	// bind sync 0x2211 to a TX-ID-derived sync word so the receiver only
	// responds to its paired transmitter.
	//
	// From SPI capture analysis (01b, 02b):
	//   Bind packet bytes: 0A 00 [0] [1] [2] [3] [4] [5] [6] 00
	//   reg 0x24 changes to: ([4] << 8) | [3] = (rx_tx_addr[4] << 8) | rx_tx_addr[3]
	//   Example: TX_ID=11 22 33 06 AB FC AD → reg 0x24 = 0xAB06
	uint8_t sync[] = {rx_tx_addr[3], rx_tx_addr[4]};
	LT8900_SetAddress(sync, 2);
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
		// Byte 1: 0x00 (verified from SPI captures 01b, 02b - original TX sends 0x00)
		packet[1] = 0x00;

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

	// Flush TX FIFO and clear status flags before sending
	NRF24L01_FlushTx();
	NRF24L01_WriteReg(NRF24L01_07_STATUS, _BV(NRF24L01_07_TX_DS) | _BV(NRF24L01_07_MAX_RT));

	// Send packet and wait for TX completion, then retransmit once.
	// Two transmissions per channel give the LT8910 two chances to
	// correlate the sync word despite the NRF24L01's wider GFSK deviation.
	LT8900_WritePayload(packet, CG022_PACKET_SIZE);
	while(NRF24L01_packet_ack() == PKT_PENDING);
	NRF24L01_FlushTx();
	NRF24L01_WriteReg(NRF24L01_07_STATUS, _BV(NRF24L01_07_TX_DS) | _BV(NRF24L01_07_MAX_RT));
	LT8900_WritePayload(packet, CG022_PACKET_SIZE);

	// Advance to next hop channel
	hopping_frequency_no++;
	if(hopping_frequency_no >= CG022_NUM_CHANNELS)
		hopping_frequency_no = 0;

	// Set power — CG022 needs full TX power during bind.
	// The stock LT8900 TX uses full power from the first bind packet, and the
	// LT8910 receiver has reduced sensitivity to the NRF24L01's wider GFSK
	// deviation (±160 kHz vs LT8900's ±96 kHz at 1 Mbps).  Using the standard
	// NRF_BIND_POWER (-18 dBm) is 18 dB below the stock TX and insufficient
	// for the LT8910 correlator to reliably lock onto the NRF24L01 signal.
	if(IS_BIND_IN_PROGRESS)
	{
		if(prev_power != NRF_POWER_3)
		{
			uint8_t val = NRF24L01_ReadReg(NRF24L01_06_RF_SETUP);
			val = (val & 0xF8) | (NRF_POWER_3 << 1) | 0x01;
			NRF24L01_WriteReg(NRF24L01_06_RF_SETUP, val);
			prev_power = NRF_POWER_3;
		}
	}
	else
		NRF24L01_SetPower();
}

static void __attribute__((unused)) CG022_RF_init()
{
	NRF24L01_Initialize();

	// CRC-16 polynomial from register 0x17 = 0x8005
	crc16_polynomial = CG022_CRC_POLY;

	// Configure LT8900 emulation: 3-byte preamble, 8-bit trailer, CRC on
	// This packs [preamble 2B][sync 2B][trailer 1B] into the 5-byte NRF
	// address, producing the exact same on-air frame as the stock LT8900 TX.
	LT8900_Config(3 /*preamble bytes*/, 8 /*trailer bits*/, _BV(LT8900_CRC_ON), CG022_CRC_INIT);

	// 1 Mbps data rate matching LT8900
	NRF24L01_SetBitrate(NRF24L01_BR_1M);

	// Set bind sync word (0x2211) — this also configures the NRF address
	CG022_set_bind_sync();

	// TX mode, power up, NRF CRC disabled (LT8900 CRC computed in software)
	LT8900_SetTxRxMode(TX_EN);
}

uint16_t CG022_callback()
{
	#ifdef MULTI_SYNC
		telemetry_set_input_sync(CG022_PACKET_PERIOD);
	#endif
	if(bind_counter)
	{
		bind_counter--;
		if(bind_counter == 0)
		{
			BIND_DONE;
			// Switch to TX-ID-derived sync word for data phase.
			// The original TX changes LT8900 reg 0x24 after exactly 166 bind packets.
			// The receiver expects data packets on this new sync word.
			CG022_set_data_sync();
		}
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
