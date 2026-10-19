#include "wiz8/render/mesh_instance.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "wiz8/math.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/matrix_scope.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_resources.h"
#include "wiz8/render/mesh_submission.h"
#include "wiz8/render/texture.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/unicode.h"
#include "wiz8/world/level_loader.h"

MeshInstance::~MeshInstance()
{
    setScene(nullptr);
}

MatrixScope MeshInstance::scopedRenderTransform(GpuRenderer& renderer) const
{
    if ((alignment_flags & 1) == 0) {
        MatrixScope scope(renderer);
        renderer.multMatrix(transform.matrix<glm::mat4>());
        return scope;
    } else {
        MatrixScope transform(renderer);
        const auto matrix = renderer.getMatrix(GpuRenderer::MATRIX_MODELVIEW);
        const glm::vec4 position = matrix * glm::vec4(glm::vec3(this->transform.getLocation()), 1);
        const auto world_scale = glm::vec3(this->transform.magnitudes());
        glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
                        glm::length(glm::vec3(matrix[2])));
        if (glm::determinant(glm::mat3(matrix)) > 0) scale = -scale;
        renderer.loadIdentity();
        renderer.translate(glm::vec3(position));
        if (align_angle != 0.0f) {
            renderer.rotate(align_angle, align_axis);
        }
        scale *= world_scale;
        renderer.scale(scale.x, scale.y, -scale.z);
        return transform;
    }
}

MeshInstance::RenderScope::RenderScope(const MeshInstance& instance, GpuRenderer& renderer,
                                       bool apply_exclusion)
    : renderer(renderer), transform(instance.scopedRenderTransform(renderer))
{
    restore_mask = apply_exclusion && instance.exclusion_mask != 0;
    if (restore_mask) {
        previous_mask = renderer.getExclusionMask();
        renderer.setExclusionMask(previous_mask | instance.exclusion_mask);
    }
}

MeshInstance::RenderScope::~RenderScope()
{
    if (restore_mask) renderer.setExclusionMask(previous_mask);
}

MeshInstance::RenderScope MeshInstance::scopedRender(GpuRenderer& renderer,
                                                     bool apply_exclusion) const
{
    return RenderScope(*this, renderer, apply_exclusion);
}

glm::vec3 MeshInstance::getAlignAxis() const
{
    return align_axis;
}

int MeshInstance::isAligned() const
{
    return (int)(alignment_flags & 1);
}

w8_ulong MeshInstance::getExclusionMask() const
{
    return exclusion_mask;
}

void MeshInstance::setAlignment(int enabled)
{
    if (enabled != 0) {
        alignment_flags |= 1;
        return;
    }
    alignment_flags &= ~1u;
}

void MeshInstance::setAlignAngle(double angle)
{
    align_angle = (float)angle;
    alignment_flags |= 1;
}

void MeshInstance::setAlignAxis(const glm::vec3& axis)
{
    align_axis = normalizedOrZero(axis);
    alignment_flags |= 1;
}

void MeshInstance::setExclusionMask(w8_ulong mask)
{
    exclusion_mask = mask;
}

constexpr float kShadowExtrusionPitchScale = 1.0f / 1500.0f;

/* Scratch vertex store shared by every highlight shell submission; grown
   on demand and kept between frames. */

/* Find the animated texture assigned to polygons whose runtime name begins
   with "mouth". Damage-stage instances use the stage-specific texture table;
   ordinary instances use the mesh's active polygon texture table. */
Texture* MeshInstance::FindMouthTexture()
{
    Mesh* mesh = static_cast<Mesh*>(getModel());
    for (auto* mesh_part : meshParts(mesh)) {
        MeshResources<Texture>* textures =
            damage_stage == -1 ? mesh_part->getPolygonTextureTable(0, 0, false)
                               : mesh_part->GetTextureTable(damage_stage_tables[damage_stage]);
        if (textures != nullptr) {
            for (int polygon = 0; polygon < mesh_part->polygon_count; ++polygon) {
                auto* animation =
                    (((*textures)[polygon].get() && (*textures)[polygon].get()->playback)
                         ? (*textures)[polygon].get()
                         : nullptr);
                if (animation != nullptr && animation->getName().starts_with("mouth")) {
                    return animation;
                }
            }
        }
    }
    return nullptr;
}

int MeshInstance::FindDamageStage(std::string_view name)
{
    Mesh* mesh = static_cast<Mesh*>(getModel());
    return mesh->FindSkinTable(name);
}

/* The scene entry is queued before the sibling chain is walked for an opaque
   model, and after it for a model flagged W8_MESH_SORTED_RENDERING, so the
   sorted models are processed after every opaque sibling, in reverse sibling
   order. Children follow unless the node terminates. */

/* Add a stage by cloning the first stage's table across the complete linked
   mesh chain. The instance stores the table id shared by that chain. */
