#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "GDFileIO.h"
#include "file_faults.h"
#include "float_constants.h"
#include "geometry.h"
#include "glm/ext/vector_float3.hpp"
#include "scoped_asset_roots.h"
#include "surrender/srMath.h"
#include "temporary_directory.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/filesystem.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/world.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/navigation_flags.h"
#include "wiz8/packed_bits.h"
#include "wiz8/retail_geometry_records.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/save_game.h"
#include "wiz8/xstatus.h"

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
    {
        FileFaults trace;
        REQUIRE((InitializeItemDatabase()));
        item_operations = trace.operations;
    }
    auto* records = g_item_records;
    for (std::size_t operation = 1; operation <= item_operations; ++operation) {
        {
            FileFaults fault(operation);
            REQUIRE((!InitializeItemDatabase()));
            REQUIRE((fault.fired));
        }
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
    {
        FileFaults trace;
        REQUIRE((InitializeItemTables()));
        table_operations = trace.operations;
    }
    auto* tables = g_item_tables;
    auto* categories = g_item_table_category_names;
    for (std::size_t operation = 1; operation <= table_operations; ++operation) {
        {
            FileFaults fault(operation);
            REQUIRE((!InitializeItemTables()));
            REQUIRE((fault.fired));
        }
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
    const std::array<srVector3, 3> vertices{srVector3(0, 0, 0), srVector3(1, 0, 0),
                                            srVector3(0, 1, 0)};
    append(bytes, vertices);
    W8SerializedMeshFace face{};
    face.vertices[0] = 0;
    face.vertices[1] = 1;
    face.vertices[2] = 2;
    append(bytes, face);
    store("mesh.bin", bytes);
    auto file = wiz8::open_file("mesh.bin");
    W8LevelFileMesh mesh{};
    REQUIRE((ReadMeshFile(*file, mesh)));
    file->seek(0, wiz8::SeekOrigin::begin);
    mesh = {};
    std::size_t mesh_operations;
    {
        FileFaults trace;
        REQUIRE((ReadMeshFile(*file, mesh)));
        mesh_operations = trace.operations;
    }
    REQUIRE((mesh.num_vertices == 3 && mesh.num_faces == 1 && mesh.vertices[1].x == 1));
    for (std::size_t operation = 1; operation <= mesh_operations; ++operation) {
        file = wiz8::open_file("mesh.bin");
        mesh = {};
        {
            FileFaults fault(operation);
            REQUIRE((!ReadMeshFile(*file, mesh)));
            REQUIRE((fault.fired));
        }
        REQUIRE((mesh.vertices.empty() && mesh.faces.empty() && mesh.lods.empty() &&
                 mesh.lod_shorts.empty()));
    }

    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("mesh.bin", bytes.substr(0, end));
        file = wiz8::open_file("mesh.bin");
        mesh = {};
        REQUIRE((!ReadMeshFile(*file, mesh)));
        REQUIRE((mesh.vertices.empty() && mesh.faces.empty() && mesh.lods.empty() &&
                 mesh.lod_shorts.empty()));
    }
    for (int count : {-1, INT32_MAX}) {
        auto bad = bytes;
        std::memcpy(bad.data() + 4, &count, sizeof(count));
        store("mesh.bin", bad);
        file = wiz8::open_file("mesh.bin");
        REQUIRE((!ReadMeshFile(*file, mesh) && mesh.vertices.empty()));
    }

    bytes.clear();
    append(bytes, 3);
    append(bytes, 3);
    append(bytes, 1);
    append(bytes, static_cast<unsigned char>(7)); // Short LOD vertices, compressed faces.
    append(bytes, srVector3(0, 0, 0));
    append(bytes, 0.0f);
    append(bytes, srVector3(0, 0, 1));
    append(bytes, srVector3(1, 1, 1));
    append(bytes, static_cast<unsigned char>(2));
    const auto lod_offset = bytes.size();
    append(bytes, static_cast<short>(1));
    const auto scale_offset = bytes.size();
    append(bytes, 1.0f);
    append(bytes, std::array<short, 9>{0, 0, 0, 1, 0, 0, 0, 1, 0});
    std::array<unsigned char, sizeof(W8LevelFileCompressedFace)> compressed{};
    const std::array<unsigned short, 3> indices{0, 1, 2};
    memcpy(compressed.data(), indices.data(), sizeof(indices));
    const auto face_offset = bytes.size();
    append(bytes, compressed);
    store("mesh.bin", bytes);
    file = wiz8::open_file("mesh.bin");
    mesh = {};
    REQUIRE((ReadMeshFile(*file, mesh)));
    REQUIRE((mesh.LodCount() == 1 && mesh.lod_shorts[0][3] == 1 &&
             mesh.compressed_faces[0].vertex_indices[2] == 2));
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("mesh.bin", bytes.substr(0, end));
        file = wiz8::open_file("mesh.bin");
        mesh = {};
        REQUIRE((!ReadMeshFile(*file, mesh)));
        REQUIRE((mesh.lod_shorts.empty() && mesh.compressed_faces.empty()));
    }
    for (int malformed = 0; malformed < 3; ++malformed) {
        auto bad = bytes;
        if (malformed == 0) {
            short zero = 0;
            std::memcpy(bad.data() + lod_offset, &zero, 2);
        } else if (malformed == 1) {
            float invalid = std::numeric_limits<float>::quiet_NaN();
            std::memcpy(bad.data() + scale_offset, &invalid, 4);
        } else {
            unsigned short invalid = 3;
            std::memcpy(bad.data() + face_offset, &invalid, 2);
        }
        store("mesh.bin", bad);
        file = wiz8::open_file("mesh.bin");
        mesh = {};
        REQUIRE((!ReadMeshFile(*file, mesh)));
        REQUIRE((mesh.lod_shorts.empty() && mesh.compressed_faces.empty()));
    }

    bytes.clear();
    append(bytes, static_cast<unsigned char>(1));
    append(bytes, static_cast<unsigned char>(2)); // Scaled paths.
    append(bytes, 7);
    append(bytes, 0u);
    append(bytes, 1);
    // Ten disk floats, independent of the runtime keyframe/vector layout.
    append(bytes, std::array<float, 10>{42, 2, 3, 0.5f, 0, 1, 0, 2, 3, 4});
    store("path.bin", bytes);
    file = wiz8::open_file("path.bin");
    W8LevelFilePathAI path{};
    REQUIRE((ReadPathAIFile(*file, path)));
    REQUIRE((path.PathCount() == 1 && path.scaled_paths[0].path.position.x == 42));
    auto encoded = wiz8::open_file("path-output.bin", wiz8::OpenMode::replace);
    REQUIRE(WritePathAIFile(*encoded, path));
    encoded->close();
    encoded = wiz8::open_file("path-output.bin");
    REQUIRE(encoded->size() == static_cast<std::int64_t>(bytes.size()));
    std::string roundtrip(bytes.size(), '\0');
    encoded->read_exact(roundtrip.data(), roundtrip.size());
    REQUIRE(roundtrip == bytes);
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("path.bin", bytes.substr(0, end));
        file = wiz8::open_file("path.bin");
        path = {};
        REQUIRE((!ReadPathAIFile(*file, path)));
        REQUIRE((path.paths.empty() && path.scaled_paths.empty()));
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
        append(bytes, 3);
        append(bytes, 1);
        append(bytes, std::array<srVector3, 3>{srVector3(2, 2, 2), srVector3(3, 2, 2),
                                               srVector3(2, 2, 3)});
        append(bytes, std::array<int, 3>{0, 1, 2});
        append(bytes, srVector3(0, 1, 0));
        append(bytes, 2);
        append(bytes, 1);
        append(bytes, 0.5f);
        append(bytes, 1.0f);
        append(bytes, std::array<char, 4>{});
        append(bytes, 100);
        append(bytes, 0);
        if (secondary) {
            append(bytes, group);
            std::array<char, 64> name{};
            strcpy(name.data(), "Door");
            append(bytes, name);
        }
        append(bytes, srVector3(2, 2, 2));
        append(bytes, srVector3(3, 3, 3));
        return bytes;
    };
    store("primary.wgd", bank(false));
    const auto secondary = bank(true, 7);
    store("secondary.wgd", secondary);
    W8GameData data(nullptr, true);
    auto load = [&](const char* path, int kind) {
        auto file = wiz8::open_file(path);
        return data.ReadWGDList(*file, kind);
    };
    REQUIRE((g_world_scale != 1.0f));
    REQUIRE((load("primary.wgd", 0)));
    REQUIRE((data.minimum.x == 2 * g_world_scale && data.maximum.x == 3 * g_world_scale &&
             data.maximum.y == 3 * g_world_scale));
    REQUIRE(
        (data.m_pVertices[1].x == 3 * g_world_scale && data.m_pVertices[2].z == 3 * g_world_scale));
    REQUIRE((load("secondary.wgd", 1)));
    REQUIRE((data.VerticesCount() == 6 && data.SurfacesCount() == 2));
    REQUIRE((data.FindPointerByName("Door") == 1 && data.StatesCount() == 2));
    std::size_t operations;
    {
        FileFaults trace;
        REQUIRE((load("secondary.wgd", 1)));
        operations = trace.operations;
    }
    REQUIRE((data.SurfacesCount() == 3 && data.m_pStates[1].poly_count == 2));
    store("unordered.wgd", bank(true, 2));
    REQUIRE((load("unordered.wgd", 1)));
    REQUIRE((data.m_pStates[0].group == 0 && data.m_pStates[1].group == 7 &&
             data.m_pStates[2].group == 2));
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
        REQUIRE((interfaces == data.m_pInterfaces.data() && states == data.m_pStates.data() &&
                 polygons == data.m_piCondPolys.data()));
        REQUIRE((names == data.interface_names && environ == g_environ));
        REQUIRE((geometry_snapshot(data) == snapshot));
    };
    for (std::size_t op = 1; op <= operations; ++op) {
        bool rejected = false;
        {
            FileFaults fault(op);
            try {
                load("secondary.wgd", 1);
            } catch (const std::exception&) {
                rejected = true;
            }
            REQUIRE((fault.fired));
        }
        REQUIRE((rejected));
        unchanged();
    }
    for (std::size_t end = 0; end < secondary.size(); ++end) {
        store("secondary.wgd", secondary.substr(0, end));
        bool rejected = false;
        try {
            load("secondary.wgd", 1);
        } catch (const std::exception&) {
            rejected = true;
        }
        REQUIRE((rejected));
        unchanged();
    }
    for (int invalid : {0, 1}) {
        auto bad = secondary;
        if (invalid == 0) {
            int index = 3;
            memcpy(bad.data() + 44, &index, 4);
        } else
            memset(bad.data() + 100, 'x', 64);
        store("secondary.wgd", bad);
        bool rejected = false;
        try {
            load("secondary.wgd", 1);
        } catch (const std::exception&) {
            rejected = true;
        }
        REQUIRE((rejected));
        unchanged();
    }
    // Processed serialization preserves the semantic geometry and interface state.
    auto file = wiz8::open_file("snapshot.bin");
    data.ReadProcessedGameData(file.get());
    // Environment motion factors are normalized by the processed loader.
    const auto roundtrip = geometry_snapshot(data);
    const auto geometry_bytes =
        snapshot.size() - data.EnvironsCount() * sizeof(wiz8::retail::Environ);
    REQUIRE((roundtrip.substr(0, geometry_bytes) == snapshot.substr(0, geometry_bytes)));
}

