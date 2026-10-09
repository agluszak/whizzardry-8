#pragma once

#include <iosfwd>

#include "srHeap.h"

// VTABLE: SURRENDER 0x100755C8
// class srFileManager
class srFileManager {
public:
    class Path {
    public:
#if !defined(SURRENDER_BUILD)
        Path& operator=(const Path& other);
#endif

        const char* getName() const;
        Path* getNext() const;

    protected:
        Path(const char* name);
        ~Path();

    private:
        friend class srFileManager;

        char* name0;
        Path* next;
        Path* previous;
    };

    srFileManager();

#if !defined(SURRENDER_BUILD)
    srFileManager(const srFileManager& other);
    srFileManager& operator=(const srFileManager& other);
#endif
    virtual ~srFileManager();

    void addPath(const char* path);
    void dump(std::ostream& stream);
    Path* getFirstPath() const;
    void removePath(const char* path);
    void setPath(const char* path);

    virtual void* allocate(const char* path);
    virtual void free(void* allocation);
    virtual void load(const char* path, void* destination, w8_ulong size);
    virtual void save(const char* path, void* source, w8_ulong size);
    virtual w8_long getSize(const char* path);

private:
    Path* first_path;
};

W8_ABI_ASSERT(sizeof(srFileManager::Path) == 0x0c, "srFileManager_Path_must_be_0x0c");
W8_ABI_ASSERT(sizeof(srFileManager) == 0x08, "srFileManager_must_be_0x08");
