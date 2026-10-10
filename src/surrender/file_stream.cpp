#include "surrender/srBinFStream.h"
#include "surrender/srBinOStream.h"

#include <ostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits>

// FUNCTION: SURRENDER 0x1002EFB0
int srBinFStream::isOpen()
{
    return file != 0;
}

// FUNCTION: SURRENDER 0x1002EFC0
void srBinFStream::close()
{
    try { if (file) file->close(); }
    catch (const std::exception&) { setState(SR_STREAM_ERROR); file.reset(); return; }
    file.reset();
    path.clear();
    setState(SR_STREAM_STATE_2);
}

// FUNCTION: SURRENDER 0x1002F010
const char* srBinFStream::getPath() const
{
    return path.c_str();
}

// FUNCTION: SURRENDER 0x1002F020
void srBinFStream::setPath(const char* path)
{
    this->path = path != nullptr ? path : "";
}

// FUNCTION: SURRENDER 0x1002F0A0
void srBinFStream::mopen(const char* path, e_mode mode)
{
    if (!isOpen()) {
        if (path == nullptr || *path == '\0') {
            setState(SR_STREAM_ERROR);
            return;
        }
        wiz8::OpenMode intent;
        switch (mode) {
        case SR_MODE_READ: intent = wiz8::OpenMode::read; break;
        case SR_MODE_WRITE: intent = wiz8::OpenMode::replace; break;
        case SR_MODE_READ_WRITE: intent = wiz8::OpenMode::update; break;
        default: setState(SR_STREAM_ERROR); return;
        }
        try {
            file = wiz8::open_file(path, intent);
            setPath(path);
            setState(SR_STREAM_OK);
            return;
        } catch (const std::exception&) {
            file.reset();
        }
    }
    setState(SR_STREAM_ERROR);
}

// FUNCTION: SURRENDER 0x1002F250
srBinFStream::srBinFStream()
{
    file.reset();
    setState(SR_STREAM_STATE_2);
}

// FUNCTION: SURRENDER 0x1002F2B0
srBinFStream::~srBinFStream()
{
    if (isOpen()) {
        close();
    }
}

// FUNCTION: SURRENDER 0x1002F340
srBinStream& srBinFStream::pseek(w8_ulong position, e_seekDir direction)
{
    wiz8::SeekOrigin origin;
    switch (direction) {
    case SR_SEEK_BEGIN: origin = wiz8::SeekOrigin::begin; break;
    case SR_SEEK_CURRENT: origin = wiz8::SeekOrigin::current; break;
    case SR_SEEK_END: origin = wiz8::SeekOrigin::end; break;
    default: setState(SR_STREAM_ERROR); return *this;
    }
    try {
        if (!file) throw std::logic_error("closed stream");
        file->seek(direction != SR_SEEK_BEGIN ? std::int64_t(std::int32_t(position)) :
                                                  std::int64_t(position), origin);
    } catch (const std::exception&) { setState(SR_STREAM_ERROR); }
    return *this;
}

srBinStream& srBinFStream::pseek(w8_ulong position)
{
    return pseek(position, SR_SEEK_BEGIN);
}

w8_ulong srBinFStream::ptell()
{
    try {
        if (!file) throw std::logic_error("closed stream");
        const auto position = file->tell();
        if (position > std::numeric_limits<w8_ulong>::max())
            throw std::overflow_error("stream position exceeds game range");
        return static_cast<w8_ulong>(position);
    } catch (const std::exception&) { setState(SR_STREAM_ERROR); return 0xffffffff; }
}

w8_ulong srBinFStream::readFile(void* destination, w8_ulong size)
{
    try {
        if (!file) throw std::logic_error("closed stream");
        return static_cast<w8_ulong>(file->read(destination, size).bytes);
    } catch (const std::exception&) { setState(SR_STREAM_ERROR); return 0; }
}
w8_ulong srBinFStream::writeFile(const void* source, w8_ulong size)
{
    try {
        if (!file) throw std::logic_error("closed stream");
        file->write(source, size);
        return size;
    } catch (const std::exception&) { setState(SR_STREAM_ERROR); return 0; }
}
unsigned short srBinFStream::getFile()
{
    unsigned char byte;
    return readFile(&byte, 1) == 1 ? byte : 0xffff;
}
unsigned short srBinFStream::putFile(char character)
{
    return writeFile(&character, 1) == 1 ? 0 : 0xffff;
}

