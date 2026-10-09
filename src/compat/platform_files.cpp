#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compat/platform.h"
#include "platform_paths.h"

#undef rename

namespace
{
thread_local DWORD last_error = ERROR_SUCCESS;
std::mutex files_lock;
uintptr_t next_handle = 1;
struct File
{
    int fd;
    DWORD access, share;
    std::string path;
    dev_t device;
    ino_t inode;
    bool delete_on_close;
};
struct Mapping
{
    int fd;
    uint64_t size;
};
struct Search
{
    std::vector<WIN32_FIND_DATAA> entries;
    size_t next;
};
std::unordered_map<HANDLE, File> files;
std::unordered_map<HANDLE, Mapping> mappings;
std::unordered_map<HANDLE, Search> searches;
std::unordered_map<const void*, size_t> views;
HANDLE handle() { return reinterpret_cast<HANDLE>(next_handle++); }
DWORD error_code(int value)
{
    switch (value)
    {
    case 0:
        return ERROR_SUCCESS;
    case ENOENT:
        return ERROR_FILE_NOT_FOUND;
    case ENOTDIR:
        return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS:
    case EISDIR:
        return ERROR_ACCESS_DENIED;
    case EBADF:
        return ERROR_INVALID_HANDLE;
    case EEXIST:
        return ERROR_FILE_EXISTS;
    case EXDEV:
        return ERROR_NOT_SAME_DEVICE;
    case ENOMEM:
        return ERROR_NOT_ENOUGH_MEMORY;
    case ENOSPC:
    case EDQUOT:
        return ERROR_DISK_FULL;
    case ENAMETOOLONG:
        return ERROR_FILENAME_EXCED_RANGE;
    case ENOTSUP:
        return ERROR_NOT_SUPPORTED;
    default:
        return ERROR_INVALID_PARAMETER;
    }
}
BOOL failure(DWORD error)
{
    last_error = error;
    return 0;
}
BOOL posix_failure() { return failure(error_code(errno)); }
BOOL path_failure(const std::string& path)
{
    const int saved = errno;
    if (saved == ENOENT)
    {
        size_t slash = path.find_last_of('/');
        if (slash != std::string::npos)
        {
            struct stat parent;
            if (stat(path.substr(0, slash).c_str(), &parent) != 0 || !S_ISDIR(parent.st_mode))
                return failure(ERROR_PATH_NOT_FOUND);
        }
    }
    return failure(error_code(saved));
}
uint64_t ticks(const FILETIME& time)
{
    return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
FILETIME file_time(time_t seconds, long nanoseconds)
{
    uint64_t value = (uint64_t(seconds) + 11644473600ULL) * 10000000 + nanoseconds / 100;
    return {DWORD(value), DWORD(value >> 32)};
}
void times(const struct stat& status, FILETIME* creation, FILETIME* access, FILETIME* write)
{
#ifdef __APPLE__
    if (creation)
        *creation = file_time(status.st_birthtimespec.tv_sec, status.st_birthtimespec.tv_nsec);
    if (access)
        *access = file_time(status.st_atimespec.tv_sec, status.st_atimespec.tv_nsec);
    if (write)
        *write = file_time(status.st_mtimespec.tv_sec, status.st_mtimespec.tv_nsec);
#else
    /* POSIX stat has no creation time. ctime is the metadata-change timestamp. */
    if (creation)
        *creation = file_time(status.st_ctim.tv_sec, status.st_ctim.tv_nsec);
    if (access)
        *access = file_time(status.st_atim.tv_sec, status.st_atim.tv_nsec);
    if (write)
        *write = file_time(status.st_mtim.tv_sec, status.st_mtim.tv_nsec);
#endif
}
DWORD attributes(const struct stat& status)
{
    DWORD result = S_ISDIR(status.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
    if ((status.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0)
        result |= FILE_ATTRIBUTE_READONLY;
    return result;
}
bool sharing(const struct stat& status, DWORD access, DWORD share, bool deleting = false)
{
    for (const auto& item : files)
    {
        const File& file = item.second;
        if (status.st_dev != file.device || status.st_ino != file.inode)
            continue;
        if (deleting && !(file.share & FILE_SHARE_DELETE))
            return false;
        if ((access & GENERIC_READ) && !(file.share & FILE_SHARE_READ))
            return false;
        if ((access & GENERIC_WRITE) && !(file.share & FILE_SHARE_WRITE))
            return false;
        if ((file.access & GENERIC_READ) && !(share & FILE_SHARE_READ))
            return false;
        if ((file.access & GENERIC_WRITE) && !(share & FILE_SHARE_WRITE))
            return false;
        if (file.delete_on_close && !(share & FILE_SHARE_DELETE))
            return false;
    }
    return true;
}
bool mutable_file(const char* input, std::string& path, struct stat& status)
{
    path = w8_native::read_path(input);
    if (path.empty() || stat(path.c_str(), &status) != 0)
    {
        path_failure(path);
        return false;
    }
    if (w8_native::is_read_only_path(input) || (attributes(status) & FILE_ATTRIBUTE_READONLY))
    {
        failure(ERROR_ACCESS_DENIED);
        return false;
    }
    if (!sharing(status, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, true))
    {
        failure(ERROR_SHARING_VIOLATION);
        return false;
    }
    return true;
}
std::string fold(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return text;
}
bool wildcard(const std::string& input_pattern, const std::string& input_name)
{
    std::string pattern = fold(input_pattern), name = fold(input_name);
    if (pattern == "*.*")
        pattern = "*";
    size_t p = 0, n = 0, star = std::string::npos, retry = 0;
    while (n < name.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n]))
        {
            ++p;
            ++n;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            retry = n;
        }
        else if (star != std::string::npos)
        {
            p = star + 1;
            n = ++retry;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    if (pattern.substr(p) == ".*")
        p = pattern.size();
    return p == pattern.size();
}
} // namespace
void w8_set_error(DWORD error) { last_error = error; }
DWORD W8GetLastError() { return last_error; }
HANDLE W8CreateFile(LPCSTR input, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                    DWORD disposition, DWORD flags, HANDLE templ)
{
    std::lock_guard<std::mutex> guard(files_lock);
    if (security || templ || (access & ~(GENERIC_READ | GENERIC_WRITE)) ||
        (share & ~(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)) ||
        (flags & ~(FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY | FILE_FLAG_DELETE_ON_CLOSE |
                   FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_SEQUENTIAL_SCAN)))
    {
        failure(ERROR_NOT_SUPPORTED);
        return INVALID_HANDLE_VALUE;
    }
    if (disposition < CREATE_NEW || disposition > TRUNCATE_EXISTING ||
        (disposition == TRUNCATE_EXISTING && !(access & GENERIC_WRITE)))
    {
        failure(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    std::string path = w8_native::read_path(input);
    if (path.empty())
    {
        posix_failure();
        return INVALID_HANDLE_VALUE;
    }
    struct stat status;
    bool exists = stat(path.c_str(), &status) == 0;
    bool writing = (access & GENERIC_WRITE) || disposition == CREATE_NEW ||
                   disposition == CREATE_ALWAYS || (disposition == OPEN_ALWAYS && !exists) ||
                   (flags & FILE_FLAG_DELETE_ON_CLOSE);
    if (exists && disposition == CREATE_NEW)
    {
        failure(ERROR_FILE_EXISTS);
        return INVALID_HANDLE_VALUE;
    }
    if (!exists && (disposition == OPEN_EXISTING || disposition == TRUNCATE_EXISTING))
    {
        path_failure(path);
        return INVALID_HANDLE_VALUE;
    }
    if (exists && !S_ISREG(status.st_mode))
    {
        failure(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    if (exists && writing && (attributes(status) & FILE_ATTRIBUTE_READONLY))
    {
        failure(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    if (exists && !sharing(status, access, share, (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0))
    {
        failure(ERROR_SHARING_VIOLATION);
        return INVALID_HANDLE_VALUE;
    }
    if (writing)
    {
        path =
            w8_native::write_path(input, disposition != CREATE_ALWAYS && disposition != CREATE_NEW);
        if (path.empty())
        {
            posix_failure();
            return INVALID_HANDLE_VALUE;
        }
    }
    int mode = (access & GENERIC_WRITE) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (disposition == CREATE_NEW)
        mode |= O_CREAT | O_EXCL;
    if (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)
        mode |= O_CREAT;
    int fd = open(path.c_str(), mode | O_CLOEXEC, 0666);
    if (fd < 0)
    {
        posix_failure();
        return INVALID_HANDLE_VALUE;
    }
    if (fstat(fd, &status) != 0)
    {
        int saved = errno;
        close(fd);
        errno = saved;
        posix_failure();
        return INVALID_HANDLE_VALUE;
    }
    if (!sharing(status, access, share, (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0))
    {
        close(fd);
        failure(ERROR_SHARING_VIOLATION);
        return INVALID_HANDLE_VALUE;
    }
    if ((disposition == CREATE_ALWAYS || disposition == TRUNCATE_EXISTING) && ftruncate(fd, 0) != 0)
    {
        int saved = errno;
        close(fd);
        errno = saved;
        posix_failure();
        return INVALID_HANDLE_VALUE;
    }
    if (!exists && (flags & FILE_ATTRIBUTE_READONLY))
        fchmod(fd, status.st_mode & ~(S_IWUSR | S_IWGRP | S_IWOTH));
    HANDLE result = handle();
    files.emplace(result, File{fd, access, share, path, status.st_dev, status.st_ino,
                               (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0});
    last_error = exists && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)
                     ? ERROR_ALREADY_EXISTS
                     : ERROR_SUCCESS;
    return result;
}
BOOL W8ReadFile(HANDLE id, LPVOID buffer, DWORD size, LPDWORD count, LPOVERLAPPED overlapped)
{
    if (count)
        *count = 0;
    std::lock_guard<std::mutex> guard(files_lock);
    auto it = files.find(id);
    if (it == files.end())
        return failure(ERROR_INVALID_HANDLE);
    if (overlapped)
        return failure(ERROR_NOT_SUPPORTED);
    if ((!buffer && size) || !count)
        return failure(ERROR_INVALID_PARAMETER);
    if (!(it->second.access & GENERIC_READ))
        return failure(ERROR_ACCESS_DENIED);
    ssize_t result;
    do
    {
        result = read(it->second.fd, buffer, size);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        return posix_failure();
    *count = DWORD(result);
    return 1;
}
BOOL W8WriteFile(HANDLE id, LPCVOID buffer, DWORD size, LPDWORD count, LPOVERLAPPED overlapped)
{
    if (count)
        *count = 0;
    std::lock_guard<std::mutex> guard(files_lock);
    auto it = files.find(id);
    if (it == files.end())
        return failure(ERROR_INVALID_HANDLE);
    if (overlapped)
        return failure(ERROR_NOT_SUPPORTED);
    if ((!buffer && size) || !count)
        return failure(ERROR_INVALID_PARAMETER);
    if (!(it->second.access & GENERIC_WRITE))
        return failure(ERROR_ACCESS_DENIED);
    while (*count < size)
    {
        ssize_t result =
            write(it->second.fd, static_cast<const char*>(buffer) + *count, size - *count);
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0)
            return posix_failure();
        if (result == 0)
            return failure(ERROR_DISK_FULL);
        *count += DWORD(result);
    }
    return 1;
}
BOOL W8CloseHandle(HANDLE id)
{
    std::lock_guard<std::mutex> guard(files_lock);
    auto file = files.find(id);
    if (file != files.end())
    {
        File value = file->second;
        files.erase(file);
        bool still_open = false, deleting = value.delete_on_close;
        for (auto& item : files)
        {
            if (item.second.device == value.device && item.second.inode == value.inode)
            {
                still_open = true;
                item.second.delete_on_close |= deleting;
            }
        }
        int result = close(value.fd);
        if (deleting && !still_open && unlink(value.path.c_str()) != 0)
            return posix_failure();
        return result == 0 ? 1 : posix_failure();
    }
    auto mapping = mappings.find(id);
    if (mapping != mappings.end())
    {
        int fd = mapping->second.fd;
        mappings.erase(mapping);
        return close(fd) == 0 ? 1 : posix_failure();
    }
    return failure(ERROR_INVALID_HANDLE);
}
DWORD W8SetFilePointer(HANDLE id, LONG low, PLONG high, DWORD method)
{
    std::lock_guard<std::mutex> guard(files_lock);
    auto it = files.find(id);
    if (it == files.end())
    {
        failure(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    if (method > FILE_END)
    {
        failure(ERROR_INVALID_PARAMETER);
        return INVALID_SET_FILE_POINTER;
    }
    int64_t distance =
        high ? int64_t((uint64_t(uint32_t(*high)) << 32) | uint32_t(low)) : int64_t(low);
    off_t origin = method == FILE_BEGIN     ? 0
                   : method == FILE_CURRENT ? lseek(it->second.fd, 0, SEEK_CUR)
                                            : 0;
    struct stat status;
    if (method == FILE_END)
    {
        if (fstat(it->second.fd, &status) != 0)
        {
            posix_failure();
            return INVALID_SET_FILE_POINTER;
        }
        origin = status.st_size;
    }
    if (origin < 0 || (distance > 0 && origin > INT64_MAX - distance))
    {
        failure(ERROR_INVALID_PARAMETER);
        return INVALID_SET_FILE_POINTER;
    }
    int64_t position = origin + distance;
    if (position < 0)
    {
        failure(ERROR_NEGATIVE_SEEK);
        return INVALID_SET_FILE_POINTER;
    }
    if (!high && uint64_t(position) > UINT32_MAX)
    {
        failure(ERROR_INVALID_PARAMETER);
        return INVALID_SET_FILE_POINTER;
    }
    off_t result = lseek(it->second.fd, position, SEEK_SET);
    if (result < 0)
    {
        posix_failure();
        return INVALID_SET_FILE_POINTER;
    }
    if (high)
        *high = LONG(uint64_t(result) >> 32);
    last_error = ERROR_SUCCESS;
    return DWORD(result);
}
DWORD W8GetFileSize(HANDLE id, LPDWORD high)
{
    std::lock_guard<std::mutex> guard(files_lock);
    auto it = files.find(id);
    if (it == files.end())
    {
        failure(ERROR_INVALID_HANDLE);
        return INVALID_FILE_SIZE;
    }
    struct stat status;
    if (fstat(it->second.fd, &status) != 0)
    {
        posix_failure();
        return INVALID_FILE_SIZE;
    }
    if (high)
        *high = DWORD(uint64_t(status.st_size) >> 32);
    last_error = ERROR_SUCCESS;
    return DWORD(status.st_size);
}
BOOL W8GetFileTime(HANDLE id, LPFILETIME creation, LPFILETIME access, LPFILETIME write)
{
    std::lock_guard<std::mutex> guard(files_lock);
    auto it = files.find(id);
    if (it == files.end())
        return failure(ERROR_INVALID_HANDLE);
    struct stat status;
    if (fstat(it->second.fd, &status) != 0)
        return posix_failure();
    times(status, creation, access, write);
    return 1;
}
BOOL W8DeleteFile(LPCSTR input)
{
    std::lock_guard<std::mutex> guard(files_lock);
    std::string path;
    struct stat status;
    if (!mutable_file(input, path, status))
        return 0;
    if (!S_ISREG(status.st_mode))
        return failure(ERROR_ACCESS_DENIED);
    return unlink(path.c_str()) == 0 ? 1 : posix_failure();
}
HANDLE W8CreateFileMapping(HANDLE id, LPSECURITY_ATTRIBUTES security, DWORD protect, DWORD high,
                           DWORD low, LPCSTR name)
{
    std::lock_guard<std::mutex> guard(files_lock);
    if (security || name || protect != PAGE_READONLY)
    {
        failure(ERROR_NOT_SUPPORTED);
        return nullptr;
    }
    auto it = files.find(id);
    if (it == files.end())
    {
        failure(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (!(it->second.access & GENERIC_READ))
    {
        failure(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    struct stat status;
    if (fstat(it->second.fd, &status) != 0)
    {
        posix_failure();
        return nullptr;
    }
    uint64_t size = (uint64_t(high) << 32) | low;
    if (size == 0)
        size = status.st_size;
    if (size == 0 || size > uint64_t(status.st_size))
    {
        failure(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    int fd = fcntl(it->second.fd, F_DUPFD_CLOEXEC, 0);
    if (fd < 0)
    {
        posix_failure();
        return nullptr;
    }
    HANDLE result = handle();
    mappings.emplace(result, Mapping{fd, size});
    return result;
}
LPVOID W8MapViewOfFile(HANDLE id, DWORD access, DWORD high, DWORD low, SIZE_T size)
{
    std::lock_guard<std::mutex> guard(files_lock);
    auto it = mappings.find(id);
    if (it == mappings.end())
    {
        failure(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (access != FILE_MAP_READ)
    {
        failure(ERROR_NOT_SUPPORTED);
        return nullptr;
    }
    uint64_t offset = (uint64_t(high) << 32) | low;
    if ((offset & 0xffff) || offset >= it->second.size || size > it->second.size - offset)
    {
        failure(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!size)
        size = it->second.size - offset;
    void* view = mmap(nullptr, size, PROT_READ, MAP_SHARED, it->second.fd, off_t(offset));
    if (view == MAP_FAILED)
    {
        posix_failure();
        return nullptr;
    }
    views.emplace(view, size);
    return view;
}
BOOL W8UnmapViewOfFile(LPCVOID view)
{
    std::lock_guard<std::mutex> guard(files_lock);
    auto it = views.find(view);
    if (it == views.end())
        return failure(ERROR_INVALID_PARAMETER);
    int result = munmap(const_cast<void*>(view), it->second);
    if (result == 0)
        views.erase(it);
    return result == 0 ? 1 : posix_failure();
}
DWORD W8GetFileAttributes(LPCSTR input)
{
    std::string path = w8_native::read_path(input);
    struct stat status;
    if (path.empty() || stat(path.c_str(), &status) != 0)
    {
        path_failure(path);
        return INVALID_FILE_ATTRIBUTES;
    }
    return attributes(status);
}
BOOL W8SetFileAttributes(LPCSTR input, DWORD value)
{
    if (value & ~(FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE))
        return failure(ERROR_NOT_SUPPORTED);
    std::lock_guard<std::mutex> guard(files_lock);
    std::string source = w8_native::read_path(input);
    struct stat status;
    if (source.empty() || stat(source.c_str(), &status) != 0)
        return posix_failure();
    std::string path = w8_native::write_path(input, true);
    if (path.empty())
        return posix_failure();
    mode_t permissions = status.st_mode & 07777;
    if (value & FILE_ATTRIBUTE_READONLY)
        permissions &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    else
        permissions |= S_IWUSR;
    return chmod(path.c_str(), permissions) == 0 ? 1 : posix_failure();
}
BOOL W8CreateDirectory(LPCSTR input, LPSECURITY_ATTRIBUTES security)
{
    if (security)
        return failure(ERROR_NOT_SUPPORTED);
    std::string existing = w8_native::read_path(input);
    struct stat status;
    if (!existing.empty() && stat(existing.c_str(), &status) == 0)
        return failure(ERROR_ALREADY_EXISTS);
    std::string path = w8_native::write_path(input, false);
    if (path.empty())
        return posix_failure();
    return mkdir(path.c_str(), 0777) == 0 ? 1 : posix_failure();
}
BOOL W8CopyFile(LPCSTR source, LPCSTR destination, BOOL fail_if_exists)
{
    HANDLE input = W8CreateFile(source, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE)
        return 0;
    HANDLE output =
        W8CreateFile(destination, GENERIC_WRITE, 0, nullptr,
                     fail_if_exists ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
    {
        DWORD saved = last_error;
        W8CloseHandle(input);
        last_error = saved;
        return 0;
    }
    char buffer[65536];
    DWORD count = 0, written = 0;
    BOOL result;
    while ((result = W8ReadFile(input, buffer, sizeof(buffer), &count, nullptr)) && count)
    {
        result = W8WriteFile(output, buffer, count, &written, nullptr);
        if (!result)
            break;
    }
    DWORD saved = last_error;
    /* CopyFile preserves last-write/access times and the read-only attribute. */
    if (result)
    {
        std::lock_guard<std::mutex> guard(files_lock);
        struct stat status;
        if (fstat(files.at(input).fd, &status) != 0)
        {
            result = posix_failure();
            saved = last_error;
        }
        else
        {
#ifdef __APPLE__
            timespec timestamps[] = {status.st_atimespec, status.st_mtimespec};
#else
            timespec timestamps[] = {status.st_atim, status.st_mtim};
#endif
            if (futimens(files.at(output).fd, timestamps) != 0 ||
                fchmod(files.at(output).fd, status.st_mode & 0777) != 0)
            {
                result = posix_failure();
                saved = last_error;
            }
        }
    }
    if (!W8CloseHandle(output) && result)
    {
        result = 0;
        saved = last_error;
    }
    W8CloseHandle(input);
    last_error = saved;
    return result;
}
BOOL W8MoveFile(LPCSTR source, LPCSTR destination)
{
    std::lock_guard<std::mutex> guard(files_lock);
    std::string from;
    struct stat status;
    if (!mutable_file(source, from, status))
        return 0;
    std::string to = w8_native::read_path(destination);
    struct stat other;
    if (!to.empty() && stat(to.c_str(), &other) == 0)
        return failure(ERROR_ALREADY_EXISTS);
    to = w8_native::write_path(destination, false);
    if (to.empty())
        return posix_failure();
    return ::rename(from.c_str(), to.c_str()) == 0 ? 1 : posix_failure();
}
HANDLE W8FindFirstFile(LPCSTR input, LPWIN32_FIND_DATAA data)
{
    if (!input || !data)
    {
        failure(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    std::string full = w8_native::full_path(input);
    size_t slash = full.find_last_of("/\\");
    std::string base = slash == std::string::npos ? "." : full.substr(0, slash + 1);
    std::string pattern = slash == std::string::npos ? full : full.substr(slash + 1);
    Search search{{}, 1};
    for (const std::string& name : w8_native::directory_entries(base.c_str()))
    {
        if (!wildcard(pattern, name))
            continue;
        struct stat status;
        std::string path = w8_native::read_path((base + name).c_str());
        if (stat(path.c_str(), &status) != 0)
            continue;
        WIN32_FIND_DATAA entry{};
        entry.dwFileAttributes = attributes(status);
        times(status, &entry.ftCreationTime, &entry.ftLastAccessTime, &entry.ftLastWriteTime);
        entry.nFileSizeHigh = DWORD(uint64_t(status.st_size) >> 32);
        entry.nFileSizeLow = DWORD(status.st_size);
        if (name.size() >= sizeof(entry.cFileName))
            continue;
        memcpy(entry.cFileName, name.c_str(), name.size() + 1);
        search.entries.push_back(entry);
    }
    if (search.entries.empty())
    {
        failure(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    std::lock_guard<std::mutex> guard(files_lock);
    HANDLE result = handle();
    *data = search.entries[0];
    searches.emplace(result, std::move(search));
    return result;
}
BOOL W8FindNextFile(HANDLE id, LPWIN32_FIND_DATAA data)
{
    std::lock_guard<std::mutex> guard(files_lock);
    auto it = searches.find(id);
    if (it == searches.end())
        return failure(ERROR_INVALID_HANDLE);
    if (!data)
        return failure(ERROR_INVALID_PARAMETER);
    if (it->second.next == it->second.entries.size())
        return failure(ERROR_NO_MORE_FILES);
    *data = it->second.entries[it->second.next++];
    return 1;
}
BOOL W8FindClose(HANDLE id)
{
    std::lock_guard<std::mutex> guard(files_lock);
    return searches.erase(id) ? 1 : failure(ERROR_INVALID_HANDLE);
}
LONG W8CompareFileTime(const FILETIME* first, const FILETIME* second)
{
    if (!first || !second)
    {
        failure(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return ticks(*first) < ticks(*second) ? -1 : ticks(*first) > ticks(*second) ? 1 : 0;
}
