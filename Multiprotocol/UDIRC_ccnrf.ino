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
// Models: UDIRC UD160x(PRO), Pinecone Models SG-160x/SG-1205, Eachine EAT15
//
// Packet flow (from RF captures of Pinecone SG-1205):
//
// BIND phase – TX hops all 4 channels using bind address 01:03:05:07:09:
//   TX sends: 01 <TX_ID[0..4]> 00 00 00 64 60 6C 00 00 <csum>
//   RX replies (zero-length enhanced ACK): P(0)=  (XN297 auto-ack)
//   Once bind_phase detected via RX 0x01 packet:
//   TX sends: 02 <TX_ID[0..4]> 00 00 00 64 60 6C 00 00 <csum>
//   After bind_counter reaches 0, bind is complete.
//
// NORMAL phase – TX stays on one fixed channel using TX address:
//   TX sends: 08 ST TH CH3 CH4 F8 00 00 30 GYRO TRIM DR FLAGS 00 <csum>
//   RX telemetry: 10 00 00 00 30 00 00 00 00 00 00 F8 00 00 30
//
// F8 and 30 bytes: captured from first RX packet (bind reply or telemetry).
// They appear at packet_in[11] and packet_in[14] and are placed into the
// normal TX control packet at positions [5] and [8].
//
// Checksum: sum of bytes [0..13], stored in [14].  packet[14] is zeroed by
// memset(&packet[3], 0, 12) before the accumulation, so += is correct.

#if defined(UDIRC_CCNRF_INO)

#include "iface_xn297.h"

#define FORCE_UDIRC_ORIGINAL_ID

#define UDIRC_PAYLOAD_SIZE		15
#define UDIRC_RF_NUM_CHANNELS	4
#define UDIRC_PACKET_PERIOD		20000
#define UDIRC_BIND_COUNT		10
#define UDIRC_WRITE_TIME		6000
#define UDIRC_RX_TIME			6000

enum {
	UDIRC_DATA1=0,
	UDIRC_RX,
	UDIRC_CHECK,
};

static void __attribute__((unused)) UDIRC_send_packet()
{
	memset(&packet[3], 0x00, 12);
	debug("bp=%d ", bind_phase);
	if(IS_BIND_IN_PROGRESS)
	{//Bind in progress
		if(packet_count > 4 && bind_phase == 0)
		{
			//Hop frequencies until the RX responds
			XN297_Hopping(hopping_frequency_no);
			debug("H%d(%d) ", hopping_frequency_no, hopping_frequency[hopping_frequency_no]);
			hopping_frequency_no++;
			hopping_frequency_no &= 3;
			packet_count = 0;
		}
		if(bind_phase && bind_counter)
		{
			bind_counter--;
			if(bind_counter == 0)
			{
				bind_phase = 3;
				BIND_DONE;
				debugln("UDIRC bind complete");
			}
		}
		//Build bind packet: 0x01 during initial invite, 0x02 after RX responds
		packet[0] = 0x01;
		if(bind_phase)
			packet[0]++;	// 0x02 ack phase
		memcpy(&packet[1], rx_tx_addr, 5);
	}
	if(bind_phase > 1)
	{//Switch to normal TX address
		XN297_SetTXAddr(rx_tx_addr, 5);
		XN297_SetRXAddr(rx_tx_addr, UDIRC_PAYLOAD_SIZE);
		debugln("UDIRC switched to TX addr");
	}
	if(IS_BIND_DONE)
	{//Normal control packet
		packet[0] = 0x08;
		//Channels SG-16xx/SG-1205: ST/TH/CH3/CH4 / F8/00/00/30 / GYRO/ST_TRIM/ST_DR
		//Channels EAT15           : ST/TH/RATE/LIGHT / F8/00/00/30 / GYRO/ST_TRIM/ST_DR
		for(uint8_t i = 0; i < 12; i++)
			packet[i+1] = convert_channel_16b_limit(i, 0, 200);
		// Bytes from RX (captured at packet_in[11..14]) go into packet[5..8]
		// Verified from captures: telemetry 0x10 has F8 at [11] and 30 at [14]
		if(packet_in[0] != 0)
		{
			packet[5] = packet_in[11];	// F8 (RX-assigned)
			packet[6] = packet_in[12];	// 00
			packet[7] = packet_in[13];	// 00
			packet[8] = packet_in[14];	// 30 (RX-assigned)
		}
		else
		{
			packet[5] = packet[6] = packet[7] = packet[8] = 0;
		}
	}
	packet[12] = GET_FLAG(CH12_SW, 0x40)		//TH.REV
				|GET_FLAG(CH13_SW, 0x80);		//ST.REV
	//packet[13] = 00; //Unknown, future flags?
	// Checksum: sum of [0..13]. packet[14] is already 0 from memset above.
	for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE-1; i++)
		packet[14] += packet[i];
	// Send
	XN297_SetFreqOffset();
	XN297_SetPower();
	XN297_SetTxRxMode(TX_EN);
	XN297_WriteEnhancedPayload(packet, UDIRC_PAYLOAD_SIZE, false);
	packet_count++;
	#ifdef DEBUG_SERIAL
		debug("TX: ");
		for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE; i++)
			debug("%02X ", packet[i]);
		debugln();
	#endif
}

