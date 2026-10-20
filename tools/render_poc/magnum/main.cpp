// Magnum prototype.
//
// SDL3 owns the window, its events and the OpenGL 3.3 core context; Magnum
// adopts that context through Platform::GLContext. Magnum supplies GPU objects
// (buffers, meshes with their VAOs, textures, framebuffers, programs) and the
// SceneGraph supplies transforms, the camera and drawable groups. Unlike a
// full scene renderer, render state and draw order stay in this file: each
// draw sets its DrawShader state explicitly.

#include <array>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Corrade/Containers/ArrayView.h>
#include <Corrade/Containers/StringView.h>
#include <Magnum/GL/AbstractShaderProgram.h>
#include <Magnum/GL/Buffer.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/Framebuffer.h>
#include <Magnum/GL/Mesh.h>
#include <Magnum/GL/MeshView.h>
#include <Magnum/GL/Renderbuffer.h>
#include <Magnum/GL/RenderbufferFormat.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/Shader.h>
#include <Magnum/GL/Texture.h>
#include <Magnum/GL/TextureFormat.h>
#include <Magnum/GL/Version.h>
#include <Magnum/Image.h>
#include <Magnum/ImageView.h>
#include <Magnum/Math/Color.h>
#include <Magnum/Math/Functions.h>
#include <Magnum/Math/Matrix4.h>
#include <Magnum/PixelFormat.h>
#include <Magnum/Platform/GLContext.h>
#include <Magnum/SceneGraph/Camera.h>
#include <Magnum/SceneGraph/Drawable.h>
#include <Magnum/SceneGraph/MatrixTransformation3D.h>
#include <Magnum/SceneGraph/Object.h>
#include <Magnum/SceneGraph/Scene.h>
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
using namespace Magnum;
using Object3D = SceneGraph::Object<SceneGraph::MatrixTransformation3D>;
using Scene3D = SceneGraph::Scene<SceneGraph::MatrixTransformation3D>;

Matrix4 toMagnum(const glm::mat4& matrix)
{
    const auto column = [&](int index) {
        return Vector4{matrix[index].x, matrix[index].y, matrix[index].z, matrix[index].w};
    };
    return Matrix4{column(0), column(1), column(2), column(3)};
}

template<std::size_t N>
std::vector<Math::Vector<4, Float>> toMagnum(const std::array<glm::vec4, N>& values)
{
    std::vector<Math::Vector<4, Float>> result;
    for (const auto& value : values) result.push_back(Vector4{value.x, value.y, value.z, value.w});
    return result;
}

constexpr const char* vertex_main = R"(
uniform mat4 u_model_view;
uniform mat4 u_projection;
layout(location = 0) in vec4 a_index;
layout(location = 1) in vec4 a_texcoords;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_diffuse_multiplier;
layout(location = 4) in vec4 a_specular_multiplier;
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
    WzVertex shaded = wzShadeVertex(u_model_view, transpose(inverse(basis)), a_index, a_texcoords,
                                    a_color, a_diffuse_multiplier, a_specular_multiplier);
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
    return glslLayoutDefinitions() + R"(
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

class WizardryShader : public GL::AbstractShaderProgram {
public:
    using Index = GL::Attribute<0, Vector4>;
    using Texcoords = GL::Attribute<1, Vector4>;
    using Color = GL::Attribute<2, Vector4>;
    using DiffuseMultiplier = GL::Attribute<3, Vector4>;
    using SpecularMultiplier = GL::Attribute<4, Vector4>;

