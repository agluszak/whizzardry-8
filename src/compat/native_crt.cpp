/* Native implementations of the Microsoft CRT extensions and two-byte wide
   string functions declared in compat/native.h. */
#include <errno.h>
#include <limits.h>
#include <wctype.h>

#include <string>
#include <chrono>
#include <thread>

namespace {
/* Database strings can start at odd byte offsets in packed retail records. */
wchar_t read_wide(const wchar_t* text, size_t index = 0)
{
    wchar_t value;
    memcpy(&value, reinterpret_cast<const unsigned char*>(text) + index * sizeof(value),
           sizeof(value));
    return value;
}

void write_wide(wchar_t* text, size_t index, wchar_t value)
{
    memcpy(reinterpret_cast<unsigned char*>(text) + index * sizeof(value), &value, sizeof(value));
}

std::string narrow(const wchar_t* text)
{
    std::string result;
    for (; read_wide(text) != 0; ++text) {
        result += read_wide(text) < 0x100 ? (char)read_wide(text) : '?';
    }
    return result;
}

wchar_t lower(wchar_t character)
{
    return (wchar_t)towlower((wint_t)(unsigned short)character);
}

/* Appends to a caller-sized buffer, as Microsoft's swprintf does. */
struct Output {
    wchar_t* buffer;
    int length;

    void put(wchar_t character)
    {
        write_wide(buffer, length++, character);
    }

    void pad(int count, wchar_t character)
    {
        for (; count > 0; --count) {
            put(character);
        }
    }

    void ascii(const char* text)
    {
        for (; *text != 0; ++text) {
            put((wchar_t)(unsigned char)*text);
        }
    }
};
} // namespace

extern "C" {

uint32_t w8_get_ticks(void)
{
    using namespace std::chrono;
    return uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
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

size_t w8_wcslen(const wchar_t* text)
{
    size_t length = 0;
    while (read_wide(text, length) != 0) {
        ++length;
    }
    return length;
}

wchar_t* w8_wcscpy(wchar_t* destination, const wchar_t* source)
{
    wchar_t* cursor = destination;
    for (;;) {
        const wchar_t character = read_wide(source++);
        write_wide(cursor++, 0, character);
        if (character == 0) break;
    }
    return destination;
}

wchar_t* w8_wcsncpy(wchar_t* destination, const wchar_t* source, size_t count)
{
    size_t index = 0;
    for (; index < count && read_wide(source, index) != 0; ++index) {
        write_wide(destination, index, read_wide(source, index));
    }
    for (; index < count; ++index) {
        write_wide(destination, index, 0);
    }
    return destination;
}

wchar_t* w8_wcscat(wchar_t* destination, const wchar_t* source)
{
    w8_wcscpy(destination + w8_wcslen(destination), source);
    return destination;
}

wchar_t* w8_wcsncat(wchar_t* destination, const wchar_t* source, size_t count)
{
    wchar_t* end = destination + w8_wcslen(destination);
    size_t index = 0;
    for (; index < count && read_wide(source, index) != 0; ++index) {
        write_wide(end, index, read_wide(source, index));
    }
    write_wide(end, index, 0);
    return destination;
}

int w8_wcsncmp(const wchar_t* first, const wchar_t* second, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        const unsigned short a = (unsigned short)read_wide(first, index);
        const unsigned short b = (unsigned short)read_wide(second, index);
        if (a != b || a == 0) {
            return a < b ? -1 : a > b ? 1 : 0;
        }
    }
    return 0;
}

int w8_wcscmp(const wchar_t* first, const wchar_t* second)
{
    return w8_wcsncmp(first, second, (size_t)-1);
}

int w8_wcsnicmp(const wchar_t* first, const wchar_t* second, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        const unsigned short a = (unsigned short)lower(read_wide(first, index));
        const unsigned short b = (unsigned short)lower(read_wide(second, index));
        if (a != b || a == 0) {
            return a < b ? -1 : a > b ? 1 : 0;
        }
    }
    return 0;
}

int w8_wcsicmp(const wchar_t* first, const wchar_t* second)
{
    return w8_wcsnicmp(first, second, (size_t)-1);
}

wchar_t* w8_wcschr(const wchar_t* text, wchar_t character)
{
    for (;; ++text) {
        if (read_wide(text) == character) {
            return const_cast<wchar_t*>(text);
        }
        if (read_wide(text) == 0) {
            return 0;
        }
    }
}

