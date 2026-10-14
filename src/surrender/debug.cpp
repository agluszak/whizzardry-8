#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <ostream>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "surrender/srDebug.h"
#include "surrender/srCore.h"
#include "compat/compiler.h"
#include "compat/native.h"

void srDumpFlags(std::ostream& stream, w8_ulong flags, const char* names)
{
    if (flags == 0) {
        stream << "[NONE]";
        return;
    }
    stream << '[';
    bool first = true;
    for (w8_ulong bit = 0; bit < 0x20; ++bit) {
        if ((flags & (w8_ulong{1} << bit)) != 0) {
            if (first) {
                first = false;
            } else {
                stream << ',';
            }
            if (names == 0 || *names == 0) {
                stream << bit;
            } else {
                while (*names != 0 && *names != ',') {
                    stream << *names++;
                }
                if (*names == ',') {
                    ++names;
                }
            }
        } else if (names != 0) {
            while (*names != 0 && *names != ',') {
                ++names;
            }
            if (*names == ',') {
                ++names;
            }
        }
    }
    stream << ']';
}

// GLOBAL: SURRENDER 0x100A0280
static srAssertHandler srAssertFunc = 0;

// FUNCTION: SURRENDER 0x10001000
void __cdecl srAssertSetFunc(srAssertHandler handler)
{
    srAssertFunc = handler;
}

// FUNCTION: SURRENDER 0x10001010
srAssertHandler __cdecl srAssertGetFunc()
{
    return srAssertFunc;
}

/* With no handler installed the process exits with code 0xdeadbabe. The buffer is terminated even
   when message is 0. */
// FUNCTION: SURRENDER 0x10001020
void __cdecl srAssertFail(const char* expression, const char* source_path, w8_long line, const char* message, ...)
{
    char buffer[0x800];
    if (srAssertFunc != 0) {
        buffer[0] = '\0';
        if (message != 0) {
            va_list args;
            va_start(args, message);
            _vsnprintf(buffer, 0x400, message, args);
            va_end(args);
        }
        srAssertFunc(expression, source_path, line, buffer);
        return;
    }
    exit(0xdeadbabe);
}

// FUNCTION: SURRENDER 0x10033330
w8_long __cdecl srDebugPrintf(w8_ulong level, const char* format, ...)
{
    if (level >= (srCore.debug_level & 0xff)) {
        return 0;
    }
    if (format == 0) {
        return 0;
    }
    char buffer[0x404];
    va_list args;
    va_start(args, format);
    w8_long result = SDL_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "%s", buffer);
    return result;
}

// FUNCTION: SURRENDER 0x100333A0
w8_long __cdecl srPrintf(const char* format, ...)
{
    if (format == 0) {
        return 0;
    }
    char buffer[0x404];
    va_list args;
    va_start(args, format);
    w8_long result = SDL_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "%s", buffer);
    return result;
}

// FUNCTION: SURRENDER 0x100333F0
w8_long __cdecl srStreamPrintf(std::ostream& stream, const char* format, ...)
{
    if (format != 0) {
        char buffer[0x404];
        va_list args;
        va_start(args, format);
        w8_long result = SDL_vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        stream << buffer;
        return result;
    }
    return 0;
}

// FUNCTION: SURRENDER 0x10002E40
const char* __cdecl srBoolToString(int value)
{
    return value != 0 ? "true" : "false";
}

/* The stock handler SurRender ships for hosts that want a dialog. */
// FUNCTION: SURRENDER 0x100466A0
void __cdecl srDefaultAssertFailFunc(const char* expression, const char* source_path, w8_long line, const char* message)
{
    char buffer[0x800];
    if (message != 0 && *message != '\0') {
        _snprintf(buffer, 0x79b,
                  "Debug assertion in module %s line %d failed:\n\nExpression [ %s ] evaluates to "
                  "false.\n\n%s\n",
                  source_path, line, expression, message);
    } else {
        _snprintf(buffer, 0x79b,
                  "Debug assertion in module %s line %d failed:\n\nExpression [ %s ] evaluates to "
                  "false.\n",
                  source_path, line, expression);
    }
    strcat(buffer, "\nSelect 'yes' to trigger the debugger or 'no' to resume program execution.\n");
    fputs(buffer, stderr);
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"},
    };
    const SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, 0, "FATAL: SR Assertion Failed", buffer, 2, buttons, 0};
    int choice = 0;
    if (SDL_ShowMessageBox(&box, &choice) && choice == 1) {
        __builtin_debugtrap();
    }
}
