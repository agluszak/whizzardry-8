#include "world.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "demo_scene.h"
#include "transparent_order.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/texture.h"
#include "world_frag.spv.h"
#include "world_vert.spv.h"

namespace render_poc {
namespace {

// Nodes released by WorldGraph::detach stay alive this many frames, longer than
// any frame VSG can still have in flight.
constexpr int kReleaseDelayFrames = 4;
constexpr std::uint32_t kTranslucentIndexCapacity = 1u << 16;

vsg::dmat4 toVsg(const glm::dmat4& matrix)
{
    vsg::dmat4 result;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row) result[column][row] = matrix[column][row];
    return result;
}

glm::mat4 toGlm(const vsg::dmat4& matrix)
{
    glm::mat4 result;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result[column][row] = static_cast<float>(matrix[column][row]);
    return result;
}

// Blocks are uploaded as vec4 arrays: std140 lays out vec4 and uvec4 members
// exactly as the C++ structs do.
template<class Block> vsg::ref_ptr<vsg::vec4Array> blockArray(vsg::DataVariance variance)
{
    static_assert(sizeof(Block) % sizeof(glm::vec4) == 0);
    auto array =
        vsg::vec4Array::create(static_cast<std::uint32_t>(sizeof(Block) / sizeof(glm::vec4)));
    array->properties.dataVariance = variance;
    return array;
}

template<class Block> void store(vsg::vec4Array& array, const Block& block)
{
    std::memcpy(array.dataPointer(), &block, sizeof(Block));
    array.dirty();
}

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

// Camera3D as VSG camera inputs, read whenever VSG asks.
class GameProjection : public vsg::Inherit<vsg::ProjectionMatrix, GameProjection> {
public:
    explicit GameProjection(const Camera3D& camera) : camera(camera) {}
    vsg::dmat4 transform() const override
    {
        return toVsg(glm::dmat4(vulkanProjection(cameraMatrices(camera).projection)));
    }

private:
    const Camera3D& camera;
};

class GameView : public vsg::Inherit<vsg::ViewMatrix, GameView> {
public:
    explicit GameView(const Camera3D& camera) : camera(camera) {}
    vsg::dmat4 transform(const vsg::dvec3& = {}) const override
    {
        return toVsg(glm::dmat4(cameraMatrices(camera).view));
    }

private:
    const Camera3D& camera;
};

// Intersection support: the geometry's vertex array holds position indices
// (ResidentVertex::index_material_alpha), so the CPU-side vertices are the
// instance's current frame, gathered through them.
class FrameArrayState : public vsg::Inherit<vsg::ArrayState, FrameArrayState> {
public:
    FrameArrayState(const MeshInstance& instance, const ResidentGeometry& geometry)
        : instance(&instance), geometry(&geometry)
    {
    }
    FrameArrayState(const vsg::ArrayState& parent, const FrameArrayState& prototype)
        : Inherit(parent), instance(prototype.instance), geometry(prototype.geometry)
    {
    }

    vsg::ref_ptr<vsg::ArrayState> cloneArrayState() override
    {
        return FrameArrayState::create(*this, *this);
    }
    vsg::ref_ptr<vsg::ArrayState> cloneArrayState(vsg::ref_ptr<vsg::ArrayState> parent) override
    {
        return FrameArrayState::create(*parent, *this);
    }

    using ArrayState::apply;
    void apply(const vsg::vec4Array& indices) override
    {
        vertices = {};
        if (instance->render_flags & MeshInstance::RENDER_NO_PICK) return;
        const auto positions =
            framePositions(*geometry, instance->frame_index, instance->frame_interpolation);
        auto current = vsg::vec3Array::create(indices.size());
        for (std::uint32_t vertex = 0; vertex < indices.size(); ++vertex) {
            const auto& position = positions[static_cast<std::size_t>(indices.at(vertex).x)];
            current->at(vertex) = vsg::vec3(position.x, position.y, position.z);
        }
        vertices = current;
    }

private:
    const MeshInstance* instance;
    const ResidentGeometry* geometry;
};

void recordDraw(vsg::RecordTraversal& traversal, const vsg::StateCommands& state_commands,
                const vsg::BindVertexBuffers& vertices, const auto& draw)
{
    auto& state = *traversal.getState();
    state.push(state_commands);
    state.record();
    auto& buffer = *traversal.getCommandBuffer();
    vertices.record(buffer);
    draw(buffer);
    state.pop(state_commands);
}

} // namespace

