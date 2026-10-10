#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
// Filename: MemMan.cpp
// Purpose: engine memory allocation diagnostics

#include "MemMan.h"
#include <cstdlib>
#if defined(_DEBUG) || defined(EXTREME_MEMORY_DEBUGGING)
#include <map>
#include <memory>
#include <mutex>
#include <string>
#endif
#ifdef EXTREME_MEMORY_DEBUGGING
#include <wiz8/filesystem.h>
#include <sstream>
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
struct Allocation
{
    UINT32 size;
    std::string sourceFile;
    INT32 line;
};

std::map<PTR, Allocation> allocations;
std::mutex allocationMutex;

PTR AllocateTracked(UINT32 size, const char* sourceFile, INT32 line, PTR special)
{
    if (!size)
        return nullptr;

    const std::lock_guard lock(allocationMutex);
    if (!fMemManagerInit) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MemAlloc: memory manager not initialized (line %d file %s)",
                          line, sourceFile);
    }

    // Payloads remain ordinary malloc blocks; metadata never changes their alignment.
    std::unique_ptr<void, decltype(&std::free)> owned(special ? nullptr : std::malloc(size),
                                                   &std::free);
    PTR ptr = special ? special : owned.get();
    if (!ptr)
        return nullptr;
    try {
        if (!allocations.emplace(ptr, Allocation{size, sourceFile ? sourceFile : "", line}).second)
            return nullptr;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    (void)owned.release();
    ++MemDebugCounter;
    guiMemTotal += size;
    guiMemAlloced += size;
#ifdef DEBUG_MEM_LEAKS
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "MemAlloc %p: %u requested bytes (line %d file %s)",
                      ptr, size, line, sourceFile);
#endif
    return ptr;
}

void FreeTracked(PTR ptr, [[maybe_unused]] const char* sourceFile,
                 [[maybe_unused]] INT32 line, PTR special)
{
    if (!ptr)
        return;
    const std::lock_guard lock(allocationMutex);
    const auto found = allocations.find(ptr);
    if (found != allocations.end()) {
#ifdef DEBUG_MEM_LEAKS
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "MemFree %p: %u requested bytes (line %d file %s)",
                          ptr, found->second.size, line, sourceFile);
#endif
        guiMemTotal -= found->second.size;
        guiMemFreed += found->second.size;
        allocations.erase(found);
        --MemDebugCounter;
    }
    if (!special)
        std::free(ptr);
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
    if (special && special != ptr && allocations.contains(special))
        return nullptr;
    auto found = allocations.find(ptr);
    const bool tracked = found != allocations.end();
    const UINT32 oldSize = tracked ? found->second.size : 0;
    Allocation record;
    try {
        record = Allocation{size, sourceFile ? sourceFile : "", line};
        // Reserve metadata before realloc: no allocation can fail after it consumes ptr.
        if (!tracked)
            found = allocations.emplace(ptr, Allocation{}).first;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }

    PTR replacement = special ? special : std::realloc(ptr, size);
    if (!replacement) {
        if (!tracked)
            allocations.erase(found);
        return nullptr;
    }

    auto node = allocations.extract(found);
    node.key() = replacement;
    node.mapped() = std::move(record);
    allocations.insert(std::move(node));
    if (!tracked)
        ++MemDebugCounter;
    guiMemTotal = guiMemTotal - oldSize + size;
    guiMemFreed += oldSize;
    guiMemAlloced += size;
#ifdef DEBUG_MEM_LEAKS
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "MemRealloc %p: %u -> %u requested bytes (line %d file %s)",
                      replacement, oldSize, size, line, sourceFile);
#endif
    return replacement;
}
} // namespace
#endif

BOOLEAN InitializeMemoryManager(void)
{
#if defined(_DEBUG) || defined(EXTREME_MEMORY_DEBUGGING)
    const std::lock_guard lock(allocationMutex);
#endif
    if (fMemManagerInit)
        return TRUE;
#if defined(_DEBUG) || defined(EXTREME_MEMORY_DEBUGGING)
    if (allocations.empty())
#endif
    {
        MemDebugCounter = 0;
        guiMemTotal = 0;
        guiMemAlloced = 0;
        guiMemFreed = 0;
    }
    fMemManagerInit = TRUE;
    return TRUE;
}

void ShutdownMemoryManager(void)
{
#if defined(_DEBUG) || defined(EXTREME_MEMORY_DEBUGGING)
    const std::lock_guard lock(allocationMutex);
#endif
    if (MemDebugCounter != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MEMORY LEAK: %u tracked blocks, %llu requested bytes still allocated",
                          MemDebugCounter, static_cast<unsigned long long>(guiMemTotal));
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "%llu requested bytes allocated, %llu requested bytes freed",
                          static_cast<unsigned long long>(guiMemAlloced),
                          static_cast<unsigned long long>(guiMemFreed));
    }
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
    if (!filename)
        return;
    try {
        struct LocationTotals
        {
            UINT32 count = 0;
            uint64_t bytes = 0;
        };
        std::map<std::pair<std::string, INT32>, LocationTotals> locations;
        UINT32 blocks;
        {
            const std::lock_guard lock(allocationMutex);
            blocks = MemDebugCounter;
            for (const auto& [ptr, allocation] : allocations) {
                auto& total = locations[{allocation.sourceFile, allocation.line}];
                ++total.count;
                total.bytes += allocation.size;
            }
        }
        std::ostringstream report;
        if (!blocks) {
            report << "NO MEMORY LEAKS DETECTED!\n";
        } else {
            report << blocks << " tracked allocation blocks remain\n";
            for (const auto& [location, total] : locations) {
                report << total.count << " occurrences of " << location.first << " -- line("
                       << location.second << ") (" << total.bytes << " requested bytes)\n";
            }
        }
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
