#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include <glm/glm.hpp>
#include <vsg/all.h>

#include "wiz8/render/draw_inputs.h"

class Material;
class Mesh;
class MeshInstance;
class Texture;

namespace render {

// Wizardry's state is installed only on its own geometry. Other VSG subgraphs
// may use their own layouts, pipelines and view state in the same scene.
class RenderContext {
public:
    explicit RenderContext(VkExtent2D extent);
    std::uint64_t frame_number = 0;

    vsg::ref_ptr<vsg::PipelineLayout> layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> view_layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> instance_layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> pass_layout;
    vsg::ref_ptr<vsg::DescriptorSetLayout> batch_layout;

    vsg::ref_ptr<vsg::BindGraphicsPipeline> pipeline(std::uint32_t shader,
                                                     FaceCull cull = FaceCull::Back,
                                                     Winding winding = Winding::Standard,
                                                     std::int32_t polygon_offset = 0);
    vsg::ref_ptr<vsg::ImageInfo> texture(Texture* source, int reduction = 0);
    vsg::ref_ptr<vsg::BindDescriptorSet> descriptor(std::uint32_t set,
                                                    vsg::ref_ptr<vsg::Data> data);
    vsg::ref_ptr<vsg::BindDescriptorSet> batchSet(std::uint32_t shader,
                                                  vsg::ref_ptr<vsg::ImageInfo> image,
                                                  vsg::ref_ptr<vsg::ImageInfo> detail_image = {});

private:
    vsg::ShaderStages stages;
    vsg::ref_ptr<vsg::ViewportState> viewport;
    std::map<std::tuple<std::uint32_t, FaceCull, Winding, std::int32_t>,
             vsg::ref_ptr<vsg::BindGraphicsPipeline>>
        pipelines;
    vsg::ref_ptr<vsg::ImageInfo> white;
};

struct InstanceBlock {
    glm::vec4 ambient{0};
    glm::vec4 bounds{0};
    // Diffuse override enabled/value; emissive override enabled/value.
    glm::vec4 overrides{0};
    // Excluded light/fog groups; reverse normals; unused; unused.
    glm::uvec4 masks{0};
};

// These SSBOs grow with the actual scene/material count. There are no prototype
// limits on lights, fog layers or materials. Build the array before compilation;
// update the existing array after traversal when collecting graph lights.
std::vector<glm::vec4> packView(const ScenePass& pass,
                                std::span<const glm::vec4> eye_clip_planes = {});
std::vector<glm::vec4> packPass(std::span<const std::shared_ptr<Material>> materials,
                                std::uint32_t draw_flags, bool has_normals, double time);
InstanceBlock packInstance(const MeshInstance& instance, const Mesh& mesh, const ScenePass& pass,
                           bool reverse_normals = false);

vsg::ref_ptr<vsg::vec4Array> makeArray(std::span<const glm::vec4> values,
                                       vsg::DataVariance variance = vsg::DYNAMIC_DATA);
void store(vsg::vec4Array& destination, std::span<const glm::vec4> values);
vsg::ref_ptr<vsg::vec4Array> makeInstanceArray(const InstanceBlock& block);
void store(vsg::vec4Array& destination, const InstanceBlock& block);

std::uint32_t fadingShader(std::uint32_t shader);

// Derive a light for the current graph-composed model-view matrix. The copy
// belongs to a view, so a second camera cannot overwrite another view's data.
void prepareLightAt(Light& light, const glm::mat4& model_view, float brightness);

} // namespace render
