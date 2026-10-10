#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srGERD.h"
#include "surrender/srTimer.h"

#include <chrono>
#include <cstdio>
#include <thread>

#define CHECK(expression)                                                                          \
    do                                                                                            \
    {                                                                                             \
        if (!(expression))                                                                        \
        {                                                                                         \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                              \
            return 1;                                                                             \
        }                                                                                         \
    } while (0)

int main()
{
    srTimer timer;
    srQuadWord frequency;
    timer.getFreq(frequency);
    CHECK(frequency.lo == 1000000 && frequency.hi == 0);
    CHECK(timer.getIdent() && timer.getOsIdent());
    const double before = timer.getTime(srTimer::TIMER_READ_DEFAULT);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(timer.getTime(srTimer::TIMER_READ_DEFAULT) > before);
    timer.setUnits(1000);
    CHECK(timer.getUnits() == 1000);

    for (int cycle = 0; cycle < 3; ++cycle) {
        CHECK(srInit() && srCore.isInitialized());
        {
            srGERD gerd(srCreateSDLGPUDevice(), "SDLGPU");
            auto* renderer = gerd.lockRenderer();
            gerd.unlockRenderer(renderer, 1);
            w8_ulong statistics[7];
            renderer->getStatistics(statistics);
            CHECK(statistics[4] == 1);

            bool reused = false;
            std::jthread worker([&] {
                auto* acquired = gerd.lockRenderer();
                reused = acquired == renderer;
                gerd.unlockRenderer(acquired, 1);
                gerd.flushImmediateRenderers();
                gerd.flushRenderers();
            });
            worker.join();
            CHECK(reused);
            renderer->getStatistics(statistics);
            CHECK(statistics[4] == 1);

            gerd.flushImmediateRenderers();
            renderer->getStatistics(statistics);
            CHECK(statistics[4] == 2);
            gerd.flushRenderers();
            renderer->getStatistics(statistics);
            CHECK(statistics[4] == 3);
        }
        CHECK(srGERD::getFirst() == nullptr);
        CHECK(srExit() && !srCore.isInitialized());
        CHECK(srExit());
    }

    puts("ok: renderer clock, owner-thread submission and core reinitialization");
}
