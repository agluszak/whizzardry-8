#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <vsg/all.h>

#include "wiz8/render/draw_inputs.h"

class Mesh;
class MeshInstance;
class WorldScene;

namespace render {
class RenderContext;
class MeshPose;

// Existing-game callers opt into resolving their owner from a VSG hit.
MeshInstance* gameOwner(const Pick& pick);

inline constexpr vsg::Mask render_mask = 1;
inline constexpr vsg::Mask pick_mask = 2;

vsg::dmat4 toVsg(const glm::dmat4& matrix);
glm::mat4 toGlm(const vsg::dmat4& matrix);

// A leaf keeps the complete Wizardry material state beside ordinary VSG
// geometry. Sorted leaves join the view's final global triangle ordering.
class DrawNode : public vsg::Inherit<vsg::StateGroup, DrawNode> {
public:
    DrawNode(vsg::ref_ptr<vsg::VertexIndexDraw> geometry, vsg::ref_ptr<vsg::vec3Array> positions,
             bool sorted = false, std::uint32_t pass_key = 0);
    void accept(vsg::RecordTraversal& traversal) const override;
    void traverse(vsg::Visitor& visitor) override;
    void traverse(vsg::ConstVisitor& visitor) const override;
    // Replaces any pipeline set before.
    void setPipeline(vsg::ref_ptr<vsg::BindGraphicsPipeline> pipeline,
                     vsg::ref_ptr<vsg::BindGraphicsPipeline> reflected);
    void recordSorted(vsg::RecordTraversal&, const vsg::BindIndexBuffer&, std::uint32_t first,
                      std::uint32_t count) const;

    vsg::ref_ptr<vsg::VertexIndexDraw> draw;
    vsg::ref_ptr<vsg::vec3Array> positions;
    vsg::ref_ptr<vsg::vec3Array> authored_normals;
    bool sorted;
    std::uint64_t sort_order = ~std::uint64_t{0};
    std::uint32_t pass_key;
    FaceCull cull = FaceCull::Back;
    Winding winding = Winding::Standard;
    bool pickable = true;

private:
    vsg::StateCommands recordState(vsg::RecordTraversal&) const;
    vsg::ref_ptr<vsg::BindGraphicsPipeline> reflected_pipeline;
    vsg::ref_ptr<vsg::BindVertexBuffers> vertices;
};

// The instance's actual scene node. Attachments are children of this transform;
// they use VSG's accumulated transform in every view and intersection traversal.
class InstanceNode : public vsg::Inherit<vsg::Transform, InstanceNode> {
public:
    explicit InstanceNode(MeshInstance& instance);
    ~InstanceNode() override;
    vsg::dmat4 transform(const vsg::dmat4& model_view) const override;
    void accept(vsg::Visitor&) override;
    void accept(vsg::ConstVisitor&) const override;
    void accept(vsg::RecordTraversal&) const override;
    void advanceTextures(std::chrono::milliseconds now);
    void prepare(RenderContext&, const ScenePass&, std::uint64_t frame_number,
                 bool static_geometry = false);
    std::optional<glm::vec3> sampledPosition(const Mesh& mesh, std::size_t index);

    MeshInstance* instance;
    bool static_geometry = false;
    std::uint64_t sort_order = ~std::uint64_t{0};

private:
    void ensureParts();
    struct Part;
    // Every model the instance has drawn keeps its parts, so an animation that switches
    // between frame models reuses their draw graphs instead of rebuilding them.
    struct DrawnModel {
        std::shared_ptr<Mesh> model;
        std::vector<std::unique_ptr<Part>> parts;
    };
    std::vector<DrawnModel> drawn;
    // The current model's parts: its head, and the rest of its chain when drawn.
    std::vector<Part*> parts;
    vsg::ref_ptr<vsg::Switch> geometry;
    vsg::ref_ptr<vsg::PipelineLayout> prepared_layout;
    std::uint64_t prepared_frame = ~std::uint64_t{0};
};

class VertexAttachment : public vsg::Inherit<vsg::Transform, VertexAttachment> {
public:
    VertexAttachment(vsg::ref_ptr<InstanceNode> parent, short mapped_key,
                     const glm::vec3& fallback = {});
    vsg::dmat4 transform(const vsg::dmat4& model_view) const override;
    short mappedKey() const { return key; }

private:
    vsg::observer_ptr<InstanceNode> parent;
    short key;
    glm::vec3 fallback;
};

} // namespace render
