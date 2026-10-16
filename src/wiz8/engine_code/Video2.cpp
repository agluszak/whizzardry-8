#include <cmath>
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
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <fmt/format.h>
#include <wiz8/filesystem.h>

#include "AnimRep.hpp"
#include "asset_paths.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/common.hpp" // IWYU pragma: keep
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/world.h"
#include "surrender/srClass.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "surrender/srTextureIFace.h"
#include "wiz8/design_rect.h"
#include "wiz8/overlay_scenes.h"
#include "wiz8/surface_storage.h"
#include "wiz8/xstatus.h"
#ifdef WIZ8_RUNTIME_TESTS
#include "runtime_instrumentation.h"
#endif
#include <SDL3/SDL_stdinc.h>

#include "Font.h"
#include "compat/video.h"
#include "himage.h"
#include "image.h"
#include "input.h"
#include "sprite.h"
#include "surrender/srCamera.h"
#include "surrender/srCore.h"
#include "surrender/srDD.h"
#include "surrender/srGERD.h"
#include "surrender/srImageIO.h"
#include "surrender/srMaterial.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srScene.h"
#include "surrender/srShader.h"
#include "surrender/srStatisticsManager.h"
#include "surrender/srTexture.h"
#include "surrender/srTextureMap.h"
#include "wiz8/application.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/float_constants.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/sr_api.h"
#include "wiz8/startup_world.h"
#include "wiz8/surface2d.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"

srModelInstance* Video2DRectToSquarePolygon(const W8ControlsRect* rect, SDL_Surface& source,
                                            srNode* parent, unsigned char overlay);
srModelInstance* Video2DRectToPolygon(const W8ControlsRect* rect, SDL_Surface& source,
                                      srNode* parent, unsigned char overlay);
unsigned char CopySurfaceWithBorder(SDL_Surface* surface, const W8ControlsRect* rect,
                                    SDL_Surface& source, float* scale_x, float* scale_y,
                                    float* mapping_x, float* mapping_y);
void SaveJpegScreenshot(void);
static void FlushUiUpload();

/* The CPU frame buffer is the game-space UI canvas and g_surface_node's
   persistent tile textures mirror it on the GPU. Invalidation accumulates one
   bounding rectangle, uploaded once per frame before composition. */
static SDL_Rect g_ui_upload{};

/*
 * The renderer window and extension loading gate the native application initializes after the
 * input manager.
 */

/* The released SGP video unit owns this platform handle.  Wiz8 replaces the
   released video manager but keeps the same source-defined interface. */
// GLOBAL: WIZ8 0x006596CC
SDL_Window* ghWindow;

// GLOBAL: WIZ8 0x603c38
bool g_world_pick_enabled = true;
// GLOBAL: WIZ8 0x603c4c
unsigned char g_fullscreen_scene_last = 1;
// GLOBAL: WIZ8 0x603c60
bool g_cursor_scene_enabled = true;
// GLOBAL: WIZ8 0x603c6d
bool g_overlay_scenes_enabled = true;
// GLOBAL: WIZ8 0x603c68
int g_frame_reset_interval = 50;
// GLOBAL: WIZ8 0x603c39
unsigned char g_fullscreen = 1;
// GLOBAL: WIZ8 0x603c3a
bool g_flush_pending = true;
// GLOBAL: WIZ8 0x603c3c
int g_screen_width = 640;
// GLOBAL: WIZ8 0x603c40
int g_screen_height = 480;
// GLOBAL: WIZ8 0x603c48
int g_pixel_format = 9;
// GLOBAL: WIZ8 0x603d74
int g_renderer_mode = 1;
// GLOBAL: WIZ8 0x65962c
srModelInstance* g_current_model_instance;
// GLOBAL: WIZ8 0x6596fc
int g_fps_frame_count;
// GLOBAL: WIZ8 0x659700
uint64_t g_fps_window_tick;
/* Per-page counters reset as each overlay page is retired; retail stores
   through g_active_page as an index, so this is one two-entry array. */
// GLOBAL: WIZ8 0x6595f8
// GLOBAL: WIZ8 0x659710
bool g_video_active;
// GLOBAL: WIZ8 0x65970e
bool g_world_blacked_out;
// GLOBAL: WIZ8 0x659711
bool g_screenshot_pending;
// GLOBAL: WIZ8 0x65970f
bool g_video_inspector_enabled;
// GLOBAL: WIZ8 0x00659634
std::unique_ptr<srGERD> g_gerd;
// GLOBAL: WIZ8 0x65971c
// GLOBAL: WIZ8 0x65969c
// GLOBAL: WIZ8 0x6596a0
// GLOBAL: WIZ8 0x6596a4
// GLOBAL: WIZ8 0x6596a8
static std::shared_ptr<SDL_Surface> primarySurface;
// GLOBAL: WIZ8 0x6596ac
// GLOBAL: WIZ8 0x6596b0
SDLSurfaceOwner videoSurfaceOwner{nullptr, SDL_DestroySurface};

// GLOBAL: WIZ8 0x600088
unsigned int g_color_key = 0x3def;
VideoOverlayScenes g_overlay_scenes;
// GLOBAL: WIZ8 0x659640
// GLOBAL: WIZ8 0x659644
// GLOBAL: WIZ8 0x659648
// GLOBAL: WIZ8 0x65964c
// GLOBAL: WIZ8 0x659650
// GLOBAL: WIZ8 0x659654
// GLOBAL: WIZ8 0x659658
// GLOBAL: WIZ8 0x65965c
// GLOBAL: WIZ8 0x659660
std::shared_ptr<SDL_Surface> g_primary_color_surface;
// GLOBAL: WIZ8 0x659664
class stSurface2D* g_surface_node;
// GLOBAL: WIZ8 0x659680
float g_surface_scale;
// GLOBAL: WIZ8 0x659670
// GLOBAL: WIZ8 0x659674
// GLOBAL: WIZ8 0x659688
std::shared_ptr<SDL_Surface> g_mouse_surface;
// GLOBAL: WIZ8 0x65967c
std::shared_ptr<srMaterial> g_blit_material;
// GLOBAL: WIZ8 0x6595dc
int g_surface_opaque_shader_bits;
// GLOBAL: WIZ8 0x654ad8
int g_surface_alpha_blend_shader_bits;
// GLOBAL: WIZ8 0x6595e8
DesignRect g_viewport;
// GLOBAL: WIZ8 0x00659AB4
W8World* g_world;
// GLOBAL: WIZ8 0x00659AB8
W8World* g_secondary_world;
// GLOBAL: WIZ8 0x652da4
bool g_camera_sway_active;
// GLOBAL: WIZ8 0x5ebb1c
extern const float g_viewport_x_scale = 1.0f / 640.0f;
// GLOBAL: WIZ8 0x5ebb20
extern const float g_viewport_y_scale = 1.0f / 480.0f;

// GLOBAL: WIZ8 0x006596e4
unsigned int g_active_page;
// GLOBAL: WIZ8 0x006596d4
int g_video_inspector_mode;
// GLOBAL: WIZ8 0x006596ec
int g_overlay_render_mode;
// GLOBAL: WIZ8 0x006596f0
int g_paired_render_mode;
// GLOBAL: WIZ8 0x659668
// GLOBAL: WIZ8 0x65966c
// GLOBAL: WIZ8 0x659678
// GLOBAL: WIZ8 0x65409c
uint64_t g_last_capture_tick;
// GLOBAL: WIZ8 0x659704
float g_frames_per_second;
// GLOBAL: WIZ8 0x659708
float g_seconds_per_frame;

// GLOBAL: WIZ8 0x659684
// GLOBAL: WIZ8 0x65968c
srMeshModel* g_cursor_model;
// GLOBAL: WIZ8 0x659690
std::shared_ptr<srTexture> g_cursor_texture;
// GLOBAL: WIZ8 0x659694
srModelInstance* g_cursor_node;
// GLOBAL: WIZ8 0x659698
uint64_t g_cursor_move_tick;
// GLOBAL: WIZ8 0x654ad0
int g_cursor_width;
// GLOBAL: WIZ8 0x654ad4
int g_cursor_height;
// GLOBAL: WIZ8 0x6596b4
int g_cursor_image_width;
// GLOBAL: WIZ8 0x6596b8
int g_cursor_image_height;
// GLOBAL: WIZ8 0x00603c50
double g_cursor_image_u_extent = 1.0;
// GLOBAL: WIZ8 0x00603c58
double g_cursor_image_v_extent = 1.0;
// GLOBAL: WIZ8 0x6596bc
int g_cursor_hotspot_x;
// GLOBAL: WIZ8 0x6596c0
int g_cursor_hotspot_y;

// FUNCTION: WIZ8 0x00428ab0
void AssertFailureHandler(const char* expression, const char* file, w8_long line,
                          const char* message)
{
    char text[2048];
    strcpy(text, "ERROR: You are viewing a message intended for the developers of "
                 "Wizardry 8. Please report the following information to technical "
                 "support. We apologize for this inconvenience.\n\n");
    char* details = text + strlen(text);
    if (message != 0 && *message != '\0') {
        std::snprintf(details, 0x6d3,
                      "Debug assertion in module %s line %d failed:\n\n"
                      "Expression [ %s ] evaluates to false.\n\n%s\n",
                      file, line, expression, message);
    } else {
        std::snprintf(details, 0x6d3,
                      "Debug assertion in module %s line %d failed:\n\n"
                      "Expression [ %s ] evaluates to false.\n",
                      file, line, expression);
    }
    g_pending_screen_state.id = W8_SCREEN_NONE;
    ShutdownWithErrorBox(text);
}

/* Reset both framebuffer pages and discard transient scene instances.
   Callers select whether to invalidate all dirty blocks or the normal region. */
static void ResetVideoDirtyState()
{
    g_ui_upload = {};
    PurgeInactiveSceneInstances(g_overlay_scenes.prerender[0].get());
    PurgeInactiveSceneInstances(g_overlay_scenes.frame[0].get());
    PurgeInactiveSceneInstances(g_overlay_scenes.prerender[1].get());
    PurgeInactiveSceneInstances(g_overlay_scenes.frame[1].get());
    InvalidateRegion(0, 0, 640, 480);
}

/* Clear the software-facing frame and retire every transient 2D overlay.
   The four scene walks are the same typed operation used during renderer
   bring-up; keeping the reset here avoids reproducing SurRender's node ABI at
   the menu call site. */
