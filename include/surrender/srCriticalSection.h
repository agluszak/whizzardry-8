#pragma once

#if defined(WIZ8_NATIVE)
#include <mutex>

/* Win32 critical sections are recursive. */
class srCriticalSection {
public:
    void getAccess()
    {
        mutex.lock();
    }

    void releaseAccess()
    {
        mutex.unlock();
    }

private:
    std::recursive_mutex mutex;
};
#else
#include <windows.h>

class srCriticalSection {
public:
    srCriticalSection()
    {
        InitializeCriticalSection(&critical_section);
    }

    ~srCriticalSection()
    {
        EnterCriticalSection(&critical_section);
        LeaveCriticalSection(&critical_section);
        DeleteCriticalSection(&critical_section);
    }

    void getAccess()
    {
        EnterCriticalSection(&critical_section);
    }

    void releaseAccess()
    {
        LeaveCriticalSection(&critical_section);
    }

private:
    CRITICAL_SECTION critical_section;
};

static_assert(sizeof(srCriticalSection) == 0x18, "srCriticalSection_must_be_0x18");
#endif

/* Scoped acquisition; the original guard spelling is unknown. */
class srCriticalSectionAccess {
public:
    explicit srCriticalSectionAccess(srCriticalSection* section) : section_(section)
    {
        section_->getAccess();
    }

    // FUNCTION: SURRENDER 0x10010660
    ~srCriticalSectionAccess()
    {
        section_->releaseAccess();
    }

private:
    srCriticalSection* section_;
};