static void geometry()
{
    // The fixed processed header is 0x68 bytes; record counts start at 0x1c.
    std::array<unsigned char, 0x68> header{};
    auto word = [&](int offset, int value) { std::memcpy(header.data() + offset, &value, 4); };
    word(0, 1);
    word(0x1c, 3);
    word(0x20, 1);
    word(0x2c, 1);
    word(0x38, 1);
    std::string bytes;
    append(bytes, header);
    const std::array<srVector3, 3> vertices{srVector3(0, 0, 0), srVector3(1, 0, 0),
                                            srVector3(0, 1, 0)};
    append(bytes, vertices);
    wiz8::retail::GDSurface surface{};
    surface.vertex_indices[0] = 0;
    surface.vertex_indices[1] = 1;
    surface.vertex_indices[2] = 2;
    surface.plane.normal = {0, 0, 1};
    surface.flags = 2; // dominant Z axis for the spatial query
    append(bytes, surface);
    W8GameData data(nullptr, true);
    store("geometry.bin", bytes);
    auto file = wiz8::open_file("geometry.bin");
    data.ReadProcessedGameData(file.get());
    file->seek(0, wiz8::SeekOrigin::begin);
    std::size_t operations;
    {
        FileFaults trace;
        data.ReadProcessedGameData(file.get());
        operations = trace.operations;
    }
    auto query = [&] {
        srVector3 start(0.25f, 0.25f, 2), end(0.25f, 0.25f, -2);
        if (!data.geometry_index) REQUIRE((InitializeGameData(&data)));
        auto delta = end - start;
        const auto hits =
            data.geometry_index->queryAlongSegment(start.glmValue(), delta.glmValue(), 0, 3);
        REQUIRE(hits.size() == 1);
        REQUIRE((hits[0] == data.m_pSurfaces.data()));
        W8OctreeTrace trace(start.glmValue(), end.glmValue());
        REQUIRE((data.TestTraceResult(1, nullptr, &trace, 0, 0)));
        REQUIRE((trace.end.z == 0 && data.last_hit_surface == 0));
        W8OctreeTrace skin_trace({0.25f, 0.25f, 0.5f}, {0.25f, 0.25f, -2});
        REQUIRE(data.TestTraceResult(1, nullptr, &skin_trace, 0, 0));
        REQUIRE(skin_trace.end.z == 0.5f);
        REQUIRE(skin_trace.hit_limit == 0.5f);
        REQUIRE(skin_trace.length == 0.5f);
    };
    for (int reload = 0; reload < 10; ++reload) {
        auto reload_file = wiz8::open_file("geometry.bin");
        data.ReadProcessedGameData(reload_file.get());
        query();
    }
    SetBit(data.pending_trigger_bits, 0);
    SetBit(data.active_trigger_bits, 0);
    const auto* old_index = data.geometry_index.get();
    const auto pending_bits = data.pending_trigger_bits;
    const auto active_bits = data.active_trigger_bits;
    auto runtime_unchanged = [&] {
        REQUIRE((data.geometry_index.get() == old_index));
        REQUIRE(
            (data.pending_trigger_bits == pending_bits && data.active_trigger_bits == active_bits));
        REQUIRE((TestBit(data.pending_trigger_bits, 0) && TestBit(data.active_trigger_bits, 0)));
        REQUIRE((data.m_ppTriggers.empty() && data.m_pTrigVertices.empty() &&
                 data.m_pTrigSurfaces.empty()));
    };
    auto* old_vertices = data.m_pVertices.data();
    auto* old_surfaces = data.m_pSurfaces.data();
    const auto snapshot = geometry_snapshot(data);
    for (std::size_t op = 1; op <= operations; ++op) {
        file = wiz8::open_file("geometry.bin");
        bool rejected = false;
        {
            FileFaults fault(op);
            try {
                data.ReadProcessedGameData(file.get());
            } catch (const std::exception&) {
                rejected = true;
            }
            REQUIRE((fault.fired));
        }
        REQUIRE((rejected && data.m_pVertices.data() == old_vertices &&
                 data.m_pSurfaces.data() == old_surfaces));
        REQUIRE((geometry_snapshot(data) == snapshot));
        runtime_unchanged();
        query();
    }
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("geometry.bin", bytes.substr(0, end));
        file = wiz8::open_file("geometry.bin");
        bool rejected = false;
        try {
            data.ReadProcessedGameData(file.get());
        } catch (const std::exception&) {
            rejected = true;
        }
        REQUIRE((rejected && data.m_pVertices.data() == old_vertices &&
                 data.m_pSurfaces.data() == old_surfaces));
        REQUIRE((data.VerticesCount() == 3 && data.SurfacesCount() == 1 && old_vertices[1].x == 1));
        runtime_unchanged();
        query();
    }
    auto bad = bytes;
    int invalid = 3;
    std::memcpy(bad.data() + header.size() + sizeof(vertices) +
                    offsetof(wiz8::retail::GDSurface, vertex_indices),
                &invalid, 4);
    store("geometry.bin", bad);
    file = wiz8::open_file("geometry.bin");
    bool rejected = false;
    try {
        data.ReadProcessedGameData(file.get());
    } catch (const std::exception&) {
        rejected = true;
    }
    REQUIRE((rejected && data.m_pVertices.data() == old_vertices &&
             data.m_pSurfaces.data() == old_surfaces));
}