// --- RenderContext ---------------------------------------------------------

RenderContext::RenderContext(VkExtent2D extent) : viewport(vsg::ViewportState::create(extent))
{
    using Bindings = vsg::DescriptorSetLayoutBindings;
    constexpr auto vertex = VK_SHADER_STAGE_VERTEX_BIT;
    constexpr auto fragment = VK_SHADER_STAGE_FRAGMENT_BIT;
    constexpr auto uniform = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constexpr auto sampler = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    view_layout =
        vsg::DescriptorSetLayout::create(Bindings{{0, uniform, 1, vertex | fragment, nullptr}});
    instance_layout = vsg::DescriptorSetLayout::create(Bindings{{0, uniform, 1, vertex, nullptr}});
    pass_layout = vsg::DescriptorSetLayout::create(
        Bindings{{0, uniform, 1, vertex, nullptr}, {1, sampler, 1, vertex, nullptr}});
    batch_layout = vsg::DescriptorSetLayout::create(
        Bindings{{0, uniform, 1, vertex | fragment, nullptr}, {1, sampler, 1, fragment, nullptr}});
    layout = vsg::PipelineLayout::create(
        vsg::DescriptorSetLayouts{view_layout, instance_layout, pass_layout, batch_layout},
        vsg::PushConstantRanges{{vertex, 0, 128}});
    const auto module = [](std::span<const std::uint32_t> code) {
        return vsg::ShaderModule::create(vsg::ShaderModule::SPIRV(code.begin(), code.end()));
    };
    stages = {vsg::ShaderStage::create(vertex, "main", module(world_vert)),
              vsg::ShaderStage::create(fragment, "main", module(world_frag))};

    auto white_pixel =
        vsg::ubvec4Array2D::create(1, 1, vsg::Data::Properties{VK_FORMAT_R8G8B8A8_UNORM});
    white_pixel->at(0, 0) = vsg::ubvec4(255, 255, 255, 255);
    white = vsg::ImageInfo::create(vsg::Sampler::create(), white_pixel);
}

vsg::ref_ptr<vsg::BindGraphicsPipeline> RenderContext::pipeline(std::uint32_t shader, bool points)
{
    auto& found = pipelines[{shader, points}];
    if (found) return found;
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
        viewport};
    found =
        vsg::BindGraphicsPipeline::create(vsg::GraphicsPipeline::create(layout, stages, states));
    return found;
}

vsg::ref_ptr<vsg::ImageInfo> RenderContext::texture(Texture* source)
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

vsg::ref_ptr<vsg::BindDescriptorSet> RenderContext::batchSet(const BatchBlock& block,
                                                             vsg::ref_ptr<vsg::ImageInfo> image)
{
    auto data = blockArray<BatchBlock>(vsg::STATIC_DATA);
    store(*data, block);
    return vsg::BindDescriptorSet::create(
        VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 3,
        vsg::DescriptorSet::create(batch_layout,
                                   vsg::Descriptors{vsg::DescriptorBuffer::create(data, 0),
                                                    vsg::DescriptorImage::create(image, 1)}));
}

// --- MeshAsset ---------------------------------------------------------------

