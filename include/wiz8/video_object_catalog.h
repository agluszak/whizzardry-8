#ifndef WIZ8_VIDEO_OBJECT_CATALOG_H
#define WIZ8_VIDEO_OBJECT_CATALOG_H
#include <memory>
#include <utility>
#include <string_view>
#include <SDL3/SDL_surface.h>

#include "himage.h"
#include "sprite.h"

struct W8VideoObjectSlot {
    int first_frame;
    short y_offset;
};

enum W8VideoStorageKind { W8_VIDEO_STORAGE_OBJECT = 0, W8_VIDEO_STORAGE_SURFACE = 1 };

struct W8CatalogArtwork {
    std::string_view path;
    W8VideoStorageKind storage_kind;
};

short GetCatalogVideoObjectYOffset(int object);
std::string_view GetCatalogVideoObjectPath(int object);
W8VideoStorageKind GetCatalogStorageKind(int object);
/* 0x00549420: load the catalog frame and return its sprite sheet; optional out
   receives the slot y_offset. */
const SpriteSheet& GetCatalogVideoObject(int object, int frame, int* y_offset_out = nullptr);
std::shared_ptr<const SpriteSheet> GetCatalogSpriteSheet(int object, int frame,
                                                        int* y_offset_out = nullptr);
std::pair<int, int> GetSpriteSize(const SpriteSheet& sprites, int image);
void DrawSpriteAndInvalidate(SDL_Surface& target, const SpriteSheet& sprites, int image,
                             int left, int top, SpriteDraw options = {});
std::pair<int, int> GetCatalogImageSize(int object, int frame, int image);
/* 0x00549700: the image's own offset inside its frame, read from the video object's
   ETRLE subregion table. */
void GetCatalogImagePosition(int object, int frame, int image, short* x, short* y);
void DrawCatalogImage(SDL_Surface& target, int object, int frame, short image, int left, int top,
                      SpriteDraw options = {});
void InvalidateCatalogImageRect(int object, int frame, int image, int left, int top, int flags);
void DrawCatalogImageAndInvalidate(SDL_Surface& target, int object, int frame, int image, int left,
                                   int top, SpriteDraw options = {});
unsigned char BlitCatalogSurfaceRectTo16BPP(SDL_Surface& target, int left, int top, int right, int bottom,
                                            int object, int source_x, int source_y);

Palette16 CopyCatalogImagePalette16BPP(int object, int frame);
SDL_Surface& GetCatalogFrameSurface(unsigned int object, unsigned int frame);

void ReleaseLoadedVideoFrames(void);

#endif
