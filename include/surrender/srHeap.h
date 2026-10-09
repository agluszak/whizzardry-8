#pragma once

#include <iosfwd>
#include <string.h>
#include <fenv.h>
#include <math.h>

#include "srCriticalSection.h"


/* Zero fill that pre-aligns the destination to an 8-byte boundary. */
inline void srZeroMemory(void* destination, w8_long size)
{
    if (size > 0) {
        /* reinterpret-ok: raw address alignment is storage the type system
           cannot express. */
        w8_ulong misalign = reinterpret_cast<w8_ulong_ptr>(destination) & 7;
        if (size >= 8 && misalign != 0) {
            w8_ulong head = 8 - misalign;
            memset(destination, 0, head);
            /* reinterpret-ok: byte-granular advance past the head fill. */
            memset(reinterpret_cast<unsigned char*>(destination) + head, 0, size - head);
        } else {
            memset(destination, 0, size);
        }
    }
}

/* Float-to-int through the FPU's current rounding mode (round to nearest, not truncation). */
inline w8_long srFloatToInt(double value)
{
    const double rounded = rint(value);
    // Retail FISTP stores a signed dword. LP64 lrint instead has a 64-bit
    // range and narrowing its integer-indefinite result can produce zero.
    if (!(rounded >= -2147483648.0 && rounded <= 2147483647.0)) {
        feraiseexcept(FE_INVALID);
        return (-2147483647 - 1);
    }
    return static_cast<w8_long>(rounded);
}

inline w8_long srFloatToInt(float value)
{
    return srFloatToInt(static_cast<double>(value));
}

class srHeap {
public:
    srHeap();
    ~srHeap();

    void* allocate(w8_ulong size);
    void free(void* allocation);
    void free(void* allocation, unsigned int size);
    void freeAll();
    w8_ulong msize(void* allocation);
    void dump(std::ostream& stream);

private:
    struct Chunk;

    struct Block {
        void* allocation;
        w8_ulong alloc_size;
        Block* next;
        Block* previous;
        w8_ulong largest_free_size;
        Chunk* largest_free_block;
        w8_ulong guard0;
        w8_ulong guard1;
    };

    W8_ABI_ASSERT(sizeof(Block) == 0x20, "srHeap_Block_must_be_0x20");

    /* In-block allocation record for the pooled mid-size path. The header sits immediately before
       the user pointer; its last byte is the allocation tag read by free(). */
    struct Chunk {
        Block* owner;
        w8_ulong size;
        Chunk* previous;
        Chunk* next;
        Chunk* free_previous;
        Chunk* free_next;
        w8_ulong free;
        char unused[3];
        char tag;
    };

    W8_ABI_ASSERT(sizeof(Chunk) == 0x20, "srHeap_Chunk_must_be_0x20");

    Block* allocateBlock(w8_ulong size);
    void releaseBlock(Block* block);
    void releaseCachedBlock();
    void freeSystemBlock(void* allocation);
    void checkBlock(Block* block);
    void* splitFree(Block* block, w8_ulong size);
    void* allocatePooled(w8_ulong size);
    void freePooled(void* allocation);
    void* allocateSystem(w8_ulong size);
    void freeSystem(void* allocation);

    void* small_free_lists[32];
    w8_ulong current_block_offset;
    Block* current_block;
    Block* small_blocks;
    Block* partial_blocks;
    Block* block;
    Block* medium_blocks;
    Block* large_blocks;
    Block* cached_block;
    w8_ulong active_block_count;
    w8_ulong block_size;
    w8_ulong block_sequence;
    w8_ulong system_block_count;
    srCriticalSection* critical_section;
};

W8_ABI_ASSERT(sizeof(srHeap) == 0xb4, "srHeap_must_be_0xb4");

extern class srHeap srHeap;
