// GLSL port of shadeVertices (src/render/pipeline/vertex_shading.cpp) for one
// vertex of one pass, plus frame interpolation from resident frame data.
//
// The host defines, before this source:
//   vec4 wzShading(int index);  // ShadingBlock (shading_block.h)
//   vec4 wzDraw(int index);     // DrawBlock
//   vec4 wzGeometry(int texel); // ResidentGeometry texels
// The statement order follows the C++ so that accumulation rounds alike.

const int WZ_DIFFUSE = 1 << 1;
const int WZ_SPECULAR = 1 << 2;
const int WZ_ALPHA = 1 << 3;
const int WZ_FOG = 1 << 4;
const int WZ_ST0 = 1 << 5;
const int WZ_ST1 = 1 << 6;
const int WZ_LIGHT_AMBIENT = 1 << 9;
const int WZ_LIGHT_DIFFUSE = 1 << 10;

const int WZ_HAS_COLORS = 0x01;
const int WZ_HAS_DIFFUSE_MULTIPLIERS = 0x02;
const int WZ_HAS_SPECULAR_MULTIPLIERS = 0x04;
const int WZ_HAS_ALPHA = 0x08;
const int WZ_HAS_TEXCOORD0 = 0x10;
const int WZ_HAS_TEXCOORD1 = 0x20;

const int WZ_OPER_DEPTH_CUE = 1 << 0;
const int WZ_OPER_ALPHA = 1 << 1;
const int WZ_OPER_SPECULAR = 1 << 2;
const int WZ_OPER_DIFFUSE = 1 << 3;
const int WZ_OPER_DIFFUSE_SPECULAR = 1 << 4;

const int WZ_DERIVED_SPOT = 0x02;
const int WZ_DERIVED_DIRECTIONAL = 0x04;
const int WZ_DERIVED_OPENGL = 0x08;
const int WZ_DERIVED_CONSTANT = 0x10;

struct WzVertex {
    vec4 eye;
    vec4 diffuse;  // vec4(1) when the batch writes no diffuse or alpha
    vec4 specular; // rgb secondary colour, a fog factor
    vec2 st0;
    vec2 st1;
};

float wzRamp(float distance, float origin, float normalization, float amplitude, bool decreasing)
{
    float value = decreasing ? origin - distance : distance - origin;
    return clamp(value * normalization, 0.0, 1.0) * amplitude;
}

float wzDepthCue(float distance)
{
    vec4 environment = wzShading(1); // minimum, maximum, scale, inverse scale
    float near_value = 1.0 - environment.z;
    float far_value = 1.0 - environment.w;
    if (environment.x == environment.y)
        return distance < environment.x ? near_value : far_value;
    return wzRamp(distance, environment.y, 1.0 / (environment.y - environment.x),
                  near_value - far_value, true) +
           (1.0 - near_value);
}

float wzAttenuation(int base, int flags, vec3 direction, float distance)
{
    float attenuation = 1.0;
    int model = (flags >> 8) & 3;
    if (model == 2) {
        vec4 range = wzShading(base + 5);
        if ((flags & (1 << 6)) != 0)
            attenuation = wzRamp(distance, range.x, range.y, 1.0, false);
        if ((flags & (1 << 7)) != 0)
            attenuation *= wzRamp(distance, range.z, range.w, 1.0, true);
    } else if (model == 1 && (flags & WZ_DERIVED_OPENGL) != 0) {
        vec3 c = wzShading(base + 6).xyz;
        float denominator =
            (flags & WZ_DERIVED_CONSTANT) != 0 ? c.x : (c.z * distance + c.y) * distance + c.x;
        attenuation = clamp(1.0 / denominator, 0.0, 1.0);
    }
    if ((flags & WZ_DERIVED_SPOT) != 0) {
        vec4 spot_direction = wzShading(base + 4);
        float spot = max(0.0, (dot(direction, -spot_direction.xyz) - spot_direction.w) /
                                  (1.0 - spot_direction.w));
        float exponent = wzShading(base + 6).w;
        if (exponent != 1.0) spot = pow(spot, min(exponent, 127.0));
        attenuation *= spot;
    }
    return attenuation;
}

vec3 wzFramePosition(int frame, int vertex)
{
    int count = int(wzDraw(0).w);
    return wzGeometry(frame * count + vertex).xyz;
}

