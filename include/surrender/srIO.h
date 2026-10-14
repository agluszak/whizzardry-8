#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>

// Ownership and checked transfers only. Scalar byte order belongs to SDL.
namespace srIO {
struct CloseIO {
    void operator()(SDL_IOStream* io) const noexcept { SDL_CloseIO(io); }
};
using IO = std::unique_ptr<SDL_IOStream, CloseIO>;
inline void check(bool succeeded)
{
    if (!succeeded) throw std::runtime_error(SDL_GetError());
}
inline IO own(SDL_IOStream* io)
{
    check(io != nullptr);
    return IO(io);
}
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
} // namespace srIO