    WizardryShader()
    {
        GL::Shader vertex{GL::Version::GL330, GL::Shader::Type::Vertex};
        GL::Shader fragment{GL::Version::GL330, GL::Shader::Type::Fragment};
        vertex.addSource(hostPrelude() + std::string(wizardryVertexGlsl()) + vertex_main);
        fragment.addSource(hostPrelude() + std::string(wizardryFragmentGlsl()) + fragment_main);
        if (!vertex.compile() || !fragment.compile())
            throw std::runtime_error("Wizardry shader compilation failed");
        attachShaders({vertex, fragment});
        if (!link()) throw std::runtime_error("Wizardry shader link failed");
        model_view = uniformLocation("u_model_view");
        projection = uniformLocation("u_projection");
        shading = uniformLocation("wz_shading");
        draw_block = uniformLocation("wz_draw");
        setUniform(uniformLocation("wz_geometry"), 0);
        setUniform(uniformLocation("wz_texture"), 1);
    }

    WizardryShader& setTransforms(const Matrix4& model_view_matrix,
                                  const Matrix4& projection_matrix)
    {
        setUniform(model_view, model_view_matrix);
        setUniform(projection, projection_matrix);
        return *this;
    }
    WizardryShader& setShading(const std::vector<Math::Vector<4, Float>>& values)
    {
        setUniform(shading, Containers::arrayView(values.data(), values.size()));
        return *this;
    }
    WizardryShader& setDraw(const DrawBlock& values)
    {
        const auto converted = toMagnum(values);
        setUniform(draw_block, Containers::arrayView(converted.data(), converted.size()));
        return *this;
    }

private:
    Int model_view, projection, shading, draw_block;
};

// The DrawShader word as explicit GL state; Magnum keeps no state objects.
void applyRasterState(std::uint32_t shader)
{
    using GL::Renderer;
    static constexpr std::array compare{
        Renderer::DepthFunction::Never,          Renderer::DepthFunction::Less,
        Renderer::DepthFunction::Equal,          Renderer::DepthFunction::LessOrEqual,
        Renderer::DepthFunction::Greater,        Renderer::DepthFunction::NotEqual,
        Renderer::DepthFunction::GreaterOrEqual, Renderer::DepthFunction::Always};
    static constexpr std::array source{Renderer::BlendFunction::Zero, Renderer::BlendFunction::One,
                                       Renderer::BlendFunction::SourceAlpha,
                                       Renderer::BlendFunction::OneMinusSourceAlpha};
    static constexpr std::array destination{
        Renderer::BlendFunction::Zero,        Renderer::BlendFunction::One,
        Renderer::BlendFunction::SourceColor, Renderer::BlendFunction::OneMinusSourceColor,
        Renderer::BlendFunction::SourceAlpha, Renderer::BlendFunction::OneMinusSourceAlpha,
        Renderer::BlendFunction::One,         Renderer::BlendFunction::One};
    const auto raster = rasterState(shader);
    Renderer::setFeature(Renderer::Feature::DepthTest, raster.depth_test);
    Renderer::setDepthFunction(compare[raster.depth_compare]);
    Renderer::setDepthMask(raster.depth_write);
    Renderer::setFeature(Renderer::Feature::Blending, raster.blend);
    Renderer::setBlendFunction(source[raster.source], destination[raster.destination]);
    const bool write = raster.color_write;
    Renderer::setColorMask(write, write, write, write);
}

class MagnumRenderer;

// One batch of one pass of one instance, drawn by the SceneGraph camera.
class BatchDrawable : public SceneGraph::Drawable3D {
public:
    BatchDrawable(Object3D& object, SceneGraph::DrawableGroup3D& group, MagnumRenderer& renderer,
                  const DemoInstance& instance, std::uint32_t pass, std::uint32_t batch)
        : SceneGraph::Drawable3D{object, &group}, renderer(renderer), instance(instance),
          pass(pass), batch(batch)
    {
    }

private:
    void draw(const Matrix4& transformation, SceneGraph::Camera3D& camera) override;

    MagnumRenderer& renderer;
    const DemoInstance& instance;
    std::uint32_t pass;
    std::uint32_t batch;
};

class MagnumRenderer {
public:
    MagnumRenderer(DemoScene& scene, int width, int height);
    void render(const FramePlan& plan);
    std::vector<std::uint8_t> readPixels(int width, int height);
    ProbeReport verify(const FramePlan& plan, const ProbePlan& probes);

