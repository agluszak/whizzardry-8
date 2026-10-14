#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "surrender/srTextureIFace.h"
#include "surrender_test_fixture.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"

class srModel;

static_assert(!std::is_copy_constructible_v<stMeshModel>);
static_assert(!std::is_copy_assignable_v<stMeshModel>);
static_assert(std::is_nothrow_move_constructible_v<stMeshModel::Frame>);
static_assert(std::is_nothrow_move_constructible_v<stMeshModel::Skin>);

static bool frame_storage()
{
    const auto registered = g_mesh_models.size();
    const int initial_bytes = g_decompressed_mesh_bytes;
    {
        stMeshModel mesh(1, 3);
        REQUIRE(mesh.frames.empty());
        REQUIRE(mesh.GetVertex(0) == nullptr);
        REQUIRE(mesh.GetVertexLocations(0, true, 0.5f) == nullptr);
        REQUIRE(mesh.GetVertexNormals(0, true) == nullptr);
        REQUIRE(mesh.GetPolygonNormals(0, true) == nullptr);
        mesh.InitializeVertexFrames(2);
        mesh.vertex_compression_scale = 0.5f;
        REQUIRE(mesh.frames.size() == 2);
        REQUIRE(g_mesh_models.size() == registered + 1);
        REQUIRE(g_decompressed_mesh_bytes == initial_bytes);
        for (const auto& frame : mesh.frames) {
            REQUIRE(frame.compressed_locations.size() == 9);
            REQUIRE(frame.compressed_vertex_normals.size() == 9);
            REQUIRE(frame.compressed_polygon_normals.size() == 3);
            REQUIRE(std::all_of(frame.compressed_locations.begin(),
                                frame.compressed_locations.end(),
                                [](short value) { return value == 0; }));
            REQUIRE((!frame.locations && !frame.vertex_normals && !frame.polygon_normals));
        }
        auto* polygon = mesh.getPolyVertex();
        polygon->x = 0;
        polygon->y = 1;
        polygon->z = 2;
        const std::array<short, 9> first{0, 0, 0, 2, 0, 0, 0, 2, 0};
        const std::array<short, 9> second{4, 0, 0, 6, 0, 0, 4, 2, 0};
        auto* compressed = mesh.GetVertex(0);
        std::copy(first.begin(), first.end(), compressed);
        mesh.InitializeVertexFrames(2); // ReadMesh repeats initialization for every frame.
        REQUIRE(mesh.GetVertex(0) == compressed);
        REQUIRE(std::equal(first.begin(), first.end(), compressed));
        REQUIRE(g_mesh_models.size() == registered + 1);
        std::copy(second.begin(), second.end(), mesh.GetVertex(1));
        mesh.FinalizeVertexFrame(0);
        mesh.FinalizeVertexFrame(1);
        const std::vector<unsigned char> vertex_normals{0, 0, 127, 0, 0, 127, 0, 0, 127};
        REQUIRE(mesh.frames[0].compressed_vertex_normals == vertex_normals);
        REQUIRE(mesh.frames[0].compressed_polygon_normals ==
                std::vector<unsigned char>({0, 0, 127}));
        REQUIRE(!mesh.frames[0].locations);
        REQUIRE(g_decompressed_mesh_bytes ==
                initial_bytes); // Normal-builder scratch is not a cache.

        auto bounds = mesh.GetFrameBounds(1);
        REQUIRE(bounds.minimum == glm::vec3(2.0f, 0.0f, 0.0f));
        REQUIRE(bounds.maximum == glm::vec3(3.0f, 1.0f, 0.0f));
        REQUIRE(!mesh.frames[1].locations);
        REQUIRE(g_decompressed_mesh_bytes ==
                initial_bytes); // Bounds must not make a frame resident.
        auto* positions = mesh.GetVertexLocations(0, true, 0.0f);
        REQUIRE(positions[1] == glm::vec3(1.0f, 0.0f, 0.0f));
        REQUIRE(g_decompressed_mesh_bytes == initial_bytes + 3 * sizeof(glm::vec3));
        REQUIRE(mesh.GetVertexLocations(0, true, 0.0f) == positions);
        auto* normals = mesh.GetVertexNormals(0, true);
        auto* polygon_normals = mesh.GetPolygonNormals(0, true);
        REQUIRE(normals[2] == glm::vec3(0.0f, 0.0f, 1.0f));
        REQUIRE(polygon_normals[0] == normals[2]);
        REQUIRE(mesh.GetVertexLocations(0, true, 0.0f) == positions);
        auto* shade_indices = mesh.getVertexShadeIndex(1);
        shade_indices[0] = shade_indices[1] = shade_indices[2] = 0;
        mesh.ComputeFrameNormals(0);
        REQUIRE(mesh.frames[0].compressed_vertex_normals == vertex_normals);
        REQUIRE(mesh.GetVertexNormals(0, true) == normals);
        REQUIRE(mesh.GetPolygonNormals(0, true) == polygon_normals);
        auto* lerp = mesh.GetVertexLocations(0, true, 0.25f);
        REQUIRE(lerp[0].x == Catch::Approx(0.5f).margin(1e-6));
        REQUIRE(lerp[1].x == Catch::Approx(1.5f).margin(1e-6));
        REQUIRE(mesh.GetVertexLocations(0, true, 1.0f) == lerp);
        REQUIRE(lerp[0] == glm::vec3(2.0f, 0.0f, 0.0f));
        REQUIRE(mesh.GetVertexLocations(1, true, 0.75f) == mesh.frames[1].locations->data());
        bounds = mesh.GetFrameBounds(0);
        REQUIRE(bounds.minimum == glm::vec3(0.0f, 0.0f, 0.0f));
        REQUIRE(bounds.maximum == glm::vec3(1.0f, 1.0f, 0.0f));
        bounds = mesh.GetFrameBounds(-1);
        REQUIRE(
            (bounds.minimum == glm::vec3(0.0f, 0.0f, 0.0f) && bounds.minimum == bounds.maximum));
        const int cache_bytes = (3 + 3 + 3 + 1) * sizeof(glm::vec3);
        REQUIRE(g_decompressed_mesh_bytes == initial_bytes + cache_bytes);
        REQUIRE(mesh.ReleaseDecompressedFrames() == cache_bytes);
        REQUIRE(mesh.ReleaseDecompressedFrames() == 0);
        REQUIRE(g_decompressed_mesh_bytes == initial_bytes);
        REQUIRE((!mesh.frames[0].locations && !mesh.frames[0].vertex_normals));
        REQUIRE((!mesh.frames[0].polygon_normals && !mesh.frames[1].locations));
        REQUIRE(mesh.GetVertex(0) == compressed);
        REQUIRE(std::equal(first.begin(), first.end(), compressed));
        REQUIRE(lerp ==
                mesh.lerp_buffer.data()); // Explicit cache eviction does not evict interpolation.
        REQUIRE(mesh.GetVertexLocations(0, true, 0.25f) == lerp);
        REQUIRE(lerp[0].x == Catch::Approx(0.5f).margin(1e-6));
        REQUIRE(mesh.GetVertex(2) == nullptr);
        REQUIRE(mesh.GetVertexLocations(2, true, 0.0f) == nullptr);
        REQUIRE(mesh.GetVertexNormals(2, true) == nullptr);
        REQUIRE(mesh.GetPolygonNormals(2, true) == nullptr);
        REQUIRE(!mesh.AllocateFrameBuffers(2, W8_MESH_FRAME_LOCATIONS));
        REQUIRE(!mesh.DecompressFrame(-1, W8_MESH_FRAME_LOCATIONS, std::span(positions, 3)));
    }
    REQUIRE(g_mesh_models.size() == registered);
    REQUIRE(g_decompressed_mesh_bytes == initial_bytes);
    return true;
}