// FUNCTION: WIZ8 0x00422b10
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
// FUNCTION: WIZ8 0x00421bb0
bool InitializeVideoManager(void)
{
    g_world_pick_enabled = true;
    SetPickedModelInstance(0);
    g_fps_frame_count = 0;
    g_fps_window_tick = w8_get_ticks();
    Initialize16BitPixelFormatMasks();
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
        if (gXStatus.world_update_blocked) {
            ResumeMainGameWorld();
        }
        if (ghWindow && g_gerd) {
            g_video_active = true;
            if (g_gerd->isWindowOpen() == 0) {
                if (!OpenRendererWindow()) {
                    goto done;
                }
            }
            SDL_ShowWindow(ghWindow);
            SDL_RestoreWindow(ghWindow);
            SDL_RaiseWindow(ghWindow);
            ResetVideoDirtyState();
            SetRendererModePair();
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

// FUNCTION: WIZ8 0x00421dc0
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
    srExit();
    W8DestroyGameWindow(native_window);
}

/* Releases the renderer scene graph and 2D objects created by the video
   startup sequence; runs from ShutdownVideoManager before the CPU surface
   teardown. */
// FUNCTION: WIZ8 0x00423f30
void ShutdownVideoScenes(void)
{
    VideoRemoveToolTip();
    ReleaseRendererObject(g_cursor_node);
    g_cursor_model = nullptr;
    g_cursor_texture.reset();
    g_overlay_scenes.cursor.reset();
    g_overlay_scenes.user.reset();
    g_overlay_scenes.fullscreen.reset();
    g_overlay_scenes.frame[0].reset();
    g_surface_node = nullptr;
    g_overlay_scenes.frame[1].reset();
    g_overlay_scenes.square.reset();
    g_overlay_scenes.square_camera = nullptr;
    g_overlay_scenes.prerender[0].reset();
    g_overlay_scenes.prerender[1].reset();
    g_overlay_scenes.camera.reset();
    g_blit_material.reset();
    g_mouse_surface.reset();
    g_primary_color_surface.reset();
}

/* Selects the SDL packed format for SGP's 16-bit framebuffer and sprites.
   The startup configuration uses ARGB1555; the high bit is an opaque-pixel
   marker, while zero is the software blitters' transparent pixel. */
// FUNCTION: WIZ8 0x004265c0
void Initialize16BitPixelFormatMasks(void)
{
    switch (g_pixel_format) {
    case 7:
        g_sgp_pixel_format = SDL_PIXELFORMAT_RGB565;
        g_color_key = 0x7bef;
        break;
    case 8:
        g_sgp_pixel_format = SDL_PIXELFORMAT_XRGB1555;
        g_color_key = 0x3def;
        break;
    case 9:
        g_sgp_pixel_format = SDL_PIXELFORMAT_ARGB1555;
        g_color_key = 0x3def;
        break;
    }
}

/* Registers and creates the real top-level Wizardry window.  The odd use of
   the horizontal screen metric for both axes is present in the retail body and
   is preserved here; AdjustWindowRect turns the four stored client bounds into
   the outer window rectangle before CreateWindowEx. */
// FUNCTION: WIZ8 0x00425ec0
unsigned char CreateWizardryWindow(void)
{
    ghWindow = W8CreateGameWindow(g_screen_width, g_screen_height, g_fullscreen != 0);
    return ghWindow != 0;
}

/* SDL owns the 640x480 CPU frame used as SurRender color output. */
// FUNCTION: WIZ8 0x00426080
void ReleasePrimarySurface(void)
{
    primarySurface.reset();
}

unsigned char InitializePrimarySurface(void)
{
    if (g_sgp_pixel_format == SDL_PIXELFORMAT_UNKNOWN) return 0;
    primarySurface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(640, 480, g_sgp_pixel_format).release(), SDL_DestroySurface);
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

// FUNCTION: WIZ8 0x00422240
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

    srInit();
    g_gerd = std::make_unique<srGERD>(W8CreateNativeRenderDevice());

    if (g_gerd->createContext(ghWindow) != srGERD::ERROR_NONE || !OpenRendererWindow()) {
        g_gerd.reset();
        return 0;
    }
    srAssertSetFunc(AssertFailureHandler);
    s_setup_disables_audio = SDL_strncasecmp(audio_setting, "none", 4) == 0;
    return 1;
} catch (const std::exception&) {
    return false;
}

/* Applies the configured window style, asks SurRender for the matching display
   mode in fullscreen operation, and opens the renderer output window. */
// FUNCTION: WIZ8 0x00422800
unsigned char OpenRendererWindow(void)
{
    if (!W8ConfigureGameWindow(ghWindow, g_fullscreen != 0, g_screen_width, g_screen_height))
        return 0;
    if (g_gerd->openWindow(g_screen_width, g_screen_height) != srGERD::ERROR_NONE) return 0;
    g_flush_pending = true;
    return 1;
}

// FUNCTION: WIZ8 0x00423390
SDL_Surface* BeginVideoPresentation(void)
{
    const auto& source = *primarySurface;
    videoSurfaceOwner.reset(SDL_CreateSurface(source.w, source.h, source.format));
    g_flush_pending = false;
    return videoSurfaceOwner.get();
}

// FUNCTION: WIZ8 0x004234A0
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
// FUNCTION: WIZ8 0x00422550
bool VideoResizeWindow(void)
{
    if (g_fullscreen || !ghWindow || !g_gerd || !g_flush_pending) {
        return 0;
    }
    g_flush_pending = false;
    g_gerd->closeWindow(static_cast<srGERD::e_closeHint>(1));
    if (g_gerd->openWindow() == srGERD::ERROR_WINDOW_OPEN_FAILED) {
        return 0;
    }
    SetRendererModePair();
    ResetTransientRenderScenes();
    g_flush_pending = true;
    return 1;
}

// FUNCTION: WIZ8 0x00422970
void VideoFullScreen(bool enabled)
{
    g_fullscreen = enabled;
    if (ghWindow && g_gerd && g_flush_pending) {
        g_flush_pending = false;
        g_gerd->closeWindow(static_cast<srGERD::e_closeHint>(1));
        OpenRendererWindow();
    }
}

// FUNCTION: WIZ8 0x00422f10
void ResetTransientRenderScenes(void)
{
    ResetVideoDirtyState();
}

/* Same dirty-block / transient-scene reset as ResetTransientRenderScenes, but
   the full-screen invalidate uses flag 1 (the all-bits redraw path). */
// FUNCTION: WIZ8 0x00423150
void ClearVideoDirtyBlocks(void)
{
    ResetVideoDirtyState();
}

// FUNCTION: WIZ8 0x004277e0
bool VideoInspectorIsEnabled(void)
{
    return g_video_inspector_enabled;
}

// FUNCTION: WIZ8 0x00422050
void SuspendVideoManager(void)
{
    if (g_video_active) {
        PauseMainGameWorld();
        g_video_active = false;
        if (g_gerd) {
            g_flush_pending = false;
            g_gerd->closeWindow(static_cast<srGERD::e_closeHint>(0));
        }
        SDL_MinimizeWindow(ghWindow);
        FreeMouseCursor();
    }
}

// FUNCTION: WIZ8 0x004220b0
bool RestoreVideoManager(void)
{
    if (g_video_active) {
        return 1;
    }
    if (gXStatus.world_update_blocked) {
        ResumeMainGameWorld();
    }
    if (ghWindow && g_gerd) {
        g_video_active = true;
        SDL_ShowWindow(ghWindow);
        SDL_RestoreWindow(ghWindow);
        if (g_gerd->isWindowOpen() != 0 || OpenRendererWindow()) {
            SDL_RaiseWindow(ghWindow);
            ResetVideoDirtyState();
            SetRendererModePair();
        }
    }
    return 0;
}

/* The mode the engine falls back to: 640x480 at 16bpp, reported height first.
   Nothing here reads a configuration - the three constants are inline. */
// FUNCTION: WIZ8 0x00422af0
void GetCurrentVideoSettings(unsigned short* height, unsigned short* width, unsigned char* depth)
{
    *height = 0x1e0;
    *width = 0x280;
    *depth = 0x10;
}

// FUNCTION: WIZ8 0x00421ff0
unsigned char ClearPrimarySurface(void)
{
    SDL_FillSurfaceRect(primarySurface.get(), nullptr, 0);
    return 1;
}

void UpdateRenderElapsedTime(void);

/* Render one scene into either the full output or a logical 640x480 viewport.
   Scene fog follows the current world's static scene for ordinary overlays;
   the world passes retain their own fog by selecting preserve_fog. */
#ifdef WIZ8_RUNTIME_TESTS
static RuntimeWorldRenderData ObserveWorldRenderState();
#endif

// FUNCTION: WIZ8 0x00427850
void RenderScene(srScene* scene, srCamera* camera, const DesignRect* viewport, bool preserve_fog)
{
    w8_ulong width = g_gerd->getWidth();
    w8_ulong height = g_gerd->getHeight();

    if (scene == 0 || scene->getChildCount() == 0) {
        return;
    }
    if (viewport != 0) {
        const auto rect = ToSurfaceRect(*viewport, width, height);
        g_gerd->setViewPort(rect.x, rect.y, rect.w, rect.h);
        g_gerd->setScissor(rect.x, rect.y, rect.w, rect.h);
#ifdef WIZ8_RUNTIME_TESTS
        if (g_world != 0 && scene == g_world->static_scene) {
            RuntimeWorldRenderData data = ObserveWorldRenderState();
            data.applied_viewport[0] = rect.x;
            data.applied_viewport[1] = rect.y;
            data.applied_viewport[2] = rect.w;
            data.applied_viewport[3] = rect.h;
            RuntimeObserveWorld(RUNTIME_WORLD_VIEWPORT_APPLIED, data);
        }
#endif
    }
    if (!preserve_fog) {
        const auto fog = g_world ? g_world->static_scene->getFogColor() : glm::vec3(0);
        // NOLINTNEXTLINE(misc-include-cleaner)
        scene->setFogColor(glm::clamp(fog, 0.0f, 1.0f));
    }
    scene->render(*g_gerd, camera);
    g_gerd->flushRenderers();
    if (viewport != 0) {
        g_gerd->setViewPort(0, 0, width, height);
        g_gerd->setScissor(0, 0, width, height);
    }
}

#ifdef WIZ8_RUNTIME_TESTS
static RuntimeWorldRenderData ObserveWorldRenderState()
{
    RuntimeWorldRenderData data;
    memset(&data, 0, sizeof(data));
    data.level = g_status.current_level;
    data.flags = (g_world_render_enabled ? 1UL : 0UL) | (g_world_blacked_out ? 2UL : 0UL) |
                 (g_video_active ? 4UL : 0UL) | (g_monster_shadow_updates_enabled ? 8UL : 0UL) |
                 (g_render_mesh_sky ? 16UL : 0UL) | (g_world_pick_enabled ? 32UL : 0UL);
    data.scene_children = g_world->static_scene->getChildCount();
    if (g_world->octree != 0 && !g_world->psrMeshes.empty()) {
        for (w8_ulong index = 0; index < g_world->octree->m_meshCount; ++index) {
            srModelInstance* instance = g_world->psrMeshes[index];
            if (instance != 0 && !instance->testFlag(srNode::FLAG_DISABLE)) {
                ++data.visible_meshes;
            }
        }
    }
    data.viewport[0] = g_viewport.left;
    data.viewport[1] = g_viewport.top;
    data.viewport[2] = g_viewport.right;
    data.viewport[3] = g_viewport.bottom;
    data.renderer_size[0] = g_gerd->getWidth();
    data.renderer_size[1] = g_gerd->getHeight();
    const auto statistics = g_gerd->getStatistics();
    data.draw_calls = statistics.draw_calls;
    glm::vec3 camera = cameraLocation();
    data.camera[0] = camera.x;
    data.camera[1] = camera.y;
    data.camera[2] = camera.z;
    return data;
}
#endif

