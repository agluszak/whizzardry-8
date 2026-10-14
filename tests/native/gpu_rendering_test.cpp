#include <array>
#include <memory>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <catch2/catch_test_macros.hpp>

#include "glm/ext/matrix_float4x4.hpp"
#include "surrender/srDD.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srFlags.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"

namespace {
struct Fixture {
    std::unique_ptr<srDD> device;
    Fixture()
    {
        SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        device.reset(srCreateSDLGPUDevice());
        REQUIRE(device->createContext(0) == srDD::ERROR_NONE);
        srDD::OpenResult result{};
        REQUIRE(device->openWindow({64, 64, 0}, result) == srDD::ERROR_NONE);
        device->setProjectionMatrix(glm::mat4(1), srRendererDefs::MATRIX_TYPE_IDENTITY);
        device->setClearValues({{0, 0, 0, 1}, {}, 1, 0});
        device->clearBuffers(srFlags<srDD::e_buffer>(srDD::BUFFER_COLOR | srDD::BUFFER_DEPTH));
    }
    ~Fixture()
    {
        device.reset();
        SDL_Quit();
    }
    void triangle(float depth, const std::array<float, 4>& color, float u = 0, float v = 0)
    {
        const float positions[]{-1, -1, depth, 1, 3, -1, depth, 1, -1, 3, depth, 1};
        const float uv[]{u, v, 1, u, v, 1, u, v, 1};
        const std::array colors{color, color, color};
        srRendererDefs::VertexArrayInfo arrays{};
        arrays.mask.value = (1 << srRendererDefs::VERTEX_ARRAY_POSITIONS) |
                            (1 << srRendererDefs::VERTEX_ARRAY_DIFFUSE) |
                            (1 << srRendererDefs::VERTEX_ARRAY_TEXCOORD0);
        arrays.components[0] = arrays.components[1] = 4;
        arrays.components[4] = 3;
        arrays.arrays[0] = positions;
        arrays.arrays[1] = colors.data();
        arrays.arrays[4] = uv;
        device->setVertexArrayInfo(&arrays);
        device->drawArrays(srRendererDefs::PRIMITIVE_TRIANGLES, 0, 3);
    }
    unsigned pixel()
    {
        unsigned pixel = 0;
        REQUIRE(device->bufferOp({0, 0, nullptr, 0, 0, 0}) == srDD::ERROR_NONE);
        srDD::BufferCommand read{0, 2, &pixel, 32, 32, 1};
        REQUIRE(device->bufferOp(read) == srDD::ERROR_NONE);
        REQUIRE(device->bufferOp({0, 1, nullptr, 0, 0, 0}) == srDD::ERROR_NONE);
        return pixel & 0xffffff;
    }
};
}

TEST_CASE("Queued coplanar overlays preserve depth bias and its restoration")
{
    Fixture fixture;
    srShader shader;
    shader.value = unsigned(srShader::PASS_LESS) | srShader::MASK_DEPTH_WRITE |
                   srShader::MASK_COLOR_WRITE |
                   (srShader::SRCBLEND_ONE << srShader::SRCBLEND_SHIFT);
    fixture.device->setShader(shader);
    fixture.triangle(0, {1, 0, 0, 1});
    fixture.device->setPolygonOffset(2);
    fixture.triangle(0, {0, 1, 0, 1});
    fixture.device->setPolygonOffset(0);
    fixture.triangle(0, {0, 0, 1, 1});
    REQUIRE(fixture.pixel() == 0x00ff00);
}

TEST_CASE("Queued texture draws preserve independent clamp and repeat samplers")
{
    Fixture fixture;
    std::array<unsigned, 4> pixels{0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
    srDD::Texture texture{};
    texture.reset();
    texture.width = texture.height = 2;
    texture.levels[0] = pixels.data();
    texture.format = fixture.device->getBufferPixelFormat();
    fixture.device->texImage(texture, 0);
    fixture.device->bindTexture(0, texture);
    srShader shader;
    shader.value = unsigned(srShader::PASS_ALWAYS) | srShader::MASK_COLOR_WRITE |
                   srShader::MASK_TEXTURING | (srShader::SRCBLEND_ONE << srShader::SRCBLEND_SHIFT);
    fixture.device->setShader(shader);
    // U clamps to the green column, V repeats into the top row.
    fixture.device->setTextureParameters(0, {(1 << 4) | (1 << 6) | (1 << 10), 0});
    fixture.triangle(0, {1, 1, 1, 1}, 1.25f, 1.25f);
    // Change the live state before queued commands are replayed.
    fixture.device->setTextureParameters(0, {(1 << 4) | (1 << 6) | (1 << 12), 0});
    REQUIRE(fixture.pixel() == 0x00ff00);
    fixture.triangle(0, {1, 1, 1, 1}, 1.25f, 1.25f);
    REQUIRE(fixture.pixel() == 0x0000ff);
    fixture.device->deleteTexture(texture);
}
