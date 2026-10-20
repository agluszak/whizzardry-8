#include <cstdint>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>

#include "glm/ext/vector_float4.hpp"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/texture.h"

static void checkConsumers()
{
    // Interning must distinguish both texture addresses, the shader word and
    // the pass's draw inputs, preserve index zero, and keep existing indices
    // across growth and clear.
    GpuRenderer::Renderer::TextureSetCache cache;
    Texture textures[2];
    GpuRenderer::Renderer::TextureSetKey key{nullptr, nullptr, {}};
    for (std::uint32_t i = 0; i < 512; ++i) {
        key.shader.value = i;
        REQUIRE(cache.intern(key) == i);
    }
    for (std::uint32_t i = 0; i < 512; ++i) {
        key.shader.value = i;
        REQUIRE(cache.intern(key) == i);
        REQUIRE(cache.sets[i].shader.value == i);
        const auto blend = (i >> DrawShader::DSTBLEND_SHIFT) & 7;
        REQUIRE((cache.sets[i].blend == (blend == 1                 ? 2
                                         : blend == 2 || blend == 3 ? 3
                                         : blend == 4 || blend == 5 ? 1
                                                                    : 0)));
    }
    key.shader.value = 0;
    key.texture0 = &textures[0];
    REQUIRE(cache.intern(key) == 512);
    key.texture0 = &textures[1];
    REQUIRE(cache.intern(key) == 513);
    key.texture0 = nullptr;
    key.texture1 = &textures[0];
    REQUIRE(cache.intern(key) == 514);
    REQUIRE(cache.sets.size() == 515);
    REQUIRE(cache.sets[512].texture0 == &textures[0]);
    REQUIRE(cache.sets[514].texture1 == &textures[0]);
    // Runs from passes with different fog or texture reduction never merge.
    key.fog_color = glm::vec4(0.5f);
    REQUIRE(cache.intern(key) == 515);
    key.texture_reduction = 1;
    REQUIRE(cache.intern(key) == 516);
    REQUIRE(
        (cache.sets[515].fog_color == glm::vec4(0.5f) && cache.sets[516].texture_reduction == 1));
    cache.clear();
    REQUIRE((cache.sets.empty() && cache.intern(key) == 0 && cache.intern(key) == 0));
    std::puts("ok: renderer texture-set interning");
}

TEST_CASE("hash_consumers_test")
{
    checkConsumers();
}
