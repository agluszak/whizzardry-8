#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include "asset_paths.h"
#include "jpeg_validate.h"
#include "surrender/srCore.h"
#include "surrender/srIO.h"
#include "surrender/srImageIO.h"
#include "wiz8/filesystem.h"
#include "wiz8/surface_storage.h"

namespace {
struct JpegHeader {
    unsigned width = 0;
    unsigned height = 0;
    unsigned components = 0;
};

SDLSurfaceOwner loadJpeg(SDL_IOStream* io)
{
    if (SDL_GetIOSize(io) > 256 * 1024 * 1024 || SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        return {nullptr, SDL_DestroySurface};
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> bytes(SDL_LoadFile_IO(io, &size, false), SDL_free);
    JpegHeader header;
    if (!bytes || size > 256 * 1024 * 1024 || SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR ||
        !srValidateJpeg(static_cast<const unsigned char*>(bytes.get()), size, &header.width,
                        &header.height, &header.components))
        return {nullptr, SDL_DestroySurface};
    auto source = srIO::own(SDL_IOFromConstMem(bytes.get(), size));
    SDLSurfaceOwner decoded(IMG_LoadJPG_IO(source.get()), SDL_DestroySurface);
    if (!decoded || decoded->w != int(header.width) || decoded->h != int(header.height))
        return {nullptr, SDL_DestroySurface};
    const bool four_channels = header.components == 4;
    SDLSurfaceOwner bgr(SDL_ConvertSurface(decoded.get(), four_channels ? SDL_PIXELFORMAT_BGRA32
                                                                        : SDL_PIXELFORMAT_BGR24),
                        SDL_DestroySurface);
    if (!bgr) return {nullptr, SDL_DestroySurface};
    if (header.components != 1) return bgr;
    auto result = CreateCpuStorage(header.width, header.height, SDL_PIXELFORMAT_INDEX8);
    std::array<SDL_Color, 256> gray;
    for (unsigned i = 0; i < gray.size(); ++i) gray[i] = {Uint8(i), Uint8(i), Uint8(i), 255};
    if (!SDL_SetPaletteColors(SDL_GetSurfacePalette(result.get()), gray.data(), 0, gray.size()) ||
        !SDL_BlitSurface(bgr.get(), nullptr, result.get(), nullptr))
        return {nullptr, SDL_DestroySurface};
    return result;
}

const char* extension(const char* path)
{
    if (!path || !*path) throw std::runtime_error("Image filename is NULL or empty");
    const char* result = "";
    for (const char* ch = path; *ch; ++ch) {
        if (*ch == '/' || *ch == '\\')
            result = "";
        else if (*ch == '.')
            result = ch + 1;
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

void saveImage(OutputFormat output, SDL_IOStream* io, SDL_Surface& source, int quality)
{
    const auto& format = source.format;
    if (!source.w || !source.h || source.w > 65535 || source.h > 65535 || source.pitch <= 0 ||
        Uint64(source.pitch) < Uint64(source.w) * (SDL_BYTESPERPIXEL(format)) ||
        Uint64(source.w) * source.h > 64 * 1024 * 1024)
        throw std::runtime_error("Image output dimensions are invalid");
    const bool jpeg = output == OutputFormat::jpeg;
    SDLSurfaceOwner view(
        SDL_ConvertSurface(&source, jpeg ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_BGRA32),
        SDL_DestroySurface);
    if (!view || !io || SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        throw std::runtime_error("Image output stream is corrupt");
    // SDL_image's JPEG/PNG writers can ignore short backend writes. Encode
    // into SDL-owned memory, then transfer explicitly to the borrowed stream.
    auto encoded = srIO::own(SDL_IOFromDynamicMem());
    const bool saved =
        output == OutputFormat::jpeg
            ? IMG_SaveJPG_IO(view.get(), encoded.get(), false, std::clamp(quality, 0, 100))
        : output == OutputFormat::png ? IMG_SavePNG_IO(view.get(), encoded.get(), false)
                                      : IMG_SaveBMP_IO(view.get(), encoded.get(), false);
    const auto size = SDL_GetIOSize(encoded.get());
    if (!saved || size <= 0 || SDL_GetIOStatus(encoded.get()) != SDL_IO_STATUS_READY)
        throw std::runtime_error("Image encoding failed");
    srIO::writeExact(io,
                     SDL_GetPointerProperty(SDL_GetIOProperties(encoded.get()),
                                            SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr),
                     static_cast<size_t>(size));
}
SDLSurfaceOwner loadImage(const char* path, SDL_IOStream* io)
{
    using namespace srImage;
    const char* type = extension(path);
    try {
        if (!io) return {nullptr, SDL_DestroySurface};
        const auto size = SDL_GetIOSize(io);
        if (size <= 0 || size > 256 * 1024 * 1024 || SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
            return {nullptr, SDL_DestroySurface};
        if (isJpeg(type)) return loadJpeg(io);
        if (SDL_strcasecmp(type, "tga") == 0) {
            auto result = loadTga(io);
            if (result && SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR)
                return {nullptr, SDL_DestroySurface};
            return result;
        }
        SDLSurfaceOwner decoded(IMG_LoadTyped_IO(io, false, type), SDL_DestroySurface);
        if (!decoded || SDL_GetIOStatus(io) == SDL_IO_STATUS_ERROR || decoded->w <= 0 ||
            decoded->h <= 0 || Uint64(decoded->w) * decoded->h > 64 * 1024 * 1024)
            return {nullptr, SDL_DestroySurface};
        SDLSurfaceOwner converted(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_BGRA32),
                                  SDL_DestroySurface);
        if (!converted) return {nullptr, SDL_DestroySurface};
        return converted;
    } catch (...) {
        return {nullptr, SDL_DestroySurface};
    }
}
} // namespace

namespace srImage {
// FUNCTION: SURRENDER 0x1002DB20
SDLSurfaceOwner load(const char* path, SDL_IOStream* stream)
{
    return loadImage(path, stream);
}

SDLSurfaceOwner load(const char* path, wiz8::File& file)
{
    try {
        return load(path, file.io());
    } catch (...) {
        return {nullptr, SDL_DestroySurface};
    }
}

SDLSurfaceOwner loadTga(wiz8::File& file)
{
    try {
        return loadTga(file.io());
    } catch (...) {
        return {nullptr, SDL_DestroySurface};
    }
}

// FUNCTION: SURRENDER 0x1002DA00
SDLSurfaceOwner load(const char* path)
{
    extension(path);
    auto file = wiz8::open_file(wiz8::asset_path(path));
    auto surface = load(path, *file);
    if (!surface) throw std::runtime_error("Image decoding failed");
    return surface;
}

// FUNCTION: SURRENDER 0x1002DCD0
void save(const char* path, SDL_IOStream* stream, SDL_Surface& surface, int quality)
{
    saveImage(outputFormat(path), stream, surface, quality);
}

void save(const char* path, wiz8::File& file, SDL_Surface& surface, int quality)
{
    save(path, file.io(), surface, quality);
}

// FUNCTION: SURRENDER 0x1002DBC0
void save(const char* path, SDL_Surface& surface, int quality)
{
    const auto format = outputFormat(path);
    auto file = wiz8::open_file(wiz8::user_path(path), wiz8::OpenMode::replace);
    saveImage(format, file->io(), surface, quality);
}
} // namespace srImage
