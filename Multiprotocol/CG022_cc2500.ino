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
// CG022 quadcopter protocol — CC2500-based implementation
//
// The stock CG022 TX uses an LT8900 chip with GFSK at 1 Mbps and ±96 kHz
// deviation.  The NRF24L01 has a fixed ±160 kHz deviation at 1 Mbps which
// the LT8910 receiver cannot decode (0% reception across 6+ test captures).
//
// This implementation uses the CC2500's configurable GFSK modulator to
// exactly match the LT8900's ±96 kHz deviation.  The CC2500's data rate
// registers are programmed for 1 Mbps (above the officially-specified
// 500 kbps maximum, but within the programmable register range for TX).
//
// Frame format on air (identical to stock LT8900 TX):
//   [Preamble 3B] [Sync 2B] [Trailer 1B] [Data 10B] [CRC-16 2B]
//   = 18 bytes total, manually assembled in the TX FIFO.
//
// The CC2500 is configured in raw GFSK mode (MDMCFG2=0x10): no CC2500
// preamble or sync word — the entire LT8900-format frame is in the payload.

#if defined(CG022_CC2500_INO)

#include "iface_cc2500.h"

#define FORCE_CG022_ORIGINAL_ID

// Protocol constants (shared with NRF24L01 version)
#define CG022_CC_PACKET_PERIOD		2310
#define CG022_CC_PACKET_SIZE		10
#define CG022_CC_NUM_CHANNELS		8
#define CG022_CC_BIND_COUNT			166
#define CG022_CC_INITIAL_WAIT		500

// CRC-16 from stock LT8900 registers
#define CG022_CC_CRC_INIT			0x4402
#define CG022_CC_CRC_POLY			0x8005

// On-air frame size: 3B preamble + 2B sync + 1B trailer + 10B data + 2B CRC
#define CG022_CC_FRAME_SIZE			18

// Channel hopping: LT8900 channels 0,40,10,50,20,60,30,70
static const uint8_t PROGMEM CG022_CC_Channels[] = { 0, 40, 10, 50, 20, 60, 30, 70 };

// Flag definitions (same as NRF24L01 version)
#define CG022_CC_FLAG_LED_OFF		0x80
#define CG022_CC_FLAG_FLIP			0x40
#define CG022_CC_FLAG_HEADLESS		0xC0

// Pre-calibrated FSCAL1 values for each channel (filled in init)
static uint8_t CG022_CC_calData[CG022_CC_NUM_CHANNELS];

// Map LT8900 channel number to CC2500 CHANNR register value.
// CC2500 channel spacing is configured to 333.251 kHz (CHANSPC_E=3, CHANSPC_M=164).
// LT8900 channel N = 2402 + N MHz.  CC2500 base ≈ 2400 MHz.
// Offset = 2 MHz / 0.333251 MHz ≈ 6 CHANNR steps.
// So LT8900 channel N → CC2500 CHANNR = N*3 + 6.
static uint8_t CG022_CC_lt8900_to_cc2500_channr(uint8_t lt8900_ch)
{
	return lt8900_ch * 3 + 6;
}

static void __attribute__((unused)) CG022_CC2500_initialize_txid()
{
	rx_tx_addr[3] = (rx_tx_addr[3] & 0xF0) | (RX_num & 0x0F);
	#ifdef FORCE_CG022_ORIGINAL_ID
		rx_tx_addr[0] = 0x11;
		rx_tx_addr[1] = 0x22;
		rx_tx_addr[2] = 0x33;
		rx_tx_addr[3] = 0x06;
		rx_tx_addr[4] = 0xAB;
	#endif
}

// Build the sync + trailer portion of the on-air frame.
// The LT8900 sends sync word MSByte first, each byte LSBit-first on air.
// Since the CC2500 sends MSBit-first, each byte must be bit-reversed.
// Trailer is 8 bits of alternating pattern following the last sync bit.
static uint8_t CG022_CC_sync_trailer[3];  // [sync_hi_rev, sync_lo_rev, trailer]

static void __attribute__((unused)) CG022_CC2500_set_sync(uint8_t sync_hi, uint8_t sync_lo)
{
	CG022_CC_sync_trailer[0] = bit_reverse(sync_hi);
	CG022_CC_sync_trailer[1] = bit_reverse(sync_lo);
	// Trailer pattern depends on last bit of the sync word (bit-reversed byte)
	// If last bit = 0, trailer = 0xAA (10101010); if 1, trailer = 0x55
	CG022_CC_sync_trailer[2] = (CG022_CC_sync_trailer[1] & 0x01) ? 0x55 : 0xAA;
}