static void malformed_octrees()
{
    // The installed-disc marker bypasses preprocessing so fixtures reach ReadOctFile.
    store("CD.ROM", "");
    W8OctFileHeader header{};
    header.version = W8OctFileHeader::VERSION;
    std::string bytes;
    append(bytes, header);
    append(bytes, UINT32_MAX);
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
        bytes.clear();
        append(bytes, header);
        append(bytes, UINT32_MAX);
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
    auto* values = path.m_pPathValues.get();
    auto* paths = path.m_pCondPaths.data();
    REQUIRE((path.m_pCondPaths.size() == 1 && path.navigation_filter == 9));
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("nodes.bin", bytes.substr(0, end));
        file = wiz8::open_file("nodes.bin");
        REQUIRE((!path.ReadPathNodes(file.get())));
        REQUIRE((path.m_pPathValues.get() == values && path.m_pCondPaths.data() == paths &&
                 path.navigation_filter == 9));
    }
    auto bad = bytes;
    std::memset(bad.data() + sizeof(W8FilePathNode) + sizeof(W8ConditionalPathHeader), 'x',
                sizeof(conditional.name));
    store("nodes.bin", bad);
    file = wiz8::open_file("nodes.bin");
    REQUIRE((!path.ReadPathNodes(file.get()) && path.m_pCondPaths.data() == paths));
    path.path_node_count = -1;
    REQUIRE((!path.ReadPathNodes(file.get()) && path.m_pPathValues.get() == values));
}

struct LoaderFixture {
    ScopedTemporaryDirectory directory{"wiz8-loader-records"};
    ScopedAssetRoots asset_roots;

