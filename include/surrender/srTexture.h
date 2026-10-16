#pragma once

#include <memory>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>

#include "srClass.h"
struct SDL_Surface;

class srTexture : public srClass {
public:
    struct Dimensions {
        w8_ulong width;
        w8_ulong height;
        SDL_PixelFormat format;
        bool generate_mipmaps = true;
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
    };

    explicit srTexture(std::shared_ptr<SDL_Surface> pixels = {});
    ~srTexture() override;
    srTexture(const srTexture&) = default;

    srTexture& operator=(const srTexture& other);
    virtual srTexture* resolve();
    Dimensions getDimensions();
    void setDimensions(const Dimensions& dimensions);
    Parameters getParameters();
    void setMipmap(e_mipmap mipmap);
    void setMipmapBias(float bias);
    void setMagFilter(e_filter filter);
    void setMinFilter(e_filter filter);
    void setWrapS(e_wrap wrap);
    void setWrapT(e_wrap wrap);

    virtual void invalidate();
    void setSurfacePtr(std::shared_ptr<SDL_Surface> pixels);
    SDL_Surface* getSurfacePtr() const { return surface.get(); }
    SDL_Rect source_region{};
    std::shared_ptr<SDL_Surface> surface;

protected:
    virtual void setupDefaultValues();

public:
    /* Bit indices into texture_flags_. */
    enum e_flag { FLAG_GENERATESURFACE_FAILURE = 0, FLAG_DIRTY_DEFAULTS = 1 };

    friend class stSurface2D;
    void setupDefaultValuesFromSurface(SDL_Surface* surface);

    Parameters parameters;
    Dimensions texture_dimensions_;
    w8_ulong texture_flags_;
};