MeshAsset::MeshAsset(RenderContext& context, Mesh& mesh) : resident(buildResidentMesh(mesh))
{
    auto geometry_sampler = vsg::Sampler::create();
    geometry_sampler->minFilter = geometry_sampler->magFilter = VK_FILTER_NEAREST;
    geometry_sampler->maxLod = 0;
    auto geometry = vsg::vec4Array2D::create(static_cast<std::uint32_t>(resident.geometry.width),
                                             static_cast<std::uint32_t>(resident.geometry.height),
                                             vsg::Data::Properties{VK_FORMAT_R32G32B32A32_SFLOAT});
    std::memcpy(geometry->dataPointer(), resident.geometry.texels.data(),
                resident.geometry.texels.size() * sizeof(glm::vec4));
    auto geometry_image = vsg::ImageInfo::create(geometry_sampler, geometry);

    for (const auto& source : resident.passes) {
        Pass pass;
        std::array<vsg::ref_ptr<vsg::vec4Array>, 5> attributes;
        for (auto& array : attributes)
            array = vsg::vec4Array::create(static_cast<std::uint32_t>(source.vertices.size()));
        for (std::size_t index = 0; index < source.vertices.size(); ++index) {
            const auto& vertex = source.vertices[index];
            const std::array values{vertex.index_material_alpha, vertex.texcoords, vertex.color,
                                    vertex.diffuse_multiplier, vertex.specular_multiplier};
            for (std::size_t attribute = 0; attribute < values.size(); ++attribute) {
                const auto& value = values[attribute];
                attributes[attribute]->at(index) = vsg::vec4(value.x, value.y, value.z, value.w);
            }
        }
        pass.vertices =
            vsg::BindVertexBuffers::create(0, vsg::DataList(attributes.begin(), attributes.end()));
        auto indices = vsg::uintArray::create(static_cast<std::uint32_t>(source.indices.size()));
        std::ranges::copy(source.indices, indices->begin());
        pass.indices = vsg::BindIndexBuffer::create(indices);
        pass.block = blockArray<PassBlock>(vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD);
        pass.bind = vsg::BindDescriptorSet::create(
            VK_PIPELINE_BIND_POINT_GRAPHICS, context.layout, 2,
            vsg::DescriptorSet::create(
                context.pass_layout,
                vsg::Descriptors{vsg::DescriptorBuffer::create(pass.block, 0),
                                 vsg::DescriptorImage::create(geometry_image, 1)}));
        for (const auto& batch : source.batches) {
            const std::array shaders{batch.shader.value, fadingShader(batch.shader.value)};
            Batch variants;
            for (std::size_t variant = 0; variant < shaders.size(); ++variant) {
                variants.pipeline[variant] = context.pipeline(shaders[variant]);
                variants.set[variant] =
                    context.batchSet(packBatch(shaders[variant]), context.texture(batch.texture));
            }
            pass.batches.push_back(std::move(variants));
        }
        pass.geometry = vsg::Commands::create();
        pass.geometry->addChild(pass.vertices);
        pass.geometry->addChild(pass.indices);
        pass.geometry->addChild(vsg::DrawIndexed::create(indices->size(), 1, 0, 0, 0));
        passes.push_back(std::move(pass));
    }
}

void MeshAsset::update(const vsg::FrameStamp& frame, double frame_time)
{
    if (frame.frameCount == updated_frame) return;
    updated_frame = frame.frameCount;
    for (std::size_t pass = 0; pass < passes.size(); ++pass)
        store(*passes[pass].block, packPass(resident, pass, frame_time));
}

void MeshAsset::traverse(vsg::Visitor& visitor)
{
    for (auto& pass : passes) {
        pass.bind->accept(visitor);
        for (auto& batch : pass.batches) {
            for (auto& pipeline : batch.pipeline) pipeline->accept(visitor);
            for (auto& set : batch.set) set->accept(visitor);
        }
        pass.geometry->accept(visitor);
    }
}

void MeshAsset::traverse(vsg::ConstVisitor& visitor) const
{
    for (const auto& pass : passes) {
        pass.bind->accept(visitor);
        for (const auto& batch : pass.batches) {
            for (const auto& pipeline : batch.pipeline) pipeline->accept(visitor);
            for (const auto& set : batch.set) set->accept(visitor);
        }
        pass.geometry->accept(visitor);
    }
}

