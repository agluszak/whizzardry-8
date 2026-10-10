#pragma once

#include <SDL3/SDL.h>
#include "srMath.h"
#include "srQuadWord.h"

#include <bit>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

// SDL owns cursors, memory/file backends and stream status. These helpers only
// describe serialized bytes; no C++ object layout is written to disk.
namespace srIO {
struct CloseIO { void operator()(SDL_IOStream* io) const noexcept { SDL_CloseIO(io); } };
using IO = std::unique_ptr<SDL_IOStream, CloseIO>;
inline IO own(SDL_IOStream* io)
{
    if (!io) throw std::runtime_error(SDL_GetError());
    return IO(io);
}
enum class ByteOrder { little, big };
inline void readExact(SDL_IOStream* io, void* data, size_t bytes)
{
    if (!bytes) return;
    if (SDL_ReadIO(io, data, bytes) != bytes || SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR)
        throw std::runtime_error(std::string("Short binary read: ") + SDL_GetError());
}
inline void writeExact(SDL_IOStream* io, const void* data, size_t bytes)
{
    if (!bytes) return;
    if (SDL_WriteIO(io, data, bytes) != bytes || SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR)
        throw std::runtime_error(std::string("Short binary write: ") + SDL_GetError());
}
template<class T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
T readInteger(SDL_IOStream* io, ByteOrder order = ByteOrder::little)
{
    using U = std::make_unsigned_t<T>;
    Uint8 bytes[sizeof(T)];
    readExact(io, bytes, sizeof(bytes));
    U value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= U(bytes[i]) << (8 * (order == ByteOrder::little ? i : sizeof(T) - 1 - i));
    return std::bit_cast<T>(value);
}
template<class T> requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
void writeInteger(SDL_IOStream* io, T value, ByteOrder order = ByteOrder::little)
{
    using U = std::make_unsigned_t<T>;
    const U bits = std::bit_cast<U>(value);
    Uint8 bytes[sizeof(T)];
    for (size_t i = 0; i < sizeof(T); ++i)
        bytes[i] = Uint8(bits >> (8 * (order == ByteOrder::little ? i : sizeof(T) - 1 - i)));
    writeExact(io, bytes, sizeof(bytes));
}
inline float readFloat(SDL_IOStream* io, ByteOrder order = ByteOrder::little)
{ return std::bit_cast<float>(readInteger<Uint32>(io, order)); }
inline double readDouble(SDL_IOStream* io, ByteOrder order = ByteOrder::little)
{ return std::bit_cast<double>(readInteger<Uint64>(io, order)); }
inline void writeFloat(SDL_IOStream* io, float value, ByteOrder order = ByteOrder::little)
{ writeInteger(io, std::bit_cast<Uint32>(value), order); }
inline void writeDouble(SDL_IOStream* io, double value, ByteOrder order = ByteOrder::little)
{ writeInteger(io, std::bit_cast<Uint64>(value), order); }
template<class T> requires std::is_integral_v<T>
void readValue(SDL_IOStream* io, T& value, ByteOrder order = ByteOrder::little)
{ value = readInteger<T>(io, order); }
inline void readValue(SDL_IOStream* io, float& value, ByteOrder order = ByteOrder::little)
{ value = readFloat(io, order); }
inline void readValue(SDL_IOStream* io, double& value, ByteOrder order = ByteOrder::little)
{ value = readDouble(io, order); }
template<class T> requires std::is_integral_v<T>
void writeValue(SDL_IOStream* io, T value, ByteOrder order = ByteOrder::little)
{ writeInteger(io, value, order); }
inline void writeValue(SDL_IOStream* io, float value, ByteOrder order = ByteOrder::little)
{ writeFloat(io, value, order); }
inline void writeValue(SDL_IOStream* io, double value, ByteOrder order = ByteOrder::little)
{ writeDouble(io, value, order); }
inline void readValue(SDL_IOStream* io, srQuadWord& value, ByteOrder order = ByteOrder::little)
{
    const auto bits = readInteger<Uint64>(io, order);
    value = {static_cast<w8_ulong>(bits), static_cast<w8_ulong>(bits >> 32)};
}
inline void writeQuadWord(SDL_IOStream* io, srQuadWord value, ByteOrder order = ByteOrder::little)
{ writeInteger(io, Uint64(value.lo) | (Uint64(value.hi) << 32), order); }
template<class T>
inline void readValue(SDL_IOStream* io, srVector2T<T>& value, ByteOrder order = ByteOrder::little)
{
    readValue(io, value.x, order);
    readValue(io, value.y, order);
}
inline void readValue(SDL_IOStream* io, srVector2i& value, ByteOrder order = ByteOrder::little)
{
    readValue(io, value.x, order);
    readValue(io, value.y, order);
}
template<class T>
inline void readValue(SDL_IOStream* io, srVector3T<T>& value, ByteOrder order = ByteOrder::little)
{
    readValue(io, value.x, order);
    readValue(io, value.y, order);
    readValue(io, value.z, order);
}
inline void readValue(SDL_IOStream* io, srVector3i& value, ByteOrder order = ByteOrder::little)
{
    readValue(io, value.x, order);
    readValue(io, value.y, order);
    readValue(io, value.z, order);
}
template<class T>
inline void readValue(SDL_IOStream* io, srVector4T<T>& value, ByteOrder order = ByteOrder::little)
{
    readValue(io, value.x, order);
    readValue(io, value.y, order);
    readValue(io, value.z, order);
    readValue(io, value.w, order);
}
inline void readValue(SDL_IOStream* io, srVector4i& value, ByteOrder order = ByteOrder::little)
{
    readValue(io, value.x, order);
    readValue(io, value.y, order);
    readValue(io, value.z, order);
    readValue(io, value.w, order);
}
inline void readValue(SDL_IOStream* io, srQuaternion& value, ByteOrder order = ByteOrder::little)
{ readValue(io, value.w, order); readValue(io, value.v, order); }
template<class T>
inline void readValue(SDL_IOStream* io, srMatrix2T<T>& value, ByteOrder order = ByteOrder::little)
{ for (auto& vector : value.vectors) readValue(io, vector, order); }
template<class T>
inline void readValue(SDL_IOStream* io, srMatrix3T<T>& value, ByteOrder order = ByteOrder::little)
{ for (auto& vector : value.vectors) readValue(io, vector, order); }
template<class T>
inline void readValue(SDL_IOStream* io, srMatrix4T<T>& value, ByteOrder order = ByteOrder::little)
{ for (auto& vector : value.vectors) readValue(io, vector, order); }
template<class T>
inline void writeValue(SDL_IOStream* io, const srVector2T<T>& value, ByteOrder order = ByteOrder::little)
{
    writeValue(io, value.x, order);
    writeValue(io, value.y, order);
}
inline void writeValue(SDL_IOStream* io, const srVector2i& value, ByteOrder order = ByteOrder::little)
{
    writeValue(io, value.x, order);
    writeValue(io, value.y, order);
}
template<class T>
inline void writeValue(SDL_IOStream* io, const srVector3T<T>& value, ByteOrder order = ByteOrder::little)
{
    writeValue(io, value.x, order);
    writeValue(io, value.y, order);
    writeValue(io, value.z, order);
}
inline void writeValue(SDL_IOStream* io, const srVector3i& value, ByteOrder order = ByteOrder::little)
{
    writeValue(io, value.x, order);
    writeValue(io, value.y, order);
    writeValue(io, value.z, order);
}
template<class T>
inline void writeValue(SDL_IOStream* io, const srVector4T<T>& value, ByteOrder order = ByteOrder::little)
{
    writeValue(io, value.x, order);
    writeValue(io, value.y, order);
    writeValue(io, value.z, order);
    writeValue(io, value.w, order);
}
inline void writeValue(SDL_IOStream* io, const srVector4i& value, ByteOrder order = ByteOrder::little)
{
    writeValue(io, value.x, order);
    writeValue(io, value.y, order);
    writeValue(io, value.z, order);
    writeValue(io, value.w, order);
}
inline void writeValue(SDL_IOStream* io, const srQuaternion& value, ByteOrder order = ByteOrder::little)
{ writeValue(io, value.w, order); writeValue(io, value.v, order); }
template<class T>
inline void writeValue(SDL_IOStream* io, const srMatrix2T<T>& value, ByteOrder order = ByteOrder::little)
{ for (const auto& vector : value.vectors) writeValue(io, vector, order); }
template<class T>
inline void writeValue(SDL_IOStream* io, const srMatrix3T<T>& value, ByteOrder order = ByteOrder::little)
{ for (const auto& vector : value.vectors) writeValue(io, vector, order); }
template<class T>
inline void writeValue(SDL_IOStream* io, const srMatrix4T<T>& value, ByteOrder order = ByteOrder::little)
{ for (const auto& vector : value.vectors) writeValue(io, vector, order); }
} // namespace srIO
