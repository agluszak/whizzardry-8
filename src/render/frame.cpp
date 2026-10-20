#include "wiz8/render/frame.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <ios>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <fmt/format.h>
#include <wiz8/filesystem.h>

#include "asset_paths.h"
#include "glm/common.hpp" // IWYU pragma: keep
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/cursor.h"
#include "wiz8/design_rect.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/timer.h"
#include "wiz8/platform/window.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/image_io.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/quality.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/startup_world.h"
#include "wiz8/surface_storage.h"
#include "wiz8/ui_input.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

void SaveJpegScreenshot(void);
static void FlushUiUpload();

/* The CPU frame buffer is the game-space UI canvas and g_ui_canvas's
   streaming texture mirrors it on the GPU. Invalidation accumulates one
   bounding rectangle, uploaded once per frame before composition. */
static SDL_Rect g_ui_upload{};

/*
 * The renderer window and extension loading gate the native application initializes after the
 * input manager.
 */

/* The released SGP video unit owns this platform handle.  Wiz8 replaces the
   released video manager but keeps the same source-defined interface. */
SDL_Window* ghWindow;

bool g_world_pick_enabled = true;
unsigned char g_fullscreen_scene_last = 1;
bool g_cursor_scene_enabled = true;
bool g_overlay_scenes_enabled = true;
unsigned char g_fullscreen = 1;
bool g_flush_pending = true;
int g_screen_width = 640;
int g_screen_height = 480;
MeshInstance* g_current_model_instance;
int g_fps_frame_count;
uint64_t g_fps_window_tick;
/* Per-page counters reset as each overlay page is retired; retail stores
   through g_active_page as an index, so this is one two-entry array. */
bool g_video_active;
bool g_world_blacked_out;
bool g_screenshot_pending;
bool g_video_inspector_enabled;
std::unique_ptr<GpuRenderer> g_gerd;
static std::shared_ptr<SDL_Surface> primarySurface;
SDLSurfaceOwner videoSurfaceOwner{nullptr, SDL_DestroySurface};

Compositor g_compositor;
std::shared_ptr<SDL_Surface> g_primary_color_surface;
std::unique_ptr<UiCanvasTexture> g_ui_canvas;
float g_texture_inset_pixels;
std::shared_ptr<SDL_Surface> g_mouse_surface;

int g_surface_opaque_shader_bits;
int g_surface_alpha_blend_shader_bits;
DesignRect g_viewport;
bool g_camera_sway_active;
extern const float g_viewport_x_scale = 1.0f / 640.0f;
extern const float g_viewport_y_scale = 1.0f / 480.0f;

int g_video_inspector_mode;
float g_frames_per_second;

std::unique_ptr<OverlayQuad> g_cursor_node;
uint64_t g_cursor_move_tick;
int g_cursor_width;
int g_cursor_height;
int g_cursor_image_width;
int g_cursor_image_height;
int g_cursor_hotspot_x;
int g_cursor_hotspot_y;

/* Reset both framebuffer pages and discard transient scene instances.
   Callers select whether to invalidate all dirty blocks or the normal region. */
static void ResetVideoDirtyState()
{
    g_ui_upload = {};
    InvalidateRegion(0, 0, 640, 480);
}

/* Clear the software-facing frame and retire every transient 2D overlay.
   The four scene walks are the same typed operation used during renderer
   bring-up; keeping the reset here avoids reproducing SurRender's node ABI at
   the menu call site. */
void ResetVideoFrameState(void)
{
    const auto& description = *primarySurface;
    memset(description.pixels, 0, description.pitch * 480);
    ResetVideoDirtyState();
}

/* Brings the renderer up, shows the window and, when the INSPECTOR switch was
   given, loads that extension from the DLL subdirectory before returning to the
   original working directory. Each gate that fails returns straight out with
   the callee's own false still in AL. */
bool InitializeVideoManager(void)
{
    g_world_pick_enabled = true;
    g_current_model_instance = 0;
    g_fps_frame_count = 0;
    g_fps_window_tick = MonotonicMilliseconds();
    if (!CreateWizardryWindow()) {
        return 0;
    }
    if (!InitializePrimarySurface()) {
        return 0;
    }
    if (!InitializeVideoDevice()) {
        return 0;
    }
    InitializeRendererSceneObjects();
    if (!g_video_active) {
        if (GameInProgress() && CurrentGame().advancement.held) {
            SyncWorldAdvancement();
        }
        if (ghWindow && g_gerd) {
            g_video_active = true;
            SyncWorldAdvancement();
            if (g_gerd->isWindowOpen() == 0) {
                if (!OpenRendererWindow()) {
                    goto done;
                }
            }
            SDL_ShowWindow(ghWindow);
            SDL_RestoreWindow(ghWindow);
            SDL_RaiseWindow(ghWindow);
            ResetVideoDirtyState();
        }
    }
done:
    SetViewport({0, 0, 0x281, 0x1e1});
    if (!InitializeStartupNavigation()) {
        return 0;
    }
    EnableAllRenderOptions();
    return InitializeMouseCursorScene();
}

void ShutdownVideoManager(void)
{
    FreeMouseCursor();
    ShutdownVideoScenes();
    ShutdownStartupNavigation();
    SuspendVideoManager();
    SDL_Window* native_window = ghWindow;
    ReleasePrimarySurface();
    videoSurfaceOwner.reset();
    if (ghWindow) {
        SDL_HideWindow(ghWindow);
        ghWindow = 0;
    }
    if (g_gerd) {
        g_flush_pending = false;
        g_gerd.reset();
    }
    DestroyGameWindow(native_window);
}

/* Releases the renderer scene graph and 2D objects created by the video
   startup sequence; runs from ShutdownVideoManager before the CPU surface
   teardown. */
void ShutdownVideoScenes(void)
{
    VideoRemoveToolTip();
    g_cursor_node.reset();
    g_compositor.clear();
    g_ui_canvas.reset();
    g_mouse_surface.reset();
    g_primary_color_surface.reset();
}

/* Selects the SDL packed format for SGP's 16-bit framebuffer and sprites.
   The startup configuration uses ARGB1555; the high bit is an opaque-pixel
   marker, while zero is the software blitters' transparent pixel. */

/* Registers and creates the real top-level Wizardry window.  The odd use of
   the horizontal screen metric for both axes is present in the retail body and
   is preserved here; AdjustWindowRect turns the four stored client bounds into
   the outer window rectangle before CreateWindowEx. */
unsigned char CreateWizardryWindow(void)
{
    ghWindow = CreateGameWindow(g_screen_width, g_screen_height, g_fullscreen != 0);
    return ghWindow != 0;
}

/* SDL owns the 640x480 CPU frame used as SurRender color output. */
void ReleasePrimarySurface(void)
{
    primarySurface.reset();
}

unsigned char InitializePrimarySurface(void)
{
    primarySurface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(640, 480, kFramePixelFormat).release(), SDL_DestroySurface);
    if (!primarySurface) return 0;
    SDL_SetSurfaceBlendMode(primarySurface.get(), SDL_BLENDMODE_NONE);
    return 1;
}

/* Starts the SDL GPU renderer using the dimensions from the legacy five-line
   3DVideo.CFG file. Its driver and depth lines are ignored; the final audio
   line still accepts "none" to disable sound. */
