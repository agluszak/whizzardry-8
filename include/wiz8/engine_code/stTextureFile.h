#pragma once

#include <filesystem>
#include <memory>

#include "compat/compiler.h"
#include "surrender/srTexture.h"

namespace wiz8 {
struct SDL_Surface;

class File;
}

// Lazily decodes a retail texture and retains its SDL pixels for this resource's lifetime.
class stTextureFile : public srTexture {
public:
    virtual ~stTextureFile() override;          /* 0x0047C8E0 */

    stTextureFile(std::filesystem::path file_name, bool preload); /* 0x0047C630 */
    stTextureFile(const stTextureFile&) = delete;
    stTextureFile& operator=(const stTextureFile&) = delete;

    int isSurfaceLoaded() const { return surface != 0; }
    SDL_Surface* getSurface() const { return surface.get(); }
    unsigned char hasAlpha() const { return has_alpha; }
    void loadSurface(); /* 0x0047BBF0 */
    void releaseSurface(); /* 0x0047BBD0 */

    srTexture* resolve() override;     /* 0x0047C5F0 */
    virtual void invalidate() override; /* 0x0047C8B0 */

protected:
    virtual void setupDefaultValues() override; /* 0x0047C600 */

private:
    /* Masks for srTexture::e_flag dump bits. */
    enum TextureState {
        LOAD_FAILED = 1UL << FLAG_GENERATESURFACE_FAILURE,
        DEFAULTS_PENDING = 1UL << FLAG_DIRTY_DEFAULTS
    };

    const std::filesystem::path file_name;
    bool has_alpha;
};
