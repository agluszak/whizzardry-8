#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <type_traits>
#include <vector>

#include "wiz8/retail_text_records.h"
#include "wiz8/asset_paths.h"
#include "wiz8/filesystem.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/xstatus.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/packed_bits.h"
#include "temporary_directory.h"
#include "file_faults.h"
#include "GDFileIO.h"
#include "float_constants.h"
#include "geometry.h"
#include "surrender/srMath.h"


static std::filesystem::path fixture_root;

template<class T> static void append(std::string& bytes, const T& value)
{
    if constexpr (requires { typename wiz8::retail::Record<T>::Disk; }) {
        const auto encoded = wiz8::retail::encode(value);
        bytes.append(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    } else {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    }
}

static void store(const char* path, const std::string& bytes)
{
    std::ofstream file(fixture_root / path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), bytes.size());
    REQUIRE((file.good()));
}

static void databases()
{
    std::filesystem::create_directories(fixture_root / "Data" / "Databases");
    W8ItemDatabaseRecord item{};
    std::string bytes;
    append(bytes, 1u);
    append(bytes, item);
    store("Data/Databases/Items.DBS", bytes);
    REQUIRE((InitializeItemDatabase()));
    std::size_t item_operations;
    { FileFaults trace; REQUIRE((InitializeItemDatabase())); item_operations = trace.operations; }
    auto* records = g_item_records;
    for (std::size_t operation = 1; operation <= item_operations; ++operation) {
        { FileFaults fault(operation); REQUIRE((!InitializeItemDatabase())); REQUIRE((fault.fired)); }
        REQUIRE((g_item_records == records && gXStatus.uiItemsInDatabase == 1));
    }
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("Data/Databases/Items.DBS", bytes.substr(0, end));
        REQUIRE((!InitializeItemDatabase()));
        REQUIRE((g_item_records == records && gXStatus.uiItemsInDatabase == 1));
    }
    std::string excessive;
    append(excessive, UINT32_MAX);
    store("Data/Databases/Items.DBS", excessive);
    REQUIRE((!InitializeItemDatabase() && g_item_records == records));
    DestroyItemDatabase();
    REQUIRE((g_item_records == nullptr && gXStatus.uiItemsInDatabase == 0));

    std::array<char, 256> category{};
    std::strcpy(category.data(), "Weapons");
    W8ItemTableRecord table{};
    std::strcpy(table.name, "Starter");
    bytes.clear();
    append(bytes, 1u);
    append(bytes, category);
    append(bytes, 1u);
    append(bytes, table);
    store("Data/Databases/ItemTables.DBS", bytes);
    REQUIRE((InitializeItemTables()));
    std::size_t table_operations;
    { FileFaults trace; REQUIRE((InitializeItemTables())); table_operations = trace.operations; }
    auto* tables = g_item_tables;
    auto* categories = g_item_table_category_names;
    for (std::size_t operation = 1; operation <= table_operations; ++operation) {
        { FileFaults fault(operation); REQUIRE((!InitializeItemTables())); REQUIRE((fault.fired)); }
        REQUIRE((g_item_tables == tables && g_item_table_category_names == categories));
        REQUIRE((gXStatus.uiItemTablesInDatabase == 1 && gXStatus.uiItemTableCategories == 1));
    }
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("Data/Databases/ItemTables.DBS", bytes.substr(0, end));
        REQUIRE((!InitializeItemTables()));
        REQUIRE((g_item_tables == tables && g_item_table_category_names == categories));
        REQUIRE((gXStatus.uiItemTablesInDatabase == 1 && gXStatus.uiItemTableCategories == 1));
        REQUIRE((!std::strcmp(tables[0].name, "Starter")));
    }
    auto bad = bytes;
    std::memset(bad.data() + 4, 'x', 256);
    store("Data/Databases/ItemTables.DBS", bad);
    REQUIRE((!InitializeItemTables() && g_item_tables == tables));
    bad = bytes;
    std::memset(bad.data() + 264, 'x', 256);
    store("Data/Databases/ItemTables.DBS", bad);
    REQUIRE((!InitializeItemTables() && g_item_tables == tables));
    DestroyItemTables();
    REQUIRE((!g_item_tables && !g_item_table_category_names));
}

