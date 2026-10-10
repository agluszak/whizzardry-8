#include "MemMan.h"
#include "allocation_failures.h"
#ifdef EXTREME_MEMORY_DEBUGGING
#include "temporary_directory.h"
#include <wiz8/asset_paths.h>
#include <filesystem>
#include <fstream>
#include <string>
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#if !defined(_DEBUG) && !defined(EXTREME_MEMORY_DEBUGGING)
#error Compile this test and MemMan.cpp with _DEBUG or EXTREME_MEMORY_DEBUGGING.
#endif

#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); std::exit(1); } } while (0)

static void counters(UINT32 blocks, uint64_t bytes)
{
    CHECK(MemDebugCounter == blocks);
    CHECK(guiMemTotal == bytes);
    CHECK(guiMemAlloced - guiMemFreed == bytes);
}

static void alignment_and_realloc()
{
    CHECK(InitializeMemoryManager());
    CHECK(!MemAlloc(0));
    MemFree(nullptr);
    counters(0, 0);
    for (UINT32 size = 1; size <= 256; ++size) {
        auto* ptr = static_cast<unsigned char*>(MemAlloc(size));
        CHECK(ptr);
        CHECK(reinterpret_cast<uintptr_t>(ptr) % alignof(std::max_align_t) == 0);
        counters(1, size);
        std::memset(ptr, 0x5a, size);
        const auto allocated = guiMemAlloced;
        const auto freed = guiMemFreed;
        auto* grown = static_cast<unsigned char*>(MemRealloc(ptr, size + 256));
        CHECK(grown);
        CHECK(reinterpret_cast<uintptr_t>(grown) % alignof(std::max_align_t) == 0);
        counters(1, size + 256);
        CHECK(guiMemAlloced == allocated + size + 256 && guiMemFreed == freed + size);
        for (UINT32 i = 0; i < size; ++i)
            CHECK(grown[i] == 0x5a);
        auto* shrunk = static_cast<unsigned char*>(MemRealloc(grown, size));
        CHECK(shrunk);
        CHECK(reinterpret_cast<uintptr_t>(shrunk) % alignof(std::max_align_t) == 0);
        counters(1, size);
        CHECK(guiMemAlloced == allocated + 2ULL * size + 256 && guiMemFreed == freed + 2ULL * size + 256);
        for (UINT32 i = 0; i < size; ++i)
            CHECK(shrunk[i] == 0x5a);
        CHECK(!MemRealloc(shrunk, 0));
        counters(0, 0);
    }
    CHECK(!MemRealloc(nullptr, 0));
    void* ptr = MemRealloc(nullptr, 19);
    CHECK(ptr);
    counters(1, 19);
    MemFree(ptr);
    counters(0, 0);
    ShutdownMemoryManager();
}

static void allocation_failures_and_untracked()
{
    CHECK(InitializeMemoryManager());
    for (int fail = 0; fail < 2; ++fail) {
        allocation_failures::after = fail;
#ifdef EXTREME_MEMORY_DEBUGGING
        void* failed = MemAllocXDebug(17, "source-location-long-enough-to-allocate.cpp", 51, nullptr);
#else
        void* failed = MemAllocReal(17, "source-location-long-enough-to-allocate.cpp", 51);
#endif
        allocation_failures::after = -1;
        CHECK(!failed);
        counters(0, 0);
        CHECK(guiMemAlloced == 0 && guiMemFreed == 0);
    }
    auto* ptr = static_cast<unsigned char*>(MemAlloc(23));
    CHECK(ptr);
    std::memset(ptr, 0x71, 23);
    const auto allocated = guiMemAlloced;
    const auto freed = guiMemFreed;
    allocation_failures::after = 0;
#ifdef EXTREME_MEMORY_DEBUGGING
    CHECK(!MemReallocXDebug(ptr, 61, "new-source-location-long-enough-to-allocate.cpp", 52, nullptr));
#else
    CHECK(!MemReallocReal(ptr, 61, "new-source-location-long-enough-to-allocate.cpp", 52));
#endif
    allocation_failures::after = -1;
    counters(1, 23);
    CHECK(guiMemAlloced == allocated && guiMemFreed == freed);
#ifdef WIZ8_TEST_WRAP_C_ALLOCATIONS
    allocation_failures::mallocFails = true;
    CHECK(!MemAlloc(29));
    allocation_failures::mallocFails = false;
    allocation_failures::reallocFails = true;
    CHECK(!MemRealloc(ptr, 61));
    allocation_failures::reallocFails = false;
    counters(1, 23);
    CHECK(guiMemAlloced == allocated && guiMemFreed == freed);
#endif
    for (unsigned i = 0; i < 23; ++i)
        CHECK(ptr[i] == 0x71);
    MemFree(ptr);
    counters(0, 0);

    ptr = static_cast<unsigned char*>(std::malloc(13));
    CHECK(ptr);
    std::memset(ptr, 0x6c, 13);
    allocation_failures::after = 0;
    CHECK(!MemRealloc(ptr, 41));
    allocation_failures::after = -1;
    counters(0, 0);
#ifdef WIZ8_TEST_WRAP_C_ALLOCATIONS
    allocation_failures::reallocFails = true;
    CHECK(!MemRealloc(ptr, 41));
    allocation_failures::reallocFails = false;
    counters(0, 0);
#endif
    auto* grown = static_cast<unsigned char*>(MemRealloc(ptr, 41));
    CHECK(grown);
    counters(1, 41);
    for (unsigned i = 0; i < 13; ++i)
        CHECK(grown[i] == 0x6c);
    MemFree(grown);
    ptr = static_cast<unsigned char*>(std::malloc(7));
    CHECK(ptr);
    MemFree(ptr);
    counters(0, 0);
    ShutdownMemoryManager();
}

