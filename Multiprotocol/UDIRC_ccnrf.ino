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
// Binding / normal packet flow (from captures):
//
// BIND phase (hop all 4 channels, bind address 01:03:05:07:09):
//   TX sends: 01 <TX_ID[0..4]> 00 00 00 64 60 6C 00 00 <csum>   (packet[0]=0x01)
//   RX replies: 01 F8 00 00 30 00 00 00 00 00 00 F8 00 00 30     (stores F8/30 bytes)
//   TX acks:  02 F8 00 00 30 00 00 00 00 64 60 6C 00 00 <csum>   (packet[0]=0x02)
//   RX echoes: 02 F8 00 00 30 ...                                 (bind confirmed)
//
// NORMAL phase (stay on one hop channel, TX address):
//   TX sends: 08 ST TH CH3 CH4 F8 00 00 30 GYRO TRIM DR FLAGS 00 <csum>
//   RX telemetry: 10 00 00 00 30 00 00 00 00 00 00 F8 00 00 30

#if defined(UDIRC_CCNRF_INO)

#include "iface_xn297.h"

#define FORCE_UDIRC_ORIGINAL_ID

#define UDIRC_PAYLOAD_SIZE		15
#define UDIRC_RF_NUM_CHANNELS	4
#define UDIRC_PACKET_PERIOD		21000
#define UDIRC_BIND_COUNT		2000
#define UDIRC_P1_P2_TIME		5000
#define UDIRC_WRITE_TIME		1500

// Bind sub-states
enum {
	UDIRC_BIND_TX1=0,	// Send bind packet (0x01)
	UDIRC_BIND_TX2,		// Resend bind packet
	UDIRC_BIND_RX,		// Listen for RX 0x01 reply, capture F8/30 bytes
	UDIRC_ACK_TX1,		// Send ack packet (0x02) with F8/30
	UDIRC_ACK_TX2,		// Resend ack packet
	UDIRC_ACK_RX,		// Listen for RX 0x02 echo, confirm bind
	UDIRC_DATA1,		// Normal: send control packet (0x08)
	UDIRC_DATA2,		// Normal: resend control packet
	UDIRC_DATA3,		// Normal: wait for TX complete, switch to RX
};

// Bytes returned by RX during bind, inserted into normal packets
static uint8_t udirc_rx_byte5;	// e.g. 0xF8
static uint8_t udirc_rx_byte8;	// e.g. 0x30

static void __attribute__((unused)) UDIRC_build_checksum()
{
	// Checksum = sum of bytes [0..13], stored in byte [14]
	uint8_t sum = 0;
	for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE - 1; i++)
		sum += packet[i];
	packet[14] = sum;
}

static void __attribute__((unused)) UDIRC_send_bind_packet()
{
	// Hop channels during bind
	XN297_Hopping(hopping_frequency_no);
	debugln("UDIRC bind hop ch=%d", hopping_frequency[hopping_frequency_no]);
	hopping_frequency_no = (hopping_frequency_no + 1) & 3;

	memset(packet, 0x00, UDIRC_PAYLOAD_SIZE);
	packet[0] = 0x01;
	memcpy(&packet[1], rx_tx_addr, 5);
	// Known constant bytes seen in original TX captures
	packet[9]  = 0x64;
	packet[10] = 0x60;
	packet[11] = 0x6C;
	UDIRC_build_checksum();

	XN297_SetFreqOffset();
	XN297_SetPower();
	XN297_SetTxRxMode(TX_EN);
	XN297_WriteEnhancedPayload(packet, UDIRC_PAYLOAD_SIZE, false);
	debug("UDIRC TX bind: ");
	#ifdef DEBUG_SERIAL
		for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE; i++)
			debug("%02X ", packet[i]);
		debugln();
	#endif
}

static void __attribute__((unused)) UDIRC_send_ack_packet()
{
	// ACK packet (0x02): echo back F8/30 bytes from RX bind reply
	memset(packet, 0x00, UDIRC_PAYLOAD_SIZE);
	packet[0] = 0x02;
	packet[1] = udirc_rx_byte5;	// F8
	packet[2] = 0x00;
	packet[3] = 0x00;
	packet[4] = udirc_rx_byte8;	// 30
	packet[9]  = 0x64;
	packet[10] = 0x60;
	packet[11] = 0x6C;
	UDIRC_build_checksum();

	XN297_SetFreqOffset();
	XN297_SetPower();
	XN297_SetTxRxMode(TX_EN);
	XN297_WriteEnhancedPayload(packet, UDIRC_PAYLOAD_SIZE, false);
	debug("UDIRC TX ack: ");
	#ifdef DEBUG_SERIAL
		for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE; i++)
			debug("%02X ", packet[i]);
		debugln();
	#endif
}

