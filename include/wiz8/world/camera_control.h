#pragma once
#include "glm/ext/vector_float3.hpp"
#include "wiz8/world/party_camera.h"

class Navigator;
struct World;

extern bool g_world_cleanup_flag;
extern bool g_navigator_vertical_enabled;
extern float g_mesh_view_half_angle_degrees;

/* One frame of party movement in the primary world under the frame's motion flags
   (camera_motion.h): turning and looking, motion against the collision index, the
   automap's visited cells and the camera paths. */
void updatePartyMovement(World& world, unsigned int flags);
/* Place the party navigator at the party's pose: `navigationPosition` of the eye, with
   the camera's angles. */
void mirrorPartyNavigator(Navigator& navigator, const PartyCamera& camera);
/* Derive the rendered views from the party: the primary world's camera and its light at
   the eye plus `view_offset` (the frame's shake), and the sky camera with the same
   orientation and field of view. Rendering changes nothing else. */
void placeWorldCameras(World& world, World* sky, const glm::vec3& view_offset);
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
void ApplyWorldUpdateFlags(World* world, unsigned int flags);
