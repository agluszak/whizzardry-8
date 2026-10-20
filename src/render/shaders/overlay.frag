#version 450
layout(constant_id = 0) const uint shader = 0u;
layout(set = 0, binding = 0) uniform sampler2D image;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 diffuse;
layout(location = 0) out vec4 color;
void main()
{
    color = texture(image, uv) * diffuse;
    if ((shader & 0x800000u) != 0u && color.a <= 0.0) discard;
    color = clamp(color, 0.0, 1.0);
}
