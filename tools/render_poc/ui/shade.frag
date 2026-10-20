#version 450
// SDL GPU renderer custom state: ShadowRect. Sampler 0 is a copy of the canvas
// region; sampler 1 is ShadeTable as a 256x256 texture indexed by the packed
// ARGB1555 value, so pixels without the alpha bit stay transparent black.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_canvas;
layout(set = 2, binding = 1) uniform sampler2D u_shade;

void main()
{
    ivec4 value = ivec4(round(texture(u_canvas, v_uv) * 255.0));
    int packed = (value.a > 127 ? 0x8000 : 0) | ((value.r >> 3) << 10) | ((value.g >> 3) << 5) |
                 (value.b >> 3);
    o_color = texelFetch(u_shade, ivec2(packed & 255, packed >> 8), 0);
}