    LoaderFixture()
    {
        fixture_root = directory.path / "assets";
        std::filesystem::create_directory(fixture_root);
        std::filesystem::create_directory(directory.path / "user");
        asset_roots.configure({wiz8::path_to_utf8(fixture_root),
                               wiz8::path_to_utf8(directory.path / "user"),
                               {"", "", ""}});
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

// Test malformed headers, interior truncations across large fixed-size fields,
// and the final two bytes. Exercising every byte of each 1-3 KiB record across
// all format variants repeats the same short-read failure thousands of times.
static std::vector<std::size_t> truncation_samples(std::size_t size)
{
    std::vector<std::size_t> lengths{0, 1, 2, size / 4, size / 2, size * 3 / 4, size - 2, size - 1};
    for (std::size_t length = 128; length < size; length += 128) lengths.push_back(length);
    std::ranges::sort(lengths);
    lengths.erase(std::unique(lengths.begin(), lengths.end()), lengths.end());
    return lengths;
}

TEST_CASE_METHOD(LoaderFixture, "Versioned level record layouts", "[loader][records]")
{
    auto written_bytes = [](const char* path) {
        auto file = wiz8::open_file(path);
        std::string bytes(file->size(), '\0');
        file->read_exact(bytes.data(), bytes.size());
        return bytes;
    };
    // Build disk records independently of the in-memory padding and pointer fields.
    for (unsigned char version : {1, 2, 3}) {
        std::string bytes;
        append(bytes, version);
        std::array<char, 128> name{};
        std::strcpy(name.data(), "fixture trigger");
        append(bytes, name);
        append(bytes, static_cast<unsigned char>(1)); // No placement or hotspot.
        bytes.append(6, '\x12');
        for (int value : {17, 29, 41}) append(bytes, value);
        std::array<char, 256> recipients{};
        std::strcpy(recipients.data(), "fixture recipients");
        append(bytes, recipients);
        bytes.append(1, '\x21');
        bytes.append(256, '\x22');
        bytes.append(1, '\x23');
        append(bytes, 101);
        bytes.append(1, '\x24');
        bytes.append(128, '\x25');
        if (version >= 2) {
            for (float value : {1.f, 2.f, 3.f, 4.f}) append(bytes, value);
            bytes.append(4, '\x32');
            for (float value : {5.f, 6.f, 7.f, 8.f}) append(bytes, value);
        }
        bytes.append(3, '\x41');
        bytes.append(256, '\x42');
        bytes.append(256, '\x43');
        bytes.append(1, '\x44');
        append(bytes, 103);
        append(bytes, 107);
        bytes.append(256, '\x45');
        append(bytes, 9.f);
        if (version >= 3) bytes.append(128, '\x51');
        bytes.append(1, '\0'); // No door.
        REQUIRE(bytes.size() == (version == 1 ? 1580 : version == 2 ? 1616 : 1744));
        store("super.bin", bytes);
        auto file = wiz8::open_file("super.bin");
        W8LevelFileTrigger trigger{};
        REQUIRE(ReadSuperTriggerFile(*file, trigger));
        auto* record = std::get_if<W8LevelFileSuperTrigger>(&trigger.data);
        REQUIRE(record);
        REQUIRE(record->trigger_off == 41);
        REQUIRE(record->normal_scale == 9.f);
        REQUIRE(record->speed == (version >= 2 ? 5.f : 0.f));
        REQUIRE(record->unknown_4b3[2] == (version >= 2 ? 8.f : 0.f));
        auto output = wiz8::open_file("roundtrip.bin", wiz8::OpenMode::replace);
        REQUIRE(WriteSuperTriggerFile(*output, trigger));
        output->close();
        REQUIRE(written_bytes("roundtrip.bin") == bytes);
        for (const std::size_t end : truncation_samples(bytes.size())) {
            store("super.bin", bytes.substr(0, end));
            file = wiz8::open_file("super.bin");
            trigger = {};
            REQUIRE_FALSE(ReadSuperTriggerFile(*file, trigger));
            REQUIRE(std::holds_alternative<std::monostate>(trigger.data));
        }
        for (std::size_t offset : {std::size_t(1), std::size_t(148)}) {
            auto invalid = bytes;
            std::memset(invalid.data() + offset, 'x', offset == 1 ? 128 : 256);
            store("super.bin", invalid);
            file = wiz8::open_file("super.bin");
            trigger = {};
            REQUIRE_FALSE(ReadSuperTriggerFile(*file, trigger));
            REQUIRE(std::holds_alternative<std::monostate>(trigger.data));
        }
    }
    for (unsigned char mode : {0, 1, 2}) {
        for (unsigned char colours : {0, 1, 2, 3}) {
            std::string bytes(1, '\1');
            for (float value : {1.f, 2.f, 3.f, 4.f, 5.f}) append(bytes, value);
            append(bytes, mode);
            if (mode >= 1)
                for (float value : {6.f, 7.f, 8.f}) append(bytes, value);
            if (mode >= 2)
                for (float value : {9.f, 10.f, 11.f, 12.f}) append(bytes, value);
            append(bytes, static_cast<unsigned char>(colours & 1));
            if (colours & 1) bytes.append(768, '\x61');
            append(bytes, static_cast<unsigned char>(colours & 2));
            if (colours & 2) bytes.append(768, '\x62');
            store("block.bin", bytes);
            auto file = wiz8::open_file("block.bin");
            W8LevelFileBlock block{};
            REQUIRE(ReadLevelFileBlock(*file, block));
            REQUIRE(block.view_distance == 5.f);
            REQUIRE(block.camera_angle == (mode >= 2 ? 9.f : 0.f));
            auto output = wiz8::open_file("roundtrip.bin", wiz8::OpenMode::replace);
            REQUIRE(WriteLevelFileBlock(*output, block));
            output->close();
            REQUIRE(written_bytes("roundtrip.bin") == bytes);
            for (const std::size_t end : truncation_samples(bytes.size())) {
                store("block.bin", bytes.substr(0, end));
                file = wiz8::open_file("block.bin");
                block = {};
                REQUIRE_FALSE(ReadLevelFileBlock(*file, block));
            }
        }
    }
}

TEST_CASE_METHOD(LoaderFixture, "Named positions commit only complete sections",
                 "[loader][records]")
{
    // The disk payload is one version byte, a 128-byte name and seven floats.
    // Build it field by field so the fixture does not depend on runtime layout.
    std::string bytes;
    append(bytes, std::int32_t{2});
    for (const char* name : {"FIRST", "SECOND"}) {
        append(bytes, std::uint8_t{1});
        std::array<char, 128> disk_name{};
        std::strcpy(disk_name.data(), name);
        append(bytes, disk_name);
        for (float value : {1.f, 2.f, 3.f, 45.f, 0.f, 1.f, 0.f}) {
            append(bytes, value);
        }
    }
    W8World world{};
    world.named_positions.emplace_back();
    std::strcpy(world.named_positions.front().name, "EXISTING");
    auto unchanged = [&] {
        REQUIRE(world.named_positions.size() == 1);
        REQUIRE(std::string(world.named_positions.front().name) == "EXISTING");
    };

    // Includes failures after the first complete entry has been decoded.
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("positions.bin", bytes.substr(0, end));
        auto file = wiz8::open_file("positions.bin");
        REQUIRE_FALSE(ReadNamedPositions(file.get(), world.named_positions));
        unchanged();
    }
    store("positions.bin", bytes);
    std::size_t operations;
    {
        auto file = wiz8::open_file("positions.bin");
        FileFaults trace;
        REQUIRE(ReadNamedPositions(file.get(), world.named_positions));
        operations = trace.operations;
    }
    REQUIRE(world.named_positions.size() == 3);
    REQUIRE(std::string(world.named_positions[1].name) == "FIRST");
    REQUIRE(std::string(world.named_positions[2].name) == "SECOND");
    REQUIRE(world.named_positions[1].position.x == 500.f);
    REQUIRE(world.named_positions[1].position.y == 1000.f);
    REQUIRE(world.named_positions[1].position.z == 1500.f);
    REQUIRE(world.named_positions[1].angle == 45.f);
    REQUIRE(world.named_positions[1].direction.y == 1.f);
    world.named_positions.resize(1);
    for (std::size_t operation = 1; operation <= operations; ++operation) {
        auto file = wiz8::open_file("positions.bin");
        FileFaults fault(operation);
        REQUIRE_FALSE(ReadNamedPositions(file.get(), world.named_positions));
        REQUIRE(fault.fired);
        unchanged();
    }
    auto invalid = bytes;
    invalid[4] = 2;
    store("positions.bin", invalid);
    {
        auto file = wiz8::open_file("positions.bin");
        REQUIRE_FALSE(ReadNamedPositions(file.get(), world.named_positions));
        unchanged();
    }
    invalid = bytes;
    invalid.replace(5, 128, 128, 'x');
    store("positions.bin", invalid);
    {
        auto file = wiz8::open_file("positions.bin");
        REQUIRE_FALSE(ReadNamedPositions(file.get(), world.named_positions));
        unchanged();
    }
    for (std::int32_t count : {-1, 0}) {
        std::string empty;
        append(empty, count);
        store("positions.bin", empty);
        auto file = wiz8::open_file("positions.bin");
        REQUIRE(ReadNamedPositions(file.get(), world.named_positions) == (count == 0));
        unchanged();
    }
}

TEST_CASE_METHOD(LoaderFixture, "World camera paths commit only complete tables",
                 "[loader][records]")
{
    std::string bytes;
    append(bytes, std::int32_t{2});
    for (std::uint8_t scaled : {0, 1}) {
        append(bytes, std::int32_t{10});
        append(bytes, std::int32_t{20});
        append(bytes, scaled);
        std::array<char, 20> name{};
        std::strcpy(name.data(), scaled ? "SCALED" : "DEFAULT");
        append(bytes, name);
        if (scaled) append(bytes, 7.5f);
        append(bytes, std::uint8_t{0}); // Path record version.
        append(bytes, std::uint8_t{1}); // Path data version, without per-node scales.
        append(bytes, 0.f);
        append(bytes, std::uint32_t{0});
        append(bytes, std::int32_t{2});
        for (float x : {0.f, 1.f}) {
            for (float value : {x, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f}) append(bytes, value);
        }
    }
    W8World world{};
    world.plsCameras.push_back(std::make_unique<W8CameraPath>());
    auto* existing = GetWorldCameraPath(&world, 0);
    std::strcpy(existing->name0, "EXISTING");
    auto unchanged = [&] {
        REQUIRE(world.plsCameras.size() == 1);
        REQUIRE(GetWorldCameraPath(&world, 0) == existing);
        REQUIRE(std::string(existing->name0) == "EXISTING");
    };
    auto read = [&](wiz8::File* file) {
        W8ReadLevelInfo info{&world, file, nullptr, nullptr};
        return ReadWorldCameras(&info, &world);
    };
    // Every truncation must also release entries and path nodes already decoded.
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("cameras.bin", bytes.substr(0, end));
        auto file = wiz8::open_file("cameras.bin");
        REQUIRE_FALSE(read(file.get()));
        unchanged();
    }
    store("cameras.bin", bytes);
    std::size_t operations;
    {
        auto file = wiz8::open_file("cameras.bin");
        FileFaults trace;
        REQUIRE(read(file.get()));
        operations = trace.operations;
    }
    REQUIRE(world.plsCameras.size() == 3);
    REQUIRE(GetWorldCameraPath(&world, 0) == existing);
    auto* first = GetWorldCameraPath(&world, 1);
    auto* second = GetWorldCameraPath(&world, 2);
    REQUIRE_FALSE(first->active);
    REQUIRE(first->path->timed);
    REQUIRE(first->path->animated);
    REQUIRE(first->path->entry_index == 0);
    REQUIRE(second->path->entry_index == 1);
    REQUIRE(first->path->speed == 15.f);
    REQUIRE(second->path->speed == 7.5f);
    REQUIRE(first->path->nodes.size() == 2);
    REQUIRE(first->path->nodes[1].x == 500.f);
    world.plsCameras.resize(1);
    for (std::size_t operation = 1; operation <= operations; ++operation) {
        auto file = wiz8::open_file("cameras.bin");
        FileFaults fault(operation);
        REQUIRE_FALSE(read(file.get()));
        REQUIRE(fault.fired);
        unchanged();
    }
    // The first path count starts after the camera header and path preamble.
    constexpr std::size_t path_count_offset = 4 + 4 + 4 + 1 + 20 + 1 + 1 + 4 + 4;
    auto invalid = bytes;
    invalid.replace(path_count_offset, 4, 4, '\0');
    store("cameras.bin", invalid);
    {
        auto file = wiz8::open_file("cameras.bin");
        REQUIRE_FALSE(read(file.get())); // An empty path must never be dereferenced.
        unchanged();
    }
    invalid = bytes;
    invalid.replace(4 + 4 + 4 + 1, 20, 20, 'x');
    store("cameras.bin", invalid);
    {
        auto file = wiz8::open_file("cameras.bin");
        REQUIRE_FALSE(read(file.get()));
        unchanged();
    }
    for (std::int32_t count : {-1, 0, 100000}) {
        std::string empty;
        append(empty, count);
        store("cameras.bin", empty);
        auto file = wiz8::open_file("cameras.bin");
        REQUIRE(static_cast<bool>(read(file.get())) == (count == 0));
        unchanged();
    }
}