static bool writable_and_empty_caches()
{
    const int initial_bytes = g_decompressed_mesh_bytes;
    {
        stMeshModel mesh(0, 1);
        mesh.InitializeVertexFrames(1);
        mesh.vertex_compression_scale = 0.25f;
        auto* source = mesh.GetVertex(0);
        source[0] = -32768;
        source[1] = 32767;
        source[2] = -1;
        auto* locations = mesh.GetVertexLocations(0, false, 0.0f);
        locations[0] = glm::vec3(7.0f, 8.0f, 9.0f);
        REQUIRE(mesh.GetVertexLocations(0, true, 0.0f) == locations);
        REQUIRE(locations[0] == glm::vec3(7.0f, 8.0f, 9.0f)); // Do not overwrite active data.
        REQUIRE(mesh.ReleaseDecompressedFrames() == sizeof(glm::vec3));
        locations = mesh.GetVertexLocations(0, true, 0.0f);
        REQUIRE(locations[0] == glm::vec3(-8192.0f, 8191.75f, -0.25f));
        mesh.frames[0].compressed_vertex_normals = {128, 255, 127};
        auto* normals = mesh.GetVertexNormals(0, true);
        REQUIRE(normals[0].x == -1.0f);
        REQUIRE(normals[0].y == Catch::Approx(-1.0f / 127.0f).margin(1e-6));
        REQUIRE(normals[0].z == 1.0f);
        REQUIRE(mesh.GetPolygonNormals(0, true) == nullptr);
        REQUIRE(mesh.frames[0].polygon_normals.has_value()); // Active zero-sized cache, not absent.
        REQUIRE(mesh.ReleaseDecompressedFrames() == 2 * sizeof(glm::vec3));
        REQUIRE(!mesh.frames[0].polygon_normals);
    }
    {
        stMeshModel empty(0, 0);
        empty.InitializeVertexFrames(1);
        REQUIRE(empty.AllocateFrameBuffers(0, 7));
        REQUIRE((empty.frames[0].locations && empty.frames[0].locations->empty()));
        REQUIRE((empty.frames[0].vertex_normals && empty.frames[0].polygon_normals));
        REQUIRE(g_decompressed_mesh_bytes == initial_bytes);
        REQUIRE(!ReclaimDecompressedBytes(1)); // No infinite loop on resident empty caches.
        REQUIRE(empty.ReleaseDecompressedFrames() == 0);
        REQUIRE(!empty.frames[0].locations);
    }
    REQUIRE(g_decompressed_mesh_bytes == initial_bytes);
    return true;
}

