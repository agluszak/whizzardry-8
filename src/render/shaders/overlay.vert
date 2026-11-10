#version 450
// The quad's transform places a unit square in the 640x480 design space; the
// overlay camera's orthographic projection maps that space to the target.
layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 model_view;
} pc;
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec4 color;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 diffuse;
void main()
{
    gl_Position = pc.projection * pc.model_view * vec4(position, 1.0);
    uv = texcoord;
    diffuse = color;
}
