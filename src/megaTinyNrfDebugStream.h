#pragma once

#include <Arduino.h>

namespace mtnrf {

class DebugStream : public Stream
{
public:
    int available() override { return false; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t c) override { m_String += (char) c; return 1; };
    void flush(Stream& target) { target.print(m_String); clear(); }
    void clear() { m_String = ""; }
    String m_String;
};

} // namespace mtnrf
