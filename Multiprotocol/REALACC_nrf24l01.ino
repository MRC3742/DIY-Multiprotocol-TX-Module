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
// Compatible with Realacc R11

#if defined(REALACC_NRF24L01_INO)

#include "iface_xn297.h"

//#define FORCE_REALACC_ORIGINAL_ID

#define REALACC_INITIAL_WAIT		500
#define REALACC_PACKET_PERIOD		2268
#define REALACC_BIND_RF_CHANNEL		80
#define REALACC_BIND_PAYLOAD_SIZE	10
#define REALACC_WLV8TX_BIND_PAYLOAD_SIZE	12
#define REALACC_WLV8TX_RX_PAYLOAD_SIZE	3
#define REALACC_WLV8TX_CMD_B3		0xB3
#define REALACC_WLV8TX_CMD_B4		0xB4
#define REALACC_WLV8TX_CMD_B5		0xB5
#define REALACC_WLV8TX_RX_ADDR_FLAG	0x80
#define REALACC_PAYLOAD_SIZE		13
#define REALACC_BIND_COUNT			50
#define REALACC_RF_NUM_CHANNELS		5
#define REALACC_WLV8TX_STEP_PERIOD	(REALACC_PACKET_PERIOD/2)	// TX/RX bind alternation slot

// Fake-RX probe mode: impersonate the car RX to observe what the real MT12 TX does.
// Useful for debugging bind failures with other people's cars.
// How to use:
//   1. Uncomment #define REALACC_WLV8RX_PROBE below, compile and flash.
//   2. Enable DEBUG_SERIAL on an STM32 board to see the serial output.
//   3. Select REALACC / WLV8TX on your radio as the model protocol.
//   4. Power on the Multiprotocol module FIRST (it starts listening for B1).
//   5. Power on the real MT12 / V8Tx transmitter.
//   6. Watch the serial output for the full bind exchange.
//   7. To run the second test, change PROBE_XOR0/1 to 0x55 / 0x55, reflash, repeat.
//#define REALACC_WLV8RX_PROBE
#define REALACC_WLV8RX_PROBE_XOR0	0xD4	// XOR byte 0 sent in B3/B5 (change to 0x55 for 2nd test)
#define REALACC_WLV8RX_PROBE_XOR1	0xE9	// XOR byte 1 sent in B3/B5 (change to 0x55 for 2nd test)

enum
{
	REALACC_WLV8TX_BIND_TX = 0,
	REALACC_WLV8TX_BIND_RX_SETUP,
	REALACC_WLV8TX_BIND_RX_CHECK,
	REALACC_WLV8TX_DATA,
	// Fake-RX probe phases (only active when REALACC_WLV8RX_PROBE is defined)
	REALACC_WLV8RXPROBE_WAIT_B1,	// =4: RX on "MAIN"/ch80, waiting for B1 from real MT12 TX
	REALACC_WLV8RXPROBE_SEND_B3,	// =5: TX mode – send 2x B3 with probe XOR bytes
	REALACC_WLV8RXPROBE_WAIT_B4,	// =6: RX on "MAIN", waiting for B4 from real MT12 TX
	REALACC_WLV8RXPROBE_SEND_B5,	// =7: TX mode – send 2x B5 with probe XOR bytes
	REALACC_WLV8RXPROBE_DUMP,		// =8: RX on data channels, log all received packets
};

static uint8_t realacc_phase;
static uint8_t realacc_bind_packet[REALACC_BIND_PAYLOAD_SIZE];
static uint8_t realacc_wlv8tx_xor_data[2];
static bool realacc_wlv8tx_got_b3;

#ifdef REALACC_WLV8RX_PROBE
// Busy-wait for the XN297 to finish sending the current packet (max 500 µs).
// Logs the loop count so the caller can tune the timing delay to reach count=0.
static void __attribute__((unused)) REALACC_wlv8rx_wait_packet_sent()
{
	uint16_t start = (uint16_t)micros(), count = 0;
	while ((uint16_t)((uint16_t)micros() - (uint16_t)start) < 500)
	{
		if (XN297_IsPacketSent())
			break;
		count++;
	}
	debug(" wait=%d", count);
}
#endif

