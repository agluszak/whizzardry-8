#pragma once

#include "surrender/srBinIStream.h"
#include "surrender/srBinOStream.h"
#include "surrender/srColorSurface.h"
#include "wiz8/filesystem.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <limits>
#include <memory>

namespace srImage {
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using IO = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

// The wrapper borrows its caller's stream only for the synchronous codec call.
struct Stream {
    Stream(srBinStream& source, srBinIStream* input = nullptr, srBinOStream* output = nullptr)
        : source(&source), input(input), output(output) {}
    explicit Stream(wiz8::File& file, bool writing = false) : file(&file), writing(writing) {}

    srBinStream* source = nullptr;
    srBinIStream* input = nullptr;
    srBinOStream* output = nullptr;
    wiz8::File* file = nullptr;
    bool writing = false;
    bool failed = false;
    bool eof = false;

    Sint64 tell() { return file ? file->tell() : source->tell(); }
    bool good() const { return file ? file->is_open() : source->good(); }

    Sint64 error() noexcept
    {
        failed = true;
        SDL_SetError("Image stream operation failed");
        return -1;
    }

    static Sint64 SDLCALL size(void* cookie) noexcept
    {
        auto& self = *static_cast<Stream*>(cookie);
        try {
            const Sint64 result = self.file ? self.file->size() : self.source->getSize();
            return self.good() && result >= 0 ? result : self.error();
        } catch (...) { return self.error(); }
    }

    static Sint64 SDLCALL seek(void* cookie, Sint64 offset, SDL_IOWhence whence) noexcept
    {
        auto& self = *static_cast<Stream*>(cookie);
        try {
            const Sint64 length = size(cookie);
            const Sint64 current = self.tell();
            const Sint64 base = whence == SDL_IO_SEEK_SET ? 0 :
                                whence == SDL_IO_SEEK_CUR ? current :
                                whence == SDL_IO_SEEK_END ? length : -1;
            const Sint64 limit = self.input || (self.file && !self.writing) ? length :
                self.file ? std::numeric_limits<Sint64>::max() : std::numeric_limits<w8_ulong>::max();
            if (self.failed || base < 0 || offset < -base || offset > limit - base)
                return self.error();
            const auto position = base + offset;
            if (self.file) self.file->seek(position, wiz8::SeekOrigin::begin);
            else self.source->seek(static_cast<w8_ulong>(position));
            if (!self.good() || self.tell() != position)
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
            const Sint64 before = self.tell();
            if ((!self.input && !self.file) || self.failed || before < 0 || before > length || !self.good())
                throw 0;
            const auto count = static_cast<size_t>(std::min<Uint64>(bytes, length - before));
            if (count == 0) {
                self.eof = true;
                *status = SDL_IO_STATUS_EOF;
                return 0;
            }
            if (self.file) {
                if (self.file->read(data, count).bytes != count) throw 0;
            } else self.input->read(data, static_cast<w8_ulong>(count));
            const Sint64 after = self.tell();
            if (!self.good() || after - before != Sint64(count))
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
            const Sint64 before = self.tell();
            const Sint64 limit = self.file ? std::numeric_limits<Sint64>::max() :
                                            std::numeric_limits<w8_ulong>::max();
            if ((!self.output && !self.file) || self.failed || !self.good() || before < 0 ||
                before > limit || bytes > Uint64(limit - before))
                throw 0;
            if (self.file) self.file->write(data, bytes);
            else self.output->write(data, static_cast<w8_ulong>(bytes));
            if (!self.good() || self.tell() - before != Sint64(bytes))
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
        interface.read = input || (file && !writing) ? read : nullptr;
        interface.write = output || (file && writing) ? write : nullptr;
        return IO(SDL_OpenIO(&interface, this), SDL_CloseIO);
    }
};

srColorSurface* loadTga(SDL_IOStream* source);

inline bool copyRows(const SDL_Surface& source, srColorSurfaceIFace& destination,
                     unsigned bytes_per_pixel, bool mirror = false)
{
    const size_t row_bytes = size_t(source.w) * bytes_per_pixel;
    if (source.w <= 0 || source.h <= 0 || !source.pixels || !destination.getDataPtr() ||
        destination.getWidth() != source.w || destination.getHeight() != source.h ||
        source.pitch < 0 || size_t(source.pitch) < row_bytes ||
        size_t(destination.getPitch()) < row_bytes)
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
