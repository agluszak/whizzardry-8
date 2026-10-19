#version 450
// Fixed-function combiner for srShader's gradient, texturing, fog and alpha-test fields.

layout(location = 0) in vec4 in_diffuse;
layout(location = 1) in vec4 in_specular;
layout(location = 2) in vec3 in_texcoord0;
layout(location = 3) in vec3 in_texcoord1;
layout(location = 4) in float in_fog;

layout(location = 0) out vec4 out_color;

layout(set = 2, binding = 0) uniform sampler2D texture0;
layout(set = 2, binding = 1) uniform sampler2D texture1;

layout(std140, set = 3, binding = 0) uniform FragmentState {
    vec4 fog_color;
    // x texturing, y gradient, z secondary gradient, w fog mode
    ivec4 mode;
    // x alpha test enable, y alpha reference
    vec4 alpha;
};

const int GRADIENT_DISABLE = 0;
const int GRADIENT_MODULATE = 1;
const int GRADIENT_ADD = 2;
const int FOG_ENABLE = 1;
const int FOG_SCALE_FRAGMENT = 2;
const int FOG_WHITE = 3;

void main()
{
    vec4 color = in_diffuse;
    if (mode.x != 0) {
        vec4 texel = texture(texture0, in_texcoord0.xy / in_texcoord0.z);
        if (mode.y == GRADIENT_DISABLE) {
            color = texel;
        } else if (mode.y == GRADIENT_MODULATE) {
            color = texel * in_diffuse;
        } else {
            color = vec4(texel.rgb + in_diffuse.rgb, texel.a * in_diffuse.a);
        }
    }
    if (mode.z != 0) {
        color.rgb += in_specular.rgb;
    }
    if (mode.w == FOG_ENABLE) {
        color.rgb = mix(color.rgb, fog_color.rgb, in_fog);
    } else if (mode.w == FOG_WHITE) {
        color.rgb = mix(color.rgb, vec3(1.0), in_fog);
    } else if (mode.w == FOG_SCALE_FRAGMENT) {
        color.rgb *= 1.0 - in_fog;
    }
    if (alpha.x != 0.0 && color.a <= alpha.y) {
        discard;
    }
    out_color = clamp(color, 0.0, 1.0);
}
