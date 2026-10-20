// VulkanSceneGraph prototype.
//
// SDL3 owns the window, its events and the VkSurfaceKHR. VSG adopts the surface
// through vsg::WindowAdapter and owns the rest of what the native GpuRenderer,
// mesh batches and vertex streams do today: device and swapchain, buffers and
// images with their uploads, descriptor sets, pipelines, transforms and command
// recording. The Wizardry-specific parts are the shared shaders (the GLSL port
// of shadeVertices) and the per-frame plan.

#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vsg/all.h>

#include "demo_scene.h"
#include "lighting_check.h"
#include "poc_app.h"
#include "resident_mesh.h"
#include "shading_block.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/texture.h"
#include "wizardry_frag.spv.h"
#include "wizardry_vert.spv.h"

namespace {
using namespace render_poc;

vsg::dmat4 toVsg(const glm::mat4& matrix)
{
    vsg::dmat4 result;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) result[column][row] = matrix[column][row];
    return result;
}

// The plan's matrices as VSG camera inputs.
class PlanProjection : public vsg::Inherit<vsg::ProjectionMatrix, PlanProjection> {
public:
    vsg::dmat4 matrix;
    vsg::dmat4 transform() const override { return matrix; }
};

class PlanView : public vsg::Inherit<vsg::ViewMatrix, PlanView> {
public:
    vsg::dmat4 matrix;
    vsg::dmat4 transform(const vsg::dvec3& = {}) const override { return matrix; }
};

// The game's projection follows OpenGL clip conventions; Vulkan's y points down
// and its depth range is [0, 1].
glm::mat4 vulkanProjection(const glm::mat4& projection)
{
    glm::mat4 clip(1);
    clip[1][1] = -1;
    clip[2][2] = .5f;
    clip[3][2] = .5f;
    return clip * projection;
}

template<std::size_t N> vsg::ref_ptr<vsg::vec4Array> dynamicArray()
{
    auto array = vsg::vec4Array::create(N);
    array->properties.dataVariance = vsg::DYNAMIC_DATA;
    return array;
}

template<std::size_t N>
void store(vsg::vec4Array& destination, const std::array<glm::vec4, N>& values)
{
    std::memcpy(destination.dataPointer(), values.data(), sizeof(values));
    destination.dirty();
}

class VsgRenderer {
public:
    VsgRenderer(SDL_Window* window, DemoScene& scene, const Options& options,
                const ProbePlan* probes);
    ~VsgRenderer();

    // Records and presents one frame. With `readback`, the top-left
    // width x height pixels of the frame are returned as RGBA8.
    void frame(const FramePlan& plan, bool probes, std::vector<std::uint8_t>* readback = nullptr,
               int width = 0, int height = 0);

private:
    struct PassResources {
        vsg::ref_ptr<vsg::vec4Array> shading = dynamicArray<kShadingVec4s>();
        vsg::ref_ptr<vsg::BindDescriptorSet> bind_pass;
        vsg::ref_ptr<vsg::BindVertexBuffers> vertices;
        vsg::ref_ptr<vsg::BindIndexBuffer> indices;
        std::vector<vsg::ref_ptr<vsg::vec4Array>> draws;
        std::vector<vsg::ref_ptr<vsg::BindDescriptorSet>> bind_batches;
    };
    struct InstanceResources {
        vsg::ref_ptr<vsg::MatrixTransform> transform = vsg::MatrixTransform::create();
        std::vector<PassResources> passes;
    };

    vsg::ref_ptr<vsg::BindGraphicsPipeline> pipeline(std::uint32_t shader);
    vsg::ref_ptr<vsg::BindGraphicsPipeline> makePipeline(std::uint32_t shader, bool points,
                                                         vsg::ref_ptr<vsg::ViewportState> viewport);
    vsg::ref_ptr<vsg::ImageInfo> texture(Texture* source);
    vsg::ref_ptr<vsg::BindDescriptorSet> batchSet(vsg::ref_ptr<vsg::vec4Array> draw,
                                                  vsg::ref_ptr<vsg::ImageInfo> image);
    vsg::ref_ptr<vsg::Node> probeNodes(const ProbePlan& probes);
    std::vector<std::uint8_t> readFrame(int width, int height);