static void lifetime_and_threads()
{
    // Allocation before init, repeated init and shutdown must not lose live records.
    void* ptr = MemAlloc(33);
    CHECK(ptr);
    counters(1, 33);
    CHECK(InitializeMemoryManager());
    CHECK(InitializeMemoryManager());
    counters(1, 33);
    ShutdownMemoryManager();
    ShutdownMemoryManager();
    counters(1, 33);
    std::memset(ptr, 0x19, 33);
    CHECK(InitializeMemoryManager());
    counters(1, 33);
    MemFree(ptr);
    ShutdownMemoryManager();
    CHECK(InitializeMemoryManager());
    CHECK(guiMemAlloced == 0 && guiMemFreed == 0);

    std::vector<std::thread> workers;
    for (unsigned thread = 0; thread < 6; ++thread) {
        workers.emplace_back([thread] {
            for (unsigned i = 0; i < 1000; ++i) {
                const UINT32 size = 8 + (i + thread) % 61;
                auto* block = static_cast<unsigned char*>(MemAlloc(size));
                CHECK(block);
                std::memset(block, static_cast<int>(thread), size);
                auto* grown = static_cast<unsigned char*>(MemRealloc(block, size + 64));
                CHECK(grown);
                for (unsigned j = 0; j < size; ++j)
                    CHECK(grown[j] == thread);
                MemFree(grown);
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    counters(0, 0);
    CHECK(guiMemAlloced > 0 && guiMemAlloced == guiMemFreed);
    ShutdownMemoryManager();
}

#ifdef EXTREME_MEMORY_DEBUGGING
static void special_and_reports()
{
    CHECK(InitializeMemoryManager());
    alignas(std::max_align_t) unsigned char first[64]{};
    alignas(std::max_align_t) unsigned char second[64]{};
    allocation_failures::after = 0;
    CHECK(!MemAllocXDebug(11, "special.cpp", 1, first));
    allocation_failures::after = -1;
    counters(0, 0);
    CHECK(MemAllocXDebug(11, "special.cpp", 1, first) == first);
    CHECK(!MemAllocXDebug(11, "special.cpp", 1, first));
    counters(1, 11);
    CHECK(MemReallocXDebug(first, 31, "special.cpp", 2, second) == second);
    counters(1, 31);
    CHECK(MemReallocXDebug(second, 7, "special.cpp", 3, second) == second);
    counters(1, 7);
    CHECK(MemAllocXDebug(17, "special.cpp", 1, first) == first);
    CHECK(!MemReallocXDebug(second, 5, "special.cpp", 4, first));
    counters(2, 24);
    MemFreeXDebug(first, "special.cpp", 5, first);
    CHECK(!MemReallocXDebug(second, 0, "special.cpp", 6, second));
    counters(0, 0);
    first[0] = 42;
    second[0] = 29;
    CHECK(first[0] == 42 && second[0] == 29);

    const auto directory = std::filesystem::path(make_temporary_directory("wiz8-memory"));
    const auto assets = directory / "assets";
    const auto user = directory / "user";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(user);
    w8_native::Roots roots{};
    roots.assets = assets.generic_string();
    roots.user = user.generic_string();
    w8_native::configure_paths(roots);
    char source[] = "borrowed-source.cpp";
    void* a = MemAllocXDebug(3, source, 17, nullptr);
    void* b = MemAllocXDebug(5, source, 17, nullptr);
    void* c = MemAllocXDebug(7, source, 18, nullptr);
    CHECK(a && b && c);
    std::memset(source, 'x', sizeof(source) - 1);
    UINT8 filename[] = "C:\\memory.txt";
    DumpMemoryInfoIntoFile(filename, FALSE);
    auto read = [&] {
        std::ifstream input(user / "memory.txt", std::ios::binary);
        CHECK(input.good());
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    const auto report = read();
    CHECK(report.find("3 tracked allocation blocks remain") != std::string::npos);
    CHECK(report.find("2 occurrences of borrowed-source.cpp -- line(17) (8 requested bytes)") != std::string::npos);
    CHECK(report.find("1 occurrences of borrowed-source.cpp -- line(18) (7 requested bytes)") != std::string::npos);
    CHECK(!std::filesystem::exists(assets / "memory.txt"));
    MemFree(a);
    MemFree(b);
    MemFree(c);
    DumpMemoryInfoIntoFile(filename, TRUE);
    CHECK(read() == report + "NO MEMORY LEAKS DETECTED!\n");
    DumpMemoryInfoIntoFile(filename, FALSE);
    CHECK(read() == "NO MEMORY LEAKS DETECTED!\n");
    DumpMemoryInfoIntoFile(nullptr, FALSE);
    UINT8 badFilename[] = "C:\\missing\\memory.txt";
    DumpMemoryInfoIntoFile(badFilename, FALSE);
    counters(0, 0);
    std::filesystem::remove_all(directory);
    ShutdownMemoryManager();
}
#endif

int main()
{
    alignment_and_realloc();
    allocation_failures_and_untracked();
    lifetime_and_threads();
#ifdef EXTREME_MEMORY_DEBUGGING
    special_and_reports();
#endif
#ifdef WIZ8_TEST_WRAP_C_ALLOCATIONS
    std::puts("debug memory: alignment, counters, lifetime, concurrency and malloc/realloc/metadata failures passed");
#else
    std::puts("debug memory: alignment, counters, lifetime, concurrency and metadata failures passed (C allocation failures not injected)");
#endif
    return 0;
}
