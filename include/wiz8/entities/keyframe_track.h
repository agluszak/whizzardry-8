#include <optional>
#ifndef WIZ8_KEYFRAME_TRACK_H
#define WIZ8_KEYFRAME_TRACK_H

#include <cstdint>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/transform.h"

namespace wiz8 {
class File;
}

/* Runtime path data. File records are read field by field by LoadPathAI. */

struct KeyframeTrack {
    KeyframeTrack() = default;
    KeyframeTrack(const KeyframeTrack&) = default;
    KeyframeTrack& operator=(const KeyframeTrack&) = default;
    ~KeyframeTrack() = default;
    KeyframeTrack restarted() const;

    /* Serialized record selector: value 2 adds a per-point scale array. */
    unsigned char version{};
    /* Normalized for interpolated movement, point units in discrete mode. */
    float position{};
    unsigned int unknown_08{};             /* serialized */
    std::vector<glm::vec3> nodes;
    int entry_index{};                     /* level camera/path entry index, assigned at load */
    std::vector<glm::mat3> rotations;
    std::vector<glm::vec3> scales;
    bool discrete_mode{};
    unsigned int point_index{};
    float interpolation_fraction{};
    uint64_t last_update_tick{};
    float speed{};
    float distance_travelled{};
    float total_length{};
    bool looping{};
    bool step_by_node{};
    bool animated{};
    /* When set the emitter target is pitched upright (rotateX pi/2); camera
       paths raise it. */
    bool upright{};
    bool timed{};
};

bool PathAIUpdate(KeyframeTrack* record, signed char direction);
void PathAIResetRecord(KeyframeTrack* record);
Transform3D sampleKeyframeTrack(const KeyframeTrack& path, const Transform3D& initial);
void PathAIClearOwned(KeyframeTrack* path);
void PathAIEnableTimedMode(KeyframeTrack* path);
void PathAIResetTick(KeyframeTrack* path);
float PathAIGetValue(KeyframeTrack* path);
bool PathAINextPoint(KeyframeTrack* path, glm::vec3* point);
bool PathAIIsComplete(KeyframeTrack* path);
void PathAISetValue(KeyframeTrack* path, float value);
void PathAIAdvanceNormalized(KeyframeTrack* path, float amount);
bool PathAITick(KeyframeTrack* path, signed char direction);
void PathAIPosition(const KeyframeTrack* path, glm::vec3* value);
std::optional<KeyframeTrack> LoadPathAI(wiz8::File& file);
void PathAIAddPoint(KeyframeTrack& path, const glm::vec3& point);

void PathAIAdvanceByDistance(KeyframeTrack* path, float value);

#endif
