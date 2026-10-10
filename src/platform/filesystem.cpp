#include "wiz8/filesystem.h"
#include "path_resolver.h"
#include "slf.h"

#include <SDL3/SDL_error.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <random>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;
namespace wiz8
{
namespace
{
[[noreturn]] void sdl_failure(const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
const fs::path& write_destination(const w8_native::ResolvedPath& path)
{
    if (path.writable.empty())
        throw std::system_error(std::make_error_code(std::errc::permission_denied), "immutable file path");
    return path.writable;
}
void prepare_write(const w8_native::ResolvedPath& path, bool preserve)
{
    const auto& destination = write_destination(path);
    if (!path.overlay)
        return;
    if (!fs::is_directory(path.readable_parent))
        throw std::system_error(std::make_error_code(std::errc::not_a_directory), "game parent directory");
    fs::create_directories(destination.parent_path());
    if (preserve && !fs::exists(destination) && fs::exists(path.readable))
    {
        fs::copy_file(path.readable, destination);
        fs::permissions(destination, fs::perms::owner_write, fs::perm_options::add);
    }
}
std::optional<FileStatus> physical_status(const fs::path& path, bool writable)
{
    const fs::file_status status = fs::status(path);
    if (!fs::exists(status))
        return std::nullopt;
    SDL_PathInfo info{};
    if (!SDL_GetPathInfo(path_to_utf8(path).c_str(), &info))
        sdl_failure("query file metadata");
    const auto write_bits = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    return FileStatus{info, writable, (status.permissions() & write_bits) == fs::perms::none};
}
char fold(char value)
{
    return value >= 'A' && value <= 'Z' ? char(value + ('a' - 'A')) : value;
}
bool matches(std::string_view name, std::string_view pattern)
{
    if (pattern == "*.*")
        pattern = "*";
    std::size_t n = 0, p = 0, star = std::string_view::npos, retry = 0;
    while (n < name.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || fold(pattern[p]) == fold(name[n])))
        {
            ++n;
            ++p;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            retry = n;
        }
        else if (star != std::string_view::npos)
        {
            p = star + 1;
            n = ++retry;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}
struct StagingDirectory
{
    fs::path path;
    explicit StagingDirectory(const fs::path& parent)
    {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            auto candidate = parent / (".wiz8-copy-" + std::to_string(random()));
            if (fs::create_directory(candidate))
            {
                path = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot reserve copy staging directory");
    }
    ~StagingDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
void copy_physical(const fs::path& source, const fs::path& destination, CopyMode mode)
{
    if (fs::exists(destination) && fs::equivalent(source, destination))
        throw std::invalid_argument("copy source and destination refer to the same file");
    // A staging file leaves the old save untouched on read/write failure.
    StagingDirectory staging(destination.parent_path());
    const fs::path temporary = staging.path / "content";
    auto input = open_host_file(source);
    auto output = open_host_file(temporary, OpenMode::replace);
    std::array<std::byte, 65536> buffer;
    for (;;)
    {
        const auto result = input->read(buffer.data(), buffer.size());
        output->write(buffer.data(), result.bytes);
        if (result.eof)
            break;
    }
    output->flush();
    output->close();
    input->close();
    if (mode == CopyMode::fail_if_exists)
        fs::copy_file(temporary, destination, fs::copy_options::none);
    else if (!SDL_RenamePath(path_to_utf8(temporary).c_str(), path_to_utf8(destination).c_str()))
        sdl_failure("replace file");
}
} // namespace

fs::path path_from_utf8(std::string_view text)
{
    return fs::u8path(text.begin(), text.end());
}
std::string path_to_utf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
File::File(fs::path path, OpenMode mode) : path_(std::move(path)), writable_(mode != OpenMode::read)
{
    const char* access = "rb";
    switch (mode)
    {
    case OpenMode::read: break;
    case OpenMode::update: access = "r+b"; break;
    case OpenMode::replace: access = "w+b"; break;
    case OpenMode::append: access = "a+b"; break;
    }
    stream_ = SDL_IOFromFile(path_to_utf8(path_).c_str(), access);
    if (!stream_)
        sdl_failure("open file");
    if (writable_)
    {
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(path_to_utf8(path_).c_str(), &info))
        {
            const std::string error = SDL_GetError();
            SDL_CloseIO(std::exchange(stream_, nullptr));
            throw std::runtime_error("snapshot writer metadata: " + error);
        }
        writer_create_time_ = info.create_time;
    }
}
File::~File()
{
    if (stream_)
        SDL_CloseIO(stream_);
}
File::File(File&& other) noexcept
    : stream_(std::exchange(other.stream_, nullptr)), path_(std::move(other.path_)),
      writable_(other.writable_), writer_create_time_(other.writer_create_time_), extent_(other.extent_)
{
}
File& File::operator=(File&& other) noexcept
{
    if (this != &other)
    {
        if (stream_)
            SDL_CloseIO(stream_);
        stream_ = std::exchange(other.stream_, nullptr);
        path_ = std::move(other.path_);
        writable_ = other.writable_;
        writer_create_time_ = other.writer_create_time_;
        extent_ = other.extent_;
    }
    return *this;
}
SDL_IOStream* File::require_stream() const
{
    if (!stream_)
        throw std::logic_error("file is closed");
    return stream_;
}
ReadResult File::read(void* data, std::size_t bytes)
{
    SDL_IOStream* stream = require_stream();
    if (!bytes)
        return {0, false};
    if (!data)
        throw std::invalid_argument("null read buffer");
    if (extent_ && (bytes > static_cast<std::uint64_t>(size() - tell()) ||
                   SDL_GetIOSize(require_stream()) < std::int64_t(extent_->offset) + extent_->length))
        throw std::out_of_range("read exceeds SLF entry");
    std::size_t total = 0;
    while (total < bytes)
    {
        total += SDL_ReadIO(stream, static_cast<std::byte*>(data) + total, bytes - total);
        const auto status = SDL_GetIOStatus(stream);
        if (status == SDL_IO_STATUS_EOF)
            return {total, true};
        if (status != SDL_IO_STATUS_READY)
            sdl_failure("read file");
    }
    return {total, false};
}
void File::read_exact(void* data, std::size_t bytes)
{
    if (read(data, bytes).bytes != bytes)
        throw std::runtime_error("short file read");
}
void File::write(const void* data, std::size_t bytes)
{
    SDL_IOStream* stream = require_stream();
    if (!writable_)
        throw std::logic_error("file was opened for reading only");
    if (bytes && !data)
        throw std::invalid_argument("null write buffer");
    std::size_t total = 0;
    while (total < bytes)
    {
        const auto count = SDL_WriteIO(stream, static_cast<const std::byte*>(data) + total, bytes - total);
        total += count;
        if (!count || SDL_GetIOStatus(stream) != SDL_IO_STATUS_READY)
            sdl_failure("write file");
    }
}
std::int64_t File::seek(std::int64_t offset, SeekOrigin origin)
{
    std::int64_t base = 0;
    switch (origin)
    {
    case SeekOrigin::begin: break;
    case SeekOrigin::current: base = tell(); break;
    case SeekOrigin::end: base = size(); break;
    default: throw std::invalid_argument("invalid seek origin");
    }
    if (offset < -base || offset > std::numeric_limits<std::int64_t>::max() - base)
        throw std::out_of_range("seek outside signed file positions");
    const auto destination = base + offset;
    if (extent_ && destination > extent_->length)
        throw std::out_of_range("seek exceeds SLF entry");
    const auto physical = destination + (extent_ ? extent_->offset : 0);
    const auto position = SDL_SeekIO(require_stream(), physical, SDL_IO_SEEK_SET);
    if (position != physical)
        sdl_failure("seek file");
    return destination;
}
std::int64_t File::tell() const
{
    const auto position = SDL_TellIO(require_stream());
    if (position < 0)
        sdl_failure("tell file");
    return position - (extent_ ? extent_->offset : 0);
}
std::int64_t File::size() const
{
    if (extent_) { require_stream(); return extent_->length; }
    const auto bytes = SDL_GetIOSize(require_stream());
    if (bytes < 0)
        sdl_failure("size file");
    return bytes;
}
void File::flush()
{
    if (!SDL_FlushIO(require_stream()))
        sdl_failure("flush file");
}
void File::close()
{
    if (stream_ && !SDL_CloseIO(std::exchange(stream_, nullptr)))
        sdl_failure("close file");
}
bool File::is_open() const noexcept { return stream_ != nullptr; }
const fs::path& File::physical_path() const noexcept { return path_; }
FileStatus File::status() const
{
    if (extent_)
    {
        FileStatus result{};
        result.info.type = SDL_PATHTYPE_FILE;
        result.info.size = extent_->length;
        result.info.modify_time = file_time_to_sdl(extent_->modified).value_or(0);
        result.read_only = result.archived = true;
        return result;
    }
    const auto info = host_file_status(path_);
    if (!info)
        throw std::runtime_error("opened file path no longer exists");
    return *info;
}
FileTimes File::times() const
{
    require_stream();
    if (extent_) return {{}, {}, extent_->modified};
    const auto info = status().info;
    const auto offset = current_utc_offset_seconds();
    const auto convert = [offset](SDL_Time time) {
        return file_time_with_legacy_local_bias(file_time_from_sdl(time), offset);
    };
    return {convert(writer_create_time_.value_or(info.create_time)),
            convert(info.access_time), convert(info.modify_time)};
}
std::optional<SDL_Time> File::opened_writer_create_time() const noexcept
{
    return writer_create_time_;
}
std::unique_ptr<File> open_file(std::string_view game_path, OpenMode mode)
{
    const auto path = w8_native::resolve_path(game_path);
    if (mode == OpenMode::read)
    {
        if (physical_status(path.readable, path.writable_source))
            return std::unique_ptr<File>(new File(path.readable, mode));
        if (const auto entry = archive_entry(game_path))
        {
            auto file = open_host_file(entry->archive);
            if (std::int64_t(entry->offset) + entry->length > file->size())
                throw std::runtime_error("truncated SLF entry");
            file->seek(entry->offset, SeekOrigin::begin);
            file->extent_ = File::Extent{entry->offset, entry->length, entry->modified};
            return file;
        }
        return std::unique_ptr<File>(new File(path.readable, mode));
    }
    if (mode == OpenMode::update && !fs::exists(path.readable))
        throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory),
                                "update existing game file");
    prepare_write(path, mode != OpenMode::replace);
    return std::unique_ptr<File>(new File(write_destination(path), mode));
}
std::unique_ptr<File> open_host_file(const fs::path& path, OpenMode mode)
{
    const auto resolved = w8_native::resolve_path(path_to_utf8(path), w8_native::PathDomain::host);
    return std::unique_ptr<File>(new File(mode == OpenMode::read ? resolved.readable
                                                               : write_destination(resolved), mode));
}
std::optional<FileStatus> file_status(std::string_view game_path)
{
    const auto path = w8_native::resolve_path(game_path);
    if (const auto status = physical_status(path.readable, path.writable_source)) return status;
    if (const auto entry = archive_entry(game_path))
    {
        FileStatus result{};
        result.info.type = SDL_PATHTYPE_FILE;
        result.info.size = entry->length;
        result.info.modify_time = file_time_to_sdl(entry->modified).value_or(0);
        result.read_only = result.archived = true;
        return result;
    }
    return std::nullopt;
}
std::optional<FileStatus> host_file_status(const fs::path& path)
{
    const auto resolved = w8_native::resolve_path(path_to_utf8(path), w8_native::PathDomain::host);
    return physical_status(resolved.readable, resolved.writable_source);
}
bool remove_file(std::string_view game_path)
{
    const auto path = w8_native::resolve_path(game_path);
    const auto& destination = write_destination(path);
    const auto info = physical_status(destination, true);
    if (!info)
        return false;
    if (info->info.type != SDL_PATHTYPE_FILE)
        throw std::invalid_argument("remove_file requires a regular file");
    if (!SDL_RemovePath(path_to_utf8(destination).c_str()))
        sdl_failure("remove game file");
    return true;
}
void create_directory(std::string_view game_path)
{
    const auto path = w8_native::resolve_path(game_path);
    if (!SDL_CreateDirectory(path_to_utf8(write_destination(path)).c_str()))
        sdl_failure("create game directory");
}
void clear_read_only(std::string_view game_path)
{
    const auto path = w8_native::resolve_path(game_path);
    const auto status = physical_status(path.readable, path.writable_source);
    if (!status)
        throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory));
    const auto& destination = write_destination(path);
    if (status->info.type == SDL_PATHTYPE_DIRECTORY)
    {
        if (!SDL_CreateDirectory(path_to_utf8(destination).c_str()))
            sdl_failure("create writable game directory");
    }
    else if (!path.writable_source)
    {
        prepare_write(path, false);
        copy_physical(path.readable, destination, CopyMode::replace);
    }
    fs::permissions(destination, fs::perms::owner_write, fs::perm_options::add);
}
std::vector<std::string> list_directory(std::string_view directory, std::string_view pattern)
{
    const auto path = w8_native::resolve_path(directory);
    const auto status = physical_status(path.readable, path.writable_source);
    if (!status || status->info.type != SDL_PATHTYPE_DIRECTORY)
        throw std::invalid_argument("list_directory requires an existing directory");
    std::vector<std::string> entries;
    for (const auto& base : path.directories)
    {
        std::error_code error;
        std::vector<std::string> next;
        for (fs::directory_iterator it(base, error), end; !error && it != end; it.increment(error))
            next.push_back(path_to_utf8(it->path().filename()));
        std::sort(next.begin(), next.end());
        for (const auto& name : next)
            if (matches(name, pattern) &&
                std::none_of(entries.begin(), entries.end(), [&](const auto& previous) {
                    return previous.size() == name.size() &&
                           std::equal(previous.begin(), previous.end(), name.begin(),
                                      [](char a, char b) { return fold(a) == fold(b); });
                }))
                entries.push_back(name);
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}
void copy_file(std::string_view source, std::string_view destination, CopyMode mode)
{
    const auto from = w8_native::resolve_path(source);
    const auto to = w8_native::resolve_path(destination);
    if (mode == CopyMode::fail_if_exists && fs::exists(to.readable))
        throw std::system_error(std::make_error_code(std::errc::file_exists), "copy game file");
    prepare_write(to, false);
    copy_physical(from.readable, write_destination(to), mode);
}
void replace_file(File& destination, std::string_view source)
{
    const auto from = w8_native::resolve_path(source);
    const auto to = w8_native::resolve_path(path_to_utf8(destination.physical_path()),
                                          w8_native::PathDomain::host);
    const auto& target = write_destination(to);
    destination.close();
    copy_physical(from.readable, target, CopyMode::replace);
}
} // namespace wiz8
