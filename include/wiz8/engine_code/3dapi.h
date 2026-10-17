#pragma once

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"

class srCamera;
struct W8World;

extern bool g_world_cleanup_flag;
extern bool g_navigator_vertical_enabled;
extern float g_mesh_view_half_angle_degrees;

/* 0x00450780: the three-argument assert the main-game code paths use; it
   forwards to the four-argument SurRender export. */
void ReportAssertion(const char* expression, const char* source_path, w8_long line);
void UpdateWorldCameraAndPaths(W8World* world, unsigned int flags);
/* Debug world controls combine these bits in unsigned int storage. */
enum {
    W8_WORLD_INCREASE_FAR_CLIP = 0x01u,
    W8_WORLD_DECREASE_FAR_CLIP = 0x02u,
    W8_WORLD_WIDEN_MESH_VIEW = 0x04u,
    W8_WORLD_NARROW_MESH_VIEW = 0x08u,
    W8_WORLD_INCREASE_ENVIRONMENT = 0x100u,
    W8_WORLD_DECREASE_ENVIRONMENT = 0x200u,
    W8_WORLD_CHANGE_FAR_CLIP = W8_WORLD_INCREASE_FAR_CLIP | W8_WORLD_DECREASE_FAR_CLIP
};
void ApplyWorldUpdateFlags(W8World* world, unsigned int flags);
void SetCameraSwayMode(srCamera* camera, int mode);
void WorldSetCameraLocation(const glm::vec3* location); /* 0x00450420 */