// The 3DSetup configuration's audio device line; "none" disables sound.
static bool s_setup_disables_audio = false;

bool SetupDisablesAudio()
{
    return s_setup_disables_audio;
}

unsigned char InitializeVideoDevice(void)
try {
    std::istringstream config;
    char audio_setting[100] = "";
    char line[10] = "";
    char* newline;

    if (g_gerd) {
        return 1;
    }

    try {
        auto input = wiz8::open_file(wiz8::user_path("config/3dvideo.cfg"));
        if (input->size() > 65536) throw std::runtime_error("oversized video configuration");
        std::string text(static_cast<std::size_t>(input->size()), '\0');
        if (input->read(text.data(), text.size()).bytes != text.size())
            throw std::runtime_error("truncated video configuration");
        config.str(text);
    } catch (const std::exception&) {
        config.setstate(std::ios::failbit);
    }
    if (config) {
        config.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Legacy driver.
        config.getline(line, sizeof(line));
        g_screen_width = atoi(line);
        config.getline(line, sizeof(line));
        g_screen_height = atoi(line);
        config.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Legacy depth.
        config.getline(audio_setting, sizeof(audio_setting));
        newline = strchr(audio_setting, '\r');
        if (newline) {
            *newline = '\0';
        }
        newline = strchr(audio_setting, '\n');
        if (newline) {
            *newline = '\0';
        }
    }

    g_gerd = std::make_unique<GpuRenderer>();

    if (g_gerd->createContext(ghWindow) != GpuRenderer::ERROR_NONE || !OpenRendererWindow()) {
        g_gerd.reset();
        return 0;
    }
    s_setup_disables_audio = SDL_strncasecmp(audio_setting, "none", 4) == 0;
    return 1;
} catch (const std::exception&) {
    return false;
}

/* Applies the configured window style, asks SurRender for the matching display
   mode in fullscreen operation, and opens the renderer output window. */
unsigned char OpenRendererWindow(void)
{
    if (!ConfigureGameWindow(ghWindow, g_fullscreen != 0, g_screen_width, g_screen_height))
        return 0;
    if (g_gerd->openWindow(g_screen_width, g_screen_height) != GpuRenderer::ERROR_NONE) return 0;
    g_flush_pending = true;
    return 1;
}

SDL_Surface* BeginVideoPresentation(void)
{
    const auto& source = *primarySurface;
    videoSurfaceOwner.reset(SDL_CreateSurface(source.w, source.h, source.format));
    g_flush_pending = false;
    return videoSurfaceOwner.get();
}

unsigned char FinishVideoPresentation(void)
{
    videoSurfaceOwner.reset();
    g_flush_pending = true;
    InvalidateRegion(0, 0, 640, 480);
    return 1;
}

/* WM_SIZE only rebuilds the SurRender output in windowed mode.  Full-screen
   startup receives the same Windows notification while the device already
   owns its configured 640x480 mode, so there is no resize operation to do. */
bool VideoResizeWindow(void)
{
    if (g_fullscreen || !ghWindow || !g_gerd || !g_flush_pending) {
        return 0;
    }
    g_flush_pending = false;
    g_gerd->closeWindow(static_cast<GpuRenderer::e_closeHint>(1));
    if (g_gerd->openWindow() == GpuRenderer::ERROR_WINDOW_OPEN_FAILED) {
        return 0;
    }
    ResetTransientRenderScenes();
    g_flush_pending = true;
    return 1;
}

void VideoFullScreen(bool enabled)
{
    g_fullscreen = enabled;
    if (ghWindow && g_gerd && g_flush_pending) {
        g_flush_pending = false;
        g_gerd->closeWindow(static_cast<GpuRenderer::e_closeHint>(1));
        OpenRendererWindow();
    }
}

void ResetTransientRenderScenes(void)
{
    ResetVideoDirtyState();
}

/* Same dirty-block / transient-scene reset as ResetTransientRenderScenes, but
   the full-screen invalidate uses flag 1 (the all-bits redraw path). */
void ClearVideoDirtyBlocks(void)
{
    ResetVideoDirtyState();
}

bool IsVideoActive()
{
    return g_video_active;
}

void SuspendVideoManager(void)
{
    if (g_video_active) {
        g_video_active = false;
        SyncWorldAdvancement();
        if (g_gerd) {
            g_flush_pending = false;
            g_gerd->closeWindow(static_cast<GpuRenderer::e_closeHint>(0));
        }
        SDL_MinimizeWindow(ghWindow);
        FreeMouseCursor();
    }
}

bool RestoreVideoManager(void)
{
    if (g_video_active) {
        return 1;
    }
    if (GameInProgress() && CurrentGame().advancement.held) {
        SyncWorldAdvancement();
    }
    if (ghWindow && g_gerd) {
        g_video_active = true;
        SyncWorldAdvancement();
        SDL_ShowWindow(ghWindow);
        SDL_RestoreWindow(ghWindow);
        if (g_gerd->isWindowOpen() != 0 || OpenRendererWindow()) {
            SDL_RaiseWindow(ghWindow);
            ResetVideoDirtyState();
        }
    }
    return 0;
}

unsigned char ClearPrimarySurface(void)
{
    SDL_FillSurfaceRect(primarySurface.get(), nullptr, 0);
    return 1;
}

/* The inputs every scene pass of the current frame shares. */
static SceneFrame CurrentSceneFrame()
{
    return {.environment_offset = CurrentEnvironmentOffset(),
            .brightness = g_monster_light_scale,
            .texture_reduction = g_resident_texture_policy,
            .time = RenderClockSeconds()};
}

/* Render one scene into either the full output or a logical 640x480 viewport.
   Scene fog follows the current world's static scene for ordinary overlays;
   the world passes retain their own fog by selecting preserve_fog. */

void RenderScene(WorldScene* scene, Camera3D* camera, const DesignRect* viewport, bool preserve_fog,
                 std::span<Pick> picks)
{
    std::uint32_t width = g_gerd->getWidth();
    std::uint32_t height = g_gerd->getHeight();

    if (scene == 0 || scene->renderableCount() == 0) {
        return;
    }
    if (viewport != 0) {
        const auto rect = ToSurfaceRect(*viewport, width, height);
        g_gerd->setViewPort(rect.x, rect.y, rect.w, rect.h);
        g_gerd->setScissor(rect.x, rect.y, rect.w, rect.h);
    }
    if (!preserve_fog) {
        const auto fog = GetWorld() ? GetWorld()->scene->getFogColor() : glm::vec3(0);
        // NOLINTNEXTLINE(misc-include-cleaner)
        scene->setFogColor(glm::clamp(fog, 0.0f, 1.0f));
    }
    scene->render(*g_gerd, camera, CurrentSceneFrame(), picks);
    if (viewport != 0) {
        g_gerd->setViewPort(0, 0, width, height);
        g_gerd->setScissor(0, 0, width, height);
    }
}

/* The central renderer frame transaction. It owns the page flip, every scene
   pass, optional world picking, transient-node retirement, and frame timing;
   callers do not reproduce any subset of that lifecycle. */
