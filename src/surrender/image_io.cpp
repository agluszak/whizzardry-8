#include "surrender/srImageIO.h"
#include "surrender/srCore.h"
#include "image_helpers.h"
#include "surrender/srIO.h"
#include "wiz8/filesystem.h"

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

bool jpegMarker(SDL_IOStream* io, bool entropy, Uint8& marker)
{
    if (!entropy) {
        Uint8 prefix;
        if (!SDL_ReadU8(io, &prefix) || prefix != 0xff) return false;
        do { if (!SDL_ReadU8(io, &marker)) return false; } while (marker == 0xff);
        return true;
    }
    // Scan entropy data in blocks, retaining marker prefixes across block
    // boundaries. Stuffed bytes and restart markers are part of the scan.
    Uint8 bytes[4096];
    bool prefix = false;
    for (;;) {
        const auto count = SDL_ReadIO(io, bytes, sizeof(bytes));
        if (!count || SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR) return false;
        for (size_t i = 0; i < count; ++i) {
            const auto byte = bytes[i];
            if (!prefix) { prefix = byte == 0xff; continue; }
            if (byte == 0xff) continue;
            prefix = false;
            if (byte == 0 || (byte >= 0xd0 && byte <= 0xd7)) continue;
            marker = byte;
            return SDL_SeekIO(io, Sint64(i + 1) - Sint64(count), SDL_IO_SEEK_CUR) >= 0;
        }
    }
}

bool completeJpeg(SDL_IOStream* io)
{
    if (SDL_SeekIO(io, 2, SDL_IO_SEEK_SET) < 0) return false;
    bool entropy = false, scanned = false;
    for (;;) {
        Uint8 marker;
        if (!jpegMarker(io, entropy, marker)) return false;
        if (marker == 0xd9) return scanned;
        if (marker == 0 || marker == 0xd8) return false;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        Uint16 length;
        if (!SDL_ReadU16BE(io, &length) || length < 2) return false;
        const auto position = SDL_TellIO(io), size = SDL_GetIOSize(io);
        if (position < 0 || size < position || length - 2 > size - position ||
            SDL_SeekIO(io, length - 2, SDL_IO_SEEK_CUR) < 0) return false;
        if (marker != 0xdc) entropy = marker == 0xda;
        scanned |= entropy;
    }
}

srColorSurface* loadJpeg(SDL_IOStream* io)
{
    if (SDL_GetIOSize(io) > 256 * 1024 * 1024 ||
        SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        return nullptr;
    JpegHeader header;
    if (!readHeader(io, header) || !completeJpeg(io) || SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        return nullptr;
    srImage::Surface decoded(IMG_LoadJPG_IO(io), SDL_DestroySurface);
    // The marker pass rejects synthesized EOI; SDL reports backend failures.
    if (!decoded || SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR || decoded->w != int(header.width) ||
        decoded->h != int(header.height))
        return nullptr;
    const bool four_channels = header.components == 4;
    srImage::Surface bgr(SDL_ConvertSurface(decoded.get(), four_channels ? SDL_PIXELFORMAT_BGRA32 :
                                                                       SDL_PIXELFORMAT_BGR24), SDL_DestroySurface);
    if (!bgr)
        return nullptr;
    auto* result = new srColorSurface(header.components == 1 ? srPixelConvert::SURFACE_L8 :
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

void saveImage(OutputFormat output, SDL_IOStream* io, srColorSurfaceIFace& source, int quality)
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
    auto* copy = new srColorSurface(jpeg ? srPixelConvert::SURFACE_RGB24 : srPixelConvert::SURFACE_BGRA32,
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
    if (!view || !io || SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        throw std::runtime_error("Image output stream is corrupt");
    // SDL_image's JPEG/PNG writers can ignore short backend writes. Encode
    // into SDL-owned memory, then transfer explicitly to the borrowed stream.
    auto encoded = srIO::own(SDL_IOFromDynamicMem());
    const bool saved = output == OutputFormat::jpeg ?
        IMG_SaveJPG_IO(view.get(), encoded.get(), false, std::clamp(quality, 0, 100)) :
        output == OutputFormat::png ? IMG_SavePNG_IO(view.get(), encoded.get(), false) :
                                     IMG_SaveBMP_IO(view.get(), encoded.get(), false);
    const auto size = SDL_GetIOSize(encoded.get());
    if (!saved || size <= 0 || SDL_GetIOStatus(encoded.get()) != SDL_IO_STATUS_READY)
        throw std::runtime_error("Image encoding failed");
    srIO::writeExact(io, SDL_GetPointerProperty(SDL_GetIOProperties(encoded.get()),
                     SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr), static_cast<size_t>(size));

}
srColorSurfaceIFace* loadImage(const char* path, SDL_IOStream* io)
{
    using namespace srImage;
    const char* type = extension(path);
    try {
        if (!srCore.isInitialized() || !io) return nullptr;
        const auto size = SDL_GetIOSize(io);
        if (size <= 0 || size > 256 * 1024 * 1024 ||
            SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0) return nullptr;
        if (isJpeg(type)) return loadJpeg(io);
        if (SDL_strcasecmp(type, "tga") == 0) {
            auto* result = loadTga(io);
            if (result && SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR) { result->release(); return nullptr; }
            return result;
        }
        Surface decoded(IMG_LoadTyped_IO(io, false, type), SDL_DestroySurface);
        if (!decoded || SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR || decoded->w <= 0 || decoded->h <= 0 ||
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
srColorSurfaceIFace* load(const char* path, SDL_IOStream* stream)
{
    return loadImage(path, stream);
}

srColorSurfaceIFace* load(const char* path, wiz8::File& file)
{
    try { return load(path, file.io()); } catch (...) { return nullptr; }
}

srColorSurface* loadTga(wiz8::File& file)
{
    try { return loadTga(file.io()); } catch (...) { return nullptr; }
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
bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path, SDL_IOStream* stream)
{
    const auto position = SDL_TellIO(stream);
    if (position < 0) return false;
    srColorSurfaceIFace* surface = nullptr;
    try { surface = load(path, stream); } catch (...) {}
    if (surface) {
        surface->getSurfaceDesc(description);
        surface->release();
    }
    return SDL_SeekIO(stream, position, SDL_IO_SEEK_SET) == position && surface != nullptr;
}

bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path, wiz8::File& file)
{
    try { return describe(description, path, file.io()); } catch (...) { return false; }
}

// FUNCTION: SURRENDER 0x1002D8F0
void describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path)
{
    auto* surface = load(path);
    surface->getSurfaceDesc(description);
    surface->release();
}

// FUNCTION: SURRENDER 0x1002DCD0
void save(const char* path, SDL_IOStream* stream, srColorSurfaceIFace& surface, int quality)
{
    saveImage(outputFormat(path), stream, surface, quality);
}

void save(const char* path, wiz8::File& file, srColorSurfaceIFace& surface, int quality)
{
    save(path, file.io(), surface, quality);
}

// FUNCTION: SURRENDER 0x1002DBC0
void save(const char* path, srColorSurfaceIFace& surface, int quality)
{
    const auto format = outputFormat(path);
    if (!srCore.isInitialized()) throw std::runtime_error("Image I/O requires SurRender initialization");
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    saveImage(format, file->io(), surface, quality);
}
} // namespace srImage
