#include "plugin_classes.h"
#include "sdl_stream.h"

#include <SDL3_image/SDL_image.h>
#include <cmath>
#include <cstdlib>
#include <string>

namespace {
struct JpegHeader {
    unsigned width = 0;
    unsigned height = 0;
    unsigned components = 0;
};

bool readHeader(SDL_IOStream* io, JpegHeader& header)
{
    Uint8 signature[2];
    if (SDL_ReadIO(io, signature, 2) != 2 || signature[0] != 0xff || signature[1] != 0xd8)
        return false;
    for (;;) {
        Uint8 prefix = 0, marker = 0;
        if (!SDL_ReadU8(io, &prefix) || prefix != 0xff)
            return false;
        do {
            if (!SDL_ReadU8(io, &marker))
                return false;
        } while (marker == 0xff);
        if (marker == 0 || marker == 0xd9 || marker == 0xda)
            return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        Uint16 length = 0;
        if (!SDL_ReadU16BE(io, &length) || length < 2)
            return false;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            Uint8 data[6];
            if (length < 8 || SDL_ReadIO(io, data, 6) != 6)
                return false;
            header.height = unsigned(data[1]) * 256 + data[2];
            header.width = unsigned(data[3]) * 256 + data[4];
            header.components = data[5];
            return data[0] == 8 && (data[5] == 1 || data[5] == 3 || data[5] == 4) &&
                   length == 8 + 3 * data[5] && header.width && header.height &&
                   Uint64(header.width) * header.height <= 64 * 1024 * 1024;
        }
        if (SDL_SeekIO(io, length - 2, SDL_IO_SEEK_CUR) < 0)
            return false;
    }
}

srColorSurface* loadJpeg(srBinIStream& stream)
{
    srImage::Stream bridge{stream, &stream};
    auto io = bridge.open();
    if (!io || SDL_GetIOSize(io.get()) > 256 * 1024 * 1024 ||
        SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) < 0)
        return nullptr;
    JpegHeader header;
    if (!readHeader(io.get(), header) || SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) < 0)
        return nullptr;
    srImage::Surface decoded(IMG_LoadJPG_IO(io.get()), SDL_DestroySurface);
    // libjpeg can synthesize EOI at EOF and ignore I/O errors; reject either case.
    if (!decoded || bridge.failed || bridge.eof || decoded->w != int(header.width) ||
        decoded->h != int(header.height))
        return nullptr;
    const bool four_channels = header.components == 4;
    srImage::Surface bgr(SDL_ConvertSurface(decoded.get(), four_channels ? SDL_PIXELFORMAT_BGRA32 :
                                                                       SDL_PIXELFORMAT_BGR24), SDL_DestroySurface);
    if (!bgr)
        return nullptr;
    auto* result = SR_NEW(srColorSurface)(header.components == 1 ? srPixelConvert::SURFACE_L8 :
                                        four_channels ? srPixelConvert::SURFACE_BGRA32 :
                                                        srPixelConvert::SURFACE_BGR24,
                                          header.width, header.height);
    if (!result || !result->getDataPtr()) {
        if (result) result->release();
        return nullptr;
    }
    if (header.components == 1) {
        for (unsigned y = 0; y < header.height; ++y) {
            const auto* src = static_cast<const Uint8*>(bgr->pixels) + size_t(y) * bgr->pitch;
            auto* dst = static_cast<Uint8*>(result->getDataPtr()) + size_t(y) * result->getPitch();
            for (unsigned x = 0; x < header.width; ++x)
                dst[x] = src[x * 3];
        }
    } else if (!srImage::copyRows(*bgr, *result, four_channels ? 4 : 3)) {
        result->release();
        return nullptr;
    }
    return result;
}

int quality(const char* options)
{
    if (!options) return 100;
    std::string text(options);
    for (char& c : text)
        c = static_cast<char>(SDL_toupper(static_cast<unsigned char>(c)));
    const auto name = text.find("QUALITY");
    if (name == std::string::npos) return 100;
    auto equals = text.find('=', name);
    if (equals == std::string::npos) return 100;
    while (equals < text.size() && (text[equals] == '=' || text[equals] == ' '))
        ++equals;
    const double value = std::strtod(text.c_str() + equals, nullptr);
    return std::isfinite(value) ? int(std::clamp(value, 0.0, 1.0) * 100) : 100;
}
} // namespace

int srJPEGImporter::getSurfaceDesc(srColorSurfaceIFace::SurfaceDesc& description,
                                 srBinIStream& stream, const srSurfaceIOManager::ImportInfo&)
{
    try {
        const auto position = stream.tell();
        auto* surface = loadJpeg(stream);
        if (!surface) return 0;
        surface->getSurfaceDesc(description);
        surface->release();
        stream.seek(position);
        return stream.good() && stream.tell() == position;
    } catch (...) { return 0; }
}

srColorSurfaceIFace* srJPEGImporter::importSurface(srBinIStream& stream,
                                                  const srSurfaceIOManager::ImportInfo&)
{
    return loadJpeg(stream);
}

void srJPEGImporter::exportSurface(srBinOStream& stream, srColorSurfaceIFace& source,
                                  const srSurfaceIOManager::ExportInfo& options)
{
    srPixelConvert::PixelFormat format;
    source.getPixelFormat(format);
    if (!source.getWidth() || !source.getHeight() || source.getWidth() > 65535 ||
        source.getHeight() > 65535 ||
        source.getPitch() <= 0 || Uint64(source.getPitch()) <
            Uint64(source.getWidth()) * (unsigned(format.pixel_size) + 1) ||
        Uint64(source.getWidth()) * source.getHeight() > 64 * 1024 * 1024)
        throw "srJPEGImporter::exportSurface: invalid dimensions";
    auto* copy = SR_NEW(srColorSurface)(srPixelConvert::SURFACE_RGB24, source.getWidth(), source.getHeight());
    std::unique_ptr<srColorSurface, void(*)(srColorSurface*)> owned(copy, [](srColorSurface* p) {
        if (p) p->release();
    });
    if (!copy || !copy->getDataPtr())
        throw "srJPEGImporter::exportSurface: allocation failed";
    copy->copy(source);
    srImage::Surface view(SDL_CreateSurfaceFrom(copy->getWidth(), copy->getHeight(),
                         SDL_PIXELFORMAT_RGB24, copy->getDataPtr(), copy->getPitch()), SDL_DestroySurface);
    srImage::Stream bridge{stream, nullptr, &stream};
    auto io = bridge.open();
    if (!view || !io || SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) < 0 ||
        !IMG_SaveJPG_IO(view.get(), io.get(), false, quality(options.option_string)) || bridge.failed)
        throw "srJPEGImporter::exportSurface: output stream is corrupt";
}
