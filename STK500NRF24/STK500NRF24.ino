#include <SPI.h>
#include "src/stk500.h"
#include "nRF24L01.h"
#include <string.h>

static const uint16_t VERSION = 0x900;

struct nrfPacket
{
	nrfPacket()
	{
		command = 0x9D; // CPU_CCP_SPM_gc
		numpackets = 0x00;
		addresslo = 0x00;
		addresshi = 0x34; // INTERNAL_SRAM_START
	}
	uint8_t command;
	uint8_t numpackets;
	uint8_t addresslo;
	uint8_t addresshi;
};
nrfPacket packet;
nrfPacket syncPacket;

uint8_t nrf24_status();
uint8_t nrf24_command(uint8_t cmd, uint8_t data = NOP);
uint8_t nrf24_begin(uint8_t cmd);
void nrf24_end();
void nrf24_set_tx_address(const uint8_t address[3]);
inline void nrf24_write_register(uint8_t reg, uint8_t data) { nrf24_command(reg | W_REGISTER, data); }
inline uint8_t nrf24_read_register(uint8_t reg) { return nrf24_command(reg, 0); }
inline uint8_t nrf24_status()
{
	return nrf24_read_register(RF_STATUS);
}
uint8_t nrf24_command(uint8_t cmd, uint8_t data)
{
	nrf24_begin(cmd);
	data = SPI.transfer(data);
	nrf24_end();
	return data;
}
uint8_t nrf24_begin(uint8_t cmd)
{
	digitalWrite(PIN_PB0, LOW);
	delayMicroseconds(5);
	return SPI.transfer(cmd);
}
inline void nrf24_end()
{
	digitalWrite(PIN_PB0, HIGH);
	delayMicroseconds(5);
}
inline bool nrf24_rx_available()
{
	return !(nrf24_read_register(FIFO_STATUS) & _BV(RX_EMPTY));
}
void nrf24_set_address(uint8_t reg, const uint8_t address[3])
{
	nrf24_begin(W_REGISTER | reg);
	SPI.transfer(address[0]);
	SPI.transfer(address[1]);
	SPI.transfer(address[2]);
	nrf24_end();
}
void nrf24_set_tx_address(const uint8_t address[3])
{
	nrf24_set_address(TX_ADDR, address);
	nrf24_set_address(RX_ADDR_P0, address);
}
void nrf24_init()
{
	SPI.begin();
	digitalWrite(PIN_PB0, HIGH);
	delay(5);
	nrf24_write_register(CONFIG, 0);
	nrf24_write_register(EN_AA, 0x3F);
	nrf24_write_register(SETUP_AW, 1);
	nrf24_write_register(RF_CH, 76);
	nrf24_write_register(SETUP_RETR, 0x7F);
	nrf24_write_register(RF_SETUP, (1 << RF_PWR_LOW) | (1 << RF_PWR_HIGH) | (1 << RF_DR_HIGH));
	nrf24_write_register(FEATURE, (1 << EN_DPL));
	nrf24_write_register(DYNPD, 1);
	nrf24_write_register(EN_RXADDR, 1);
	nrf24_command(FLUSH_TX);
	nrf24_command(FLUSH_RX);
}
void nrf24_begin_tx()
{
	nrf24_write_register(CONFIG, 0);
	nrf24_command(FLUSH_RX);
	nrf24_command(FLUSH_TX);
	nrf24_write_register(RF_STATUS, _BV(MAX_RT));
	nrf24_write_register(CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP));
	delay(5);
}
void nrf24_begin_rx()
{
	nrf24_write_register(CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP) | (1 << PRIM_RX));
}
bool nrf24_tx(const void* data, uint8_t len)
{
	uint8_t status;
	const uint8_t* addr = (const uint8_t*) data;
	for (;;)
	{
		status = nrf24_status();
		if (!(status & _BV(TX_FULL)))
			break;
		if (status & _BV(MAX_RT))
		{
			nrf24_write_register(RF_STATUS, status);
			nrf24_command(FLUSH_TX, 0);
			return false;
		}
	}
	nrf24_begin(W_TX_PAYLOAD);
	while (len--)
		SPI.transfer(*addr++);
	nrf24_end();
	return true;
}
bool nrf24_tx_end()
{
	for (;;)
	{
		if (nrf24_read_register(FIFO_STATUS) & _BV(TX_EMPTY))
			return true;
		uint8_t status = nrf24_status();
		if (status & _BV(MAX_RT))
		{
			nrf24_write_register(RF_STATUS, status);
			nrf24_command(FLUSH_TX, 0);
			return false;
		}
	}
}

