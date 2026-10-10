#pragma once

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include "wiz8/file_time.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wiz8
{
// All game path strings and strings passed to SDL are UTF-8.
std::filesystem::path path_from_utf8(std::string_view text);
std::string path_to_utf8(const std::filesystem::path& path);

enum class OpenMode { read, update, replace, append };
enum class SeekOrigin { begin, current, end };
enum class CopyMode { fail_if_exists, replace };

struct ReadResult
{
    std::size_t bytes;
    bool eof;
};

struct FileStatus
{
    SDL_PathInfo info{};
    // Mutable-layer policy for the resolved path, not host permission guarantees.
    bool writable = false;
    // Host permission bits, separate from immutable/overlay access policy.
    bool read_only = false;
    bool archived = false;
};

struct FileTimes
{
    DiskFileTime created, accessed, modified;
};

// Owns one SDL stream. No shared cursor or global handle registry. Operations
// throw on failure; read returns a short result only for EOF. Not thread-safe.
class File
{
public:
    ~File();
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    ReadResult read(void* data, std::size_t bytes);
    void read_exact(void* data, std::size_t bytes);
    void write(const void* data, std::size_t bytes);
    std::int64_t seek(std::int64_t offset, SeekOrigin origin);
    std::int64_t tell() const;
    std::int64_t size() const;
    void flush();
    void close();
    bool is_open() const noexcept;
    const std::filesystem::path& physical_path() const noexcept;
    FileStatus status() const;
    FileTimes times() const;

    // SDL 3.4.12 create_time means POSIX ctime on Unix/macOS, birth on Windows.
    // A writer snapshots it immediately after open (before writes change ctime).
    // This is NOT portable birth time, and is never replaced with mtime.
    // The save metadata unit must still resolve iron-man validation after
    // close/reopen: this snapshot does not persist birth metadata across sessions.
    std::optional<SDL_Time> opened_writer_create_time() const noexcept;

private:
    File(std::filesystem::path path, OpenMode mode);
    SDL_IOStream* require_stream() const;
    SDL_IOStream* stream_ = nullptr;
    std::filesystem::path path_;
    bool writable_ = false;
    std::optional<SDL_Time> writer_create_time_;
    struct Extent
    {
        std::int64_t offset, length;
        DiskFileTime modified;
    };
    std::optional<Extent> extent_;
    friend std::unique_ptr<File> open_file(std::string_view, OpenMode);
    friend std::unique_ptr<File> open_host_file(const std::filesystem::path&, OpenMode);
};

// C: through F: (either slash spelling) are ALWAYS virtual asset drives.
// Native absolute paths are rejected here: use open_host_file instead.
// Colons are permitted only in the virtual drive prefix, not path components.
// Relative game paths use the game's current directory. Reads prefer overlays;
// update/append copy up assets, replace truncates only the writable overlay.
// update requires an existing file; replace/append may create a file in an
// existing game directory. Use create_directory for new game directories.
std::unique_ptr<File> open_file(std::string_view game_path, OpenMode mode = OpenMode::read);
// Native imports are explicitly separate, even for C:/... on Windows.
// Host writes to immutable roots or symlink aliases of them are rejected.
std::unique_ptr<File> open_host_file(const std::filesystem::path& path,
                                     OpenMode mode = OpenMode::read);

// Validates the complete directory before publishing entries. Mounted archives
// contain no live streams; each open owns its own bounded cursor. Loose files
// win over archives; patches win over base archives, later patches win ties.
void mount_slf(std::string_view game_path, bool patch = false);
void clear_asset_archives();
void refresh_asset_archives();

// Missing paths return nullopt; metadata/permission/I/O failures throw.
std::optional<FileStatus> file_status(std::string_view game_path);
std::optional<FileStatus> host_file_status(const std::filesystem::path& path);
// Deletion removes only an overlay file; an underlying asset can become visible
// again. No tombstones or recursive deletion of installed assets are performed.
bool remove_file(std::string_view game_path);
void create_directory(std::string_view game_path);
// Materializes an overlay when needed; installed files remain untouched.
void clear_read_only(std::string_view game_path);
// Basenames, merged overlay-first, ASCII case-insensitive * / ? matching.
// *.* also matches extensionless game files. Missing directories throw.
std::vector<std::string> list_directory(std::string_view game_directory,
                                      std::string_view pattern = "*");

// Streams opened for a copy are closed before destination replacement. Callers
// must close independently owned destination streams themselves (also Windows).
void copy_file(std::string_view source, std::string_view destination,
               CopyMode mode = CopyMode::fail_if_exists);
// Closes the supplied destination BEFORE replacement and leaves it closed.
// Other independently owned streams of that path must also be closed by caller.
void replace_file(File& destination, std::string_view source);
} // namespace wiz8
