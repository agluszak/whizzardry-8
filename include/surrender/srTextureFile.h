#pragma once

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
    srTextureFile(std::string file_name = {}, int cached = 0);

    srTextureFile& operator=(const srTextureFile& other);

    // FUNCTION: SURRENDER 0x1005FF10
    static const char* sGetClassName()
    {
        return "srTextureFile";
    }

    const std::string& getFileName() const;
    void setFileName(std::string file_name);
    void setCached(int cached);
    int isSurfaceLoaded() const;
    void loadSurface();
    void releaseSurface();

    virtual void dump(std::ostream& stream) override;
    virtual srClass* vInstance() override;
    virtual w8_ulong getTextureFrameHandle() override;
    virtual void getMipmapData(MultiRequest& request) override;
    virtual void getMipmapLevelPartial(PartialRequest& request) override;
    virtual void invalidate() override;

protected:
    virtual ~srTextureFile() override;
    virtual void setupDefaultValues() override;

    int cached;
    std::string file_name;
    srColorSurfaceIFace* surface;
    w8_ulong frame_handle;
};

W8_ABI_ASSERT(sizeof(srTextureFile) == 0x64, "srTextureFile_must_be_0x64");
