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
// Last sync with deviation main github branch

#if defined(SLT_CCNRF_INO)

#include "iface_nrf250k.h"

//#define SLT_Q200_FORCE_ID
//#define SLT_V1_4_FORCE_ID

// For code readability
#define SLT_PAYLOADSIZE_V1		7
#define SLT_PAYLOADSIZE_V1_4	5
#define SLT_PAYLOADSIZE_V2		11
#define SLT_PAYLOADSIZE_Q100	19
#define SLT_NFREQCHANNELS		15
#define SLT_TXID_SIZE			4
#define SLT_BIND_CHANNEL		0x50

enum{
	// flags going to packet[6] (Q200)
	FLAG_Q200_FMODE	= 0x20,
	FLAG_Q200_VIDON	= 0x10,
	FLAG_Q200_FLIP	= 0x08,
	FLAG_Q200_VIDOFF= 0x04,
};

enum{
	// flags going to packet[6] (MR100 & Q100)
	FLAG_MR100_FMODE	= 0x20,
	FLAG_MR100_FLIP		= 0x04,
	FLAG_MR100_VIDEO	= 0x02,
	FLAG_MR100_PICTURE	= 0x01,
};

enum {
	SLT_BUILD=0,
	SLT_DATA,
	SLT_LAST_DATA,
	SLT_BIND1,
	SLT_BIND2,
};

static void __attribute__((unused)) SLT_RF_init()
{
	NRF250K_Init();
	NRF250K_SetTXAddr(rx_tx_addr, SLT_TXID_SIZE);
}

static void __attribute__((unused)) SLT_set_freq(void)
{
	// Frequency hopping sequence generation
	for (uint8_t i = 0; i < SLT_TXID_SIZE; ++i)
	{
		uint8_t next_i = (i+1) % SLT_TXID_SIZE; // is & 3 better than % 4 ?
		uint8_t base = i < 2 ? 0x03 : 0x10;
		hopping_frequency[i*4 + 0]  = (rx_tx_addr[i] & 0x3f) + base;
		hopping_frequency[i*4 + 1]  = (rx_tx_addr[i] >> 2) + base;
		hopping_frequency[i*4 + 2]  = (rx_tx_addr[i] >> 4) + (rx_tx_addr[next_i] & 0x03)*0x10 + base;
		hopping_frequency[i*4 + 3]  = (rx_tx_addr[i] >> 6) + (rx_tx_addr[next_i] & 0x0f)*0x04 + base;
	}

	// Unique freq
	uint8_t max_freq = 0x50;	//V1, V2, and MR100
	if(sub_protocol == Q200)
		max_freq=45;
	for (uint8_t i = 0; i < SLT_NFREQCHANNELS; ++i)
	{
		if(sub_protocol == Q200 && hopping_frequency[i] >= max_freq)
			hopping_frequency[i] = hopping_frequency[i] - max_freq + 0x03;
		uint8_t done = 0;
		while (!done)
		{
			done = 1;
			for (uint8_t j = 0; j < i; ++j)
				if (hopping_frequency[i] == hopping_frequency[j])
				{
					done = 0;
					hopping_frequency[i] += 7;
					if (hopping_frequency[i] >= max_freq)
						hopping_frequency[i] = hopping_frequency[i] - max_freq + 0x03;
				}
		}
	}
	#ifdef DEBUG_SERIAL
		debug("CH:");
		for (uint8_t i = 0; i < SLT_NFREQCHANNELS; ++i)
			debug(" %02X(%d)", hopping_frequency[i], hopping_frequency[i]);
		debugln();
	#endif
	
	//Bind channel
	hopping_frequency[SLT_NFREQCHANNELS] = SLT_BIND_CHANNEL;
	
	//Calib all channels
	NRF250K_HoppingCalib(SLT_NFREQCHANNELS+1);
}

static void __attribute__((unused)) SLT_wait_radio()
{
	if (packet_sent)
		while (!NRF250K_IsPacketSent());
	packet_sent = 0;
}

static void __attribute__((unused)) SLT_send_packet(uint8_t len)
{
	SLT_wait_radio();
	NRF250K_WritePayload(packet, len);
	packet_sent = 1;
}

