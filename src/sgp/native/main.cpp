#include "platform_events.h"
#include "sgp.h"
#include "wiz8/local_code/Gameloop.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <exception>
#include <string>

int main(int argc, char** argv)
{
    int result = 1;
    try
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            fprintf(stderr, "SDL initialization: %s\n", SDL_GetError());
            return 1;
        }
        std::string command_line;
        for (int i = 1; i < argc; ++i)
        {
            if (i != 1)
                command_line += ' ';
            command_line += argv[i];
        }
        ProcessCommandLine(command_line.data());
        if (InitializeStandardGamingPlatform(nullptr, 9))
        {
            gfApplicationActive = TRUE;
            gfProgramIsRunning = TRUE;
            MSG message{};
            result = 0;
            while (gfProgramIsRunning)
            {
                if (W8PeekMessage(&message, nullptr, 0, 0, PM_NOREMOVE))
                {
                    BOOL received = W8GetMessage(&message, nullptr, 0, 0);
                    if (received <= 0)
                    {
                        result = received < 0 ? 1 : int(message.wParam);
                        break;
                    }
                    W8TranslateMessage(&message);
                    W8DispatchMessage(&message);
                }
                else if (!gfApplicationActive)
                    W8WaitMessage();
                else
                {
                    GameLoop();
                    gfSGPInputReceived = FALSE;
                }
            }
        }
        else
        {
            fprintf(stderr, "Wizardry initialization failed: %s\n", gzErrorMsg);
        }
    }
    catch (const std::exception& failure)
    {
        fprintf(stderr, "Wizardry native platform: %s\n", failure.what());
    }
    SGPExit();
    SDL_Quit();
    return result;
}
