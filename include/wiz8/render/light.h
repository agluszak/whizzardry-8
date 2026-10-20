#pragma once
class WorldScene;
class GpuRenderer;
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "wiz8/entities/keyframe_track.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/transform.h"
#include "wiz8/render/vertex_channels.h"
class Prop;
struct World;
struct W8Chunk;
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

struct ParametricLightAnim {
    unsigned flags = 0;
    float flicker_chance = 0;
    glm::vec3 color{1}, color_to{1};
    float intensity = 1, intensity_to = 1;
    float period = 1, rate = 0, path_speed = 0;
    int subcycle_min = 0, subcycle_max = 255;
    bool IsEnabledForSubcycle(unsigned char subcycle) const
    {
        return subcycle >= subcycle_min && subcycle <= subcycle_max;
    }
    glm::vec3 GetInterpolatedColor(float blend) const;
};
struct KeyframedLightAnim {
    std::vector<int> frame_sums;
    std::vector<int> key_frames;
    std::vector<float> key_intensities;
    std::vector<glm::vec3> key_colors;
    int keyframe_index = 0;
    float time = 0;
    int start_frame = 0;
    float end_frame = 0;
    bool IsEnabledForSubcycle(unsigned char) const
    {
        return !key_frames.empty() && key_frames.front() <= time && time <= key_frames.back();
    }
};
struct MonsterLightAnim {
    float vertical_offset = 0;
    glm::vec3 color_first{1}, color_second{0};
    std::optional<float> start_time;
    bool cycle_color = false;
    bool fade_out = false;
};
using LightAnimator =
    std::variant<std::monostate, ParametricLightAnim, KeyframedLightAnim, MonsterLightAnim>;

class Light {

public:
    enum e_preset { PRESET_DIRECTIONAL = 0, PRESET_POINT = 1, PRESET_SPOT = 2 };

    enum e_enable {
        ENABLE_SPOT = 0,
        ENABLE_DIRECTIONAL = 1,

        ENABLE_BOUNDING_SPHERE = 2,
        ENABLE_RANGE_NEAR = 3,
        ENABLE_RANGE_FAR = 4
    };
    enum e_attenuationModel {
        ATTENUATION_NONE = 0,
        ATTENUATION_OPENGL = 1,
        ATTENUATION_3DSTUDIO_MAX = 2
    };

    Light(WorldScene* scene = 0, e_preset preset = PRESET_POINT);
    ~Light();

    Light(const Light& other);

    // Derives the light's eye-space values for one scene view.
    void beginPass(GpuRenderer& renderer, const DrawTransform& view);
    Light& operator=(const Light& other);

public:
public:
    void setLinearAttenuation(float range, float attenuation);
    glm::vec3 getAmbient() const;
    glm::vec3 getSpotDirection() const;
    void setAmbient(const glm::vec3& color);
    void setIntensity(float intensity);
    void setSpotDirection(const glm::vec3& direction);
    void setAttenuation(const glm::vec3& attenuation);
    glm::vec3 getAttenuation() const;

    e_attenuationModel attenuation_model; /* 0x150 */
    double near_start;                    /* 0x158 */
    double near_end;                      /* 0x160 */
    double far_start;                     /* 0x168 */
    double far_end;                       /* 0x170 */

    float scaled_near_start; /* 0x178 */
    float scaled_far_end;    /* 0x17c */
    float near_attenuation;  /* 0x180 */
    float far_attenuation;   /* 0x184 */

