#include "image_io.h"

#include "surrender/srBinIStream.h"
#include "surrender/srBinOStream.h"
#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srExporter.h"
#include "surrender/srImporter.h"

#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using IOStream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

class ImageHandler : public srSurfaceIOManager::SurfaceImporter,
                     public srSurfaceIOManager::SurfaceExporter {
public:
    explicit ImageHandler(const char* type) : type(type)
    {
        auto* manager = srCore.getSurfaceIOManager();
        addToImporters(manager, type);
        if (strcmp(type, "jpg") == 0) {
            addToImporters(manager, "jpeg");
            addToExporters(manager, "jpg");
            addToExporters(manager, "jpeg");
        }
    }

    ~ImageHandler() override
    {
        auto* manager = srCore.getSurfaceIOManager();
        removeFromImporters(manager);
        removeFromExporters(manager);
    }

    const char* getTypeName() const override { return type; }

    srColorSurfaceIFace* importSurface(srBinIStream& stream,
                                     const srSurfaceIOManager::ImportInfo&) override
    {
        const auto start = stream.tell();
        const auto size = stream.getSize();
        if (!stream.good() || size <= start) return nullptr;
        std::vector<unsigned char> data(size - start);
        stream.read(data.data(), static_cast<w8_ulong>(data.size()));
        if (!stream.good()) return nullptr;
        IOStream input(SDL_IOFromConstMem(data.data(), data.size()), SDL_CloseIO);
        if (!input) return nullptr;
        Surface decoded(IMG_LoadTyped_IO(input.get(), false, type), SDL_DestroySurface);
        if (!decoded) return nullptr;
        Surface converted(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_BGRA32),
                          SDL_DestroySurface);
        if (!converted) return nullptr;
        auto* result = new srColorSurface(srPixelConvert::SURFACE_BGRA32,
                                         converted->w, converted->h);
        for (int y = 0; y < converted->h; ++y) {
            memcpy(static_cast<unsigned char*>(result->getDataPtr()) + y * result->getPitch(),
                   static_cast<unsigned char*>(converted->pixels) + y * converted->pitch,
                   converted->w * 4);
        }
        return result;
    }

    void exportSurface(srBinOStream& stream, srColorSurfaceIFace& source,
                       const srSurfaceIOManager::ExportInfo& options) override
    {
        int quality = 100;
        if (options.option_string) {
            std::string text(options.option_string);
            for (char& ch : text) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const auto key = text.find("QUALITY");
            const auto equal = key == std::string::npos ? key : text.find('=', key);
            if (equal != std::string::npos) {
                double value = std::strtod(text.c_str() + equal + 1, nullptr);
                if (!std::isfinite(value)) value = 1.0;
                quality = static_cast<int>(std::clamp(value, 0.0, 1.0) * 100.0);
            }
        }
        std::unique_ptr<srColorSurfaceIFace, void (*)(srColorSurfaceIFace*)> copy(
            new srColorSurface(srPixelConvert::SURFACE_RGB24, source.getWidth(), source.getHeight()),
            [](srColorSurfaceIFace* surface) { surface->release(); });
        copy->copy(source);
        Surface pixels(SDL_CreateSurfaceFrom(source.getWidth(), source.getHeight(),
                                              SDL_PIXELFORMAT_RGB24, copy->getDataPtr(),
                                              copy->getPitch()), SDL_DestroySurface);
        if (!pixels) throw srIOManager::Error(SDL_GetError());
        IOStream output(SDL_IOFromDynamicMem(), SDL_CloseIO);
        if (!output || !IMG_SaveJPG_IO(pixels.get(), output.get(), false, quality)) {
            throw srIOManager::Error(SDL_GetError());
        }
        const auto size = SDL_GetIOSize(output.get());
        if (size <= 0 || size > std::numeric_limits<w8_ulong>::max()) {
            throw srIOManager::Error("Invalid JPEG output size");
        }
        const void* data = SDL_GetPointerProperty(SDL_GetIOProperties(output.get()),
                                                 SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
        if (!data) throw srIOManager::Error(SDL_GetError());
        stream.seek(0, srBinStream::SR_SEEK_BEGIN);
        stream.write(data, static_cast<w8_ulong>(size));
        if (!stream.good()) throw srIOManager::Error("JPEG output stream is corrupt");
    }

private:
    const char* type;
};

struct ImageHandlers {
    ImageHandler jpeg{"jpg"};
    ImageHandler targa{"tga"};
    ImageHandler bmp{"bmp"};
    ImageHandler pcx{"pcx"};
};
ImageHandlers* handlers = nullptr;
} // namespace

void srInitImageIO()
{
    if (!handlers) handlers = new ImageHandlers;
}

void srExitImageIO()
{
    delete handlers;
    handlers = nullptr;
}
