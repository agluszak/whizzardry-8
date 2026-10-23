#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <glm/common.hpp>

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/chunk.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/vsg_scene.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

/* The previous 00497af0-004adb20 range spanned eight units that carry their
   own assertion-backed intervals -
   ParticleSystem, OctSubMesh, Item, AnimObj, Missile, GrCycle, PathAI and Spells -
   and every body actually in this file belongs to the single unnamed unit that
   sits between them. The upper bound is OctSubMesh.cpp's interval lower bound
   less one; the lower bound is one past ParticleSystem.cpp's last emission at
   0x0049BA50, since that unit runs past its own assertion interval. No CPP path
   string in the image names this unit; the class ownership is nevertheless
   established by its definitions. */

/* Normalizes a retail 15-bit raw draw to a 0..1 flicker probability;
   only Update uses it. */
const float g_light_flicker_random_unit_scale = 3.0518509447574615e-05f;

/* The parent-taking constructor never forwards the parent to Light: the base
   runs with its own defaults and the node is linked afterwards, which is why
   the emitted body carries an explicit null test around setParent. Everything
   below +0x228 is Light's own state, and both timestamps start from the same
   converted tick. */

/* Assignment deep-copies both owned members - the definition through its
   virtual Clone slot and the path through the PathAI clone - so a copied light
   shares nothing with its source except the prop reference and the parent it
   is re-linked under. The two timestamps restart from the current tick instead
   of being copied. */

void Light::SetDefinitionTime(float time)
{
    if (auto* definition = std::get_if<KeyframedLightAnim>(&animator)) definition->time = time;
}

/* Advance the light's definition-driven state by one update. A keyframed
   (type 2) definition walks keyframe_index through the time table and lerps
   intensity and diffuse color between the surrounding keys; a flags-driven
   definition either oscillates intensity between intensity and
   intensity_to (optionally lerping color toward color_to_*), ramps it
   one way, or flickers the node disable flag and its prop's animated
   texture at a per-update probability. Any owned path advances once the
   elapsed seconds times the path rate exceed one whole step, wrapping or
   ping-ponging at the ends. */
glm::vec3 ParametricLightAnim::GetInterpolatedColor(float blend) const
{
    return glm::min(glm::mix(color, color_to, blend), glm::vec3(1));
}

