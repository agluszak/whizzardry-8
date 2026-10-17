#pragma once
struct W8Chunk;

#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srDebug.h"
#include "surrender/srLight.h"
#include "surrender/srNode.h"

class W8Prop;
struct W8PathAI;
namespace wiz8 {
class File;
}

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
    stLightDefinition(const stLightDefinition&) = default;
    stLightDefinition& operator=(const stLightDefinition&) = default;
    virtual ~stLightDefinition();
    virtual std::unique_ptr<stLightDefinition> Clone() const = 0;
    virtual bool IsEnabledForSubcycle(unsigned char subcycle) = 0;

    W8LightDefinitionKind kind;
};

/* Type 1: a light animated from parameters (flicker chance, colour and
   intensity ranges, period and rate) over a subcycle window. */
// VTABLE: WIZ8 0x005ecdbc
class stParametricLightDefinition : public stLightDefinition {
public:
    stParametricLightDefinition() { kind = W8_LIGHT_DEFINITION_PARAMETRIC; }
    // FUNCTION: WIZ8 0x004A2140
    virtual std::unique_ptr<stLightDefinition> Clone() const override
    {
        return std::make_unique<stParametricLightDefinition>(*this);
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
    /* Per-update flicker probability, compared against Random15()/32768. */
    float flicker_chance;
    glm::vec3 color;
    /* The diffuse color the intensity sweep lerps toward under flag bit 3. */
    glm::vec3 color_to;
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
    virtual std::unique_ptr<stLightDefinition> Clone() const override;
    virtual bool IsEnabledForSubcycle(unsigned char subcycle) override;

    std::vector<int> frame_sums;
    std::vector<int> key_frames;
    std::vector<float> key_intensities;
    std::vector<glm::vec3> key_colors;
    int keyframe_index;
    float time;
    int start_frame;
    float end_frame;
};

// VTABLE: WIZ8 0x005ecc64 stLight
// VTABLE: WIZ8 0x005ecc58 srVertexProcessor
class stLight : public srLight {
    friend class W8GrCycle;
    friend class Trigger;

public:
    virtual ~stLight() override; /* 0x0049C430 */

    explicit stLight(srNode* parent = nullptr); /* 0x0049C2C0 */
    stLight& operator=(const stLight& other); /* 0x0049C690 */

public:
    virtual void traverse(srNode::TraverseInfo& info) override; /* 0x0049C7A0 */
    virtual void process(const srNode::ProcessInfo& info,
                         srNode::e_processType type) override; /* 0x0049C8D0 */
    void Reset();                                              /* 0x0049D070 */
    void SetDefinitionTime(float time);                        /* 0x0049C940 */
    void Update();                                             /* 0x0049C960 */

    float positionalX() const { return m_position.x; }
    float positionalY() const { return m_position.y; }
    float positionalZ() const { return m_position.z; }
    stLightDefinition* definition() const { return m_definition.get(); }
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
    glm::vec3 m_position;    /* 0x228 */
    std::unique_ptr<stLightDefinition> m_definition;
    /* Oscillation direction: zero sweeps intensity down, nonzero sweeps up. */
    unsigned char m_direction;
    /* Raised by light-toggle triggers; the save path serializes the names of
       lights carrying it so their toggled state persists in savegames. */
    bool m_save_marked; /* 0x23a */
    /* w8_get_ticks()/1000 timestamp of the last intensity/color update. */
    float m_level_time;
    /* Current 0..1 sweep level driving intensity and the color lerp. */
    float m_level;
    std::unique_ptr<W8PathAI> path_ai;
    /* Current path entry index, advanced by m_path_direction. */
    int m_path_index;
    /* w8_get_ticks()/1000 timestamp of the last path advance. */
    float m_path_time;
    /* Path step direction, +1 or -1 under the ping-pong flag. */
    int m_path_direction;
    W8Prop* m_prop; /* 0x254 */
};

void SaveLightStates(wiz8::File& handle);
void LoadLightStates(W8Chunk& handle);

/* Animation copies retain per-light position before assignment, detach the new
   light and register it with the world, which owns it, before adding it to the
   returned list. */