static void __attribute__((unused)) SLT_build_packet()
{
	static uint8_t calib_counter=0;
	
	// Set radio channel - once per packet batch
	NRF250K_SetFreqOffset();	// Set frequency offset
	NRF250K_Hopping(hopping_frequency_no);
	if (++hopping_frequency_no >= SLT_NFREQCHANNELS)
		hopping_frequency_no = 0;

	// aileron, elevator, throttle, rudder, gear, pitch
	uint8_t e = 0; // byte where extension 2 bits for every 10-bit channel are packed
	for (uint8_t i = 0; i < 4; ++i)
	{
		uint16_t v = convert_channel_10b(sub_protocol != SLT_V1_4 ? CH_AETR[i] : i, false);
		if(sub_protocol>SLT_V2 && (i==CH2 || i==CH3) && sub_protocol != SLT_V1_4 && sub_protocol != RF_SIM)
			v=1023-v;	// reverse throttle and elevator channels for Q100/Q200/MR100 protocols
		packet[i] = v;
		e = (e >> 2) | (uint8_t) ((v >> 2) & 0xC0);
	}
	// Extra bits for AETR
	packet[4] = e;

	//->V1_4CH stops here

	// 8-bit channels
	packet[5] = convert_channel_8b(CH5);
	packet[6] = convert_channel_8b(CH6);

	//->V1 stops here

	if(sub_protocol == Q200)
		packet[6] =  GET_FLAG(CH9_SW , FLAG_Q200_FMODE)
					|GET_FLAG(CH10_SW, FLAG_Q200_FLIP)
					|GET_FLAG(CH11_SW, FLAG_Q200_VIDON)
					|GET_FLAG(CH12_SW, FLAG_Q200_VIDOFF);
	else if(sub_protocol == MR100 || sub_protocol == Q100)
		packet[6] =  GET_FLAG(CH9_SW , FLAG_MR100_FMODE)
					|GET_FLAG(CH10_SW, FLAG_MR100_FLIP)
					|GET_FLAG(CH11_SW, FLAG_MR100_VIDEO)	// Does not exist on the Q100 but...
					|GET_FLAG(CH12_SW, FLAG_MR100_PICTURE);	// Does not exist on the Q100 but...
	packet[7] = convert_channel_8b(CH7);
	packet[8] = convert_channel_8b(CH8);
	if(sub_protocol == RF_SIM)
	{
		packet[9]  = convert_channel_8b(CH9);
		packet[10] = convert_channel_8b(CH10);
	}
	else
	{
		packet[9]  = 0xAA;		//normal mode for Q100/Q200, unknown for V2/MR100
		packet[10] = 0x00;		//normal mode for Q100/Q200, unknown for V2/MR100
	}
	if((sub_protocol == Q100 || sub_protocol == Q200) && CH13_SW)
	{//Calibrate
		packet[9] = 0x77;			//enter calibration
		if(calib_counter >= 20 && calib_counter <= 25)	// 7 packets for Q100 / 3 packets for Q200
			packet[10] = 0x20;	//launch calibration
		calib_counter++;
		if(calib_counter > 250) calib_counter = 250;
	}
	else
		calib_counter = 0;

	// Q100 uses 19-byte packets: bytes[11..18] are zero-padded
	if(sub_protocol == Q100)
		for(uint8_t i = 11; i < SLT_PAYLOADSIZE_Q100; i++)
			packet[i] = 0x00;
}

static void __attribute__((unused)) SLT_send_bind_packet()
{
	SLT_wait_radio();
	if(phase == SLT_BIND2)
		NRF250K_Hopping(SLT_NFREQCHANNELS);	//Bind channel for BIND2 only
	BIND_IN_PROGRESS;					//Limit TX power to bind level
	NRF250K_SetPower();
	BIND_DONE;
	NRF250K_SetTXAddr((uint8_t *)"\x7E\xB8\x63\xA9", SLT_TXID_SIZE);
	memcpy((void*)packet, (void*)rx_tx_addr, SLT_TXID_SIZE);
	if(phase == SLT_BIND2)
		SLT_send_packet(SLT_TXID_SIZE);
	else // SLT_BIND1
		SLT_send_packet(packet_length);
}