void Light::Update(float seconds)
{
    if (auto* definition = std::get_if<KeyframedLightAnim>(&animator)) {
        if (definition->key_frames.empty()) return;
        float time = definition->time;
        int count = static_cast<int>(definition->key_frames.size());
        if (time <= definition->key_frames.back()) {
            definition->keyframe_index = 0;
            if (count > 1) {
                do {
                    int next = definition->keyframe_index + 1;
                    if (definition->key_frames[next] <= time) {
                        definition->keyframe_index = next;
                    } else {
                        break;
                    }
                } while (definition->keyframe_index < count - 1);
            }
        } else {
            time = definition->key_frames.back();
            definition->keyframe_index = count - 2;
        }
        if (definition->key_frames.front() <= time) {
            int index = definition->keyframe_index;
            if (count - 2 <= index) {
                return;
            }
            const int from = definition->key_frames[index];
            const int to = definition->key_frames[index + 1];
            float span = to - from;
            float blend = 1.0f;
            if (span != 0.0f) {
                blend = (time - from) / span;
            }
            const float key_from = definition->key_intensities[index];
            const float key_to = definition->key_intensities[index + 1];
            float inverse = 1.0f - blend;
            intensity = inverse * key_from + blend * key_to;
            index = definition->keyframe_index;
            const auto& color_to = definition->key_colors[index + 1];
            const auto& color_from = definition->key_colors[index];
            diffuse = glm::min(glm::mix(color_from, color_to, blend), glm::vec3(1));
            return;
        }
        intensity = 0.0f;
        return;
    }

    if (animation_time == seconds) return;
    animation_time = seconds;
    KeyframeTrack* path = (path_ai ? &*path_ai : nullptr);
    if (m_level_time < 0) m_path_time = m_level_time = seconds;
    auto* definition = std::get_if<ParametricLightAnim>(&animator);
    if (!definition) return;
    if (definition->period < 0.0001f) {
        definition->period = 1.0f;
    }
    unsigned int mode = definition->flags & W8_PARAM_LIGHT_MODE_MASK;
    if (mode == W8_PARAM_LIGHT_OSCILLATE) {
        float step = (seconds - m_level_time) * definition->rate;
        if (0.0f < step) {
            float level = m_level;
            step = (1.0f / definition->period) * step;
            if (m_direction == 0) {
                level -= step;
                if (level < 0.0f) {
                    m_direction = 1;
                    level = step + step + level;
                }
            } else {
                level = step + level;
                if (1.0f < level) {
                    m_direction = 0;
                    level -= step + step;
                }
            }
            float blend = 0.0f;
            if (0.0f < level) {
                blend = level;
                if (1.0f <= level) {
                    blend = 1.0f;
                }
            }
            intensity =
                (definition->intensity_to - definition->intensity) * blend + definition->intensity;
            m_level = blend;
            if ((definition->flags & W8_PARAM_LIGHT_COLOR) == 0) {
                m_level_time = seconds;
            } else {
                diffuse = definition->GetInterpolatedColor(blend);
                m_level_time = seconds;
            }
        }
    } else if (mode == W8_PARAM_LIGHT_RAMP) {
        float step = (seconds - m_level_time) * definition->rate;
        if (0.0f < step) {
            float blend = (1.0f / definition->period) * step + m_level;
            if (blend <= 1.0f) {
                float level = blend;
                intensity = (definition->intensity_to - definition->intensity) * level +
                            definition->intensity;
                m_level = level;
                if ((definition->flags & W8_PARAM_LIGHT_COLOR) != 0) {
                    diffuse = definition->GetInterpolatedColor(blend);
                }
                m_level_time = seconds;
            }
        }
    } else if ((definition->flags & W8_PARAM_LIGHT_FLICKER) == W8_PARAM_LIGHT_FLICKER) {
        Prop* prop = m_prop;
        MeshInstance* instance = 0;
        if (prop != 0) {
            instance = prop->FrameModel(prop->subcycle);
            Mesh* model = static_cast<Mesh*>(instance->getModel());
            if (!MeshHasAnimatedTexture(model)) {
                m_prop = 0;
                instance = 0;
            }
        }
        if ((definition->flags & W8_PARAM_LIGHT_COLOR) != 0) {
            diffuse = definition->color;
        }
        if (visible) {
            visible = false;
            if (instance != 0) {
                SetModelAnimatedTextureFrame(instance, 1);
            }
        } else {
            int roll = PresentationRng().next() & 0x7fff;
            if (roll * g_light_flicker_random_unit_scale < definition->flicker_chance) {
                if (visible) {
                    visible = false;
                    if (instance != 0) {
                        SetModelAnimatedTextureFrame(instance, 1);
                    }
                } else {
                    visible = true;
                    if (instance != 0) {
                        SetModelAnimatedTextureFrame(instance, 0);
                    }
                }
            }
        }
    }
    if ((path == 0) || (path->points().size() == 0) ||
        ((seconds - m_path_time) * definition->path_speed < 1.0f)) {
        return;
    }
    int index = static_cast<int>((seconds - m_path_time) * definition->path_speed);
    index = index * m_path_direction + m_path_index;
    if (index < static_cast<int>(path->points().size())) {
        if (index < 0) {
            m_path_direction = 1;
            index = 0;
        }
    } else if ((definition->flags & W8_PARAM_LIGHT_PING_PONG_PATH) != 0) {
        m_path_direction = -1;
        index = static_cast<int>(path->points().size()) - 2;
    } else {
        index = 0;
    }
    PathAISetValue(path, static_cast<float>(index));
    this->transform = sampleKeyframeTrack(*(path), this->transform);
    m_path_index = index;
    m_path_time = seconds;
}

/* Restart the light-definition driven state when a Monster switches cycles.
   The definition at +0x234 is owned by Light; type two resets its own pair
   of counters, while the other forms restore intensity and an optional
   colour. */
void Light::Reset()
{
    if (auto* keyframed = std::get_if<KeyframedLightAnim>(&animator)) {
        keyframed->time = 0;
        keyframed->keyframe_index = 0;
        m_path_index = 0;
        m_path_direction = 1;
    } else if (auto* parametric = std::get_if<ParametricLightAnim>(&animator)) {
        intensity = parametric->intensity;
        if (parametric->flags & W8_PARAM_LIGHT_COLOR) diffuse = parametric->color;
    }
    m_level = 0;
    animation_time.reset();
    m_path_time = m_level_time = -1;
}

