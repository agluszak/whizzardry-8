#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/materials.h"
#include "surrender/srModelInstance.h"
#include "wiz8/sr_api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#define CHECK(expression)                                                                         \
    do {                                                                                          \
        if (!(expression)) {                                                                      \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                           \
            return false;                                                                         \
        }                                                                                         \
    } while (0)

static_assert(!std::is_copy_constructible_v<stMeshModel>);
static_assert(std::is_copy_assignable_v<stMeshModel>); // Registry clone assigns a fresh instance.
static_assert(std::is_nothrow_move_constructible_v<stMeshModel::Frame>);
static_assert(std::is_nothrow_move_constructible_v<stMeshModel::Skin>);

static bool near(float left, float right)
{
    return std::abs(left - right) < 1e-6f;
}

static bool frame_storage()
{
    const auto registered = g_mesh_models.size();
    const int initial_bytes = g_decompressed_mesh_bytes;
    {
        stMeshModel mesh(1, 3);
        CHECK(mesh.frames.empty());
        CHECK(mesh.GetVertex(0) == nullptr);
        CHECK(mesh.GetVertexLocations(0, true, 0.5f) == nullptr);
        CHECK(mesh.GetVertexNormals(0, true) == nullptr);
        CHECK(mesh.GetPolygonNormals(0, true) == nullptr);
        mesh.InitializeVertexFrames(2);
        mesh.vertex_compression_scale = 0.5f;
        CHECK(mesh.frames.size() == 2);
        CHECK(g_mesh_models.size() == registered + 1);
        CHECK(g_decompressed_mesh_bytes == initial_bytes);
        for (const auto& frame : mesh.frames) {
            CHECK(frame.compressed_locations.size() == 9);
            CHECK(frame.compressed_vertex_normals.size() == 9);
            CHECK(frame.compressed_polygon_normals.size() == 3);
            CHECK(std::all_of(frame.compressed_locations.begin(), frame.compressed_locations.end(),
                              [](short value) { return value == 0; }));
            CHECK(!frame.locations && !frame.vertex_normals && !frame.polygon_normals);
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
        CHECK(mesh.GetVertex(0) == compressed);
        CHECK(std::equal(first.begin(), first.end(), compressed));
        CHECK(g_mesh_models.size() == registered + 1);
        std::copy(second.begin(), second.end(), mesh.GetVertex(1));
        mesh.FinalizeVertexFrame(0);
        mesh.FinalizeVertexFrame(1);
        const std::vector<unsigned char> vertex_normals{0, 0, 127, 0, 0, 127, 0, 0, 127};
        CHECK(mesh.frames[0].compressed_vertex_normals == vertex_normals);
        CHECK(mesh.frames[0].compressed_polygon_normals == std::vector<unsigned char>({0, 0, 127}));
        CHECK(!mesh.frames[0].locations);
        CHECK(g_decompressed_mesh_bytes == initial_bytes); // Normal-builder scratch is not a cache.

        srVector3T<float> minimum, maximum;
        mesh.GetFrameBounds(1, &minimum, &maximum);
        CHECK(minimum == srVector3T<float>(2.0f, 0.0f, 0.0f));
        CHECK(maximum == srVector3T<float>(3.0f, 1.0f, 0.0f));
        CHECK(!mesh.frames[1].locations);
        CHECK(g_decompressed_mesh_bytes == initial_bytes); // Bounds must not make a frame resident.
        auto* positions = mesh.GetVertexLocations(0, true, 0.0f);
        CHECK(positions[1] == srVector3T<float>(1.0f, 0.0f, 0.0f));
        CHECK(g_decompressed_mesh_bytes == initial_bytes + 3 * sizeof(srVector3T<float>));
        CHECK(mesh.GetVertexLocations(0, true, 0.0f) == positions);
        auto* normals = mesh.GetVertexNormals(0, true);
        auto* polygon_normals = mesh.GetPolygonNormals(0, true);
        CHECK(normals[2] == srVector3T<float>(0.0f, 0.0f, 1.0f));
        CHECK(polygon_normals[0] == normals[2]);
        CHECK(mesh.GetVertexLocations(0, true, 0.0f) == positions);
        auto* shade_indices = mesh.getVertexShadeIndex(1);
        shade_indices[0] = shade_indices[1] = shade_indices[2] = 0;
        mesh.ComputeFrameNormals(0);
        CHECK(mesh.frames[0].compressed_vertex_normals == vertex_normals);
        CHECK(mesh.GetVertexNormals(0, true) == normals);
        CHECK(mesh.GetPolygonNormals(0, true) == polygon_normals);
        auto* lerp = mesh.GetVertexLocations(0, true, 0.25f);
        CHECK(near(lerp[0].x, 0.5f) && near(lerp[1].x, 1.5f));
        CHECK(mesh.GetVertexLocations(0, true, 1.0f) == lerp);
        CHECK(lerp[0] == srVector3T<float>(2.0f, 0.0f, 0.0f));
        CHECK(mesh.GetVertexLocations(1, true, 0.75f) == mesh.frames[1].locations->data());
        mesh.GetFrameBounds(0, &minimum, &maximum);
        CHECK(minimum == srVector3T<float>(0.0f, 0.0f, 0.0f));
        CHECK(maximum == srVector3T<float>(1.0f, 1.0f, 0.0f));
        mesh.GetFrameBounds(-1, &minimum, &maximum);
        CHECK(minimum == srVector3T<float>(0.0f, 0.0f, 0.0f) && minimum == maximum);
        const int cache_bytes = (3 + 3 + 3 + 1) * sizeof(srVector3T<float>);
        CHECK(g_decompressed_mesh_bytes == initial_bytes + cache_bytes);
        CHECK(mesh.ReleaseDecompressedFrames() == cache_bytes);
        CHECK(mesh.ReleaseDecompressedFrames() == 0);
        CHECK(g_decompressed_mesh_bytes == initial_bytes);
        CHECK(!mesh.frames[0].locations && !mesh.frames[0].vertex_normals);
        CHECK(!mesh.frames[0].polygon_normals && !mesh.frames[1].locations);
        CHECK(mesh.GetVertex(0) == compressed);
        CHECK(std::equal(first.begin(), first.end(), compressed));
        CHECK(lerp == mesh.lerp_buffer.data()); // Explicit cache eviction does not evict interpolation.
        CHECK(mesh.GetVertexLocations(0, true, 0.25f) == lerp);
        CHECK(near(lerp[0].x, 0.5f));
        CHECK(mesh.GetVertex(2) == nullptr);
        CHECK(mesh.GetVertexLocations(2, true, 0.0f) == nullptr);
        CHECK(mesh.GetVertexNormals(2, true) == nullptr);
        CHECK(mesh.GetPolygonNormals(2, true) == nullptr);
        CHECK(!mesh.AllocateFrameBuffers(2, W8_MESH_FRAME_LOCATIONS));
        CHECK(!mesh.DecompressFrame(-1, W8_MESH_FRAME_LOCATIONS, positions));
    }
    CHECK(g_mesh_models.size() == registered);
    CHECK(g_decompressed_mesh_bytes == initial_bytes);
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
        locations[0] = srVector3T<float>(7.0f, 8.0f, 9.0f);
        CHECK(mesh.GetVertexLocations(0, true, 0.0f) == locations);
        CHECK(locations[0] == srVector3T<float>(7.0f, 8.0f, 9.0f)); // Do not overwrite active data.
        CHECK(mesh.ReleaseDecompressedFrames() == sizeof(srVector3T<float>));
        locations = mesh.GetVertexLocations(0, true, 0.0f);
        CHECK(locations[0] == srVector3T<float>(-8192.0f, 8191.75f, -0.25f));
        mesh.frames[0].compressed_vertex_normals = {128, 255, 127};
        auto* normals = mesh.GetVertexNormals(0, true);
        CHECK(normals[0].x == -1.0f && near(normals[0].y, -1.0f / 127.0f) && normals[0].z == 1.0f);
        CHECK(mesh.GetPolygonNormals(0, true) == nullptr);
        CHECK(mesh.frames[0].polygon_normals.has_value()); // Active zero-sized cache, not absent.
        CHECK(mesh.ReleaseDecompressedFrames() == 2 * sizeof(srVector3T<float>));
        CHECK(!mesh.frames[0].polygon_normals);
    }
    {
        stMeshModel empty(0, 0);
        empty.InitializeVertexFrames(1);
        CHECK(empty.AllocateFrameBuffers(0, 7));
        CHECK(empty.frames[0].locations && empty.frames[0].locations->empty());
        CHECK(empty.frames[0].vertex_normals && empty.frames[0].polygon_normals);
        CHECK(g_decompressed_mesh_bytes == initial_bytes);
        CHECK(!ReclaimDecompressedBytes(1)); // No infinite loop on resident empty caches.
        CHECK(empty.ReleaseDecompressedFrames() == 0);
        CHECK(!empty.frames[0].locations);
    }
    CHECK(g_decompressed_mesh_bytes == initial_bytes);
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
        CHECK(ReclaimDecompressedBytes(sizeof(srVector3T<float>)));
        CHECK(!old_mesh.frames[0].locations && recent_mesh.frames[0].locations);
        CHECK(!ReclaimDecompressedBytes(1, &recent_mesh));
        CHECK(recent_mesh.frames[0].locations);
        CHECK(!ReclaimDecompressedBytes(1000)); // Drains the remainder, then terminates.
        CHECK(!recent_mesh.frames[0].locations);
        CHECK(g_decompressed_mesh_bytes == initial_bytes);
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
        CHECK(lerp[0].x == 4.0f);
        auto* normals = mesh.GetVertexNormals(0, true);
        CHECK(normals != nullptr);
        CHECK(mesh.GetVertexLocations(0, true, 0.0f) == positions && positions[0].x == 2.0f);
        CHECK(mesh.GetVertexLocations(0, true, 0.5f) == lerp && lerp[0].x == 4.0f);
        CHECK(!other.frames[0].locations);
        CHECK(g_decompressed_mesh_bytes == initial_bytes + 3 * vertices * sizeof(srVector3T<float>));
        CHECK(mesh.ReleaseDecompressedFrames() == 3 * vertices * sizeof(srVector3T<float>));
        CHECK(mesh.lerp_buffer.data() == lerp);
    }
    CHECK(g_decompressed_mesh_bytes == initial_bytes);
    return true;
}