/* The central renderer frame transaction. It owns the page flip, every scene
   pass, optional world picking, transient-node retirement, and frame timing;
   callers do not reproduce any subset of that lifecycle. */
// FUNCTION: WIZ8 0x00426790
void RenderFrame(void)
{
    /* The movie adapter owns presentation until FinishVideoPresentation. */
    if (!g_flush_pending) {
        return;
    }
    glm::vec3 clear_color(0);
    glm::vec3 saved_world_position;
    glm::vec3 shifted_world_position;
    unsigned int next_page;
    w8_ulong now;
    float elapsed;
    float frames_per_second;
    srScene* first_page;
    srScene* second_page;
    srScene* retire_prerender;
    srScene* retire_overlay;

    if (!g_video_active) {
        return;
    }

    if (g_trigger_action_active && GetWorld() != 0) {
        saved_world_position = cameraLocation();
        shifted_world_position = saved_world_position + g_trigger_action_scene_offset;
        SetWorldScenePosition(GetWorld(), &shifted_world_position);
    }
    if (g_world != 0) {
        if (!IsSkyEnabled()) {
            GetWorldLightValue(g_world, &clear_color);
        } else {
            clear_color =
                // NOLINTNEXTLINE(misc-include-cleaner)
                glm::clamp(g_world->static_scene->getFogColor(), 0.0f, 1.0f);
        }
    }

    g_gerd->setClearColor(clear_color.x, clear_color.y, clear_color.z, 1.0f);
    SyncSystemCursor();
    UpdateUiHelp();
    FlushUiUpload();
    if (g_gerd != 0) {
        g_gerd->resetStatistics();
    }
    srCore.getStatisticsManager()->reset();
    g_gerd->beginFrame();
#ifdef WIZ8_RUNTIME_TESTS
    RuntimeObserve(RUNTIME_FRAME_BEGIN, g_fps_frame_count, g_current_screen_state.id,
                   g_video_active);
#endif
    UpdateRenderElapsedTime();

    if (!g_monster_shadow_updates_enabled) {
        if (g_world_blacked_out) {
            goto clear_viewport;
        }
    } else if (g_world_blacked_out || !g_render_mesh_sky || g_secondary_world == 0) {
    clear_viewport: {
        w8_ulong height = g_gerd->getHeight();
        w8_ulong width = g_gerd->getWidth();
        const auto rect = ToSurfaceRect(g_viewport, width, height);
        g_gerd->setScissor(rect.x, rect.y, rect.w, rect.h);
        g_gerd->clear(srDD::BUFFER_COLOR | srDD::BUFFER_DEPTH);
        g_gerd->setScissor(0, 0, width, height);
    }
    }

    first_page =
        g_active_page ? g_overlay_scenes.prerender[0].get() : g_overlay_scenes.prerender[1].get();
    second_page =
        g_active_page ? g_overlay_scenes.prerender[1].get() : g_overlay_scenes.prerender[0].get();
    RenderScene(first_page, g_overlay_scenes.camera.get(), 0, false);
    RenderScene(second_page, g_overlay_scenes.camera.get(), 0, false);
    g_gerd->setTextureReduction(g_resident_texture_policy);

    if (g_render_mesh_sky && g_secondary_world != 0 && g_monster_shadow_updates_enabled) {
        RenderScene(g_secondary_world->static_scene, g_secondary_world->camera, &g_viewport, false);
    }
    if (g_world != 0 && g_world_render_enabled) {
#ifdef WIZ8_RUNTIME_TESTS
        RuntimeWorldRenderData world_observation = ObserveWorldRenderState();
        RuntimeObserveWorld(RUNTIME_WORLD_RENDER_BEGIN, world_observation);
#endif
        g_gerd->setTextureReduction(g_resident_texture_policy);
        if (!g_world_pick_enabled || g_cursor_hotspot_x + g_cursor_width < g_viewport.left ||
            g_cursor_hotspot_y + g_cursor_height < g_viewport.top ||
            g_viewport.right < g_cursor_hotspot_x + g_cursor_width ||
            g_viewport.bottom < g_cursor_hotspot_y + g_cursor_height) {
            RenderScene(g_world->static_scene, g_world->camera, &g_viewport, true);
        } else {
            int half_width = (g_viewport.right - g_viewport.left) / 2;
            int half_height = (g_viewport.bottom - g_viewport.top) / 2;
            srGERD::Pick pick;
            pick.position.x = (g_cursor_hotspot_x - half_width - g_viewport.left + g_cursor_width) /
                              static_cast<float>(half_width);
            pick.position.y = -static_cast<float>(g_cursor_hotspot_y - half_height -
                                                  g_viewport.top + g_cursor_height) /
                              half_height;
            pick.position.z = 1.0f;
            pick.selected_model = 0;
            pick.triangle_index = 0;
            g_gerd->setPickKey(0);
            g_gerd->pushPick(pick);
            RenderScene(g_world->static_scene, g_world->camera, &g_viewport, true);
            pick = *g_gerd->popPick();
            SetPickedModelInstance(pick.selected_model);
            ResolvePickedProp(g_world);
        }
#ifdef WIZ8_RUNTIME_TESTS
        RuntimeObserveWorld(RUNTIME_WORLD_RENDER_END, ObserveWorldRenderState());
#endif
    }

    g_gerd->setTextureReduction(0);
#ifdef WIZ8_RUNTIME_TESTS
    if (getenv("WIZ8_RUNTIME_HIDE_OVERLAY") == 0)
#endif
        if (g_overlay_scenes_enabled) {
            if (!g_fullscreen_scene_last) {
                RenderScene(g_overlay_scenes.fullscreen.get(), g_overlay_scenes.camera.get(),
                            nullptr, false);
            }
            RenderScene(g_overlay_scenes.frame[0].get(), g_overlay_scenes.camera.get(), 0, false);
            RenderScene(g_overlay_scenes.user.get(), g_overlay_scenes.camera.get(), 0, false);
            RenderScene(g_overlay_scenes.square.get(), g_overlay_scenes.square_camera, nullptr,
                        false);
            if (g_fullscreen_scene_last) {
                RenderScene(g_overlay_scenes.fullscreen.get(), g_overlay_scenes.camera.get(),
                            nullptr, false);
            }
            if (g_cursor_scene_enabled) {
                RenderScene(g_overlay_scenes.cursor.get(), g_overlay_scenes.camera.get(), 0, false);
            }
        }
    g_gerd->endFrame();
#ifdef WIZ8_RUNTIME_TESTS
    RuntimeObserve(RUNTIME_FRAME_SUBMITTED, g_fps_frame_count, g_current_screen_state.id, 0);
#endif

    if (g_screenshot_pending) {
        SaveJpegScreenshot();
        g_screenshot_pending = false;
    }
    if (g_auto_capture) {
        now = w8_get_ticks();
        if (now < g_last_capture_tick || g_last_capture_tick + g_frame_reset_interval < now) {
            g_screenshot_pending = true;
            g_last_capture_tick = now;
        }
    }

    next_page = g_active_page ^ 1;
    g_active_page = next_page;
    ++g_fps_frame_count;
    retire_prerender =
        next_page ? g_overlay_scenes.prerender[1].get() : g_overlay_scenes.prerender[0].get();
    retire_overlay = next_page ? g_overlay_scenes.frame[1].get() : g_overlay_scenes.frame[0].get();
    PurgeInactiveSceneInstances(retire_prerender);
    PurgeInactiveSceneInstances(retire_overlay);

    now = w8_get_ticks();
    elapsed = static_cast<float>(now - g_fps_window_tick);
    frames_per_second = g_fps_frame_count / elapsed * 1000.0f;
    if (g_fps_frame_count > 50) {
        g_fps_window_tick = w8_get_ticks();
        g_fps_frame_count = 0;
    }
    g_seconds_per_frame = 1.0f / frames_per_second;
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
unsigned char RenderWorldToSurface(SDL_Surface* target, const DesignRect* rect,
                                   bool render_secondary)
{
    srGERD* gerd = g_gerd.get();
    glm::vec3 clear_color{1.0f};
    gerd->beginFrame();
    gerd->setTextureReduction(g_resident_texture_policy);
    gerd->setScissor(0, 0, gerd->getWidth(), gerd->getHeight());
    const DesignRect bounds = rect != nullptr ? *rect : g_viewport;
    const auto viewport = ToSurfaceRect(bounds, gerd->getWidth(), gerd->getHeight());
    gerd->setViewPort(viewport.x, viewport.y, viewport.w, viewport.h);
    if (IsFogEnabled()) {
        GetLightDirection(&clear_color);
    } else {
        GetWorldLightValue(g_world, &clear_color);
    }
    gerd->setClearColor(clear_color.x, clear_color.y, clear_color.z, 1.0f);
    if (g_inverted_depth_render) {
        gerd->setClearDepth(0.0);
    }
    gerd->clear(srDD::BUFFER_COLOR | srDD::BUFFER_DEPTH);
    if (render_secondary && g_render_mesh_sky && g_secondary_world != 0) {
        g_secondary_world->static_scene->render(*gerd, g_secondary_world->camera);
    }
    g_world->static_scene->render(*gerd, g_world->camera);
    gerd->endFrame();
    if (g_inverted_depth_render) {
        gerd->setClearDepth(1.0);
    }
    gerd->flushRenderers();
    gerd->setTextureReduction(0);
    if (rect != 0) {
        gerd->setViewPort(0, 0, gerd->getWidth(), gerd->getHeight());
    }
    SDL_Surface* buffer = gerd->lockBuffer();
    if (buffer != 0) {
        StretchCpuSurface(*buffer, *target, SDL_SCALEMODE_NEAREST);
        gerd->unlockBuffer();
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x00427440
void InvalidateRendererTextureCache(void)
{
    if (g_gerd != 0) {
        g_gerd->invalidateTextureCache();
    }
}

/* The renderer and both static scenes take the light direction as their fog
   colour. The renderer is flushed before the new colour lands. */
// FUNCTION: WIZ8 0x00427380
void PublishLightDirection(const glm::vec3* direction)
{
    if (g_gerd != 0) {
        g_gerd->flush();
        g_gerd->setFogColor(glm::vec4(*direction, 0));
    }
    if (g_world != 0) {
        g_world->static_scene->setFogColor(*direction);
    }
    if (g_secondary_world != 0) {
        g_secondary_world->static_scene->setFogColor(*direction);
    }
}

// FUNCTION: WIZ8 0x00427230
void SetRendererAutoFlipEnabled(bool enabled)
{
    if (g_gerd != 0) {
        if ((!enabled && g_gerd->isEnabled(srGERD::ENABLE_AUTO_FLIP)) ||
            (enabled && !g_gerd->isEnabled(srGERD::ENABLE_AUTO_FLIP))) {
            g_gerd->toggle(srGERD::ENABLE_AUTO_FLIP);
        }
    }
}

/* The static scene's fog colour clamped into 0..1 for the caller; an empty
   world reports black. */
// FUNCTION: WIZ8 0x00427290
void GetWorldColour(glm::vec3* colour)
{
    if (g_world != 0) {
        *colour =
            // NOLINTNEXTLINE(misc-include-cleaner)
            glm::clamp(g_world->static_scene->getFogColor(), 0.0f, 1.0f);
        return;
    }
    *colour = glm::vec3(0);
}

// FUNCTION: WIZ8 0x00427260
bool RendererBufferIsLockable(void)
{
    return g_gerd && g_gerd->isWindowOpen();
}

// FUNCTION: WIZ8 0x00427830
void SetWorldModelPickingEnabled(bool enabled)
{
    g_world_pick_enabled = enabled;
    if (!enabled) {
        SetPickedModelInstance(0);
    }
}

/* Mouse cursor scene and rendering. */

// The four sprite builders share the same clockwise quad and vertically flipped UVs.
static void SetSpriteQuad(srMeshModel& model, float width, float height,
                          glm::vec2 uv_scale = glm::vec2(0), glm::vec2 uv_offset = glm::vec2(0))
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

// FUNCTION: WIZ8 0x00424EB0
srModelInstance* MakePolygonBrush(srNode* parent, std::shared_ptr<SDL_Surface> surface,
                                  double width, double height, float mapping_x, float mapping_y,
                                  float mapping_width, float mapping_height, unsigned char overlay)
{
    std::shared_ptr<srMeshModel> model;
    stModelInstance2D* instance;
    srShader shader;

    model = std::make_shared<srMeshModel>(0L, 0L);
    model->setName("Video2DMakePolygonBrush");

    SetSpriteQuad(*model, static_cast<float>(width), static_cast<float>(height),
                  {mapping_width, mapping_height}, {mapping_x, mapping_y});

    shader.value = overlay ? g_surface_alpha_blend_shader_bits : g_surface_opaque_shader_bits;
    if (!surface) {
        shader.value &= ~srShader::MASK_TEXTURING;
    } else {
        auto texture = std::make_shared<srTextureMap>();
        texture->setName("Video2DMakePolygonBrush");
        texture->setSurfacePtr(surface);
        texture->setCorrection(srTextureIFace::CORRECTION_FASTEST);
        texture->setMagFilter(srTextureIFace::FILTER_BEST);
        texture->setMinFilter(srTextureIFace::FILTER_BEST);
        texture->setMipmap(srTextureIFace::MIPMAP_NONE);
        texture->setWrapS(srTextureIFace::WRAP_CLAMP);
        texture->setWrapT(srTextureIFace::WRAP_CLAMP);
        texture->enableHint(overlay ? srTextureIFace::HINT_ONE_BIT_ALPHA
                                    : srTextureIFace::HINT_NO_ALPHA);
        texture->enableHint(srTextureIFace::HINT_NO_MIPMAPS);
        model->setMaterial(g_blit_material, 0, srMeshModel::SIDE_FRONT);
        model->setTexture(std::move(texture), 0, 0);
    }
    model->setShader(shader, 0);

    instance = new stModelInstance2D(parent);
    instance->setName("Video2DMakePolygonBrush");
    instance->SetModel(model);
    instance->configure2D(static_cast<unsigned short>(width * 640.0),
                          static_cast<unsigned short>(height * 480.0));
    return instance;
}

// FUNCTION: WIZ8 0x00425190
stModelInstance2D* CreateSpriteFromTexture(std::shared_ptr<srTextureIFace> texture, double width,
                                           double height, bool keep_aspect, bool overlay)
{
    srShader shader;
    int w = static_cast<int>(width * 640.0);
    int h = static_cast<int>(height * 480.0);

    auto model = std::make_shared<srMeshModel>(0L, 0L);
    model->setName("Video2DMakePolygonBrush");

    float step = g_surface_scale * (g_float_one / w);
    SetSpriteQuad(*model, static_cast<float>(width), static_cast<float>(height),
                  glm::vec2(g_float_one - (step + step)), glm::vec2(step));

    shader.value = keep_aspect ? g_surface_alpha_blend_shader_bits : g_surface_opaque_shader_bits;
    if (!texture) {
        shader.value &= ~srShader::MASK_TEXTURING;
    } else {
        model->setMaterial(g_blit_material, 0, srMeshModel::SIDE_FRONT);
        model->setTexture(std::move(texture), 0, 0);
    }
    model->setShader(shader, 0);

    stModelInstance2D* instance = new stModelInstance2D(0);
    if (instance) {
        instance->render_state.width = static_cast<unsigned short>(w);
        instance->render_state.height = static_cast<unsigned short>(h);
        instance->setName("Video2DMakePolygonBrush");
        instance->SetModel(model);
        if (overlay) {
            instance->overlay_scene_flag |= 1;
        }
    }
    return instance;
}

// FUNCTION: WIZ8 0x00484A40
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

// FUNCTION: WIZ8 0x00427c90
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
    delete g_cursor_node;
    mapping_scale = g_surface_scale / extent;
    g_cursor_node = MakePolygonBrush(g_overlay_scenes.cursor.get(), g_mouse_surface, extent / 640.0,
                                     extent / 480.0, mapping_scale, mapping_scale, 1.0f, 1.0f, 1);
    g_cursor_node->setName("MouseResize");
    PositionMouseCursor(g_cursor_width, g_cursor_height, false);
    g_cursor_model = static_cast<srMeshModel*>(g_cursor_node->getModel());
    g_cursor_model->enable(srMeshModel::CONTROL_SORTED_RENDERING);
    g_cursor_model->setSortBias(-100000.0f);
    g_cursor_model->setName("Mouse Cursor Mesh");
    g_cursor_texture = std::static_pointer_cast<srTexture>(g_cursor_model->textures[0][0]);
    g_cursor_texture->setWrapS(srTextureIFace::WRAP_CLAMP);
    g_cursor_texture->setWrapT(srTextureIFace::WRAP_CLAMP);
    return true;
}

/* Move the OS/system cursor when the hotspot changes, then resync the rendered
   cursor. */
// FUNCTION: WIZ8 0x00427f00
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

// FUNCTION: WIZ8 0x00427ab0
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
    g_cursor_image_u_extent = g_cursor_image_width * g_inverse_screen_width;
    g_cursor_image_v_extent = g_cursor_image_height * g_inverse_screen_height;
    ClearMouseSurface();
    return BlitVideoObjectToColorSurface(sprites, region, *g_mouse_surface, 0, 0);
}

// FUNCTION: WIZ8 0x00427fc0
void BlitToMouseCursor(const SpriteSheet& sprites, std::uint16_t region, std::uint16_t x,
                       std::uint16_t y)
{
    BlitVideoObjectToColorSurface(sprites, region, *g_mouse_surface, x, y);
}

// FUNCTION: WIZ8 0x00427ff0
void RefreshMouseCursorTexture(void)
{
    g_cursor_texture->invalidate();
}

/* Whether the tracked cursor position lies inside the render viewport. The
   two automap callers use it, but nothing names the viewport as automap-only
   state; placed here with the neighbouring cursor bodies. */
// FUNCTION: WIZ8 0x00427e70
bool ClearMouseSurface(void)
{
    SDL_FillSurfaceRect(g_mouse_surface.get(), nullptr, 0);
    return true;
}

/* Distinct from IsCursorInsideViewport: the whole cursor image must fit
   inside the viewport, not just the hotspot point. */
// FUNCTION: WIZ8 0x00428030
bool IsCursorImageInsideViewport(void)
{
    if (g_cursor_width >= g_viewport.left && g_cursor_height >= g_viewport.top &&
        g_cursor_width + g_cursor_image_width <= g_viewport.right &&
        g_cursor_height + g_cursor_image_height <= g_viewport.bottom) {
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x00428070
bool IsCursorInsideViewport(void)
{
    int x = g_cursor_hotspot_x + g_cursor_width;
    int y = g_cursor_hotspot_y + g_cursor_height;
    return x >= g_viewport.left && y >= g_viewport.top && x <= g_viewport.right &&
           y <= g_viewport.bottom;
}

// FUNCTION: WIZ8 0x004280c0
void WarpSystemCursor(int x, int y)
{
    WarpGameMouse(ghWindow, x, y);
}

// FUNCTION: WIZ8 0x00428140
void PositionMouseCursor(int width, int height, bool reset_tick)
{
    glm::dvec3 location;

    if (g_mouse_surface) {
        g_cursor_width = width < 641 ? width : 640;
        g_cursor_height = height < 481 ? height : 480;
        if (g_cursor_node) {
            location.x = g_cursor_width / 640.0 + g_mouse_surface->w / 1280.0;
            location.y = 1.0 - g_cursor_height / 480.0 - g_mouse_surface->h / 960.0;
            location.z = 0.0;
            g_cursor_node->setLocation(location);
            if (reset_tick) {
                g_cursor_move_tick = w8_get_ticks();
            }
        }
    }
}

/* Milliseconds since the cursor last moved (or was repositioned with the
   reset-tick arm). */
// FUNCTION: WIZ8 0x00428220
unsigned int GetMillisecondsSinceCursorMove(void)
{
    return w8_get_ticks() - g_cursor_move_tick;
}

/* The tracked cursor as viewport-relative 0..1 coordinates, or zero when it
   lies outside. Same tracked position and viewport as the query above. */
// FUNCTION: WIZ8 0x00428230
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
// FUNCTION: WIZ8 0x004282F0
glm::vec2 GetCursorScaledPosition()
{
    return glm::vec2(g_cursor_hotspot_x + g_cursor_width, g_cursor_hotspot_y + g_cursor_height) *
           glm::vec2(g_viewport_x_scale, g_viewport_y_scale);
}

/* Keep the rendered cursor synchronized with the OS cursor. The input layer
   already reports design-space coordinates, so windowed and fullscreen share
   the same position; fullscreen coordinates are clamped to the 640x480 game
   surface. */
// FUNCTION: WIZ8 0x00428340
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

/* Fill a caller-supplied point with the sum of the two global pairs, after the
   refresh that recomputes them. Fifty callers reach this; what the two pairs
   mean is not established beyond the sum, so both keep address-qualified names. */
// FUNCTION: WIZ8 0x004284f0
void SGPMouseGetPos(SDL_Point* point)
{
    if (point != 0) {
        SyncSystemCursor();
        point->x = g_cursor_hotspot_x + g_cursor_width;
        point->y = g_cursor_hotspot_y + g_cursor_height;
    }
}

// FUNCTION: WIZ8 0x00428520
bool IsCursorInRectangle(int left, int top, int right, int bottom)
{
    SDL_Point point;

    SGPMouseGetPos(&point);
    return point.x >= left && point.x <= right && point.y >= top && point.y <= bottom;
}

/* Creates the shipped 128x128 mouse polygon inside its dedicated scene. */
// FUNCTION: WIZ8 0x004285c0
unsigned char InitializeMouseCursorScene(void)
{
    auto cursor_scene = std::make_unique<srScene>(nullptr);
    cursor_scene->detach();
    cursor_scene->setAmbientLight(0.0f, 0.0f, 0.0f);
    cursor_scene->setFogColor(0.0f, 0.0f, 0.0f);
    g_overlay_scenes.cursor = std::move(cursor_scene);
    g_overlay_scenes.cursor->setName("Mouse Cursor Scene");
    if (!g_mouse_surface) {
        return 0;
    }
    ClearMouseSurface();
    g_cursor_node =
        MakePolygonBrush(g_overlay_scenes.cursor.get(), g_mouse_surface, 0.2, 0.26666666666666666,
                         g_surface_scale / 128.0f, g_surface_scale / 128.0f, 1.0f, 1.0f, 1);
    if (g_cursor_node) {
        g_cursor_node->setName("MouseInit");
        g_cursor_model = static_cast<srMeshModel*>(g_cursor_node->getModel());
        g_cursor_model->enable(srMeshModel::CONTROL_SORTED_RENDERING);
        g_cursor_model->setSortBias(-100000.0f);
        g_cursor_texture = std::static_pointer_cast<srTexture>(g_cursor_model->textures[0][0]);
        g_cursor_texture->setWrapS(srTextureIFace::WRAP_CLAMP);
        g_cursor_texture->setWrapT(srTextureIFace::WRAP_CLAMP);
        PositionMouseCursor(640, 480, true);
    }
    return 1;
}

// GLOBAL: WIZ8 0x65970d
unsigned char g_world_render_enabled;

/* Retire a 2D node: invalidate the texture its model exposes and release it. */
void ReleaseSurfaceNode(srNode* node)
{
    if (node == 0) {
        return;
    }
    srMeshModel* model =
        static_cast<srMeshModel*>(static_cast<stModelInstance2D*>(node)->getModel());
    if (model != 0) {
        srTextureIFace* texture = model->getTexture(0, 0);
        if (texture != 0) {
            texture->invalidate();
        }
    }
    delete node;
}

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
    g_surface_node->updateRectangle(g_gerd.get(), upload.x, upload.y, upload.x + upload.w,
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

// FUNCTION: WIZ8 0x00426250
void SetViewport(const DesignRect& bounds)
{
    float fractional_left;
    float fractional_top;
    float fractional_right;
    float fractional_bottom;
    srCamera::Rect plane;

    if (g_gerd != 0 && g_flush_pending) {
        g_gerd->flush();
    }
    g_viewport = bounds;
    fractional_left = bounds.left * g_viewport_x_scale;
    fractional_top = bounds.top * g_viewport_y_scale;
    fractional_right = g_viewport.right * g_viewport_x_scale;
    fractional_bottom = g_viewport.bottom * g_viewport_y_scale;

    if (g_world != 0 && g_world->camera != 0) {
        g_world->camera->setViewPlane(
            3.14159265358979323846 * g_float_inverse_half_turn_degrees * 85.0f,
            3.14159265358979323846 * g_float_inverse_half_turn_degrees * 71.0f);
        const auto view = g_world->camera->getViewPlane().rectangle;

        plane.left = fractional_left * (view.right - view.left) + view.left;
        plane.right = fractional_right * (view.right - view.left) + view.left;
        plane.bottom = (g_double_one - fractional_bottom) * (view.top - view.bottom) + view.bottom;
        plane.top = (g_double_one - fractional_top) * (view.top - view.bottom) + view.bottom;

        g_world->camera->setViewPlane(plane, 1.0);
        if (g_secondary_world != 0) {
            g_secondary_world->camera->setViewPlane(plane, 1.0);
        }
        if (g_camera_sway_active) {
            SetCameraSwayMode(g_world->camera, 1);
        }
    }
}

// FUNCTION: WIZ8 0x004229d0
void PrintScreen(void)
{
    g_screenshot_pending = true;
}

/* The debug stats readout drawn over the primary surface: a black band plus
   the frame-rate line always, the object/poly/vertex counters, texture and
   pagefile memory in mode 2, or the scaled camera position in mode 3. */
// FUNCTION: WIZ8 0x00427460
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
                                  fmt::format("OC: {:d}", g_world->level->m_submitted_polygons));
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
                                  {text_left, text_top + 0x64},
                                  fmt::format("TM: {:d}K", g_gerd->getTextureCacheUsed()));
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

// FUNCTION: WIZ8 0x004277d0
void VideoInspectorEnable(void)
{
    g_video_inspector_enabled = true;
}

// GLOBAL: WIZ8 0x006548a0
std::int32_t g_help_box_width;
// GLOBAL: WIZ8 0x00654acc
std::int32_t g_help_box_height;

/* Live tooltip sprites, left to right. */
static std::vector<stModelInstance2D*> g_tooltip_nodes;

// FUNCTION: WIZ8 0x00429770
void VideoRemoveToolTip(void)
{
    for (auto* node : g_tooltip_nodes) {
        delete node;
    }
    g_tooltip_nodes.clear();
}

// FUNCTION: WIZ8 0x00424A90
srNode* VideoMakePoster(std::shared_ptr<SDL_Surface> surface, float width, float height,
                        bool additive)
{
    srTextureIFace::e_hint hint;
    auto texture = std::make_shared<srTextureMap>();
    texture->setMipmapBias(-8.0f);
    texture->setName("VideoMakePoster");
    texture->setSurfacePtr(surface);
    texture->setWrapS(srTextureIFace::WRAP_CLAMP);
    texture->setWrapT(srTextureIFace::WRAP_CLAMP);
    if (!additive) {
        hint = srTextureIFace::HINT_NO_ALPHA;
    } else {
        hint = srTextureIFace::HINT_ONE_BIT_ALPHA;
    }
    texture->enableHint(hint);
    return MakePosterQuad(std::move(texture), width, height, additive);
}

void PresentMenuOverlayFrame(void)
{
    srNode::ProcessInfo process;

    FlushUiUpload();
    g_gerd->beginFrame();
    process.renderer = g_gerd.get();
    g_surface_node->process(process, srNode::PROCESS_RENDER);
    g_gerd->flushRenderers();
    g_gerd->endFrame();
}

// FUNCTION: WIZ8 0x00425570
void SetPrimarySurfaceTextureHint2Enabled(bool enabled)
{
    if (g_surface_node) {
        g_surface_node->setAlphaTestEnabled(enabled);
    }
}

// FUNCTION: WIZ8 0x00424040
unsigned char InitializeMouseSurface(void)
{
    if (g_pixel_format == 7) {
        g_mouse_surface = std::shared_ptr<SDL_Surface>(
            CreateCpuStorage(128UL, 128UL, SDL_PIXELFORMAT_RGB565).release(), SDL_DestroySurface);
    } else if (g_pixel_format == 8) {
        g_mouse_surface = std::shared_ptr<SDL_Surface>(
            CreateCpuStorage(128UL, 128UL, SDL_PIXELFORMAT_XRGB1555).release(), SDL_DestroySurface);
    } else if (g_pixel_format == 9) {
        g_mouse_surface = std::shared_ptr<SDL_Surface>(
            CreateCpuStorage(128UL, 128UL, SDL_PIXELFORMAT_ARGB1555).release(), SDL_DestroySurface);
    } else {
        return 0;
    }
    ClearMouseSurface();
    return 1;
}

/* A black, fog-free scene with no parent, used for the 2D overlay layers. */
static std::unique_ptr<srScene> createOverlayScene(const char* name)
{
    auto scene = std::make_unique<srScene>(nullptr);
    scene->detach();
    scene->setName(name);
    scene->setAmbientLight(0.0f, 0.0f, 0.0f);
    scene->setFogColor(0.0f, 0.0f, 0.0f);
    return scene;
}

/* A 2D overlay camera over the unit view plane, from (0, 0) to (right, top). */
static std::unique_ptr<srCamera> createOverlayCamera(srNode* parent, const char* name, double right,
                                                     double top)
{
    srCamera::Rect view;

    auto camera = std::make_unique<srCamera>(parent);
    if (!parent) {
        camera->detach();
    }
    camera->setName(name);
    camera->setClipRange(0.01, 2.0);
    camera->setLocation(0.0, 0.0, -1.0);
    camera->setRotation(0.0, 0.0, 0.0);
    view.left = 0.0;
    view.bottom = 0.0;
    view.right = right;
    view.top = top;
    camera->setViewPlane(view, 1.0);
    camera->setEnvironmentRange(0.0f, 0.0f);
    return camera;
}

// FUNCTION: WIZ8 0x00423500
unsigned char InitializeRendererSceneObjects(void)
{
    std::shared_ptr<srMaterial> material;
    glm::vec4 material_value;
    char renderer_name[128];

    InitializeMouseSurface();
    g_overlay_scenes.user = createOverlayScene("2D User Overlay Scene");
    g_overlay_scenes.fullscreen = createOverlayScene("Full Screen Overlay Scene");
    g_overlay_scenes.frame[0] = createOverlayScene("2D Overlay Scene (0)");
    g_overlay_scenes.frame[1] = createOverlayScene("2D Overlay Scene (1)");
    g_overlay_scenes.square = createOverlayScene("2D Square Overlay Scene");
    g_overlay_scenes.prerender[0] = createOverlayScene("2D Pre-render Overlay Scene (0)");
    g_overlay_scenes.prerender[1] = createOverlayScene("2D Pre-render Overlay Scene (1)");
    g_overlay_scenes.camera = createOverlayCamera(nullptr, "2D Overlay Camera", 1.0, 1.0);
    g_overlay_scenes.square_camera =
        createOverlayCamera(g_overlay_scenes.square.get(), "2D Square Overlay Camera", 1.0, 0.75)
            .release();

    material = std::make_shared<srMaterial>();
    g_blit_material = material;
    material->setName("Blit Rect Material");
    material_value = glm::vec4(1.0f);
    material->setEmissive(material_value);
    material_value = glm::vec4(0.0f);
    material->setDiffuse(material_value);
    material->setSpecular(material_value);
    material->setOpacity(1.0);

    g_viewport.left = 0;
    g_viewport.top = 0;
    g_viewport.right = 0;
    g_surface_opaque_shader_bits = 0x100a017;
    g_surface_alpha_blend_shader_bits = 0x100c0b7;
    g_viewport.bottom = 0;

    /* Shares the CPU surface's pixels, so they outlive its recreation. */
    g_primary_color_surface = primarySurface;

    g_surface_node =
        new stSurface2D(g_primary_color_surface, 640, 480, g_overlay_scenes.frame[0].get(), 128);
    if (!g_surface_node) return 0;

    strncpy(renderer_name, g_gerd->getName().c_str(), 127);
    renderer_name[127] = 0;
    SDL_strupr(renderer_name);
    if (strstr(renderer_name, "GLIDE")) {
        g_surface_node->enableTextureUpdateFlags(stSurface2D::UPDATE_FULL_TILE);
    }
    g_renderer_mode = strstr(renderer_name, "DIRECT3D") || strstr(renderer_name, "GLIDE") ||
                      strstr(renderer_name, "SOFTWARE2");
    return 1;
}

// FUNCTION: WIZ8 0x004263f0
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

// FUNCTION: WIZ8 0x00426500
void PurgeInactiveSceneInstances(srScene* scene)
{
    srNode* node;

    if (!scene) return;
    node = scene->first_child_;
    while (node) {
        srNode* next = node->next_sibling_;
        unsigned char display_state = 3;
        if (auto* instance = dynamic_cast<stModelInstance*>(node)) {
            /* Retail also applies the 2D byte test to 3D highlight alpha.
               This representation read does not establish a 3D state member. */
            memcpy(&display_state, &instance->highlight_colour.w, sizeof(display_state));
        } else if (auto* sprite = dynamic_cast<stModelInstance2D*>(node)) {
            display_state = sprite->displayState();
        }
        if (display_state != 3) {
            ReleaseSurfaceNode(node);
        }
        node = next;
    }
}

// FUNCTION: WIZ8 0x00425820
void ClearNodeFlag(srNode* node)
{
    if (node) {
        node->setFlag(srNode::FLAG_DISABLE);
    }
}

// FUNCTION: WIZ8 0x00427810
srModelInstance* GetPickedModelInstance(void)
{
    return g_current_model_instance;
}

// FUNCTION: WIZ8 0x00427820
void SetPickedModelInstance(srModelInstance* value)
{
    g_current_model_instance = value;
}

// FUNCTION: WIZ8 0x00428010
unsigned char DisableCursorScene(void)
{
    g_cursor_scene_enabled = false;
    return 1;
}

// FUNCTION: WIZ8 0x00428020
unsigned char EnableCursorScene(void)
{
    g_cursor_scene_enabled = true;
    return 1;
}

/* Release an srClass, leaving the renderer in 2D mode, or in the paired
   mode when overlay_scene_flag bit 0 says otherwise. Recovered callers pass
   stModelInstance2D sprites (highlight / formation board). */
// FUNCTION: WIZ8 0x004257F0
void ReleaseObject(stModelInstance2D* object)
{
    if ((object->overlay_scene_flag & 1) != 0) {
        SetOverlayRenderMode();
    } else {
        SetRendererModePair();
    }
    delete object;
}

/* Rotate a 2D sprite node by `degrees` about z and dirty the renderer mode
   word its overlay_scene_flag bit 0 selects. */
// FUNCTION: WIZ8 0x00425840
void RotateNodeInDegrees(srNode* node, int degrees)
{
    node->setRotation(0.0, 0.0, 3.141592653589793 * g_float_inverse_half_turn_degrees * degrees);
    if ((static_cast<stModelInstance2D*>(node)->overlay_scene_flag & 1) != 0) {
        SetOverlayRenderMode();
    } else {
        SetRendererModePair();
    }
}

// FUNCTION: WIZ8 0x00428A90
void SetOverlayRenderMode(void)
{
    g_overlay_render_mode = 2;
}

// FUNCTION: WIZ8 0x00428AA0
void SetRendererModePair(void)
{
    g_paired_render_mode = 2;
    SetOverlayRenderMode();
}

/* Install a texture (often an stTextureAnim) on the mouse-cursor mesh. A null
   argument reuses the current cursor texture. */
// FUNCTION: WIZ8 0x00429170
void SetMouseCursorTexture(std::shared_ptr<srTextureIFace> texture)
{
    if (texture == 0) {
        texture = g_cursor_texture;
    }
    g_cursor_model->setTexture(std::move(texture), 0, 0);
    g_cursor_model->setDirty(srMeshModel::DIRTY_TRI_MESH);
}

// FUNCTION: WIZ8 0x004291C0
unsigned char GetRendererModeByte(void)
{
    return static_cast<unsigned char>(g_renderer_mode);
}

// FUNCTION: WIZ8 0x004297D0
bool HasScreenTransitionObjects(void)
{
    return !g_tooltip_nodes.empty();
}

// FUNCTION: WIZ8 0x004297e0
void SetSurfaceScale(float scale)
{
    g_surface_node->setScale(scale);
    g_surface_scale = scale;
}

// FUNCTION: WIZ8 0x004298E0
void SetFullscreenSceneLast(unsigned char value)
{
    g_fullscreen_scene_last = value;
}

// FUNCTION: WIZ8 0x004298F0
bool HasEnoughFreeDiskSpace(void)
{
    return W8HasEnoughSaveSpace();
}

// FUNCTION: WIZ8 0x004229b0
bool VideoIsFullScreen(void)
{
    return g_fullscreen;
}

// FUNCTION: WIZ8 0x00428b80
int VideoDumpMemoryLeaks(void)
{
    return 0;
}

/* The retail linker folds this trivial TRUE stub with other returns; its
   0x42B830 address sits past the proven Video2 hull, so its placement stays
   provisional. */
// FUNCTION: WIZ8 0x0042b830
bool CheckCdPresent(void)
{
    return true;
}

// FUNCTION: WIZ8 0x00427a70
void VideoGetClientRect(SDL_Rect* rect)
{
    *rect = {0, 0, 640, 480};
}

SDL_Surface& GetFrameBufferSurface()
{
    return *primarySurface;
}

/* The retail empty video-capture entry folds with the shared 0x4023a0 ret. */
void VideoCaptureToggle(void) {}

// GLOBAL: WIZ8 0x006596f4
bool g_auto_capture;
// GLOBAL: WIZ8 0x00659724
int g_screenshot_index;
// GLOBAL: WIZ8 0x00659728
int g_screenshot_page;

// FUNCTION: WIZ8 0x004229e0
void SaveJpegScreenshot(void)
{
    SDL_Surface* surface = g_gerd->lockBuffer();
    if (surface != 0) {
        char filename[32];

        if (g_auto_capture) {
            int screenshot_index = g_screenshot_index++;
            sprintf(filename, "Wiz8%5.5d.JPG", screenshot_index);
            PauseSharedGameTimers();
            srImage::save(filename, *surface, 35);
            ResumeSharedGameTimers();
        } else {
            int screenshot_index = g_screenshot_index++;
            sprintf(filename, "Wiz8%5.5d.JPG", screenshot_index);
            srImage::save(filename, *surface);
        }
        g_gerd->unlockBuffer();
    }
    g_screenshot_page = (g_screenshot_page - 1) & 1;
}

/* Tooltip placement state. The left/top pair records the last position the
   tooltip builder used; the scale participates in the texture mapping. */
// GLOBAL: WIZ8 0x00654ab8
int g_help_box_x;
// GLOBAL: WIZ8 0x00654abc
int g_help_box_y;

// GLOBAL: WIZ8 0x005ebe88
const double g_inverse_screen_height = 0.0020833333333333333;
// GLOBAL: WIZ8 0x005ebe90
const double g_inverse_screen_width = 0.0015625;
// GLOBAL: WIZ8 0x005ebf40
const double g_double_three_quarters = 0.75;

/* Pack normalized alpha/red/green/blue into the surface's high-to-low
   bytes, using the current x87 rounding mode. */
// FUNCTION: WIZ8 0x00429700
void PackColourBytes(unsigned char* colour, double alpha, double red, double green, double blue)
{
    colour[3] = static_cast<unsigned char>(srFloatToInt(alpha * g_color_byte_scale));
    colour[2] = static_cast<unsigned char>(srFloatToInt(red * g_color_byte_scale));
    colour[1] = static_cast<unsigned char>(srFloatToInt(green * g_color_byte_scale));
    colour[0] = static_cast<unsigned char>(srFloatToInt(blue * g_color_byte_scale));
}

/* Places one tooltip node at a screen position in normalized coordinates.
   With positional set, the position is snapped to the renderer's pixel grid;
   the node keeps the screen x/y in its right/bottom extent fields. */
// FUNCTION: WIZ8 0x004255F0
void PositionToolTipNode(srNode* node, int x, int y, bool positional)
{
    stModelInstance2D* instance = static_cast<stModelInstance2D*>(node);
    double position_x = x * g_inverse_screen_width;
    double position_y = y * g_inverse_screen_height;

    if (positional && g_gerd != 0) {
        double whole;
        w8_long width = g_gerd->getWidth();
        double fraction = modf(width * position_x, &whole);
        position_x -= fraction / width;
        w8_long height = g_gerd->getHeight();
        fraction = modf(height * position_y, &whole);
        position_y -= fraction / height;
    }

    int width = instance->GetScaledWidth();
    double half_width = width * g_inverse_screen_width * g_double_half;
    int height = instance->GetScaledHeight();
    double half_height = height * g_inverse_screen_height * g_double_half;

    glm::dvec3 location;
    location.x = half_width + position_x;
    location.z = -0.0001;
    if ((instance->overlay_scene_flag & 1U) == 0) {
        location.y = g_double_one - (half_height + position_y);
        g_paired_render_mode = 2;
    } else {
        location.y = g_double_three_quarters - (half_height + position_y) * g_double_three_quarters;
    }
    node->setLocation(location);
    SetOverlayRenderMode();
    instance->render_state.position_x = static_cast<short>(x);
    instance->render_state.position_y = static_cast<short>(y);
}

/* Builds a square power-of-two polygon brush from a surface rectangle. The
   larger source extent (including a one-pixel border) is rounded up to 16..256,
   then CopySurfaceWithBorder / MakePolygonBrush install it on the square
   overlay scene. */
// FUNCTION: WIZ8 0x00424560
srModelInstance* Video2DRectToSquarePolygon(const W8ControlsRect* rect, SDL_Surface& source,
                                            srNode* parent, unsigned char overlay)
{
    int extent = (rect->bottom - rect->top) + 2;
    int width_extent = (rect->right - rect->left) + 2;
    double width = rect->right * g_inverse_screen_width - rect->left * g_inverse_screen_width;
    if (extent < width_extent) {
        extent = width_extent;
    }
    if (extent <= 0x100) {
        w8_ulong size;
        if (extent > 0x80) {
            size = 0x100;
        } else if (extent > 0x40) {
            size = 0x80;
        } else if (extent > 0x20) {
            size = 0x40;
        } else {
            size = ((extent <= 0x10) - 1 & 0x10) + 0x10;
        }

        auto surface = std::shared_ptr<SDL_Surface>(
            CreateCpuStorage(size, size, SDL_PIXELFORMAT_ARGB1555).release(), SDL_DestroySurface);
        SDL_FillSurfaceRect(surface.get(), nullptr, 0);

        float scale_x;
        float scale_y;
        float mapping_x;
        float mapping_y;
        if (CopySurfaceWithBorder(surface.get(), rect, source, &scale_x, &scale_y, &mapping_x,
                                  &mapping_y)) {
            srModelInstance* node = MakePolygonBrush(parent, surface, width, width, scale_x,
                                                     scale_y, mapping_x, mapping_y, overlay);
            node->setName("Video2DRectToSquarePolygon");
            stModelInstance2D* instance = static_cast<stModelInstance2D*>(node);
            instance->render_state.display_state = static_cast<unsigned char>(g_active_page);
            instance->overlay_scene_flag |= 1;
            instance->render_state.width = static_cast<unsigned short>(size);
            instance->render_state.height = static_cast<unsigned short>(size);
            PositionToolTipNode(node, rect->left, rect->top, false);
            return node;
        }
    }
    return 0;
}

/* Copies a surface rectangle into the user or square overlay path, and
   records the resulting 2D instance extents. */
// FUNCTION: WIZ8 0x004253F0
stModelInstance2D* CreateSpriteFromSurface(SDL_Surface& surface, const W8ControlsRect* bounds,
                                           int square, int overlay)
{
    unsigned short width;
    unsigned short height;
    W8ControlsRect source_rect;
    srModelInstance* node;
    stModelInstance2D* instance;
    char mode = static_cast<char>(square);

    if (bounds == 0) {
        source_rect.left = 0;
        source_rect.top = 0;
        source_rect.right = surface.w;
        source_rect.bottom = surface.h;
        width = surface.w;
        height = surface.h;
    } else {
        width = static_cast<unsigned short>(static_cast<short>(bounds->right) -
                                            static_cast<short>(bounds->left));
        height = static_cast<unsigned short>(static_cast<short>(bounds->bottom) -
                                             static_cast<short>(bounds->top));
        source_rect.left = bounds->left;
        source_rect.top = bounds->top;
        source_rect.right = bounds->right;
        source_rect.bottom = bounds->bottom;
        if (width > surface.w) {
            return 0;
        }
        if (height > surface.h) {
            return 0;
        }
    }
    if (width > 0x100 || height > 0x100) {
        return 0;
    }
    if (surface.pixels == 0) {
        return 0;
    }
    if (mode != 0) {
        node = Video2DRectToSquarePolygon(&source_rect, surface, g_overlay_scenes.square.get(),
                                          overlay);
    } else {
        node = Video2DRectToPolygon(&source_rect, surface, g_overlay_scenes.user.get(), overlay);
        g_paired_render_mode = 2;
    }
    SetOverlayRenderMode();
    instance = static_cast<stModelInstance2D*>(node);
    if (instance != 0) {
        if (mode != 0) {
            unsigned short extent = width;
            if (width <= height) {
                extent = height;
            }
            instance->render_state.height = extent;
            if (width <= height) {
                width = height;
            }
        } else {
            instance->render_state.height = height;
        }
        instance->render_state.width = width;
    }
    /* Retail writes display_state even when the node factory returned null. */
    SetModelInstance2DDisplayState(instance, 3);
    return instance;
}

/* Builds a solid-color quad sprite. Width/height are pixel counts; color becomes
   the material emissive. Radar blip templates are the observed callers. */
// FUNCTION: WIZ8 0x00424790
stModelInstance2D* CreateColoredPolygonSprite(int width, int height, const glm::vec4* color,
                                              bool fullscreen)
{
    double scale_x = width * g_inverse_screen_width;
    double scale_y = height * g_inverse_screen_height;
    auto model = std::make_shared<srMeshModel>(0L, 0L);

    SetSpriteQuad(*model, static_cast<float>(scale_x), static_cast<float>(scale_y));

    auto material = std::make_shared<srMaterial>();
    material->setEmissive(*color);
    glm::vec4 zero;
    zero = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    material->setDiffuse(zero);
    material->setSpecular(zero);
    material->parms.shininess = 1.0f;
    material->parms.diffuse.w = 1.0f;
    material->dirty = 1;
    model->setMaterial(std::move(material), 0, srMeshModel::SIDE_FRONT);

    stModelInstance2D* instance = new stModelInstance2D(g_overlay_scenes.user.get());
    instance->setName("Video2DPolyColored");
    instance->SetModel(model);
    instance->setRotation(0.0, 0.0, 0.0);

    srShader shader;
    shader.value = 0x2417;
    model->setShader(shader, 0);

    instance->render_state.width = static_cast<unsigned short>(width);
    instance->render_state.height = static_cast<unsigned short>(height);
    SetModelInstance2DDisplayState(instance, 3);
    if (fullscreen) {
        instance->setParent(g_overlay_scenes.fullscreen.get(), 1);
    }
    return instance;
}

// FUNCTION: WIZ8 0x004257D0
void Position2DNodeUnsnapped(srNode* node, int x, int y)
{
    PositionToolTipNode(node, x, y, false);
}

// FUNCTION: WIZ8 0x004264F0
void SetModelInstance2DDisplayState(stModelInstance2D* object, unsigned char state)
{
    object->render_state.display_state = state;
}

/* Positions every live tooltip object left to right starting at x, advancing
   the cursor by each node's scaled width. With no live objects, just record
   the requested position. */
// FUNCTION: WIZ8 0x00429210
void VideoPositionToolTip(std::int32_t x, std::int32_t y)
{
    if (!g_tooltip_nodes.empty()) {
        std::int32_t offset = x;
        for (auto* node : g_tooltip_nodes) {
            PositionToolTipNode(node, offset, y, true);
            offset += node->GetScaledWidth();
        }
        g_help_box_y = y;
        g_help_box_x = x;
        return;
    }
    g_help_box_x = x;
    g_help_box_y = y;
}

/* Copies the tooltip source rectangle into a size-rounded 16-bit surface,
   repeating its border one pixel outward, and reports the texture mapping
   scales for the resulting polygon brush. */
// FUNCTION: WIZ8 0x00428B90
unsigned char CopySurfaceWithBorder(SDL_Surface* surface, const W8ControlsRect* rect,
                                    SDL_Surface& source, float* scale_x, float* scale_y,
                                    float* mapping_x, float* mapping_y)
{
    if (surface == 0 || rect == 0 || source.pixels == 0 || scale_x == 0 || scale_y == 0 ||
        mapping_x == 0 || mapping_y == 0) {
        return 0;
    }
    int width = (rect->right > 0x27f ? 0x280 : rect->right) - rect->left;
    int height = (rect->bottom > 0x1df ? 0x1e0 : rect->bottom) - rect->top;
    int right = width + 1;
    int bottom = height + 1;

    const int source_width = rect->left + width;
    const int source_height = rect->top + height;
    if (rect->left < 0 || rect->top < 0 || width <= 0 || height <= 0 || source_width > source.w ||
        source_height > source.h) {
        return 0;
    }
    SDLSurfaceOwner source_view{SDL_CreateSurfaceFrom(source_width, source_height, source.format,
                                                      source.pixels, source.pitch),
                                SDL_DestroySurface};
    SDLSurfaceOwner destination_view{SDL_CreateSurfaceFrom(surface->w, surface->h, source.format,
                                                           surface->pixels, surface->pitch),
                                     SDL_DestroySurface};
    if (!source_view || !destination_view ||
        !SDL_SetSurfaceBlendMode(source_view.get(), SDL_BLENDMODE_NONE) ||
        !SDL_SetSurfaceColorKey(source_view.get(), false, 0) ||
        !SDL_SetSurfaceBlendMode(destination_view.get(), SDL_BLENDMODE_NONE)) {
        return 0;
    }
    const auto copy = [&](int destination_x, int destination_y, int source_x, int source_y,
                          int copy_width, int copy_height) {
        SDL_Rect source_rect{source_x, source_y, copy_width, copy_height};
        SDL_Rect destination_rect{destination_x, destination_y, copy_width, copy_height};
        return SDL_BlitSurface(source_view.get(), &source_rect, destination_view.get(),
                               &destination_rect);
    };
    if (!copy(1, 1, rect->left, rect->top, width, height) ||
        !copy(1, 0, rect->left, rect->top, width, 1) ||
        !copy(1, bottom, rect->left, rect->top - 1 + height, width, 1) ||
        !copy(0, 1, rect->left, rect->top, 1, height) ||
        !copy(right, 1, rect->left - 1 + width, rect->top, 1, height) ||
        !copy(0, 0, rect->left, rect->top, 1, 1) ||
        !copy(right, 0, rect->left - 1 + width, rect->top, 1, 1) ||
        !copy(0, bottom, rect->left, rect->top - 1 + height, 1, 1) ||
        !copy(right, bottom, rect->left - 1 + width, rect->top - 1 + height, 1, 1)) {
        return 0;
    }

    float scale = 1.0f / surface->w;
    *scale_x = scale;
    *scale_x = scale * g_surface_scale + scale;
    scale = 1.0f / surface->h;
    *scale_y = scale;
    *scale_y = scale * g_surface_scale + scale;
    *mapping_x = (rect->right - rect->left) / static_cast<float>(surface->w);
    *mapping_y = (rect->bottom - rect->top) / static_cast<float>(surface->h);
    return 1;
}

/* Builds a polygon brush from a tooltip surface rectangle. The larger source
   extent is rounded up to the next power of two between 16 and 256, the copy
   repeats its border, and the node records the rectangle extents. */
// FUNCTION: WIZ8 0x00424280
srModelInstance* Video2DRectToPolygon(const W8ControlsRect* rect, SDL_Surface& source,
                                      srNode* parent, unsigned char overlay)
{
    double left = rect->left * g_inverse_screen_width;
    int extent = rect->right - rect->left;
    double top = rect->top * g_inverse_screen_height;
    int rect_height = rect->bottom - rect->top;
    double width = rect->right * g_inverse_screen_width - left;
    double height = rect->bottom * g_inverse_screen_height - top;

    if (extent <= rect_height) {
        extent = rect_height;
    }
    if (extent < 0x10) {
        extent = 0x10;
    } else if (extent < 0x20) {
        if (extent != 0x10) {
            extent = 0x20;
        }
    } else if (extent < 0x40) {
        if (extent != 0x20) {
            extent = 0x40;
        }
    } else if (extent < 0x80) {
        if (extent != 0x40) {
            extent = 0x80;
        }
    } else {
        if (0x100 < extent) {
            return 0;
        }
        if (extent != 0x80) {
            extent = 0x100;
        }
    }

    auto surface = std::shared_ptr<SDL_Surface>(CreateCpuStorage(static_cast<w8_ulong>(extent),
                                                                 static_cast<w8_ulong>(extent),
                                                                 SDL_PIXELFORMAT_ARGB1555)
                                                    .release(),
                                                SDL_DestroySurface);
    float scale_x;
    float scale_y;
    float mapping_x;
    float mapping_y;
    if (!CopySurfaceWithBorder(surface.get(), rect, source, &scale_x, &scale_y, &mapping_x,
                               &mapping_y)) {
        return 0;
    }
    srModelInstance* node = MakePolygonBrush(parent, surface, width, height, scale_x, scale_y,
                                             mapping_x, mapping_y, overlay);
    if (node != 0) {
        stModelInstance2D* instance = static_cast<stModelInstance2D*>(node);
        instance->render_state.display_state = static_cast<unsigned char>(g_active_page);
        instance->render_state.width = static_cast<unsigned short>(rect->right - rect->left);
        instance->render_state.height = static_cast<unsigned short>(rect->bottom - rect->top);
        instance->render_state.position_x = static_cast<short>(rect->left);
        instance->render_state.position_y = static_cast<short>(rect->top);
        glm::dvec3 location;
        location = glm::dvec3(width * g_double_half + left,
                              g_double_one - (height * g_double_half + top), -0.0001);
        node->setLocation(location);
        instance->setName("Video2DRectToPolygon");
    }
    return node;
}

/* Builds the help box: renders the text into an ARGB1555 surface, draws the
   border, converts the surface to a polygon brush, appends it to the live
   tooltip objects and positions them above the cursor. Only one tooltip is
   alive at a time. */
// FUNCTION: WIZ8 0x00429290
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
    W8TextBuffer* buffer = new W8TextBuffer(&bounds, text, g_font10arial,
                                            g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4);
    if (buffer == 0) {
        return;
    }
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
    w8_ulong border_colour;
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
    srModelInstance* node =
        Video2DRectToPolygon(&rect, *text_surface, g_overlay_scenes.cursor.get(), 1);
    if (node != 0) {
        g_tooltip_nodes.push_back(static_cast<stModelInstance2D*>(node));
    }

    int position_y = g_cursor_height - g_help_box_height;
    int position_x = g_cursor_width;
    int offset = position_x;
    for (auto* node : g_tooltip_nodes) {
        PositionToolTipNode(node, offset, position_y, true);
        offset += node->GetScaledWidth();
    }
    g_help_box_x = position_x;
    g_help_box_y = position_y;
    delete buffer;
}

/* Renderer configuration helpers reached from the video device, not the
   persisted configuration block. */
// GLOBAL: WIZ8 0x659714
int g_resident_texture_policy;

// FUNCTION: WIZ8 0x004266e0
void SetResidentTexturePolicy(int policy)
{
    if (policy != g_resident_texture_policy) {
        g_gerd->invalidateResidentTextures();
        g_gerd->invalidateTextureCache();
        g_resident_texture_policy = policy;
    }
}

// GLOBAL: WIZ8 0x659718
bool g_swap_interval_enabled;

// FUNCTION: WIZ8 0x00426710
void SetSwapInterval(bool enabled)
{
    g_swap_interval_enabled = enabled;
    g_gerd->setSwapInterval(enabled ? 1 : 0);
}

// FUNCTION: WIZ8 0x00426740
void SetTextureCacheSize(w8_ulong bytes)
{
    if (bytes > 0x7fffff && bytes != g_gerd->getTextureCacheSize()) {
        g_gerd->invalidateResidentTextures();
        g_gerd->invalidateTextureCache();
        g_gerd->setTextureCacheSize(bytes);
    }
}

/* Build an stTextureAnim whose frames are consecutive VObject subimages starting
   at start_frame. use_argb1555 selects SURFACE_ARGB1555 versus SURFACE_RGB555 for
   each frame surface. The setName string is the retail identity. */
// FUNCTION: WIZ8 0x00428E90
std::shared_ptr<stTextureAnim> VideoVObjectToTextureAnim(const SpriteSheet& sprites,
                                                         unsigned short start_frame,
                                                         unsigned short frame_count,
                                                         bool use_argb1555)
{
    unsigned short frame;
    unsigned short end_frame;
    w8_ulong extent;
    unsigned short largest;

    auto animation = std::make_shared<stTextureAnim>();
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
                extent = static_cast<w8_ulong>(-1);
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
                auto texture = std::make_shared<srTextureMap>();
                texture->setName("VideoVObjecttoTextureAnim");
                texture->setMipmapBias(-8.0f);
                texture->setSurfacePtr(surface);
                texture->setCorrection(srTextureIFace::CORRECTION_FASTEST);
                texture->setWrapS(srTextureIFace::WRAP_CLAMP);
                texture->setWrapT(srTextureIFace::WRAP_CLAMP);
                texture->setMagFilter(srTextureIFace::FILTER_NONE);
                texture->setMinFilter(srTextureIFace::FILTER_NONE);
                texture->enableHint(srTextureIFace::HINT_NO_MIPMAPS);
                texture->setMipmap(srTextureIFace::MIPMAP_NONE);
                texture->enableHint(use_argb1555 ? srTextureIFace::HINT_ONE_BIT_ALPHA
                                                 : srTextureIFace::HINT_NO_ALPHA);
                animation->AddTexture(std::move(texture));
            }
        }
        ++frame;
    }
    return animation;
}

