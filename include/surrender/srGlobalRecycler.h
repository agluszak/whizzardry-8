#pragma once


#include "srHeap.h"

class srGlobalRecycler {
public:
    srGlobalRecycler();
    ~srGlobalRecycler();

    void* allocate(w8_ulong size);
    void free(void* allocation);
    void releaseAllUnused();
    void setLimit(w8_ulong limit);

private:
    void freeEntry(w8_ulong index);

    struct CacheEntry {
        void* allocation;
        w8_ulong size;
    };

    /* By-value critical section with a trivial constructor and a draining destructor; the owner
       initializes, enters and leaves it explicitly. */
    class CriticalSection {
    public:
        std::recursive_mutex critical_section;
    };

    CacheEntry entries[16];
    w8_ulong used_mask;
    w8_ulong cached_bytes;
    w8_ulong limit;
    CriticalSection critical_section;
};

W8_ABI_ASSERT(sizeof(srGlobalRecycler) == 0xa4, "srGlobalRecycler_must_be_0xa4");
