#include "wiz8/engine_code/AnimObj.h"

#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/retail_text_records.h"

// FUNCTION: WIZ8 0x004a01a0
W8AnimObj::W8AnimObj() = default;
W8AnimObj::~W8AnimObj() = default;

// FUNCTION: WIZ8 0x004a05c0
bool AnimObjReadFromFile(W8ReadLevelInfo* info, W8AnimObj* animation, int load_all,
                         std::vector<std::unique_ptr<stLight>>* light_list, int)
try {
    unsigned char version = 0;
    bool success;
    unsigned char discarded[50];
    unsigned char channel_bytes[8];
    wiz8::File* handle;
    int index;

    if (info == 0 || info->hFile == 0 || animation == 0) {
        srAssertFail("pInfo && pInfo->hFile && pao");
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
            std::unique_ptr<stLightDefinition> definition;

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
                auto typed = std::make_unique<stParametricLightDefinition>();
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
                definition = std::move(typed);
            } else if (definition_kind == 2) {
                auto typed = std::make_unique<stKeyframedLightDefinition>();
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
                definition = std::move(typed);
            }

            if (light_list == 0) {
                srAssertFail("0", "AnimObjReadFromFile: Where is the light list?");
            } else {
                auto light = CreateLight(nullptr, "MonsterLight");
                light->diffuse = color;
                light->specular = glm::vec3(0);
                ConfigureWorldLight(light.get(), range * kWorldScale);
                light->intensity = intensity;
                light->setLocation(position.x, position.y, position.z);
                light->m_position = position;
                light->m_definition = std::move(definition);
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
                srAssertFail("fSuccess");
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
        W8AniMesh* mesh = animation->entries[2].get();
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
// FUNCTION: WIZ8 0x004a0320
W8AnimObj::W8AnimObj(const W8AnimObj& other)
    : group_count(other.group_count), animation_playing(other.animation_playing),
      frame_method(other.frame_method), behaviour(other.behaviour), cycle(other.cycle),
      path_lists(other.path_lists), playback_scale(other.playback_scale),
      random_play(other.random_play), play_chance(other.play_chance),
      start_frame(other.start_frame), end_frame(other.end_frame), frame_count(other.frame_count),
      frame_bounds(other.frame_bounds)
{
    for (int group = 0; group < 3; ++group) {
        if (other.entries[group])
            entries[group] = std::make_unique<W8AniMesh>(*other.entries[group]);
        meshes[group].reserve(other.meshes[group].size());
        for (const auto& mesh : other.meshes[group]) {
            meshes[group].push_back(mesh ? std::make_unique<W8AniMesh>(*mesh) : nullptr);
        }
        paths[group].reserve(other.paths[group].size());
        for (const auto& source : other.paths[group]) {
            auto copy = source ? std::make_unique<W8PathAI>(*source) : nullptr;
            if (copy) copy->timed = false;
            paths[group].push_back(std::move(copy));
        }
    }
    if (other.path) {
        path = std::make_unique<W8PathAI>(*other.path);
        path->timed = false;
    }
}

/* Cache the frame's mesh bounds. Retail applies each path but merges the
   untransformed mesh boxes; the discarded transformed box is not part of the result. */
// FUNCTION: WIZ8 0x004a1710
std::optional<srBoundingBox> AnimObjGetBounds(W8AnimObj* animation, signed char list_index,
                                              unsigned int frame)
{
    if (!animation || list_index < 0 ||
        std::cmp_greater_equal(list_index, std::size(animation->entries)))
        return std::nullopt;
    frame &= 0xff;
    if (frame < animation->frame_bounds.size() && animation->frame_bounds[frame].known) {
        const auto& bounds = animation->frame_bounds[frame];
        return srBoundingBox{bounds.minimum, bounds.maximum};
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
            PathAIApply(path, static_cast<stModelInstance*>(instance));
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
// FUNCTION: WIZ8 0x004a1df0
void TransformBounds(const glm::mat3* rotation, const glm::vec3* translation,
                     const glm::vec3* scale, glm::vec3* minimum, glm::vec3* maximum)
{
    const srBoundingBox bounds{(*minimum), (*maximum)};
    const auto transformed = bounds.transformedBy(
        [&](const glm::vec3& point) { return (*rotation * point + (*translation)) * (*scale); });
    *maximum = transformed.maximum;
    *minimum = transformed.minimum;
}

// FUNCTION: WIZ8 0x004a01e0

/* While the object is inactive the selected owned entry supplies the value;
   once active, AnimObj keeps the live value in its own byte at 0x16. */
// FUNCTION: WIZ8 0x004a15d0
unsigned int AnimObjValue(const W8AnimObj* animation, signed char index)
{
    if (animation == 0) {
        srAssertFail("pao");
    }
    if (!animation->path_lists) {
        return AniMeshValue(animation->entries[index].get());
    }
    return animation->frame_count;
}

// FUNCTION: WIZ8 0x004a1620
unsigned int AnimObjListCount(W8AnimObj* animation, signed char index)
{
    return animation->meshes[index].size();
}

// FUNCTION: WIZ8 0x004a16c0
W8PathAI* AnimObjListEntry(W8AnimObj* animation, signed char list_index, signed char entry_index)
{
    if (animation == 0) {
        srAssertFail("pao");
    }
    if (!animation->path_lists) {
        return 0;
    }
    const auto& paths = animation->paths[list_index];
    if (entry_index < 0 || std::cmp_greater_equal(entry_index, paths.size())) {
        return nullptr;
    }
    return paths[entry_index].get();
}

// FUNCTION: WIZ8 0x004a14d0
srModelInstance* AnimObjDispatch(W8AnimObj* animation, signed char list_index, unsigned char value)
{
    W8AniMesh* entry;

    if (animation == 0) {
        srAssertFail("pao");
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

// FUNCTION: WIZ8 0x004a1560
srModelInstance* AnimObjDispatchList(W8AnimObj* animation, signed char list_index,
                                     signed char entry_index)
{
    W8AniMesh* entry;

    if (animation == 0) {
        srAssertFail("pao");
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

// FUNCTION: WIZ8 0x004a1660
W8AniMesh* AnimObjEntry(W8AnimObj* animation, signed char list_index, unsigned int entry_index)
{
    if (animation == 0) {
        srAssertFail("pao");
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

// FUNCTION: WIZ8 0x004A2220
stLightDefinition::~stLightDefinition() = default;

/* Type-two light definitions own four synchronized keyframe vectors.  The
   clone iterates the second vector's count, which is the retail authority for
   the shared extent, then copies the four scalar playback  */
// FUNCTION: WIZ8 0x004a2230
std::unique_ptr<stLightDefinition> stKeyframedLightDefinition::Clone() const
{
    return std::make_unique<stKeyframedLightDefinition>(*this);
}

// FUNCTION: WIZ8 0x004a2580
bool stKeyframedLightDefinition::IsEnabledForSubcycle(unsigned char subcycle)
{
    if (key_frames[0] <= time && time <= key_frames.back()) {
        return true;
    }
    return false;
}