static void level_records()
{
    std::string bytes;
    append(bytes, 1); // Uncompressed, no transform.
    append(bytes, 3);
    append(bytes, 1);
    const std::array<srVector3, 3> vertices{srVector3(0, 0, 0), srVector3(1, 0, 0), srVector3(0, 1, 0)};
    append(bytes, vertices);
    W8SerializedMeshFace face{};
    face.vertices[0] = 0; face.vertices[1] = 1; face.vertices[2] = 2;
    append(bytes, face);
    store("mesh.bin", bytes);
    auto file = wiz8::open_file("mesh.bin");
    W8LevelFileMesh mesh{};
    REQUIRE((ReadMeshFile(file.get(), &mesh)));
    file->seek(0, wiz8::SeekOrigin::begin);
    std::free(mesh.pstVertices); std::free(mesh.pstFaces); mesh = {};
    std::size_t mesh_operations;
    { FileFaults trace; REQUIRE((ReadMeshFile(file.get(), &mesh))); mesh_operations = trace.operations; }
    REQUIRE((mesh.num_vertices == 3 && mesh.num_faces == 1 && mesh.pstVertices[1].x == 1));
    std::free(mesh.pstVertices);
    std::free(mesh.pstFaces);
    for (std::size_t operation = 1; operation <= mesh_operations; ++operation) {
        file = wiz8::open_file("mesh.bin"); mesh = {};
        { FileFaults fault(operation); REQUIRE((!ReadMeshFile(file.get(), &mesh))); REQUIRE((fault.fired)); }
        REQUIRE((!mesh.pstVertices && !mesh.pstFaces && !mesh.lods && !mesh.lod_shorts));
    }

    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("mesh.bin", bytes.substr(0, end));
        file = wiz8::open_file("mesh.bin");
        mesh = {};
        REQUIRE((!ReadMeshFile(file.get(), &mesh)));
        REQUIRE((!mesh.pstVertices && !mesh.pstFaces && !mesh.lods && !mesh.lod_shorts));
    }
    for (int count : {-1, INT32_MAX}) {
        auto bad = bytes;
        std::memcpy(bad.data() + 4, &count, sizeof(count));
        store("mesh.bin", bad);
        file = wiz8::open_file("mesh.bin");
        REQUIRE((!ReadMeshFile(file.get(), &mesh) && !mesh.pstVertices));
    }

    bytes.clear();
    append(bytes, 3); append(bytes, 3); append(bytes, 1);
    append(bytes, static_cast<unsigned char>(7)); // Short LOD vertices, compressed faces.
    append(bytes, srVector3(0, 0, 0));
    append(bytes, 0.0f); append(bytes, srVector3(0, 0, 1));
    append(bytes, srVector3(1, 1, 1));
    append(bytes, static_cast<unsigned char>(2));
    const auto lod_offset = bytes.size();
    append(bytes, static_cast<short>(1));
    const auto scale_offset = bytes.size();
    append(bytes, 1.0f);
    append(bytes, std::array<short, 9>{0, 0, 0, 1, 0, 0, 0, 1, 0});
    std::array<unsigned char, sizeof(W8LevelFileCompressedFace)> compressed{};
    const std::array<unsigned short, 3> indices{0,1,2};
    memcpy(compressed.data(), indices.data(), sizeof(indices));
    const auto face_offset = bytes.size();
    append(bytes, compressed);
    store("mesh.bin", bytes);
    file = wiz8::open_file("mesh.bin");
    mesh = {};
    REQUIRE((ReadMeshFile(file.get(), &mesh)));
    REQUIRE((mesh.num_lods == 1 && mesh.lod_shorts[0][3] == 1 && mesh.pstCompFaces[0].vertex_indices[2] == 2));
    std::free(mesh.lod_shorts[0]); std::free(mesh.lod_shorts); std::free(mesh.pstCompFaces);
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("mesh.bin", bytes.substr(0, end));
        file = wiz8::open_file("mesh.bin");
        mesh = {};
        REQUIRE((!ReadMeshFile(file.get(), &mesh)));
        REQUIRE((!mesh.lod_shorts && !mesh.pstCompFaces));
    }
    for (int malformed = 0; malformed < 3; ++malformed) {
        auto bad = bytes;
        if (malformed == 0) {
            short zero = 0; std::memcpy(bad.data() + lod_offset, &zero, 2);
        } else if (malformed == 1) {
            float invalid = std::numeric_limits<float>::quiet_NaN();
            std::memcpy(bad.data() + scale_offset, &invalid, 4);
        } else {
            unsigned short invalid = 3; std::memcpy(bad.data() + face_offset, &invalid, 2);
        }
        store("mesh.bin", bad);
        file = wiz8::open_file("mesh.bin");
        mesh = {};
        REQUIRE((!ReadMeshFile(file.get(), &mesh)));
        REQUIRE((!mesh.lod_shorts && !mesh.pstCompFaces));
    }

    bytes.clear();
    append(bytes, static_cast<unsigned char>(1));
    append(bytes, static_cast<unsigned char>(2)); // Scaled paths.
    append(bytes, 7);
    append(bytes, 0u);
    append(bytes, 1);
    alignas(4) W8LevelFileScaledPathNode node{};
    node.path.position.x = 42;
    append(bytes, node);
    store("path.bin", bytes);
    file = wiz8::open_file("path.bin");
    W8LevelFilePathAI path{};
    REQUIRE((ReadPathAIFile(file.get(), &path)));
    REQUIRE((path.path_count == 1 && path.pScaledPaths[0].path.position.x == 42));
    std::free(path.pScaledPaths);
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("path.bin", bytes.substr(0, end));
        file = wiz8::open_file("path.bin");
        path = {};
        REQUIRE((!ReadPathAIFile(file.get(), &path)));
        REQUIRE((!path.pPaths && !path.pScaledPaths));
    }
}

