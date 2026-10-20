#version 450
#extension GL_GOOGLE_include_directive : require
// VulkanSceneGraph host for the shared vertex stage. VSG pushes the projection
// and model-view matrices; the shading and draw blocks are uniform buffers.

#include "wizardry_layout.glsl"

layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 model_view;
} pc;
layout(std140, set = 0, binding = 0) uniform Shading { vec4 values[WZ_SHADING_VEC4S]; } shading;
layout(set = 0, binding = 1) uniform sampler2D geometry;
layout(std140, set = 1, binding = 0) uniform Draw { vec4 values[WZ_DRAW_VEC4S]; } draw;

vec4 wzShading(int index) { return shading.values[index]; }
vec4 wzDraw(int index) { return draw.values[index]; }
vec4 wzGeometry(int texel)
{
    return texelFetch(geometry, ivec2(texel % WZ_GEOMETRY_WIDTH, texel / WZ_GEOMETRY_WIDTH), 0);
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
    basis[0] = normalize(basis[0]);
    basis[1] = normalize(basis[1]);
    basis[2] = normalize(basis[2]);
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
