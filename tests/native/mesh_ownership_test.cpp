#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srGERD.h"
#include "surrender/srMeshModel.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srVectorMath.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <vector>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                            \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

static_assert(std::is_same_v<decltype(srMeshModel::poly_vertices), std::vector<srVector3i>>);
static_assert(std::is_same_v<decltype(srMeshModel::vertex_locations),
                             std::vector<srVector3T<float>>>);
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
    CHECK(mesh->vertex_count == 0 && mesh->polygon_count == 0 && mesh->pass_count == 0);
    CHECK(mesh->control_flags == 0 && mesh->active_polygon_count == 0);
    CHECK(mesh->poly_vertices == nullptr && mesh->poly_equations == nullptr);
    CHECK(mesh->positions == nullptr && mesh->normals == nullptr && mesh->active_polygons == nullptr);
    CHECK(mesh->bounds_minimum == srVector3T<float>(0, 0, 0));
    CHECK(mesh->bounds_maximum == srVector3T<float>(0, 0, 0));
    CHECK(mesh->bounds_center == srVector3T<float>(0, 0, 0));
    CHECK(mesh->bounds_radius == 0 && mesh->sort_bias == 0);
    for (int pass = 0; pass < srMeshModel::MAX_PASSES; ++pass) {
        CHECK(mesh->dig[pass] == nullptr && mesh->dcg[pass] == nullptr && mesh->scg[pass] == nullptr);
        CHECK(mesh->poly_uv[pass] == nullptr && mesh->poly_shaders[pass] == nullptr);
        CHECK(mesh->shaders[pass].value == srShader().value);
        for (int side = 0; side < 2; ++side) {
            CHECK(mesh->texcoords[pass][side] == nullptr && mesh->materials[pass][side] == nullptr);
            CHECK(mesh->textures[pass][side] == nullptr);
            CHECK(mesh->vertex_materials[pass][side] == nullptr);
            CHECK(mesh->poly_textures[pass][side] == nullptr);
        }
    }
    std::destroy_at(mesh);
    return true;
}

static bool zeroNormals()
{
    std::array<srVector4, 2> normals;
    normals[0].Set(0, 0, 0, 0);
    normals[1].Set(0, 0, 0, 2);
    srMath::normalize(normals, normals.data(), 1.0f);
    CHECK(normals[0].x == 0 && normals[0].y == 0 && normals[0].z == 0 && normals[0].w == 0);
    CHECK(normals[1].x == 0 && normals[1].y == 0 && normals[1].z == 0 && normals[1].w == 1);
    return true;
}

