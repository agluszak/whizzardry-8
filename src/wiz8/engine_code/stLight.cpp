#include "wiz8/engine_code/stLight.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/common.hpp"
#include "glm/ext/vector_float3.hpp"
#include "random.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/octree_spatial.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/world.h"

/* The previous 00497af0-004adb20 range spanned eight units that carry their
   own assertion-backed intervals -
   stParticle, OctSubMesh, Item, AnimObj, Missile, GrCycle, PathAI and Spells -
   and every body actually in this file belongs to the single unnamed unit that
   sits between them. The upper bound is OctSubMesh.cpp's interval lower bound
   less one; the lower bound is one past stParticle.cpp's last emission at
   0x0049BA50, since that unit runs past its own assertion interval. No CPP path
   string in the image names this unit; the class ownership is nevertheless
   established by its definitions. */

// GLOBAL: WIZ8 0x0060bfdc
unsigned int g_light_update_flags = 1;

/* Normalizes a retail 15-bit Random15() roll to a 0..1 flicker probability;
   only Update uses it. */
// GLOBAL: WIZ8 0x005ec1e4
const float g_light_flicker_random_unit_scale = 3.0518509447574615e-05f;

/* The parent-taking constructor never forwards the parent to srLight: the base
   runs with its own defaults and the node is linked afterwards, which is why
   the emitted body carries an explicit null test around setParent. Everything
   below +0x228 is stLight's own state, and both timestamps start from the same
   converted tick. */
// FUNCTION: WIZ8 0x0049C2C0
stLight::stLight(srNode* parent) : srLight(static_cast<srNode*>(nullptr), srLight::PRESET_POINT)
{
    m_save_marked = false;
    m_prop = 0;
    if (parent != 0) {
        setParent(parent, 0);
    }
    attenuation_model = srLight::ATTENUATION_3DSTUDIO_MAX;
    m_direction = 1;
    m_path_index = 0;
    m_path_direction = 1;
    m_position = glm::vec3(0);
    m_level = 0;
    m_level_time = m_path_time = w8_get_ticks() * 0.001f;
}

stLight::~stLight() = default;

/* Assignment deep-copies both owned members - the definition through its
   virtual Clone slot and the path through the PathAI clone - so a copied light
   shares nothing with its source except the prop reference and the parent it
   is re-linked under. The two timestamps restart from the current tick instead
   of being copied. */
// FUNCTION: WIZ8 0x0049C690
stLight& stLight::operator=(const stLight& other)
{
    if (this == &other) return *this;
    srLight::operator=(other);
    setParent(other.parent_, 0);
    m_position = other.m_position;
    if (other.m_definition != 0) {
        m_definition = other.m_definition->Clone();
    } else {
        m_definition.reset();
    }
    m_direction = other.m_direction;
    if (other.path_ai != 0) {
        path_ai = std::make_unique<W8PathAI>(*other.path_ai);
        path_ai->timed = false;
    } else {
        path_ai.reset();
    }
    m_path_index = other.m_path_index;
    m_path_direction = other.m_path_direction;
    m_level = other.m_level;
    m_level_time = m_path_time = w8_get_ticks() * 0.001f;
    m_prop = other.m_prop;
    m_save_marked = other.m_save_marked;
    return *this;
}

// FUNCTION: WIZ8 0x0049C7A0
void stLight::traverse(srNode::TraverseInfo& info)
{
    traverseEffect(info, [&] {
        if (fabs(intensity) <= 0.0001 || (g_light_update_flags & 1) == 0)
            return EffectTraversal::childrenOnly;
        return m_definition ? EffectTraversal::active : EffectTraversal::skipSubtree;
    });
}

// FUNCTION: WIZ8 0x0049C8D0
void stLight::process(const srNode::ProcessInfo& info, srNode::e_processType type)
{
    if ((type == PROCESS_PUSH || type == PROCESS_PUSH_GLOBAL) && g_monster_light_scale != 1.0f) {
        float saved_scale = intensity;
        intensity = saved_scale * g_monster_light_scale;
        srLight::process(info, type);
        intensity = saved_scale;
        return;
    }
    srLight::process(info, type);
}

