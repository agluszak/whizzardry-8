// OGRE prototype (classic OGRE 14).
//
// SDL3 owns the window, its events and the OpenGL 3.3 core context. OGRE's
// GL3Plus render system adopts the current context ("currentGLContext") and
// leaves buffer swaps to SDL ("externalGLControl"). OGRE owns the hardware
// vertex/index buffers, textures, GLSL programs, materials and passes with
// their render state, the render queue and the render system. The game feeds
// the queue through one custom MovableObject whose renderables are queued in
// the game's draw order.

#include <array>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Ogre.h>
#include <OgreGL3PlusPlugin.h>
#include <SDL3/SDL.h>

#include "demo_scene.h"
#include "lighting_check.h"
#include "poc_app.h"
#include "resident_mesh.h"
#include "shading_block.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/texture.h"

namespace {
using namespace render_poc;

constexpr const char* vertex_main = R"(
uniform mat4 u_model_view;
uniform mat4 u_projection;
// OGRE binds VES_TEXTURE_COORDINATES i to the attribute named uv<i>.
in vec4 uv0;
in vec4 uv1;
in vec4 uv2;
in vec4 uv3;
in vec4 uv4;
out vec4 v_diffuse;
out vec4 v_specular;
out vec2 v_st0;

void main()
{
    // GpuRenderer's normal matrix: the model-view basis without scaling.
    mat3 basis = mat3(u_model_view);
    basis[0] = normalize(basis[0]);
    basis[1] = normalize(basis[1]);
    basis[2] = normalize(basis[2]);
    WzVertex shaded = wzShadeVertex(u_model_view, transpose(inverse(basis)), uv0, uv1, uv2, uv3,
                                    uv4);
    if (wzProbing()) {
        gl_Position = wzProbePosition(gl_VertexID);
        v_diffuse = wzProbeValue(shaded);
        return;
    }
    gl_Position = u_projection * shaded.eye;
    v_diffuse = shaded.diffuse;
    v_specular = shaded.specular;
    v_st0 = shaded.st0;
}
)";

constexpr const char* fragment_main = R"(
uniform sampler2D wz_texture;
in vec4 v_diffuse;
in vec4 v_specular;
in vec2 v_st0;
out vec4 out_color;

void main()
{
    if (wzProbing()) {
        out_color = clamp(v_diffuse, 0.0, 1.0);
        return;
    }
    vec4 color = wzCombine(texture(wz_texture, v_st0), v_diffuse, v_specular);
    if (wzAlphaTest() && color.a <= 0.0) discard;
    out_color = color;
}
)";

std::string hostPrelude()
{
    return "#version 330 core\n" + glslLayoutDefinitions() + R"(
uniform vec4 wz_shading[WZ_SHADING_VEC4S];
uniform vec4 wz_draw[WZ_DRAW_VEC4S];
uniform sampler2D wz_geometry;
vec4 wzShading(int index) { return wz_shading[index]; }
vec4 wzDraw(int index) { return wz_draw[index]; }
vec4 wzGeometry(int texel)
{
    return texelFetch(wz_geometry, ivec2(texel % WZ_GEOMETRY_WIDTH, texel / WZ_GEOMETRY_WIDTH), 0);
}
)";
}

Ogre::Matrix4 toOgre(const glm::mat4& matrix)
{
    // OGRE stores rows; glm stores columns.
    Ogre::Matrix4 result;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) result[row][column] = matrix[column][row];
    return result;
}

template<std::size_t N>
void setBlock(Ogre::Pass& pass, const char* name, const std::array<glm::vec4, N>& values)
{
    const auto* data = &values[0].x;
    pass.getVertexProgramParameters()->setNamedConstant(name, data, N, 4);
    if (std::string_view(name) == "wz_draw")
        pass.getFragmentProgramParameters()->setNamedConstant(name, data, N, 4);
}

// A draw of index range or vertex range with its material and world transform.
class Draw : public Ogre::Renderable {
public:
    Ogre::MaterialPtr material;
    Ogre::RenderOperation operation;
    std::unique_ptr<Ogre::IndexData> indices;
    Ogre::Matrix4 world = Ogre::Matrix4::IDENTITY;