    glm::vec3 opengl_attenuation;    /* 0x188 */
    w8_ulong enable_flags;           /* 0x194 */
    glm::vec3 ambient;               /* 0x198 */
    glm::vec3 diffuse;               /* 0x1a4 */
    glm::vec3 specular;              /* 0x1b0 */
    glm::vec3 spot_direction;        /* 0x1bc */
    float spot_angle;                     /* 0x1c8 */
    float spot_exponent;                  /* 0x1cc */
    float intensity;                      /* 0x1d0 */
    float safe_range;                     /* 0x1d4 */
    glm::vec4 scaled_ambient;     /* 0x1d8: ambient * intensity */
    glm::vec4 scaled_diffuse;     /* 0x1e8: diffuse * intensity */
    glm::vec4 scaled_specular;    /* 0x1f8: specular * intensity */
    glm::vec3 spot_direction_eye; /* 0x208 */
    float spot_cutoff;                    /* 0x214: cos(spot_angle) */
    float attenuation_range;              /* 0x218: scaled far end */
    enum {
        DERIVED_ACTIVE = 0x01u,
        DERIVED_SPOT = 0x02u,
        DERIVED_DIRECTIONAL = 0x04u,
        DERIVED_OPENGL_ATTENUATION = 0x08u,
        DERIVED_CONSTANT_ATTENUATION = 0x10u,
        DERIVED_RANGE_CULL = 0x20u
    };
    w8_ulong derived_flags; /* 0x21c */
    w8_ulong channel_mask;  /* 0x220 */
public:
public:
    void Reset();                                              /* 0x0049D070 */
    void SetDefinitionTime(float time);                        /* 0x0049C940 */
    void Update();                                             /* 0x0049C960 */

    float positionalX() const { return m_position.x; }
    float positionalY() const { return m_position.y; }
    float positionalZ() const { return m_position.z; }
    void ConfigureMonsterCopy()
    {
        attenuation_model = Light::ATTENUATION_3DSTUDIO_MAX;
        enable_flags |= 0x10; /* ENABLE_RANGE_FAR */
        enable_flags |= 4;    /* ENABLE_BOUNDING_SPHERE */
    }

    /* One value, not three floats: 0x0049C690 copies it through the base-pointer
       form VC6 emits for a class type's memberwise assignment, not through three
       independent displacement loads. */
    glm::vec3 m_position;    /* 0x228 */
    LightAnimator animator;
    /* Oscillation direction: zero sweeps intensity down, nonzero sweeps up. */
    unsigned char m_direction;
    /* Raised by light-toggle triggers; the save path serializes the names of
       lights carrying it so their toggled state persists in savegames. */
    bool m_save_marked; /* 0x23a */
    /* w8_get_ticks()/1000 timestamp of the last intensity/color update. */
    float m_level_time;
    /* Current 0..1 sweep level driving intensity and the color lerp. */
    float m_level;
    std::optional<KeyframeTrack> path_ai;
    /* Current path entry index, advanced by m_path_direction. */
    int m_path_index;
    /* w8_get_ticks()/1000 timestamp of the last path advance. */
    float m_path_time;
    /* Path step direction, +1 or -1 under the ping-pong flag. */
    int m_path_direction;
    Prop* m_prop; /* 0x254 */

    Light(WorldScene* scene, bool cycle_color, float range, const glm::vec3* first_color,
          const glm::vec3* second_color);
    void SetVisible(bool visible);
    void SetRange(float range);
    void Update(const glm::vec3* position);
    void StartFadeOut();

public:
    std::string name = "anonymous";
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    bool visible = true;
    bool bake_into_mesh = false;
    Transform3D transform;
    WorldScene* scene = nullptr; // Borrowed typed render membership.
    void setScene(WorldScene* value);
    w8_ulong group_mask = 0x80000000u;
    glm::vec3 eye_location{0};
    void setGroupMask(w8_ulong value) { group_mask = value; }
    w8_ulong getGroupMask() const { return group_mask; }
};
void SaveLightStates(wiz8::File& handle);
struct W8LightState {
    std::string name;
    unsigned char enabled;
};
std::vector<W8LightState> ReadLightStates(W8Chunk& file);
void ApplyLightStates(World& world, std::span<const W8LightState> states);

/* Animation copies retain per-light position before assignment, detach the new
   light and register it with the world, which owns it, before adding it to the
   returned list. */