static bool lazyTables()
{
    srMeshModel empty;
    CHECK(empty.getPolyVertex() == nullptr && empty.getVertexLoc() == nullptr);
    CHECK(empty.getVertexNormal() == nullptr && empty.getPolyEq() == nullptr);
    CHECK(empty.getActivePolygonTable(1) == nullptr && empty.getVertexShadeIndex(1) == nullptr);
    CHECK(empty.getPolyShader(0, 1) == nullptr && empty.getPolyUVIndex(0, 1) == nullptr);
    CHECK(empty.getVertexTexCoords(0, 0, 1) == nullptr && empty.getVertexDIG(0, 1) == nullptr);
    CHECK(empty.getVertexDCG(0, 1) == nullptr && empty.getVertexSCG(0, 1) == nullptr);
    CHECK(empty.getPolyTexture(0, 0, 1) == nullptr);
    CHECK(empty.getVertexMaterial(0, srMeshModel::SIDE_FRONT, 1) == nullptr);

    srMeshModel mesh(2, 3);
    CHECK(mesh.poly_vertices.empty() && mesh.vertex_locations.empty() && mesh.vertex_normals.empty());
    CHECK(mesh.poly_equations.empty() && mesh.active_polygons.empty());
    for (int pass = 0; pass < srMeshModel::MAX_PASSES; ++pass) {
        CHECK(mesh.getPolyShader(pass, 0) == nullptr && mesh.getPolyUVIndex(pass, 0) == nullptr);
        CHECK(mesh.getVertexDIG(pass, 0) == nullptr && mesh.getVertexDCG(pass, 0) == nullptr);
        CHECK(mesh.getVertexSCG(pass, 0) == nullptr);
        for (int side = 0; side < 2; ++side) {
            CHECK(mesh.getPolyTexture(pass, side, 0) == nullptr);
            CHECK(mesh.getVertexMaterial(pass, static_cast<srMeshModel::e_side>(side), 0) == nullptr);
            CHECK(mesh.getVertexTexCoords(pass, side, 0) == nullptr);
        }
    }
    CHECK(mesh.getTriMesh().dig[0] == nullptr);
    auto* locations = mesh.getVertexLoc();
    CHECK(locations == mesh.vertex_locations.data() && mesh.vertex_locations.size() == 3);
    CHECK(mesh.getVertexLoc() == locations);
    for (const auto& value : mesh.vertex_locations) CHECK(value == srVector3T<float>(0, 0, 0));
    for (const auto& value : mesh.vertex_normals) CHECK(value == srVector3T<float>(0, 0, 0));
    auto* corners = mesh.getPolyVertex();
    corners[0].x = 0;
    corners[0].y = 1;
    corners[0].z = 2;
    auto* uv_indices = mesh.getPolyUVIndex(2, 1);
    CHECK(uv_indices != corners && uv_indices[0].x == 0 && uv_indices[0].y == 1 && uv_indices[0].z == 2);
    CHECK(mesh.getTriMesh().poly_uv[2] == uv_indices);
    CHECK(mesh.getPolyUVIndex(0, 0) == nullptr);
    auto* active = mesh.getActivePolygonTable(1);
    CHECK(active[0] == 0 && active[1] == 1);
    CHECK(mesh.getTriMesh().active_polygons == active);
    mesh.setActivePolygonCount(1);
    CHECK(mesh.getTriMesh().active_polygon_count == 1);
    auto* dig = mesh.getVertexDIG(0, 1);
    CHECK(mesh.getTriMesh().dig[0] == dig);
    for (int index = 0; index < 3; ++index) {
        CHECK(dig[index] == srVector3T<float>(0, 0, 0));
        const auto& dcg = mesh.getVertexDCG(0, 1)[index];
        const auto& scg = mesh.getVertexSCG(0, 1)[index];
        CHECK(dcg.x == 1 && dcg.y == 1 && dcg.z == 1 && dcg.w == 1);
        CHECK(scg.x == 1 && scg.y == 1 && scg.z == 1 && scg.w == 1);
    }
    CHECK(mesh.getPolyShader(3, 1)[0].value == srShader().value);
    CHECK(mesh.getVertexShadeIndex(1)[0] == 0);
    CHECK(mesh.getPolyTexture(3, 1, 1)[0].get() == nullptr);
    CHECK(mesh.getVertexMaterial(3, srMeshModel::SIDE_BACK, 1)[0].get() == nullptr);
    CHECK(mesh.getVertexDIG(-1, 1) == nullptr && mesh.getVertexDIG(4, 1) == nullptr);
    CHECK(mesh.getVertexTexCoords(0, 2, 1) == nullptr);
    CHECK(mesh.getPolyTexture(4, 0, 1) == nullptr && mesh.getPolyShader(-1, 1) == nullptr);
    CHECK(mesh.getVertexMaterial(0, static_cast<srMeshModel::e_side>(2), 1) == nullptr);
    return true;
}

