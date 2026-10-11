#include "wiz8/engine_code/ReadLevel.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "glm/ext/vector_float4.hpp"
#include "surrender/srModel.h"
#include "surrender/srTypeRegistry.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/float_constants.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srNode.h"
#include "surrender/srTriMeshPipeline.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Video2.h"
#include "surrender/srMath.h"
#include "AnimRep.hpp"
#include "compat/native.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srShader.h"
#include "surrender/srTextureIFace.h"

#define ST_MODEL_INSTANCE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\stModelInstance.cpp"

extern float g_monster_light_scale;

/* Scratch vertex store shared by every highlight shell submission; grown
   on demand and kept between frames. */
// GLOBAL: WIZ8 0x0065A148
static std::vector<glm::vec3> g_vertex_scratch;

// VTABLE: WIZ8 0x005ec89c srClassSupport<srModelInstance, class srNode, 4352>
// class srClassSupport<stModelInstance2D, class srModelInstance, 65541>
/* Find the animated texture assigned to polygons whose runtime name begins
   with "mouth". Damage-stage instances use the stage-specific texture table;
   ordinary instances use the mesh's active polygon texture table. */
// FUNCTION: WIZ8 0x00481080
stTextureAnim* stModelInstance::FindMouthTexture()
{
    stMeshModel* mesh = static_cast<stMeshModel*>(getModel());
    while (mesh != nullptr) {
        std::shared_ptr<srTextureIFace>* textures =
            damage_stage == -1 ? mesh->getPolyTexture(0, 0, 0)
                               : mesh->GetTextureTable(damage_stage_tables[damage_stage]);
        if (textures != nullptr) {
            for (int polygon = 0; polygon < mesh->polygon_count; ++polygon) {
                auto* animation = sr_cast<stTextureAnim>(textures[polygon]);
                if (animation != nullptr &&
                    _strnicmp(animation->getName().c_str(), "mouth", 5) == 0) {
                    return animation;
                }
            }
        }
        mesh = mesh->Next();
    }
    return nullptr;
}

// FUNCTION: WIZ8 0x00480790
int stModelInstance::FindDamageStage(const char* name)
{
    stMeshModel* mesh = static_cast<stMeshModel*>(getModel());
    return mesh->FindSkinTable(name);
}

/* The scene entry is queued before the sibling chain is walked for an opaque
   model, and after it for a model flagged W8_MESH_SORTED_RENDERING, so the
   sorted models are processed after every opaque sibling, in reverse sibling
   order. Children follow unless the node terminates. */
// FUNCTION: WIZ8 0x004803F0
void stModelInstance::traverse(TraverseInfo& info)
{
    stMeshModel* model = static_cast<stMeshModel*>(getModel());

    if (!testFlag(FLAG_DISABLE) && model != 0 && (model->flags & W8_MESH_SORTED_RENDERING) == 0) {
        info.entries.push_back({this, 0});
    }

    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }

    if (!testFlag(FLAG_DISABLE) && model != 0 && (model->flags & W8_MESH_SORTED_RENDERING) != 0) {
        info.entries.push_back({this, 0});
    }

    if (!testFlag(FLAG_TERMINATE) && first_child_ != 0) {
        first_child_->traverse(info);
    }
}

/* Add a stage by cloning the first stage's table across the complete linked
   mesh chain. The instance stores the table id shared by that chain. */
// FUNCTION: WIZ8 0x00480560
int stModelInstance::AddDamageStage(const char* name)
{
    stMeshModel* mesh = static_cast<stMeshModel*>(getModel());

    if (mesh->FindSkinTable(name) != -1) {
        return -1;
    }

    int stage = static_cast<int>(damage_stage_tables.size());
    int base_table = stage > 0 ? damage_stage_tables[0] : -1;
    damage_stage_tables.push_back(mesh->CreateSkinTable(name, base_table));
    for (mesh = mesh->Next(); mesh != 0; mesh = mesh->Next()) {
        mesh->CreateSkinTable(name, base_table);
    }
    return stage;
}

// FUNCTION: WIZ8 0x00480670
int stModelInstance::AddExistingDamageStage(const char* name)
{
    stMeshModel* mesh = static_cast<stMeshModel*>(getModel());
    int table = mesh->FindSkinTable(name);

    if (table == -1) {
        return -1;
    }

    int stage = static_cast<int>(damage_stage_tables.size());
    damage_stage_tables.push_back(table);
    return stage;
}

