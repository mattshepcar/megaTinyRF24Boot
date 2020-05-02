#pragma once

#include <Arduino.h>
#include "nRF24L01.h"

namespace mtnrf {

typedef struct { uint8_t packetsize; uint8_t* packetend; } rx_return;
class Config;
enum BitRate
{
    RF24_250KBPS = _BV(RF_DR_LOW),
    RF24_1MBPS = 0,
    RF24_2MBPS = _BV(RF_DR_HIGH),
};
enum PowerLevel 
{
    RF24_PA_MIN = 0,
    RF24_PA_LOW = _BV(RF_PWR_LOW),
    RF24_PA_HIGH = _BV(RF_PWR_HIGH),
    RF24_PA_MAX = _BV(RF_PWR_LOW) | _BV(RF_PWR_HIGH),
};

// lightweight nRF24L01+ radio interface
class Radio
{
public:
    // construct radio instance using specified chip enable and chip select pins
    Radio(uint8_t cePin, uint8_t csnPin);

    ///////////////////////////////////////////////////////////////////////////
    // config

    // initialise radio (not necessary in bootloader enabled app)
    bool begin(const Config& config);
    // set radio address
    void setAddress(const void* address, uint8_t addressLength);
    // set radio channel (0-127)
    void setChannel(uint8_t channel);
    // set radio bitrate (RF24_250KBPS, RF24_1MBPS or RF24_2MBPS)
    void setBitRate(BitRate bitrate);
    // get radio channel (0-127)
    uint8_t getChannel();
    // get radio bitrate (RF24_250KBPS, RF24_1MBPS or RF24_2MBPS)
    BitRate getBitRate();
    // set retry parameters
    void setRetries(uint8_t delay, uint8_t nrfRetries, uint8_t mcuRetries);

    ///////////////////////////////////////////////////////////////////////////
    // receive mode

    // set least significant byte of radio address on specified read pipe
    void openReadingPipe(uint8_t address, uint8_t pipe = 1);
    // start radio listening on specified pipes
    void startListening(uint8_t pipes = _BV(1) | _BV(5));
    // poll for packets on pipe 5 and reset into bootloader if necessary
    void bootPoll();
    // has anything been received
    bool available();
    // get pipe number for incoming packet (or 7 if no packet available)
    uint8_t readPipe();
    // read incoming data
    rx_return read(void* dstbuf);
    // write payload to be sent back after the next packet is received
    void writeAckPayload(const void* data, uint8_t size, uint8_t pipe = 1);

    ///////////////////////////////////////////////////////////////////////////
    // transmit mode

    // set write least significant byte of radio write address
    void openWritingPipe(uint8_t address);
    // set radio into transmit mode
    void stopListening();
    // is the radio ready to accept data
    bool availableForWrite();
    // write single packet (blocks if FIFO full, returns false if max retries exceeded)
    bool write(const void* data, uint8_t size);
    // write a typed packet
    template<class T>
    bool write(const T& data) { return write(&data, sizeof(data)); }
    // write multiple packets
    bool writeLong(const void* data, uint16_t len);
    // flush pending writes and return send success status
    bool flush(bool entireTxFifo = true);
    // returns true when transmit buffer has emptied
    bool writeCompleted();
    // returns true if transmit failed (remote radio did not acknowledge)
    bool writeFailed();

    ///////////////////////////////////////////////////////////////////////////
    // utility functions

    // switch to TX mode, write data to specified pipe then switch back to RX mode
    bool write(uint8_t address, const void* data, uint16_t len);
    // switch to TX mode, write string to specified pipe then switch back to RX mode
    void write(uint8_t address, const char* str);

#if !DISABLE_MTNB_STATS
    ///////////////////////////////////////////////////////////////////////////
    // stats
    
    // reset counters
    void resetStats();
    // get sent packet count
    int getSendCount();
    // get resend count
    int getResendCount();
#endif

    ///////////////////////////////////////////////////////////////////////////
    // low level functions

    void    ce(uint8_t state);
    void    writeImmediate(const void* data, uint8_t size);
    void    powerDown();
    uint8_t status();
    uint8_t readRegister(uint8_t reg);
#if !MEGA_TINY_NRF24_BOOT
    void    readRegister(uint8_t reg, void* result, uint8_t size);
    uint8_t beginCommand(uint8_t command);
    void    endCommand();
#endif
    void    writeRegister(uint8_t reg, uint8_t data);
    void    writeRegister(uint8_t reg, const void* data, uint8_t size);
    uint8_t command(uint8_t cmd);
    uint8_t command(uint8_t cmd, uint8_t data);
    uint8_t commandLong(uint8_t cmd, const void* data, uint8_t size);
    void    clearReadFifo();
    void    clearWriteFifo();
    void    clearWriteFailed();

private:

    uint8_t m_NumRetries;
    uint8_t m_CsnPin;
    uint8_t m_CePin;

#if !DISABLE_MTNB_STATS
#if COUNT_ALL_RESENDS
    bool m_DataPending = false;
#endif
    int m_SendCount = 0;
    int m_ResendCount = 0;
#endif
};

// nRF24L01+ radio configuration parameters
class Config
{
public:
    Config(const void* address, uint8_t addressLength,
        uint8_t channel = 50, BitRate bitrate = RF24_2MBPS, PowerLevel powerLevel = RF24_PA_MAX,
        uint8_t retryDelay = 0, uint8_t nrfRetries = 15, uint8_t mcuRetries = 0)
    {
        m_Address = address;
        m_AddressLength = addressLength;
        m_Channel = channel;
        m_Setup = bitrate | powerLevel;
        setRetries(retryDelay, nrfRetries, mcuRetries);
    }
    void setChannel(uint8_t channel) { m_Channel = channel; }
    void setPowerLevel(PowerLevel level) { m_Setup = (m_Setup & ~RF24_PA_MAX) | level; }
    void setBitRate(BitRate bitrate) { m_Setup = (m_Setup & ~(_BV(RF_DR_LOW) | _BV(RF_DR_HIGH))) | bitrate; }
    void setRetries(uint8_t delay, uint8_t nrfRetries, uint8_t mcuRetries)
    {
        m_NrfRetries = (delay << 4) | nrfRetries;
        m_McuRetries = mcuRetries;
    }

private:
    friend class Radio;

    const void* m_Address;
    uint8_t m_AddressLength;
    uint8_t m_Setup;
    uint8_t m_Channel;
    uint8_t m_NrfRetries;
    uint8_t m_McuRetries;
};

///////////////////////////////////////////////////////////////////////////////
// inlines

inline Radio::Radio(uint8_t cePin, uint8_t csnPin)
{    
    m_NumRetries = 0;
    m_CePin = cePin;
    m_CsnPin = csnPin;
}
inline void Radio::writeRegister(uint8_t reg, uint8_t data)
{
    command(reg | W_REGISTER, data); 
}
inline void Radio::writeRegister(uint8_t reg, const void* data, uint8_t size)
{
    commandLong(reg | W_REGISTER, data, size);
}
inline uint8_t Radio::readRegister(uint8_t reg)
{
    return command(reg, 0); 
}
inline void Radio::writeImmediate(const void* data, uint8_t len)
{
#if !DISABLE_MTNB_STATS
    ++m_SendCount;
#endif
    commandLong(W_TX_PAYLOAD, data, len);
}
inline void Radio::writeAckPayload(const void* data, uint8_t len, uint8_t pipe)
{
    commandLong(W_ACK_PAYLOAD + pipe, data, len);
}
inline void Radio::powerDown()
{
    writeRegister(CONFIG, 0);
}
inline bool Radio::writeCompleted()
{ 
    return (readRegister(FIFO_STATUS) & _BV(TX_EMPTY)) != 0; 
}
inline bool Radio::availableForWrite()
{ 
    return (status() & _BV(TX_FULL)) == 0; 
}
inline bool Radio::writeFailed()
{
    return (status() & _BV(MAX_RT)) != 0;
}
inline void Radio::clearWriteFailed()
{
    writeRegister(STATUS_NRF, _BV(MAX_RT));
}
inline uint8_t Radio::readPipe()
{
    return (status() & 0x0E) >> 1;
}
inline bool Radio::available()
{
    return (status() & 0x0E) != 0x0E;
}
inline void Radio::clearWriteFifo() 
{ 
    command(FLUSH_TX); 
}
inline void Radio::clearReadFifo()
{
    command(FLUSH_RX);
}
inline void Radio::write(uint8_t address, const char* str)
{
    write(address, str, strlen(str));
}
#if !DISABLE_MTNB_STATS
inline int Radio::getSendCount()
{
    return m_SendCount;
}
inline int Radio::getResendCount()
{
    return m_ResendCount;
}
#endif
inline void Radio::ce(uint8_t state)
{
    digitalWrite(m_CePin, state ? HIGH : LOW);
}
#if !MEGA_TINY_NRF24_BOOT
inline uint8_t Radio::status()
{
    return readRegister(STATUS_NRF);
}
inline void Radio::bootPoll()
{
    // no bootloader present
}
inline uint8_t Radio::command(uint8_t cmd)
{
    return command(cmd, NOP);
}
#else
// these functions are provided by the bootloader:
extern "C" {
void nrf24_boot_poll();
uint8_t nrf24_status();
uint8_t nrf24_command(uint8_t cmd);
uint8_t nrf24_command_data(uint8_t cmd, uint8_t data);
uint8_t nrf24_command_long(uint8_t cmd, const void* data, uint8_t count);
rx_return nrf24_read_payload(void* dstbuf);
void nrf24_begin_tx();
void nrf24_begin_rx(uint8_t pipeBits);
} // extern "C"
inline void Radio::bootPoll() { nrf24_boot_poll(); }
inline uint8_t Radio::status() { return nrf24_status(); }
inline uint8_t Radio::command(uint8_t cmd) { return nrf24_command(cmd); }
inline uint8_t Radio::command(uint8_t cmd, uint8_t data) { return nrf24_command_data(cmd, data); }
inline uint8_t Radio::commandLong(uint8_t cmd, const void* data, uint8_t count) { return nrf24_command_long(cmd, data, count); }
inline rx_return Radio::read(void* dstbuf) { return nrf24_read_payload(dstbuf); }
inline void Radio::startListening(uint8_t pipes) { nrf24_begin_rx(pipes); }
inline void Radio::stopListening() { nrf24_begin_tx(); }
#endif

} // namespace mtnrf