static void __attribute__((unused)) UDIRC_send_normal_packet()
{
	memset(packet, 0x00, UDIRC_PAYLOAD_SIZE);
	packet[0] = 0x08;
	// Channels SG-16xx/SG-1205: ST/TH/CH3/CH4/F8/00/00/30/GYRO/ST_TRIM/ST_DR/FLAGS
	// Channels EAT15           : ST/TH/RATE/LIGHT/F8/00/00/30/GYRO/ST_TRIM/ST_DR/FLAGS
	packet[1] = convert_channel_16b_limit(0, 0, 200);	// Steering
	packet[2] = convert_channel_16b_limit(1, 0, 200);	// Throttle
	packet[3] = convert_channel_16b_limit(2, 0, 200);	// CH3
	packet[4] = convert_channel_16b_limit(3, 0, 200);	// CH4
	packet[5] = udirc_rx_byte5;							// F8 from bind (RX-assigned)
	packet[6] = 0x00;
	packet[7] = 0x00;
	packet[8] = udirc_rx_byte8;							// 30 from bind (RX-assigned)
	packet[9]  = convert_channel_16b_limit(8, 0, 200);	// Gyro
	packet[10] = convert_channel_16b_limit(9, 0, 200);	// ST Trim
	packet[11] = convert_channel_16b_limit(10, 0, 200);// ST DR
	packet[12] = GET_FLAG(CH12_SW, 0x40)				// TH.REV
				|GET_FLAG(CH13_SW, 0x80);				// ST.REV
	packet[13] = 0x00;									// Unknown, future flags
	UDIRC_build_checksum();

	XN297_SetFreqOffset();
	XN297_SetPower();
	XN297_SetTxRxMode(TX_EN);
	XN297_WriteEnhancedPayload(packet, UDIRC_PAYLOAD_SIZE, false);
	#ifdef DEBUG_SERIAL
		debug("UDIRC TX: ");
		for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE; i++)
			debug("%02X ", packet[i]);
		debugln();
	#endif
}

static void __attribute__((unused)) UDIRC_initialize_txid()
{
	#ifdef FORCE_UDIRC_ORIGINAL_ID
		if(RX_num)
		{
			rx_tx_addr[0] = 0xD0;
			rx_tx_addr[1] = 0x06;
			rx_tx_addr[2] = 0x00;
			rx_tx_addr[3] = 0x00;
			rx_tx_addr[4] = 0x81;
		}
		else
		{
			// Captured TX ID: C3 E4 04 00 81
			rx_tx_addr[0] = 0xC3;
			rx_tx_addr[1] = 0xE4;
			rx_tx_addr[2] = 0x04;
			rx_tx_addr[3] = 0x00;
			rx_tx_addr[4] = 0x81;
		}
		// Captured hopping channels: 45, 52, 59, 67
		hopping_frequency[0] = 45;
		hopping_frequency[1] = 52;
		hopping_frequency[2] = 59;
		hopping_frequency[3] = 67;
	#endif
}

static void __attribute__((unused)) UDIRC_RF_init()
{
	XN297_Configure(XN297_CRCEN, XN297_SCRAMBLED, XN297_250K);
	// Start with bind address
	XN297_SetTXAddr((uint8_t*)"\x01\x03\x05\x07\x09", 5);
	XN297_SetRXAddr((uint8_t*)"\x01\x03\x05\x07\x09", UDIRC_PAYLOAD_SIZE);
	XN297_HoppingCalib(UDIRC_RF_NUM_CHANNELS);
}