// FUNCTION: WIZ8 0x004807b0
unsigned char stModelInstance::ReplaceDamageStageTexture(
    int stage, const char* old_name, const std::shared_ptr<srTextureIFace>& replacement)
{
    stMeshModel* mesh = static_cast<stMeshModel*>(getModel());
    bool replaced = false;

    if (auto* animation = sr_cast<stTextureAnim>(replacement)) {
        animation->Prepare();
    }

    for (; mesh != 0; mesh = mesh->Next()) {
        std::shared_ptr<srTextureIFace>* textures = mesh->GetTextureTable(damage_stage_tables[stage]);
        if (textures == 0) {
            continue;
        }

        for (int polygon = 0; polygon < mesh->polygon_count; ++polygon) {
            srTextureIFace* texture = textures[polygon].get();
            if (!sr_cast<stTextureFile>(texture) && !sr_cast<stTextureAnim>(texture)) {
                continue;
            }

            if (_stricmp(texture->getName().c_str(), old_name) == 0) {
                replaced = true;
                while (polygon < mesh->polygon_count && textures[polygon].get() == texture) {
                    textures[polygon] = replacement;
                    ++polygon;
                }
            } else {
                while (polygon < mesh->polygon_count && textures[polygon].get() == texture) {
                    ++polygon;
                }
            }
            --polygon;
        }
    }
    return replaced;
}

/* Retail's Video2, dialogue and radar callers all call this one emitted body,
   which precedes the rest of the stModelInstance2D lifecycle family. It is an
   ordinary stModelInstance.cpp definition, not a header/template emission. */
// FUNCTION: WIZ8 0x0047F0F0
stModelInstance2D::stModelInstance2D(srNode* parent)
    : srClassSupport<stModelInstance2D, srModelInstance, 0x10005>(static_cast<srNode*>(0))
{
    render_state.display_state = 0;
    render_state.width = 0;
    render_state.height = 0;
    render_state.position_x = 0;
    render_state.position_y = 0;
    overlay_scene_flag = 0;
    render_state.glow_enabled = false;
    render_state.render_depth = 2000;
    if (parent != 0) {
        setParent(parent, 1);
    }
}

/* Deliberately not a whole glm::vec4 assignment. Retail
   copies the 2D fields around the parent/state work and leaves the upper two
   bytes of state untouched; GrCycle's separate whole-block copy is a
   different operation. */
// FUNCTION: WIZ8 0x0047F290
stModelInstance2D& stModelInstance2D::operator=(const stModelInstance2D& other)
{
    srModelInstance::operator=(other);
    render_state.display_state = other.render_state.display_state;
    render_state.width = other.render_state.width;
    render_state.height = other.render_state.height;
    render_state.position_x = other.render_state.position_x;
    render_state.position_y = other.render_state.position_y;
    overlay_scene_flag = other.overlay_scene_flag;
    if (other.parent_ != 0) {
        setParent(other.parent_, 1);
    }
    render_state.glow_enabled = other.render_state.glow_enabled;
    render_state.render_depth = other.render_state.render_depth;
    if (other.glow_color_base) {
        glow_color_base = other.glow_color_base;
    }
    if (other.glow_color_peak) {
        glow_color_peak = other.glow_color_peak;
    }
    return *this;
}

// FUNCTION: WIZ8 0x0047F3A0
void stModelInstance2D::SetModel(std::shared_ptr<srModel> model)
{
    setModel(model);
    if (model != 0) {
        /* Retail enables the auto-sphere and auto-box skip flags separately. */
        static_cast<srMeshModel*>(model.get())->enable(srMeshModel::CONTROL_SKIP_AUTO_SPHERE);
        static_cast<srMeshModel*>(model.get())->enable(srMeshModel::CONTROL_SKIP_AUTO_BOX);
    }
}

/* Render the instance through SurRender's detached TriMesh value. Aligned
   instances retain their screen-facing orientation while preserving the
   current view matrix's translation, scale and handedness. The optional glow
   path clones the mesh material once, oscillates between the two configured
   emissive colors, and overrides only this draw's material and first shader. */
