#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "surrender/srNode.h"
#include "surrender/srTexture.h"
#include "compat/compiler.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srTypeRegistry.h"

class srGERD;

// VTABLE: WIZ8 0x005EC6C0 stTexture2D
// VTABLE: WIZ8 0x005EC704 srClassSupport<stTexture2D, srTexture, 65551>
class stTexture2D : public srClassSupport<stTexture2D, srTexture, 0x1000f> {
public:
    static const char* sGetClassName()
    {
        return "stTexture2D";
    }

    stTexture2D();
    virtual ~stTexture2D() override;
    stTexture2D& operator=(const stTexture2D&) = default;
    virtual std::uint32_t getTextureFrameHandle() override;
    virtual void getMipmapData(const MultiRequest& request) override;
    virtual void getMipmapLevelPartial(PartialRequest& request) override;
    virtual void invalidate() override;

protected:
    virtual void setupDefaultValues() override;

public:
    std::int32_t left;                     /* 0x54 */
    std::int32_t top;                      /* 0x58 */
    std::int32_t right;                    /* 0x5c */
    std::int32_t bottom;                   /* 0x60 */
    std::uint32_t frame_handle;   /* 0x64 */
    std::shared_ptr<srColorSurfaceIFace> surface; /* 0x68 */
};

// VTABLE: WIZ8 0x005EC748 stSurface2D
// VTABLE: WIZ8 0x005EC77C srClassSupport<stSurface2D, srNode, 65550>
class stSurface2D : public srClassSupport<stSurface2D, srNode, 0x1000e> {
public:
    enum { UPDATE_FULL_TILE = 1 };

    static const char* sGetClassName()
    {
        return "stSurface2D";
    }

    stSurface2D(std::shared_ptr<srColorSurfaceIFace> surface, std::int32_t width, std::int32_t height, srNode* parent, std::int32_t tile_size);

    virtual void traverse(TraverseInfo& info) override;
    virtual void process(const ProcessInfo& info, e_processType type) override;

    /* Rebase the tile texture texture_coordinates by the scale delta over the tile
       size, then store the new scale. Called by SetSurfaceScale. */
    void setScale(float scale); /* 0x0047E560 */
    /* The ortho tile render pass; process() forwards the renderer to it. */
    void DrawTiles(srGERD* renderer); /* 0x0047E100 */
    void enableTextureUpdateFlags(std::uint32_t flag);
    void setAlphaTestEnabled(bool enabled);
    void invalidateTiles();
    void updateRectangle(srGERD* renderer, void* pixels, std::int32_t pitch, std::int32_t left, std::int32_t top, std::int32_t right,
                         std::int32_t bottom);

    std::int32_t vertex_array_mask;               /* 0x13c */
    std::uint32_t shader_bits;            /* 0x140 */
    std::int32_t tile_size;                       /* 0x144 */
    std::int32_t columns;                         /* 0x148 */
    std::int32_t rows;                            /* 0x14c */
    std::int32_t width;                           /* 0x154 */
    std::int32_t height;                          /* 0x158 */
    std::vector<std::unique_ptr<stTexture2D>> tiles;
    float tile_u;                        /* 0x160 */
    float tile_v;                        /* 0x164 */
    std::int32_t field_168;
    float field_16c;
    float texture_coordinates[4][2]; /* 0x170: u, v per corner */
    float scale;                     /* 0x190 */
    std::int32_t texture_update_flags;
};
