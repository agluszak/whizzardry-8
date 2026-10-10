#include "surrender/srBinIStream.h"
#include "surrender/srCore.h"
#include "surrender/srIStreamOpener.h"
#include "wiz8/virtual_file.h"
#include "wiz8/virtual_file_stream.h"
#include "wiz8/filesystem.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

// FUNCTION: WIZ8 0x0047CBD0
W8VirtualFileBinIStream::W8VirtualFileBinIStream(const char* path)
{
    if (path && *path) {
        m_hFile = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    }
    if (m_hFile != 0) {
        setState(SR_STREAM_OK);
    } else {
        setState(SR_STREAM_ERROR);
    }
}

// FUNCTION: WIZ8 0x0047D490
W8VirtualFileBinIStream::~W8VirtualFileBinIStream()
{
    if (m_hFile) {
        m_hFile.reset();
    }
}

// FUNCTION: WIZ8 0x0047D4D0
srBinStream& W8VirtualFileBinIStream::seek(w8_ulong position, e_seekDir direction)
try
{
    if (!m_hFile) { setState(SR_STREAM_ERROR); return *this; }
    wiz8::SeekOrigin origin;
    switch (direction) {
    case SR_SEEK_BEGIN:
        origin = wiz8::SeekOrigin::begin;
        break;
    case SR_SEEK_CURRENT:
        origin = wiz8::SeekOrigin::current;
        break;
    case SR_SEEK_END:
        origin = wiz8::SeekOrigin::end;
        break;
    default:
        return *this;
    }
    m_hFile->seek(direction == SR_SEEK_END ? -std::int64_t(position) : position, origin);
    return *this;
}
catch (const std::exception&) { setState(SR_STREAM_ERROR); return *this; }

// FUNCTION: WIZ8 0x0047D560
srBinStream& W8VirtualFileBinIStream::seek(w8_ulong position)
try
{
    if (!m_hFile) { setState(SR_STREAM_ERROR); return *this; }
    m_hFile->seek(position, wiz8::SeekOrigin::begin);
    return *this;
}
catch (const std::exception&) { setState(SR_STREAM_ERROR); return *this; }

// FUNCTION: WIZ8 0x0047D5B0
w8_ulong W8VirtualFileBinIStream::tell()
try
{
    if (!m_hFile) return 0;
    return m_hFile->tell();
}
catch (const std::exception&) { setState(SR_STREAM_ERROR); return 0; }

// FUNCTION: WIZ8 0x0047d5c0
w8_ulong W8VirtualFileBinIStream::vread(void* buffer, w8_ulong size)
try
{
    if (!m_hFile) { setState(SR_STREAM_ERROR); return 0; }
    return m_hFile->read(buffer, size).bytes;
}
catch (const std::exception&) { setState(SR_STREAM_ERROR); return 0; }

// FUNCTION: WIZ8 0x0047CB30
srBinIStream* W8VirtualFileStreamOpener::open(std::string_view path)
{
    const std::string filename(path);
    return new W8VirtualFileBinIStream(filename.c_str());
}

// FUNCTION: WIZ8 0x0047CBA0
std::string_view W8VirtualFileStreamOpener::getDescription() const
{
    return "stBinIStream";
}

// GLOBAL: WIZ8 0x0065A124
W8VirtualFileStreamOpener g_virtual_file_stream_opener;

/* MSVC PDB spelling uses overload ordinals, not parameter types. Retail
   secondary-vtable order is seek(2)=ulong, seek(1)=ulong+dir, tell. */

// FUNCTION: WIZ8 0x0047d5f0
void InitializeVirtualFileImageImporters(void)
{
    srCore.getIStreamOpener()->addStreamType(&g_virtual_file_stream_opener, "jpg");
    srCore.getIStreamOpener()->addStreamType(&g_virtual_file_stream_opener, "jpeg");
    srCore.getIStreamOpener()->addStreamType(&g_virtual_file_stream_opener, "tga");
}