    void drawBatch(const DemoInstance& instance, std::uint32_t pass, std::uint32_t batch,
                   const Matrix4& model_view, const Matrix4& projection);

private:
    struct PassResources {
        GL::Buffer vertices;
        GL::Buffer indices;
        GL::Mesh triangles{GL::MeshPrimitive::Triangles};
        GL::Mesh points{GL::MeshPrimitive::Points};
        GL::Mesh ordered{GL::MeshPrimitive::Triangles}; // over the per-frame index buffer
        std::vector<Math::Vector<4, Float>> shading;
        std::vector<DrawBlock> draws;
    };
    struct InstanceResources {
        Object3D* object = nullptr; // owned by the scene
        GL::Texture2D geometry;
        std::vector<std::unique_ptr<PassResources>> passes;
    };

    GL::Texture2D& texture(Texture* source);
    void bindPass(InstanceResources& resources, const PassResources& pass);

    int width;
    int height;
    WizardryShader shader;
    Scene3D scene_graph;
    Object3D camera_object{&scene_graph};
    SceneGraph::Camera3D camera{camera_object};
    SceneGraph::DrawableGroup3D opaque;
    GL::Buffer ordered_indices;
    std::map<const DemoInstance*, InstanceResources> instances;
    std::map<Texture*, GL::Texture2D> textures;
    GL::Texture2D white;
    glm::vec4 clear_color{0};
};

void BatchDrawable::draw(const Matrix4& transformation, SceneGraph::Camera3D& camera)
{
    renderer.drawBatch(instance, pass, batch, transformation, camera.projectionMatrix());
}

MagnumRenderer::MagnumRenderer(DemoScene& scene, int width, int height)
    : width(width), height(height)
{
    const std::array<std::uint8_t, 4> white_pixel{255, 255, 255, 255};
    white.setStorage(1, GL::TextureFormat::RGBA8, {1, 1})
        .setSubImage(0, {},
                     ImageView2D{PixelFormat::RGBA8Unorm,
                                 {1, 1},
                                 Containers::arrayView(white_pixel.data(), white_pixel.size())});
    clear_color = scene.clear_color;

    std::size_t ordered_capacity = 0;
    for (const auto& source : scene.instances) {
        const auto& resident = source.resident;
        auto& resources = instances[&source];
        resources.object = new Object3D{&scene_graph};
        const Vector2i size{resident.geometry.width, resident.geometry.height};
        resources.geometry.setMinificationFilter(GL::SamplerFilter::Nearest)
            .setMagnificationFilter(GL::SamplerFilter::Nearest)
            .setStorage(1, GL::TextureFormat::RGBA32F, size)
            .setSubImage(0, {},
                         ImageView2D{PixelFormat::RGBA32F, size,
                                     Containers::arrayView(resident.geometry.texels.data(),
                                                           resident.geometry.texels.size() *
                                                               sizeof(glm::vec4))});
        for (std::uint32_t pass_index = 0; pass_index < resident.passes.size(); ++pass_index) {
            const auto& pass = resident.passes[pass_index];
            auto resources_pass = std::make_unique<PassResources>();
            resources_pass->vertices.setData(Containers::arrayView(
                pass.vertices.data(), pass.vertices.size() * sizeof(ResidentVertex)));
            resources_pass->indices.setData(
                Containers::arrayView(pass.indices.data(), pass.indices.size()));
            const auto attach = [&](GL::Mesh& mesh) {
                mesh.addVertexBuffer(resources_pass->vertices, 0, WizardryShader::Index{},
                                     WizardryShader::Texcoords{}, WizardryShader::Color{},
                                     WizardryShader::DiffuseMultiplier{},
                                     WizardryShader::SpecularMultiplier{});
            };
            attach(resources_pass->triangles);
            resources_pass->triangles.setIndexBuffer(resources_pass->indices, 0,
                                                     GL::MeshIndexType::UnsignedInt);
            attach(resources_pass->points);
            resources_pass->points.setCount(static_cast<Int>(pass.vertices.size()));
            attach(resources_pass->ordered);
            resources_pass->ordered.setIndexBuffer(ordered_indices, 0,
                                                   GL::MeshIndexType::UnsignedInt);
            resources_pass->draws.resize(pass.batches.size());
            for (const auto& batch : pass.batches) texture(batch.texture);
            if (resident.sorted)
                ordered_capacity += pass.indices.size();
            else
                for (std::uint32_t batch = 0; batch < pass.batches.size(); ++batch)
                    new BatchDrawable{*resources.object, opaque, *this, source, pass_index, batch};
            resources.passes.push_back(std::move(resources_pass));
        }
    }
    ordered_indices.setData(
        Containers::ArrayView<const void>{nullptr, ordered_capacity * sizeof(std::uint32_t)},
        GL::BufferUsage::DynamicDraw);
}

