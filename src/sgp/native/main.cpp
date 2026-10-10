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
            result = 0;
            while (gfProgramIsRunning)
            {
                PumpGameEvents(!gfApplicationActive);
                if (!gfProgramIsRunning)
                    break;
                if (gfApplicationActive)
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