static bool uvGrowth()
{
    srMeshModel mesh(1, 3);
    auto* uv = mesh.getVertexTexCoords(1, 1, 1);
    uv[0].Set(0.25f, 0.75f);
    mesh.getTriMesh();
    mesh.setUVCount(12);
    uv = mesh.getVertexTexCoords(1, 1, 0);
    CHECK(mesh.texcoords[1][1].size() == 12 && uv[0].x == 0.25f && uv[0].y == 0.75f);
    for (int index = 3; index < 12; ++index) CHECK(uv[index].x == 0 && uv[index].y == 0);
    CHECK(mesh.getTriMesh().texcoords[1][1] == uv);
    CHECK(mesh.getVertexTexCoords(0, 0, 0) == nullptr);
    mesh.setUVCount(1);
    CHECK(mesh.getUVCount() == 3 && mesh.texcoords[1][1].size() == 3);
    mesh.setUVCount(6);
    uv = mesh.getVertexTexCoords(1, 1, 0);
    for (int index = 3; index < 6; ++index) CHECK(uv[index].x == 0 && uv[index].y == 0);

    // A cleared table may still have allocation capacity; it is nevertheless absent/null.
    srMeshModel no_vertices;
    no_vertices.setUVCount(8);
    CHECK(no_vertices.getVertexTexCoords(0, 0, 1) != nullptr);
    no_vertices.setUVCount(0);
    CHECK(no_vertices.getVertexTexCoords(0, 0, 0) == nullptr);
    no_vertices.setUVCount(4);
    CHECK(no_vertices.getVertexTexCoords(0, 0, 0) == nullptr);
    CHECK(no_vertices.getVertexTexCoords(0, 0, 1)[0].x == 0);
    return true;
}

static bool copyAndReset()
{
    auto* material = srCore.getMaterial();
    auto* texture = srCore.getTexture();
    const auto material_references = material->getReferenceCount();
    const auto texture_references = texture->getReferenceCount();
    {
        srMeshModel source(1, 3);
        source.getVertexLoc()[1].Set(1, 2, 3);
        source.getPolyVertex()[0].y = 1;
        source.setUVCount(7);
        source.getVertexTexCoords(2, 0, 1)[6].Set(0.5f, 0.75f);
        source.getVertexMaterial(1, srMeshModel::SIDE_BACK, 1)[2] = material;
        source.getPolyTexture(1, 0, 1)[0] = texture;
        source.setMaterial(material, 0, srMeshModel::SIDE_FRONT);
        source.setTexture(texture, 0, 0);
        source.setSortBias(0.25f);
        source.setPassCount(3);
        const auto view = source.getTriMesh();
        CHECK(material->getReferenceCount() == material_references + 2);
        CHECK(texture->getReferenceCount() == texture_references + 2);
        srMeshModel copy(source);
        CHECK(copy.getID() != source.getID());
        CHECK(copy.getUVCount() == 7 && copy.getSortBias() == 0.25f && copy.getPassCount() == 3);
        CHECK(copy.tri_mesh.positions == nullptr && copy.testDirty(srMeshModel::DIRTY_TRI_MESH));
        const auto copied_view = copy.getTriMesh();
        CHECK(copied_view.positions != view.positions && copied_view.poly_vertices != view.poly_vertices);
        CHECK(copied_view.texcoords[2][0] != view.texcoords[2][0]);
        CHECK(copied_view.positions[1] == srVector3T<float>(1, 2, 3));
        CHECK(copied_view.texcoords[2][0][6].x == 0.5f);
        CHECK(copy.getVertexDIG(0, 0) == nullptr && copy.getPolyShader(0, 0) == nullptr);
        CHECK(material->getReferenceCount() == material_references + 4);
        CHECK(texture->getReferenceCount() == texture_references + 4);

        srMeshModel assigned(5, 10);
        assigned.getVertexDIG(0, 1);
        assigned = source;
        CHECK(assigned.getUVCount() == 7 && assigned.getSortBias() == 0.25f);
        CHECK(assigned.getTriMesh().positions != view.positions);
        CHECK(assigned.getVertexDIG(0, 0) == nullptr);
        const srMeshModel& self = assigned;
        assigned = self;
        CHECK(assigned.getTriMesh().texcoords[2][0][6].y == 0.75f);
        copy.getVertexLoc()[1].x = 99;
        CHECK(source.getVertexLoc()[1].x == 1);
        copy.reset(2, 4);
        CHECK(copy.getVertexCount() == 4 && copy.getUVCount() == 4 && copy.getPolygonCount() == 2);
        CHECK(copy.getPassCount() == 1 && copy.getActivePolygonCount() == 2);
        CHECK(copy.getSortBias() == 0.25f && copy.getMaterial(0, srMeshModel::SIDE_FRONT) == material);
        CHECK(copy.getTexture(0, 0) == texture);
        CHECK(copy.vertex_locations.empty() && copy.vertex_locations.capacity() == 0);
        CHECK(copy.texcoords[2][0].empty() && copy.texcoords[2][0].capacity() == 0);
        CHECK(copy.tri_mesh.positions == nullptr && copy.tri_mesh.texcoords[2][0] == nullptr);
        CHECK(copy.getVertexMaterial(1, srMeshModel::SIDE_BACK, 0) == nullptr);
        CHECK(copy.getPolyTexture(1, 0, 0) == nullptr && copy.getActivePolygonTable(0) == nullptr);
        CHECK(copy.getVertexLoc()[1] == srVector3T<float>(0, 0, 0));
        // Resetting the source cannot invalidate the destination's owned tables.
        source.reset(0, 0);
        CHECK(assigned.getTriMesh().positions[1].x == 1);
        CHECK(assigned.getTriMesh().texcoords[2][0][6].x == 0.5f);
        CHECK(source.getTriMesh().poly_vertices == nullptr && source.getTriMesh().positions == nullptr);
    }
    CHECK(material->getReferenceCount() == material_references);
    CHECK(texture->getReferenceCount() == texture_references);
    return true;
}

