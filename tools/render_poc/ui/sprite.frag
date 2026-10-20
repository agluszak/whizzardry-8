#version 450
// SDL GPU renderer custom state: an STI sprite frame drawn with DrawSprite's
// rules. Sampler 0 is the frame texture SDL binds (R index, G 1 unless the
// texel is the transparent-run sentinel); sampler 1 is the 256-entry palette.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_frame;
layout(set = 2, binding = 1) uniform sampler2D u_palette;
layout(std140, set = 3, binding = 0) uniform SpriteState {
    vec4 foreground;
    vec4 background;
    vec4 shadow;
    ivec4 mode; // x mono, y background set, z shadow set
} state;

void main()
{
    vec4 texel = texture(u_frame, v_uv);
    int index = int(round(texel.r * 255.0));
    bool run = texel.g > 0.5; // false for the transparent-run sentinel
    if (state.mode.x != 0) {
        if (run && index == 1) {
            if (state.mode.z == 0) discard;
            o_color = state.shadow;
        } else if (run && index != 0) {
            o_color = state.foreground;
        } else {
            if (state.mode.y == 0) discard;
            o_color = state.background;
        }
        return;
    }
    // The sentinel leaves the canvas untouched; every palette index writes,
    // including colours whose 1555 alpha bit is clear.
    if (!run) discard;
    o_color = texelFetch(u_palette, ivec2(index, 0), 0);
}