static void __attribute__((unused)) CG022_CC2500_send_packet()
{
	uint8_t frame[CG022_CC_FRAME_SIZE];
	uint8_t pos = 0;

	// --- Preamble: 3 bytes ---
	// LT8900 preamble is 010101... pattern.  The sync word MSByte (bit-reversed)
	// determines whether the preamble is 0x55 or 0xAA.
	uint8_t preamble_byte = (CG022_CC_sync_trailer[0] & 0x80) ? 0x55 : 0xAA;
	// For sync 0x2211: bit_reverse(0x22) = 0x44, MSBit = 0 → preamble = 0xAA
	// Wait: preamble ends with the opposite of the first sync bit.
	// NRF24L01 auto-preamble: if first address bit = 1 → preamble = 0xAA (starts with 1)
	// LT8900: preamble is always 010101... (0x55) and ends naturally before sync.
	// The key is: preamble byte = 0x55 when sync MSBit = 0, or 0xAA when sync MSBit = 1.
	// For sync_hi_rev = 0x44 (01000100), MSBit = 0 → preamble = 0x55.
	preamble_byte = (CG022_CC_sync_trailer[0] & 0x80) ? 0xAA : 0x55;
	frame[pos++] = preamble_byte;
	frame[pos++] = preamble_byte;
	frame[pos++] = preamble_byte;

	// --- Sync word (bit-reversed) + Trailer ---
	frame[pos++] = CG022_CC_sync_trailer[0];
	frame[pos++] = CG022_CC_sync_trailer[1];
	frame[pos++] = CG022_CC_sync_trailer[2];

	// --- Build data packet (same logic as CG022_nrf24l01.ino) ---
	packet[0] = 0x0A;
	if(IS_BIND_IN_PROGRESS)
	{
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
			packet[7] = ~rx_tx_addr[1];
			packet[8] = ~rx_tx_addr[2];
		#endif
		packet[9] = 0x00;
	}
	else
	{
		packet[1] = 0x00;
		packet[2] = convert_channel_16b_limit(THROTTLE, 0x00, 0x3F);
		packet[3] = convert_channel_16b_limit(ELEVATOR, 0x00, 0x3F);
		packet[4] = convert_channel_16b_limit(RUDDER, 0x00, 0x3F);
		packet[5] = convert_channel_16b_limit(AILERON, 0x00, 0x3F);
		packet[6] = 0x20;
		if(CH6_SW)
			packet[6] |= CG022_CC_FLAG_LED_OFF;
		packet[7] = 0x20;
		if(CH7_SW)
			packet[7] |= CG022_CC_FLAG_HEADLESS;
		else if(CH5_SW)
			packet[7] |= CG022_CC_FLAG_FLIP;
		packet[8] = 0x20;
		packet[9] = 0;
		for(uint8_t i = 2; i <= 8; i++)
			packet[9] += packet[i];
	}

	// --- Data bytes: bit-reversed, with CRC computation ---
	crc = CG022_CC_CRC_INIT;
	crc16_polynomial = CG022_CC_CRC_POLY;
	for(uint8_t i = 0; i < CG022_CC_PACKET_SIZE; i++)
	{
		uint8_t tmp = bit_reverse(packet[i]);
		frame[pos++] = tmp;
		crc16_update(tmp, 8);
	}

	// --- CRC: 2 bytes, bit-reversed ---
	frame[pos++] = bit_reverse(crc >> 8);
	frame[pos++] = bit_reverse(crc);

	// --- Set RF channel ---
	uint8_t lt_ch = pgm_read_byte_near(&CG022_CC_Channels[hopping_frequency_no]);
	uint8_t cc_channr = CG022_CC_lt8900_to_cc2500_channr(lt_ch);
	CC2500_Strobe(CC2500_SIDLE);
	CC2500_WriteReg(CC2500_0A_CHANNR, cc_channr);
	CC2500_WriteReg(CC2500_25_FSCAL1, CG022_CC_calData[hopping_frequency_no]);

	// --- Transmit frame ---
	CC2500_Strobe(CC2500_SFTX);
	CC2500_WriteRegisterMulti(CC2500_3F_TXFIFO, frame, CG022_CC_FRAME_SIZE);
	CC2500_Strobe(CC2500_STX);

	// Advance channel
	hopping_frequency_no++;
	if(hopping_frequency_no >= CG022_CC_NUM_CHANNELS)
		hopping_frequency_no = 0;
}

