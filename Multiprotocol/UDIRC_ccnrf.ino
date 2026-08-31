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
//Models: UDIRC UD160x(PRO), Pinecone Models SG-160x, Eachine EAT15

#if defined(UDIRC_CCNRF_INO)

#include "iface_xn297.h"

#define FORCE_UDIRC_ORIGINAL_ID

#define UDIRC_PAYLOAD_SIZE			15
#define UDIRC_RF_NUM_CHANNELS		4
#define UDIRC_PACKET_PERIOD			20000
#define UDIRC_BIND_COUNT			2000
#define UDIRC_WRITE_TIME			6000
#define UDIRC_RX_TIME				6000

enum {
	UDIRC_DATA1=0,
	UDIRC_RX,
	UDIRC_CHECK,
};

static void __attribute__((unused)) UDIRC_send_packet()
{
	memset(&packet[3], 0x00, 12);
	debug("b%D : ",bind_phase);
	if(IS_BIND_IN_PROGRESS)
	{//Bind in progress
		if(packet_count>4 && bind_phase == 0)
		{
			//Change frequencies until the RX responds
			XN297_Hopping(hopping_frequency_no);
			debug("H %d ",hopping_frequency_no);
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
			}
		}
		//Build bind packet
		packet[0] = 0x01;
		if(bind_phase)
			packet[0]++;
		memcpy(&packet[1],rx_tx_addr,5);
	}
	if(bind_phase > 1)
	{//Switch to normal
		XN297_SetTXAddr(rx_tx_addr, 5);
		XN297_SetRXAddr(rx_tx_addr, UDIRC_PAYLOAD_SIZE);
	}
	if(IS_BIND_DONE)
	{//Normal
		packet[0] = 0x08;
		//Channels SG-16xx: ST/TH/CH4 /CH3  /UNK/UNK/UNK/UNK/GYRO/ST_TRIM/ST_DR
		//Channels EAT15  : ST/TH/RATE/LIGHT/UNK/UNK/UNK/UNK/GYRO/ST_TRIM/ST_DR
		for(uint8_t i=0; i<12; i++)
			packet[i+1] = convert_channel_16b_limit(i,0,200);
	
		if(packet_in[0] >= 0x01)
		{
			packet[5] = packet_in[11];
			packet[6] = packet_in[12];
			packet[7] = packet_in[13];
			packet[8] = packet_in[14];
		}
		else 
		{
			packet[5] = packet[6] = packet[7] = packet[8] = 0;
		}
	}

	packet[12] = GET_FLAG(CH12_SW,  0x40)						//TH.REV
				|GET_FLAG(CH13_SW,  0x80);						//ST.REV
	//packet[13] = 00; //Unknown, future flags?
	for(uint8_t i=0;i<UDIRC_PAYLOAD_SIZE-1;i++)
		packet[14] += packet[i];
	// Send
	XN297_SetFreqOffset();
	XN297_SetPower();
	XN297_SetTxRxMode(TX_EN);
	XN297_WriteEnhancedPayload(packet, UDIRC_PAYLOAD_SIZE,false);
	packet_count++;
	//#if 0
	#ifdef DEBUG_SERIAL
		for(uint8_t i=0; i < UDIRC_PAYLOAD_SIZE; i++)
			debug("%02X ", packet[i]);
		debugln();
	#endif
}

static void __attribute__((unused)) UDIRC_initialize_txid()
{
	#ifdef FORCE_UDIRC_ORIGINAL_ID
		if(RX_num==1)
		{
			rx_tx_addr[0] = 0xD0;
			rx_tx_addr[1] = 0x06;
			rx_tx_addr[2] = 0x00;
			rx_tx_addr[3] = 0x00;
			rx_tx_addr[4] = 0x81;
		}
		else if (RX_num==2)
		{
			rx_tx_addr[0] = 0xF6;
			rx_tx_addr[1] = 0x96;
			rx_tx_addr[2] = 0x01;
			rx_tx_addr[3] = 0x00;
			rx_tx_addr[4] = 0x81;
		}
		else // Pinecone model SG-1205
		{
			rx_tx_addr[0] = 0xC3;
			rx_tx_addr[1] = 0xE4;
			rx_tx_addr[2] = 0x04;
			rx_tx_addr[3] = 0x00;
			rx_tx_addr[4] = 0x81;
		}
	#endif
	memcpy(hopping_frequency,(uint8_t*)"\x2D\x3B\x34\x43",UDIRC_RF_NUM_CHANNELS);		//45,59,52,67
}

static void __attribute__((unused)) UDIRC_RF_init()
{
	XN297_Configure(XN297_CRCEN, XN297_SCRAMBLED, XN297_250K);
	//Bind address
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
			debug(".")
			phase++;	//UDIRC_RX;
			return UDIRC_WRITE_TIME;
		case UDIRC_RX:
			//Switch to RX
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(RX_EN);
			phase++;	//UDIRC_CHECK
			return UDIRC_RX_TIME;
		default:		//UDIRC_CHECK
			rx = XN297_IsRX();
			XN297_SetTxRxMode(TXRX_OFF);
			if(rx)
			{
				uint8_t val = XN297_ReadEnhancedPayload(packet_in, UDIRC_PAYLOAD_SIZE);
				debug("RX(%d):",val);
				if(val == UDIRC_PAYLOAD_SIZE)
				{//Good CRC and length
					if(packet_in[0] == 0x10)
					{//Telemetry packets
						v_lipo1 = packet_in[1] == 0x01 ? 0x00:0xFF;	//Low voltage flag
						telemetry_link = 1;
					}
					else
					{//Send ack
						XN297_SetTxRxMode(TX_EN);
						XN297_WriteEnhancedPayload(packet, 0,false);
						if(IS_BIND_IN_PROGRESS && packet_in[0] == 0x01)
							bind_phase = 1;
					}
					#ifdef DEBUG_SERIAL
						for(uint8_t i=0; i < UDIRC_PAYLOAD_SIZE; i++)
							debug(" %02X", packet_in[i]);
					#endif
				}
				debugln("");
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

	if(IS_BIND_IN_PROGRESS)
	{
		bind_counter = 10;
		bind_phase = 0;
	}
	else
		bind_phase = 3;
	phase = UDIRC_DATA1;
	hopping_frequency_no = 0;
	packet_count = 0;
	RX_RSSI = 100;		// Dummy value
}

#endif
