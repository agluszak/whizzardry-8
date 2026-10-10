#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>
#include <cstdio>
TEST_CASE("gpu_lifecycle")
{    SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    auto* window = SDL_CreateWindow("SDL GPU lifecycle control", 640, 480, 0);
    auto* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    INFO(SDL_GetError());
    REQUIRE(window);
    REQUIRE(device);
    REQUIRE(SDL_ClaimWindowForGPUDevice(device, window));
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
