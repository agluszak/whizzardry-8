#include "wiz8/render/world_scene.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <vsg/app/Camera.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/RecordTraversal.h>
#include <vsg/app/View.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/commands/BindIndexBuffer.h>
#include <vsg/commands/BindVertexBuffers.h>
#include <vsg/core/Array.h>
#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Data.h>
#include <vsg/core/Inherit.h>
#include <vsg/core/Mask.h>
#include <vsg/core/Object.h>
#include <vsg/core/Visitor.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/mat4.h>
#include <vsg/maths/transform.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Transform.h>
#include <vsg/nodes/VertexIndexDraw.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/PipelineLayout.h>
#include <vsg/state/StateCommand.h>
#include <vsg/state/ViewDependentState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/LineSegmentIntersector.h>
#include <vsg/vk/CommandBuffer.h>
#include <vsg/vk/Context.h>
#include <vulkan/vulkan_core.h>

#include "glm/common.hpp"
#include "glm/ext/matrix_double4x4.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/ground_shadow.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/native_content.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"

namespace render {
namespace {

class Projection : public vsg::Inherit<vsg::ProjectionMatrix, Projection> {
public:
    explicit Projection(const vsg::dmat4& matrix) : matrix(matrix) {}
    vsg::dmat4 transform() const override { return matrix; }
    vsg::dmat4 matrix;
};
class ViewMatrix : public vsg::Inherit<vsg::ViewMatrix, ViewMatrix> {
public:
    explicit ViewMatrix(const vsg::dmat4& matrix) : matrix(matrix) {}
    vsg::dmat4 transform(const vsg::dvec3& = {}) const override { return matrix; }
    vsg::dmat4 matrix;
};

bool bindViewState(vsg::StateGroup& group, RenderContext& context)
{
    if (!group.stateCommands.empty())
        if (const auto* binding = group.stateCommands.front()->cast<vsg::BindViewDescriptorSets>();
            binding && binding->layout == context.layout)
            return false;
    group.stateCommands.clear();
    group.add(
        vsg::BindViewDescriptorSets::create(VK_PIPELINE_BIND_POINT_GRAPHICS, context.layout, 0));
    return true;
}

class WorldViewState : public vsg::Inherit<vsg::ViewDependentState, WorldViewState> {
public:
    WorldViewState(vsg::View* view, RenderContext& context, const ScenePass& source,
                   std::size_t light_count, std::size_t sorted_indices, std::size_t opaque_indices,
                   std::span<const glm::vec4> clip_planes)
        : Inherit(view), context(context), context_layout(context.layout)
    {
        refresh(source, light_count, sorted_indices, opaque_indices, clip_planes);
    }

    bool usesContext(const RenderContext& candidate) const
    {
        return context_layout == candidate.layout;
    }

    void refresh(const ScenePass& source, std::size_t light_count, std::size_t sorted_capacity,
                 std::size_t opaque_capacity, std::span<const glm::vec4> clip_planes)
    {
        pass = source;
        eye_clip_planes.assign(clip_planes.begin(), clip_planes.end());
        prepared_lights.resize(light_count);
        fog_pointers.assign(source.fogs.begin(), source.fogs.end());
        std::erase_if(fog_pointers, [](const auto* fog) { return !fog->visible; });
        pass.fogs = fog_pointers;
        const auto values = packView(pass, eye_clip_planes, prepared_lights);
        if (!block || block->size() < values.size()) {
            block = vsg::vec4Array::create(capacity(values.size()));
            block->properties.dataVariance = vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD;
            game_set = context.descriptor(0, block)->descriptorSet;
        }
        store(*block, values);
        growIndices(indices, bind_indices, sorted_capacity);
        growIndices(opaque_indices, bind_opaque, opaque_capacity);
    }

