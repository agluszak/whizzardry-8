/* Modified for the Wizardry 8 reconstruction: 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "FileMan.h"
#include "LibraryDataBase.h"
#include "file_handles.h"
#include <wiz8/asset_paths.h>
#include <wiz8/filesystem.h>
#include <wiz8/file_time.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

DatabaseManagerHeaderStruct gFileDataBase{};

namespace sgp
{
namespace
{
std::vector<std::unique_ptr<OpenFile>> files(1);
}
OpenFile* find_file(HWFILE handle)
{
    return handle < files.size() ? files[handle].get() : nullptr;
}
HWFILE register_file(OpenFile file)
{
    if (!file.stream || !file.stream->is_open())
        return 0;
    auto entry = std::make_unique<OpenFile>(std::move(file));
    const auto free = std::find(files.begin() + 1, files.end(), nullptr);
    if (free != files.end())
    {
        *free = std::move(entry);
        return HWFILE(free - files.begin());
    }
    if (files.size() >= std::numeric_limits<HWFILE>::max())
        return 0;
    files.push_back(std::move(entry));
    return HWFILE(files.size() - 1);
}
void close_files(INT16 library)
{
    for (std::size_t id = 1; id < files.size(); ++id)
        if (files[id] && (library < 0 ||
                         (files[id]->archive && files[id]->archive->library == library)))
            FileClose(HWFILE(id));
}
std::int64_t OpenFile::tell() const
{
    const auto position = stream->tell() - (archive ? std::int64_t(archive->offset) : 0);
    if (position < 0 || (archive && position > archive->length))
        throw std::out_of_range("SLF cursor outside entry");
    return position;
}
std::int64_t OpenFile::size() const
{
    return archive ? std::int64_t(archive->length) : stream->size();
}
wiz8::ReadResult OpenFile::read(void* data, std::size_t bytes)
{
    if (archive && (bytes > std::uint64_t(size() - tell()) ||
                    std::int64_t(archive->offset) + archive->length > stream->size()))
        throw std::out_of_range("read exceeds SLF extent");
    return stream->read(data, bytes);
}
void OpenFile::seek(std::int64_t offset, wiz8::SeekOrigin origin)
{
    if (!archive)
    {
        stream->seek(offset, origin);
        return;
    }
    const auto base = origin == wiz8::SeekOrigin::begin ? 0 :
                      origin == wiz8::SeekOrigin::current ? tell() : size();
    if (offset < -base || offset > size() - base)
        throw std::out_of_range("seek exceeds SLF extent");
    stream->seek(std::int64_t(archive->offset) + base + offset, wiz8::SeekOrigin::begin);
}
} // namespace sgp

namespace
{
struct DirectorySearch
{
    bool active = false;
    std::string directory;
    std::vector<std::string> names;
    std::size_t next = 0;
};
std::array<DirectorySearch, 20> searches;

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

UINT32 attributes(const wiz8::FileStatus& status, std::string_view name, bool enumeration)
{
    UINT32 bits = status.info.type == SDL_PATHTYPE_DIRECTORY
                      ? (enumeration ? FILE_IS_DIRECTORY : FILE_ATTRIBUTES_DIRECTORY)
                      : (enumeration ? FILE_IS_NORMAL : FILE_ATTRIBUTES_NORMAL);
    if (!status.writable || status.read_only)
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
    return TRUE;
}

void ShutdownFileManager()
{
    sgp::close_files();
    for (auto& search : searches)
        search = {};
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

HWFILE FileOpen(const char* filename, UINT32 options, BOOLEAN delete_on_close)
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
        return sgp::register_file({wiz8::open_file(filename, open_mode(options, exists)), options,
                                  delete_on_close ? w8_native::full_path(filename) : "", {}});
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
        return sgp::register_file({wiz8::open_host_file(path, open_mode(options, status.has_value())),
                                  options, {}, {}});
    }
    catch (...) { return 0; }
}

void FileClose(HWFILE file)
{
    if (!sgp::find_file(file))
        return;
    auto entry = std::move(sgp::files[file]);
    try { entry->stream->close(); }
    catch (...) { return; }
    if (!entry->delete_on_close.empty())
        FileDelete(entry->delete_on_close.data());
}

BOOLEAN FileRead(HWFILE file, PTR destination, UINT32 bytes, UINT32* read)
{
    if (read)
        *read = 0;
    if (bytes && !destination)
        return FALSE;
    try
    {
        if (auto* slot = sgp::find_file(file))
        {
            if (!(slot->access & FILE_ACCESS_READ))
                return FALSE;
            const auto result = slot->read(destination, bytes);
            if (read)
                *read = UINT32(result.bytes);
            return result.bytes == bytes;
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
        auto* slot = sgp::find_file(file);
        if (!slot || !(slot->access & FILE_ACCESS_WRITE))
            return FALSE;
        slot->stream->write(source, bytes);
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
        if (auto* slot = sgp::find_file(file))
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
            slot->seek(offset, origin);
            return TRUE;
        }
    }
    catch (...) {}
    return FALSE;
}

INT32 FileGetPos(HWFILE file)
{
    try
    {
        if (auto* slot = sgp::find_file(file))
        {
            const auto position = slot->tell();
            return position <= std::numeric_limits<INT32>::max() ? INT32(position) : BAD_INDEX;
        }
    }
    catch (...) {}
    return BAD_INDEX;
}

UINT32 FileGetSize(HWFILE file)
{
    try
    {
        if (auto* slot = sgp::find_file(file))
        {
            const auto size = slot->size();
            return size <= std::numeric_limits<UINT32>::max() ? UINT32(size) : 0;
        }
    }
    catch (...) {}
    return 0;
}

wiz8::File* OpenLibraryStream(HWFILE file)
{
    auto* slot = sgp::find_file(file);
    if (!slot || !slot->archive)
        return nullptr;
    try
    {
        auto stream = wiz8::open_host_file(slot->stream->physical_path());
        const auto& extent = *slot->archive;
        if (std::int64_t(extent.offset) + extent.length > stream->size())
            return nullptr;
        stream->seek(extent.offset, wiz8::SeekOrigin::begin);
        return stream.release();
    }
    catch (...) { return nullptr; }
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
        return attributes(*status, filename, false);
    }
    catch (...) { return UINT32(-1); }
}

BOOLEAN FileClearAttributes(STR filename)
{
    if (!filename)
        return FALSE;
    try
    {
        wiz8::clear_read_only(filename);
        return TRUE;
    }
    catch (...) { return FALSE; }
}

BOOLEAN FileCheckEndOfFile(HWFILE file)
{
    try
    {
        if (auto* slot = sgp::find_file(file))
            return slot->tell() >= slot->size();
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
        if (auto* slot = sgp::find_file(file))
        {
            if (slot->archive)
            {
                if (write)
                    *write = slot->archive->modified;
                return TRUE;
            }
            const auto& stream = *slot->stream;
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
