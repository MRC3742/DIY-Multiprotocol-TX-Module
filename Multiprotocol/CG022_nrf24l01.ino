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
//
// Protocol decoded from SPI captures of original AO-SEN-MA TX hardware.
// Key findings from capture analysis:
//   - Bind phase uses fixed sync word 0x2211 (LT8900 register 0x24)
//   - After 166 bind packets, TX switches to TX-ID-derived sync word
//     New reg 0x24 = (bind_pkt[6] << 8) | bind_pkt[5]  (i.e. rx_tx_addr[4], rx_tx_addr[3])
//   - Data packets use this new sync word so receiver only hears its paired TX
//   - CRC-16 poly=0x8005, init=0x4402, 3-byte preamble, 8-bit trailer
//
// NRF24L01 PCF workaround (captures 60-67 analysis):
//   The NRF24L01 Enhanced ShockBurst inserts a 9-bit Packet Control Field
//   (PCF) between the address field and payload on the air.  When the LT8900
//   emulation layer packs sync+trailer into the NRF address, the PCF lands
//   between trailer and data, creating a 9-bit offset that prevents the
//   LT8910 correlator from decoding any packet (0 FIFO reads in all captures
//   60b through 66b).
//
//   Fix: set the NRF24L01 address to all-preamble bytes (0x55 or 0xAA) and
//   place sync word + trailer + data + CRC entirely in the NRF payload.
//   The PCF then sits between preamble and sync word — a gap the LT8910
//   correlator can tolerate because it searches for the sync pattern within
//   a window after preamble detection ends.
//
//   On air:  [NRF preamble 1B] [address 5B = preamble] [PCF 9b] [sync 2B]
//            [trailer 1B] [data 10B] [CRC 2B]

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

// Current sync word bytes (bit-reversed, ready for on-air transmission)
// Updated when switching from bind to data phase
static uint8_t CG022_sync[2];
// Preamble byte for NRF address (0x55 or 0xAA, depends on sync word)
static uint8_t CG022_preamble_byte;

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

static void __attribute__((unused)) CG022_set_sync(uint8_t sync_lo, uint8_t sync_hi)
{
	// Set sync word for on-air transmission.
	// LT8900 sync word register stores MSByte:LSByte, e.g. 0x2211 = hi=0x22, lo=0x11.
	// LT8900 sends LSBit-first; NRF24L01 sends MSBit-first → bit-reverse each byte.
	// The LT8900 transmits hi byte first, then lo byte.
	CG022_sync[0] = bit_reverse(sync_hi);
	CG022_sync[1] = bit_reverse(sync_lo);

	// Preamble polarity: based on MSBit of the first sync byte on-air.
	// After bit-reversal, CG022_sync[0] bit 0 determines polarity:
	//   bit0 == 0 → preamble = 0x55 (01010101)
	//   bit0 == 1 → preamble = 0xAA (10101010)
	CG022_preamble_byte = (CG022_sync[0] & 0x01) ? 0xAA : 0x55;

	// Set NRF24L01 address to 5 bytes of preamble.
	// The NRF auto-generates 1 matching preamble byte, giving 6 total on air.
	// This makes the entire NRF address look like preamble to the LT8910,
	// so the PCF (between address and payload) cannot corrupt the sync/data.
	uint8_t addr[5];
	memset(addr, CG022_preamble_byte, 5);
	NRF24L01_WriteRegisterMulti(NRF24L01_0A_RX_ADDR_P0, addr, 5);
	NRF24L01_WriteRegisterMulti(NRF24L01_10_TX_ADDR, addr, 5);
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
	CG022_set_sync(rx_tx_addr[3], rx_tx_addr[4]);
}