static std::string geometry_snapshot(W8GameData& data)
{
    auto output = wiz8::open_file("snapshot.bin", wiz8::OpenMode::replace);
    REQUIRE((data.WriteGameData(output.get())));
    output->close();
    auto input = wiz8::open_file("snapshot.bin");
    std::string bytes(input->size(), '\0');
    input->read_exact(bytes.data(), bytes.size());
    return bytes;
}

static void wgd_banks()
{
    auto bank = [](bool secondary, int group = 0) {
        std::string bytes;
        append(bytes, 3); append(bytes, 1);
        append(bytes, std::array<srVector3, 3>{srVector3(2,2,2), srVector3(3,2,2), srVector3(2,2,3)});
        append(bytes, std::array<int, 3>{0,1,2});
        append(bytes, srVector3(0,1,0)); append(bytes, 2);
        append(bytes, 1); append(bytes, 0.5f); append(bytes, 1.0f);
        append(bytes, std::array<char,4>{}); append(bytes, 100); append(bytes, 0);
        if (secondary) { append(bytes, group); std::array<char,64> name{}; strcpy(name.data(), "Door"); append(bytes, name); }
        append(bytes, srVector3(2,2,2)); append(bytes, srVector3(3,3,3));
        return bytes;
    };
    store("primary.wgd", bank(false));
    const auto secondary = bank(true, 7);
    store("secondary.wgd", secondary);
    W8GameData data(nullptr, true);
    auto load = [&](const char* path, int kind) { auto file = wiz8::open_file(path); return data.ReadWGDList(*file, kind); };
    REQUIRE((g_world_scale != 1.0f));
    REQUIRE((load("primary.wgd", 0)));
    REQUIRE((data.minimum.x == 2 * g_world_scale && data.maximum.x == 3 * g_world_scale && data.maximum.y == 3 * g_world_scale));
    REQUIRE((data.m_pVertices[1].x == 3 * g_world_scale && data.m_pVertices[2].z == 3 * g_world_scale));
    REQUIRE((load("secondary.wgd", 1)));
    REQUIRE((data.VerticesCount() == 6 && data.SurfacesCount() == 2));
    REQUIRE((data.FindPointerByName("Door") == 1 && data.StatesCount() == 2));
    std::size_t operations;
    { FileFaults trace; REQUIRE((load("secondary.wgd", 1))); operations = trace.operations; }
    REQUIRE((data.SurfacesCount() == 3 && data.m_pStates[1].poly_count == 2));
    store("unordered.wgd", bank(true, 2));
    REQUIRE((load("unordered.wgd", 1)));
    REQUIRE((data.m_pStates[0].group == 0 && data.m_pStates[1].group == 7 && data.m_pStates[2].group == 2));
    data.SetInterfaceState(1, 7);
    REQUIRE((!(data.m_pSurfaces[1].flags & W8_GD_SURFACE_CONDITIONAL_DISABLED)));
    REQUIRE((data.m_pSurfaces[3].flags & W8_GD_SURFACE_CONDITIONAL_DISABLED));
    const auto snapshot = geometry_snapshot(data);
    const auto* vertices = data.m_pVertices.data();
    const auto* surfaces = data.m_pSurfaces.data();
    const auto* interfaces = data.m_pInterfaces.data();
    const auto* states = data.m_pStates.data();
    const auto* polygons = data.m_piCondPolys.data();
    const auto* environ = g_environ;
    const auto names = data.interface_names;
    auto unchanged = [&] {
        REQUIRE((vertices == data.m_pVertices.data() && surfaces == data.m_pSurfaces.data()));
        REQUIRE((interfaces == data.m_pInterfaces.data() && states == data.m_pStates.data() && polygons == data.m_piCondPolys.data()));
        REQUIRE((names == data.interface_names && environ == g_environ));
        REQUIRE((geometry_snapshot(data) == snapshot));
    };
    for (std::size_t op = 1; op <= operations; ++op) {
        bool rejected = false;
        { FileFaults fault(op); try { load("secondary.wgd", 1); } catch (const std::exception&) { rejected = true; } REQUIRE((fault.fired)); }
        REQUIRE((rejected)); unchanged();
    }
    for (std::size_t end = 0; end < secondary.size(); ++end) {
        store("secondary.wgd", secondary.substr(0, end));
        bool rejected = false;
        try { load("secondary.wgd", 1); } catch (const std::exception&) { rejected = true; }
        REQUIRE((rejected)); unchanged();
    }
    for (int invalid : {0, 1}) {
        auto bad = secondary;
        if (invalid == 0) { int index = 3; memcpy(bad.data() + 44, &index, 4); }
        else memset(bad.data() + 100, 'x', 64);
        store("secondary.wgd", bad);
        bool rejected = false;
        try { load("secondary.wgd", 1); } catch (const std::exception&) { rejected = true; }
        REQUIRE((rejected)); unchanged();
    }
    // Processed serialization preserves the semantic geometry and interface state.
    auto file = wiz8::open_file("snapshot.bin");
    data.ReadProcessedGameData(file.get());
    // Environment motion factors are normalized by the processed loader.
    const auto roundtrip = geometry_snapshot(data);
    const auto geometry_bytes = snapshot.size() - data.EnvironsCount() * sizeof(W8EnvironRecord);
    REQUIRE((roundtrip.substr(0, geometry_bytes) == snapshot.substr(0, geometry_bytes)));
}

