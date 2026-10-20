#include "wiz8/render/mesh_instance.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../effects/effect_geometry.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "wiz8/math.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_resources.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/unicode.h"
#include "wiz8/world/level_loader.h"

MeshInstance::~MeshInstance()
{
    setScene(nullptr);
    node_->instance = nullptr;
}

DrawTransform MeshInstance::renderTransform(const DrawTransform& view) const
{
    if ((alignment_flags & 1) == 0) return view.withModel(transform.matrix<glm::mat4>());
    const auto& matrix = view.model_view;
    const glm::vec4 position = matrix * glm::vec4(glm::vec3(this->transform.getLocation()), 1);
    const auto world_scale = glm::vec3(this->transform.magnitudes());
    glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
                    glm::length(glm::vec3(matrix[2])));
    if (glm::determinant(glm::mat3(matrix)) > 0) scale = -scale;
    auto model_view = glm::translate(glm::mat4(1), glm::vec3(position));
    if (align_angle != 0.0f) {
        // A zero alignment axis discards the translation, as the retail rotation did.
        model_view = glm::dot(align_axis, align_axis) == 0
                         ? glm::mat4(1)
                         : glm::rotate(model_view, align_angle, align_axis);
    }
    scale *= world_scale;
    return view.withModelView(glm::scale(model_view, glm::vec3(scale.x, scale.y, -scale.z)));
}

glm::vec3 MeshInstance::getAlignAxis() const
{
    return align_axis;
}

int MeshInstance::isAligned() const
{
    return (int)(alignment_flags & 1);
}

std::uint32_t MeshInstance::getExclusionMask() const
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

void MeshInstance::setExclusionMask(std::uint32_t mask)
{
    exclusion_mask = mask;
}

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
    node_ = render::InstanceNode::create(*this);
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

MeshInstance::MeshInstance(const MeshInstance& other) : MeshInstance()
{
    *this = other;
}

