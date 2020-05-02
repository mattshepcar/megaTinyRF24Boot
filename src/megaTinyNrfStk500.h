#pragma once

#include <stdint.h>

class Stream;

namespace mtnrf {

class BootLoader;

// handle STK500v2 programming protocol (from avrdude etc)
class Stk500
{
public:
    Stk500(BootLoader& device);

    void begin(Stream& stream);

    // returns true when programming completed or operation timed out
    bool handle();

private:
    int getch();
    bool endCommand();

    Stream* m_Stream;
    BootLoader& m_Device;
    uint16_t m_CommandStartTime;
    uint16_t m_LastCommandTime;
    uint16_t m_ProgramAddress;
    bool m_ValidCommand;
    bool m_Success;
};

} // namespace mtnrf