static void __attribute__((unused)) REALACC_send_packet()
{
	packet[ 0]= 0xDC;							// DC/D6/DE
	packet[ 1]= convert_channel_8b(AILERON);	// 00..80..FF
	packet[ 2]= convert_channel_8b(ELEVATOR);	// 00..80..FF
	packet[ 3]= convert_channel_8b(THROTTLE);	// 00..FF
	packet[ 4]= convert_channel_8b(RUDDER);		// 00..80..FF
	packet[ 5]= 0x20; 							// Trim
	packet[ 6]= 0x20; 							// Trim
	packet[ 7]= 0x20; 							// Trim
	packet[ 8]= 0x20; 							// Trim
	packet[ 9]= 0x88;							// Change at each power up: C5 A2 77 F0 84 58, fixed for the E017 = 88
	if(sub_protocol == REALACC_WLV8TX)
		packet[10] = 0x0C;						// WL-V8Tx flag value
	else
		packet[10]= 0x04 							// Flag1: R11=04, E017=0C
			| 0x02									//   Rate1=0, Rate2=1, Rate3=2
			| GET_FLAG(CH8_SW, 0x20);				//   Headless
	packet[11]= 0x00 							// Flag2
		| GET_FLAG(CH7_SW, 0x01)				//   Calib
		| GET_FLAG(CH9_SW, 0x20)				//   Return
		| GET_FLAG(CH10_SW,0x80);				//   Throttle cut
	packet[12]= 0x00 							// Flag3
		| GET_FLAG(CH5_SW, 0x01)				//   Flip
		| GET_FLAG(CH11_SW,0x02)				//   Rotating
		| GET_FLAG(CH6_SW, 0x80);				//   Light

	XN297_Hopping(hopping_frequency_no);
	hopping_frequency_no++;
	hopping_frequency_no %= REALACC_RF_NUM_CHANNELS;
	XN297_WriteEnhancedPayload(packet, REALACC_PAYLOAD_SIZE,0);
}

static void __attribute__((unused)) REALACC_send_bind_packet()
{
	if(sub_protocol == REALACC_WLV8TX && realacc_wlv8tx_got_b3)
	{
		packet[0] = REALACC_WLV8TX_CMD_B4;
		packet[1] = realacc_wlv8tx_xor_data[0];
		packet[2] = realacc_wlv8tx_xor_data[1];
		memcpy(&packet[3], realacc_bind_packet, 4);	// Original TX ID before XOR
		memcpy(&packet[7], hopping_frequency, 5);		// RF frequencies
		XN297_WriteEnhancedPayload(packet, REALACC_WLV8TX_BIND_PAYLOAD_SIZE,1);
	}
	else
	{
		packet[0] = 0xB1;							// B0/B1
		memcpy(&packet[1],realacc_bind_packet,4);	// Address
		memcpy(&packet[5],hopping_frequency,5);		// RF frequencies
		XN297_WriteEnhancedPayload(packet, REALACC_BIND_PAYLOAD_SIZE,1);
	}
}

static void __attribute__((unused)) REALACC_initialize_txid()
{
	rx_tx_addr[3] &= 0x3F;
	calc_fh_channels(REALACC_RF_NUM_CHANNELS);
	#ifdef FORCE_REALACC_ORIGINAL_ID
		if(RX_num==0)
		{//TX1
			rx_tx_addr[0]=0x99;
			rx_tx_addr[1]=0x06;
			rx_tx_addr[2]=0x00;
			rx_tx_addr[3]=0x00;	// 00..3F:OK, 40..:NOK
			hopping_frequency[0]=0x55;
			hopping_frequency[1]=0x59;
			hopping_frequency[2]=0x5A;
			hopping_frequency[3]=0x5A;
			hopping_frequency[4]=0x62;
		}
		else
		{//TX2
			rx_tx_addr[0]=0x4F;
			rx_tx_addr[1]=0xB9;
			rx_tx_addr[2]=0xA1;
			rx_tx_addr[3]=0x17;
			hopping_frequency[0]=0x45;
			hopping_frequency[1]=0x38;
			hopping_frequency[2]=0x3C;
			hopping_frequency[3]=0x41;
			hopping_frequency[4]=0x3F;
		}
	#endif
	memcpy(realacc_bind_packet, rx_tx_addr, 4);
	#if 0
		debug("ID: %02X %02X %02X %02X, C: ",rx_tx_addr[0],rx_tx_addr[1],rx_tx_addr[2],rx_tx_addr[3]);
		for(uint8_t i=0; i<REALACC_RF_NUM_CHANNELS; i++)
			debug(" %02X",hopping_frequency[i]);
		debugln("");
	#endif

}

static void __attribute__((unused)) REALACC_RF_init()
{
	XN297_Configure(XN297_CRCEN, XN297_SCRAMBLED, XN297_1M);
	XN297_SetTXAddr((uint8_t*)"MAIN", 4);
	XN297_RFChannel(REALACC_BIND_RF_CHANNEL);	// Set bind channel
}

