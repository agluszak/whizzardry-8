#include "glm/ext/vector_float3.hpp"
#include "wiz8/design_rect.h"
#ifndef WIZ8_VIDEO2_H
#define WIZ8_VIDEO2_H

#include <cstdint>
#include <memory>
#include <string_view>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include "compat/compiler.h"
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
extern void GetCurrentVideoSettings(std::uint16_t* usWidth, std::uint16_t* usHeight,
                                    std::uint8_t* ubBitDepth);
/* Mark a game-space rectangle of the UI canvas for upload this frame. */
void InvalidateRegion(int left, int top, int right, int bottom);
inline void InvalidateRegion(SDL_Rect bounds)
{
    InvalidateRegion(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
}
/* The pending UI upload rectangle, cleared by the call. */
SDL_Rect TakeUiUpload();
extern void PrintScreen(void);

void VideoCaptureToggle(void);

enum { INVAL_SRC_TRANS = 1, PIXEL_DEPTH = 16, SCREEN_WIDTH = 640, SCREEN_HEIGHT = 480 };

bool VideoIsFullScreen(void);
void VideoFullScreen(bool enabled);
bool VideoResizeWindow(void);
void VideoInspectorEnable(void);
bool VideoInspectorIsEnabled(void);
int VideoDumpMemoryLeaks(void);
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

void InvalidateScreenRects(DesignRect* rects, unsigned int count);

/* Renderer state and helpers shared by the product C++ consumers. */
extern bool g_auto_capture;
extern int g_cursor_image_height;
/* 0x00652DA4: set while the swaying camera view is active; see
   SetCameraSwayMode in 3dapi.cpp. */
extern bool g_camera_sway_active;
extern int g_screenshot_index;
extern int g_screenshot_page;
void ClearSurfaceRect(int left, unsigned int top, int right, unsigned int bottom);
/* Clear a rectangle of the interface overlay and upload it. */
void EraseRegion(int left, int top, int right, int bottom);

extern const float g_viewport_x_scale;
extern const float g_viewport_y_scale;

struct SDL_Surface;
class srModelInstance;
class srNode;
class srTextureIFace;
class stModelInstance2D;

srNode* MakePosterQuad(std::shared_ptr<srTextureIFace> texture, float width, float height,
                       bool additive);
/* 0x00425190: build a 2D marker model instance over a texture. */
stModelInstance2D* CreateSpriteFromTexture(std::shared_ptr<srTextureIFace> texture, double width,
                                           double height, bool keep_aspect, bool overlay);
/* 0x00426F80: render the world into a caller-owned color surface through a
   scissored viewport, then blit the locked frame buffer onto the target. */
unsigned char RenderWorldToSurface(SDL_Surface* target, const DesignRect* rect,
                                   bool render_secondary);
void SetPickKey(void* key);

struct W8ControlsRect;

/* 0x00424790: build a solid-color quad sprite; width/height are pixel counts
   and `color` becomes the material's emissive vector. MGSRadarMap's blip
   templates are its observed callers. */
void SetFullscreenSceneLast(unsigned char value); /* 0x004298E0 */
stModelInstance2D* CreateColoredPolygonSprite(int width, int height, const glm::vec4* color,
                                              bool fullscreen);
/* Copies an optional source rectangle into a square or ordinary overlay. */
stModelInstance2D* CreateSpriteFromSurface(SDL_Surface& surface, const W8ControlsRect* bounds,
                                           int square, int overlay);
/* 0x004257D0: position a 2D node without pixel snapping. */
void Position2DNodeUnsnapped(srNode* node, int x, int y);
/* 0x004264F0: write the display-state byte of a 2D model instance. */
void SetModelInstance2DDisplayState(stModelInstance2D* object, unsigned char state);
/* 0x00425820: set FLAG_DISABLE on a node; the overlay drawers call it to hide
   the highlight sprite before redrawing the panel contents. */
void ClearNodeFlag(srNode* node);
/* 0x00425840: rotate an srNode in degrees and invalidate the renderer mode. */
void RotateNodeInDegrees(srNode* node, int degrees);
/* Invalidate a 2D node's model texture and release the node. */
/* 0x00427E70: surface-lock helper used while installing a drag cursor. */
bool ClearMouseSurface(void);
/* 0x004255F0: place a 2D node at a screen position in normalized
   coordinates; positional snaps to the renderer's pixel grid. */
void PositionToolTipNode(srNode* node, int x, int y, bool positional);
/* 0x00427460: the debug stats readout - frame rate always, the full counter
   block in inspector mode 2, or the scaled camera position in mode 3. */
void DrawVideoInspector(int left, unsigned int top);
/* 0x00428830: the option-4-suppressed render probe - draws the node between
   the dynamic scene's bracketing passes and returns the sampled statistic. */
unsigned int MeasureNodeRenderWithoutPositionalOption(srNode* node);
/* 0x00424EB0: build the 2D polygon-brush model instance over a surface;
   RenderAutomapMarkers' item-marker factory calls it cross-TU. */
srModelInstance* MakePolygonBrush(srNode* parent, std::shared_ptr<SDL_Surface> surface,
                                  double width, double height, float mapping_x, float mapping_y,
                                  float mapping_width, float mapping_height, unsigned char overlay);
/* 0x00484A40: blit one frame of a video object into a color surface. */
bool BlitVideoObjectToColorSurface(const SpriteSheet& sprites, std::uint16_t region,
                                   SDL_Surface& destination, int x, int y);
class stTextureAnim;

/* 0x00428E90: build an stTextureAnim whose frames are VObject subimages. */
std::shared_ptr<stTextureAnim> VideoVObjectToTextureAnim(const SpriteSheet& sprites,
                                                         unsigned short start_frame,
                                                         unsigned short frame_count,
                                                         bool use_argb1555);
/* 0x00428A90: mark the primary renderer mode word dirty. */
void SetOverlayRenderMode(void);
/* 0x00428AA0: mark both renderer mode words dirty. */
void SetRendererModePair(void);
/* 0x00428910 / 0x004289C0 / 0x004289E0: the render-probe bracket the region
   link builder uses to count a mesh's drawn faces — begin the probe pass,
   draw the node and return its covered-face count, then end the pass. */
void BeginRenderProbe(void);
unsigned int MeasureNodeRender(srNode* node);
void EndRenderProbe(void);

void WarpSystemCursor(int x, int y); /* 0x004280C0: fullscreen-safe */

class srCamera;
class srGERD;
class srMaterial;
class srScene;
class stSurface2D;

extern int g_pixel_format;
extern unsigned char g_fullscreen;
extern bool g_screenshot_pending;
extern bool g_video_inspector_enabled;
extern bool g_cursor_scene_enabled;
extern unsigned char g_fullscreen_scene_last;
extern std::shared_ptr<SDL_Surface> g_primary_color_surface;
void DrawColorSurface(SDL_Surface* surface, int x, int y);
srNode* VideoMakePoster(std::shared_ptr<SDL_Surface> surface, float width, float height,
                        bool additive); /* 0x00424A90 */
extern bool g_texture_cache_enabled;
extern std::unique_ptr<srGERD> g_gerd;
/* Secondary renderer device preferred by the offscreen world-render path. */
extern stSurface2D* g_surface_node;
extern std::shared_ptr<srMaterial> g_blit_material;
extern std::shared_ptr<SDL_Surface> g_mouse_surface;
extern srModelInstance* g_current_model_instance;
extern int g_overlay_render_mode;
extern int g_paired_render_mode;
extern float g_surface_scale;
extern int g_surface_opaque_shader_bits;
extern int g_surface_alpha_blend_shader_bits;
extern DesignRect g_viewport;
extern int g_resident_texture_policy;
extern unsigned char g_world_render_enabled;
extern bool g_world_blacked_out;

void SetResidentTexturePolicy(int policy);
void SetSurfaceScale(float scale);
void SetTextureCacheSize(w8_ulong bytes);
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
/* Lock the primary GERD buffer and emit one debug wireframe line. */
void DrawBufferLine(w8_long x0, w8_long y0, w8_long x1, w8_long y1, w8_ulong* pixel);
unsigned char InitializeRendererSceneObjects(void);
void PurgeInactiveSceneInstances(srScene* scene);
void ResetVideoFrameState(void);
void SetPrimarySurfaceTextureHint2Enabled(bool enabled);
unsigned char ClearPrimarySurface(void);
void ResetTransientRenderScenes(void);
void ClearVideoDirtyBlocks(void); /* 0x00423150 */
void RenderScene(srScene* scene, srCamera* camera, const DesignRect* viewport, bool preserve_fog);
void RenderFrame(void);
/* The native presentation shell also owns this opaque surface entry point. */
SDL_Surface* BeginVideoPresentation(void);
unsigned char FinishVideoPresentation(void);
void PublishLightDirection(const glm::vec3* direction);
void GetWorldColour(glm::vec3* colour); /* 0x00427290 */
void ReleaseObject(stModelInstance2D* object);
void Initialize16BitPixelFormatMasks(void);
unsigned char CreateWizardryWindow(void);
unsigned char InitializePrimarySurface(void);
void ReleasePrimarySurface(void);
unsigned char InitializeVideoDevice(void);
bool SetupDisablesAudio();
unsigned char OpenRendererWindow(void);
void InvalidateRendererTextureCache(void);
unsigned char DisableCursorScene(void);
unsigned char EnableCursorScene(void);
void SetWorldModelPickingEnabled(bool enabled);
bool RendererBufferIsLockable(void);
void SetRendererAutoFlipEnabled(bool enabled);
bool HasEnoughFreeDiskSpace(void);
srModelInstance* GetPickedModelInstance(void);
void SetPickedModelInstance(srModelInstance* value);
bool IsCursorInsideViewport(void);      /* 0x00428070 */
bool IsCursorImageInsideViewport(void); /* 0x00428030 */

/* Delete a scene object and clear the slot that owned it. */
template<class T> inline void ReleaseRendererObject(T*& object)
{
    delete object;
    object = nullptr;
}

#endif
