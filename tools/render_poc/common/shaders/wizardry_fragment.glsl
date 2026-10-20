// The DrawShader combiner of src/render/shaders/sr_fragment.glsl: texturing,
// gradient, secondary gradient and fog. `specular.a` carries the fog factor,
// as retail's specular alpha did. The host samples `texel` and applies the
// alpha test (discard when wzAlphaTest() is set and alpha <= 0).

const int WZ_GRADIENT_DISABLE = 0;
const int WZ_GRADIENT_MODULATE = 1;
const int WZ_FOG_ENABLE = 1;
const int WZ_FOG_SCALE_FRAGMENT = 2;
const int WZ_FOG_WHITE = 3;

bool wzAlphaTest()
{
    return wzDraw(4).x != 0.0;
}

vec4 wzCombine(vec4 texel, vec4 diffuse, vec4 specular)
{
    ivec4 mode = ivec4(wzDraw(2)); // texturing, gradient, secondary gradient, fog mode
    vec4 color = diffuse;
    if (mode.x != 0) {
        if (mode.y == WZ_GRADIENT_DISABLE)
            color = texel;
        else if (mode.y == WZ_GRADIENT_MODULATE)
            color = texel * diffuse;
        else
            color = vec4(texel.rgb + diffuse.rgb, texel.a * diffuse.a);
    }
    if (mode.z != 0) color.rgb += specular.rgb;
    float fog = specular.a;
    if (mode.w == WZ_FOG_ENABLE)
        color.rgb = mix(color.rgb, wzDraw(3).rgb, fog);
    else if (mode.w == WZ_FOG_WHITE)
        color.rgb = mix(color.rgb, vec3(1.0), fog);
    else if (mode.w == WZ_FOG_SCALE_FRAGMENT)
        color.rgb *= 1.0 - fog;
    return clamp(color, 0.0, 1.0);
}

bool wzProbing()
{
    return wzDraw(5).x != 0.0;
}