void RenderFrame(void)
{
    /* The movie adapter owns presentation until FinishVideoPresentation. */
    if (!g_flush_pending) {
        return;
    }
    glm::vec3 clear_color(0);
    glm::vec3 saved_world_position;
    glm::vec3 shifted_world_position;
    std::uint64_t now;
    float elapsed;
    float frames_per_second;

    if (!g_video_active) {
        return;
    }

    if (g_trigger_action_active && GetWorld() != 0) {
        saved_world_position = cameraLocation();
        shifted_world_position = saved_world_position + g_trigger_action_scene_offset;
        SetWorldScenePosition(GetWorld(), &shifted_world_position);
    }
    if (GetWorld() != 0) {
        if (!IsSkyEnabled()) {
            GetWorldLightValue(GetWorld(), &clear_color);
        } else {
            clear_color =
                // NOLINTNEXTLINE(misc-include-cleaner)
                glm::clamp(GetWorld()->scene->getFogColor(), 0.0f, 1.0f);
        }
    }

    const GpuRenderer::ClearValues clear_values{.color = glm::vec4(clear_color, 1.0f)};
    SyncSystemCursor();
    UpdateUiHelp();
    FlushUiUpload();
    if (g_gerd != 0) {
        g_gerd->resetStatistics();
    }

    g_gerd->beginFrame();
    UpdateRenderElapsedTime();

    if (!g_monster_shadow_updates_enabled) {
        if (g_world_blacked_out) {
            goto clear_viewport;
        }
    } else if (g_world_blacked_out || !g_render_mesh_sky || GetSecondaryWorld() == 0) {
    clear_viewport:
        {
            std::uint32_t height = g_gerd->getHeight();
            std::uint32_t width = g_gerd->getWidth();
            const auto rect = ToSurfaceRect(g_viewport, width, height);
            g_gerd->setScissor(rect.x, rect.y, rect.w, rect.h);
            g_gerd->clear(GpuRenderer::GPU_BUFFER_COLOR | GpuRenderer::GPU_BUFFER_DEPTH,
                          clear_values);
            g_gerd->setScissor(0, 0, width, height);
        }
    }

    g_compositor.draw(*g_gerd, OverlayLayer::PreWorld);

    if (g_render_mesh_sky && GetSecondaryWorld() != 0 && g_monster_shadow_updates_enabled) {
        RenderScene(GetSecondaryWorld()->scene.get(), GetSecondaryWorld()->camera, &g_viewport,
                    false);
    }
    if (GetWorld() != 0 && g_world_render_enabled) {
        if (!g_world_pick_enabled || g_cursor_hotspot_x + g_cursor_width < g_viewport.left ||
            g_cursor_hotspot_y + g_cursor_height < g_viewport.top ||
            g_viewport.right < g_cursor_hotspot_x + g_cursor_width ||
            g_viewport.bottom < g_cursor_hotspot_y + g_cursor_height) {
            RenderScene(GetWorld()->scene.get(), GetWorld()->camera, &g_viewport, true);
        } else {
            int half_width = (g_viewport.right - g_viewport.left) / 2;
            int half_height = (g_viewport.bottom - g_viewport.top) / 2;
            Pick pick;
            pick.position.x = (g_cursor_hotspot_x - half_width - g_viewport.left + g_cursor_width) /
                              static_cast<float>(half_width);
            pick.position.y = -static_cast<float>(g_cursor_hotspot_y - half_height -
                                                  g_viewport.top + g_cursor_height) /
                              half_height;
            RenderScene(GetWorld()->scene.get(), GetWorld()->camera, &g_viewport, true,
                        std::span{&pick, 1});
            g_current_model_instance = pick.selected_model;
            ResolvePickedProp();
        }
    }

    if (g_overlay_scenes_enabled) {
        if (!g_fullscreen_scene_last) g_compositor.draw(*g_gerd, OverlayLayer::Fullscreen);
        g_compositor.draw(*g_gerd, OverlayLayer::UnderUi);
        g_ui_canvas->draw(g_gerd.get());
        g_compositor.draw(*g_gerd, OverlayLayer::OverUi);
        g_compositor.draw(*g_gerd, OverlayLayer::SquareAspect);
        if (g_fullscreen_scene_last) g_compositor.draw(*g_gerd, OverlayLayer::Fullscreen);
        if (g_cursor_scene_enabled) g_compositor.draw(*g_gerd, OverlayLayer::Cursor);
    }
    g_gerd->endFrame();

    if (g_screenshot_pending) {
        SaveJpegScreenshot();
        g_screenshot_pending = false;
    }

    ++g_fps_frame_count;

    now = MonotonicMilliseconds();
    elapsed = static_cast<float>(now - g_fps_window_tick);
    frames_per_second = g_fps_frame_count / elapsed * 1000.0f;
    if (g_fps_frame_count > 50) {
        g_fps_window_tick = MonotonicMilliseconds();
        g_fps_frame_count = 0;
    }
    g_frames_per_second = frames_per_second;

    if (g_trigger_action_active && GetWorld() != 0) {
        SetWorldScenePosition(GetWorld(), &saved_world_position);
    }
}

/* Render the world into the locked frame buffer and copy the pixels onto a
   caller-owned color surface. `rect` selects a logical 640x480 viewport; otherwise
   the stored game viewport applies. Saves use this for SHOT screenshots, and
   the automap uses it for its backdrop. */
// FUNCTION: WIZ8 0x00426f80
bool RenderWorldToSurface(SDL_Surface* target, const DesignRect* rect, bool render_secondary)
{
    GpuRenderer* gerd = g_gerd.get();
    glm::vec3 clear_color{1.0f};
    gerd->beginFrame();
    gerd->setScissor(0, 0, gerd->getWidth(), gerd->getHeight());
    const DesignRect bounds = rect != nullptr ? *rect : g_viewport;
    const auto viewport = ToSurfaceRect(bounds, gerd->getWidth(), gerd->getHeight());
    gerd->setViewPort(viewport.x, viewport.y, viewport.w, viewport.h);
    if (GetWorld() && GetWorld()->environment.fog_enabled) {
        clear_color = EnvironmentLightDirection();
    } else {
        GetWorldLightValue(GetWorld(), &clear_color);
    }
    gerd->clear(GpuRenderer::GPU_BUFFER_COLOR | GpuRenderer::GPU_BUFFER_DEPTH,
                {.color = glm::vec4(clear_color, 1.0f)});
    const auto frame = CurrentSceneFrame();
    if (render_secondary && g_render_mesh_sky && GetSecondaryWorld() != 0) {
        GetSecondaryWorld()->scene->render(*gerd, GetSecondaryWorld()->camera, frame);
    }
    GetWorld()->scene->render(*gerd, GetWorld()->camera, frame);
    gerd->endFrame();
    gerd->flushRenderers();
    if (rect != 0) {
        gerd->setViewPort(0, 0, gerd->getWidth(), gerd->getHeight());
    }
    SDL_Surface* buffer = gerd->lockBuffer();
    if (buffer != 0) {
        StretchCpuSurface(*buffer, *target, SDL_SCALEMODE_NEAREST);
        gerd->unlockBuffer();
        return true;
    }
    return false;
}

void InvalidateRendererTextureCache(void)
{
    if (g_gerd != 0) {
        g_gerd->invalidateTextureCache();
    }
}

