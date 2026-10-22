#include "wiz8/render/image_io.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include "asset_paths.h"
#include "wiz8/filesystem.h"
#include "wiz8/platform/sdl_io.h"
#include "wiz8/surface_storage.h"

namespace {
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
    const auto target_format = jpeg ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_BGRA32;
    SDLSurfaceOwner converted{nullptr, SDL_DestroySurface};
    SDL_Surface* pixels = &source;
    if (source.format != target_format) {
        converted.reset(SDL_ConvertSurface(&source, target_format));
        pixels = converted.get();
    }
    if (!pixels || !io || SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0)
        throw std::runtime_error("Image output stream is corrupt");
    // SDL_image's JPEG/PNG writers can ignore short backend writes. Encode
    // into SDL-owned memory, then transfer explicitly to the borrowed stream.
    auto encoded = sdlIO::own(SDL_IOFromDynamicMem());
    const bool saved = output == OutputFormat::jpeg  ? IMG_SaveJPG_IO(pixels, encoded.get(), false,
                                                                      std::clamp(quality, 0, 100))
                       : output == OutputFormat::png ? IMG_SavePNG_IO(pixels, encoded.get(), false)
                                                     : IMG_SaveBMP_IO(pixels, encoded.get(), false);
    const auto size = SDL_GetIOSize(encoded.get());
    if (!saved || size <= 0 || SDL_GetIOStatus(encoded.get()) != SDL_IO_STATUS_READY)
        throw std::runtime_error("Image encoding failed");
    sdlIO::writeExact(io,
                      SDL_GetPointerProperty(SDL_GetIOProperties(encoded.get()),
                                             SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr),
                      static_cast<size_t>(size));
}
} // namespace

namespace imageIO {
SDLSurfaceOwner loadTga(wiz8::File& file)
{
    try {
        return loadTga(file.io());
    } catch (...) {
        return {nullptr, SDL_DestroySurface};
    }
}

void save(const char* path, SDL_IOStream* stream, SDL_Surface& surface, int quality)
{
    saveImage(outputFormat(path), stream, surface, quality);
}

void save(const char* path, wiz8::File& file, SDL_Surface& surface, int quality)
{
    save(path, file.io(), surface, quality);
}

void save(const char* path, SDL_Surface& surface, int quality)
{
    const auto format = outputFormat(path);
    auto file = wiz8::open_file(wiz8::user_path(path), wiz8::OpenMode::replace);
    saveImage(format, file->io(), surface, quality);
}
} // namespace imageIO