uint8_t progAddress [] = { 'P','0','1' };
uint8_t uartAddress [] = { 'U','0','1' };

uint16_t startT;
int getch()
{
	while (!Serial.available())
	{
		uint16_t t = millis();
		if (t - startT > 1000)
			break;
		KeepAlive(t);
	}
	return Serial.read();
}
inline void putch(int ch)
{
	while (!Serial.availableForWrite())
	{
		uint16_t t = millis();
		KeepAlive(t);
	}
	Serial.write(ch);
}

bool verifySpace()
{
	bool insync = getch() == CRC_EOP;
	putch(insync ? STK_INSYNC : STK_NOSYNC);
	return insync;
}

uint8_t serialbuf[32];
uint8_t serialbufpos = 0;
uint8_t stk500mode;
uint16_t lastSendTime = 0;

enum eMode
{
	MODE_UART,
	MODE_STK500,
	MODE_CONFIGURE,
};
eMode gMode = MODE_UART;

void OpenUart()
{
	gMode = MODE_UART;
	nrf24_write_register(CONFIG, 0);
	nrf24_set_tx_address(uartAddress);
	nrf24_begin_rx();
	serialbufpos = 0;
}

bool SendSyncPacket()
{
	return nrf24_tx(&syncPacket, sizeof(syncPacket)) && nrf24_tx_end();
}
void KeepAlive(uint16_t t)
{
	static uint16_t lastSyncTime = 0;
	if (t - lastSyncTime > 250)
	{
		lastSyncTime = t;
		SendSyncPacket();
	}
}

uint8_t OpenStk500()
{
	nrf24_set_tx_address(progAddress);
	nrf24_begin_tx();

	// wait for 4 sync packets to be received.  Up to 3 can fit in
	// the receivers FIFO so only with 4 can we be sure the bootloader
	// has actually started pulling them out of the FIFO.
	uint8_t retries = 0;
	uint8_t successes = 0;
	for(;;)
	{
		if (SendSyncPacket())
		{
			if (++successes == 4)
				break;
		}
		else
		{
			if (++retries == 30)
				break;
			delay(50);
		}
	}
	return successes;
}

bool RespondToStk500Sync()
{
	serialbufpos = 0;
	putch(STK_INSYNC);
	putch(STK_OK);
	putch(STK_INSYNC);
	if (OpenStk500() == 4)
	{
		putch(STK_OK);
		Serial.flush();
		lastSendTime = millis();
		gMode = MODE_STK500;
	}
	else
	{
		putch(STK_FAILED);
		Serial.flush();
		OpenUart();
	}
}

void PrintAddresses()
{
	uint8_t a0 = uartAddress[1];
	uint8_t a1 = uartAddress[2];
	Serial.printf("UART addr = %02x%02x%02x  Programming addr = %02x%02x%02x\n", 'U', a0, a1, 'P', a0, a1);
}

void OpenConfig()
{
	gMode = MODE_CONFIGURE;
	Serial.write("\nConfigure STK500-nRF24L01+ interface\n");
	PrintAddresses();
	Serial.write("\n addr <xyz>   - set address of target device\n");
	Serial.write(" setid <xyz>  - reprogram target device's listen address\n");
	Serial.write(" r            - reset target device\n>");
	serialbufpos = 0;
	while (Serial.read() >= 0);
}

uint8_t MatchSerialCommand(const char* seq, uint8_t len)
{
	if (len > serialbufpos)
		len = serialbufpos;
	for (; len > 0; --len)
		if (memcmp(&serialbuf[serialbufpos - len], seq, len) == 0)
			break;
	return len;
}

char inbuf[256];
uint8_t inbufpos = 0;

