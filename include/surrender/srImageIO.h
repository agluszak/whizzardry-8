#pragma once

#include <SDL3/SDL_iostream.h>

#include "srColorSurface.h"
namespace wiz8 {
class File;
}

namespace srImage {
// Surface allocation requires srInit().
// Stream overloads borrow the stream. Loading starts at zero; description queries restore its cursor.
std::unique_ptr<srColorSurfaceIFace> load(const char* path, SDL_IOStream* stream);
std::unique_ptr<srColorSurfaceIFace> load(const char* path, wiz8::File& file);
std::unique_ptr<srColorSurfaceIFace> load(const char* path);
bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path,
              SDL_IOStream* stream);
bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path, wiz8::File& file);
void describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path);
void save(const char* path, SDL_IOStream* stream, srColorSurfaceIFace& surface, int quality = 100);
void save(const char* path, wiz8::File& file, srColorSurfaceIFace& surface, int quality = 100);
void save(const char* path, srColorSurfaceIFace& surface, int quality = 100);

// Retail TGA metadata includes global palettes, palette-only files and mirrored origins.
std::unique_ptr<srColorSurface> loadTga(SDL_IOStream* stream);
std::unique_ptr<srColorSurface> loadTga(wiz8::File& file);
} // namespace srImage
