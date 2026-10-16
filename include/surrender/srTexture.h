#pragma once

#include <SDL3/SDL_pixels.h>

#include "srTextureIFace.h"

class srTexture : public srTextureIFace {
public:
    virtual ~srTexture() override;

    /* Copies share the palette stored in Dimensions. */

    srTexture& operator=(const srTexture& other);
    virtual w8_ulong getTextureFrameHandle() override;
    virtual float getPriority() override;
    Dimensions getDimensions() override;
    void setDimensions(const Dimensions& dimensions);
    virtual void getMipmapData(const MultiRequest& request) override;
    virtual void getMipmapLevelPartial(PartialRequest& request) override;
    Parameters getParameters() override;
    void setMipmap(e_mipmap mipmap);
    void setMipmapBias(float bias);
    float getMipmapBias() const;
    void setPriority(float priority);
    void setDimensionsDirty();
    e_compression getCompression() const;
    void setCompression(e_compression compression);
    void enableHint(e_hint hint);
    void disableHint(e_hint hint);
    int isHintEnabled(e_hint hint) const;
    void setCorrection(e_correction correction);
    void setMagFilter(e_filter filter);
    void setMinFilter(e_filter filter);
    void setWrapS(e_wrap wrap);
    void setWrapT(e_wrap wrap);
    e_correction getCorrection() const;
    e_filter getMagFilter() const;
    e_filter getMinFilter() const;
    e_mipmap getMipmap() const;
    e_wrap getWrapS() const;
    e_wrap getWrapT() const;

protected:
    static void copyMipmapChain(SDL_Surface& source, const MultiRequest& request);

public:
    /* Bit indices into texture_flags_. */
    enum e_flag { FLAG_GENERATESURFACE_FAILURE = 0, FLAG_DIRTY_DEFAULTS = 1 };

protected:
    friend class stSurface2D;
    srTexture();
    static w8_ulong getNewFrameHandle();
    /* Allocates count consecutive frame handles and returns the first. */
    static w8_ulong getNewFrameHandles(w8_ulong count);
    static w8_ulong _frameHandle;
    void invalidateFrameHandle(w8_ulong handle);
    void setupDefaultValuesFromSurface(SDL_Surface* surface);

    w8_ulong packed_state;     /* 0x18: correction/mag/min/mipmap/wrap bits */
    float mipmap_bias;              /* 0x1c */
    Dimensions texture_dimensions_; /* 0x20 */
    float texture_priority;         /* 0x4c: getPriority; the ctor seeds 0.5f */
    w8_ulong texture_flags_;   /* 0x50 */
};
