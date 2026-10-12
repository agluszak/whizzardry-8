#ifndef WIZ8_VIDEO2_H
#define WIZ8_VIDEO2_H

#include <SDL3/SDL_rect.h>
#include <cstdint>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <string_view>
#include <memory>

#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"
#include "himage.h"
/* Modified for the Wizardry 8 reconstruction, 2026-09-10.
   Reconstructed Wizardry Video2 interface.
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
extern void SuspendVideoManager(void);
extern bool RestoreVideoManager(void);
extern void GetCurrentVideoSettings(std::uint16_t* usWidth, std::uint16_t* usHeight, std::uint8_t* ubBitDepth);
extern void InvalidateRegion(std::int32_t iLeft, std::int32_t iTop, std::int32_t iRight, std::int32_t iBottom, std::uint32_t uiFlags);
extern void PrintScreen(void);

void VideoCaptureToggle(void);

enum { INVAL_SRC_TRANS = 1, PIXEL_DEPTH = 16, SCREEN_WIDTH = 640, SCREEN_HEIGHT = 480 };

bool VideoIsFullScreen(void);
void VideoFullScreen(bool enabled);
bool VideoResizeWindow(void);
void VideoInspectorEnable(void);
bool VideoInspectorIsEnabled(void);
char* VideoGetConfigFile(void);
void VideoSetConfigFile(const char* path);
std::int32_t VideoDumpMemoryLeaks(void);
bool CheckCdPresent(void);
void VideoGetClientRect(SDL_Rect* rect);
SDL_Surface& GetFrameBufferSurface();
void VideoToolTip(std::string_view text);
extern std::int32_t g_help_box_width;
extern std::int32_t g_help_box_height;
/* Region code accesses these fields inline. */
static __inline std::int32_t VideoGetToolTipWidth(void)
{
    return g_help_box_width;
}
static __inline std::int32_t VideoGetToolTipHeight(void)
{
    return g_help_box_height;
}
void VideoPositionToolTip(std::int32_t x, std::int32_t y);
void VideoRemoveToolTip(void);

void SGPMouseGetPos(SDL_Point* point);

/* Tooltip ownership query: nonzero while a VideoToolTip object is alive. */
bool HasScreenTransitionObjects(void); /* 0x004297D0 */
/* 0x00422EC0: invalidate each rectangle in a run. */
struct W8ScreenRect;

void InvalidateScreenRects(W8ScreenRect* rects, std::uint32_t count, std::int32_t flags);

/* Renderer state and helpers shared by the product C++ consumers. */
extern bool g_auto_capture;
extern std::int32_t g_cursor_image_height;
/* 0x00652DA4: set while the swaying camera view is active; see
   SetCameraSwayMode in 3dapi.cpp. */
extern bool g_camera_sway_active;
extern std::int32_t g_screenshot_index;
extern std::int32_t g_screenshot_page;
void ClearSurfaceRect(std::int32_t left, std::uint32_t top, std::int32_t right, std::uint32_t bottom);

extern const float g_viewport_x_scale;
extern const float g_viewport_y_scale;

class srColorSurface;
class srColorSurfaceIFace;
class srModelInstance;
class srNode;
class srTextureIFace;
class stModelInstance2D;
template <class T> class srVector3T;

srNode* MakePosterQuad(std::shared_ptr<srTextureIFace> texture, float width, float height,
                       bool additive);
/* 0x00425190: build a 2D marker model instance over a texture. */
stModelInstance2D* CreateSpriteFromTexture(std::shared_ptr<srTextureIFace> texture, double width, double height,
                                           bool keep_aspect, bool overlay);
/* 0x00426F80: render the world into a caller-owned color surface through a
   scissored viewport, then blit the locked frame buffer onto the target. */
bool RenderWorldToSurface(srColorSurface* target, W8ScreenRect* rect,
                                   bool render_secondary);
void SetPickKey(void* key);

struct W8ControlsRect;

/* 0x00424790: build a solid-color quad sprite; width/height are pixel counts
   and `color` becomes the material's emissive vector. MGSRadarMap's blip
   templates are its observed callers. */
void SetFullscreenSceneLast(bool value); /* 0x004298E0 */
stModelInstance2D* CreateColoredPolygonSprite(std::int32_t width, std::int32_t height, const glm::vec4* color,
                                              bool fullscreen);
/* 0x004253F0: the render-target sprite factory CreateSpriteFromSurface wraps; the
   radar overlay is created through it directly. */
stModelInstance2D* CreateSpriteFromVideoSurface(SDL_Surface& surface, const W8ControlsRect* bounds,
                                                std::int32_t square, std::int32_t, char overlay);
