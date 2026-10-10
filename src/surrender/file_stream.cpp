#include "surrender/srBinFStream.h"
#include "surrender/srBinOStream.h"
#include "surrender/srCore.h"
#include "surrender/srFileManager.h"
#include "surrender/srIStreamOpener.h"
#include "surrender/srMemoryAllocator.h"
#include "surrender/srSystem.h"

#include <ostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits>

// FUNCTION: SURRENDER 0x1002E010
srFileManager::Path::Path(const char* name)
{
    next = 0;
    previous = 0;
    if (name != 0 && *name != '\0') {
        name0 = new char[strlen(name) + 1];
        strcpy(name0, name);
    } else {
        name0 = 0;
    }
}

// FUNCTION: SURRENDER 0x1002E080
srFileManager::Path::~Path()
{
    if (name0 != 0) {
        delete[] name0;
    }
}

// FUNCTION: SURRENDER 0x1002E090
const char* srFileManager::Path::getName() const
{
    return name0;
}

// FUNCTION: SURRENDER 0x1002E0A0
srFileManager::Path* srFileManager::Path::getNext() const
{
    return next;
}

// FUNCTION: SURRENDER 0x1002E0B0
void srFileManager::addPath(const char* path)
{
    if (path != 0 && *path != '\0' && strlen(path) < 0x103) {
        char local_path[0x104];
        strcpy(local_path, path);
        int length = strlen(local_path);
        if (local_path[length - 1] != '/') {
            strcat(local_path, "/");
        }
        for (int index = 0; index < length; ++index) {
            if (local_path[index] == '\\') {
                local_path[index] = '/';
            }
        }
        for (Path* node = first_path; node != 0; node = node->next) {
            if (strcmp(local_path, node->getName()) == 0) {
                return;
            }
        }
        Path* new_node = new Path(local_path);
        new_node->next = first_path;
        new_node->previous = 0;
        if (first_path != 0) {
            first_path->previous = new_node;
        }
        first_path = new_node;
    }
}

// FUNCTION: SURRENDER 0x1002E240
void srFileManager::removePath(const char* path)
{
    if (path != 0 && *path != '\0' && strlen(path) < 0x103) {
        char local_path[0x104];
        strcpy(local_path, path);
        if (local_path[strlen(local_path) - 1] != '/') {
            strcat(local_path, "/");
        }
        Path* node = first_path;
        while (node != 0) {
            if (strcmp(local_path, node->getName()) == 0) {
                if (node->next != 0) {
                    node->next->previous = node->previous;
                }
                if (node->previous != 0) {
                    node->previous->next = node->next;
                }
                if (node == first_path) {
                    first_path = node->next;
                }
                delete node;
                return;
            }
            node = node->next;
        }
    }
}

// FUNCTION: SURRENDER 0x1002E390
void srFileManager::setPath(const char* path)
{
    while (first_path != 0) {
        Path* node = first_path;
        Path* next = node->next;
        delete node;
        first_path = next;
    }
    if (path != 0) {
        addPath(path);
    }
}

// FUNCTION: SURRENDER 0x1002E3E0
w8_long srFileManager::getSize(const char* path)
{
    w8_long size = -1;
    srBinIFStream stream(path);
    if (stream.good()) {
        size = stream.getSize();
    }
    return size;
}

// FUNCTION: SURRENDER 0x1002E490
void srFileManager::load(const char* path, void* destination, w8_ulong size)
{
    if (destination != 0 && size != 0) {
        srBinIFStream stream;
        stream.exceptions(true);
        stream.open(path);
        stream.read(destination, size);
    }
}

// FUNCTION: SURRENDER 0x1002E520
void* srFileManager::allocate(const char* path)
{
    if (path != 0 && *path != '\0') {
        srBinIFStream stream;
        stream.exceptions(true);
        stream.open(path);
        w8_ulong size = stream.getSize();
        void* allocation = srCore.getMemoryAllocator()->allocate(size, path);
        if (allocation != 0) {
            stream.read(allocation, size);
        }
        return allocation;
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1002E630
void srFileManager::free(void* allocation)
{
    if (allocation != 0) {
        srCore.getMemoryAllocator()->free(allocation);
    }
}

// FUNCTION: SURRENDER 0x1002E650
void srFileManager::save(const char* path, void* source, w8_ulong size)
{
    if (source != 0 && size != 0 && path != 0 && *path != '\0') {
        srBinOFStream stream;
        stream.exceptions(true);
        stream.open(path);
        stream.write(source, size);
    }
}

// FUNCTION: SURRENDER 0x1002E6F0
srFileManager::Path* srFileManager::getFirstPath() const
{
    return first_path;
}

// FUNCTION: SURRENDER 0x1002E700
void srFileManager::dump(std::ostream& stream)
{
    for (Path* path = first_path; path != 0; path = path->getNext()) {
        stream << path->getName() << '\n';
    }
}

// FUNCTION: SURRENDER 0x1002E740
srFileManager::srFileManager()
{
    first_path = 0;
}

// FUNCTION: SURRENDER 0x1002E750
srFileManager::~srFileManager()
{
    setPath(0);
}

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
void srBinFStream::mopen(const char* path, e_mode mode, int search_paths)
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
        auto open = [&](const char* name) {
            try { file = wiz8::open_file(name, intent); setPath(name); }
            catch (const std::exception&) { file.reset(); }
        };
        open(path);
        if (file != 0) {
            setState(SR_STREAM_OK);
            return;
        }
        if (search_paths != 0) {
            char drive[_MAX_DRIVE];
            char directory[_MAX_DIR];
            char filename[_MAX_FNAME];
            char extension[_MAX_EXT];
            srSystem::splitPath(path, drive, directory, filename, extension);
            char full_name[_MAX_PATH];
            strncpy(full_name, directory, _MAX_DIR);
            strncat(full_name, filename, _MAX_FNAME);
            srFileManager* manager = srCore.getFileManager();
            for (srFileManager::Path* search = manager->getFirstPath(); search != 0;
                 search = search->getNext()) {
                char search_filename[_MAX_FNAME];
                char search_extension[_MAX_EXT];
                srSystem::splitPath(search->getName(), drive, directory, search_filename,
                                    search_extension);
                char candidate[_MAX_PATH];
                srSystem::makePath(candidate, drive, directory, full_name, extension);
                open(candidate);
                if (file != 0) {
                    setPath(candidate);
                    setState(SR_STREAM_OK);
                    return;
                }
            }
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
    mopen(path, SR_MODE_READ, 1);
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
    mopen(path, SR_MODE_READ_WRITE, 0);
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
    mopen(path, SR_MODE_WRITE, 0);
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
