#include <wiz8/filesystem.h>
#include <sstream>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
// Filename: MemMan.cpp
// Purpose: engine memory allocation diagnostics

#include "MemMan.h"
#include "DEBUG.H"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(_DEBUG) || defined(EXTREME_MEMORY_DEBUGGING)
#include <mutex>
#endif

// Requested engine allocation bytes, not allocator capacity or host memory usage.
uint64_t guiMemTotal = 0;
uint64_t guiMemAlloced = 0;
uint64_t guiMemFreed = 0;
UINT32 MemDebugCounter = 0;
BOOLEAN fMemManagerInit = FALSE;

#if defined(_DEBUG) || defined(EXTREME_MEMORY_DEBUGGING)
namespace
{
struct MEMORY_NODE
{
    PTR pBlock;
    MEMORY_NODE *next, *prev;
    const char* sourceFile;
    INT32 line;
    UINT32 uiSize;
};

MEMORY_NODE* gpMemoryHead = nullptr;
MEMORY_NODE* gpMemoryTail = nullptr;
std::mutex allocationMutex;

MEMORY_NODE* FindAllocation(PTR ptr)
{
    for (MEMORY_NODE* node = gpMemoryHead; node; node = node->next) {
        if (node->pBlock == ptr)
            return node;
    }
    return nullptr;
}

void LinkAllocation(MEMORY_NODE* node)
{
    node->prev = gpMemoryTail;
    node->next = nullptr;
    if (gpMemoryTail)
        gpMemoryTail->next = node;
    else
        gpMemoryHead = node;
    gpMemoryTail = node;
    ++MemDebugCounter;
}

void UnlinkAllocation(MEMORY_NODE* node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        gpMemoryHead = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        gpMemoryTail = node->prev;
    --MemDebugCounter;
    free(node);
}

PTR AllocateTracked(UINT32 size, const char* sourceFile, INT32 line, PTR special)
{
    if (!size)
        return nullptr;

    if (!fMemManagerInit)
        DbgMessage(TOPIC_MEMORY_MANAGER, DBG_LEVEL_0,
                   String("MemAlloc: memory manager not initialized (line %d file %s)",
                          line, sourceFile));

    // Separate metadata preserves malloc alignment and the caller's exact pointer.
    auto* node = static_cast<MEMORY_NODE*>(malloc(sizeof(MEMORY_NODE)));
    if (!node)
        return nullptr;
    PTR ptr = special ? special : malloc(size);
    if (!ptr) {
        free(node);
        DbgMessage(TOPIC_MEMORY_MANAGER, DBG_LEVEL_0,
                   String("MemAlloc failed: %u bytes (line %d file %s)", size, line, sourceFile));
        return nullptr;
    }

    const std::lock_guard lock(allocationMutex);
    node->pBlock = ptr;
    node->uiSize = size;
    node->sourceFile = sourceFile;
    node->line = line;
    LinkAllocation(node);
    guiMemTotal += size;
    guiMemAlloced += size;
#ifdef DEBUG_MEM_LEAKS
    DbgMessage(TOPIC_MEMORY_MANAGER, DBG_LEVEL_1,
               String("MemAlloc %p: %u requested bytes (line %d file %s)",
                      ptr, size, line, sourceFile));
#endif
    return ptr;
}

void FreeTracked(PTR ptr, [[maybe_unused]] const char* sourceFile,
                 [[maybe_unused]] INT32 line, PTR special)
{
    if (!ptr)
        return;
    const std::lock_guard lock(allocationMutex);
    auto* node = FindAllocation(ptr);
    if (node) {
#ifdef DEBUG_MEM_LEAKS
        DbgMessage(TOPIC_MEMORY_MANAGER, DBG_LEVEL_1,
                   String("MemFree %p: %u requested bytes (line %d file %s)",
                          ptr, node->uiSize, line, sourceFile));
#endif
        guiMemTotal -= node->uiSize;
        guiMemFreed += node->uiSize;
        UnlinkAllocation(node);
    }
    if (!special)
        free(ptr);
}

PTR ReallocateTracked(PTR ptr, UINT32 size, const char* sourceFile, INT32 line, PTR special)
{
    if (!ptr)
        return AllocateTracked(size, sourceFile, line, special);
    if (!size) {
        FreeTracked(ptr, sourceFile, line, special);
        return nullptr;
    }

    const std::lock_guard lock(allocationMutex);
    MEMORY_NODE* node = FindAllocation(ptr);
    const bool tracked = node != nullptr;
    const UINT32 oldSize = tracked ? node->uiSize : 0;
    if (!tracked) {
        node = static_cast<MEMORY_NODE*>(malloc(sizeof(MEMORY_NODE)));
        if (!node)
            return nullptr;
    }

    PTR replacement = special ? special : realloc(ptr, size);
    if (!replacement) {
        // A failed nonzero realloc leaves both the old allocation and its record intact.
        if (!tracked)
            free(node);
        DbgMessage(TOPIC_MEMORY_MANAGER, DBG_LEVEL_0,
                   String("MemRealloc failed: %p, %u -> %u bytes (line %d file %s)",
                          ptr, oldSize, size, line, sourceFile));
        return nullptr;
    }

    node->pBlock = replacement;
    node->uiSize = size;
    node->sourceFile = sourceFile;
    node->line = line;
    if (!tracked)
        LinkAllocation(node);
    guiMemTotal = guiMemTotal - oldSize + size;
    guiMemFreed += oldSize;
    guiMemAlloced += size;
#ifdef DEBUG_MEM_LEAKS
    DbgMessage(TOPIC_MEMORY_MANAGER, DBG_LEVEL_1,
               String("MemRealloc %p: %u -> %u requested bytes (line %d file %s)",
                      replacement, oldSize, size, line, sourceFile));
#endif
    return replacement;
}
} // namespace
#endif

