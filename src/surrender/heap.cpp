#include "surrender/srHeap.h"
#include "surrender/srMemoryAllocator.h"

#include <stdlib.h>
#include <string.h>

#include "surrender/srDebug.h"

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

// FUNCTION: SURRENDER 0x10036500
srMemoryAllocator::srMemoryAllocator()
{
    first_block = 0;
    allocated_bytes = 0;
    allocation_count = 0;
    alignment = ALIGN_SIZE_32;
    clear = 1;
}

// FUNCTION: SURRENDER 0x10036520
srMemoryAllocator::~srMemoryAllocator() {}

// FUNCTION: SURRENDER 0x100042D0
void srMemoryAllocator::setAlignment(e_alignSize alignment)
{
    this->alignment = alignment;
}

// FUNCTION: SURRENDER 0x10036530
srMemoryAllocator::Block* srMemoryAllocator::align(void* allocation)
{
    /* The header precedes the aligned user address (0x20 bytes on Windows). */
    // reinterpret-ok: block alignment is computed on the raw allocation bits.
    return reinterpret_cast<Block*>(
        ((reinterpret_cast<w8_ulong_ptr>(allocation) + alignment + sizeof(Block) - 1) &
         ~static_cast<w8_ulong_ptr>(alignment - 1)) -
        sizeof(Block));
}

// FUNCTION: SURRENDER 0x10036570
void* srMemoryAllocator::allocate(w8_ulong count, w8_ulong size, const char* name)
{
    w8_ulong requested = count * size;
    w8_ulong allocation_size = alignment + sizeof(Block) - 1 + requested;
    if (name != 0) {
        allocation_size += strlen(name) + 1;
    }
    void* raw = operator new(allocation_size);
    memset(raw, 0, allocation_size);
    if (raw == 0) {
        return 0;
    }
    Block* block = align(raw);
    block->requested_size = requested;
    block->raw_allocation = raw;
    block->total_size = allocation_size;
    if (name == 0) {
        block->name = 0;
    } else {
        // reinterpret-ok: the name string is stored right after the user area.
        block->name = reinterpret_cast<char*>(block) + sizeof(Block) + requested;
        strcpy(block->name, name);
    }
    block->next = first_block;
    block->previous = 0;
    if (first_block != 0) {
        first_block->previous = block;
    }
    first_block = block;
    ++allocation_count;
    allocated_bytes += block->total_size;
    return block + 1;
}

// FUNCTION: SURRENDER 0x10036550
void* srMemoryAllocator::allocate(w8_ulong size, const char* name)
{
    return allocate(1, size, name);
}

// FUNCTION: SURRENDER 0x100366F0
w8_ulong srMemoryAllocator::getSize(void* allocation) const
{
    return (static_cast<Block*>(allocation) - 1)->requested_size;
}

// FUNCTION: SURRENDER 0x10036700
const char* srMemoryAllocator::getName(void* allocation) const
{
    return (static_cast<Block*>(allocation) - 1)->name;
}

// FUNCTION: SURRENDER 0x10036710
void srMemoryAllocator::dump() const
{
    srPrintf("Memory dump\n");
    srPrintf("\nAddress      Size    Tag  Name\n");
    srPrintf("-------------------------------------------------------------------\n");
    for (Block* block = first_block; block != 0; block = block->next) {
        const char* name = block->name;
        if (name == 0) {
            name = "<unnamed>";
        }
        srPrintf("%8p %8d %s\n", block + 1, block->requested_size, name);
    }
    srPrintf("-------------------------------------------------------------------\n");
    srPrintf("Total memory used %d bytes (%d Kb) for %d entries.\n", allocated_bytes,
             (w8_long)(allocated_bytes + 0x3ff) / 1024, allocation_count);
    srPrintf("Alignment: %d Clear: %s\n", alignment, clear != 0 ? "Yes" : "No");
}

// FUNCTION: SURRENDER 0x100366A0
void srMemoryAllocator::free(void* allocation)
{
    Block* block = static_cast<Block*>(allocation) - 1;
    if (block->next != 0) {
        block->next->previous = block->previous;
    }
    if (block->previous != 0) {
        block->previous->next = block->next;
    }
    if (block == first_block) {
        first_block = block->next;
    }
    allocated_bytes -= block->total_size;
    --allocation_count;
    operator delete(block->raw_allocation);
}

// GLOBAL: SURRENDER 0x100A48D0
class srHeap srHeap;
