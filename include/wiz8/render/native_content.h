#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <vsg/nodes/MatrixTransform.h>

#include "wiz8/render/vsg_material.h"
#include "wiz8/render/vsg_scene.h"

class Material;
class Texture;

namespace render {

// A focused adapter for static, indexed VSG geometry with position/normal/UV
// arrays. It retains VSG geometry and graph identity and uses the game's light,
// fog and material descriptors, without creating a retail Mesh or MeshInstance.
class NativeContent : public vsg::Inherit<vsg::MatrixTransform, NativeContent> {
public:
    explicit NativeContent(vsg::ref_ptr<vsg::Node> source, std::shared_ptr<Material> material,
                           std::shared_ptr<Texture> texture = {});
    void prepare(RenderContext& context, const ScenePass& pass);
    void advanceTextures(std::chrono::milliseconds now);

private:
    struct Part {
        vsg::ref_ptr<DrawNode> node;
        glm::vec4 bounds{0};
        vsg::ref_ptr<vsg::vec4Array> instance, material;
        vsg::ref_ptr<vsg::PipelineLayout> layout;
        vsg::ref_ptr<vsg::ImageInfo> image;
    };
    std::vector<Part> parts;
    std::shared_ptr<Material> material;
    std::shared_ptr<Texture> texture;
};

// VSG's native readers own file decoding. Unsupported geometry fails explicitly.
vsg::ref_ptr<NativeContent> loadGameContent(const std::filesystem::path& path,
                                            std::shared_ptr<Material> material,
                                            std::shared_ptr<Texture> texture = {});

} // namespace render
