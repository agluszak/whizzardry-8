#include "surrender/srHeap.h"

#include <stdlib.h>
#include <string.h>

#pragma intrinsic(memset)

// FUNCTION: SURRENDER 0x100359C0
srHeap::srHeap()
{
    critical_section = new srCriticalSection;
    system_block_count = 0;
    block_sequence = 0;
    active_block_count = 0;
    large_blocks = 0;
    cached_block = 0;
    block_size = 0x7fe0;
    memset(small_free_lists, 0, sizeof(small_free_lists));
    small_blocks = 0;
    current_block = 0;
    current_block_offset = 0;
    partial_blocks = 0;
    block = 0;
    medium_blocks = 0;
}

// FUNCTION: SURRENDER 0x10035A50
srHeap::~srHeap()
{
    freeAll();
    delete critical_section;
}

/* The pooled allocator packs pointer-bearing 0x20-byte headers in front of
   each allocation; natively srHeap is a thin layer over malloc that keeps the
   requested size for msize(). */
namespace {
const size_t HEADER_SIZE = 16;

void* nativeAllocate(size_t size)
{
    unsigned char* raw = static_cast<unsigned char*>(malloc(size + HEADER_SIZE));
    if (raw == 0) {
        return 0;
    }
    *reinterpret_cast<size_t*>(raw) = size;
    return raw + HEADER_SIZE;
}

unsigned char* nativeHeader(void* allocation)
{
    return static_cast<unsigned char*>(allocation) - HEADER_SIZE;
}
} // namespace

void srHeap::freeAll() {}

void srHeap::dump(std::ostream&) {}

w8_ulong srHeap::msize(void* allocation)
{
    if (allocation == 0) {
        return 0;
    }
    return (w8_ulong)*reinterpret_cast<size_t*>(nativeHeader(allocation));
}

void* srHeap::allocate(w8_ulong size)
{
    return nativeAllocate(size);
}

void srHeap::free(void* allocation)
{
    if (allocation != 0) {
        ::free(nativeHeader(allocation));
    }
}

void srHeap::free(void* allocation, unsigned int)
{
    free(allocation);
}

// GLOBAL: SURRENDER 0x100A48D0
class srHeap srHeap;
