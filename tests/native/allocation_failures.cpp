#include "allocation_failures.h"

#include <cstdlib>
#include <new>

namespace allocation_failures
{
thread_local int after = -1;
thread_local bool mallocFails = false;
thread_local bool reallocFails = false;
}

void* operator new(std::size_t size)
{
    if (allocation_failures::after == 0) {
        allocation_failures::after = -1;
        throw std::bad_alloc();
    }
    if (allocation_failures::after > 0)
        --allocation_failures::after;
    if (void* ptr = std::malloc(size ? size : 1))
        return ptr;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

#ifdef WIZ8_TEST_WRAP_C_ALLOCATIONS
// Optional linker --wrap=malloc/--wrap=realloc lane; not a production allocator hook.
extern "C" void* __real_malloc(std::size_t size);
extern "C" void* __real_realloc(void* ptr, std::size_t size);

extern "C" void* __wrap_malloc(std::size_t size)
{
    return allocation_failures::mallocFails ? nullptr : __real_malloc(size);
}

extern "C" void* __wrap_realloc(void* ptr, std::size_t size)
{
    return allocation_failures::reallocFails ? nullptr : __real_realloc(ptr, size);
}
#endif
