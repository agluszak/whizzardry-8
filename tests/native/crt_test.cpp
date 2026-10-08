// Microsoft CRT semantics of the native wide-string layer.
#include <stdio.h>

static int failures = 0;

static void expect(const wchar_t* actual, const wchar_t* expected, const char* what)
{
    if (wcscmp(actual, expected) != 0) {
        char narrow[256];
        size_t index = 0;
        for (; actual[index] != 0 && index + 1 < sizeof(narrow); ++index) {
            narrow[index] = (char)actual[index];
        }
        narrow[index] = 0;
        fprintf(stderr, "%s: got '%s'\n", what, narrow);
        ++failures;
    }
}

int main()
{
    static_assert(sizeof(wchar_t) == 2, "Windows-sized wchar_t");
    wchar_t buffer[256];
    swprintf(buffer, L"%s has %d gold", L"Vi", 42);
    expect(buffer, L"Vi has 42 gold", "%s is wide");
    swprintf(buffer, L"[%S|%hs]", "ab", "cd");
    expect(buffer, L"[ab|cd]", "%S and %hs are narrow");
    swprintf(buffer, L"%-4s|%4s|%.1s", L"a", L"b", L"xyz");
    expect(buffer, L"a   |   b|x", "width and precision");
    swprintf(buffer, L"%05.1f %x %c %ld %%", 3.14159, 255, L'Z', -7L);
    expect(buffer, L"003.1 ff Z -7 %", "numbers");
    swprintf(buffer, L"%*d", 4, 9);
    expect(buffer, L"   9", "star width");
    wcscpy(buffer, L"one,two,,three");
    wchar_t* token = wcstok(buffer, L",");
    expect(token, L"one", "wcstok first");
    token = wcstok(0, L",");
    token = wcstok(0, L",");
    expect(token, L"three", "wcstok skips empty");
    if (_wcsnicmp(L"Hello", L"hELLo world", 5) != 0 || wcslen(L"four") != 4 ||
        wcscspn(L"abc;d", L";") != 3) {
        fprintf(stderr, "comparison helpers\n");
        ++failures;
    }
    char text[] = "MiXed";
    if (strcmp(_strupr(text), "MIXED") != 0) {
        fprintf(stderr, "_strupr\n");
        ++failures;
    }
    if (failures == 0) {
        printf("ok\n");
    }
    return failures;
}
