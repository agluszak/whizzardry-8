#include <chrono>
#include <cstdint>

#include "wiz8/compat/native.h"
#include "wiz8/runtime_test_hooks.h"

std::uint64_t w8_clock_us()
{
    WIZ8_TEST_HOOK(if (g_runtime_test_hooks.virtual_clock) return g_runtime_test_hooks.clock_us;)
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

std::uint64_t w8_get_ticks()
{
    return w8_clock_us() / 1000;
}