/* 0x004255C0: wrap the sprite-surface factory - image is a video surface
   handle (or a negative target id), rect an optional source rectangle. */
stModelInstance2D* CreateSpriteFromSurface(SDL_Surface& image, const W8ControlsRect* rect, std::int32_t mode,
                                           std::int32_t unused, std::int32_t overlay);
/* 0x004257D0: position a 2D node without pixel snapping. */
void Position2DNodeUnsnapped(srNode* node, std::int32_t x, std::int32_t y);
/* 0x004264F0: write the display-state byte of a 2D model instance. */
void SetModelInstance2DDisplayState(stModelInstance2D* object, std::uint8_t state);
/* 0x00425820: set FLAG_DISABLE on a node; the overlay drawers call it to hide
   the highlight sprite before redrawing the panel contents. */
void ClearNodeFlag(srNode* node);
/* 0x00425840: rotate an srNode in degrees and invalidate the renderer mode. */
void RotateNodeInDegrees(srNode* node, std::int32_t degrees);
/* 0x00425950: drop a 2D node from the surface-tile table, invalidate its
   model's texture, and release the node. */
void ReleaseSurfaceNode(srNode* node);
/* 0x00427E70: surface-lock helper used while installing a drag cursor. */
bool ClearMouseSurface(void);
/* 0x004255F0: place a 2D node at a screen position in normalized
   coordinates; positional snaps to the renderer's pixel grid. */
void PositionToolTipNode(srNode* node, std::int32_t x, std::int32_t y, bool positional);
/* 0x00427460: the debug stats readout - frame rate always, the full counter
   block in inspector mode 2, or the scaled camera position in mode 3. */
void DrawVideoInspector(std::int32_t left, std::uint32_t top);
/* 0x00428830: the option-4-suppressed render probe - draws the node between
   the dynamic scene's bracketing passes and returns the sampled statistic. */
std::uint32_t MeasureNodeRenderWithoutPositionalOption(srNode* node);
/* 0x00424EB0: build the 2D polygon-brush model instance over a surface;
   RenderAutomapMarkers' item-marker factory calls it cross-TU. */
srModelInstance* MakePolygonBrush(srNode* parent, std::shared_ptr<srColorSurfaceIFace> surface, double width,
                                  double height, float mapping_x, float mapping_y,
                                  float mapping_width, float mapping_height, bool overlay);
/* 0x00484A40: blit one frame of a video object into a color surface. */
bool BlitVideoObjectToColorSurface(const SpriteSheet& sprites, std::uint16_t region,
                                      srColorSurface& destination, std::int32_t x, std::int32_t y);
class stTextureAnim;

/* 0x00428E90: build an stTextureAnim whose frames are VObject subimages. */
std::shared_ptr<stTextureAnim> VideoVObjectToTextureAnim(const SpriteSheet& sprites, std::uint16_t start_frame,
                                         std::uint16_t frame_count, bool use_argb1555);
/* 0x00428A90: mark the primary renderer mode word dirty. */
void SetOverlayRenderMode(void);
/* 0x00428AA0: mark both renderer mode words dirty. */
void SetRendererModePair(void);
/* 0x00428910 / 0x004289C0 / 0x004289E0: the render-probe bracket the region
   link builder uses to count a mesh's drawn faces — begin the probe pass,
   draw the node and return its covered-face count, then end the pass. */
void BeginRenderProbe(void);
std::uint32_t MeasureNodeRender(srNode* node);
void EndRenderProbe(void);

void WarpSystemCursor(std::int32_t x, std::int32_t y); /* 0x004280C0: fullscreen-safe */

class srCamera;
class srModeler;
class srGERD;
class srMaterial;
class srScene;
class stSurface2D;
struct EnvironmentColour;

extern std::int32_t g_pixel_format;
extern std::uint8_t g_fullscreen;
extern bool g_screenshot_pending;
extern bool g_video_inspector_enabled;
extern bool g_cursor_scene_enabled;
extern bool g_fullscreen_scene_last;
extern const std::int32_t* g_overlay_viewport;
extern srModeler* g_modeler;
extern srScene* g_scene_user;
extern srScene* g_scene_fullscreen;
extern srScene* g_scene_permanent;
extern srScene* g_scene_prerender0;
extern srScene* g_scene_prerender1;
extern srScene* g_scene_overlay0;
extern srScene* g_scene_overlay1;
extern srScene* g_scene_square;
extern std::shared_ptr<srColorSurface> g_primary_color_surface;
void DrawColorSurface(srColorSurface* surface, std::int32_t x, std::int32_t y);
srNode* VideoMakePoster(std::shared_ptr<srColorSurfaceIFace> surface, float width, float height,
                        bool additive); /* 0x00424A90 */
