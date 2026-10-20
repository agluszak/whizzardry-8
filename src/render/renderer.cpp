#include "wiz8/render/renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <vsg/app/Camera.h>
#include <vsg/app/CommandGraph.h>
#include <vsg/app/CompileManager.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/WindowAdapter.h>
#include <vsg/app/WindowTraits.h>
#include <vsg/core/Array.h>
#include <vsg/core/Array2D.h>
#include <vsg/core/Data.h>
#include <vsg/core/Exception.h>
#include <vsg/core/Value.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/BindDescriptorSet.h>
#include <vsg/state/Buffer.h>
#include <vsg/state/ColorBlendState.h>
#include <vsg/state/DepthStencilState.h>
#include <vsg/state/Descriptor.h>
#include <vsg/state/DescriptorImage.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/DescriptorSetLayout.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/Image.h>
#include <vsg/state/ImageInfo.h>
#include <vsg/state/ImageView.h>
#include <vsg/state/InputAssemblyState.h>
#include <vsg/state/MultisampleState.h>
#include <vsg/state/PipelineLayout.h>
#include <vsg/state/RasterizationState.h>
#include <vsg/state/Sampler.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/state/VertexInputState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/CommandBuffer.h>
#include <vsg/vk/CommandPool.h>
#include <vsg/vk/Device.h>
#include <vsg/vk/DeviceMemory.h>
#include <vsg/vk/Framebuffer.h>
#include <vsg/vk/Instance.h>
#include <vsg/vk/PhysicalDevice.h>
#include <vsg/vk/RenderPass.h>
#include <vsg/vk/SubmitCommands.h>
#include <vsg/vk/Surface.h>
#include <vulkan/vulkan_core.h>

#include "overlay_frag.spv.h"
#include "overlay_vert.spv.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/surface_storage.h"

namespace {
vsg::ref_ptr<vsg::ShaderModule> module(std::span<const std::uint32_t> code)
{
    return vsg::ShaderModule::create(vsg::ShaderModule::SPIRV(code.begin(), code.end()));
}
SDL_PixelFormat readbackFormat(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        return SDL_PIXELFORMAT_RGBA32;
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        return SDL_PIXELFORMAT_BGRA32;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return SDL_PIXELFORMAT_ABGR8888;
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        return SDL_PIXELFORMAT_ARGB2101010;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return SDL_PIXELFORMAT_ABGR2101010;
    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_R8G8B8_SRGB:
        return SDL_PIXELFORMAT_RGB24;
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_B8G8R8_SRGB:
        return SDL_PIXELFORMAT_BGR24;
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
        return SDL_PIXELFORMAT_RGB565;
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
        return SDL_PIXELFORMAT_BGR565;
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
        return SDL_PIXELFORMAT_RGBA5551;
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
        return SDL_PIXELFORMAT_BGRA5551;
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        return SDL_PIXELFORMAT_ARGB1555;
    case VK_FORMAT_R16G16B16A16_UNORM:
        return SDL_PIXELFORMAT_RGBA64;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return SDL_PIXELFORMAT_RGBA64_FLOAT;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return SDL_PIXELFORMAT_RGBA128_FLOAT;
    default:
        throw std::runtime_error("Unsupported Vulkan screenshot pixel format");
    }
}
}

struct Renderer::Impl {
    explicit Impl(SDL_Window* platform_window);
    void createOffscreen();
    void syncTargets();
    vsg::ref_ptr<vsg::View> overlayView();
    vsg::ref_ptr<vsg::BindGraphicsPipeline> overlayPipeline(DrawShader shader);
    void capture(vsg::ref_ptr<vsg::Image> image, VkImageLayout layout);

    SDL_Window* platform_window;
    vsg::ref_ptr<vsg::Instance> instance;
    vsg::ref_ptr<vsg::WindowAdapter> window;
    vsg::ref_ptr<vsg::Viewer> viewer = vsg::Viewer::create();
    vsg::ref_ptr<vsg::Viewer> offscreen_viewer = vsg::Viewer::create();
    vsg::ref_ptr<vsg::Group> frame_root = vsg::Group::create();
    vsg::ref_ptr<vsg::RenderGraph> render_graph;
    vsg::ref_ptr<vsg::RenderGraph> offscreen_graph;
    vsg::ref_ptr<vsg::Image> offscreen_color;
    std::unique_ptr<render::RenderContext> context;
    vsg::ref_ptr<vsg::DescriptorSetLayout> overlay_set_layout;
    vsg::ref_ptr<vsg::PipelineLayout> overlay_layout;
    std::map<std::uint32_t, vsg::ref_ptr<vsg::BindGraphicsPipeline>> overlay_pipelines;
    std::vector<vsg::ref_ptr<vsg::View>> overlay_views;
    vsg::ref_ptr<vsg::View> current_overlay;
    std::size_t overlay_count = 0;
    vsg::ref_ptr<vsg::ImageInfo> white;
    SDLSurfaceOwner captured{nullptr, SDL_DestroySurface};
    std::uint64_t frame_number = 0;
    int graphics_family = -1;
    bool open = true;
    bool capture_enabled = false;
    bool capture_window = false;
};

