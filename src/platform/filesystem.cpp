#include "wiz8/filesystem.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#ifdef WIZ8_FILE_FAULTS
#include "file_operations.h"

#define WIZ8_FILE_OPERATION(op) testing::before_operation(testing::FileOperation::op)
#else
#define WIZ8_FILE_OPERATION(op) ((void)0)
#endif
#include <algorithm>
#include <array>
#include <limits>
#include <random>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <SDL3/SDL_error.h>

#include "wiz8/file_time.h"

#ifdef WIZ8_FILE_FAULTS
namespace wiz8::testing {
namespace {
thread_local FileOperationHook operation_hook = nullptr;
}
FileOperationHook set_file_operation_hook(FileOperationHook hook)
{
    return std::exchange(operation_hook, hook);
}
void before_operation(FileOperation operation)
{
    if (operation_hook) operation_hook(operation);
}
}
#endif
namespace fs = std::filesystem;
namespace wiz8 {
namespace {
[[noreturn]] void sdl_failure(const char* operation)
{
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
std::optional<FileStatus> physical_status(const fs::path& path)
{
    const fs::file_status status = fs::status(path);
    if (!fs::exists(status)) return std::nullopt;
    SDL_PathInfo info{};
    if (!SDL_GetPathInfo(path_to_utf8(path).c_str(), &info)) sdl_failure("query file metadata");
    const auto write_bits =
        fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    return FileStatus{info, (status.permissions() & write_bits) == fs::perms::none};
}
char fold(char value)
{
    return value >= 'A' && value <= 'Z' ? char(value + ('a' - 'A')) : value;
}
bool matches(std::string_view name, std::string_view pattern)
{
    if (pattern == "*.*") pattern = "*";
    std::size_t n = 0, p = 0, star = std::string_view::npos, retry = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || fold(pattern[p]) == fold(name[n]))) {
            ++n;
            ++p;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++retry;
        } else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}
struct StagingDirectory {
    fs::path path;
    explicit StagingDirectory(const fs::path& parent)
    {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            auto candidate = parent / (".wiz8-copy-" + std::to_string(random()));
            if (fs::create_directory(candidate)) {
                path = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot reserve copy staging directory");
    }
    StagingDirectory(const StagingDirectory&) = delete;
    StagingDirectory& operator=(const StagingDirectory&) = delete;
    ~StagingDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
void publish_staged_file(const fs::path& temporary, const fs::path& destination)
{
    WIZ8_FILE_OPERATION(publish);
    if (!SDL_RenamePath(path_to_utf8(temporary).c_str(), path_to_utf8(destination).c_str()))
        sdl_failure("replace file");
}
void copy_physical(const fs::path& source, const fs::path& destination, CopyMode mode)
{
    if (fs::exists(destination) && fs::equivalent(source, destination))
        throw std::invalid_argument("copy source and destination refer to the same file");
    // A staging file leaves the old save untouched on read/write failure.
    StagingDirectory staging(destination.parent_path());
    const fs::path temporary = staging.path / "content";
    auto input = open_file(source);
    auto output = open_file(temporary, OpenMode::replace);
    std::array<std::byte, 65536> buffer;
    for (;;) {
        const auto result = input->read(buffer.data(), buffer.size());
        output->write(buffer.data(), result.bytes);
        if (result.eof) break;
    }
    output->flush();
    output->close();
    input->close();
    if (mode == CopyMode::fail_if_exists)
        fs::copy_file(temporary, destination, fs::copy_options::none);
    else
        publish_staged_file(temporary, destination);
}
} // namespace

fs::path path_from_utf8(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}
std::string path_to_utf8(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
File::File(fs::path path, OpenMode mode) : path_(std::move(path)), writable_(mode != OpenMode::read)
{
    const char* access = "rb";
    switch (mode) {
    case OpenMode::read:
        break;
    case OpenMode::update:
        access = "r+b";
        break;
    case OpenMode::replace:
        access = "w+b";
        break;
    case OpenMode::append:
        access = "a+b";
        break;
    }
    stream_ = SDL_IOFromFile(path_to_utf8(path_).c_str(), access);
    if (!stream_) sdl_failure("open file");
    if (writable_) {
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(path_to_utf8(path_).c_str(), &info)) {
            const std::string error = SDL_GetError();
            SDL_CloseIO(std::exchange(stream_, nullptr));
            throw std::runtime_error("snapshot writer metadata: " + error);
        }
        writer_create_time_ = info.create_time;
    }
}
File::~File()
{
    if (stream_) SDL_CloseIO(stream_);
}
File::File(File&& other) noexcept
    : stream_(std::exchange(other.stream_, nullptr)), path_(std::move(other.path_)),
      writable_(other.writable_), writer_create_time_(other.writer_create_time_)
{
}
File& File::operator=(File&& other) noexcept
{
    if (this != &other) {
        if (stream_) SDL_CloseIO(stream_);
        stream_ = std::exchange(other.stream_, nullptr);
        path_ = std::move(other.path_);
        writable_ = other.writable_;
        writer_create_time_ = other.writer_create_time_;
    }
    return *this;
}
SDL_IOStream* File::require_stream() const
{
    if (!stream_) throw std::logic_error("file is closed");
    return stream_;
}
ReadResult File::read(void* data, std::size_t bytes)
{
    WIZ8_FILE_OPERATION(read);
    SDL_IOStream* stream = require_stream();
    if (!bytes) return {0, false};
    if (!data) throw std::invalid_argument("null read buffer");
    std::size_t total = 0;
    while (total < bytes) {
        const auto count = SDL_ReadIO(stream, static_cast<std::byte*>(data) + total, bytes - total);
        total += count;
        const auto status = SDL_GetIOStatus(stream);
        if (status == SDL_IO_STATUS_EOF) return {total, true};
        if (!count || status != SDL_IO_STATUS_READY) sdl_failure("read file");
    }
    return {total, false};
}
void File::read_exact(void* data, std::size_t bytes)
{
    if (read(data, bytes).bytes != bytes) throw std::runtime_error("short file read");
}

std::string File::read_text()
{
    const auto remaining = size() - tell();
    if (remaining < 0) throw std::runtime_error("file cursor is beyond its end");
    std::string text(static_cast<std::size_t>(remaining), '\0');
    read_exact(text.data(), text.size());
    return text;
}

void File::write(const void* data, std::size_t bytes)
{
    WIZ8_FILE_OPERATION(write);
    SDL_IOStream* stream = require_stream();
    if (!writable_) throw std::logic_error("file was opened for reading only");
    if (bytes && !data) throw std::invalid_argument("null write buffer");
    std::size_t total = 0;
    while (total < bytes) {
        const auto count =
            SDL_WriteIO(stream, static_cast<const std::byte*>(data) + total, bytes - total);
        total += count;
        if (!count || SDL_GetIOStatus(stream) != SDL_IO_STATUS_READY) sdl_failure("write file");
    }
}

void File::write_padded(std::string_view text, std::size_t width)
{
    if (!width) throw std::invalid_argument("empty fixed-width field");
    std::string field(width, '\0');
    text.copy(field.data(), std::min(text.size(), width - 1));
    write(field.data(), field.size());
}
std::int64_t File::seek(std::int64_t offset, SeekOrigin origin)
{
    WIZ8_FILE_OPERATION(seek);
    std::int64_t base = 0;
    switch (origin) {
    case SeekOrigin::begin:
        break;
    case SeekOrigin::current:
        base = tell();
        break;
    case SeekOrigin::end:
        base = size();
        break;
    default:
        throw std::invalid_argument("invalid seek origin");
    }
    if (offset < -base || offset > std::numeric_limits<std::int64_t>::max() - base)
        throw std::out_of_range("seek outside signed file positions");
    const auto destination = base + offset;
    const auto physical = destination;
    const auto position = SDL_SeekIO(require_stream(), physical, SDL_IO_SEEK_SET);
    if (position != physical) sdl_failure("seek file");
    return destination;
}
std::int64_t File::tell() const
{
    const auto position = SDL_TellIO(require_stream());
    if (position < 0) sdl_failure("tell file");
    return position;
}
std::int64_t File::size() const
{
    const auto bytes = SDL_GetIOSize(require_stream());
    if (bytes < 0) sdl_failure("size file");
    return bytes;
}
void File::flush()
{
    WIZ8_FILE_OPERATION(flush);
    if (!SDL_FlushIO(require_stream())) sdl_failure("flush file");
}
void File::close()
{
    WIZ8_FILE_OPERATION(close);
    if (stream_ && !SDL_CloseIO(std::exchange(stream_, nullptr))) sdl_failure("close file");
}
SDL_IOStream* File::io() const
{
    return require_stream();
}
SDL_IOStream* File::release_io() noexcept
{
    return std::exchange(stream_, nullptr);
}
bool File::is_open() const noexcept
{
    return stream_ != nullptr;
}
const fs::path& File::physical_path() const noexcept
{
    return path_;
}
FileStatus File::status() const
{
    const auto info = file_status(path_);
    if (!info) throw std::runtime_error("opened file path no longer exists");
    return *info;
}
FileTimes File::times() const
{
    require_stream();
    const auto info = status().info;
    const auto offset = current_utc_offset_seconds();
    const auto convert = [offset](SDL_Time time) {
        return file_time_with_legacy_local_bias(file_time_from_sdl(time), offset);
    };
    return {convert(writer_create_time_.value_or(info.create_time)), convert(info.access_time),
            convert(info.modify_time)};
}
std::optional<SDL_Time> File::opened_writer_create_time() const noexcept
{
    return writer_create_time_;
}
std::unique_ptr<File> open_file(const fs::path& path, OpenMode mode)
{
    return std::unique_ptr<File>(new File(path, mode));
}
std::optional<FileStatus> file_status(const fs::path& path)
{
    return physical_status(path);
}
bool remove_file(const fs::path& path)
{
    const auto info = file_status(path);
    if (!info) return false;
    if (info->info.type != SDL_PATHTYPE_FILE)
        throw std::invalid_argument("remove_file requires a regular file");
    if (!SDL_RemovePath(path_to_utf8(path).c_str())) sdl_failure("remove file");
    return true;
}
void create_directory(const fs::path& path)
{
    if (!SDL_CreateDirectory(path_to_utf8(path).c_str())) sdl_failure("create directory");
}
void clear_read_only(const fs::path& path)
{
    fs::permissions(path, fs::perms::owner_write, fs::perm_options::add);
}
std::vector<std::string> list_directory(const fs::path& directory, std::string_view pattern)
{
    std::vector<std::string> entries;
    for (const auto& entry : fs::directory_iterator(directory)) {
        auto name = path_to_utf8(entry.path().filename());
        if (matches(name, pattern)) entries.push_back(std::move(name));
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}
void copy_file(const fs::path& source, const fs::path& destination, CopyMode mode)
{
    copy_physical(source, destination, mode);
}
struct StagedFile::Impl {
    fs::path target;
    StagingDirectory staging;
    std::shared_ptr<File> stream;
    bool committed = false;
    explicit Impl(fs::path destination)
        : target(std::move(destination)), staging(target.parent_path()),
          stream(open_file(staging.path / "content", OpenMode::replace))
    {
    }
};
StagedFile::StagedFile(const fs::path& destination) : impl_(std::make_unique<Impl>(destination)) {}
StagedFile::~StagedFile()
{
    // Close borrowed references too, before removing the staging directory.
    try {
        if (impl_->stream->is_open()) impl_->stream->close();
    } catch (...) {
    }
}
std::shared_ptr<File> StagedFile::file() const
{
    return impl_->stream;
}
void StagedFile::commit()
{
    if (impl_->committed) throw std::logic_error("staged file already committed");
    if (impl_->stream->is_open()) {
        impl_->stream->flush();
        impl_->stream->close();
    }
    publish_staged_file(impl_->staging.path / "content", impl_->target);
    impl_->committed = true;
}
} // namespace wiz8
