#pragma once

#include "nRF24L01.h"

extern "C" {
    // reset into the bootloader if any programming packets are received (also resets watchdog timer)
    void nrf24_boot_poll();

    // retrieve the value of the radio's status register
    uint8_t nrf24_status();

    // send a two byte command to the radio
    uint8_t nrf24_command(uint8_t cmd, uint8_t data = 0xFF);

    // send a multibyte command to the radio
    uint8_t nrf24_command_long(uint8_t cmd, const uint8_t* data, uint8_t count);

    // read the payload, update pointer and return packet size
    struct rx_return { uint8_t* packetend; uint8_t packetsize; };
    rx_return nrf24_read_payload(uint8_t* dstbuf);

    // power up the radio in TX mode
    void nrf24_begin_tx();

    // power up the radio in RX mode listening on specified pipes (bitmask)
    void nrf24_begin_rx(uint8_t pipeBits);
}

// get pipe number for packet in RX FIFO (or 7 if no packet in FIFO)
uint8_t nrf24_rx_pipe_no();

// check if anything in the RX FIFO
bool nrf24_rx_available();

// check if there's room in the TX FIFO
bool nrf24_tx_available();

// write payload to TX FIFO (blocks if full, returns false if max retries exceeded)
static bool nrf24_write_fast(const uint8_t* data, uint8_t len);

// wait for TX FIFO to empty (returns false if max retries exceeded)
static bool nrf24_tx_standby();

// write ack payload
void nrf24_write_ack_payload(uint8_t pipe, const uint8_t* data, uint8_t count);

// switch to TX mode, write data to UART pipe then switch back to RX mode
static void nrf24_write_uart(const void* data, uint16_t len);
void nrf24_write_uart(const char* str);

/////////////////////////////////////////////////////////////////////////////////////

inline void nrf24_write_register(uint8_t reg, uint8_t data) 
{
    nrf24_command(reg | W_REGISTER, data); 
}
inline uint8_t nrf24_read_register(uint8_t reg) 
{
    return nrf24_command(reg, 0); 
}
inline void nrf24_write_payload(const uint8_t* data, uint8_t len)
{
    nrf24_command_long(W_TX_PAYLOAD, data, len);
}
inline void nrf24_write_ack_payload(uint8_t pipe, const uint8_t* data, uint8_t count)
{
    nrf24_command_long(W_ACK_PAYLOAD | pipe, data, count);
}
inline void nrf24_power_down() 
{
    nrf24_write_register(CONFIG, 0);
}
inline bool nrf24_tx_fifo_empty() 
{ 
    return (nrf24_read_register(FIFO_STATUS) & _BV(TX_EMPTY)) != 0; 
}
inline bool nrf24_tx_fifo_full() 
{ 
    return (nrf24_status() & _BV(TX_FULL)) != 0; 
}
inline bool nrf24_rx_available()
{
    return nrf24_rx_pipe_no() < 6;
}
inline bool nrf24_tx_available()
{
    return !nrf24_tx_fifo_full();
}
inline void nrf24_flush_tx() 
{ 
    nrf24_command(FLUSH_TX); 
}
inline void nrf24_flush_rx()
{
    nrf24_command(FLUSH_RX);
}
inline uint8_t nrf24_get_rx_pipe_no()
{
    return (nrf24_status() & 0xE) >> 1;
}

void nrf24_begin_tx(uint8_t channel)
{
    nrf24_boot_poll();
    nrf24_power_down();
    nrf24_write_register(TX_ADDR, channel);
    nrf24_write_register(RX_ADDR_P0, channel);
    nrf24_begin_tx();
    delay(5);
}

static bool nrf24_reset_tx()
{
    nrf24_write_register(STATUS_NRF, _BV(MAX_RT));
    nrf24_flush_tx();
    return false;
}

static bool nrf24_write_fast(const uint8_t* data, uint8_t len)
{
    for (;;)
    {
        uint8_t status = nrf24_status();
        if (!(status & _BV(TX_FULL)))
            break;
        if (status & _BV(MAX_RT))
            return nrf24_reset_tx();
    }
    nrf24_write_payload(data, len);
    return true;
}

static bool nrf24_tx_standby()
{
    while (!nrf24_tx_fifo_empty())
    {
        uint8_t status = nrf24_status();
        if (status & _BV(MAX_RT))
            return nrf24_reset_tx();
    }
    return true;
}

static void nrf24_write_uart(const void* data, uint16_t len)
{
    nrf24_flush_tx();
    nrf24_begin_tx('U');
    const uint8_t* addr = (const uint8_t*) data;
    for (uint8_t i = 0; i < len;)
    {
        uint8_t packetlen = min(32u, len);
        nrf24_write_fast(&addr[i], packetlen);
        i += packetlen;
    }
    nrf24_tx_standby();
    nrf24_power_down();
    nrf24_begin_rx(_BV(5));
}

inline void nrf24_write_uart(const char* str)
{
    nrf24_write_uart(str, strlen(str));
}
