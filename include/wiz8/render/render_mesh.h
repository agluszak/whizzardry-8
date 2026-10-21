#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <vsg/core/Array.h>
#include <vsg/nodes/VertexIndexDraw.h>

#include "wiz8/render/draw_shader.h"

class Material;
class Mesh;
class Texture;

namespace render {

enum MeshAttribute : std::uint32_t {
    HasColors = 0x01u,
    HasDiffuseMultipliers = 0x02u,
    HasSpecularMultipliers = 0x04u,
    HasAlpha = 0x08u,
    HasTexcoord0 = 0x10u,
    HasTexcoord1 = 0x20u,
    HasVertexMaterials = 0x40u,
    ParticleBillboard = 0x80u
};

// The topology, UV seams and material assignments of one retail mesh. This is
// shared by its visual instances and contains no animation-frame encoding.
struct MeshAsset {
    struct Batch {
        std::array<std::shared_ptr<Texture>, 2> textures;
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::uint32_t first_polygon = 0;
    };
    struct Pass {
        unsigned pass = 0;
        unsigned side = 0;
        // The side whose materials this pass draws (the front when reused).
        unsigned material_side = 0;
        bool reverse_normals = false;
        bool cull = true;
        // Shader locations 2..6: UVs, additive colour, diffuse/specular
        // multipliers, then (material, mapped UV, vertex alpha, spare).
        std::array<vsg::ref_ptr<vsg::vec4Array>, 5> attributes;
        // Static geometry shares these arrays as well as its topology. Animated
        // instances supply their evaluated pose at the same shader locations.
        vsg::ref_ptr<vsg::vec3Array> positions;
        vsg::ref_ptr<vsg::vec3Array> normals;
        std::vector<std::uint32_t> source_vertices;
        vsg::ref_ptr<vsg::uintArray> indices;
        std::vector<std::uint32_t> polygons;
        std::vector<Batch> batches;
        std::vector<std::shared_ptr<Material>> materials;
        DrawShader shader;
        std::uint32_t draw_flags = 0;
    };
    // Pass-wide state changes that keep the topology. A pass that takes its
    // materials or a texture layer from a per-element table ignores them.
    void setMaterial(unsigned pass, unsigned side, const std::shared_ptr<Material>& material);
    void setTexture(unsigned pass, unsigned layer, const std::shared_ptr<Texture>& texture);
    void setShader(unsigned pass, DrawShader shader);

    std::vector<Pass> passes;
    glm::vec3 bounds_center{0};
    float bounds_radius = 0;
    bool sorted = false;
    bool animated = false;
    bool offset_pass_keys = true;
};

// One CPU-evaluated pose, reused by every view, picking, sorting and vertex
// attachments. Position and normal arrays are ordinary VSG geometry inputs.
class MeshPose {
public:
    struct Pass {
        vsg::ref_ptr<vsg::vec3Array> positions;
        vsg::ref_ptr<vsg::vec3Array> normals;
        // Authored integer-frame polygon normals, mapped to the currently
        // selected index buffer's triangle slots. CPU view filtering uses
        // these directly; they are not a shader input.
        vsg::ref_ptr<vsg::vec3Array> authored_normals;
        std::vector<std::uint32_t> source_polygons;
        std::vector<vsg::ref_ptr<vsg::VertexIndexDraw>> draws;
    };

    explicit MeshPose(Mesh& mesh);
    void update(Mesh& mesh, unsigned frame, float interpolation, int skin = -1,
                bool check_blanking = true);
    // Refresh live baked colours without rebuilding topology or batch nodes.
    void refreshColors(const Mesh& mesh);
    std::shared_ptr<Texture> texture(const Mesh& mesh, std::size_t pass, std::size_t batch,
                                     int skin, unsigned layer = 0) const;

    std::shared_ptr<MeshAsset> asset;
    std::vector<Pass> passes;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec3> polygon_normals;

private:
    void makeDraws();
    void selectPolygons(Mesh& mesh, int skin, bool check_blanking);
    unsigned sampled_frame_ = ~0u;
    float sampled_interpolation_ = -1;
    std::vector<std::uint32_t> selected_polygons_;
    bool selection_initialized_ = false;
    std::vector<vsg::ref_ptr<vsg::uintArray>> selected_indices_;
};

std::shared_ptr<MeshAsset> makeMeshAsset(Mesh& mesh);
bool authoredFaceVisible(const vsg::vec3Array& normals, const vsg::vec3Array& positions,
                         const vsg::uintArray& indices, std::uint32_t first, const glm::vec3& eye,
                         bool positive_winding);

} // namespace render
