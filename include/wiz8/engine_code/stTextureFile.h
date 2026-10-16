#pragma once

#include <iosfwd>
#include <memory>
#include <string>

#include "compat/compiler.h"
#include "surrender/srColorSurface.h"
#include "surrender/srTexture.h"

namespace wiz8 {
class File;
}

std::unique_ptr<srColorSurface> LoadSurface(wiz8::File* handle, w8_long*);

/* Wizardry's virtual-file-backed texture. Priority, dimensions, parameters and
   name are inherited; has_alpha records the loaded surface's alpha channel. */
class stTextureFile : public srTexture {
public:
    virtual ~stTextureFile() override;          /* 0x0047C8E0 */

    stTextureFile(const char* file_name, int cached); /* 0x0047C630 */
    stTextureFile& operator=(const stTextureFile& other);

    void setFileName(std::string file_name); /* 0x0047C830 */
    void setCached(int cached) { this->cached = cached; }
    int isSurfaceLoaded() const { return surface != 0; }
    srColorSurface* getSurface() const { return surface.get(); }
    unsigned char hasAlpha() const { return has_alpha; }
    void loadSurface(); /* 0x0047BBF0 */
    /* Emitted at 0x47BBD0 for the materials.cpp probe caller and inlined at
       the invalidate/getMipmapData sites; the pending-defaults flag is part
       of the release sequence itself. */
    void releaseSurface(); /* 0x0047BBD0 */

    virtual w8_ulong getTextureFrameHandle() override;     /* 0x0047C5F0 */
    virtual void getMipmapData(const MultiRequest& request) override; /* 0x0047CA50 */
    virtual void getMipmapLevelPartial(PartialRequest& request) override;
    virtual void invalidate() override; /* 0x0047C8B0 */

protected:
    virtual void setupDefaultValues() override; /* 0x0047C600 */

private:
    /* Masks for srTexture::e_flag dump bits. */
    enum TextureState {
        LOAD_FAILED = 1UL << FLAG_GENERATESURFACE_FAILURE,
        DEFAULTS_PENDING = 1UL << FLAG_DIRTY_DEFAULTS
    };

    int cached;
    std::string file_name;
    std::unique_ptr<srColorSurface> surface;
    w8_ulong frame_handle;
    bool has_alpha;
};
