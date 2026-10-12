#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "compat/compiler.h"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srMath.h"
#include "surrender/srModel.h"
#include "surrender/srTypeRegistry.h"

class srGERD;
class srMaterial;
class stMaterial;
class srTextureIFace;
class stTextureAnim;
class srNode;

/* Engine Code\stModelInstance.cpp. */
// VTABLE: WIZ8 0x005ec7d0 stModelInstance
/* The following are the construction-phase tables for the support base. */
// VTABLE: WIZ8 0x005ec814 srClassSupport<stModelInstance,srModelInstance,65540>
class stModelInstance : public srClassSupport<stModelInstance, srModelInstance, 0x10004> {
public:
    enum { RENDER_LIGHTING_BAKED = 0x02u, RENDER_SHADOW = 0x08u, RENDER_NO_PICK = 0x10u };

    static const char* sGetClassName()
    {
        return "stModelInstance";
    }

    explicit stModelInstance(srNode* parent, Ownership ownership = Ownership::parent);                 /* 0x0047EC80 */
    stModelInstance& operator=(const stModelInstance& other); /* 0x0047EDF0 */

    /* Queues opaque models ahead of their siblings and sorted-rendering
       models after them. */
    virtual void traverse(TraverseInfo& info) override;                         /* 0x004803F0 */
    virtual void process(const ProcessInfo& info, e_processType type) override; /* 0x0047F560 */
    /* Mesh-chain submit reached from process(): frustum culling, ambient and
       highlight state, the linked stMeshModel list, then the optional
       highlight shell pass with reversed winding. */
    void RenderMeshes(srGERD& renderer);
    /* Shadow-volume extrusion helper for the first mesh in the chain. */
    void RenderShadow(srGERD& renderer, srMeshModel::TriMesh& mesh);

    stTextureAnim* FindMouthTexture(); /* 0x00481080 */
    std::int32_t AddDamageStage(const char* name);
    std::int32_t AddExistingDamageStage(const char* name);
    std::int32_t FindDamageStage(const char* name);
    std::uint8_t ReplaceDamageStageTexture(std::int32_t stage, const char* old_name,
                                            const std::shared_ptr<srTextureIFace>& replacement);

public:
    std::uint32_t overlay_scene_flag;
    glm::vec4 highlight_colour;
    /* Lazily built highlight material; RenderMeshes fills it from the
       render-state RGBA and installs it as the pass material. */
    std::shared_ptr<srMaterial> highlight_material;
    std::uint32_t render_flags;
    std::int32_t mesh_index;
    std::uint32_t frame_index;
    std::int32_t damage_stage;
    std::vector<std::int32_t> damage_stage_tables;
    std::int32_t highlight_pass_mode;
    glm::vec3 light_scale;
    bool diffuse_scale_enabled;
    bool emissive_override_enabled;
    std::uint8_t padding_1a2[2];
    float diffuse_scale;
    float emissive_override;
    float frame_interpolation;
};


/* Concrete 2D model instance. Slot 5 and the secondary slot-0 adjustor are
   SYNTHETIC compiler-generated deleting destructors; no source body owns
   either address. */
// VTABLE: WIZ8 0x005ec858 srClassSupport<srModelInstance, class srNode, 4352>
class stModelInstance2D
    : public srClassSupport<stModelInstance2D, srModelInstance, 0x10005> {
public:
    static const char* sGetClassName()
    {
        return "stModelInstance2D";
    }

    explicit stModelInstance2D(srNode* parent); /* 0x0047F0F0 */

    stModelInstance2D& operator=(const stModelInstance2D& other); /* 0x0047F290 */
    void SetModel(std::shared_ptr<srModel> model);                                /* 0x0047F3A0 */

    void process(const ProcessInfo& info, e_processType type) override; /* 0x00480920 */
    std::uint16_t GetScaledWidth();                                    /* 0x00480EF0 */
    std::uint16_t GetScaledHeight();                                   /* 0x00480F70 */
    void SetGlowEnabled(bool enable);                                   /* 0x00480EB0 */
    void SetGlowColors(const glm::vec4& first, const glm::vec4& second);

    std::uint8_t displayState() const
    {
        return render_state.display_state;
    }
    void configure2D(std::uint16_t width, std::uint16_t height)
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
    void setRenderDepth(std::uint32_t depth)
    {
        render_state.render_depth = depth;
    }

    std::uint32_t overlay_scene_flag;
    W8ModelInstance2DRenderState render_state;
    std::optional<glm::vec4> glow_color_base;
    std::optional<glm::vec4> glow_color_peak;
    /* Lazily copied from the mesh material while the glow is enabled. */
    std::shared_ptr<stMaterial> glow_material;
};
