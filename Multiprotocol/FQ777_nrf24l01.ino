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
// Last sync with bikemike FQ777-124.ino

#if defined(FQ777_NRF24L01_INO)

#include "iface_nrf24l01.h"

#define FQ777_INITIAL_WAIT		500
#define FQ777_PACKET_PERIOD		2000
#define FQ777_PACKET_SIZE		8
#define FQ777_BIND_COUNT		400
#define FQ777_NUM_RF_CHANNELS	4

enum {
	FQ777_FLAG_RETURN     = 0x40,  // 0x40 when not off, !0x40 when one key return
	FQ777_FLAG_HEADLESS   = 0x04,
	FQ777_FLAG_EXPERT     = 0x01,
	FQ777_FLAG_FLIP       = 0x80,
};

#define XBM37_B5_BASE_STATE		0x20
#define XBM37_B5_OK			0x80
#define XBM37_B4_DISARMED		0x20
#define XBM37_B4_ARMED			0x21
#define XBM37_B6_RATE_LOW		0x00
#define XBM37_B6_RATE_MID		0x01
#define XBM37_B6_RATE_HIGH		0x02
#define XBM37_B6_LED_OFF		0x04
#define XBM37_B6_HEADLESS		0x10
#define XBM37_B6_VIDEO			0x20
#define XBM37_B6_PICTURE		0x40
#define XBM37_B6_FLIP			0x80
#define XBM37_BIND_OPEN_CHANNEL		0x00
#define XBM37_ARM_HOLD_PACKETS		200	// 200 * 2ms = ~400ms
#define XBM37_THROTTLE_LOW_THR		0xD0

const uint8_t ssv_xor[] = {0x80,0x44,0x64,0x75,0x6C,0x71,0x2A,0x36,0x7C,0xF1,0x6E,0x52,0x9,0x9D,0x1F,0x78,0x3F,0xE1,0xEE,0x16,0x6D,0xE8,0x73,0x9,0x15,0xD7,0x92,0xE7,0x3,0xBA};
uint8_t FQ777_bind_addr []   = {0xe7,0xe7,0xe7,0xe7,0x67};
// XBM-37-specific state while prototyping protocol alignment inside the FQ777 path.
static uint16_t xbm37_low_throttle_count;
static uint8_t xbm37_armed;
static uint8_t xbm37_first_bind_open_sent;

static void __attribute__((unused)) ssv_pack_dpl(uint8_t addr[], uint8_t pid, uint8_t* len, uint8_t* payload, uint8_t* packed_payload)
{
	uint8_t i = 0;

	uint16_t pcf = (*len & 0x3f) << 3;
	pcf |= (pid & 0x3) << 1;
	pcf |= 0x00; // noack field
	
	uint8_t header[7] = {0};
	header[6] = pcf;
	header[5] = (pcf >> 7) | (addr[0] << 1);
	header[4] = (addr[0] >> 7) | (addr[1] << 1);
	header[3] = (addr[1] >> 7) | (addr[2] << 1);
	header[2] = (addr[2] >> 7) | (addr[3] << 1);
	header[1] = (addr[3] >> 7) | (addr[4] << 1);
	header[0] = (addr[4] >> 7);

	// calculate the crc
	crc=0x3c18;
	for (i = 0; i < 7; ++i)
		crc16_update(header[i],8);
	for (i = 0; i < *len; ++i)
		crc16_update(payload[i],8);

	// encode payload and crc
	// xor with this:
	for (i = 0; i < *len; ++i)
		payload[i] ^= ssv_xor[i];
	crc ^= ssv_xor[i++]<<8;
	crc ^= ssv_xor[i++];

	// pack the pcf, payload, and crc into packed_payload
	packed_payload[0] = pcf >> 1;
	packed_payload[1] = (pcf << 7) | (payload[0] >> 1);
	
	for (i = 0; i < *len - 1; ++i)
		packed_payload[i+2] = (payload[i] << 7) | (payload[i+1] >> 1);

	packed_payload[i+2] = (payload[i] << 7) | (crc >> 9);
	++i;
	packed_payload[i+2] = (crc >> 1 & 0x80 ) | (crc >> 1 & 0x7F);
	++i;
	packed_payload[i+2] = (crc << 7);

	*len += 4;
}

