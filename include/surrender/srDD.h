#pragma once

#include <span>

#include "srFlags.h"
#include "srMath.h"
#include "srRendererDefs.h"
#include "srShader.h"

struct SDL_Window;

class srDD {
public:
    // Backend identity only. Cache policy and CPU mip storage belong to GERD.
    struct Texture {
        w8_ulong handle = 0;
        w8_ulong size = 0;
    };
    struct TextureRegion {
        w8_ulong x, y, width, height;
    };
    /* Device identity returned by getDriverInfo. */
    struct DriverInfo {
        w8_ulong driver_id;
        char name[64];
    };
    /* Capabilities and limits returned by getInfo. */
    struct Info {
        enum { PIXEL_TEXTURE_STATISTICS = 0x10u, RELEASE_SURFACE_AFTER_BIND = 0x20u };
        /* openWindowInternal rejects back-buffer dimensions above these
           maximums. */
        w8_ulong max_back_buffer_width;
        w8_ulong max_back_buffer_height;
        /* changeTexture tests RELEASE_SURFACE_AFTER_BIND. */
        w8_ulong flags;
        /* SDL GPU defaults: 0x100; createRenderer passes it to each
           Renderer as its batch limit. */
        w8_ulong renderer_batch_limit;
        /* Device texture RAM in bytes; 0 means unlimited. */
        w8_ulong texture_ram;
        /* Texture-dimension clamps applied by evaluateTextureDimensions. */
        w8_ulong max_texture_stages;
        w8_ulong texture_min_dim;
        w8_ulong texture_max_dim;
        w8_ulong texture_max_aspect;
    };
    // CPU accumulation remains in GERD; the backend clears color, depth and stencil.
    struct ClearValues {
        glm::vec4 color;
        float depth;
        w8_ulong stencil;
    };
    /* Scissor rectangle {left, top, right, bottom}. */
    struct Scissor {
        w8_ulong left, top, right, bottom;
    };
    // View rectangle and depth range used by SDL GPU.
    struct ViewPort {
        w8_long x, y, width, height;
        /* Depth range forwarded by applyViewStateChanges. */
        float min_depth, max_depth;
    };
    /* srGERD::setTextureParameters repacks the texture's Parameters into
       this per-stage record and passes it to the device. */
    struct TexParms {
        w8_ulong packed;
        float mipmap_bias;
    };

    /* srDD_OpenGL clearBuffers: bit 0 clears GL_COLOR_BUFFER_BIT, bit 1
       GL_DEPTH_BUFFER_BIT and bit 2 GL_STENCIL_BUFFER_BIT. */
    enum e_buffer { BUFFER_COLOR = 1, BUFFER_DEPTH = 2, BUFFER_STENCIL = 4 };
    /* srDD_OpenGL setCullMode: 0 disables GL_CULL_FACE, 1 culls GL_BACK and
       2 culls GL_FRONT. */
    enum e_cullMode { CULL_NONE = 0, CULL_BACK = 1, CULL_FRONT = 2 };
    /* srDD_OpenGL setPolygonMode: glPolygonMode(GL_FRONT_AND_BACK, ...) with
       GL_POINT, GL_LINE and GL_FILL. */
    enum e_polygonMode { POLYGON_POINT = 0, POLYGON_LINE = 1, POLYGON_FILL = 2 };
    enum e_driverID {};

    virtual ~srDD() = default;
    virtual Info getInfo() = 0;
    virtual bool openWindow(w8_ulong width, w8_ulong height) = 0;
    virtual void closeWindow() = 0;
    virtual void flushFrame() = 0;
    virtual void flipFrame() = 0;
    virtual void clearBuffers(const srFlags<e_buffer>& buffers) = 0;
    virtual void lockBuffer() = 0;
    virtual void unlockBuffer() = 0;
    virtual void readPixels(w8_long x, w8_long y, bool vertical, std::span<w8_ulong> pixels) = 0;
    virtual void writePixels(w8_long x, w8_long y, bool vertical,
                             std::span<const w8_ulong> pixels) = 0;
    virtual void fillPixels(w8_long x, w8_long y, w8_long count, w8_ulong pixel) = 0;
    virtual void setSwapInterval(w8_ulong interval) = 0;
    virtual void setScissor(const Scissor& scissor) = 0;
    virtual void setViewPort(const ViewPort& viewport) = 0;
    virtual void setClearValues(const ClearValues& values) = 0;
    virtual void setFogColor(const glm::vec4& color) = 0;
    virtual void setShader(const srShader& shader) = 0;
    virtual void setTextureParameters(w8_ulong stage, const TexParms& parms) = 0;
    virtual void bindTexture(w8_ulong stage, const Texture& texture) = 0;
    virtual void deleteTexture(Texture& texture) = 0;
    virtual Texture createTexture(w8_ulong width, w8_ulong height, w8_ulong levels) = 0;
    // ARGB32 bytes, with pitch in bytes. The backend copies the region before returning.
    virtual void uploadTexture(const Texture& texture, w8_ulong level, TextureRegion region,
                               std::span<const unsigned char> pixels, w8_ulong pitch) = 0;
    virtual bool createContext(SDL_Window* window) = 0;
    virtual void deleteContext() = 0;
    virtual DriverInfo getDriverInfo() = 0;
    virtual void setPolygonMode(e_polygonMode mode) = 0;
    virtual void setCullMode(e_cullMode mode) = 0;
    virtual void setProjectionMatrix(const glm::mat4& matrix) = 0;
    virtual void setVertexArrayInfo(const srRendererDefs::VertexArrayInfo* info) = 0;
    virtual void drawElements(srRendererDefs::e_primitive primitive,
                              std::span<const w8_ulong> indices) = 0;
    virtual void drawArrays(srRendererDefs::e_primitive primitive, w8_long first,
                            w8_ulong count) = 0;
    virtual void setPolygonOffset(w8_long offset) = 0;
};
