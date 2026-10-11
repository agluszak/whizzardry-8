#include "glm/ext/vector_float3.hpp"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srGERD.h"
#include "surrender/srMeshModel.h"
#include "surrender/srTextureMap.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srMath.h"
#include "glm/ext/vector_float2.hpp"
#include "surrender/srMaterial.h"
#include "surrender/srShader.h"
#include "surrender/srVertexPipe.h"


static_assert(std::is_same_v<decltype(srMeshModel::poly_vertices), std::vector<srVector3i>>);
static_assert(std::is_same_v<decltype(srMeshModel::vertex_locations),
                             std::vector<glm::vec3>>);
static_assert(std::is_same_v<decltype(srTriMeshPipeline::vertex_pipe),
                             std::unique_ptr<srVertexPipe>>);
static_assert(!std::is_copy_constructible_v<srTriMeshPipeline>);

static bool defaults()
{
    // The vector default constructors do not zero their components. Start on dirty storage so
    // aggregate/member initialization, not incidental stack contents, must initialize the view.
    alignas(srMeshModel::TriMesh) std::array<std::byte, sizeof(srMeshModel::TriMesh)> storage;
    storage.fill(std::byte{0xa5});
    auto* mesh = std::construct_at(reinterpret_cast<srMeshModel::TriMesh*>(storage.data()));
    REQUIRE((mesh->vertex_count == 0 && mesh->polygon_count == 0 && mesh->pass_count == 0));
    REQUIRE((mesh->control_flags == 0 && !mesh->active_polygons));
    REQUIRE((mesh->poly_vertices == nullptr && mesh->poly_equations == nullptr));
    REQUIRE((mesh->positions == nullptr && mesh->normals == nullptr && !mesh->active_polygons));
    REQUIRE(mesh->bounds_minimum == glm::vec3(0, 0, 0));
    REQUIRE(mesh->bounds_maximum == glm::vec3(0, 0, 0));
    REQUIRE(mesh->bounds_center == glm::vec3(0, 0, 0));
    REQUIRE((mesh->bounds_radius == 0 && mesh->sort_bias == 0));
    for (int pass = 0; pass < srMeshModel::MAX_PASSES; ++pass) {
        REQUIRE((mesh->dig[pass] == nullptr && mesh->dcg[pass] == nullptr && mesh->scg[pass] == nullptr));
        REQUIRE((mesh->poly_uv[pass] == nullptr && mesh->poly_shaders[pass] == nullptr));
        REQUIRE(mesh->shaders[pass].value == srShader().value);
        for (int side = 0; side < 2; ++side) {
            REQUIRE((mesh->texcoords[pass][side] == nullptr && mesh->materials[pass][side] == nullptr));
            REQUIRE(mesh->textures[pass][side] == nullptr);
            REQUIRE(mesh->vertex_materials[pass][side] == nullptr);
            REQUIRE(mesh->poly_textures[pass][side] == nullptr);
        }
    }
    std::destroy_at(mesh);
    return true;
}

static bool zeroNormals()
{
    std::array<srVector3, 2> normals;
    normals[0].Set(0, 0, 0);
    normals[1].Set(0, 0, 2);
    for (auto& normal : normals)
        normal.Normalize();
    REQUIRE(normals[0] == srVector3(0, 0, 0));
    REQUIRE(normals[1] == srVector3(0, 0, 1));
    return true;
}

