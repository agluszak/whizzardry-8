#pragma once

#include <cstdint>
#include "srFlags.h"
#include "srMath.h"
#include "srPixelConvert.h"
#include "srRendererDefs.h"
#include "srShader.h"
#include <span>

class srARGB;
class srDD;

// Dynamic driver entrypoints.
enum { SR_DD_MIN_API_VERSION = 0x128 };
typedef std::uint32_t(__cdecl* srDDGetDriverApiVersionFn)();
typedef const char*(__cdecl* srDDGetDriverNameFn)();
typedef void(__cdecl* srDDConfigureDriverFn)(const char* configuration);
typedef std::uint32_t(__cdecl* srDDGetDeviceCountFn)();
typedef const char*(__cdecl* srDDGetDeviceNameFn)(std::uint32_t index);
typedef srDD*(__cdecl* srDDInitDeviceFn)(std::uint32_t index);

/* Empty base class; its original name is unknown. */
class srDDEmptyBase {};

class srDD : public srDDEmptyBase {
public:
    /* Device palette record handed to bindPalette/deletePalette. */
    struct Palette {
        const srARGB* data;
        std::uint32_t size;
        std::uint32_t flags;
    };
    /* Device pixel format: srPixelConvert::PixelFormat without its trailing FourCC word. */
    struct PixelFormat {
        std::uint8_t red_bits;
        std::uint8_t red_shift;
        std::uint8_t green_bits;
        std::uint8_t green_shift;
        std::uint8_t blue_bits;
        std::uint8_t blue_shift;
        std::uint8_t alpha_bits;
        std::uint8_t alpha_shift;
        /* +8 conversion class of srPixelConvert::PixelFormat, copied
           verbatim by convertPixelFormat. */
        srPixelConvert::e_colorModel color_model;
        srPixelConvert::e_pixelSize pixel_size;
    };
    /* Device texture record handed to bindTexture/deleteTexture; levels holds the per-level data
       pointers. */
    struct Texture {
        /* Shared initialization in GERD allocation/deletion. reset is a
           descriptive name; format, deleted and resident are set separately. */
        void reset()
        {
            flags = 0;
            size = 0;
            last_use = 0;
            priority = 0.5f;
            resident_data = 0;
            width = 0;
            height = 0;
            first_level = 0;
            last_level = 0;
            format_index = 0;
            parameter = 0;
            resident_size = 0;
            for (std::int32_t level = 0; level < 12; ++level) {
                levels[level] = 0;
            }
        }

        std::uint32_t flags;
        PixelFormat format;
        float priority;
        std::uint32_t last_use;
        std::uint32_t size;
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t first_level;
        std::uint32_t last_level;
        std::uint32_t format_index;
        std::uint32_t parameter;
        void* levels[12];
        /* Resident device-surface record and its byte size. */
        std::uint32_t resident_data;
        std::uint32_t resident_size;
        /* markTextureAsDeleted sets this once the texture is on the
           GERD-side deleted list. */
        std::uint32_t deleted;
        std::uint32_t resident;
    };
    /* Six-dword buffer command handed to bufferOp. Opcodes: 0 lock, 1 unlock, 2 read row, 3 write
       row, 4 horizontal fill, 6 read column, 7 write column. Lock and unlock fill only the leading
       dwords. */
    struct BufferCommand {
        std::uint32_t flags;
        std::uint32_t opcode;
        void* data;
        std::int32_t x;
        std::int32_t y;
        std::int32_t count;
    };
    /* Device identity returned by getDriverInfo. */
    struct DriverInfo {
        std::uint32_t dd_api_version;
        std::uint32_t driver_id;
        char name[64];
        char api_name[64];
    };
    /* getInfo output record. The nine trailing strings are the device identity fields. */
    struct Info {
        enum { PIXEL_TEXTURE_STATISTICS = 0x10u, RELEASE_SURFACE_AFTER_BIND = 0x20u };
        /* openWindowInternal rejects back-buffer dimensions above these
           maximums. */
        std::uint32_t max_back_buffer_width;
        std::uint32_t max_back_buffer_height;
        /* SDL GPU defaults: 4. */
        std::uint32_t unknown_08_;
        /* SDL GPU defaults: 0x10. */
        std::uint32_t unknown_0c_;
        /* SDL GPU defaults: 1.0f / 65536.0f. */
        float unknown_10_;
        float unknown_14_;
        /* changeTexture tests bit 5; getDepthBufferType reads bit 3. */
        std::uint32_t flags;
        /* SDL GPU defaults: 0x100; createRenderer passes it to each
           Renderer as its batch limit. */
        std::uint32_t renderer_batch_limit;
        /* SDL GPU defaults: 0x3b808081. */
        std::uint32_t unknown_20_;
        /* Device texture RAM in bytes; dumpTextureCache prints it in kB and
           treats 0 as "infinite" (no residency percentage). */
        std::uint32_t texture_ram;
        /* Texture-dimension clamps applied by evaluateTextureDimensions. */
        std::uint32_t max_texture_stages;
        std::uint32_t texture_min_dim;
        std::uint32_t texture_max_dim;
        std::uint32_t texture_max_aspect;
        /* SDL GPU defaults: 1. getHardwareID result; e_hardwareID is an
           empty enum and cannot be the field type. */
        std::uint32_t hardware_id;
        /* Device identity strings in getter order: device name, vendor,
           platform, driver name, vendor, version, hardware chipset, name,
           vendor. */
        char text[9][0x40];
    };
    /* getStatistics output record. */
    struct Statistics {
        double texture_transfer;
        double pixels_drawn;
        std::uint32_t value_10;
        std::uint32_t value_14;
        std::uint32_t triangles_received;
        /* Retail GERD::getStatistics copies +0x1c into its vertex-index
           counter and +0x20 into its vertices-transferred counter; the
           Glide2x driver accumulates per-call vertex counts into +0x20. */
        std::uint32_t vertex_indices;
        std::uint32_t vertices_transferred;
        std::uint32_t value_24;
    };
    /* getDisplayMode matches {width, height, depth} triples. */
    struct WindowInfo {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t depth;
    };
    /* srGERD::openWindowInternal forwards the requested backbuffer size and
       display-mode index; the device reports the back-buffer count in the
       single-dword result. */
    struct OpenInfo {
        std::uint32_t width;
        std::uint32_t height;
        std::int32_t display_mode;
    };
    struct OpenResult {
        std::uint32_t back_buffer_type;
    };
    /* setClearColor clamps color, accumClear clamps accum to [-1,1] per channel and setClearDepth
       clamps depth to [0,1]. */
#pragma pack(push, 4)
    struct ClearValues {
        glm::vec4 color;
        glm::vec4 accum;
        double depth;
        std::uint32_t stencil;
    };
#pragma pack(pop)
    /* Scissor rectangle {left, top, right, bottom}. */
    struct Scissor {
        std::uint32_t left, top, right, bottom;
    };
    /* setViewPort receives (x, y, width, height) plus four opaque trailing
       slots applyViewStateChanges copies through unchanged. */
    struct ViewPort {
        std::int32_t x, y, width, height;
        /* Depth range forwarded by applyViewStateChanges. */
        std::uint32_t extra[4];
    };
    /* Frame state handed to update(): dirty bits, gamma, a constant 1.0f, swap interval, antialias
       mode and enable bit 0. */
    struct Update {
        enum {
            UPDATE_ENABLE = 0x01u,
            UPDATE_SWAP_INTERVAL = 0x02u,
            UPDATE_GAMMA = 0x04u,
            UPDATE_ANTIALIAS = 0x08u
        };
        std::uint32_t flags;
        srVector3T<float> gamma;
        float value_10;
        std::uint32_t swap_interval;
        std::uint32_t antialias;
        std::uint32_t enabled;
    };
    /* srGERD::setTextureParameters repacks the texture's Parameters into
       this per-stage record and passes it to the device. */
    struct TexParms {
        std::uint32_t packed;
        float mipmap_bias;
    };

