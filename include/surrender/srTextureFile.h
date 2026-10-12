#pragma once

#include <cstdint>
#include "srColorSurface.h"
#include "srTexture.h"

#include <string>

// VTABLE: SURRENDER 0x1007752C
// class srClassSupport<srTextureFile, srTexture, 8466>

/* File-backed texture. Wizardry does not use it; it owns a parallel stTextureFile with the same
   interface. */
// VTABLE: SURRENDER 0x100774E8 srTextureFile
class srTextureFile : public srClassSupport<srTextureFile, srTexture, 0x2112> {
public:
    virtual ~srTextureFile() override;
    srTextureFile(std::string file_name = {}, std::int32_t cached = 0);

    srTextureFile& operator=(const srTextureFile& other);

    // FUNCTION: SURRENDER 0x1005FF10
    static const char* sGetClassName()
    {
        return "srTextureFile";
    }

    const std::string& getFileName() const;
    void setFileName(std::string file_name);
    void setCached(std::int32_t cached);
    bool isSurfaceLoaded() const;
    void loadSurface();
    void releaseSurface();

    virtual void dump(std::ostream& stream) override;
    virtual std::uint32_t getTextureFrameHandle() override;
    virtual void getMipmapData(const MultiRequest& request) override;
    virtual void getMipmapLevelPartial(PartialRequest& request) override;
    virtual void invalidate() override;

protected:
    virtual void setupDefaultValues() override;

    std::int32_t cached;
    std::string file_name;
    std::unique_ptr<srColorSurfaceIFace> surface;
    std::uint32_t frame_handle;
};