MeshInstance& MeshInstance::operator=(const MeshInstance& other)
{
    if (this == &other) return *this;
    transform = other.transform;
    name = other.name;
    visible = other.visible;
    draw_parts = other.draw_parts;
    alignment_flags = other.alignment_flags;
    setModel(other.model_);
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

vsg::ref_ptr<render::InstanceNode> MeshInstance::renderNode() const
{
    return node_;
}

void MeshInstance::setModel(std::shared_ptr<Mesh> model)
{
    model_ = std::move(model);
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
    auto old_scene = scene;
    setScene(nullptr);
    if (parent.scene) {
        parent.scene->dynamic_meshes.push_back(this);
        scene = parent.scene;
    } else if (old_scene) {
        // Parentage is authoritative; an unattached parent leaves its subtree
        // unattached until the parent joins a scene.
        scene = nullptr;
    }
    if (mapped_key) {
        attachment_ = render::VertexAttachment::create(parent.node_, *mapped_key);
        attachment_->addChild(node_);
        parent.node_->addChild(attachment_);
        render_parent_ = parent.node_;
    } else {
        parent.node_->addChild(node_);
        render_parent_ = parent.node_;
    }
}

std::optional<glm::vec3> MeshInstance::sampledPosition(const Mesh& mesh, std::size_t index) const
{
    return node_->sampledPosition(mesh, index);
}

namespace render {

struct InstanceNode::Part {
    Mesh* mesh;
    MeshPose pose;
    Part(Mesh& mesh) : mesh(&mesh), pose(mesh) {}
};

InstanceNode::InstanceNode(MeshInstance& instance)
    : instance(&instance), geometry(vsg::Switch::create())
{
    addChild(geometry);
}
InstanceNode::~InstanceNode() = default;
vsg::dmat4 InstanceNode::transform(const vsg::dmat4& model_view) const
{
    if (!instance) return model_view;
    DrawTransform view;
    view.model_view = toGlm(model_view);
    return toVsg(glm::dmat4(instance->renderTransform(view).model_view));
}
void InstanceNode::accept(vsg::Visitor& visitor)
{
    if (instance) visitor.apply(static_cast<vsg::Transform&>(*this));
}
void InstanceNode::accept(vsg::ConstVisitor& visitor) const
{
    if (instance && instance->visible) visitor.apply(static_cast<const vsg::Transform&>(*this));
}
void InstanceNode::accept(vsg::RecordTraversal& visitor) const
{
    if (instance && instance->visible) visitor.apply(static_cast<const vsg::Transform&>(*this));
}
std::optional<glm::vec3> InstanceNode::sampledPosition(const Mesh& mesh, std::size_t index) const
{
    for (const auto& part : parts)
        if (part->mesh == &mesh && index < part->pose.positions.size())
            return part->pose.positions[index];
    return {};
}

namespace {

vsg::ref_ptr<vsg::Node> extrusionShadow(RenderContext& context, const MeshInstance& instance,
                                        Mesh& mesh, const ScenePass& pass)
{
    const auto bounds = mesh.getTriMesh();
    const float height = bounds.bounds_maximum.y - bounds.bounds_minimum.y;
    const auto material = std::make_shared<Material>();
    material->setAmbient(glm::vec4(1));
    material->setSpecular(glm::vec4(0));
    material->setEmissive(glm::vec4(0));
    material->setDiffuse(glm::vec4(1, 1, 1, 0));
    material->parms.shininess = 1;
    EffectGeometry geometry(context, 6, material, 0, false, pass.frame.time);
    static constexpr std::array<glm::vec3, 6> positions{
        {{-250, 250, 0}, {0, -250, 0}, {250, 250, 0}, {0, 250, -250}, {0, -250, 0}, {0, 250, 250}}};
    auto indices = vsg::uintArray::create(6);
    for (std::uint32_t vertex = 0; vertex < positions.size(); ++vertex) {
        const auto& point = positions[vertex];
        geometry.positions->at(vertex) = {point.x, point.y, point.z};
        indices->at(vertex) = vertex;
    }
    auto node = geometry.draw(indices, true);
    node->pickable = true;
    constexpr std::uint32_t shader = 0x44b3;
    node->add(context.pipeline(shader, FaceCull::None));
    store(*geometry.instance, packInstance(instance, mesh, pass));
    node->add(geometry.instance_bind);
    node->add(geometry.pass_bind);
    node->add(context.batchSet(shader, context.texture(nullptr)));
    const auto matrix = vsg::translate(0.0, static_cast<double>(height * .5f), 0.0) *
                        vsg::scale(static_cast<double>(std::max(height / 1500.f, .5f)));
    auto transform = vsg::MatrixTransform::create(matrix);
    transform->addChild(node);
    return transform;
}

} // namespace

void InstanceNode::prepare(RenderContext& context, const ScenePass& pass,
                           std::uint64_t frame_number, bool static_geometry)
{
    if (prepared_frame == frame_number) return;
    prepared_frame = frame_number;
    geometry->children.clear();
    if (!instance || !instance->getModel()) {
        parts.clear();
        return;
    }
    std::vector<Mesh*> models;
    for (auto* mesh : meshParts(instance->getModel(), instance->draw_parts)) models.push_back(mesh);
    if (parts.size() != models.size() ||
        !std::equal(models.begin(), models.end(), parts.begin(),
                    [](const auto* mesh, const auto& part) { return mesh == part->mesh; })) {
        parts.clear();
        for (auto* mesh : models) parts.push_back(std::make_unique<Part>(*mesh));
    }
    const bool highlighted = glm::any(glm::notEqual(instance->highlight_colour, glm::vec4(0)));
    if (highlighted) {
        if (!instance->highlight_material) {
            instance->highlight_material = std::make_shared<Material>();
            instance->highlight_material->setAmbient(glm::vec4(0));
            instance->highlight_material->setDiffuse(glm::vec4(0));
            instance->highlight_material->setSpecular(glm::vec4(0));
            instance->highlight_material->setOpacity(.35);
        }
        instance->highlight_material->setEmissive(instance->highlight_colour);
    }
    const int skin =
        instance->damage_stage >= 0 ? instance->damage_stage_tables.at(instance->damage_stage) : -1;
    if (instance->render_flags & MeshInstance::RENDER_SHADOW)
        geometry->addChild(render_mask | pick_mask,
                           extrusionShadow(context, *instance, *instance->getModel(), pass));
    for (auto& part : parts) {
        auto& mesh = *part->mesh;
        mesh.SetAmbientColor(glm::vec3(pass.ambient_light), pass.frame.environment_offset,
                             pass.frame.brightness);
        part->pose.update(mesh, instance->frame_index, instance->frame_interpolation,
                          pass.frame.time, skin, !static_geometry);
        auto& pose = part->pose;
        const auto& asset = *pose.asset;
        for (std::size_t pass_index = 0; pass_index < asset.passes.size(); ++pass_index) {
            const auto& source = asset.passes[pass_index];
            auto& sampled = pose.passes[pass_index];
            const auto instance_data = makeInstanceArray(packInstance(*instance, mesh, pass));
            auto materials = source.materials;
            if (highlighted && instance->highlight_pass_mode == 1)
                std::ranges::fill(materials, instance->highlight_material);
            const auto pass_data = makeArray(
                packPass(materials, source.draw_flags, !pose.normals.empty(), pass.frame.time));
            auto body = vsg::Group::create();
            for (std::size_t batch_index = 0; batch_index < source.batches.size(); ++batch_index) {
                const auto& batch = source.batches[batch_index];
                auto shader = effectiveShader(*instance, batch.shader.value);
                const auto cull =
                    source.cull ? (source.side ? FaceCull::Front : FaceCull::Back) : FaceCull::None;
                const auto sorted = asset.sorted || drawsSorted(*instance);
                auto leaf =
                    DrawNode::create(sampled.draws[batch_index], sampled.positions, sorted,
                                     asset.sort_bias, asset.offset_pass_keys ? source.pass : 0);
                leaf->cull = cull;
                leaf->pickable = !(instance->render_flags & MeshInstance::RENDER_NO_PICK) &&
                                 !(static_geometry && asset.sorted);
                leaf->add(context.pipeline(shader, cull));
                leaf->add(context.descriptor(1, instance_data));
                leaf->add(context.descriptor(2, pass_data));
                auto texture = pose.texture(mesh, pass_index, batch_index, skin);
                auto texture2 = pose.texture(mesh, pass_index, batch_index, skin, 1);
                leaf->add(context.batchSet(
                    shader, context.texture(texture.get(), pass.frame.texture_reduction),
                    context.texture(texture2.get(), pass.frame.texture_reduction)));
                body->addChild(leaf);

                if (highlighted && (source.side == 0) &&
                    (!texture || !texture->getName().starts_with("blank"))) {
                    auto expanded = vsg::vec3Array::create(sampled.positions->size());
                    const auto scale_y = instance->transform.getScale().y;
                    const float amount =
                        scale_y != 0
                            ? static_cast<float>((asset.bounds_radius * scale_y * .001 + 1) *
                                                 15.625 / scale_y)
                            : 0;
                    for (std::size_t vertex = 0; vertex < expanded->size(); ++vertex)
                        expanded->at(vertex) =
                            sampled.positions->at(vertex) + sampled.normals->at(vertex) * amount;
                    auto draw = vsg::VertexIndexDraw::create(*sampled.draws[batch_index]);
                    vsg::DataList arrays;
                    arrays.push_back(expanded);
                    for (std::size_t binding = 1; binding < draw->arrays.size(); ++binding)
                        arrays.push_back(draw->arrays[binding]->data);
                    draw->assignArrays(arrays);
                    auto shell = DrawNode::create(draw, expanded, true, asset.sort_bias,
                                                  asset.offset_pass_keys ? source.pass : 0);
                    shell->pickable = leaf->pickable;
                    const auto shell_shader = (shader & 0xffff5cb7u) | 0x40a0u;
                    const auto winding =
                        instance->highlight_pass_mode == 0 ? Winding::Reversed : Winding::Standard;
                    shell->cull = cull;
                    shell->winding = winding;
                    shell->add(context.pipeline(shell_shader, cull, winding));
                    shell->add(context.descriptor(1, instance_data));
                    std::vector<std::shared_ptr<Material>> shell_materials(
                        source.materials.size(), instance->highlight_material);
                    shell->add(context.descriptor(
                        2, makeArray(packPass(shell_materials,
                                              source.draw_flags &
                                                  ~static_cast<std::uint32_t>(HasVertexMaterials),
                                              !pose.normals.empty(), pass.frame.time))));
                    shell->add(context.batchSet(shell_shader, context.texture(nullptr)));
                    body->addChild(shell);
                }
            }
            const auto bound = vsg::dsphere(asset.bounds_center.x, asset.bounds_center.y,
                                            asset.bounds_center.z, asset.bounds_radius);
            geometry->addChild(render_mask | pick_mask, vsg::CullNode::create(bound, body));
        }
    }
}

VertexAttachment::VertexAttachment(vsg::ref_ptr<InstanceNode> parent, short mapped_key,
                                   const glm::vec3& fallback)
    : parent(parent), key(mapped_key), fallback(fallback)
{
}
vsg::dmat4 VertexAttachment::transform(const vsg::dmat4& model_view) const
{
    auto source = parent.ref_ptr();
    auto offset = fallback;
    if (source && source->instance && source->instance->getModel()) {
        const auto mapped = findMappedVertex(source->instance->getModel(), key);
        if (mapped)
            if (auto position = source->sampledPosition(*mapped->mesh, mapped->index))
                offset = *position;
    }
    return model_view * vsg::translate(static_cast<double>(offset.x), static_cast<double>(offset.y),
                                       static_cast<double>(offset.z));
}

} // namespace render

glm::dmat4 MeshInstance::worldMatrix() const
{
    glm::dmat4 parent_matrix(1);
    if (auto parent = render_parent_.ref_ptr())
        if (const auto* ancestor = dynamic_cast<const render::InstanceNode*>(parent.get());
            ancestor && ancestor->instance)
            parent_matrix = ancestor->instance->worldMatrix();
    if (attachment_) {
        const auto attached = attachment_->transform(render::toVsg(parent_matrix));
        parent_matrix = glm::dmat4(render::toGlm(attached));
    }
    return parent_matrix * transform.matrix();
}
