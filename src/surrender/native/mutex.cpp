#include "surrender/srMutex.h"

#include <mutex>

/* Native srMutex: a recursive mutex like the Win32 mutex object it replaces. */
namespace {
std::recursive_mutex* nativeMutex(void* handle)
{
    return static_cast<std::recursive_mutex*>(handle);
}
} // namespace

srMutex::srMutex()
{
    access_count = 0;
    handle = new std::recursive_mutex;
}

srMutex::~srMutex()
{
    delete nativeMutex(handle);
}

int srMutex::accessAvailable()
{
    if (!nativeMutex(handle)->try_lock()) {
        return 0;
    }
    nativeMutex(handle)->unlock();
    return 1;
}

void srMutex::getAccess()
{
    nativeMutex(handle)->lock();
    access_count++;
}

void srMutex::releaseAccess()
{
    nativeMutex(handle)->unlock();
    access_count--;
}
