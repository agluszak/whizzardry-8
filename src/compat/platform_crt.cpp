#include <algorithm>
#include "platform_paths.h"
#include "compat/platform.h"

#include <cerrno>
#include <unistd.h>

#undef fopen
#undef rename

extern "C"
{
    FILE* w8_fopen(const char* input, const char* mode)
    {
        if (!input || !mode || (*mode != 'r' && *mode != 'w' && *mode != 'a'))
        {
            errno = EINVAL;
            return nullptr;
        }
        bool writing = *mode != 'r' || strchr(mode, '+') != nullptr;
        std::string path = w8_native::read_path(input);
        struct stat status;
        bool exists = !path.empty() && stat(path.c_str(), &status) == 0;
        if (*mode == 'r' && !exists)
        {
            errno = ENOENT;
            return nullptr;
        }
        if (writing && exists && !(status.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
        {
            errno = EACCES;
            return nullptr;
        }
        if (writing)
            path = w8_native::write_path(input, *mode != 'w');
        if (path.empty())
            return nullptr;
        /* Microsoft's 't' is the default text flag. POSIX has no CRLF translation;
           existing binary callers always specify 'b'. Logs use native newlines. */
        std::string native_mode(mode);
        native_mode.erase(std::remove(native_mode.begin(), native_mode.end(), 't'),
                          native_mode.end());
        return ::fopen(path.c_str(), native_mode.c_str());
    }
    int w8_access(const char* input, int mode)
    {
        if (mode & ~6)
        {
            errno = EINVAL;
            return -1;
        }
        const std::string path = w8_native::read_path(input);
        if (path.empty())
            return -1;
        int native = F_OK;
        if (mode & 4)
            native |= R_OK;
        if (mode & 2)
        {
            struct stat status;
            if (stat(path.c_str(), &status) != 0)
                return -1;
            if (!(status.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))
            {
                errno = EACCES;
                return -1;
            }
            /* Assets can be copied up; configured discs remain read-only. */
            const std::string full = w8_native::full_path(input);
            if (full.size() >= 2 && full[1] == ':' && full[0] != 'C')
            {
                errno = EACCES;
                return -1;
            }
        }
        return access(path.c_str(), native);
    }
    int w8_chmod(const char* path, int mode)
    {
        if (W8SetFileAttributes(path,
                                mode & S_IWUSR ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_READONLY))
            return 0;
        errno = W8GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT : EACCES;
        return -1;
    }
    int w8_chdir(const char* path) { return w8_native::change_directory(path); }
    char* w8_getcwd(char* buffer, int size)
    {
        const std::string path = w8_native::current_directory();
        if (size < 0 || (size != 0 && size_t(size) < path.size() + 1))
        {
            errno = ERANGE;
            return nullptr;
        }
        if (!buffer)
        {
            if (!size)
                size = int(path.size() + 1);
            buffer = static_cast<char*>(malloc(size));
            if (!buffer)
            {
                errno = ENOMEM;
                return nullptr;
            }
        }
        else if (!size)
        {
            errno = EINVAL;
            return nullptr;
        }
        memcpy(buffer, path.c_str(), path.size() + 1);
        return buffer;
    }
    int w8_rename(const char* source, const char* destination)
    {
        if (W8MoveFile(source, destination))
            return 0;
        const DWORD error = W8GetLastError();
        errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
        return -1;
    }
    int w8_remove(const char* path)
    {
        if (W8DeleteFile(path))
            return 0;
        const DWORD error = W8GetLastError();
        errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
        return -1;
    }
}
