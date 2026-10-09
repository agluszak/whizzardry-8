#include "surrender/srSystem.h"

#include "surrender/srStringTable.h"
#include "compat/platform.h"
#include "platform_paths.h"

#include <dirent.h>
#include <string>
#include <fnmatch.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

w8_long srSystem::scanFiles(srStringTable& files, const char* path)
{
    if (path == 0 || *path == '\0') {
        return 0;
    }
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    if (slash == 0) {
        return scanFiles(files, 0, path);
    }
    if (slash[1] == '\0') {
        return 0;
    }
    std::string directory(path, slash - path + 1);
    return scanFiles(files, directory.c_str(), slash + 1);
}

char* srSystem::getCwd(char* path, w8_long size)
{
    return w8_getcwd(path, size);
}

w8_long srSystem::chDir(const char* path)
{
    return w8_chdir(path);
}

w8_long srSystem::scanLibraries(srStringTable&, const char*, const char*)
{
    /* Device drivers, vector processors and extensions are built in. */
    return 0;
}

w8_long srSystem::scanFiles(srStringTable& files, const char* directory, const char* pattern)
{
    if (pattern == 0) {
        return 0;
    }
    std::string base = directory != nullptr ? directory : ".";
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '\\';
    WIN32_FIND_DATAA entry;
    HANDLE search = W8FindFirstFile((base + pattern).c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) return 0;
    w8_long count = 0;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            const std::string full = w8_native::full_path((base + entry.cFileName).c_str());
            files.addString(full.c_str());
            ++count;
        }
    } while (W8FindNextFile(search, &entry));
    W8FindClose(search);
    return count;
}

void srSystem::makePath(char* path, const char* drive, const char* directory, const char* filename,
                        const char* extension)
{
    path[0] = '\0';
    if (drive != 0 && *drive != '\0') {
        strcat(path, drive);
    }
    if (directory != 0 && *directory != '\0') {
        strcat(path, directory);
        const char last = directory[strlen(directory) - 1];
        if (last != '/' && last != '\\') {
            strcat(path, "/");
        }
    }
    if (filename != 0) {
        strcat(path, filename);
    }
    if (extension != 0 && *extension != '\0') {
        if (*extension != '.') {
            strcat(path, ".");
        }
        strcat(path, extension);
    }
}

char* srSystem::fullPath(char* absolute_path, const char* path, w8_ulong size)
{
    if (absolute_path == 0) {
        absolute_path = new char[PATH_MAX];
        size = PATH_MAX;
    }
    const std::string full = w8_native::full_path(path);
    if (full.empty()) return 0;
    if (full.size() + 1 > size) {
        return 0;
    }
    strcpy(absolute_path, full.c_str());
    return absolute_path;
}

void srSystem::splitPath(const char* path, char* drive, char* directory, char* filename,
                         char* extension)
{
    w8_splitpath(path, drive, directory, filename, extension);
}
