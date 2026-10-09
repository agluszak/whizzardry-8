#pragma once


class srStringTable;

class srSystem {
public:
    static w8_long chDir(const char* path);
    static char* fullPath(char* absolute_path, const char* path, w8_ulong size);
    static char* getCwd(char* path, w8_long size);
    static void makePath(char* path, const char* drive, const char* directory,
                                       const char* filename, const char* extension);
    static w8_long scanFiles(srStringTable& files, const char* path);
    static w8_long scanFiles(srStringTable& files, const char* directory,
                                        const char* pattern);
    static w8_long scanLibraries(srStringTable& libraries, const char* directory,
                                            const char* extension);
    static void splitPath(const char* path, char* drive, char* directory,
                                        char* filename, char* extension);
};

static_assert(sizeof(srSystem) == 0x01, "srSystem_must_be_stateless");