    const Ogre::MaterialPtr& getMaterial() const override { return material; }
    void getRenderOperation(Ogre::RenderOperation& out) override { out = operation; }
    void getWorldTransforms(Ogre::Matrix4* out) const override { *out = world; }
    Ogre::Real getSquaredViewDepth(const Ogre::Camera*) const override { return 0; }
    const Ogre::LightList& getLights() const override
    {
        static const Ogre::LightList none;
        return none;
    }
};

// Queues its draws in the order given. One renderable per priority keeps the
// game's order: OGRE otherwise groups solids by pass and sorts transparents
// by object depth, which cannot interleave triangles of different meshes.
class OrderedDraws : public Ogre::MovableObject {
public:
    explicit OrderedDraws(const std::string& name) : Ogre::MovableObject(name) {}
    std::vector<Draw*> draws;

    const Ogre::String& getMovableType() const override
    {
        static const Ogre::String type = "WizardryOrderedDraws";
        return type;
    }
    const Ogre::AxisAlignedBox& getBoundingBox() const override
    {
        return Ogre::AxisAlignedBox::BOX_INFINITE;
    }
    Ogre::Real getBoundingRadius() const override { return 0; }
    void _updateRenderQueue(Ogre::RenderQueue* queue) override
    {
        Ogre::ushort priority = 0;
        for (auto* draw : draws) queue->addRenderable(draw, Ogre::RENDER_QUEUE_MAIN, priority++);
    }
    void visitRenderables(Ogre::Renderable::Visitor* visitor, bool) override
    {
        for (auto* draw : draws) visitor->visit(draw, 0, false);
    }
};

class OgreRenderer {
public:
    OgreRenderer(DemoScene& scene, int width, int height);
    ~OgreRenderer();
    void render(const FramePlan& plan);
    std::vector<std::uint8_t> readPixels();
    ProbeReport verify(const FramePlan& plan, const ProbePlan& probes);

private:
    struct PassResources {
        Ogre::VertexData vertices;
        Ogre::HardwareIndexBufferSharedPtr indices;
        std::vector<Ogre::MaterialPtr> materials; // one per batch
        std::vector<std::unique_ptr<Draw>> draws; // opaque batches
    };
    struct InstanceResources {
        Ogre::TexturePtr geometry;
        std::vector<std::unique_ptr<PassResources>> passes;
    };

    Ogre::MaterialPtr makeMaterial(std::uint32_t shader, const Ogre::TexturePtr& geometry,
                                   const Ogre::TexturePtr& texture);
    Ogre::TexturePtr texture(Texture* source);
    std::unique_ptr<Draw> indexedDraw(PassResources& pass,
                                      const Ogre::HardwareIndexBufferSharedPtr& buffer,
                                      std::uint32_t first, std::uint32_t count);

    std::unique_ptr<Ogre::LogManager> log = std::make_unique<Ogre::LogManager>();
    std::unique_ptr<Ogre::Root> root;
    std::unique_ptr<Ogre::GL3PlusPlugin> plugin = std::make_unique<Ogre::GL3PlusPlugin>();
    Ogre::RenderWindow* window = nullptr;
    Ogre::SceneManager* scene_manager = nullptr;
    Ogre::Camera* camera = nullptr;
    std::unique_ptr<OrderedDraws> ordered = std::make_unique<OrderedDraws>("ordered");
    Ogre::HardwareIndexBufferSharedPtr ordered_indices;
    std::vector<std::unique_ptr<Draw>> runs;
    std::map<const DemoInstance*, InstanceResources> instances;
    std::map<Texture*, Ogre::TexturePtr> textures;
    Ogre::TexturePtr white;
    int width;
    int height;
    int unique = 0;
};

