#ifndef WIZ8_ENGINE_CODE_CAMERA_H
#define WIZ8_ENGINE_CODE_CAMERA_H

#include <memory>

struct W8World;
struct W8PathAI;
struct W8MonsterInfo;
template<class T> class srVector3T;

/* Runtime camera entry. The world owns the entry, which owns its path data;
   the level file's fixed-width camera record is decoded separately. */
struct W8CameraPath {
    W8CameraPath();
    ~W8CameraPath();

    char name0[0x14]{};
    bool active{};
    std::unique_ptr<W8PathAI> path;
};

/* Engine Code\Camera.cpp. The TU's only anchor; turns a camera path on and
   off for the world and dispatches the per-path end actions. */
void UpdateCameraPathState(W8World* world, W8CameraPath* path, int active);
/* 0x0048F280 is UpdateCameraPathStateByName in World.h: activate the world's
   camera path whose name0 matches `name` (case-insensitive), then dispatch
   through UpdateCameraPathState. */
/* 0x0048F650: face the camera at a monster's head; force overrides the
   tracking-mode gate, animate chooses the eased transition over the snap. */
void PointCameraAtMonster(W8MonsterInfo* monster_info, bool force, bool animate);
/* 0x0048F800: the position-taking variant of the camera orientation helper. */
void PointCameraAtTarget(srVector3T<float>* position, bool force, bool animate);

#endif
