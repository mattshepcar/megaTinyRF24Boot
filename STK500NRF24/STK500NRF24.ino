#include <SPI.h>
#include "src/stk500.h"
#include "nRF24L01.h"
#include <string.h>

//#define COUNT_ALL_RESENDS 1
#define NRF24_CSN_PIN PIN_PB0
#define NRF24_CE_PIN PIN_PB1
static const uint16_t VERSION = 0x900;
uint8_t gProgAddress [] = { 'P','0','1' };
uint8_t gUartAddress [] = { 'U','0','1' };
uint8_t gChannel = 50;
uint8_t gDataRate = (1 << RF_DR_HIGH);

struct nrfPacket
{
	uint8_t command = 0x9D; // CPU_CCP_SPM_gc
	uint8_t numpackets = 0;
	uint8_t addresslo = 0x80; // last 128 bytes of SRAM
	uint8_t addresshi = 0x3F; 
};

uint8_t nrf24_status();
uint8_t nrf24_command(uint8_t cmd, uint8_t data = NOP);
uint8_t nrf24_begin(uint8_t cmd);
void nrf24_end();
void nrf24_set_tx_address(const uint8_t address[3]);
inline void nrf24_write_register(uint8_t reg, uint8_t data) { nrf24_command(reg | W_REGISTER, data); }
inline uint8_t nrf24_read_register(uint8_t reg) { return nrf24_command(reg, 0); }
inline uint8_t nrf24_status()
{
	return nrf24_read_register(STATUS_NRF);
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
	digitalWrite(NRF24_CSN_PIN, LOW);
	delayMicroseconds(5);
	return SPI.transfer(cmd);
}
inline void nrf24_end()
{
	digitalWrite(NRF24_CSN_PIN, HIGH);
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
	digitalWrite(NRF24_CSN_PIN, HIGH);
	delay(5);
	nrf24_write_register(CONFIG, 0);
	nrf24_write_register(EN_AA, 0x3F);
	nrf24_write_register(SETUP_AW, 1);
	nrf24_write_register(SETUP_RETR, 15);
	nrf24_write_register(RF_CH, gChannel);
	nrf24_write_register(RF_SETUP, (1 << RF_PWR_LOW) | (1 << RF_PWR_HIGH) | gDataRate);
	nrf24_write_register(FEATURE, (1 << EN_DPL) | (1 << EN_ACK_PAY) | (1 << EN_DYN_ACK));
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
	nrf24_write_register(STATUS_NRF, _BV(MAX_RT));
	nrf24_write_register(CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP));
	delay(5);
}
void nrf24_begin_rx()
{
	nrf24_write_register(RF_CH, gChannel);
	nrf24_write_register(CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP) | (1 << PRIM_RX));
}
uint16_t gPacketsResent = 0;
uint16_t gPacketsSent = 0;
uint16_t gProgramAddress = 0;
uint16_t gLastProgrammedAddress = 0;
uint8_t gLastPageSize = 0;
uint16_t gCrc = 0;
int16_t gLastAck = -1;
bool gDataPending = false;
bool nrf24_tx_end(uint8_t retries = 16, uint8_t resendDelay = 1, bool waitForWrite = false)
{
	uint16_t startTime = millis();
	for (;;)
	{
		uint8_t fifostatus = nrf24_read_register(FIFO_STATUS);
		if (!(fifostatus & _BV(RX_EMPTY)))
		{
			gLastAck = nrf24_command(R_RX_PAYLOAD);
		}
		if (fifostatus & _BV(TX_EMPTY))
		{
#if COUNT_ALL_RESENDS
			if (gDataPending)
				gPacketsResent += nrf24_read_register(OBSERVE_TX) & 15;
			gDataPending = waitForWrite;
#endif
			return true;
		}
		uint8_t status = nrf24_status();
		if (status & _BV(MAX_RT))
		{
			nrf24_write_register(STATUS_NRF, _BV(MAX_RT));
			if (retries-- > 0)
			{
				++gPacketsResent;
				digitalWrite(NRF24_CE_PIN, LOW);
				delay(resendDelay);
				digitalWrite(NRF24_CE_PIN, HIGH);
			}
			else
			{
				nrf24_command(FLUSH_TX, 0);
				return false;
			}
		}
#if !COUNT_ALL_RESENDS
		if (waitForWrite && !(status & _BV(TX_FULL)))
			return true;
#endif
		uint16_t t = millis() - startTime;
		if (t > 500)
		{
			nrf24_command(FLUSH_TX);

			Serial.print("FIFO STATUS = ");
			Serial.print(fifostatus, HEX);
			Serial.print("\nSTATUS = ");
			Serial.print(status, HEX);
			Serial.println("\nTimed out in transmit!!!");
			return false;
		}
	}
}
bool nrf24_tx(const void* data, uint8_t len, uint8_t retries = 16, uint8_t resendDelay = 1)
{
	if (!nrf24_tx_end(retries, resendDelay, true))
		return false;
	const uint8_t* addr = (const uint8_t*) data;
	nrf24_begin(W_TX_PAYLOAD);
	while (len--)
		SPI.transfer(*addr++);
	nrf24_end();
	++gPacketsSent;
	return true;
}

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

