#include "surrender/srBinIStream.h"
#include "surrender/srCore.h"
#include "surrender/srIStreamOpener.h"
#include "wiz8/virtual_file.h"
#include "wiz8/virtual_file_stream.h"
#include "FileMan.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

// FUNCTION: WIZ8 0x0047CBD0
W8VirtualFileBinIStream::W8VirtualFileBinIStream(const char* path) : m_hFile(0)
{
    m_hFile = FileOpen(path, 0x41, 0);
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
        FileClose(m_hFile);
    }
}

// FUNCTION: WIZ8 0x0047D4D0
srBinStream& W8VirtualFileBinIStream::seek(w8_ulong position, e_seekDir direction)
{
    int origin;
    switch (direction) {
    case SR_SEEK_BEGIN:
        origin = 1;
        break;
    case SR_SEEK_CURRENT:
        origin = 4;
        break;
    case SR_SEEK_END:
        origin = 2;
        break;
    default:
        return *this;
    }
    if (!FileSeek(m_hFile, position, origin)) {
        setState(SR_STREAM_ERROR);
    }
    return *this;
}

// FUNCTION: WIZ8 0x0047D560
srBinStream& W8VirtualFileBinIStream::seek(w8_ulong position)
{
    if (!FileSeek(m_hFile, position, 1)) {
        setState(SR_STREAM_ERROR);
    }
    return *this;
}

// FUNCTION: WIZ8 0x0047D5B0
w8_ulong W8VirtualFileBinIStream::tell()
{
    return FileGetPos(m_hFile);
}

// FUNCTION: WIZ8 0x0047d5c0
w8_ulong W8VirtualFileBinIStream::vread(void* buffer, w8_ulong size)
{
    unsigned int bytes_read;

    if (FileRead(m_hFile, buffer, size, &bytes_read)) {
        return bytes_read;
    }
    return 0;
}

// FUNCTION: WIZ8 0x0047CB30
srBinIStream* W8VirtualFileStreamOpener::open(const char* path)
{
    return new W8VirtualFileBinIStream(path);
}

// FUNCTION: WIZ8 0x0047CBA0
const char* W8VirtualFileStreamOpener::getDescription() const
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
