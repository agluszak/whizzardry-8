#include "wiz8/platform/timer.h"

#include <chrono>
#include <cstdint>

#include "wiz8/runtime_test_hooks.h"

std::uint64_t MonotonicMicroseconds()
{
    WIZ8_TEST_HOOK(if (g_runtime_test_hooks.virtual_clock) return g_runtime_test_hooks.clock_us;)
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

std::uint64_t MonotonicMilliseconds()
{
    return MonotonicMicroseconds() / 1000;
}