    vsg::ref_ptr<vsg::Instance> instance;
    vsg::ref_ptr<vsg::WindowAdapter> window;
    vsg::ref_ptr<vsg::Viewer> viewer = vsg::Viewer::create();
    vsg::ref_ptr<PlanProjection> projection = PlanProjection::create();
    vsg::ref_ptr<PlanView> view = PlanView::create();
    vsg::ref_ptr<vsg::Switch> views = vsg::Switch::create();
    vsg::ref_ptr<vsg::Group> transparent = vsg::Group::create();
    vsg::ref_ptr<vsg::uintArray> transparent_indices;
    vsg::ref_ptr<vsg::BindIndexBuffer> bind_transparent_indices;

    vsg::ref_ptr<vsg::DescriptorSetLayout> pass_layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> batch_layout;
    vsg::ref_ptr<vsg::PipelineLayout> layout;
    vsg::ShaderStages stages;
    vsg::ref_ptr<vsg::ViewportState> viewport;
    std::map<std::uint32_t, vsg::ref_ptr<vsg::BindGraphicsPipeline>> pipelines;
    std::map<Texture*, vsg::ref_ptr<vsg::ImageInfo>> textures;
    vsg::ref_ptr<vsg::ImageInfo> white;
    std::map<const DemoInstance*, InstanceResources> instances;
};

