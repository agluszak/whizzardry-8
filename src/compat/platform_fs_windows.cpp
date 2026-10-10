#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <string>

#include "platform_fs_windows.h"

namespace w8_native
{
namespace
{
void file_error(DWORD error)
{
    switch (error)
    {
    case ERROR_ACCESS_DENIED:
        errno = EACCES;
        break;
    case ERROR_FILE_NOT_FOUND:
        errno = ENOENT;
        break;
    case ERROR_PATH_NOT_FOUND:
        errno = ENOTDIR;
        break;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        errno = EEXIST;
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        errno = ENOMEM;
        break;
    case ERROR_DISK_FULL:
        errno = ENOSPC;
        break;
    default:
        errno = EINVAL;
    }
}
}

int open_shared_file(const char* path, int flags, int permissions)
{
    (void)permissions;
    UINT code_page = CP_UTF8;
    int length = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (!length)
    {
        code_page = CP_ACP;
        length = MultiByteToWideChar(code_page, 0, path, -1, nullptr, 0);
    }
    if (!length)
    {
        file_error(GetLastError());
        return -1;
    }
    std::wstring wide(size_t(length), L'\0');
    MultiByteToWideChar(code_page, 0, path, -1, wide.data(), length);
    const DWORD access = (flags & _O_RDWR) ? GENERIC_READ | GENERIC_WRITE
                         : (flags & _O_WRONLY) ? GENERIC_WRITE : GENERIC_READ;
    const DWORD disposition = !(flags & _O_CREAT) ? OPEN_EXISTING
                              : (flags & _O_EXCL) ? CREATE_NEW : OPEN_ALWAYS;
    HANDLE file = CreateFileW(wide.c_str(), access,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        file_error(GetLastError());
        return -1;
    }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(file),
                                  (flags & ~(_O_CREAT | _O_EXCL)) | _O_BINARY);
    if (fd == -1)
        CloseHandle(file);
    return fd;
}

void* map_file_readonly(int fd, uint64_t offset, size_t size)
{
    HANDLE file = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping)
    {
        file_error(GetLastError());
        return nullptr;
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, DWORD(offset >> 32), DWORD(offset), size);
    const DWORD error = GetLastError();
    CloseHandle(mapping);
    if (!view)
        file_error(error);
    return view;
}

bool unmap_file(const void* view)
{
    if (UnmapViewOfFile(view))
        return true;
    file_error(GetLastError());
    return false;
}

bool copy_file_times(int input, int output)
{
    FILETIME creation, access, write;
    if (GetFileTime(reinterpret_cast<HANDLE>(_get_osfhandle(input)), &creation, &access, &write) &&
        SetFileTime(reinterpret_cast<HANDLE>(_get_osfhandle(output)), &creation, &access, &write))
        return true;
    file_error(GetLastError());
    return false;
}

uint64_t available_physical_memory()
{
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullAvailPhys : 0;
}

uint64_t committed_memory()
{
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPageFile - status.ullAvailPageFile : 0;
}
}
