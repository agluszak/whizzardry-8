#include <optional>
#ifndef WIZ8_ENGINE_CODE_CAMERA_H
#define WIZ8_ENGINE_CODE_CAMERA_H

#include <memory>

#include "glm/ext/vector_float3.hpp"

struct World;
#include "wiz8/entities/keyframe_track.h"
struct W8MonsterInfo;

/* Runtime camera entry. The world owns the entry, which owns its path data;
   the level file's fixed-width camera record is decoded separately. */
struct W8CameraPath {
    W8CameraPath();
    ~W8CameraPath();

    char name0[0x14]{};
    bool active{};
    std::optional<KeyframeTrack> path;
};

/* Engine Code\Camera.cpp. The TU's only anchor; turns a camera path on and
   off for the world and dispatches the per-path end actions. */
void UpdateCameraPathState(World* world, W8CameraPath* path, int active);
/* 0x0048F280 is UpdateCameraPathStateByName in World.h: activate the world's
   camera path whose name0 matches `name` (case-insensitive), then dispatch
   through UpdateCameraPathState. */
/* 0x0048F650: face the camera at a monster's head; force overrides the
   tracking-mode gate, animate chooses the eased transition over the snap. */
void PointCameraAtMonster(W8MonsterInfo* monster_info, bool force, bool animate);
/* 0x0048F800: the position-taking variant of the camera orientation helper. */
void PointCameraAtTarget(glm::vec3* position, bool force, bool animate);

#endif
