#pragma once

#include "surrender/srBinIStream.h"
#include "surrender/srBinOStream.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <limits>
#include <memory>

namespace srImage {
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using IO = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

// The wrapper borrows its caller's stream only for the synchronous codec call.
struct Stream {
    srBinStream& source;
    srBinIStream* input = nullptr;
    srBinOStream* output = nullptr;
    bool failed = false;
    bool eof = false;

    Sint64 error() noexcept
    {
        failed = true;
        SDL_SetError("SurRender image stream operation failed");
        return -1;
    }

    static Sint64 SDLCALL size(void* cookie) noexcept
    {
        auto& self = *static_cast<Stream*>(cookie);
        try {
            const auto result = self.source.getSize();
            return self.source.good() ? result : self.error();
        } catch (...) { return self.error(); }
    }

    static Sint64 SDLCALL seek(void* cookie, Sint64 offset, SDL_IOWhence whence) noexcept
    {
        auto& self = *static_cast<Stream*>(cookie);
        try {
            const Sint64 length = size(cookie);
            const Sint64 current = self.source.tell();
            const Sint64 base = whence == SDL_IO_SEEK_SET ? 0 :
                                whence == SDL_IO_SEEK_CUR ? current :
                                whence == SDL_IO_SEEK_END ? length : -1;
            const Sint64 limit = self.input ? length : std::numeric_limits<w8_ulong>::max();
            if (self.failed || base < 0 || offset < -base || offset > limit - base)
                return self.error();
            const auto position = static_cast<w8_ulong>(base + offset);
            self.source.seek(position);
            if (!self.source.good() || self.source.tell() != position)
                return self.error();
            return position;
        } catch (...) { return self.error(); }
    }

    static size_t SDLCALL read(void* cookie, void* data, size_t bytes,
                              SDL_IOStatus* status) noexcept
    {
        auto& self = *static_cast<Stream*>(cookie);
        try {
            const Sint64 length = size(cookie);
            const Sint64 before = self.source.tell();
            if (!self.input || self.failed || before > length || !self.source.good())
                throw 0;
            const auto count = static_cast<w8_ulong>(std::min<Uint64>(bytes, length - before));
            if (count == 0) {
                self.eof = true;
                *status = SDL_IO_STATUS_EOF;
                return 0;
            }
            self.input->read(data, count);
            const Sint64 after = self.source.tell();
            if (!self.source.good() || after - before != count)
                throw 0;
            if (count < bytes)
                *status = SDL_IO_STATUS_EOF;
            return count;
        } catch (...) {
            self.error();
            *status = SDL_IO_STATUS_ERROR;
            return 0;
        }
    }

    static size_t SDLCALL write(void* cookie, const void* data, size_t bytes,
                               SDL_IOStatus* status) noexcept
    {
        auto& self = *static_cast<Stream*>(cookie);
        try {
            const auto before = self.source.tell();
            if (!self.output || self.failed || !self.source.good() ||
                bytes > std::numeric_limits<w8_ulong>::max() - before)
                throw 0;
            self.output->write(data, static_cast<w8_ulong>(bytes));
            if (!self.source.good() || self.source.tell() - before != bytes)
                throw 0;
            return bytes;
        } catch (...) {
            self.error();
            *status = SDL_IO_STATUS_ERROR;
            return 0;
        }
    }

    IO open()
    {
        SDL_IOStreamInterface interface;
        SDL_INIT_INTERFACE(&interface);
        interface.size = size;
        interface.seek = seek;
        interface.read = input ? read : nullptr;
        interface.write = output ? write : nullptr;
        return IO(SDL_OpenIO(&interface, this), SDL_CloseIO);
    }
};

inline bool copyRows(const SDL_Surface& source, srColorSurfaceIFace& destination,
                     unsigned bytes_per_pixel, bool mirror = false)
{
    const size_t row_bytes = size_t(source.w) * bytes_per_pixel;
    if (source.w <= 0 || source.h <= 0 || !source.pixels || !destination.getDataPtr() ||
        destination.getWidth() != unsigned(source.w) ||
        destination.getHeight() != unsigned(source.h) || source.pitch < 0 ||
        size_t(source.pitch) < row_bytes || destination.getPitch() < row_bytes)
        return false;
    for (int y = 0; y < source.h; ++y) {
        const auto* src = static_cast<const unsigned char*>(source.pixels) + size_t(y) * source.pitch;
        auto* dst = static_cast<unsigned char*>(destination.getDataPtr()) + size_t(y) * destination.getPitch();
        if (!mirror) {
            SDL_memcpy(dst, src, row_bytes);
        } else {
            for (int x = 0; x < source.w; ++x)
                SDL_memcpy(dst + size_t(x) * bytes_per_pixel,
                           src + size_t(source.w - 1 - x) * bytes_per_pixel, bytes_per_pixel);
        }
    }
    return true;
}
} // namespace srImage