// FUNCTION: WIZ8 0x004291d0
void SetDisplayGamma(float value)
{
    g_gerd->setGamma(glm::vec3(value));
}

/* The option-4-suppressed variant of the render probe: with the positional
   option forced off, draw the node between the camera's bracketing
   passes, restore the option and return the sampled statistic. */
// FUNCTION: WIZ8 0x00428830
unsigned int MeasureNodeRenderWithoutPositionalOption(srNode* node)
{
    if (g_gerd != 0 && g_gerd->isEnabled(srGERD::ENABLE_AUTO_FLIP)) {
        g_gerd->toggle(srGERD::ENABLE_AUTO_FLIP);
    }
    unsigned int pixels_drawn = MeasureNodeRender(node);
    if (g_gerd != 0 && !g_gerd->isEnabled(srGERD::ENABLE_AUTO_FLIP)) {
        g_gerd->toggle(srGERD::ENABLE_AUTO_FLIP);
    }
    return pixels_drawn;
}

/* Open the render-probe pass: force renderer option 4 off, reset the frame
   to a solid blue clear and prime the scissor before the measured draw. */
// FUNCTION: WIZ8 0x00428910
void BeginRenderProbe(void)
{
    if (g_gerd->isEnabled(srGERD::ENABLE_AUTO_FLIP)) {
        g_gerd->toggle(srGERD::ENABLE_AUTO_FLIP);
    }
    g_gerd->setClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    g_gerd->setAmbientLight(1.0f, 1.0f, 1.0f, 1.0f);
    g_gerd->beginFrame();
    g_gerd->setScissor(0, 0, g_gerd->getWidth(), g_gerd->getHeight());
    g_gerd->clear(srDD::BUFFER_COLOR | srDD::BUFFER_DEPTH);
    g_gerd->endFrame();
}

