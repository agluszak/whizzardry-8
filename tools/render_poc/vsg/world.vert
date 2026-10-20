#version 450
#extension GL_GOOGLE_include_directive : require
// VulkanSceneGraph host for the shared vertex stage. VSG pushes the projection
// and the model-view matrix its traversal accumulated through the scene graph.

#include "wizardry_layout.glsl"
#include "world_blocks.glsl"

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 model_view;
} pc;
layout(std140, set = 1, binding = 0) uniform Instance {
    vec4 ambient;
    vec4 frames;
    vec4 bounds;
    vec4 overrides;
    uvec4 masks;
} instance;
layout(std140, set = 2, binding = 0) uniform Pass {
    vec4 materials[WZ_MAX_MATERIALS * WZ_MATERIAL_VEC4S];
    vec4 mesh;
} pass;
layout(set = 2, binding = 1) uniform sampler2D geometry;

// The instance's bounding sphere in eye space, for the per-object tests.
vec3 eye_center;
float eye_radius;

vec4 wzShading(int index)
{
    if (index == 0) return instance.ambient;
    if (index < WZ_FOG_BASE) return view.values[index];
    if (index < WZ_MATERIAL_BASE) {
        // Within the fog's reach the density ramps; beyond it, it is constant.
        vec4 fog = view.values[index];
        float limit = eye_radius + fog.w;
        return vec4(fog.xyz, dot(eye_center, eye_center) <= limit * limit ? 1.0 : 0.0);
    }
    int slot = index - WZ_MATERIAL_BASE;
    vec4 value = pass.materials[slot];
    // The instance overrides, applied to its material records as RenderMeshes does.
    int field = slot % WZ_MATERIAL_VEC4S;
    if (field == 0 && instance.overrides.x != 0.0) value.w *= instance.overrides.y;
    if (field == 3 && instance.overrides.z != 0.0) value = vec4(vec3(instance.overrides.w), 1.0);
    return value;
}

vec4 wzDraw(int index)
{
    if (index == 0) return vec4(batch.channels.x, pass.mesh.w, pass.mesh.z, pass.mesh.x);
    if (index == 1) return vec4(instance.frames.xyz, pass.mesh.y);
    if (index == 2) return batch.modes;
    if (index == 3) return view.fog_color;
    if (index == 4) return batch.alpha_test;
    return batch.probe;
}

vec4 wzGeometry(int texel)
{
    return texelFetch(geometry, ivec2(texel % WZ_GEOMETRY_WIDTH, texel / WZ_GEOMETRY_WIDTH), 0);
}

// shadeVertices' rejections before its vertex loop: group exclusion, the range
// of a 3ds Max light and the cone of a spot light.
bool wzLightActive(int light)
{
    if ((view.masks[light].x & instance.masks.x) != 0u) return false;
    int base = WZ_LIGHT_BASE + light * WZ_LIGHT_VEC4S;
    vec4 location = view.values[base];
    int flags = int(location.w);
    vec3 offset = eye_center - location.xyz;
    float distance_squared = dot(offset, offset);
    if ((flags & 0x20) != 0) { // DERIVED_RANGE_CULL
        float reach = view.values[base + 5].z + eye_radius;
        if (reach * reach <= distance_squared) return false;
    }
    if ((flags & 0x02) != 0 && eye_radius * eye_radius < distance_squared) { // DERIVED_SPOT
        vec4 spot = view.values[base + 4];
        float distance = sqrt(distance_squared);
        if (dot(offset, spot.xyz) / distance +
                eye_radius / sqrt(eye_radius * eye_radius + distance_squared) <
            spot.w)
            return false;
    }
    return true;
}

bool wzFogActive(int layer)
{
    if ((view.masks[WZ_MAX_LIGHTS + layer].x & instance.masks.x) != 0u) return false;
    vec4 fog = view.values[WZ_FOG_BASE + layer];
    if (fog.z <= 0.0) return false;
    float start = fog.x;
    return !(eye_radius <= start &&
             dot(eye_center, eye_center) < (start - eye_radius) * (start - eye_radius));
}

#include "wizardry_vertex.glsl"

layout(location = 0) in vec4 a_index;
layout(location = 1) in vec4 a_texcoords;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_diffuse_multiplier;
layout(location = 4) in vec4 a_specular_multiplier;
layout(location = 0) out vec4 v_diffuse;
layout(location = 1) out vec4 v_specular;
layout(location = 2) out vec2 v_st0;

void main()
{
    // GpuRenderer's normal matrix: the model-view basis without scaling.
    mat3 basis = mat3(pc.model_view);
    float scale = max(length(basis[0]), max(length(basis[1]), length(basis[2])));
    basis[0] = normalize(basis[0]);
    basis[1] = normalize(basis[1]);
    basis[2] = normalize(basis[2]);
    eye_center = (pc.model_view * vec4(instance.bounds.xyz, 1.0)).xyz;
    eye_radius = instance.bounds.w * scale;
    WzVertex shaded = wzShadeVertex(pc.model_view, transpose(inverse(basis)), a_index, a_texcoords,
                                    a_color, a_diffuse_multiplier, a_specular_multiplier);
    gl_PointSize = 1.0;
    if (wzProbing()) {
        gl_Position = wzProbePosition(gl_VertexIndex);
        v_diffuse = wzProbeValue(shaded);
        v_specular = vec4(0.0);
        v_st0 = vec2(0.0);
        return;
    }
    gl_Position = pc.projection * shaded.eye;
    v_diffuse = shaded.diffuse;
    v_specular = shaded.specular;
    v_st0 = shaded.st0;
}
