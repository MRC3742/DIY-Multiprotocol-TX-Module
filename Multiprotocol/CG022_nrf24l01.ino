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

#if defined(CG022_NRF24L01_INO)

#include "iface_nrf24l01.h"

#define CG022_PACKET_PERIOD		2315
#define CG022_BIND_COUNT		166
#define CG022_PACKET_SIZE		10
#define CG022_RF_CHANNEL_COUNT	8

static const uint8_t PROGMEM CG022_hop[] = { 0x0A, 0x32, 0x14, 0x3C, 0x1E, 0x46, 0x00, 0x28 };
static const uint8_t CG022_addr[] = { 0x5A, 0x5A, 0x00, 0x33 };
static const uint8_t CG022_bind_id[] = { 0x00, 0x11, 0x22, 0x33 };

static uint8_t CG022_scale_channel(uint8_t channel)
{
	return (convert_channel_8b(channel) + 2) >> 2;
}

static void __attribute__((unused)) CG022_set_channel()
{
	NRF24L01_WriteReg(NRF24L01_05_RF_CH, pgm_read_byte_near(&CG022_hop[hopping_frequency_no]));
	hopping_frequency_no++;
	if (hopping_frequency_no >= CG022_RF_CHANNEL_COUNT)
		hopping_frequency_no = 0;
}

static void __attribute__((unused)) CG022_build_bind_packet()
{
	packet[0] = 0x0A;
	packet[1] = CG022_bind_id[0];
	packet[2] = CG022_bind_id[1];
	packet[3] = CG022_bind_id[2];
	packet[4] = CG022_bind_id[3];
	packet[5] = 0x06;
	packet[6] = 0xAB;
	packet[7] = 0xFC;
	packet[8] = 0xAD;
	packet[9] = 0x00;
}

static void __attribute__((unused)) CG022_build_data_packet()
{
	packet[0] = 0x0A;
	packet[1] = CG022_bind_id[0];
	packet[2] = CG022_scale_channel(THROTTLE);
	packet[3] = CG022_scale_channel(ELEVATOR);
	packet[4] = CG022_scale_channel(RUDDER);
	packet[5] = CG022_scale_channel(AILERON);
	packet[6] = 0x20 | GET_FLAG(CH6_SW, 0x80);
	packet[7] = 0x20 | GET_FLAG(CH5_SW, 0x40) | GET_FLAG(CH7_SW, 0x80);
	packet[8] = 0x20;
	packet[9] = (packet[2] + packet[3] + packet[4] + packet[5] + packet[6] + packet[7] + packet[8]) & 0xFF;
}

static void __attribute__((unused)) CG022_send_packet()
{
	if (IS_BIND_IN_PROGRESS)
		CG022_build_bind_packet();
	else
		CG022_build_data_packet();

	CG022_set_channel();
	NRF24L01_WritePayload(packet, CG022_PACKET_SIZE);
	NRF24L01_SetPower();
}

static void __attribute__((unused)) CG022_RF_init()
{
	NRF24L01_Initialize();
	NRF24L01_WriteReg(NRF24L01_03_SETUP_AW, 0x02);
	NRF24L01_WriteRegisterMulti(NRF24L01_0A_RX_ADDR_P0, (uint8_t *)CG022_addr, sizeof(CG022_addr));
	NRF24L01_WriteRegisterMulti(NRF24L01_10_TX_ADDR, (uint8_t *)CG022_addr, sizeof(CG022_addr));
	NRF24L01_WriteReg(NRF24L01_11_RX_PW_P0, CG022_PACKET_SIZE);
	NRF24L01_SetBitrate(NRF24L01_BR_1M);
	NRF24L01_WriteReg(NRF24L01_00_CONFIG, _BV(NRF24L01_00_PWR_UP));
}

uint16_t CG022_callback()
{
	#ifdef MULTI_SYNC
		telemetry_set_input_sync(CG022_PACKET_PERIOD);
	#endif
	if (bind_counter)
	{
		bind_counter--;
		if (bind_counter == 0)
			BIND_DONE;
	}
	CG022_send_packet();
	return CG022_PACKET_PERIOD;
}

void CG022_init(void)
{
	BIND_IN_PROGRESS;	// autobind protocol
	bind_counter = CG022_BIND_COUNT;
	hopping_frequency_no = 0;
	CG022_RF_init();
	packet_period = CG022_PACKET_PERIOD;
}

#endif
