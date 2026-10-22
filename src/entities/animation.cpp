#include "wiz8/entities/animation.h"

#include <cassert>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/render/light.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/update.h"

Animation::Animation() = default;
Animation::~Animation() = default;

bool AnimObjReadFromFile(LevelLoadContext* info, Animation* animation,
                         std::vector<std::unique_ptr<Light>>* light_list)
try {
    unsigned char version = 0;
    unsigned char group_count = 0;
    bool success;
    unsigned char discarded[50];
    unsigned char channel_bytes[8];
    wiz8::File* handle;
    int index;

    assert(info != 0 && info->hFile != 0 && animation != 0);
    handle = info->hFile;
    const auto read_flag = [handle](bool& flag) {
        unsigned char value;
        handle->read_exact(&value, 1);
        flag = value != 0;
        return true;
    };
    success = (handle->read(&version, 1).bytes == static_cast<std::size_t>(1));
    success = success && (handle->read(&group_count, 1).bytes == static_cast<std::size_t>(1));
    success = success && read_flag(animation->animation_playing);
    success =
        success && (handle->read(&animation->frame_method, 1).bytes == static_cast<std::size_t>(1));
    success =
        success && (handle->read(&animation->behaviour, 1).bytes == static_cast<std::size_t>(1));
    success = success && (handle->read(&animation->cycle, 1).bytes == static_cast<std::size_t>(1));
    success = success && read_flag(animation->positioned_parts);

    if (version < 3) {
        animation->playback_scale = 15.0f;
    } else {
        success = success && (handle->read(&animation->playback_scale, 4).bytes ==
                              static_cast<std::size_t>(4));
    }
    if (version < 5) {
        animation->start_frame = 0;
    } else {
        success = success &&
                  (handle->read(&animation->start_frame, 1).bytes == static_cast<std::size_t>(1));
    }
    if (version < 11) {
        animation->end_frame = 0;
    } else {
        success = success &&
                  (handle->read(&animation->end_frame, 1).bytes == static_cast<std::size_t>(1));
    }
    if (version < 6) {
        animation->random_play = false;
        animation->play_chance = 1.0f;
    } else {
        success = success && read_flag(animation->random_play);
        success = success &&
                  (handle->read(&animation->play_chance, 4).bytes == static_cast<std::size_t>(4));
    }
    success = success && (handle->read(discarded, sizeof(discarded)).bytes ==
                          static_cast<std::size_t>(sizeof(discarded)));
    if (!success || group_count > animation->parts.size()) {
        return false;
    }
    for (index = 0; index < static_cast<signed char>(group_count); ++index) {
        success = success &&
                  (handle->read(&channel_bytes[index], 1).bytes == static_cast<std::size_t>(1));
    }

    if (version > 6) {
        unsigned char frames;
        handle->read_exact(&frames, 1);
        animation->frame_bounds.resize(frames);
        for (auto& bounds : animation->frame_bounds) {
            wiz8::retail::read_exact(*handle, bounds.minimum);
            wiz8::retail::read_exact(*handle, bounds.maximum);
            bounds.minimum *= static_cast<float>(500.0);
            bounds.maximum *= static_cast<float>(500.0);
            bounds.known = true;
        }
    }

    if (version > 7) {
        unsigned char light_count;
        glm::vec3 color(1.0f, 1.0f, 1.0f);
        handle->read_exact(&light_count, 1);
        for (index = 0; index < static_cast<signed char>(light_count); ++index) {
            unsigned char light_version;
            unsigned char definition_kind = 0;
            unsigned char ignored;
            glm::vec3 position;
            float range;
            float intensity;
            LightAnimator definition;

            handle->read_exact(&light_version, 1);
            wiz8::retail::read_exact(*handle, position);
            wiz8::retail::read_exact(*handle, color);
            handle->read_exact(&intensity, 4);
            handle->read_exact(&range, 4);
            position *= static_cast<float>(500.0);
            if (light_version < 3) {
                definition_kind = light_version == 2 ? 1 : 0;
            } else {
                handle->read_exact(&definition_kind, 1);
            }

            if (definition_kind == 1) {
                auto* typed = &definition.emplace<ParametricLightAnim>();
                handle->read_exact(&typed->flags, 4);
                handle->read_exact(&typed->flicker_chance, 4);
                wiz8::retail::read_exact(*handle, typed->color);
                handle->read_exact(&typed->color_to.x, 4);
                handle->read_exact(&typed->color_to.y, 4);
                handle->read_exact(&typed->color_to.z, 4);
                handle->read_exact(&typed->intensity, 4);
                handle->read_exact(&typed->intensity_to, 4);
                handle->read_exact(&typed->period, 4);
                handle->read_exact(&typed->rate, 4);
                handle->read_exact(&typed->path_speed, 4);
                handle->read_exact(&typed->subcycle_min, 4);
                handle->read_exact(&typed->subcycle_max, 4);
            } else if (definition_kind == 2) {
                auto* typed = &definition.emplace<KeyframedLightAnim>();
                int previous = 0;
                int key;

                handle->read_exact(&ignored, 1);
                handle->read_exact(&typed->start_frame, 4);
                handle->read_exact(&typed->end_frame, 4);
                for (key = 0; key < 6; ++key) {
                    int frame;
                    float value;
                    glm::vec3 vector;

                    handle->read_exact(&frame, 4);
                    handle->read_exact(&value, 4);
                    wiz8::retail::read_exact(*handle, vector);
                    if (frame < previous) {
                        frame = previous + 1;
                    }
                    if (typed->end_frame <= frame) {
                        frame = static_cast<int>(typed->end_frame);
                    }
                    previous = frame;
                    typed->key_frames.push_back(frame);
                    typed->key_intensities.push_back(value);
                    typed->key_colors.push_back(vector);
                    if (static_cast<int>(typed->key_frames.size()) == 1) {
                        typed->frame_sums.push_back(frame);
                    } else {
                        typed->frame_sums.push_back(typed->frame_sums.back() + frame);
                    }
                }
            }

            // Only monster and emitter animations may carry lights.
            if (light_list == 0) {
                return false;
            } else {
                auto light = CreateLight(nullptr, "Light");
                light->diffuse = color;
                light->specular = glm::vec3(0);
                ConfigureWorldLight(light.get(), range * kWorldScale);
                light->intensity = intensity;
                light->transform.setLocation(position.x, position.y, position.z);
                light->m_position = position;
                light->animator = std::move(definition);
                light->setGroupMask(2);
                light_list->push_back(std::move(light));
            }
        }
    }

    if (!animation->positioned_parts && version > 8) {
        unsigned char has_path;
        handle->read_exact(&has_path, 1);
        if (has_path != 0) {
            auto path = LoadPathAI(*handle);
            if (!path) return false;
            path->discrete_mode = true;
            path->animated = false;
            path->speed = animation->playback_scale;
            animation->frame_count = static_cast<unsigned char>(path->nodes.size());
            animation->path = std::move(path);
        }
    }
    if (version > 9) {
        unsigned char ignored;
        success = success && (handle->read(&ignored, 1).bytes == static_cast<std::size_t>(1));
    }

    for (int group = 0; group < group_count; ++group) {
        signed char count = 1;
        if (animation->positioned_parts) handle->read_exact(&count, 1);
        for (int entry = 0; entry < count; ++entry) {
            signed char channel;
            handle->read_exact(&channel, 1);
            if (channel < 0 || channel >= static_cast<int>(animation->parts.size())) return false;
            auto mesh = ReadAnimatedMesh(*handle, info->bitmap_folder);
            if (!mesh) return false;
            AnimationPart part{std::move(*mesh), std::nullopt};
            auto& parts = animation->parts[channel];
            if (animation->positioned_parts) {
                part.path = LoadPathAI(*handle);
                if (!part.path) return false;
                part.path->discrete_mode = true;
                part.path->animated = false;
                part.path->speed = animation->playback_scale;
                animation->frame_count = static_cast<unsigned char>(part.path->nodes.size());
                if (std::cmp_greater(entry, parts.size())) return false;
                parts.insert(parts.begin() + entry, std::move(part));
            } else {
                parts.clear();
                parts.push_back(std::move(part));
            }
        }
    }

    if (animation->end_frame == 0) {
        // Positioned-part records without an end frame keep their start frame.
        // Frame sequences use the highest available LOD's mesh extent.
        animation->end_frame = animation->start_frame;
        if (!animation->positioned_parts) {
            for (signed char lod = 2; lod >= 0; --lod) {
                if (auto* mesh = animation->mesh(lod)) {
                    animation->end_frame = static_cast<unsigned char>(mesh->frame_count - 1);
                    break;
                }
            }
        }
    }
    return success;
} catch (const std::exception&) {
    return false;
}