// FUNCTION: WIZ8 0x00480920
void stModelInstance2D::process(const ProcessInfo& info, e_processType)
{
    srGERD* renderer = info.renderer;

    const auto scope = scopedRender(*renderer, false);

    srMeshModel* model = static_cast<srMeshModel*>(getModel());
    auto mesh = model->getTriMesh();

    if (render_state.glow_enabled) {
        if (glow_material == 0) {
            glow_material = std::make_shared<stMaterial>();
            if (mesh.materials[0][0] != 0) {
                glow_material->srMaterial::operator=(*mesh.materials[0][0]);
            }
        }

        float glow_weight = static_cast<float>(
            fabs(sin(((w8_get_ticks() % render_state.render_depth) /
                      static_cast<double>(static_cast<int>(render_state.render_depth))) *
                     g_camera_angle_period)));
        float base_weight = g_float_one - glow_weight;
        /* Each weighted colour is rounded to float before the sum. */
        glm::vec4 emissive =
            *glow_color_base * base_weight + *glow_color_peak * glow_weight;
        emissive.w = g_float_one;
        glow_material->setEmissive(emissive);
        mesh.materials[0][0] = glow_material;
        mesh.shaders[0].value = (mesh.shaders[0].value & ~srShader::MASK_GRADIENT_MODULATE) |
                                srShader::MASK_GRADIENT_ADD;
    }

    model->renderTriMesh(*renderer, mesh);
}

/* Disabling the glow releases the retained glow material; the render-state
   byte at 0x0d is the glow pass's enable flag. */
// FUNCTION: WIZ8 0x00480EB0
void stModelInstance2D::SetGlowEnabled(bool enable)
{
    if (!enable) {
        glow_material.reset();
    }
    render_state.glow_enabled = enable;
}

/* Scaled 2D extent used by the tooltip and cursor placement code. A unit
   scale returns the stored screen extent directly; otherwise the matching
   axis scale from the node is applied and truncated. */
// FUNCTION: WIZ8 0x00480EF0
unsigned short stModelInstance2D::GetScaledWidth()
{
    srVector3T<double> scale = getScale();
    float scale_z = static_cast<float>(scale.z);
    if (scale.x == 1.0f && scale.y == 1.0f && scale_z == 1.0f) {
        return render_state.width;
    }
    return static_cast<unsigned short>(render_state.width * scale.x);
}

// FUNCTION: WIZ8 0x00480F70
unsigned short stModelInstance2D::GetScaledHeight()
{
    srVector3T<double> scale = getScale();
    float scale_y = static_cast<float>(scale.y);
    float scale_z = static_cast<float>(scale.z);
    if (scale.x == 1.0f && scale_y == 1.0f && scale_z == 1.0f) {
        return render_state.height;
    }
    return static_cast<unsigned short>(render_state.height * scale_z);
}

/* The process pass blends between the two glow colors into the material's
   emissive term. */
// FUNCTION: WIZ8 0x00480FF0
void stModelInstance2D::SetGlowColors(const glm::vec4& first, const glm::vec4& second)
{
    glow_color_base = first;
    glow_color_peak = second;
}

// FUNCTION: WIZ8 0x0047EC80
stModelInstance::stModelInstance(srNode* parent, Ownership ownership)
    : srClassSupport<stModelInstance, srModelInstance, 0x10004>(static_cast<srNode*>(nullptr), ownership)
{
    highlight_colour = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    render_flags = 0;
    mesh_index = -1;
    frame_index = 0;
    highlight_pass_mode = 0;
    if (parent != 0) {
        setParent(parent, 1);
    }
    damage_stage = -1;
    light_scale = glm::vec3(1);
    diffuse_scale_enabled = false;
    diffuse_scale = 0.0f;
    emissive_override_enabled = false;
    emissive_override = 0.0f;
    frame_interpolation = 0.0f;
}

// FUNCTION: WIZ8 0x0047EDF0
stModelInstance& stModelInstance::operator=(const stModelInstance& other)
{
    srModelInstance::operator=(other);
    highlight_colour = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    render_flags = other.render_flags;
    mesh_index = other.mesh_index;
    frame_index = other.frame_index;

    damage_stage_tables = other.damage_stage_tables;
    damage_stage = other.damage_stage;
    highlight_pass_mode = other.highlight_pass_mode;
    highlight_material.reset();
    light_scale = other.light_scale;
    diffuse_scale = 0.0f;
    diffuse_scale_enabled = false;
    emissive_override_enabled = other.emissive_override_enabled;
    emissive_override = other.emissive_override;
    frame_interpolation = 0.0f;
    return *this;
}

