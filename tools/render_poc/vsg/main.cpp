// VulkanSceneGraph prototype.
//
// SDL3 owns the window, its events and the VkSurfaceKHR. VSG adopts the surface
// through vsg::WindowAdapter and owns the rest: device and swapchain, buffers,
// images, descriptor sets, pipelines, and the scene graph itself (world.h).
// The game objects live in DemoScene; this file only attaches them to the
// world and runs the frame loop.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <optional>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vsg/all.h>

#include "demo_scene.h"
#include "lighting_check.h"
#include "poc_app.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh_instance.h"
#include "world.h"

namespace {
using namespace render_poc;

// Spawning and despawning a creature of a new kind, once per cycle of frames.
constexpr int kVisitorCycle = 120;
constexpr int kVisitorArrives = 20;
constexpr int kVisitorLeaves = 80;
// The probe target: every probed vertex's diffuse and specular, one pixel each.
constexpr int kProbeHeight = 128;

class Renderer {
public:
    Renderer(SDL_Window* window, const Options& options, DemoScene& scene);
    ~Renderer();

    // Records and presents one frame. With `readback`, the top-left
    // width x height pixels of the frame are returned as RGBA8.
    void frame(std::vector<std::uint8_t>* readback = nullptr, int width = 0, int height = 0);
    void showProbes(bool probes);

    RenderContext context;
    WorldGraph world;
    vsg::ref_ptr<vsg::Viewer> viewer = vsg::Viewer::create();
    vsg::ref_ptr<vsg::View> main_view;
    vsg::ref_ptr<vsg::View> probe_view;

private:
    std::vector<std::uint8_t> readFrame(int width, int height);