/* Draw the node between the camera's bracketing passes and return the
   renderer statistic the probe samples. */
// FUNCTION: WIZ8 0x004289e0
unsigned int MeasureNodeRender(srNode* node)
{
    srNode::ProcessInfo process;

    g_gerd->flushRenderers();
    g_gerd->resetStatistics();
    g_gerd->beginFrame();
    srNode::lockSceneGraph();
    process.renderer = g_gerd.get();
    g_world->camera->process(process, srNode::PROCESS_PUSH);
    node->process(process, srNode::PROCESS_RENDER);
    g_world->camera->process(process, srNode::PROCESS_POP);
    srNode::unlockSceneGraph();
    g_gerd->endFrame();
    g_gerd->flushRenderers();
    const auto statistics = g_gerd->getStatistics();
    return static_cast<unsigned int>(statistics.pixels_drawn);
}

/* Close the render-probe pass: restore renderer option 4 and present. */
// FUNCTION: WIZ8 0x004289c0
void EndRenderProbe(void)
{
    if (!g_gerd->isEnabled(srGERD::ENABLE_AUTO_FLIP)) {
        g_gerd->toggle(srGERD::ENABLE_AUTO_FLIP);
    }
    g_gerd->flipFrame();
}

// VTABLE: WIZ8 0x005EBE98
// class srClientSupport<srMeshModel,8208>

