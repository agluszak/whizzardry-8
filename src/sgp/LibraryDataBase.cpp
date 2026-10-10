/* Modified for the Wizardry 8 reconstruction: 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "LibraryDataBase.h"

#include "file_handles.h"
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
void release_library(LibraryHeaderStruct& library)
{
    library = {};
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
    return !gFileDataBase.pLibraries.empty() && id >= 0 && id < gFileDataBase.usNumberOfLibraries;
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
    // SLF names may alias payloads; each entry's extent was checked independently.
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
    auto end = library.pFileHeader.end();
    auto found = std::lower_bound(library.pFileHeader.begin(), end, name,
        [](const FileHeaderStruct& entry, std::string_view key) {
            return std::string_view(entry.pFileName) < key;
        });
    return found != end && name == found->pFileName ? &*found : nullptr;
}

} // namespace

BOOLEAN InitializeFileDatabase(void)
{
    ShutDownFileDatabase();
    try
    {
        gFileDataBase.pLibraries.resize(MAX_NUMBER_OF_LIBRARIES);
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
    if (!directory || gFileDataBase.pLibraries.empty())
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
    if (gFileDataBase.pLibraries.empty())
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
    sgp::close_files();
    gFileDataBase.pLibraries.clear();
    gFileDataBase.usNumberOfLibraries = 0;
    gFileDataBase.fInitialized = FALSE;
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
        LibraryHeaderStruct header{};
        header.fPatchLibrary = library->fPatchLibrary;
        header.sLibraryPath = entry_key(disk_text(disk.sPathToLibrary));
        if (!entries.empty())
        {
            header.pFileHeader.resize(entries.size());
            header.usNumberOfEntries = static_cast<UINT16>(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                auto& target = header.pFileHeader[i];
                target.pFileName = std::move(entries[i].name);
                target.uiFileOffset = entries[i].offset;
                target.uiFileLength = entries[i].length;
                target.sFileTime = entries[i].time;
            }
        }
        header.fLibraryOpen = TRUE;
        header.hLibraryHandle = std::move(file);
        *library = std::move(header);
        return TRUE;
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

INT16 GetLibraryIDFromFileName(const char* name)
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
                 library.sLibraryPath.size() >
                 gFileDataBase.pLibraries[best].sLibraryPath.size()))
                best = id;
        }
        return best;
    }
    catch (const std::exception&)
    {
        return -1;
    }
}

HWFILE OpenFileFromLibrary(const char* name)
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
        auto stream = wiz8::open_host_file(library.hLibraryHandle->physical_path());
        if (std::int64_t(header->uiFileOffset) + header->uiFileLength > stream->size())
            return 0;
        stream->seek(header->uiFileOffset, wiz8::SeekOrigin::begin);
        return sgp::register_file({std::move(stream), FILE_ACCESS_READ, {},
                                  sgp::ArchiveExtent{id, header->uiFileOffset,
                                                     header->uiFileLength, header->sFileTime}});
    }
    catch (const std::exception&)
    {
        return 0;
    }
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
    sgp::close_files(id);
    const auto patch = gFileDataBase.pLibraries[id].fPatchLibrary;
    release_library(gFileDataBase.pLibraries[id]);
    gFileDataBase.pLibraries[id].fPatchLibrary = patch;
    return TRUE;
}

BOOLEAN IsLibraryOpened(INT16 id)
{
    return valid_library(id) && gFileDataBase.pLibraries[id].fLibraryOpen;
}