static bool lazyTables()
{
    srMeshModel empty;
    REQUIRE((empty.getPolyVertex() == nullptr && empty.getVertexLoc() == nullptr));
    REQUIRE((empty.getVertexNormal() == nullptr && empty.getPolyEq() == nullptr));
    REQUIRE((empty.getActivePolygonTable(1) == nullptr && empty.getVertexShadeIndex(1) == nullptr));
    REQUIRE((empty.getPolyShader(0, 1) == nullptr && empty.getPolyUVIndex(0, 1) == nullptr));
    REQUIRE((empty.getVertexTexCoords(0, 0, 1) == nullptr && empty.getVertexDIG(0, 1) == nullptr));
    REQUIRE((empty.getVertexDCG(0, 1) == nullptr && empty.getVertexSCG(0, 1) == nullptr));
    REQUIRE(empty.getPolyTexture(0, 0, 1) == nullptr);
    REQUIRE(empty.getVertexMaterial(0, srMeshModel::SIDE_FRONT, 1) == nullptr);

    srMeshModel mesh(2, 3);
    REQUIRE((mesh.poly_vertices.empty() && mesh.vertex_locations.empty() && mesh.vertex_normals.empty()));
    REQUIRE((mesh.poly_equations.empty() && mesh.active_polygons.empty()));
    for (int pass = 0; pass < srMeshModel::MAX_PASSES; ++pass) {
        REQUIRE((mesh.getPolyShader(pass, 0) == nullptr && mesh.getPolyUVIndex(pass, 0) == nullptr));
        REQUIRE((mesh.getVertexDIG(pass, 0) == nullptr && mesh.getVertexDCG(pass, 0) == nullptr));
        REQUIRE(mesh.getVertexSCG(pass, 0) == nullptr);
        for (int side = 0; side < 2; ++side) {
            REQUIRE(mesh.getPolyTexture(pass, side, 0) == nullptr);
            REQUIRE(mesh.getVertexMaterial(pass, static_cast<srMeshModel::e_side>(side), 0) == nullptr);
            REQUIRE(mesh.getVertexTexCoords(pass, side, 0) == nullptr);
        }
    }
    REQUIRE(mesh.getTriMesh().dig[0] == nullptr);
    auto* locations = mesh.getVertexLoc();
    REQUIRE((locations == mesh.vertex_locations.data() && mesh.vertex_locations.size() == 3));
    REQUIRE(mesh.getVertexLoc() == locations);
    for (const auto& value : mesh.vertex_locations) REQUIRE(value == glm::vec3(0, 0, 0));
    for (const auto& value : mesh.vertex_normals) REQUIRE(value == glm::vec3(0, 0, 0));
    auto* corners = mesh.getPolyVertex();
    corners[0].x = 0;
    corners[0].y = 1;
    corners[0].z = 2;
    auto* uv_indices = mesh.getPolyUVIndex(2, 1);
    REQUIRE((uv_indices != corners && uv_indices[0].x == 0 && uv_indices[0].y == 1 && uv_indices[0].z == 2));
    REQUIRE(mesh.getTriMesh().poly_uv[2] == uv_indices);
    REQUIRE(mesh.getPolyUVIndex(0, 0) == nullptr);
    auto* active = mesh.getActivePolygonTable(1);
    REQUIRE((active[0] == 0 && active[1] == 1));
    REQUIRE(mesh.getTriMesh().active_polygons->data() == active);
    mesh.setActivePolygonCount(1);
    REQUIRE(mesh.getTriMesh().active_polygons->size() == 1);
    auto* dig = mesh.getVertexDIG(0, 1);
    REQUIRE(mesh.getTriMesh().dig[0] == dig);
    for (int index = 0; index < 3; ++index) {
        REQUIRE(dig[index] == glm::vec3(0, 0, 0));
        const auto& dcg = mesh.getVertexDCG(0, 1)[index];
        const auto& scg = mesh.getVertexSCG(0, 1)[index];
        REQUIRE((dcg.x == 1 && dcg.y == 1 && dcg.z == 1 && dcg.w == 1));
        REQUIRE((scg.x == 1 && scg.y == 1 && scg.z == 1 && scg.w == 1));
    }
    REQUIRE(mesh.getPolyShader(3, 1)[0].value == srShader().value);
    REQUIRE(mesh.getVertexShadeIndex(1)[0] == 0);
    REQUIRE(mesh.getPolyTexture(3, 1, 1)[0].get() == nullptr);
    REQUIRE(mesh.getVertexMaterial(3, srMeshModel::SIDE_BACK, 1)[0].get() == nullptr);
    REQUIRE((mesh.getVertexDIG(-1, 1) == nullptr && mesh.getVertexDIG(4, 1) == nullptr));
    REQUIRE(mesh.getVertexTexCoords(0, 2, 1) == nullptr);
    REQUIRE((mesh.getPolyTexture(4, 0, 1) == nullptr && mesh.getPolyShader(-1, 1) == nullptr));
    REQUIRE(mesh.getVertexMaterial(0, static_cast<srMeshModel::e_side>(2), 1) == nullptr);
    return true;
}

