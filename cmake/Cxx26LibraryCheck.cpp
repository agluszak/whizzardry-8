// The C++26 library facilities the game requires. CMake compiles this at configuration
// (WIZ8_CXX26_LIBRARY), and the Platforms workflow compiles it with each platform's
// toolchain, once per facility (WIZ8_CHECK_FUNCTION_REF or WIZ8_CHECK_INPLACE_VECTOR
// alone), so a report names each one a library lacks.
#if !defined(WIZ8_CHECK_FUNCTION_REF) && !defined(WIZ8_CHECK_INPLACE_VECTOR)
#define WIZ8_CHECK_FUNCTION_REF
#define WIZ8_CHECK_INPLACE_VECTOR
#endif

#ifdef WIZ8_CHECK_FUNCTION_REF
#include <functional>
#if __cpp_lib_function_ref < 202306L
#error std::function_ref is unavailable
#endif
#endif
#ifdef WIZ8_CHECK_INPLACE_VECTOR
#include <inplace_vector>
#if __cpp_lib_inplace_vector < 202406L
#error std::inplace_vector is unavailable
#endif
#endif

int main()
{
    int result = 0;
#ifdef WIZ8_CHECK_INPLACE_VECTOR
    std::inplace_vector<int, 8> slots;
    slots.push_back(1);
    result += slots.front() - 1;
#endif
#ifdef WIZ8_CHECK_FUNCTION_REF
    int calls = 0;
    auto visit = [&] { ++calls; };
    std::function_ref<void()> callback = visit;
    callback();
    result += calls - 1;
#endif
    return result;
}
