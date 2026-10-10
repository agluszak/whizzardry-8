#include "wiz8/filesystem.h"
#include "platform_paths.h"

#include <SDL3/SDL_error.h>

#include <algorithm>
#include <array>
#include <cerrno>
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
std::string checked_path(std::string path)
{
    if (path.empty())
        throw std::system_error(errno ? errno : EINVAL, std::generic_category(), "resolve file path");
    return path;
}
std::string path_text(std::string_view text)
{
    if (text.empty() || text.find('\0') != std::string_view::npos)
        throw std::invalid_argument("empty file path or embedded NUL");
    return std::string(text);
}
std::string game_path_text(std::string_view text)
{
    auto name = path_text(text);
    if (!(name.size() >= 2 && name[1] == ':') && path_from_utf8(name).is_absolute())
        throw std::invalid_argument("native absolute paths require the host import API");
    return name;
}
fs::path read_destination(std::string_view text)
{
    const auto name = game_path_text(text);
    return path_from_utf8(checked_path(w8_native::read_path(name.c_str())));
}
fs::path mutation_destination(std::string_view text)
{
    const auto name = game_path_text(text);
    return path_from_utf8(checked_path(w8_native::mutation_path(name.c_str())));
}
std::optional<FileStatus> physical_status(const fs::path& path, bool writable)
{
    const fs::file_status status = fs::status(path);
    if (!fs::exists(status))
        return std::nullopt;
    SDL_PathInfo info{};
    if (!SDL_GetPathInfo(path_to_utf8(path).c_str(), &info))
        sdl_failure("query file metadata");
    return FileStatus{info, writable};
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
      writable_(other.writable_), writer_create_time_(other.writer_create_time_)
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
    SDL_IOWhence whence = SDL_IO_SEEK_SET;
    switch (origin)
    {
    case SeekOrigin::begin: break;
    case SeekOrigin::current: whence = SDL_IO_SEEK_CUR; break;
    case SeekOrigin::end: whence = SDL_IO_SEEK_END; break;
    }
    const auto position = SDL_SeekIO(require_stream(), offset, whence);
    if (position < 0)
        sdl_failure("seek file");
    return position;
}
std::int64_t File::tell() const
{
    const auto position = SDL_TellIO(require_stream());
    if (position < 0)
        sdl_failure("tell file");
    return position;
}
std::int64_t File::size() const
{
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
    const auto info = host_file_status(path_);
    if (!info)
        throw std::runtime_error("opened file path no longer exists");
    return *info;
}
std::optional<SDL_Time> File::opened_writer_create_time() const noexcept
{
    return writer_create_time_;
}
std::unique_ptr<File> open_file(std::string_view game_path, OpenMode mode)
{
    const auto name = game_path_text(game_path);
    std::string physical;
    if (mode == OpenMode::read)
        physical = checked_path(w8_native::read_path(name.c_str()));
    else
    {
        if (mode == OpenMode::update && !file_status(game_path))
            throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory),
                                    "update existing game file");
        const bool copy_up = mode != OpenMode::replace &&
                             !fs::exists(mutation_destination(game_path)) && file_status(game_path);
        physical = checked_path(w8_native::write_path(name.c_str(), mode != OpenMode::replace));
        if (copy_up)
            fs::permissions(path_from_utf8(physical), fs::perms::owner_write, fs::perm_options::add);
    }
    return std::unique_ptr<File>(new File(path_from_utf8(physical), mode));
}
std::unique_ptr<File> open_host_file(const fs::path& path, OpenMode mode)
{
    const auto name = path_text(path_to_utf8(path));
    const auto physical = checked_path(mode == OpenMode::read ? w8_native::host_read_path(name)
                                                            : w8_native::host_write_path(name));
    return std::unique_ptr<File>(new File(path_from_utf8(physical), mode));
}
std::optional<FileStatus> file_status(std::string_view game_path)
{
    const auto name = game_path_text(game_path);
    return physical_status(read_destination(game_path), !w8_native::is_read_only_path(name.c_str()));
}
std::optional<FileStatus> host_file_status(const fs::path& path)
{
    const auto name = path_text(path_to_utf8(path));
    const auto physical = checked_path(w8_native::host_read_path(name));
    return physical_status(path_from_utf8(physical), !w8_native::host_write_path(name).empty());
}
bool remove_file(std::string_view game_path)
{
    const auto destination = mutation_destination(game_path);
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
    const auto destination = mutation_destination(game_path);
    if (!SDL_CreateDirectory(path_to_utf8(destination).c_str()))
        sdl_failure("create game directory");
}
std::vector<std::string> list_directory(std::string_view directory, std::string_view pattern)
{
    const auto name = game_path_text(directory);
    const auto status = file_status(directory);
    if (!status || status->info.type != SDL_PATHTYPE_DIRECTORY)
        throw std::invalid_argument("list_directory requires an existing directory");
    auto entries = w8_native::directory_entries(name.c_str());
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::string& entry)
                                 { return !matches(entry, pattern); }), entries.end());
    return entries;
}
void copy_file(std::string_view source, std::string_view destination, CopyMode mode)
{
    if (mode == CopyMode::fail_if_exists && file_status(destination))
        throw std::system_error(std::make_error_code(std::errc::file_exists), "copy game file");
    const auto target = game_path_text(destination);
    const auto physical = path_from_utf8(checked_path(w8_native::write_path(target.c_str(), false)));
    copy_physical(read_destination(source), physical, mode);
}
void replace_file(File& destination, std::string_view source)
{
    const auto path = destination.physical_path();
    const auto target = checked_path(w8_native::host_write_path(path_to_utf8(path)));
    destination.close();
    copy_physical(read_destination(source), path_from_utf8(target), CopyMode::replace);
}
} // namespace wiz8