/* Both static scenes take the light direction as their fog colour. */
void PublishLightDirection(const glm::vec3* direction)
{
    if (GetWorld() != 0) {
        GetWorld()->scene->setFogColor(*direction);
    }
    if (GetSecondaryWorld() != 0) {
        GetSecondaryWorld()->scene->setFogColor(*direction);
    }
}

void SetRendererAutoFlipEnabled(bool enabled)
{
    if (g_gerd != 0) {
        if ((!enabled && g_gerd->isEnabled(GpuRenderer::ENABLE_AUTO_FLIP)) ||
            (enabled && !g_gerd->isEnabled(GpuRenderer::ENABLE_AUTO_FLIP))) {
            g_gerd->toggle(GpuRenderer::ENABLE_AUTO_FLIP);
        }
    }
}

bool RendererBufferIsLockable(void)
{
    return g_gerd && g_gerd->isWindowOpen();
}

void SetWorldModelPickingEnabled(bool enabled)
{
    g_world_pick_enabled = enabled;
    if (!enabled) {
        g_current_model_instance = 0;
    }
}

/* Mouse cursor scene and rendering. */

// The four sprite builders share the same clockwise quad and vertically flipped UVs.
static void SetSpriteQuad(Mesh& model, float width, float height, glm::vec2 uv_scale = glm::vec2(0),
                          glm::vec2 uv_offset = glm::vec2(0))
{
    if (width == 0 || height == 0) {
        model.reset(0, 0);
        return;
    }
    model.reset(2, 4);
    model.vertex_locations = {{-0.5f * width, -0.5f * height, 0},
                              {-0.5f * width, 0.5f * height, 0},
                              {0.5f * width, 0.5f * height, 0},
                              {0.5f * width, -0.5f * height, 0}};
    model.poly_vertices = {{0, 1, 2}, {0, 2, 3}};
    if (uv_scale != glm::vec2(0) || uv_offset != glm::vec2(0))
        model.texcoords[0][0] = {uv_offset + glm::vec2(0, uv_scale.y), uv_offset,
                                 uv_offset + glm::vec2(uv_scale.x, 0), uv_offset + uv_scale};
}

std::unique_ptr<OverlayQuad> makeOverlayFromTexture(std::shared_ptr<Texture> texture, double width,
                                                    double height, bool alpha_blend, bool square)
{
    auto quad = std::make_unique<OverlayQuad>();
    quad->texture = std::move(texture);
    quad->size = {width * 640, height * 480};
    quad->shader.value = alpha_blend ? 0x100c4b7 : 0x100a417;
    if (quad->texture) {
        auto dimensions = quad->texture->getDimensions();
        dimensions.native_size = true;
        quad->texture->setDimensions(dimensions);
        quad->texture->setMipmap(Texture::MIPMAP_NONE);
        quad->texture->setWrapS(Texture::WRAP_CLAMP);
        quad->texture->setWrapT(Texture::WRAP_CLAMP);
    }
    g_compositor.attach(*quad, square ? OverlayLayer::SquareAspect : OverlayLayer::OverUi);
    return quad;
}

std::unique_ptr<OverlayQuad> makeOverlayFromSurface(SDL_Surface& surface,
                                                    const W8ControlsRect* bounds, bool square,
                                                    bool alpha_blend)
{
    SDL_Rect source = bounds ? SDL_Rect{bounds->left, bounds->top, bounds->right - bounds->left,
                                        bounds->bottom - bounds->top}
                             : SDL_Rect{0, 0, surface.w, surface.h};
    if (source.w <= 0 || source.h <= 0 || source.x < 0 || source.y < 0 ||
        source.x + source.w > surface.w || source.y + source.h > surface.h)
        return {};
    auto storage = CreateCpuStorage(source.w, source.h, surface.format);
    if (!storage) return {};
    if (auto* palette = SDL_GetSurfacePalette(&surface))
        if (!SDL_SetSurfacePalette(storage.get(), palette)) return {};
    SDL_BlendMode blend;
    SDL_GetSurfaceBlendMode(&surface, &blend);
    SDL_SetSurfaceBlendMode(&surface, SDL_BLENDMODE_NONE);
    const bool copied = SDL_BlitSurface(&surface, &source, storage.get(), nullptr);
    SDL_SetSurfaceBlendMode(&surface, blend);
    if (!copied) return {};
    auto texture = std::make_shared<Texture>(std::shared_ptr<SDL_Surface>(std::move(storage)));
    const int extent = std::max(source.w, source.h);
    const auto size = square ? glm::vec2(extent) : glm::vec2(source.w, source.h);
    auto quad = makeOverlayFromTexture(std::move(texture), size.x / 640.0, size.y / 480.0,
                                       alpha_blend, square);
    quad->position = {source.x, source.y};
    return quad;
}

std::unique_ptr<OverlayQuad> makeColorOverlay(int width, int height, const glm::vec4& color,
                                              bool fullscreen)
{
    auto quad = std::make_unique<OverlayQuad>();
    quad->size = {width, height};
    quad->color = color;
    quad->shader.value = 0x2417;
    g_compositor.attach(*quad, fullscreen ? OverlayLayer::Fullscreen : OverlayLayer::OverUi);
    return quad;
}

void positionOverlay(OverlayQuad* quad, int x, int y, bool snap)
{
    if (!quad) return;
    quad->position = {x, y};
    if (snap && g_gerd) {
        const auto output = glm::vec2(g_gerd->getWidth(), g_gerd->getHeight());
        const auto scale = output / glm::vec2(640, 480);
        quad->position = glm::floor(quad->position * scale) / scale;
    }
}

bool BlitVideoObjectToColorSurface(const SpriteSheet& sprites, std::uint16_t region,
                                   SDL_Surface& destination, int x, int y)
{
    if (region >= sprites.frames.size()) {
        return false;
    }
    DrawSprite(destination, sprites, region, x, y);
    return true;
}

static void MoveSystemCursor(int x, int y)
{
    /* The client space is the fixed 640x480 design space, so client and
       screen coordinates coincide. */
    WarpGameMouse(ghWindow, x, y);
}

bool ResizeMouseCursorSurface(int width, int height)
{
    int extent;
    float mapping_scale;

    if (g_cursor_image_width == width && g_cursor_image_height == height) {
        return true;
    }
    extent = width > height ? width : height;
    if (extent <= 16)
        extent = 16;
    else if (extent <= 32)
        extent = 32;
    else if (extent <= 64)
        extent = 64;
    else if (extent <= 128)
        extent = 128;
    else if (extent <= 256)
        extent = 256;
    else
        extent = -1;
    if (extent < 0) return false;
    g_mouse_surface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(extent, extent, g_mouse_surface->format).release(), SDL_DestroySurface);
    g_cursor_node.reset();
    mapping_scale = g_texture_inset_pixels / extent;
    auto texture = std::make_shared<Texture>(g_mouse_surface);
    g_cursor_node = makeOverlayFromTexture(texture, extent / 640.0, extent / 480.0, true, false);
    g_compositor.attach(*g_cursor_node, OverlayLayer::Cursor);
    g_cursor_node->uv_min = glm::vec2(mapping_scale);
    g_cursor_node->uv_max = 1.0f + g_cursor_node->uv_min;
    PositionMouseCursor(g_cursor_width, g_cursor_height, false);
    return true;
}