vec3 wzPosition(int vertex)
{
    vec4 frames = wzDraw(1); // frame, next frame, interpolation, frame count
    vec3 current = wzFramePosition(int(frames.x), vertex);
    if (frames.y < 0.0) return current;
    vec3 next = wzFramePosition(int(frames.y), vertex);
    // Mesh::GetVertexLocations copies the next frame when interpolation is 1.
    return frames.z == 1.0 ? next : mix(current, next, frames.z);
}

vec3 wzNormal(int vertex)
{
    vec4 frames = wzDraw(1);
    int count = int(wzDraw(0).w);
    // Normals always come from the current integer frame.
    return wzGeometry((int(frames.w) + int(frames.x)) * count + vertex).xyz;
}

// index_material_alpha, texcoords, colour, diffuse and specular multipliers are
// the ResidentVertex attributes.
WzVertex wzShadeVertex(mat4 model_view, mat3 normal_matrix, vec4 index_material_alpha,
                       vec4 texcoords, vec4 vertex_color, vec4 diffuse_multiplier,
                       vec4 specular_multiplier)
{
    int vertex = int(index_material_alpha.x);
    vec4 draw = wzDraw(0);
    int final_channels = int(draw.x);
    int draw_flags = int(draw.y);
    bool has_normals = draw.z != 0.0;

    WzVertex result;
    vec4 eye = model_view * vec4(wzPosition(vertex), 1.0);
    vec3 normal = has_normals ? normal_matrix * wzNormal(vertex) : vec3(0.0, 0.0, -1.0);
    float eye_distance = length(eye.xyz);

    int material = WZ_MATERIAL_BASE + int(index_material_alpha.y) * WZ_MATERIAL_VEC4S;
    vec4 info_diffuse = wzShading(material + 0);
    vec4 info_ambient = wzShading(material + 1);
    vec4 info_specular = wzShading(material + 2);
    vec4 info_emissive = wzShading(material + 3);
    vec4 info_scalars = wzShading(material + 4);
    vec4 info_mapping = wzShading(material + 5);
    float shininess = info_scalars.x;
    float fog_scale = info_scalars.y;
    int operations = int(info_scalars.z);
    int disabled = int(info_scalars.w);

    int channels = final_channels & ~disabled;
    if ((channels & WZ_DIFFUSE) == 0) channels &= ~(WZ_LIGHT_DIFFUSE | WZ_LIGHT_AMBIENT);
    if ((channels & (WZ_LIGHT_DIFFUSE | WZ_LIGHT_AMBIENT)) == 0) channels &= ~WZ_DIFFUSE;
    if ((operations & (WZ_OPER_DIFFUSE_SPECULAR | WZ_OPER_DIFFUSE)) != 0) {
        channels |= WZ_DIFFUSE | WZ_LIGHT_DIFFUSE | WZ_LIGHT_AMBIENT;
        if ((operations & WZ_OPER_DIFFUSE_SPECULAR) != 0)
            channels |= WZ_SPECULAR;
        else
            channels &= ~WZ_SPECULAR;
    } else if ((operations & WZ_OPER_SPECULAR) != 0) {
        channels &= ~(WZ_DIFFUSE | WZ_LIGHT_DIFFUSE | WZ_LIGHT_AMBIENT);
        channels |= WZ_SPECULAR;
    }

    vec4 ambient_light = wzShading(0);
    vec4 base = ambient_light * info_ambient + info_emissive;
    if ((draw_flags & WZ_HAS_COLORS) != 0) base += vec4(vertex_color.rgb, 1.0);
    vec4 diffuse = vec4(0.0);
    vec4 specular = vec4(0.0);
    float alpha = 1.0;
    float fog = 0.0;
    bool diffuse_ready = false;
    bool specular_ready = false;
    bool alpha_ready = false;
    bool fog_ready = false;

    int light_count = int(wzShading(2).x);
    for (int light = 0; light < WZ_MAX_LIGHTS; ++light) {
        if (light >= light_count) break;
        int light_base = WZ_LIGHT_BASE + light * WZ_LIGHT_VEC4S;
        vec4 location = wzShading(light_base);
        int flags = int(location.w);
        int light_channels = ((flags & (1 << 10)) != 0 ? WZ_LIGHT_AMBIENT : 0) |
                             ((flags & (1 << 11)) != 0 ? WZ_LIGHT_DIFFUSE : 0) |
                             ((flags & (1 << 12)) != 0 ? WZ_SPECULAR : 0);
        int enabled = channels & light_channels;
        if (enabled == 0) continue;
        bool directional = (flags & WZ_DERIVED_DIRECTIONAL) != 0;
        vec3 direction = location.xyz;
        float distance = 0.0;
        if (!directional) {
            vec3 offset = location.xyz - eye.xyz;
            distance = length(offset);
            direction = distance == 0.0 ? offset : offset / distance;
        }
        float attenuation = directional ? 1.0 : wzAttenuation(light_base, flags, direction, distance);
        if (attenuation == 0.0) continue;
        if ((enabled & WZ_LIGHT_AMBIENT) != 0) {
            if (!diffuse_ready) { diffuse = base; diffuse_ready = true; }
            diffuse += (wzShading(light_base + 1) * info_ambient) * attenuation;
        }
        if ((enabled & (WZ_LIGHT_DIFFUSE | WZ_SPECULAR)) == 0) continue;
        float facing = directional ? clamp(dot(normal, direction), 0.0, 1.0)
                                   : max(0.0, dot(normal, direction));
        if (facing == 0.0) continue;
        if ((enabled & WZ_LIGHT_DIFFUSE) != 0) {
            if (!diffuse_ready) { diffuse = base; diffuse_ready = true; }
            diffuse += ((wzShading(light_base + 2) * info_diffuse) * attenuation) * facing;
        }
        if ((enabled & WZ_SPECULAR) != 0) {
            vec3 eye_direction = eye.xyz / length(eye.xyz);
            vec3 halfway = direction - eye_direction;
            float halfway_length = length(halfway);
            if (halfway_length != 0.0) halfway /= halfway_length;
            float highlight = max(0.0, dot(normal, halfway));
            if (shininess > 1.0) highlight = pow(highlight, min(shininess, 127.0));
            highlight *= facing;
            specular_ready = true;
            specular += ((wzShading(light_base + 3) * info_specular) * attenuation) * highlight;
        }
    }

    if ((channels & WZ_FOG) != 0) {
        int fog_count = int(wzShading(2).y);
        for (int layer = 0; layer < WZ_MAX_FOGS; ++layer) {
            if (layer >= fog_count) break;
            vec4 parameters = wzShading(WZ_FOG_BASE + layer); // start, 1/(end-start), density, ramped
            float density = parameters.w != 0.0
                                ? wzRamp(eye_distance, parameters.x, parameters.y, parameters.z, false)
                                : parameters.z;
            fog = fog * (1.0 - density) + density;
            fog_ready = true;
        }
    }

    bool depth_cue = (operations & WZ_OPER_DEPTH_CUE) != 0;
    float cue = wzDepthCue(eye_distance);
    if ((operations & WZ_OPER_DIFFUSE_SPECULAR) != 0) {
        if (!diffuse_ready) { diffuse = base; diffuse_ready = true; }
        specular_ready = true;
        vec4 swapped = diffuse;
        diffuse = specular;
        specular = swapped;
    } else if ((operations & WZ_OPER_DIFFUSE) != 0) {
        if (!diffuse_ready) { diffuse = base; diffuse_ready = true; }
        if (depth_cue) {
            diffuse *= cue;
            depth_cue = false;
        }
        specular_ready = true;
        specular = diffuse;
        channels |= WZ_SPECULAR;
    } else if ((operations & WZ_OPER_SPECULAR) != 0) {
        specular_ready = true;
        if (depth_cue) {
            specular *= cue;
            depth_cue = false;
        }
        if (!diffuse_ready) diffuse_ready = true;
        diffuse = specular;
        channels |= WZ_DIFFUSE;
    }
    if (depth_cue) {
        if ((channels & WZ_DIFFUSE) != 0) {
            if (!diffuse_ready) { diffuse = base; diffuse_ready = true; }
            diffuse *= cue;
        }
        if ((channels & WZ_SPECULAR) != 0) {
            specular_ready = true;
            specular *= cue;
        }
    }
    if ((operations & WZ_OPER_ALPHA) != 0 && (channels & WZ_ALPHA) != 0) {
        alpha *= cue;
        alpha_ready = true;
    }
    diffuse_ready = diffuse_ready && (final_channels & WZ_DIFFUSE) != 0;
    specular_ready = specular_ready && (final_channels & WZ_SPECULAR) != 0;
    alpha_ready = alpha_ready && (final_channels & WZ_ALPHA) != 0;
    fog_ready = fog_ready && (final_channels & WZ_FOG) != 0;

    result.diffuse = vec4(1.0);
    if ((final_channels & (WZ_DIFFUSE | WZ_ALPHA)) != 0) {
        if (!diffuse_ready && !alpha_ready &&
            (draw_flags & (WZ_HAS_COLORS | WZ_HAS_ALPHA)) == 0) {
            diffuse = ambient_light * info_ambient + info_emissive;
            diffuse.w = info_diffuse.w;
            diffuse = clamp(diffuse, 0.0, 1.0);
        } else {
            if ((final_channels & WZ_DIFFUSE) != 0 && !diffuse_ready) {
                diffuse = base;
                diffuse_ready = true;
            }
            if ((final_channels & WZ_ALPHA) != 0) {
                if ((draw_flags & WZ_HAS_ALPHA) != 0) {
                    alpha = alpha_ready ? alpha * index_material_alpha.w : index_material_alpha.w;
                    alpha_ready = true;
                }
                diffuse.w = !alpha_ready ? clamp(info_diffuse.w, 0.0, 1.0)
                            : (final_channels & WZ_DIFFUSE) != 0
                                ? info_diffuse.w * alpha
                                : info_diffuse.w * clamp(alpha, 0.0, 1.0);
            }
            if ((final_channels & WZ_DIFFUSE) != 0) diffuse = clamp(diffuse, 0.0, 1.0);
        }
        if ((draw_flags & WZ_HAS_DIFFUSE_MULTIPLIERS) != 0) diffuse *= diffuse_multiplier;
        result.diffuse = diffuse;
    }

    result.specular = vec4(0.0);
    if ((final_channels & (WZ_SPECULAR | WZ_FOG)) != 0) {
        if (!specular_ready && !fog_ready) {
            specular = vec4(0.0);
        } else if ((final_channels & WZ_SPECULAR) == 0) {
            specular.w = fog_ready ? fog_scale * clamp(fog, 0.0, 1.0) : 0.0;
        } else {
            if (!specular_ready) specular = vec4(0.0);
            if ((final_channels & WZ_FOG) != 0) specular.w = fog_scale * fog;
            specular = clamp(specular, 0.0, 1.0);
        }
        if ((draw_flags & WZ_HAS_SPECULAR_MULTIPLIERS) != 0) specular *= specular_multiplier;
        result.specular = specular;
    }

    // Stage 0 mapping applies only to corners that share the vertex's own UV
    // (the native dedup path copies raw table coordinates to the others).
    vec2 uv = (draw_flags & WZ_HAS_TEXCOORD0) != 0 ? texcoords.xy : vec2(0.0);
    if (index_material_alpha.z != 0.0 && (channels & WZ_ST0) != 0) {
        int mapping = int(info_mapping.x);
        if (mapping == 1)
            uv = (normal.xy + 1.0) * 0.5;
        else if (mapping == 2)
            uv += info_mapping.yz;
    }
    result.st0 = uv;
    result.st1 = (draw_flags & WZ_HAS_TEXCOORD1) != 0 ? texcoords.zw : vec2(0.0);
    result.eye = eye;
    return result;
}

// Verification probe (lighting_check.h): with wzDraw(5).x set, each vertex is
// drawn as one point at its own pixel and outputs a shading result directly.
bool wzProbing()
{
    return wzDraw(5).x != 0.0;
}

vec4 wzProbePosition(int vertex_id)
{
    vec4 probe = wzDraw(5); // mode (1 diffuse, 2 specular), first slot, width, height
    int width = int(probe.z);
    int slot = int(probe.y) + vertex_id;
    int row = slot / width + (probe.x == 2.0 ? int(probe.w) / 2 : 0);
    vec2 pixel = vec2(float(slot - (slot / width) * width), float(row)) + 0.5;
    return vec4(pixel / vec2(float(width), probe.w) * 2.0 - 1.0, 0.0, 1.0);
}

vec4 wzProbeValue(WzVertex shaded)
{
    return wzDraw(5).x == 2.0 ? shaded.specular : shaded.diffuse;
}
