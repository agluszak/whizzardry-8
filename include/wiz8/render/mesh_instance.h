#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <vsg/core/observer_ptr.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/entities/animation_types.h"
#include "wiz8/math.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/transform.h"

namespace render {
class InstanceNode;
class VertexAttachment;
}
class WorldScene;
class Texture;

class MeshInstance {

public:
    ~MeshInstance();
    MeshInstance(WorldScene* scene = nullptr);
    MeshInstance(const MeshInstance& other);

    MeshInstance& operator=(const MeshInstance& other);

    // The instance shares ownership of its model; renderers borrow it.
    Mesh* getModel() const { return model_.get(); }
    void setModel(std::shared_ptr<Mesh> model);

    vsg::ref_ptr<render::InstanceNode> renderNode() const;
    glm::dmat4 worldMatrix() const;
    // Emitter orientation follows authored rotations independently of scale.
    glm::dmat3 worldRotation() const;
    void attachTo(MeshInstance& parent, std::optional<short> mapped_key = {});
    std::optional<glm::vec3> sampledPosition(const Mesh& mesh, std::size_t index) const;

    glm::vec3 getAlignAxis() const;
    std::uint32_t getExclusionMask() const;
    int isAligned() const;
    void setAlignAngle(double angle);
    void setAlignAxis(const glm::vec3& axis);
    void setAlignment(int enabled);
    void setExclusionMask(std::uint32_t mask);

    std::uint32_t alignment_flags = 0;

private:
    /* The view composed with the instance: unaligned instances take the
       ordinary world-space matrix; aligned instances rebuild the model-view
       matrix from the camera basis so the model stays screen-facing while
       inheriting the view's translation, scale and handedness. */
    friend class render::InstanceNode;
    friend class WorldScene;
    DrawTransform renderTransform(const DrawTransform& view) const;
    vsg::ref_ptr<render::InstanceNode> node_;
    vsg::observer_ptr<vsg::Group> render_parent_;
    vsg::ref_ptr<render::VertexAttachment> attachment_;

    glm::vec3 align_axis;
    float align_angle;
    std::uint32_t exclusion_mask;

private:
    std::shared_ptr<Mesh> model_;

public:
    enum { RENDER_SHADOW = 0x08u, RENDER_NO_PICK = 0x10u };

    Texture* FindMouthTexture(); /* 0x00481080 */
    // The skin table this instance draws `mesh` with: the one named skin + damage_stage, or
    // -1 for the authored textures.
    int skinTable(Mesh& mesh) const;

public:
    glm::vec4 highlight_colour;
    /* Lazily built highlight material; RenderMeshes fills it from the
       render-state RGBA and installs it as the pass material. */
    std::shared_ptr<Material> highlight_material;
    std::uint32_t render_flags;
    std::int32_t mesh_index;
    unsigned int frame_index;
    int damage_stage;
    std::string skin;
    // The models whose vertex lighting has been baked for this instance's placement.
    std::vector<const Mesh*> baked_models;
    int highlight_pass_mode;
    glm::vec3 light_scale;
    bool diffuse_scale_enabled;
    bool emissive_override_enabled;
    float diffuse_scale;
    float emissive_override;
    float frame_interpolation;
    /* The head mesh's polygons that the world's view update leaves visible.
       Without a selection every polygon is drawn. */
    std::optional<std::vector<std::uint32_t>> visible_polygons;

public:
    std::string name = "anonymous";
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    bool visible = true;
    Transform3D transform;
    WorldScene* scene = nullptr; // Borrowed world for activation and named queries.
    void setScene(WorldScene* value);
    bool draw_parts = true;
};