/* Move the OS/system cursor when the hotspot changes, then resync the rendered
   cursor. */
void SetMouseCursorHotspot(short hotspot_x, short hotspot_y)
{
    int x;
    int y;

    if (g_cursor_hotspot_x != hotspot_x || g_cursor_hotspot_y != hotspot_y) {
        x = g_cursor_width - hotspot_x + g_cursor_hotspot_x;
        y = g_cursor_height - hotspot_y + g_cursor_hotspot_y;
        MoveSystemCursor(x, y);
        SyncSystemCursor();
        g_cursor_hotspot_x = hotspot_x;
        g_cursor_hotspot_y = hotspot_y;
    }
}

bool SetMouseCursorFromVideoObject(const SpriteSheet& sprites, std::uint16_t region,
                                   std::int16_t offset_x, std::int16_t offset_y)
{
    int x;
    int y;

    if (region >= sprites.frames.size()) {
        return false;
    }
    const SpriteFrame& properties = sprites.frames[region];
    if (!ResizeMouseCursorSurface(properties.width + 1, properties.height + 1)) {
        return false;
    }
    if (g_cursor_hotspot_x != offset_x || g_cursor_hotspot_y != offset_y) {
        x = g_cursor_width - offset_x + g_cursor_hotspot_x;
        y = g_cursor_height - offset_y + g_cursor_hotspot_y;
        MoveSystemCursor(x, y);
        SyncSystemCursor();
        g_cursor_hotspot_x = offset_x;
        g_cursor_hotspot_y = offset_y;
    }
    g_cursor_image_width = properties.width;
    g_cursor_image_height = properties.height;
    ClearMouseSurface();
    return BlitVideoObjectToColorSurface(sprites, region, *g_mouse_surface, 0, 0);
}

void BlitToMouseCursor(const SpriteSheet& sprites, std::uint16_t region, std::uint16_t x,
                       std::uint16_t y)
{
    BlitVideoObjectToColorSurface(sprites, region, *g_mouse_surface, x, y);
}

void RefreshMouseCursorTexture(void)
{
    if (g_cursor_node && g_cursor_node->texture) g_cursor_node->texture->invalidate();
}

/* Whether the tracked cursor position lies inside the render viewport. The
   two automap callers use it, but nothing names the viewport as automap-only
   state; placed here with the neighbouring cursor bodies. */
bool ClearMouseSurface(void)
{
    SDL_FillSurfaceRect(g_mouse_surface.get(), nullptr, 0);
    return true;
}

/* Distinct from IsCursorInsideViewport: the whole cursor image must fit
   inside the viewport, not just the hotspot point. */
bool IsCursorImageInsideViewport(void)
{
    if (g_cursor_width >= g_viewport.left && g_cursor_height >= g_viewport.top &&
        g_cursor_width + g_cursor_image_width <= g_viewport.right &&
        g_cursor_height + g_cursor_image_height <= g_viewport.bottom) {
        return true;
    }
    return false;
}

bool IsCursorInsideViewport(void)
{
    int x = g_cursor_hotspot_x + g_cursor_width;
    int y = g_cursor_hotspot_y + g_cursor_height;
    return x >= g_viewport.left && y >= g_viewport.top && x <= g_viewport.right &&
           y <= g_viewport.bottom;
}

void WarpSystemCursor(int x, int y)
{
    WarpGameMouse(ghWindow, x, y);
}

void PositionMouseCursor(int width, int height, bool reset_tick)
{
    if (!g_mouse_surface) return;
    g_cursor_width = std::min(width, 640);
    g_cursor_height = std::min(height, 480);
    if (g_cursor_node) {
        g_cursor_node->position = {g_cursor_width, g_cursor_height};
        if (reset_tick) g_cursor_move_tick = MonotonicMilliseconds();
    }
}

/* Milliseconds since the cursor last moved (or was repositioned with the
   reset-tick arm). */
std::uint64_t GetMillisecondsSinceCursorMove(void)
{
    return MonotonicMilliseconds() - g_cursor_move_tick;
}

/* The tracked cursor as viewport-relative 0..1 coordinates, or zero when it
   lies outside. Same tracked position and viewport as the query above. */
unsigned char GetCursorPositionInViewport(glm::vec2* position)
{
    int x = g_cursor_hotspot_x + g_cursor_width;
    int y = g_cursor_hotspot_y + g_cursor_height;
    if (x >= g_viewport.left && y >= g_viewport.top && x <= g_viewport.right &&
        y <= g_viewport.bottom) {
        position->x =
            (x - g_viewport.left) / static_cast<float>(g_viewport.right - g_viewport.left);
        position->y = (y - g_viewport.top) / static_cast<float>(g_viewport.bottom - g_viewport.top);
        return 1;
    }
    return 0;
}

/* The tracked cursor tip (hotspot + size) in pixel-scale units. DropHeldItem
   turns it into the toss direction. */
glm::vec2 GetCursorScaledPosition()
{
    return glm::vec2(g_cursor_hotspot_x + g_cursor_width, g_cursor_hotspot_y + g_cursor_height) *
           glm::vec2(g_viewport_x_scale, g_viewport_y_scale);
}

/* Keep the rendered cursor synchronized with the OS cursor. The input layer
   already reports design-space coordinates, so windowed and fullscreen share
   the same position; fullscreen coordinates are clamped to the 640x480 game
   surface. */
void SyncSystemCursor(void)
{
    SDL_Point cursor = GetGameMousePosition();
    if (!g_fullscreen) {
        /* GetGameMousePosition already returns design-space coordinates. */
        if (cursor.x != g_cursor_width || cursor.y != g_cursor_height) {
            PositionMouseCursor(cursor.x, cursor.y, true);
        }
        return;
    }

    if (cursor.x < 1)
        cursor.x = 0;
    else if (cursor.x >= 640)
        cursor.x = 640;
    if (cursor.y < 1)
        cursor.y = 0;
    else if (cursor.y >= 480)
        cursor.y = 480;
    if (cursor.x != g_cursor_width || cursor.y != g_cursor_height) {
        PositionMouseCursor(cursor.x, cursor.y, true);
        WarpGameMouse(ghWindow, cursor.x, cursor.y);
    }
}

// The cursor tip in design coordinates, including the active hotspot.
SDL_Point CursorPosition()
{
    SyncSystemCursor();
    return {g_cursor_hotspot_x + g_cursor_width, g_cursor_hotspot_y + g_cursor_height};
}

