#version 450
#extension GL_GOOGLE_include_directive : require

#include "wizardry_layout.glsl"
#include "world_blocks.glsl"

layout(set = 3, binding = 1) uniform sampler2D material_texture;

vec4 wzDraw(int index)
{
    if (index == 2) return batch.modes;
    if (index == 3) return view.fog_color;
    if (index == 4) return batch.alpha_test;
    if (index == 5) return batch.probe;
    return vec4(0.0);
}

#include "wizardry_fragment.glsl"

layout(location = 0) in vec4 v_diffuse;
layout(location = 1) in vec4 v_specular;
layout(location = 2) in vec2 v_st0;
layout(location = 0) out vec4 out_color;

void main()
{
    if (wzProbing()) {
        out_color = clamp(v_diffuse, 0.0, 1.0);
        return;
    }
    vec4 color = wzCombine(texture(material_texture, v_st0), v_diffuse, v_specular);
    if (wzAlphaTest() && color.a <= 0.0) discard;
    out_color = color;
}
