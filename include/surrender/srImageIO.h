#pragma once

#include "srColorSurface.h"

class srBinIStream;
class srBinOStream;
namespace wiz8 { class File; }

namespace srImage {
// Surface allocation requires srInit().
// Stream overloads borrow the stream. Loading starts at zero; description queries restore its cursor.
srColorSurfaceIFace* load(const char* path, srBinIStream& stream);
srColorSurfaceIFace* load(const char* path, wiz8::File& file);
srColorSurfaceIFace* load(const char* path);
bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path, srBinIStream& stream);
bool describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path, wiz8::File& file);
void describe(srColorSurfaceIFace::SurfaceDesc& description, const char* path);
void save(const char* path, srBinOStream& stream, srColorSurfaceIFace& surface, int quality = 100);
void save(const char* path, wiz8::File& file, srColorSurfaceIFace& surface, int quality = 100);
void save(const char* path, srColorSurfaceIFace& surface, int quality = 100);

// Retail TGA metadata includes global palettes, palette-only files and mirrored origins.
srColorSurface* loadTga(srBinIStream& stream);
srColorSurface* loadTga(wiz8::File& file);
} // namespace srImage