wchar_t* w8_wcsrchr(const wchar_t* text, wchar_t character)
{
    const wchar_t* found = 0;
    for (;; ++text) {
        if (read_wide(text) == character) {
            found = text;
        }
        if (read_wide(text) == 0) {
            return const_cast<wchar_t*>(found);
        }
    }
}

wchar_t* w8_wcsstr(const wchar_t* text, const wchar_t* pattern)
{
    const size_t length = w8_wcslen(pattern);
    for (; read_wide(text) != 0; ++text) {
        if (w8_wcsncmp(text, pattern, length) == 0) {
            return const_cast<wchar_t*>(text);
        }
    }
    return length == 0 ? const_cast<wchar_t*>(text) : 0;
}

size_t w8_wcscspn(const wchar_t* text, const wchar_t* reject)
{
    size_t length = 0;
    while (read_wide(text, length) != 0 && w8_wcschr(reject, read_wide(text, length)) == 0) {
        ++length;
    }
    return length;
}

size_t w8_wcsspn(const wchar_t* text, const wchar_t* accept)
{
    size_t length = 0;
    while (read_wide(text, length) != 0 && w8_wcschr(accept, read_wide(text, length)) != 0) {
        ++length;
    }
    return length;
}

/* Microsoft's two-argument wcstok keeps its position in static state. */
wchar_t* w8_wcstok(wchar_t* text, const wchar_t* delimiters)
{
    static wchar_t* next = 0;
    if (text == 0) {
        text = next;
    }
    if (text == 0) {
        return 0;
    }
    text += w8_wcsspn(text, delimiters);
    if (read_wide(text) == 0) {
        next = 0;
        return 0;
    }
    wchar_t* end = text + w8_wcscspn(text, delimiters);
    if (read_wide(end) != 0) {
        write_wide(end, 0, 0);
        next = end + 1;
    } else {
        next = 0;
    }
    return text;
}

wchar_t* w8_wcsdup(const wchar_t* text)
{
    const size_t size = (w8_wcslen(text) + 1) * sizeof(wchar_t);
    wchar_t* copy = static_cast<wchar_t*>(malloc(size));
    if (copy != 0) {
        memcpy(copy, text, size);
    }
    return copy;
}

int w8_wtoi(const wchar_t* text)
{
    return atoi(narrow(text).c_str());
}

size_t w8_wcstombs(char* destination, const wchar_t* source, size_t count)
{
    mbstate_t state = {};
    size_t written = 0;
    for (size_t index = 0;; ++index) {
        if (destination != 0 && written == count) {
            return written;
        }
        const wchar_t character = read_wide(source, index);
        char bytes[MB_LEN_MAX];
        // The host receives a scalar character, never a two-byte text pointer.
        const size_t length = wcrtomb(bytes, character, &state);
        if (length == static_cast<size_t>(-1)) {
            return length;
        }
        if (destination != 0) {
            if (length > count - written) {
                return written;
            }
            memcpy(destination + written, bytes, length);
        }
        if (character == 0) {
            return written + length - 1;
        }
        written += length;
    }
}