bool IsCursorInRectangle(int left, int top, int right, int bottom)
{
    SDL_Point point;

    point = CursorPosition();
    return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

/* Creates the shipped 128x128 mouse polygon inside its dedicated scene. */
unsigned char InitializeMouseCursorScene(void)
{
    if (!g_mouse_surface) return 0;
    ClearMouseSurface();
    auto texture = std::make_shared<Texture>(g_mouse_surface);
    g_cursor_node = makeOverlayFromTexture(texture, .2, 128.0 / 480.0, true, false);
    g_compositor.attach(*g_cursor_node, OverlayLayer::Cursor);
    g_cursor_node->uv_min = glm::vec2(g_texture_inset_pixels / 128.0f);
    g_cursor_node->uv_max = 1.0f + g_cursor_node->uv_min;
    PositionMouseCursor(640, 480, true);
    return 1;
}

unsigned char g_world_render_enabled;

void InvalidateRegion(int left, int top, int right, int bottom)
{
    constexpr SDL_Rect screen{0, 0, 640, 480};
    const SDL_Rect area{left, top, right - left, bottom - top};
    SDL_Rect clipped;
    if (!SDL_GetRectIntersection(&area, &screen, &clipped)) return;
    if (SDL_RectEmpty(&g_ui_upload))
        g_ui_upload = clipped;
    else
        SDL_GetRectUnion(&g_ui_upload, &clipped, &g_ui_upload);
}

void InvalidateScreenRects(DesignRect* rects, unsigned int count)
{
    for (const auto& rect : std::span(rects, count))
        InvalidateRegion(rect.left, rect.top, rect.right, rect.bottom);
}

SDL_Rect TakeUiUpload()
{
    return std::exchange(g_ui_upload, SDL_Rect{});
}

static void FlushUiUpload()
{
    const auto upload = TakeUiUpload();
    if (SDL_RectEmpty(&upload)) return;
    g_ui_canvas->updateRectangle(g_gerd.get(), upload.x, upload.y, upload.x + upload.w,
                                 upload.y + upload.h);
}

void SetScaledViewport(const DesignRect& bounds)
{
    if (bounds == g_viewport) {
        return;
    }
    const auto rect = ToSurfaceRect(bounds, g_gerd->getWidth(), g_gerd->getHeight());
    g_gerd->setViewPort(rect.x, rect.y, rect.w, rect.h);
    g_viewport = bounds;
}

/* Sets the viewport and rebuilds the camera view plane to match it. */

void SetViewport(const DesignRect& bounds)
{
    float fractional_left;
    float fractional_top;
    float fractional_right;
    float fractional_bottom;
    Camera3D::Rect plane;

    if (g_gerd != 0 && g_flush_pending) {
        g_gerd->flush();
    }
    g_viewport = bounds;
    fractional_left = bounds.left * g_viewport_x_scale;
    fractional_top = bounds.top * g_viewport_y_scale;
    fractional_right = g_viewport.right * g_viewport_x_scale;
    fractional_bottom = g_viewport.bottom * g_viewport_y_scale;

    if (GetWorld() != 0 && GetWorld()->camera != 0) {
        GetWorld()->camera->setViewPlane(3.14159265358979323846 * 0.0055555557f * 85.0f,
                                         3.14159265358979323846 * 0.0055555557f * 71.0f);
        const auto view = GetWorld()->camera->getViewPlane().rectangle;

        plane.left = fractional_left * (view.right - view.left) + view.left;
        plane.right = fractional_right * (view.right - view.left) + view.left;
        plane.bottom = (1.0 - fractional_bottom) * (view.top - view.bottom) + view.bottom;
        plane.top = (1.0 - fractional_top) * (view.top - view.bottom) + view.bottom;

        GetWorld()->camera->setViewPlane(plane, 1.0);
        if (GetSecondaryWorld() != 0) {
            GetSecondaryWorld()->camera->setViewPlane(plane, 1.0);
        }
        if (g_camera_sway_active) {
            GetWorld()->party_camera.applySway(*GetWorld()->camera, CameraSwayMode::Start);
        }
    }
}

/* The debug stats readout drawn over the primary surface: a black band plus
   the frame-rate line always, the object/poly/vertex counters, texture and
   pagefile memory in mode 2, or the scaled camera position in mode 3. */
void DrawVideoInspector(int left, unsigned int top)
{
    glm::vec3 position;
    unsigned int bottom;
    unsigned char* row;
    int rows;
    /* Retail left the band height uninitialised for inspector modes outside
       1..3 and read that storage into bottom; natively it starts at zero. */
    int height = 0;

    if (g_video_inspector_mode == 1) {
        height = 0xb;
    } else if (g_video_inspector_mode == 2) {
        height = 0x9a;
    } else if (g_video_inspector_mode == 3) {
        height = 0x2c;
    }
    bottom = top + height;
    const auto& description = *primarySurface;
    if (description.pixels != 0) {
        if (top < bottom) {
            row = static_cast<unsigned char*>(description.pixels) + description.pitch * top +
                  left * 2;
            for (rows = bottom - top; rows != 0; --rows) {
                memset(row, 0, 0x226);
                row += description.pitch;
            }
        }
    }
    InvalidateRegion(left, top, left + 0x113, bottom);
    if (g_gerd != 0) {
        const auto statistics = g_gerd->getStatistics();
        g_smfnt_font.setPalette(g_font_state_palettes[W8_FONT_PALETTE_YELLOW]);
        const int text_left = static_cast<int>(left);
        const int text_top = static_cast<int>(top);
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font, {text_left, text_top},
                              fmt::format("FR: {:4.1f}", g_frames_per_second));
        if (g_video_inspector_mode == 2) {
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0xa},
                                  fmt::format("OC: {:d}", GetWorld()->scene->submitted_polygons));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x14},
                                  fmt::format("PI: {:d}", statistics.input_triangles));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x1e},
                                  fmt::format("PO: {:d}", statistics.device_triangles));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x28},
                                  fmt::format("VI: {:d}", statistics.input_vertices));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x32},
                                  fmt::format("VO: {:d}", statistics.device_vertices));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x3c},
                                  fmt::format("DD: {:d}", statistics.device_calls));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x46},
                                  fmt::format("TC: {:d}", statistics.texture_binds));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x50},
                                  fmt::format("TT: {:.0f}", statistics.texture_transfer));
            DrawTextAndInvalidate(
                GetFrameBufferSurface(), g_smfnt_font, {text_left, text_top + 0x5a},
                fmt::format("RM: {:d}K", g_gerd->getResidentTextureMemUsed() >> 10));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x6e},
                                  fmt::format("DR: {:3d}", GetCameraYawAndRotation(0)));
            DrawTextAndInvalidate(
                GetFrameBufferSurface(), g_smfnt_font, {text_left, text_top + 0x78},
                fmt::format("MM: {:d}K",
                            static_cast<unsigned int>(g_decompressed_mesh_bytes) >> 10));
            return;
        }
        if (g_video_inspector_mode == 3) {
            float scaled;

            position = cameraLocation();
            if (IsKeyHeld(SDLK_F1)) {
                position.x *= g_world_cursor_scale;
                scaled = position.y * g_world_cursor_scale;
                position.y = position.z * g_world_cursor_scale;
                position.z = scaled;
            }
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0xa},
                                  fmt::format(" X: {:.2f}", position.x));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x14},
                                  fmt::format(" Y: {:.2f}", position.y));
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {text_left, text_top + 0x1e},
                                  fmt::format(" Z: {:.2f}", position.z));
        }
    }
}

void VideoInspectorEnable(void)
{
    g_video_inspector_enabled = true;
}

std::int32_t g_help_box_width;
std::int32_t g_help_box_height;

/* Live tooltip sprites, left to right. */
static std::vector<std::unique_ptr<OverlayQuad>> g_tooltip_nodes;

void VideoRemoveToolTip(void)
{
    g_tooltip_nodes.clear();
}