TEST_CASE_METHOD(LoaderFixture, "Trigger layouts retain version gates and cleanup",
                 "[loader][records]")
{
    auto text = []<std::size_t N>(std::string& bytes, const char* name) {
        std::array<char, N> value{};
        std::strcpy(value.data(), name);
        append(bytes, value);
    };
    for (char type : {1, 2, 3}) {
        for (char version = 1; version <= (type == 1 ? 4 : 5); ++version) {
            std::string bytes;
            append(bytes, char{1});
            append(bytes, type);
            append(bytes, version);
            std::vector<std::pair<std::size_t, std::size_t>> validated_strings;
            auto checked_text = [&]<std::size_t N>(const char* value) {
                validated_strings.emplace_back(bytes.size(), N);
                text.template operator()<N>(bytes, value);
            };
            if (type == 1) {
                for (int value : {9, 11, 13, 15, 17, 19, 21}) append(bytes, value);
                append(bytes, char{1});
                append(bytes, char{1});
                checked_text.template operator()<128>("switch");
                checked_text.template operator()<256>("recipient");
                text.template operator()<128>(bytes, "switch.wav");
                if (version > 1) {
                    append(bytes, 2.f);
                    checked_text.template operator()<64>("surface");
                }
                if (version > 2) {
                    append(bytes, char{1});
                    append(bytes, char{1}); // Owned door, kind 1.
                    append(bytes, char{1}); // Door version.
                    bytes.append(9, '\x34');
                    append(bytes, short{23});
                    append(bytes, char{1});
                    for (float value : {3.f, 5.f, 7.f}) append(bytes, value);
                    text.template operator()<128>(bytes, "linked");
                }
                if (version > 3) append(bytes, 29);
            } else if (type == 2) {
                append(bytes, 2.5f);
                for (float value : {1.f, 2.f, 3.f}) append(bytes, value);
                append(bytes, 29);
                append(bytes, 31);
                append(bytes, char{1});
                append(bytes, char{1});
                checked_text.template operator()<128>("invisible");
                checked_text.template operator()<256>("recipient");
                if (version > 1) {
                    append(bytes, char{1});
                    for (int index = 0; index < 12; ++index)
                        append(bytes, static_cast<float>(index));
                }
                if (version > 2) {
                    append(bytes, 1.25f);
                    for (float value : {5.f, 7.f, 11.f}) append(bytes, value);
                    append(bytes, char{1});
                    text.template operator()<128>(bytes, "action");
                }
                if (version > 3) {
                    append(bytes, char{1});
                    append(bytes, 37);
                }
                if (version > 4) {
                    append(bytes, char{1});
                    append(bytes, char{2});
                    // The serialized kind does not set the distinct legacy kind gate.
                }
            } else {
                for (int value : {2, 3, 5, 7, 11, 13, 17}) append(bytes, value);
                append(bytes, 2.5f);
                for (int index = 0; index < 9; ++index) append(bytes, static_cast<float>(index));
                text.template operator()<128>(bytes, "ambient.wav");
                if (version > 1) {
                    append(bytes, char{1});
                    append(bytes, char{2});
                }
                if (version > 2) {
                    for (float value : {19.f, 23.f, 29.f}) append(bytes, value);
                    append(bytes, 1.25f);
                    for (int index = 0; index < 6; ++index)
                        append(bytes, static_cast<float>(index));
                }
                if (version > 3) text.template operator()<128>(bytes, "sound");
                if (version > 4) append(bytes, char{1});
            }
            store("trigger.bin", bytes);
            auto file = wiz8::open_file("trigger.bin");
            W8LevelFileTrigger trigger{};
            REQUIRE(ReadTriggerFile(*file, trigger));
            REQUIRE(trigger.type == type);
            REQUIRE(!std::holds_alternative<std::monostate>(trigger.data));
            if (type == 1) {
                auto* record = std::get_if<W8LevelFileSwitch>(&trigger.data);
                REQUIRE(record->state_count == 11);
                REQUIRE(record->door.door.has_value() == (version > 2));
            } else if (type == 2) {
                auto* record = std::get_if<W8LevelFileInvisible>(&trigger.data);
                REQUIRE(record->searchable == 31);
                REQUIRE(record->plane.has_value() == (version > 1));
            } else {
                auto* record = std::get_if<W8LevelFileSound>(&trigger.data);
                REQUIRE(record->unbounded == 17);
                REQUIRE(record->shared == (version > 4));
            }
            auto output = wiz8::open_file("roundtrip.bin", wiz8::OpenMode::replace);
            REQUIRE(WriteTriggerFile(*output, trigger));
            output->close();
            output = wiz8::open_file("roundtrip.bin");
            std::string written(output->size(), '\0');
            output->read_exact(written.data(), written.size());
            REQUIRE(written == bytes);
            // Record variants retain their round-trip checks. Sample short
            // headers, interior field boundaries and the final bytes instead
            // of rewriting each byte-prefix of every large version.
            for (const std::size_t end : truncation_samples(bytes.size())) {
                store("trigger.bin", bytes.substr(0, end));
                file = wiz8::open_file("trigger.bin");
                trigger = {};
                REQUIRE_FALSE(ReadTriggerFile(*file, trigger));
                REQUIRE(std::holds_alternative<std::monostate>(trigger.data));
            }
            for (auto [offset, length] : validated_strings) {
                auto invalid = bytes;
                std::memset(invalid.data() + offset, 'x', length);
                store("trigger.bin", invalid);
                file = wiz8::open_file("trigger.bin");
                trigger = {};
                REQUIRE_FALSE(ReadTriggerFile(*file, trigger));
                REQUIRE(std::holds_alternative<std::monostate>(trigger.data));
            }
        }
    }
}

