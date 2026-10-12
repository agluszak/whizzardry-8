#pragma once

#include <cstdint>
#include "srPixelConvert.h"
#include "srTextureIFace.h"

class SR_DLL_IMPORT srTexture
    : public srClassSupport<srTexture, srTextureIFace, 0x2110> {
public:
    virtual ~srTexture() override;
    static const char* sGetClassName();
    /* Copies share the palette stored in Dimensions. */

    srTexture& operator=(const srTexture& other);
    virtual void dump(std::ostream& stream) override;
    virtual std::uint32_t getTextureFrameHandle() override;
    virtual float getPriority() override;
    Dimensions getDimensions() override;
    void setDimensions(const Dimensions& dimensions);
    virtual void getMipmapData(const MultiRequest& request) override;
    virtual void getMipmapLevelPartial(PartialRequest& request) override;
    Parameters getParameters() override;
    srFilter* getFilter() const;
    void setFilter(srFilter* filter);
    void setMipmap(e_mipmap mipmap);
    void setMipmapBias(float bias);
    float getMipmapBias() const;
    void setPriority(float priority);
    void setDimensionsDirty();
    e_compression getCompression() const;
    void setCompression(e_compression compression);
    void enableHint(e_hint hint);
    void disableHint(e_hint hint);
    std::int32_t isHintEnabled(e_hint hint) const;
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
    static void copyMipmapChain(srColorSurfaceIFace& source, const MultiRequest& request);

public:
    /* Bit indices into texture_flags_. */
    enum e_flag { FLAG_GENERATESURFACE_FAILURE = 0, FLAG_DIRTY_DEFAULTS = 1 };

protected:
    friend class stSurface2D;
    srTexture();
    static std::uint32_t getNewFrameHandle();
    /* Allocates count consecutive frame handles and returns the first. */
    static std::uint32_t getNewFrameHandles(std::uint32_t count);
    static std::uint32_t _frameHandle;
    void invalidateFrameHandle(std::uint32_t handle);
    void setupDefaultValuesFromSurface(srColorSurfaceIFace* surface);

    std::uint32_t packed_state;     /* 0x18: correction/mag/min/mipmap/wrap bits */
    float mipmap_bias;              /* 0x1c */
    Dimensions texture_dimensions_; /* 0x20 */
    float texture_priority;         /* 0x4c: getPriority; the ctor seeds 0.5f */
    std::uint32_t texture_flags_;   /* 0x50 */
};

