#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec4 color;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 diffuse;
void main()
{
    gl_Position = vec4(position.x / 320.0 - 1.0, position.y / 240.0 - 1.0, 0.0, 1.0);
    uv = texcoord;
    diffuse = color;
}
