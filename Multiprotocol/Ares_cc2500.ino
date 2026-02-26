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
// Compatible with ARES 6HPA transmitter

#if defined(ARES_CC2500_INO)

#include "iface_cc2500.h"

//#define ARES_FORCE_ID

#define ARES_COARSE			0

#define ARES_PACKET_LEN		17
#define ARES_NUM_FREQUENCE	60

enum {
	ARES_START = 0x00,
	ARES_CALIB = 0x01,
	ARES_PREP  = 0x02,
	ARES_DATA  = 0x03,
};

// CC2500 register init values captured from the ARES 6HPA transmitter
const PROGMEM uint8_t ARES_init_values[] = {
  /* 00 */ 0x06, 0x2E, 0x2E, 0x07, 0x5A, 0x60, 0x30, 0x04,
  /* 08 */ 0x05, 0x00, 0x00, 0x06, 0x00, 0x5C, 0xB1, 0x3B + ARES_COARSE,
  /* 10 */ 0x6A, 0xF8, 0x03, 0x23, 0x7A, 0x44, 0x07, 0x30,
  /* 18 */ 0x18, 0x16, 0x6C, 0x43, 0x40, 0x91, 0x87, 0x6B,
  /* 20 */ 0xF8, 0x56, 0x10, 0xA9, 0x0A, 0x00, 0x11
};

// The 60 RF channel values used for hopping
static const PROGMEM uint8_t ARES_channels[] = {
	0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1D,
	0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C,
	0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C,
	0x60, 0x64, 0x68, 0x6C, 0x6F, 0x74, 0x78, 0x7C,
	0x80, 0x84, 0x88, 0x8C, 0x90, 0x94, 0x98, 0x9B,
	0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC,
	0xC0, 0xC3, 0xC6, 0xCC, 0xD0, 0xD4, 0xD8, 0xDC,
	0xE0, 0xE4, 0xE8, 0xEC
};

static void __attribute__((unused)) ARES_CC2500_init()
{
	CC2500_Strobe(CC2500_SRES);
	delayMilliseconds(1);
	CC2500_Strobe(CC2500_SIDLE);

	for (uint8_t i = 0; i < 39; ++i)
		CC2500_WriteReg(i, pgm_read_byte_near(&ARES_init_values[i]));

	CC2500_WriteReg(CC2500_0C_FSCTRL0, option);
	prev_option = option;

	// Write PATABLE to max power (0xFF for all 8 entries) as captured
	for (uint8_t i = 0; i < 8; i++)
		CC2500_WriteReg(CC2500_3E_PATABLE, 0xFF);

	CC2500_SetTxRxMode(TX_EN);
	CC2500_SetPower();
}

// Generate hopping table: random permutation of ARES_channels[]
static void __attribute__((unused)) ARES_RF_channels()
{
	// Start with channels in order
	for (uint8_t i = 0; i < ARES_NUM_FREQUENCE; i++)
		hopping_frequency[i] = pgm_read_byte_near(&ARES_channels[i]);

	// Fisher-Yates shuffle using TX ID as seed
	uint32_t rnd = MProtocol_id;
	for (uint8_t i = ARES_NUM_FREQUENCE - 1; i > 0; i--)
	{
		rnd = rnd * 0x0019660D + 0x3C6EF35F;
		uint8_t j = (rnd >> 8) % (i + 1);
		uint8_t tmp = hopping_frequency[i];
		hopping_frequency[i] = hopping_frequency[j];
		hopping_frequency[j] = tmp;
	}
}

static void __attribute__((unused)) ARES_tune_chan()
{
	CC2500_Strobe(CC2500_SIDLE);
	CC2500_WriteReg(CC2500_0A_CHANNR, hopping_frequency[hopping_frequency_no]);
	CC2500_Strobe(CC2500_SFTX);
	CC2500_Strobe(CC2500_SCAL);
}

static void __attribute__((unused)) ARES_change_chan_fast()
{
	CC2500_Strobe(CC2500_SIDLE);
	CC2500_WriteReg(CC2500_0A_CHANNR, hopping_frequency[hopping_frequency_no]);
	CC2500_WriteReg(CC2500_25_FSCAL1, calData[hopping_frequency_no]);
}

// Advance the hop counter: cycles through 0-58 with step, inserting 59 when wrapping through 0
static uint8_t __attribute__((unused)) ARES_next_counter(uint8_t current, uint8_t step)
{
	if (current == 59)
		return 0;
	uint8_t next = (current + step) % 59;
	if (next == 0)
		return 59;
	return next;
}