int MeshInstance::AddDamageStage(std::string_view name)
{
    Mesh* mesh = static_cast<Mesh*>(getModel());

    if (mesh->FindSkinTable(name) != -1) {
        return -1;
    }

    int stage = static_cast<int>(damage_stage_tables.size());
    int base_table = stage > 0 ? damage_stage_tables[0] : -1;
    damage_stage_tables.push_back(mesh->CreateSkinTable(name, base_table));
    for (const auto& part : mesh->parts) {
        part->CreateSkinTable(name, base_table);
    }
    return stage;
}

int MeshInstance::AddExistingDamageStage(std::string_view name)
{
    Mesh* mesh = static_cast<Mesh*>(getModel());
    int table = mesh->FindSkinTable(name);

    if (table == -1) {
        return -1;
    }

    int stage = static_cast<int>(damage_stage_tables.size());
    damage_stage_tables.push_back(table);
    return stage;
}

unsigned char MeshInstance::ReplaceDamageStageTexture(int stage, std::string_view old_name,
                                                      const std::shared_ptr<Texture>& replacement)
{
    Mesh* mesh = static_cast<Mesh*>(getModel());
    bool replaced = false;

    if (replacement && replacement->playback) {
        replacement->Prepare();
    }

    for (auto* mesh_part : meshParts(mesh)) {
        MeshResources<Texture>* textures = mesh_part->GetTextureTable(damage_stage_tables[stage]);
        if (textures == 0) {
            continue;
        }

        for (int polygon = 0; polygon < mesh_part->polygon_count; ++polygon) {
            Texture* texture = (*textures)[polygon].get();
            if (!texture || (texture->source_path.empty() && !texture->playback)) {
                continue;
            }

            if (CompareTextIgnoreAsciiCase(texture->getName(), old_name) == 0) {
                replaced = true;
                while (polygon < mesh_part->polygon_count &&
                       (*textures)[polygon].get() == texture) {
                    textures->set(polygon, replacement);
                    ++polygon;
                }
            } else {
                while (polygon < mesh_part->polygon_count &&
                       (*textures)[polygon].get() == texture) {
                    ++polygon;
                }
            }
            --polygon;
        }
    }
    return replaced;
}

MeshInstance::MeshInstance(WorldScene* scene)
{
    alignment_flags = 0;
    align_angle = 0.0f;
    align_axis = glm::vec3(0, 0, 1);
    exclusion_mask = 0;

    highlight_colour = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    render_flags = 0;
    mesh_index = -1;
    frame_index = 0;
    highlight_pass_mode = 0;
    setScene(scene);
    damage_stage = -1;
    light_scale = glm::vec3(1);
    diffuse_scale_enabled = false;
    diffuse_scale = 0.0f;
    emissive_override_enabled = false;
    emissive_override = 0.0f;
    frame_interpolation = 0.0f;
}

