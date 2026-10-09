#pragma once

#include "srHeap.h"

class srMemoryAllocator {
public:
    enum e_alignSize { ALIGN_SIZE_32 = 0x20 };

    srMemoryAllocator();
    ~srMemoryAllocator();
#if !defined(SURRENDER_BUILD)
    srMemoryAllocator& operator=(const srMemoryAllocator& other);
#endif

    void* allocate(w8_ulong size, const char* name);
    void* allocate(w8_ulong count, w8_ulong size, const char* name);
    void dump() const;
    void free(void* allocation);
    const char* getName(void* allocation) const;
    w8_ulong getSize(void* allocation) const;
    void setAlignment(e_alignSize alignment);

private:
    class Block {
    public:
        Block* next;
        Block* previous;
        void* raw_allocation;
        /* Written through by allocate's strcpy - mutable storage despite the
           read-only getName accessor. */
        char* name;
        w8_ulong total_size;
        w8_ulong requested_size;
        w8_ulong reserved[2];
    };

    Block* align(void* allocation);

    Block* first_block;
    w8_ulong allocated_bytes;
    w8_ulong allocation_count;
    e_alignSize alignment;
    int clear;
};

W8_ABI_ASSERT(sizeof(srMemoryAllocator) == 0x14, "srMemoryAllocator_must_be_0x14");
