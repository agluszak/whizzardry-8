#pragma once

#include <ios>

/* The recovered dump code manipulates stream flags with the VC6 <ios> bit
   values.  Natively those bits are translated to the library's fmtflags. */
namespace srStreamFlagsDetail {
struct Bit {
    w8_long msvc;
    std::ios_base::fmtflags native;
};

static const Bit bits[] = {
    {0x0001, std::ios_base::skipws},   {0x0002, std::ios_base::unitbuf},
    {0x0004, std::ios_base::uppercase}, {0x0008, std::ios_base::showbase},
    {0x0010, std::ios_base::showpoint}, {0x0020, std::ios_base::showpos},
    {0x0040, std::ios_base::left},     {0x0080, std::ios_base::right},
    {0x0100, std::ios_base::internal}, {0x0200, std::ios_base::dec},
    {0x0400, std::ios_base::oct},      {0x0800, std::ios_base::hex},
    {0x1000, std::ios_base::scientific}, {0x2000, std::ios_base::fixed},
    {0x4000, std::ios_base::boolalpha},
};
} // namespace srStreamFlagsDetail

inline w8_long srGetStreamFlags(const std::ios_base& stream)
{
    w8_long result = 0;
    for (const srStreamFlagsDetail::Bit& bit : srStreamFlagsDetail::bits) {
        if (stream.flags() & bit.native) {
            result |= bit.msvc;
        }
    }
    return result;
}

inline void srSetStreamFlags(std::ios_base& stream, w8_long flags)
{
    std::ios_base::fmtflags result = std::ios_base::fmtflags();
    for (const srStreamFlagsDetail::Bit& bit : srStreamFlagsDetail::bits) {
        if (flags & bit.msvc) {
            result |= bit.native;
        }
    }
    stream.flags(result);
}
