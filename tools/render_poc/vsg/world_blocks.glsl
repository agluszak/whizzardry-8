// The shading blocks of shading_block.h as VulkanSceneGraph binds them:
// set 0 per view (BindViewDescriptorSets), set 1 per MeshInstance, set 2 per
// shared mesh pass, set 3 per shared batch.

layout(std140, set = 0, binding = 0) uniform View {
    vec4 values[WZ_MATERIAL_BASE];
    vec4 fog_color;
    uvec4 masks[WZ_MAX_LIGHTS + WZ_MAX_FOGS];
} view;
layout(std140, set = 3, binding = 0) uniform Batch {
    vec4 channels;
    vec4 modes;
    vec4 alpha_test;
    vec4 probe;
} batch;