OgreRenderer::OgreRenderer(DemoScene& scene, int width, int height) : width(width), height(height)
{
    log->createLog("", true, false, true); // no log file, no console spam
    root = std::make_unique<Ogre::Root>("", "", "");
    root->installPlugin(plugin.get());
    root->setRenderSystem(root->getAvailableRenderers().front());
    root->initialise(false);
    const Ogre::NameValuePairList parameters{{"currentGLContext", "true"},
                                             {"externalGLControl", "true"}};
    window = root->createRenderWindow("Whizzardry", width, height, false, &parameters);
    Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();

    auto& programs = Ogre::HighLevelGpuProgramManager::getSingleton();
    auto vertex = programs.createProgram("wizardry_vertex", Ogre::RGN_DEFAULT, "glsl",
                                         Ogre::GPT_VERTEX_PROGRAM);
    vertex->setSource(hostPrelude() + std::string(wizardryVertexGlsl()) + vertex_main);
    auto fragment = programs.createProgram("wizardry_fragment", Ogre::RGN_DEFAULT, "glsl",
                                           Ogre::GPT_FRAGMENT_PROGRAM);
    fragment->setSource(hostPrelude() + std::string(wizardryFragmentGlsl()) + fragment_main);
    vertex->load();
    fragment->load();

    scene_manager = root->createSceneManager();
    camera = scene_manager->createCamera("camera");
    scene_manager->getRootSceneNode()->attachObject(camera);
    scene_manager->getRootSceneNode()->attachObject(ordered.get());
    auto* viewport = window->addViewport(camera);
    const auto& clear = scene.clear_color;
    viewport->setBackgroundColour(Ogre::ColourValue(clear.r, clear.g, clear.b, clear.a));

    white = Ogre::TextureManager::getSingleton().createManual(
        "white", Ogre::RGN_DEFAULT, Ogre::TEX_TYPE_2D, 1, 1, 0, Ogre::PF_BYTE_RGBA);
    const std::array<std::uint8_t, 4> white_pixel{255, 255, 255, 255};
    white->getBuffer()->blitFromMemory(
        Ogre::PixelBox(1, 1, 1, Ogre::PF_BYTE_RGBA, const_cast<std::uint8_t*>(white_pixel.data())));

    auto& buffers = Ogre::HardwareBufferManager::getSingleton();
    std::size_t ordered_capacity = 3;
    for (const auto& source : scene.instances) {
        const auto& resident = source.resident;
        auto& resources = instances[&source];
        resources.geometry = Ogre::TextureManager::getSingleton().createManual(
            "geometry" + std::to_string(unique++), Ogre::RGN_DEFAULT, Ogre::TEX_TYPE_2D,
            resident.geometry.width, resident.geometry.height, 0, Ogre::PF_FLOAT32_RGBA);
        resources.geometry->getBuffer()->blitFromMemory(Ogre::PixelBox(
            resident.geometry.width, resident.geometry.height, 1, Ogre::PF_FLOAT32_RGBA,
            const_cast<glm::vec4*>(resident.geometry.texels.data())));

        for (const auto& pass : resident.passes) {
            auto resources_pass = std::make_unique<PassResources>();
            auto* declaration = resources_pass->vertices.vertexDeclaration;
            for (unsigned short attribute = 0; attribute < 5; ++attribute)
                declaration->addElement(0, attribute * sizeof(glm::vec4), Ogre::VET_FLOAT4,
                                        Ogre::VES_TEXTURE_COORDINATES, attribute);
            auto vertex_buffer = buffers.createVertexBuffer(
                sizeof(ResidentVertex), pass.vertices.size(), Ogre::HBU_GPU_ONLY);
            vertex_buffer->writeData(0, pass.vertices.size() * sizeof(ResidentVertex),
                                     pass.vertices.data());
            resources_pass->vertices.vertexBufferBinding->setBinding(0, vertex_buffer);
            resources_pass->vertices.vertexCount = pass.vertices.size();
            resources_pass->indices = buffers.createIndexBuffer(
                Ogre::HardwareIndexBuffer::IT_32BIT, pass.indices.size(), Ogre::HBU_GPU_ONLY);
            resources_pass->indices->writeData(0, pass.indices.size() * sizeof(std::uint32_t),
                                               pass.indices.data());
            for (const auto& batch : pass.batches) {
                resources_pass->materials.push_back(
                    makeMaterial(batch.shader.value, resources.geometry, texture(batch.texture)));
                if (resident.sorted) continue;
                auto draw = indexedDraw(*resources_pass, resources_pass->indices, batch.first_index,
                                        batch.index_count);
                draw->material = resources_pass->materials.back();
                resources_pass->draws.push_back(std::move(draw));
            }
            if (resident.sorted) ordered_capacity += pass.indices.size();
            resources.passes.push_back(std::move(resources_pass));
        }
    }
    // One index buffer, rewritten every frame, holds the ordered translucent runs.
    ordered_indices = buffers.createIndexBuffer(Ogre::HardwareIndexBuffer::IT_32BIT,
                                                ordered_capacity, Ogre::HBU_CPU_TO_GPU);
}