#define SLT_FRAME_PERIOD		18000
#define SLT_TIMING_BUILD		1000
#define SLT_V1_TIMING_PACKET	1600
#define SLT_V1_4_TIMING_PACKET	1643
#define SLT_V2_TIMING_PACKET	2042
#define SLT_Q100_TIMING_PACKET	1897
#define SLT_MR100_TIMING_PACKET	2007
#define SLT_V1_TIMING_BIND2		1000
#define SLT_V2_TIMING_BIND1		6507
#define SLT_V2_TIMING_BIND2		2112
#define SLT_Q100_TIMING_BIND1	3652
#define SLT_Q100_TIMING_BIND2	1217
#define SLT_MR100_TIMING_BIND2	1008
uint16_t SLT_callback()
{
	switch (phase)
	{
		case SLT_BUILD:
			#ifdef MULTI_SYNC
				telemetry_set_input_sync(packet_period);
			#endif
			SLT_build_packet();
			NRF250K_SetPower();					//Change power level
			NRF250K_SetTXAddr(rx_tx_addr, SLT_TXID_SIZE);
			bind_phase = 0;						//Reset data packet counter
			phase = SLT_DATA;
			if(sub_protocol == MR100)
				return 0;						//MR100: no build delay
			return SLT_TIMING_BUILD;
		case SLT_DATA:
			SLT_send_packet(packet_length);
			bind_phase++;
			if(bind_phase >= rf_ch_num)
			{// Last data packet of the frame
				phase = SLT_LAST_DATA;
			}
			if(sub_protocol == SLT_V1 || sub_protocol == SLT_V2 || sub_protocol == RF_SIM)
				return SLT_V1_TIMING_PACKET;
			if(sub_protocol == SLT_V1_4)
				return SLT_V1_4_TIMING_PACKET;
			if(sub_protocol == Q100)
				return SLT_Q100_TIMING_PACKET;
			if(sub_protocol == MR100)
				return SLT_MR100_TIMING_PACKET;
			//Q200
			return SLT_V2_TIMING_PACKET;
		case SLT_LAST_DATA:
			SLT_send_packet(packet_length);
			if (++packet_count >= num_ch)
			{// Send bind packet
				packet_count = 0;
				if(sub_protocol == SLT_V1 || sub_protocol == SLT_V1_4 || sub_protocol == SLT_V2 || sub_protocol == RF_SIM)
				{
					phase = SLT_BIND2;
					return SLT_V1_TIMING_BIND2;
				}
				if(sub_protocol == MR100)
				{// MR100: only BIND2 (no BIND1)
					phase = SLT_BIND2;
					return SLT_MR100_TIMING_BIND2;
				}
				if(sub_protocol == Q100)
				{// Q100: BIND1+BIND2
					phase = SLT_BIND1;
					return SLT_Q100_TIMING_BIND1;
				}
				//Q200
				phase = SLT_BIND1;
				return SLT_V2_TIMING_BIND1;
			}
			else
			{// Continue to send normal packets
				phase = SLT_BUILD;
				if(sub_protocol == SLT_V1 || sub_protocol == SLT_V2 || sub_protocol == RF_SIM)
					return SLT_FRAME_PERIOD - SLT_TIMING_BUILD - SLT_V1_TIMING_PACKET;
				if(sub_protocol == SLT_V1_4)
					return SLT_FRAME_PERIOD - SLT_TIMING_BUILD - SLT_V1_4_TIMING_PACKET;
				if(sub_protocol == Q100)
					return SLT_FRAME_PERIOD - SLT_TIMING_BUILD - 6*SLT_Q100_TIMING_PACKET;
				if(sub_protocol == MR100)
					return SLT_FRAME_PERIOD - 8*SLT_MR100_TIMING_PACKET;
				//Q200
				return SLT_FRAME_PERIOD - SLT_TIMING_BUILD - 2*SLT_V2_TIMING_PACKET;
			}
		case SLT_BIND1:
			SLT_send_bind_packet();
			phase = SLT_BIND2;
			if(sub_protocol == Q100)
				return SLT_Q100_TIMING_BIND2;
			return SLT_V2_TIMING_BIND2;
		case SLT_BIND2:
			SLT_send_bind_packet();
			phase = SLT_BUILD;
			if(sub_protocol == SLT_V1 || sub_protocol == SLT_V2 || sub_protocol == RF_SIM)
				return SLT_FRAME_PERIOD - SLT_TIMING_BUILD - SLT_V1_TIMING_PACKET - SLT_V1_TIMING_BIND2;
			if(sub_protocol == SLT_V1_4)
				return SLT_FRAME_PERIOD - SLT_TIMING_BUILD - SLT_V1_4_TIMING_PACKET - SLT_V1_TIMING_BIND2;
			if(sub_protocol == Q100)
				return SLT_FRAME_PERIOD - SLT_TIMING_BUILD - 6*SLT_Q100_TIMING_PACKET - SLT_Q100_TIMING_BIND1 - SLT_Q100_TIMING_BIND2;
			if(sub_protocol == MR100)
				return SLT_FRAME_PERIOD - 8*SLT_MR100_TIMING_PACKET - SLT_MR100_TIMING_BIND2;
			//Q200
			return SLT_FRAME_PERIOD - SLT_TIMING_BUILD - 2*SLT_V2_TIMING_PACKET - SLT_V2_TIMING_BIND1 - SLT_V2_TIMING_BIND2;
	}
	return 19000;
}

