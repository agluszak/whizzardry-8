#include <SDL3/SDL.h>
#include <cstdio>
int main()
{
    SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;
    auto window = SDL_CreateWindow("SDL GPU lifecycle control", 640, 480, 0);
    auto device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
    if (!window || !device || !SDL_ClaimWindowForGPUDevice(device, window))
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
