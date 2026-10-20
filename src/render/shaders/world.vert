#version 450
#extension GL_GOOGLE_include_directive : require

#include "world_blocks.glsl"

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 model_view;
} pc;
layout(std140, set = 1, binding = 0) uniform Instance {
    vec4 ambient;
    vec4 bounds;
    vec4 overrides;
    uvec4 masks;
} instance;
layout(std430, set = 2, binding = 0) readonly buffer Pass {
    vec4 values[];
} pass;

vec3 eye_center;
float eye_radius;

vec4 wzShading(int index)
{
    if (index == 0) return instance.ambient;
    if (index == 1) return view.values[0];
    if (index == 2) return view.values[2];
    if (index < WZ_FOG_BASE) return view.values[index];
    if (index < WZ_MATERIAL_BASE) {
        vec4 fog = view.values[WZ_FOG_BASE + (index - WZ_FOG_BASE) * 2];
        float limit = eye_radius + fog.w;
        return vec4(fog.xyz, dot(eye_center, eye_center) <= limit * limit ? 1.0 : 0.0);
    }
    int slot = index - WZ_MATERIAL_BASE;
    vec4 value = pass.values[1 + slot];
    int field = slot % WZ_MATERIAL_VEC4S;
    if (field == 0 && instance.overrides.x != 0.0) value.w *= instance.overrides.y;
    if (field == 3 && instance.overrides.z != 0.0) value = vec4(vec3(instance.overrides.w), 1.0);
    return value;
}

vec4 wzDraw(int index)
{
    if (index == 0) return vec4(batch.channels.x, pass.values[0].xy, 0.0);
    if (index == 2) return batch.modes;
    if (index == 3) return view.values[1];
    if (index == 4) return batch.alpha_test;
    return vec4(0.0);
}

bool wzLightActive(int light)
{
    int base = WZ_LIGHT_BASE + light * WZ_LIGHT_VEC4S;
    if ((floatBitsToUint(view.values[base + 7].x) & instance.masks.x) != 0u) return false;
    vec4 location = view.values[base];
    int flags = int(location.w);
    vec3 offset = eye_center - location.xyz;
    float distance_squared = dot(offset, offset);
    if ((flags & 0x20) != 0) {
        float reach = view.values[base + 5].z + eye_radius;
        if (reach * reach <= distance_squared) return false;
    }
    if ((flags & 0x02) != 0 && eye_radius * eye_radius < distance_squared) {
        vec4 spot = view.values[base + 4];
        float distance = sqrt(distance_squared);
        if (dot(offset, spot.xyz) / distance +
                eye_radius / sqrt(eye_radius * eye_radius + distance_squared) < spot.w)
            return false;
    }
    return true;
}

bool wzFogActive(int layer)
{
    int base = WZ_FOG_BASE + layer * 2;
    if ((floatBitsToUint(view.values[base + 1].x) & instance.masks.x) != 0u) return false;
    vec4 fog = view.values[base];
    if (fog.z <= 0.0) return false;
    float start = fog.x;
    return !(eye_radius <= start &&
             dot(eye_center, eye_center) < (start - eye_radius) * (start - eye_radius));
}

#include "wizardry_vertex.glsl"

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_texcoords;
layout(location = 3) in vec4 a_color;
layout(location = 4) in vec4 a_diffuse_multiplier;
layout(location = 5) in vec4 a_specular_multiplier;
layout(location = 6) in vec4 a_metadata;
layout(location = 0) out vec4 v_diffuse;
layout(location = 1) out vec4 v_specular;
layout(location = 2) out vec2 v_st0;
layout(location = 3) out vec3 v_eye;
layout(location = 4) out vec2 v_st1;

void main()
{
    mat3 basis = mat3(pc.model_view);
    float scale = max(length(basis[0]), max(length(basis[1]), length(basis[2])));
    for (int axis = 0; axis < 3; ++axis) {
        float length = length(basis[axis]);
        if (length != 0.0) basis[axis] /= length;
    }
    mat3 normal_matrix = transpose(inverse(basis));
    if (instance.masks.y != 0u) normal_matrix = -normal_matrix;
    eye_center = (pc.model_view * vec4(instance.bounds.xyz, 1.0)).xyz;
    eye_radius = instance.bounds.w * scale;
    WzVertex shaded = wzShadeVertex(pc.model_view, normal_matrix, a_position, a_normal,
                                     a_metadata, a_texcoords, a_color,
                                     a_diffuse_multiplier, a_specular_multiplier);
    gl_Position = pc.projection * shaded.eye;
    v_diffuse = shaded.diffuse;
    v_specular = shaded.specular;
    v_st0 = shaded.st0;
    v_eye = shaded.eye.xyz;
    v_st1 = shaded.st1;
}
