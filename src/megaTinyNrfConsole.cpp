#if !MEGA_TINY_NRF24_BOOT
#include "megaTinyNrfConsole.h"
#include "megaTinyNrfStk500.h"
#include "stk500.h"

namespace mtnrf {

Console::Console(BootLoader& device)
:	m_Device(device)
,	m_Stream(nullptr)
,	m_Stk500(device)
{
}

void Console::begin(Stream& stream)
{
	m_Stream = &stream;
	openConfig();
	//openUart();
}

void Console::end()
{
	m_Stream = nullptr;
}

void Console::handle()
{
	if (!m_Stream)
		return;

	switch (m_Mode)
	{
	case MODE_STK500: handleStk500(); break;
	case MODE_UART: handleUart(); break;
	case MODE_CONFIGURE: handleConfigure(); break;
	}
}

void Console::openUart()
{
	m_Mode = MODE_UART;
	auto& radio = m_Device.getRadio();
	radio.powerDown();
	radio.openWritingPipe('U');
	radio.startListening(_BV(0));
	m_SerialBuf = "";
}

void Console::openConfig()
{
	m_Mode = MODE_CONFIGURE;
	m_Device.setDebugStream(m_Stream);
	m_Stream->print(F("\n"
		"Configure nRF24L01+ ATtiny 0/1 interface\n"
		" addr <xyz> [channel]   - set radio address\n"
		" ch <channel>           - set radio channel\n"
		" setid <xyz> [channel]  - reprogram target device's radio address\n"
		" setch <channel>        - reprogram target device's radio channel (erases application)\n"
		" reset                  - reset target device\n"
		" crc                    - perform a CRC check of device flash\n"
		" scan                   - scan RF channels\n\n"));
	m_Device.printAddresses();
#if !DISABLE_MTNB_STATS
	auto& radio = m_Device.getRadio();
	m_Stream->print(radio.getResendCount());
	m_Stream->print(F(" retransmits for "));
	m_Stream->print(radio.getSendCount());
	m_Stream->println(F(" packets during last programming attempt"));
	radio.resetStats();
#endif
	m_Stream->print(F("\n>"));

	while (m_Stream->available())
		m_Stream->read();

	m_SerialBuf = "";
}

uint8_t Console::matchSerialCommand(const char* seq, uint8_t len)
{
	uint8_t buflen = m_SerialBuf.length();
	const char* buf = m_SerialBuf.c_str();
	if (len > buflen)
		len = buflen;
	for (; len > 0; --len)
		if (memcmp(&buf[buflen - len], seq, len) == 0)
			break;
	return len;
}

void Console::respondToStk500Sync()
{
	m_SerialBuf = "";
	m_Device.setDebugStream(&m_Debug);
	m_Stream->write(STK_INSYNC);
	m_Stream->write(STK_OK);
	m_Stream->write(STK_INSYNC);
	bool success = m_Device.enterBootLoader();
	m_Stream->write(success ? STK_OK : STK_FAILED);
	if (m_AllowStk500Debug)
		m_Debug.flush(*m_Stream);
	m_Stream->flush();
	m_Debug.clear();
	if (success)
	{
		m_Mode = MODE_STK500;
		m_Stk500.begin(*m_Stream);
	}
	else
	{
		openUart();
	}
}

void Console::handleStk500()
{
	bool finished = m_Stk500.handle();

	if (m_AllowStk500Debug)
		m_Debug.flush(*m_Stream);
	else
		m_Debug.clear();

	if (finished)
	{
		m_Stream->println(F("Closing STK500 interface"));
		m_AllowStk500Debug = false;
		openUart();
	}
}

void Console::handleUart()
{
#if !DISABLEMILLIS
	uint16_t t = millis();
#else
	uint16_t t = 0;
#endif
	uint8_t stk500match = 0;

	if (m_SerialBuf.length() < 32 && m_Stream->available())
		m_SerialBuf += (char)m_Stream->read();

	stk500match = matchSerialCommand("0 0 ", 4);
	if (stk500match == 4)
	{
		respondToStk500Sync();
		return;
	}
	auto& radio = m_Device.getRadio();
	if (!stk500match) // don't talk back on serial if STK500 is being initiated
	{
		while (radio.available())
		{
			uint8_t buf[32];
			uint8_t bytes = radio.read(buf).packetsize;
			m_Stream->write(buf, bytes);
		}
	}

	uint8_t idcmd = matchSerialCommand("*cfg\n", 4);
	if (idcmd == 4)
	{
		openConfig();
		return;
	}

	if (m_SerialBuf.length() == 32 || (m_SerialBuf.length() > 0 && t - m_LastSendTime > 100 && !stk500match && !idcmd))
	{
		radio.stopListening();
		delay(5);
		radio.writeLong(m_SerialBuf.c_str(), m_SerialBuf.length());
		radio.flush();
		radio.startListening(_BV(0));
		m_SerialBuf = "";
	}
	if (m_SerialBuf.length() == 0)
	{
		m_LastSendTime = t;
	}
}

void Console::handleConfigure()
{
	if (m_Scanning)
		scanChannels();

	if (!m_Stream->available())
		return;
	char ch = m_Stream->read();
	m_Stream->write(ch);
	if (ch == '\r')
		return;
	if (ch == '\n')
		ch = 0;
	m_SerialBuf += ch;
	if (matchSerialCommand("0 0 ", 4) == 4)
	{
		respondToStk500Sync();
		return;
	}
	if (ch != 0)
		return;
	const char* serialbuf = m_SerialBuf.c_str();
	m_Scanning = false;
	if (serialbuf[0] == 'q')
	{
		m_Stream->println("done.");
		openUart();
		return;
	}
	if (serialbuf[0] == 'r')
	{
		if (m_Device.enterBootLoader() && m_Device.exitBootLoader())
		{
			openUart();
			return;
		}
	}
	else if (m_SerialBuf.startsWith(F("sc")))
	{
		m_Scanning = true;
		m_ScanNo = 0;
		outputChannelHeader();
		m_SerialBuf = "";
		return;
	}
	else if (m_SerialBuf.startsWith(F("crc")))
	{
		if (m_Device.enterBootLoader())
			m_Device.performCrcCheck();
	}
	else if (serialbuf[0] == 'c' && serialbuf[1] == 'h' && strchr(serialbuf, ' '))
	{
		m_Device.getRadio().setChannel(atoi(strchr(serialbuf, ' ') + 1));
	}
	else if (serialbuf[0] == 'a' && serialbuf[1] == 'd' && strchr(serialbuf, ' '))
	{
		char* addr = strchr(serialbuf, ' ') + 1;
		m_Device.getRadio().setAddress(addr, 3);
		if (addr[3] == ' ' || addr[3] == ',' || addr[3] == ':')
			m_Device.getRadio().setChannel(atoi(&addr[4]));
		m_Device.printAddresses();
	}
	else if (m_SerialBuf.startsWith(F("id ")))
	{
		m_Device.getRadio().setAddress(&serialbuf[3], 3);
		m_Device.printAddresses();
	}
	else if (m_SerialBuf.startsWith(F("setid ")))
	{
		m_Device.reprogramAddress(&serialbuf[6]);
	}
	else if (m_SerialBuf.startsWith(F("setch ")))
	{
		m_Device.reprogramChannel(atoi(&serialbuf[6]));
	}
	else if (serialbuf[0] == 'v')
	{
		m_AllowStk500Debug = true;
	}
	m_SerialBuf = "";
	m_Stream->write(">");
	m_Stream->flush();
}

void Console::scanChannels()
{
	auto& radio = m_Device.getRadio();
	radio.ce(LOW);
	radio.startListening(0);
	uint8_t channel = radio.getChannel();
	for (uint8_t i = 0; i < CHANNELS; i++)
	{
		// select a new channel
		radio.setChannel((128 * i) / CHANNELS);

		// switch on RX
		radio.ce(HIGH);
		delayMicroseconds(130);
		// this is actually the point where the RPD-flag
		// is set, when CE goes low
		radio.ce(LOW);

		// read out RPD flag; set to 1 if
		// received power > -64dBm
		if (radio.readRegister(RPD) > 0)
			m_Channel[i]++;
	}
	radio.setChannel(channel);
	radio.ce(HIGH);
	if (++m_ScanNo == 200)
	{
		outputChannels();
		m_ScanNo = 0;
	}
}

void Console::outputChannelHeader()
{
	m_Stream->println(F(" 0         1         2         3         4         5         6"));
	m_Stream->println(F(" 0123456789012345678901234567890123456789012345678901234567890123"));
	m_Stream->println(F(">      1 2  3 4  5  6 7 8  9 10 11 12 13  14                     <"));
	m_ScanLine = 0;
}

// outputs channel data as a simple grey map
void Console::outputChannels()
{
	if (++m_ScanLine == 12)
		outputChannelHeader();

	uint8_t norm = 0;

	// find the maximal count in channel array
	for (int i = 0; i < CHANNELS; i++)
		if (m_Channel[i] > norm) norm = m_Channel[i];

	// now output the data
	m_Stream->print('|');
	for (int i = 0; i < CHANNELS; i++)
	{
		uint8_t pos;

		// calculate grey value position
		if (norm != 0) pos = (uint16_t(m_Channel[i]) * 10) / norm;
		else          pos = 0;

		// boost low values
		if (pos == 0 && m_Channel[i] > 0) pos++;

		// clamp large values
		if (pos > 9) pos = 9;

		m_Stream->write((char)pgm_read_byte(PSTR(" .:-=+*aRW") + pos));
		m_Channel[i] = 0;
	}

	// indicate overall power
	m_Stream->print(F("| "));
	m_Stream->println(norm);
}

} // namespace mtnrf
#endif
