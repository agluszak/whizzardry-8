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
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "asset_paths.h"
#include "file_faults.h"
#include "file_operations.h"
#include "geometry.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "save_payload.h"
#include "scoped_asset_roots.h"
#include "scoped_records.h"
#include "temporary_directory.h"
#include "wiz8/chunk.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/location_variables.h"
#include "wiz8/navigation_flags.h"
#include "wiz8/packed_bits.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/retail_geometry_records.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/save_game.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/spatial/surface_index.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/world.h"
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
    std::filesystem::create_directories(fixture_root / "data" / "databases");
    W8ItemDatabaseRecord item{};
    std::string bytes;
    append(bytes, 1u);
    append(bytes, item);
    store("data/databases/items.dbs", bytes);
    REQUIRE((InitializeItemDatabase()));
    std::size_t item_operations;
    {
        FileFaults trace;
        REQUIRE((InitializeItemDatabase()));
        item_operations = trace.operations;
    }
    const auto* records = g_item_records.data();
    for (std::size_t operation = 1; operation <= item_operations; ++operation) {
        {
            FileFaults fault(operation);
            REQUIRE((!InitializeItemDatabase()));
            REQUIRE((fault.fired));
        }
        REQUIRE((g_item_records.data() == records && g_item_records.size() == 1));
    }
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("data/databases/items.dbs", bytes.substr(0, end));
        REQUIRE((!InitializeItemDatabase()));
        REQUIRE((g_item_records.data() == records && g_item_records.size() == 1));
    }
    std::string excessive;
    append(excessive, UINT32_MAX);
    store("data/databases/items.dbs", excessive);
    REQUIRE((!InitializeItemDatabase() && g_item_records.data() == records));
    DestroyItemDatabase();
    REQUIRE(g_item_records.empty());

    std::array<char, 256> category{};
    std::strcpy(category.data(), "Weapons");
    W8ItemTableRecord table{};
    std::strcpy(table.name, "Starter");
    bytes.clear();
    append(bytes, 1u);
    append(bytes, category);
    append(bytes, 1u);
    append(bytes, table);
    store("data/databases/itemtables.dbs", bytes);
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
        REQUIRE((ItemTableCount() == 1 && ItemTableCategoryCount() == 1));
    }
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("data/databases/itemtables.dbs", bytes.substr(0, end));
        REQUIRE((!InitializeItemTables()));
        REQUIRE((g_item_tables == tables && g_item_table_category_names == categories));
        REQUIRE((ItemTableCount() == 1 && ItemTableCategoryCount() == 1));
        REQUIRE((!std::strcmp(tables[0].name, "Starter")));
    }
    auto bad = bytes;
    std::memset(bad.data() + 4, 'x', 256);
    store("data/databases/itemtables.dbs", bad);
    REQUIRE((!InitializeItemTables() && g_item_tables == tables));
    bad = bytes;
    std::memset(bad.data() + 264, 'x', 256);
    store("data/databases/itemtables.dbs", bad);
    REQUIRE((!InitializeItemTables() && g_item_tables == tables));
    DestroyItemTables();
    REQUIRE((!g_item_tables && !g_item_table_category_names));
}

static std::string geometry_snapshot(const LevelSpatialIndex& data)
{
    // A rollback snapshot, not a file encoder: compare the live banks and
    // existing retail codecs without retaining the removed OCT writer.
    std::string bytes;
    for (const auto& value : data.m_pVertices) append(bytes, value);
    for (const auto& value : data.m_pSurfaces) append(bytes, wiz8::retail::encode(value));
    for (const auto& value : data.m_pInterfaces) append(bytes, value);
    for (const auto& value : data.m_pStates) append(bytes, value);
    for (const auto& value : data.m_piCondPolys) append(bytes, value);
    for (const auto& value : data.interface_names) {
        bytes += value;
        bytes += '\0';
    }
    for (const auto& value : data.environments) append(bytes, wiz8::retail::encode(value));
    return bytes;
}

