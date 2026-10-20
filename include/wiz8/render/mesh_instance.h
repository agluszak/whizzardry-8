#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <math.h>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/entities/animation_types.h"
#include "wiz8/math.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/transform.h"

class GpuRenderer;
class WorldScene;
class Texture;

class MeshInstance {

public:
    ~MeshInstance();
    MeshInstance(WorldScene* scene = nullptr);

    MeshInstance& operator=(const MeshInstance& other);

    // The instance shares ownership of its model; renderers borrow it.
    Mesh* getModel() const { return model_.get(); }
    void setModel(std::shared_ptr<Mesh> model) { model_ = std::move(model); }

    glm::vec3 getAlignAxis() const;
    w8_ulong getExclusionMask() const;
    int isAligned() const;
    void setAlignAngle(double angle);
    void setAlignAxis(const glm::vec3& axis);
    void setAlignment(int enabled);
    void setExclusionMask(w8_ulong mask);

    std::uint32_t alignment_flags = 0;

private:
    class RenderScope {
    public:
        ~RenderScope();
        RenderScope(const RenderScope&) = delete;
        RenderScope& operator=(const RenderScope&) = delete;

    private:
        friend class MeshInstance;
        RenderScope(const MeshInstance& instance, GpuRenderer& renderer, bool apply_exclusion);
        GpuRenderer& renderer;
        w8_ulong previous_mask = 0;
        bool restore_mask = false;
    };
    RenderScope scopedRender(GpuRenderer& renderer, bool apply_exclusion = true) const;

    /* The view composed with the instance: unaligned instances take the
       ordinary world-space matrix; aligned instances rebuild the model-view
       matrix from the camera basis so the model stays screen-facing while
       inheriting the view's translation, scale and handedness. */
    DrawTransform renderTransform(const DrawTransform& view) const;

    glm::vec3 align_axis;
    float align_angle;
    w8_ulong exclusion_mask;

private:
    std::shared_ptr<Mesh> model_;

public:
    enum { RENDER_LIGHTING_BAKED = 0x02u, RENDER_SHADOW = 0x08u, RENDER_NO_PICK = 0x10u };

    /* Queues opaque models ahead of their siblings and sorted-rendering
       models after them. */
    /* Mesh-chain submit reached from process(): frustum culling, ambient and
       highlight state, the linked Mesh list, then the optional
       highlight shell pass with reversed winding. */
    void draw(GpuRenderer& renderer, const DrawTransform& view);
    void RenderMeshes(GpuRenderer& renderer, const DrawTransform& transform);
    /* Shadow-volume extrusion helper for the first mesh in the chain. */
    void RenderShadow(GpuRenderer& renderer, Mesh::TriMesh& mesh, const DrawTransform& transform);

    Texture* FindMouthTexture(); /* 0x00481080 */
    int AddDamageStage(std::string_view name);
    int AddExistingDamageStage(std::string_view name);
    int FindDamageStage(std::string_view name);
    unsigned char ReplaceDamageStageTexture(int stage, std::string_view old_name,
                                            const std::shared_ptr<Texture>& replacement);

public:
    glm::vec4 highlight_colour;
    /* Lazily built highlight material; RenderMeshes fills it from the
       render-state RGBA and installs it as the pass material. */
    std::shared_ptr<Material> highlight_material;
    w8_ulong render_flags;
    w8_long mesh_index;
    unsigned int frame_index;
    int damage_stage;
    std::vector<int> damage_stage_tables;
    int highlight_pass_mode;
    glm::vec3 light_scale;
    bool diffuse_scale_enabled;
    bool emissive_override_enabled;
    float diffuse_scale;
    float emissive_override;
    float frame_interpolation;

public:
    std::string name = "anonymous";
    const std::string& getName() const { return name; }
    void setName(std::string value) { name = std::move(value); }
    bool isNamed() const { return !name.empty() && name != "anonymous"; }
    bool visible = true;
    Transform3D transform;
    WorldScene* scene = nullptr; // Borrowed typed render membership.
    void setScene(WorldScene* value);
    bool draw_parts = true;
};