/* Apply the instance transform for the render pass. Unaligned instances take
   the ordinary world-space matrix; aligned instances rebuild the model-view
   matrix from the camera basis so the model stays screen-facing while
   inheriting the view's translation, scale and handedness. */
// FUNCTION: WIZ8 0x0047F560
void stModelInstance::process(const ProcessInfo& info, e_processType)
{
    const auto scope = scopedRender(*info.renderer);
    RenderMeshes(*info.renderer);
}

/* Submit the instance's linked mesh chain. Pass one fills a detached TriMesh
   per model — animated vertex buffers when the model requests them, the
   damage-stage texture/active-polygon tables, the lazily built highlight
   material or the global material overrides — and pushes each mesh through
   RenderTriMeshWithEquations. When the highlight RGBA is configured
   the chain is submitted again inflated along its normals into a shared
   scratch buffer with reversed winding, skipping the linked "blank" skin. */
// FUNCTION: WIZ8 0x0047F930
void stModelInstance::RenderMeshes(srGERD& renderer)
{
    srMeshModel::TriMesh mesh;
    mesh.control_flags = 0;

    stMeshModel* model = static_cast<stMeshModel*>(getModel());
    stMeshModel* first_model = model;

    const auto bounds = model->getBounds();
    if (!model->isVisible(renderer)) return;

    // c-style-cast-ok: the pick key is an opaque void* token
    SetPickKey((render_flags & RENDER_NO_PICK) != 0 ? (void*)0 : (void*)this);

    const auto ambient = renderer.getAmbientLight();
    const glm::vec3 ambient_color(ambient);

    glm::vec4 light;
    if (!model->vertex_lighting_ready) {
        light = glm::vec4((ambient_color * light_scale + g_environment_offset) *
                              g_monster_light_scale,
                          1);
    } else {
        light = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    renderer.setAmbientLight(light);

    if (highlight_colour.x != g_float_zero || highlight_colour.y != g_float_zero ||
        highlight_colour.z != g_float_zero || highlight_colour.w != g_float_zero) {
        if (highlight_material == 0) {
            highlight_material = std::make_shared<srMaterial>();
            glm::vec4 zero;
            zero = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            highlight_material->setAmbient(zero);
            highlight_material->setDiffuse(zero);
            highlight_material->setSpecular(zero);
            highlight_material->setOpacity(0.35);
        }
        highlight_material->setEmissive(highlight_colour);
    }

    bool first_pass = true;
    srNode* child = this;
    while (model != 0) {
        model->SetAmbientColor(ambient_color);
        mesh = model->getTriMesh();
        if ((render_flags & RENDER_SHADOW) != 0 && first_pass) {
            RenderShadow(renderer, mesh);
        }

        std::span<const glm::vec3> poly_normals;
        if ((model->flags & W8_MESH_HAS_FRAME_STORAGE) != 0) {
            const auto frame = model->frameGeometry(frame_index, frame_interpolation);
            mesh.positions = frame.positions.data();
            mesh.normals = frame.normals.data();
            poly_normals = frame.polygon_normals;
        }

        if (g_render_untextured) {
            mesh.shaders[0].value &= 0xffff73ff;
            mesh.poly_shaders[0] = 0;
            mesh.poly_textures[0][0] = 0;
            mesh.poly_uv[0] = 0;
            mesh.texcoords[0][0] = 0;
        }
        if (g_render_unlit) {
            mesh.dig[0] = 0;
        }
        std::shared_ptr<srTextureIFace>*(*poly_textures)[2] = mesh.poly_textures;
        if (poly_textures != 0 && !mesh.active_polygons) {
            srMeshModel::PolygonSelection active;
            if (damage_stage >= 0) {
                mesh.poly_textures[0][0] =
                    model->GetTextureTable(damage_stage_tables[damage_stage]);
                active = model->GetActivePolygons(damage_stage_tables[damage_stage], true);
            } else {
                active = model->GetActivePolygons(-1, true);
            }
            if (active) {
                mesh.active_polygons = active;
            }
        }

        if (((highlight_colour.x == g_float_zero) && (highlight_colour.y == g_float_zero) &&
             (highlight_colour.z == g_float_zero) && (highlight_colour.w == g_float_zero)) ||
            (highlight_pass_mode != 1)) {
            if (diffuse_scale_enabled) {
                g_material_diffuse_scale = diffuse_scale;
                mesh.shaders[0].value = (mesh.shaders[0].value & 0xffffd7bf) | 0x44a0;
                mesh.control_flags |= (1UL << srMeshModel::CONTROL_SORTED_RENDERING);
                g_material_diffuse_scale_enabled = true;
            }
            if (emissive_override_enabled) {
                g_material_emissive_override = emissive_override;
                g_material_emissive_override_enabled = true;
            }
        } else {
            mesh.vertex_materials[0][0] = 0;
            mesh.materials[0][0] = highlight_material;
        }

        if ((render_flags & RENDER_NO_PICK) != 0 && !renderer.isPickStackEmpty()) {
            srGERD::Pick pick;
            renderer.popPick(pick);
            model->RenderTriMeshWithEquations(renderer, mesh, poly_normals);
            renderer.pushPick(pick);
        } else {
            model->RenderTriMeshWithEquations(renderer, mesh, poly_normals);
        }

        first_pass = false;
        srNode* previous = child;
        if (child == 0 || child->testFlag(FLAG_TERMINATE) == 0) {
            model = model->Next();
            if (previous != 0) {
                child = previous->first_child_;
            }
        } else {
            model = 0;
        }
        if (diffuse_scale_enabled) {
            g_material_diffuse_scale_enabled = false;
        }
        if (emissive_override_enabled) {
            g_material_emissive_override_enabled = false;
        }
    }

    if (highlight_colour.x != g_float_zero || highlight_colour.y != g_float_zero ||
        highlight_colour.z != g_float_zero || highlight_colour.w != g_float_zero) {
        model = static_cast<stMeshModel*>(getModel());
        if (highlight_pass_mode == 0) {
            renderer.setWinding(srGERD::WINDING_POSITIONAL_1);
        }
        while (model != 0) {
            mesh = model->getTriMesh();
            if (((model->flags & W8_MESH_SORTED_RENDERING) == 0) || (model == first_model)) {
                if (mesh.poly_textures[0][0] != 0 ||
                    ((mesh.textures[0][0] != 0) &&
                     (_strnicmp("blank", mesh.textures[0][0]->getName().c_str(), 5) != 0))) {
                    g_vertex_scratch.resize(mesh.vertex_count);

                    std::span<const glm::vec3> poly_normals;
                    if ((model->flags & W8_MESH_HAS_FRAME_STORAGE) != 0) {
                        const auto frame = model->frameGeometry(frame_index, frame_interpolation);
                        mesh.dig[0] = frame.positions.data();
                        mesh.dig[1] = frame.normals.data();
                        poly_normals = frame.polygon_normals;
                    }
                    std::shared_ptr<srTextureIFace>*(*poly_textures)[2] = mesh.poly_textures;
                    if (poly_textures != 0 && !mesh.active_polygons) {
                        srMeshModel::PolygonSelection active;
                        if (damage_stage >= 0) {
                            active = model->GetActivePolygons(damage_stage_tables[damage_stage], true);
                        } else {
                            active = model->GetActivePolygons(-1, true);
                        }
                        if (active) {
                            mesh.active_polygons = active;
                        }
                    }

                    mesh.materials[0][0] = highlight_material;
                    mesh.shaders[1].value = (mesh.shaders[0].value & 0xffff5cb7) | 0x40a0;
                    mesh.poly_shaders[1] = 0;
                    mesh.poly_textures[0][1] = 0;
                    mesh.vertex_materials[0][1] = 0;
                    mesh.poly_uv[1] = 0;

                    double factor = g_double_one / getScale().y * g_double_fifteen_and_five_eighths;
                    float expand = static_cast<float>(
                        (bounds.sphere.radius * getScale().y * g_double_one_thousandth + g_double_one) * factor);
                    const glm::vec3 offsets(expand);

                    if (mesh.vertex_count != 0) {
                        if (offsets.x == g_float_zero && offsets.y == g_float_zero &&
                            offsets.z == g_float_zero) {
                            std::fill_n(g_vertex_scratch.data(), mesh.vertex_count,
                                        glm::vec3(0));
                        } else {
                            for (std::size_t i = 0; i < mesh.vertex_count; ++i)
                                (g_vertex_scratch.data())[i] = offsets * mesh.normals[i];
                        }
                    }
                    std::transform(g_vertex_scratch.begin(), g_vertex_scratch.end(),
                                   mesh.positions, g_vertex_scratch.begin(),
                                   [](const auto& offset, const auto& position) {
                                       return offset + position;
                                   });
                    mesh.positions = g_vertex_scratch.data();
                    mesh.control_flags |= (1UL << srMeshModel::CONTROL_SORTED_RENDERING);

                    if ((render_flags & RENDER_NO_PICK) != 0 && !renderer.isPickStackEmpty()) {
                        srGERD::Pick pick;
                        renderer.popPick(pick);
                        model->RenderTriMeshWithEquations(renderer, mesh, poly_normals);
                        renderer.pushPick(pick);
                    } else {
                        model->RenderTriMeshWithEquations(renderer, mesh, poly_normals);
                    }
                }
            }

            srNode* previous = child;
            if (child != 0 && child->testFlag(FLAG_TERMINATE) != 0) {
                break;
            }
            model = model->Next();
            if (previous != 0) {
                child = previous->first_child_;
            }
        }
        if (highlight_pass_mode == 0) {
            renderer.setWinding(srGERD::WINDING_POSITIONAL_0);
        }
    }
    renderer.setAmbientLight(ambient);
}

// GLOBAL: WIZ8 0x005EC8E0
const float g_shadow_extrusion_pitch_scale = 1.0f / 1500.0f;

/* Shared shadow quad: two upright triangles on a 500-unit ground span, lit by
   a dedicated material so the extruded shadow pass submits through the
   ordinary TriMesh pipeline. */
static const srVector3i g_shadow_triangles[2] = {{0, 1, 2}, {3, 4, 5}};
static const glm::vec3 g_shadow_positions[6] = {
    {-250.0f, 250.0f, 0.0f}, {0.0f, -250.0f, 0.0f}, {250.0f, 250.0f, 0.0f},
    {0.0f, 250.0f, -250.0f}, {0.0f, -250.0f, 0.0f}, {0.0f, 250.0f, 250.0f}};

static stMaterial& ShadowMaterial()
{
    static const std::unique_ptr<stMaterial> material = [] {
        auto created = std::make_unique<stMaterial>();
        glm::vec4 color;
        color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        created->setAmbient(color);
        color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        created->setSpecular(color);
        created->parms.shininess = 1.0f;
        created->dirty = 1;
        created->setEmissive(color);
        color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        created->setDiffuse(color);
        created->m_surface_flags = 0;
        return created;
    }();
    return *material;
}

/* Submit the shared shadow-quad mesh extruded over the source mesh's height:
   front-cull the pass, translate to the mesh's mid-height, scale the unit
   quad by a clamped pitch factor, then feed the pipeline slots directly. */
// FUNCTION: WIZ8 0x004811D0
void stModelInstance::RenderShadow(srGERD& renderer, srMeshModel::TriMesh& mesh)
{
    float height = mesh.bounds_maximum.y - mesh.bounds_minimum.y;
    renderer.pushEnable();
    renderer.setCullMode(srGERD::CULL_NONE);
    if (!renderer.isEnabled(srGERD::ENABLE_SORTED_RENDERING)) {
        renderer.toggle(srGERD::ENABLE_SORTED_RENDERING);
    }
    renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
    renderer.pushMatrix();
    renderer.translate(0.0, height * g_float_half, 0.0);
    height *= g_shadow_extrusion_pitch_scale;
    if (height < g_float_half) {
        height = g_float_half;
    }
    double scale = height;
    renderer.scale(scale, scale, scale);

    srTriMeshPipeline* pipeline = srTriMeshPipeline::Get(&renderer);
    pipeline->triangle_count = std::size(g_shadow_triangles);
    pipeline->triangles = g_shadow_triangles;
    pipeline->vertex_count = std::size(g_shadow_positions);
    pipeline->positions = g_shadow_positions;
    pipeline->vertex_extras = nullptr;
    pipeline->records.resize(2);
    pipeline->passes.resize(2);
    pipeline->PrepareSlot();
    pipeline->current_record->flags = 0;
    pipeline->current_pass->shaders = 0;
    pipeline->current_pass->texture_tables[0] = 0;
    pipeline->current_pass->texture_tables[1] = 0;
    pipeline->material = &ShadowMaterial();
    pipeline->current_record->material = pipeline->material;
    srShader shader;
    shader.value = 0x44b3;
    pipeline->SetFlags(shader);
    ++pipeline->slot_count;
    pipeline->PrepareSlot();
    pipeline->FlushIfCurrent();
    renderer.popMatrix();
    renderer.popEnable();
}