VsgRenderer::VsgRenderer(SDL_Window* sdl_window, DemoScene& scene, const Options& options,
                         const ProbePlan* probes)
{
    auto traits = vsg::WindowTraits::create();
    traits->width = static_cast<std::uint32_t>(options.width);
    traits->height = static_cast<std::uint32_t>(options.height);
    // The game presents unorm values; an sRGB swapchain would re-encode them.
    traits->swapchainPreferences.surfaceFormat = {VK_FORMAT_B8G8R8A8_UNORM,
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    traits->swapchainPreferences.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (options.frames) traits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;

    Uint32 count = 0;
    const auto* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) throw std::runtime_error(SDL_GetError());
    instance = vsg::Instance::create(vsg::Names(names, names + count), traits->requestedLayers,
                                     traits->vulkanVersion);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(sdl_window, *instance, nullptr, &surface))
        throw std::runtime_error(SDL_GetError());
    // vsg::Surface takes ownership of the SDL-created surface.
    window = vsg::WindowAdapter::create(vsg::Surface::create(surface, instance), traits);
    window->windowValid = true;
    window->windowVisible = true;
    viewer->addWindow(window);

    pass_layout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}});
    batch_layout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    layout =
        vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{pass_layout, batch_layout},
                                    vsg::PushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}});
    const auto module = [](std::span<const std::uint32_t> code) {
        return vsg::ShaderModule::create(vsg::ShaderModule::SPIRV(code.begin(), code.end()));
    };
    stages = {
        vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", module(wizardry_vert)),
        vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", module(wizardry_frag))};
    viewport = vsg::ViewportState::create(window->extent2D());

    auto white_pixel =
        vsg::ubvec4Array2D::create(1, 1, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    white_pixel->at(0, 0) = vsg::ubvec4(255, 255, 255, 255);
    white = vsg::ImageInfo::create(vsg::Sampler::create(), white_pixel);

    auto geometry_sampler = vsg::Sampler::create();
    geometry_sampler->minFilter = geometry_sampler->magFilter = VK_FILTER_NEAREST;
    geometry_sampler->maxLod = 0;

    auto opaque = vsg::Group::create();
    std::size_t transparent_capacity = 0;
    for (const auto& instance_source : scene.instances) {
        const auto& resident = instance_source.resident;
        auto& resources = instances[&instance_source];
        auto geometry =
            vsg::vec4Array2D::create(static_cast<std::uint32_t>(resident.geometry.width),
                                     static_cast<std::uint32_t>(resident.geometry.height),
                                     vsg::Data::Properties{VK_FORMAT_R32G32B32A32_SFLOAT});
        std::memcpy(geometry->dataPointer(), resident.geometry.texels.data(),
                    resident.geometry.texels.size() * sizeof(glm::vec4));
        auto geometry_image = vsg::ImageInfo::create(geometry_sampler, geometry);

        for (const auto& pass : resident.passes) {
            PassResources pass_resources;
            std::array<vsg::ref_ptr<vsg::vec4Array>, 5> attributes;
            for (auto& array : attributes)
                array = vsg::vec4Array::create(static_cast<std::uint32_t>(pass.vertices.size()));
            for (std::size_t index = 0; index < pass.vertices.size(); ++index) {
                const auto& vertex = pass.vertices[index];
                const std::array values{vertex.index_material_alpha, vertex.texcoords, vertex.color,
                                        vertex.diffuse_multiplier, vertex.specular_multiplier};
                for (std::size_t attribute = 0; attribute < values.size(); ++attribute) {
                    const auto& value = values[attribute];
                    attributes[attribute]->at(index) =
                        vsg::vec4(value.x, value.y, value.z, value.w);
                }
            }
            pass_resources.vertices = vsg::BindVertexBuffers::create(
                0, vsg::DataList(attributes.begin(), attributes.end()));
            auto indices = vsg::uintArray::create(static_cast<std::uint32_t>(pass.indices.size()));
            std::ranges::copy(pass.indices, indices->begin());
            pass_resources.indices = vsg::BindIndexBuffer::create(indices);
            pass_resources.bind_pass = vsg::BindDescriptorSet::create(
                VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                vsg::DescriptorSet::create(
                    pass_layout,
                    vsg::Descriptors{vsg::DescriptorBuffer::create(pass_resources.shading, 0),
                                     vsg::DescriptorImage::create(geometry_image, 1)}));

            auto pass_group = vsg::StateGroup::create();
            pass_group->add(pass_resources.bind_pass);
            for (const auto& batch : pass.batches) {
                auto draw = dynamicArray<kDrawVec4s>();
                auto bind = batchSet(draw, texture(batch.texture));
                pass_resources.draws.push_back(draw);
                pass_resources.bind_batches.push_back(bind);
                // Translucent runs bind their pipeline per frame; create it now
                // so that it is compiled with the scene.
                pipeline(batch.shader.value);
                if (resident.sorted) continue;
                auto batch_group = vsg::StateGroup::create();
                batch_group->add(pipeline(batch.shader.value));
                batch_group->add(bind);
                auto commands = vsg::Commands::create();
                commands->addChild(pass_resources.vertices);
                commands->addChild(pass_resources.indices);
                commands->addChild(
                    vsg::DrawIndexed::create(batch.index_count, 1, batch.first_index, 0, 0));
                batch_group->addChild(commands);
                pass_group->addChild(batch_group);
            }
            if (resident.sorted)
                transparent_capacity += pass.indices.size();
            else
                resources.transform->addChild(pass_group);
            resources.passes.push_back(std::move(pass_resources));
        }
        if (!resident.sorted) opaque->addChild(resources.transform);
    }
    // One index buffer, rewritten every frame, holds the ordered translucent runs.
    transparent_indices = vsg::uintArray::create(
        static_cast<std::uint32_t>(std::max<std::size_t>(transparent_capacity, 3)));
    transparent_indices->properties.dataVariance = vsg::DYNAMIC_DATA;
    bind_transparent_indices = vsg::BindIndexBuffer::create(transparent_indices);

    // Recording follows child order: opaque meshes in scene order, then the
    // globally sorted translucent runs. VSG does not regroup by state.
    auto scene_group = vsg::Group::create();
    scene_group->addChild(opaque);
    scene_group->addChild(transparent);
    // Everything the per-frame translucent runs bind is compiled up front.
    auto compiled = vsg::StateGroup::create();
    for (const auto& [shader, bind] : pipelines) compiled->add(bind);
    auto bind_buffers = vsg::Commands::create();
    bind_buffers->addChild(bind_transparent_indices);
    for (const auto& [source, resources] : instances) {
        if (!source->resident.sorted) continue;
        for (const auto& pass : resources.passes) {
            compiled->add(pass.bind_pass);
            for (const auto& bind : pass.bind_batches) compiled->add(bind);
            bind_buffers->addChild(pass.vertices);
        }
    }
    compiled->addChild(bind_buffers);
    auto scene_root = vsg::Group::create();
    scene_root->addChild(scene_group);
    scene_root->addChild(compiled);
    auto camera = vsg::Camera::create(projection, view, viewport);
    // VSG applies a view's camera viewport to its pipelines, so the probes get
    // their own view with a probe-sized viewport.
    views->addChild(true, vsg::View::create(camera, scene_root));
    if (probes) {
        auto probe_camera = vsg::Camera::create(
            projection, view,
            vsg::ViewportState::create(0, 0, static_cast<std::uint32_t>(probes->width),
                                       static_cast<std::uint32_t>(probes->height)));
        views->addChild(false, vsg::View::create(probe_camera, probeNodes(*probes)));
    }
    auto render_graph = vsg::RenderGraph::create(window);
    render_graph->addChild(views);
    const auto& clear = scene.clear_color;
    render_graph->setClearValues({{clear.r, clear.g, clear.b, clear.a}}, {1.0f, 0});
    auto command_graph = vsg::CommandGraph::create(window);
    command_graph->addChild(render_graph);
    viewer->assignRecordAndSubmitTaskAndPresentation({command_graph});
    viewer->compile();
}

VsgRenderer::~VsgRenderer()
{
    viewer->deviceWaitIdle();
}

vsg::ref_ptr<vsg::BindGraphicsPipeline> VsgRenderer::pipeline(std::uint32_t shader)
{
    auto& found = pipelines[shader];
    if (!found) found = makePipeline(shader, false, viewport);
    return found;
}

vsg::ref_ptr<vsg::BindGraphicsPipeline>
VsgRenderer::makePipeline(std::uint32_t shader, bool points, vsg::ref_ptr<vsg::ViewportState> area)
{
    static constexpr std::array source{VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE,
                                       VK_BLEND_FACTOR_SRC_ALPHA,
                                       VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    static constexpr std::array destination{
        VK_BLEND_FACTOR_ZERO,      VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ONE};
    const auto raster = rasterState(shader);

    vsg::VertexInputState::Bindings bindings;
    vsg::VertexInputState::Attributes attributes;
    for (std::uint32_t location = 0; location < 5; ++location) {
        bindings.push_back({location, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.push_back({location, location, VK_FORMAT_R32G32B32A32_SFLOAT, 0});
    }
    auto assembly = vsg::InputAssemblyState::create();
    assembly->topology =
        points ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // The native mesh path culls in software and rasterizes without culling.
    auto rasterization = vsg::RasterizationState::create();
    rasterization->cullMode = VK_CULL_MODE_NONE;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = raster.blend;
    blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = source[raster.source];
    blend.dstColorBlendFactor = blend.dstAlphaBlendFactor = destination[raster.destination];
    blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = raster.color_write
                               ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                               : 0;
    auto depth = vsg::DepthStencilState::create();
    depth->depthTestEnable = raster.depth_test;
    depth->depthWriteEnable = raster.depth_write;
    // DrawShader's PASS_* values use Vulkan's compare-op order.
    depth->depthCompareOp = static_cast<VkCompareOp>(raster.depth_compare);

    vsg::GraphicsPipelineStates states{
        vsg::VertexInputState::create(bindings, attributes),
        assembly,
        rasterization,
        vsg::MultisampleState::create(),
        vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{blend}),
        depth,
        area};
    return vsg::BindGraphicsPipeline::create(vsg::GraphicsPipeline::create(layout, stages, states));
}

vsg::ref_ptr<vsg::ImageInfo> VsgRenderer::texture(Texture* source)
{
    if (!source) return white;
    auto& found = textures[source];
    if (found) return found;
    auto& surface = *source->getSurface();
    const auto pixels = rgba8Pixels(surface);
    auto image = vsg::ubvec4Array2D::create(static_cast<std::uint32_t>(surface.w),
                                            static_cast<std::uint32_t>(surface.h),
                                            vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    std::memcpy(image->dataPointer(), pixels.data(), pixels.size());
    auto sampler = vsg::Sampler::create();
    sampler->addressModeU = sampler->addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler->mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler->maxLod = 16; // VSG generates the mip chain on upload
    found = vsg::ImageInfo::create(sampler, image);
    return found;
}

vsg::ref_ptr<vsg::BindDescriptorSet> VsgRenderer::batchSet(vsg::ref_ptr<vsg::vec4Array> draw,
                                                           vsg::ref_ptr<vsg::ImageInfo> image)
{
    return vsg::BindDescriptorSet::create(
        VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
        vsg::DescriptorSet::create(batch_layout,
                                   vsg::Descriptors{vsg::DescriptorBuffer::create(draw, 0),
                                                    vsg::DescriptorImage::create(image, 1)}));
}

vsg::ref_ptr<vsg::Node> VsgRenderer::probeNodes(const ProbePlan& probes)
{
    // Every vertex becomes one point at its own pixel; no depth test, no blending.
    auto group = vsg::Group::create();
    constexpr std::uint32_t unblended_always =
        DrawShader::PASS_ALWAYS | DrawShader::MASK_COLOR_WRITE |
        DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT;
    auto points = makePipeline(unblended_always, true, viewport);
    for (const auto& probe : probes.cases) {
        for (int mode = 1; mode <= 2; ++mode) {
            auto draw = dynamicArray<kDrawVec4s>();
            store(*draw, probeDrawBlock(probes, probe, mode));
            auto transform = vsg::MatrixTransform::create(
                toVsg(probe.draw->instance->transform.matrix<glm::mat4>()));
            const auto& pass = instances.at(probe.draw->instance).passes[probe.pass];
            auto state = vsg::StateGroup::create();
            state->add(points);
            state->add(pass.bind_pass);
            state->add(batchSet(draw, white));
            auto commands = vsg::Commands::create();
            commands->addChild(pass.vertices);
            commands->addChild(vsg::Draw::create(
                static_cast<std::uint32_t>(probe.expected_diffuse.size()), 1, 0, 0));
            state->addChild(commands);
            transform->addChild(state);
            group->addChild(transform);
        }
    }
    return group;
}

std::vector<std::uint8_t> VsgRenderer::readFrame(int width, int height)
{
    // Copy the presented image to host memory before it is handed to the presentation engine.
    auto device = window->getOrCreateDevice();
    const auto device_id = device->deviceID;
    auto image = window->imageView(window->imageIndex())->image;
    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;
    auto buffer = vsg::createBufferAndMemory(
        device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const auto family = static_cast<std::uint32_t>(
        window->getOrCreatePhysicalDevice()->getQueueFamily(VK_QUEUE_GRAPHICS_BIT));
    auto pool = vsg::CommandPool::create(device, family);
    auto fence = vsg::Fence::create(device);
    vsg::submitCommandsToQueue(
        pool, fence, 1'000'000'000, device->getQueue(family), [&](vsg::CommandBuffer& commands) {
            const auto barrier = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags source,
                                     VkAccessFlags destination) {
                VkImageMemoryBarrier info{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                info.srcAccessMask = source;
                info.dstAccessMask = destination;
                info.oldLayout = from;
                info.newLayout = to;
                info.srcQueueFamilyIndex = info.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                info.image = image->vk(device_id);
                info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                                     1, &info);
            };
            barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {static_cast<std::uint32_t>(width),
                                  static_cast<std::uint32_t>(height), 1};
            vkCmdCopyImageToBuffer(commands, image->vk(device_id),
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer->vk(device_id), 1,
                                   &region);
            barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT);
        });
    void* mapped = nullptr;
    auto* memory = buffer->getDeviceMemory(device_id);
    memory->map(buffer->getMemoryOffset(device_id), size, 0, &mapped);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size));
    std::memcpy(pixels.data(), mapped, pixels.size());
    memory->unmap();
    // The swapchain is BGRA.
    for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4)
        std::swap(pixels[pixel], pixels[pixel + 2]);
    return pixels;
}

