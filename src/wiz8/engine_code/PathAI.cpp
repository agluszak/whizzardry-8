#include "glm/ext/vector_double3.hpp"
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>

#include "glm/fwd.hpp"
#include "wiz8/filesystem.h"
#include "surrender/srMath.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/float_constants.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/sr_api.h"
#include "surrender/srNode.h"
#include "GrCycle.h"
#include "compat/native.h"

#define PATH_AI_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\PathAI.CPP"

// FUNCTION: WIZ8 0x004a9260
bool PathAIUpdate(W8AIRecord* record, signed char direction)
{
    if (record == 0) {
        return false;
    }
    switch (record->kind) {
    case W8_AI_RECORD_PATH:
        return PathAITick(static_cast<W8PathAI*>(record), direction);
    case W8_AI_RECORD_MISSILE:
        return UpdateMissileAI(static_cast<W8AIMissile*>(record));
    default:
        return false;
    }
}

// FUNCTION: WIZ8 0x004a92a0
bool LoadPathAI(W8PathAI** output, wiz8::File* handle)
try {
    if (!output) {
        return false;
    }
    unsigned char version;
    handle->read_exact(&version, sizeof(version));
    if (version != 0) {
        return false;
    }
    auto path = std::make_unique<W8PathAI>();
    int point_count;
    handle->read_exact(&path->version, 1);
    handle->read_exact(&path->position, 4);
    handle->read_exact(&path->unknown_08, 4);
    handle->read_exact(&point_count, 4);
    if (point_count < 0) {
        return false;
    }
    if (point_count == 0) {
        *output = nullptr;
        return true;
    }
    path->rotations.resize(point_count);
    if (path->version == 2) {
        path->scales.resize(point_count);
    }
    for (int index = 0; index < point_count; ++index) {
        srVector3T<float> point, axis;
        float angle;
        handle->read_exact(&point.x, 4);
        handle->read_exact(&point.y, 4);
        handle->read_exact(&point.z, 4);
        point *= g_double_five_hundred;
        PathAIAddPoint(*path, point);
        handle->read_exact(&angle, 4);
        handle->read_exact(&axis.x, 4);
        handle->read_exact(&axis.y, 4);
        handle->read_exact(&axis.z, 4);
        auto& rotation = path->rotations[index];
        rotation = glm::mat3(1);
        if (angle != g_double_zero) {
            rotation *=
                glm::mat3_cast(glm::angleAxis(float(std::atan2(sin(angle), cos(angle))),
                                              normalizedOrZero(glm::vec3(axis.glmValue()))));
        }
        if (path->version == 2) {
            handle->read_exact(&path->scales[index], sizeof(path->scales[index]));
        }
    }
    *output = path.release();
    return true;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x004a9720
void PathAIResetRecord(W8AIRecord* record)
{
    if (record && record->kind == W8_AI_RECORD_PATH) {
        static_cast<W8PathAI*>(record)->position = 0;
    }
}

// FUNCTION: WIZ8 0x004a91f0
void PathAIApplyToRep(W8AIRecord* record, W8AnimRepBase* representation)
{
    if (record->kind != W8_AI_RECORD_PATH) {
        return;
    }
    W8PathAI* path = static_cast<W8PathAI*>(record);
    if (path == 0 || representation == 0) {
        srAssertFail("pPathAI&&pRep", PATH_AI_CPP, 0x595, 0);
    }
    PathAIPosition(path, &representation->parent_location);
    representation->location = representation->parent_location;
}

std::unique_ptr<W8AIRecord> W8PathAI::Clone() const
{
    auto copy = std::make_unique<W8PathAI>(*this);
    copy->timed = false;
    return copy;
}

// FUNCTION: WIZ8 0x004a9bb0
void PathAIClearOwned(W8PathAI* path)
{
    if (path) {
        path->nodes.clear();
        path->rotations.clear();
        path->scales.clear();
    }
}

// FUNCTION: WIZ8 0x004a9c30
void PathAIAddPoint(W8PathAI& path, const srVector3T<float>& point)
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
        total_length += (path.nodes[index] - path.nodes[index + 1]).Length();
    }
    path.total_length = total_length;
    if (path.position > g_float_zero && total_length > g_float_zero) {
        PathAISetValue(&path, path.distance_travelled / total_length);
    }
}

// FUNCTION: WIZ8 0x004a9ba0
void PathAIEnableTimedMode(W8PathAI* path)
{
    path->animated = true;
    path->timed = true;
}

// FUNCTION: WIZ8 0x004a9c20
void PathAIResetTick(W8PathAI* path)
{
    path->last_update_tick = w8_get_ticks();
}