std::unique_ptr<MeshInstance> VideoMakePoster(std::shared_ptr<SDL_Surface> surface, float width,
                                              float height, bool additive)
{
    auto texture = std::make_shared<Texture>();
    texture->setMipmapBias(-8.0f);
    texture->setName("VideoMakePoster");
    texture->setSurfacePtr(surface);
    texture->setWrapS(Texture::WRAP_CLAMP);
    texture->setWrapT(Texture::WRAP_CLAMP);
    return MakePosterQuad(std::move(texture), width, height, additive);
}

void PresentMenuOverlayFrame(void)
{
    FlushUiUpload();
    g_gerd->beginFrame();
    g_ui_canvas->draw(g_gerd.get());
    g_gerd->flushRenderers();
    g_gerd->endFrame();
}

void SetPrimarySurfaceTextureHint2Enabled(bool enabled)
{
    if (g_ui_canvas) {
        g_ui_canvas->setAlphaTestEnabled(enabled);
    }
}

unsigned char InitializeMouseSurface(void)
{
    g_mouse_surface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(128, 128, kFramePixelFormat).release(), SDL_DestroySurface);
    ClearMouseSurface();
    return 1;
}

/* A black, fog-free scene with no parent, used for the 2D overlay layers. */
unsigned char InitializeRendererSceneObjects(void)
{
    InitializeMouseSurface();
    g_primary_color_surface = primarySurface;
    g_ui_canvas = std::make_unique<UiCanvasTexture>(primarySurface);
    g_viewport = {};
    g_surface_opaque_shader_bits = 0x100a017;
    g_surface_alpha_blend_shader_bits = 0x100c0b7;
    return 1;
}

void ClearSurfaceRect(int left, unsigned int top, int right, unsigned int bottom)
{
    if (top >= bottom || left >= right) return;
    const SDL_Rect rectangle{left, static_cast<int>(top), right - left,
                             static_cast<int>(bottom - top)};
    SDL_FillSurfaceRect(primarySurface.get(), &rectangle, 0);
}

void EraseRegion(int left, int top, int right, int bottom)
{
    ClearSurfaceRect(left, top, right, bottom);
    InvalidateRegion(left, top, right, bottom);
}

unsigned char DisableCursorScene(void)
{
    g_cursor_scene_enabled = false;
    return 1;
}

unsigned char EnableCursorScene(void)
{
    g_cursor_scene_enabled = true;
    return 1;
}

/* Install a texture (often an Texture) on the mouse-cursor mesh. A null
   argument reuses the current cursor texture. */
void SetMouseCursorTexture(std::shared_ptr<Texture> texture)
{
    if (texture && g_cursor_node) g_cursor_node->texture = std::move(texture);
}

bool HasScreenTransitionObjects(void)
{
    return !g_tooltip_nodes.empty();
}

void SetTextureInset(float scale)
{
    g_texture_inset_pixels = scale;
}

void SetFullscreenSceneLast(unsigned char value)
{
    g_fullscreen_scene_last = value;
}

/* The retail linker folds this trivial TRUE stub with other returns; its
   0x42B830 address sits past the proven Video2 hull, so its placement stays
   provisional. */

SDL_Surface& GetFrameBufferSurface()
{
    return *primarySurface;
}

/* The retail empty video-capture entry folds with the shared 0x4023a0 ret. */

int g_screenshot_index;
int g_screenshot_page;

void SaveJpegScreenshot(void)
{
    SDL_Surface* surface = g_gerd->lockBuffer();
    if (surface != 0) {
        char filename[32];

        int screenshot_index = g_screenshot_index++;
        sprintf(filename, "Wiz8%5.5d.JPG", screenshot_index);
        imageIO::save(filename, *surface);

        g_gerd->unlockBuffer();
    }
    g_screenshot_page = (g_screenshot_page - 1) & 1;
}

/* Places one tooltip node at a screen position in normalized coordinates.
   With positional set, the position is snapped to the renderer's pixel grid;
   the node keeps the screen x/y in its right/bottom extent fields. */
void VideoPositionToolTip(std::int32_t x, std::int32_t y)
{
    if (!g_tooltip_nodes.empty()) {
        std::int32_t offset = x;
        for (const auto& node : g_tooltip_nodes) {
            positionOverlay(node.get(), offset, y, true);
            offset += static_cast<int>(node->scaledSize().x);
        }
        return;
    }
}

/* Copies the tooltip source rectangle into a size-rounded 16-bit surface,
   repeating its border one pixel outward, and reports the texture mapping
   scales for the resulting polygon brush. */
void VideoToolTip(std::string_view text)
{
    if (!g_tooltip_nodes.empty()) {
        return;
    }
    auto surface = CreateCpuStorage(0xfeUL, 0xfeUL, SDL_PIXELFORMAT_ARGB1555);
    W8ControlsRect bounds;
    bounds.left = 0;
    bounds.top = 0;
    bounds.right = 0xfa;
    bounds.bottom = 0xfa;
    auto buffer = std::make_unique<W8TextBuffer>(
        &bounds, text, g_font10arial, g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4);
    g_help_box_width = static_cast<int>(buffer->m_maxLineWidth) + 4;
    g_help_box_height = g_font10arial.height() * buffer->m_lineCount + 2;
    SDL_FillSurfaceRect(surface.get(), nullptr, 0);
    void* data = surface->pixels;
    SDL_Rect background{0, 0, g_help_box_width, g_help_box_height};
    SDL_FillSurfaceRect(surface.get(), &background, SDL_MapSurfaceRGB(surface.get(), 0, 0, 0));
    SDLSurfaceOwner text_surface{
        SDL_CreateSurfaceFrom(surface->w, surface->h, SDL_PIXELFORMAT_RGB565, data, surface->pitch),
        SDL_DestroySurface};
    if (text_surface) buffer->RenderText(*text_surface, 2, 1, true);
    std::uint32_t border_colour;
    PackColourToLong(&border_colour, 1.0, 0.93, 0.6, 0.33);
    const SDL_Rect edges[]{{0, 0, g_help_box_width, 1},
                           {0, g_help_box_height - 1, g_help_box_width, 1},
                           {0, 0, 1, g_help_box_height},
                           {g_help_box_width - 1, 0, 1, g_help_box_height}};
    SDL_FillSurfaceRects(surface.get(), edges, 4,
                         SDL_MapSurfaceRGBA(surface.get(), (border_colour >> 16) & 255,
                                            (border_colour >> 8) & 255, border_colour & 255,
                                            border_colour >> 24));

    W8ControlsRect rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = 0xfe;
    rect.bottom = 0xfe;
    if (!text_surface) {
        return;
    }
    auto quad = makeOverlayFromSurface(*text_surface, &rect, false, true);
    if (quad) {
        g_compositor.attach(*quad, OverlayLayer::Cursor);
        g_tooltip_nodes.push_back(std::move(quad));
    }

    int position_y = g_cursor_height - g_help_box_height;
    int position_x = g_cursor_width;
    int offset = position_x;
    for (const auto& node : g_tooltip_nodes) {
        positionOverlay(node.get(), offset, position_y, true);
        offset += static_cast<int>(node->scaledSize().x);
    }
}

/* Renderer configuration helpers reached from the video device, not the
   persisted configuration block. */
