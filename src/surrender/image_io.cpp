#include "surrender/srImageIO.h"
#include "surrender/srCore.h"
#include "image_helpers.h"
#include "jpeg_validate.h"
#include "surrender/srIO.h"
#include "wiz8/filesystem.h"

#include <SDL3_image/SDL_image.h>
#include <stdexcept>
#include <cstring>

namespace srImage {
bool copyRows(const SDL_Surface& source, srColorSurfaceIFace& destination,
                     unsigned bytes_per_pixel, bool mirror)
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

namespace {
struct JpegHeader {
    unsigned width = 0;
    unsigned height = 0;
    unsigned components = 0;
};

std::unique_ptr<srColorSurface> loadJpeg(SDL_IOStream* io)
{
    if (SDL_GetIOSize(io) > 256 * 1024 * 1024 ||
        SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        return nullptr;
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> bytes(SDL_LoadFile_IO(io, &size, false), SDL_free);
    JpegHeader header;
    if (!bytes || size > 256 * 1024 * 1024 ||
        SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR ||
        !srValidateJpeg(static_cast<const unsigned char*>(bytes.get()), size,
                        &header.width, &header.height, &header.components))
        return nullptr;
    auto source = srIO::own(SDL_IOFromConstMem(bytes.get(), size));
    srImage::Surface decoded(IMG_LoadJPG_IO(source.get()), SDL_DestroySurface);
    if (!decoded || decoded->w != int(header.width) || decoded->h != int(header.height))
        return nullptr;
    const bool four_channels = header.components == 4;
    srImage::Surface bgr(SDL_ConvertSurface(decoded.get(), four_channels ? SDL_PIXELFORMAT_BGRA32 :
                                                                       SDL_PIXELFORMAT_BGR24), SDL_DestroySurface);
    if (!bgr)
        return nullptr;
    auto result = std::make_unique<srColorSurface>(header.components == 1 ? srPixelConvert::SURFACE_L8 :
                                        four_channels ? srPixelConvert::SURFACE_BGRA32 :
                                                        srPixelConvert::SURFACE_BGR24,
                                          header.width, header.height);
    if (!result->getDataPtr()) return nullptr;
    if (header.components == 1) {
        for (unsigned y = 0; y < header.height; ++y) {
            const auto* src = static_cast<const Uint8*>(bgr->pixels) + size_t(y) * bgr->pitch;
            auto* dst = static_cast<Uint8*>(result->getDataPtr()) + size_t(y) * result->getPitch();
            for (unsigned x = 0; x < header.width; ++x)
                dst[x] = src[x * 3];
        }
    } else if (!srImage::copyRows(*bgr, *result, four_channels ? 4 : 3)) {
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
    auto copy = std::make_unique<srColorSurface>(jpeg ? srPixelConvert::SURFACE_RGB24 : srPixelConvert::SURFACE_BGRA32,
                                      source.getWidth(), source.getHeight());
    if (!copy->getDataPtr())
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
std::unique_ptr<srColorSurfaceIFace> loadImage(const char* path, SDL_IOStream* io)
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
            auto result = loadTga(io);
            if (result && SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR) return nullptr;
            return result;
        }
        Surface decoded(IMG_LoadTyped_IO(io, false, type), SDL_DestroySurface);
        if (!decoded || SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR || decoded->w <= 0 || decoded->h <= 0 ||
            Uint64(decoded->w) * decoded->h > 64 * 1024 * 1024) return nullptr;
        Surface converted(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_BGRA32), SDL_DestroySurface);
        if (!converted) return nullptr;
        auto result = std::make_unique<srColorSurface>(srPixelConvert::SURFACE_BGRA32, converted->w, converted->h);
        if (!copyRows(*converted, *result, 4)) {
            return nullptr;
        }
        return result;
    } catch (...) { return nullptr; }
}
} // namespace

namespace srImage {
// FUNCTION: SURRENDER 0x1002DB20
std::unique_ptr<srColorSurfaceIFace> load(const char* path, SDL_IOStream* stream)
{
    return loadImage(path, stream);
}

std::unique_ptr<srColorSurfaceIFace> load(const char* path, wiz8::File& file)
{
    try { return load(path, file.io()); } catch (...) { return nullptr; }
}

std::unique_ptr<srColorSurface> loadTga(wiz8::File& file)
{
    try { return loadTga(file.io()); } catch (...) { return nullptr; }
}

// FUNCTION: SURRENDER 0x1002DA00
std::unique_ptr<srColorSurfaceIFace> load(const char* path)
{
    extension(path);
    if (!srCore.isInitialized()) throw std::runtime_error("Image I/O requires SurRender initialization");
    auto file = wiz8::open_file(path);
    auto surface = load(path, *file);
    if (!surface) throw std::runtime_error("Image decoding failed");
    return surface;
}

// FUNCTION: SURRENDER 0x1002DD70
bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path, SDL_IOStream* stream)
{
    const auto position = SDL_TellIO(stream);
    if (position < 0) return false;
    std::unique_ptr<srColorSurfaceIFace> surface;
    try { surface = load(path, stream); } catch (...) {}
    if (surface) {
        surface->getSurfaceDesc(description);
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
    auto surface = load(path);
    surface->getSurfaceDesc(description);
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