Renderer::Impl::Impl(SDL_Window* platform_window) : platform_window(platform_window)
{
    if (!platform_window) throw std::runtime_error("Renderer requires an SDL window");
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(platform_window, &width, &height))
        throw std::runtime_error(SDL_GetError());
    auto traits = vsg::WindowTraits::create();
    traits->width = static_cast<std::uint32_t>(std::max(width, 1));
    traits->height = static_cast<std::uint32_t>(std::max(height, 1));
    // The canvas and retail lighting already contain display-space color.
    traits->swapchainPreferences.surfaceFormat = {VK_FORMAT_B8G8R8A8_UNORM,
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    Uint32 count = 0;
    const auto* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) throw std::runtime_error(SDL_GetError());
    instance = vsg::Instance::create(vsg::Names(names, names + count), traits->requestedLayers,
                                     traits->vulkanVersion);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(platform_window, *instance, nullptr, &surface))
        throw std::runtime_error(SDL_GetError());
    window = vsg::WindowAdapter::create(vsg::Surface::create(surface, instance), traits);
    window->windowValid = window->windowVisible = true;
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(window->getOrCreatePhysicalDevice()->vk(),
                                                  surface, &capabilities) != VK_SUCCESS)
        throw std::runtime_error("Cannot query Vulkan window capabilities");
    // WSI only guarantees color-attachment usage. Screenshots can render the
    // same graph offscreen when the presentation images cannot be copied.
    capture_window = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (capture_window) traits->swapchainPreferences.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    window->getOrCreateSwapchain();
    context = std::make_unique<render::RenderContext>(window->extent2D());

    overlay_set_layout = vsg::DescriptorSetLayout::create(vsg::DescriptorSetLayoutBindings{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}});
    overlay_layout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{overlay_set_layout},
                                                 vsg::PushConstantRanges{});
    auto pixel = vsg::ubvec4Array2D::create(1, 1, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    pixel->at(0, 0) = vsg::ubvec4(255, 255, 255, 255);
    white = vsg::ImageInfo::create(vsg::Sampler::create(), pixel);

    render_graph = vsg::RenderGraph::create(window);
    render_graph->previous_extent = window->extent2D();
    render_graph->addChild(frame_root);
    auto commands = vsg::CommandGraph::create(window);
    graphics_family = commands->queueFamily;
    commands->addChild(render_graph);
    viewer->assignRecordAndSubmitTaskAndPresentation({commands});
    createOffscreen();
}

void Renderer::Impl::createOffscreen()
{
    auto device = window->getOrCreateDevice();
    const auto extent = window->extent2D();
    const auto image = [&](VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
        auto result = vsg::Image::create();
        result->imageType = VK_IMAGE_TYPE_2D;
        result->format = format;
        result->extent = {extent.width, extent.height, 1};
        result->mipLevels = result->arrayLayers = 1;
        result->usage = usage;
        return vsg::createImageView(device, result, aspect);
    };
    auto color = image(window->surfaceFormat().format,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT);
    auto depth = image(window->depthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT);
    offscreen_color = color->image;
    // The offscreen pass has the same attachment formats as the window pass.
    // Its final color layout permits screenshots without acquiring a swapchain
    // image or presenting an automap/model-preview frame to the player.
    auto window_pass = window->getOrCreateRenderPass();
    auto attachments = window_pass->attachments;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    auto pass = vsg::RenderPass::create(device, attachments, window_pass->subpasses,
                                        window_pass->dependencies);
    auto framebuffer = vsg::Framebuffer::create(pass, vsg::ImageViews{color, depth}, extent.width,
                                                extent.height, 1);
    offscreen_graph = vsg::RenderGraph::create();
    offscreen_graph->framebuffer = framebuffer;
    offscreen_graph->renderArea = {{0, 0}, extent};
    offscreen_graph->viewportState = vsg::ViewportState::create(extent);
    offscreen_graph->windowResizeHandler = {};
    offscreen_graph->clearValues = render_graph->clearValues;
    offscreen_graph->addChild(frame_root);
    auto commands = vsg::CommandGraph::create(device, graphics_family);
    commands->addChild(offscreen_graph);
    offscreen_viewer->assignRecordAndSubmitTaskAndPresentation({commands});
}