void SLT_init()
{
	BIND_DONE;	// Not a TX bind protocol
	packet_count = 0;
	packet_sent = 0;
	hopping_frequency_no = 0;

	if(sub_protocol == SLT_V1)
	{
		packet_length = SLT_PAYLOADSIZE_V1;
		rf_ch_num = 1;									//2 packets per frame
		num_ch = 88;									//Bind every 88 frames
		#ifdef MULTI_SYNC
			packet_period = SLT_FRAME_PERIOD;
		#endif
	}
	else if(sub_protocol == SLT_V1_4)
	{
		packet_length = SLT_PAYLOADSIZE_V1_4;
		rf_ch_num = 1;									//2 packets per frame
		num_ch = 88;									//Bind every 88 frames
		#ifdef MULTI_SYNC
			packet_period = SLT_FRAME_PERIOD;
		#endif
		 //Force high part of the ID otherwise the RF frequencies do not match, only tested the 2 last bytes...
		rx_tx_addr[0]=0xF4;
		rx_tx_addr[1]=0x71;
		#ifdef SLT_V1_4_FORCE_ID	// ID taken from TX dumps
			memcpy(rx_tx_addr,"\xF4\x71\x8D\x01",SLT_TXID_SIZE);
		#endif
	}
	else if(sub_protocol == Q100)
	{
		packet_length = SLT_PAYLOADSIZE_Q100;
		rf_ch_num = 6;									//7 packets per frame
		num_ch = 50;									//Bind every 50 frames
		#ifdef MULTI_SYNC
			packet_period = SLT_FRAME_PERIOD;
		#endif
	}
	else if(sub_protocol == MR100)
	{
		packet_length = SLT_PAYLOADSIZE_V2;
		rf_ch_num = 8;									//9 packets per frame
		num_ch = 83;									//Bind every ~83 frames
		#ifdef MULTI_SYNC
			packet_period = SLT_FRAME_PERIOD;
		#endif
	}
	else if(sub_protocol == SLT_V2 || sub_protocol == RF_SIM)
	{
		packet_length = SLT_PAYLOADSIZE_V2;
		rf_ch_num = 1;									//2 packets per frame (same as V1)
		num_ch = 88;									//Bind every 88 frames (same as V1)
		#ifdef MULTI_SYNC
			packet_period = SLT_FRAME_PERIOD;
		#endif
	}
	else //Q200
	{
		packet_length = SLT_PAYLOADSIZE_V2;
		rf_ch_num = 2;									//3 packets per frame
		num_ch = 50;									//Bind every 50 frames
		#ifdef MULTI_SYNC
			packet_period = SLT_FRAME_PERIOD;
		#endif
	}

	if(sub_protocol == Q200 || sub_protocol == Q100)
	{ //Q200/Q100: Force high part of the ID otherwise it won't bind
		rx_tx_addr[0]=0x01;
		rx_tx_addr[1]=0x02;
		#ifdef SLT_Q200_FORCE_ID	// ID taken from TX dumps
			rx_tx_addr[0]=0x01;rx_tx_addr[1]=0x02;rx_tx_addr[2]=0x6A;rx_tx_addr[3]=0x31;
		/*	rx_tx_addr[0]=0x01;rx_tx_addr[1]=0x02;rx_tx_addr[2]=0x0B;rx_tx_addr[3]=0x57;*/
		#endif
	}

	SLT_RF_init();
	SLT_set_freq();

	phase = SLT_BUILD;
}

#endif
//SLT v1_4ch timing
//268363 + 1643 / 15 = 18000
