#pragma once

// The game's scene as a VulkanSceneGraph graph.
//
// WorldGraph replaces WorldScene's flat attach lists. Game objects (MeshInstance,
// Light, FogParams) stay the source of truth and are borrowed, never copied:
// each node reads its object when VSG traverses it, so moving an instance,
// advancing its animation or fading it needs no synchronisation code.
//
//   root: StateGroup binding the per-view set (BindViewDescriptorSets)
//    ├─ scene
//    │   ├─ InstanceTransform ─┬─ CullNode ─ InstanceDrawable
//    │   │   (a creature)      └─ VertexAttachment (mapped "hand" vertex)
//    │   │                         └─ InstanceTransform (lantern) ─┬─ CullNode ─ InstanceDrawable
//    │   │                                                         └─ LightNode (its lamp)
//    │   └─ LightNode ...
//    └─ TranslucentStage
//
// What VSG does here, and the native scene layer does by hand today:
//  - hierarchy: transforms compose, so attachments follow mapped vertices;
//  - culling: the record traversal tests each CullNode against the frustum;
//  - lights: LightNodes report their accumulated model-view to the view's
//    WorldViewState (VSG's per-view light collection), which packs the view block;
//  - translucency: sorted instances defer to TranslucentStage, which runs the
//    native global triangle order over everything the view collected;
//  - picking: VSG's LineSegmentIntersector, through a custom ArrayState that
//    exposes the current animation frame;
//  - lifetime: new nodes compile through the viewer's CompileManager and
//    detached ones are released once no frame in flight can use them.

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include <glm/glm.hpp>
#include <vsg/all.h>

#include "lighting_check.h"
#include "resident_mesh.h"
#include "shading_block.h"

class Camera3D;
class FogParams;
class Light;
class Mesh;
class MeshInstance;
class Texture;

namespace render_poc {

class WorldGraph;

// Pipelines, layouts and textures shared by every view and mesh.
class RenderContext {
public:
    explicit RenderContext(VkExtent2D extent);

    vsg::ref_ptr<vsg::PipelineLayout> layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> view_layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> instance_layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> pass_layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> batch_layout;

    // One pipeline per DrawShader word; `points` draws verification probes.
    vsg::ref_ptr<vsg::BindGraphicsPipeline> pipeline(std::uint32_t shader, bool points = false);
    vsg::ref_ptr<vsg::ImageInfo> texture(Texture* source);
    vsg::ref_ptr<vsg::BindDescriptorSet> batchSet(const BatchBlock& block,
                                                  vsg::ref_ptr<vsg::ImageInfo> image);

private:
    vsg::ShaderStages stages;
    vsg::ref_ptr<vsg::ViewportState> viewport;
    std::map<std::pair<std::uint32_t, bool>, vsg::ref_ptr<vsg::BindGraphicsPipeline>> pipelines;
    std::map<Texture*, vsg::ref_ptr<vsg::ImageInfo>> textures;
    vsg::ref_ptr<vsg::ImageInfo> white;
};

// The GPU-resident form of one Mesh, shared by every instance of it.
class MeshAsset : public vsg::Inherit<vsg::Object, MeshAsset> {
public:
    MeshAsset(RenderContext& context, Mesh& mesh);

    struct Batch {
        // [0] the mesh's own shader, [1] the blended shader of a fading instance.
        std::array<vsg::ref_ptr<vsg::BindGraphicsPipeline>, 2> pipeline;
        std::array<vsg::ref_ptr<vsg::BindDescriptorSet>, 2> set;
    };
    struct Pass {
        vsg::ref_ptr<vsg::vec4Array> block;
        vsg::ref_ptr<vsg::BindDescriptorSet> bind;
        vsg::ref_ptr<vsg::BindVertexBuffers> vertices;
        vsg::ref_ptr<vsg::BindIndexBuffer> indices;
        std::vector<Batch> batches;
        // Vertex and index buffers with one draw, for intersection traversals.
        vsg::ref_ptr<vsg::Commands> geometry;
    };

    ResidentMesh resident;
    std::vector<Pass> passes;

