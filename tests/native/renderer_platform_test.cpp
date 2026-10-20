#include <cstdio>

#include <catch2/catch_test_macros.hpp>

#include "wiz8/render/gpu_renderer.h"

TEST_CASE("renderer platform")
{
    for (int cycle = 0; cycle < 3; ++cycle) {
        {
            GpuRenderer gerd;
            REQUIRE(gerd.lockBuffer() == nullptr);
            REQUIRE(gerd.getError() == GpuRenderer::ERROR_WINDOW_NOT_OPEN);
            auto* renderer = &gerd.batchRenderer(false);
            renderer->submit();
            auto statistics = renderer->getStatistics();
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
        REQUIRE(GpuRenderer::getActive() == nullptr);
    }

    puts("ok: renderer submission and independent resource lifetimes");
}