static void geometry()
{
    // The fixed processed header is 0x68 bytes; record counts start at 0x1c.
    std::array<unsigned char, 0x68> header{};
    auto word = [&](int offset, int value) { std::memcpy(header.data() + offset, &value, 4); };
    word(0, 1); word(0x1c, 3); word(0x20, 1); word(0x2c, 1); word(0x38, 1);
    std::string bytes;
    append(bytes, header);
    const std::array<srVector3, 3> vertices{srVector3(0, 0, 0), srVector3(1, 0, 0), srVector3(0, 1, 0)};
    append(bytes, vertices);
    W8GDSurface surface{};
    surface.vertex_indices[0] = 0; surface.vertex_indices[1] = 1; surface.vertex_indices[2] = 2;
    surface.plane.normal.Set(0, 0, 1);
    surface.flags = 2; // dominant Z axis for the spatial query
    append(bytes, surface);
    W8GameData data(nullptr, true);
    store("geometry.bin", bytes);
    auto file = wiz8::open_file("geometry.bin");
    data.ReadProcessedGameData(file.get());
    file->seek(0, wiz8::SeekOrigin::begin);
    std::size_t operations;
    { FileFaults trace; data.ReadProcessedGameData(file.get()); operations = trace.operations; }
    auto query = [&] {
        srVector3 start(0.25f, 0.25f, 2), end(0.25f, 0.25f, -2);
        if (!data.geometry_index) REQUIRE((InitializeGameData(&data)));
        auto delta = end - start;
        W8GDSurface** hits = nullptr;
        REQUIRE((data.geometry_index->CollectObjectsAlongSegment(&hits, &start, &delta, 0, 0, 3) == 1));
        REQUIRE((hits[0] == data.m_pSurfaces.data()));
        W8OctreeTrace trace(&start, &end);
        REQUIRE((data.TestTraceResult(1, nullptr, &trace, 0, 0)));
        REQUIRE((trace.end.z == 0 && data.last_hit_surface == 0));
    };
    for (int reload = 0; reload < 10; ++reload) {
        auto reload_file = wiz8::open_file("geometry.bin");
        data.ReadProcessedGameData(reload_file.get());
        query();
    }
    SetBit(data.pending_trigger_bits, 0); SetBit(data.active_trigger_bits, 0);
    const auto* old_index = data.geometry_index.get();
    const auto pending_bits = data.pending_trigger_bits;
    const auto active_bits = data.active_trigger_bits;
    auto runtime_unchanged = [&] {
        REQUIRE((data.geometry_index.get() == old_index));
        REQUIRE((data.pending_trigger_bits == pending_bits && data.active_trigger_bits == active_bits));
        REQUIRE((TestBit(data.pending_trigger_bits, 0) && TestBit(data.active_trigger_bits, 0)));
        REQUIRE((data.m_ppTriggers.empty() && data.m_pTrigVertices.empty() && data.m_pTrigSurfaces.empty()));
    };
    auto* old_vertices = data.m_pVertices.data();
    auto* old_surfaces = data.m_pSurfaces.data();
    const auto snapshot = geometry_snapshot(data);
    for (std::size_t op = 1; op <= operations; ++op) {
        file = wiz8::open_file("geometry.bin");
        bool rejected = false;
        { FileFaults fault(op); try { data.ReadProcessedGameData(file.get()); } catch (const std::exception&) { rejected = true; } REQUIRE((fault.fired)); }
        REQUIRE((rejected && data.m_pVertices.data() == old_vertices && data.m_pSurfaces.data() == old_surfaces));
        REQUIRE((geometry_snapshot(data) == snapshot)); runtime_unchanged(); query();
    }
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("geometry.bin", bytes.substr(0, end));
        file = wiz8::open_file("geometry.bin");
        bool rejected = false;
        try { data.ReadProcessedGameData(file.get()); }
        catch (const std::exception&) { rejected = true; }
        REQUIRE((rejected && data.m_pVertices.data() == old_vertices && data.m_pSurfaces.data() == old_surfaces));
        REQUIRE((data.VerticesCount() == 3 && data.SurfacesCount() == 1 && old_vertices[1].x == 1)); runtime_unchanged(); query();
    }
    auto bad = bytes;
    int invalid = 3;
    std::memcpy(bad.data() + header.size() + sizeof(vertices) + offsetof(W8GDSurface, vertex_indices),
                &invalid, 4);
    store("geometry.bin", bad);
    file = wiz8::open_file("geometry.bin");
    bool rejected = false;
    try { data.ReadProcessedGameData(file.get()); }
    catch (const std::exception&) { rejected = true; }
    REQUIRE((rejected && data.m_pVertices.data() == old_vertices && data.m_pSurfaces.data() == old_surfaces));
}

