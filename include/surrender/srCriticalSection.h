#pragma once

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