void VsgRenderer::frame(const FramePlan& plan, bool probes, std::vector<std::uint8_t>* readback,
                        int width, int height)
{
    projection->matrix = toVsg(vulkanProjection(plan.camera.projection));
    view->matrix = toVsg(plan.camera.view);

    const auto update = [&](const PlannedDraw& draw) {
        auto& resources = instances.at(draw.instance);
        resources.transform->matrix = toVsg(draw.instance->transform.matrix<glm::mat4>());
        for (std::size_t pass = 0; pass < resources.passes.size(); ++pass) {
            store(*resources.passes[pass].shading, draw.shading[pass]);
            for (std::size_t batch = 0; batch < resources.passes[pass].draws.size(); ++batch)
                store(*resources.passes[pass].draws[batch], draw.draw_blocks[pass][batch]);
        }
    };
    for (const auto& draw : plan.opaque) update(draw);
    for (const auto& draw : plan.sorted) update(draw);

    // The translucent runs: lightweight per-frame nodes over compiled state.
    std::ranges::copy(plan.transparent.indices, transparent_indices->begin());
    transparent_indices->dirty();
    transparent->children.clear();
    for (const auto& run : plan.transparent.runs) {
        const auto& draw = plan.sorted[run.draw];
        const auto& pass = instances.at(draw.instance).passes[run.pass];
        const auto& batch = draw.instance->resident.passes[run.pass].batches[run.batch];
        auto state = vsg::StateGroup::create();
        state->add(pipeline(batch.shader.value));
        state->add(pass.bind_pass);
        state->add(pass.bind_batches[run.batch]);
        auto commands = vsg::Commands::create();
        commands->addChild(pass.vertices);
        commands->addChild(bind_transparent_indices);
        commands->addChild(vsg::DrawIndexed::create(run.index_count, 1, run.first_index, 0, 0));
        state->addChild(commands);
        auto transform =
            vsg::MatrixTransform::create(toVsg(draw.instance->transform.matrix<glm::mat4>()));
        transform->addChild(state);
        transparent->addChild(transform);
    }
    views->children[0].mask = vsg::boolToMask(!probes);
    if (views->children.size() > 1) views->children[1].mask = vsg::boolToMask(probes);

    if (!viewer->advanceToNextFrame()) throw std::runtime_error("VSG viewer closed");
    viewer->update();
    viewer->recordAndSubmit();
    if (readback) {
        viewer->deviceWaitIdle();
        *readback = readFrame(width, height);
    }
    viewer->present();
}