    enum e_error { ERROR_NONE = 0 };
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
    enum e_hardwareID {};

    virtual ~srDD() = 0;
    virtual Info getInfo() = 0;
    virtual std::span<const WindowInfo> getWindowList() = 0;
    virtual std::span<const PixelFormat> getTextureFormats() = 0;
    virtual Statistics getStatistics() = 0;
    virtual void resetStatistics() = 0;
    virtual void extCommand(std::uint32_t command, void* data, std::uint32_t size) = 0;
    virtual bool isBusy() = 0;
    virtual e_error openWindow(const OpenInfo& info, OpenResult& result) = 0;
    virtual void closeWindow() = 0;
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void flushFrame() = 0;
    virtual void flipFrame(const Scissor* first, const Scissor* second, std::uint32_t value) = 0;
    virtual void clearBuffers(const srFlags<e_buffer>& buffers) = 0;
    virtual e_error bufferOp(const BufferCommand& command) = 0;
    virtual void update(const Update& values) = 0;
    virtual void setScissor(const Scissor& scissor) = 0;
    virtual void setViewPort(const ViewPort& viewport) = 0;
    virtual void setClearValues(const ClearValues& values) = 0;
    virtual void setFogColor(const glm::vec4& color) = 0;
    virtual void setShader(const srShader& shader) = 0;
    virtual void setTextureParameters(std::uint32_t stage, const TexParms& parms) = 0;
    virtual void bindTexture(std::uint32_t stage, Texture& texture) = 0;
    virtual void deleteTexture(Texture& texture) = 0;
    virtual void texImage(Texture& texture, std::uint32_t level) = 0;
    virtual void texSubImage(Texture& texture, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                             std::uint32_t d, std::uint32_t e) = 0;
    virtual void setGlobalPalette(std::uint32_t* palette, std::uint32_t count) = 0;
    virtual void bindPalette(Palette& palette) = 0;
    virtual void deletePalette(Palette& palette) = 0;
    virtual e_error createContext(std::uintptr_t window) = 0;
    virtual void deleteContext() = 0;
    virtual DriverInfo getDriverInfo() = 0;
    virtual void fence() = 0;
    virtual PixelFormat getBufferPixelFormat() = 0;
    virtual void preBindTexture(std::uint32_t stage, Texture& texture) = 0;
    virtual void setPolygonMode(e_polygonMode mode) = 0;
    virtual void setCullMode(e_cullMode mode) = 0;
    virtual void setProjectionMatrix(const glm::mat4& matrix,
                                     srRendererDefs::e_matrixType type) = 0;
    virtual void setVertexArrayInfo(const srRendererDefs::VertexArrayInfo* info) = 0;
    virtual void drawElements(srRendererDefs::e_primitive primitive, std::uint32_t count,
                              srRendererDefs::e_indexType type, const void* indices) = 0;
    virtual void drawArrays(srRendererDefs::e_primitive primitive, std::int32_t first,
                            std::uint32_t count) = 0;
    virtual void setPolygonOffset(std::int32_t offset) = 0;
};

inline srDD::~srDD() {}
