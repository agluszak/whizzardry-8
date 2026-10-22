#version 450
#extension GL_GOOGLE_include_directive : require

#include "world_blocks.glsl"

layout(set = 3, binding = 1) uniform sampler2D material_texture;
layout(set = 3, binding = 2) uniform sampler2D detail_texture;

vec4 wzDraw(int index)
{
    if (index == 2) return batch.modes;
    if (index == 3) return view.values[1];
    if (index == 4) return batch.alpha_test;
    return vec4(0.0);
}

#include "wizardry_fragment.glsl"

layout(location = 0) in vec4 v_diffuse;
layout(location = 1) in vec4 v_specular;
layout(location = 2) in vec2 v_st0;
layout(location = 3) in vec3 v_eye;
layout(location = 4) in vec2 v_st1;
layout(location = 0) out vec4 out_color;

void main()
{
    int clip_base = int(view.values[2].w) + int(view.values[2].y) * 2;
    for (int plane = 0; plane < int(view.values[2].z); ++plane)
        if (dot(view.values[clip_base + plane], vec4(v_eye, 1.0)) < 0.0) discard;
    vec4 color = wzCombine(texture(material_texture, v_st0), v_diffuse, v_specular);
    if (wzAlphaTest() && color.a <= 0.0) discard;
    out_color = color;
}
