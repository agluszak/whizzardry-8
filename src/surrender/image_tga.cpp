#include "image_stream.h"
#include "surrender/srImageIO.h"
#include "surrender/srCore.h"
#include "surrender/srColorSurface.h"
#include <SDL3_image/SDL_image.h>
#include <array>
#include <vector>

namespace srImage {
namespace tga {
inline unsigned word(const Uint8* data) { return data[0] | unsigned(data[1]) << 8; }

// Normalize only metadata SDL_image does not support. Pixel/RLE decoding stays in SDL_image.
struct Input {
    SDL_IOStream* source;
    std::vector<Uint8> prefix;
    Sint64 data_start, length;
    Sint64 position = 0;
    bool failed = false;

    static Sint64 SDLCALL size(void* cookie) noexcept
    {
        return static_cast<Input*>(cookie)->length;
    }
    static Sint64 SDLCALL seek(void* cookie, Sint64 offset, SDL_IOWhence whence) noexcept
    {
        auto& self = *static_cast<Input*>(cookie);
        const Sint64 base = whence == SDL_IO_SEEK_SET ? 0 :
                            whence == SDL_IO_SEEK_CUR ? self.position :
                            whence == SDL_IO_SEEK_END ? self.length : -1;
        if (base < 0 || offset < -base || offset > self.length - base) {
            self.failed = true;
            SDL_SetError("Invalid normalized TGA seek");
            return -1;
        }
        return self.position = base + offset;
    }
    static size_t SDLCALL read(void* cookie, void* data, size_t bytes, SDL_IOStatus* status) noexcept
    {
        auto& self = *static_cast<Input*>(cookie);
        if (self.failed) { *status = SDL_IO_STATUS_ERROR; return 0; }
        const auto count = std::min<Uint64>(bytes, self.length - self.position);
        const auto first = self.position < Sint64(self.prefix.size()) ?
            std::min<Uint64>(count, self.prefix.size() - self.position) : 0;
        auto* output = static_cast<Uint8*>(data);
        if (first) SDL_memcpy(output, self.prefix.data() + self.position, first);
        self.position += first;
        if (count > first) {
            if (SDL_SeekIO(self.source, self.data_start + self.position - self.prefix.size(),
                           SDL_IO_SEEK_SET) < 0 ||
                SDL_ReadIO(self.source, output + first, count - first) != count - first) {
                self.failed = true;
                *status = SDL_IO_STATUS_ERROR;
                return 0;
            }
            self.position += count - first;
        }
        if (count < bytes) *status = SDL_IO_STATUS_EOF;
        return count;
    }
    IO open()
    {
        SDL_IOStreamInterface interface;
        SDL_INIT_INTERFACE(&interface);
        interface.size = size;
        interface.seek = seek;
        interface.read = read;
        return IO(SDL_OpenIO(&interface, this), SDL_CloseIO);
    }
};

inline bool validatePixels(SDL_IOStream* io, unsigned start, Uint64 pixels,
                           unsigned bytes, bool rle)
{
    if (SDL_SeekIO(io, start, SDL_IO_SEEK_SET) < 0) return false;
    if (!rle)
        return SDL_GetIOSize(io) - start >= Sint64(pixels * bytes);
    while (pixels) {
        Uint8 packet;
        if (!SDL_ReadU8(io, &packet)) return false;
        const unsigned count = (packet & 127) + 1;
        if (count > pixels || SDL_SeekIO(io, (packet & 128 ? 1 : count) * bytes,
                                       SDL_IO_SEEK_CUR) < 0)
            return false;
        pixels -= count;
    }
    return true;
}
} // namespace tga

srColorSurface* loadTga(SDL_IOStream* source)
{
    if (!srCore.isInitialized()) return nullptr;
    try {
        const auto size = SDL_GetIOSize(source);
        if (size < 18 || size > 256 * 1024 * 1024 ||
            SDL_SeekIO(source, 0, SDL_IO_SEEK_SET) < 0) return nullptr;
        std::vector<Uint8> prefix(18);
        if (SDL_ReadIO(source, prefix.data(), 18) != 18) return nullptr;
        const unsigned type = prefix[2], depth = prefix[16], descriptor = prefix[17];
        const unsigned origin = tga::word(prefix.data() + 3), count = tga::word(prefix.data() + 5);
        const unsigned palette_bits = prefix[7], palette_bytes = (palette_bits + 7) / 8;
        unsigned width = tga::word(prefix.data() + 12), height = tga::word(prefix.data() + 14);
        const bool palette_only = type == 0;
        const bool indexed = palette_only || type == 1 || type == 9;
        const bool gray = type == 3 || type == 11;
        const bool truecolor = type == 2 || type == 10;
        const bool has_palette = prefix[1] == 1;
        if (prefix[1] > 1 || (!indexed && !gray && !truecolor) || (descriptor & 0xc0) ||
            (!palette_only && (!width || !height || Uint64(width) * height > 64 * 1024 * 1024)) ||
            (indexed && !palette_only && depth != 8) || (gray && depth != 8) ||
            (truecolor && depth != 16 && depth != 24 && depth != 32) ||
            (has_palette && (count == 0 || origin + count > 256 ||
                            (palette_bits != 16 && palette_bits != 24 && palette_bits != 32))) ||
            (palette_only && !has_palette))
            return nullptr;
        const unsigned metadata = 18 + prefix[0] + (has_palette ? count * palette_bytes : 0);
        if (metadata > size) return nullptr;
        std::vector<Uint8> palette_data(has_palette ? count * palette_bytes : 0);
        if (SDL_SeekIO(source, 18 + prefix[0], SDL_IO_SEEK_SET) < 0 ||
            (has_palette && SDL_ReadIO(source, palette_data.data(), palette_data.size()) != palette_data.size()))
            return nullptr;
        srPalette* core_palette = nullptr;
        if (indexed && !has_palette) {
            core_palette = srCore.getPalette();
            if (!core_palette) return nullptr;
            palette_data.resize(256 * 3);
            for (unsigned i = 0; i < 256; ++i) {
                const srARGB color = core_palette->getColor(i);
                palette_data[i * 3] = color.blue;
                palette_data[i * 3 + 1] = color.green;
                palette_data[i * 3 + 2] = color.red;
            }
            prefix[1] = 1; prefix[3] = prefix[4] = 0;
            prefix[5] = 0; prefix[6] = 1; prefix[7] = 24;
        }
        prefix[0] = 0;
        prefix[17] &= ~0x10;
        if (palette_only) {
            width = height = 1;
            prefix[2] = 1; prefix[12] = prefix[14] = 1;
            prefix[13] = prefix[15] = 0; prefix[16] = 8;
        }
        prefix.insert(prefix.end(), palette_data.begin(), palette_data.end());
        const unsigned pixel_start = prefix.size();
        if (palette_only) prefix.push_back(origin);
        const Sint64 normalized_size = prefix.size() + size - metadata;
        tga::Input normalized{source, std::move(prefix), metadata, normalized_size};
        auto io = normalized.open();
        const unsigned bytes = palette_only ? 1 : depth / 8;
        if (!io || !tga::validatePixels(io.get(), pixel_start, Uint64(width) * height,
                                        bytes, type >= 9) ||
            SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) < 0)
            return nullptr;
        Surface decoded(IMG_LoadTGA_IO(io.get()), SDL_DestroySurface);
        if (!decoded || normalized.failed || decoded->w != int(width) ||
            decoded->h != int(height)) return nullptr;
        const auto format = indexed ? srPixelConvert::SURFACE_P8 : gray ? srPixelConvert::SURFACE_L8 :
            depth == 16 ? ((descriptor & 15) ? srPixelConvert::SURFACE_ARGB1555 : srPixelConvert::SURFACE_RGB555) :
            depth == 24 ? srPixelConvert::SURFACE_BGR24 :
            (descriptor & 15) ? srPixelConvert::SURFACE_BGRA32 : srPixelConvert::SURFACE_BGRX32;
        auto* surface = SR_NEW(W8ColorSurface)(format, width, height);
        std::unique_ptr<srColorSurface, void(*)(srColorSurface*)> owned(surface, [](srColorSurface* p) {
            if (p) p->release();
        });
        if (!surface || !copyRows(*decoded, *surface, bytes, descriptor & 0x10)) return nullptr;
        if (indexed) {
            if (core_palette) {
                surface->setPalette(core_palette);
            } else {
                const auto* palette = SDL_GetSurfacePalette(decoded.get());
                if (!palette || unsigned(palette->ncolors) != count) return nullptr;
                std::array<srARGB, 256> colors{};
                SDL_memset(colors.data(), 0, sizeof(colors));
                for (unsigned i = 0; i < count; ++i) {
                    colors[origin + i].red = palette->colors[i].r;
                    colors[origin + i].green = palette->colors[i].g;
                    colors[origin + i].blue = palette->colors[i].b;
                    colors[origin + i].alpha = palette_bits == 32 ? palette_data[i * 4 + 3] : 255;
                }
                for (unsigned y = 0; y < height; ++y) {
                    const auto* row = static_cast<const Uint8*>(surface->getDataPtr()) + size_t(y) * surface->getPitch();
                    for (unsigned x = 0; x < width; ++x)
                        if (row[x] < origin || row[x] >= origin + count) return nullptr;
                }
                auto* matching = srPalette::findMatchingPalette(colors.data(), origin + count);
                if (!matching) {
                    matching = SR_NEW(W8Palette)(colors.data(), origin + count);
                    matching->autoRelease();
                    matching->setName("TGA-importer generated palette");
                }
                surface->setPalette(matching);
            }
        }
        return owned.release();
    } catch (...) { return nullptr; }
}
} // namespace srImage