TEST_CASE_METHOD(LoaderFixture, "World item sections reject invalid signed counts",
                 "[loader][records]")
{
    W8World world{};
    for (const int count : {-1, 100000, 0}) {
        std::string bytes;
        append(bytes, count);
        store("items.bin", bytes);
        auto file = wiz8::open_file("items.bin");
        W8ReadLevelInfo info{&world, file.get(), nullptr, nullptr};
        REQUIRE(bool(ReadWorldItems(&info, &world)) == (count == 0));
    }
    store("items.bin", std::string(3, '\0'));
    auto file = wiz8::open_file("items.bin");
    W8ReadLevelInfo info{&world, file.get(), nullptr, nullptr};
    REQUIRE_FALSE(ReadWorldItems(&info, &world));
}

TEST_CASE_METHOD(LoaderFixture,
                 "Waypoint graph growth retains indices and conditional edge updates",
                 "[loader][pathing]")
{
    W8Octree tree(nullptr, nullptr);
    const struct RestoreOctree {
        W8Octree* previous;
        ~RestoreOctree() { g_octree = previous; }
    } restore{g_octree};
    g_octree = &tree;
    tree.object_registry = new W8OctreeObjectRegistry;
    tree.m_spatial.m_bounds.minimum = glm::vec3(0);
    tree.m_spatial.m_node_extent = 1;
    W8PathingService path;
    path.m_pPathValues = std::make_unique<std::unordered_multimap<unsigned, unsigned>>();
    path.grid_scale = 1;
    path.span = 1;
    for (unsigned i = 1; i <= 150; ++i) {
        const srVector3 position(i, 1, 1);
        path.AddWaypoint(&position);
        path.m_waypoints[i].flags |= W8_WAYPOINT_CONDITIONAL;
        const auto node = path.AllocateSearchNode();
        REQUIRE(node == i);
        REQUIRE(path.m_search_nodes[node].flags == 0);
        REQUIRE(path.m_search_nodes[node].parent_node == 0);
        path.m_search_nodes[node].parent_node = node - 1;
        path.m_search_nodes[node].position = position;
        if (i > 1) path.AddWaypointLink(1, i, W8_PATH_EDGE_TELEPORTAL);
    }
    REQUIRE(path.m_waypoints.size() == 151);
    REQUIRE(path.m_pEdges.size() == 150);
    for (unsigned i = 1; i <= 150; ++i) {
        REQUIRE(path.m_waypoints[i].index == i);
        REQUIRE(path.m_waypoints[i].position == srVector3(i, 1, 1));
        REQUIRE(path.m_search_nodes[i].parent_node == i - 1);
        REQUIRE(path.m_search_nodes[i].position == path.m_waypoints[i].position);
    }
    unsigned edge = path.m_waypoints[1].first_edge;
    for (unsigned destination = 2; destination <= 150; ++destination) {
        REQUIRE(edge != 0);
        REQUIRE(path.m_pEdges[edge].source == 1);
        REQUIRE(path.m_pEdges[edge].destination == destination);
        edge = path.m_pEdges[edge].next;
    }
    REQUIRE(edge == 0);
    const auto first_edge = path.m_waypoints[1].first_edge;
    path.m_pEdges[first_edge].flags = 0;
    REQUIRE(path.UpdateWaypointLink(1, 2, W8_PATH_EDGE_TELEPORTAL));
    REQUIRE(path.m_pEdges.size() == 150);
    REQUIRE(path.m_pEdges[first_edge].flags ==
            (W8_PATH_EDGE_TELEPORTAL | W8_PATH_EDGE_CONDITIONAL));

    path.level_name = "graph";
    path.path_node_count = 1;
    path.edge_node_count = 1;
    std::vector<W8FileWaypoint> snapshot(path.m_waypoints.size());
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
        snapshot[i].flags = path.m_waypoints[i].flags;
        snapshot[i].first_edge = path.m_waypoints[i].first_edge;
        snapshot[i].position = path.m_waypoints[i].position;
    }
    REQUIRE(path.WriteWaypointFile(snapshot));
    REQUIRE(path.ReadWaypointFile());
    REQUIRE(path.m_waypoints.size() == snapshot.size());
    REQUIRE(path.m_pEdges.size() == 150);
    for (std::size_t i = 1; i < snapshot.size(); ++i) {
        REQUIRE(path.m_waypoints[i].position == snapshot[i].position);
        REQUIRE(path.m_waypoints[i].first_edge == snapshot[i].first_edge);
    }
}