GL::Texture2D& MagnumRenderer::texture(Texture* source)
{
    if (!source) return white;
    auto [found, inserted] = textures.try_emplace(source);
    if (!inserted) return found->second;
    auto& surface = *source->getSurface();
    const auto pixels = rgba8Pixels(surface);
    const Vector2i size{surface.w, surface.h};
    found->second.setWrapping(GL::SamplerWrapping::Repeat)
        .setMinificationFilter(GL::SamplerFilter::Linear, GL::SamplerMipmap::Linear)
        .setMagnificationFilter(GL::SamplerFilter::Linear)
        .setStorage(Math::log2(std::max(surface.w, surface.h)) + 1, GL::TextureFormat::RGBA8, size)
        .setSubImage(0, {},
                     ImageView2D{PixelFormat::RGBA8Unorm, size,
                                 Containers::arrayView(pixels.data(), pixels.size())})
        .generateMipmap();
    return found->second;
}

void MagnumRenderer::bindPass(InstanceResources& resources, const PassResources& pass)
{
    resources.geometry.bind(0);
    shader.setShading(pass.shading);
}

void MagnumRenderer::drawBatch(const DemoInstance& instance, std::uint32_t pass,
                               std::uint32_t batch, const Matrix4& model_view,
                               const Matrix4& projection)
{
    auto& resources = instances.at(&instance);
    auto& pass_resources = *resources.passes[pass];
    const auto& source = instance.resident.passes[pass].batches[batch];
    bindPass(resources, pass_resources);
    texture(source.texture).bind(1);
    applyRasterState(source.shader.value);
    shader.setTransforms(model_view, projection).setDraw(pass_resources.draws[batch]);
    GL::MeshView view{pass_resources.triangles};
    view.setCount(static_cast<Int>(source.index_count))
        .setIndexOffset(static_cast<Int>(source.first_index));
    shader.draw(view);
}

