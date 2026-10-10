/* Modified for the Wizardry 8 reconstruction: 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "LibraryDataBase.h"
#include "MemMan.h"
#include <wiz8/filesystem.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

CHAR8 gzCdDirectory[SGPFILENAME_LEN];

static_assert(sizeof(LIBHEADER) == 532);
static_assert(sizeof(DIRENTRY) == 280);
static_assert(offsetof(LIBHEADER, iReserved) == 528);
static_assert(offsetof(DIRENTRY, sFileTime) == 268);

namespace
{
constexpr UINT32 max_file_id = (1u << DB_BITS_FOR_FILE_ID) - 1;

void release_library(LibraryHeaderStruct& library)
{
    delete library.hLibraryHandle;
    for (UINT16 i = 0; i < library.usNumberOfEntries; ++i)
        MemFree(library.pFileHeader[i].pFileName);
    MemFree(library.pFileHeader);
    MemFree(library.pOpenFiles);
    MemFree(library.sLibraryPath);
    library = {};
}

struct PendingLibrary
{
    LibraryHeaderStruct header{};
    ~PendingLibrary() { release_library(header); }
};

template<typename T>
T* allocate_array(std::size_t count)
{
    if (count > std::numeric_limits<UINT32>::max() / sizeof(T))
        throw std::length_error("SLF allocation overflow");
    auto* result = static_cast<T*>(MemAlloc(count * sizeof(T)));
    if (!result)
        throw std::bad_alloc();
    std::memset(result, 0, count * sizeof(T));
    return result;
}

char* copy_text(std::string_view text)
{
    auto* result = allocate_array<char>(text.size() + 1);
    std::memcpy(result, text.data(), text.size());
    return result;
}

template<std::size_t N>
std::string_view disk_text(const char (&text)[N])
{
    const auto* end = static_cast<const char*>(std::memchr(text, '\0', N));
    if (!end)
        throw std::runtime_error("unterminated SLF text field");
    return {text, static_cast<std::size_t>(end - text)};
}

std::string entry_key(std::string_view text)
{
    std::string result(text);
    for (char& value : result)
    {
        if (value == '/')
            value = '\\';
        else if (value >= 'A' && value <= 'Z')
            value += 'a' - 'A';
    }
    return result;
}

std::string lookup_key(std::string_view text)
{
    auto result = entry_key(text);
    if (result.size() >= 2 && result[0] >= 'c' && result[0] <= 'f' && result[1] == ':')
    {
        result.erase(0, 2);
        result.erase(0, result.find_first_not_of('\\'));
    }
    while (result.starts_with(".\\"))
        result.erase(0, 2);
    return result;
}

bool valid_library(INT16 id)
{
    return gFileDataBase.pLibraries && id >= 0 && id < gFileDataBase.usNumberOfLibraries;
}

FileOpenStruct* opened_entry(INT16 library_id, UINT32 file_id)
{
    if (!IsLibraryOpened(library_id) || file_id == 0)
        return nullptr;
    auto& library = gFileDataBase.pLibraries[library_id];
    if (file_id >= static_cast<UINT32>(library.iSizeOfOpenFileArray))
        return nullptr;
    auto& entry = library.pOpenFiles[file_id];
    return entry.uiFileID && entry.pFileHeader ? &entry : nullptr;
}

std::unique_ptr<wiz8::File> open_archive(const char* name, bool on_cd)
{
    try
    {
        return wiz8::open_file(name);
    }
    catch (const std::exception&)
    {
        if (!on_cd)
            throw;
    }
    std::string path(disk_text(gzCdDirectory));
    if (!path.empty() && path.back() != '/' && path.back() != '\\')
        path += '/';
    path += name;
    return wiz8::open_file(path);
}

void read_exact(wiz8::File& file, void* data, std::size_t size)
{
    if (file.read(data, size).bytes != size)
        throw std::runtime_error("truncated SLF archive");
}

struct ArchiveEntry
{
    std::string name;
    UINT32 offset, length;
    SGP_FILETIME time;
};

std::vector<ArchiveEntry> read_archive_table(wiz8::File& file, LIBHEADER& header)
{
    const auto file_size = file.size();
    if (file_size < static_cast<std::int64_t>(sizeof(header)))
        throw std::runtime_error("truncated SLF header");
    read_exact(file, &header, sizeof(header));
    disk_text(header.sLibName);
    disk_text(header.sPathToLibrary);
    if (header.iEntries < 0 || header.iUsed < 0 || header.iUsed > header.iEntries ||
        header.iUsed > std::numeric_limits<UINT16>::max())
        throw std::runtime_error("invalid SLF entry count");
    const auto table_size = std::uint64_t(header.iEntries) * sizeof(DIRENTRY);
    if (table_size > std::uint64_t(file_size) - sizeof(header))
        throw std::runtime_error("SLF directory exceeds archive");
    const auto table_start = std::uint64_t(file_size) - table_size;
    file.seek(static_cast<std::int64_t>(table_start), wiz8::SeekOrigin::begin);

    std::vector<ArchiveEntry> entries;
    std::array<DIRENTRY, 64> buffer;
    for (UINT32 remaining = header.iEntries; remaining;)
    {
        const auto count = std::min<std::size_t>(remaining, buffer.size());
        read_exact(file, buffer.data(), count * sizeof(DIRENTRY));
        remaining -= count;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& disk = buffer[i];
            if (disk.ubState != FILE_OK)
                continue;
            const auto name = disk_text(disk.sFileName);
            if (name.empty() || disk.uiOffset < sizeof(header) || disk.uiOffset > table_start ||
                disk.uiLength > table_start - disk.uiOffset)
                throw std::runtime_error("invalid SLF entry extent");
            if (entries.size() == std::numeric_limits<UINT16>::max())
                throw std::runtime_error("too many live SLF entries");
            entries.push_back({entry_key(name), disk.uiOffset, disk.uiLength, disk.sFileTime});
        }
    }

    if (entries.size() != static_cast<std::size_t>(header.iUsed))
        throw std::runtime_error("inconsistent SLF live entry count");
    std::vector<const ArchiveEntry*> extents;
    for (const auto& entry : entries)
        if (entry.length)
            extents.push_back(&entry);
    std::sort(extents.begin(), extents.end(), [](auto* left, auto* right) {
        return left->offset < right->offset;
    });
    std::uint64_t previous_end = sizeof(header);
    for (const auto* entry : extents)
    {
        if (entry->offset < previous_end)
            throw std::runtime_error("overlapping SLF entries");
        previous_end = std::uint64_t(entry->offset) + entry->length;
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    for (std::size_t i = 1; i < entries.size(); ++i)
        if (entries[i - 1].name == entries[i].name)
            throw std::runtime_error("duplicate SLF entry name");
    return entries;
}

FileHeaderStruct* find_entry(LibraryHeaderStruct& library, std::string_view name)
{
    const std::string_view path(library.sLibraryPath);
    if (!name.starts_with(path) || !library.usNumberOfEntries)
        return nullptr;
    name.remove_prefix(path.size());
    auto* end = library.pFileHeader + library.usNumberOfEntries;
    auto* found = std::lower_bound(library.pFileHeader, end, name,
        [](const FileHeaderStruct& entry, std::string_view key) {
            return std::string_view(entry.pFileName) < key;
        });
    return found != end && name == found->pFileName ? found : nullptr;
}

template<typename T>
UINT32 reserve_slot(T*& slots, INT32& capacity)
{
    for (INT32 i = 1; i < capacity; ++i)
        if (!slots[i].uiFileID)
            return i;
    if (capacity < 0 || UINT32(capacity) > max_file_id - NUM_FILES_TO_ADD_AT_A_TIME)
        return 0;
    const auto new_capacity = capacity + NUM_FILES_TO_ADD_AT_A_TIME;
    auto* grown = static_cast<T*>(MemRealloc(slots, std::size_t(new_capacity) * sizeof(T)));
    if (!grown)
        return 0;
    std::memset(grown + capacity, 0, NUM_FILES_TO_ADD_AT_A_TIME * sizeof(T));
    const UINT32 slot = capacity ? capacity : 1;
    capacity = new_capacity;
    slots = grown;
    return slot;
}
} // namespace

BOOLEAN InitializeFileDatabase(void)
{
    ShutDownFileDatabase();
    try
    {
        gFileDataBase.pLibraries = allocate_array<LibraryHeaderStruct>(MAX_NUMBER_OF_LIBRARIES);
        gFileDataBase.RealFiles.pRealFilesOpen = allocate_array<RealFileOpenStruct>(INITIAL_NUM_HANDLES);
        gFileDataBase.RealFiles.iSizeOfOpenFileArray = INITIAL_NUM_HANDLES;
        std::strcpy(gzCdDirectory, ".");
        gFileDataBase.usNumberOfLibraries = NUMBER_OF_LIBRARIES;
        for (INT16 id = 0; id < NUMBER_OF_LIBRARIES; ++id)
            if (gGameLibaries[id].fInitOnStart)
                OpenLibrary(id);
        return TRUE;
    }
    catch (const std::exception&)
    {
        ShutDownFileDatabase();
        return FALSE;
    }
}

INT32 LoadPatchSlfArchives(const CHAR8* directory)
{
    if (!directory || !gFileDataBase.pLibraries)
        return 0;
    INT32 loaded = 0;
    try
    {
        for (int patch = 0; patch < 50 &&
             gFileDataBase.usNumberOfLibraries < MAX_NUMBER_OF_LIBRARIES; ++patch)
        {
            const auto number = std::to_string(patch);
            const auto path = std::string(directory) + "\\Patch." +
                std::string(3 - number.size(), '0') + number;
            if (path.size() >= FILENAME_SIZE)
                continue;
            const auto status = wiz8::file_status(path);
            if (!status || status->info.type != SDL_PATHTYPE_FILE)
                continue;
            const auto id = gFileDataBase.usNumberOfLibraries;
            auto& config = gGameLibaries[id];
            config = {};
            std::memcpy(config.sLibraryName, path.c_str(), path.size() + 1);
            ++gFileDataBase.usNumberOfLibraries;
            if (OpenLibrary(id))
            {
                gFileDataBase.pLibraries[id].fPatchLibrary = TRUE;
                ++loaded;
            }
            else
            {
                --gFileDataBase.usNumberOfLibraries;
                config = {};
            }
        }
    }
    catch (const std::exception&)
    {
        // Already loaded patches remain usable if a later directory query fails.
    }
    return loaded;
}

BOOLEAN ReopenCDLibraries(void)
{
    if (!gFileDataBase.pLibraries)
        return FALSE;
    for (INT16 id = 0; id < NUMBER_OF_LIBRARIES; ++id)
        if (gGameLibaries[id].fOnCDrom)
        {
            CloseLibrary(id);
            OpenLibrary(id);
        }
    return TRUE;
}

BOOLEAN ShutDownFileDatabase()
{
    if (gFileDataBase.pLibraries)
    {
        for (UINT16 id = 0; id < gFileDataBase.usNumberOfLibraries; ++id)
            release_library(gFileDataBase.pLibraries[id]);
        MemFree(gFileDataBase.pLibraries);
    }
    if (gFileDataBase.RealFiles.pRealFilesOpen)
    {
        for (INT32 id = 1; id < gFileDataBase.RealFiles.iSizeOfOpenFileArray; ++id)
            if (gFileDataBase.RealFiles.pRealFilesOpen[id].uiFileID)
                FileClose(gFileDataBase.RealFiles.pRealFilesOpen[id].uiFileID);
        MemFree(gFileDataBase.RealFiles.pRealFilesOpen);
    }
    gFileDataBase.pLibraries = nullptr;
    gFileDataBase.usNumberOfLibraries = 0;
    gFileDataBase.fInitialized = FALSE;
    gFileDataBase.RealFiles = {};
    return TRUE;
}

BOOLEAN InitializeLibrary(STR name, LibraryHeaderStruct* library, BOOLEAN on_cd)
{
    if (!name || !library || library->fLibraryOpen || library->hLibraryHandle)
        return FALSE;
    try
    {
        auto file = open_archive(name, on_cd);
        LIBHEADER disk{};
        auto entries = read_archive_table(*file, disk);
        PendingLibrary pending;
        auto& header = pending.header;
        header.fPatchLibrary = library->fPatchLibrary;
        header.sLibraryPath = copy_text(entry_key(disk_text(disk.sPathToLibrary)));
        if (!entries.empty())
        {
            header.pFileHeader = allocate_array<FileHeaderStruct>(entries.size());
            header.usNumberOfEntries = static_cast<UINT16>(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                auto& target = header.pFileHeader[i];
                target.pFileName = copy_text(entries[i].name);
                target.uiFileOffset = entries[i].offset;
                target.uiFileLength = entries[i].length;
                target.sFileTime = entries[i].time;
            }
        }
        header.pOpenFiles = allocate_array<FileOpenStruct>(INITIAL_NUM_HANDLES);
        header.iSizeOfOpenFileArray = INITIAL_NUM_HANDLES;
        header.fLibraryOpen = TRUE;
        header.hLibraryHandle = file.release();
        *library = header;
        header = {};
        return TRUE;
    }
    catch (const std::exception&)
    {
        return FALSE;
    }
}

wiz8::File* OpenLibraryStream(HWFILE file)
{
    const auto id = static_cast<INT16>(DB_EXTRACT_LIBRARY(file));
    auto* entry = opened_entry(id, DB_EXTRACT_FILE_ID(file));
    if (!entry)
        return nullptr;
    try
    {
        auto stream = wiz8::open_host_file(gFileDataBase.pLibraries[id].hLibraryHandle->physical_path());
        const auto& header = *entry->pFileHeader;
        if (std::uint64_t(header.uiFileOffset) + header.uiFileLength > std::uint64_t(stream->size()))
            return nullptr;
        stream->seek(header.uiFileOffset, wiz8::SeekOrigin::begin);
        return stream.release();
    }
    catch (const std::exception&)
    {
        return nullptr;
    }
}

BOOLEAN LoadDataFromLibrary(INT16 id, UINT32 file_id, PTR data, UINT32 size, UINT32* bytes_read)
{
    if (!bytes_read)
        return FALSE;
    *bytes_read = 0;
    auto* entry = opened_entry(id, file_id);
    if (!entry || (!data && size))
        return FALSE;
    const auto length = entry->pFileHeader->uiFileLength;
    if (entry->uiFilePosInFile > length || size > length - entry->uiFilePosInFile)
        return FALSE;
    if (!size)
        return TRUE;
    try
    {
        auto& stream = *gFileDataBase.pLibraries[id].hLibraryHandle;
        if (std::uint64_t(entry->pFileHeader->uiFileOffset) + length > std::uint64_t(stream.size()))
            return FALSE;
        stream.seek(std::int64_t(entry->pFileHeader->uiFileOffset) + entry->uiFilePosInFile,
                    wiz8::SeekOrigin::begin);
        const auto result = stream.read(data, size);
        *bytes_read = static_cast<UINT32>(result.bytes);
        entry->uiFilePosInFile += *bytes_read;
        return result.bytes == size;
    }
    catch (const std::exception&)
    {
        return FALSE;
    }
}

BOOLEAN CheckIfFileExistInLibrary(STR name)
{
    return GetLibraryIDFromFileName(name) != -1;
}

INT16 GetLibraryIDFromFileName(STR name)
{
    if (!name)
        return -1;
    try
    {
        const auto key = lookup_key(name);
        INT16 best = -1;
        for (INT16 id = 0; id < gFileDataBase.usNumberOfLibraries; ++id)
        {
            if (!IsLibraryOpened(id))
                continue;
            auto& library = gFileDataBase.pLibraries[id];
            if (!find_entry(library, key))
                continue;
            if (best == -1 || library.fPatchLibrary ||
                (!gFileDataBase.pLibraries[best].fPatchLibrary &&
                 std::strlen(library.sLibraryPath) >
                 std::strlen(gFileDataBase.pLibraries[best].sLibraryPath)))
                best = id;
        }
        return best;
    }
    catch (const std::exception&)
    {
        return -1;
    }
}

HWFILE OpenFileFromLibrary(STR name)
{
    const auto id = GetLibraryIDFromFileName(name);
    if (id < 0)
        return 0;
    try
    {
        auto& library = gFileDataBase.pLibraries[id];
        auto* header = find_entry(library, lookup_key(name));
        if (!header)
            return 0;
        const auto slot = reserve_slot(library.pOpenFiles, library.iSizeOfOpenFileArray);
        if (!slot)
            return 0;
        const auto handle = (UINT32(id) << DB_BITS_FOR_FILE_ID) | slot;
        library.pOpenFiles[slot] = {handle, 0, header};
        ++library.iNumFilesOpen;
        return handle;
    }
    catch (const std::exception&)
    {
        return 0;
    }
}

HWFILE CreateRealFileHandle(wiz8::File* file)
{
    if (!file || !file->is_open())
        return 0;
    auto& real = gFileDataBase.RealFiles;
    const auto slot = reserve_slot(real.pRealFilesOpen, real.iSizeOfOpenFileArray);
    if (!slot)
        return 0;
    const auto handle = (UINT32(REAL_FILE_LIBRARY_ID) << DB_BITS_FOR_FILE_ID) | slot;
    real.pRealFilesOpen[slot] = {handle, file};
    ++real.iNumFilesOpen;
    return handle;
}

BOOLEAN GetLibraryAndFileIDFromLibraryFileHandle(HWFILE file, INT16* library_id, UINT32* file_id)
{
    if (!library_id || !file_id)
        return FALSE;
    *library_id = static_cast<INT16>(DB_EXTRACT_LIBRARY(file));
    *file_id = DB_EXTRACT_FILE_ID(file);
    if (*library_id == REAL_FILE_LIBRARY_ID)
    {
        auto& real = gFileDataBase.RealFiles;
        return real.pRealFilesOpen && *file_id &&
            *file_id < static_cast<UINT32>(real.iSizeOfOpenFileArray) &&
            real.pRealFilesOpen[*file_id].uiFileID == file;
    }
    return opened_entry(*library_id, *file_id) != nullptr;
}

BOOLEAN CloseLibraryFile(INT16 id, UINT32 file_id)
{
    auto* entry = opened_entry(id, file_id);
    if (!entry)
        return FALSE;
    *entry = {};
    --gFileDataBase.pLibraries[id].iNumFilesOpen;
    return TRUE;
}

BOOLEAN LibraryFileSeek(INT16 id, UINT32 file_id, UINT32 distance, UINT8 origin)
{
    auto* entry = opened_entry(id, file_id);
    if (!entry)
        return FALSE;
    const auto length = entry->pFileHeader->uiFileLength;
    const auto current = entry->uiFilePosInFile;
    if (current > length)
        return FALSE;
    UINT32 position;
    switch (origin)
    {
    case FILE_SEEK_FROM_START:
        if (distance > length)
            return FALSE;
        position = distance;
        break;
    case FILE_SEEK_FROM_END:
        if (distance > length)
            return FALSE;
        position = length - distance;
        break;
    case FILE_SEEK_FROM_CURRENT:
    {
        // Legacy callers pass backward relative seeks through the UINT32 argument.
        const auto delta = distance <= std::numeric_limits<INT32>::max() ?
            std::int64_t(distance) : std::int64_t(distance) - (std::int64_t(1) << 32);
        const auto target = std::int64_t(current) + delta;
        if (target < 0 || target > length)
            return FALSE;
        position = static_cast<UINT32>(target);
        break;
    }
    default:
        return FALSE;
    }
    entry->uiFilePosInFile = position;
    return TRUE;
}

BOOLEAN OpenLibrary(INT16 id)
{
    if (!valid_library(id) || gFileDataBase.pLibraries[id].fLibraryOpen)
        return FALSE;
    auto& config = gGameLibaries[id];
    if (!InitializeLibrary(config.sLibraryName, &gFileDataBase.pLibraries[id], config.fOnCDrom))
        return FALSE;
    gFileDataBase.fInitialized = TRUE;
    return TRUE;
}

BOOLEAN CloseLibrary(INT16 id)
{
    if (!IsLibraryOpened(id))
        return FALSE;
    const auto patch = gFileDataBase.pLibraries[id].fPatchLibrary;
    release_library(gFileDataBase.pLibraries[id]);
    gFileDataBase.pLibraries[id].fPatchLibrary = patch;
    return TRUE;
}

BOOLEAN IsLibraryOpened(INT16 id)
{
    return valid_library(id) && gFileDataBase.pLibraries[id].fLibraryOpen;
}

BOOLEAN GetLibraryFileTime(INT16 id, UINT32 file_id, SGP_FILETIME* time)
{
    if (!time)
        return FALSE;
    *time = {};
    const auto* entry = opened_entry(id, file_id);
    if (!entry)
        return FALSE;
    *time = entry->pFileHeader->sFileTime;
    return TRUE;
}