static void malformed_octrees()
{
    // The installed-disc marker bypasses preprocessing so fixtures reach ReadOctFile.
    store("CD.ROM", "");
    W8OctFileHeader header{};
    header.version = W8OctFileHeader::VERSION;
    std::string bytes;
    append(bytes, header); append(bytes, UINT32_MAX);
    for (std::size_t end = 0; end <= bytes.size(); ++end) {
        store("fixture.oct", bytes.substr(0, end));
        W8GameData* data = nullptr;
        W8Octree tree("fixture.oct", &data);
        REQUIRE((tree.HasLoadError() && !data));
    }
    for (int invalid = 0; invalid < 3; ++invalid) {
        header.m_grid_dims.x = header.m_grid_dims.y = header.m_grid_dims.z = 1;
        header.m_depth = invalid == 0 ? 31 : 0;
        header.m_leaf_level = invalid == 1 ? 9 : 0;
        header.m_prop_count = invalid == 2 ? 65536 : 0;
        bytes.clear(); append(bytes, header); append(bytes, UINT32_MAX);
        store("fixture.oct", bytes);
        W8Octree tree("fixture.oct", nullptr);
        REQUIRE((tree.HasLoadError()));
    }
    REQUIRE((std::filesystem::remove(fixture_root / "CD.ROM")));
}

