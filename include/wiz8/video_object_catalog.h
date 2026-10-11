#ifndef WIZ8_VIDEO_OBJECT_CATALOG_H
#define WIZ8_VIDEO_OBJECT_CATALOG_H
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "vobject.h"
#include "vsurface.h"

struct W8VideoObjectSlot {
    int first_frame;
    short y_offset;
};

enum W8VideoStorageKind { W8_VIDEO_STORAGE_OBJECT = 0, W8_VIDEO_STORAGE_SURFACE = 1 };

struct W8VideoFrame {
    std::string path;
    W8VideoStorageKind storage_kind;
    std::unique_ptr<SGPVObject> object;
    std::unique_ptr<SGPVSurface> surface;
};

extern std::vector<W8VideoObjectSlot> g_video_slots;
extern std::vector<W8VideoFrame> g_video_frames;

// Load first, then publish an item image without changing the fixed UI catalog.
int RegisterItemVideoImage(std::string path);
void ReleaseItemVideoImages();

short GetCatalogVideoObjectYOffset(int object);
/* 0x00549420: load the catalog frame and return its sprite object; optional out
   receives the slot y_offset. */
SGPVObject& GetCatalogVideoObject(int object, int frame, int* y_offset_out = nullptr);
std::pair<int, int> GetCatalogImageSize(int object, int frame, int image);
/* 0x00549700: the image's own offset inside its frame, read from the video object's
   ETRLE subregion table. */
void GetCatalogImagePosition(int object, int frame, int image, short* x, short* y);
void DrawCatalogImage(SGPVSurface& target, int object, int frame, short image, int left, int top,
                      UINT32 mode, blt_fx* effects);
void InvalidateCatalogImageRect(int object, int frame, int image, int left, int top, int flags);
void DrawCatalogImageAndInvalidate(SGPVSurface& target, int object, int frame, int image, int left,
                                   int top, UINT32 mode, blt_fx* effects);
unsigned char BlitCatalogSurfaceRectTo16BPP(SGPVSurface& target, int left, int top, int right, int bottom,
                                            int object, int source_x, int source_y);

void EnsureCatalogFrameLoaded(int object, int frame);
Palette16 CopyCatalogImagePalette16BPP(int object, int frame);
SGPVSurface& GetCatalogFrameSurface(unsigned int object, unsigned int frame);

void ReleaseLoadedVideoFrames(void);

#endif