// --- Transforms ----------------------------------------------------------------

InstanceTransform::InstanceTransform(const MeshInstance& instance) : instance(instance) {}

vsg::dmat4 InstanceTransform::transform(const vsg::dmat4& model_view) const
{
    return model_view * toVsg(instance.transform.matrix<glm::dmat4>());
}

VertexAttachment::VertexAttachment(const MeshInstance& instance, const ResidentMesh& mesh,
                                   int vertex)
    : instance(instance), mesh(mesh), vertex(static_cast<std::size_t>(vertex))
{
}

vsg::dmat4 VertexAttachment::transform(const vsg::dmat4& model_view) const
{
    const auto selection =
        selectFrame(mesh.geometry, instance.frame_index, instance.frame_interpolation);
    const auto at = framePosition(mesh.geometry, selection, vertex);
    return model_view * vsg::translate(vsg::dvec3(at.x, at.y, at.z));
}

// --- InstanceDrawable ----------------------------------------------------------

InstanceDrawable::InstanceDrawable(WorldGraph& world, const MeshInstance& instance,
                                   vsg::ref_ptr<MeshAsset> mesh_asset)
    : instance(instance), asset(std::move(mesh_asset)), world(world),
      block(blockArray<InstanceBlock>(vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD))
{
    bind = vsg::BindDescriptorSet::create(
        VK_PIPELINE_BIND_POINT_GRAPHICS, world.context.layout, 1,
        vsg::DescriptorSet::create(world.context.instance_layout,
                                   vsg::Descriptors{vsg::DescriptorBuffer::create(block, 0)}));
    intersection = vsg::StateGroup::create();
    intersection->prototypeArrayState = FrameArrayState::create(instance, asset->resident.geometry);
    for (const auto& pass : asset->passes) intersection->addChild(pass.geometry);
}

void InstanceDrawable::accept(vsg::RecordTraversal& traversal) const
{
    auto* view = WorldViewState::of(traversal);
    if (!view || !instance.visible) return;
    const auto& model_view = traversal.getState()->modelviewMatrixStack.top();
    store(*block, packInstance(instance, asset->resident, world.environment.ambient_light));
    asset->update(*traversal.getFrameStamp(), world.environment.frame_time);
    ++view->drawn_instances;

    if (view->probing) {
        if (view->probe_draws.empty()) {
            view->probe_sources.push_back({&instance, &asset->resident, toGlm(model_view)});
            return;
        }
        const auto [first, last] = view->probe_draws.equal_range(&instance);
        for (auto probe = first; probe != last; ++probe)
            drawProbe(traversal, probe->second.pass, probe->second.set, view->probe_pipeline);
        return;
    }
    // Translucent instances join the view's global triangle order instead.
    if (drawsSorted(instance)) {
        view->addSorted(*this, model_view);
        return;
    }
    for (std::size_t pass = 0; pass < asset->passes.size(); ++pass) {
        const auto& batches = asset->resident.passes[pass].batches;
        for (std::size_t batch = 0; batch < batches.size(); ++batch)
            draw(traversal, pass, batch, *asset->passes[pass].indices, batches[batch].first_index,
                 batches[batch].index_count);
    }
}

void InstanceDrawable::draw(vsg::RecordTraversal& traversal, std::size_t pass, std::size_t batch,
                            const vsg::BindIndexBuffer& indices, std::uint32_t first_index,
                            std::uint32_t index_count) const
{
    const auto variant = instance.diffuse_scale_enabled ? 1 : 0;
    const auto& resources = asset->passes[pass];
    const auto& state = resources.batches[batch];
    recordDraw(
        traversal,
        vsg::StateCommands{state.pipeline[variant], bind, resources.bind, state.set[variant]},
        *resources.vertices, [&](vsg::CommandBuffer& buffer) {
            indices.record(buffer);
            vkCmdDrawIndexed(buffer, index_count, 1, first_index, 0, 0);
        });
}