OgreRenderer::~OgreRenderer()
{
    // Every OGRE resource handle must go before the root that owns the managers.
    ordered->detachFromParent();
    runs.clear();
    instances.clear();
    textures.clear();
    white.reset();
    ordered_indices.reset();
    root.reset();
}

Ogre::MaterialPtr OgreRenderer::makeMaterial(std::uint32_t shader, const Ogre::TexturePtr& geometry,
                                             const Ogre::TexturePtr& image)
{
    static constexpr std::array compare{Ogre::CMPF_ALWAYS_FAIL,   Ogre::CMPF_LESS,
                                        Ogre::CMPF_EQUAL,         Ogre::CMPF_LESS_EQUAL,
                                        Ogre::CMPF_GREATER,       Ogre::CMPF_NOT_EQUAL,
                                        Ogre::CMPF_GREATER_EQUAL, Ogre::CMPF_ALWAYS_PASS};
    static constexpr std::array source{Ogre::SBF_ZERO, Ogre::SBF_ONE, Ogre::SBF_SOURCE_ALPHA,
                                       Ogre::SBF_ONE_MINUS_SOURCE_ALPHA};
    static constexpr std::array destination{
        Ogre::SBF_ZERO,          Ogre::SBF_ONE,
        Ogre::SBF_SOURCE_COLOUR, Ogre::SBF_ONE_MINUS_SOURCE_COLOUR,
        Ogre::SBF_SOURCE_ALPHA,  Ogre::SBF_ONE_MINUS_SOURCE_ALPHA,
        Ogre::SBF_ONE,           Ogre::SBF_ONE};
    const auto raster = rasterState(shader);
    auto material = Ogre::MaterialManager::getSingleton().create(
        "wizardry" + std::to_string(unique++), Ogre::RGN_DEFAULT);
    auto* pass = material->getTechnique(0)->getPass(0);
    pass->setLightingEnabled(false);
    pass->setCullingMode(Ogre::CULL_NONE);
    pass->setManualCullingMode(Ogre::MANUAL_CULL_NONE);
    pass->setDepthCheckEnabled(raster.depth_test);
    pass->setDepthWriteEnabled(raster.depth_write);
    pass->setDepthFunction(compare[raster.depth_compare]);
    pass->setSceneBlending(source[raster.source], destination[raster.destination]);
    pass->setColourWriteEnabled(raster.color_write);
    pass->setVertexProgram("wizardry_vertex");
    pass->setFragmentProgram("wizardry_fragment");
    auto vertex = pass->getVertexProgramParameters();
    vertex->setNamedAutoConstant("u_model_view", Ogre::GpuProgramParameters::ACT_WORLDVIEW_MATRIX);
    vertex->setNamedAutoConstant("u_projection", Ogre::GpuProgramParameters::ACT_PROJECTION_MATRIX);
    auto* geometry_unit = pass->createTextureUnitState();
    geometry_unit->setTexture(geometry);
    geometry_unit->setTextureFiltering(Ogre::TFO_NONE);
    auto* image_unit = pass->createTextureUnitState();
    image_unit->setTexture(image);
    image_unit->setTextureFiltering(Ogre::TFO_TRILINEAR);
    image_unit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_WRAP);
    vertex->setNamedConstant("wz_geometry", 0);
    pass->getFragmentProgramParameters()->setNamedConstant("wz_texture", 1);
    material->load();
    return material;
}