void Renderer::Impl::syncTargets()
{
    const auto extent = window->extent2D();
    if (offscreen_color->extent.width == extent.width &&
        offscreen_color->extent.height == extent.height)
        return;
    // Acquisition and present-mode changes can rebuild the swapchain as well
    // as explicit resize events. Keep every target on its actual WSI extent.
    render_graph->resized();
    render_graph->renderArea = {{0, 0}, extent};
    createOffscreen();
    for (auto& view : overlay_views)
        view->camera->viewportState = vsg::ViewportState::create(extent);
}

vsg::ref_ptr<vsg::BindGraphicsPipeline> Renderer::Impl::overlayPipeline(DrawShader shader)
{
    auto& cached = overlay_pipelines[shader.value];
    if (cached) return cached;
    auto vertex =
        vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", module(overlay_vert));
    auto fragment =
        vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", module(overlay_frag));
    fragment->specializationConstants[0] = vsg::uintValue::create(shader.value);
    auto raster = vsg::RasterizationState::create();
    raster->cullMode = VK_CULL_MODE_NONE;
    auto depth = vsg::DepthStencilState::create();
    depth->depthTestEnable = depth->depthWriteEnable = false;
    constexpr std::array source{VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE,
                                VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    constexpr std::array destination{
        VK_BLEND_FACTOR_ZERO,      VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
    VkPipelineColorBlendAttachmentState blend{};
    const auto src = (shader.value >> DrawShader::SRCBLEND_SHIFT) & 3;
    const auto dst = (shader.value >> DrawShader::DSTBLEND_SHIFT) & 7;
    blend.blendEnable = src != DrawShader::SRCBLEND_ONE || dst != DrawShader::DSTBLEND_ZERO;
    blend.srcColorBlendFactor = blend.srcAlphaBlendFactor = source[src];
    blend.dstColorBlendFactor = blend.dstAlphaBlendFactor =
        destination[std::min<std::size_t>(dst, 5)];
    blend.colorBlendOp = blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = (shader.value & DrawShader::MASK_COLOR_WRITE)
                               ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                               : 0;
    const vsg::VertexInputState::Bindings bindings{{0, 12, VK_VERTEX_INPUT_RATE_VERTEX},
                                                   {1, 8, VK_VERTEX_INPUT_RATE_VERTEX},
                                                   {2, 16, VK_VERTEX_INPUT_RATE_VERTEX}};
    const vsg::VertexInputState::Attributes attributes{{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                       {1, 1, VK_FORMAT_R32G32_SFLOAT, 0},
                                                       {2, 2, VK_FORMAT_R32G32B32A32_SFLOAT, 0}};
    vsg::GraphicsPipelineStates states{
        vsg::VertexInputState::create(bindings, attributes),
        vsg::InputAssemblyState::create(),
        raster,
        vsg::MultisampleState::create(),
        depth,
        vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{blend}),
        vsg::ViewportState::create(window->extent2D())};
    cached = vsg::BindGraphicsPipeline::create(
        vsg::GraphicsPipeline::create(overlay_layout, vsg::ShaderStages{vertex, fragment}, states));
    return cached;
}

vsg::ref_ptr<vsg::View> Renderer::Impl::overlayView()
{
    if (current_overlay) return current_overlay;
    if (overlay_count == overlay_views.size()) {
        auto camera = vsg::Camera::create(vsg::Perspective::create(), vsg::LookAt::create(),
                                          vsg::ViewportState::create(window->extent2D()));
        overlay_views.push_back(
            vsg::View::create(camera, vsg::Group::create(), static_cast<vsg::ViewFeatures>(0)));
    }
    current_overlay = overlay_views[overlay_count++];
    frame_root->addChild(current_overlay);
    return current_overlay;
}

Renderer::Renderer(SDL_Window* window)
try : impl(std::make_unique<Impl>(window)) {
} catch (const vsg::Exception& error) {
    throw std::runtime_error(error.message);
}
Renderer::~Renderer()
{
    impl->viewer->deviceWaitIdle();
    impl->offscreen_viewer->deviceWaitIdle();
}
bool Renderer::reopen()
try {
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(impl->platform_window, &width, &height) || width <= 0 ||
        height <= 0)
        return false;
    impl->window->windowVisible = true;
    const VkExtent2D next{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    if (next.width != impl->window->extent2D().width ||
        next.height != impl->window->extent2D().height) {
        impl->viewer->deviceWaitIdle();
        impl->offscreen_viewer->deviceWaitIdle();
        impl->window->updateExtents(next.width, next.height);
        impl->window->resize();
    }
    impl->syncTargets();
    impl->open = true;
    return true;
} catch (const vsg::Exception& error) {
    throw std::runtime_error(error.message);
}
void Renderer::close()
{
    impl->viewer->deviceWaitIdle();
    impl->offscreen_viewer->deviceWaitIdle();
    impl->open = false;
    impl->window->windowVisible = false;
}
bool Renderer::isOpen() const
{
    return impl->open;
}
std::uint32_t Renderer::width() const
{
    return impl->window->extent2D().width;
}
std::uint32_t Renderer::height() const
{
    return impl->window->extent2D().height;
}
std::uint64_t Renderer::frameNumber() const
{
    return impl->frame_number;
}
render::RenderContext& Renderer::context()
{
    return *impl->context;
}
void Renderer::setCaptureEnabled(bool enabled)
{
    impl->capture_enabled = enabled;
}
void Renderer::setVsync(bool enabled)
try {
    const auto mode = enabled ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (impl->window->traits()->swapchainPreferences.presentMode == mode) return;
    impl->viewer->deviceWaitIdle();
    impl->offscreen_viewer->deviceWaitIdle();
    impl->window->traits()->swapchainPreferences.presentMode = mode;
    int width = 0, height = 0;
    if (SDL_GetWindowSizeInPixels(impl->platform_window, &width, &height) && width > 0 &&
        height > 0)
        impl->window->updateExtents(static_cast<std::uint32_t>(width),
                                    static_cast<std::uint32_t>(height));
    impl->window->resize();
    impl->syncTargets();
} catch (const vsg::Exception& error) {
    throw std::runtime_error(error.message);
}
void Renderer::beginFrame(const glm::vec4& clear)
{
    ++impl->frame_number;
    impl->context->frame_number = impl->frame_number;
    impl->frame_root->children.clear();
    impl->current_overlay = {};
    impl->overlay_count = 0;
    for (auto& view : impl->overlay_views)
        view->children.front().cast<vsg::Group>()->children.clear();
    impl->render_graph->setClearValues({{clear.r, clear.g, clear.b, clear.a}}, {0, 0});
    impl->offscreen_graph->setClearValues({{clear.r, clear.g, clear.b, clear.a}}, {0, 0});
}
void Renderer::addView(vsg::ref_ptr<vsg::View> view)
{
    impl->current_overlay = {};
    impl->frame_root->addChild(view);
}
void Renderer::drawOverlayQuad(const OverlayQuad& quad)
{
    const auto size = quad.scaledSize();
    if (!quad.visible || size.x <= 0 || size.y <= 0) return;
    const auto center = quad.position + size * .5f;
    const auto cosine = std::cos(quad.rotation), sine = std::sin(quad.rotation);
    auto positions = vsg::vec3Array::create(4);
    constexpr std::array corners{glm::vec2(-.5f, -.5f), glm::vec2(.5f, -.5f), glm::vec2(-.5f, .5f),
                                 glm::vec2(.5f, .5f)};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto offset = corners[i] * size;
        positions->at(i) = {center.x + cosine * offset.x - sine * offset.y,
                            center.y + sine * offset.x + cosine * offset.y, 0};
    }
    auto uv = vsg::vec2Array::create(4);
    uv->at(0) = {quad.uv_min.x, quad.uv_min.y};
    uv->at(1) = {quad.uv_max.x, quad.uv_min.y};
    uv->at(2) = {quad.uv_min.x, quad.uv_max.y};
    uv->at(3) = {quad.uv_max.x, quad.uv_max.y};
    auto colors = vsg::vec4Array::create(
        4, vsg::vec4(quad.color.r, quad.color.g, quad.color.b, quad.color.a));
    auto indices = vsg::ushortArray::create({0, 1, 2, 2, 1, 3});
    auto draw = vsg::VertexIndexDraw::create();
    draw->assignArrays({positions, uv, colors});
    draw->assignIndices(indices);
    draw->indexCount = 6;
    draw->instanceCount = 1;
    auto image = quad.texture ? quad.texture->imageInfo(0, impl->frame_number) : impl->white;
    if (!image) image = impl->white;
    auto state = vsg::StateGroup::create();
    state->add(impl->overlayPipeline(quad.shader));
    state->add(vsg::BindDescriptorSet::create(
        VK_PIPELINE_BIND_POINT_GRAPHICS, impl->overlay_layout, 0,
        vsg::DescriptorSet::create(impl->overlay_set_layout,
                                   vsg::Descriptors{vsg::DescriptorImage::create(image, 0)})));
    state->addChild(draw);
    impl->overlayView()->children.front().cast<vsg::Group>()->addChild(state);
}

void Renderer::Impl::capture(vsg::ref_ptr<vsg::Image> image, VkImageLayout layout)
{
    auto device = window->getOrCreateDevice();
    const auto device_id = device->deviceID;
    // Swapchain wrappers leave Image::format and extent undefined; their
    // metadata belongs to the window. Owned offscreen images carry both.
    const bool swapchain = image->format == VK_FORMAT_UNDEFINED;
    const auto extent =
        swapchain ? window->extent2D() : VkExtent2D{image->extent.width, image->extent.height};
    const auto format =
        readbackFormat(swapchain ? window->imageView(window->imageIndex())->format : image->format);
    const auto row_bytes = VkDeviceSize(extent.width) * SDL_BYTESPERPIXEL(format);
    const VkDeviceSize bytes = row_bytes * extent.height;
    auto next =
        CreateCpuStorage(static_cast<int>(extent.width), static_cast<int>(extent.height), format);
    auto buffer = vsg::createBufferAndMemory(
        device, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const auto family = static_cast<std::uint32_t>(graphics_family);
    const auto copied = vsg::submitCommandsToQueue(
        vsg::CommandPool::create(device, family), device->getQueue(family),
        [&](vsg::CommandBuffer& commands) {
            const auto barrier = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags source,
                                     VkAccessFlags destination) {
                VkImageMemoryBarrier info{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
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
            barrier(layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(commands, image->vk(device_id),
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer->vk(device_id), 1,
                                   &region);
            barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_MEMORY_READ_BIT);
        });
    if (copied != VK_SUCCESS) throw std::runtime_error("Cannot copy rendered frame");
    auto* memory = buffer->getDeviceMemory(device_id);
    void* mapped = nullptr;
    if (memory->map(buffer->getMemoryOffset(device_id), bytes, 0, &mapped) != VK_SUCCESS)
        throw std::runtime_error("Cannot map rendered frame");
    for (std::uint32_t y = 0; y < extent.height; ++y)
        std::memcpy(static_cast<std::uint8_t*>(next->pixels) + std::size_t(y) * next->pitch,
                    static_cast<const std::uint8_t*>(mapped) + y * row_bytes, row_bytes);
    memory->unmap();
    captured = std::move(next);
}
void Renderer::endFrame(bool present, bool capture)
try {
    if (!impl->open) return;
    auto viewer = present ? impl->viewer : impl->offscreen_viewer;
    if (!viewer->advanceToNextFrame()) throw std::runtime_error("VSG frame acquisition failed");
    // WSI acquisition can resize the window before this frame is recorded.
    impl->syncTargets();
    // Viewer::compile discovers current views and newly attached resources.
    // Retain its manager so VSG can reuse device memory and descriptor pools.
    const auto compiled = viewer->compile();
    if (compiled.result != VK_SUCCESS)
        throw std::runtime_error("VSG scene compilation failed: " + compiled.message);
    viewer->update();
    viewer->recordAndSubmit();
    // A single completion boundary handles removed graph nodes, replaced
    // descriptors and streamed UI pixels without parallel retirement queues.
    viewer->deviceWaitIdle();
    if (capture || impl->capture_enabled) {
        if (present && !impl->capture_window) {
            // The render graph has no simulation updates: another target sees
            // the same prepared pose, materials and canvas publication.
            if (!impl->offscreen_viewer->advanceToNextFrame())
                throw std::runtime_error("VSG offscreen frame acquisition failed");
            const auto compiled_offscreen = impl->offscreen_viewer->compile();
            if (compiled_offscreen.result != VK_SUCCESS)
                throw std::runtime_error("VSG scene compilation failed: " +
                                         compiled_offscreen.message);
            impl->offscreen_viewer->update();
            impl->offscreen_viewer->recordAndSubmit();
            impl->offscreen_viewer->deviceWaitIdle();
        }
        const bool window_capture = present && impl->capture_window;
        impl->capture(window_capture ? impl->window->imageView(impl->window->imageIndex())->image
                                     : impl->offscreen_color,
                      window_capture ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                     : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }
    if (present) viewer->present();
    viewer->deviceWaitIdle();
} catch (const vsg::Exception& error) {
    throw std::runtime_error(error.message);
}
SDL_Surface* Renderer::readback() const
{
    return impl->captured.get();
}