int w8_vswprintf(wchar_t* buffer, const wchar_t* format, va_list arguments)
{
    Output out = {buffer, 0};
    for (const wchar_t* cursor = format; read_wide(cursor) != 0; ++cursor) {
        if (read_wide(cursor) != L'%') {
            out.put(read_wide(cursor));
            continue;
        }
        if (read_wide(cursor, 1) == L'%') {
            out.put(L'%');
            ++cursor;
            continue;
        }
        /* Collect flags, width and precision into a narrow spec. */
        std::string spec = "%";
        ++cursor;
        bool left = false;
        while (read_wide(cursor) != 0 && wcschr(L"-+ #0", read_wide(cursor)) != 0) {
            left |= read_wide(cursor) == L'-';
            spec += (char)read_wide(cursor++);
        }
        int width = -1;
        if (read_wide(cursor) == L'*') {
            width = va_arg(arguments, int);
            if (width < 0) {
                left = true;
                width = -width;
            }
            spec += std::to_string(width);
            ++cursor;
        } else {
            while (read_wide(cursor) >= L'0' && read_wide(cursor) <= L'9') {
                width = (width < 0 ? 0 : width * 10) + (read_wide(cursor) - L'0');
                spec += (char)read_wide(cursor++);
            }
        }
        int precision = -1;
        if (read_wide(cursor) == L'.') {
            spec += '.';
            ++cursor;
            precision = 0;
            if (read_wide(cursor) == L'*') {
                precision = va_arg(arguments, int);
                spec += std::to_string(precision);
                ++cursor;
            } else {
                while (read_wide(cursor) >= L'0' && read_wide(cursor) <= L'9') {
                    precision = precision * 10 + (read_wide(cursor) - L'0');
                    spec += (char)read_wide(cursor++);
                }
            }
        }
        /* Length: h, l, ll, I64, w. long is 32 bits on Windows. */
        enum { DEFAULT, SHORT, LONG64, NARROW, WIDE } size = DEFAULT;
        for (;;) {
            if (read_wide(cursor) == L'h') {
                size = SHORT;
                ++cursor;
            } else if (read_wide(cursor) == L'l' && read_wide(cursor, 1) == L'l') {
                size = LONG64;
                cursor += 2;
            } else if (read_wide(cursor) == L'l' || read_wide(cursor) == L'w') {
                size = size == DEFAULT ? WIDE : size;
                ++cursor;
            } else if (read_wide(cursor) == L'I' && read_wide(cursor, 1) == L'6' && read_wide(cursor, 2) == L'4') {
                size = LONG64;
                cursor += 3;
            } else {
                break;
            }
        }
        const wchar_t conversion = read_wide(cursor);
        if (conversion == 0) {
            break;
        }
        if (conversion == L's' || conversion == L'S') {
            /* In the wide family %s is wide and %S narrow; h/l override. */
            const bool narrow_text = conversion == L'S' ? size != WIDE : size == SHORT;
            const void* argument = va_arg(arguments, const void*);
            int length;
            if (argument == 0) {
                argument = narrow_text ? (const void*)"(null)" : (const void*)L"(null)";
            }
            if (narrow_text) {
                length = (int)strlen(static_cast<const char*>(argument));
            } else {
                length = (int)w8_wcslen(static_cast<const wchar_t*>(argument));
            }
            if (precision >= 0 && precision < length) {
                length = precision;
            }
            if (!left) {
                out.pad(width - length, L' ');
            }
            for (int index = 0; index < length; ++index) {
                out.put(narrow_text ? (wchar_t)(unsigned char)static_cast<const char*>(argument)[index]
                                    : read_wide(static_cast<const wchar_t*>(argument), index));
            }
            if (left) {
                out.pad(width - length, L' ');
            }
            continue;
        }
        if (conversion == L'c' || conversion == L'C') {
            const wchar_t character = (wchar_t)va_arg(arguments, int);
            if (!left) {
                out.pad(width - 1, L' ');
            }
            out.put(conversion == L'C' || size == SHORT ? (wchar_t)(unsigned char)character
                                                         : character);
            if (left) {
                out.pad(width - 1, L' ');
            }
            continue;
        }
        char text[512];
        switch (conversion) {
        case L'd':
        case L'i':
        case L'u':
        case L'x':
        case L'X':
        case L'o':
            if (size == LONG64) {
                spec += "ll";
                spec += (char)conversion;
                snprintf(text, sizeof(text), spec.c_str(), va_arg(arguments, long long));
            } else {
                int value = va_arg(arguments, int);
                if (size == SHORT) {
                    value = conversion == L'd' || conversion == L'i' ? (short)value
                                                                    : (unsigned short)value;
                }
                spec += (char)conversion;
                snprintf(text, sizeof(text), spec.c_str(), value);
            }
            break;
        case L'e':
        case L'E':
        case L'f':
        case L'g':
        case L'G':
            spec += (char)conversion;
            snprintf(text, sizeof(text), spec.c_str(), va_arg(arguments, double));
            break;
        case L'p':
            snprintf(text, sizeof(text), "%p", va_arg(arguments, void*));
            break;
        default:
            snprintf(text, sizeof(text), "%%%c", (char)conversion);
            break;
        }
        out.ascii(text);
    }
    out.put(0);
    return out.length - 1;
}

int w8_swprintf(wchar_t* buffer, const wchar_t* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int length = w8_vswprintf(buffer, format, arguments);
    va_end(arguments);
    return length;
}

} // extern "C"
