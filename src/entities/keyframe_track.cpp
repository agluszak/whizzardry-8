#include "wiz8/entities/keyframe_track.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

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
#include "wiz8/render/transform.h"

bool PathAIUpdate(KeyframeTrack* record, signed char direction, std::uint64_t now)
{
    if (record == 0) {
        return false;
    }
    return PathAITick(record, direction, now);
}

KeyframeGeometry::KeyframeGeometry(std::vector<KeyframePoint> values) : points(std::move(values))
{
    for (std::size_t index = 1; index < points.size(); ++index)
        length += glm::length(points[index - 1].position - points[index].position);
}

KeyframeTrack::KeyframeTrack(std::vector<KeyframePoint> points)
    : geometry(std::make_shared<const KeyframeGeometry>(std::move(points)))
{
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
    std::vector<KeyframePoint> points(point_count);
    const auto read_vector = [&] {
        glm::vec3 value(0);
        file.read_exact(&value.x, sizeof(float));
        file.read_exact(&value.y, sizeof(float));
        file.read_exact(&value.z, sizeof(float));
        return value;
    };
    for (auto& point : points) {
        point.position = read_vector() * 500.0f;
        float angle;
        file.read_exact(&angle, 4);
        const auto axis = read_vector();
        point.rotation = glm::mat3(1);
        if (angle != 0.0) {
            *point.rotation *= glm::mat3_cast(
                glm::angleAxis(float(std::atan2(sin(angle), cos(angle))), normalizedOrZero(axis)));
        }
        if (path->version == 2) point.scale = read_vector();
    }
    path->geometry = std::make_shared<const KeyframeGeometry>(std::move(points));
    path->position = 0; // Loading the first point starts playback, regardless of the record cursor.
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

void PathAIEnableTimedMode(KeyframeTrack* path)
{
    path->animated = true;
    path->timed = true;
}

void PathAIResetTick(KeyframeTrack* path, std::uint64_t now)
{
    path->last_update_tick = now;
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
    if (path == 0 || path->points().empty()) {
        return false;
    }
    if (path->point_index >= static_cast<unsigned int>(path->points().size())) {
        if (!path->looping) {
            return false;
        }
        path->point_index = 0;
    }
    *point = path->points()[path->point_index].position;
    ++path->point_index;
    return true;
}

bool PathAIIsComplete(KeyframeTrack* path)
{
    if (path != 0 && !path->points().empty() &&
        (path->point_index < static_cast<unsigned int>(path->points().size()) || path->looping)) {
        return false;
    }
    return true;
}

void PathAISetValue(KeyframeTrack* path, float value)
{
    assert(path != 0);
    if (path->animated) {
        path->position = 0.0f;
        path->interpolation_fraction = 0;
        path->distance_travelled = 0;
        path->point_index = 0;
        if (path->length() <= 0.0f) {
            path->position = value;
            return;
        }
        PathAIAdvanceByDistance(path, value * path->length());
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

    if (path->length() <= 0.0f) {
        return;
    }
    path->distance_travelled += distance;
    path->position = path->distance_travelled / path->length();
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
        path->distance_travelled = path->length();
        path->point_index = static_cast<int>(path->points().size()) - 1;
        return;
    }
    remaining = distance;
    while (true) {
        PathAIPosition(path, &position);
        index = path->point_index + 1;
        segment = glm::length(position - path->points()[index].position);
        if (remaining < segment) {
            break;
        }
        path->point_index = index;
        remaining -= segment;
        path->interpolation_fraction = 0.0f;
        if (static_cast<int>(path->points().size()) - 1U <= index) {
            path->point_index = static_cast<int>(path->points().size()) - 1;
            path->position = 1.0f;
            path->interpolation_fraction = 1.0f;
            return;
        }
    }
    index = path->point_index;
    segment = glm::length(path->points()[index].position - path->points()[index + 1].position);
    path->interpolation_fraction = (segment * path->interpolation_fraction + remaining) / segment;
}

void PathAIAdvanceNormalized(KeyframeTrack* path, float amount)
{
    unsigned int index;
    float position;

    position = amount * path->speed / static_cast<int>(path->points().size()) + path->position;
    path->position = position;
    if (position < 1.0f) {
        position *= static_cast<int>(path->points().size());
        index = static_cast<unsigned int>(position);
        path->point_index = index;
        path->interpolation_fraction = position - index;
        return;
    }
    if (!path->looping) {
        path->position = 1.0f;
        path->interpolation_fraction = 1.0f;
        path->distance_travelled = path->length();
        path->point_index = static_cast<int>(path->points().size()) - 1;
        return;
    }
    path->position = 0.0f;
    path->interpolation_fraction = 0.0f;
    path->distance_travelled = 0.0f;
    path->point_index = 0;
}

bool PathAITick(KeyframeTrack* path, signed char direction, std::uint64_t now)
{
    uint64_t elapsed;
    float amount;
    float point_count;

    assert(path != 0);
    if (!path->discrete_mode && path->animated) {
        elapsed = now - path->last_update_tick;
        if (path->last_update_tick < now) {
            if (path->timed) {
                PathAIAdvanceNormalized(path, elapsed * 0.001f);
            } else {
                point_count = static_cast<float>(path->points().size());
                PathAIAdvanceByDistance(path, path->length() / point_count * path->speed * elapsed *
                                                  0.001f);
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
            point_count = static_cast<float>(path->points().size());
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
            path->position = static_cast<int>(path->points().size()) - 1.0f;
            return true;
        }
        path->position = 0.0f;
    }
    path->last_update_tick = now;
    return true;
}

void PathAIPosition(const KeyframeTrack* path, glm::vec3* value)
{
    if (!path || path->points().empty()) return;
    if (!path->animated) {
        int index = path->discrete_mode
                        ? roundedInt32(path->position + 0.1)
                        : roundedInt32((path->points().size() - 1) * path->position + 0.5);
        index = std::min(index, static_cast<int>(path->points().size()) - 1);
        if (index >= 0) *value = path->points()[index].position;
        return;
    }
    const auto index = path->point_index;
    if (index >= path->points().size()) return;
    if (index + 1 < path->points().size()) {
        *value = path->points()[index].position * (1.0f - path->interpolation_fraction) +
                 path->points()[index + 1].position * path->interpolation_fraction;
    } else {
        *value = path->points()[index].position;
    }
}

Transform3D sampleKeyframeTrack(const KeyframeTrack& path, const Transform3D& initial)
{
    auto result = initial;
    glm::vec3 position(result.position);
    PathAIPosition(&path, &position);
    result.position = position;
    if (path.points().empty()) return result;
    int index = path.discrete_mode ? roundedInt32(path.position + 0.1) : path.point_index;
    index = std::clamp(index, 0, static_cast<int>(path.points().size()) - 1);
    const float blend = path.discrete_mode || index + 1 >= static_cast<int>(path.points().size())
                            ? -1
                            : path.interpolation_fraction;
    const auto& point = path.points()[index];
    if (point.rotation) {
        glm::dmat3 rotation(*point.rotation);
        if (blend > 0 && path.points()[index + 1].rotation) {
            const auto first = glm::quat_cast(rotation);
            auto next = glm::dquat(glm::quat_cast(*path.points()[index + 1].rotation));
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
    if (point.scale) result.scale = *point.scale;
    if (path.upright) result.rotateX(1.5707963);
    return result;
}