static void __attribute__((unused)) CG022_CC2500_RF_init()
{
	CC2500_Strobe(CC2500_SIDLE);

	// --- CC2500 register configuration for LT8900 emulation ---
	// Raw GFSK mode: no CC2500 preamble/sync, data from FIFO only
	//
	// Key differences from stock CC2500_250K_Init():
	//   - Data rate: 1 Mbps (DRATE_E=15, DRATE_M=59) vs 250 kbps
	//   - Deviation: ±96 kHz (DEVIATION_E=5, DEVIATION_M=7) vs ±127 kHz
	//   - These match the stock LT8900 TX parameters exactly
	
	CC2500_WriteReg(CC2500_06_PKTLEN,   CG022_CC_FRAME_SIZE);  // Fixed packet length = 18 bytes
	CC2500_WriteReg(CC2500_07_PKTCTRL1, 0x00);  // No address check, no status append
	CC2500_WriteReg(CC2500_08_PKTCTRL0, 0x00);  // Fixed length, normal mode
	CC2500_WriteReg(CC2500_0B_FSCTRL1,  0x0A);  // IF frequency
	CC2500_WriteReg(CC2500_0C_FSCTRL0,  0x00);  // Frequency offset
	// Base frequency ≈ 2400 MHz (same as CC2500_250K_Init)
	CC2500_WriteReg(CC2500_0D_FREQ2,    0x5C);
	CC2500_WriteReg(CC2500_0E_FREQ1,    0x4E);
	CC2500_WriteReg(CC2500_0F_FREQ0,    0xC5);

	// --- 1 Mbps data rate ---
	// RDATA = 26 MHz / 2^28 * (256 + DRATE_M) * 2^DRATE_E
	// With DRATE_E=15, DRATE_M=59: RDATA = 999.8 kbps ≈ 1 Mbps
	// MDMCFG4[7:4] = CHANBW (don't care for TX, use default BW)
	// MDMCFG4[3:0] = DRATE_E = 0xF
	CC2500_WriteReg(CC2500_10_MDMCFG4,  0x0F);  // DRATE_E=15, wide BW
	CC2500_WriteReg(CC2500_11_MDMCFG3,  0x3B);  // DRATE_M=59 → 1 Mbps

	// --- GFSK, no CC2500 preamble/sync (raw mode) ---
	CC2500_WriteReg(CC2500_12_MDMCFG2,  0x10);  // GFSK, SYNC_MODE=0 (no preamble/sync)

	// --- Channel spacing: 333.251 kHz (same as CC2500_250K_Init) ---
	// This allows LT8900 channel N → CC2500 CHANNR = N*3 + 6
	CC2500_WriteReg(CC2500_13_MDMCFG1,  0x03);  // CHANSPC_E=3
	CC2500_WriteReg(CC2500_14_MDMCFG0,  0xA4);  // CHANSPC_M=164

	// --- Deviation: ±96 kHz ---
	// DEV = 26 MHz / 2^17 * (8 + DEVIATION_M) * 2^DEVIATION_E
	// With E=5, M=7: DEV = 95,367 Hz ≈ 96 kHz
	CC2500_WriteReg(CC2500_15_DEVIATN,  0x57);

	// --- Standard radio control settings ---
	CC2500_WriteReg(CC2500_18_MCSM0,    0x08);  // Auto-cal when going from IDLE to TX
	CC2500_WriteReg(CC2500_19_FOCCFG,   0x1D);  // FOC config
	CC2500_WriteReg(CC2500_1A_BSCFG,    0x1C);  // Bit sync config
	CC2500_WriteReg(CC2500_1B_AGCCTRL2, 0xC7);  // AGC
	CC2500_WriteReg(CC2500_1C_AGCCTRL1, 0x00);
	CC2500_WriteReg(CC2500_1D_AGCCTRL0, 0xB0);
	CC2500_WriteReg(CC2500_21_FREND1,   0xB6);  // Front end RX config
	CC2500_WriteReg(CC2500_23_FSCAL3,   0xEA);  // Freq synth cal
	CC2500_WriteReg(CC2500_25_FSCAL1,   0x00);
	CC2500_WriteReg(CC2500_26_FSCAL0,   0x11);

	// TX mode, set power
	CC2500_SetTxRxMode(TX_EN);
	CC2500_SetPower();

	// --- Pre-calibrate PLL for all 8 hop channels ---
	for(uint8_t i = 0; i < CG022_CC_NUM_CHANNELS; i++)
	{
		uint8_t lt_ch = pgm_read_byte_near(&CG022_CC_Channels[i]);
		uint8_t cc_channr = CG022_CC_lt8900_to_cc2500_channr(lt_ch);
		CC2500_Strobe(CC2500_SIDLE);
		CC2500_WriteReg(CC2500_0A_CHANNR, cc_channr);
		CC2500_Strobe(CC2500_SCAL);
		delayMicroseconds(900);
		CG022_CC_calData[i] = CC2500_ReadReg(CC2500_25_FSCAL1);
	}
}

uint16_t CG022_CC2500_callback()
{
	#ifdef MULTI_SYNC
		telemetry_set_input_sync(CG022_CC_PACKET_PERIOD);
	#endif
	if(bind_counter)
	{
		bind_counter--;
		if(bind_counter == 0)
		{
			BIND_DONE;
			// Switch to TX-ID-derived sync word for data phase
			CG022_CC2500_set_sync(rx_tx_addr[4], rx_tx_addr[3]);
		}
	}
	CG022_CC2500_send_packet();
	return CG022_CC_PACKET_PERIOD;
}

void CG022_CC2500_init()
{
	BIND_IN_PROGRESS;
	CG022_CC2500_initialize_txid();
	CG022_CC2500_RF_init();
	hopping_frequency_no = 0;
	bind_counter = CG022_CC_BIND_COUNT;

	// Set bind sync word: 0x2211
	CG022_CC2500_set_sync(0x22, 0x11);

	// CRC polynomial
	crc16_polynomial = CG022_CC_CRC_POLY;
}

#endif