void HandleUart()
{
	uint16_t t = millis();
	uint8_t stk500match = 0;

	while (inbufpos < 256 - 32 && nrf24_rx_available())
	{
		uint8_t size = nrf24_command(R_RX_PL_WID);
		nrf24_begin(R_RX_PAYLOAD);
		while (size--)
			inbuf[inbufpos++] = SPI.transfer(0);
		nrf24_end();
	}

	if (serialbufpos < 32 && Serial.available())
		serialbuf[serialbufpos++] = Serial.read();

	stk500match = MatchSerialCommand("0 0 ", 4);
	if (stk500match == 4)
	{
		RespondToStk500Sync();
		return;
	}
	if (!stk500match) // don't talk back on serial if STK500 is being initiated
	{
		if (inbufpos > 0)
		{			
			Serial.write(inbuf, inbufpos);
			//Serial.flush();
			inbufpos = 0;
		}
	}

	uint8_t idcmd = MatchSerialCommand("*cfg\n", 4);
	if (idcmd == 4)
	{
		OpenConfig();
		return;
	}

	if (serialbufpos == 32 || (serialbufpos > 0 && t - lastSendTime > 100 && !stk500match && !idcmd))
	{
		nrf24_begin_tx();
		nrf24_tx(serialbuf, serialbufpos);
		nrf24_tx_end();
		nrf24_begin_rx();
		serialbufpos = 0;
	}
	if (serialbufpos == 0)
	{
		lastSendTime = t;
	}
}

void HandleStk500()
{
	if (!Serial.available())
	{
		uint16_t t = millis();
		// time out after 5 seconds
		if (t - lastSendTime > 5000)
			OpenUart();
		else 
			KeepAlive(t);
		return;
	}

	uint8_t tmp[32];
	bool failed = false;
	bool finished = false;

	startT = millis();

	switch (Serial.read())
	{
	case STK_GET_SYNC:
	{
		if (!verifySpace() || !SendSyncPacket())
			failed = true;
		break;
	}
	case STK_GET_PARAMETER:
	{
		uint8_t which = getch();
		if (!verifySpace())
			return;

		/*
		 * Send optiboot version as "SW version"
		 * Note that the references to memory are optimized away.
		 */
		if (which == STK_SW_MINOR)
		{
			putch(VERSION & 0xFF);
		}
		else if (which == STK_SW_MAJOR)
		{
			putch(VERSION >> 8);
		}
		else
		{
			/*
				* GET PARAMETER returns a generic 0x03 reply for
				* other parameters - enough to keep Avrdude happy
				*/
			putch(0x03);
		}
		break;
	}
	case STK_SET_DEVICE:
	{
		// SET DEVICE is ignored
		for (uint8_t i = 0; i < 20; ++i)
			getch();
		if (!verifySpace())
			return;
		break;
	}
	case STK_SET_DEVICE_EXT:
	{
		for (uint8_t i = 0; i < 5; ++i)
			getch();
		if (!verifySpace())
			return;
		break;
	}
	case STK_LOAD_ADDRESS:
	{
		packet.addresslo = getch();
		packet.addresshi = getch();
		if (!verifySpace())
			return;
		break;
	}
	case STK_UNIVERSAL:
	{
#ifndef RAMPZ
		// UNIVERSAL command is ignored
		for (uint8_t i = 0; i < 4; ++i)
			getch();
		if (!verifySpace())
			return;
		putch('\0');
#endif
		break;
	}
	/* Write memory, length is big endian and is in bytes */
	case STK_PROG_PAGE:
	{
		// PROGRAM PAGE - any kind of page!
		int16_t length = getch() << 8;
		length |= getch();
		uint8_t desttype = getch();

		failed = true;
		if (length <= 64)
		{
			char packetbuf[64];
			for (uint8_t i = 0; i < length; ++i)
				packetbuf[i] = getch();

			if (desttype == 'F')
				packet.addresshi += 0x80; // progmem
			else if (desttype == 'E')
				packet.addresshi += 0x14; // eeprom
			else if (desttype == 'U')
				packet.addresshi += 0x13; // userrow
			packet.numpackets = (length + 31) / 32;
			if (nrf24_tx(&packet, sizeof(packet)))
			{
				failed = false;
				for (uint8_t i = 0; i < length; i += 32)
				{
					if (!nrf24_tx(&packetbuf[i], min(32, length - i)))
					{
						failed = true;
						break;
					}
				}
				if (!nrf24_tx_end())
					failed = true;
			}
		}
		// Read command terminator, start reply
		if (!verifySpace())
			return;
		break;
	}
	/* Read memory block mode, length is big endian.  */
	case STK_READ_PAGE:
	{
		int16_t length = getch() << 8;
		length |= getch();
		uint8_t desttype = getch();
		if (!verifySpace())
			return;
		//failed = true;
		do {
			putch(0xFF);
		} while (--length);
		break;
	}
	/* Get device signature bytes  */
	case STK_READ_SIGN:
	{
		// READ SIGN - return what Avrdude wants to hear
		if (!verifySpace())
			return;
		putch(SIGROW_DEVICEID0);
		putch(SIGROW_DEVICEID1);
		putch(SIGROW_DEVICEID2);
		break;
	}
	case STK_LEAVE_PROGMODE:
	{
		if (!verifySpace())
			return;
		nrfPacket resetPacket;
		resetPacket.command = 0;
		if (!nrf24_tx(&resetPacket, sizeof(resetPacket)) || !nrf24_tx_end())
			failed = true;
		
		finished = true;
		break;
	}
	default:
	{
		// This covers the response to commands like STK_ENTER_PROGMODE
		if (!verifySpace())
			return;
		break;
	}
	}
	putch(failed ? STK_FAILED : STK_OK);
	Serial.flush();
	uint16_t t = millis();
	if (failed || finished || t - lastSendTime > 5000)
	{
		OpenUart();
	}
	else
	{
		lastSendTime = t;
	}
}