/* 0x00424A50 calls the imported ~srMeshModel: it is the local
   ??_GsrMeshModel thunk, not the support-class deleting destructor. */

/* CVDUMP includes the class tag on the repeated self-type argument in each
   vftable symbol below. These remain ordinary self-support instantiations. */
// VTABLE: WIZ8 0x005EBEEC
// class srClientSupport<srTextureMap,8465>

/* 0x00424B70 calls the imported ~srTextureMap: it is the local
   ??_GsrTextureMap thunk, not the support-class deleting destructor. */

// VTABLE: WIZ8 0x005EBDE0
// class srClientSupport<srMaterial,8720>

/* 0x00423E50 calls the imported ~srMaterial: it is the local ??_GsrMaterial
   thunk, not the support-class deleting destructor. */

/* CVDUMP includes the class tag on the repeated self-type argument in the
   vftable symbol.  It is still the ordinary srCamera self-support template. */
// VTABLE: WIZ8 0x005EBE14
// class srClientSupport<srCamera,5120>

/* 0x00423E80 calls the imported ~srCamera: it is the local ??_GsrCamera
   thunk, not the support-class deleting destructor. */

/* CVDUMP includes the class tag on the repeated self-type argument in the
   vftable symbol.  It is still the ordinary srScene self-support template. */