// FUNCTION: WIZ8 0x004a9e70
float PathAIGetValue(W8PathAI* path)
{
    if (path == 0) {
        return g_float_zero;
    }
    return path->position;
}

// FUNCTION: WIZ8 0x004a9e90
bool PathAINextPoint(W8PathAI* path, srVector3T<float>* point)
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

// FUNCTION: WIZ8 0x004a9ef0
bool PathAIIsComplete(W8PathAI* path)
{
    if (path != 0 && !path->nodes.empty() &&
        (path->point_index < static_cast<unsigned int>(path->nodes.size()) || path->looping)) {
        return false;
    }
    return true;
}

// FUNCTION: WIZ8 0x004a9f60
void PathAISetValue(W8PathAI* path, float value)
{
    if (path == 0) {
        srAssertFail("pPathAI", PATH_AI_CPP, 0x46e, 0);
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
// FUNCTION: WIZ8 0x004a9fe0
void PathAIAdvanceByDistance(W8PathAI* path, float distance)
{
    srVector3T<float> position;
    srVector3T<float>* next;
    srVector3T<float>* current;
    unsigned int index;
    float remaining;
    float segment;

    if (path->total_length <= g_float_zero) {
        return;
    }
    path->distance_travelled += distance;
    path->position = path->distance_travelled / path->total_length;
    if (path->position >= g_float_one) {
        if (path->looping) {
            path->position = g_float_zero;
            path->interpolation_fraction = g_float_zero;
            path->distance_travelled = g_float_zero;
            path->point_index = 0;
            return;
        }
        path->position = g_float_one;
        path->interpolation_fraction = g_float_one;
        path->distance_travelled = path->total_length;
        path->point_index = static_cast<int>(path->nodes.size()) - 1;
        return;
    }
    remaining = distance;
    while (true) {
        PathAIPosition(path, &position);
        index = path->point_index + 1;
        next = &path->nodes[index];
        segment = (position - *next).Length();
        if (remaining < segment) {
            break;
        }
        path->point_index = index;
        remaining -= segment;
        path->interpolation_fraction = g_float_zero;
        if (static_cast<int>(path->nodes.size()) - 1U <= index) {
            path->point_index = static_cast<int>(path->nodes.size()) - 1;
            path->position = g_float_one;
            path->interpolation_fraction = g_float_one;
            return;
        }
    }
    index = path->point_index;
    next = &path->nodes[index + 1];
    current = &path->nodes[index];
    segment = (*current - *next).Length();
    path->interpolation_fraction = (segment * path->interpolation_fraction + remaining) / segment;
}

// FUNCTION: WIZ8 0x004aa160
void PathAIAdvanceNormalized(W8PathAI* path, float amount)
{
    unsigned int index;
    float position;

    position = amount * path->speed / static_cast<int>(path->nodes.size()) + path->position;
    path->position = position;
    if (position < g_float_one) {
        position *= static_cast<int>(path->nodes.size());
        index = static_cast<unsigned int>(position);
        path->point_index = index;
        path->interpolation_fraction = position - index;
        return;
    }
    if (!path->looping) {
        path->position = g_float_one;
        path->interpolation_fraction = g_float_one;
        path->distance_travelled = path->total_length;
        path->point_index = static_cast<int>(path->nodes.size()) - 1;
        return;
    }
    path->position = g_float_zero;
    path->interpolation_fraction = g_float_zero;
    path->distance_travelled = g_float_zero;
    path->point_index = 0;
}

// FUNCTION: WIZ8 0x004aa1f0
bool PathAITick(W8PathAI* path, signed char direction)
{
    uint64_t now;
    uint64_t elapsed;
    float amount;
    float point_count;

    if (path == 0) {
        srAssertFail("pPathAI", PATH_AI_CPP, 0x520, 0);
    }
    now = w8_get_ticks();
    if (!path->discrete_mode && path->animated) {
        elapsed = now - path->last_update_tick;
        if (path->last_update_tick < now) {
            if (path->timed) {
                PathAIAdvanceNormalized(path, elapsed * g_float_one_thousandth);
            } else {
                point_count = static_cast<float>(path->nodes.size());
                PathAIAdvanceByDistance(path, path->total_length / point_count * path->speed *
                                                  elapsed * g_float_one_thousandth);
            }
            path->last_update_tick = now;
            return true;
        }
    } else {
        if (!path->step_by_node) {
            amount =
                (now - path->last_update_tick) * g_float_one_thousandth * direction * path->speed;
        } else {
            amount = g_negative_one;
            if (direction > 0) {
                amount = g_float_one;
            }
        }
        path->position += amount;
        if (path->position >= g_float_zero) {
            point_count = static_cast<float>(path->nodes.size());
            if (path->position < point_count) {
                path->last_update_tick = now;
                return true;
            }
            if (!path->looping) {
                path->last_update_tick = now;
                path->position = point_count - g_float_one;
                return true;
            }
        } else if (path->looping) {
            path->last_update_tick = now;
            path->position = static_cast<int>(path->nodes.size()) - g_float_one;
            return true;
        }
        path->position = g_float_zero;
    }
    path->last_update_tick = now;
    return true;
}

// FUNCTION: WIZ8 0x004aa370
void PathAIPosition(W8PathAI* path, srVector3T<float>* value)
{
    if (!path || path->nodes.empty()) {
        return;
    }
    if (!path->animated) {
        int index = path->discrete_mode
                        ? srFloatToInt(path->position + g_double_one_tenth)
                        : srFloatToInt((path->nodes.size() - 1) * path->position + g_double_half);
        index = std::min(index, static_cast<int>(path->nodes.size()) - 1);
        if (index >= 0) {
            *value = path->nodes[index];
        }
        return;
    }
    const auto index = path->point_index;
    if (index >= path->nodes.size()) {
        return;
    }
    if (index + 1 < path->nodes.size()) {
        *value = path->nodes[index] * (g_float_one - path->interpolation_fraction) +
                 path->nodes[index + 1] * path->interpolation_fraction;
    } else {
        *value = path->nodes[index];
    }
}

// GLOBAL: WIZ8 0x005EC1E8
const double g_quaternion_matrix_normalization = 2.0;

/* FLT_EPSILON: the dot-product closeness bound at which the keyframe slerps
   fall back to a linear blend. */
// GLOBAL: WIZ8 0x005EC1F0
const double g_slerp_epsilon = 1.1920928955078125e-07;

// FUNCTION: WIZ8 0x004aa520
void PathAIApply(W8PathAI* path, srNode* target)
{
    int index;
    float blend;
    srNode* node;
    srVector3T<float>* scale_vector;
    srVector3T<float> position;
    glm::dvec3 location;
    glm::mat3 rotation;
    glm::mat3 next;

    if (target == 0) {
        return;
    }
    PathAIPosition(path, &position);

    node = target->first_child_;
    if (node == 0) {
        location = glm::dvec3(position.glmValue());
        target->setLocation(location);
    } else {
        do {
            location = glm::dvec3(position.glmValue());
            node->setLocation(location);
            node = node->next_sibling_;
        } while (node != 0);
    }

    if (0 < static_cast<int>(path->nodes.size())) {
        if (!path->discrete_mode) {
            blend = path->interpolation_fraction;
            index = path->point_index;
        } else {
            index = srFloatToInt(path->position + g_double_one_tenth);
            blend = -1.0f;
        }
        if (static_cast<int>(path->nodes.size()) - 1 <= index) {
            index = static_cast<int>(path->nodes.size()) - 1;
            blend = -1.0f;
        }

        rotation = path->rotations[index];
        if (g_float_zero < blend) {
            next = path->rotations[index + 1];
            if (!(rotation == next)) {
                const auto first_rotation = glm::dquat(glm::quat_cast(rotation));
                auto next_rotation = glm::dquat(glm::quat_cast(next));
                double rotation_dot = glm::dot(first_rotation, next_rotation);
                if (rotation_dot < g_double_zero) {
                    next_rotation = -next_rotation;
                    rotation_dot = -rotation_dot;
                }
                const auto interpolated_rotation =
                    g_double_one - rotation_dot <= g_slerp_epsilon
                        ? first_rotation * (g_double_one - blend) + next_rotation * double(blend)
                        : glm::slerp(first_rotation, next_rotation, double(blend));
                rotation = glm::mat3_cast(glm::normalize(interpolated_rotation));
            }
        }

        node = target->first_child_;
        if (node == 0) {
            target->setRotation(rotation);
        } else {
            do {
                node->setRotation(rotation);
                node = node->next_sibling_;
            } while (node != 0);
        }

        if (!path->scales.empty()) {
            scale_vector = &path->scales[index];
            node = target->first_child_;
            if (node == 0) {
                location = glm::dvec3(scale_vector->glmValue());
                target->setScale(location);
            } else {
                do {
                    location = glm::dvec3(scale_vector->glmValue());
                    node->setScale(location);
                    node = node->next_sibling_;
                } while (node != 0);
            }
        }

        if (path->upright) {
            target->rotateX(1.5707963);
        }
    }
}
