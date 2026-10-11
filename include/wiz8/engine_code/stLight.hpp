#pragma once

#include <vector>

#include "surrender/srLight.h"
#include "wiz8/sr_api.h"
#include "wiz8/layouts/world.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "surrender/srTypeRegistry.h"

class W8Prop;
struct W8PathAI;
namespace wiz8 { class File; }

extern unsigned int g_light_update_flags;

enum W8ParametricLightFlag {
    W8_PARAM_LIGHT_MODE_MASK = 3,
    W8_PARAM_LIGHT_OSCILLATE = 0,
    W8_PARAM_LIGHT_FLICKER = 1,
    W8_PARAM_LIGHT_RAMP = 3,
    W8_PARAM_LIGHT_COLOR = 8,
    W8_PARAM_LIGHT_HAS_PATH = 0x10,
    W8_PARAM_LIGHT_PING_PONG_PATH = 0x20
};

enum W8LightDefinitionKind {
    W8_LIGHT_DEFINITION_NONE = 0,
    W8_LIGHT_DEFINITION_PARAMETRIC = 1,
    W8_LIGHT_DEFINITION_KEYFRAMED = 2
};

class stLightDefinition {
public:
    stLightDefinition() : kind(W8_LIGHT_DEFINITION_NONE) {}
    virtual ~stLightDefinition();
    virtual stLightDefinition* Clone() const = 0;
    virtual bool IsEnabledForSubcycle(unsigned char subcycle) = 0;

    W8LightDefinitionKind kind;
};

/* Type 1: a light animated from parameters (flicker chance, colour and
   intensity ranges, period and rate) over a subcycle window. */
// VTABLE: WIZ8 0x005ecdbc
class stParametricLightDefinition : public stLightDefinition {
public:
    stParametricLightDefinition()
    {
        kind = W8_LIGHT_DEFINITION_PARAMETRIC;
    }
    // FUNCTION: WIZ8 0x004A2140
    virtual stLightDefinition* Clone() const override
    {
        stParametricLightDefinition* copy = new stParametricLightDefinition;
        if (copy == 0) {
            srAssertFail("pNew", "..\\Engine Code\\Include\\stLight.hpp", 0x56, 0);
        }
        copy->flags = flags;
        copy->flicker_chance = flicker_chance;
        copy->color = color;
        copy->color_to = color_to;
        copy->intensity = intensity;
        copy->intensity_to = intensity_to;
        copy->period = period;
        copy->rate = rate;
        copy->path_speed = path_speed;
        copy->subcycle_min = subcycle_min;
        copy->subcycle_max = subcycle_max;
        return copy;
    }

    // FUNCTION: WIZ8 0x004A21E0
    virtual bool IsEnabledForSubcycle(unsigned char subcycle) override
    {
        if (subcycle >= subcycle_min && subcycle <= subcycle_max) {
            return true;
        }
        return false;
    }

    glm::vec3 GetInterpolatedColor(float blend) const;

    /* flags bits 0-1 select the update mode (0 oscillating intensity, 1
       flicker, 3 one-way ramp); bit 3 lerps diffuse toward the target color,
       bit 5 ping-pongs the path direction at the ends. */
    unsigned int flags;
    /* Per-update flicker probability, compared against rand()/32768. */
    float flicker_chance;
    srVector3T<float> color;
    /* The diffuse color the intensity sweep lerps toward under flag bit 3. */
    srVector3T<float> color_to;
    float intensity;
    /* The intensity the sweep lerps toward. */
    float intensity_to;
    /* Sweep period divisor; Update clamps it up to 1.0 when below ~0.0001. */
    float period;
    /* Elapsed-time multiplier applied to the sweep step. */
    float rate;
    float path_speed;
    int subcycle_min;
    int subcycle_max;
};

/* Type 2: a light driven by keyframe tables stepped by keyframe_index. */
// VTABLE: WIZ8 0x005ecda0
class stKeyframedLightDefinition : public stLightDefinition {
public:
    stKeyframedLightDefinition() : keyframe_index(0), time(0.0f)
    {
        kind = W8_LIGHT_DEFINITION_KEYFRAMED;
    }
    virtual stLightDefinition* Clone() const override;
    virtual bool IsEnabledForSubcycle(unsigned char subcycle) override;

