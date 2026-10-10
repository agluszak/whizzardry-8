#include "surrender/srImageIO.h"
#include "surrender/srCore.h"
#include "surrender/srIStreamOpener.h"
#include "surrender/srBinFStream.h"
#include "image_stream.h"

#include <SDL3_image/SDL_image.h>
#include <stdexcept>
#include <cstring>

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

const char* extension(const char* path)
{
    if (!path || !*path) throw std::runtime_error("Image filename is NULL or empty");
    const char* result = "";
    for (const char* ch = path; *ch; ++ch) {
        if (*ch == '/' || *ch == '\\') result = "";
        else if (*ch == '.') result = ch + 1;
    }
    return result;
}

bool isJpeg(const char* type)
{
    return SDL_strcasecmp(type, "jpg") == 0 || SDL_strcasecmp(type, "jpeg") == 0;
}

enum class OutputFormat { jpeg, png, bmp };

OutputFormat outputFormat(const char* path)
{
    const char* type = extension(path);
    if (isJpeg(type)) return OutputFormat::jpeg;
    if (SDL_strcasecmp(type, "png") == 0) return OutputFormat::png;
    if (SDL_strcasecmp(type, "bmp") == 0) return OutputFormat::bmp;
    throw std::runtime_error("Unsupported image output format");
}

void saveImage(OutputFormat output, srBinOStream& stream, srColorSurfaceIFace& source, int quality)
{
    srPixelConvert::PixelFormat format;
    source.getPixelFormat(format);
    if (!source.getWidth() || !source.getHeight() || source.getWidth() > 65535 ||
        source.getHeight() > 65535 ||
        source.getPitch() <= 0 || Uint64(source.getPitch()) <
            Uint64(source.getWidth()) * (unsigned(format.pixel_size) + 1) ||
        Uint64(source.getWidth()) * source.getHeight() > 64 * 1024 * 1024)
        throw std::runtime_error("Image output dimensions are invalid");
    const bool jpeg = output == OutputFormat::jpeg;
    auto* copy = SR_NEW(srColorSurface)(jpeg ? srPixelConvert::SURFACE_RGB24 : srPixelConvert::SURFACE_BGRA32,
                                      source.getWidth(), source.getHeight());
    std::unique_ptr<srColorSurface, void(*)(srColorSurface*)> owned(copy, [](srColorSurface* p) {
        if (p) p->release();
    });
    if (!copy || !copy->getDataPtr())
        throw std::runtime_error("Image output allocation failed");
    copy->copy(source);
    srImage::Surface view(SDL_CreateSurfaceFrom(copy->getWidth(), copy->getHeight(),
                         jpeg ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_BGRA32,
                         copy->getDataPtr(), copy->getPitch()), SDL_DestroySurface);
    srImage::Stream bridge{stream, nullptr, &stream};
    auto io = bridge.open();
    if (!view || !io || SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) < 0)
        throw std::runtime_error("Image output stream is corrupt");
    const bool saved = output == OutputFormat::jpeg ?
        IMG_SaveJPG_IO(view.get(), io.get(), false, std::clamp(quality, 0, 100)) :
        output == OutputFormat::png ? IMG_SavePNG_IO(view.get(), io.get(), false) :
                                     IMG_SaveBMP_IO(view.get(), io.get(), false);
    if (!saved || bridge.failed || !stream.good())
        throw std::runtime_error("Image output stream is corrupt");
}
} // namespace

namespace srImage {
// FUNCTION: SURRENDER 0x1002DB20
srColorSurfaceIFace* load(const char* path, srBinIStream& stream)
{
    const char* type = extension(path);
    try {
        if (!stream.good()) return nullptr;
        if (isJpeg(type)) return loadJpeg(stream);
        if (SDL_strcasecmp(type, "tga") == 0) return loadTga(stream);
        Stream bridge{stream, &stream};
        auto io = bridge.open();
        const auto size = io ? SDL_GetIOSize(io.get()) : -1;
        if (size <= 0 || size > 256 * 1024 * 1024 ||
            SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) < 0) return nullptr;
        Surface decoded(IMG_LoadTyped_IO(io.get(), false, type), SDL_DestroySurface);
        if (!decoded || bridge.failed || !stream.good() || decoded->w <= 0 || decoded->h <= 0 ||
            Uint64(decoded->w) * decoded->h > 64 * 1024 * 1024) return nullptr;
        Surface converted(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_BGRA32), SDL_DestroySurface);
        if (!converted) return nullptr;
        auto* result = new srColorSurface(srPixelConvert::SURFACE_BGRA32, converted->w, converted->h);
        if (!copyRows(*converted, *result, 4)) {
            result->release();
            return nullptr;
        }
        return result;
    } catch (...) { return nullptr; }
}

// FUNCTION: SURRENDER 0x1002DA00
srColorSurfaceIFace* load(const char* path)
{
    extension(path);
    auto* opener = srCore.getIStreamOpener();
    if (!opener) throw std::runtime_error("Image stream opener is not initialized");
    std::unique_ptr<srBinIStream> stream(opener->open(path));
    if (!stream || !stream->good()) throw std::runtime_error("Image file could not be opened");
    auto* surface = load(path, *stream);
    if (!surface) throw std::runtime_error("Image decoding failed");
    return surface;
}

// FUNCTION: SURRENDER 0x1002DD70
bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path, srBinIStream& stream)
{
    try {
        const auto position = stream.tell();
        auto* surface = load(path, stream);
        if (surface) {
            surface->getSurfaceDesc(description);
            surface->release();
        }
        stream.seek(position);
        return surface && stream.good() && stream.tell() == position;
    } catch (...) { return false; }
}

// FUNCTION: SURRENDER 0x1002D8F0
void describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path)
{
    auto* surface = load(path);
    surface->getSurfaceDesc(description);
    surface->release();
}

// FUNCTION: SURRENDER 0x1002DCD0
void save(const char* path, srBinOStream& stream, srColorSurfaceIFace& surface, int quality)
{
    saveImage(outputFormat(path), stream, surface, quality);
}

// FUNCTION: SURRENDER 0x1002DBC0
void save(const char* path, srColorSurfaceIFace& surface, int quality)
{
    const auto format = outputFormat(path);
    srBinOFStream stream(path);
    if (!stream.good()) throw std::runtime_error("Image file could not be opened for writing");
    saveImage(format, stream, surface, quality);
}
} // namespace srImage
