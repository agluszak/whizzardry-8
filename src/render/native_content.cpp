#include "wiz8/render/native_content.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>

#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <vsg/core/Array.h>
#include <vsg/core/ConstVisitor.h>
#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/io/read.h>
#include <vsg/maths/mat4.h>
#include <vsg/state/BufferInfo.h>
#include <vsg/state/ViewDependentState.h>
#include <vulkan/vulkan_core.h>

#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/material.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"

namespace render {

NativeContent::NativeContent(vsg::ref_ptr<vsg::Node> source, std::shared_ptr<Material> material,
                             std::shared_ptr<Texture> texture)
    : material(std::move(material)), texture(std::move(texture))
{
    if (!source || !this->material)
        throw std::invalid_argument("Native content requires geometry and a material");
    struct Import : vsg::ConstVisitor {
        NativeContent& target;
        vsg::dmat4 matrix;
        explicit Import(NativeContent& target) : target(target) {}
        void apply(const vsg::Object& object) override { object.traverse(*this); }
        void apply(const vsg::Transform& transform) override
        {
            const auto previous = matrix;
            matrix = transform.transform(matrix);
            transform.traverse(*this);
            matrix = previous;
        }
        void apply(const vsg::VertexIndexDraw& source) override
        {
            if (source.firstBinding != 0 || source.arrays.size() < 2 || !source.indices ||
                source.instanceCount != 1 || source.vertexOffset != 0)
                throw std::runtime_error(
                    "Native game materials require one indexed position/normal stream");
            auto positions = source.arrays[0]->data.cast<vsg::vec3Array>();
            auto normals = source.arrays[1]->data.cast<vsg::vec3Array>();
            if (!positions || !normals || positions->empty() ||
                normals->size() != positions->size())
                throw std::runtime_error(
                    "Native game materials require per-vertex vec3 positions and normals");
            auto uv = source.arrays.size() > 2 ? source.arrays[2]->data.cast<vsg::vec2Array>()
                                               : vsg::ref_ptr<vsg::vec2Array>{};
            if (uv && uv->size() != positions->size())
                throw std::runtime_error("Native texture coordinates must match positions");
            const auto count = positions->size();
            auto texcoords = vsg::vec4Array::create(count, vsg::vec4(0, 0, 0, 0));
            for (std::size_t i = 0; uv && i < count; ++i)
                texcoords->at(i) = vsg::vec4(uv->at(i).x, uv->at(i).y, 0, 0);
            auto zero = vsg::vec4Array::create(count, vsg::vec4(0, 0, 0, 0));
            auto one = vsg::vec4Array::create(count, vsg::vec4(1, 1, 1, 1));
            auto metadata = vsg::vec4Array::create(count, vsg::vec4(0, 0, 1, 0));
            auto draw = vsg::VertexIndexDraw::create();
            draw->assignArrays({positions, normals, texcoords, zero, one, one, metadata});
            draw->assignIndices(source.indices->data);
            draw->indexCount = source.indexCount;
            draw->firstIndex = source.firstIndex;
            draw->instanceCount = 1;
            auto node = DrawNode::create(draw, positions);
            node->cull = FaceCull::None;
            auto transform = vsg::MatrixTransform::create(matrix);
            transform->addChild(node);
            target.addChild(transform);
            glm::vec3 minimum(positions->at(0).x, positions->at(0).y, positions->at(0).z);
            auto maximum = minimum;
            for (const auto& p : *positions) {
                minimum = glm::min(minimum, glm::vec3(p.x, p.y, p.z));
                maximum = glm::max(maximum, glm::vec3(p.x, p.y, p.z));
            }
            const auto center = (minimum + maximum) * .5f;
            target.parts.push_back(
                {.node = node, .bounds = glm::vec4(center, glm::length(maximum - center))});
        }
    } import(*this);
    source->accept(import);
    if (parts.empty()) throw std::runtime_error("Native content has no supported indexed geometry");
}

void NativeContent::advanceTextures(std::chrono::milliseconds now)
{
    if (texture) texture->UpdateFrame(now);
}

void NativeContent::prepare(RenderContext& context, const ScenePass& pass)
{
    DrawShader shader;
    shader.value |= DrawShader::FOG_ENABLE << DrawShader::FOG_SHIFT;
    if (texture) shader.value |= DrawShader::MASK_TEXTURING;
    const auto image = context.texture(texture.get(), pass.frame.texture_reduction);
    const auto values =
        packPass(std::array{material}, texture ? HasTexcoord0 : 0u, true, pass.frame.time);
    for (auto& part : parts) {
        InstanceBlock instance{
            .ambient = glm::vec4((glm::vec3(pass.ambient_light) + pass.frame.environment_offset) *
                                     pass.frame.brightness,
                                 1),
            .bounds = part.bounds,
            .masks = {pass.exclusion_mask, 0, 0, 0}};
        if (!part.instance) {
            part.instance = makeInstanceArray(instance);
            part.material = makeArray(values);
        } else {
            store(*part.instance, instance);
            store(*part.material, values);
        }
        if (part.layout == context.layout && part.image == image) continue;
        part.node->stateCommands.clear();
        part.node->add(vsg::BindViewDescriptorSets::create(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                           context.layout, 0));
        part.node->add(context.descriptor(1, part.instance));
        part.node->add(context.descriptor(2, part.material));
        part.node->add(context.batchSet(shader.value, image));
        part.node->setPipeline(context.pipeline(shader.value, FaceCull::None),
                               context.pipeline(shader.value, FaceCull::None, Winding::Reversed));
        part.layout = context.layout;
        part.image = image;
    }
}

vsg::ref_ptr<NativeContent> loadGameContent(const std::filesystem::path& path,
                                            std::shared_ptr<Material> material,
                                            std::shared_ptr<Texture> texture)
{
    auto source = vsg::read_cast<vsg::Node>(path.string());
    if (!source) throw std::runtime_error("Cannot read native VSG content: " + path.string());
    return NativeContent::create(source, std::move(material), std::move(texture));
}

} // namespace render
