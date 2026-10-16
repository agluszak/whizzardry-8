#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL_messagebox.h>

#include "compat/compiler.h"
#include "surrender/srDebug.h"
// GLOBAL: SURRENDER 0x100A0280
static srAssertHandler srAssertFunc = 0;

// FUNCTION: SURRENDER 0x10001000
void srAssertSetFunc(srAssertHandler handler)
{
    srAssertFunc = handler;
}

// FUNCTION: SURRENDER 0x10001010
srAssertHandler srAssertGetFunc()
{
    return srAssertFunc;
}

/* With no handler installed the process exits with code 0xdeadbabe. The buffer is terminated even
   when message is 0. */
// FUNCTION: SURRENDER 0x10001020
void srAssertFail(const char* expression, const char* source_path, w8_long line,
                  const char* message, ...)
{
    char buffer[0x800];
    if (srAssertFunc != 0) {
        buffer[0] = '\0';
        if (message != 0) {
            va_list args;
            va_start(args, message);
            std::vsnprintf(buffer, 0x400, message, args);
            va_end(args);
        }
        srAssertFunc(expression, source_path, line, buffer);
        return;
    }
    exit(0xdeadbabe);
}
/* The stock handler SurRender ships for hosts that want a dialog. */
// FUNCTION: SURRENDER 0x100466A0
void srDefaultAssertFailFunc(const char* expression, const char* source_path, w8_long line,
                             const char* message)
{
    char buffer[0x800];
    if (message != 0 && *message != '\0') {
        std::snprintf(
            buffer, 0x79b,
            "Debug assertion in module %s line %d failed:\n\nExpression [ %s ] evaluates to "
            "false.\n\n%s\n",
            source_path, line, expression, message);
    } else {
        std::snprintf(
            buffer, 0x79b,
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
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_WARNING, 0, "FATAL: SR Assertion Failed", buffer, 2, buttons, 0};
    int choice = 0;
    if (SDL_ShowMessageBox(&box, &choice) && choice == 1) {
        __builtin_debugtrap();
    }
}
