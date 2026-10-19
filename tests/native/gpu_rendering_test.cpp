#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "compat/compiler.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/vertex_stream.h"

namespace {
vertexStream::VertexArrayInfo float_vertex_arrays(std::uint32_t mask)
{
    constexpr auto float_type = vertexStream::TYPE_FLOAT;
    return {.mask = mask,
            .count = 0,
            .clip = 0,
            .components = {},
            .types = {float_type, float_type, float_type, float_type, float_type, float_type}};
}

struct Fixture {
    std::unique_ptr<GpuRenderer> device;
    Fixture()
    {
        SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        device = std::make_unique<GpuRenderer>();
        REQUIRE(device->gpuCreateContext(nullptr));
        REQUIRE(device->gpuOpenWindow(64, 64));
        device->gpuSetProjectionMatrix(glm::mat4(1));
        device->gpuSetClearValues({{0, 0, 0, 1}, 1, 0});
        device->gpuClearBuffers(GpuRenderer::GPU_BUFFER_COLOR | GpuRenderer::GPU_BUFFER_DEPTH);
    }
    ~Fixture()
    {
        device.reset();
        SDL_Quit();
    }
    void triangle(float depth, const std::array<float, 4>& color, float u = 0, float v = 0,
                  float weight = 1)
    {
        const float positions[]{-1, -1, depth, 1, 3, -1, depth, 1, -1, 3, depth, 1};
        const float uv[]{u, v, 1, u, v, 1, u, v, 1};
        const std::array colors{color, color, color};
        const std::array weights{weight, weight, weight};
        auto arrays = float_vertex_arrays((1 << vertexStream::VERTEX_ARRAY_POSITIONS) |
                                          (1 << vertexStream::VERTEX_ARRAY_DIFFUSE) |
                                          (1 << vertexStream::VERTEX_ARRAY_TEXCOORD0));
        arrays.components[0] = arrays.components[1] = 4;
        arrays.components[4] = 3;
        arrays.arrays[0] = positions;
        arrays.arrays[1] = colors.data();
        arrays.arrays[4] = uv;
        arrays.texture_weights[0] = weights.data();
        device->gpuSetVertexArrayInfo(&arrays);
        device->gpuDrawArrays(vertexStream::PRIMITIVE_TRIANGLES, 0, 3);
    }
    unsigned pixel(int x = 32, int y = 32)
    {
        unsigned pixel = 0;
        auto* surface = device->gpuLockBuffer();
        std::memcpy(&pixel,
                    static_cast<unsigned char*>(surface->pixels) + y * surface->pitch + x * 4, 4);
        device->gpuUnlockBuffer();
        return pixel & 0xffffff;
    }
};
}

TEST_CASE("World viewport clears preserve surrounding UI and unselected buffers")
{
    Fixture fixture;
    DrawShader shader;
    shader.value = DrawShader::PASS_LESS | DrawShader::MASK_DEPTH_WRITE |
                   DrawShader::MASK_COLOR_WRITE |
                   (DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT);
    fixture.device->gpuSetShader(shader);
    fixture.triangle(0, {1, 0, 0, 1});
    fixture.device->gpuSetScissor({16, 16, 48, 48});
    fixture.device->gpuSetViewPort({0, 0, 1, 1, 0.2f, 0.3f});
    fixture.device->gpuSetClearValues({{0, 1, 0, 1}, 1, 0});
    fixture.device->gpuClearBuffers(GpuRenderer::GPU_BUFFER_COLOR);
    // State can change again before the queued clear is submitted.
    fixture.device->gpuSetScissor({0, 0, 64, 64});
    fixture.device->gpuSetViewPort({0, 0, 64, 64, 0, 1});
    REQUIRE(fixture.pixel() == 0x00ff00);
    REQUIRE(fixture.pixel(8, 32) == 0xff0000);
    REQUIRE(fixture.pixel(48, 32) == 0xff0000);
    fixture.triangle(0.5f, {0, 0, 1, 1});
    REQUIRE(fixture.pixel() == 0x00ff00); // Color-only clear retained depth.
    fixture.device->gpuSetScissor({16, 16, 48, 48});
    fixture.device->gpuClearBuffers(GpuRenderer::GPU_BUFFER_DEPTH);
    fixture.device->gpuSetScissor({0, 0, 64, 64});
    REQUIRE(fixture.pixel() == 0x00ff00); // Depth-only clear retained color.
    fixture.triangle(0.5f, {0, 0, 1, 1});
    REQUIRE(fixture.pixel() == 0x0000ff);
    REQUIRE(fixture.pixel(8, 32) == 0xff0000);
    fixture.device->gpuSetScissor({32, 32, 32, 48});
    fixture.device->gpuClearBuffers(GpuRenderer::GPU_BUFFER_COLOR | GpuRenderer::GPU_BUFFER_DEPTH);
    fixture.triangle(-0.5f, {1, 1, 1, 1});
    REQUIRE(fixture.pixel() == 0x0000ff); // An empty scissor affects no pixels.
}

