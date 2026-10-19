#pragma once

#include <SDL3/SDL_iostream.h>

#include "wiz8/surface_storage.h"
namespace wiz8 {
class File;
}

namespace imageIO {
// Stream overloads borrow the stream. Loading starts at zero.
SDLSurfaceOwner load(const char* path, SDL_IOStream* stream);
SDLSurfaceOwner load(const char* path, wiz8::File& file);
SDLSurfaceOwner load(const char* path);
void save(const char* path, SDL_IOStream* stream, SDL_Surface& surface, int quality = 100);
void save(const char* path, wiz8::File& file, SDL_Surface& surface, int quality = 100);
void save(const char* path, SDL_Surface& surface, int quality = 100);

// Retail TGA metadata includes global palettes, palette-only files and mirrored origins.
SDLSurfaceOwner loadTga(SDL_IOStream* stream);
SDLSurfaceOwner loadTga(wiz8::File& file);
} // namespace imageIO