BOOLEAN InitializeMemoryManager(void)
{
    RegisterDebugTopic(TOPIC_MEMORY_MANAGER, "Memory Manager");
    MemDebugCounter = 0;
    guiMemTotal = 0;
    guiMemAlloced = 0;
    guiMemFreed = 0;
    fMemManagerInit = TRUE;
    return TRUE;
}

void ShutdownMemoryManager(void)
{
    if (MemDebugCounter != 0) {
        DbgMessage(TOPIC_MEMORY_MANAGER, DBG_LEVEL_0,
                   String("MEMORY LEAK: %u tracked blocks, %llu requested bytes still allocated",
                          MemDebugCounter, static_cast<unsigned long long>(guiMemTotal)));
        DbgMessage(TOPIC_MEMORY_MANAGER, DBG_LEVEL_0,
                   String("%llu requested bytes allocated, %llu requested bytes freed",
                          static_cast<unsigned long long>(guiMemAlloced),
                          static_cast<unsigned long long>(guiMemFreed)));
    }
    UnRegisterDebugTopic(TOPIC_MEMORY_MANAGER, "Memory Manager Un-initialized");
    fMemManagerInit = FALSE;
}

#ifdef EXTREME_MEMORY_DEBUGGING
PTR MemAllocXDebug(UINT32 size, const char* sourceFile, INT32 line, void* special)
{
    return AllocateTracked(size, sourceFile, line, special);
}

void MemFreeXDebug(PTR ptr, const char* sourceFile, INT32 line, void* special)
{
    FreeTracked(ptr, sourceFile, line, special);
}

PTR MemReallocXDebug(PTR ptr, UINT32 size, const char* sourceFile, INT32 line, void* special)
{
    return ReallocateTracked(ptr, size, sourceFile, line, special);
}

void DumpMemoryInfoIntoFile(UINT8* filename, BOOLEAN append)
{
    std::ostringstream report;
    const std::lock_guard lock(allocationMutex);
    if (!gpMemoryHead) {
        report << "NO MEMORY LEAKS DETECTED!\n";
    } else {
        report << MemDebugCounter << " tracked allocation blocks remain\n";
        for (auto* node = gpMemoryHead; node; node = node->next) {
            auto sameLocation = [node](const MEMORY_NODE* other) {
                return node->line == other->line &&
                       strcmp(node->sourceFile, other->sourceFile) == 0;
            };
            bool reported = false;
            for (auto* previous = gpMemoryHead; previous != node; previous = previous->next) {
                if (sameLocation(previous)) {
                    reported = true;
                    break;
                }
            }
            if (reported)
                continue;
            UINT32 count = 0;
            unsigned long long bytes = 0;
            for (auto* other = node; other; other = other->next) {
                if (sameLocation(other)) {
                    ++count;
                    bytes += other->uiSize;
                }
            }
            report << count << " occurrences of " << node->sourceFile << " -- line("
                   << node->line << ") (" << bytes << " requested bytes)\n";
        }
    }
    try {
        auto output = wiz8::open_file(reinterpret_cast<const char*>(filename),
            append ? wiz8::OpenMode::append : wiz8::OpenMode::replace);
        const auto text = report.str();
        output->write(text.data(), text.size());
    } catch (const std::exception&) {}

}

BOOLEAN _AddAndRecordMemAlloc(UINT32, UINT32, UINT8*)
{
    return FALSE;
}
#elif defined(_DEBUG)
PTR MemAllocReal(UINT32 size, const char* sourceFile, INT32 line)
{
    return AllocateTracked(size, sourceFile, line, nullptr);
}

void MemFreeReal(PTR ptr, const char* sourceFile, INT32 line)
{
    FreeTracked(ptr, sourceFile, line, nullptr);
}

PTR MemReallocReal(PTR ptr, UINT32 size, const char* sourceFile, INT32 line)
{
    return ReallocateTracked(ptr, size, sourceFile, line, nullptr);
}
#endif