static void path_tables()
{
    W8PathingService path;
    path.path_node_count = 1;
    std::string bytes;
    append(bytes, W8FilePathNode{17, 23});
    append(bytes, W8ConditionalPathHeader{1, 2, 2, 9});
    GDPropCondPaths conditional{};
    std::strcpy(conditional.name, "door");
    append(bytes, conditional);
    append(bytes, std::array<unsigned, 2>{0, 0}); // Frame lookup sentinel.
    append(bytes, std::array<unsigned short, 2>{0, 0});
    append(bytes, std::array<unsigned, 2>{7, 0}); // Node key sentinel.
    append(bytes, std::array<unsigned, 2>{1, 0});
    store("nodes.bin", bytes);
    auto file = wiz8::open_file("nodes.bin");
    REQUIRE((path.ReadPathNodes(file.get())));
    auto* values = path.m_pPathValues;
    auto* paths = path.m_pCondPaths;
    REQUIRE((path.m_ulNumCondPaths == 1 && path.navigation_filter == 9));
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("nodes.bin", bytes.substr(0, end));
        file = wiz8::open_file("nodes.bin");
        REQUIRE((!path.ReadPathNodes(file.get())));
        REQUIRE((path.m_pPathValues == values && path.m_pCondPaths == paths && path.navigation_filter == 9));
    }
    auto bad = bytes;
    std::memset(bad.data() + sizeof(W8FilePathNode) + sizeof(W8ConditionalPathHeader), 'x', sizeof(conditional.name));
    store("nodes.bin", bad);
    file = wiz8::open_file("nodes.bin");
    REQUIRE((!path.ReadPathNodes(file.get()) && path.m_pCondPaths == paths));
    path.path_node_count = -1;
    REQUIRE((!path.ReadPathNodes(file.get()) && path.m_pPathValues == values));
}

struct LoaderFixture
{
    ScopedTemporaryDirectory directory{"wiz8-loader-records"};

    LoaderFixture()
    {
        fixture_root = directory.path / "assets";
        std::filesystem::create_directory(fixture_root);
        std::filesystem::create_directory(directory.path / "user");
        w8_native::configure_paths({wiz8::path_to_utf8(fixture_root),
                                    wiz8::path_to_utf8(directory.path / "user"), {"", "", ""}});
    }

    ~LoaderFixture()
    {
        wiz8::clear_asset_archives();
        fixture_root.clear();
    }
};

TEST_CASE_METHOD(LoaderFixture, "Database record validation", "[loader][records]")
{
    databases();
}

TEST_CASE_METHOD(LoaderFixture, "Level mesh file records", "[loader][records]")
{
    level_records();
}

TEST_CASE_METHOD(LoaderFixture, "Processed geometry", "[loader][records]")
{
    geometry();
}

TEST_CASE_METHOD(LoaderFixture, "WGD geometry banks", "[loader][records]")
{
    wgd_banks();
}

TEST_CASE_METHOD(LoaderFixture, "Path tables", "[loader][records]")
{
    path_tables();
}

TEST_CASE_METHOD(LoaderFixture, "Malformed octree files", "[loader][records]")
{
    malformed_octrees();
}
