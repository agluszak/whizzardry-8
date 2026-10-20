#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <vsg/all.h>

#include "wiz8/render/material.h"
#include "wiz8/render/render_mesh.h"
#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"

namespace render {

// Ordinary vertex arrays used by the game's billboard and floor-decal effects.
// Their state stays local to the effect, alongside standard VSG subgraphs.
struct EffectGeometry {
    vsg::ref_ptr<vsg::vec3Array> positions;
    vsg::ref_ptr<vsg::vec3Array> normals;
    std::array<vsg::ref_ptr<vsg::vec4Array>, 5> attributes;
    vsg::ref_ptr<vsg::vec4Array> instance;
    vsg::ref_ptr<vsg::vec4Array> pass;
    vsg::ref_ptr<vsg::BindDescriptorSet> instance_bind;
    vsg::ref_ptr<vsg::BindDescriptorSet> pass_bind;

    EffectGeometry(RenderContext& context, std::uint32_t count,
                   const std::shared_ptr<Material>& material, std::uint32_t flags,
                   bool has_normals, double time)
        : positions(vsg::vec3Array::create(count)), normals(vsg::vec3Array::create(count)),
          instance(makeInstanceArray({})),
          pass(makeArray(packPass(std::array{material}, flags, has_normals, time)))
    {
        positions->properties.dataVariance = vsg::DYNAMIC_DATA;
        for (auto& attribute : attributes) {
            attribute = vsg::vec4Array::create(count);
            attribute->properties.dataVariance = vsg::DYNAMIC_DATA;
        }
        for (std::size_t index = 0; index < count; ++index) {
            normals->at(index) = vsg::vec3(0, 0, 1);
            attributes[0]->at(index) = vsg::vec4(0);
            attributes[1]->at(index) = vsg::vec4(0);
            attributes[2]->at(index) = vsg::vec4(1);
            attributes[3]->at(index) = vsg::vec4(1);
            attributes[4]->at(index) = vsg::vec4(0, 1, 1, 0);
        }
        instance_bind = context.descriptor(1, instance);
        pass_bind = context.descriptor(2, pass);
    }

    vsg::ref_ptr<DrawNode> draw(vsg::ref_ptr<vsg::uintArray> indices, bool sorted)
    {
        auto geometry = vsg::VertexIndexDraw::create();
        geometry->assignArrays(vsg::DataList{positions, normals, attributes[0], attributes[1],
                                              attributes[2], attributes[3], attributes[4]});
        geometry->assignIndices(indices);
        geometry->indexCount = static_cast<std::uint32_t>(indices->size());
        return DrawNode::create(geometry, positions, sorted);
    }

    void dirty()
    {
        positions->dirty();
        normals->dirty();
        for (auto& attribute : attributes) attribute->dirty();
    }
};

} // namespace render