Ogre::TexturePtr OgreRenderer::texture(Texture* source)
{
    if (!source) return white;
    auto& found = textures[source];
    if (found) return found;
    auto& surface = *source->getSurface();
    auto pixels = rgba8Pixels(surface);
    found = Ogre::TextureManager::getSingleton().createManual(
        "texture" + std::to_string(unique++), Ogre::RGN_DEFAULT, Ogre::TEX_TYPE_2D, surface.w,
        surface.h, Ogre::MIP_UNLIMITED, Ogre::PF_BYTE_RGBA, Ogre::TU_AUTOMIPMAP);
    found->getBuffer()->blitFromMemory(
        Ogre::PixelBox(surface.w, surface.h, 1, Ogre::PF_BYTE_RGBA, pixels.data()));
    return found;
}

std::unique_ptr<Draw> OgreRenderer::indexedDraw(PassResources& pass,
                                                const Ogre::HardwareIndexBufferSharedPtr& buffer,
                                                std::uint32_t first, std::uint32_t count)
{
    auto draw = std::make_unique<Draw>();
    draw->indices = std::make_unique<Ogre::IndexData>();
    draw->indices->indexBuffer = buffer;
    draw->indices->indexStart = first;
    draw->indices->indexCount = count;
    draw->operation.operationType = Ogre::RenderOperation::OT_TRIANGLE_LIST;
    draw->operation.vertexData = &pass.vertices;
    draw->operation.indexData = draw->indices.get();
    draw->operation.useIndexes = true;
    return draw;
}

void OgreRenderer::render(const FramePlan& plan)
{
    camera->setCustomViewMatrix(true, Ogre::Affine3(toOgre(plan.camera.view)));
    camera->setCustomProjectionMatrix(true, toOgre(plan.camera.projection));
    ordered->draws.clear();

    const auto update = [&](const PlannedDraw& draw) {
        auto& resources = instances.at(draw.instance);
        const auto world = toOgre(draw.instance->transform.matrix<glm::mat4>());
        for (std::size_t pass = 0; pass < resources.passes.size(); ++pass) {
            auto& resources_pass = *resources.passes[pass];
            for (std::size_t batch = 0; batch < resources_pass.materials.size(); ++batch) {
                auto& material_pass = *resources_pass.materials[batch]->getTechnique(0)->getPass(0);
                setBlock(material_pass, "wz_shading", draw.shading[pass]);
                setBlock(material_pass, "wz_draw", draw.draw_blocks[pass][batch]);
            }
            for (auto& opaque : resources_pass.draws) {
                opaque->world = world;
                ordered->draws.push_back(opaque.get());
            }
        }
    };
    for (const auto& draw : plan.opaque) update(draw);
    for (const auto& draw : plan.sorted) update(draw);

    // The globally sorted translucent runs, queued after the opaque meshes.
    ordered_indices->writeData(0, plan.transparent.indices.size() * sizeof(std::uint32_t),
                               plan.transparent.indices.data(), true);
    runs.clear();
    for (const auto& run : plan.transparent.runs) {
        const auto& draw = plan.sorted[run.draw];
        auto& pass = *instances.at(draw.instance).passes[run.pass];
        auto ordered_run = indexedDraw(pass, ordered_indices, run.first_index, run.index_count);
        ordered_run->material = pass.materials[run.batch];
        ordered_run->world = toOgre(draw.instance->transform.matrix<glm::mat4>());
        ordered->draws.push_back(ordered_run.get());
        runs.push_back(std::move(ordered_run));
    }
    root->renderOneFrame();
}

std::vector<std::uint8_t> OgreRenderer::readPixels()
{
    // OGRE returns the window rows top first.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    window->copyContentsToMemory(
        Ogre::Box(0, 0, width, height),
        Ogre::PixelBox(width, height, 1, Ogre::PF_BYTE_RGBA, pixels.data()));
    return pixels;
}