static bool cloned_ownership()
{
    const auto registered = g_mesh_models.size();
    const int initial_bytes = g_decompressed_mesh_bytes;
    {
        stMeshModel original(0, 1);
        original.InitializeVertexFrames(1);
        original.GetVertex(0)[0] = 3;
        original.GetVertexLocations(0, true, 0.0f);
        original.LinkTo(std::make_unique<stMeshModel>(0, 1));
        original.next->InitializeVertexFrames(1);
        original.next->GetVertexLocations(0, true, 0.0f);
        auto clone = std::make_unique<stMeshModel>(0, 0);
        *clone = original;
        CHECK(clone->next.get() != original.next.get() && clone->next->previous == clone.get());
        CHECK(clone->previous == nullptr);
        CHECK(clone->GetVertex(0) != original.GetVertex(0));
        CHECK(clone->GetVertexLocations(0, true, 0.0f) != original.GetVertexLocations(0, true, 0.0f));
        clone->GetVertex(0)[0] = 9;
        clone->GetVertexLocations(0, false, 0.0f)[0].x = 11.0f;
        CHECK(original.GetVertex(0)[0] == 3);
        CHECK(original.GetVertexLocations(0, true, 0.0f)[0].x == 3.0f);
        CHECK(g_mesh_models.size() == registered + 4);
        CHECK(g_decompressed_mesh_bytes == initial_bytes + 4 * sizeof(srVector3T<float>));
        CHECK(clone->ReleaseDecompressedFrames() == sizeof(srVector3T<float>));
        CHECK(original.frames[0].locations);
        *clone = static_cast<const stMeshModel&>(*clone);
        CHECK(g_mesh_models.size() == registered + 4);
        stMeshModel empty(0, 0);
        *clone = empty;
        CHECK(clone->frames.empty() && clone->next == nullptr);
        CHECK(g_mesh_models.size() == registered + 2);
        CHECK(g_decompressed_mesh_bytes == initial_bytes + 2 * sizeof(srVector3T<float>));
    }
    CHECK(g_mesh_models.size() == registered);
    CHECK(g_decompressed_mesh_bytes == initial_bytes);
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
    W8GrowableVector<short> mapped_values, mapped_keys;
    unsigned int mesh_count = 0;
    std::vector<std::vector<int>> maps;
    auto model = BuildSingleLevelMesh(3, faces, 9, materials, &mesh_count, maps, &mapped_values,
                                     &mapped_keys);
    CHECK(model && mesh_count == 2 && maps.size() == 2);
    CHECK(model->next && model->next->previous == model.get());
    CHECK(model->getVertexMaterial(0, srMeshModel::SIDE_FRONT, 0)[0] == materials[0].material);
    CHECK(model->next->getMaterial(0, srMeshModel::SIDE_FRONT) == materials[2].material.get());
    std::weak_ptr<srModel> lifetime = model;
    auto* first = new srModelInstance(nullptr);
    first->setModel(std::move(model));
    auto* second = new srModelInstance(nullptr);
    *second = *first;
    delete first;
    CHECK(!lifetime.expired());
    delete second;
    CHECK(lifetime.expired());
    for (const auto& material : materials) {
        CHECK(material.material.use_count() == 1);
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
            CHECK(first == 0);
            first_name[0] = 'x';
            CHECK(mesh.skins[0].name == cycle + '0'); // Owned, not the caller's mutable name.
            CHECK(mesh.CreateSkinTable(cycle + '0', -1) == -1);
            const int second = mesh.CreateSkinTable("second1", first);
            auto* second_textures = mesh.GetTextureTable(second);
            CHECK(second == 1 && second_textures[0] == blank);
            const int third = mesh.CreateSkinTable("third2", second);
            w8_long count = -1;
            CHECK(mesh.GetActivePolygons(&count, second, false) == nullptr && count == 0);
            auto* selection = mesh.GetActivePolygons(&count, second, true);
            CHECK(selection != nullptr && count == 2 && selection[0] == 1 && selection[1] == 2);
            CHECK(mesh.GetActivePolygons(&count, second, true) == selection);
            mesh.RemoveSkinTable(0);
            CHECK(mesh.FindSkinTable("SECOND1") == second); // ID is no longer its vector position.
            CHECK(mesh.GetTextureTable(second) == second_textures);
            CHECK(mesh.GetActivePolygons(&count, second, true) == selection);
            const std::array<char, 9> bounded_name{'t', 'h', 'i', 'r', 'd', '2', 'X', 'X', 'X'};
            CHECK(mesh.FindSkinTable(std::string_view(bounded_name.data(), 6)) == third);
            CHECK(mesh.CreateSkinTable("reused3", -1) == 0);
            CHECK(mesh.GetTextureTable(second) == second_textures); // Vector growth does not move tables.
            CHECK(mesh.CreateSkinTable("", -1) == 3);
            CHECK(mesh.CreateSkinTable(cycle + '0', -1) == 4);
            CHECK(mesh.CreateSkinTable(cycle + '1', -1) == 5);
            mesh.RemoveSkinTablesForCycle(cycle);
            CHECK(mesh.FindSkinTable(cycle + '0') == -1 && mesh.FindSkinTable(cycle + '1') == -1);
            CHECK(mesh.FindSkinTable("") == 3); // Empty names must not underflow during cycle removal.
            CHECK(mesh.FindSkinTable("SECOND1") == second);
            {
                auto clone = std::make_unique<stMeshModel>(0, 0);
                *clone = mesh;
                CHECK(clone->GetTextureTable(second) != second_textures);
                CHECK(clone->GetTextureTable(second)[0] == second_textures[0]);
                auto* cloned_selection = clone->GetActivePolygons(&count, second, true);
                CHECK(cloned_selection != selection && count == 2);
                clone->GetTextureTable(second)[0] = nullptr;
                CHECK(second_textures[0] == blank);
                clone->RemoveSkinTablesForCycle("second");
                CHECK(clone->FindSkinTable("second1") == -1 && mesh.FindSkinTable("second1") == second);
            }

            const std::array<std::string_view, 1> excluded{visible->getName()};
            mesh.ApplyAutomapPolygonFilter(excluded);
            selection = mesh.GetActivePolygons(&count, -1, false);
            CHECK(selection != nullptr && count == 2 && selection[0] == 0 && selection[1] == 2);
            mesh.ApplyAutomapPolygonFilter({});
            CHECK(mesh.GetActivePolygons(&count, -1, true) == nullptr && count == 0); // All selected.
            mesh.ClearAutomapPolygonFilter();
            CHECK(mesh.GetActivePolygons(&count, -1, false) == nullptr && count == 0);
            selection = mesh.GetActivePolygons(&count, -1, true);
            CHECK(selection != nullptr && count == 2 && selection[0] == 1 && selection[1] == 2);
            base[1] = blank;
            base[2] = blank;
            const std::array<std::string_view, 1> exclude_all{"BLANK-BODY"};
            mesh.ApplyAutomapPolygonFilter(exclude_all);
            selection = mesh.GetActivePolygons(&count, -1, true);
            CHECK(selection != nullptr && count == 0); // None selected is NOT the unfiltered null case.
            CHECK(mesh.automap_polygons && mesh.automap_polygons->empty());
            mesh.ClearAutomapPolygonFilter();
            CHECK(mesh.GetActivePolygons(&count, -1, true) != nullptr && count == 0);
            const int all_blank = mesh.CreateSkinTable("all-blank", -1);
            CHECK(mesh.GetActivePolygons(&count, all_blank, true) != nullptr && count == 0);
            mesh.SetMappedVertex(7, 10);
            mesh.SetMappedVertex(9, 10);
            CHECK(mesh.FindMappedIndex(10) == 9 && mesh.mapped_vertices.size() == 1);
            CHECK(mesh.FindMappedIndex(-1) == -1 && mesh.FindMappedIndex(11) == -1);
        }
        CHECK(blank.use_count() == 1);
    }
    CHECK(TestTexture::destroyed == destroyed + 2);
    return true;
}

int main()
{
    if (!srInit()) {
        return 1;
    }
    const bool passed = frame_storage() && writable_and_empty_caches() &&
                        eviction_and_borrowed_outputs() && cloned_ownership() && loaded_model_ownership() && skins_and_automap();
    const bool drained = g_mesh_models.empty() && g_decompressed_mesh_bytes == 0;
    srExit();
    if (!passed || !drained) {
        return 1;
    }
    std::puts("ok: mesh frame ownership, cache eviction, interpolation, skin names and automap reset");
    return 0;
}
