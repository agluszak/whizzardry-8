#include "wiz8/entities/keyframe_track.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>

#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/fwd.hpp"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/math.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/transform.h"

bool PathAIUpdate(KeyframeTrack* record, signed char direction)
{
    if (record == 0) {
        return false;
    }
    return PathAITick(record, direction);
}

std::optional<KeyframeTrack> LoadPathAI(wiz8::File& file)
{
    unsigned char version;
    file.read_exact(&version, sizeof(version));
    if (version != 0) throw std::runtime_error("Unknown AI path record kind");
    auto path = std::optional<KeyframeTrack>(std::in_place);
    int point_count;
    file.read_exact(&path->version, 1);
    file.read_exact(&path->position, 4);
    file.read_exact(&path->unknown_08, 4);
    file.read_exact(&point_count, 4);
    const std::int64_t point_bytes = path->version == 2 ? 40 : 28;
    if (point_count < 0 || point_count > (file.size() - file.tell()) / point_bytes)
        throw std::runtime_error("Invalid AI path point count");
    if (point_count == 0) return std::nullopt;
    path->rotations.resize(point_count);
    if (path->version == 2) path->scales.resize(point_count);
    const auto read_vector = [&] {
        glm::vec3 value(0);
        file.read_exact(&value.x, sizeof(float));
        file.read_exact(&value.y, sizeof(float));
        file.read_exact(&value.z, sizeof(float));
        return value;
    };
    for (int index = 0; index < point_count; ++index) {
        glm::vec3 point = read_vector();
        float angle;
        point *= 500.0f;
        PathAIAddPoint(*path, point);
        file.read_exact(&angle, 4);
        const auto axis = read_vector();
        auto& rotation = path->rotations[index];
        rotation = glm::mat3(1);
        if (angle != 0.0) {
            rotation *= glm::mat3_cast(
                glm::angleAxis(float(std::atan2(sin(angle), cos(angle))), normalizedOrZero(axis)));
        }
        if (path->version == 2) path->scales[index] = read_vector();
    }
    return path;
}

void PathAIResetRecord(KeyframeTrack* record)
{
    if (record) {
        record->position = 0;
    }
}

KeyframeTrack KeyframeTrack::restarted() const
{
    auto copy = *this;
    copy.timed = false;
    return copy;
}

void PathAIClearOwned(KeyframeTrack* path)
{
    if (path) {
        path->nodes.clear();
        path->rotations.clear();
        path->scales.clear();
    }
}

void PathAIAddPoint(KeyframeTrack& path, const glm::vec3& point)
{
    if (path.nodes.empty()) {
        path.position = 0;
        path.interpolation_fraction = 0;
        path.distance_travelled = 0;
        path.point_index = 0;
    }
    path.nodes.push_back(point);
    if (path.nodes.size() < 2) {
        path.total_length = 0;
        return;
    }

    float total_length = 0;
    for (std::size_t index = 0; index + 1 < path.nodes.size(); ++index) {
        total_length += glm::length(path.nodes[index] - path.nodes[index + 1]);
    }
    path.total_length = total_length;
    if (path.position > 0.0f && total_length > 0.0f) {
        PathAISetValue(&path, path.distance_travelled / total_length);
    }
}

void PathAIEnableTimedMode(KeyframeTrack* path)
{
    path->animated = true;
    path->timed = true;
}

void PathAIResetTick(KeyframeTrack* path)
{
    path->last_update_tick = MonotonicMilliseconds();
}

float PathAIGetValue(KeyframeTrack* path)
{
    if (path == 0) {
        return 0.0f;
    }
    return path->position;
}

bool PathAINextPoint(KeyframeTrack* path, glm::vec3* point)
{
    if (path == 0 || path->nodes.empty()) {
        return false;
    }
    if (path->point_index >= static_cast<unsigned int>(path->nodes.size())) {
        if (!path->looping) {
            return false;
        }
        path->point_index = 0;
    }
    *point = path->nodes[path->point_index];
    ++path->point_index;
    return true;
}

bool PathAIIsComplete(KeyframeTrack* path)
{
    if (path != 0 && !path->nodes.empty() &&
        (path->point_index < static_cast<unsigned int>(path->nodes.size()) || path->looping)) {
        return false;
    }
    return true;
}

void PathAISetValue(KeyframeTrack* path, float value)
{
    if (path == 0) {
        Fail("pPathAI");
    }
    if (path->animated) {
        path->position = 0.0f;
        path->interpolation_fraction = 0;
        path->distance_travelled = 0;
        path->point_index = 0;
        if (path->total_length <= 0.0f) {
            path->position = value;
            return;
        }
        PathAIAdvanceByDistance(path, value * path->total_length);
        return;
    }
    path->position = value;
}

/* Advance the timed path by `distance` world units: fold it into the elapsed
   accumulator, then walk the node chain past every segment the distance
   covers, ending with the in-segment fraction in interpolation_fraction. */
