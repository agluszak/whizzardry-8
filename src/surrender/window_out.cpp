#include <cstdint>
#include <iostream>

#include "surrender/srWindowOut.h"

/* The Windows build opens a RichEdit diagnostic console; natively the stream
   writes to standard error. */
srWindowOut::srWindowOut(std::uint32_t, const char*, std::int32_t, std::uint32_t)
    : std::ostream(std::cerr.rdbuf())
{
}

srWindowOut::~srWindowOut() = default;