static void __attribute__((unused)) REALACC_wlv8tx_process_rx()
{
	if(!XN297_IsRX())
		return;
	uint8_t len = XN297_ReadEnhancedPayload(packet_in, REALACC_WLV8TX_BIND_PAYLOAD_SIZE);	// maximum incoming bind payload is B4 (12 bytes)
	if(len != REALACC_WLV8TX_RX_PAYLOAD_SIZE)		// 1 command byte (B3/B5) + 2 XOR bytes
		return;

	if(packet_in[0] == REALACC_WLV8TX_CMD_B3 && !realacc_wlv8tx_got_b3)
	{
		realacc_wlv8tx_got_b3 = true;
		realacc_wlv8tx_xor_data[0] = packet_in[1];
		realacc_wlv8tx_xor_data[1] = packet_in[2];
		rx_tx_addr[2] ^= packet_in[1];
		rx_tx_addr[3] ^= packet_in[2];
	}
	else if(packet_in[0] == REALACC_WLV8TX_CMD_B5 && realacc_wlv8tx_got_b3)
	{
		BIND_DONE;
		XN297_SetTXAddr(rx_tx_addr, 4);
		realacc_phase = REALACC_WLV8TX_DATA;
	}
}

uint16_t REALACC_callback()
{
	#ifdef MULTI_SYNC
		telemetry_set_input_sync(REALACC_PACKET_PERIOD);
	#endif
	XN297_SetPower();
	if(sub_protocol == REALACC_WLV8TX)
	{
#ifdef REALACC_WLV8RX_PROBE
		// ---- Fake-RX probe state machine ----
		// Impersonates the car RX to observe what the real MT12 TX does at each
		// stage.  All bind packets (B1/B3/B4/B5) stay on channel 80 ("MAIN").
		// B3 and B5 are sent from address (TX-ID | 0x80 in byte[3]) because that
		// is the address multi listens on when acting as the TX – i.e. the address
		// the real car RX uses as its TX address.  B4 is sent by the MT12 TX to
		// the "MAIN" address (same as B1).
		uint8_t probe_addr[4];
		if(realacc_phase == REALACC_WLV8RXPROBE_WAIT_B1)
		{
			if(XN297_IsRX())
			{
				uint8_t len = XN297_ReadEnhancedPayload(packet_in, REALACC_BIND_PAYLOAD_SIZE);
				if(len == REALACC_BIND_PAYLOAD_SIZE && (packet_in[0] == 0xB1 || packet_in[0] == 0xB0))
				{
					memcpy(rx_id, &packet_in[1], 4);					// Capture real TX-ID
					memcpy(hopping_frequency, &packet_in[5], 5);		// Capture RF channels
					debug("B1 rx: cmd=%02X ID=%02X %02X %02X %02X CH=", packet_in[0], rx_id[0], rx_id[1], rx_id[2], rx_id[3]);
					for(uint8_t i = 0; i < REALACC_RF_NUM_CHANNELS; i++) debug("%02X ", hopping_frequency[i]);
					debugln("");
					// MT12 TX listens for B3/B5 at (TX-ID with bit7 set in byte[3])
					memcpy(probe_addr, rx_id, 4);
					probe_addr[3] |= REALACC_WLV8TX_RX_ADDR_FLAG;
					XN297_SetTXAddr(probe_addr, 4);
					realacc_phase = REALACC_WLV8RXPROBE_SEND_B3;
				}
			}
			return REALACC_WLV8TX_STEP_PERIOD;
		}

		if(realacc_phase == REALACC_WLV8RXPROBE_SEND_B3)
		{
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(TX_EN);
			for(uint8_t i = 0; i < 2; i++)
			{
				packet[0] = REALACC_WLV8TX_CMD_B3;
				packet[1] = REALACC_WLV8RX_PROBE_XOR0;
				packet[2] = REALACC_WLV8RX_PROBE_XOR1;
				XN297_WriteEnhancedPayload(packet, REALACC_WLV8TX_RX_PAYLOAD_SIZE, 1);
				debug("TX B3#%d: %02X %02X %02X", i + 1, packet[0], packet[1], packet[2]);
				REALACC_wlv8rx_wait_packet_sent();
				debugln("");
			}
			// Switch to RX and wait for B4 (MT12 TX sends B4 addressed to "MAIN")
			XN297_SetRXAddr((uint8_t *)"MAIN", REALACC_WLV8TX_BIND_PAYLOAD_SIZE);
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(RX_EN);
			bind_counter = 100;								// ~113 ms timeout before giving up
			realacc_phase = REALACC_WLV8RXPROBE_WAIT_B4;
			return REALACC_WLV8TX_STEP_PERIOD;
		}

		if(realacc_phase == REALACC_WLV8RXPROBE_WAIT_B4)
		{
			if(XN297_IsRX())
			{
				uint8_t len = XN297_ReadEnhancedPayload(packet_in, REALACC_WLV8TX_BIND_PAYLOAD_SIZE);
				if(len == REALACC_WLV8TX_BIND_PAYLOAD_SIZE && packet_in[0] == REALACC_WLV8TX_CMD_B4)
				{
					debug("B4 rx:");
					for(uint8_t i = 0; i < REALACC_WLV8TX_BIND_PAYLOAD_SIZE; i++) debug(" %02X", packet_in[i]);
					debugln("");
					// Restore TX addr for sending B5
					memcpy(probe_addr, rx_id, 4);
					probe_addr[3] |= REALACC_WLV8TX_RX_ADDR_FLAG;
					XN297_SetTXAddr(probe_addr, 4);
					realacc_phase = REALACC_WLV8RXPROBE_SEND_B5;
					return REALACC_WLV8TX_STEP_PERIOD;
				}
			}
			if(--bind_counter == 0)
			{
				debugln("B4 timeout – restarting from B1");
				XN297_SetRXAddr((uint8_t *)"MAIN", REALACC_BIND_PAYLOAD_SIZE);
				XN297_SetTxRxMode(TXRX_OFF);
				XN297_SetTxRxMode(RX_EN);
				realacc_phase = REALACC_WLV8RXPROBE_WAIT_B1;
			}
			return REALACC_WLV8TX_STEP_PERIOD;
		}

		if(realacc_phase == REALACC_WLV8RXPROBE_SEND_B5)
		{
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(TX_EN);
			for(uint8_t i = 0; i < 2; i++)
			{
				packet[0] = REALACC_WLV8TX_CMD_B5;
				packet[1] = REALACC_WLV8RX_PROBE_XOR0;
				packet[2] = REALACC_WLV8RX_PROBE_XOR1;
				XN297_WriteEnhancedPayload(packet, REALACC_WLV8TX_RX_PAYLOAD_SIZE, 1);
				debug("TX B5#%d: %02X %02X %02X", i + 1, packet[0], packet[1], packet[2]);
				REALACC_wlv8rx_wait_packet_sent();
				debugln("");
			}
			// Dump mode: hop the data channels and log whatever the MT12 TX sends
			XN297_SetRXAddr(rx_id, REALACC_PAYLOAD_SIZE);
			hopping_frequency_no = 0;
			XN297_Hopping(hopping_frequency_no);
			XN297_SetTxRxMode(TXRX_OFF);
			XN297_SetTxRxMode(RX_EN);
			bind_counter = 10;								// hop channel every 10 callbacks
			debugln("Dump: watching data packets on hopping channels...");
			realacc_phase = REALACC_WLV8RXPROBE_DUMP;
			return REALACC_PACKET_PERIOD;
		}

		if(realacc_phase == REALACC_WLV8RXPROBE_DUMP)
		{
			if(XN297_IsRX())
			{
				uint8_t len = XN297_ReadEnhancedPayload(packet_in, REALACC_PAYLOAD_SIZE);
				debug("ch%02X:", hopping_frequency[hopping_frequency_no]);
				for(uint8_t i = 0; i < len; i++) debug(" %02X", packet_in[i]);
				debugln("");
				XN297_SetTxRxMode(RX_EN);					// Re-arm receiver
			}
			if(--bind_counter == 0)
			{
				bind_counter = 10;
				hopping_frequency_no = (hopping_frequency_no + 1) % REALACC_RF_NUM_CHANNELS;
				XN297_Hopping(hopping_frequency_no);
			}
			return REALACC_PACKET_PERIOD;
		}
#endif // REALACC_WLV8RX_PROBE

		if(realacc_phase == REALACC_WLV8TX_DATA)
		{
			XN297_SetTxRxMode(TX_EN);
			REALACC_send_packet();
			return REALACC_PACKET_PERIOD;
		}

		if(realacc_phase == REALACC_WLV8TX_BIND_RX_SETUP)
		{
			XN297_SetTxRxMode(RX_EN);
			realacc_phase = REALACC_WLV8TX_BIND_RX_CHECK;
			return REALACC_WLV8TX_STEP_PERIOD;
		}

		if(realacc_phase == REALACC_WLV8TX_BIND_RX_CHECK)
		{
			REALACC_wlv8tx_process_rx();
			if(realacc_phase == REALACC_WLV8TX_DATA)
				return REALACC_PACKET_PERIOD;
			realacc_phase = REALACC_WLV8TX_BIND_TX;
			return REALACC_WLV8TX_STEP_PERIOD;
		}

		XN297_SetTxRxMode(TX_EN);
		REALACC_send_bind_packet();
		realacc_phase = REALACC_WLV8TX_BIND_RX_SETUP;
		return REALACC_WLV8TX_STEP_PERIOD;
	}

	XN297_SetTxRxMode(TX_EN);
	if(IS_BIND_IN_PROGRESS)
	{
		REALACC_send_bind_packet();
		if(--bind_counter==0)
		{
			BIND_DONE;
			XN297_SetTXAddr(rx_tx_addr, 4);
		}
	}
	else
		REALACC_send_packet();
	return REALACC_PACKET_PERIOD;
}

