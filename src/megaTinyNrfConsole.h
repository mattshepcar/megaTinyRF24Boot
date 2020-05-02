#pragma once

#include "megaTinyNrfBoot.h"
#include "megaTinyNrfStk500.h"
#include "megaTinyNrfDebugStream.h"

namespace mtnrf {

// simple console interface for megaTinyNrf bootloader
class Console
{
public:
    Console(BootLoader& device);

    void begin(Stream& stream);
    void end();
    void handle();
    Stream* getStream();

private:
    void openUart();
    void handleUart();
    void openConfig();
    void handleConfigure();
    uint8_t matchSerialCommand(const char* seq, uint8_t len);
    void respondToStk500Sync();
    void handleStk500();

    void scanChannels();
    void outputChannels();
    void outputChannelHeader();

    BootLoader& m_Device;
    Stream* m_Stream;
    Stk500 m_Stk500;
    bool m_AllowStk500Debug;
    DebugStream m_Debug;

    enum eMode
    {
        MODE_UART,
        MODE_STK500,
        MODE_CONFIGURE,
    };
    eMode m_Mode;
    String m_SerialBuf;
    uint16_t m_LastSendTime;
    bool m_Scanning;
    uint8_t m_ScanLine;
    uint8_t m_ScanNo;
    static const int CHANNELS = 64;
    uint8_t m_Channel[CHANNELS];
};

inline Stream* Console::getStream() { return m_Stream; }

} // namespace mtnrf
