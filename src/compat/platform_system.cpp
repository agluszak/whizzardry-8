#include <algorithm>
#include <atomic>
#include <cerrno>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "compat/platform.h"
#include "platform_paths.h"

namespace
{
std::atomic<UINT> error_mode{0};
void system_time(const tm& value, unsigned milliseconds, SYSTEMTIME* output)
{
    *output = {uint16_t(value.tm_year + 1900), uint16_t(value.tm_mon + 1), uint16_t(value.tm_wday),
               uint16_t(value.tm_mday),        uint16_t(value.tm_hour),    uint16_t(value.tm_min),
               uint16_t(value.tm_sec),         uint16_t(milliseconds)};
}
bool unix_time(const FILETIME* input, time_t& seconds, unsigned& milliseconds)
{
    if (!input)
        return false;
    uint64_t ticks = (uint64_t(input->dwHighDateTime) << 32) | input->dwLowDateTime;
    if (ticks >= 0x8000000000000000ULL)
        return false;
    seconds = time_t(ticks / 10000000) - 11644473600LL;
    milliseconds = unsigned(ticks % 10000000 / 10000);
    return true;
}
void utc_system_time(int64_t seconds, unsigned milliseconds, SYSTEMTIME* output)
{
    const int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    const unsigned remainder = unsigned(seconds - days * 86400);
    const int64_t civil_days = days + 719468;
    const int64_t era = (civil_days >= 0 ? civil_days : civil_days - 146096) / 146097;
    const unsigned day_of_era = unsigned(civil_days - era * 146097);
    const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 -
                                  day_of_era / 146096) / 365;
    const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 -
                                              year_of_era / 100);
    const unsigned march_month = (5 * day_of_year + 2) / 153;
    const unsigned day = day_of_year - (153 * march_month + 2) / 5 + 1;
    const unsigned month = march_month < 10 ? march_month + 3 : march_month - 9;
    const int64_t year = year_of_era + era * 400 + (month <= 2);
    *output = {uint16_t(year), uint16_t(month), uint16_t(((days + 4) % 7 + 7) % 7),
               uint16_t(day), uint16_t(remainder / 3600), uint16_t(remainder / 60 % 60),
               uint16_t(remainder % 60), uint16_t(milliseconds)};
}
int drive(const char* root)
{
    std::string path = w8_native::full_path(root ? root : "\\");
    if (path.size() < 3 || path[1] != ':' || path.size() != 3)
        return -1;
    return path[0] - 'C';
}
bool copy_string(const std::string& input, char* buffer, size_t size)
{
    if (!buffer)
        return true;
    if (size <= input.size())
        return false;
    memcpy(buffer, input.c_str(), input.size() + 1);
    return true;
}
} // namespace
/* Shared with the file implementation, kept private to the native library. */
void w8_set_error(DWORD error);