int g_resident_texture_policy;

void SetResidentTexturePolicy(int policy)
{
    if (policy != g_resident_texture_policy) {
        g_gerd->invalidateResidentTextures();
        g_gerd->invalidateTextureCache();
        g_resident_texture_policy = policy;
    }
}

void SetSwapInterval(bool enabled)
{
    g_gerd->setSwapInterval(enabled ? 1 : 0);
}

/* Build an Texture whose frames are consecutive VObject subimages starting
   at start_frame. use_argb1555 selects SURFACE_ARGB1555 versus SURFACE_RGB555 for
   each frame surface. The setName string is the retail identity. */
std::shared_ptr<Texture> VideoVObjectToTextureAnim(const SpriteSheet& sprites,
                                                   unsigned short start_frame,
                                                   unsigned short frame_count, bool use_argb1555)
{
    unsigned short frame;
    unsigned short end_frame;
    std::uint32_t extent;
    unsigned short largest;

    auto animation = std::make_shared<Texture>(TexturePlayback{});
    animation->setName("VideoVObjecttoTextureAnim");
    animation->setMipmapBias(-8.0f);

    frame = start_frame;
    end_frame = static_cast<unsigned short>(start_frame + frame_count);
    while (frame < end_frame) {
        if (frame < sprites.frames.size()) {
            const SpriteFrame& properties = sprites.frames[frame];
            largest = properties.height;
            if (properties.height < properties.width) {
                largest = properties.width;
            }
            if (largest < 0x101) {
                if (largest < 0x81) {
                    if (largest < 0x41) {
                        if (largest < 0x21) {
                            extent = ((largest < 0x11) - 1 & 0x10) + 0x10;
                        } else {
                            extent = 0x40;
                        }
                    } else {
                        extent = 0x80;
                    }
                } else {
                    extent = 0x100;
                }
            } else {
                extent = static_cast<std::uint32_t>(-1);
            }

            std::shared_ptr<SDL_Surface> surface;
            if (!use_argb1555) {
                surface = std::shared_ptr<SDL_Surface>(
                    CreateCpuStorage(extent, extent, SDL_PIXELFORMAT_XRGB1555).release(),
                    SDL_DestroySurface);
            } else {
                surface = std::shared_ptr<SDL_Surface>(
                    CreateCpuStorage(extent, extent, SDL_PIXELFORMAT_ARGB1555).release(),
                    SDL_DestroySurface);
            }
            SDL_FillSurfaceRect(surface.get(), nullptr, 0);
            if (BlitVideoObjectToColorSurface(sprites, frame, *surface, 0, 0)) {
                auto texture = std::make_shared<Texture>();
                texture->setName("VideoVObjecttoTextureAnim");
                texture->setMipmapBias(-8.0f);
                texture->setSurfacePtr(surface);
                texture->setWrapS(Texture::WRAP_CLAMP);
                texture->setWrapT(Texture::WRAP_CLAMP);
                texture->setMagFilter(Texture::FILTER_NONE);
                texture->setMinFilter(Texture::FILTER_NONE);

                texture->setMipmap(Texture::MIPMAP_NONE);

                animation->AddTexture(std::move(texture));
            }
        }
        ++frame;
    }
    return animation;
}

/* The option-4-suppressed variant of the render probe: with the positional
   option forced off, draw the node between the camera's bracketing
   passes, restore the option and return the sampled statistic. */
unsigned int MeasureMeshRenderWithoutPositionalOption(MeshInstance* mesh)
{
    if (g_gerd != 0 && g_gerd->isEnabled(GpuRenderer::ENABLE_AUTO_FLIP)) {
        g_gerd->toggle(GpuRenderer::ENABLE_AUTO_FLIP);
    }
    unsigned int pixels_drawn = MeasureMeshRender(mesh);
    if (g_gerd != 0 && !g_gerd->isEnabled(GpuRenderer::ENABLE_AUTO_FLIP)) {
        g_gerd->toggle(GpuRenderer::ENABLE_AUTO_FLIP);
    }
    return pixels_drawn;
}

/* Draw the node outside any scene pass and return the renderer statistic the
   probe samples. */
unsigned int MeasureMeshRender(MeshInstance* mesh)
{

    g_gerd->flushRenderers();
    g_gerd->resetStatistics();
    g_gerd->beginFrame();
    {
        const auto& camera = *GetWorld()->camera;
        // No scene lights, fogs or picks; the ambient is the renderer's historical default.
        mesh->draw(
            *g_gerd, camera.drawTransform(),
            {.ambient_light = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f), .depth_cue = camera.depthCue()});
        g_gerd->flushRenderers();
    }
    g_gerd->endFrame();
    g_gerd->flushRenderers();
    const auto statistics = g_gerd->getStatistics();
    return static_cast<unsigned int>(statistics.pixels_drawn);
}

std::unique_ptr<MeshInstance> MakePosterQuad(std::shared_ptr<Texture> texture, float width,
                                             float height, bool additive)
{
    DrawShader shader;
    Texture::Dimensions dimensions;
    dimensions.width = 64;
    dimensions.height = 64;

    auto model = std::make_shared<Mesh>(0, 0);
    float extent_w = 1.0f / dimensions.width * g_texture_inset_pixels;
    float extent_h = 1.0f / dimensions.height * g_texture_inset_pixels;
    model->setName("VideoMakePoster");
    texture->getDimensions();
    SetSpriteQuad(*model, width, height, {1.0f - extent_w, 1.0f - extent_h}, {extent_w, extent_h});

    shader.value = 0x100a013;
    if (additive) {
        shader.value = 0x100c0b3;
        model->enable(Mesh::CONTROL_SORTED_RENDERING);
    }
    auto material = std::make_shared<Material>();
    material->setEmissive(glm::vec4(1));
    material->setDiffuse(glm::vec4(0));
    material->setSpecular(glm::vec4(0));
    model->setMaterial(std::move(material), 0, Mesh::SIDE_FRONT);
    model->setTexture(std::move(texture), 0, 0);
    model->setShader(shader, 0);

    auto instance = std::make_unique<MeshInstance>(nullptr);
    instance->setName("VideoMakePoster");
    instance->setModel(model);
    instance->render_flags |= MeshInstance::RENDER_NO_PICK;
    return instance;
}

void DrawColorSurface(SDL_Surface* surface, int x, int y)
{
    SDL_Rect destination{x, y, surface->w, surface->h};
    SDL_BlitSurface(surface, nullptr, g_primary_color_surface.get(), &destination);
}

ViewportBounds GetScaledViewportBounds()
{
    return {{g_viewport.left * g_viewport_x_scale, g_viewport.top * g_viewport_y_scale},
            {g_viewport.right * g_viewport_x_scale, g_viewport.bottom * g_viewport_y_scale}};
}

/* VertexChannels::MaterialInfo's inline ctor emitted out-of-line inside
   Material's locally-compiled constructor; the primary is in
   srVertexProcessor.h. */

void DrawTextAndInvalidate(SDL_Surface& target, Font& font, SDL_Point at, std::string_view text,
                           TextOptions options)
{
    DrawText(target, font, at, text, options);
    InvalidateRegion(at.x, at.y, at.x + font.textWidth(text), at.y + font.height());
}
