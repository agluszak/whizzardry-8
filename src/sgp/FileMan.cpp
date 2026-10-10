/* Modified for the Wizardry 8 reconstruction: 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "FileMan.h"
#include "LibraryDataBase.h"
#include "DEBUG.H"
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>
#include <wiz8/file_time.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

DatabaseManagerHeaderStruct gFileDataBase{};

namespace
{
struct SlotPolicy
{
    UINT32 access = 0;
    std::string delete_on_close;
};
// Streams remain owned by the realloc-managed SGP slots, not a handle registry.
std::vector<SlotPolicy> slot_policies;

struct DirectorySearch
{
    bool active = false;
    std::string directory;
    std::vector<std::string> names;
    std::size_t next = 0;
};
std::array<DirectorySearch, 20> searches;

RealFileOpenStruct* real_slot(HWFILE file)
{
    const auto id = DB_EXTRACT_FILE_ID(file);
    auto& real = gFileDataBase.RealFiles;
    if (DB_EXTRACT_LIBRARY(file) != REAL_FILE_LIBRARY_ID || !id ||
        !real.pRealFilesOpen || id >= UINT32(real.iSizeOfOpenFileArray))
        return nullptr;
    auto& slot = real.pRealFilesOpen[id];
    return slot.uiFileID == file && slot.hRealFileHandle ? &slot : nullptr;
}

FileOpenStruct* library_slot(HWFILE file)
{
    const auto library = DB_EXTRACT_LIBRARY(file);
    const auto id = DB_EXTRACT_FILE_ID(file);
    if (!gFileDataBase.fInitialized || !gFileDataBase.pLibraries ||
        library >= gFileDataBase.usNumberOfLibraries || !id)
        return nullptr;
    auto& entry = gFileDataBase.pLibraries[library];
    if (!entry.fLibraryOpen || !entry.pOpenFiles || id >= UINT32(entry.iSizeOfOpenFileArray))
        return nullptr;
    auto& slot = entry.pOpenFiles[id];
    return slot.uiFileID == file && slot.pFileHeader ? &slot : nullptr;
}

bool can_access(HWFILE file, UINT32 access)
{
    const auto id = DB_EXTRACT_FILE_ID(file);
    return id < slot_policies.size() && (slot_policies[id].access & access) != 0;
}

wiz8::OpenMode open_mode(UINT32 options, bool exists)
{
    if (!(options & FILE_ACCESS_WRITE))
        return wiz8::OpenMode::read;
    if (options & FILE_ACCESS_APPEND)
        return wiz8::OpenMode::append;
    if (options & (FILE_CREATE_ALWAYS | FILE_TRUNCATE_EXISTING | FILE_CREATE_NEW))
        return wiz8::OpenMode::replace;
    return exists ? wiz8::OpenMode::update : wiz8::OpenMode::replace;
}

bool valid_options(UINT32 options, bool exists)
{
    if (!(options & FILE_ACCESS_READWRITE) ||
        ((options & FILE_ACCESS_APPEND) &&
         (!(options & FILE_ACCESS_WRITE) || (options & (FILE_CREATE_ALWAYS | FILE_TRUNCATE_EXISTING)))))
        return false;
    if ((options & FILE_CREATE_NEW) && exists)
        return false;
    // Chunk::OpenWrite combines CREATE_ALWAYS and TRUNCATE_EXISTING.
    const bool create = options & (FILE_CREATE_NEW | FILE_CREATE_ALWAYS);
    if (!create && (options & (FILE_OPEN_EXISTING | FILE_TRUNCATE_EXISTING)) && !exists)
        return false;
    if ((options & (FILE_CREATE_NEW | FILE_CREATE_ALWAYS | FILE_TRUNCATE_EXISTING)) &&
        !(options & FILE_ACCESS_WRITE))
        return false;
    return true;
}

HWFILE register_stream(std::unique_ptr<wiz8::File> stream, SlotPolicy policy)
{
    const auto capacity = std::size_t(std::max(0, gFileDataBase.RealFiles.iSizeOfOpenFileArray)) +
                          NUM_FILES_TO_ADD_AT_A_TIME;
    slot_policies.resize(std::max(slot_policies.size(), capacity));
    const HWFILE id = CreateRealFileHandle(stream.get());
    if (id)
    {
        (void)stream.release();
        slot_policies[DB_EXTRACT_FILE_ID(id)] = std::move(policy);
    }
    return id;
}

UINT32 attributes(const wiz8::FileStatus& status, std::string_view name, bool enumeration)
{
    UINT32 bits = status.info.type == SDL_PATHTYPE_DIRECTORY
                      ? (enumeration ? FILE_IS_DIRECTORY : FILE_ATTRIBUTES_DIRECTORY)
                      : (enumeration ? FILE_IS_NORMAL : FILE_ATTRIBUTES_NORMAL);
    if (!status.writable)
        bits |= enumeration ? FILE_IS_READONLY : FILE_ATTRIBUTES_READONLY;
    const auto slash = name.find_last_of("/\\");
    const auto base = slash == std::string_view::npos ? name : name.substr(slash + 1);
    if (!base.empty() && base.front() == '.')
        bits |= enumeration ? FILE_IS_HIDDEN : FILE_ATTRIBUTES_HIDDEN;
    return bits;
}

bool next_entry(DirectorySearch& search, GETFILESTRUCT& result)
{
    while (search.next < search.names.size())
    {
        const auto& name = search.names[search.next++];
        const auto status = wiz8::file_status(search.directory + name);
        if (!status)
            continue;
        if (name.size() >= sizeof(result.zFileName))
            return false;
        std::memcpy(result.zFileName, name.c_str(), name.size() + 1);
        result.uiFileSize = UINT32(std::min<Uint64>(status->info.size,
                                                   std::numeric_limits<UINT32>::max()));
        result.uiFileAttribs = attributes(*status, name, true);
        return true;
    }
    return false;
}

void store_time(SGP_FILETIME* output, wiz8::DiskFileTime value)
{
    if (output)
    {
        output->dwLowDateTime = value.low;
        output->dwHighDateTime = value.high;
    }
}

std::uint64_t ticks(const SGP_FILETIME& value)
{
    return wiz8::DiskFileTime{value.dwLowDateTime, value.dwHighDateTime}.ticks();
}
}

BOOLEAN InitializeFileManager(STR)
{
    RegisterDebugTopic(TOPIC_FILE_MANAGER, "File Manager");
    return TRUE;
}

void ShutdownFileManager()
{
    auto& real = gFileDataBase.RealFiles;
    for (INT32 id = 1; real.pRealFilesOpen && id < real.iSizeOfOpenFileArray; ++id)
        if (real.pRealFilesOpen[id].uiFileID)
            FileClose(real.pRealFilesOpen[id].uiFileID);
    slot_policies.clear();
    for (auto& search : searches)
        search = {};
    UnRegisterDebugTopic(TOPIC_FILE_MANAGER, "File Manager");
}

BOOLEAN FileExistsNoDB(STR filename)
{
    if (!filename)
        return FALSE;
    try
    {
        const auto status = wiz8::file_status(filename);
        return status && status->info.type == SDL_PATHTYPE_FILE;
    }
    catch (...) { return FALSE; }
}

BOOLEAN FileExists(STR filename)
{
    if (!filename)
        return FALSE;
    try
    {
        const auto status = wiz8::file_status(filename);
        if (status)
            return status->info.type == SDL_PATHTYPE_FILE;
        return gFileDataBase.fInitialized && CheckIfFileExistInLibrary(filename);
    }
    catch (...) { return FALSE; }
}

BOOLEAN FileDelete(STR filename)
{
    if (!filename)
        return FALSE;
    try { return wiz8::remove_file(filename); }
    catch (...) { return FALSE; }
}

HWFILE FileOpen(STR filename, UINT32 options, BOOLEAN delete_on_close)
{
    if (!filename)
        return 0;
    try
    {
        const auto status = wiz8::file_status(filename);
        const bool exists = status.has_value();
        if (!exists && !(options & FILE_ACCESS_WRITE) && !delete_on_close &&
            valid_options(options, true) && gFileDataBase.fInitialized)
            return OpenFileFromLibrary(filename);
        if (!valid_options(options, exists) ||
            (status && status->info.type != SDL_PATHTYPE_FILE))
            return 0;
        if (delete_on_close && !(options & FILE_ACCESS_WRITE) && (!status || !status->writable))
            return 0;
        SlotPolicy policy{options, delete_on_close ? w8_native::full_path(filename) : ""};
        return register_stream(wiz8::open_file(filename, open_mode(options, exists)), std::move(policy));
    }
    catch (...) { return 0; }
}

HWFILE FileOpenHost(const std::filesystem::path& path, UINT32 options)
{
    try
    {
        const auto status = wiz8::host_file_status(path);
        if (!valid_options(options, status.has_value()) ||
            (status && status->info.type != SDL_PATHTYPE_FILE))
            return 0;
        return register_stream(wiz8::open_host_file(path, open_mode(options, status.has_value())),
                               SlotPolicy{options, {}});
    }
    catch (...) { return 0; }
}

void FileClose(HWFILE file)
{
    if (auto* slot = real_slot(file))
    {
        std::unique_ptr<wiz8::File> stream(slot->hRealFileHandle);
        slot->hRealFileHandle = nullptr;
        slot->uiFileID = 0;
        --gFileDataBase.RealFiles.iNumFilesOpen;
        const auto id = DB_EXTRACT_FILE_ID(file);
        SlotPolicy policy;
        if (id < slot_policies.size())
            policy = std::exchange(slot_policies[id], {});
        try { stream->close(); }
        catch (...) { return; }
        if (!policy.delete_on_close.empty())
            FileDelete(policy.delete_on_close.data());
    }
    else if (library_slot(file))
        CloseLibraryFile(INT16(DB_EXTRACT_LIBRARY(file)), DB_EXTRACT_FILE_ID(file));
}

BOOLEAN FileRead(HWFILE file, PTR destination, UINT32 bytes, UINT32* read)
{
    if (read)
        *read = 0;
    if (bytes && !destination)
        return FALSE;
    try
    {
        if (auto* slot = real_slot(file))
        {
            if (!can_access(file, FILE_ACCESS_READ))
                return FALSE;
            const auto result = slot->hRealFileHandle->read(destination, bytes);
            if (read)
                *read = UINT32(result.bytes);
            return result.bytes == bytes;
        }
        if (library_slot(file))
        {
            UINT32 count = 0;
            const bool success = LoadDataFromLibrary(INT16(DB_EXTRACT_LIBRARY(file)),
                                                     DB_EXTRACT_FILE_ID(file), destination, bytes, &count);
            if (read)
                *read = count;
            return success && count == bytes;
        }
    }
    catch (...) {}
    return FALSE;
}

BOOLEAN FileWrite(HWFILE file, PTR source, UINT32 bytes, UINT32* written)
{
    if (written)
        *written = 0;
    try
    {
        auto* slot = real_slot(file);
        if (!slot || !can_access(file, FILE_ACCESS_WRITE))
            return FALSE;
        slot->hRealFileHandle->write(source, bytes);
        if (written)
            *written = bytes;
        return TRUE;
    }
    catch (...) { return FALSE; }
}

BOOLEAN FileSeek(HWFILE file, UINT32 distance, UINT8 how)
{
    if (how != FILE_SEEK_FROM_START && how != FILE_SEEK_FROM_CURRENT && how != FILE_SEEK_FROM_END)
        return FALSE;
    try
    {
        if (auto* slot = real_slot(file))
        {
            std::int64_t offset = how == FILE_SEEK_FROM_CURRENT ? std::int64_t(INT32(distance))
                                                              : std::int64_t(distance);
            auto origin = wiz8::SeekOrigin::begin;
            if (how == FILE_SEEK_FROM_END)
            {
                origin = wiz8::SeekOrigin::end;
                offset = -offset;
            }
            else if (how == FILE_SEEK_FROM_CURRENT)
                origin = wiz8::SeekOrigin::current;
            slot->hRealFileHandle->seek(offset, origin);
            return TRUE;
        }
        if (library_slot(file))
            return LibraryFileSeek(INT16(DB_EXTRACT_LIBRARY(file)), DB_EXTRACT_FILE_ID(file), distance, how);
    }
    catch (...) {}
    return FALSE;
}

INT32 FileGetPos(HWFILE file)
{
    try
    {
        if (auto* slot = real_slot(file))
        {
            const auto position = slot->hRealFileHandle->tell();
            return position <= std::numeric_limits<INT32>::max() ? INT32(position) : BAD_INDEX;
        }
        if (auto* slot = library_slot(file))
            return slot->uiFilePosInFile <= UINT32(std::numeric_limits<INT32>::max())
                       ? INT32(slot->uiFilePosInFile) : BAD_INDEX;
    }
    catch (...) {}
    return BAD_INDEX;
}

UINT32 FileGetSize(HWFILE file)
{
    try
    {
        if (auto* slot = real_slot(file))
        {
            const auto size = slot->hRealFileHandle->size();
            return size <= std::numeric_limits<UINT32>::max() ? UINT32(size) : 0;
        }
        if (auto* slot = library_slot(file))
            return slot->pFileHeader->uiFileLength;
    }
    catch (...) {}
    return 0;
}

BOOLEAN DirectoryExists(STRING512 directory)
{
    if (!directory)
        return FALSE;
    try
    {
        const auto status = wiz8::file_status(directory);
        return status && status->info.type == SDL_PATHTYPE_DIRECTORY;
    }
    catch (...) { return FALSE; }
}

BOOLEAN MakeFileManDirectory(STRING512 directory)
{
    if (!directory)
        return FALSE;
    try { wiz8::create_directory(directory); return TRUE; }
    catch (...) { return FALSE; }
}

BOOLEAN GetExecutableDirectory(STRING512 directory)
{
    if (!directory)
        return FALSE;
    try
    {
        const auto assets = w8_native::path_roots().assets;
        if (assets.empty() || assets.size() >= sizeof(STRING512))
            return FALSE;
        std::memcpy(directory, assets.c_str(), assets.size() + 1);
        return TRUE;
    }
    catch (...) { return FALSE; }
}

BOOLEAN GetFileFirst(CHAR8* spec, GETFILESTRUCT* result)
{
    if (!spec || !result)
        return FALSE;
    result->iFindHandle = -1;
    try
    {
        const std::string path(spec);
        const auto slash = path.find_last_of("/\\");
        DirectorySearch search;
        search.directory = slash == std::string::npos ? "" : path.substr(0, slash + 1);
        const auto start = slash == std::string::npos && path.size() >= 2 && path[1] == ':' ? 2
                          : slash == std::string::npos ? 0 : slash + 1;
        if (start == 2 && slash == std::string::npos)
            search.directory = path.substr(0, 2);
        search.names = wiz8::list_directory(search.directory.empty() ? "." : search.directory,
                                            path.substr(start));
        search.directory = w8_native::full_path(search.directory.empty() ? "." : search.directory.c_str());
        if (search.directory.empty())
            return FALSE;
        if (search.directory.back() != '\\')
            search.directory += '\\';
        for (std::size_t index = 0; index < searches.size(); ++index)
            if (!searches[index].active)
            {
                if (!next_entry(search, *result))
                    return FALSE;
                search.active = true;
                searches[index] = std::move(search);
                result->iFindHandle = INT32(index);
                return TRUE;
            }
    }
    catch (...) {}
    return FALSE;
}

BOOLEAN GetFileNext(GETFILESTRUCT* result)
{
    if (!result || result->iFindHandle < 0 || std::size_t(result->iFindHandle) >= searches.size())
        return FALSE;
    try
    {
        auto& search = searches[result->iFindHandle];
        return search.active && next_entry(search, *result);
    }
    catch (...) { return FALSE; }
}

void GetFileClose(GETFILESTRUCT* result)
{
    if (!result || result->iFindHandle < 0 || std::size_t(result->iFindHandle) >= searches.size())
        return;
    searches[result->iFindHandle] = {};
    result->iFindHandle = -1;
}

BOOLEAN FileCopy(STR source, STR destination, BOOLEAN fail_if_exists)
{
    if (!source || !destination)
        return FALSE;
    try
    {
        wiz8::copy_file(source, destination, fail_if_exists ? wiz8::CopyMode::fail_if_exists
                                                         : wiz8::CopyMode::replace);
        return TRUE;
    }
    catch (...) { return FALSE; }
}

UINT32 FileGetAttributes(STR filename)
{
    if (!filename)
        return UINT32(-1);
    try
    {
        const auto status = wiz8::file_status(filename);
        if (!status)
            return UINT32(-1);
        auto bits = attributes(*status, filename, false);
        if (status->writable)
        {
            const auto physical = w8_native::mutation_path(filename);
            const auto permissions = std::filesystem::status(wiz8::path_from_utf8(physical)).permissions();
            using P = std::filesystem::perms;
            if ((permissions & (P::owner_write | P::group_write | P::others_write)) == P::none)
                bits |= FILE_ATTRIBUTES_READONLY;
        }
        return bits;
    }
    catch (...) { return UINT32(-1); }
}

BOOLEAN FileClearAttributes(STR filename)
{
    if (!filename)
        return FALSE;
    try
    {
        const auto status = wiz8::file_status(filename);
        if (!status)
            return FALSE;
        if (status->info.type == SDL_PATHTYPE_DIRECTORY)
            wiz8::create_directory(filename);
        else if (!status->writable)
        {
            // Copying through owned streams creates a writable overlay even
            // when the installed source has read-only host permissions.
            wiz8::copy_file(filename, filename, wiz8::CopyMode::replace);
        }
        const auto physical = w8_native::mutation_path(filename);
        if (physical.empty())
            return FALSE;
        std::filesystem::permissions(wiz8::path_from_utf8(physical), std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add);
        return TRUE;
    }
    catch (...) { return FALSE; }
}

BOOLEAN FileCheckEndOfFile(HWFILE file)
{
    try
    {
        if (auto* slot = real_slot(file))
            return slot->hRealFileHandle->tell() >= slot->hRealFileHandle->size();
        if (auto* slot = library_slot(file))
            return slot->uiFilePosInFile >= slot->pFileHeader->uiFileLength;
    }
    catch (...) {}
    return FALSE;
}

BOOLEAN GetFileManFileTime(HWFILE file, SGP_FILETIME* creation, SGP_FILETIME* access, SGP_FILETIME* write)
{
    store_time(creation, {});
    store_time(access, {});
    store_time(write, {});
    try
    {
        if (auto* slot = real_slot(file))
        {
            const auto& stream = *slot->hRealFileHandle;
            const auto info = stream.status().info;
            const int offset = wiz8::current_utc_offset_seconds();
            const auto convert = [offset](SDL_Time time)
            {
                return wiz8::file_time_with_legacy_local_bias(wiz8::file_time_from_sdl(time), offset);
            };
            // Readers use SDL metadata. Neither branch substitutes mtime for ctime.
            store_time(creation, convert(stream.opened_writer_create_time().value_or(info.create_time)));
            store_time(access, convert(info.access_time));
            store_time(write, convert(info.modify_time));
            return TRUE;
        }
        if (library_slot(file))
        {
            SGP_FILETIME disk{};
            if (!GetLibraryFileTime(INT16(DB_EXTRACT_LIBRARY(file)), DB_EXTRACT_FILE_ID(file), &disk))
                return FALSE;
            if (write)
                *write = disk;
            return TRUE;
        }
    }
    catch (...) {}
    return FALSE;
}

INT32 CompareSGPFileTimes(SGP_FILETIME* first, SGP_FILETIME* second)
{
    if (!first || !second)
        return 0;
    const auto a = ticks(*first), b = ticks(*second);
    return a < b ? -1 : a > b ? 1 : 0;
}

BOOLEAN FileIsOlderThanFile(CHAR8* first, CHAR8* second, UINT32 seconds)
{
    if (!first || !second)
        return FALSE;
    try
    {
        const auto a = wiz8::file_status(first), b = wiz8::file_status(second);
        if (!a || !b || a->info.type != SDL_PATHTYPE_FILE || b->info.type != SDL_PATHTYPE_FILE)
            return FALSE;
        const auto at = wiz8::file_time_from_sdl(a->info.modify_time).ticks();
        const auto bt = wiz8::file_time_from_sdl(b->info.modify_time).ticks();
        return at < bt && (!seconds || (bt - at) / 10000000 >= seconds);
    }
    catch (...) { return FALSE; }
}