// VTABLE: WIZ8 0x005EBE48
// class srClientSupport<srScene,4112>

/* 0x00423EB0 calls the imported ~srScene: it is the local ??_GsrScene
   thunk, not the support-class deleting destructor. */

/* 0x004229C0 is a bare JMP to RenderFrame: a tail-jump thunk with no
   distinct source entity. */

// FUNCTION: WIZ8 0x00424BA0
srNode* MakePosterQuad(std::shared_ptr<srTextureIFace> texture, float width, float height,
                       bool additive)
{
    srShader shader;
    srTextureIFace::Dimensions dimensions;
    dimensions.width = 64;
    dimensions.height = 64;

    auto model = std::make_shared<stMeshModel>(0, 0);
    float extent_w = g_float_one / dimensions.width * g_surface_scale;
    float extent_h = g_float_one / dimensions.height * g_surface_scale;
    model->setName("VideoMakePoster");
    texture->getDimensions();
    SetSpriteQuad(*model, width, height, {g_float_one - extent_w, g_float_one - extent_h},
                  {extent_w, extent_h});

    shader.value = 0x100a013;
    if (additive) {
        shader.value = 0x100c0b3;
        model->enable(srMeshModel::CONTROL_SORTED_RENDERING);
    }
    model->setMaterial(g_blit_material, 0, srMeshModel::SIDE_FRONT);
    model->setTexture(std::move(texture), 0, 0);
    model->setShader(shader, 0);

    stModelInstance* instance = new stModelInstance(static_cast<srNode*>(0));
    instance->setName("VideoMakePoster");
    if (instance != 0) {
        instance->setModel(model);
    }
    instance->render_flags |= stModelInstance::RENDER_NO_PICK;
    return instance;
}

