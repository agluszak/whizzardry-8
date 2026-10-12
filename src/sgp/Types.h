/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-04, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#ifndef __TYPES_
#define __TYPES_

#include <SDL3/SDL_log.h>
#include <cstdlib>

#define Assert(condition) AssertMsg(condition, "Assertion failed: " #condition)
#define AssertMsg(condition, message)                                                               \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s (%s:%d)",                             \
                            reinterpret_cast<const char*>(message), __FILE__, __LINE__);            \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

#define BAD_INDEX -1

#endif