TEST_CASE("Queued coplanar overlays preserve depth bias and its restoration")
{
    Fixture fixture;
    DrawShader shader;
    shader.value = DrawShader::PASS_LESS | DrawShader::MASK_DEPTH_WRITE |
                   DrawShader::MASK_COLOR_WRITE |
                   (DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT);
    fixture.device->gpuSetShader(shader);
    fixture.triangle(0, {1, 0, 0, 1});
    fixture.device->gpuSetPolygonOffset(2);
    fixture.triangle(0, {0, 1, 0, 1});
    fixture.device->gpuSetPolygonOffset(0);
    fixture.triangle(0, {0, 0, 1, 1});
    REQUIRE(fixture.pixel() == 0x00ff00);
}

TEST_CASE("Queued texture draws preserve independent clamp and repeat samplers")
{
    Fixture fixture;
    std::array<unsigned, 4> pixels{0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
    auto texture = fixture.device->gpuCreateTexture(2, 2, 1);
    fixture.device->gpuUploadTexture(
        texture, 0, {0, 0, 2, 2},
        {reinterpret_cast<const unsigned char*>(pixels.data()), sizeof(pixels)}, 8);
    fixture.device->gpuBindTexture(0, texture);
    DrawShader shader;
    shader.value = DrawShader::PASS_ALWAYS | DrawShader::MASK_COLOR_WRITE |
                   DrawShader::MASK_TEXTURING |
                   (DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT);
    fixture.device->gpuSetShader(shader);
    // U clamps to the green column, V repeats into the top row.
    fixture.device->gpuSetTextureParameters(0,
                                            {.wrap_s = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE});
    fixture.triangle(0, {1, 1, 1, 1}, 1.25f, 1.25f);
    // Change the live state before queued commands are replayed.
    fixture.device->gpuSetTextureParameters(0,
                                            {.wrap_t = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE});
    REQUIRE(fixture.pixel() == 0x00ff00);
    fixture.triangle(0, {1, 1, 1, 1}, 1.25f, 1.25f);
    REQUIRE(fixture.pixel() == 0x0000ff);
    // Separate Q weights must survive submission and perspective texture division.
    fixture.triangle(0, {1, 1, 1, 1}, 0.75f, 0.25f, 2.0f);
    REQUIRE(fixture.pixel() == 0xff0000);
    fixture.device->gpuDeleteTexture(texture);
}

TEST_CASE("Texture uploads copy pitched regions before CPU storage changes and defer deletion")
{
    Fixture fixture;
    auto texture = fixture.device->gpuCreateTexture(4, 4, 1);
    std::array<unsigned, 24> pixels;
    pixels.fill(0xffff0000);
    fixture.device->gpuUploadTexture(
        texture, 0, {0, 0, 4, 4},
        {reinterpret_cast<const unsigned char*>(pixels.data()), sizeof(pixels)}, 24);
    pixels.fill(0xff0000ff);
    pixels[7] = pixels[8] = pixels[13] = pixels[14] = 0xff00ff00;
    fixture.device->gpuUploadTexture(
        texture, 0, {1, 1, 2, 2},
        {reinterpret_cast<const unsigned char*>(pixels.data()), sizeof(pixels)}, 24);
    pixels.fill(0xff0000ff);
    fixture.device->gpuBindTexture(0, texture);
    fixture.device->gpuSetTextureParameters(0, {});
    DrawShader shader;
    shader.value = DrawShader::PASS_ALWAYS | DrawShader::MASK_COLOR_WRITE |
                   DrawShader::MASK_TEXTURING |
                   (DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT);
    fixture.device->gpuSetShader(shader);
    fixture.triangle(0, {1, 1, 1, 1}, 0.125f, 0.125f);
    REQUIRE(fixture.pixel() == 0xff0000);
    fixture.triangle(0, {1, 1, 1, 1}, 0.375f, 0.375f);
    fixture.device->gpuDeleteTexture(texture);
    REQUIRE(fixture.pixel() == 0x00ff00);
}

TEST_CASE("SDL framebuffer edits upload only when explicitly marked modified")
{
    Fixture fixture;
    auto* surface = fixture.device->gpuLockBuffer();
    SDL_Rect pixel{32, 32, 1, 1};
    REQUIRE(SDL_FillSurfaceRect(surface, &pixel, 0xffffffff));
    fixture.device->gpuUnlockBuffer(true);
    REQUIRE(fixture.pixel() == 0xffffff);
}

TEST_CASE("Indexed draws reuse corners and snapshot mutable source streams")
{
    Fixture fixture;
    const std::array<std::array<float, 4>, 4> positions{
        {{-1, -1, 0, 1}, {-1, 1, 0, 1}, {1, -1, 0, 1}, {1, 1, 0, 1}}};
    std::array<std::array<float, 4>, 4> colors;
    colors.fill({1, 0, 0, 1});
    const std::array<w8_ulong, 6> indices{0, 1, 2, 2, 1, 3};
    auto arrays = float_vertex_arrays((1 << vertexStream::VERTEX_ARRAY_POSITIONS) |
                                      (1 << vertexStream::VERTEX_ARRAY_DIFFUSE));
    arrays.components[0] = arrays.components[1] = 4;
    arrays.arrays[0] = positions.data();
    arrays.arrays[1] = colors.data();
    fixture.device->gpuSetVertexArrayInfo(&arrays);
    DrawShader shader;
    shader.value = DrawShader::PASS_ALWAYS | DrawShader::MASK_COLOR_WRITE |
                   (DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT);
    fixture.device->gpuSetShader(shader);
    fixture.device->gpuSetScissor({0, 0, 32, 64});
    fixture.device->gpuDrawElements(vertexStream::PRIMITIVE_TRIANGLES, indices);
    colors.fill({0, 0, 1, 1});
    fixture.device->gpuSetScissor({32, 0, 64, 64});
    fixture.device->gpuDrawElements(vertexStream::PRIMITIVE_TRIANGLES, indices);
    auto* surface = fixture.device->gpuLockBuffer();
    unsigned left = 0, right = 0;
    const auto* row = static_cast<const unsigned char*>(surface->pixels) + 32 * surface->pitch;
    std::memcpy(&left, row + 16 * 4, 4);
    std::memcpy(&right, row + 48 * 4, 4);
    fixture.device->gpuUnlockBuffer();
    REQUIRE((left & 0xffffff) == 0xff0000);
    REQUIRE((right & 0xffffff) == 0x0000ff);
}

TEST_CASE("Texture updates preserve pixels sampled by earlier queued draws")
{
    Fixture fixture;
    auto texture = fixture.device->gpuCreateTexture(1, 1, 1);
    const auto update = [&](unsigned pixel) {
        fixture.device->gpuUploadTexture(
            texture, 0, {0, 0, 1, 1},
            {reinterpret_cast<const unsigned char*>(&pixel), sizeof(pixel)}, sizeof(pixel));
    };
    update(0xffff0000);
    fixture.device->gpuBindTexture(0, texture);
    DrawShader shader;
    shader.value = DrawShader::PASS_ALWAYS | DrawShader::MASK_COLOR_WRITE |
                   DrawShader::MASK_TEXTURING |
                   (DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT);
    fixture.device->gpuSetShader(shader);
    fixture.device->gpuSetScissor({0, 0, 32, 64});
    fixture.triangle(0, {1, 1, 1, 1});
    update(0xff00ff00);
    fixture.device->gpuSetScissor({32, 0, 64, 64});
    fixture.triangle(0, {1, 1, 1, 1});
    auto* surface = fixture.device->gpuLockBuffer();
    unsigned left = 0, right = 0;
    const auto* row = static_cast<const unsigned char*>(surface->pixels) + 32 * surface->pitch;
    std::memcpy(&left, row + 16 * 4, 4);
    std::memcpy(&right, row + 48 * 4, 4);
    fixture.device->gpuUnlockBuffer();
    REQUIRE((left & 0xffffff) == 0xff0000);
    REQUIRE((right & 0xffffff) == 0x00ff00);
    fixture.device->gpuDeleteTexture(texture);
}

TEST_CASE("GPU mipmaps follow base uploads and updates before minified draws")
{
    Fixture fixture;
    auto texture = fixture.device->gpuCreateTexture(4, 4, 3);
    std::array<unsigned, 16> pixels;
    for (unsigned i = 0; i < pixels.size(); ++i) pixels[i] = i % 2 ? 0xffff0000 : 0xff0000ff;
    const auto update = [&] {
        fixture.device->gpuUploadTexture(
            texture, 0, {0, 0, 4, 4},
            {reinterpret_cast<const unsigned char*>(pixels.data()), sizeof(pixels)}, 16);
    };
    update();
    fixture.device->gpuBindTexture(0, texture);
    fixture.device->gpuSetTextureParameters(0, {.max_lod = 2});
    DrawShader shader;
    shader.value = DrawShader::PASS_ALWAYS | DrawShader::MASK_COLOR_WRITE |
                   DrawShader::MASK_TEXTURING |
                   (DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT);
    fixture.device->gpuSetShader(shader);
    const float positions[]{-1, -1, 0, 1, 3, -1, 0, 1, -1, 3, 0, 1};
    const float uv[]{0, 0, 1, 64, 0, 1, 0, 64, 1};
    auto arrays = float_vertex_arrays((1 << vertexStream::VERTEX_ARRAY_POSITIONS) |
                                      (1 << vertexStream::VERTEX_ARRAY_TEXCOORD0));
    arrays.components[0] = 4;
    arrays.components[4] = 3;
    arrays.arrays[0] = positions;
    arrays.arrays[4] = uv;
    fixture.device->gpuSetVertexArrayInfo(&arrays);
    fixture.device->gpuSetScissor({0, 0, 32, 64});
    fixture.device->gpuDrawArrays(vertexStream::PRIMITIVE_TRIANGLES, 0, 3);
    pixels.fill(0xff00ff00);
    update();
    fixture.device->gpuSetScissor({32, 0, 64, 64});
    fixture.device->gpuDrawArrays(vertexStream::PRIMITIVE_TRIANGLES, 0, 3);
    const auto purple = fixture.pixel(16, 32);
    REQUIRE(((purple >> 16) & 255) >= 127);
    REQUIRE(((purple >> 16) & 255) <= 128);
    REQUIRE((purple & 255) >= 127);
    REQUIRE((purple & 255) <= 128);
    REQUIRE((purple & 0xff00) == 0);
    REQUIRE(fixture.pixel(48, 32) == 0x00ff00);
    fixture.device->gpuDeleteTexture(texture);
}
