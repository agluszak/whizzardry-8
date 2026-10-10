#pragma once

#include "wiz8/file_time.h"
#include <cstdint>

namespace wiz8
{
// Retail SLF records, including the format's padding bytes.
struct SlfHeader
{
    char sLibName[256];
    char sPathToLibrary[256];
    std::int32_t iEntries, iUsed;
    std::uint16_t iSort, iVersion;
    std::uint8_t fContainsSubDirectories;
    std::int32_t iReserved;
};
struct SlfEntry
{
    char sFileName[256];
    std::uint32_t uiOffset, uiLength;
    std::uint8_t ubState, ubReserved;
    DiskFileTime sFileTime;
    std::uint16_t usReserved2;
};
static_assert(sizeof(SlfHeader) == 532 && offsetof(SlfHeader, iReserved) == 528);
static_assert(sizeof(SlfEntry) == 280 && offsetof(SlfEntry, sFileTime) == 268);
} // namespace wiz8
