#include "megaTinyNrf24.h"
#if !MEGA_TINY_NRF24_BOOT
#include <SPI.h>
#endif

namespace mtnrf {

#if !MEGA_TINY_NRF24_BOOT
bool Radio::begin(const Config& config)
{
	SPI.begin();
	pinMode(m_CePin, OUTPUT);
	pinMode(m_CsnPin, OUTPUT);
	digitalWrite(m_CePin, HIGH);
	digitalWrite(m_CsnPin, HIGH);
	delay(5);
	powerDown();
	writeRegister(EN_AA, 0x3F);
	writeRegister(SETUP_AW, config.m_AddressLength - 2);
	writeRegister(SETUP_RETR, config.m_NrfRetries);
	writeRegister(RF_SETUP, config.m_Setup);
	writeRegister(FEATURE, _BV(EN_DPL) | _BV(EN_ACK_PAY) | _BV(EN_DYN_ACK));
	writeRegister(DYNPD, 0x3F);
	setAddress(config.m_Address, config.m_AddressLength);
	setChannel(config.m_Channel);
	clearReadFifo();
	clearWriteFifo();
	m_NumRetries = config.m_McuRetries;
	startListening();
	return readRegister(RF_SETUP) == config.m_Setup;
}
uint8_t Radio::beginCommand(uint8_t cmd)
{
	SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
	digitalWrite(m_CsnPin, LOW);
	delayMicroseconds(5);
	return SPI.transfer(cmd);
}
void Radio::endCommand()
{
	SPI.endTransaction();
	digitalWrite(m_CsnPin, HIGH);
	delayMicroseconds(5);
}
uint8_t Radio::command(uint8_t cmd, uint8_t data) 
{ 
	beginCommand(cmd);
	data = SPI.transfer(data);
	endCommand();
	return data;
}
uint8_t Radio::commandLong(uint8_t cmd, const void* data, uint8_t count) 
{ 
	const uint8_t* u8data = (const uint8_t*) data;
	uint8_t result = beginCommand(cmd);
	do {
		result = SPI.transfer(*u8data++);
	} while (--count);
	endCommand();
	return result;
}
rx_return Radio::read(void* dstbuf) 
{
	uint8_t packetSize = command(R_RX_PL_WID);
	rx_return ret;
	ret.packetsize = packetSize;
	ret.packetend = (uint8_t*) dstbuf;
	beginCommand(R_RX_PAYLOAD);
	do {
		*ret.packetend++ = SPI.transfer(0xFF);
	} while (--packetSize);
	endCommand();
	return ret;
}
void Radio::readRegister(uint8_t reg, void* result, uint8_t size)
{
	uint8_t* u8result = static_cast<uint8_t*>(result);
	beginCommand(R_REGISTER | reg);
	do {
		*u8result++ = SPI.transfer(0xFF);
	} while (--size);
	endCommand();
}
void Radio::startListening(uint8_t pipes) 
{ 
	writeRegister(EN_RXADDR, pipes);
	writeRegister(CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP) | (1 << PRIM_RX));
}
void Radio::stopListening() 
{
	writeRegister(EN_RXADDR, _BV(0));
	writeRegister(CONFIG, (1 << MASK_RX_DR) | (1 << MASK_TX_DS) | (1 << MASK_MAX_RT) | (1 << CRCO) | (1 << EN_CRC) | (1 << PWR_UP));
}
#endif

#if !DISABLE_MTNB_STATS
void Radio::resetStats()
{
	m_SendCount = m_ResendCount = 0;
}
#endif

void Radio::setAddress(const void* address, uint8_t addressLength)
{
	writeRegister(TX_ADDR, address, addressLength);
	writeRegister(RX_ADDR_P0, address, addressLength);
	writeRegister(RX_ADDR_P1, address, addressLength);
}
void Radio::setChannel(uint8_t channel)
{
	writeRegister(RF_CH, channel);
}
void Radio::setBitRate(BitRate bitrate)
{
	writeRegister(RF_SETUP, (readRegister(RF_SETUP) & ~(_BV(RF_DR_LOW) | _BV(RF_DR_HIGH))) | bitrate);
}
uint8_t Radio::getChannel()
{
	return readRegister(RF_CH);
}
BitRate Radio::getBitRate()
{
	return static_cast<BitRate>(readRegister(RF_SETUP) & (_BV(RF_DR_LOW) | _BV(RF_DR_HIGH)));
}
void Radio::setRetries(uint8_t delay, uint8_t nrfRetries, uint8_t mcuRetries)
{
	writeRegister(SETUP_RETR, (delay << 4) | nrfRetries);
	m_NumRetries = mcuRetries;
}

void Radio::openWritingPipe(uint8_t address)
{
	writeRegister(TX_ADDR, address);
	writeRegister(RX_ADDR_P0, address);
}
void Radio::openReadingPipe(uint8_t address, uint8_t pipe)
{
	writeRegister(RX_ADDR_P0 + pipe, address);
}

bool Radio::write(const void* data, uint8_t len)
{
	if (!flush(false))
		return false;
    writeImmediate(data, len);
    return true;
}

bool Radio::writeLong(const void* data, uint16_t len)
{
	const uint8_t* u8data = (const uint8_t*) data;
	for(; len > 32; len -= 32, u8data += 32)
		if (!write(u8data, 32))
			return false;
	return write(u8data, len);
}

bool Radio::write(uint8_t address, const void* data, uint16_t len)
{
	uint8_t pipes = readRegister(EN_RXADDR);
	bootPoll();
	powerDown();
	clearWriteFifo();
	openWritingPipe(address);
	stopListening();
	delay(5);
	bool result = writeLong(data, len) && flush();
	powerDown();
	startListening(pipes);
	return result;
}

bool Radio::flush(bool entireTxFifo)
{
	uint8_t retries = m_NumRetries;
	for (;;)
	{
		if (writeCompleted())
		{
#if !DISABLE_MTNB_STATS
#if COUNT_ALL_RESENDS
			if (m_DataPending)
				m_ResendCount += readRegister(OBSERVE_TX) & 15;
			m_DataPending = !entireTxFifo;
#endif
#endif
			return true;
		}
		uint8_t s = status();
		if (s & _BV(MAX_RT))
		{
			writeRegister(STATUS_NRF, _BV(MAX_RT));
#if !DISABLEMILLIS
			if (retries > 0)
			{
				--retries;
#if !DISABLE_MTNB_STATS
				++m_ResendCount;
#endif
				ce(LOW);
				delay(1);
				ce(HIGH);
			}
			else
#endif
			{
				clearWriteFifo();
				return false;
			}
		}
#if !COUNT_ALL_RESENDS
		if (!entireTxFifo && !(s & _BV(TX_FULL)))
			return true;
#endif
	}
}

} // namespace mtnrf
