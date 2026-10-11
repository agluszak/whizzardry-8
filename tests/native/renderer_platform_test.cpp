#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <thread>

#include "surrender/srClock.h"
#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srGERD.h"
#include "compat/compiler.h"


TEST_CASE("renderer platform")
{
    srClock timer;
    const double before = timer.seconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(timer.seconds() > before);
    REQUIRE(timer.milliseconds() >= 1);
    REQUIRE(timer.ticks(10000) >= 10);

    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE((srInit() && srCore.isInitialized()));
        {
            srGERD gerd(srCreateSDLGPUDevice(), "SDLGPU");
            auto* renderer = gerd.lockRenderer();
            gerd.unlockRenderer(renderer, 1);
            w8_ulong statistics[7];
            renderer->getStatistics(statistics);
            REQUIRE(statistics[4] == 1);

            bool reused = false;
            std::jthread worker([&] {
                auto* acquired = gerd.lockRenderer();
                reused = acquired == renderer;
                gerd.unlockRenderer(acquired, 1);
                gerd.flushImmediateRenderers();
                gerd.flushRenderers();
            });
            worker.join();
            REQUIRE(reused);
            renderer->getStatistics(statistics);
            REQUIRE(statistics[4] == 1);

            gerd.flushImmediateRenderers();
            renderer->getStatistics(statistics);
            REQUIRE(statistics[4] == 2);
            gerd.flushRenderers();
            renderer->getStatistics(statistics);
            REQUIRE(statistics[4] == 3);
        }
        REQUIRE(srGERD::getFirst() == nullptr);
        REQUIRE((srExit() && !srCore.isInitialized()));
        REQUIRE(srExit());
    }

    puts("ok: renderer clock, owner-thread submission and core reinitialization");

}