static void __attribute__((unused)) CG022_write_payload(uint8_t *msg, uint8_t len)
{
	// Build NRF24L01 payload with full LT8900-compatible framing:
	//   [sync_word 2B] [trailer 1B] [data bit-reversed] [CRC-16 bit-reversed]
	//
	// The NRF address is set to all-preamble bytes, so the on-air format is:
	//   [preamble 6B] [PCF 9b] [sync 2B] [trailer 1B] [data 10B] [CRC 2B]
	// The LT8910 detects preamble, skips the PCF gap, correlates the sync word,
	// then reads trailer + data + CRC normally.
	uint8_t buf[20];	// max: 2 sync + 1 trailer + 10 data + 2 CRC = 15
	uint8_t pos = 0;

	// Sync word (bit-reversed for on-air LSBit-first format)
	buf[pos++] = CG022_sync[0];
	buf[pos++] = CG022_sync[1];

	// Trailer: 8 bits of alternating, complement of last sync byte's LSBit
	// Last sync byte on-air: CG022_sync[1], bit 0 determines trailer polarity
	buf[pos++] = (CG022_sync[1] & 0x01) ? 0x55 : 0xAA;

	// Data bytes (bit-reversed) and CRC computation
	crc = CG022_CRC_INIT;
	for(uint8_t i = 0; i < len; i++)
	{
		uint8_t tmp = bit_reverse(msg[i]);
		buf[pos++] = tmp;
		crc16_update(tmp, 8);
	}

	// CRC (bit-reversed, matching LT8900 LSBit-first on-air format)
	buf[pos++] = bit_reverse(crc >> 8);
	buf[pos++] = bit_reverse(crc & 0xFF);

	NRF24L01_WritePayload(buf, pos);
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
	// NRF24L01 channel = LT8900 channel + 2 (NRF base 2400 MHz vs LT8900 base 2402 MHz)
	NRF24L01_WriteReg(NRF24L01_05_RF_CH, pgm_read_byte_near(&CG022_Channels[hopping_frequency_no]) + 2);

	// Flush TX FIFO and clear status flags before sending
	NRF24L01_FlushTx();
	NRF24L01_WriteReg(NRF24L01_07_STATUS, _BV(NRF24L01_07_TX_DS) | _BV(NRF24L01_07_MAX_RT));

	// Send packet and wait for TX completion, then retransmit once.
	// Two transmissions per channel give the LT8910 two chances to
	// correlate the sync word despite the NRF24L01's wider GFSK deviation.
	CG022_write_payload(packet, CG022_PACKET_SIZE);
	while(NRF24L01_packet_ack() == PKT_PENDING);
	NRF24L01_FlushTx();
	NRF24L01_WriteReg(NRF24L01_07_STATUS, _BV(NRF24L01_07_TX_DS) | _BV(NRF24L01_07_MAX_RT));
	CG022_write_payload(packet, CG022_PACKET_SIZE);

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

	// 1 Mbps data rate matching LT8900
	NRF24L01_SetBitrate(NRF24L01_BR_1M);

	// Disable Enhanced ShockBurst features that are unnecessary for TX-only
	// operation and may cause the NRF24L01 to insert a PCF on-air.
	NRF24L01_WriteReg(NRF24L01_01_EN_AA, 0x00);		// No auto-ack
	NRF24L01_WriteReg(NRF24L01_02_EN_RXADDR, 0x00);	// No RX pipes needed for TX
	NRF24L01_WriteReg(NRF24L01_04_SETUP_RETR, 0x00);	// No auto-retransmit
	NRF24L01_WriteReg(NRF24L01_1C_DYNPD, 0x00);		// No dynamic payload
	NRF24L01_WriteReg(NRF24L01_1D_FEATURE, 0x00);		// No enhanced features

	// 5-byte address width
	NRF24L01_WriteReg(NRF24L01_03_SETUP_AW, 0x03);

	// Set bind sync word (0x2211) and preamble-only NRF address
	CG022_set_sync(0x11, 0x22);

	// TX mode, power up, CRC disabled (LT8900 CRC computed in software)
	NRF24L01_SetTxRxMode(TXRX_OFF);
	NRF24L01_SetTxRxMode(TX_EN);
	NRF24L01_WriteReg(NRF24L01_00_CONFIG, _BV(NRF24L01_00_PWR_UP));
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
