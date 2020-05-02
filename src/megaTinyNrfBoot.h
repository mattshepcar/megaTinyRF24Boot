#pragma once

#include "megaTinyNrf24.h"

//#define DISABLE_MTNB_DEBUG 1

namespace mtnrf {

// Communicate with the megaTiny bootloader over a nRF24L01+ radio link.
class BootLoader
{
public:
    BootLoader(Radio& radio, Stream* debuglog = nullptr);

    // get the radio instance
    Radio& getRadio();
    // set stream to write debug messages to
    void setDebugStream(Stream* debugStream);
    // reset the remote device into bootloader mode. returns true is successful
    bool enterBootLoader();
    // tell the remote device to leave the bootloader and run the application
    bool exitBootLoader();
    // read the 3 byte device signature to determine the chip type
    bool readDeviceSignature(uint8_t* signature = nullptr);
    // get remote device's flash size in bytes (must call readDeviceSignature first)
    uint16_t getFlashSize() const;
    // get remote device's flash page size in bytes (must call readDeviceSignature first)
    uint8_t getFlashPageSize() const;
    // send a packet to the remote radio programming pipe and return true if it was received
    bool sendSyncPacket();
    // send a packet every 250ms to prevent the remote device from timing out of bootloader mode
    void keepAlive(uint16_t currentMillisValue);
    // write to a single page of device memory
    bool writeMemory(uint16_t address, const void* data, uint8_t length);
    // write multiple pages of device memory
    bool writeMemoryLong(uint16_t address, const void* data, uint16_t length);
    // write a single byte of device memory
    bool writeMemory(uint16_t address, uint8_t value);
    // wait for any EEPROM writes to complete (flash writes always complete immediately)
    bool waitForEepromWrites();
    // flush any pending radio commands
    bool flushWrites();
    // check if the remote device's flash CRC is correct
    bool performCrcCheck();
    // write to remote device memory and then return the byte in the address following the written data
    int16_t writeAndReadMemory(uint16_t address, const void* data, uint8_t len, uint8_t retries = 16);
    // write only a single byte and return byte from next address
    int16_t writeAndReadMemory(uint16_t address, uint8_t value, uint8_t retries = 16);
    
    // temporarily change the remote device's radio settings and reestablish a connection (erases flash!)
    bool changeRadioSettings(uint8_t channel, BitRate bitrate);
    // permanently reprogram the remote device's radio address
    bool reprogramAddress(const char* addr);
    // permanently reprogram the remote device's radio channel
    bool reprogramChannel(uint8_t channel);

    // log current radio address information to debug stream
    void printAddresses();

private:
    Radio& m_Radio;
#if !DISABLE_MTNB_DEBUG
    Stream* m_DebugLog;
#endif
    uint8_t m_FlashSize = 0;
    uint16_t m_LastKeepAlive = 0;
};

inline Radio& BootLoader::getRadio()
{
    return m_Radio;
}
inline uint16_t BootLoader::getFlashSize() const
{
    return 0x400 << m_FlashSize;
}
inline uint8_t BootLoader::getFlashPageSize() const
{
    return m_FlashSize >= 5 ? 0x80 : 0x40;
}
inline void BootLoader::setDebugStream(Stream* debugStream)
{
#if !DISABLE_MTNB_DEBUG
    m_DebugLog = debugStream;
#endif
}

} // namespace mtnrf
