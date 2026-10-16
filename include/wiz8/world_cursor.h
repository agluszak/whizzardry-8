#pragma once

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"

class W8Monster;
struct W8MonsterInfo;
class stParticle;
class srNode;
class stLight;

struct W8WorldCursorState {
    /* detached with the world when the cursor hides. */
    W8Monster* monster = nullptr;
    /* deactivated with a zero when the cursor hides. */
    stParticle* particle = nullptr;
    /* set by the right-button path while light exists; the group
       bind consumes and clears it. */
    bool group_bind_pending{};
    /* accumulated cursor input - screen dx, right-drag dy and screen
       dy - scaled by 1e+01f into the movement vector each update,
       then cleared. The group-bind paths also bump .z. */
    glm::ivec3 input_delta{0};
    /* camera-relative offset the placement update derives from
       position. */
    glm::vec3 cam_rel_offset{0};
    /* the light node the complete teardown removes from the world. */
    stLight* light = nullptr;
    /* read back by the path-visualization update as a world point. */
    glm::vec3 position{0};
    /* the last position published to the cursor's nodes and monster;
       the update compares it against position to detect a move. Seeded
       to the -1e7 sentinel by the placement update. */
    glm::vec3 last_published{0};
    /* authored name fEnabled - the cursor update asserts it. */
    bool enabled{};
    /* ground tracking. Set by the initializer and the placement
       update; the target march lifts each step to the settled ground height
       while set and re-arms it when the result lands near the ground, and
       the cursor update settles position to the terrain while set. */
    bool track_ground{};
    /* the cursor's march range, initialized to 50000; both movement
       paths clamp the step/offset length to it. */
    float range{};
    /* left-button latch - releasing the button while set is the
       placement click. */
    bool left_held{};
    /* the selected monster group id - passed to
       GetMonsterGroupIndexByID, assigned from monster_group->group_id,
       written by SetWorldCursorGroupId and seeded from/restored to
       g_cursor_saved_group_id. */
    int monster_group_id{};
    /* when set the cursor is detached from the camera - input moves
       position directly and the placement update skips the
       camera-relative cam_rel_offset store. */
    bool detached{};
    /* while detached, run the ground/sight march on the moved
       point. */
    bool march_enabled{};
    /* midpoint of the probe box; the target march traces its sight fan
       from this offset. */
    glm::vec3 probe_center{0};
    /* eight probe offsets forming a box around the cursor point. The
       target march sight-traces all eight and ground-settles the last four;
       the target resolver ground-probes the same last four. */
    glm::vec3 probe_offsets[8]{};
    /* footprint-placement mode - the target resolver uses the two
       fixed probe offsets and the click path requires the occupied-box
       test before placing. */
    bool footprint_mode{};
    /* first fixed probe offset used when footprint_mode is set. */
    glm::vec3 extent_min{0};
    /* second fixed probe offset used when footprint_mode is set. */
    glm::vec3 extent_max{0};
    /* the monster info latched by the shift-drag; the update drags
       it to the cursor position and releases it when shift lifts. */
    W8MonsterInfo* dragged_info = nullptr;
};

/* authored name gp3DCursor - the cursor update asserts it. */
extern W8WorldCursorState* gp3DCursor;
/* Build the 3D cursor, light, particle and initial camera-relative bounds. */
void InitializeWorldCursor(void);
bool IsWorldCursorVisible(void);
void GetWorldCursorPosition(glm::vec3* position);
void SetWorldCursorNodesVisible(bool visible);
bool SelectWorldCursorNode(void);
int GetWorldCursorNodeCount(void);
void HideWorldCursor(void);
void ShowWorldCursor(void);
/* Clamp and install an action-range distance into the live world cursor, then
   invalidate last_published so the next update republishes. */
void SetWorldCursorRange(float distance); /* 0x00491650 */
void SetWorldCursorGroupId(int group_id); /* 0x004916A0 */
/* Arm footprint placement: the target resolver ground-probes the two fixed
   offsets instead of the probe box. */
void SetWorldCursorExtents(const glm::vec3* minimum, const glm::vec3* maximum); /* 0x00492190 */
/* 0x00490C20: copies the cursor state vector at +0x28, or zero when there is
   no cursor. */
void GetWorldCursorAnchor(glm::vec3* position);
void ReleaseWorldCursor(void);
void ReleaseWorldCursorNodes(void);