    // Advances scrolling materials once per frame, however many instances draw.
    void update(const vsg::FrameStamp& frame, double frame_time);

    void traverse(vsg::Visitor& visitor) override;
    void traverse(vsg::ConstVisitor& visitor) const override;

private:
    std::uint64_t updated_frame = ~std::uint64_t{0};
};

// A MeshInstance's pose. The game sets it on the instance; VSG composes it.
class InstanceTransform : public vsg::Inherit<vsg::Transform, InstanceTransform> {
public:
    explicit InstanceTransform(const MeshInstance& instance);
    const MeshInstance& instance;
    vsg::dmat4 transform(const vsg::dmat4& model_view) const override;
};

// Follows a mapped vertex of an instance at its current animation frame.
class VertexAttachment : public vsg::Inherit<vsg::Transform, VertexAttachment> {
public:
    VertexAttachment(const MeshInstance& instance, const ResidentMesh& mesh, int vertex);
    vsg::dmat4 transform(const vsg::dmat4& model_view) const override;

private:
    const MeshInstance& instance;
    const ResidentMesh& mesh;
    std::size_t vertex;
};

// Draws one MeshInstance with its shared MeshAsset.
class InstanceDrawable : public vsg::Inherit<vsg::Node, InstanceDrawable> {
public:
    InstanceDrawable(WorldGraph& world, const MeshInstance& instance,
                     vsg::ref_ptr<MeshAsset> asset);

    const MeshInstance& instance;
    vsg::ref_ptr<MeshAsset> asset;

    void accept(vsg::RecordTraversal& traversal) const override;
    void traverse(vsg::Visitor& visitor) override;
    void traverse(vsg::ConstVisitor& visitor) const override;
    void traverse(vsg::RecordTraversal&) const override {}

    // Records one batch with the given index buffer range and shader variant.
    void draw(vsg::RecordTraversal& traversal, std::size_t pass, std::size_t batch,
              const vsg::BindIndexBuffer& indices, std::uint32_t first_index,
              std::uint32_t index_count) const;
    // Records the pass's vertices as probe points with a probe batch set.
    void drawProbe(vsg::RecordTraversal& traversal, std::size_t pass,
                   vsg::ref_ptr<vsg::BindDescriptorSet> probe_set,
                   vsg::ref_ptr<vsg::BindGraphicsPipeline> points) const;

private:
    WorldGraph& world;
    vsg::ref_ptr<vsg::vec4Array> block;
    vsg::ref_ptr<vsg::BindDescriptorSet> bind;
    vsg::ref_ptr<vsg::StateGroup> intersection;
};

// A Light at its place in the graph, prepared per view as Light::beginPass does.
class LightNode : public vsg::Inherit<vsg::Node, LightNode> {
public:
    explicit LightNode(Light& light);
    Light& light;
    void accept(vsg::RecordTraversal& traversal) const override;
};

// Records the translucent runs of everything the view collected, last.
class TranslucentStage : public vsg::Inherit<vsg::Node, TranslucentStage> {
public:
    void accept(vsg::RecordTraversal& traversal) const override;
};

// Per-view state: VSG clears it before a view's record traversal and calls
// traverse(RecordTraversal&) after it. It collects lights and sorted draws,
// owns the view block and the translucent index buffer, and, for --verify,
// records probe points instead of triangles.
class WorldViewState : public vsg::Inherit<vsg::ViewDependentState, WorldViewState> {
public:
    WorldViewState(vsg::View* view, WorldGraph& world);

    static WorldViewState* of(vsg::RecordTraversal& traversal);

    void init(vsg::ResourceRequirements&) override {}
    void clear() override;
    void traverse(vsg::Visitor& visitor) override;
    void traverse(vsg::ConstVisitor& visitor) const override;
    void traverse(vsg::RecordTraversal& traversal) const override;

    void addLight(Light& light, const vsg::dmat4& model_view);
    void addSorted(const InstanceDrawable& drawable, const vsg::dmat4& model_view);
    void recordTranslucent(vsg::RecordTraversal& traversal);

    std::vector<Light*> lights; // in collection order, prepared for this view
    std::size_t drawn_instances = 0;

