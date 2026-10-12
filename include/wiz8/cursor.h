#ifndef WIZ8_CURSOR_H
#define WIZ8_CURSOR_H

#include <cstdint>
#include "glm/ext/vector_float2.hpp"
#include "himage.h"
#include "input.h"
#include "wiz8/engine_code/Video2.h"
#include <memory>

class srModelInstance;
class srScene;

extern srModelInstance* g_cursor_node;
extern srScene* g_cursor_scene;
bool InitializeMouseCursorScene(void);

class srTextureIFace;

bool SetMouseCursorFromVideoObject(const SpriteSheet& sprites, std::uint16_t region,
                                   std::int16_t offset_x, std::int16_t offset_y);
void BlitToMouseCursor(const SpriteSheet& sprites, std::uint16_t region, std::uint16_t x,
                       std::uint16_t y);
void RefreshMouseCursorTexture(void);
bool ResizeMouseCursorSurface(std::int32_t width, std::int32_t height);      /* 0x00427c90 */
void SetMouseCursorHotspot(std::int16_t hotspot_x, std::int16_t hotspot_y); /* 0x00427f00 */
void SetMouseCursorTexture(std::shared_ptr<srTextureIFace> texture);          /* 0x00429170 */
void SyncSystemCursor(void);                                  /* 0x00428340 */

/* Indices into the 17-slot main-game cursor table that SetTargetCursor takes.
   The parameter stays int because retail mangles it as `H`. Only slots with
   recovered meaning are named. */
enum {
    W8_CURSOR_NONE = -1,
    W8_CURSOR_VALID_TARGET = 6,
    W8_CURSOR_INVALID_TARGET = 7,
    W8_CURSOR_TEXT_INPUT = 8,
    W8_CURSOR_MAP_LOAD = 9
};

extern std::int32_t g_cursor_width;
extern std::int32_t g_cursor_height;
extern std::int32_t g_cursor_hotspot_x;
extern std::int32_t g_cursor_hotspot_y;

/* 0x00428580 and 0x004285A0: the packed atom mouse position projected into
   screen space by the current cursor hotspot. */
std::int32_t GetEventCursorX(const InputEvent* atom);
std::int32_t GetEventCursorY(const InputEvent* atom);

void PositionMouseCursor(std::int32_t x, std::int32_t y, bool reset_tick);
/* 0x00428220: milliseconds since PositionMouseCursor last stamped the move
   tick. */
std::uint32_t GetMillisecondsSinceCursorMove(void);
/* 0x00428520: whether the current cursor hotspot is inside the inclusive
   rectangle (left, top, right, bottom). */
bool IsCursorInRectangle(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom);
bool GetCursorPositionInViewport(glm::vec2* position);
/* The tracked cursor tip in normalized screen coordinates. */
glm::vec2 GetCursorScaledPosition();
void UpdateHeldItemCursor(void);
void ClearHeldItemDisplay(void);
void SetItemCursor(std::int32_t overlay_video_object); /* 0x0055F160 */

#endif
