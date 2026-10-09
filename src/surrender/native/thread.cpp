#include "surrender/srThread.h"

#include <chrono>
#include <functional>
#include <thread>

w8_long srThread::yieldCount;

w8_ulong srThread::begin(void (*entry)(void*), void* argument)
{
    std::thread thread(entry, argument);
    w8_ulong handle = std::hash<std::thread::id>()(thread.get_id());
    thread.detach();
    return handle;
}

/* Workers return from their entry function after calling end(). */
void srThread::end() {}

w8_ulong srThread::getHandle()
{
    return std::hash<std::thread::id>()(std::this_thread::get_id());
}

w8_long srThread::getYieldCount()
{
    return yieldCount;
}

void srThread::yield(w8_ulong milliseconds)
{
    yieldCount = yieldCount + 1;
    if (milliseconds == 0) {
        std::this_thread::yield();
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}
