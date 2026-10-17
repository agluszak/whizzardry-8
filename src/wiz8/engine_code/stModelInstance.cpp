#include "wiz8/engine_code/stModelInstance.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL_stdinc.h>

#include "AnimRep.hpp"
#include "compat/native.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "surrender/mesh_resources.h"
#include "surrender/mesh_submission.h"
#include "surrender/model_view_scope.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterial.h"
#include "surrender/srModel.h"
#include "surrender/srNode.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "surrender/srTriMeshPipeline.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/gameplay_constants.h"

constexpr float kShadowExtrusionPitchScale = 1.0f / 1500.0f;

#define ST_MODEL_INSTANCE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\stModelInstance.cpp"

extern float g_monster_light_scale;

/* Scratch vertex store shared by every highlight shell submission; grown
   on demand and kept between frames. */
// GLOBAL: WIZ8 0x0065A148
static std::vector<glm::vec3> g_vertex_scratch;

/* Find the animated texture assigned to polygons whose runtime name begins
   with "mouth". Damage-stage instances use the stage-specific texture table;
   ordinary instances use the mesh's active polygon texture table. */
// FUNCTION: WIZ8 0x00481080
stTextureAnim* stModelInstance::FindMouthTexture()
{
    stMeshModel* mesh = static_cast<stMeshModel*>(getModel());
    while (mesh != nullptr) {
        MeshResources<srTexture>* textures =
            damage_stage == -1 ? mesh->getPolygonTextureTable(0, 0, false)
                               : mesh->GetTextureTable(damage_stage_tables[damage_stage]);
        if (textures != nullptr) {
            for (int polygon = 0; polygon < mesh->polygon_count; ++polygon) {
                auto* animation = dynamic_cast<stTextureAnim*>((*textures)[polygon].get());
                if (animation != nullptr &&
                    SDL_strncasecmp(animation->getName().c_str(), "mouth", 5) == 0) {
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
unsigned char
stModelInstance::ReplaceDamageStageTexture(int stage, const char* old_name,
                                           const std::shared_ptr<srTexture>& replacement)
{
    stMeshModel* mesh = static_cast<stMeshModel*>(getModel());
    bool replaced = false;

    if (auto* animation = dynamic_cast<stTextureAnim*>(replacement.get())) {
        animation->Prepare();
    }

    for (; mesh != 0; mesh = mesh->Next()) {
        MeshResources<srTexture>* textures = mesh->GetTextureTable(damage_stage_tables[stage]);
        if (textures == 0) {
            continue;
        }

        for (int polygon = 0; polygon < mesh->polygon_count; ++polygon) {
            srTexture* texture = (*textures)[polygon].get();
            if (!dynamic_cast<stTextureFile*>(texture) && !dynamic_cast<stTextureAnim*>(texture)) {
                continue;
            }

            if (SDL_strcasecmp(texture->getName().c_str(), old_name) == 0) {
                replaced = true;
                while (polygon < mesh->polygon_count && (*textures)[polygon].get() == texture) {
                    textures->set(polygon, replacement);
                    ++polygon;
                }
            } else {
                while (polygon < mesh->polygon_count && (*textures)[polygon].get() == texture) {
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
stModelInstance2D::stModelInstance2D(srNode* parent) : srModelInstance(static_cast<srNode*>(0))
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
            glow_material = std::make_shared<srMaterial>();
            if (mesh.materials[0][0] != 0) {
                glow_material->srMaterial::operator=(*mesh.materials[0][0]);
            }
        }

        float glow_weight = static_cast<float>(
            fabs(sin(((w8_get_ticks() % render_state.render_depth) /
                      static_cast<double>(static_cast<int>(render_state.render_depth))) *
                     kCameraAnglePeriod)));
        float base_weight = 1.0f - glow_weight;
        /* Each weighted colour is rounded to float before the sum. */
        glm::vec4 emissive = *glow_color_base * base_weight + *glow_color_peak * glow_weight;
        emissive.w = 1.0f;
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
    glm::dvec3 scale = getScale();
    float scale_z = static_cast<float>(scale.z);
    if (scale.x == 1.0f && scale.y == 1.0f && scale_z == 1.0f) {
        return render_state.width;
    }
    return static_cast<unsigned short>(render_state.width * scale.x);
}

// FUNCTION: WIZ8 0x00480F70
unsigned short stModelInstance2D::GetScaledHeight()
{
    glm::dvec3 scale = getScale();
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
stModelInstance::stModelInstance(srNode* parent) : srModelInstance(static_cast<srNode*>(nullptr))
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
   material or the instance material overrides — and pushes each mesh through
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

    const srMeshSubmission::AmbientScope lighting(renderer);
    const auto ambient = lighting.ambient();
    const glm::vec3 ambient_color(ambient);

    glm::vec4 light;
    if (!model->vertex_lighting_ready) {
        light = glm::vec4(
            (ambient_color * light_scale + g_environment_offset) * g_monster_light_scale, 1);
    } else {
        light = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    renderer.setAmbientLight(light);

    if (highlight_colour.x != 0.0f || highlight_colour.y != 0.0f || highlight_colour.z != 0.0f ||
        highlight_colour.w != 0.0f) {
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

    const auto drawLinked = [&](auto draw) {
        srNode* child = this;
        for (auto* linked = first_model; linked; linked = linked->Next()) {
            draw(linked, linked == first_model);
            if (child && child->testFlag(FLAG_TERMINATE)) break;
            if (child) child = child->getChild();
        }
    };
    const auto selectPolygons = [&](stMeshModel& linked, srMeshModel::TriMesh& selected,
                                    bool replace_damage_texture) {
        if (selected.active_polygons) return;
        const int table = damage_stage >= 0 ? damage_stage_tables.at(damage_stage) : -1;
        if (replace_damage_texture && damage_stage >= 0)
            selected.poly_textures[0][0] = linked.GetTextureTable(table);
        if (const auto active = linked.GetActivePolygons(table, true))
            selected.active_polygons = active;
    };
    const auto submit = [&](stMeshModel& linked, const srMeshModel::TriMesh& submitted,
                            std::span<const glm::vec3> normals) {
        const srMeshSubmission::PickScope picking(renderer, (render_flags & RENDER_NO_PICK) != 0);
        linked.RenderTriMeshWithEquations(renderer, submitted, normals);
    };
    drawLinked([&](stMeshModel* model, bool first_pass) {
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
        selectPolygons(*model, mesh, true);

        if (((highlight_colour.x == 0.0f) && (highlight_colour.y == 0.0f) &&
             (highlight_colour.z == 0.0f) && (highlight_colour.w == 0.0f)) ||
            (highlight_pass_mode != 1)) {
            if (diffuse_scale_enabled) {
                mesh.diffuse_scale = diffuse_scale;
                mesh.shaders[0].value = (mesh.shaders[0].value & 0xffffd7bf) | 0x44a0;
                mesh.control_flags |= (1UL << srMeshModel::CONTROL_SORTED_RENDERING);
            }
            if (emissive_override_enabled) {
                mesh.emissive_override = emissive_override;
            }
        } else {
            mesh.vertex_materials[0][0] = 0;
            mesh.materials[0][0] = highlight_material;
        }

        submit(*model, mesh, poly_normals);
    });

    if (highlight_colour.x != 0.0f || highlight_colour.y != 0.0f || highlight_colour.z != 0.0f ||
        highlight_colour.w != 0.0f) {
        const srMeshSubmission::WindingScope winding(renderer);
        if (highlight_pass_mode == 0) {
            renderer.setWinding(srGERD::WINDING_POSITIONAL_1);
        }
        drawLinked([&](stMeshModel* model, bool first_pass) {
            mesh = model->getTriMesh();
            if (((model->flags & W8_MESH_SORTED_RENDERING) == 0) || first_pass) {
                if (mesh.poly_textures[0][0] != 0 ||
                    ((mesh.textures[0][0] != 0) &&
                     (SDL_strncasecmp("blank", mesh.textures[0][0]->getName().c_str(), 5) != 0))) {
                    g_vertex_scratch.resize(mesh.vertex_count);

                    std::span<const glm::vec3> poly_normals;
                    if ((model->flags & W8_MESH_HAS_FRAME_STORAGE) != 0) {
                        const auto frame = model->frameGeometry(frame_index, frame_interpolation);
                        mesh.dig[0] = frame.positions.data();
                        mesh.dig[1] = frame.normals.data();
                        poly_normals = frame.polygon_normals;
                    }
                    selectPolygons(*model, mesh, false);

                    mesh.materials[0][0] = highlight_material;
                    mesh.shaders[1].value = (mesh.shaders[0].value & 0xffff5cb7) | 0x40a0;
                    mesh.poly_shaders[1] = 0;
                    mesh.poly_textures[0][1] = 0;
                    mesh.vertex_materials[0][1] = 0;
                    mesh.poly_uv[1] = 0;

                    double factor = 1.0 / getScale().y * 15.625;
                    float expand = static_cast<float>(
                        (bounds.sphere.radius * getScale().y * 0.001 + 1.0) * factor);
                    if (expand == 0) {
                        std::copy_n(mesh.positions, mesh.vertex_count, g_vertex_scratch.begin());
                    } else {
                        for (std::size_t i = 0; i < g_vertex_scratch.size(); ++i)
                            g_vertex_scratch[i] = expand * mesh.normals[i] + mesh.positions[i];
                    }
                    mesh.positions = g_vertex_scratch.data();
                    mesh.control_flags |= (1UL << srMeshModel::CONTROL_SORTED_RENDERING);

                    submit(*model, mesh, poly_normals);
                }
            }
        });
    }
}

/* Shared shadow quad: two upright triangles on a 500-unit ground span, lit by
   a dedicated material so the extruded shadow pass submits through the
   ordinary TriMesh pipeline. */
static const glm::ivec3 g_shadow_triangles[2] = {{0, 1, 2}, {3, 4, 5}};
static const glm::vec3 g_shadow_positions[6] = {{-250.0f, 250.0f, 0.0f}, {0.0f, -250.0f, 0.0f},
                                                {250.0f, 250.0f, 0.0f},  {0.0f, 250.0f, -250.0f},
                                                {0.0f, -250.0f, 0.0f},   {0.0f, 250.0f, 250.0f}};

static srMaterial& ShadowMaterial()
{
    static const std::unique_ptr<srMaterial> material = [] {
        auto created = std::make_unique<srMaterial>();
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
    const srMeshSubmission::EnableScope enabled(renderer, true);
    renderer.setCullMode(srGERD::CULL_NONE);
    const srModelViewScope transform(renderer);
    renderer.translate(0.0, height * 0.5f, 0.0);
    height *= kShadowExtrusionPitchScale;
    if (height < 0.5f) {
        height = 0.5f;
    }
    double scale = height;
    renderer.scale(scale, scale, scale);

    srTriMeshPipeline* pipeline = &renderer.meshPipeline();
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
    pipeline->Flush();
}
