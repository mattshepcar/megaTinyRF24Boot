#if !MEGA_TINY_NRF24_BOOT
#include "megaTinyNRFBoot.h"

namespace mtnrf {

#if DISABLE_MTNB_DEBUG
#define MTNB_DEBUG(X) do {} while (0)
#else
#define MTNB_DEBUG(X) do {if (m_DebugLog) m_DebugLog->X;} while (0)
#endif

struct Packet
{
	uint8_t command = 0x9D; // CPU_CCP_SPM_gc
	uint8_t numpackets = 0;
	uint8_t addresslo = 0x80; // last 128 bytes of SRAM
	uint8_t addresshi = 0x3F;
};

BootLoader::BootLoader(Radio& m_Radio, Stream* debuglog)
:	m_Radio(m_Radio)
{
	setDebugStream(debuglog);
}

bool BootLoader::readDeviceSignature(uint8_t* sig)
{
	uint8_t buf[3];
	if (!sig)
		sig = buf;
	uint8_t retry = 3;
	for (uint8_t i = 0; i < 3; ++i)
	{
		int16_t sigbyte = writeAndReadMemory(0x10FF + i, 0);
		if (sigbyte < 0)
			return false;
		sig[i] = sigbyte & 255;
		if (i == 0 && sig[i] != 0x1E)
		{
			if (!retry--)
				return false;
			--i;
		}
	}
	m_FlashSize = sig[1] - 0x90;
	return true;
}

bool BootLoader::sendSyncPacket()
{
	Packet syncPacket;
	m_Radio.clearReadFifo();
	return m_Radio.write(syncPacket) && m_Radio.flush();
}

void BootLoader::keepAlive(uint16_t t)
{
	if (t - m_LastKeepAlive > 250)
	{
		m_LastKeepAlive = t;
		sendSyncPacket();
	}
}

bool BootLoader::enterBootLoader()
{
	m_Radio.powerDown();
	m_Radio.openWritingPipe('P');
	m_Radio.clearReadFifo();
	m_Radio.clearWriteFifo();
	m_Radio.stopListening();
	delay(5);

	// wait for 4 sync packets to be received.  Up to 3 can fit in
	// the receivers FIFO so only with 4 can we be sure the bootloader
	// has actually started pulling them out of the FIFO.
	uint8_t retries = 0;
	uint8_t successes = 0;
	for (;;)
	{
		if (sendSyncPacket())
		{
			if (++successes == 4)
			{
				// make sure to clear read fifo in case application had queued any ack payloads
				MTNB_DEBUG(println(F("Reset device succesfully")));
				return true;
			}
		}
		else
		{
			if (++retries == 10) // todo: timeout setting
			{
				MTNB_DEBUG(print(F("Failed resetting device (")));
				MTNB_DEBUG(print(successes));
				MTNB_DEBUG(println(F(" packets were acknowledged)")));
				return false;
			}
			delay(50);
		}
	}
}

bool BootLoader::writeMemory(uint16_t address, const void* data, uint8_t length)
{
	// writes cannot cross page boundaries
	m_Radio.clearReadFifo();
	Packet packet;
	packet.addresshi = address >> 8;
	packet.addresslo = address & 255;
	packet.numpackets = (length + 31) / 32;
	return m_Radio.write(packet) && m_Radio.writeLong(data, length);
}
bool BootLoader::writeMemoryLong(uint16_t address, const void* data, uint16_t length)
{
	// writes can cross page boundaries
	uint8_t pageSize = 32;
	if (address >= 0x8000)
	{
		if (m_FlashSize == 0 && !readDeviceSignature())
			return false;
		pageSize = getFlashPageSize();
	}
	const uint8_t* u8data = (const uint8_t*) data;
	do 
	{
		uint8_t pageLength = pageSize - ((uint8_t)address & (pageSize - 1));
		if (pageLength > length)
			pageLength = length;
		if (!writeMemory(address, u8data, pageLength))
			return false;
		u8data += pageLength;
		address += pageLength;
		length -= pageLength;
	} while (length);
	return true;
}
bool BootLoader::writeMemory(uint16_t address, uint8_t value)
{
	return writeMemory(address, &value, 1);
}
int16_t BootLoader::writeAndReadMemory(uint16_t address, const void* data, uint8_t len, uint8_t retries)
{
	for(;;)
	{
		if (!flushWrites() ||
			!writeMemory(address, data, len))
		{
			MTNB_DEBUG(println(F("failed sending write")));
			return -1;
		}
		bool gotPacket = false;		
		for (uint8_t sync = 0; sync < 3; ++sync)
		{
			if (m_Radio.available())
			{
				gotPacket = true;
			}
			else if (!sendSyncPacket())
			{
				MTNB_DEBUG(println(F("failed sending write")));
				return -1;
			}
		}
		if (gotPacket)
			break;
#ifdef ESP8266
		wdt_reset();
#endif
		if (!retries--)
		{
			MTNB_DEBUG(println(F("No response to read memory request")));
			return -1;
		}
		delay(1);
	}
	int16_t value;
	do 
	{
		value = m_Radio.command(R_RX_PAYLOAD);
	} 
	while (m_Radio.available());

	return value;
}
int16_t BootLoader::writeAndReadMemory(uint16_t address, uint8_t value, uint8_t retries)
{
	return writeAndReadMemory(address, &value, 1, retries);
}
bool BootLoader::flushWrites()
{
	return m_Radio.flush();
}
bool BootLoader::exitBootLoader()
{
	Packet resetPacket;
	resetPacket.command = 0;
	return m_Radio.write(resetPacket) && m_Radio.flush();
}

bool BootLoader::waitForEepromWrites()
{
	uint16_t startTime = millis();
	for (;;)
	{
		int16_t nvmstatus = writeAndReadMemory(0x1001, 0);
		if (nvmstatus < 0)
		{
			MTNB_DEBUG(println(F("Failed to read non-volatile memory controller status register")));
			return false;
		}
		if ((nvmstatus & 3) == 0)
			return true;
		uint16_t t = millis() - startTime;
		if (t > 200)
		{
			MTNB_DEBUG(println(F("Timed out waiting for EEPROM writes!")));
			return false;
		}
	}
}

bool BootLoader::performCrcCheck()
{
	MTNB_DEBUG(println(F("Requesting CRC check")));
	// set CRCSCAN.ENABLE=1
	uint8_t vals [] = { 1, 0 };
	int16_t crcstatus = writeAndReadMemory(0x120, vals, 2);
	if (crcstatus < 0)
	{
		MTNB_DEBUG(println(F("Failed to read CRC check status!")));
		return false;
	}
	if ((crcstatus & 3) == 2)
	{
		MTNB_DEBUG(println(F("CRC check passed OK!")));
		return true;
	}
	MTNB_DEBUG(print(F("CRC status = ")));
	MTNB_DEBUG(print(crcstatus, HEX));
	MTNB_DEBUG(println(F("\nCRC check failed!")));
	return false;
}

bool BootLoader::reprogramAddress(const char* addr)
{
	char addrbuf[4] = { addr[0], addr[1], addr[2], 0 };
	// reprogram the user signature area with new address/channel
	uint8_t addrsize = 3;
	if (addr[3] == ' ' || addr[3] == ',' || addr[3] == ':')
	{
		addrbuf[3] = atoi(&addr[4]);
		addrsize = 4;
		// the new channel will only be programmed if ChangeRadioSettings succeeds
		// i.e. we can definitely talk to the device on the new channel
	}
	if ((addrsize < 4 || changeRadioSettings(addrbuf[3], m_Radio.getBitRate())) &&
		writeMemory(0x1300, addrbuf, addrsize) &&
		waitForEepromWrites() &&
		exitBootLoader() &&
		sendSyncPacket()) // trigger a software reset to reconfigure the radio address
	{
		// address updated successfully
		m_Radio.setAddress(addr, 3);
		return enterBootLoader();
	}
	return false;
}

bool BootLoader::reprogramChannel(uint8_t channel)
{
	uint8_t oldChannel = m_Radio.getChannel();
	if (changeRadioSettings(channel, m_Radio.getBitRate()))
	{
		if (writeMemory(0x1303, channel) &&
			waitForEepromWrites() &&
			exitBootLoader() &&
			enterBootLoader())
		{
			MTNB_DEBUG(println(F("Reprogrammed radio channel OK")));
			printAddresses();
			return true;
		}
		else
		{
			MTNB_DEBUG(println(F("Failed reprogramming radio channel")));
			m_Radio.setChannel(oldChannel);
			enterBootLoader();
		}
	}
	return false;
}

bool BootLoader::changeRadioSettings(uint8_t channel, BitRate bitrate)
{
	bool success = false;
	// reprogram the first flash page with a small program to restart 
	// the bootloader with new radio settings. the radio reverts
	// back to its original settings if the watchdog kicks in.
	static const uint8_t reprogramApp [] PROGMEM =
	{
		0x03, 0xFC, // sbrc r0, RSTCTRL_WDRF_bp 
		0xEC, 0xCF,	// rjmp wait_for_command
		0xA0, 0xDF, // rcall nrf24_set_config_r21
		0xDB, 0xCF, // rjmp start_bootloader_custom_channel
	};
	Packet resetPacket;
	resetPacket.command = 0; // r21 config value
	resetPacket.addresslo = sizeof(reprogramApp);
	resetPacket.addresshi = 0x81;
	resetPacket.numpackets = 0;
	Packet packet;
	packet.addresslo = 0;
	packet.addresshi = 0x81; // PROGMEM
	packet.numpackets = 2;
	if (m_Radio.write(packet) &&
		m_Radio.write(reprogramApp) &&
		m_Radio.write(channel) &&
		m_Radio.write(resetPacket) &&
		m_Radio.flush())
	{
		MTNB_DEBUG(println(F("Sent channel change request OK")));
		// change our radio settings
		m_Radio.powerDown();
		BitRate oldBitRate = m_Radio.getBitRate();
		uint8_t oldChannel = m_Radio.getChannel();
		m_Radio.setChannel(channel);
		m_Radio.setBitRate(bitrate);

		if (enterBootLoader())
		{
			success = true;
		}
		else
		{
			MTNB_DEBUG(println(F("Failed to switch radio channel")));

			// failed, change settings back
			m_Radio.powerDown();
			m_Radio.setChannel(oldChannel);
			m_Radio.setBitRate(oldBitRate);

			enterBootLoader();
		}

		// it's only necessary to erase the channel switcher program because
		// reprogramming the channel in USERROW relies on reentering the app to
		// trigger a full software reset to reboot the bootloader with the
		// new radio settings.  we could rely on the watchdog timeout instead to
		// reboot but it would be slower.
		static const uint8_t standbyProgram [] PROGMEM =
		{
			0x86, 0xDF,// rcall nrf24_poll_reset
			0xFE, 0xCF,// rjmp .-4
		};
		packet.numpackets = 1;
		if (m_Radio.write(packet) &&
			m_Radio.write(standbyProgram) &&
			m_Radio.flush())
		{
			MTNB_DEBUG(println(F("Channel switcher program cleared OK")));
		}
		else
		{
			MTNB_DEBUG(println(F("Warning: failed to clear channel switcher")));
		}
	}
	else
	{
		MTNB_DEBUG(println("Failed to send channel change request"));
	}

	return success;
}

void BootLoader::printAddresses()
{
#if !DISABLE_MTNB_DEBUG
	uint8_t channel = m_Radio.getChannel();
	uint8_t addr[3];
	m_Radio.readRegister(TX_ADDR, addr, 3);
	char buf[64];
	sprintf_P(buf, PSTR("Channel = %i  UART addr = %02x%02x%02x  Programming addr = %02x%02x%02x"), channel, 'U', addr[1], addr[2], 'P', addr[1], addr[2]);
	MTNB_DEBUG(println(buf));
#endif
}

} // namespace mtnrf
#endif