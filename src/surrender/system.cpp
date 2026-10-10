#include "surrender/srSystem.h"

#include "surrender/srStringTable.h"
#include "platform_paths.h"
#include "wiz8/filesystem.h"

#include <string>
#include <memory>
#include <stdlib.h>
#include <string.h>

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
    if (path == nullptr || size <= 0) return nullptr;
    const auto cwd = w8_native::current_directory();
    if (cwd.size() >= static_cast<std::size_t>(size)) return nullptr;
    memcpy(path, cwd.c_str(), cwd.size() + 1);
    return path;
}

w8_long srSystem::chDir(const char* path)
{
    return w8_native::change_directory(path);
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
    w8_long count = 0;
    try {
        const auto host = wiz8::path_from_utf8(base);
        const bool virtual_drive = base.size() >= 2 && base[1] == ':' &&
            ((base[0] >= 'C' && base[0] <= 'F') || (base[0] >= 'c' && base[0] <= 'f'));
        if (!virtual_drive && host.is_absolute()) {
            const std::string host_path = wiz8::path_to_utf8(host);
            std::unique_ptr<char*, decltype(&SDL_free)> entries(
                SDL_GlobDirectory(host_path.c_str(), strcmp(pattern, "*.*") == 0 ? "*" : pattern,
                                  SDL_GLOB_CASEINSENSITIVE, nullptr), &SDL_free);
            if (!entries) return 0;
            for (char** entry = entries.get(); *entry; ++entry) {
                const auto path = host / wiz8::path_from_utf8(*entry);
                const auto info = wiz8::host_file_status(path);
                if (info && info->info.type == SDL_PATHTYPE_FILE) {
                    const auto full = wiz8::path_to_utf8(path);
                    files.addString(full.c_str());
                    ++count;
                }
            }
            return count;
        }
        if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '\\';
        for (const auto& name : wiz8::list_directory(base, pattern)) {
            const std::string game_path = base + name;
            const auto entry = wiz8::file_status(game_path);
            if (entry && entry->info.type == SDL_PATHTYPE_FILE) {
                const std::string full = w8_native::full_path(game_path.c_str());
                files.addString(full.c_str());
                ++count;
            }
        }
    } catch (const std::exception&) {
        return count;
    }
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
    const std::string full = w8_native::full_path(path);
    if (full.empty()) return 0;
    if (absolute_path == 0) size = 4096;
    if (full.size() + 1 > size) {
        return 0;
    }
    if (absolute_path == 0) absolute_path = new char[size];
    strcpy(absolute_path, full.c_str());
    return absolute_path;
}

void srSystem::splitPath(const char* path, char* drive, char* directory, char* filename,
                         char* extension)
{
    std::string text = path != nullptr ? path : "";
    std::string prefix;
    if (text.size() >= 2 && text[1] == ':') {
        prefix = text.substr(0, 2);
        text.erase(0, 2);
    }
    const auto slash = text.find_last_of("/\\");
    const std::string folder = slash == std::string::npos ? "" : text.substr(0, slash + 1);
    const std::string leaf = slash == std::string::npos ? text : text.substr(slash + 1);
    const auto dot = leaf.find_last_of('.');
    const std::string stem = dot == std::string::npos ? leaf : leaf.substr(0, dot);
    const std::string suffix = dot == std::string::npos ? "" : leaf.substr(dot);
    if (drive) strcpy(drive, prefix.c_str());
    if (directory) strcpy(directory, folder.c_str());
    if (filename) strcpy(filename, stem.c_str());
    if (extension) strcpy(extension, suffix.c_str());
}
