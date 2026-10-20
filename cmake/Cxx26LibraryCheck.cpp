// The C++26 library facilities the game requires. CMake compiles this at configuration
// (WIZ8_CXX26_LIBRARY), and the Platforms workflow compiles it with each platform's
// toolchain.
#include <functional>
#include <inplace_vector>
#if __cpp_lib_function_ref < 202306L || __cpp_lib_inplace_vector < 202406L
#error Required C++26 library facilities are unavailable
#endif
int main()
{
    std::inplace_vector<int, 8> slots;
    auto visit = [&] { slots.push_back(1); };
    std::function_ref<void()> callback = visit;
    callback();
    return slots.front() - 1;
}