    void clear() override
    {
        vsg::ViewDependentState::clear();
        prepared_lights.clear();
        sorted.clear();
        opaque_cursor = 0;
    }
    void traverse(vsg::Visitor& visitor) override
    {
        if (descriptorSet) vsg::ViewDependentState::traverse(visitor);
        game_set->accept(visitor);
        bind_indices->accept(visitor);
        bind_opaque->accept(visitor);
    }
    void traverse(vsg::ConstVisitor& visitor) const override
    {
        if (descriptorSet) vsg::ViewDependentState::traverse(visitor);
        game_set->accept(visitor);
        bind_indices->accept(visitor);
        bind_opaque->accept(visitor);
    }
    void traverse(vsg::RecordTraversal& visitor) const override
    {
        vsg::ViewDependentState::traverse(visitor);
        store(*block, packView(pass, eye_clip_planes, prepared_lights));
    }
    void compile(vsg::Context& compile_context) override
    {
        vsg::ViewDependentState::compile(compile_context);
        game_set->compile(compile_context);
        bind_indices->compile(compile_context);
        bind_opaque->compile(compile_context);
    }
    void bindDescriptorSets(vsg::CommandBuffer& command, VkPipelineBindPoint bind_point,
                            VkPipelineLayout layout, std::uint32_t first_set) override
    {
        if (layout != context.layout->vk(command.deviceID)) {
            vsg::ViewDependentState::bindDescriptorSets(command, bind_point, layout, first_set);
            return;
        }
        auto set = game_set->vk(command.deviceID);
        vkCmdBindDescriptorSets(command, bind_point, layout, first_set, 1, &set, 0, nullptr);
    }
    void addLight(const Light& source, const vsg::dmat4& model_view)
    {
        if (!source.visible || source.intensity == 0) return;
        auto prepared = prepareLightAt(source, toGlm(model_view), pass.frame.brightness);
        if (static_cast<unsigned>(prepared[0].w) & 1u) prepared_lights.push_back(prepared);
    }
    void addSorted(const DrawNode& node, const vsg::dmat4& model_view)
    {
        sorted.push_back({&node, model_view});
    }
    void recordOpaque(const DrawNode& node, vsg::RecordTraversal& traversal)
    {
        const auto* source = node.draw->indices->data->cast<vsg::uintArray>();
        if (!source) return;
        const auto eye = toGlm(vsg::inverse(traversal.getState()->modelviewMatrixStack.top()))[3];
        const auto start = opaque_cursor;
        for (std::uint32_t first = node.draw->firstIndex;
             first + 2 < node.draw->firstIndex + node.draw->indexCount; first += 3) {
            if (!authoredFaceVisible(*node.authored_normals, *node.positions, *source, first,
                                     glm::vec3(eye), node.winding == Winding::Standard))
                continue;
            if (opaque_cursor + 3 > opaque_indices->size())
                throw std::length_error("Opaque geometry changed after view preparation");
            for (std::uint32_t corner = 0; corner < 3; ++corner)
                opaque_indices->at(opaque_cursor++) = source->at(first + corner);
        }
        opaque_indices->dirty();
        if (opaque_cursor > start)
            node.recordSorted(traversal, *bind_opaque, start, opaque_cursor - start);
    }
    void recordSorted(vsg::RecordTraversal& traversal)
    {
        struct Triangle {
            std::uint32_t key;
            std::size_t draw;
            std::uint32_t first;
        };
        std::vector<Triangle> triangles;
        const auto projection = toGlm(traversal.getState()->projectionMatrixStack.top());
        // The old renderer's key uses the near-rescaled OpenGL projection.
        // VSG reverse clip Z = (W - old Z)/2; restore the game's projection.
        auto original_projection = projection;
        for (int column = 0; column < 4; ++column)
            original_projection[column][2] = projection[column][3] - projection[column][2] * 2;
        DrawTransform transform;
        transform.projection = original_projection;
        const auto key_projection = transform.projectClipNear();
        for (std::size_t draw_index = 0; draw_index < sorted.size(); ++draw_index) {
            const auto& item = sorted[draw_index];
            const auto* source = item.node->draw->indices->data->cast<vsg::uintArray>();
            if (!source) continue;
            const auto matrix = key_projection * toGlm(item.model_view);
            for (std::uint32_t first = item.node->draw->firstIndex;
                 first + 2 < item.node->draw->firstIndex + item.node->draw->indexCount;
                 first += 3) {
                if (item.node->authored_normals) {
                    const auto eye = glm::vec3(toGlm(vsg::inverse(item.model_view))[3]);
                    if (!authoredFaceVisible(*item.node->authored_normals, *item.node->positions,
                                             *source, first, eye,
                                             item.node->winding == Winding::Standard))
                        continue;
                }
                float depth = 0;
                for (std::uint32_t corner = 0; corner < 3; ++corner) {
                    const auto& p = item.node->positions->at(source->at(first + corner));
                    depth += (matrix * glm::vec4(p.x, p.y, p.z, 1)).z;
                }
                const auto bits = std::bit_cast<std::uint32_t>(-depth);
                auto key = (bits & 0x80000000u) == 0 ? bits | 0x80000000u
                                                     : 0x7fffffffu - (bits & 0x7fffffffu);
                triangles.push_back({key + item.node->pass_key, draw_index, first});
            }
        }
        std::ranges::stable_sort(triangles, [&](const Triangle& first, const Triangle& second) {
            if (first.key != second.key) return first.key < second.key;
            return sorted[first.draw].node->sort_order < sorted[second.draw].node->sort_order;
        });
        if (triangles.size() > indices->size() / 3)
            throw std::length_error("Translucent geometry changed after view preparation");
        std::uint32_t destination = 0;
        for (const auto& triangle : triangles) {
            const auto* source =
                sorted[triangle.draw].node->draw->indices->data->cast<vsg::uintArray>();
            for (std::uint32_t corner = 0; corner < 3; ++corner)
                indices->at(destination++) = source->at(triangle.first + corner);
        }
        indices->dirty();
        auto& state = *traversal.getState();
        for (std::size_t first = 0; first < triangles.size();) {
            std::size_t end = first + 1;
            while (end < triangles.size() && triangles[end].draw == triangles[first].draw) ++end;
            const auto& item = sorted[triangles[first].draw];
            state.modelviewMatrixStack.push(item.model_view);
            state.dirty = true;
            item.node->recordSorted(traversal, *bind_indices, static_cast<std::uint32_t>(first * 3),
                                    static_cast<std::uint32_t>((end - first) * 3));
            state.modelviewMatrixStack.pop();
            state.dirty = true;
            first = end;
        }
    }

private:
    static std::uint32_t capacity(std::size_t required)
    {
        // Keep compiled buffers until their contents outgrow them. Rounding up
        // also avoids repeated allocations as effects and lights become active.
        const auto result = std::bit_ceil(std::max<std::size_t>(required, 1));
        if (result > std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Scene view exceeds supported buffer size");
        return static_cast<std::uint32_t>(result);
    }
    static void growIndices(vsg::ref_ptr<vsg::uintArray>& array,
                            vsg::ref_ptr<vsg::BindIndexBuffer>& binding, std::size_t required)
    {
        if (array && array->size() >= required) return;
        array = vsg::uintArray::create(capacity(required));
        array->properties.dataVariance = vsg::DYNAMIC_DATA_TRANSFER_AFTER_RECORD;
        binding = vsg::BindIndexBuffer::create(array);
    }
    struct Sorted {
        const DrawNode* node;
        vsg::dmat4 model_view;
    };
    RenderContext& context;
    vsg::ref_ptr<vsg::PipelineLayout> context_layout;
    ScenePass pass;
    std::vector<glm::vec4> eye_clip_planes;
    std::vector<FogParams*> fog_pointers;
    std::vector<PreparedLight> prepared_lights;
    std::vector<Sorted> sorted;
    vsg::ref_ptr<vsg::vec4Array> block;
    vsg::ref_ptr<vsg::DescriptorSet> game_set;
    vsg::ref_ptr<vsg::uintArray> indices;
    vsg::ref_ptr<vsg::BindIndexBuffer> bind_indices;
    vsg::ref_ptr<vsg::uintArray> opaque_indices;
    vsg::ref_ptr<vsg::BindIndexBuffer> bind_opaque;
    std::uint32_t opaque_cursor = 0;
};

WorldViewState* viewState(vsg::RecordTraversal& traversal)
{
    return dynamic_cast<WorldViewState*>(traversal.viewDependentState.get());
}

class LightNode : public vsg::Inherit<vsg::Node, LightNode> {
public:
    explicit LightNode(Light& light) : light(&light) {}
    void accept(vsg::RecordTraversal& traversal) const override
    {
        if (auto* state = viewState(traversal); state && light) {
            const auto matrix =
                light->attachment == Light::Attachment::CycleOrigin
                    ? state->view->camera->viewMatrix->transform() * toVsg(light->worldMatrix())
                    : traversal.getState()->modelviewMatrixStack.top() *
                          toVsg(light->transform.matrix());
            state->addLight(*light, matrix);
        }
    }
    Light* light;
};

class TranslucentStage : public vsg::Inherit<vsg::Node, TranslucentStage> {
public:
    void accept(vsg::RecordTraversal& traversal) const override
    {
        if (auto* state = viewState(traversal)) state->recordSorted(traversal);
    }
};

struct SetSortOrder : vsg::Visitor {
    explicit SetSortOrder(std::uint64_t order) : order(order) {}
    std::uint64_t order;
    void apply(vsg::Object& object) override { object.traverse(*this); }
    void apply(vsg::StateGroup& group) override
    {
        if (auto* draw = dynamic_cast<DrawNode*>(&group)) draw->sort_order = order;
        group.traverse(*this);
    }
};

struct CountIndices : vsg::ConstVisitor {
    std::size_t sorted = 0;
    std::size_t opaque = 0;
    std::vector<Light*> lights;
    void apply(const vsg::Node& node) override
    {
        if (const auto* light = dynamic_cast<const LightNode*>(&node); light && light->light)
            lights.push_back(light->light);
        node.traverse(*this);
    }
    void apply(const vsg::Object& object) override { object.traverse(*this); }
    void apply(const vsg::StateGroup& group) override
    {
        if (const auto* draw = dynamic_cast<const DrawNode*>(&group)) {
            if (draw->sorted)
                sorted += draw->draw->indexCount;
            else if (draw->authored_normals)
                opaque += draw->draw->indexCount;
        }
        group.traverse(*this);
    }
};

void updateMembership(vsg::Node& root, WorldScene* target)
{
    struct Membership : vsg::Visitor {
        WorldScene* target;
        explicit Membership(WorldScene* target) : target(target) { overrideMask = vsg::MASK_ALL; }
        void apply(vsg::Object& object) override { object.traverse(*this); }
        void apply(vsg::Transform& transform) override
        {
            if (auto* node = dynamic_cast<InstanceNode*>(&transform); node && node->instance) {
                auto& item = *node->instance;
                if (item.scene != target) {
                    if (item.scene) std::erase(item.scene->dynamic_meshes, &item);
                    item.scene = target;
                    if (target && !node->static_geometry) target->dynamic_meshes.push_back(&item);
                }
            }
            transform.traverse(*this);
        }
        void apply(vsg::Node& node) override
        {
            if (auto* collector = dynamic_cast<LightNode*>(&node); collector && collector->light) {
                auto& item = *collector->light;
                if (item.scene != target) {
                    if (item.scene) std::erase(item.scene->lights, &item);
                    item.scene = target;
                    if (target) target->lights.push_back(&item);
                }
            }
            node.traverse(*this);
        }
    } membership(target);
    root.accept(membership);
}

} // namespace

DrawNode::DrawNode(vsg::ref_ptr<vsg::VertexIndexDraw> geometry,
                   vsg::ref_ptr<vsg::vec3Array> positions, bool sorted, std::uint32_t pass_key)
    : draw(std::move(geometry)), positions(std::move(positions)), sorted(sorted),
      pass_key(pass_key), vertices(vsg::BindVertexBuffers::create())
{
    vertices->firstBinding = draw->firstBinding;
    vertices->arrays = draw->arrays;
    addChild(draw);
}
void DrawNode::accept(vsg::RecordTraversal& traversal) const
{
    if (!draw->indexCount) return;
    if (sorted) {
        if (auto* state = viewState(traversal)) {
            state->addSorted(*this, traversal.getState()->modelviewMatrixStack.top());
            return;
        }
    }
    if (authored_normals)
        if (auto* view = viewState(traversal)) {
            view->recordOpaque(*this, traversal);
            return;
        }
    auto commands = recordState(traversal);
    auto& state = *traversal.getState();
    state.push(commands);
    draw->accept(traversal);
    state.pop(commands);
}
void DrawNode::setPipeline(vsg::ref_ptr<vsg::BindGraphicsPipeline> pipeline,
                           vsg::ref_ptr<vsg::BindGraphicsPipeline> reflected)
{
    std::erase_if(stateCommands, [](const auto& command) {
        return command->template cast<vsg::BindGraphicsPipeline>() != nullptr;
    });
    add(pipeline);
    reflected_pipeline = reflected;
}
vsg::StateCommands DrawNode::recordState(vsg::RecordTraversal& traversal) const
{
    auto commands = stateCommands;
    // Native face selection uses the eye in object space, so reflections do
    // not change which authored side is drawn. Hardware winding reverses when
    // the accumulated transform changes handedness (the game camera flips Z).
    const auto matrix = toGlm(traversal.getState()->modelviewMatrixStack.top());
    const auto& projection = traversal.getState()->projectionMatrixStack.top();
    const auto projection_winding =
        projection[0][0] * projection[1][1] - projection[0][1] * projection[1][0];
    if (reflected_pipeline && glm::determinant(glm::mat3(matrix)) * projection_winding < 0)
        for (auto& command : commands)
            if (command->cast<vsg::BindGraphicsPipeline>()) command = reflected_pipeline;
    return commands;
}
void DrawNode::traverse(vsg::Visitor& visitor)
{
    vsg::StateGroup::traverse(visitor);
    vertices->accept(visitor);
    if (reflected_pipeline) reflected_pipeline->accept(visitor);
}
void DrawNode::traverse(vsg::ConstVisitor& visitor) const
{
    vsg::StateGroup::traverse(visitor);
    if (reflected_pipeline) reflected_pipeline->accept(visitor);
    // VertexIndexDraw already supplies ordinary arrays to intersection traversal.
}
void DrawNode::recordSorted(vsg::RecordTraversal& traversal,
                            const vsg::BindIndexBuffer& index_buffer, std::uint32_t first,
                            std::uint32_t count) const
{
    auto& state = *traversal.getState();
    auto commands = recordState(traversal);
    state.push(commands);
    state.record();
    auto& command = *traversal.getCommandBuffer();
    vertices->record(command);
    index_buffer.record(command);
    vkCmdDrawIndexed(command, count, 1, first, 0, 0);
    state.pop(commands);
}

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

} // namespace render

struct WorldScene::CachedView {
    Camera3D* source;
    VkViewport viewport;
    std::uint64_t last_frame = ~std::uint64_t{0};
    vsg::ref_ptr<vsg::View> view;
    vsg::ref_ptr<render::Projection> projection;
    vsg::ref_ptr<render::ViewMatrix> view_matrix;
    vsg::ref_ptr<vsg::StateGroup> translucent;
};

WorldScene::WorldScene()
    : root(vsg::Group::create()), game_root(vsg::StateGroup::create()),
      objects(vsg::Group::create()), map_root(vsg::StateGroup::create())
{
    game_root->addChild(objects);
    root->addChild(game_root);
}
WorldScene::~WorldScene()
{
    for (auto& item : static_meshes) item->scene = nullptr;
    for (auto* item : dynamic_meshes) item->scene = nullptr;
    for (auto* item : lights) item->scene = nullptr;
    for (auto* item : particles) item->scene = nullptr;
    for (auto* item : shadows) item->scene = nullptr;
    for (auto* item : fogs) item->scene = nullptr;
    // Retained views may outlive the world while the final frame completes.
    // Stop graph traversal from retaining borrowers of its game objects.
    objects->children.clear();
    root->children.clear();
    map_root->children.clear();
}
MeshInstance* WorldScene::addStaticMesh(std::unique_ptr<MeshInstance> mesh)
{
    mesh->setScene(nullptr);
    auto* result = mesh.get();
    objects->addChild(mesh->renderNode());
    mesh->renderNode()->static_geometry = true;
    mesh->render_parent_ = objects;
    render::updateMembership(*mesh->renderNode(), this);
    static_meshes.push_back(std::move(mesh));
    return result;
}
void WorldScene::advanceVisuals(std::chrono::milliseconds now)
{
    struct Advance : vsg::Visitor {
        std::chrono::milliseconds now;
        explicit Advance(std::chrono::milliseconds now) : now(now) {}
        void apply(vsg::Object& object) override { object.traverse(*this); }
        void apply(vsg::Transform& transform) override
        {
            if (auto* instance = dynamic_cast<render::InstanceNode*>(&transform))
                instance->advanceTextures(now);
            if (auto* native = dynamic_cast<render::NativeContent*>(&transform))
                native->advanceTextures(now);
            transform.traverse(*this);
        }
    } advance(now);
    root->accept(advance);
}

void WorldScene::prepare(render::RenderContext& context, const SceneFrame& frame,
                         std::uint64_t frame_number)
{
    if (render::bindViewState(*game_root, context)) prepared_frame = ~std::uint64_t{0};
    if (prepared_frame == frame_number) return;
    prepared_frame = frame_number;
    const ScenePass pass{.frame = frame,
                         .ambient_light = glm::vec4(ambient_light, 0),
                         .fog_color = glm::vec4(fog_color, 0),
                         .fogs = fogs};
    submitted_polygons = 0;
    // Equal-depth translucent triangles keep the game's submission precedence:
    // level geometry, reverse ordinary instances, forward sorted instances,
    // then ground shadows and particles. Parentage stays entirely in VSG.
    const auto static_count = static_cast<std::uint64_t>(static_meshes.size());
    const auto dynamic_count = static_cast<std::uint64_t>(dynamic_meshes.size());
    for (std::size_t index = 0; index < static_meshes.size(); ++index)
        static_meshes[index]->renderNode()->sort_order = index;
    for (std::size_t index = 0; index < dynamic_meshes.size(); ++index) {
        auto* instance = dynamic_meshes[index];
        const bool sorted =
            instance->getModel() && (instance->getModel()->flags & W8_MESH_SORTED_RENDERING);
        instance->renderNode()->sort_order =
            static_count + (sorted ? dynamic_count + index : dynamic_count - index - 1);
    }
    struct Prepare : vsg::Visitor {
        render::RenderContext& context;
        const ScenePass& pass;
        std::uint64_t frame_number;
        unsigned exclusion;
        unsigned& polygons;
        Prepare(render::RenderContext& context, const ScenePass& pass, std::uint64_t frame_number,
                unsigned exclusion, unsigned& polygons)
            : context(context), pass(pass), frame_number(frame_number), exclusion(exclusion),
              polygons(polygons)
        {
        }
        void apply(vsg::Object& object) override { object.traverse(*this); }
        void apply(vsg::Transform& transform) override
        {
            if (auto* visual = dynamic_cast<render::InstanceNode*>(&transform)) {
                auto instance_pass = pass;
                if (visual->static_geometry) instance_pass.exclusion_mask = exclusion;
                visual->prepare(context, instance_pass, frame_number, visual->static_geometry);
                if (!visual->instance || !visual->instance->visible) return;
                render::SetSortOrder order(visual->sort_order);
                visual->traverse(order);
                if (visual->instance && visual->instance->visible && visual->instance->getModel())
                    polygons += visual->instance->getModel()->polygon_count;
            }
            if (auto* native = dynamic_cast<render::NativeContent*>(&transform))
                native->prepare(context, pass);
            transform.traverse(*this);
        }
    } prepare_instances(context, pass, frame_number, static_exclusion_mask, submitted_polygons);
    root->accept(prepare_instances);
    std::uint64_t effect_order = static_count + dynamic_count * 2;
    for (auto* shadow : shadows) {
        shadow->updateRenderNode(context, frame, frame_number);
        render::SetSortOrder order(effect_order++);
        shadow->renderNode()->accept(order);
    }
    for (auto* particle : particles) {
        particle->updateRenderNode(context, frame, frame_number);
        render::SetSortOrder order(effect_order++);
        particle->renderNode()->accept(order);
    }
}
vsg::ref_ptr<vsg::View> WorldScene::createView(render::RenderContext& context, Camera3D* source,
                                               const SceneFrame& frame,
                                               vsg::ref_ptr<vsg::ViewportState> viewport,
                                               std::uint64_t frame_number)
{
    prepare(context, frame, frame_number);
    const ScenePass pass{.frame = frame,
                         .ambient_light = glm::vec4(ambient_light, 0),
                         .fog_color = glm::vec4(glm::clamp(fog_color, 0.0f, 1.0f), 0),
                         .fogs = fogs,
                         .depth_cue = source ? source->depthCue() : DepthCue{}};
    return cachedView(context, source, root, pass, std::move(viewport), frame_number);
}
vsg::ref_ptr<vsg::View> WorldScene::createMapView(render::RenderContext& context,
                                                  const SceneFrame& frame,
                                                  vsg::ref_ptr<vsg::ViewportState> viewport,
                                                  std::uint64_t frame_number)
{
    // Explored colours are the map's whole appearance: no ambient, lights, fog or depth cue.
    const ScenePass pass{.frame = frame};
    render::bindViewState(*map_root, context);
    const auto graph = map.prepare(context, static_meshes, pass);
    if (map_root->children.empty()) map_root->addChild(graph);
    return cachedView(context, &map.camera, map_root, pass, std::move(viewport), frame_number);
}
vsg::ref_ptr<vsg::View> WorldScene::cachedView(render::RenderContext& context, Camera3D* source,
                                               vsg::ref_ptr<vsg::Node> graph, const ScenePass& pass,
                                               vsg::ref_ptr<vsg::ViewportState> viewport,
                                               std::uint64_t frame_number)
{
    const auto dimensions = viewport->getViewport();
    auto found = std::ranges::find_if(views, [&](const auto& entry) {
        const auto& previous = entry->viewport;
        return entry->source == source && entry->last_frame != frame_number &&
               previous.x == dimensions.x && previous.y == dimensions.y &&
               previous.width == dimensions.width && previous.height == dimensions.height;
    });
    if (found == views.end()) {
        // Reuse a camera's inactive view when the window changes size. Views
        // used twice in one frame keep separate descriptor/index storage.
        found = std::ranges::find_if(views, [&](const auto& entry) {
            return entry->source == source && entry->last_frame != frame_number;
        });
        if (found != views.end()) (*found)->viewport = dimensions;
    }
    if (found == views.end()) {
        auto entry = std::make_unique<CachedView>();
        entry->source = source;
        entry->viewport = dimensions;
        entry->view = vsg::View::create();
        entry->projection = render::Projection::create(vsg::dmat4{});
        entry->view_matrix = render::ViewMatrix::create(vsg::dmat4{});
        entry->view->camera = vsg::Camera::create(entry->projection, entry->view_matrix, viewport);
        entry->view->addChild(graph);
        entry->translucent = vsg::StateGroup::create();
        entry->translucent->addChild(render::TranslucentStage::create());
        entry->view->addChild(entry->translucent);
        views.push_back(std::move(entry));
        found = std::prev(views.end());
    }
    (*found)->last_frame = frame_number;
    render::bindViewState(*(*found)->translucent, context);
    auto& view = (*found)->view;
    const auto transform = source ? source->drawTransform() : DrawTransform{};
    glm::dmat4 clip(1);
    clip[1][1] = -1;
    clip[2][2] = -.5;
    clip[3][2] = .5;
    (*found)->projection->matrix = render::toVsg(clip * glm::dmat4(transform.projection));
    (*found)->view_matrix->matrix = render::toVsg(glm::dmat4(transform.model_view));
    view->camera->viewportState = viewport;
    view->mask = render::render_mask;
    render::CountIndices counts;
    graph->accept(counts);
    std::vector<glm::vec4> clip_planes_eye;
    for (const auto& plane : clip_planes)
        if (plane->visible)
            clip_planes_eye.push_back(
                eyeSpaceClipPlane(transform.model_view * plane->transform.matrix<glm::mat4>(),
                                  plane->getClipPlane()));
    auto* state = dynamic_cast<render::WorldViewState*>(view->viewDependentState.get());
    if (state && state->usesContext(context))
        state->refresh(pass, counts.lights.size(), counts.sorted, counts.opaque, clip_planes_eye);
    else
        view->viewDependentState =
            render::WorldViewState::create(view.get(), context, pass, counts.lights.size(),
                                           counts.sorted, counts.opaque, clip_planes_eye);
    return view;
}
void WorldScene::pick(const vsg::Camera& camera, std::span<Pick> picks) const
{
    const auto viewport = camera.getViewport();
    if (viewport.width <= 0 || viewport.height <= 0) return;
    const auto projection = camera.projectionMatrix->transform();
    const auto inverse_projection = vsg::inverse(projection);
    const bool reverse_depth = projection[2][2] > 0;
    for (auto& pick : picks) {
        const auto x = static_cast<int>(viewport.x + (pick.position.x + 1) * .5f * viewport.width);
        const auto y = static_cast<int>(viewport.y + (1 - pick.position.y) * .5f * viewport.height);
        auto test = vsg::LineSegmentIntersector::create(camera, x, y);
        const auto ndc_x = (x - viewport.x) / viewport.width * 2 - 1;
        const auto ndc_y = (y - viewport.y) / viewport.height * 2 - 1;
        const auto near_point =
            inverse_projection *
            vsg::dvec3(ndc_x, ndc_y, reverse_depth ? viewport.maxDepth : viewport.minDepth);
        const auto far_point =
            inverse_projection *
            vsg::dvec3(ndc_x, ndc_y, reverse_depth ? viewport.minDepth : viewport.maxDepth);
        const auto toward_eye = near_point - far_point;
        test->traversalMask = render::pick_mask;
        root->accept(*test);
        double nearest = std::numeric_limits<double>::max();
        for (const auto& hit : test->intersections) {
            if (hit->ratio >= nearest) continue;
            const render::DrawNode* drawable = nullptr;
            auto model_view = camera.viewMatrix->transform();
            for (auto* node : hit->nodePath) {
                if (const auto* transform = dynamic_cast<const vsg::Transform*>(node))
                    model_view = transform->transform(model_view);
                if (const auto* draw = dynamic_cast<const render::DrawNode*>(node)) drawable = draw;
            }
            if (drawable && !drawable->pickable) continue;
            const auto eye_intersection = model_view * hit->localIntersection;
            bool clipped = false;
            const auto view = render::toGlm(camera.viewMatrix->transform());
            for (const auto& plane : clip_planes) {
                if (!drawable || !plane->visible) continue;
                const auto eye_plane = eyeSpaceClipPlane(
                    view * plane->transform.matrix<glm::mat4>(), plane->getClipPlane());
                if (glm::dot(eye_plane, glm::vec4(eye_intersection.x, eye_intersection.y,
                                                  eye_intersection.z, 1)) < 0)
                    clipped = true;
            }
            if (clipped) continue;
            if (drawable && drawable->authored_normals && hit->indexRatios.size() == 3) {
                const auto* indices = drawable->draw->indices->data->cast<vsg::uintArray>();
                bool visible = false;
                if (indices) {
                    const auto eye = glm::vec3(render::toGlm(vsg::inverse(model_view))[3]);
                    for (std::uint32_t first = drawable->draw->firstIndex;
                         first + 2 < drawable->draw->firstIndex + drawable->draw->indexCount;
                         first += 3) {
                        if (indices->at(first) != hit->indexRatios[0].index ||
                            indices->at(first + 1) != hit->indexRatios[1].index ||
                            indices->at(first + 2) != hit->indexRatios[2].index)
                            continue;
                        visible = render::authoredFaceVisible(
                            *drawable->authored_normals, *drawable->positions, *indices, first, eye,
                            drawable->winding == Winding::Standard);
                        break;
                    }
                }
                if (!visible) continue;
            }
            if (drawable && drawable->cull != FaceCull::None && hit->indexRatios.size() == 3) {
                const auto a = vsg::dvec3(drawable->positions->at(hit->indexRatios[0].index));
                const auto b = vsg::dvec3(drawable->positions->at(hit->indexRatios[1].index));
                const auto c = vsg::dvec3(drawable->positions->at(hit->indexRatios[2].index));
                const auto direction = vsg::inverse(model_view) * vsg::dvec4(toward_eye, 0);
                auto facing = vsg::dot(vsg::cross(b - a, c - a),
                                       vsg::dvec3(direction.x, direction.y, direction.z));
                if (drawable->winding == Winding::Reversed) facing = -facing;
                if (drawable->cull == FaceCull::Back ? facing <= 0 : facing >= 0) continue;
            }
            nearest = hit->ratio;
            pick.hit = hit;
            const auto clip =
                camera.projectionMatrix->transform() * vsg::dvec4(eye_intersection, 1);
            pick.position.z = static_cast<float>(1 - clip.z / clip.w * 2);
        }
    }
}
void WorldScene::attach(MeshInstance& item)
{
    if (item.scene == this && item.render_parent_.valid()) return;
    if (item.scene)
        item.scene->detach(item);
    else if (auto parent = item.render_parent_.ref_ptr())
        std::erase(parent->children, item.attachment_ ? vsg::ref_ptr<vsg::Node>(item.attachment_)
                                                      : vsg::ref_ptr<vsg::Node>(item.node_));
    objects->addChild(item.node_);
    item.node_->static_geometry = false;
    item.render_parent_ = objects;
    item.attachment_ = {};
    render::updateMembership(*item.node_, this);
}
void WorldScene::detach(MeshInstance& item)
{
    render::updateMembership(*item.node_, nullptr);
    if (auto parent = item.render_parent_.ref_ptr()) {
        std::erase(parent->children, item.attachment_ ? vsg::ref_ptr<vsg::Node>(item.attachment_)
                                                      : vsg::ref_ptr<vsg::Node>(item.node_));
        if (item.attachment_) {
            // The attachment belongs to the old parent, and does not retain it.
            item.attachment_->children.clear();
        }
    }
    item.render_parent_.reset();
    item.attachment_ = {};
}
void MeshInstance::setScene(WorldScene* value)
{
    if (value == scene &&
        ((value && render_parent_.valid()) || (!value && !render_parent_.valid())))
        return;
    if (scene)
        scene->detach(*this);
    else if (auto parent = render_parent_.ref_ptr()) {
        std::erase(parent->children, attachment_ ? vsg::ref_ptr<vsg::Node>(attachment_)
                                                 : vsg::ref_ptr<vsg::Node>(node_));
        render_parent_.reset();
        attachment_ = {};
    }
    render::updateMembership(*node_, nullptr);
    if (value) value->attach(*this);
}
void MeshInstance::attachTo(MeshInstance& parent, std::optional<short> mapped_key)
{
    // Reject a cycle before changing either relationship.
    for (auto ancestor = parent.render_parent_.ref_ptr(); ancestor;) {
        if (ancestor.get() == node_.get())
            throw std::invalid_argument("A visual cannot be attached beneath itself");
        auto* instance_node = dynamic_cast<render::InstanceNode*>(ancestor.get());
        if (!instance_node || !instance_node->instance) break;
        ancestor = instance_node->instance->render_parent_.ref_ptr();
    }
    if (&parent == this) throw std::invalid_argument("A visual cannot be its own parent");
    setScene(nullptr);
    if (mapped_key) {
        attachment_ = render::VertexAttachment::create(parent.node_, *mapped_key);
        attachment_->addChild(node_);
        parent.node_->addChild(attachment_);
    } else {
        parent.node_->addChild(node_);
    }
    render_parent_ = parent.node_;
    render::updateMembership(*node_, parent.scene);
}
void WorldScene::attach(Light& item)
{
    if (item.scene == this) return;
    item.setScene(nullptr);
    item.render_node = render::LightNode::create(item);
    objects->addChild(item.render_node);
    item.render_parent = objects;
    lights.push_back(&item);
    item.scene = this;
}
void WorldScene::detach(Light& item)
{
    std::erase(lights, &item);
    if (auto parent = item.render_parent.ref_ptr())
        std::erase(parent->children, item.render_attachment
                                         ? vsg::ref_ptr<vsg::Node>(item.render_attachment)
                                         : item.render_node);
    item.render_parent.reset();
    item.render_attachment = {};
    if (auto* node = dynamic_cast<render::LightNode*>(item.render_node.get()))
        node->light = nullptr;
    item.render_node = {};
    if (item.scene == this) item.scene = nullptr;
}
void Light::setScene(WorldScene* value)
{
    if (value == scene &&
        ((value && render_parent.valid()) || (!value && !render_parent.valid() && !render_node)))
        return;
    if (scene)
        scene->detach(*this);
    else {
        if (auto parent = render_parent.ref_ptr())
            std::erase(parent->children, render_attachment
                                             ? vsg::ref_ptr<vsg::Node>(render_attachment)
                                             : render_node);
        if (auto* node = dynamic_cast<render::LightNode*>(render_node.get())) node->light = nullptr;
        render_node = {};
        render_attachment = {};
        render_parent.reset();
    }
    if (value) value->attach(*this);
}
void WorldScene::attach(ParticleSystem& item)
{
    if (item.scene == this) return;
    if (item.scene) item.scene->detach(item);
    particles.push_back(&item);
    objects->addChild(item.renderNode());
    item.scene = this;
}
void WorldScene::detach(ParticleSystem& item)
{
    std::erase(particles, &item);
    std::erase(objects->children, item.renderNode());
    // Nodes already handed to a frame stay alive through the renderer's idle
    // barrier. Their GPU arrays do not borrow simulation storage.
    if (item.scene == this) item.scene = nullptr;
}
void ParticleSystem::setScene(WorldScene* value)
{
    if (value == scene) return;
    if (scene) scene->detach(*this);
    if (value) value->attach(*this);
}
void WorldScene::attach(GroundShadow& item)
{
    if (item.scene == this) return;
    if (item.scene) item.scene->detach(item);
    shadows.push_back(&item);
    objects->addChild(item.renderNode());
    item.scene = this;
}
void WorldScene::detach(GroundShadow& item)
{
    std::erase(shadows, &item);
    std::erase(objects->children, item.renderNode());
    if (item.scene == this) item.scene = nullptr;
}
void WorldScene::attach(FogParams& item)
{
    if (item.scene == this) return;
    if (item.scene) item.scene->detach(item);
    fogs.push_back(&item);
    item.scene = this;
}
void WorldScene::detach(FogParams& item)
{
    std::erase(fogs, &item);
    if (item.scene == this) item.scene = nullptr;
}
void FogParams::setScene(WorldScene* value)
{
    if (value == scene) return;
    if (scene) scene->detach(*this);
    if (value) value->attach(*this);
}

void Light::attachTo(MeshInstance& parent, std::optional<short> mapped_key, Attachment policy)
{
    auto parent_node = parent.renderNode();
    if (render_parent == parent_node && scene == parent.scene && render_node &&
        attachment == policy &&
        ((!mapped_key && !render_attachment) ||
         (mapped_key && render_attachment && render_attachment->mappedKey() == *mapped_key)))
        return;
    setScene(nullptr);
    if (parent.scene) {
        parent.scene->lights.push_back(this);
        scene = parent.scene;
    }
    attachment = policy;
    render_node = render::LightNode::create(*this);
    render_parent = parent_node;
    if (mapped_key) {
        render_attachment = render::VertexAttachment::create(parent_node, *mapped_key);
        render_attachment->addChild(render_node);
        parent_node->addChild(render_attachment);
    } else {
        parent_node->addChild(render_node);
    }
}
glm::dmat4 Light::worldMatrix() const
{
    glm::dmat4 parent_matrix(1);
    if (auto parent = render_parent.ref_ptr())
        if (const auto* node = dynamic_cast<const render::InstanceNode*>(parent.get());
            node && node->instance) {
            if (attachment == Attachment::CycleOrigin) {
                const auto& actor = node->instance->transform;
                auto matrix = transform.matrix();
                matrix[3] = glm::dvec4(actor.position + actor.rotation * transform.position, 1);
                return matrix;
            }
            parent_matrix = node->instance->worldMatrix();
        }
    if (render_attachment)
        parent_matrix =
            glm::dmat4(render::toGlm(render_attachment->transform(render::toVsg(parent_matrix))));
    return parent_matrix * transform.matrix();
}

MeshInstance* render::gameOwner(const Pick& pick)
{
    if (pick.hit)
        for (auto node = pick.hit->nodePath.rbegin(); node != pick.hit->nodePath.rend(); ++node)
            if (const auto* instance = dynamic_cast<const InstanceNode*>(*node))
                return instance->instance;
    return nullptr;
}