static bool eviction_and_borrowed_outputs()
{
    const int initial_bytes = g_decompressed_mesh_bytes;
    {
        stMeshModel old_mesh(0, 1), recent_mesh(0, 1), drained_mesh(0, 1);
        old_mesh.InitializeVertexFrames(1);
        recent_mesh.InitializeVertexFrames(1);
        drained_mesh.InitializeVertexFrames(1);
        old_mesh.GetVertexLocations(0, true, 0.0f);
        recent_mesh.GetVertexLocations(0, true, 0.0f);
        old_mesh.last_decompress_release_tick = 1;
        recent_mesh.last_decompress_release_tick = 2;
        drained_mesh.last_decompress_release_tick = 0;
        REQUIRE(ReclaimDecompressedBytes(sizeof(glm::vec3)));
        REQUIRE((!old_mesh.frames[0].locations && recent_mesh.frames[0].locations));
        REQUIRE(!ReclaimDecompressedBytes(1, &recent_mesh));
        REQUIRE(recent_mesh.frames[0].locations);
        REQUIRE(!ReclaimDecompressedBytes(1000)); // Drains the remainder, then terminates.
        REQUIRE(!recent_mesh.frames[0].locations);
        REQUIRE(g_decompressed_mesh_bytes == initial_bytes);
    }
    {
        // Two location frames and normals exceed the 8 MiB budget. Loading the
        // second frame or normals must not invalidate the first borrowed output.
        constexpr int vertices = 240000;
        stMeshModel other(0, 1), mesh(0, vertices);
        other.InitializeVertexFrames(1);
        other.GetVertexLocations(0, true, 0.0f);
        mesh.InitializeVertexFrames(2);
        mesh.GetVertex(0)[0] = 2;
        mesh.GetVertex(1)[0] = 6;
        auto* positions = mesh.GetVertexLocations(0, true, 0.0f);
        auto* lerp = mesh.GetVertexLocations(0, true, 0.5f);
        REQUIRE(lerp[0].x == 4.0f);
        auto* normals = mesh.GetVertexNormals(0, true);
        REQUIRE(normals != nullptr);
        REQUIRE((mesh.GetVertexLocations(0, true, 0.0f) == positions && positions[0].x == 2.0f));
        REQUIRE((mesh.GetVertexLocations(0, true, 0.5f) == lerp && lerp[0].x == 4.0f));
        REQUIRE(!other.frames[0].locations);
        REQUIRE(g_decompressed_mesh_bytes == initial_bytes + 3 * vertices * sizeof(glm::vec3));
        REQUIRE(mesh.ReleaseDecompressedFrames() == 3 * vertices * sizeof(glm::vec3));
        REQUIRE(mesh.lerp_buffer.data() == lerp);
    }
    REQUIRE(g_decompressed_mesh_bytes == initial_bytes);
    return true;
}

static bool part_ownership()
{
    const auto registered = g_mesh_models.size();
    const int initial_bytes = g_decompressed_mesh_bytes;
    {
        stMeshModel original(0, 1);
        original.InitializeVertexFrames(1);
        original.GetVertex(0)[0] = 3;
        original.GetVertexLocations(0, true, 0.0f);
        original.AddPart(std::make_unique<stMeshModel>(0, 1));
        original.parts[0]->InitializeVertexFrames(1);
        original.parts[0]->GetVertexLocations(0, true, 0.0f);
        REQUIRE((original.head == nullptr && original.parts[0]->head == &original));
        REQUIRE(
            (original.Next() == original.parts[0].get() && original.parts[0]->Next() == nullptr));
        REQUIRE(g_mesh_models.size() == registered + 2);
        REQUIRE(g_decompressed_mesh_bytes == initial_bytes + 2 * sizeof(glm::vec3));
    }
    REQUIRE(g_mesh_models.size() == registered);
    REQUIRE(g_decompressed_mesh_bytes == initial_bytes);
    return true;
}