void InstanceDrawable::drawProbe(vsg::RecordTraversal& traversal, std::size_t pass,
                                 vsg::ref_ptr<vsg::BindDescriptorSet> probe_set,
                                 vsg::ref_ptr<vsg::BindGraphicsPipeline> points) const
{
    const auto& resources = asset->passes[pass];
    const auto count = static_cast<std::uint32_t>(asset->resident.passes[pass].vertices.size());
    recordDraw(traversal, vsg::StateCommands{points, bind, resources.bind, probe_set},
               *resources.vertices,
               [&](vsg::CommandBuffer& buffer) { vkCmdDraw(buffer, count, 1, 0, 0); });
}

void InstanceDrawable::traverse(vsg::Visitor& visitor)
{
    bind->accept(visitor);
    asset->accept(visitor);
    intersection->accept(visitor);
}

void InstanceDrawable::traverse(vsg::ConstVisitor& visitor) const
{
    // Intersection traversals see geometry only; others (resource collection)
    // see every state the drawable records with.
    if (!dynamic_cast<vsg::Intersector*>(&visitor)) {
        bind->accept(visitor);
        asset->accept(visitor);
    }
    intersection->accept(visitor);
}

// --- Lights and the translucent stage --------------------------------------------

LightNode::LightNode(Light& light) : light(light) {}

void LightNode::accept(vsg::RecordTraversal& traversal) const
{
    if (auto* view = WorldViewState::of(traversal))
        view->addLight(light, traversal.getState()->modelviewMatrixStack.top() *
                                  toVsg(light.transform.matrix<glm::dmat4>()));
}

void TranslucentStage::accept(vsg::RecordTraversal& traversal) const
{
    if (auto* view = WorldViewState::of(traversal)) view->recordTranslucent(traversal);
}

// --- WorldViewState --------------------------------------------------------------

WorldViewState::WorldViewState(vsg::View* view, WorldGraph& world)
    : Inherit(view), world(world),
      block(blockArray<ViewBlock>(vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD))
{
    descriptorSetLayout = world.context.view_layout;
    descriptorSet = vsg::DescriptorSet::create(
        descriptorSetLayout, vsg::Descriptors{vsg::DescriptorBuffer::create(block, 0)});
    translucent_indices = vsg::uintArray::create(kTranslucentIndexCapacity);
    translucent_indices->properties.dataVariance = vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD;
    bind_translucent = vsg::BindIndexBuffer::create(translucent_indices);
}

WorldViewState* WorldViewState::of(vsg::RecordTraversal& traversal)
{
    return dynamic_cast<WorldViewState*>(traversal.viewDependentState.get());
}

void WorldViewState::clear()
{
    lights.clear();
    sorted.clear();
    drawn_instances = 0;
    if (probe_draws.empty()) probe_sources.clear();
}

void WorldViewState::traverse(vsg::Visitor& visitor)
{
    descriptorSet->accept(visitor);
    bind_translucent->accept(visitor);
}

void WorldViewState::traverse(vsg::ConstVisitor& visitor) const
{
    descriptorSet->accept(visitor);
    bind_translucent->accept(visitor);
}

void WorldViewState::traverse(vsg::RecordTraversal&) const
{
    // After the view's traversal: every light has reported itself.
    store(*block, packView(world.environment, lights, world.fogs, world.fog_color));
}

void WorldViewState::addLight(Light& light, const vsg::dmat4& model_view)
{
    // WorldScene::render's filter, then Light::beginPass for this view.
    if (!light.visible || light.intensity == 0) return;
    prepareLightAt(light, toGlm(model_view));
    if (light.derived_flags & Light::DERIVED_ACTIVE) lights.push_back(&light);
}

void WorldViewState::addSorted(const InstanceDrawable& drawable, const vsg::dmat4& model_view)
{
    sorted.push_back({&drawable, model_view});
}