static void __attribute__((unused)) FQ777_send_packet()
{
	uint8_t packet_len = FQ777_PACKET_SIZE;
	uint8_t packet_ori[8];
	if (IS_BIND_IN_PROGRESS)
	{
		// 4,5,6 = address fields
		// last field is checksum of address fields
		// XBM-37 bind constants (kept through Test #3)
		packet_ori[0] = 0x20;
		packet_ori[1] = 0x14;
		packet_ori[2] = 0x07;
		packet_ori[3] = 0x03;
		packet_ori[4] = rx_tx_addr[0];
		packet_ori[5] = rx_tx_addr[1];
		packet_ori[6] = rx_tx_addr[2];
		packet_ori[7] = packet_ori[4] + packet_ori[5] + packet_ori[6];
	}
	else
	{
		// throt, yaw, pitch, roll, trims, flags/left button,00,right button
		//0-3 0x00-0x64
		//4 roll/pitch/yaw trims. cycles through one trim at a time - 0-40 trim1, 40-80 trim2, 80-C0 trim3 (center:  A0 20 60)
		//5 flags for throttle button, two buttons above throttle - def: 0x40
		//6 00 ??
		//7 checksum - add values in other fields 

		
		// XBM-37 throttle endpoint direction from 05b capture: 0xE1=low, 0x00=high.
		uint8_t throttle = convert_channel_16b_limit(THROTTLE,0xE1,0);
		packet_ori[0] = throttle;
		packet_ori[1] = convert_channel_16b_limit(RUDDER,0,0xE1);
		packet_ori[2] = convert_channel_16b_limit(AILERON,0,0xE1);
		packet_ori[3] = convert_channel_16b_limit(ELEVATOR,0,0xE1);
		if (!xbm37_armed)
		{
			if (throttle >= XBM37_THROTTLE_LOW_THR)
			{
				if (xbm37_low_throttle_count < XBM37_ARM_HOLD_PACKETS)
					xbm37_low_throttle_count++;
				if (xbm37_low_throttle_count >= XBM37_ARM_HOLD_PACKETS)
					xbm37_armed = 1;
			}
			else
				xbm37_low_throttle_count = 0;
		}
		packet_ori[4] = xbm37_armed ? XBM37_B4_ARMED : XBM37_B4_DISARMED;
		// Test #3: migrate B5/B6 semantics to observed XBM-37 layout.
		uint8_t rate_bits;
		// CH11 three-position interpretation: low (<CHANNEL_MIN_COMMAND), mid, high (CH11_SW).
		if (CH11_SW)
			rate_bits = XBM37_B6_RATE_HIGH;		// high rate
		else if (Channel_data[CH11] < CHANNEL_MIN_COMMAND)
			rate_bits = XBM37_B6_RATE_LOW;		// low rate
		else
			rate_bits = XBM37_B6_RATE_MID;		// medium rate

		packet_ori[5] = XBM37_B5_BASE_STATE		// base state bit
			      | GET_FLAG(CH10_SW, XBM37_B5_OK);	// OK button
		packet_ori[6] = rate_bits			// bits[1:0] = rate
			      | GET_FLAG(!CH6_SW, XBM37_B6_LED_OFF)	// bit2 = LED off
			      | GET_FLAG(CH7_SW, XBM37_B6_HEADLESS)	// bit4 = headless
			      | GET_FLAG(CH8_SW, XBM37_B6_VIDEO)	// bit5 = video
			      | GET_FLAG(CH9_SW, XBM37_B6_PICTURE)	// bit6 = picture
			      | GET_FLAG(CH5_SW, XBM37_B6_FLIP);	// bit7 = flip
		// calculate checksum
		uint8_t checksum = 0;
		for (int i = 0; i < 7; ++i)
			checksum += packet_ori[i];
		packet_ori[7] = checksum;

		packet_count++;
	}

	ssv_pack_dpl( IS_BIND_IN_PROGRESS ? FQ777_bind_addr : rx_tx_addr, hopping_frequency_no, &packet_len, packet_ori, packet);
	
	uint8_t rf_ch;
	if (IS_BIND_IN_PROGRESS && !xbm37_first_bind_open_sent)
	{
		rf_ch = XBM37_BIND_OPEN_CHANNEL;
		xbm37_first_bind_open_sent = 1;
	}
	else
	{
		rf_ch = hopping_frequency[hopping_frequency_no++];
		hopping_frequency_no %= FQ777_NUM_RF_CHANNELS;
	}
	NRF24L01_WriteReg(NRF24L01_00_CONFIG,_BV(NRF24L01_00_PWR_UP));
	NRF24L01_WriteReg(NRF24L01_05_RF_CH, rf_ch);
	NRF24L01_WriteReg(NRF24L01_07_STATUS, 0x70);
	NRF24L01_FlushTx();
	NRF24L01_WritePayload(packet, packet_len);
	NRF24L01_WritePayload(packet, packet_len);
	NRF24L01_WritePayload(packet, packet_len);
}

static void __attribute__((unused)) FQ777_RF_init()
{
	NRF24L01_Initialize();

	NRF24L01_WriteRegisterMulti(NRF24L01_10_TX_ADDR, FQ777_bind_addr, 5);
	NRF24L01_SetBitrate(NRF24L01_BR_250K);
}

uint16_t FQ777_callback()
{
	#ifdef MULTI_SYNC
		telemetry_set_input_sync(FQ777_PACKET_PERIOD);
	#endif
	if(bind_counter)
	{
		bind_counter--;
		if (bind_counter == 0)
		{
			NRF24L01_WriteRegisterMulti(NRF24L01_10_TX_ADDR, rx_tx_addr, 5);
			BIND_DONE;
		}
	}
	FQ777_send_packet();
	return FQ777_PACKET_PERIOD;
}

void FQ777_init(void)
{
	BIND_IN_PROGRESS;	// autobind protocol
	bind_counter = FQ777_BIND_COUNT;
	packet_count=0;
	xbm37_armed = 0;
	xbm37_low_throttle_count = 0;
	xbm37_first_bind_open_sent = 0;
	// XBM-37 first test hop sequence
	hopping_frequency[0] = 0x49;
	hopping_frequency[1] = 0x34;
	hopping_frequency[2] = 0x26;
	hopping_frequency[3] = 0x07;
	hopping_frequency_no=0;
	rx_tx_addr[2] = 0x00;
	rx_tx_addr[3] = 0xe7;
	rx_tx_addr[4] = 0x67;
	FQ777_RF_init();
}

#endif