// Mesh resources are shared; each animation keeps its own part/path values.
Animation::Animation(const Animation& other)
    : animation_playing(other.animation_playing), frame_method(other.frame_method),
      behaviour(other.behaviour), cycle(other.cycle), positioned_parts(other.positioned_parts),
      playback_scale(other.playback_scale), random_play(other.random_play),
      play_chance(other.play_chance), start_frame(other.start_frame), end_frame(other.end_frame),
      frame_count(other.frame_count), path(other.path), parts(other.parts),
      frame_bounds(other.frame_bounds)
{
    for (auto& group : parts)
        for (auto& part : group)
            if (part.path) part.path->timed = false;
    if (path) path->timed = false;
}

/* Cache the frame's mesh bounds: the union of the untransformed part boxes. */
std::optional<BoundingBox> AnimObjGetBounds(Animation* animation, signed char list_index,
                                            unsigned int frame)
{
    if (!animation || list_index < 0 || std::cmp_greater_equal(list_index, animation->parts.size()))
        return std::nullopt;
    frame &= 0xff;
    if (frame < animation->frame_bounds.size() && animation->frame_bounds[frame].known) {
        const auto& bounds = animation->frame_bounds[frame];
        return BoundingBox{bounds.minimum, bounds.maximum};
    }
    if (!animation->positioned_parts) {
        const auto* mesh = animation->mesh(list_index);
        return mesh ? std::optional{mesh->bounds} : std::nullopt;
    }
    if (frame >= animation->frame_count) return std::nullopt;
    if (animation->frame_bounds.size() < animation->frame_count)
        animation->frame_bounds.resize(animation->frame_count);
    const auto& parts = animation->parts[list_index];
    if (parts.empty()) return std::nullopt;
    auto bounds = parts.front().mesh.bounds;
    for (const auto& part : parts) bounds.include(part.mesh.bounds);
    animation->frame_bounds[frame] = {true, bounds.minimum, bounds.maximum};
    return bounds;
}