static bool pipelineSlots()
{
    // Metadata-only GERD/device construction: no window, GPU context or rendering is requested.
    srGERD renderer(srCreateSDLGPUDevice(), "SDLGPU");
    auto* pipeline = srTriMeshPipeline::Get(&renderer);
    CHECK(pipeline && pipeline->vertex_pipe && pipeline->active_triangle_count == 0);
    srMeshModel mesh(1, 3);
    auto* uv = mesh.getVertexTexCoords(0, 0, 1);
    auto* textures = mesh.getPolyTexture(0, 0, 1);
    auto* indices = mesh.getPolyUVIndex(0, 1);
    srShader shader;
    for (unsigned slot = 0; slot < 96; ++slot) {
        CHECK(pipeline->current_record == &pipeline->records[slot]);
        CHECK(pipeline->current_pass == &pipeline->passes[slot]);
        CHECK(pipeline->current_record->flags == 0 && pipeline->current_record->st0 == nullptr);
        CHECK(pipeline->current_record->dcg == nullptr && pipeline->current_record->alphas == nullptr);
        CHECK(pipeline->current_pass->poly_uv == nullptr);
        pipeline->current_record->st0 = uv;
        pipeline->current_record->flags = srVertexPipe::Record::HAS_TEXCOORD0;
        pipeline->current_pass->poly_uv = indices;
        pipeline->current_pass->texture_tables[0] = textures;
        pipeline->current_pass->shaders = &shader;
        ++pipeline->slot_count;
        pipeline->PrepareSlot();
        for (unsigned previous = 0; previous <= slot; ++previous) {
            CHECK(pipeline->records[previous].st0 == uv);
            CHECK(pipeline->passes[previous].poly_uv == indices);
            CHECK(pipeline->passes[previous].texture_tables[0] == textures);
            CHECK(pipeline->passes[previous].shaders == &shader);
        }
    }
    // Zero triangles exercise the real Flush boundary without any device work.
    pipeline->FlushIfCurrent();
    CHECK(pipeline->records[0].st0 == uv && pipeline->passes[0].texture_tables[0] == textures);
    pipeline->Reset(&renderer);
    CHECK(pipeline->slot_count == 0 && pipeline->current_record->st0 == nullptr);
    CHECK(pipeline->current_pass->texture_tables[0] == nullptr && pipeline->current_pass->shaders == nullptr);
    CHECK(pipeline->current_record->vertex_materials == nullptr);
    CHECK(pipeline->bounds_minimum == srVector3T<float>(0, 0, 0));
    return true;
}

int main()
{
    if (!defaults() || !srInit()) return 1;
    const bool passed = zeroNormals() && lazyTables() && uvGrowth() && copyAndReset() && pipelineSlots();
    if (!srExit() || !passed) return 1;
    std::puts("ok: mesh ownership, lazy tables, reset/copy cache state and pipeline slot growth");
}