// FUNCTION: WIZ8 0x00425590
void DrawColorSurface(SDL_Surface* surface, int x, int y)
{
    SDL_Rect destination{x, y, surface->w, surface->h};
    SDL_BlitSurface(surface, nullptr, g_primary_color_surface.get(), &destination);
}

// FUNCTION: WIZ8 0x00426490
void DrawBufferLine(w8_long x0, w8_long y0, w8_long x1, w8_long y1, w8_ulong* pixel)
{
    if (x0 != 0 && y0 != 0 && x1 != 0 && y1 != 0) {
        SDL_Surface* surface = g_gerd->lockBuffer();
        if (surface != 0) {
            SDL_Rect clip;
            SDL_GetSurfaceClipRect(surface, &clip);
            int x = x0, y = y0;
            const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
            const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
            int error = dx + dy;
            while (true) {
                if (x >= clip.x && y >= clip.y && x < clip.x + clip.w && y < clip.y + clip.h)
                    SDL_WriteSurfacePixel(surface, x, y, (*pixel >> 16) & 255, (*pixel >> 8) & 255,
                                          *pixel & 255, *pixel >> 24);
                if (x == x1 && y == y1) break;
                const int twice = 2 * error;
                if (twice >= dy) {
                    error += dy;
                    x += sx;
                }
                if (twice <= dx) {
                    error += dx;
                    y += sy;
                }
            }
            g_gerd->unlockBuffer(true);
        }
    }
}

// FUNCTION: WIZ8 0x004273F0
ViewportBounds GetScaledViewportBounds()
{
    return {{g_viewport.left * g_viewport_x_scale, g_viewport.top * g_viewport_y_scale},
            {g_viewport.right * g_viewport_x_scale, g_viewport.bottom * g_viewport_y_scale}};
}

// FUNCTION: WIZ8 0x004277F0
void SetPickKey(void* key)
{
    if (g_gerd) {
        g_gerd->setPickKey(
            reinterpret_cast<w8_ulong_ptr>(key)); // reinterpret-ok: opaque pick token
    }
}

/* srVertexProcessor::MaterialInfo's inline ctor emitted out-of-line inside
   srMaterial's locally-compiled constructor; the primary is in
   srVertexProcessor.h. */