static void wgd_banks()
{
    auto bank = [](bool secondary, int group = 0) {
        std::string bytes;
        append(bytes, 3);
        append(bytes, 1);
        append(bytes, std::array<glm::vec3, 3>{glm::vec3(2, 2, 2), glm::vec3(3, 2, 2),
                                               glm::vec3(2, 2, 3)});
        append(bytes, std::array<int, 3>{0, 1, 2});
        append(bytes, glm::vec3(0, 1, 0));
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
        append(bytes, glm::vec3(2, 2, 2));
        append(bytes, glm::vec3(3, 3, 3));
        return bytes;
    };
    store("primary.wgd", bank(false));
    const auto secondary = bank(true, 7);
    store("secondary.wgd", secondary);
    LevelSpatialIndex data;
    data.compiled_available = false;
    data.collision_available = true;
    auto load = [&](const char* path, int kind) {
        auto file = wiz8::open_file(wiz8::asset_path(path));
        return data.ReadWGDList(*file, kind);
    };
    REQUIRE((kWorldScale != 1.0f));
    REQUIRE((load("primary.wgd", 0)));
    REQUIRE((data.minimum.x == 2 * kWorldScale && data.maximum.x == 3 * kWorldScale &&
             data.maximum.y == 3 * kWorldScale));
    REQUIRE((data.m_pVertices[1].x == 3 * kWorldScale && data.m_pVertices[2].z == 3 * kWorldScale));
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
    const auto original_flags = data.m_pSurfaces[3].flags;
    data.SetInterfaceState(1, 7);
    REQUIRE((!(effectiveSurfaceFlags(data.m_pSurfaces[1], data.state.conditional_disabled[1]) &
               W8_GD_SURFACE_CONDITIONAL_DISABLED)));
    REQUIRE((effectiveSurfaceFlags(data.m_pSurfaces[3], data.state.conditional_disabled[3]) &
             W8_GD_SURFACE_CONDITIONAL_DISABLED));
    REQUIRE(data.m_pSurfaces[3].flags == original_flags);
    const auto snapshot = geometry_snapshot(data);
    const auto* vertices = data.m_pVertices.data();
    const auto* surfaces = data.m_pSurfaces.data();
    const auto* interfaces = data.m_pInterfaces.data();
    const auto* states = data.m_pStates.data();
    const auto* polygons = data.m_piCondPolys.data();
    const auto* environ = data.environments.data();
    const auto names = data.interface_names;
    auto unchanged = [&] {
        REQUIRE((vertices == data.m_pVertices.data() && surfaces == data.m_pSurfaces.data()));
        REQUIRE((interfaces == data.m_pInterfaces.data() && states == data.m_pStates.data() &&
                 polygons == data.m_piCondPolys.data()));
        REQUIRE((names == data.interface_names && environ == data.environments.data()));
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
    const std::array<glm::vec3, 3> vertices{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0),
                                            glm::vec3(0, 1, 0)};
    append(bytes, vertices);
    wiz8::retail::GDSurface surface{};
    surface.vertex_indices[0] = 0;
    surface.vertex_indices[1] = 1;
    surface.vertex_indices[2] = 2;
    surface.plane.normal = {0, 0, 1};
    surface.flags = 2; // dominant Z axis for the spatial query
    append(bytes, surface);
    LevelSpatialIndex data;
    data.compiled_available = false;
    data.collision_available = true;
    store("geometry.bin", bytes);
    auto file = wiz8::open_file(wiz8::asset_path("geometry.bin"));
    data.ReadProcessedCollision(file.get());
    file->seek(0, wiz8::SeekOrigin::begin);
    std::size_t operations;
    {
        FileFaults trace;
        data.ReadProcessedCollision(file.get());
        operations = trace.operations;
    }
    auto query = [&] {
        glm::vec3 start(0.25f, 0.25f, 2), end(0.25f, 0.25f, -2);
        if (!data.geometry_index) REQUIRE((BuildCollisionIndex(&data)));
        auto delta = end - start;
        const auto hits = data.geometry_index->queryAlongSegment(start, delta, 0);
        REQUIRE(hits.size() == 1);
        REQUIRE((hits[0] == data.m_pSurfaces.data()));
        const auto query_segment = [](const glm::vec3& from, const glm::vec3& to) {
            const auto delta = to - from;
            const auto length = glm::length(delta);
            return PlaneSegment{from, to, delta / length, length, length};
        };
        const auto trace = data.traceGeometry(query_segment(start, end), {});
        REQUIRE(trace);
        REQUIRE((trace->point.z == 0 && trace->surface_index == 0));
        const auto skin_trace =
            data.traceGeometry(query_segment({0.25f, 0.25f, 0.5f}, {0.25f, 0.25f, -2}), {});
        REQUIRE(skin_trace);
        REQUIRE(skin_trace->point.z == 0.5f);
        REQUIRE(skin_trace->distance == 0.5f);
    };
    for (int reload = 0; reload < 10; ++reload) {
        auto reload_file = wiz8::open_file(wiz8::asset_path("geometry.bin"));
        data.ReadProcessedCollision(reload_file.get());
        query();
    }
    SetBit(data.state.pending_triggers, 0);
    SetBit(data.state.active_triggers, 0);
    const auto* old_index = data.geometry_index.get();
    const auto pending_bits = data.state.pending_triggers;
    const auto active_bits = data.state.active_triggers;
    auto runtime_unchanged = [&] {
        REQUIRE((data.geometry_index.get() == old_index));
        REQUIRE((data.state.pending_triggers == pending_bits &&
                 data.state.active_triggers == active_bits));
        REQUIRE(
            (TestBit(data.state.pending_triggers, 0) && TestBit(data.state.active_triggers, 0)));
        REQUIRE((data.m_ppTriggers.empty() && data.m_pTrigVertices.empty() &&
                 data.m_pTrigSurfaces.empty()));
    };
    auto* old_vertices = data.m_pVertices.data();
    auto* old_surfaces = data.m_pSurfaces.data();
    const auto snapshot = geometry_snapshot(data);
    for (std::size_t op = 1; op <= operations; ++op) {
        file = wiz8::open_file(wiz8::asset_path("geometry.bin"));
        bool rejected = false;
        {
            FileFaults fault(op);
            try {
                data.ReadProcessedCollision(file.get());
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
        file = wiz8::open_file(wiz8::asset_path("geometry.bin"));
        bool rejected = false;
        try {
            data.ReadProcessedCollision(file.get());
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
    file = wiz8::open_file(wiz8::asset_path("geometry.bin"));
    bool rejected = false;
    try {
        data.ReadProcessedCollision(file.get());
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
        LevelSpatialIndex tree("fixture.oct");
        REQUIRE((tree.HasLoadError() && !tree.collision_available));
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
        LevelSpatialIndex tree("fixture.oct", nullptr);
        REQUIRE((tree.HasLoadError()));
    }
    REQUIRE((std::filesystem::remove(fixture_root / "CD.ROM")));
}

static void path_tables()
{
    NavigationMesh path;
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
    auto file = wiz8::open_file(wiz8::asset_path("nodes.bin"));
    REQUIRE((path.ReadPathNodes(file.get())));
    auto* values = std::addressof(*path.cells.begin());
    auto* paths = path.m_pCondPaths.data();
    REQUIRE((path.m_pCondPaths.size() == 1));
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("nodes.bin", bytes.substr(0, end));
        file = wiz8::open_file(wiz8::asset_path("nodes.bin"));
        REQUIRE((!path.ReadPathNodes(file.get())));
        REQUIRE(
            (std::addressof(*path.cells.begin()) == values && path.m_pCondPaths.data() == paths));
    }
    auto bad = bytes;
    std::memset(bad.data() + sizeof(W8FilePathNode) + sizeof(W8ConditionalPathHeader), 'x',
                sizeof(conditional.name));
    store("nodes.bin", bad);
    file = wiz8::open_file(wiz8::asset_path("nodes.bin"));
    REQUIRE((!path.ReadPathNodes(file.get()) && path.m_pCondPaths.data() == paths));
    path.path_node_count = -1;
    REQUIRE((!path.ReadPathNodes(file.get()) && std::addressof(*path.cells.begin()) == values));
}

struct LoaderFixture {
    ScopedTemporaryDirectory directory{"wiz8-loader-records"};
    ScopedAssetRoots asset_roots;

    LoaderFixture()
    {
        fixture_root = directory.path / "assets";
        std::filesystem::create_directory(fixture_root);
        std::filesystem::create_directory(directory.path / "user");
        asset_roots.configure(
            {wiz8::path_to_utf8(fixture_root), wiz8::path_to_utf8(directory.path / "user")});
    }

    ~LoaderFixture() { fixture_root.clear(); }
};

TEST_CASE_METHOD(LoaderFixture, "Database record validation", "[loader][records]")
{
    databases();
}

TEST_CASE_METHOD(LoaderFixture, "NPC database replacement owns appended stock rules",
                 "[loader][records]")
{
    const struct Cleanup {
        ~Cleanup() { DestroyNpcDatabase(); }
    } cleanup;
    std::filesystem::create_directories(fixture_root / "data/databases");
    W8NpcDatabaseRecord npc{};
    npc.version = 2;
    npc.source_name = "merchant";
    std::string bytes;
    append(bytes, 2u);
    append(bytes, npc);
    append(bytes, 2u);
    append(bytes, W8NpcItemStockRule{17, 3, 1});
    append(bytes, W8NpcItemStockRule{29, 5, 0});
    npc.source_name = "no stock";
    append(bytes, npc);
    append(bytes, 0u);
    store("data/databases/npc.dbs", bytes);
    REQUIRE(InitializeNpcDatabase());
    const auto* original = g_npc_records.data();
    REQUIRE(g_npc_records.size() == 2);
    REQUIRE(original[0].item_stock_rules.size() == 2);
    REQUIRE(original[0].item_stock_rules[1].item_id == 29);
    REQUIRE(original[1].item_stock_rules.empty());

    // Fail after constructing the first record and its stock. The published database survives.
    auto truncated = bytes.substr(0, bytes.size() - sizeof(unsigned int));
    append(truncated, 1u); // A second stock list whose payload is missing.
    store("data/databases/npc.dbs", truncated);
    REQUIRE_FALSE(InitializeNpcDatabase());
    REQUIRE(g_npc_records.data() == original);
    REQUIRE(g_npc_records.size() == 2);
    REQUIRE(original[0].item_stock_rules[0].quantity == 3);

    npc.no_item_stock = 1;
    bytes.clear();
    append(bytes, 1u);
    append(bytes, npc);
    store("data/databases/npc.dbs", bytes);
    REQUIRE(InitializeNpcDatabase());
    REQUIRE(g_npc_records.size() == 1);
    REQUIRE(g_npc_records[0].item_stock_rules.empty());
    DestroyNpcDatabase();
    DestroyNpcDatabase();
    REQUIRE(g_npc_records.empty());
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
    World world{};
    world.named_positions.emplace_back();
    std::strcpy(world.named_positions.front().name, "EXISTING");
    auto unchanged = [&] {
        REQUIRE(world.named_positions.size() == 1);
        REQUIRE(std::string(world.named_positions.front().name) == "EXISTING");
    };

    // Includes failures after the first complete entry has been decoded.
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("positions.bin", bytes.substr(0, end));
        auto file = wiz8::open_file(wiz8::asset_path("positions.bin"));
        REQUIRE_FALSE(ReadNamedPositions(file.get(), world.named_positions));
        unchanged();
    }
    store("positions.bin", bytes);
    std::size_t operations;
    {
        auto file = wiz8::open_file(wiz8::asset_path("positions.bin"));
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
        auto file = wiz8::open_file(wiz8::asset_path("positions.bin"));
        FileFaults fault(operation);
        REQUIRE_FALSE(ReadNamedPositions(file.get(), world.named_positions));
        REQUIRE(fault.fired);
        unchanged();
    }
    auto invalid = bytes;
    invalid[4] = 2;
    store("positions.bin", invalid);
    {
        auto file = wiz8::open_file(wiz8::asset_path("positions.bin"));
        REQUIRE_FALSE(ReadNamedPositions(file.get(), world.named_positions));
        unchanged();
    }
    invalid = bytes;
    invalid.replace(5, 128, 128, 'x');
    store("positions.bin", invalid);
    {
        auto file = wiz8::open_file(wiz8::asset_path("positions.bin"));
        REQUIRE_FALSE(ReadNamedPositions(file.get(), world.named_positions));
        unchanged();
    }
    for (std::int32_t count : {-1, 0}) {
        std::string empty;
        append(empty, count);
        store("positions.bin", empty);
        auto file = wiz8::open_file(wiz8::asset_path("positions.bin"));
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
    World world{};
    auto* existing = world.plsCameras.emplace_back(std::make_unique<W8CameraPath>()).get();
    std::strcpy(existing->name0, "EXISTING");
    auto unchanged = [&] {
        REQUIRE(world.plsCameras.size() == 1);
        REQUIRE(GetWorldCameraPath(&world, 0) == existing);
        REQUIRE(std::string(existing->name0) == "EXISTING");
    };
    auto read = [&](wiz8::File* file) {
        LevelLoadContext info{&world, file, {}};
        return ReadWorldCameras(&info, &world);
    };
    // Every truncation must also release entries and path nodes already decoded.
    for (std::size_t end = 0; end < bytes.size(); ++end) {
        store("cameras.bin", bytes.substr(0, end));
        auto file = wiz8::open_file(wiz8::asset_path("cameras.bin"));
        REQUIRE_FALSE(read(file.get()));
        unchanged();
    }
    store("cameras.bin", bytes);
    std::size_t operations;
    {
        auto file = wiz8::open_file(wiz8::asset_path("cameras.bin"));
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
        auto file = wiz8::open_file(wiz8::asset_path("cameras.bin"));
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
        auto file = wiz8::open_file(wiz8::asset_path("cameras.bin"));
        REQUIRE_FALSE(read(file.get())); // An empty path must never be dereferenced.
        unchanged();
    }
    invalid = bytes;
    invalid.replace(4 + 4 + 4 + 1, 20, 20, 'x');
    store("cameras.bin", invalid);
    {
        auto file = wiz8::open_file(wiz8::asset_path("cameras.bin"));
        REQUIRE_FALSE(read(file.get()));
        unchanged();
    }
    for (std::int32_t count : {-1, 0, 100000}) {
        std::string empty;
        append(empty, count);
        store("cameras.bin", empty);
        auto file = wiz8::open_file(wiz8::asset_path("cameras.bin"));
        REQUIRE(static_cast<bool>(read(file.get())) == (count == 0));
        unchanged();
    }
}

TEST_CASE_METHOD(LoaderFixture, "World item sections reject invalid signed counts",
                 "[loader][records]")
{
    World world{};
    for (const int count : {-1, 100000, 0}) {
        std::string bytes;
        append(bytes, count);
        store("items.bin", bytes);
        auto file = wiz8::open_file(wiz8::asset_path("items.bin"));
        LevelLoadContext info{&world, file.get(), {}};
        REQUIRE(bool(ReadWorldItems(&info, &world)) == (count == 0));
    }
    store("items.bin", std::string(3, '\0'));
    auto file = wiz8::open_file(wiz8::asset_path("items.bin"));
    LevelLoadContext info{&world, file.get(), {}};
    REQUIRE_FALSE(ReadWorldItems(&info, &world));
}

TEST_CASE_METHOD(LoaderFixture, "Environment tables decode RGB bytes without runtime vector layout",
                 "[loader][records]")
{
    std::string bytes;
    for (int index = 0; index < 256; ++index) {
        bytes.push_back(static_cast<char>(index));
        bytes.push_back(static_cast<char>(255 - index));
        bytes.push_back(static_cast<char>(index / 2));
    }
    std::array<glm::vec3, 256> colours;
    auto* table = &colours;
    {
        store("colours.bin", bytes);
        auto file = wiz8::open_file(wiz8::asset_path("colours.bin"));
        REQUIRE(ReadEnvironmentColours(file.get(), colours));
        REQUIRE(file->tell() == 768);
        for (int index : {0, 1, 127, 255}) {
            const glm::vec3 expected(index, 255 - index, index / 2);
            REQUIRE((*table)[index] == expected * (1.0f / 255.0f));
        }
        const auto complete = *table;
        store("colours.bin", bytes.substr(0, 767));
        file = wiz8::open_file(wiz8::asset_path("colours.bin"));
        REQUIRE_FALSE(ReadEnvironmentColours(file.get(), colours));
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
    LevelEnvironment live_environment;
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

TEST_CASE_METHOD(LoaderFixture, "World item groups transfer ownership and preserve save records",
                 "[loader][items]")
{
    wiz8::retail::WorldItem first{}, second{}, third{};
    first.runtime_id = 17;
    first.position = {1, -2, 3};
    first.flags = W8_WORLD_ITEM_HIDDEN;
    first.entity_flags = second.entity_flags = W8_ITEM_ENTITY_RADAR_SEEN;
    first.saved_marker = second.saved_marker = 1;
    first.next = 1;
    first.sector_id = second.sector_id = -2;
    first.unknown_3d[15] = 0xa5;
    second.runtime_id = 29;
    second.position = {4, 5, -6};
    second.next = 1;
    third = second;
    third.runtime_id = 31;
    third.next = 0;
    std::string bytes;
    append(bytes, first);
    append(bytes, second);
    append(bytes, third);
    store("items.bin", bytes.substr(0, bytes.size() - 1));
    auto file = wiz8::open_file(wiz8::asset_path("items.bin"));
    REQUIRE_THROWS(ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadItem(chunk); }));
    file.reset();
    store("items.bin", bytes);
    file = wiz8::open_file(wiz8::asset_path("items.bin"));
    auto group = ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadItem(chunk); });
    REQUIRE(group);
    REQUIRE(group->contents.size() == 2);
    REQUIRE(group->contents[0]->runtime_id == 29);
    REQUIRE(group->contents[1]->runtime_id == 31);
    const auto roundtrip_path = wiz8::user_path("items-roundtrip.bin");
    auto output = wiz8::open_file(roundtrip_path, wiz8::OpenMode::replace);
    group->saved_marker = 23;
    group->contents.front()->saved_marker = 24;
    REQUIRE_NOTHROW(SaveItemFile(*output, *group));
    REQUIRE(group->saved_marker == 23);
    REQUIRE(group->contents.front()->saved_marker == 24);
    output.reset();
    auto encoded = wiz8::open_file(roundtrip_path);
    std::string roundtrip(encoded->size(), '\0');
    encoded->read_exact(roundtrip.data(), roundtrip.size());
    REQUIRE(roundtrip.size() == bytes.size());
    REQUIRE(std::equal(roundtrip.begin(), roundtrip.end(), bytes.begin()));
    file->seek(0, wiz8::SeekOrigin::begin);
    auto defaults = ReadSavePayload(*file, [](W8Chunk& chunk) { return LoadItem(chunk, true); });
    REQUIRE(defaults);
    REQUIRE((defaults->entity_flags & W8_ITEM_ENTITY_RADAR_SEEN) == 0);
    REQUIRE((defaults->contents.front()->entity_flags & W8_ITEM_ENTITY_RADAR_SEEN) == 0);
}

TEST_CASE_METHOD(LoaderFixture, "Trigger rejects an incomplete saved item group",
                 "[loader][trigger][items]")
{
    std::string bytes;
    append(bytes, 0u); // flags
    append(bytes, std::uint8_t{0}); // state index
    append(bytes, std::uint8_t{0}); // state direction
    append(bytes, std::uint16_t{0}); // action
    append(bytes, std::uint8_t{0}); // action state
    append(bytes, -1); // required item
    append(bytes, std::uint8_t{0}); // no action payload
    append(bytes, std::uint8_t{1}); // an item group must follow
    // Enough bytes for the four trailing fields, but no complete item record.
    bytes.append(13, '\0');
    store("truncated-trigger.bin", bytes);
    auto file = wiz8::open_file(wiz8::asset_path("truncated-trigger.bin"));
    Trigger trigger;
    REQUIRE_FALSE(
        ReadSavePayload(*file, [&](W8Chunk& chunk) { return trigger.Load(chunk, 5, false); }));
}

TEST_CASE_METHOD(LoaderFixture, "Nested item write failure aborts a staged trigger save",
                 "[loader][trigger][items]")
{
    const struct WorldScope {
        std::unique_ptr<World> previous = ReplaceWorld(std::make_unique<World>());
        ~WorldScope() { ReplaceWorld(std::move(previous)); }
    } scope;
    auto trigger = std::make_unique<Trigger>(GetWorld());
    trigger->world_item_group = std::make_unique<W8WorldItem>();
    GetWorld()->triggers.push_back(std::move(trigger));
    const auto path = wiz8::user_path("trigger-save.bin");
    wiz8::open_file(path, wiz8::OpenMode::replace)->write("old", 3);
    {
        wiz8::StagedFile staged(path);
        static wiz8::File* writer;
        static bool fired;
        writer = staged.file().get();
        fired = false;
        const auto previous = wiz8::testing::set_file_operation_hook([](auto operation) {
            // Version 5's first item follows the 148-byte trigger header.
            if (!fired && operation == wiz8::testing::FileOperation::write &&
                writer->tell() == 148) {
                fired = true;
                throw std::runtime_error("fixture item write failure");
            }
        });
        const struct RestoreHook {
            wiz8::testing::FileOperationHook previous;
            ~RestoreHook() { wiz8::testing::set_file_operation_hook(previous); }
        } restore{previous};
        REQUIRE_THROWS_WITH(SaveWorldTriggers(*writer), "fixture item write failure");
        REQUIRE(fired);
    }
    REQUIRE(wiz8::open_file(path)->read_text() == "old");
}

// Level-script entry point; exercise its saved-item transfer without loading a level.
void LoadAwayCampChest();

TEST_CASE_METHOD(LoaderFixture, "Camp chest retains every item from saved flat groups",
                 "[loader][items]")
{
    const struct WorldScope {
        std::unique_ptr<World> previous = ReplaceWorld(std::make_unique<World>());
        ~WorldScope() { ReplaceWorld(std::move(previous)); }
    } scope;
    const ScopedRecords records(g_item_records, 2);
    g_item_records[0].flags = W8_ITEM_FLAG_CONTAINER;
    Trigger trigger(GetWorld());
    trigger.world_item_group = std::make_unique<W8WorldItem>();
    auto prop = std::make_unique<Prop>();
    prop->name = "AwayCampChest";
    prop->trigger = &trigger;
    GetWorld()->plsProps.push_back(std::move(prop));

    std::filesystem::create_directory(wiz8::user_path("saves"));
    const auto write_items = [&](bool truncated) {
        W8Chunk chunks;
        REQUIRE(chunks.OpenWrite(wiz8::user_path("saves/CurrentGame.SAV")));
        chunks.WriteGroup(W8ChunkId("LVLS"), [&] {
            chunks.WriteValue(0x26);
            chunks.WriteChunk(W8ChunkId("ITEM"), [&] {
                chunks.WriteValue(2u);
                // The container itself stays in the old level, while its loot is recovered.
                // A non-container head transfers together with all of its following records.
                for (const auto [id, kind, next] : {std::array{10, 0, 1}, std::array{11, 1, 0},
                                                    std::array{20, 1, 1}, std::array{21, 1, 0}}) {
                    wiz8::retail::WorldItem image{};
                    image.runtime_id = id;
                    image.item.iItemNo = kind;
                    image.next = next;
                    if (truncated && id == 21)
                        chunks.WriteBytes(&image, 13);
                    else
                        chunks.WriteValue(image);
                }
            });
        });
        chunks.Close();
    };
    write_items(true);
    LoadAwayCampChest();
    REQUIRE(trigger.world_item_group->contents.empty());
    write_items(false);
    LoadAwayCampChest();
    const auto& contents = trigger.world_item_group->contents;
    REQUIRE(contents.size() == 3);
    REQUIRE(contents[0]->runtime_id == 20);
    REQUIRE(contents[1]->runtime_id == 21);
    REQUIRE(contents[2]->runtime_id == 11);
    for (const auto& item : contents) REQUIRE(item->contents.empty());
}

TEST_CASE_METHOD(LoaderFixture, "Retail waypoint versions load without an editor writer",
                 "[loader][pathing]")
{
    LevelSpatialIndex tree("", nullptr);
    tree.registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    tree.m_spatial.m_bounds.minimum = glm::vec3(0);
    tree.m_spatial.m_node_extent = 1;
    NavigationMesh path;
    path.level_name = "waypoints";
    path.path_node_count = 1;
    path.grid_scale = path.span = 1;
    path.level_bounds.minimum = glm::vec3(0);
    for (const auto version : {1u, 2u}) {
        std::string bytes;
        for (const auto value : {version, 1u, 3u, 2u}) append(bytes, value);
        append(bytes, W8FileWaypoint{});
        append(bytes, W8FileWaypoint{W8_WAYPOINT_CONDITIONAL, 1, {1, 2, 3}});
        append(bytes, W8FileWaypoint{W8_WAYPOINT_CONDITIONAL, 0, {4, 5, 6}});
        for (unsigned index = 0; index < 2; ++index) {
            const W8PathEdge edge{0, static_cast<unsigned short>(index),
                                  static_cast<unsigned short>(index ? 2 : 0), 5.0f, 0};
            if (version == 1) {
                append(bytes, edge.flags);
                append(bytes, edge.destination);
                append(bytes, edge.distance);
                append(bytes, edge.next);
            } else {
                append(bytes, edge);
            }
        }
        store("waypoints.wpt", bytes);
        REQUIRE(path.ReadWaypointFile(tree));
        REQUIRE(path.m_waypoints.size() == 3);
        REQUIRE(path.m_waypoints[1].position == glm::vec3(1, 2, 3));
        REQUIRE(path.m_waypoints[2].position == glm::vec3(4, 5, 6));
        REQUIRE(path.m_waypoints[1].first_edge == 1);
        REQUIRE(path.m_pEdges[1].source == 1);
        REQUIRE(path.m_pEdges[1].destination == 2);
        REQUIRE(path.m_pEdges[1].distance == 5.0f);
        auto* waypoints = path.m_waypoints.data();
        auto* edges = path.m_pEdges.data();
        for (std::size_t end = 0; end < bytes.size(); ++end) {
            store("waypoints.wpt", bytes.substr(0, end));
            REQUIRE_FALSE(path.ReadWaypointFile(tree));
            REQUIRE(path.m_waypoints.data() == waypoints);
            REQUIRE(path.m_pEdges.data() == edges);
        }
    }
}

TEST_CASE_METHOD(LoaderFixture,
                 "Spell database replacement rejects incomplete and oversized inputs",
                 "[loader][records]")
{
    const struct Cleanup {
        ~Cleanup() { ReleaseSpellDatabase(); }
    } cleanup;
    std::filesystem::create_directories(fixture_root / "data/databases");
    W8SpellRuntimeRecord spell{};
    spell.display_name = "Fixture spell";
    spell.duration = 12;
    std::string bytes;
    append(bytes, 2);
    append(bytes, 1u);
    for (int index = 0; index < 2; ++index) {
        bytes.append(0x101, '\0');
        append(bytes, spell);
    }
    store("data/databases/spelltables.dbs", bytes);
    REQUIRE(InitializeSpellDatabase());
    const auto* previous = g_spell_records.data();
    REQUIRE(previous[1].display_name == spell.display_name);
    REQUIRE(previous[1].duration == spell.duration);
    for (auto end : {0u, 4u, 7u, 8u, 264u, unsigned(bytes.size() - 1)}) {
        store("data/databases/spelltables.dbs", bytes.substr(0, end));
        REQUIRE_FALSE(InitializeSpellDatabase());
        REQUIRE(g_spell_records.data() == previous);
        REQUIRE(previous[1].display_name == spell.display_name);
    }
    for (const int count : {0, -1, std::numeric_limits<int>::max()}) {
        std::string invalid;
        append(invalid, count);
        append(invalid, 1u);
        store("data/databases/spelltables.dbs", invalid);
        REQUIRE_FALSE(InitializeSpellDatabase());
        REQUIRE(g_spell_records.data() == previous);
    }
    ReleaseSpellDatabase();
    REQUIRE(g_spell_records.empty());
}

TEST_CASE_METHOD(LoaderFixture, "Path parameters accept complete CRLF and unterminated text lines",
                 "[loader][text]")
{
    std::filesystem::create_directories(fixture_root / "data/monsters");
    store("data/monsters/pathparms.txt", "# path tuning\r\nMIN_ANIMATION_RATE" +
                                             std::string(2048, ' ') +
                                             "0.75\r\nFLY_SWIM_MIN_ANIM_RATE 0.25");
    NavigationMesh pathing;
    REQUIRE(pathing.settings.minimum_animation_rate == 0.75f);
    REQUIRE(pathing.settings.flying_swimming_minimum_animation_rate == 0.25f);
    store("data/monsters/pathparms.txt", "MIN_ANIMATION_RATE 0.1");
    NavigationMesh replacement;
    REQUIRE(replacement.settings.minimum_animation_rate == 0.1f);
    REQUIRE(pathing.settings.minimum_animation_rate == 0.75f);
    REQUIRE(replacement.settings.flying_swimming_minimum_animation_rate == 0.8999999761581421f);
}
struct LocationVariableFixture : LoaderFixture {
    std::unique_ptr<World> previous_world = ReplaceWorld(nullptr);
    LocationVariableFixture() { ClearLocationVariables(); }
    bool load_variables(const std::string& bytes, bool inject_failure = false)
    {
        const auto path = wiz8::asset_path("section.bin");
        W8Chunk chunk;
        REQUIRE(chunk.OpenWrite(path));
        chunk.WriteChunk(W8ChunkId("VARS"), [&] { chunk.WriteBytes(bytes.data(), bytes.size()); });
        // A following chunk must never supply bytes missing from this section.
        chunk.WriteChunk(W8ChunkId("NEXT"), [&] { chunk.WriteValue(std::array<char, 256>{}); });
        chunk.Close();
        REQUIRE(chunk.OpenRead(path));
        bool loaded = false;
        chunk.ForEachChunk([&](unsigned) {
            FileFaults fault(inject_failure ? 5 : 0);
            loaded = LoadLocationVariables(chunk);
            if (inject_failure) REQUIRE(FileFaults::fired);
            return false;
        });
        return loaded;
    }
    ~LocationVariableFixture()
    {
        ClearLocationVariables();
        ReplaceWorld(std::move(previous_world));
    }
};

TEST_CASE_METHOD(LocationVariableFixture,
                 "Location variables retain level-scoped names and retail records",
                 "[loader][records]")
{
    REQUIRE_FALSE(HasLocationVariables());
    ReplaceWorld(std::make_unique<World>(3));
    CreateLocationVar("Switch", 17);
    CreateLocationVar("SWITCH", 99);
    REQUIRE(GetLocationVarValueByName("switch") == 17);
    SetTriggerVariableByName("switch", 23);
    ReplaceWorld(std::make_unique<World>(4));
    REQUIRE(GetLocationVarIDByName("Switch") == -1);
    CreateLocationVar("Switch", -7);
    REQUIRE(HasLocationVariables());

    std::string expected;
    append(expected, std::int32_t{2});
    for (const auto [value, level] : {std::pair{23, 3}, std::pair{31, 4}}) {
        append(expected, std::int32_t{value});
        std::array<char, 128> name{};
        std::strcpy(name.data(), "Switch");
        append(expected, name);
        append(expected, std::int32_t{level});
    }
    {
        auto file = wiz8::open_file(wiz8::asset_path("variables.bin"), wiz8::OpenMode::replace);
        SaveLocationVariables(*file, W8SavedLocationState{{"SWITCH", 31}});
        REQUIRE(GetLocationVarValueByName("Switch") == -7);
    }
    std::ifstream saved(fixture_root / "variables.bin", std::ios::binary);
    REQUIRE(std::string(std::istreambuf_iterator<char>(saved), {}) == expected);
    ClearLocationVariables();
    REQUIRE(load_variables(expected));
    REQUIRE(GetLocationVarValueByName("SWITCH") == 31);
    ReplaceWorld(std::make_unique<World>(3));
    REQUIRE(GetLocationVarValueByName("switch") == 23);
}

TEST_CASE_METHOD(LocationVariableFixture,
                 "Invalid location variable sections leave existing state intact",
                 "[loader][records]")
{
    ReplaceWorld(std::make_unique<World>(5));
    CreateLocationVar("Existing", 42);
    std::string bytes;
    append(bytes, std::int32_t{2});
    for (const char* name : {"First", "Second"}) {
        append(bytes, std::int32_t{9});
        std::array<char, 128> disk_name{};
        std::strcpy(disk_name.data(), name);
        append(bytes, disk_name);
        append(bytes, std::int32_t{5});
    }
    bool inject_failure = false;
    SECTION("Short count")
    {
        bytes.resize(2);
    }
    SECTION("Truncated second record")
    {
        bytes.pop_back();
    }
    SECTION("Negative count")
    {
        bytes.clear();
        append(bytes, std::int32_t{-1});
    }
    SECTION("Unterminated second name")
    {
        bytes.replace(4 + 136 + 4, 128, 128, 'x');
    }
    SECTION("Read failure after first record")
    {
        inject_failure = true;
    }
    REQUIRE_FALSE(load_variables(bytes, inject_failure));
    REQUIRE(GetLocationVarValueByName("Existing") == 42);
    REQUIRE(GetLocationVarIDByName("First") == -1);
    REQUIRE(GetLocationVarIDByName("Second") == -1);
}

TEST_CASE_METHOD(LoaderFixture, "Particle record versions initialize omitted fields",
                 "[loader][records]")
{
    for (const auto [version, size] :
         {std::pair{1, 0x216}, std::pair{2, 0x218}, std::pair{3, 0x21d}, std::pair{4, 0x225}}) {
        W8LevelParticleRecord record{};
        std::strcpy(record.name, "Emitter");
        record.rotation_axis = {0, 1, 0};
        record.particle_count = 2;
        record.material.opacity = 1;
        record.attachment_key = 7;
        record.start_frame = 4;
        record.end_frame = 9;
        std::string bytes;
        append(bytes, std::int32_t{2});
        // The packed stride is odd: the second record is not naturally aligned.
        for (int index = 0; index < 2; ++index) {
            append(bytes, static_cast<std::uint8_t>(version));
            bytes.append(reinterpret_cast<const char*>(&record), size);
        }
        store("particles.bin", bytes);
        auto file = wiz8::open_file(wiz8::asset_path("particles.bin"));
        LevelLoadContext info{nullptr, file.get(), {}};
        WorldScene scene;
        std::vector<std::unique_ptr<ParticleSystem>> particles;
        REQUIRE(ReadWorldParticles(&info, &scene, &particles));
        REQUIRE(particles.size() == 2);
        for (const auto& particle : particles) {
            REQUIRE(particle->particle_count == 2);
            REQUIRE(particle->getName() == "EMITTER");
            REQUIRE(particle->attachment_key == (version < 2 ? -1 : 7));
            REQUIRE(particle->start_frame == (version < 4 ? -1 : 4));
            REQUIRE(particle->end_frame == (version < 4 ? -1 : 9));
        }
    }
}

TEST_CASE_METHOD(LoaderFixture, "Malformed particle sections publish no scene objects",
                 "[loader][records]")
{
    W8LevelParticleRecord first{};
    std::strcpy(first.name, "First");
    first.rotation_axis = {0, 1, 0};
    first.material.opacity = 1;
    auto second = first;
    bool truncate = false;
    std::int32_t count = 2;
    SECTION("Short second record")
    {
        truncate = true;
    }
    SECTION("Unterminated name")
    {
        std::memset(second.name, 'x', sizeof(second.name));
    }
    SECTION("Nonfinite flutter period")
    {
        second.flutter_mode = 1;
        second.flutter_period = std::numeric_limits<float>::quiet_NaN();
    }
    SECTION("Out-of-range flutter period")
    {
        second.flutter_mode = 1;
        second.flutter_period = 0x1p32f;
    }
    SECTION("Negative count")
    {
        count = -1;
    }
    std::string bytes;
    append(bytes, count);
    append(bytes, std::uint8_t{4});
    append(bytes, first);
    append(bytes, std::uint8_t{4});
    append(bytes, second);
    if (truncate) bytes.pop_back();
    store("particles.bin", bytes);
    auto file = wiz8::open_file(wiz8::asset_path("particles.bin"));
    LevelLoadContext info{nullptr, file.get(), {}};
    WorldScene scene;
    std::vector<std::unique_ptr<ParticleSystem>> particles;
    particles.push_back(std::make_unique<ParticleSystem>(nullptr, 0));
    const auto* existing = particles.front().get();
    REQUIRE_FALSE(ReadWorldParticles(&info, &scene, &particles));
    REQUIRE(particles.size() == 1);
    REQUIRE(particles.front().get() == existing);
    REQUIRE(scene.particles.empty());
}

TEST_CASE("Octree particle registration checks the whole batch before publishing",
          "[loader][records]")
{
    LevelSpatialIndex octree("", nullptr);
    octree.registry.collecting_links = true;
    octree.m_ulNumParticles = 1;
    ParticleSystem first(nullptr, 0), second(nullptr, 0);
    const std::array<ParticleSystem*, 2> particles{&first, &second};
    REQUIRE_THROWS(octree.AddLoadedParticles(particles));
    REQUIRE(octree.registry.particles.empty());
    octree.AddLoadedParticles(std::span(particles).first(1));
    REQUIRE(octree.registry.particles == std::vector<ParticleSystem*>{&first});
}

TEST_CASE_METHOD(LoaderFixture, "Cursor scratch replacement validates complete saved records",
                 "[loader][cursor][ownership]")
{
    W8SavedCursorNodes table;
    table.nodes.resize(2);
    std::strcpy(table.nodes[0].name.data(), "first");
    std::strcpy(table.nodes[1].name.data(), "second");
    table.nodes[0].userdata = {std::byte{7}};
    table.nodes[1].userdata = {std::byte{8}};
    std::string bytes;
    append(bytes, 2);
    append(bytes, 2u);
    for (const auto& node : table.nodes) {
        bytes.append(node.name.data(), node.name.size());
        append(bytes, 2);
        bytes.append("ab", 2);
    }
    const auto load = [&](const std::string& data) {
        store("cursor-state.dat", data);
        auto file = wiz8::open_file(wiz8::asset_path("cursor-state.dat"));
        ReadSavePayload(*file, [&](W8Chunk& chunk) { ReadWorldCursorNodeStates(chunk, table); });
    };
    REQUIRE_THROWS(load(bytes.substr(0, bytes.size() - 1)));
    REQUIRE(table.nodes[0].userdata == std::vector{std::byte{7}});
    REQUIRE(table.nodes[1].userdata == std::vector{std::byte{8}});
    REQUIRE_NOTHROW(load(bytes));
    REQUIRE(table.nodes[0].userdata == std::vector{std::byte{'a'}, std::byte{'b'}});
    REQUIRE_NOTHROW(load(bytes));
    const auto original = table.nodes[0].userdata;
    auto invalid = bytes;
    std::fill(invalid.begin() + 8, invalid.begin() + 40, 'x');
    REQUIRE_THROWS(load(invalid));
    invalid = bytes;
    const int negative = -1;
    std::memcpy(invalid.data() + 40, &negative, sizeof(negative));
    REQUIRE_THROWS(load(invalid));
    REQUIRE(table.nodes[0].userdata == original);
}

TEST_CASE_METHOD(LoaderFixture, "Prop loader rejects an unterminated asset name", "[loader][prop]")
{
    std::string bytes;
    append(bytes, static_cast<unsigned char>(7));
    append(bytes, static_cast<unsigned char>(1));
    append(bytes, static_cast<unsigned char>(0));
    for (int i = 0; i < 3; ++i) append(bytes, 0.0f);
    append(bytes, 0u);
    bytes.append(64, 'x');
    store("bad-prop.dat", bytes);
    auto file = wiz8::open_file(wiz8::asset_path("bad-prop.dat"));
    LevelLoadContext info{};
    info.hFile = file.get();
    Prop prop;
    prop.name = "previous";
    REQUIRE_FALSE(prop.LoadProp(&info, &prop));
    REQUIRE(prop.name == "previous");
}

TEST_CASE_METHOD(LoaderFixture, "Trigger action loading rejects incomplete and unterminated text",
                 "[loader][trigger]")
{
    World world;
    std::string bytes;
    append(bytes, 1);
    append(bytes, 1);
    bytes.append(128, 'x');
    bytes.append(256, '\0');
    const auto load = [&](const std::string& data) {
        store("trigger-action.dat", data);
        auto file = wiz8::open_file(wiz8::asset_path("trigger-action.dat"));
        return ReadSavePayload(*file,
                               [&](W8Chunk& chunk) { return LoadTriggerActionData(world, chunk); });
    };
    REQUIRE_FALSE(load(bytes));
    REQUIRE_FALSE(load(bytes.substr(0, bytes.size() - 1)));
    std::fill(bytes.begin() + 8, bytes.begin() + 136, '\0');
    std::fill(bytes.begin() + 136, bytes.end(), 'x');
    REQUIRE_FALSE(load(bytes));
}

TEST_CASE_METHOD(LoaderFixture, "Prop save records decode missing entries within their section",
                 "[loader][records]")
{
    const struct WorldScope {
        std::unique_ptr<World> previous = ReplaceWorld(std::make_unique<World>());
        ~WorldScope() { ReplaceWorld(std::move(previous)); }
    } scope;
    World& world = *GetWorld();
    Prop first;
    WorldItemEntity item; // Items consume the same legacy sequence as props.
    auto prop = std::make_unique<Prop>();
    REQUIRE(prop->legacy_save_id == first.legacy_save_id + 2);
    const int saved_id = prop->legacy_save_id;
    prop->name = "Present";
    auto* present = prop.get();
    world.plsProps.push_back(std::move(prop));
    for (bool named : {false, true}) {
        std::string bytes;
        if (named) {
            append(bytes, 0xDEADD00Du);
            append(bytes, 1u);
        }
        append(bytes, 2u);
        for (bool known : {false, true}) {
            if (named) {
                std::array<char, 64> name{};
                std::strcpy(name.data(), known ? "Present" : "Missing");
                append(bytes, name);
            } else {
                append(bytes, known ? saved_id : saved_id + 1000);
            }
            append(bytes, std::array<unsigned char, 6>{7, 2, 9, 1, 1, 0});
        }
        const auto path = wiz8::asset_path("props.riff");
        W8Chunk chunk;
        REQUIRE(chunk.OpenWrite(path));
        chunk.WriteChunk(W8ChunkId("APST"), [&] { chunk.WriteBytes(bytes.data(), bytes.size()); });
        chunk.Close();
        REQUIRE(chunk.OpenRead(path));
        chunk.ForEachChunk([&](unsigned) {
            LoadWorldProps(GetWorld(), chunk);
            REQUIRE(chunk.RemainingBytes() == 0);
        });
        chunk.Close();
        REQUIRE(present->subcycle == 7);
        REQUIRE(present->first_frame == 2);
        REQUIRE(present->last_frame == 9);
        present->subcycle = 3;
        bytes.pop_back();
        REQUIRE(chunk.OpenWrite(path));
        chunk.WriteChunk(W8ChunkId("APST"), [&] { chunk.WriteBytes(bytes.data(), bytes.size()); });
        chunk.WriteChunk(W8ChunkId("NEXT"), [&] { chunk.WriteValue(0u); });
        chunk.Close();
        REQUIRE(chunk.OpenRead(path));
        chunk.ForEachChunk([&](unsigned) {
            REQUIRE_THROWS(LoadWorldProps(GetWorld(), chunk));
            return false;
        });
        REQUIRE(present->subcycle == 3);
    }
}