TEST_CASE_METHOD(LoaderFixture, "Environment tables decode RGB bytes without runtime vector layout",
                 "[loader][records]")
{
    const struct RestoreTables {
        std::array<glm::vec3, 256> environment = g_environment_colours0;
        std::array<glm::vec3, 256> light = g_environment_colours1;
        ~RestoreTables()
        {
            g_environment_colours0 = environment;
            g_environment_colours1 = light;
        }
    } restore;
    std::string bytes;
    for (int index = 0; index < 256; ++index) {
        bytes.push_back(static_cast<char>(index));
        bytes.push_back(static_cast<char>(255 - index));
        bytes.push_back(static_cast<char>(index / 2));
    }
    for (const auto& [read, table] :
         {std::pair{&ReadEnvironmentColourTable, &g_environment_colours0},
          std::pair{&ReadLightColourTable, &g_environment_colours1}}) {
        store("colours.bin", bytes);
        auto file = wiz8::open_file("colours.bin");
        REQUIRE(read(file.get()));
        REQUIRE(file->tell() == 768);
        for (int index : {0, 1, 127, 255}) {
            const glm::vec3 expected(index, 255 - index, index / 2);
            REQUIRE((*table)[index] == expected * (1.0f / 255.0f));
        }
        const auto complete = *table;
        store("colours.bin", bytes.substr(0, 767));
        file = wiz8::open_file("colours.bin");
        REQUIRE_FALSE(read(file.get()));
        REQUIRE(*table == complete);
    }
}

