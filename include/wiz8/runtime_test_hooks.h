#ifndef WIZ8_RUNTIME_TEST_HOOKS_H
#define WIZ8_RUNTIME_TEST_HOOKS_H

/* Test-only instrumentation. Sources listed in the runtime test target are
   recompiled with WIZ8_RUNTIME_TESTS; elsewhere WIZ8_TEST_HOOK expands to
   nothing, so the shipped game contains none of it. Keep hooks in .cpp files:
   a hook in a header would give inline functions different definitions in
   instrumented and ordinary translation units. */
#ifdef WIZ8_RUNTIME_TESTS
struct W8RuntimeTestHooks
{
    /* Movie opens fail, so every caller takes its existing no-movie path. */
    bool skip_movies = false;
    unsigned movie_frames_presented = 0;
    /* When set, w8_clock_us returns clock_us, which the test advances per frame. */
    bool virtual_clock = false;
    unsigned long long clock_us = 0;
};
extern W8RuntimeTestHooks g_runtime_test_hooks;
#define WIZ8_TEST_HOOK(...) __VA_ARGS__
#else
#define WIZ8_TEST_HOOK(...)
#endif

#endif