static void __attribute__((unused)) UDIRC_initialize_txid()
{
	#ifdef FORCE_UDIRC_ORIGINAL_ID
		if(RX_num == 1)
		{
			rx_tx_addr[0] = 0xD0;
			rx_tx_addr[1] = 0x06;
			rx_tx_addr[2] = 0x00;
			rx_tx_addr[3] = 0x00;
			rx_tx_addr[4] = 0x81;
		}
		else if(RX_num == 2)
		{
			rx_tx_addr[0] = 0xF6;
			rx_tx_addr[1] = 0x96;
			rx_tx_addr[2] = 0x01;
			rx_tx_addr[3] = 0x00;
			rx_tx_addr[4] = 0x81;
		}
		else	// Pinecone SG-1205 (RX_num=0 or default)
		{
			rx_tx_addr[0] = 0xC3;
			rx_tx_addr[1] = 0xE4;
			rx_tx_addr[2] = 0x04;
			rx_tx_addr[3] = 0x00;
			rx_tx_addr[4] = 0x81;
		}
	#endif
	// Capture-verified hop channel order: 45, 52, 59, 67 (hex: 2D 34 3B 43)
	memcpy(hopping_frequency, (uint8_t*)"\x2D\x34\x3B\x43", UDIRC_RF_NUM_CHANNELS);
}

static void __attribute__((unused)) UDIRC_RF_init()
{
	XN297_Configure(XN297_CRCEN, XN297_SCRAMBLED, XN297_250K);
	// Use bind address during bind phase
	XN297_SetTXAddr((uint8_t*)"\x01\x03\x05\x07\x09", 5);
	XN297_SetRXAddr((uint8_t*)"\x01\x03\x05\x07\x09", UDIRC_PAYLOAD_SIZE);
	XN297_HoppingCalib(UDIRC_RF_NUM_CHANNELS);
	XN297_Hopping(0);
}

uint16_t UDIRC_callback()
{
	bool rx;
	switch(phase)
	{
		case UDIRC_DATA1:
			#ifdef MULTI_SYNC
				telemetry_set_input_sync(UDIRC_PACKET_PERIOD);
			#endif
			UDIRC_send_packet();
			phase++;	// → UDIRC_RX
			return UDIRC_WRITE_TIME;

		case UDIRC_RX:
			// Switch to RX to listen for telemetry or bind reply
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(RX_EN);
			phase++;	// → UDIRC_CHECK
			return UDIRC_RX_TIME;

		default:	// UDIRC_CHECK
			rx = XN297_IsRX();
			XN297_SetTxRxMode(TXRX_OFF);
			if(rx)
			{
				uint8_t val = XN297_ReadEnhancedPayload(packet_in, UDIRC_PAYLOAD_SIZE);
				debug("RX(%d):", val);
				if(val == UDIRC_PAYLOAD_SIZE)
				{//Good CRC and length
					#ifdef DEBUG_SERIAL
						for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE; i++)
							debug(" %02X", packet_in[i]);
					#endif
					if(packet_in[0] == 0x10)
					{//Telemetry packet
						v_lipo1 = (packet_in[1] == 0x01) ? 0x00 : 0xFF;	// Low voltage flag
						telemetry_link = 1;
						debug(" (telem)");
					}
					else
					{//Bind reply from RX (0x01 or 0x02) - send enhanced ACK
						XN297_SetTxRxMode(TX_EN);
						XN297_WriteEnhancedPayload(packet, 0, false);
						if(IS_BIND_IN_PROGRESS && packet_in[0] == 0x01)
						{
							// RX confirmed our TX ID - start ack phase
							bind_phase = 1;
							debug(" (bind reply -> start ack)");
						}
					}
				}
				debugln();
			}
			phase = UDIRC_DATA1;
			return UDIRC_PACKET_PERIOD - UDIRC_WRITE_TIME - UDIRC_RX_TIME;
	}
	return 0;
}

void UDIRC_init()
{
	UDIRC_initialize_txid();
	UDIRC_RF_init();

	// Clear packet_in so packet_in[0] check works correctly on first normal packet
	memset(packet_in, 0x00, UDIRC_PAYLOAD_SIZE);

	if(IS_BIND_IN_PROGRESS)
	{
		bind_counter = UDIRC_BIND_COUNT;
		bind_phase = 0;
	}
	else
		bind_phase = 3;
	phase = UDIRC_DATA1;
	hopping_frequency_no = 0;
	packet_count = 0;
	RX_RSSI = 100;	// Dummy value
}

#endif
