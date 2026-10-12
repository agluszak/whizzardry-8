#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>

#include "surrender/srColorSurface.h"
#include "surrender/srTexture.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "surrender/srTypeRegistry.h"

namespace wiz8 { class File; }

std::unique_ptr<srColorSurface> __stdcall LoadSurface(wiz8::File* handle, std::int32_t*);

/* Wizardry's virtual-file-backed texture, parallel to SR.DLL's srTextureFile
   (id 0x2112). It overrides the same texture-interface methods SR does;
   priority, dimensions, parameters and name are inherited. has_alpha is
   Wizardry-only. */
class stTextureFile : public srClassSupport<stTextureFile, srTexture, 0x10001> {
public:
    virtual ~stTextureFile() override;          /* 0x0047C8E0 */
    static const char* sGetClassName()
    {
        return "stTextureFile";
    }

    stTextureFile(const char* file_name, std::int32_t cached); /* 0x0047C630 */
    stTextureFile& operator=(const stTextureFile& other);

    const char* getFileName() const
    {
        return file_name;
    }
    void setFileName(const char* file_name); /* 0x0047C830 */
    void setCached(std::int32_t cached)
    {
        this->cached = cached;
    }
    bool isSurfaceLoaded() const
    {
        return surface != 0;
    }
    srColorSurface* getSurface() const
    {
        return surface.get();
    }
    std::uint8_t hasAlpha() const
    {
        return has_alpha;
    }
    void loadSurface(); /* 0x0047BBF0 */
    /* Emitted at 0x47BBD0 for the materials.cpp probe caller and inlined at
       the invalidate/getMipmapData sites; the pending-defaults flag is part
       of the release sequence itself. */
    void releaseSurface(); /* 0x0047BBD0 */

    virtual void dump(std::ostream& stream) override;
    virtual std::uint32_t getTextureFrameHandle() override;     /* 0x0047C5F0 */
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

    std::int32_t cached;
    char* file_name;
    std::unique_ptr<srColorSurface> surface;
    std::uint32_t frame_handle;
    bool has_alpha;
};
