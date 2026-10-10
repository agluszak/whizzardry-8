#include "wiz8/compat/native.h"
#include "wiz8/runtime_test_hooks.h"
#include <chrono>
#include <thread>

extern "C" {

uint64_t w8_clock_us(void)
{
    WIZ8_TEST_HOOK(if (g_runtime_test_hooks.virtual_clock) return g_runtime_test_hooks.clock_us;)
    using namespace std::chrono;
    return uint64_t(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

uint32_t w8_get_ticks(void)
{
    return uint32_t(w8_clock_us() / 1000);
}

void w8_sleep(uint32_t milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

char* w8_strupr(char* text)
{
    for (char* cursor = text; *cursor != 0; ++cursor) {
        *cursor = (char)toupper((unsigned char)*cursor);
    }
    return text;
}

char* w8_strlwr(char* text)
{
    for (char* cursor = text; *cursor != 0; ++cursor) {
        *cursor = (char)tolower((unsigned char)*cursor);
    }
    return text;
}

void w8_splitpath(const char* path, char* drive, char* directory, char* name, char* extension)
{
    const bool has_drive = path[0] != 0 && path[1] == ':';
    if (drive != 0) {
        if (has_drive) { drive[0] = path[0]; drive[1] = ':'; drive[2] = 0; }
        else drive[0] = 0;
    }
    if (has_drive) path += 2;
    const char* separator = 0;
    for (const char* cursor = path; *cursor != 0; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            separator = cursor;
        }
    }
    const char* base = separator != 0 ? separator + 1 : path;
    if (directory != 0) {
        const size_t length = (size_t)(base - path);
        memcpy(directory, path, length);
        directory[length] = 0;
    }
    const char* dot = strrchr(base, '.');
    if (dot == 0) {
        dot = base + strlen(base);
    }
    if (name != 0) {
        memcpy(name, base, (size_t)(dot - base));
        name[dot - base] = 0;
    }
    if (extension != 0) {
        strcpy(extension, dot);
    }
}

} // extern "C"