// Different materials sharing a shader take the per-vertex table path;
// another shader adds a uniquely owned successor to the shared mesh head.
static bool loaded_model_ownership()
{
    std::vector<W8MeshMaterial> materials(3);
    for (auto& material : materials) {
        material.material = std::make_shared<stMaterial>();
        material.render_flags.value = 0x251b;
    }
    materials[2].render_flags.value = 0x2517;
    W8ReadMeshFace faces[3]{};
    for (int face = 0; face < 3; ++face) {
        faces[face].material_index = face;
        for (int corner = 0; corner < 3; ++corner) {
            faces[face].vertices[corner] = face * 3 + corner;
        }
    }
    std::vector<short> mapped_values, mapped_keys;
    unsigned int mesh_count = 0;
    std::vector<std::vector<int>> maps;
    auto model = BuildSingleLevelMesh(3, faces, 9, materials, &mesh_count, maps, &mapped_values,
                                      &mapped_keys);
    REQUIRE((model && mesh_count == 2 && maps.size() == 2));
    REQUIRE((model->parts.size() == 1 && model->parts[0]->head == model.get()));
    REQUIRE(model->getVertexMaterial(0, srMeshModel::SIDE_FRONT, 0)[0] == materials[0].material);
    REQUIRE(model->parts[0]->getMaterial(0, srMeshModel::SIDE_FRONT) ==
            materials[2].material.get());
    std::weak_ptr<srModel> lifetime = model;
    auto* first = new srModelInstance(nullptr);
    first->setModel(std::move(model));
    auto* second = new srModelInstance(nullptr);
    *second = *first;
    delete first;
    REQUIRE(!lifetime.expired());
    delete second;
    REQUIRE(lifetime.expired());
    for (const auto& material : materials) {
        REQUIRE(material.material.use_count() == 1);
    }
    return true;
}

class TestTexture : public srTexture {
public:
    inline static int destroyed = 0;
    void invalidate() override {}
    void setupDefaultValues() override {}
    ~TestTexture() override { ++destroyed; }
};

