#pragma once

#define SR_SYSTEM_API

#include <cstdint>
#include <string>
#include <vector>

class srSystem {
public:
    static SR_SYSTEM_API std::int32_t chDir(const char* path);
    static SR_SYSTEM_API char* fullPath(char* absolute_path, const char* path, std::uint32_t size);
    static SR_SYSTEM_API char* getCwd(char* path, std::int32_t size);
    static SR_SYSTEM_API void makePath(char* path, const char* drive, const char* directory,
                                       const char* filename, const char* extension);
    static SR_SYSTEM_API std::int32_t scanFiles(std::vector<std::string>& files, const char* path);
    static SR_SYSTEM_API std::int32_t scanFiles(std::vector<std::string>& files, const char* directory,
                                        const char* pattern);
    static SR_SYSTEM_API void splitPath(const char* path, char* drive, char* directory,
                                        char* filename, char* extension);
};

#undef SR_SYSTEM_API