static void __attribute__((unused)) ARES_build_packet()
{
	// Length byte: 16 data bytes follow
	packet[0] = 0x10;

	// TX ID
	packet[1] = rx_tx_addr[1];
	packet[2] = rx_tx_addr[2];
	packet[3] = rx_tx_addr[3];

	// 6 channels encoded as interleaved 12-bit values in bytes 4-12
	uint16_t ch[6];
	for (uint8_t i = 0; i < 6; i++)
		ch[i] = convert_channel_16b_nolimit(i, 1820, 3300, false);

	packet[4]  = ch[0] >> 4;
	packet[5]  = ((ch[0] & 0x0F) << 4) | (ch[1] & 0x0F);
	packet[6]  = ch[1] >> 4;
	packet[7]  = ch[2] >> 4;
	packet[8]  = ((ch[2] & 0x0F) << 4) | (ch[3] & 0x0F);
	packet[9]  = ch[3] >> 4;
	packet[10] = ch[4] >> 4;
	packet[11] = ((ch[4] & 0x0F) << 4) | (ch[5] & 0x0F);
	packet[12] = ch[5] >> 4;

	// Byte 16: counter step size (stored in crc, set to 1-58 in ARES_init)
	uint8_t step = crc;

	// Bytes 13-15: running counter with rotating bit 7 frame indicator
	// The counter cycles 0-58 with a step, inserting 59 before wrapping to 0
	// Each group of 3 packets has 3 consecutive counter values
	// packet_count holds the current counter value
	uint8_t c0 = packet_count;
	uint8_t c1 = ARES_next_counter(c0, step);
	uint8_t c2 = ARES_next_counter(c1, step);

	// Frame indicator: each data frame is sent 3 times
	// bind_phase tracks position 0/1/2 within the group of 3
	packet[13] = c0;
	packet[14] = c1;
	packet[15] = c2;
	packet[16] = step;

	// Set the rotating frame bit (bit 7) on one of bytes 13-15
	switch (bind_phase)
	{
		case 0:
			packet[13] |= 0x80;
			break;
		case 1:
			packet[14] |= 0x80;
			break;
		case 2:
			packet[15] |= 0x80;
			break;
	}
}

static void __attribute__((unused)) ARES_send_packet()
{
	ARES_change_chan_fast();
	CC2500_SetPower();
	CC2500_WriteData(packet, ARES_PACKET_LEN);
}

#define ARES_PACKET_PERIOD	6670	// 6.67ms between packets
#define ARES_PREP_TIMING	2000

uint16_t ARES_callback()
{
	switch(phase)
	{
		case ARES_START:
			ARES_CC2500_init();
			hopping_frequency_no = 0;
			bind_phase = 0;
			ARES_tune_chan();
			phase = ARES_CALIB;
			return ARES_PREP_TIMING;
		case ARES_CALIB:
			calData[hopping_frequency_no] = CC2500_ReadReg(CC2500_25_FSCAL1);
			hopping_frequency_no++;
			if (hopping_frequency_no < ARES_NUM_FREQUENCE)
				ARES_tune_chan();
			else
			{
				hopping_frequency_no = 0;
				phase = ARES_PREP;
			}
			return ARES_PREP_TIMING;
		case ARES_PREP:
			if (prev_option != option)
			{
				phase = ARES_START;
				return ARES_PREP_TIMING;
			}
			#ifdef MULTI_SYNC
				telemetry_set_input_sync(ARES_PACKET_PERIOD);
			#endif
			ARES_build_packet();
			phase = ARES_DATA;
			// Fall through
		case ARES_DATA:
			ARES_send_packet();
			hopping_frequency_no++;
			if (hopping_frequency_no >= ARES_NUM_FREQUENCE)
				hopping_frequency_no = 0;
			bind_phase++;
			if (bind_phase >= 3)
			{
				bind_phase = 0;
				// Advance counter to start of next group
				uint8_t step = crc;
				packet_count = ARES_next_counter(packet_count, step);
				packet_count = ARES_next_counter(packet_count, step);
				packet_count = ARES_next_counter(packet_count, step);
			}
			phase = ARES_PREP;
			return ARES_PACKET_PERIOD;
	}
	return 0;
}

void ARES_init()
{
	BIND_DONE;	// Autobind protocol - no TX-initiated bind phase
	ARES_RF_channels();

	// Counter step: must be 1-58 (all coprime with 59 since 59 is prime)
	crc = rx_tx_addr[2] % 58 + 1;

	// Counter start value
	packet_count = rx_tx_addr[1] % 59;

	#ifdef ARES_FORCE_ID
		rx_tx_addr[1] = 0xDC;
		rx_tx_addr[2] = 0xCC;
		rx_tx_addr[3] = 0x00;
		// Counter step and start from capture
		crc = 23;
		packet_count = 35;
		// Hopping table from capture
		memcpy((void *)hopping_frequency,
			(void *)"\xB0\x6F\x1D\xB4\x74\x20\xB8\xD8\x24\xBC\xDC\x28\x48\xE0\x2C\x4C\xE4\x90\x50\xE8\x94\x54\xEC\x00\x98\x58\x04\x9B\x5C\x08\xA0\xC0\x0C\xA4\xC3\x10\x30\xC6\x14\x34\xCC\x78\x38\xD0\x7C\x3C\xD4\x80\x40\x60\x84\x44\x64\x88\xA8\x68\x8C\xAC\x6C\x18",
			ARES_NUM_FREQUENCE);
	#endif
	phase = ARES_START;
}

#endif
