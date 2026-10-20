// OpenSceneGraph prototype.
//
// SDL3 owns the window, events and the OpenGL 3.3 core context. OSG renders into
// that context through GraphicsWindowEmbedded and owns everything else that the
// native GpuRenderer, mesh batches and vertex streams do today: transforms,
// vertex/index buffer objects and VAOs, textures, GLSL programs, uniforms,
// depth/blend state and the draw traversal. The Wizardry-specific parts are the
// shared shaders (the GLSL port of shadeVertices) and the per-frame plan.

#include <array>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <span>
#include <vector>

#include <SDL3/SDL.h>
#include <glm/gtc/type_ptr.hpp>
#include <osg/BlendFunc>
#include <osg/Camera>
#include <osg/ColorMask>
#include <osg/Depth>
#include <osg/DisplaySettings>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/MatrixTransform>
#include <osg/Program>
#include <osg/Texture2D>
#include <osgViewer/Viewer>

#include "demo_scene.h"
#include "lighting_check.h"
#include "poc_app.h"
#include "resident_mesh.h"
#include "shading_block.h"
#include "wiz8/render/texture.h"

namespace {
using namespace render_poc;

constexpr const char* vertex_main = R"(
uniform mat4 osg_ModelViewMatrix;
uniform mat4 osg_ProjectionMatrix;
in vec4 a_index;
in vec4 a_texcoords;
in vec4 a_color;
in vec4 a_diffuse_multiplier;
in vec4 a_specular_multiplier;
out vec4 v_diffuse;
out vec4 v_specular;
out vec2 v_st0;

void main()
{
    // GpuRenderer's normal matrix: the model-view basis without scaling.
    mat3 basis = mat3(osg_ModelViewMatrix);
    basis[0] = normalize(basis[0]);
    basis[1] = normalize(basis[1]);
    basis[2] = normalize(basis[2]);
    WzVertex shaded = wzShadeVertex(osg_ModelViewMatrix, transpose(inverse(basis)), a_index,
                                    a_texcoords, a_color, a_diffuse_multiplier,
                                    a_specular_multiplier);
    if (wzProbing()) {
        gl_Position = wzProbePosition(gl_VertexID);
        v_diffuse = wzProbeValue(shaded);
        return;
    }
    gl_Position = osg_ProjectionMatrix * shaded.eye;
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

osg::ref_ptr<osg::Program> makeProgram()
{
    osg::ref_ptr<osg::Program> program = new osg::Program;
    program->addShader(new osg::Shader(
        osg::Shader::VERTEX, hostPrelude() + std::string(wizardryVertexGlsl()) + vertex_main));
    program->addShader(
        new osg::Shader(osg::Shader::FRAGMENT,
                        hostPrelude() + std::string(wizardryFragmentGlsl()) + fragment_main));
    const std::array names{"a_index", "a_texcoords", "a_color", "a_diffuse_multiplier",
                           "a_specular_multiplier"};
    for (unsigned location = 0; location < names.size(); ++location)
        program->addBindAttribLocation(names[location], location + 1);
    return program;
}

template<std::size_t N> void setArray(osg::Uniform& uniform, const std::array<glm::vec4, N>& values)
{
    for (unsigned index = 0; index < N; ++index)
        uniform.setElement(
            index, osg::Vec4(values[index].x, values[index].y, values[index].z, values[index].w));
}

osg::Matrixd toOsg(const glm::mat4& matrix)
{
    // Both store columns of the column-vector form contiguously.
    return osg::Matrixd(glm::value_ptr(matrix));
}

osg::ref_ptr<osg::StateSet> rasterStateSet(std::uint32_t shader)
{
    static constexpr std::array compare{
        osg::Depth::NEVER,   osg::Depth::LESS,     osg::Depth::EQUAL,  osg::Depth::LEQUAL,
        osg::Depth::GREATER, osg::Depth::NOTEQUAL, osg::Depth::GEQUAL, osg::Depth::ALWAYS};
    static constexpr std::array source{GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
    static constexpr std::array destination{
        GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
        GL_ONE,  GL_ONE};
    const auto raster = rasterState(shader);
    osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
    state->setAttributeAndModes(
        new osg::Depth(compare[raster.depth_compare], 0, 1, raster.depth_write),
        raster.depth_test ? osg::StateAttribute::ON : osg::StateAttribute::OFF);
    state->setAttributeAndModes(
        new osg::BlendFunc(source[raster.source], destination[raster.destination]),
        raster.blend ? osg::StateAttribute::ON : osg::StateAttribute::OFF);
    const bool write = raster.color_write;
    state->setAttribute(new osg::ColorMask(write, write, write, write));
    return state;
}

class OsgRenderer {
public:
    OsgRenderer(DemoScene& scene, int width, int height);
    void render(const FramePlan& plan);
    std::vector<std::uint8_t> readPixels(int width, int height);
    ProbeReport verify(const FramePlan& plan, const ProbePlan& probes);

private:
    struct PassNodes {
        std::array<osg::ref_ptr<osg::Vec4Array>, 5> attributes;
        osg::ref_ptr<osg::StateSet> state; // geometry texture and shading uniform
        osg::ref_ptr<osg::Uniform> shading;
        std::vector<osg::ref_ptr<osg::StateSet>> batch_states;
        std::vector<osg::ref_ptr<osg::Uniform>> batch_draws;
        osg::ref_ptr<osg::Group> opaque;
    };
    struct InstanceNodes {
        osg::ref_ptr<osg::MatrixTransform> transform;
        std::vector<PassNodes> passes;
    };

    osg::ref_ptr<osg::Texture2D> uploadTexture(Texture* texture);
    osg::ref_ptr<osg::Geometry> geometryFor(const PassNodes& pass, osg::PrimitiveSet* primitives);
    osg::ref_ptr<osg::Node> passNode(const glm::mat4& model, const PassNodes& pass,
                                     osg::Node* child);

    osgViewer::Viewer viewer;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> window;
    osg::ref_ptr<osg::Group> root = new osg::Group;
    osg::ref_ptr<osg::Group> opaque = new osg::Group;
    osg::ref_ptr<osg::Group> transparent = new osg::Group;
    osg::ref_ptr<osg::Uniform> fragment_texture = new osg::Uniform("wz_texture", 1);
    std::map<const DemoInstance*, InstanceNodes> instances;
    std::map<Texture*, osg::ref_ptr<osg::Texture2D>> textures;
};

OsgRenderer::OsgRenderer(DemoScene& scene, int width, int height)
{
    // Core profiles need vertex array objects.
    osg::DisplaySettings::instance()->setVertexBufferHint(
        osg::DisplaySettings::VERTEX_ARRAY_OBJECT);
    window = viewer.setUpViewerAsEmbeddedInWindow(0, 0, width, height);
    window->getState()->setUseModelViewAndProjectionUniforms(true);
    viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
    viewer.setCameraManipulator(nullptr);
    auto* camera = viewer.getCamera();
    // Visibility follows the game's rules, not OSG's automatic near/far fitting
    // or small-feature culling: the plan already decided what is drawn.
    camera->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
    camera->setCullingMode(osg::CullSettings::NO_CULLING);

    auto* state = root->getOrCreateStateSet();
    state->setAttributeAndModes(makeProgram());
    state->addUniform(new osg::Uniform("wz_geometry", 0));
    state->addUniform(fragment_texture);
    // Keep the game's submission order: opaque meshes in scene order, then the
    // globally sorted translucent runs. OSG's default bins would regroup by state.
    state->setRenderBinDetails(0, "TraversalOrderBin");
    root->addChild(opaque);
    root->addChild(transparent);

    for (const auto& instance : scene.instances) {
        auto& nodes = instances[&instance];
        nodes.transform = new osg::MatrixTransform;
        const auto& resident = instance.resident;
        osg::ref_ptr<osg::Image> image = new osg::Image;
        image->setImage(resident.geometry.width, resident.geometry.height, 1, GL_RGBA32F_ARB,
                        GL_RGBA, GL_FLOAT,
                        reinterpret_cast<unsigned char*>(
                            const_cast<glm::vec4*>(resident.geometry.texels.data())),
                        osg::Image::NO_DELETE);
        osg::ref_ptr<osg::Texture2D> geometry = new osg::Texture2D(image);
        geometry->setInternalFormat(GL_RGBA32F_ARB);
        geometry->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
        geometry->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
        geometry->setResizeNonPowerOfTwoHint(false);
        geometry->setUnRefImageDataAfterApply(false);

        for (std::size_t pass_index = 0; pass_index < resident.passes.size(); ++pass_index) {
            const auto& pass = resident.passes[pass_index];
            PassNodes nodes_pass;
            for (auto& array : nodes_pass.attributes) array = new osg::Vec4Array;
            for (const auto& vertex : pass.vertices) {
                const std::array values{vertex.index_material_alpha, vertex.texcoords, vertex.color,
                                        vertex.diffuse_multiplier, vertex.specular_multiplier};
                for (std::size_t attribute = 0; attribute < values.size(); ++attribute)
                    nodes_pass.attributes[attribute]->push_back(
                        osg::Vec4(values[attribute].x, values[attribute].y, values[attribute].z,
                                  values[attribute].w));
            }
            nodes_pass.state = new osg::StateSet;
            nodes_pass.state->setTextureAttribute(0, geometry);
            nodes_pass.shading =
                new osg::Uniform(osg::Uniform::FLOAT_VEC4, "wz_shading", kShadingVec4s);
            nodes_pass.state->addUniform(nodes_pass.shading);
            nodes_pass.opaque = new osg::Group;
            nodes_pass.opaque->setStateSet(nodes_pass.state);
            for (const auto& batch : pass.batches) {
                auto batch_state = rasterStateSet(batch.shader.value);
                if (batch.texture)
                    batch_state->setTextureAttribute(1, uploadTexture(batch.texture));
                osg::ref_ptr<osg::Uniform> draw =
                    new osg::Uniform(osg::Uniform::FLOAT_VEC4, "wz_draw", kDrawVec4s);
                batch_state->addUniform(draw);
                nodes_pass.batch_states.push_back(batch_state);
                nodes_pass.batch_draws.push_back(draw);
                if (resident.sorted) continue;
                auto* elements = new osg::DrawElementsUInt(
                    GL_TRIANGLES, pass.indices.begin() + batch.first_index,
                    pass.indices.begin() + batch.first_index + batch.index_count);
                auto geometry_node = geometryFor(nodes_pass, elements);
                geometry_node->setStateSet(batch_state);
                nodes_pass.opaque->addChild(geometry_node);
            }
            if (!resident.sorted) nodes.transform->addChild(nodes_pass.opaque);
            nodes.passes.push_back(std::move(nodes_pass));
        }
        if (!resident.sorted) opaque->addChild(nodes.transform);
    }
    viewer.setSceneData(root);
    viewer.realize();
}

osg::ref_ptr<osg::Texture2D> OsgRenderer::uploadTexture(Texture* texture)
{
    auto& uploaded = textures[texture];
    if (uploaded) return uploaded;
    auto* surface = texture->getSurface();
    const auto pixels = rgba8Pixels(*surface);
    osg::ref_ptr<osg::Image> image = new osg::Image;
    auto* data = new unsigned char[pixels.size()];
    std::copy(pixels.begin(), pixels.end(), data);
    image->setImage(surface->w, surface->h, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data,
                    osg::Image::USE_NEW_DELETE);
    uploaded = new osg::Texture2D(image);
    uploaded->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
    uploaded->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
    uploaded->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
    uploaded->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
    return uploaded;
}

osg::ref_ptr<osg::Geometry> OsgRenderer::geometryFor(const PassNodes& pass,
                                                     osg::PrimitiveSet* primitives)
{
    // Geometries of one pass share their arrays, and so their buffer objects.
    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;
    geometry->setUseDisplayList(false);
    geometry->setUseVertexBufferObjects(true);
    for (unsigned attribute = 0; attribute < pass.attributes.size(); ++attribute)
        geometry->setVertexAttribArray(attribute + 1, pass.attributes[attribute],
                                       osg::Array::BIND_PER_VERTEX);
    geometry->addPrimitiveSet(primitives);
    return geometry;
}

osg::ref_ptr<osg::Node> OsgRenderer::passNode(const glm::mat4& model, const PassNodes& pass,
                                              osg::Node* child)
{
    osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform(toOsg(model));
    osg::ref_ptr<osg::Group> group = new osg::Group;
    group->setStateSet(pass.state);
    group->addChild(child);
    transform->addChild(group);
    return transform;
}

void OsgRenderer::render(const FramePlan& plan)
{
    auto* camera = viewer.getCamera();
    camera->setViewMatrix(toOsg(plan.camera.view));
    camera->setProjectionMatrix(toOsg(plan.camera.projection));
    const auto& clear = plan.clear_color;
    camera->setClearColor(osg::Vec4(clear.r, clear.g, clear.b, clear.a));

    const auto update = [&](const PlannedDraw& draw) {
        auto& nodes = instances.at(draw.instance);
        nodes.transform->setMatrix(toOsg(draw.instance->transform.matrix<glm::mat4>()));
        for (std::size_t pass = 0; pass < nodes.passes.size(); ++pass) {
            setArray(*nodes.passes[pass].shading, draw.shading[pass]);
            for (std::size_t batch = 0; batch < nodes.passes[pass].batch_draws.size(); ++batch)
                setArray(*nodes.passes[pass].batch_draws[batch], draw.draw_blocks[pass][batch]);
        }
    };
    for (const auto& draw : plan.opaque) update(draw);
    for (const auto& draw : plan.sorted) update(draw);

    // The translucent runs: one node per run, reusing the batch state sets.
    transparent->removeChildren(0, transparent->getNumChildren());
    for (const auto& run : plan.transparent.runs) {
        const auto& draw = plan.sorted[run.draw];
        const auto& pass = instances.at(draw.instance).passes[run.pass];
        const auto first = plan.transparent.indices.begin() + run.first_index;
        auto geometry = geometryFor(
            pass, new osg::DrawElementsUInt(GL_TRIANGLES, first, first + run.index_count));
        geometry->setStateSet(pass.batch_states[run.batch]);
        transparent->addChild(
            passNode(draw.instance->transform.matrix<glm::mat4>(), pass, geometry));
    }
    viewer.frame();
}

std::vector<std::uint8_t> OsgRenderer::readPixels(int width, int height)
{
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

ProbeReport OsgRenderer::verify(const FramePlan& plan, const ProbePlan& probes)
{
    render(plan);
    // A pre-render camera draws every probe point into an image OSG reads back.
    osg::ref_ptr<osg::Image> image = new osg::Image;
    image->allocateImage(probes.width, probes.height, 1, GL_RGBA, GL_UNSIGNED_BYTE);
    osg::ref_ptr<osg::Camera> camera = new osg::Camera;
    camera->setRenderOrder(osg::Camera::PRE_RENDER);
    camera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
    camera->setViewport(0, 0, probes.width, probes.height);
    camera->setClearColor(osg::Vec4(0, 0, 0, 0));
    camera->attach(osg::Camera::COLOR_BUFFER, image.get());
    camera->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF |
                                                              osg::StateAttribute::OVERRIDE);
    camera->getOrCreateStateSet()->setMode(GL_BLEND, osg::StateAttribute::OFF |
                                                         osg::StateAttribute::OVERRIDE);
    for (const auto& probe : probes.cases) {
        const auto& pass = instances.at(probe.draw->instance).passes[probe.pass];
        const auto count = static_cast<int>(probe.expected_diffuse.size());
        for (int mode = 1; mode <= 2; ++mode) {
            auto geometry = geometryFor(pass, new osg::DrawArrays(GL_POINTS, 0, count));
            osg::ref_ptr<osg::Uniform> draw =
                new osg::Uniform(osg::Uniform::FLOAT_VEC4, "wz_draw", kDrawVec4s);
            setArray(*draw, probeDrawBlock(probes, probe, mode));
            geometry->getOrCreateStateSet()->addUniform(draw);
            camera->addChild(
                passNode(probe.draw->instance->transform.matrix<glm::mat4>(), pass, geometry));
        }
    }
    root->addChild(camera);
    viewer.frame();
    root->removeChild(camera);
    const std::span pixels(image->data(), image->getTotalSizeInBytes());
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
    auto* window =
        SDL_CreateWindow("Whizzardry - OpenSceneGraph", options.width, options.height, flags);
    if (!window) throw std::runtime_error(SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    if (!context) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetSwapInterval(options.frames ? 0 : 1);

    int status = 0;
    {
        DemoScene scene;
        OsgRenderer renderer(scene, options.width, options.height);
        if (options.verify) {
            scene.update(options.time.value_or(1.3));
            const auto plan = planFrame(scene);
            const auto report = renderer.verify(plan, planProbes(scene, plan));
            std::printf("OpenSceneGraph vertex stage vs shadeVertices: %s\n",
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
            renderer.render(planFrame(scene));
            const bool last = options.frames && frame + 1 == *options.frames;
            if (last && !options.screenshot.empty())
                savePng(options.screenshot, renderer.readPixels(options.width, options.height),
                        options.width, options.height, true);
            SDL_GL_SwapWindow(window);
            clock.frameDone();
        }
        std::printf("OpenSceneGraph: %s\n", clock.summary().c_str());
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