void WorldViewState::recordTranslucent(vsg::RecordTraversal& traversal)
{
    if (sorted.empty()) return;
    const auto projection = projectClipNear(cameraMatrices(world.camera).projection);
    std::vector<std::vector<glm::vec3>> positions;
    std::vector<SortedDraw> draws;
    positions.reserve(sorted.size());
    for (const auto& entry : sorted) {
        const auto& instance = entry.drawable->instance;
        const auto& mesh = entry.drawable->asset->resident;
        positions.push_back(
            framePositions(mesh.geometry, instance.frame_index, instance.frame_interpolation));
        draws.push_back({&mesh, toGlm(entry.model_view), projection, positions.back()});
    }
    const auto order = orderTransparent(draws);
    if (order.indices.size() > translucent_indices->size())
        throw std::length_error("Too many translucent triangles for one view");
    std::ranges::copy(order.indices, translucent_indices->begin());
    translucent_indices->dirty();

    auto& state = *traversal.getState();
    for (const auto& run : order.runs) {
        const auto& entry = sorted[run.draw];
        state.modelviewMatrixStack.push(entry.model_view);
        state.dirty = true;
        entry.drawable->draw(traversal, run.pass, run.batch, *bind_translucent, run.first_index,
                             run.index_count);
        state.modelviewMatrixStack.pop();
        state.dirty = true;
    }
}

// --- WorldGraph -----------------------------------------------------------------------

WorldGraph::WorldGraph(RenderContext& context, const Camera3D& camera,
                       const ShadingEnvironment& environment, const glm::vec4& fog_color)
    : context(context), camera(camera), environment(environment), fog_color(fog_color),
      bind_view(
          vsg::BindViewDescriptorSets::create(VK_PIPELINE_BIND_POINT_GRAPHICS, context.layout, 0))
{
    root_group->add(bind_view);
    root_group->addChild(scene);
    root_group->addChild(TranslucentStage::create());
}

vsg::ref_ptr<MeshAsset> WorldGraph::asset(Mesh& mesh)
{
    auto& found = assets[&mesh];
    if (!found.asset) found.asset = MeshAsset::create(context, mesh);
    ++found.users;
    return found.asset;
}

vsg::ref_ptr<InstanceTransform> WorldGraph::makeNode(MeshInstance& instance)
{
    auto mesh = asset(*instance.getModel());
    auto node = InstanceTransform::create(instance);
    const vsg::dsphere bound(mesh->resident.bounds_center.x, mesh->resident.bounds_center.y,
                             mesh->resident.bounds_center.z, mesh->resident.bounds_radius);
    node->addChild(vsg::CullNode::create(bound, InstanceDrawable::create(*this, instance, mesh)));
    return node;
}

void WorldGraph::stage(vsg::ref_ptr<vsg::Node> node, vsg::ref_ptr<vsg::Group> parent)
{
    staged.push_back({std::move(node), std::move(parent)});
}

void WorldGraph::attach(MeshInstance& instance)
{
    auto node = makeNode(instance);
    stage(node, scene);
    nodes[&instance] = {node, scene};
}

void WorldGraph::attach(MeshInstance& instance, const MeshInstance& parent, short key)
{
    auto& parent_node = nodes.at(&parent).node;
    const int vertex = parent.getModel()->FindMappedIndex(key);
    if (vertex < 0) throw std::invalid_argument("No such mapped vertex");
    auto attachment =
        VertexAttachment::create(parent, assets.at(parent.getModel()).asset->resident, vertex);
    auto node = makeNode(instance);
    attachment->addChild(node);
    stage(attachment, parent_node);
    nodes[&instance] = {node, attachment};
}

void WorldGraph::attach(Light& light, const MeshInstance* parent)
{
    stage(LightNode::create(light),
          parent ? vsg::ref_ptr<vsg::Group>(nodes.at(parent).node) : scene);
}

void WorldGraph::attach(FogParams& fog)
{
    fogs.push_back(&fog);
}