uint16_t UDIRC_callback()
{
	bool rx_ready;
	uint8_t rx_len;

	switch(phase)
	{
		// --- BIND phase ---
		case UDIRC_BIND_TX1:
			XN297_SetTxRxMode(TXRX_OFF);
			#ifdef MULTI_SYNC
				telemetry_set_input_sync(UDIRC_PACKET_PERIOD);
			#endif
			if(bind_counter)
			{
				UDIRC_send_bind_packet();
				bind_counter--;
				phase = UDIRC_BIND_TX2;
			}
			else
			{
				// Bind timed out without any RX reply - restart bind counter
				debugln("UDIRC bind timeout, restarting");
				bind_counter = UDIRC_BIND_COUNT;
				phase = UDIRC_BIND_TX2;
			}
			return UDIRC_P1_P2_TIME;

		case UDIRC_BIND_TX2:
			XN297_ReSendPayload();
			phase = UDIRC_BIND_RX;
			return UDIRC_WRITE_TIME;

		case UDIRC_BIND_RX:
			// Wait for TX complete, then listen for RX bind reply (0x01)
			while(XN297_IsPacketSent() == false);
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(RX_EN);
			phase = UDIRC_BIND_TX1;		// Default: loop bind packets
			rx_ready = XN297_IsRX();
			if(rx_ready)
			{
				rx_len = XN297_ReadEnhancedPayload(packet_in, UDIRC_PAYLOAD_SIZE);
				debug("UDIRC RX bind(%d): ", rx_len);
				#ifdef DEBUG_SERIAL
					for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE; i++)
						debug("%02X ", packet_in[i]);
					debugln();
				#endif
				if(rx_len == UDIRC_PAYLOAD_SIZE && packet_in[0] == 0x01)
				{
					// Capture the RX-assigned F8/30 bytes
					udirc_rx_byte5 = packet_in[1];		// e.g. 0xF8
					udirc_rx_byte8 = packet_in[4];		// e.g. 0x30
					debugln("UDIRC captured rx_byte5=%02X rx_byte8=%02X", udirc_rx_byte5, udirc_rx_byte8);
					phase = UDIRC_ACK_TX1;
				}
			}
			return UDIRC_PACKET_PERIOD - UDIRC_P1_P2_TIME - UDIRC_WRITE_TIME;

		case UDIRC_ACK_TX1:
			XN297_SetTxRxMode(TXRX_OFF);
			UDIRC_send_ack_packet();
			phase = UDIRC_ACK_TX2;
			return UDIRC_P1_P2_TIME;

		case UDIRC_ACK_TX2:
			XN297_ReSendPayload();
			phase = UDIRC_ACK_RX;
			return UDIRC_WRITE_TIME;

		case UDIRC_ACK_RX:
			// Wait for TX complete, then listen for RX 0x02 echo confirming bind
			while(XN297_IsPacketSent() == false);
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(RX_EN);
			phase = UDIRC_ACK_TX1;		// Default: resend ack if no echo
			rx_ready = XN297_IsRX();
			if(rx_ready)
			{
				rx_len = XN297_ReadEnhancedPayload(packet_in, UDIRC_PAYLOAD_SIZE);
				debug("UDIRC RX ack(%d): ", rx_len);
				#ifdef DEBUG_SERIAL
					for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE; i++)
						debug("%02X ", packet_in[i]);
					debugln();
				#endif
				if(rx_len == UDIRC_PAYLOAD_SIZE && packet_in[0] == 0x02)
				{
					// Bind confirmed - switch to normal TX address and normal channel
					debugln("UDIRC bind complete, switching to normal");
					BIND_DONE;
					bind_counter = 0;
					XN297_SetTXAddr(rx_tx_addr, 5);
					XN297_SetRXAddr(rx_tx_addr, UDIRC_PAYLOAD_SIZE);
					// Stay on current hop channel (first channel from bind sequence)
					XN297_Hopping(0);
					phase = UDIRC_DATA1;
				}
			}
			return UDIRC_PACKET_PERIOD - UDIRC_P1_P2_TIME - UDIRC_WRITE_TIME;

		// --- NORMAL phase ---
		case UDIRC_DATA1:
			rx_ready = XN297_IsRX();
			XN297_SetTxRxMode(TXRX_OFF);
			#ifdef MULTI_SYNC
				telemetry_set_input_sync(UDIRC_PACKET_PERIOD);
			#endif
			if(rx_ready)
			{
				rx_len = XN297_ReadEnhancedPayload(packet_in, UDIRC_PAYLOAD_SIZE);
				debug("UDIRC RX telem(%d): ", rx_len);
				#ifdef DEBUG_SERIAL
					for(uint8_t i = 0; i < UDIRC_PAYLOAD_SIZE; i++)
						debug("%02X ", packet_in[i]);
					debugln();
				#endif
				// Telemetry packet starts with 0x10
				if(rx_len == UDIRC_PAYLOAD_SIZE && packet_in[0] == 0x10)
				{
					// RX-assigned bytes appear at [11] and [14] in telemetry packets;
					// update only if the captured bind values are still zero (pre-bind
					// reconnect), otherwise trust the bind-time values.
					if(!udirc_rx_byte5)
						udirc_rx_byte5 = packet_in[11];
					if(!udirc_rx_byte8)
						udirc_rx_byte8 = packet_in[14];
				}
			}
			UDIRC_send_normal_packet();
			phase = UDIRC_DATA2;
			return UDIRC_P1_P2_TIME;

		case UDIRC_DATA2:
			// Resend normal packet
			XN297_ReSendPayload();
			phase = UDIRC_DATA3;
			return UDIRC_WRITE_TIME;

		case UDIRC_DATA3:
			// Wait for TX complete, switch to RX for telemetry window
			while(XN297_IsPacketSent() == false);
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(RX_EN);
			phase = UDIRC_DATA1;
			return UDIRC_PACKET_PERIOD - UDIRC_P1_P2_TIME - UDIRC_WRITE_TIME;

		default:
			break;
	}
	return 0;
}

void UDIRC_init()
{
	UDIRC_initialize_txid();
	UDIRC_RF_init();

	udirc_rx_byte5 = 0x00;
	udirc_rx_byte8 = 0x00;
	hopping_frequency_no = 0;

	if(IS_BIND_IN_PROGRESS)
	{
		bind_counter = UDIRC_BIND_COUNT;
		phase = UDIRC_BIND_TX1;
	}
	else
	{
		// Already bound - use last known F8/30 bytes (0x00 until telemetry updates them)
		bind_counter = 0;
		XN297_SetTXAddr(rx_tx_addr, 5);
		XN297_SetRXAddr(rx_tx_addr, UDIRC_PAYLOAD_SIZE);
		XN297_Hopping(0);
		phase = UDIRC_DATA1;
	}
}

#endif
