#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <cstdio>
#include <memory>

namespace {
struct VideoSession {
    VideoSession()
    {
        // This isolated process owns every D-Bus user. SDL documents this hint
        // for leak tests; production must not shut down other libraries' buses.
        SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    }
    ~VideoSession() { SDL_Quit(); }
    VideoSession(const VideoSession&) = default;
    VideoSession& operator=(const VideoSession&) = default;
};
using Device = std::unique_ptr<SDL_GPUDevice, decltype(&SDL_DestroyGPUDevice)>;
Device CreateDevice()
{
    Device device{SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr),
                  SDL_DestroyGPUDevice};
    INFO(SDL_GetError());
    REQUIRE(device);
    const auto properties = SDL_GetGPUDeviceProperties(device.get());
    std::printf("GPU device: %s; driver: %s\n",
                SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"),
                SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "unknown"));
    return device;
}
}

TEST_CASE("SDL video startup and shutdown without GPU")
{
    VideoSession session;
}

TEST_CASE("SDL GPU device startup and shutdown without window")
{
    VideoSession session;
    auto device = CreateDevice();
}

TEST_CASE("SDL GPU window claim and teardown")
{
    VideoSession session;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        SDL_CreateWindow("SDL GPU lifecycle control", 640, 480, 0), SDL_DestroyWindow};
    REQUIRE(window);
    auto device = CreateDevice();
    INFO(SDL_GetError());
    REQUIRE(SDL_ClaimWindowForGPUDevice(device.get(), window.get()));
    SDL_ReleaseWindowFromGPUDevice(device.get(), window.get());
}
