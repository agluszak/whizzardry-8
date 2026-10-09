#version 450
// SurRender hands the device eye-space positions; only the projection runs here.

layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_diffuse;
layout(location = 2) in vec4 in_specular;
layout(location = 3) in vec3 in_texcoord0;
layout(location = 4) in vec3 in_texcoord1;
layout(location = 5) in float in_fog;

layout(location = 0) out vec4 out_diffuse;
layout(location = 1) out vec4 out_specular;
layout(location = 2) out vec3 out_texcoord0;
layout(location = 3) out vec3 out_texcoord1;
layout(location = 4) out float out_fog;

layout(std140, set = 1, binding = 0) uniform VertexState {
    mat4 projection;
};

void main()
{
    // srMatrix4T stores rows; uploaded as-is GLSL sees the transpose.
    vec4 clip = in_position * projection;
    // OpenGL clip depth [-w, w] to the [0, w] range SDL GPU expects.
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;
    out_diffuse = in_diffuse;
    out_specular = in_specular;
    out_texcoord0 = in_texcoord0;
    out_texcoord1 = in_texcoord1;
    out_fog = in_fog;
}
