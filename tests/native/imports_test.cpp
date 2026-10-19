#include <cassert>
#include <cfenv>
#include <cstdio>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include "wiz8/math.h"
#include "wiz8/platform/assertion.h"
/* Native consumer-side linking and lifetime for the renderer contracts used
   by the game, including compiler-generated members imported on Windows. */
#include "compat/compiler.h"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/surface_storage.h"

class Mesh;

// Instances, including copies, share ownership of their model.
static int shared_model_lifetimes()
{
    auto model = std::make_shared<Mesh>(0, 0);
    std::weak_ptr<Mesh> lifetime = model;
    auto* first = new MeshInstance(nullptr);
    first->setModel(std::move(model));
    auto* copy = new MeshInstance(nullptr);
    *copy = *first;
    REQUIRE(copy->getModel() == first->getModel());
    delete first;
    REQUIRE(!lifetime.expired());
    delete copy;
    REQUIRE(lifetime.expired());
    return 0;
}

// Maps share their SDL source surface.
static int shared_surface_lifetimes()
{
    std::weak_ptr<SDL_Surface> surface_lifetime;
    {
        auto surface = std::shared_ptr<SDL_Surface>(
            CreateCpuStorage(2, 2, SDL_PIXELFORMAT_INDEX8).release(), SDL_DestroySurface);
        surface_lifetime = surface;
        Texture first(surface), second;
        second = first;
        surface.reset();
        first.setSurfacePtr(nullptr);
        REQUIRE(!surface_lifetime.expired());
        REQUIRE(second.getSurfacePtr()->w == 2);
        second.setSurfacePtr(nullptr);
        REQUIRE(surface_lifetime.expired());
    }
    return 0;
}

TEST_CASE("imports_test")
{
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        REQUIRE(fesetround(mode) == 0);
        const double inputs[] = {0.0,
                                 1.5,
                                 -1.5,
                                 2147483647.0,
                                 -2147483648.0,
                                 2147483648.0,
                                 -2147483649.0,
                                 4294967296.0,
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()};
        for (volatile double input : inputs) {
            feclearexcept(FE_ALL_EXCEPT);
            const w8_long actual = roundedInt32(input);
            const bool invalid = fetestexcept(FE_INVALID);
#if defined(__i386__) || defined(__x86_64__)
            w8_long retail;
            unsigned short saved_control, status;
            asm volatile("fnstcw %0" : "=m"(saved_control));
            const unsigned rounding = mode == FE_DOWNWARD     ? 1
                                      : mode == FE_UPWARD     ? 2
                                      : mode == FE_TOWARDZERO ? 3
                                                              : 0;
            const unsigned short control = (saved_control & ~0x0c00u) | (rounding << 10);
            // The Windows x64 CRT controls SSE, not the oracle's x87 state.
            asm volatile("fldcw %0; fnclex" : : "m"(control));
            asm volatile("fldl %2; fistpl %0; fnstsw %1"
                         : "=m"(retail), "=m"(status)
                         : "m"(input)
                         : "st");
            asm volatile("fnclex; fldcw %0" : : "m"(saved_control));
            REQUIRE((actual == retail && invalid == bool(status & 1)));
#else
            if (!std::isfinite(input) || input >= 2147483648.0 || input < -2147483648.0) {
                REQUIRE((actual == (-2147483647 - 1) && invalid));
            }
#endif
        }
    }
    REQUIRE(fesetround(FE_TONEAREST) == 0);
    REQUIRE(roundedInt32(std::numeric_limits<float>::quiet_NaN()) == (-2147483647 - 1));
    REQUIRE(shared_surface_lifetimes() == 0);
    REQUIRE(shared_model_lifetimes() == 0);
    {
        Camera3D source, copy;
        source.setViewPlane(2, 3);
        copy = source;
        const auto before = source.getViewPlane();
        const auto after = copy.getViewPlane();
        REQUIRE((before.rectangle.left == after.rectangle.left &&
                 before.rectangle.top == after.rectangle.top && before.distance == after.distance));
        const auto normalized = copy.getNormalizedViewPlane();
        REQUIRE((normalized.left == after.rectangle.left / after.distance &&
                 normalized.bottom == after.rectangle.bottom / after.distance &&
                 normalized.right == after.rectangle.right / after.distance &&
                 normalized.top == after.rectangle.top / after.distance));
        copy.setClipRange(1000, 10);
        const auto [near_clip, far_clip] = copy.getClipRange();
        REQUIRE((near_clip == 10 && far_clip == 1000));
        copy.setEnvironmentRange(3, 9);
        const auto [near_range, far_range] = copy.getEnvironmentRange();
        REQUIRE((near_range == 3 && far_range == 9));
        copy.setEnvironmentScale(0.5f, 2);
        const auto [near_scale, far_scale] = copy.getEnvironmentScale();
        REQUIRE((near_scale == 0.5f && far_scale == 2));
        ClipPlane plane, plane_copy;
        glm::vec4 plane_value;
        plane_value.x = 1;
        plane_value.y = 2;
        plane_value.z = 3;
        plane_value.w = 4;
        plane.setClipPlane(plane_value);
        plane_copy = plane;
        REQUIRE(plane_copy.getClipPlane().w == 4);
        FogParams fog, fog_copy;
        fog.setDensity(0.25f);
        fog_copy = fog;
        REQUIRE(fog_copy.getDensity() == 0.25f);
        Material material, material_copy;
        glm::vec4 emissive;
        emissive.x = 0.25f;
        emissive.y = 0.5f;
        emissive.z = 0.75f;
        emissive.w = 1;
        material.setEmissive(emissive);
        material_copy.Material::operator=(material);
        REQUIRE(material_copy.getEmissive().z == 0.75f);
    }
    const auto location = std::source_location::current();
    try {
        Fail("native probe", fmt::format("value {}", 7));
        FAIL("assertion must throw");
    } catch (const std::runtime_error& error) {
        const std::string diagnostic = error.what();
        REQUIRE(diagnostic.find(location.file_name()) != std::string::npos);
        REQUIRE(diagnostic.find(std::to_string(location.line() + 2)) != std::string::npos);
        REQUIRE(diagnostic.find("native probe\nvalue 7") != std::string::npos);
    }
    puts("ok: native renderer consumer imports, generated special members and assertion "
         "diagnostics");
}