char serialbuf[32];
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
uint8_t gScanning = 0;
uint8_t gScanLine = 0;
uint8_t gScanNo = 0;

void OpenUart()
{
	gMode = MODE_UART;
	nrf24_write_register(CONFIG, 0);
	nrf24_set_tx_address(gUartAddress);
	nrf24_begin_rx();
	serialbufpos = 0;
}

bool SendSyncPacket(uint8_t count = 1)
{
	nrfPacket syncPacket;
	do {
		if (!nrf24_tx(&syncPacket, sizeof(syncPacket)))
			return false;
	} while (--count);
	return nrf24_tx_end();
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
	gPacketsResent = 0;
	gPacketsSent = 0;
	gLastProgrammedAddress = 0;
	gLastPageSize = 0;
	gCrc = 0xFFFF;
	nrf24_set_tx_address(gProgAddress);
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
			if (++retries == 10) // todo: timeout setting
				break;
			delay(50);
		}
	}
	return successes;
}

bool ChangeRadioSettings(uint8_t channel, uint8_t datarate);

void RespondToStk500Sync()
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
	uint8_t a0 = gUartAddress[1];
	uint8_t a1 = gUartAddress[2];
	Serial.printf("Channel = %i  UART addr = %02x%02x%02x  Programming addr = %02x%02x%02x\n", gChannel, 'U', a0, a1, 'P', a0, a1);
}

void ScanChannels();

