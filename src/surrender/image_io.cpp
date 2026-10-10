#include "surrender/srImageIO.h"
#include "surrender/srCore.h"
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

srColorSurface* loadJpeg(srImage::Stream& bridge, SDL_IOStream* io)
{
    if (SDL_GetIOSize(io) > 256 * 1024 * 1024 ||
        SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        return nullptr;
    JpegHeader header;
    if (!readHeader(io, header) || SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        return nullptr;
    srImage::Surface decoded(IMG_LoadJPG_IO(io), SDL_DestroySurface);
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

void saveImage(OutputFormat output, srImage::Stream& bridge, srColorSurfaceIFace& source, int quality)
{
    if (!srCore.isInitialized()) throw std::runtime_error("Image I/O requires SurRender initialization");
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
    auto io = bridge.open();
    if (!view || !io || SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) < 0)
        throw std::runtime_error("Image output stream is corrupt");
    const bool saved = output == OutputFormat::jpeg ?
        IMG_SaveJPG_IO(view.get(), io.get(), false, std::clamp(quality, 0, 100)) :
        output == OutputFormat::png ? IMG_SavePNG_IO(view.get(), io.get(), false) :
                                     IMG_SaveBMP_IO(view.get(), io.get(), false);
    if (!saved || bridge.failed || !bridge.good())
        throw std::runtime_error("Image output stream is corrupt");
}
srColorSurfaceIFace* loadImage(const char* path, srImage::Stream& bridge)
{
    using namespace srImage;
    const char* type = extension(path);
    try {
        if (!srCore.isInitialized() || !bridge.good()) return nullptr;
        auto io = bridge.open();
        const auto size = io ? SDL_GetIOSize(io.get()) : -1;
        if (size <= 0 || size > 256 * 1024 * 1024 ||
            SDL_SeekIO(io.get(), 0, SDL_IO_SEEK_SET) < 0) return nullptr;
        if (isJpeg(type)) return loadJpeg(bridge, io.get());
        if (SDL_strcasecmp(type, "tga") == 0) {
            auto* result = loadTga(io.get());
            if (result && bridge.failed) { result->release(); return nullptr; }
            return result;
        }
        Surface decoded(IMG_LoadTyped_IO(io.get(), false, type), SDL_DestroySurface);
        if (!decoded || bridge.failed || !bridge.good() || decoded->w <= 0 || decoded->h <= 0 ||
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
} // namespace

namespace srImage {
// FUNCTION: SURRENDER 0x1002DB20
srColorSurfaceIFace* load(const char* path, srBinIStream& stream)
{
    Stream bridge{stream, &stream};
    return loadImage(path, bridge);
}

srColorSurfaceIFace* load(const char* path, wiz8::File& file)
{
    Stream bridge{file};
    return loadImage(path, bridge);
}

srColorSurface* loadTga(srBinIStream& stream)
{
    Stream bridge{stream, &stream};
    auto io = bridge.open();
    auto* result = io ? loadTga(io.get()) : nullptr;
    if (result && bridge.failed) { result->release(); return nullptr; }
    return result;
}

srColorSurface* loadTga(wiz8::File& file)
{
    Stream bridge{file};
    auto io = bridge.open();
    auto* result = io ? loadTga(io.get()) : nullptr;
    if (result && bridge.failed) { result->release(); return nullptr; }
    return result;
}

// FUNCTION: SURRENDER 0x1002DA00
srColorSurfaceIFace* load(const char* path)
{
    extension(path);
    if (!srCore.isInitialized()) throw std::runtime_error("Image I/O requires SurRender initialization");
    auto file = wiz8::open_file(path);
    auto* surface = load(path, *file);
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

bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path, wiz8::File& file)
{
    try {
        const auto position = file.tell();
        auto* surface = load(path, file);
        if (surface) {
            surface->getSurfaceDesc(description);
            surface->release();
        }
        file.seek(position, wiz8::SeekOrigin::begin);
        return surface != nullptr && file.tell() == position;
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
    Stream bridge{stream, nullptr, &stream};
    saveImage(outputFormat(path), bridge, surface, quality);
}

void save(const char* path, wiz8::File& file, srColorSurfaceIFace& surface, int quality)
{
    Stream bridge{file, true};
    saveImage(outputFormat(path), bridge, surface, quality);
}

// FUNCTION: SURRENDER 0x1002DBC0
void save(const char* path, srColorSurfaceIFace& surface, int quality)
{
    const auto format = outputFormat(path);
    if (!srCore.isInitialized()) throw std::runtime_error("Image I/O requires SurRender initialization");
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    Stream bridge{*file, true};
    saveImage(format, bridge, surface, quality);
}
} // namespace srImage
