#include "surrender/srDynamicLibrary.h"
#include "surrender/srMutex.h"
#include "surrender/srThread.h"
#include "surrender/srTimer.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

#define CHECK(expression)                                                                          \
    do                                                                                            \
    {                                                                                             \
        if (!(expression))                                                                        \
        {                                                                                         \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                              \
            return 1;                                                                             \
        }                                                                                         \
    } while (0)

static void worker(void* argument)
{
    static_cast<std::promise<int>*>(argument)->set_value(42);
}

int main()
{
    srTimer timer;
    srQuadWord frequency;
    timer.getFreq(frequency);
    CHECK(frequency.lo == 1000000 && frequency.hi == 0);
    CHECK(timer.getIdent() && timer.getOsIdent());
    const double before = timer.getTime(srTimer::TIMER_READ_DEFAULT);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(timer.getTime(srTimer::TIMER_READ_DEFAULT) > before);
    timer.setUnits(1000);
    CHECK(timer.getUnits() == 1000);

    srMutex mutex;
    mutex.getAccess();
    mutex.getAccess();
    CHECK(mutex.accessAvailable());
    bool available = true;
    std::thread competing([&] { available = mutex.accessAvailable(); });
    competing.join();
    CHECK(!available);
    mutex.releaseAccess();
    mutex.releaseAccess();
    CHECK(mutex.accessAvailable());

    std::promise<int> result;
    auto completed = result.get_future();
    srThread::begin(worker, &result);
    CHECK(completed.get() == 42);
    const auto yields = srThread::getYieldCount();
    srThread::yield(0);
    CHECK(srThread::getYieldCount() == yields + 1);

    CHECK(!srDynamicLibrary::load(nullptr));
    void* library = srDynamicLibrary::load(WIZ8_PLATFORM_LIBRARY);
    CHECK(library);
    CHECK(srDynamicLibrary::getFunction(library, "w8_wcscmp"));
    CHECK(!srDynamicLibrary::getFunction(library, "missing_native_fixture_symbol"));
    CHECK(srDynamicLibrary::free(library));
    puts("ok: renderer monotonic clock, recursive mutex, worker and shared-object loading");
}