bool ResetDevice()
{
	uint8_t success = OpenStk500();
	if (success == 4)
	{
		Serial.println("Device reset successfully");
		return true;
	}
	else
	{
		Serial.println("Error communicating with device");
		Serial.printf("%i packets transmitted successfully\n", success);
		return false;
	}
}

void HandleConfigure()
{
	if (!Serial.available())
		return;
	char ch = Serial.read();
	Serial.write(ch);
	if (ch == '\r')
		return;
	if (ch == '\n')
		ch = 0;
	if (serialbufpos >= sizeof(serialbuf))
		--serialbufpos;
	serialbuf[serialbufpos++] = ch;
	if (MatchSerialCommand("0 0 ", 4) == 4)
	{
		RespondToStk500Sync();
		return;
	}
	if (ch != 0)
		return;
	if (serialbufpos == 1 || serialbuf[0] == 'q')
	{
		Serial.println("done.");
		OpenUart();
		return;
	}
	if (serialbuf[0] == 'r')
	{
		ResetDevice();
	}
	else if (memcmp(serialbuf, "addr ", 5) == 0)
	{
		uartAddress[1] = progAddress[1] = serialbuf[6];
		uartAddress[2] = progAddress[2] = serialbuf[7];
		PrintAddresses();
		ResetDevice();
	}
	else if (memcmp(serialbuf, "id ", 3) == 0)
	{
		uartAddress[1] = progAddress[1] = serialbuf[4];
		uartAddress[2] = progAddress[2] = serialbuf[5];
		PrintAddresses();
		ResetDevice();
	}
	else if (memcmp(serialbuf, "setid ", 6) == 0)
	{
		if (ResetDevice())
		{
			// reprogram the user signature area with new address
			packet.addresshi = 0x13; // USERROW
			packet.addresslo = 0;
			packet.numpackets = 1;
			nrfPacket resetPacket;
			resetPacket.command = 0;
			if (nrf24_tx(&packet, sizeof(packet)) &&
				nrf24_tx(&serialbuf[6], 3) &&
				nrf24_tx(&resetPacket, sizeof(resetPacket)) && // exit from the bootloader
				SendSyncPacket()) // trigger a reset back into the bootloader to reconfigure the radio address
			{
				// address updated successfully
				uartAddress[1] = progAddress[1] = serialbuf[7];
				uartAddress[2] = progAddress[2] = serialbuf[8];
				PrintAddresses();
			}
			// re-establish connection on new address
			ResetDevice();
		}
		else
		{
			Serial.println("Error communicating with target device");
		}
	}
	serialbufpos = 0;
	Serial.write(">");
	Serial.flush();
}

void setup()
{
	pinMode(PIN_PB0, OUTPUT);
	pinMode(PIN_PB1, OUTPUT);
	digitalWrite(PIN_PB1, HIGH);
	Serial.begin(500000);
	nrf24_init();
	while (nrf24_read_register(RF_SETUP) != ((1 << RF_PWR_LOW) | (1 << RF_PWR_HIGH) | (1 << RF_DR_HIGH)))
	{
		Serial.println("radio not connected");
		nrf24_init();
		delay(500);
	}
	OpenUart();
}

void loop()
{
	switch (gMode)
	{
	case MODE_STK500: HandleStk500(); break;
	case MODE_UART: HandleUart(); break;
	case MODE_CONFIGURE: HandleConfigure(); break;
	}
}
