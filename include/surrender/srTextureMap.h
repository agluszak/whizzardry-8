#pragma once

#include <memory>

#include "srTexture.h"

// VTABLE: SURRENDER 0x10077578 srTextureMap
// class srTextureMap
class SR_DLL_IMPORT srTextureMap : public srTexture {
public:
    virtual ~srTextureMap() override;
    srTextureMap(std::shared_ptr<srColorSurfaceIFace> surface = {});

    srTextureMap& operator=(const srTextureMap& other);

public:
    virtual w8_ulong getTextureFrameHandle() override;
    virtual void getMipmapData(const MultiRequest& request) override;
    void setSurfacePtr(std::shared_ptr<srColorSurfaceIFace> surface);
    srColorSurfaceIFace* getSurfacePtr() const;
    virtual void invalidate() override;

protected:
    virtual void setupDefaultValues() override;
    std::shared_ptr<srColorSurfaceIFace> surface;
    w8_ulong frame_handle; /* 0x58: ctor stores getNewFrameHandle() */
};