extern srCamera* g_overlay_camera;
extern srCamera* g_square_camera;
extern bool g_texture_cache_enabled;
extern srGERD* g_gerd;
/* Secondary renderer device preferred by the offscreen world-render path. */
extern srGERD* g_secondary_gerd;
extern stSurface2D* g_surface_node;
extern std::shared_ptr<srMaterial> g_blit_material;
extern std::shared_ptr<srColorSurface> g_mouse_surface;
extern srNode* g_surface_nodes[0x12c0];
extern std::uint8_t g_block[0x12c0];
extern srModelInstance* g_current_model_instance;
extern std::int32_t g_renderer_mode;
extern std::int32_t g_overlay_render_mode;
extern std::int32_t g_paired_render_mode;
extern float g_surface_scale;
extern std::int32_t g_surface_opaque_shader_bits;
extern std::int32_t g_surface_alpha_blend_shader_bits;
struct W8ViewportRect {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};
extern W8ViewportRect g_viewport;
extern std::int32_t g_dirty_tile_count;
extern std::int32_t g_resident_texture_policy;
extern std::uint8_t g_world_render_enabled;
extern bool g_world_blacked_out;

void SetResidentTexturePolicy(std::int32_t policy);
void SetSurfaceScale(float scale);
void SetTextureCacheSize(std::uint32_t bytes);
void SetSwapInterval(bool enabled);
std::uint8_t GetRendererModeByte(void);
void SetViewport(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
/* Scale a 640x480 design-space rect onto the GERD surface and remember it;
   no-ops when the stored bounds already match. */

void SetWorldScaledViewport(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
/* 0x00425C90: same scaled-viewport update; the automap installs its viewport
   through it. */
void SetAutomapScaledViewport(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
/* Read the stored pixel viewport back out in normalized 0..1 scale. */
struct ViewportBounds {
    glm::vec2 minimum;
    glm::vec2 maximum;
};
ViewportBounds GetScaledViewportBounds();
/* Lock the primary GERD buffer and emit one debug wireframe line. */
void DrawBufferLine(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::uint32_t* pixel);
bool InitializeRendererSceneObjects(void);
void PurgeInactiveSceneInstances(srScene* scene);
void ResetVideoFrameState(void);
void SetPrimarySurfaceTextureHint2Enabled(bool enabled);
bool ClearPrimarySurface(void);
void ResetTransientRenderScenes(void);
void ClearVideoDirtyBlocks(void); /* 0x00423150 */
void RenderScene(srScene* scene, srCamera* camera, const std::int32_t* viewport, bool preserve_fog);
void RenderFrame(void);
/* The native presentation shell also owns this opaque surface entry point. */
SDL_Surface* BeginVideoPresentation(void);
bool FinishVideoPresentation(void);
void PublishLightDirection(const EnvironmentColour* direction);
void GetWorldColour(EnvironmentColour* colour); /* 0x00427290 */
srVector3T<float>* __fastcall SaturateColor(srVector3T<float>* color);
void ReleaseObject(stModelInstance2D* object);
void Initialize16BitPixelFormatMasks(void);
bool CreateWizardryWindow(void);
bool InitializePrimarySurface(void);
void ReleasePrimarySurface(void);
std::uint8_t InitializeVideoDevice(void);
bool SetupDisablesAudio();
bool OpenRendererWindow(void);
void InvalidateRendererTextureCache(void);
void AssertFailureHandler(const char* expression, const char* file, std::int32_t line, const char* message);
bool DisableCursorScene(void);
bool EnableCursorScene(void);
void SetOverlayViewport(const std::int32_t* value);
void SetWorldModelPickingEnabled(bool enabled);
bool RendererBufferIsLockable(void);
void SetRendererAutoFlipEnabled(bool enabled);
bool HasEnoughFreeDiskSpace(void);
srModelInstance* GetPickedModelInstance(void);
void SetPickedModelInstance(srModelInstance* value);
bool IsCursorInsideViewport(void);      /* 0x00428070 */
bool IsCursorImageInsideViewport(void); /* 0x00428030 */

void SetDisplayGamma(float value);
std::uint32_t GetTotalPhysicalMemory(void);
std::int32_t GetRendererFamily(void);

/* Delete a scene object and clear the slot that owned it. */
template <class T> inline void ReleaseRendererObject(T*& object)
{
    delete object;
    object = nullptr;
}

#endif
