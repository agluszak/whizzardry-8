#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/stTextureFile.h"

#include "FileMan.h"
#include "tga_import.h"

#include <cstring>

namespace {
class TextureInput : public srBinIStream {
public:
    explicit TextureInput(int handle) : handle(handle) { setState(SR_STREAM_OK); }
    w8_ulong getSize() override { return FileGetSize(handle); }
    w8_ulong tell() override { return FileGetPos(handle); }
    srBinStream& seek(w8_ulong position) override
    {
        if (position > getSize() || !FileSeek(handle, position, FILE_SEEK_FROM_START))
            setState(SR_STREAM_ERROR);
        return *this;
    }
    srBinStream& seek(w8_ulong offset, e_seekDir direction) override
    {
        return seek(direction == SR_SEEK_BEGIN ? offset :
                    direction == SR_SEEK_CURRENT ? tell() + offset : getSize() - offset);
    }
private:
    w8_ulong vread(void* data, w8_ulong bytes) override
    {
        UINT32 count = 0;
        if (!FileRead(handle, data, bytes, &count))
            setState(SR_STREAM_ERROR);
        return count;
    }
    int handle;
};
} // namespace

srColorSurface* __stdcall LoadSurface(int handle, w8_long*)
{
    TextureInput input(handle);
    return srImage::loadTga(input);
}

// VTABLE: WIZ8 0x005EC5F8
// class stTextureFile

// VTABLE: WIZ8 0x005EC63C
// class srClassSupport<stTextureFile,srTexture,0,65537>

/* The TGA loader instantiates srClassSupport for the imported srPalette
   (class id 0x2900); its registry and clone slots are emitted in this TU. */
// VTABLE: WIZ8 0x005EC5D8
// class srClientSupport<srPalette,10496>

// FUNCTION: WIZ8 0x0047BBD0
void stTextureFile::releaseSurface()
{
    ReleaseRendererObject(surface);
    texture_flags_ |= DEFAULTS_PENDING;
}

// FUNCTION: WIZ8 0x0047C630
stTextureFile::stTextureFile(const char* file_name, int cached)
    : cached(0), file_name(0), surface(0), frame_handle(getNewFrameHandle()), has_alpha(0)
{
    /* Retail stores 0 then conditionally stores 1: the authored value is the
       normalized predicate, not the raw parameter. */
    this->cached = (cached != 0);
    invalidate();
    setFileName(file_name);
    if (file_name != 0) {
        setName(file_name);
    }
    if (cached != 0 && file_name != 0) {
        setupDefaultValues();
    }
}

stTextureFile& stTextureFile::operator=(const stTextureFile& other)
{
    if (this != &other) {
        srTexture::operator=(other);
        setFileName(other.file_name);
        cached = other.cached;
    }
    return *this;
}

// FUNCTION: WIZ8 0x0047C5F0
w8_ulong stTextureFile::getTextureFrameHandle()
{
    if ((texture_flags_ & LOAD_FAILED) != 0) {
        return 0;
    }
    return frame_handle;
}

// FUNCTION: WIZ8 0x0047C600
void stTextureFile::setupDefaultValues()
{
    if ((texture_flags_ & DEFAULTS_PENDING) == 0) {
        return;
    }

    texture_flags_ &= ~DEFAULTS_PENDING;
    if (surface == 0) {
        loadSurface();
    }
    setupDefaultValuesFromSurface(surface);
}

// FUNCTION: WIZ8 0x0047C7A0
srClass* stTextureFile::vInstance()
{
    return new stTextureFile(0, 0);
}

// FUNCTION: WIZ8 0x0047C830
void stTextureFile::setFileName(const char* file_name)
{
    invalidate();
    delete[] this->file_name;
    this->file_name = 0;

    if (file_name != 0 && file_name[0] != 0) {
        this->file_name = new char[strlen(file_name) + 1];
        strcpy(this->file_name, file_name);
    }

    texture_flags_ &= ~LOAD_FAILED;
    texture_flags_ |= DEFAULTS_PENDING;
}

// FUNCTION: WIZ8 0x0047C8B0
void stTextureFile::invalidate()
{
    releaseSurface();
    invalidateFrameHandle(frame_handle);
    texture_flags_ &= ~LOAD_FAILED;
}

/* Retail runs the invalidate sequence twice: this call is expanded inline,
   and the setFileName(0) invalidation stays a virtual dispatch. */
// FUNCTION: WIZ8 0x0047C8E0
stTextureFile::~stTextureFile()
{
    if (IsTextureInReadMeshScratch(this)) {
        ReleaseReadMeshScratch();
    }
    invalidate();
    setFileName(0);
}

// FUNCTION: WIZ8 0x0047BBF0
void stTextureFile::loadSurface()
{
    /* The second LoadSurface argument is an out-pointer the callee ignores;
       the caller still initializes the dword it passes. */
    w8_long unused = 0;

    if (surface != 0) {
        invalidate();
    }
    /* A missing file name lands on the same LOAD_FAILED tail as a failed
       load; retail has no silent early return here. */
    if (file_name == 0) {
        texture_flags_ |= LOAD_FAILED;
        return;
    }

    surface = 0;
    int handle = FileOpen(file_name, 0x41, 0);
    if (handle != 0) {
        surface = LoadSurface(handle, &unused);
        FileClose(handle);
    }

    if (surface == 0) {
        texture_flags_ |= LOAD_FAILED;
        return;
    }

    setupDefaultValues();
    surface->setFilter(getFilter());
    /* Retail reads the alpha channel count straight out of the surface's
       pixel format (unsigned SETA): the authored comparison is `> 0`. */
    has_alpha = (surface->pixel_format.alpha_bits > 0);
}

// FUNCTION: WIZ8 0x0047CA50
void stTextureFile::getMipmapData(MultiRequest& request)
{
    if (surface == 0) {
        loadSurface();
    }
    if (surface == 0) {
        return;
    }

    /* 0x0047CA85 and 0x0047CAAB compare the level against last_level
       unsigned, and last_level is already unsigned in the request record. */
    w8_ulong level = static_cast<w8_ulong>(request.mipmap_level);
    if (request.destinations[level] != 0) {
        request.destinations[level]->copy(*surface);
    }
    for (++level; level <= request.last_level; ++level) {
        if (request.destinations[level] != 0 && request.destinations[level - 1] != 0) {
            request.destinations[level]->copy(*request.destinations[level - 1]);
        }
    }

    /* Retail tests the flag with TEST byte ptr [+0x54],0x1 even though the
       constructor stores the field dword-wide: the authored predicate is a
       bit test, not a zero compare. */
    if ((cached & 1) == 0) {
        releaseSurface();
    }
}

void stTextureFile::getMipmapLevelPartial(PartialRequest&) {}

void stTextureFile::dump(std::ostream&) {}
