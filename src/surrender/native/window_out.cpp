#include "surrender/srWindowOut.h"

#include <iostream>

/* The Windows build opens a RichEdit diagnostic console; natively the stream
   writes to standard error. */
srWindowOut::srWindowOut(w8_ulong, const char*, w8_long, w8_ulong)
    : std::ostream(std::cerr.rdbuf())
{
}

srWindowOut::~srWindowOut() {}