    vsg::ref_ptr<vsg::Instance> instance;
    vsg::ref_ptr<vsg::WindowAdapter> window;
    vsg::ref_ptr<vsg::Switch> views = vsg::Switch::create();
};

vsg::ref_ptr<vsg::WindowTraits> windowTraits(const Options& options)
{
    auto traits = vsg::WindowTraits::create();
    traits->width = static_cast<std::uint32_t>(options.width);
    traits->height = static_cast<std::uint32_t>(options.height);
    // The game presents unorm values; an sRGB swapchain would re-encode them.
    traits->swapchainPreferences.surfaceFormat = {VK_FORMAT_B8G8R8A8_UNORM,
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    traits->swapchainPreferences.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (options.frames) traits->swapchainPreferences.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    return traits;
}

Renderer::Renderer(SDL_Window* sdl_window, const Options& options, DemoScene& scene)
    : context(
          {static_cast<std::uint32_t>(options.width), static_cast<std::uint32_t>(options.height)}),
      world(context, scene.camera, scene.environment, scene.fog_color)
{
    auto traits = windowTraits(options);
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

    // The game's objects join the graph; nothing is copied out of them.
    for (const auto& mesh_instance : scene.instances) {
        if (mesh_instance.get() == scene.lantern)
            world.attach(*scene.lantern, *scene.herd.front(), DemoScene::kHandKey);
        else
            world.attach(*mesh_instance);
    }
    for (const auto& light : scene.lights) {
        if (light.get() == scene.lamp)
            world.attach(*light, scene.lantern);
        else
            world.attach(*light);
    }
    for (const auto& fog : scene.fogs) world.attach(*fog);

    main_view = world.createView(vsg::ViewportState::create(window->extent2D()));
    // VSG applies a view's camera viewport to its pipelines, so the probes get
    // their own view with a probe-sized viewport.
    probe_view = world.createView(vsg::ViewportState::create(
        0, 0, static_cast<std::uint32_t>(ProbePlan{}.width), kProbeHeight));
    // Both views are on while the viewer sets up its compile contexts, which
    // only visit enabled views.
    views->addChild(true, main_view);
    views->addChild(true, probe_view);
    auto render_graph = vsg::RenderGraph::create(window);
    render_graph->addChild(views);
    const auto& clear = scene.clear_color;
    render_graph->setClearValues({{clear.r, clear.g, clear.b, clear.a}}, {1.0f, 0});
    auto command_graph = vsg::CommandGraph::create(window);
    command_graph->addChild(render_graph);
    viewer->assignRecordAndSubmitTaskAndPresentation({command_graph});
    viewer->compile();
    showProbes(false);
}

Renderer::~Renderer()
{
    viewer->deviceWaitIdle();
}

void Renderer::showProbes(bool probes)
{
    views->children[0].mask = vsg::boolToMask(!probes);
    views->children[1].mask = vsg::boolToMask(probes);
}

void Renderer::frame(std::vector<std::uint8_t>* readback, int width, int height)
{
    world.compile(*viewer);
    if (!viewer->advanceToNextFrame()) throw std::runtime_error("VSG viewer closed");
    viewer->update();
    viewer->recordAndSubmit();
    if (readback) {
        viewer->deviceWaitIdle();
        *readback = readFrame(width, height);
    }
    viewer->present();
}

std::vector<std::uint8_t> Renderer::readFrame(int width, int height)
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

WorldViewState& viewState(vsg::View& view)
{
    return static_cast<WorldViewState&>(*view.viewDependentState);
}

// GPU lighting against shadeVertices, over what the probe view saw.
bool verifyLighting(Renderer& renderer, DemoScene& scene)
{
    auto& probes = viewState(*renderer.probe_view);
    probes.probing = true;
    renderer.showProbes(true);
    renderer.frame(); // collects the visible instances and the view's lights
    auto plan =
        planProbes(probes.probe_sources, probes.lights, renderer.world.fogs, scene.environment);
    if (plan.height > kProbeHeight) throw std::length_error("Probe target too small");
    plan.height = kProbeHeight;
    renderer.world.setProbePlan(probes, plan, *renderer.viewer);
    std::vector<std::uint8_t> pixels;
    renderer.frame(&pixels, plan.width, plan.height);
    renderer.showProbes(false);
    const auto report = compareProbes(plan, pixels);
    std::printf("VulkanSceneGraph vertex stage vs shadeVertices: %s, %zu lights collected\n",
                report.summary.c_str(), probes.lights.size());
    return report.passed();
}

// Picks at the screen position of an instance's origin.
WorldGraph::Pick pickAt(Renderer& renderer, const DemoScene& scene, const MeshInstance& target,
                        const Options& options, double* target_distance)
{
    const auto camera = cameraMatrices(scene.camera);
    const auto world_at = glm::vec4(target.transform.matrix<glm::mat4>()[3]);
    const auto eye = camera.view * world_at;
    *target_distance = glm::length(glm::vec3(eye));
    auto clip = camera.projection * eye;
    clip /= clip.w;
    // Window y points down; the game's projection has y up.
    const int x = static_cast<int>((clip.x * .5f + .5f) * float(options.width));
    const int y = static_cast<int>((.5f - clip.y * .5f) * float(options.height));
    return renderer.world.pick(*renderer.main_view->camera, x, y);
}

bool verifyPicking(Renderer& renderer, const DemoScene& scene, const Options& options)
{
    // Each creature is picked at its centre unless something nearer covers
    // it; the lantern is RENDER_NO_PICK and never picked.
    int direct = 0;
    bool passed = true;
    for (const auto* creature : scene.herd) {
        double distance = 0;
        const auto picked = pickAt(renderer, scene, *creature, options, &distance);
        if (picked.instance == creature)
            ++direct;
        else
            passed &= picked.instance && picked.distance < distance;
    }
    double lantern_distance = 0;
    const auto at_lantern = pickAt(renderer, scene, *scene.lantern, options, &lantern_distance);
    passed &= at_lantern.instance != scene.lantern && direct > 0;
    std::printf("Picking: %d of %zu creatures picked at their centre, the rest behind nearer "
                "instances; the lantern's centre picks %s: %s\n",
                direct, scene.herd.size(),
                at_lantern.instance ? at_lantern.instance->getName().c_str() : "nothing",
                passed ? "ok" : "FAILED");
    return passed;
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
        DemoScene scene(options.herd);
        Renderer renderer(window, options, scene);
        if (options.verify) {
            scene.update(options.time.value_or(1.3));
            if (!verifyLighting(renderer, scene)) status = 1;
            if (!verifyPicking(renderer, scene, options)) status = 1;
        }

        FrameClock clock;
        MeshInstance* visitor = nullptr;
        std::size_t drawn = 0;
        int frame = 0;
        for (; !options.frames || frame < *options.frames; ++frame) {
            bool quit = false;
            for (SDL_Event event; SDL_PollEvent(&event);) {
                quit |= event.type == SDL_EVENT_QUIT ||
                        (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE);
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    const auto picked = renderer.world.pick(
                        *renderer.main_view->camera, int(event.button.x), int(event.button.y));
                    std::printf("Picked %s\n",
                                picked.instance ? picked.instance->getName().c_str() : "nothing");
                }
            }
            if (quit) break;
            scene.update(clock.sceneTime(options));
            // A creature of a new kind arrives and leaves while the world runs.
            if (frame % kVisitorCycle == kVisitorArrives) {
                visitor = &scene.addVisitor();
                renderer.world.attach(*visitor);
            } else if (frame % kVisitorCycle == kVisitorLeaves && visitor) {
                renderer.world.detach(*visitor);
                scene.removeVisitor(*visitor); // safe at once: the world keeps GPU resources
                visitor = nullptr;
            }
            const bool last = options.frames && frame + 1 == *options.frames;
            std::vector<std::uint8_t> pixels;
            const bool capture = last && !options.screenshot.empty();
            renderer.frame(capture ? &pixels : nullptr, options.width, options.height);
            if (capture) savePng(options.screenshot, pixels, options.width, options.height, false);
            drawn += viewState(*renderer.main_view).drawn_instances;
            clock.frameDone();
        }
        std::printf("VulkanSceneGraph: %s; %zu instances attached, %.1f drawn per frame after "
                    "culling, %zu meshes resident\n",
                    clock.summary().c_str(), renderer.world.instanceCount(),
                    frame ? double(drawn) / frame : 0.0, renderer.world.assetCount());
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
