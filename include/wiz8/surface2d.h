#pragma once

#include <memory>
#include <vector>

#include "compat/compiler.h"
#include "surrender/srNode.h"
#include "surrender/srTexture.h"

struct SDL_Surface;

class srGERD;

// VTABLE: WIZ8 0x005EC6C0 stTexture2D
class stTexture2D : public srTexture {
public:
    stTexture2D();
    virtual ~stTexture2D() override;
    stTexture2D& operator=(const stTexture2D&) = default;
    virtual w8_ulong getTextureFrameHandle() override;
    virtual void getMipmapData(const MultiRequest& request) override;
    virtual void getMipmapLevelPartial(PartialRequest& request) override;
    virtual void invalidate() override;

protected:
    virtual void setupDefaultValues() override;

public:
    int left;                     /* 0x54 */
    int top;                      /* 0x58 */
    int right;                    /* 0x5c */
    int bottom;                   /* 0x60 */
    w8_ulong frame_handle;   /* 0x64 */
    std::shared_ptr<SDL_Surface> surface; /* 0x68 */
};

// VTABLE: WIZ8 0x005EC748 stSurface2D
class stSurface2D : public srNode {
public:
    stSurface2D(std::shared_ptr<SDL_Surface> surface, int width, int height, srNode* parent,
                int tile_size);

    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;

    /* Rebase the tile texture texture_coordinates by the scale delta over the tile
       size, then store the new scale. Called by SetSurfaceScale. */
    void setScale(float scale); /* 0x0047E560 */
    /* The ortho tile render pass; process() forwards the renderer to it. */
    void DrawTiles(srGERD* renderer); /* 0x0047E100 */
    void setAlphaTestEnabled(bool enabled);
    void invalidateTiles();
    void updateRectangle(srGERD* renderer, int left, int top, int right, int bottom);

    int vertex_array_mask;               /* 0x13c */
    unsigned int shader_bits;            /* 0x140 */
    int tile_size;                       /* 0x144 */
    int columns;                         /* 0x148 */
    int rows;                            /* 0x14c */
    int width;                           /* 0x154 */
    int height;                          /* 0x158 */
    std::vector<std::unique_ptr<stTexture2D>> tiles;
    float tile_u;                        /* 0x160 */
    float tile_v;                        /* 0x164 */
    int field_168;
    float field_16c;
    float texture_coordinates[4][2]; /* 0x170: u, v per corner */
    float scale;                     /* 0x190 */
};
