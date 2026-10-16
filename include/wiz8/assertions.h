#pragma once

#include <cstdlib>

#include <SDL3/SDL_log.h>

// Unlike the standard assert macro, this check is active in release builds.
#define W8_ASSERT(condition)                                                                       \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Assertion failed: %s (%s:%d)",          \
                            #condition, __FILE__, __LINE__);                                       \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
