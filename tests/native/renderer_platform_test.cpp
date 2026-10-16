#include <chrono>
#include <cstdio>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingSphere.h"
#include "surrender/srClock.h"
#include "surrender/srCore.h"
#include "surrender/srGERD.h"

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
            srGERD gerd;
            gerd.matrixMode(srGERD::MATRIX_MODELVIEW);
            gerd.loadIdentity();
            const auto sphere = gerd.eyeSpaceBounds(srBoundingSphere{{1, 2, 3}, 2});
            REQUIRE(sphere.center == glm::vec3(1, 2, 3));
            REQUIRE(sphere.radius == 2);
            gerd.translate(5, 6, 7);
            gerd.scale(2, -3, 4);
            const auto scaled = gerd.eyeSpaceBounds(srBoundingSphere{{1, 2, 3}, 2});
            REQUIRE(scaled.center == glm::vec3(7, 0, 19));
            REQUIRE(scaled.radius == 8);
            REQUIRE(gerd.eyeSpaceBounds(srBoundingSphere{{0, 0, 0}, 0}).radius == 0);
            REQUIRE(sphere.center == glm::vec3(1, 2, 3));
            REQUIRE(sphere.radius == 2);
            gerd.loadIdentity();
            REQUIRE(gerd.lockBuffer() == nullptr);
            REQUIRE(gerd.getError() == srGERD::ERROR_WINDOW_NOT_OPEN);
            auto* renderer = gerd.lockRenderer();
            gerd.unlockRenderer(renderer, 1);
            auto statistics = renderer->getStatistics();
            REQUIRE(statistics.device_calls == 1);

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
            statistics = renderer->getStatistics();
            REQUIRE(statistics.device_calls == 1);

            gerd.flushImmediateRenderers();
            statistics = renderer->getStatistics();
            REQUIRE(statistics.device_calls == 2);
            gerd.flushRenderers();
            statistics = renderer->getStatistics();
            REQUIRE(statistics.device_calls == 3);
            const auto snapshot = renderer->getStatistics();
            renderer->statistics.triangle_chunks = 5;
            renderer->statistics.input_triangles = 7;
            renderer->statistics.clipped_triangles = 2;
            renderer->statistics.input_vertices = 9;
            renderer->statistics.device_vertices = 11;
            renderer->statistics.device_vertex_indices = 13;
            renderer->sorted = 1;
            const auto combined = gerd.getStatistics();
            REQUIRE(combined.triangle_chunks == 5);
            REQUIRE(combined.input_triangles == 7);
            REQUIRE(combined.clipped_triangles == 2);
            REQUIRE(combined.input_vertices == 9);
            REQUIRE(combined.sorted_triangles == 13);
            REQUIRE(snapshot.triangle_chunks == 0);
            REQUIRE(snapshot.device_calls == 3);
            renderer->resetStatistics();
            REQUIRE(renderer->getStatistics().device_calls == 0);
            REQUIRE(renderer->getStatistics().device_vertex_indices == 0);
            REQUIRE(gerd.getStatistics().input_triangles == 0);
            REQUIRE(gerd.getStatistics().sorted_triangles == 0);
            REQUIRE(combined.input_triangles == 7);
        }
        REQUIRE(srGERD::getActive() == nullptr);
        REQUIRE((srExit() && !srCore.isInitialized()));
        REQUIRE(srExit());
    }

    puts("ok: renderer clock, owner-thread submission and core reinitialization");
}