    // Verification: the first frame collects the visible instances, the next
    // draws the plan made from them (see WorldGraph::setProbePlan).
    bool probing = false;
    std::vector<ProbeSource> probe_sources;
    struct ProbeDraw {
        std::size_t pass;
        vsg::ref_ptr<vsg::BindDescriptorSet> set;
    };
    std::multimap<const MeshInstance*, ProbeDraw> probe_draws;
    vsg::ref_ptr<vsg::BindGraphicsPipeline> probe_pipeline;

private:
    struct Sorted {
        const InstanceDrawable* drawable;
        vsg::dmat4 model_view;
    };
    WorldGraph& world;
    vsg::ref_ptr<vsg::vec4Array> block;
    std::vector<Sorted> sorted;
    vsg::ref_ptr<vsg::uintArray> translucent_indices;
    vsg::ref_ptr<vsg::BindIndexBuffer> bind_translucent;
};

class WorldGraph {
public:
    // The camera, environment and fog colour belong to the game, as they did
    // to WorldScene; the world borrows them.
    WorldGraph(RenderContext& context, const Camera3D& camera,
               const ShadingEnvironment& environment, const glm::vec4& fog_color);

    // Scene membership. Attached nodes join the graph at the next compile().
    void attach(MeshInstance& instance);
    // Places `instance` relative to `parent`'s mapped vertex `key`.
    void attach(MeshInstance& instance, const MeshInstance& parent, short key);
    void attach(Light& light, const MeshInstance* parent = nullptr);
    void attach(FogParams& fog);
    // The instance leaves the graph at once and may be destroyed by the caller;
    // its GPU resources are released once no frame in flight uses them.
    void detach(MeshInstance& instance);

    // Compiles nodes attached since the last call and links them into the
    // graph; releases retired nodes. Call once per frame before recording.
    void compile(vsg::Viewer& viewer);

    // A view of the world through the game's camera, with its own WorldViewState.
    vsg::ref_ptr<vsg::View> createView(vsg::ref_ptr<vsg::ViewportState> viewport);
    vsg::ref_ptr<vsg::Node> root() const { return root_group; }

    // The nearest pickable instance under a window pixel, by VSG intersection.
    struct Pick {
        MeshInstance* instance = nullptr;
        double distance = 0; // from the eye, in world units
    };
    Pick pick(const vsg::Camera& camera, int x, int y) const;

    // Builds the probe draws of `plan` for a probing view.
    void setProbePlan(WorldViewState& view, const ProbePlan& plan, vsg::Viewer& viewer);

    std::size_t instanceCount() const { return nodes.size(); }
    std::size_t assetCount() const { return assets.size(); }

    RenderContext& context;
    const Camera3D& camera;
    const ShadingEnvironment& environment;
    const glm::vec4& fog_color;
    std::vector<FogParams*> fogs;
    vsg::ref_ptr<vsg::BindViewDescriptorSets> bind_view;

private:
    vsg::ref_ptr<MeshAsset> asset(Mesh& mesh);
    vsg::ref_ptr<InstanceTransform> makeNode(MeshInstance& instance);
    void stage(vsg::ref_ptr<vsg::Node> node, vsg::ref_ptr<vsg::Group> parent);

    struct CachedAsset {
        vsg::ref_ptr<MeshAsset> asset;
        int users = 0;
    };
    struct Membership {
        vsg::ref_ptr<InstanceTransform> node;
        vsg::ref_ptr<vsg::Group> parent;
    };
    struct Staged {
        vsg::ref_ptr<vsg::Node> node;
        vsg::ref_ptr<vsg::Group> parent;
    };
    struct Retired {
        vsg::ref_ptr<vsg::Node> node;
        int frames_left;
    };
    std::map<const Mesh*, CachedAsset> assets;
    std::map<const MeshInstance*, Membership> nodes;
    std::vector<Staged> staged;
    std::deque<Retired> retired;
    vsg::ref_ptr<vsg::StateGroup> root_group = vsg::StateGroup::create();
    vsg::ref_ptr<vsg::Group> scene = vsg::Group::create();
};

} // namespace render_poc