static bool skins_and_automap()
{
    const int destroyed = TestTexture::destroyed;
    {
        std::shared_ptr<srTextureIFace> blank = std::make_shared<TestTexture>();
        std::shared_ptr<srTextureIFace> visible = std::make_shared<TestTexture>();
        blank->setName("bLaNk-body");
        visible->setName(std::string(600, 'V')); // No fixed-size texture-name copies.

        {
            stMeshModel mesh(3, 1);
            auto* base = mesh.getPolyTexture(0, 0, 1);
            base[0] = blank;
            base[1] = visible;
            std::string cycle(400, 'c');
            std::string first_name = cycle + '0';
            const int first = mesh.CreateSkinTable(first_name, -1);
            REQUIRE(first == 0);
            first_name[0] = 'x';
            REQUIRE(mesh.skins[0].name == cycle + '0'); // Owned, not the caller's mutable name.
            REQUIRE(mesh.CreateSkinTable(cycle + '0', -1) == -1);
            const int second = mesh.CreateSkinTable("second1", first);
            auto* second_textures = mesh.GetTextureTable(second);
            REQUIRE((second == 1 && second_textures[0] == blank));
            const int third = mesh.CreateSkinTable("third2", second);
            REQUIRE(!mesh.GetActivePolygons(second, false));
            auto selection = mesh.GetActivePolygons(second, true);
            REQUIRE((selection && selection->size() == 2 && (*selection)[0] == 1 &&
                     (*selection)[1] == 2));
            REQUIRE(mesh.GetActivePolygons(second, true)->data() == selection->data());
            mesh.RemoveSkinTable(0);
            REQUIRE(mesh.FindSkinTable("SECOND1") ==
                    second); // ID is no longer its vector position.
            REQUIRE(mesh.GetTextureTable(second) == second_textures);
            REQUIRE(mesh.GetActivePolygons(second, true)->data() == selection->data());
            const std::array<char, 9> bounded_name{'t', 'h', 'i', 'r', 'd', '2', 'X', 'X', 'X'};
            REQUIRE(mesh.FindSkinTable(std::string_view(bounded_name.data(), 6)) == third);
            REQUIRE(mesh.CreateSkinTable("reused3", -1) == 0);
            REQUIRE(mesh.GetTextureTable(second) ==
                    second_textures); // Vector growth does not move tables.
            REQUIRE(mesh.CreateSkinTable("", -1) == 3);
            REQUIRE(mesh.CreateSkinTable(cycle + '0', -1) == 4);
            REQUIRE(mesh.CreateSkinTable(cycle + '1', -1) == 5);
            mesh.RemoveSkinTablesForCycle(cycle);
            REQUIRE(
                (mesh.FindSkinTable(cycle + '0') == -1 && mesh.FindSkinTable(cycle + '1') == -1));
            REQUIRE(mesh.FindSkinTable("") ==
                    3); // Empty names must not underflow during cycle removal.
            REQUIRE(mesh.FindSkinTable("SECOND1") == second);

            const std::array<std::string_view, 1> excluded{visible->getName()};
            mesh.ApplyAutomapPolygonFilter(excluded);
            selection = mesh.GetActivePolygons(-1, false);
            REQUIRE((selection && selection->size() == 2 && (*selection)[0] == 0 &&
                     (*selection)[1] == 2));
            mesh.ApplyAutomapPolygonFilter({});
            REQUIRE(!mesh.GetActivePolygons(-1, true)); // All selected.
            mesh.ClearAutomapPolygonFilter();
            REQUIRE(!mesh.GetActivePolygons(-1, false));
            selection = mesh.GetActivePolygons(-1, true);
            REQUIRE((selection && selection->size() == 2 && (*selection)[0] == 1 &&
                     (*selection)[1] == 2));
            base[1] = blank;
            base[2] = blank;
            const std::array<std::string_view, 1> exclude_all{"BLANK-BODY"};
            mesh.ApplyAutomapPolygonFilter(exclude_all);
            selection = mesh.GetActivePolygons(-1, true);
            REQUIRE((selection &&
                     selection->empty())); // None selected is NOT the unfiltered null case.
            REQUIRE((mesh.automap_polygons && mesh.automap_polygons->empty()));
            mesh.ClearAutomapPolygonFilter();
            const auto empty_automap = mesh.GetActivePolygons(-1, true);
            REQUIRE(empty_automap);
            REQUIRE(empty_automap->empty());
            const int all_blank = mesh.CreateSkinTable("all-blank", -1);
            const auto empty_skin = mesh.GetActivePolygons(all_blank, true);
            REQUIRE(empty_skin);
            REQUIRE(empty_skin->empty());
            mesh.SetMappedVertex(7, 10);
            mesh.SetMappedVertex(9, 10);
            REQUIRE((mesh.FindMappedIndex(10) == 9 && mesh.mapped_vertices.size() == 1));
            REQUIRE((mesh.FindMappedIndex(-1) == -1 && mesh.FindMappedIndex(11) == -1));
        }
        REQUIRE(blank.use_count() == 1);
    }
    REQUIRE(TestTexture::destroyed == destroyed + 2);
    return true;
}

TEST_CASE_METHOD(SurRenderTestFixture, "Mesh frame storage", "[mesh][ownership]")
{
    REQUIRE(frame_storage());
    REQUIRE(g_mesh_models.empty());
    REQUIRE(g_decompressed_mesh_bytes == 0);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Writable mesh cache", "[mesh][ownership]")
{
    REQUIRE(writable_and_empty_caches());
    REQUIRE(g_mesh_models.empty());
    REQUIRE(g_decompressed_mesh_bytes == 0);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Mesh eviction and borrowed outputs", "[mesh][ownership]")
{
    REQUIRE(eviction_and_borrowed_outputs());
    REQUIRE(g_mesh_models.empty());
    REQUIRE(g_decompressed_mesh_bytes == 0);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Mesh part ownership", "[mesh][ownership]")
{
    REQUIRE(part_ownership());
    REQUIRE(g_mesh_models.empty());
    REQUIRE(g_decompressed_mesh_bytes == 0);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Loaded model ownership", "[mesh][ownership]")
{
    REQUIRE(loaded_model_ownership());
    REQUIRE(g_mesh_models.empty());
    REQUIRE(g_decompressed_mesh_bytes == 0);
}

TEST_CASE_METHOD(SurRenderTestFixture, "Mesh skins and automap", "[mesh][ownership]")
{
    REQUIRE(skins_and_automap());
    REQUIRE(g_mesh_models.empty());
    REQUIRE(g_decompressed_mesh_bytes == 0);
}