// FUNCTION: SURRENDER 0x1002F6B0
srBinIFStream::srBinIFStream() {}

// FUNCTION: SURRENDER 0x1002F760
srBinIFStream::srBinIFStream(const char* path)
{
    open(path);
}

// FUNCTION: SURRENDER 0x1002F830
void srBinIFStream::open(const char* path)
{
    mopen(path, SR_MODE_READ);
}

// FUNCTION: SURRENDER 0x1002F850
unsigned short srBinIFStream::vget()
{
    return getFile();
}

// FUNCTION: SURRENDER 0x1002F870
w8_ulong srBinIFStream::vread(void* destination, w8_ulong size)
{
    return readFile(destination, size);
}

// FUNCTION: SURRENDER 0x1002F890
srBinStream& srBinIFStream::seek(w8_ulong position, e_seekDir direction)
{
    return pseek(position, direction);
}

// FUNCTION: SURRENDER 0x1002F8B0
srBinStream& srBinIFStream::seek(w8_ulong position)
{
    return pseek(position);
}

// FUNCTION: SURRENDER 0x1002F8C0
w8_ulong srBinIFStream::tell()
{
    return ptell();
}

// FUNCTION: SURRENDER 0x1002FC40
srBinIOFStream::srBinIOFStream() {}

// FUNCTION: SURRENDER 0x1002FD20
srBinIOFStream::srBinIOFStream(const char* path)
{
    open(path);
}

// FUNCTION: SURRENDER 0x1002FE10
void srBinIOFStream::open(const char* path)
{
    mopen(path, SR_MODE_READ_WRITE);
}

// FUNCTION: SURRENDER 0x1002FE30
w8_ulong srBinIOFStream::vwrite(const void* source, w8_ulong size)
{
    return writeFile(source, size);
}

// FUNCTION: SURRENDER 0x1002FE50
unsigned short srBinIOFStream::vput(char character)
{
    return putFile(character);
}

// FUNCTION: SURRENDER 0x1002FE80
unsigned short srBinIOFStream::vget()
{
    return getFile();
}

// FUNCTION: SURRENDER 0x1002FEA0
w8_ulong srBinIOFStream::vread(void* destination, w8_ulong size)
{
    return readFile(destination, size);
}

// FUNCTION: SURRENDER 0x1002FEC0
srBinStream& srBinIOFStream::seek(w8_ulong position, e_seekDir direction)
{
    return pseek(position, direction);
}

// FUNCTION: SURRENDER 0x1002FEE0
srBinStream& srBinIOFStream::seek(w8_ulong position)
{
    return pseek(position);
}

// FUNCTION: SURRENDER 0x1002FEF0
w8_ulong srBinIOFStream::tell()
{
    return ptell();
}

// FUNCTION: SURRENDER 0x10030330
srBinOFStream::srBinOFStream(const char* path)
{
    open(path);
}

// FUNCTION: SURRENDER 0x10030420
srBinOFStream::srBinOFStream() {}

// FUNCTION: SURRENDER 0x100304F0
void srBinOFStream::open(const char* path)
{
    mopen(path, SR_MODE_WRITE);
}

// FUNCTION: SURRENDER 0x10030510
w8_ulong srBinOFStream::vwrite(const void* source, w8_ulong size)
{
    return writeFile(source, size);
}

// FUNCTION: SURRENDER 0x10030540
unsigned short srBinOFStream::vput(char character)
{
    return putFile(character);
}

// FUNCTION: SURRENDER 0x10030570
srBinStream& srBinOFStream::seek(w8_ulong position, e_seekDir direction)
{
    return pseek(position, direction);
}

// FUNCTION: SURRENDER 0x10030590
srBinStream& srBinOFStream::seek(w8_ulong position)
{
    return pseek(position);
}

// FUNCTION: SURRENDER 0x100305B0
w8_ulong srBinOFStream::tell()
{
    return ptell();
}
