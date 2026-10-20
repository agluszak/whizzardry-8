#include <cstdint>
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <vsg/state/ImageInfo.h>

#include "wiz8/texture_animation.h"
struct SDL_Surface;

enum W8TextureTriggerMode {
    W8_TEXTURE_TRIGGER_CONTINUOUS = 0,
    W8_TEXTURE_TRIGGER_RANDOM_FRAME = 1,
    W8_TEXTURE_TRIGGER_RANDOM_START = 2
};

class Texture;
struct TexturePlayback {
    std::vector<std::shared_ptr<Texture>> textures;
    int frame = 0;
    int direction = 1;
    W8TextureAnimationMode animation_mode = W8_TEXTURE_ANIM_LOOP;
    int initial_frame = 0;
    float frame_rate = 15.0f;
    std::uint64_t frame_tick = 0;
    std::optional<std::uint64_t> visual_frame;
    W8TextureTriggerMode trigger_mode = W8_TEXTURE_TRIGGER_CONTINUOUS;
    float probability = -1.0f;
    bool running = false;
};

class Texture {

public:
    struct Dimensions {
        std::uint32_t width;
        std::uint32_t height;
        // UI canvases must bypass power-of-two rounding and world texture reduction.
        bool native_size = false;
    };
    /* Texture filtering quality.
       setTextureDefaultMagFilter remaps DEFAULT (4) to GOOD. */
    enum e_filter {
        FILTER_NONE = 0,
        FILTER_FASTEST = 1,
        FILTER_GOOD = 2,
        FILTER_BEST = 3,
        FILTER_DEFAULT = 4
    };
    /* Dump: NONE / FASTEST / BEST, else DEFAULT. GOOD is not a mipmap
       enumerator. setTextureDefaultMipmap remaps DEFAULT (3) to FASTEST. */
    enum e_mipmap { MIPMAP_NONE = 0, MIPMAP_FASTEST = 1, MIPMAP_BEST = 2, MIPMAP_DEFAULT = 3 };
    enum e_wrap { WRAP_REPEAT = 0, WRAP_CLAMP = 1 };
    struct Parameters {
        e_filter mag = FILTER_DEFAULT;
        e_filter min = FILTER_DEFAULT;
        e_mipmap mipmap = MIPMAP_DEFAULT;
        e_wrap wrap_s = WRAP_REPEAT;
        e_wrap wrap_t = WRAP_REPEAT;
        float mipmap_bias = 0;
        bool operator==(const Parameters&) const = default;
    };

    static Parameters defaults;

    explicit Texture(TexturePlayback playback);
    Texture(std::filesystem::path path, bool preload);
    explicit Texture(std::shared_ptr<SDL_Surface> pixels = {});
    ~Texture();
    Texture(const Texture& other);

    Texture& operator=(const Texture& other);
    Texture* resolve(std::optional<std::uint64_t> visual_frame = {});
    // The current animation snapshot, without advancing its clock or random
    // triggers. Particle simulation advances before any views consume it.
    Texture* currentFrame();
    Dimensions getDimensions();
    void setDimensions(const Dimensions& dimensions);
    Parameters getParameters();
    void setMipmap(e_mipmap mipmap);
    void setMipmapBias(float bias);
    void setMagFilter(e_filter filter);
    void setMinFilter(e_filter filter);
    void setWrapS(e_wrap wrap);
    void setWrapT(e_wrap wrap);

    void invalidate();
    // The texture owns its graphics representation. Animated textures resolve
    // their current image here rather than freezing a frame at asset load time.
    vsg::ref_ptr<vsg::ImageInfo> imageInfo(int reduction = 0,
                                           std::optional<std::uint64_t> visual_frame = {});
    void setSurfacePtr(std::shared_ptr<SDL_Surface> pixels);
    SDL_Surface* getSurfacePtr() const { return surface.get(); }
    std::shared_ptr<SDL_Surface> surface;

public:
    void setupDefaultValues();

public:
    /* Bit indices into texture_flags_. */
    enum e_flag { FLAG_GENERATESURFACE_FAILURE = 0, FLAG_DIRTY_DEFAULTS = 1 };

    void setupDefaultValuesFromSurface(SDL_Surface* surface);

    Parameters parameters;
    Dimensions texture_dimensions_;
    std::uint32_t texture_flags_;

public:
    std::string name{"anonymous"};
    std::filesystem::path source_path;
    std::optional<TexturePlayback> playback;
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    void SetFrame(int frame);
    void AddTexture(std::shared_ptr<Texture> texture);
    int IsFinished() const;
    unsigned char Prepare();
    void UpdateFrame(std::optional<std::uint64_t> visual_frame = {});
    bool isSurfaceLoaded() const { return surface != nullptr; }
    SDL_Surface* getSurface() const { return surface.get(); }
    bool hasAlpha();
    void loadSurface();
    void releaseSurface();

private:
    vsg::ref_ptr<vsg::ImageInfo> image_info;
    Parameters image_parameters;
    int image_reduction = -1;
};
