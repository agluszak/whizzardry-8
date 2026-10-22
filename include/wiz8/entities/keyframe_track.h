#ifndef WIZ8_KEYFRAME_TRACK_H
#define WIZ8_KEYFRAME_TRACK_H

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/transform.h"

namespace wiz8 {
class File;
}

struct KeyframePoint {
    glm::vec3 position{0};
    std::optional<glm::mat3> rotation{};
    std::optional<glm::vec3> scale{};
};

struct KeyframeGeometry {
    explicit KeyframeGeometry(std::vector<KeyframePoint> points);
    std::vector<KeyframePoint> points;
    float length = 0;
};

/* Immutable authored geometry is shared; cursor and timing belong to the player. */

struct KeyframeTrack {
    KeyframeTrack() = default;
    explicit KeyframeTrack(std::vector<KeyframePoint> points);
    std::shared_ptr<const KeyframeGeometry> geometry;
    std::span<const KeyframePoint> points() const
    {
        return geometry ? std::span<const KeyframePoint>(geometry->points)
                        : std::span<const KeyframePoint>{};
    }
    float length() const { return geometry ? geometry->length : 0; }
    KeyframeTrack restarted() const;

    /* Serialized record selector: value 2 adds a per-point scale array. */
    unsigned char version{};
    /* Normalized for interpolated movement, point units in discrete mode. */
    float position{};
    unsigned int unknown_08{};             /* serialized */
    int entry_index{};                     /* level camera/path entry index, assigned at load */
    bool discrete_mode{};
    unsigned int point_index{};
    float interpolation_fraction{};
    uint64_t last_update_tick{}; // Held world-clock milliseconds.
    float speed{};
    float distance_travelled{};
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

void PathAIAdvanceByDistance(KeyframeTrack* path, float value);

#endif