void OpenConfig()
{
	gMode = MODE_CONFIGURE;
	Serial.println("\nConfigure STK500-nRF24L01+ interface\n");
	Serial.println(" addr <xyz> [channel]   - set address of target device");
	Serial.println(" ch <channel>           - set channel of target device");
	Serial.println(" setid <xyz> [channel]  - reprogram target device's listen address");
	Serial.println(" setch <channel>        - reprogram target device's channel (erases application)");
	Serial.println(" reset                  - reset target device");
	Serial.println(" crc                    - perform a CRC check of device flash");
	Serial.println(" scan                   - scan for available channels\n");
	PrintAddresses();
	Serial.print(gPacketsResent);
	Serial.write(" retransmits for ");
	Serial.print(gPacketsSent);
	Serial.println(" packets during last programming attempt");
	//Serial.print("CRC = 0x");
	//Serial.print(gCrc, HEX);
	Serial.print("\n>");

	gPacketsResent = 0;
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

#ifdef __AVR__
#include <util/crc16.h>
inline uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
	return _crc_xmodem_update(crc, byte);
}
#else
uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
	crc ^= uint16_t(byte) << 8;
	for (uint8_t i = 0; i < 8; i++)
	{
		if (crc & 0x8000)
			crc = (crc << 1) ^ 0x1021;
		else
			crc <<= 1;
	}
	return crc;
}
#endif
uint16_t crc16(const void* data, uint16_t length, uint16_t crc = 0xFFFF)
{
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	while (length--)
		crc = crc16_update(crc, *bytes++);
	return crc;
}
int16_t WriteAndReadMemory(uint16_t address, const void* data, uint8_t len, uint8_t retries = 16);
int16_t WriteAndReadMemory(uint16_t address, uint8_t value);

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
		gProgramAddress = getch();
		gProgramAddress += getch() << 8;
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
		if (length <= 128)
		{
			char packetbuf[128];
			for (uint8_t i = 0; i < length; ++i)
				packetbuf[i] = getch();

			if (desttype == 'F')
			{
				gProgramAddress += 0x8000; // progmem
				gLastProgrammedAddress = gProgramAddress + length;
				gLastPageSize = length;
				gCrc = crc16(packetbuf, length, gCrc);
			}
			else if (desttype == 'E')
			{
				gProgramAddress += 0x1400; // eeprom
			}
			else if (desttype == 'U')
			{
				gProgramAddress += 0x1300; // userrow
			}
			if (WriteMemory(gProgramAddress, packetbuf, length))
			{
				failed = false;
				if (!nrf24_tx_end())
					failed = true;
				if (gProgramAddress < 0x8000 && !WaitForEepromWrites())
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
		/*uint8_t desttype =*/ getch();
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

		putch(WriteAndReadMemory(0x10FF, 0));
		putch(WriteAndReadMemory(0x1100, 0));
		putch(WriteAndReadMemory(0x1101, 0));
		break;
	}
	case STK_LEAVE_PROGMODE:
	{
		if (!verifySpace())
			return;

		if (!ExitBootloader())
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

bool WriteMemory(uint16_t address, const void* data, uint8_t length)
{
	const uint8_t packetsize = 32;
	nrfPacket packet;
	packet.addresshi = address >> 8;
	packet.addresslo = address & 255;
	packet.numpackets = (length + packetsize - 1) / packetsize;
	if (!nrf24_tx(&packet, sizeof(packet)))
		return false;
	const uint8_t* packetbuf = (const uint8_t*) data;
	for (uint8_t i = 0; i < length; i += packetsize)
		if (!nrf24_tx(&packetbuf[i], min(packetsize, length - i)))
			return false;
	return true;
}
bool WriteMemory(uint16_t address, uint8_t value)
{
	return WriteMemory(address, &value, 1);
}
int16_t WriteAndReadMemory(uint16_t address, const void* data, uint8_t len, uint8_t retries)
{
	if (!nrf24_tx_end())
		return -1;
	gLastAck = -1;
	nrf24_command(FLUSH_RX);
	if (WriteMemory(address, data, len) && SendSyncPacket(3))
	{
		while (gLastAck < 0 && retries--)
		{
			// the ack payload might have get lost on the air so keep retrying
			delay(1);
			if (!WriteMemory(address + len - 1, (uint8_t*) data + len - 1, 1))
				break;
		}
	}
	return gLastAck;
}
int16_t WriteAndReadMemory(uint16_t address, uint8_t value)
{
	return WriteAndReadMemory(address, &value, 1);
}
bool ExitBootloader()
{
	nrfPacket resetPacket;
	resetPacket.command = 0;
	return nrf24_tx(&resetPacket, sizeof(resetPacket)) && nrf24_tx_end();
}

bool WaitForEepromWrites()
{
	uint16_t startTime = millis();
	for (;;)
	{
		int16_t nvmstatus = WriteAndReadMemory(0x1001, 0);
		if (nvmstatus < 0)
		{
			Serial.println("Failed to read non-volatile memory controller status register");
			return false;
		}
		if ((nvmstatus & 3) == 0)
			return true;
		uint16_t t = millis() - startTime;
		if (t > 200)
		{
			Serial.println("Timed out waiting for EEPROM writes!");
			return false;
		}
	}
}

bool WriteCrc()
{
	if (gLastProgrammedAddress >= 0xC000)
		return true;

	// clear rest of flash memory
	static const uint8_t pagebuf[128] = { 0 };
	for (uint16_t addr = gLastProgrammedAddress; addr < 0xC000; addr += gLastPageSize)
	{
		int16_t r = WriteAndReadMemory(addr, pagebuf, gLastPageSize);
		if (r < 0)
			return false;
		// if next page starts with 0 assume it is blank
		if (r == 0)
			break;
	}
	//uint8_t vals [] = { 1, 0 };
	//int16_t crcstatus = WriteAndReadMemory(0x120, vals, 2);
	//if (crcstatus < 0 || (crcstatus & 3) != 2)
	//	return false;

	//// calculate CRC
	//uint16_t crc = gCrc;
	//uint16_t addr = gLastProgrammedAddress;
	//for (; addr < 0xBFFE; ++addr)
	//	crc = crc16_update(crc, 0xFF);
	//pagebuf[62] = (uint8_t) (crc >> 8);
	//pagebuf[63] = (uint8_t) crc;
	//crc = crc16_update(crc, pagebuf[62]);
	//crc = crc16_update(crc, pagebuf[63]);
	//gCrc = crc;
	//// write the CRC
	////if (WriteMemory(0xBFE0, pagebuf, 64) && nrf24_tx_end())
	//if (WriteMemory(0xBFFE, pagebuf + 62, 2) && nrf24_tx_end())
	//	Serial.println("Updated CRC successfully!");
	//else
	//	Serial.println("Error writing CRC");

	PerformCrcCheck();
}
bool PerformCrcCheck()
{
	Serial.println("Requesting CRC check");
	// set CRCSCAN.ENABLE=1
	uint8_t vals [] = { 1, 0 };
	int16_t crcstatus = WriteAndReadMemory(0x120, vals, 2); 
	if (crcstatus < 0)
	{
		Serial.println("Failed to read CRC check status!");
		return false;
	}
	if ((crcstatus & 3) == 2)
	{
		Serial.println("CRC check passed OK!");
		return true;
	}
	Serial.print("CRC status = ");
	Serial.print(crcstatus, HEX);
	Serial.println("\nCRC check failed!");
	return false;
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

bool ChangeRadioSettings(uint8_t channel, uint8_t datarate)
{
	bool success = false;
	// reprogram the first flash page with a small program to restart 
	// the bootloader with new radio settings. the radio reverts
	// back to its original settings if the watchdog kicks in.
	static const uint8_t reprogramApp [] =
	{
		0x03, 0xFC, // sbrc r0, RSTCTRL_WDRF_bp 
		0xEC, 0xCF,	// rjmp wait_for_command
		0x9F, 0xDF, // rcall nrf24_set_config_r21
		0xDB, 0xCF, // rjmp start_bootloader_custom_channel
	};
	nrfPacket resetPacket;
	resetPacket.command = 0; // r21 config value
	resetPacket.addresslo = sizeof(reprogramApp);
	resetPacket.addresshi = 0x81;
	resetPacket.numpackets = 0;
	nrfPacket packet;
	packet.addresslo = 0;
	packet.addresshi = 0x81; // PROGMEM
	packet.numpackets = 2;
	if (nrf24_tx(&packet, sizeof(packet)) &&
		nrf24_tx(&reprogramApp[0], sizeof(reprogramApp)) &&
		nrf24_tx(&channel, 1) &&
		nrf24_tx(&resetPacket, sizeof(resetPacket)) &&
		nrf24_tx_end())
	{
		Serial.println("Sent channel change request OK");
		// change our radio settings
		nrf24_write_register(CONFIG, 0);
		nrf24_write_register(RF_CH, channel);
		nrf24_write_register(RF_SETUP, _BV(RF_PWR_LOW) | _BV(RF_PWR_HIGH) | datarate);

		if (ResetDevice())
		{
			success = true;
		}
		else
		{
			Serial.println("Failed to switch radio channel");

			// failed, change settings back
			nrf24_write_register(CONFIG, 0);
			nrf24_write_register(RF_CH, gChannel);
			nrf24_write_register(RF_SETUP, _BV(RF_PWR_LOW) | _BV(RF_PWR_HIGH) | gDataRate);

			ResetDevice();
		}

		// it's only necessary to erase the channel switcher program because
		// reprogramming the channel in USERROW relies on reentering the app to
		// trigger a full software reset to reboot the bootloader with the
		// new radio settings.  we could rely on the watchdog timeout instead to
		// reboot but it would be slower.
		static const uint8_t standbyProgram [] =
		{
			0x00, 0x24,// clr r0
			0x86, 0xDF,// rcall nrf24_poll_reset
			0xFE, 0xCF,// rjmp .-4
		};
		packet.numpackets = 1;
		if (nrf24_tx(&packet, sizeof(packet)) &&
			nrf24_tx(&standbyProgram[0], sizeof(standbyProgram)) &&
			nrf24_tx_end())
		{
			Serial.println("Channel switcher program cleared OK");
		}
		else
		{
			Serial.println("Warning: failed to clear channel switcher");
		}
	}
	else
	{
		Serial.println("Failed to send channel change request");
	}

	return success;
}

void SetChannel(const char* ch)
{
	gChannel = atoi(ch);
	nrf24_write_register(RF_CH, gChannel);
	PrintAddresses();
}

void SetAddress(const char* addr)
{
	nrf24_write_register(CONFIG, 0);
	gUartAddress[1] = gProgAddress[1] = addr[1];
	gUartAddress[2] = gProgAddress[2] = addr[2];
	if (addr[3] == ' ' || addr[3] == ',' || addr[3] == ':')
		SetChannel(&addr[4]);
	else
		PrintAddresses();
}

void HandleConfigure()
{
	if (gScanning)
		ScanChannels();

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
	gScanning = false;
	if (serialbufpos == 1 || serialbuf[0] == 'q')
	{
		Serial.println("done.");
		OpenUart();
		return;
	}
	if (serialbuf[0] == 'r')
	{
		if (ResetDevice() && ExitBootloader())
		{
			OpenUart();
			return;
		}
	}
	else if (serialbuf[0] == 's' && serialbuf[1] == 'c')
	{
		gScanning = true;
		gScanNo = 0;
		OutputChannelHeader();
		serialbufpos = 0;
		return;
	}
	else if (memcmp(serialbuf, "crc", 3) == 0)
	{
		if (ResetDevice())
			PerformCrcCheck();
	}
	else if (memcmp(serialbuf, "setcrc", 6) == 0)
	{
		if (ResetDevice())
			WriteCrc();
	}
	else if (memcmp(serialbuf, "ch", 2) == 0 && strchr(serialbuf, ' '))
	{
		SetChannel(strchr(serialbuf, ' ') + 1);
	}
	else if (memcmp(serialbuf, "ad", 2) == 0 && strchr(serialbuf, ' '))
	{
		SetAddress(strchr(serialbuf, ' ') + 1);
	}
	else if (memcmp(serialbuf, "id ", 3) == 0)
	{
		SetAddress(&serialbuf[3]);
	}
	else if (memcmp(serialbuf, "setid ", 6) == 0)
	{
		if (ResetDevice())
		{
			// reprogram the user signature area with new address/channel
			uint8_t addrsize = 3;
			if (serialbuf[9] == ' ' || serialbuf[9] == ',' || serialbuf[9] == ':')
			{
				serialbuf[9] = atoi(&serialbuf[10]);
				addrsize = 4;
				// the new channel will only be programmed if ChangeRadioSettings succeeds
				// i.e. we can definitely talk to the device on the new channel
			}
			if ((addrsize < 4 || ChangeRadioSettings(serialbuf[9], gDataRate)) &&
				WriteMemory(0x1300, &serialbuf[6], addrsize) &&
				WaitForEepromWrites() &&
				ExitBootloader() &&
				SendSyncPacket()) // trigger a reset back into the bootloader to reconfigure the radio address
			{
				// address updated successfully
				gUartAddress[1] = gProgAddress[1] = serialbuf[7];
				gUartAddress[2] = gProgAddress[2] = serialbuf[8];
				if (addrsize == 4)
					gChannel = serialbuf[9];
				PrintAddresses();
			}
			// re-establish connection on new address
			ResetDevice();
		}
	}
	else if (memcmp(serialbuf, "setch ", 6) == 0)
	{
		uint8_t channel = atoi(&serialbuf[6]);
		if (ResetDevice() && ChangeRadioSettings(channel, gDataRate))
		{
			if (WriteMemory(0x1303, channel) &&
				WaitForEepromWrites() &&
				ExitBootloader() && 
				SendSyncPacket()) // trigger a reset back into the bootloader to reconfigure the radio address
			{
				Serial.println("Reprogrammed radio channel OK");
				gChannel = channel;
				PrintAddresses();
			}
			else
			{
				Serial.println("Failed reprogramming radio channel");
			}
			// re-establish connection on new address
			ResetDevice();
		}
	}
	serialbufpos = 0;
	Serial.write(">");
	Serial.flush();
}

// Array to hold Channel data
#define CHANNELS  64
uint8_t channel[CHANNELS];

// greyscale mapping
static const char grey [] = " .:-=+*aRW";

void OutputChannelHeader()
{
	Serial.println(" 0         1         2         3         4         5         6");
	Serial.println(" 0123456789012345678901234567890123456789012345678901234567890123");
	Serial.println(">      1 2  3 4  5  6 7 8  9 10 11 12 13  14                     <");
	gScanLine = 0;
}

// outputs channel data as a simple grey map
void OutputChannels()
{
	if (++gScanLine == 12)
		OutputChannelHeader();

	uint8_t norm = 0;

	// find the maximal count in channel array
	for (int i = 0; i < CHANNELS; i++)
		if (channel[i] > norm) norm = channel[i];

	// now output the data
	Serial.print('|');
	for (int i = 0; i < CHANNELS; i++)
	{
		uint8_t pos;

		// calculate grey value position
		if (norm != 0) pos = (uint16_t(channel[i]) * 10) / norm;
		else          pos = 0;

		// boost low values
		if (pos == 0 && channel[i] > 0) pos++;

		// clamp large values
		if (pos > 9) pos = 9;

		// print it out
		Serial.print(grey[pos]);
		channel[i] = 0;
	}

	// indicate overall power
	Serial.print("| ");
	Serial.println(norm);
}

// scanning all channels in the 2.4GHz band
void ScanChannels()
{
	digitalWrite(NRF24_CE_PIN, LOW);
	nrf24_write_register(CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP) | (1 << PRIM_RX));
	for (uint8_t i = 0; i < CHANNELS; i++)
	{
		// select a new channel
		nrf24_write_register(RF_CH, (128 * i) / CHANNELS);

		// switch on RX
		digitalWrite(NRF24_CE_PIN, HIGH);
		delayMicroseconds(130);
		// this is actually the point where the RPD-flag
		// is set, when CE goes low
		digitalWrite(NRF24_CE_PIN, LOW);

		// read out RPD flag; set to 1 if
		// received power > -64dBm
		if (nrf24_read_register(RPD) > 0)
			channel[i]++;
	}
	nrf24_write_register(RF_CH, gChannel);
	digitalWrite(NRF24_CE_PIN, HIGH);
	if (++gScanNo == 200)
	{
		OutputChannels();
		gScanNo = 0;
	}
}

void setup()
{
	pinMode(NRF24_CSN_PIN, OUTPUT);
	pinMode(NRF24_CE_PIN, OUTPUT);
	digitalWrite(NRF24_CE_PIN, HIGH);
	Serial.begin(500000);
	nrf24_init();
	while (nrf24_read_register(RF_SETUP) != ((1 << RF_PWR_LOW) | (1 << RF_PWR_HIGH) | gDataRate))
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