    std::vector<int> frame_sums;
    std::vector<int> key_frames;
    std::vector<float> key_intensities;
    std::vector<srVector3T<float>> key_colors;
    int keyframe_index;
    float time;
    int start_frame;
    float end_frame;
};

// VTABLE: WIZ8 0x005ecc64 stLight
// VTABLE: WIZ8 0x005ecc58 srVertexProcessor
class stLight : public srClassSupport<stLight, srLight, 0x10006> {
    friend class W8GrCycle;
    friend class Trigger;

public:
    virtual ~stLight() override; /* 0x0049C430 */
    static const char* sGetClassName()
    {
        return "stLight";
    }

    stLight() {}
    explicit stLight(srNode* parent);         /* 0x0049C2C0 */
    stLight& operator=(const stLight& other); /* 0x0049C690 */

public:
    virtual void traverse(srNode::TraverseInfo& info) override; /* 0x0049C7A0 */
    virtual void process(const srNode::ProcessInfo& info,
                         srNode::e_processType type) override; /* 0x0049C8D0 */
    void Reset();                                              /* 0x0049D070 */
    void SetDefinitionTime(float time);                        /* 0x0049C940 */
    void Update();                                             /* 0x0049C960 */

    float positionalX() const
    {
        return m_position.x;
    }
    float positionalY() const
    {
        return m_position.y;
    }
    float positionalZ() const
    {
        return m_position.z;
    }
    stLightDefinition* definition() const
    {
        return m_definition;
    }
    void ConfigureMonsterCopy()
    {
        attenuation_model = srLight::ATTENUATION_3DSTUDIO_MAX;
        enable_flags |= 0x10; /* ENABLE_RANGE_FAR */
        enable_flags |= 4;    /* ENABLE_BOUNDING_SPHERE */
    }

public:
    /* One value, not three floats: 0x0049C690 copies it through the base-pointer
       form VC6 emits for a class type's memberwise assignment, not through three
       independent displacement loads. */
    srVector3T<float> m_position;    /* 0x228 */
    stLightDefinition* m_definition; /* 0x234: owned */
    unsigned char m_unknown_238;     /* 0x238 */
    /* Oscillation direction: zero sweeps intensity down, nonzero sweeps up. */
    unsigned char m_direction;
    /* Raised by light-toggle triggers; the save path serializes the names of
       lights carrying it so their toggled state persists in savegames. */
    bool m_save_marked; /* 0x23a */
    unsigned char m_padding_23b;
    /* w8_get_ticks()/1000 timestamp of the last intensity/color update. */
    float m_level_time;
    /* Current 0..1 sweep level driving intensity and the color lerp. */
    float m_level;
    W8PathAI* path_ai; /* 0x244 */
    /* Current path entry index, advanced by m_path_direction. */
    int m_path_index;
    /* w8_get_ticks()/1000 timestamp of the last path advance. */
    float m_path_time;
    /* Path step direction, +1 or -1 under the ping-pong flag. */
    int m_path_direction;
    W8Prop* m_prop; /* 0x254 */
};

void SaveLightStates(wiz8::File& handle);
void LoadLightStates(wiz8::File& handle);

/* Animation copies retain per-light position before assignment, detach the new
   light and register it with the world before adding it to the owned list. */
inline std::vector<stLight*>* CloneAnimationLightList(std::vector<stLight*>* source,
                                                      const char* file, int list_line,
                                                      int light_line)
{
    std::vector<stLight*>* copy = 0;
    if (source != 0) {
        copy = new std::vector<stLight*>;
        if (copy == 0) {
            srAssertFail("plsNewLights", file, list_line,
                         "Out of memory creating monster light list");
        }
        for (int index = 0; index < static_cast<int>(source->size()); ++index) {
            stLight* light = (*source)[index];
            float x = light->positionalX();
            float y = light->positionalY();
            float z = light->positionalZ();
            stLight* copied_light = new stLight;
            if (copied_light != 0) {
                *copied_light = *light;
            }
            if (copied_light == 0) {
                srAssertFail("pstNewLight", file, light_line,
                             "Out of memory creating monster light");
            }
            copied_light->ConfigureMonsterCopy();
            copied_light->setLocation(x, y, z);
            copied_light->setParent(0, 0);
            g_world->transient_lights.push_back(copied_light);
            copy->push_back(copied_light);
        }
    }
    return copy;
}