int run(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    const auto flags = SDL_WINDOW_VULKAN | (options.hidden ? SDL_WINDOW_HIDDEN : 0);
    auto* window =
        SDL_CreateWindow("Whizzardry - VulkanSceneGraph", options.width, options.height, flags);
    if (!window) throw std::runtime_error(SDL_GetError());

    int status = 0;
    {
        DemoScene scene;
        std::optional<ProbePlan> probes;
        std::optional<FramePlan> probe_frame;
        if (options.verify) {
            scene.update(options.time.value_or(1.3));
            probe_frame = planFrame(scene);
            probes = planProbes(scene, *probe_frame);
        }
        VsgRenderer renderer(window, scene, options, probes ? &*probes : nullptr);
        if (probes) {
            std::vector<std::uint8_t> pixels;
            renderer.frame(*probe_frame, true, &pixels, probes->width, probes->height);
            const auto report = compareProbes(*probes, pixels);
            std::printf("VulkanSceneGraph vertex stage vs shadeVertices: %s\n",
                        report.summary.c_str());
            if (!report.passed()) status = 1;
        }
        FrameClock clock;
        for (int frame = 0; !options.frames || frame < *options.frames; ++frame) {
            bool quit = false;
            for (SDL_Event event; SDL_PollEvent(&event);)
                quit |= event.type == SDL_EVENT_QUIT ||
                        (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
            if (quit) break;
            scene.update(clock.sceneTime(options));
            const bool last = options.frames && frame + 1 == *options.frames;
            std::vector<std::uint8_t> pixels;
            const bool capture = last && !options.screenshot.empty();
            renderer.frame(planFrame(scene), false, capture ? &pixels : nullptr, options.width,
                           options.height);
            if (capture) savePng(options.screenshot, pixels, options.width, options.height, false);
            clock.frameDone();
        }
        std::printf("VulkanSceneGraph: %s\n", clock.summary().c_str());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
