#include "surrender/srClock.h"
#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srGERD.h"

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
    srClock timer;
    const double before = timer.seconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(timer.seconds() > before);
    CHECK(timer.milliseconds() >= 1);
    CHECK(timer.ticks(10000) >= 10);

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
