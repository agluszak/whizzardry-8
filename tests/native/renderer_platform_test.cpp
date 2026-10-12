#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <string_view>
#include <thread>
#include "glm/ext/vector_float3.hpp"

#include "surrender/srClock.h"
#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srDD.h"
#include "surrender/srGERD.h"
#include "surrender/srBoundingSphere.h"


TEST_CASE("renderer platform")
{
    srClock timer;
    const double before = timer.seconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(timer.seconds() > before);
    REQUIRE(timer.milliseconds() >= 1);
    REQUIRE(timer.ticks(10000) >= 10);

    for (std::int32_t cycle = 0; cycle < 3; ++cycle) {
        REQUIRE((srInit() && srCore.isInitialized()));
        {
            auto* device = srCreateSDLGPUDevice();
            srGERD gerd(device, "SDLGPU");
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
            const auto info = device->getInfo();
            REQUIRE(info.max_back_buffer_width == 8192);
            REQUIRE(info.max_back_buffer_height == 8192);
            REQUIRE(info.renderer_batch_limit == 0x100);
            REQUIRE(info.hardware_id == 1);
            REQUIRE(info.flags == 0);
            REQUIRE(info.unknown_08_ == 4);
            REQUIRE(info.unknown_0c_ == 0x10);
            REQUIRE(info.unknown_10_ == 1);
            REQUIRE(info.unknown_14_ == 65536);
            REQUIRE(info.unknown_20_ == 0x3b808081);
            REQUIRE(info.texture_ram == 0);
            REQUIRE(info.max_texture_stages == 2);
            REQUIRE(info.texture_min_dim == 1);
            REQUIRE(info.texture_max_dim == 4096);
            REQUIRE(info.texture_max_aspect == 4096);
            REQUIRE(std::string_view(info.text[0]) == "SDL GPU");
            REQUIRE(std::string_view(info.text[4]) == "Unknown");
            const auto driver = device->getDriverInfo();
            REQUIRE(driver.dd_api_version == SR_DD_MIN_API_VERSION);
            REQUIRE(driver.driver_id == 0);
            REQUIRE(std::string_view(driver.name) == "SDLGPU");
            REQUIRE(std::string_view(driver.api_name) == "SDL GPU");
            REQUIRE(device->getWindowList().empty());
            const auto formats = device->getTextureFormats();
            REQUIRE(formats.size() == 1);
            REQUIRE(formats[0].red_bits == 8);
            REQUIRE(formats[0].alpha_shift == 24);
            REQUIRE(device->getTextureFormats().data() == formats.data());
            REQUIRE(device->getBufferPixelFormat().red_shift == 16);
            const auto device_stats = device->getStatistics();
            REQUIRE(device_stats.texture_transfer == 0);
            REQUIRE(device_stats.pixels_drawn == 0);
            REQUIRE(device_stats.triangles_received == 0);
            REQUIRE(device_stats.vertices_transferred == 0);
            REQUIRE(device_stats.vertex_indices == 0);
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
        REQUIRE(srGERD::getFirst() == nullptr);
        REQUIRE((srExit() && !srCore.isInitialized()));
        REQUIRE(srExit());
    }

    puts("ok: renderer clock, owner-thread submission and core reinitialization");

}