/* A version byte and count followed by each light's 0x80-byte name and its enable flag. */
void WriteLightStates(wiz8::File& handle, std::span<const W8LightState> states)
{
    const unsigned char version = 1;
    const int count = static_cast<int>(states.size());
    handle.write(&version, sizeof(version));
    handle.write(&count, sizeof(count));
    for (const auto& [name, enabled] : states) {
        handle.write_padded(name, 0x80);
        handle.write(&enabled, sizeof(enabled));
    }
}

/* Reload the serialized positional-light states: a version byte and count
   followed by each light's 0x80-byte name and its enable flag, applied to the
   light found by name in the loaded worlds. */
std::vector<W8LightState> ReadLightStates(W8Chunk& file)
{
    unsigned char version;
    int count;
    file.ReadBytes(&version, sizeof(version));
    file.ReadBytes(&count, sizeof(count));
    if (count < 0 || count > file.RemainingBytes() / 129)
        throw std::runtime_error("invalid Light state count");
    std::vector<W8LightState> records;
    records.reserve(count);
    for (int index = 0; index < count; ++index) {
        char name[128];
        unsigned char enabled;
        file.ReadBytes(name, sizeof(name));
        file.ReadBytes(&enabled, sizeof(enabled));
        const auto end = std::find(name, name + sizeof(name), '\0');
        if (end == name + sizeof(name)) throw std::runtime_error("unterminated Light name");
        records.push_back({std::string(name, end), enabled});
    }
    return records;
}

void ApplyLightStates(World& world, std::span<const W8LightState> states)
{
    for (const auto& state : states) {
        if (auto* light = FindWorldLight(world, state.name)) {
            if (state.enabled)
                light->visible = true;
            else
                light->visible = false;
        }
    }
}

/* The registry base's own destructor, emitted out of line here rather than
   inlined the way 0x0049C430 expands it. */

/* Test a point against the six inward-facing planes of one region volume. */
bool W8OctRegionVolume::ContainsPoint(const glm::vec3& point) const
{
    return PointInsideFrustum(point, m_planes);
}

const float g_monster_light_cycle_rate = 0.025f;

/* Monster's fixed light is a regular Light specialization.  Its two colours
   are retained for the optional cycle, while the first colour is also the
   initial renderer colour. Construction needs no gameplay clock; colour cycling
   starts when the light first participates in a live update. */
Light::Light(WorldScene* scene, bool cycle_color, float range, const glm::vec3* first_color,
             const glm::vec3* second_color)
    : Light(scene, PRESET_POINT)
{
    bake_into_mesh = true;
    animator = MonsterLightAnim{
        .color_first = *first_color, .color_second = *second_color, .cycle_color = cycle_color};

    setName("MonFixedLight");
    attenuation_model = Light::ATTENUATION_3DSTUDIO_MAX;
    enable_flags |= (1UL << Light::ENABLE_RANGE_FAR);
    far_end = range;
    near_start = 0.0;
    near_end = 0.0;
    far_start = 0.0;
    setLinearAttenuation(range, 0.0019569471f);
    specular = glm::vec3(0);
    diffuse = (*first_color);
    visible = false;
}

/* A copied monster light preserves the authored light configuration and
   colour-cycle settings. The owning monster attaches it to its target scene
   and the first live update starts its interval. */

void Light::SetRange(float range)
{
    far_start = 0.0;
    far_end = range;
    setLinearAttenuation(range, 0.0019569471f);
}

void Light::SetVisible(bool visible)
{
    this->visible = visible;
}

void Light::Update(const glm::vec3* position, float now)
{
    auto& state = std::get<MonsterLightAnim>(animator);
    if (!state.start_time) state.start_time = now;
    const float elapsed = now - *state.start_time;

    if (state.fade_out) {
        float fade = elapsed * 0.1f;
        if (fade > 1.0f) {
            fade = 1.0f;
        }
        intensity = 1.0f - fade;
    } else if (state.cycle_color) {
        float cycle = elapsed * g_monster_light_cycle_rate;
        double whole = floor(cycle);
        float first_weight =
            static_cast<float>(sin((cycle - whole) * kMotionFullTurnRadians) + 1.0f) * 0.5f;
        diffuse = glm::mix(state.color_second, state.color_first, first_weight);
    }

    glm::dvec3 location;
    location = glm::dvec3(position->x, position->y + state.vertical_offset, position->z);
    this->transform.setLocation(location);
}

void Light::StartFadeOut(float world_seconds)
{
    auto& state = std::get<MonsterLightAnim>(animator);
    state.fade_out = true;
    state.start_time = world_seconds;
}