void MagnumRenderer::render(const FramePlan& plan)
{
    camera_object.setTransformation(toMagnum(glm::inverse(plan.camera.view)));
    camera.setProjectionMatrix(toMagnum(plan.camera.projection));
    camera.setViewport({width, height});

    const auto update = [&](const PlannedDraw& draw) {
        auto& resources = instances.at(draw.instance);
        resources.object->setTransformation(toMagnum(draw.instance->transform.matrix<glm::mat4>()));
        for (std::size_t pass = 0; pass < resources.passes.size(); ++pass) {
            resources.passes[pass]->shading = toMagnum(draw.shading[pass]);
            for (std::size_t batch = 0; batch < resources.passes[pass]->draws.size(); ++batch)
                resources.passes[pass]->draws[batch] = draw.draw_blocks[pass][batch];
        }
    };
    for (const auto& draw : plan.opaque) update(draw);
    for (const auto& draw : plan.sorted) update(draw);

    GL::defaultFramebuffer.setViewport({{}, {width, height}}).bind();
    GL::Renderer::setDepthMask(true);
    GL::Renderer::setColorMask(true, true, true, true);
    GL::Renderer::setClearColor({clear_color.r, clear_color.g, clear_color.b, clear_color.a});
    GL::defaultFramebuffer.clear(GL::FramebufferClear::Color | GL::FramebufferClear::Depth);

    // Opaque meshes in scene order through the SceneGraph camera.
    camera.draw(opaque);

    // The globally sorted translucent runs, in order, from one index buffer.
    ordered_indices.setSubData(
        0, Containers::arrayView(plan.transparent.indices.data(), plan.transparent.indices.size()));
    for (const auto& run : plan.transparent.runs) {
        const auto& draw = plan.sorted[run.draw];
        auto& resources = instances.at(draw.instance);
        auto& pass = *resources.passes[run.pass];
        const auto& batch = draw.instance->resident.passes[run.pass].batches[run.batch];
        bindPass(resources, pass);
        texture(batch.texture).bind(1);
        applyRasterState(batch.shader.value);
        shader
            .setTransforms(camera.cameraMatrix() * resources.object->absoluteTransformationMatrix(),
                           camera.projectionMatrix())
            .setDraw(pass.draws[run.batch]);
        GL::MeshView view{pass.ordered};
        view.setCount(static_cast<Int>(run.index_count))
            .setIndexOffset(static_cast<Int>(run.first_index));
        shader.draw(view);
    }
}

std::vector<std::uint8_t> MagnumRenderer::readPixels(int read_width, int read_height)
{
    const auto image =
        GL::defaultFramebuffer.read({{}, {read_width, read_height}}, {PixelFormat::RGBA8Unorm});
    const auto data = image.data();
    return {data.begin(), data.end()};
}

ProbeReport MagnumRenderer::verify(const FramePlan& plan, const ProbePlan& probes)
{
    render(plan);
    const Vector2i size{probes.width, probes.height};
    GL::Renderbuffer color;
    color.setStorage(GL::RenderbufferFormat::RGBA8, size);
    GL::Framebuffer target{{{}, size}};
    target.attachRenderbuffer(GL::Framebuffer::ColorAttachment{0}, color);
    target.clearColor(0, Color4{0}).bind();
    GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
    GL::Renderer::disable(GL::Renderer::Feature::Blending);
    GL::Renderer::setColorMask(true, true, true, true);
    for (const auto& probe : probes.cases) {
        auto& resources = instances.at(probe.draw->instance);
        auto& pass = *resources.passes[probe.pass];
        bindPass(resources, pass);
        white.bind(1);
        for (int mode = 1; mode <= 2; ++mode) {
            shader.setTransforms(toMagnum(probe.draw->model_view), Matrix4{})
                .setDraw(probeDrawBlock(probes, probe, mode));
            shader.draw(pass.points);
        }
    }
    const auto image = target.read({{}, size}, {PixelFormat::RGBA8Unorm});
    GL::defaultFramebuffer.bind();
    const auto data = image.data();
    const std::span pixels(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
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
    auto* window = SDL_CreateWindow("Whizzardry - Magnum", options.width, options.height, flags);
    if (!window) throw std::runtime_error(SDL_GetError());
    auto context = SDL_GL_CreateContext(window);
    if (!context) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetSwapInterval(options.frames ? 0 : 1);

    int status = 0;
    {
        Platform::GLContext magnum_context;
        DemoScene scene;
        MagnumRenderer renderer(scene, options.width, options.height);
        if (options.verify) {
            scene.update(options.time.value_or(1.3));
            const auto plan = planFrame(scene);
            const auto report = renderer.verify(plan, planProbes(scene, plan));
            std::printf("Magnum vertex stage vs shadeVertices: %s\n", report.summary.c_str());
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
        std::printf("Magnum: %s\n", clock.summary().c_str());
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
