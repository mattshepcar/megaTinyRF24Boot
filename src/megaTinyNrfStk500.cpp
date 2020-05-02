#if !MEGA_TINY_NRF24_BOOT
#include "megaTinyNrfStk500.h"
#include "megaTinyNrfBoot.h"
#include "stk500.h"

namespace mtnrf {

Stk500::Stk500(BootLoader& device)
:	m_Stream(nullptr)
,	m_Device(device)
{}

void Stk500::begin(Stream& stream)
{
	m_Stream = &stream;
#if !DISABLEMILLIS
	m_LastCommandTime = millis();
#else
	m_LastCommandTime = 0;
#endif
}

bool Stk500::handle()
{
#if !DISABLEMILLIS
	m_CommandStartTime = millis();
#else
	m_CommandStartTime = 0;
#endif
	if (!m_Stream->available())
	{
		if ((m_CommandStartTime - m_LastCommandTime) > 5000)
			return true; // timed out
		m_Device.keepAlive(m_CommandStartTime);
		return false;
	}
	bool finished = false;
	m_ValidCommand = false;
	m_Success = true;
	uint8_t command = m_Stream->read();
	switch (command)
	{
	case STK_GET_SYNC:
	{
		if (endCommand())
			m_Success &= m_Device.sendSyncPacket();
		break;
	}
	case STK_GET_PARAMETER:
	{
		uint8_t which = getch();
		if (endCommand())
		{
			if (which == STK_SW_MINOR)
				m_Stream->write('\x00');
			else if (which == STK_SW_MAJOR)
				m_Stream->write('\x09');
			else
				m_Stream->write('\x03');
		}
		break;
	}
	case STK_SET_DEVICE:
	{
		// ignore
		for (uint8_t i = 0; i < 20; ++i)
			getch();
		endCommand();
		break;
	}
	case STK_SET_DEVICE_EXT:
	{
		// ignore
		for (uint8_t i = 0; i < 5; ++i)
			getch();
		endCommand();
		break;
	}
	case STK_LOAD_ADDRESS:
	{
		m_ProgramAddress = getch();
		m_ProgramAddress += getch() << 8;
		endCommand();
		break;
	}
	case STK_UNIVERSAL:
	{
#ifndef RAMPZ
		// UNIVERSAL command is ignored
		for (uint8_t i = 0; i < 4; ++i)
			getch();
		if (endCommand())
			m_Stream->write('\0');
#endif
		break;
	}
	case STK_PROG_PAGE:
	{
		int16_t length = getch() << 8;
		length |= getch();
		uint8_t desttype = getch();

		m_Success = false;
		if (length <= 128)
		{
			char packetbuf[128];
			for (uint8_t i = 0; i < length; ++i)
				packetbuf[i] = getch();

			if (desttype == 'F')
			{
				m_ProgramAddress += 0x8000; // progmem
			}
			else if (desttype == 'E')
			{
				m_ProgramAddress += 0x1400; // eeprom
			}
			else if (desttype == 'U')
			{
				m_ProgramAddress += 0x1300; // userrow
			}
			if (m_Device.writeMemory(m_ProgramAddress, packetbuf, length))
			{
				m_Success = m_Device.flushWrites();
				m_Success &= m_ProgramAddress >= 0x8000 || m_Device.waitForEepromWrites();
			}
		}
		endCommand();
		break;
	}
	case STK_READ_PAGE:
	{
		int16_t length = getch() << 8;
		length |= getch();
		/*uint8_t desttype =*/ getch();
		if (endCommand())
		{
			do {
				m_Stream->write(0xFF);
			} while (--length);
		}
		break;
	}
	// Get device signature bytes
	case STK_READ_SIGN:
	{
		if (endCommand())
		{
			uint8_t sig[3];
			m_Success = m_Device.readDeviceSignature(sig);
			m_Stream->write(sig, 3);
		}
		break;
	}
	case STK_LEAVE_PROGMODE:
	{
		if (endCommand())
		{
			finished = m_Success = m_Device.exitBootLoader();
		}
		break;
	}
	default:
	{
		endCommand();
		break;
	}
	}
	if (m_ValidCommand)
	{
		m_Stream->write(m_Success ? STK_OK : STK_FAILED);
		m_LastCommandTime = m_CommandStartTime;
	}
	return finished;
}

int Stk500::getch()
{
	while (!m_Stream->available())
	{
#if !DISABLEMILLIS
		uint16_t t = millis();
		if (t - m_CommandStartTime > 1000)
		{
			m_Success = m_ValidCommand = false;
			return -1;
		}
#endif
	}
	return m_Stream->read();
}

bool Stk500::endCommand()
{
	m_ValidCommand = (getch() == CRC_EOP);
	m_Stream->write(m_ValidCommand ? STK_INSYNC : STK_NOSYNC);
	return m_ValidCommand;
}

} // namespace mtnrf
#endif