void WorldGraph::detach(MeshInstance& instance)
{
    const auto found = nodes.find(&instance);
    if (found == nodes.end()) throw std::invalid_argument("Instance is not attached");
    auto [node, parent] = found->second;
    if (node->children.size() > 1) throw std::logic_error("Detach attachments first");
    nodes.erase(found);
    std::erase_if(staged, [&](const Staged& entry) { return entry.node == node; });
    std::erase(parent->children, vsg::ref_ptr<vsg::Node>(node));
    retired.push_back({node, kReleaseDelayFrames});
    // The cache must not outlive the Mesh: its resident data borrows materials.
    const auto mesh = assets.find(instance.getModel());
    if (--mesh->second.users == 0) assets.erase(mesh);
}

void WorldGraph::compile(vsg::Viewer& viewer)
{
    for (auto& entry : retired) --entry.frames_left;
    std::erase_if(retired, [](const Retired& entry) { return entry.frames_left <= 0; });
    if (staged.empty()) return;
    auto batch = vsg::Group::create();
    for (const auto& entry : staged) batch->addChild(entry.node);
    const auto result = viewer.compileManager->compile(batch);
    if (!result) throw std::runtime_error("Compiling attached nodes failed");
    vsg::updateViewer(viewer, result);
    for (const auto& entry : staged) entry.parent->addChild(entry.node);
    staged.clear();
}

vsg::ref_ptr<vsg::View> WorldGraph::createView(vsg::ref_ptr<vsg::ViewportState> viewport)
{
    auto view = vsg::View::create(
        vsg::Camera::create(GameProjection::create(camera), GameView::create(camera), viewport),
        root_group);
    view->viewDependentState = WorldViewState::create(view.get(), *this);
    return view;
}

WorldGraph::Pick WorldGraph::pick(const vsg::Camera& view_camera, int x, int y) const
{
    auto intersector = vsg::LineSegmentIntersector::create(view_camera, x, y);
    scene->accept(*intersector);
    const auto eye = vsg::inverse(view_camera.viewMatrix->transform()) * vsg::dvec3(0, 0, 0);
    Pick nearest;
    double nearest_ratio = 2;
    for (const auto& hit : intersector->intersections) {
        if (hit->ratio >= nearest_ratio) continue;
        for (auto node = hit->nodePath.rbegin(); node != hit->nodePath.rend(); ++node) {
            if (const auto* drawable = dynamic_cast<const InstanceDrawable*>(*node)) {
                nearest_ratio = hit->ratio;
                nearest = {const_cast<MeshInstance*>(&drawable->instance),
                           vsg::length(hit->worldIntersection - eye)};
                break;
            }
        }
    }
    return nearest;
}

void WorldGraph::setProbePlan(WorldViewState& view, const ProbePlan& plan, vsg::Viewer& viewer)
{
    // Every vertex becomes one point at its own pixel; no depth test, no blending.
    constexpr std::uint32_t unblended_always =
        DrawShader::PASS_ALWAYS | DrawShader::MASK_COLOR_WRITE |
        DrawShader::SRCBLEND_ONE << DrawShader::SRCBLEND_SHIFT;
    auto resources = vsg::StateGroup::create();
    view.probe_pipeline = context.pipeline(unblended_always, true);
    resources->add(view.probe_pipeline);
    view.probe_draws.clear();
    for (const auto& probe : plan.cases) {
        for (int mode = 1; mode <= 2; ++mode) {
            auto block = packBatch(probe.shader);
            block.probe = probeSelector(plan, probe, mode);
            auto set = context.batchSet(block, context.texture(nullptr));
            resources->add(set);
            view.probe_draws.insert({probe.source.instance, {probe.pass, set}});
        }
    }
    const auto result = viewer.compileManager->compile(resources);
    if (!result) throw std::runtime_error("Compiling probe resources failed");
    vsg::updateViewer(viewer, result);
}

} // namespace render_poc