TEST_CASE("Collision disk codecs retain vector components and unknown bytes", "[loader][records]")
{
    wiz8::retail::Environ environment{};
    environment.value_00 = 17;
    environment.ground_latch = 1;
    environment.airborne = 3;
    environment.pad_06[0] = 0xa5;
    environment.pad_06[1] = 0x5a;
    environment.scale = 2;
    environment.vector = {3, -4, 5};
    environment.value_40 = 11;
    W8EnvironRecord live_environment;
    wiz8::retail::decode_image(environment, live_environment);
    REQUIRE(live_environment.vector == glm::vec3(3, -4, 5));
    REQUIRE(live_environment.ground_latch);
    REQUIRE(live_environment.scale == 2);
    const auto encoded_environment = wiz8::retail::encode(live_environment);
    REQUIRE(std::memcmp(encoded_environment.data(), &environment, sizeof(environment)) == 0);

    wiz8::retail::GDSurface surface{};
    surface.plane = {{1.25f, -2.5f, 3.75f}, -4.5f};
    surface.positional[0] = 0xa5;
    surface.positional[1] = 0x5a;
    W8GDSurface live_surface{};
    wiz8::retail::decode_image(surface, live_surface);
    REQUIRE(live_surface.plane.normal == glm::vec3(1.25f, -2.5f, 3.75f));
    REQUIRE(live_surface.plane.w == -4.5f);
    const auto encoded_surface = wiz8::retail::encode(live_surface);
    REQUIRE(std::memcmp(encoded_surface.data(), &surface, sizeof(surface)) == 0);

    wiz8::retail::RegionVolume region{};
    region.flags = 0x10203040;
    region.m_region = 0x1234;
    region.positional[5] = 0x9a;
    region.value_18 = 0x87654321;
    for (int i = 0; i < 9; ++i) region.m_points[i] = {float(i), float(i + 10), float(i + 20)};
    for (int i = 0; i < 6; ++i)
        region.m_planes[i] = {{float(i + 30), float(i + 40), float(i + 50)}, float(i + 60)};
    W8OctRegionVolume live_region{};
    wiz8::retail::decode_image(region, live_region);
    REQUIRE(live_region.m_region == 0x1234);
    REQUIRE(live_region.m_points[8] == glm::vec3(8, 18, 28));
    REQUIRE(live_region.m_planes[5].normal == glm::vec3(35, 45, 55));
    REQUIRE(live_region.m_planes[5].w == 65);
    const auto encoded_region = wiz8::retail::encode(live_region);
    REQUIRE(std::memcmp(encoded_region.data(), &region, sizeof(region)) == 0);
}

TEST_CASE_METHOD(LoaderFixture,
                 "Octree sample points transfer float triples independently of GLM layout",
                 "[loader][records]")
{
    W8Octree tree(nullptr, nullptr);
    tree.m_sample_points = {{1, -2, 3.5f}, {4, 5, -6}};
    REQUIRE(tree.SavePoints("sample-fixture.lvl"));
    auto file = wiz8::open_file("sample-fixture.pts");
    REQUIRE(file->size() == 4 + 6 * sizeof(float));
    unsigned int count = 0;
    file->read_exact(&count, sizeof(count));
    REQUIRE(count == 2);
    std::array<float, 6> coordinates{};
    file->read_exact(coordinates.data(), coordinates.size() * sizeof(float));
    REQUIRE(coordinates == std::array<float, 6>{1, -2, 3.5f, 4, 5, -6});
    file->close();
    tree.m_sample_points.clear();
    REQUIRE(tree.LoadPointFiles("sample-fixture.lvl"));
    REQUIRE(tree.m_sample_points == std::vector<glm::vec3>{{1, -2, 3.5f}, {4, 5, -6}});
}

TEST_CASE_METHOD(LoaderFixture, "World item groups transfer ownership and preserve save records",
                 "[loader][items]")
{
    const auto registered = g_searchables.size();
    const struct RestoreSearchables {
        std::size_t count;
        ~RestoreSearchables()
        {
            while (g_searchables.size() > count) {
                delete g_searchables.back();
                g_searchables.pop_back();
            }
        }
    } restore{registered};
    wiz8::retail::WorldItem first{}, second{};
    first.runtime_id = 17;
    first.position = {1, -2, 3};
    first.flags = W8_WORLD_ITEM_HIDDEN;
    first.saved_marker = second.saved_marker = 1;
    first.next = 1;
    first.sector_id = second.sector_id = -2;
    first.unknown_3d[15] = 0xa5;
    second.runtime_id = 29;
    second.position = {4, 5, -6};
    std::string bytes;
    append(bytes, first);
    append(bytes, second);
    store("items.bin", bytes.substr(0, bytes.size() - 1));
    auto file = wiz8::open_file("items.bin");
    REQUIRE_FALSE(LoadItem(*file));
    REQUIRE(g_searchables.size() == registered);
    file.reset();
    store("items.bin", bytes);
    file = wiz8::open_file("items.bin");
    auto group = LoadItem(*file);
    REQUIRE(group);
    REQUIRE(ItemInfoGetNumInGroup(group.get()) == 2);
    REQUIRE(g_searchables.size() == registered + 1);
    REQUIRE(g_searchables.back()->world_item == group.get());
    auto detached = ItemInfoRemoveFromGroup(group.get(), group->next.get());
    REQUIRE(detached->runtime_id == 29);
    REQUIRE_FALSE(group->next);
    ItemInfoAddToGroup(group.get(), std::move(detached));
    auto output = wiz8::open_file("items-roundtrip.bin", wiz8::OpenMode::replace);
    REQUIRE(SaveItemFile(*output, group.get()));
    output.reset();
    std::ifstream encoded(fixture_root / "items-roundtrip.bin", std::ios::binary);
    REQUIRE(std::string(std::istreambuf_iterator<char>(encoded), {}) == bytes);
}
