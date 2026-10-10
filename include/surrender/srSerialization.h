#pragma once

#include "srIO.h"
#include "srQuadWord.h"
#include <bit>

// Only formats SDL does not represent directly. Integers use SDL_Read/Write*.
namespace srSerialization {
inline float readFloatLE(SDL_IOStream* io)
{
    Uint32 bits;
    srIO::check(SDL_ReadU32LE(io, &bits));
    return std::bit_cast<float>(bits);
}
inline void writeFloatLE(SDL_IOStream* io, float value)
{ srIO::check(SDL_WriteU32LE(io, std::bit_cast<Uint32>(value))); }
inline float readFloatBE(SDL_IOStream* io)
{
    Uint32 bits;
    srIO::check(SDL_ReadU32BE(io, &bits));
    return std::bit_cast<float>(bits);
}
inline void writeFloatBE(SDL_IOStream* io, float value)
{ srIO::check(SDL_WriteU32BE(io, std::bit_cast<Uint32>(value))); }
inline double readDoubleLE(SDL_IOStream* io)
{
    Uint64 bits;
    srIO::check(SDL_ReadU64LE(io, &bits));
    return std::bit_cast<double>(bits);
}
inline void writeDoubleLE(SDL_IOStream* io, double value)
{ srIO::check(SDL_WriteU64LE(io, std::bit_cast<Uint64>(value))); }
inline double readDoubleBE(SDL_IOStream* io)
{
    Uint64 bits;
    srIO::check(SDL_ReadU64BE(io, &bits));
    return std::bit_cast<double>(bits);
}
inline void writeDoubleBE(SDL_IOStream* io, double value)
{ srIO::check(SDL_WriteU64BE(io, std::bit_cast<Uint64>(value))); }
inline srQuadWord readQuadWordLE(SDL_IOStream* io)
{
    Uint64 bits;
    srIO::check(SDL_ReadU64LE(io, &bits));
    return {Uint32(bits), Uint32(bits >> 32)};
}
inline void writeQuadWordLE(SDL_IOStream* io, srQuadWord value)
{ srIO::check(SDL_WriteU64LE(io, Uint64(value.lo) | (Uint64(value.hi) << 32))); }
inline srQuadWord readQuadWordBE(SDL_IOStream* io)
{
    Uint64 bits;
    srIO::check(SDL_ReadU64BE(io, &bits));
    return {Uint32(bits), Uint32(bits >> 32)};
}
inline void writeQuadWordBE(SDL_IOStream* io, srQuadWord value)
{ srIO::check(SDL_WriteU64BE(io, Uint64(value.lo) | (Uint64(value.hi) << 32))); }
} // namespace srSerialization
