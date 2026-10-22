#pragma once
#include "glm/ext/vector_float3.hpp"

struct World;

/* 0x00490AF0: toggle the 3D world cursor. If a cursor exists, release it;
   if not, initialize one. */
void ToggleWorldCursor(void);
/* 0x00490C60: scale and clear the accumulated input deltas, move the cursor
   inside its range/ground constraints and republish a moved position. */
void ApplyWorldCursorInput(void);
/* 0x004916C0: the per-frame world-cursor update - mouse movement, group bind
   and the placement click. */
void UpdateWorldCursor(void);
/* 0x004914E0: when the cursor's group-bind flag is set, locate the monster
   group recorded on the cursor and attach its leader at the cursor point. */
void BindCursorMonsterToGroup(void);
/* 0x00491EC0: place the world cursor's target point the world's recalled distance in
   front of the camera and re-derive the cursor position from it. */
void UpdateWorldCursorPlacement(void);
/* 0x004919E0: march the requested cursor target from the stored position,
   ground-lifting and sight-checking each step; returns 1 when the march is
   cut short by a blocked sight ray. */
signed char MarchWorldCursorTarget(glm::vec3* target);
/* 0x004921E0: resolve the world cursor's target position into the out
   vector: copy the cursor position, ground-probe its candidate offsets and
   keep the settled height. */
int ResolveWorldCursorTarget(glm::vec3* position);
void GetWorldCursorTargetPosition(glm::vec3* position);
// 0x00492530: a new cursor in `world` is placed at the default distance again.
void ResetWorldCursorDistance(World& world);
