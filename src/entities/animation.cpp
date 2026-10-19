#include "wiz8/entities/animation.h"

#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/checked_integer.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/update.h"

Animation::Animation() = default;
Animation::~Animation() = default;

bool AnimObjReadFromFile(LevelLoadContext* info, Animation* animation, int load_all,
                         std::vector<std::unique_ptr<Light>>* light_list, int)
try {
    unsigned char version = 0;
    bool success;
    unsigned char discarded[50];
    unsigned char channel_bytes[8];
    wiz8::File* handle;
    int index;

    if (info == 0 || info->hFile == 0 || animation == 0) {
        Fail("pInfo && pInfo->hFile && pao");
    }
    handle = info->hFile;
    const auto read_flag = [handle](bool& flag) {
        unsigned char value;
        handle->read_exact(&value, 1);
        flag = value != 0;
        return true;
    };
    success = (handle->read(&version, 1).bytes == static_cast<std::size_t>(1));
    success =
        success && (handle->read(&animation->group_count, 1).bytes == static_cast<std::size_t>(1));
    success = success && read_flag(animation->animation_playing);
    success =
        success && (handle->read(&animation->frame_method, 1).bytes == static_cast<std::size_t>(1));
    success =
        success && (handle->read(&animation->behaviour, 1).bytes == static_cast<std::size_t>(1));
    success = success && (handle->read(&animation->cycle, 1).bytes == static_cast<std::size_t>(1));
    success = success && read_flag(animation->path_lists);

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
    if (!success || animation->group_count > std::size(animation->entries)) {
        return false;
    }
    for (index = 0; index < static_cast<signed char>(animation->group_count); ++index) {
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

            if (light_list == 0) {
                Fail("0", "AnimObjReadFromFile: Where is the light list?");
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

    if (!animation->path_lists && version > 8) {
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

    if (!animation->path_lists) {
        int mesh_index;
        for (mesh_index = 0; mesh_index < static_cast<signed char>(animation->group_count);
             ++mesh_index) {
            signed char channel = 0;

            success = success && (handle->read(&channel, 1).bytes == static_cast<std::size_t>(1));
            if (!success || channel < 0 ||
                channel >= static_cast<int>(std::size(animation->entries))) {
                return false;
            }
            auto mesh = ReadAnimatedMesh(*handle, info->bitmap_folder, load_all);
            if (!mesh) return false;
            animation->entries[channel] = std::move(mesh);
        }
    } else {
        int group;
        for (group = 0; group < static_cast<signed char>(animation->group_count); ++group) {
            signed char entry_count;
            int entry;
            success = (handle->read(&entry_count, 1).bytes == static_cast<std::size_t>(1));
            if (!success) {
                Fail("fSuccess");
            }
            for (entry = 0; entry < entry_count; ++entry) {
                signed char channel;

                if (handle->read(&channel, 1).bytes != 1) {
                    return false;
                }
                if (channel < 0 || channel >= static_cast<int>(std::size(animation->entries))) {
                    return false;
                }
                auto mesh = ReadAnimatedMesh(*handle, info->bitmap_folder, true);
                if (!mesh) return false;
                animation->meshes[channel].insert(animation->meshes[channel].begin() + entry,
                                                  std::move(mesh));
                auto path = LoadPathAI(*handle);
                if (!path) return false;
                path->discrete_mode = true;
                path->animated = false;
                path->speed = animation->playback_scale;
                animation->frame_count = static_cast<unsigned char>(path->nodes.size());
                animation->paths[channel].insert(animation->paths[channel].begin() + entry,
                                                 std::move(path));
            }
        }
    }

    if (animation->end_frame == 0) {
        AnimationMesh* mesh = animation->entries[2].get();
        if (mesh == 0) {
            mesh = animation->entries[1].get();
        }
        if (mesh == 0) {
            mesh = animation->entries[0].get();
        }
        if (mesh == 0) {
            animation->end_frame = animation->start_frame;
        } else if (!animation->path_lists) {
            animation->end_frame = static_cast<unsigned char>(AniMeshValue(mesh) - 1);
        } else {
            animation->end_frame = animation->frame_count - 1;
        }
    }
    return success;
} catch (const std::exception&) {
    return false;
}

/* Clone owned meshes and paths; bounds copy as ordinary values. */
Animation::Animation(const Animation& other)
    : group_count(other.group_count), animation_playing(other.animation_playing),
      frame_method(other.frame_method), behaviour(other.behaviour), cycle(other.cycle),
      path_lists(other.path_lists), playback_scale(other.playback_scale),
      random_play(other.random_play), play_chance(other.play_chance),
      start_frame(other.start_frame), end_frame(other.end_frame), frame_count(other.frame_count),
      frame_bounds(other.frame_bounds)
{
    for (int group = 0; group < 3; ++group) {
        if (other.entries[group])
            entries[group] = std::make_unique<AnimationMesh>(*other.entries[group]);
        meshes[group].reserve(other.meshes[group].size());
        for (const auto& mesh : other.meshes[group]) {
            meshes[group].push_back(mesh ? std::make_unique<AnimationMesh>(*mesh) : nullptr);
        }
        paths[group].reserve(other.paths[group].size());
        for (const auto& source : other.paths[group]) {
            auto copy = source;
            if (copy) copy->timed = false;
            paths[group].push_back(std::move(copy));
        }
    }
    if (other.path) {
        path = std::optional<KeyframeTrack>(std::in_place, *other.path);
        path->timed = false;
    }
}

/* Cache the frame's mesh bounds. Retail applies each path but merges the
   untransformed mesh boxes; the discarded transformed box is not part of the result. */
std::optional<BoundingBox> AnimObjGetBounds(Animation* animation, signed char list_index,
                                            unsigned int frame)
{
    if (!animation || list_index < 0 ||
        std::cmp_greater_equal(list_index, std::size(animation->entries)))
        return std::nullopt;
    frame &= 0xff;
    if (frame < animation->frame_bounds.size() && animation->frame_bounds[frame].known) {
        const auto& bounds = animation->frame_bounds[frame];
        return BoundingBox{bounds.minimum, bounds.maximum};
    }
    if (!animation->path_lists) return GetAniMeshBounds(animation->entries[list_index].get());
    if (frame >= animation->frame_count) return std::nullopt;
    if (animation->frame_bounds.size() < animation->frame_count)
        animation->frame_bounds.resize(animation->frame_count);
    auto* first_mesh = AnimObjEntry(animation, list_index, 0);
    if (!first_mesh) return std::nullopt;
    auto bounds = GetAniMeshBounds(first_mesh);
    if (!bounds) return std::nullopt;
    const auto count = animation->meshes[list_index].size();
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) {
            const auto mesh_bounds = GetAniMeshBounds(AnimObjEntry(animation, list_index, index));
            if (!mesh_bounds) return std::nullopt;
            bounds->include(*mesh_bounds);
        }
        auto* instance =
            AnimObjDispatchList(animation, list_index, static_cast<signed char>(index));
        if (!instance) return std::nullopt;
        if (auto* path = AnimObjListEntry(animation, list_index, static_cast<signed char>(index))) {
            const float saved = PathAIGetValue(path);
            PathAISetValue(path, static_cast<float>(index));
            (instance)->transform = sampleKeyframeTrack(*(path), (instance)->transform);
            PathAISetValue(path, saved);
        }
    }
    animation->frame_bounds[frame] = {true, bounds->minimum, bounds->maximum};
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

/* While the object is inactive the selected owned entry supplies the value;
   once active, AnimObj keeps the live value in its own byte at 0x16. */
unsigned int AnimObjValue(const Animation* animation, signed char index)
{
    if (animation == 0) {
        Fail("pao");
    }
    if (!animation->path_lists) {
        return AniMeshValue(animation->entries[index].get());
    }
    return animation->frame_count;
}

unsigned int AnimObjListCount(Animation* animation, signed char index)
{
    return wiz8::checked_integer<unsigned int>(animation->meshes[index].size());
}

KeyframeTrack* AnimObjListEntry(Animation* animation, signed char list_index,
                                signed char entry_index)
{
    if (animation == 0) {
        Fail("pao");
    }
    if (!animation->path_lists) {
        return 0;
    }
    auto& paths = animation->paths[list_index];
    if (entry_index < 0 || std::cmp_greater_equal(entry_index, paths.size())) {
        return nullptr;
    }
    return paths[entry_index] ? &*paths[entry_index] : nullptr;
}

MeshInstance* AnimObjDispatch(Animation* animation, signed char list_index, unsigned char value)
{
    AnimationMesh* entry;

    if (animation == 0) {
        Fail("pao");
    }
    if (!animation->path_lists) {
        entry = animation->entries[list_index].get();
        if (entry != nullptr) {
            return GetAniMeshFrame(entry, value);
        }
    } else {
        entry = AnimObjEntry(animation, list_index, 0);
        if (entry != 0) {
            return GetAniMeshFrame(entry, value);
        }
    }
    return 0;
}

MeshInstance* AnimObjDispatchList(Animation* animation, signed char list_index,
                                  signed char entry_index)
{
    AnimationMesh* entry;

    if (animation == 0) {
        Fail("pao");
    }
    if (animation->path_lists) {
        const auto& list = animation->meshes[list_index];
        if (entry_index >= 0 && std::cmp_less(entry_index, list.size())) {
            entry = list[entry_index].get();
            if (entry != 0) {
                return GetAniMeshFrame(entry, 0);
            }
        }
    }
    return 0;
}

AnimationMesh* AnimObjEntry(Animation* animation, signed char list_index, std::size_t entry_index)
{
    if (animation == 0) {
        Fail("pao");
    }
    if (!animation->path_lists) {
        return animation->entries[list_index].get();
    }
    const auto& meshes = animation->meshes[list_index];
    const auto frame = entry_index & 0xff;
    if (frame >= meshes.size()) {
        return nullptr;
    }
    return meshes[frame].get();
}

/* Type-two light definitions own four synchronized keyframe vectors.  The
   clone iterates the second vector's count, which is the retail authority for
   the shared extent, then copies the four scalar playback  */