// FUNCTION: WIZ8 0x0049C940
void stLight::SetDefinitionTime(float time)
{
    if (m_definition != 0 && m_definition->kind == W8_LIGHT_DEFINITION_KEYFRAMED) {
        static_cast<stKeyframedLightDefinition*>(m_definition.get())->time = time;
    }
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
glm::vec3 stParametricLightDefinition::GetInterpolatedColor(float blend) const
{
    return glm::min(glm::mix(color, color_to, blend), glm::vec3(1));
}

// FUNCTION: WIZ8 0x0049C960
void stLight::Update()
{
    if (m_definition != 0 && m_definition->kind == W8_LIGHT_DEFINITION_KEYFRAMED) {
        stKeyframedLightDefinition* definition =
            static_cast<stKeyframedLightDefinition*>(m_definition.get());
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

    w8_ulong ticks = w8_get_ticks();
    W8PathAI* path = path_ai.get();
    float seconds = ticks * 0.001f;
    stParametricLightDefinition* definition =
        static_cast<stParametricLightDefinition*>(m_definition.get());
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
        W8Prop* prop = m_prop;
        srModelInstance* instance = 0;
        if (prop != 0) {
            instance = prop->ToggleRepAnimationDefault();
            srMeshModel* model = static_cast<srMeshModel*>(instance->getModel());
            if (!MeshHasAnimatedTexture(model)) {
                m_prop = 0;
                instance = 0;
            }
        }
        if ((definition->flags & W8_PARAM_LIGHT_COLOR) != 0) {
            diffuse = definition->color;
        }
        if (testFlag(FLAG_DISABLE) == 0) {
            setFlag(FLAG_DISABLE);
            if (instance != 0) {
                SetModelAnimatedTextureFrame(instance, 1);
            }
        } else {
            int roll = Random15() & 0x7fff;
            if (roll * g_light_flicker_random_unit_scale < definition->flicker_chance) {
                if (testFlag(FLAG_DISABLE) == 0) {
                    setFlag(FLAG_DISABLE);
                    if (instance != 0) {
                        SetModelAnimatedTextureFrame(instance, 1);
                    }
                } else {
                    clearFlag(FLAG_DISABLE);
                    if (instance != 0) {
                        SetModelAnimatedTextureFrame(instance, 0);
                    }
                }
            }
        }
    }
    if ((path == 0) || (path->nodes.size() == 0) ||
        ((seconds - m_path_time) * definition->path_speed < 1.0f)) {
        return;
    }
    int index = static_cast<int>((seconds - m_path_time) * definition->path_speed);
    index = index * m_path_direction + m_path_index;
    if (index < static_cast<int>(path->nodes.size())) {
        if (index < 0) {
            m_path_direction = 1;
            index = 0;
        }
    } else if ((definition->flags & W8_PARAM_LIGHT_PING_PONG_PATH) != 0) {
        m_path_direction = -1;
        index = static_cast<int>(path->nodes.size()) - 2;
    } else {
        index = 0;
    }
    PathAISetValue(path, static_cast<float>(index));
    PathAIApply(path, this);
    m_path_index = index;
    m_path_time = seconds;
}

/* Restart the light-definition driven state when a Monster switches cycles.
   The definition at +0x234 is owned by stLight; type two resets its own pair
   of counters, while the other forms restore intensity and an optional
   colour. */
// FUNCTION: WIZ8 0x0049D070
void stLight::Reset()
{
    if (m_definition != 0) {
        if (m_definition->kind == W8_LIGHT_DEFINITION_KEYFRAMED) {
            stKeyframedLightDefinition* definition =
                static_cast<stKeyframedLightDefinition*>(m_definition.get());
            definition->time = 0.0f;
            definition->keyframe_index = 0;
            m_path_index = 0;
            m_path_direction = 1;
        } else {
            stParametricLightDefinition* definition =
                static_cast<stParametricLightDefinition*>(m_definition.get());
            intensity = definition->intensity;
            if ((definition->flags & W8_PARAM_LIGHT_COLOR) != 0) {
                diffuse = definition->color;
            }
        }
    }

    m_level = 0;
    m_path_time = w8_get_ticks() * 0.001f;
    m_level_time = m_path_time;
}

/* Serialize the trigger-toggled lights of the loaded worlds: a version byte and
   count followed by each light's 0x80-byte name and its enable flag. */
// FUNCTION: WIZ8 0x0049D120
void SaveLightStates(wiz8::File& handle)
{
    unsigned char version = 1;

    handle.write(&version, sizeof(version));

    std::vector<stLight*> marked;
    ForEachWorldNode<stLight>([&](stLight* light) {
        if (light->m_save_marked) {
            marked.push_back(light);
        }
    });
    int count = static_cast<int>(marked.size());
    handle.write(&count, sizeof(count));

    for (stLight* light : marked) {
        handle.write_padded(light->getName(), 0x80);
        unsigned char enabled = light->testFlag(srNode::FLAG_DISABLE) == 0;
        handle.write(&enabled, sizeof(enabled));
    }
}

/* Reload the serialized positional-light states: a version byte and count
   followed by each light's 0x80-byte name and its enable flag, applied to the
   light found by name in the loaded worlds. */
// FUNCTION: WIZ8 0x0049D390
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

void ApplyLightStates(std::span<const W8LightState> states)
{
    for (const auto& state : states) {
        if (auto* light = FindWorldNode<stLight>(state.name)) {
            if (state.enabled)
                light->clearFlag(srNode::FLAG_DISABLE);
            else
                light->setFlag(srNode::FLAG_DISABLE);
        }
    }
}

/* The registry base's own destructor, emitted out of line here rather than
   inlined the way 0x0049C430 expands it. */

/* Test a point against the six inward-facing planes of one region volume. */
// FUNCTION: WIZ8 0x0049e460
bool W8OctRegionVolume::ContainsPoint(const glm::vec3& point) const
{
    return PointInsideFrustum(point, m_planes);
}
