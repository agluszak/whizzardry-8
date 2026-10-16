#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModel.h"
#include "surrender/srModelInstance.h"
#include "wiz8/engine_code/AnimRep.hpp"

class srGERD;
class srMaterial;
class stMaterial;
class srTextureIFace;
class stTextureAnim;
class srNode;

/* Engine Code\stModelInstance.cpp. */
// VTABLE: WIZ8 0x005ec7d0 stModelInstance
/* The following are the construction-phase tables for the support base. */
class stModelInstance : public srModelInstance {
public:
    enum { RENDER_LIGHTING_BAKED = 0x02u, RENDER_SHADOW = 0x08u, RENDER_NO_PICK = 0x10u };

    explicit stModelInstance(srNode* parent,
                             Ownership ownership = Ownership::parent); /* 0x0047EC80 */
    stModelInstance& operator=(const stModelInstance& other); /* 0x0047EDF0 */

    /* Queues opaque models ahead of their siblings and sorted-rendering
       models after them. */
    virtual void traverse(TraverseInfo& info) override; /* 0x004803F0 */
    virtual void process(const ProcessInfo& info, e_processType type) override; /* 0x0047F560 */
    /* Mesh-chain submit reached from process(): frustum culling, ambient and
       highlight state, the linked stMeshModel list, then the optional
       highlight shell pass with reversed winding. */
    void RenderMeshes(srGERD& renderer);
    /* Shadow-volume extrusion helper for the first mesh in the chain. */
    void RenderShadow(srGERD& renderer, srMeshModel::TriMesh& mesh);

    stTextureAnim* FindMouthTexture(); /* 0x00481080 */
    int AddDamageStage(const char* name);
    int AddExistingDamageStage(const char* name);
    int FindDamageStage(const char* name);
    unsigned char ReplaceDamageStageTexture(int stage, const char* old_name,
                                            const std::shared_ptr<srTextureIFace>& replacement);

public:
    w8_ulong overlay_scene_flag;
    glm::vec4 highlight_colour;
    /* Lazily built highlight material; RenderMeshes fills it from the
       render-state RGBA and installs it as the pass material. */
    std::shared_ptr<srMaterial> highlight_material;
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
};

/* Concrete 2D model instance. Slot 5 and the secondary slot-0 adjustor are
   SYNTHETIC compiler-generated deleting destructors; no source body owns
   either address. */
class stModelInstance2D : public srModelInstance {
public:
    explicit stModelInstance2D(srNode* parent); /* 0x0047F0F0 */

    stModelInstance2D& operator=(const stModelInstance2D& other); /* 0x0047F290 */
    void SetModel(std::shared_ptr<srModel> model); /* 0x0047F3A0 */

    void process(const ProcessInfo& info, e_processType type) override; /* 0x00480920 */
    unsigned short GetScaledWidth(); /* 0x00480EF0 */
    unsigned short GetScaledHeight(); /* 0x00480F70 */
    void SetGlowEnabled(bool enable); /* 0x00480EB0 */
    void SetGlowColors(const glm::vec4& first, const glm::vec4& second);

    unsigned char displayState() const { return render_state.display_state; }
    void configure2D(unsigned short width, unsigned short height)
    {
        overlay_scene_flag = 0;
        render_state.render_depth = 2000;
        render_state.width = width;
        render_state.height = height;
        render_state.position_x = 0;
        render_state.position_y = 0;
        render_state.display_state = 0;
        render_state.glow_enabled = false;
        glow_color_base.reset();
        glow_color_peak.reset();
        glow_material.reset();
    }
    void setRenderDepth(w8_ulong depth) { render_state.render_depth = depth; }

    w8_ulong overlay_scene_flag;
    W8ModelInstance2DRenderState render_state;
    std::optional<glm::vec4> glow_color_base;
    std::optional<glm::vec4> glow_color_peak;
    /* Lazily copied from the mesh material while the glow is enabled. */
    std::shared_ptr<stMaterial> glow_material;
};