static bool uvGrowth()
{
    srMeshModel mesh(1, 3);
    auto* uv = mesh.getVertexTexCoords(1, 1, 1);
    uv[0] = glm::vec2(0.25f, 0.75f);
    mesh.getTriMesh();
    mesh.setUVCount(12);
    uv = mesh.getVertexTexCoords(1, 1, 0);
    REQUIRE((mesh.texcoords[1][1].size() == 12 && uv[0].x == 0.25f && uv[0].y == 0.75f));
    for (int index = 3; index < 12; ++index) REQUIRE((uv[index].x == 0 && uv[index].y == 0));
    REQUIRE(mesh.getTriMesh().texcoords[1][1] == uv);
    REQUIRE(mesh.getVertexTexCoords(0, 0, 0) == nullptr);
    mesh.setUVCount(1);
    REQUIRE((mesh.getUVCount() == 3 && mesh.texcoords[1][1].size() == 3));
    mesh.setUVCount(6);
    uv = mesh.getVertexTexCoords(1, 1, 0);
    for (int index = 3; index < 6; ++index) REQUIRE((uv[index].x == 0 && uv[index].y == 0));

    // A cleared table may still have allocation capacity; it is nevertheless absent/null.
    srMeshModel no_vertices;
    no_vertices.setUVCount(8);
    REQUIRE(no_vertices.getVertexTexCoords(0, 0, 1) != nullptr);
    no_vertices.setUVCount(0);
    REQUIRE(no_vertices.getVertexTexCoords(0, 0, 0) == nullptr);
    no_vertices.setUVCount(4);
    REQUIRE(no_vertices.getVertexTexCoords(0, 0, 0) == nullptr);
    REQUIRE(no_vertices.getVertexTexCoords(0, 0, 1)[0].x == 0);
    return true;
}

static bool reset()
{
    auto material = std::make_shared<srMaterial>();
    auto texture = std::make_shared<srTextureMap>();
    {
        srMeshModel mesh(1, 3);
        mesh.getVertexLoc()[1] = glm::vec3(1, 2, 3);
        mesh.setUVCount(7);
        mesh.getVertexTexCoords(2, 0, 1)[6] = glm::vec2(0.5f, 0.75f);
        mesh.getVertexMaterial(1, srMeshModel::SIDE_BACK, 1)[2] = material;
        mesh.getPolyTexture(1, 0, 1)[0] = texture;
        mesh.setMaterial(material, 0, srMeshModel::SIDE_FRONT);
        mesh.setTexture(texture, 0, 0);
        mesh.setSortBias(0.25f);
        mesh.setPassCount(3);
        REQUIRE(mesh.getTriMesh().texcoords[2][0][6].x == 0.5f);
        mesh.reset(2, 4);
        REQUIRE((mesh.getVertexCount() == 4 && mesh.getUVCount() == 4 && mesh.getPolygonCount() == 2));
        REQUIRE((mesh.getPassCount() == 1 && mesh.getActivePolygonCount() == 2));
        REQUIRE((mesh.getSortBias() == 0.25f && mesh.getMaterial(0, srMeshModel::SIDE_FRONT) == material.get()));
        REQUIRE(mesh.getTexture(0, 0) == texture.get());
        REQUIRE((mesh.vertex_locations.empty() && mesh.vertex_locations.capacity() == 0));
        REQUIRE((mesh.texcoords[2][0].empty() && mesh.texcoords[2][0].capacity() == 0));
        REQUIRE((mesh.tri_mesh.positions == nullptr && mesh.tri_mesh.texcoords[2][0] == nullptr));
        REQUIRE(mesh.getVertexMaterial(1, srMeshModel::SIDE_BACK, 0) == nullptr);
        REQUIRE((mesh.getPolyTexture(1, 0, 0) == nullptr && mesh.getActivePolygonTable(0) == nullptr));
        REQUIRE(mesh.getVertexLoc()[1] == glm::vec3(0, 0, 0));
    }
    REQUIRE((material.use_count() == 1 && texture.use_count() == 1));
    return true;
}

