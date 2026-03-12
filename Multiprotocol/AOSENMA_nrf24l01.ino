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

#if defined(AOSENMA_NRF24L01_INO)

#include "iface_nrf24l01.h"

#define AOSENMA_PACKET_PERIOD	2100
#define AOSENMA_BIND_COUNT		166
#define AOSENMA_PACKET_SIZE		9
#define AOSENMA_CHECKSUM_START	1
#define AOSENMA_CHECKSUM_END	8
#define AOSENMA_ACK_TIMEOUT		1000
#define AOSENMA_LT8900_FLAGS	(_BV(6) | _BV(4))	// LT8910-compatible config flag bits: CRC enable + packet-length byte

// #define AOSENMA_CG022_FORCE_ID	// Original CG022 TX ID from analyzed captures: 11 22 33 06 AB

const uint8_t PROGMEM AOSENMA_hopping[] = { 0, 40, 10, 50, 20, 60, 30, 70 };	// Raw power-on bind capture shows packet 1 on channel 0, then 40/10/50/20/60/30/70 without resetting at bind completion

static void __attribute__((unused)) AOSENMA_set_bind_sync()
{
	uint8_t bind_sync[] = { 0x11, 0x22 };
	LT8900_SetAddress(bind_sync, 2);
}

static void __attribute__((unused)) AOSENMA_set_data_sync()
{
	uint8_t data_sync[] = { rx_tx_addr[3], rx_tx_addr[4] };
	LT8900_SetAddress(data_sync, 2);
}

static void __attribute__((unused)) AOSENMA_RF_init()
{
	NRF24L01_Initialize();
	LT8900_Config(4, 8, AOSENMA_LT8900_FLAGS, 0xAA);
	AOSENMA_set_bind_sync();
	LT8900_SetChannel(pgm_read_byte_near(&AOSENMA_hopping[0]));
	LT8900_SetTxRxMode(TX_EN);
}

static void __attribute__((unused)) AOSENMA_initialize_txid()
{
	#ifdef AOSENMA_CG022_FORCE_ID			// data taken from TX dump
		rx_tx_addr[0] = 0x11;
		rx_tx_addr[1] = 0x22;
		rx_tx_addr[2] = 0x33;
		rx_tx_addr[3] = 0x06;
		rx_tx_addr[4] = 0xAB;
	#endif
}

static void __attribute__((unused)) AOSENMA_send_bind_packet()
{
	packet[0] = 0x00;
	packet[1] = rx_tx_addr[0];
	packet[2] = rx_tx_addr[1];
	packet[3] = rx_tx_addr[2];
	packet[4] = rx_tx_addr[3];
	packet[5] = rx_tx_addr[4];
	packet[6] = 0xFC;
	packet[7] = 0xAD;
	packet[8] = 0x00;
}

static void __attribute__((unused)) AOSENMA_send_data_packet()
{
	packet[0] = convert_channel_16b_limit(THROTTLE, 0x00, 0x3F);
	packet[1] = 0x00;
	packet[2] = convert_channel_16b_limit(ELEVATOR, 0x00, 0x3F);
	packet[3] = 0x20;
	packet[4] = convert_channel_16b_limit(RUDDER, 0x00, 0x3F);
	packet[5] = convert_channel_16b_limit(AILERON, 0x00, 0x3F);
	packet[6] = 0x20;
	packet[7] = 0x20;
	packet[8] = 0x00;
	for(uint8_t i = AOSENMA_CHECKSUM_START; i < AOSENMA_CHECKSUM_END; i++)
		packet[8] = (uint8_t)(packet[8] + packet[i]);
}

static void __attribute__((unused)) AOSENMA_send_packet()
{
	if(IS_BIND_IN_PROGRESS)
		AOSENMA_send_bind_packet();
	else
		AOSENMA_send_data_packet();

	LT8900_SetChannel(pgm_read_byte_near(&AOSENMA_hopping[hopping_frequency_no]));
	hopping_frequency_no++;
	if(hopping_frequency_no >= sizeof(AOSENMA_hopping))
		hopping_frequency_no = 0;

	LT8900_WritePayload(packet, AOSENMA_PACKET_SIZE);
	for(uint16_t i = 0; i < AOSENMA_ACK_TIMEOUT; i++)
		if(NRF24L01_packet_ack() != PKT_PENDING)
			break;
	NRF24L01_SetPower();
}

uint16_t AOSENMA_callback()
{
	#ifdef MULTI_SYNC
		telemetry_set_input_sync(AOSENMA_PACKET_PERIOD);
	#endif

	AOSENMA_send_packet();

	if(bind_counter)
	{
		bind_counter--;
		if(bind_counter == 0)
		{
			BIND_DONE;
			AOSENMA_set_data_sync();
		}
	}
	return AOSENMA_PACKET_PERIOD;
}

void AOSENMA_init()
{
	BIND_IN_PROGRESS;	// autobind protocol
	bind_counter = AOSENMA_BIND_COUNT;
	hopping_frequency_no = 0;
	packet_period = AOSENMA_PACKET_PERIOD;
	AOSENMA_initialize_txid();
	AOSENMA_RF_init();
}

#endif