/* Rotate, translate and scale the eight corners of a box and take the extent of
   the result. The first corner seeds both ends rather than starting from an
   infinite box, which is why the loop carries the is-first test instead of
   pre-filling. */
void TransformBounds(const glm::mat3* rotation, const glm::vec3* translation,
                     const glm::vec3* scale, glm::vec3* minimum, glm::vec3* maximum)
{
    const BoundingBox bounds{(*minimum), (*maximum)};
    const auto transformed = bounds.transformedBy(
        [&](const glm::vec3& point) { return (*rotation * point + (*translation)) * (*scale); });
    *maximum = transformed.maximum;
    *minimum = transformed.minimum;
}

unsigned int Animation::frameCount(signed char lod) const
{
    return positioned_parts ? frame_count : parts[lod].front().mesh.frame_count;
}

KeyframeTrack* Animation::partPath(signed char lod, signed char part)
{
    if (part < 0 || std::cmp_greater_equal(part, parts[lod].size())) return nullptr;
    auto& path = parts[lod][part].path;
    return path ? &*path : nullptr;
}

AnimationFrame Animation::frame(signed char lod, unsigned char value, std::size_t part)
{
    const auto* selected = mesh(lod, part);
    return selected ? GetAniMeshFrame(*selected, positioned_parts ? 0 : value) : AnimationFrame{};
}

AnimationMesh* Animation::mesh(signed char lod, std::size_t part)
{
    return part < parts[lod].size() ? &parts[lod][part].mesh : nullptr;
}
