#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "surrender/srDebug.h"

[[noreturn]] void srAssertFail(const char* expression, const char* source_path, int line,
                               const char* message, ...)
{
    std::string diagnostic = "Debug assertion in module ";
    diagnostic += source_path ? source_path : "(unknown)";
    diagnostic += " line " + std::to_string(line) + " failed:\n\nExpression [ ";
    diagnostic += expression ? expression : "(unknown)";
    diagnostic += " ] evaluates to false.\n";
    if (message && *message) {
        char details[1024]{};
        va_list arguments;
        va_start(arguments, message);
        std::vsnprintf(details, sizeof(details), message, arguments);
        va_end(arguments);
        diagnostic += "\n";
        diagnostic += details;
        diagnostic += "\n";
    }
    throw std::runtime_error(diagnostic);
}
