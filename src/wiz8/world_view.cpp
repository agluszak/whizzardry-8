#include "wiz8/world_view.h"

#include <chrono>
#include <optional>

#include <SDL3/SDL_surface.h>

#include "glm/common.hpp"
#include "glm/ext/vector_float2.hpp"
#include "wiz8/cursor.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/quality.h"
#include "wiz8/render/renderer.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/ui_input.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

SceneFrame GameSceneFrame(const Game* game)
{
    const auto* world = game ? game->level.primary.get() : nullptr;
    return {.environment_offset = world ? world->environment.offset : glm::vec3(0),
            .brightness = g_monster_light_scale,
            .texture_reduction = g_resident_texture_policy,
            .time = game ? std::chrono::duration<double>(game->visual_time).count()
                         : RenderClockSeconds()};
}

namespace {
FrameView PrepareWorldView(Game* game, const glm::vec3& offset)
{
    FrameView view{.frame = GameSceneFrame(game)};
    if (!game) return view;
    auto* sky = game->level.sky.get();
    if (auto* world = game->level.primary.get()) {
        placeWorldCameras(*world, sky, offset);
        view.world = world->scene.get();
        view.camera = world->camera;
    }
    if (sky) {
        view.sky = sky->scene.get();
        view.sky_camera = sky->camera;
    }
    return view;
}
}

std::optional<Pick> PresentGameFrame(Game* game, bool pick_world)
{
    if (!CanPresentFrame()) return std::nullopt;
    const auto offset = game ? game->camera_shakes.offset() : glm::vec3(0);
    auto view = PrepareWorldView(game, offset);
    if (view.world) {
        const auto& world = *game->level.primary;
        if (!world.environment.sky_enabled)
            GetWorldLightValue(&world, &view.background);
        else
            view.background = glm::clamp(view.world->getFogColor(), 0.0f, 1.0f);
    }
    if (!g_render_mesh_sky || !g_monster_shadow_updates_enabled) view.sky = nullptr;
    if (view.sky)
        view.sky->setFogColor(view.world ? glm::clamp(view.world->getFogColor(), 0.0f, 1.0f)
                                         : glm::vec3(0));
    if (!g_world_render_enabled) view.world = nullptr;
    SyncSystemCursor();
    UpdateUiHelp();
    std::optional<glm::vec2> pick;
    const int x = g_cursor_hotspot_x + g_cursor_width;
    const int y = g_cursor_hotspot_y + g_cursor_height;
    if (pick_world && view.world && x >= g_viewport.left && x <= g_viewport.right &&
        y >= g_viewport.top && y <= g_viewport.bottom) {
        const int half_width = (g_viewport.right - g_viewport.left) / 2;
        const int half_height = (g_viewport.bottom - g_viewport.top) / 2;
        pick = glm::vec2((x - half_width - g_viewport.left) / float(half_width),
                         -(y - half_height - g_viewport.top) / float(half_height));
    }
    return RenderFrame(view, pick);
}

bool CaptureGameView(Game& game, SDL_Surface& target, const DesignRect& bounds)
{
    if (!g_renderer || !g_renderer->isOpen() || !game.level.primary) return false;
    // Screenshots use a steady camera and preserve the sky's own fog.
    auto view = PrepareWorldView(&game, glm::vec3(0));
    const auto& world = *game.level.primary;
    if (world.environment.fog_enabled)
        view.background = world.environment.light_direction;
    else
        GetWorldLightValue(&world, &view.background);
    if (!g_render_mesh_sky) view.sky = nullptr;
    return RenderViewToSurface(target, view, bounds);
}

void SetWorldViewport(Game* game, const DesignRect& bounds)
{
    World* world = game ? game->level.primary.get() : nullptr;
    World* sky = game ? game->level.sky.get() : nullptr;
    float fractional_left;
    float fractional_top;
    float fractional_right;
    float fractional_bottom;
    Camera3D::Rect plane;

    g_viewport = bounds;
    fractional_left = bounds.left * g_viewport_x_scale;
    fractional_top = bounds.top * g_viewport_y_scale;
    fractional_right = g_viewport.right * g_viewport_x_scale;
    fractional_bottom = g_viewport.bottom * g_viewport_y_scale;

    if (world != 0 && world->camera != 0) {
        world->camera->setViewPlane(3.14159265358979323846 * 0.0055555557f * 85.0f,
                                    3.14159265358979323846 * 0.0055555557f * 71.0f);
        const auto view = world->camera->getViewPlane().rectangle;

        plane.left = fractional_left * (view.right - view.left) + view.left;
        plane.right = fractional_right * (view.right - view.left) + view.left;
        plane.bottom = (1.0 - fractional_bottom) * (view.top - view.bottom) + view.bottom;
        plane.top = (1.0 - fractional_top) * (view.top - view.bottom) + view.bottom;

        world->camera->setViewPlane(plane, 1.0);
        if (sky != 0) {
            sky->camera->setViewPlane(plane, 1.0);
        }
        if (world->party.camera.swayActive()) {
            world->party.camera.applySway(*world->camera, CameraSwayMode::Start);
        }
    }
}
