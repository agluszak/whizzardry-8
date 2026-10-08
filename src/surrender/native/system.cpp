#include "surrender/srSystem.h"

#include "surrender/srStringTable.h"

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
    return getcwd(path, size);
}

w8_long srSystem::chDir(const char* path)
{
    return chdir(path);
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
    std::string base = directory != 0 ? directory : ".";
    DIR* search = opendir(base.c_str());
    if (search == 0) {
        return 0;
    }
    if (base.back() != '/') {
        base += '/';
    }
    w8_long count = 0;
    while (dirent* entry = readdir(search)) {
        if (fnmatch(pattern, entry->d_name, FNM_CASEFOLD) != 0) {
            continue;
        }
        const std::string candidate = base + entry->d_name;
        struct stat status;
        if (stat(candidate.c_str(), &status) != 0 || S_ISDIR(status.st_mode)) {
            continue;
        }
        char absolute_path[PATH_MAX];
        if (realpath(candidate.c_str(), absolute_path) != 0) {
            files.addString(absolute_path);
            ++count;
        }
    }
    closedir(search);
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
    std::string full;
    if (path[0] == '/') {
        full = path;
    } else {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == 0) {
            return 0;
        }
        full = std::string(cwd) + "/" + path;
    }
    if (full.size() + 1 > size) {
        return 0;
    }
    strcpy(absolute_path, full.c_str());
    return absolute_path;
}

void srSystem::splitPath(const char* path, char* drive, char* directory, char* filename,
                         char* extension)
{
    if (drive != 0) {
        drive[0] = '\0';
    }
    const char* separator = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (backslash != 0 && (separator == 0 || backslash > separator)) {
        separator = backslash;
    }
    const char* name = separator != 0 ? separator + 1 : path;
    if (directory != 0) {
        const size_t length = name - path;
        memcpy(directory, path, length);
        directory[length] = '\0';
    }
    const char* dot = strrchr(name, '.');
    if (dot == 0) {
        dot = name + strlen(name);
    }
    if (filename != 0) {
        memcpy(filename, name, dot - name);
        filename[dot - name] = '\0';
    }
    if (extension != 0) {
        strcpy(extension, dot);
    }
}
