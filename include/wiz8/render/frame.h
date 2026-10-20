#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/design_rect.h"
#include "wiz8/render/draw_inputs.h"
/* Modified for the Wizardry 8 reconstruction, 2026-09-10.
   Native frame, window and UI-canvas integration.
   Remove the artificial SGP C boundary and unused local configuration include, 2026-10-04.
   Distributed under the accompanying SFI Source Code license agreement. */

#define BUFFER_READY 0x00
#define BUFFER_BUSY 0x01
#define BUFFER_DIRTY 0x02
#define BUFFER_DISABLED 0x03

#define MAX_CURSOR_WIDTH 64
#define MAX_CURSOR_HEIGHT 64
#define VIDEO_NO_CURSOR 0xFFFF

extern SDL_Window* ghWindow;

extern bool InitializeVideoManager(void);
extern void ShutdownVideoManager(void);
extern void ShutdownVideoScenes(void);
bool IsVideoActive();
extern void SuspendVideoManager(void);
extern bool RestoreVideoManager(void);
/* Mark a game-space rectangle of the UI canvas for upload this frame. */
void InvalidateRegion(int left, int top, int right, int bottom);
inline void InvalidateRegion(SDL_Rect bounds)
{
    InvalidateRegion(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
}
/* The pending UI upload rectangle, cleared by the call. */
SDL_Rect TakeUiUpload();

enum { INVAL_SRC_TRANS = 1, PIXEL_DEPTH = 16, SCREEN_WIDTH = 640, SCREEN_HEIGHT = 480 };
void VideoFullScreen(bool enabled);
bool VideoResizeWindow(void);
void VideoInspectorEnable(void);
SDL_Surface& GetFrameBufferSurface();
void VideoToolTip(std::string_view text);
extern std::int32_t g_help_box_width;
extern std::int32_t g_help_box_height;
void VideoPositionToolTip(std::int32_t x, std::int32_t y);
void VideoRemoveToolTip(void);

SDL_Point CursorPosition();

/* Tooltip ownership query: nonzero while a VideoToolTip object is alive. */
bool HasScreenTransitionObjects(void); /* 0x004297D0 */
/* 0x00422EC0: invalidate each rectangle in a run. */

void InvalidateScreenRects(DesignRect* rects, unsigned int count);

/* Renderer state and helpers shared by the product C++ consumers. */
extern int g_cursor_image_height;
/* 0x00652DA4: set while the swaying camera view is active; see
   PartyCamera::applySway. */
extern bool g_camera_sway_active;
extern int g_screenshot_index;
extern int g_screenshot_page;
void ClearSurfaceRect(int left, unsigned int top, int right, unsigned int bottom);
/* Clear a rectangle of the interface overlay and upload it. */
void EraseRegion(int left, int top, int right, int bottom);

extern const float g_viewport_x_scale;
extern const float g_viewport_y_scale;

struct SDL_Surface;
class MeshInstance;
class MeshInstance;
class Texture;
struct OverlayQuad;
class UiCanvasTexture;

std::unique_ptr<MeshInstance> MakePosterQuad(std::shared_ptr<Texture> texture, float width,
                                             float height, bool additive);
std::unique_ptr<OverlayQuad> makeOverlayFromTexture(std::shared_ptr<Texture> texture, double width,
                                                    double height, bool alpha_blend, bool square);
/* 0x00426F80: render the world into a caller-owned color surface through a
   scissored viewport, then blit the locked frame buffer onto the target. */
bool RenderWorldToSurface(SDL_Surface* target, const DesignRect* rect, bool render_secondary);

struct W8ControlsRect;

/* 0x00424790: build a solid-color quad sprite; width/height are pixel counts
   and `color` becomes the material's emissive vector. MGSRadarMap's blip
   templates are its observed callers. */
void SetFullscreenSceneLast(unsigned char value); /* 0x004298E0 */
std::unique_ptr<OverlayQuad> makeColorOverlay(int width, int height, const glm::vec4& color,
                                              bool fullscreen);
std::unique_ptr<OverlayQuad> makeOverlayFromSurface(SDL_Surface& surface,
                                                    const W8ControlsRect* bounds, bool square,
                                                    bool alpha_blend);
void positionOverlay(OverlayQuad* quad, int x, int y, bool snap = false);
/* 0x00427E70: surface-lock helper used while installing a drag cursor. */
bool ClearMouseSurface(void);
/* 0x004255F0: place a 2D node at a screen position in normalized
   coordinates; positional snaps to the renderer's pixel grid. */
/* 0x00427460: the debug stats readout - frame rate always, the full counter
   block in inspector mode 2, or the scaled camera position in mode 3. */
void DrawVideoInspector(int left, unsigned int top);
/* 0x00428830: the option-4-suppressed render probe - draws the node between
   the dynamic scene's bracketing passes and returns the sampled statistic. */
unsigned int MeasureMeshRenderWithoutPositionalOption(MeshInstance* mesh);
/* 0x00424EB0: build the 2D polygon-brush model instance over a surface;
   RenderAutomapMarkers' item-marker factory calls it cross-TU. */
/* 0x00484A40: blit one frame of a video object into a color surface. */
bool BlitVideoObjectToColorSurface(const SpriteSheet& sprites, std::uint16_t region,
                                   SDL_Surface& destination, int x, int y);
class Texture;

/* 0x00428E90: build an Texture whose frames are VObject subimages. */
std::shared_ptr<Texture> VideoVObjectToTextureAnim(const SpriteSheet& sprites,
                                                   unsigned short start_frame,
                                                   unsigned short frame_count, bool use_argb1555);
/* 0x00428AA0: mark both renderer mode words dirty. */
/* 0x004289C0: the render probe that counts a mesh's drawn pixels — draw the
   node through the world camera and return the sampled statistic. */
unsigned int MeasureMeshRender(MeshInstance* mesh);

void WarpSystemCursor(int x, int y); /* 0x004280C0: fullscreen-safe */

class Camera3D;
class GpuRenderer;
class Material;
class WorldScene;
class UiCanvasTexture;

extern unsigned char g_fullscreen;
extern bool g_screenshot_pending;
extern bool g_video_inspector_enabled;
extern bool g_cursor_scene_enabled;
extern unsigned char g_fullscreen_scene_last;
extern std::shared_ptr<SDL_Surface> g_primary_color_surface;
void DrawColorSurface(SDL_Surface* surface, int x, int y);
std::unique_ptr<MeshInstance> VideoMakePoster(std::shared_ptr<SDL_Surface> surface, float width,
                                              float height, bool additive); /* 0x00424A90 */
extern bool g_texture_cache_enabled;
extern std::unique_ptr<GpuRenderer> g_gerd;
/* Secondary renderer device preferred by the offscreen world-render path. */
extern std::unique_ptr<UiCanvasTexture> g_ui_canvas;
extern std::shared_ptr<SDL_Surface> g_mouse_surface;
extern MeshInstance* g_current_model_instance;
extern float g_texture_inset_pixels;
extern int g_surface_opaque_shader_bits;
extern int g_surface_alpha_blend_shader_bits;
extern DesignRect g_viewport;
extern int g_resident_texture_policy;
extern unsigned char g_world_render_enabled;
extern bool g_world_blacked_out;

void SetResidentTexturePolicy(int policy);
void SetTextureInset(float scale);
void SetSwapInterval(bool enabled);
void SetViewport(const DesignRect& bounds);
// Update the surface viewport without changing the camera view plane.
void SetScaledViewport(const DesignRect& bounds);
/* Read the stored pixel viewport back out in normalized 0..1 scale. */
struct ViewportBounds {
    glm::vec2 minimum;
    glm::vec2 maximum;
};
ViewportBounds GetScaledViewportBounds();
bool InitializeRendererSceneObjects(void);
void ResetVideoFrameState(void);
void SetPrimarySurfaceTextureHint2Enabled(bool enabled);
bool ClearPrimarySurface(void);
void ResetTransientRenderScenes(void);
void ClearVideoDirtyBlocks(void); /* 0x00423150 */
// Advances the render clock by the time since the previous rendered frame.
void UpdateRenderElapsedTime(void);
// Seconds on the render clock that scene passes animate textures with.
double RenderClockSeconds();
// Draws one scene pass; `picks` are tested against its pickable triangles.
void RenderScene(WorldScene* scene, Camera3D* camera, const DesignRect* viewport, bool preserve_fog,
                 std::span<Pick> picks = {});
void RenderFrame(void);
/* The native presentation shell also owns this opaque surface entry point. */
SDL_Surface* BeginVideoPresentation(void);
bool FinishVideoPresentation(void);
void PublishLightDirection(const glm::vec3* direction);
bool CreateWizardryWindow(void);
bool InitializePrimarySurface(void);
void ReleasePrimarySurface(void);
unsigned char InitializeVideoDevice(void);
bool SetupDisablesAudio();
bool OpenRendererWindow(void);
void InvalidateRendererTextureCache(void);
bool DisableCursorScene(void);
bool EnableCursorScene(void);
void SetWorldModelPickingEnabled(bool enabled);
bool RendererBufferIsLockable(void);
void SetRendererAutoFlipEnabled(bool enabled);
bool IsCursorInsideViewport(void);      /* 0x00428070 */
bool IsCursorImageInsideViewport(void); /* 0x00428030 */

void DrawTextAndInvalidate(SDL_Surface& target, Font& font, SDL_Point at, std::string_view text,
                           TextOptions options = {});