BOOL W8FileTimeToSystemTime(const FILETIME* input, LPSYSTEMTIME output)
{
    time_t seconds;
    unsigned milliseconds;
    if (!output || !unix_time(input, seconds, milliseconds))
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    utc_system_time(seconds, milliseconds, output);
    return 1;
}
BOOL W8FileTimeToLocalFileTime(const FILETIME* input, LPFILETIME output)
{
    time_t seconds;
    unsigned milliseconds;
    tm local;
    const time_t now = time(nullptr);
    if (!output || input == output || !unix_time(input, seconds, milliseconds))
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
#ifdef _WIN32
    const bool converted = localtime_s(&local, &now) == 0;
#else
    const bool converted = localtime_r(&now, &local) != nullptr;
#endif
    if (!converted)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* Use the current timezone/DST bias, even for a historical file time. */
    uint64_t original = (uint64_t(input->dwHighDateTime) << 32) | input->dwLowDateTime;
#ifdef _WIN32
    tm utc;
    if (gmtime_s(&utc, &now) != 0)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    utc.tm_isdst = local.tm_isdst;
    const time_t local_interpretation = mktime(&utc);
    if (local_interpretation == time_t(-1))
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    int64_t bias = int64_t(difftime(now, local_interpretation));
#else
    int64_t bias = local.tm_gmtoff;
#endif
    uint64_t shifted = original + bias * 10000000;
    *output = {DWORD(shifted), DWORD(shifted >> 32)};
    return 1;
}
void W8GetLocalTime(LPSYSTEMTIME output)
{
    if (!output)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    tm local;
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    system_time(local, unsigned(milliseconds), output);
}
UINT W8SetErrorMode(UINT mode) { return error_mode.exchange(mode); }
UINT W8GetDriveType(LPCSTR root)
{
    int index = drive(root);
    if (index < 0 || index > 3)
        return DRIVE_NO_ROOT_DIR;
    auto roots = w8_native::path_roots();
    struct stat status;
    const std::string& path = index == 0 ? roots.assets : roots.discs[index - 1];
    if (path.empty() || stat(path.c_str(), &status) != 0 || !S_ISDIR(status.st_mode))
        return DRIVE_NO_ROOT_DIR;
    return index == 0 ? DRIVE_FIXED : DRIVE_CDROM;
}
DWORD W8GetLogicalDriveStrings(DWORD size, LPSTR buffer)
{
    std::string result("C:\\\0", 4);
    for (char letter = 'D'; letter <= 'F'; ++letter)
    {
        char root[] = {letter, ':', '\\', 0};
        if (W8GetDriveType(root) == DRIVE_CDROM)
            result.append(root, 4);
    }
    if (size <= result.size())
        return DWORD(result.size() + 1);
    if (!buffer)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    memcpy(buffer, result.c_str(), result.size() + 1);
    return DWORD(result.size());
}
BOOL W8GetVolumeInformation(LPCSTR root, LPSTR label, DWORD label_size, LPDWORD serial,
                            LPDWORD components, LPDWORD flags, LPSTR file_system, DWORD fs_size)
{
    const int index = drive(root);
    UINT type = W8GetDriveType(root);
    if (type == DRIVE_NO_ROOT_DIR)
    {
        w8_set_error(ERROR_PATH_NOT_FOUND);
        return 0;
    }
    const std::string name = index == 0 ? "WHIZZARDRY" : "WIZ8_" + std::to_string(index);
    if (!copy_string(name, label, label_size) ||
        !copy_string(index == 0 ? "W8FS" : "CDFS", file_system, fs_size))
    {
        w8_set_error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    if (serial)
        *serial = index;
    if (components)
        *components = 255;
    if (flags)
        *flags = type == DRIVE_CDROM ? 0x00080000u : 0; // FILE_READ_ONLY_VOLUME
    return 1;
}
BOOL W8GetDiskFreeSpace(LPCSTR root, LPDWORD sectors, LPDWORD bytes, LPDWORD free_count,
                        LPDWORD total)
{
    int index = drive(root);
    if (W8GetDriveType(root) == DRIVE_NO_ROOT_DIR)
    {
        w8_set_error(ERROR_PATH_NOT_FOUND);
        return 0;
    }
    auto roots = w8_native::path_roots();
    std::filesystem::path path = index == 0 ? roots.user : roots.discs[index - 1];
    /* A not-yet-created user directory uses its nearest existing ancestor. */
    std::error_code error;
    auto space = std::filesystem::space(path, error);
    while (error)
    {
        const auto parent = path.parent_path();
        if (parent == path || parent.empty())
        {
            w8_set_error(ERROR_PATH_NOT_FOUND);
            return 0;
        }
        path = parent;
        error.clear();
        space = std::filesystem::space(path, error);
    }
    if (bytes)
        *bytes = 512;
    if (sectors)
        *sectors = 8;
    if (free_count)
        *free_count = index == 0 ? DWORD(std::min<uint64_t>(space.available / 4096, UINT32_MAX)) : 0;
    if (total)
        *total = DWORD(std::min<uint64_t>(space.capacity / 4096, UINT32_MAX));
    return 1;
}
DWORD W8GetEnvironmentVariable(LPCSTR name, LPSTR buffer, DWORD size)
{
    if (!name)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const char* value = getenv(name);
    if (!value)
    {
        w8_set_error(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }
    size_t length = strlen(value);
    if (size <= length)
        return DWORD(length + 1);
    if (!buffer)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    memcpy(buffer, value, length + 1);
    return DWORD(length);
}
BOOL W8SetEnvironmentVariable(LPCSTR name, LPCSTR value)
{
    if (!name || !*name || strchr(name, '='))
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
#ifdef _WIN32
    const int result = _putenv_s(name, value ? value : "");
#else
    const int result = value ? setenv(name, value, 1) : unsetenv(name);
#endif
    if (result != 0)
    {
        w8_set_error(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return 1;
}
DWORD W8GetModuleFileName(HMODULE module, LPSTR buffer, DWORD size)
{
    if (module)
    {
        w8_set_error(ERROR_NOT_SUPPORTED);
        return 0;
    }
    if (!size || !buffer)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    char path[4096];
#ifdef _WIN32
    char* program = nullptr;
    if (_get_pgmptr(&program) != 0 || !program)
    {
        w8_set_error(ERROR_FILE_NOT_FOUND);
        return 0;
    }
    snprintf(path, sizeof(path), "%s", program);
#elif defined(__APPLE__)
    uint32_t capacity = sizeof(path);
    if (_NSGetExecutablePath(path, &capacity) != 0)
    {
        w8_set_error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
#else
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length < 0)
    {
        w8_set_error(ERROR_FILE_NOT_FOUND);
        return 0;
    }
    path[length] = 0;
#endif
    /* The virtual executable lives at the asset root, as retail assumes. */
    const char* name = strrchr(path, '/');
#ifdef _WIN32
    const char* backslash = strrchr(path, '\\');
    if (backslash && (!name || backslash > name))
        name = backslash;
#endif
    std::string virtual_path = std::string("C:\\") + (name ? name + 1 : path);
    size_t count = std::min<size_t>(virtual_path.size(), size - 1);
    memcpy(buffer, virtual_path.data(), count);
    buffer[count] = 0;
    if (count < virtual_path.size())
    {
        w8_set_error(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    return DWORD(count);
}
int W8GetDateFormat(LCID locale, DWORD flags, const SYSTEMTIME* input, LPCSTR format, LPSTR buffer,
                    int size)
{
    if (locale != LOCALE_SYSTEM_DEFAULT || flags || !format || size < 0)
    {
        w8_set_error(ERROR_NOT_SUPPORTED);
        return 0;
    }
    SYSTEMTIME date;
    if (input)
        date = *input;
    else
        W8GetLocalTime(&date);
    tm value{};
    value.tm_year = date.wYear - 1900;
    value.tm_mon = date.wMonth - 1;
    value.tm_mday = date.wDay;
    value.tm_isdst = -1;
    if (date.wMonth < 1 || date.wMonth > 12 || date.wDay < 1 || date.wDay > 31 ||
        mktime(&value) == time_t(-1) || value.tm_mon != date.wMonth - 1 ||
        value.tm_mday != date.wDay)
    {
        w8_set_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* NLS date tokens used by NPC Scripting. Names follow the process locale. */
    std::string result;
    for (size_t i = 0; format[i];)
    {
        if (format[i] == '\'')
        {
            ++i;
            while (format[i] && format[i] != '\'')
                result += format[i++];
            if (format[i])
                ++i;
            continue;
        }
        char token = format[i];
        size_t count = 1;
        while (format[i + count] == token)
            ++count;
        const char* conversion = nullptr;
        if (token == 'd')
            conversion = count >= 4 ? "%A" : count == 3 ? "%a" : count == 2 ? "%d" : "%e";
        if (token == 'M')
            conversion = count >= 4 ? "%B" : count == 3 ? "%b" : "%m";
        if (token == 'y')
            conversion = count >= 3 ? "%Y" : "%y";
        if (conversion)
        {
            char part[128]{};
            strftime(part, sizeof(part), conversion, &value);
            std::string text(part);
            if (count == 1)
                while (text.size() > 1 && (text[0] == '0' || text[0] == ' '))
                    text.erase(0, 1);
            result += text;
        }
        else
            result.append(format + i, count);
        i += count;
    }
    if (size == 0)
        return int(result.size() + 1);
    if (!copy_string(result, buffer, size))
    {
        w8_set_error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return int(result.size() + 1);
}
DWORD W8FormatMessage(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPSTR buffer,
                      DWORD size, va_list* arguments)
{
    if (flags != FORMAT_MESSAGE_FROM_SYSTEM || source || language || arguments)
    {
        w8_set_error(ERROR_NOT_SUPPORTED);
        return 0;
    }
    const char* text;
    switch (message)
    {
    case ERROR_FILE_NOT_FOUND:
        text = "The system cannot find the file specified.\r\n";
        break;
    case ERROR_PATH_NOT_FOUND:
        text = "The system cannot find the path specified.\r\n";
        break;
    case ERROR_ACCESS_DENIED:
        text = "Access is denied.\r\n";
        break;
    case ERROR_SHARING_VIOLATION:
        text =
            "The process cannot access the file because it is being used by another process.\r\n";
        break;
    case ERROR_INVALID_HANDLE:
        text = "The handle is invalid.\r\n";
        break;
    default:
        text = "Native platform operation failed.\r\n";
        break;
    }
    if (!buffer || !copy_string(text, buffer, size))
    {
        w8_set_error(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return DWORD(strlen(text));
}