void REALACC_init()
{
	uint8_t bind_rx_addr[4];
	BIND_IN_PROGRESS;	// autobind protocol
	REALACC_initialize_txid();
	REALACC_RF_init();
	if(sub_protocol == REALACC_WLV8TX)
	{
		realacc_wlv8tx_xor_data[0] = 0;
		realacc_wlv8tx_xor_data[1] = 0;
		realacc_wlv8tx_got_b3 = false;
#ifdef REALACC_WLV8RX_PROBE
		// Probe mode: start as a passive receiver waiting for B1 from the real MT12 TX.
		// The radio is already configured for ch 80 and "MAIN" address by REALACC_RF_init.
		XN297_SetRXAddr((uint8_t *)"MAIN", REALACC_BIND_PAYLOAD_SIZE);
		XN297_SetTxRxMode(RX_EN);
		realacc_phase = REALACC_WLV8RXPROBE_WAIT_B1;
		debugln("WLV8RX Probe: XOR=%02X %02X – power on the MT12 TX now", REALACC_WLV8RX_PROBE_XOR0, REALACC_WLV8RX_PROBE_XOR1);
#else
		realacc_phase = REALACC_WLV8TX_BIND_TX;
		memcpy(bind_rx_addr, realacc_bind_packet, 4);
		bind_rx_addr[3] |= REALACC_WLV8TX_RX_ADDR_FLAG;	// WL-V8Tx listens on TX-ID with bit7 set in 4th byte (index 3)
		XN297_SetRXAddr(bind_rx_addr, REALACC_WLV8TX_BIND_PAYLOAD_SIZE);
#endif
	}
	else
		bind_counter=REALACC_BIND_COUNT;
	hopping_frequency_no=0;
}