MeshInstance& MeshInstance::operator=(const MeshInstance& other)
{
    if (this == &other) return *this;
    transform = other.transform;
    name = other.name;
    visible = other.visible;
    draw_parts = other.draw_parts;
    alignment_flags = other.alignment_flags;
    model_ = other.model_;
    align_angle = other.align_angle;
    align_axis = other.align_axis;
    exclusion_mask = other.exclusion_mask;
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
void MeshInstance::draw(GpuRenderer& renderer)
{
    const auto scope = scopedRender(renderer);
    RenderMeshes(renderer);
}

/* Submit the instance's linked mesh chain. Pass one fills a detached TriMesh
   per model — animated vertex buffers when the model requests them, the
   damage-stage texture/active-polygon tables, the lazily built highlight
   material or the instance material overrides — and pushes each mesh through
   RenderTriMeshWithEquations. When the highlight RGBA is configured
   the chain is submitted again inflated along its normals into a shared
   scratch buffer with reversed winding, skipping the linked "blank" skin. */
void MeshInstance::RenderMeshes(GpuRenderer& renderer)
{
    Mesh::TriMesh mesh;
    mesh.control_flags = 0;

    Mesh* model = static_cast<Mesh*>(getModel());
    Mesh* first_model = model;

    const auto bounds = model->getBounds();
    if (!model->isVisible(renderer)) return;

    // c-style-cast-ok: the pick key is an opaque void* token
    renderer.setPickKey(
        (render_flags & RENDER_NO_PICK) != 0 ? 0 : reinterpret_cast<w8_ulong_ptr>(this));

    const meshSubmission::AmbientScope lighting(renderer);
    const auto ambient = lighting.ambient();
    const glm::vec3 ambient_color(ambient);

    glm::vec4 light;
    if (!model->vertex_lighting_ready) {
        light = glm::vec4((ambient_color * light_scale + renderer.environment_offset) *
                              renderer.scene_brightness,
                          1);
    } else {
        light = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    renderer.setAmbientLight(light);

    if (highlight_colour.x != 0.0f || highlight_colour.y != 0.0f || highlight_colour.z != 0.0f ||
        highlight_colour.w != 0.0f) {
        if (highlight_material == 0) {
            highlight_material = std::make_shared<Material>();
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
        for (auto* linked : meshParts(first_model)) {
            draw(linked, linked == first_model);
            if (!draw_parts) break;
        }
    };
    const auto selectPolygons = [&](Mesh& linked, Mesh::TriMesh& selected,
                                    bool replace_damage_texture) {
        if (selected.active_polygons) return;
        const int table = damage_stage >= 0 ? damage_stage_tables.at(damage_stage) : -1;
        if (replace_damage_texture && damage_stage >= 0)
            selected.poly_textures[0][0] = linked.GetTextureTable(table);
        if (const auto active = linked.GetActivePolygons(table, true))
            selected.active_polygons = active;
    };
    const auto submit = [&](Mesh& linked, const Mesh::TriMesh& submitted,
                            std::span<const glm::vec3> normals) {
        const meshSubmission::PickScope picking(renderer, (render_flags & RENDER_NO_PICK) != 0);
        linked.RenderTriMeshWithEquations(renderer, submitted, normals);
    };
    drawLinked([&](Mesh* model, bool first_pass) {
        model->SetAmbientColor(ambient_color, renderer.environment_offset,
                               renderer.scene_brightness);
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

        selectPolygons(*model, mesh, true);

        if (((highlight_colour.x == 0.0f) && (highlight_colour.y == 0.0f) &&
             (highlight_colour.z == 0.0f) && (highlight_colour.w == 0.0f)) ||
            (highlight_pass_mode != 1)) {
            if (diffuse_scale_enabled) {
                mesh.diffuse_scale = diffuse_scale;
                mesh.shaders[0].value = (mesh.shaders[0].value & 0xffffd7bf) | 0x44a0;
                mesh.control_flags |= (1UL << Mesh::CONTROL_SORTED_RENDERING);
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
        const meshSubmission::WindingScope winding(renderer);
        if (highlight_pass_mode == 0) {
            renderer.setWinding(GpuRenderer::WINDING_POSITIONAL_1);
        }
        drawLinked([&](Mesh* model, bool first_pass) {
            mesh = model->getTriMesh();
            if (((model->flags & W8_MESH_SORTED_RENDERING) == 0) || first_pass) {
                if (mesh.poly_textures[0][0] != 0 ||
                    ((mesh.textures[0][0] != 0) &&
                     (!mesh.textures[0][0]->getName().starts_with("blank")))) {
                    renderer.highlight_vertices.resize(mesh.vertex_count);

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

                    double factor = 1.0 / this->transform.getScale().y * 15.625;
                    float expand = static_cast<float>(
                        (bounds.sphere.radius * this->transform.getScale().y * 0.001 + 1.0) *
                        factor);
                    if (expand == 0) {
                        std::copy_n(mesh.positions, mesh.vertex_count,
                                    renderer.highlight_vertices.begin());
                    } else {
                        for (std::size_t i = 0; i < renderer.highlight_vertices.size(); ++i)
                            renderer.highlight_vertices[i] =
                                expand * mesh.normals[i] + mesh.positions[i];
                    }
                    mesh.positions = renderer.highlight_vertices.data();
                    mesh.control_flags |= (1UL << Mesh::CONTROL_SORTED_RENDERING);

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

static const std::shared_ptr<Material>& ShadowMaterial()
{
    static const std::shared_ptr<Material> material = [] {
        auto created = std::make_shared<Material>();
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
    return material;
}

/* Submit the shared shadow-quad mesh extruded over the source mesh's height:
   front-cull the pass, translate to the mesh's mid-height, scale the unit
   quad by a clamped pitch factor, then feed the pipeline slots directly. */
void MeshInstance::RenderShadow(GpuRenderer& renderer, Mesh::TriMesh& mesh)
{
    float height = mesh.bounds_maximum.y - mesh.bounds_minimum.y;
    const meshSubmission::EnableScope enabled(renderer, true);
    renderer.setCullMode(GpuRenderer::CULL_NONE);
    const MatrixScope transform(renderer);
    renderer.translate(0.0, height * 0.5f, 0.0);
    height *= kShadowExtrusionPitchScale;
    if (height < 0.5f) {
        height = 0.5f;
    }
    double scale = height;
    renderer.scale(scale, scale, scale);

    Mesh::TriMesh shadow;
    shadow.polygon_count = std::size(g_shadow_triangles);
    shadow.poly_vertices = g_shadow_triangles;
    shadow.vertex_count = std::size(g_shadow_positions);
    shadow.positions = g_shadow_positions;
    shadow.pass_count = 1;
    shadow.control_flags =
        (1u << Mesh::CONTROL_SKIP_AUTO_BOX) | (1u << Mesh::CONTROL_SKIP_AUTO_SPHERE);
    shadow.materials[0][0] = ShadowMaterial();
    shadow.shaders[0].value = 0x44b3;
    renderer.submitMesh(shadow);
}