ProbeReport OgreRenderer::verify(const FramePlan& plan, const ProbePlan& probes)
{
    render(plan);
    // The probe points render into a texture through their own scene manager,
    // with point-list operations over the resident vertex buffers.
    auto* probe_scene = root->createSceneManager();
    auto* probe_camera = probe_scene->createCamera("probe");
    probe_scene->getRootSceneNode()->attachObject(probe_camera);
    OrderedDraws probe_draws("probes");
    probe_scene->getRootSceneNode()->attachObject(&probe_draws);
    constexpr std::uint32_t unblended_always =
        DrawShader::PASS_ALWAYS | DrawShader::MASK_COLOR_WRITE |
        DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT;
    std::vector<std::unique_ptr<Draw>> draws;
    for (const auto& probe : probes.cases) {
        auto& resources = instances.at(probe.draw->instance);
        auto& pass = *resources.passes[probe.pass];
        for (int mode = 1; mode <= 2; ++mode) {
            auto draw = std::make_unique<Draw>();
            draw->material = makeMaterial(unblended_always, resources.geometry, white);
            auto& material_pass = *draw->material->getTechnique(0)->getPass(0);
            setBlock(material_pass, "wz_shading", probe.draw->shading[probe.pass]);
            setBlock(material_pass, "wz_draw", probeDrawBlock(probes, probe, mode));
            draw->operation.operationType = Ogre::RenderOperation::OT_POINT_LIST;
            draw->operation.vertexData = &pass.vertices;
            draw->operation.useIndexes = false;
            draw->world = toOgre(probe.draw->instance->transform.matrix<glm::mat4>());
            probe_draws.draws.push_back(draw.get());
            draws.push_back(std::move(draw));
        }
    }
    probe_camera->setCustomViewMatrix(true, Ogre::Affine3(toOgre(plan.camera.view)));
    auto target_texture = Ogre::TextureManager::getSingleton().createManual(
        "probe", Ogre::RGN_DEFAULT, Ogre::TEX_TYPE_2D, probes.width, probes.height, 0,
        Ogre::PF_BYTE_RGBA, Ogre::TU_RENDERTARGET);
    auto* target = target_texture->getBuffer()->getRenderTarget();
    auto* viewport = target->addViewport(probe_camera);
    viewport->setBackgroundColour(Ogre::ColourValue::ZERO);
    target->update();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(probes.width) * probes.height * 4);
    target->copyContentsToMemory(
        Ogre::Box(0, 0, probes.width, probes.height),
        Ogre::PixelBox(probes.width, probes.height, 1, Ogre::PF_BYTE_RGBA, pixels.data()));
    // OGRE renders into textures upside down and flips window reads, so a
    // render-texture read already starts at clip-space y = -1, as probes expect.
    probe_scene->getRootSceneNode()->detachAllObjects();
    root->destroySceneManager(probe_scene);
    Ogre::TextureManager::getSingleton().remove(target_texture);
    return compareProbes(probes, pixels);
}

int run(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const auto flags = SDL_WINDOW_OPENGL | (options.hidden ? SDL_WINDOW_HIDDEN : 0);
    auto* window = SDL_CreateWindow("Whizzardry - OGRE", options.width, options.height, flags);
    if (!window) throw std::runtime_error(SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    if (!context) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetSwapInterval(options.frames ? 0 : 1);

    int status = 0;
    {
        DemoScene scene;
        OgreRenderer renderer(scene, options.width, options.height);
        if (options.verify) {
            scene.update(options.time.value_or(1.3));
            const auto plan = planFrame(scene);
            const auto report = renderer.verify(plan, planProbes(scene, plan));
            std::printf("OGRE vertex stage vs shadeVertices: %s\n", report.summary.c_str());
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
            renderer.render(planFrame(scene));
            const bool last = options.frames && frame + 1 == *options.frames;
            if (last && !options.screenshot.empty())
                savePng(options.screenshot, renderer.readPixels(), options.width, options.height,
                        false);
            SDL_GL_SwapWindow(window);
            clock.frameDone();
        }
        std::printf("OGRE: %s\n", clock.summary().c_str());
    }
    SDL_GL_DestroyContext(context);
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
