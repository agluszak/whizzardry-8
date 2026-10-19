#pragma once

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

#include "wiz8/file_time.h"

namespace wiz8 {
// All game path strings and strings passed to SDL are UTF-8.
std::filesystem::path path_from_utf8(std::string_view text);
std::string path_to_utf8(const std::filesystem::path& path);

enum class OpenMode { read, update, replace, append };
enum class SeekOrigin { begin, current, end };

struct ReadResult {
    std::size_t bytes;
    bool eof;
};

struct FileStatus {
    SDL_PathInfo info{};
    bool read_only = false;
};

struct FileTimes {
    DiskFileTime created, accessed, modified;
};

// Owns one SDL stream. No shared cursor or global handle registry. Operations
// throw on failure; read returns a short result only for EOF. Not thread-safe.
class File {
public:
    ~File();
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    // Borrowed SDL stream; shares this File's cursor and lifetime. Do not close it.
    SDL_IOStream* io() const;
    // Transfers the SDL stream and its close responsibility to the caller.
    // The File becomes closed.
    SDL_IOStream* release_io() noexcept;

    ReadResult read(void* data, std::size_t bytes);
    void read_exact(void* data, std::size_t bytes);
    // Read the remaining bytes as owned text, without encoding or newline conversion.
    std::string read_text();
    void write(const void* data, std::size_t bytes);
    // Write `text` as a fixed field of `width` bytes: truncated to leave room
    // for the terminator, then NUL-padded, so no stale bytes reach the file.
    void write_padded(std::string_view text, std::size_t width);
    std::int64_t seek(std::int64_t offset, SeekOrigin origin);
    std::int64_t tell() const;
    std::int64_t size() const;
    void flush();
    void close();
    bool is_open() const noexcept;
    FileStatus status() const;
    FileTimes times() const;
    const std::filesystem::path& physical_path() const noexcept;

private:
    File(std::filesystem::path path, OpenMode mode);
    SDL_IOStream* require_stream() const;
    SDL_IOStream* stream_ = nullptr;
    std::filesystem::path path_;
    bool writable_ = false;
    std::optional<SDL_Time> writer_create_time_;
    friend std::unique_ptr<File> open_file(const std::filesystem::path&, OpenMode);
};

// Ordinary native paths. Callers select asset_path() or user_path() explicitly.
std::unique_ptr<File> open_file(const std::filesystem::path& path, OpenMode mode = OpenMode::read);
std::optional<FileStatus> file_status(const std::filesystem::path& path);
bool remove_file(const std::filesystem::path& path);
void create_directory(const std::filesystem::path& path);
void clear_read_only(const std::filesystem::path& path);
// Basenames; SDL case-insensitive * / ? matching. Missing directories throw.
std::vector<std::string> list_directory(const std::filesystem::path& directory,
                                        std::string_view pattern = "*");

// Stage a writable copy before replacing the destination. Callers must close
// independently owned destination streams themselves (also Windows).
void copy_file(const std::filesystem::path& source, const std::filesystem::path& destination);
// Owns a same-filesystem staging file. commit flushes and closes every shared
// stream reference before publication; abandonment removes the temporary file.
class StagedFile {
public:
    explicit StagedFile(const std::filesystem::path& destination);
    ~StagedFile();
    StagedFile(const StagedFile&) = delete;
    StagedFile& operator=(const StagedFile&) = delete;
    std::shared_ptr<File> file() const;
    void commit();

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace wiz8