static bool pipelineSlots()
{
    // Metadata-only GERD/device construction: no window, GPU context or rendering is requested.
    srGERD renderer(srCreateSDLGPUDevice(), "SDLGPU");
    auto* pipeline = srTriMeshPipeline::Get(&renderer);
    REQUIRE((pipeline && pipeline->vertex_pipe && !pipeline->active_triangles));
    pipeline->triangle_count = 1;
    pipeline->active_triangles.emplace();
    pipeline->FlushSlots();
    REQUIRE(pipeline->active_triangles->empty());
    pipeline->Reset(&renderer);
    REQUIRE_FALSE(pipeline->active_triangles);
    srMeshModel mesh(1, 3);
    auto* uv = mesh.getVertexTexCoords(0, 0, 1);
    auto* textures = mesh.getPolyTexture(0, 0, 1);
    auto* indices = mesh.getPolyUVIndex(0, 1);
    srShader shader;
    auto submitted = mesh.getTriMesh();
    submitted.poly_shaders[0] = &shader;
    for (unsigned slot = 0; slot < 96; ++slot) {
        REQUIRE(pipeline->current_record == &pipeline->records[slot]);
        REQUIRE(pipeline->current_pass == &pipeline->passes[slot]);
        REQUIRE((pipeline->current_record->flags == 0 &&
              pipeline->current_record->st_source[0] == nullptr));
        REQUIRE((pipeline->current_record->spec_for_diffuse == nullptr &&
              pipeline->current_record->alpha_source == nullptr));
        REQUIRE(pipeline->current_pass->poly_uv == nullptr);
        pipeline->AppendMeshPass(submitted, 0, srMeshModel::SIDE_FRONT, srShader());
        for (unsigned previous = 0; previous <= slot; ++previous) {
            REQUIRE(pipeline->records[previous].st_source[0] == uv);
            REQUIRE(pipeline->passes[previous].poly_uv == indices);
            REQUIRE(pipeline->passes[previous].texture_tables[0] == textures);
            REQUIRE(pipeline->passes[previous].shaders == &shader);
        }
    }
    // Zero triangles exercise the real Flush boundary without any device work.
    pipeline->FlushIfCurrent();
    REQUIRE((pipeline->records[0].st_source[0] == uv && pipeline->passes[0].texture_tables[0] == textures));
    pipeline->Reset(&renderer);
    REQUIRE((pipeline->slot_count == 0 && pipeline->current_record->st_source[0] == nullptr));
    REQUIRE((pipeline->current_pass->texture_tables[0] == nullptr && pipeline->current_pass->shaders == nullptr));
    REQUIRE(pipeline->current_record->materials == nullptr);
    REQUIRE(pipeline->bounds_minimum == glm::vec3(0, 0, 0));
    // A per-polygon shader table wins over the caller's uniform override.
    srShader override_shader;
    override_shader.value ^= srShader::PASS_MASK;
    pipeline->AppendMeshPass(submitted, 0, srMeshModel::SIDE_FRONT, override_shader);
    REQUIRE(pipeline->passes[0].shaders == &shader);
    submitted.poly_shaders[0] = nullptr;
    pipeline->AppendMeshPass(submitted, 0, srMeshModel::SIDE_FRONT, override_shader);
    REQUIRE(pipeline->passes[1].shaders == nullptr);
    REQUIRE(pipeline->passes[1].shader.value == override_shader.value);

    // Sphere bounds take precedence when both automatic bounds are enabled.
    for (unsigned box = 0; box < 2; ++box) {
        for (unsigned sphere = 0; sphere < 2; ++sphere) {
            pipeline->Reset(&renderer);
            submitted.control_flags =
                (box ? 0 : 1u << srMeshModel::CONTROL_SKIP_AUTO_BOX) |
                (sphere ? 0 : 1u << srMeshModel::CONTROL_SKIP_AUTO_SPHERE);
            pipeline->SetMeshBounds(submitted);
            REQUIRE(pipeline->bounds_source == (sphere ? srTriMeshPipeline::BOUNDS_SPHERE :
                box ? srTriMeshPipeline::BOUNDS_BOX : srTriMeshPipeline::BOUNDS_FROM_VERTICES));
        }
    }
    return true;
}

TEST_CASE("mesh_ownership_test")
{
    REQUIRE(defaults());
    REQUIRE(srInit());
    REQUIRE(zeroNormals());
    REQUIRE(lazyTables());
    REQUIRE(uvGrowth());
    REQUIRE(reset());
    REQUIRE(pipelineSlots());
    REQUIRE(srExit());
}
