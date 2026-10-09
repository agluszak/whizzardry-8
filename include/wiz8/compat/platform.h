#pragma once

/* Temporary file/event adapter for callers awaiting native API migration.
   File operations resolve asset casing and backslash paths. */

#include "compat/kernel32.h"

#include <stdarg.h>
typedef const void* LPCVOID;
typedef LONG* PLONG;
typedef size_t SIZE_T;
typedef void* LPSECURITY_ATTRIBUTES;
typedef void* LPOVERLAPPED;
typedef FILETIME* LPFILETIME;
typedef SYSTEMTIME* LPSYSTEMTIME;
typedef MSG* LPMSG;
typedef DWORD LCID;
typedef void (CALLBACK* TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);

typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[MAX_PATH];
    CHAR cAlternateFileName[14];
} WIN32_FIND_DATAA, WIN32_FIND_DATA, *LPWIN32_FIND_DATAA, *LPWIN32_FIND_DATA;

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_READONLY 0x00000001u
#define FILE_ATTRIBUTE_HIDDEN 0x00000002u
#define FILE_ATTRIBUTE_SYSTEM 0x00000004u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_ATTRIBUTE_TEMPORARY 0x00000100u
#define FILE_ATTRIBUTE_OFFLINE 0x00001000u
#define FILE_ATTRIBUTE_COMPRESSED 0x00000800u
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000u
#define FILE_FLAG_RANDOM_ACCESS 0x10000000u
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000u
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#define PAGE_READONLY 0x02
#define FILE_MAP_READ 0x0004
#define DRIVE_UNKNOWN 0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4
#define DRIVE_CDROM 5
#define DRIVE_RAMDISK 6
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_NOT_SAME_DEVICE 17
#define ERROR_NO_MORE_FILES 18
#define ERROR_SHARING_VIOLATION 32
#define ERROR_NOT_SUPPORTED 50
#define ERROR_FILE_EXISTS 80
#define ERROR_INVALID_PARAMETER 87
#define ERROR_DISK_FULL 112
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_NEGATIVE_SEEK 131
#define ERROR_ALREADY_EXISTS 183
#define ERROR_ENVVAR_NOT_FOUND 203
#define ERROR_FILENAME_EXCED_RANGE 206
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000u
#define LOCALE_SYSTEM_DEFAULT 0x0800

/* Message values used by the recovered game loop and native SDL bridge. */
#define WM_QUIT 0x0012
#define WM_CLOSE 0x0010
#define WM_SIZE 0x0005
#define WM_ACTIVATEAPP 0x001C
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_MOUSEWHEEL 0x020A
#define PM_NOREMOVE 0
#define PM_REMOVE 1
#define WHEEL_DELTA 120

HANDLE W8CreateFile(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                    DWORD disposition, DWORD flags, HANDLE templ);
BOOL W8ReadFile(HANDLE file, LPVOID buffer, DWORD size, LPDWORD read, LPOVERLAPPED overlapped);
BOOL W8WriteFile(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped);
BOOL W8CloseHandle(HANDLE handle);
DWORD W8SetFilePointer(HANDLE file, LONG distance, PLONG distance_high, DWORD method);
DWORD W8GetFileSize(HANDLE file, LPDWORD size_high);
BOOL W8GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME access, LPFILETIME write);
BOOL W8DeleteFile(LPCSTR path);
HANDLE W8FindFirstFile(LPCSTR pattern, LPWIN32_FIND_DATAA data);
BOOL W8FindNextFile(HANDLE find, LPWIN32_FIND_DATAA data);
BOOL W8FindClose(HANDLE find);
DWORD W8GetLastError(void);
HANDLE W8CreateFileMapping(HANDLE file, LPSECURITY_ATTRIBUTES security, DWORD protect,
                           DWORD size_high, DWORD size_low, LPCSTR name);
LPVOID W8MapViewOfFile(HANDLE mapping, DWORD access, DWORD offset_high, DWORD offset_low,
                       SIZE_T size);
BOOL W8UnmapViewOfFile(LPCVOID view);
DWORD W8GetFileAttributes(LPCSTR path);
BOOL W8SetFileAttributes(LPCSTR path, DWORD attributes);
BOOL W8CopyFile(LPCSTR source, LPCSTR destination, BOOL fail_if_exists);
BOOL W8MoveFile(LPCSTR source, LPCSTR destination);
BOOL W8CreateDirectory(LPCSTR path, LPSECURITY_ATTRIBUTES security);
LONG W8CompareFileTime(const FILETIME* first, const FILETIME* second);
BOOL W8FileTimeToLocalFileTime(const FILETIME* file_time, LPFILETIME local_time);
BOOL W8FileTimeToSystemTime(const FILETIME* file_time, LPSYSTEMTIME system_time);
BOOL W8GetVolumeInformation(LPCSTR root, LPSTR volume_name, DWORD volume_name_size,
                            LPDWORD serial, LPDWORD component_length, LPDWORD flags,
                            LPSTR file_system, DWORD file_system_size);
UINT W8GetDriveType(LPCSTR root);
BOOL W8GetDiskFreeSpace(LPCSTR root, LPDWORD sectors_per_cluster, LPDWORD bytes_per_sector,
                        LPDWORD free_clusters, LPDWORD total_clusters);
DWORD W8GetEnvironmentVariable(LPCSTR name, LPSTR buffer, DWORD size);
BOOL W8SetEnvironmentVariable(LPCSTR name, LPCSTR value);
DWORD W8GetModuleFileName(HMODULE module, LPSTR buffer, DWORD size);
DWORD W8GetLogicalDriveStrings(DWORD size, LPSTR buffer);
UINT W8SetErrorMode(UINT mode);
void W8GetLocalTime(LPSYSTEMTIME time);
int W8GetDateFormat(LCID locale, DWORD flags, const SYSTEMTIME* date, LPCSTR format,
                    LPSTR buffer, int size);
DWORD W8FormatMessage(DWORD flags, LPCVOID source, DWORD message, DWORD language,
                       LPSTR buffer, DWORD size, va_list* arguments);

/* The shell implements these through SDL. Shared game code retains its
   existing wait/peek/dispatch protocol until that shell is ported. */
BOOL W8WaitMessage(void);
BOOL W8PeekMessage(LPMSG message, HWND window, UINT first, UINT last, UINT remove);
BOOL W8GetMessage(LPMSG message, HWND window, UINT first, UINT last);
BOOL W8TranslateMessage(const MSG* message);
LRESULT W8DispatchMessage(const MSG* message);
UINT_PTR W8SetTimer(HWND window, UINT_PTR id, UINT interval, TIMERPROC callback);
BOOL W8KillTimer(HWND window, UINT_PTR id);
/* SDL input uses game-client coordinates, matching SGPMouseGetPos. */
void W8GetMousePosition(POINT* point);
BOOL W8ClipCursor(const RECT* rect);
BOOL W8MinimizeWindow(HWND window);