#endif

// XN297 speed 1Mb, scrambled, enhanced
// Bind
//   Address = 4D 41 49 4E = 'MAIN'
//   Channel = 80 (most likely from dump)
//   TX1
//   ---
//     P(10) = B1 99 06 00 00 55 59 5A 5A 62
//     Bx indicates bind packet, why x=1?
//     99 06 00 00 = ID = address of normal packets
//     55 59 5A 5A 62 = 85, 89, 90, 90, 98 = RF channels to be used (kind of match previous dumps)
//   TX2
//   ---
//     P(10) = B0 4F B9 A1 17 45 38 3C 41 3F
//     Bx indicates bind packet, why x=0?
//     4F B9 A1 17 = ID = address of normal packets
//     45 38 3C 41 3F  = 69, 56, 60, 65, 63 = RF channels to be used
// Normal
//   TX1
//   ---
//     Address = 99 06 00 00
//     Channels = 84, 89, 90, 90, 98 (guess from bind)
//     P(13)= DC 80 80 32 80 20 20 20 20 58 04 00 00
//     Dx = normal packet, why C ?
//     80 80 32 80 : AETR 00..80..FF
//     20 20 20 20 : Trims
//     58 : changing every time the TX restart
//     04 : |0x20=headless, |0x01=rate2, |0x02=rate3
//     00 : |0x01=calib, |0x20=return, |0x80=unknown
//     00 : |0x80=light, |0x01=flip
//   TX2
//   ---
//     Address = 4F B9 A1 17
//     P(13)= D6/DE 80 80 80 80 20 20 20 20 88 0C 00 00
//     Dx = normal packet, why 6/E ?
//     80 80 32 80 : AETR 00..80..FF
//     20 20 20 20 : Trims
//     88 : not changing unknown
//     0C : |0x20=headless, |0x01=rate2, |0x02=rate3
//     00 : |0x01=calib, |0x20=return, |0x80=unknown
//     00 : |0x80=light, |0x01=flip, |0x02=Rotating