void PathAIAdvanceByDistance(KeyframeTrack* path, float distance)
{
    glm::vec3 position(0);
    unsigned int index;
    float remaining;
    float segment;

    if (path->total_length <= 0.0f) {
        return;
    }
    path->distance_travelled += distance;
    path->position = path->distance_travelled / path->total_length;
    if (path->position >= 1.0f) {
        if (path->looping) {
            path->position = 0.0f;
            path->interpolation_fraction = 0.0f;
            path->distance_travelled = 0.0f;
            path->point_index = 0;
            return;
        }
        path->position = 1.0f;
        path->interpolation_fraction = 1.0f;
        path->distance_travelled = path->total_length;
        path->point_index = static_cast<int>(path->nodes.size()) - 1;
        return;
    }
    remaining = distance;
    while (true) {
        PathAIPosition(path, &position);
        index = path->point_index + 1;
        segment = glm::length(position - path->nodes[index]);
        if (remaining < segment) {
            break;
        }
        path->point_index = index;
        remaining -= segment;
        path->interpolation_fraction = 0.0f;
        if (static_cast<int>(path->nodes.size()) - 1U <= index) {
            path->point_index = static_cast<int>(path->nodes.size()) - 1;
            path->position = 1.0f;
            path->interpolation_fraction = 1.0f;
            return;
        }
    }
    index = path->point_index;
    segment = glm::length(path->nodes[index] - path->nodes[index + 1]);
    path->interpolation_fraction = (segment * path->interpolation_fraction + remaining) / segment;
}

void PathAIAdvanceNormalized(KeyframeTrack* path, float amount)
{
    unsigned int index;
    float position;

    position = amount * path->speed / static_cast<int>(path->nodes.size()) + path->position;
    path->position = position;
    if (position < 1.0f) {
        position *= static_cast<int>(path->nodes.size());
        index = static_cast<unsigned int>(position);
        path->point_index = index;
        path->interpolation_fraction = position - index;
        return;
    }
    if (!path->looping) {
        path->position = 1.0f;
        path->interpolation_fraction = 1.0f;
        path->distance_travelled = path->total_length;
        path->point_index = static_cast<int>(path->nodes.size()) - 1;
        return;
    }
    path->position = 0.0f;
    path->interpolation_fraction = 0.0f;
    path->distance_travelled = 0.0f;
    path->point_index = 0;
}

bool PathAITick(KeyframeTrack* path, signed char direction)
{
    uint64_t now;
    uint64_t elapsed;
    float amount;
    float point_count;

    if (path == 0) {
        Fail("pPathAI");
    }
    now = MonotonicMilliseconds();
    if (!path->discrete_mode && path->animated) {
        elapsed = now - path->last_update_tick;
        if (path->last_update_tick < now) {
            if (path->timed) {
                PathAIAdvanceNormalized(path, elapsed * 0.001f);
            } else {
                point_count = static_cast<float>(path->nodes.size());
                PathAIAdvanceByDistance(path, path->total_length / point_count * path->speed *
                                                  elapsed * 0.001f);
            }
            path->last_update_tick = now;
            return true;
        }
    } else {
        if (!path->step_by_node) {
            amount = (now - path->last_update_tick) * 0.001f * direction * path->speed;
        } else {
            amount = -1.0f;
            if (direction > 0) {
                amount = 1.0f;
            }
        }
        path->position += amount;
        if (path->position >= 0.0f) {
            point_count = static_cast<float>(path->nodes.size());
            if (path->position < point_count) {
                path->last_update_tick = now;
                return true;
            }
            if (!path->looping) {
                path->last_update_tick = now;
                path->position = point_count - 1.0f;
                return true;
            }
        } else if (path->looping) {
            path->last_update_tick = now;
            path->position = static_cast<int>(path->nodes.size()) - 1.0f;
            return true;
        }
        path->position = 0.0f;
    }
    path->last_update_tick = now;
    return true;
}

void PathAIPosition(const KeyframeTrack* path, glm::vec3* value)
{
    if (!path || path->nodes.empty()) return;
    if (!path->animated) {
        int index = path->discrete_mode
                        ? roundedInt32(path->position + 0.1)
                        : roundedInt32((path->nodes.size() - 1) * path->position + 0.5);
        index = std::min(index, static_cast<int>(path->nodes.size()) - 1);
        if (index >= 0) *value = path->nodes[index];
        return;
    }
    const auto index = path->point_index;
    if (index >= path->nodes.size()) return;
    if (index + 1 < path->nodes.size()) {
        *value = path->nodes[index] * (1.0f - path->interpolation_fraction) +
                 path->nodes[index + 1] * path->interpolation_fraction;
    } else {
        *value = path->nodes[index];
    }
}

Transform3D sampleKeyframeTrack(const KeyframeTrack& path, const Transform3D& initial)
{
    auto result = initial;
    glm::vec3 position(result.position);
    PathAIPosition(&path, &position);
    result.position = position;
    if (path.nodes.empty()) return result;
    int index = path.discrete_mode ? roundedInt32(path.position + 0.1) : path.point_index;
    index = std::clamp(index, 0, static_cast<int>(path.nodes.size()) - 1);
    const float blend = path.discrete_mode || index + 1 >= static_cast<int>(path.nodes.size())
                            ? -1
                            : path.interpolation_fraction;
    if (static_cast<std::size_t>(index) < path.rotations.size()) {
        glm::dmat3 rotation(path.rotations[index]);
        if (blend > 0 && static_cast<std::size_t>(index + 1) < path.rotations.size()) {
            const auto first = glm::quat_cast(rotation);
            auto next = glm::dquat(glm::quat_cast(path.rotations[index + 1]));
            auto dot = glm::dot(first, next);
            if (dot < 0) {
                next = -next;
                dot = -dot;
            }
            const auto interpolated = 1.0 - dot <= kSlerpEpsilon
                                          ? first * (1.0 - blend) + next * double(blend)
                                          : glm::slerp(first, next, double(blend));
            rotation = glm::mat3_cast(glm::normalize(interpolated));
        }
        result.rotation = rotation;
    }
    if (static_cast<std::size_t>(index) < path.scales.size()) result.scale = path.scales[index];
    if (path.upright) result.rotateX(1.5707963);
    return result;
}
