// Wizardry material state. Dynamic storage buffers are sized from the actual
// content; light, fog and material counts have no renderer-imposed fixed cap.
layout(std430, set = 0, binding = 0) readonly buffer View {
    vec4 values[];
} view;
layout(std140, set = 3, binding = 0) uniform Batch {
    vec4 channels;
    vec4 modes;
    vec4 alpha_test;
} batch;

#define WZ_LIGHT_BASE 3
#define WZ_LIGHT_VEC4S 8
#define WZ_FOG_BASE int(view.values[2].w)
#define WZ_MATERIAL_BASE (WZ_FOG_BASE + int(view.values[2].y))
#define WZ_MATERIAL_VEC4S 6
