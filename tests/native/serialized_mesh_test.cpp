#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "file_faults.h"
#include "filesystem.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "scoped_asset_roots.h"
#include "temporary_directory.h"
#include "wiz8/asset_paths.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/render/compiled_mesh.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/visibility_grid.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

namespace {
template<class T> void Write(wiz8::File& file, const T& value)
{
    file.write(&value, sizeof(value));
}

void WriteMesh(wiz8::File& file, int predecessor, int texture_index = 0)
{
    const std::array<int, 7> header{0, 3, 3, 1, predecessor, -1, 0};
    Write(file, header);
    const std::array<std::array<float, 3>, 3> locations{std::array<float, 3>{0, 0, 0},
                                                        std::array<float, 3>{1, 0, 0},
                                                        std::array<float, 3>{0, 1, 0}};
    Write(file, locations);
    const std::array<glm::vec2, 3> uv{glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0, 1)};
    Write(file, uv);
    const std::array<int, 3> triangle{0, 1, 2};
    Write(file, triangle); // UV indices.
    Write(file, triangle); // Vertex indices.
    Write(file, texture_index); // Texture index.
    const std::array<std::array<float, 3>, 3> normals{std::array<float, 3>{0, 0, 1},
                                                      std::array<float, 3>{0, 0, 1},
                                                      std::array<float, 3>{0, 0, 1}};
    Write(file, normals);
    Write(file, locations); // Static vertex lighting.
    const glm::vec4 equation(0, 0, 1, 0);
    Write(file, equation);
    Write(file, -1);
}

void WriteFixture(const std::filesystem::path& path, bool duplicate_successor,
                  bool invalid_texture = false)
{
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    Write(*file, 4u); // Two roots and two successors.
    Write(*file, 2u);
    Write(*file, short{2});
    W8MaterialRecord material{};
    material.version = 1;
    file->write(&material, 0x11a);
    file->write(&material, 0x11a); // Unused material must still have a cache owner.
    Write(*file, -1);
    WriteMesh(*file, -1);
    WriteMesh(*file, -1);
    WriteMesh(*file, 0);
    WriteMesh(*file, duplicate_successor ? 0 : 2, invalid_texture ? 17 : 0);
    Write(*file, -1);
}

void WriteAnimatedFixture(const std::filesystem::path& path, short shift = 0)
{
    auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
    Write(*file, 3); // Format with flags and transform.
    Write(*file, 3); // Vertices.
    Write(*file, 1); // Faces.
    Write(*file, static_cast<unsigned char>(W8_LEVEL_MESH_LOD_VERTICES |
                                            W8_LEVEL_MESH_SHORT_LOD_VERTICES));
    const std::array<float, 10> transform{0, 0, 0, 0, 0, 1, 0, 1, 1, 1};
    Write(*file, transform);
    Write(*file, static_cast<unsigned char>(2));
    Write(*file, short{2});
    Write(*file, 1000.0f);
    const std::array<short, 9> first{shift, 0, 0, 2, 0, 0, 0, 2, 0};
    const std::array<short, 9> second{4, 0, 0, 6, 0, 0, 4, 2, 0};
    Write(*file, first);
    Write(*file, second);
    W8SerializedMeshFace face{};
    face.vertices[0] = 0;
    face.vertices[1] = 1;
    face.vertices[2] = 2;
    Write(*file, face);
    Write(*file, short{1});
    W8MaterialRecord material{};
    material.version = 1;
    file->write(&material, 0x11a);
}

bool LoadAnimated(const std::filesystem::path& path)
{
    auto file = wiz8::open_file(path);
    auto first = ReadSingleLevelMesh(*file, {});
    REQUIRE((first));
    auto* model = static_cast<Mesh*>(first->getModel());
    REQUIRE((model->frames.size() == 2));
    REQUIRE(model->framePosition(0, 1)->x == 1.0f);
    REQUIRE(model->framePosition(1, 1)->x == 3.0f);
    // A cache hit shares geometry but reads this record's own placement.
    {
        auto writer = wiz8::open_file(path, wiz8::OpenMode::update);
        writer->seek(13, wiz8::SeekOrigin::begin); // version/counts/flags
        Write(*writer, std::array<float, 10>{2, 3, 4, .5f, 0, 1, 0, 1, 1, 1});
    }
    file = wiz8::open_file(path);
    auto second = ReadSingleLevelMesh(*file, {});
    REQUIRE((second && second->getModel() == model));
    REQUIRE(first->transform.getLocation() == glm::dvec3(0));
    REQUIRE(second->transform.getLocation() == glm::dvec3(1000, 1500, 2000));
    REQUIRE(second->transform.getRotation() != first->transform.getRotation());
    REQUIRE(first->scene == nullptr);
    REQUIRE(second->scene == nullptr);
    auto truncated = ReadSingleLevelMesh(*file, {});
    REQUIRE_FALSE(truncated);
    REQUIRE(first->getModel() == model);
    first.reset();
    second.reset();
    return true;
}

bool LoadCycles(const std::filesystem::path& path)
{
    for (int cycle = 0; cycle != 3; ++cycle) {
        auto file = wiz8::open_file(path);
        World world{};
        {
            auto instances = ReadMultipleLevelMeshes(*file, 4, {});
            REQUIRE(instances.size() == 4);
            REQUIRE((instances[0] && instances[1] && !instances[2] && !instances[3]));
            REQUIRE(instances[0]->scene == nullptr);
            REQUIRE(instances[1]->scene == nullptr);
            auto* head = static_cast<Mesh*>(instances[0]->getModel());
            REQUIRE((head->parts.size() == 2));
            REQUIRE((head->getPolyVertex()[0].x == 0 && head->getPolyVertex()[0].z == 2));
            REQUIRE((head->getVertexLoc()[1].x == 1));
            REQUIRE((head->parts[0]->vertex_location_count == 3));
            // Transfer exactly once into the scene. The world table only observes.
            WorldScene scene;
            world.psrMeshes.resize(5, nullptr);
            for (std::size_t index = 0; index < instances.size(); ++index) {
                if (instances[index])
                    world.psrMeshes[index] = scene.addStaticMesh(std::move(instances[index]));
            }
            REQUIRE(scene.static_meshes.size() == 2);
            REQUIRE(world.psrMeshes[2] == nullptr);
            REQUIRE(world.psrMeshes[3] == nullptr);
            REQUIRE(world.psrMeshes.back() == nullptr);
        }
    }
    return true;
}
}

TEST_CASE("serialized_mesh_test")
{
    ScopedTemporaryDirectory temporary{"wiz8-mesh-fixture"};
    const auto& directory = temporary.path;
    const auto good = directory / "linked.pvl", bad = directory / "duplicate.pvl";
    const auto invalid = directory / "invalid-texture.pvl";
    const auto truncated = directory / "truncated.pvl";
    WriteFixture(good, false);
    WriteFixture(bad, true);
    WriteFixture(invalid, false, true);
    WriteFixture(truncated, false);
    std::filesystem::resize_file(truncated, std::filesystem::file_size(truncated) - 1);
    const auto animated = directory / "animated.mesh";
    WriteAnimatedFixture(animated);
    bool ok = LoadCycles(good) && LoadAnimated(animated);
    for (const auto& path : {bad, invalid, truncated}) {
        auto file = wiz8::open_file(path);
        auto instances = ReadMultipleLevelMeshes(*file, 4, {});
        ok = ok && instances.empty();
    }
    {
        for (std::size_t size : {0u, 3u, 5u}) {
            auto file = wiz8::open_file(good);
            REQUIRE(ReadMultipleLevelMeshes(*file, size, {}).empty());
        }
        std::size_t operations;
        {
            auto file = wiz8::open_file(good);
            FileFaults trace;
            auto instances = ReadMultipleLevelMeshes(*file, 4, {});
            REQUIRE(instances.size() == 4);
            operations = trace.operations;
        }
        // Probe early framing, middle payload, and final cleanup rather than
        // replaying every read/seek in the same malformed-load path.
        REQUIRE(operations >= 3);
        for (const std::size_t operation : {std::size_t{1}, operations / 2, operations}) {
            auto file = wiz8::open_file(good);
            FileFaults fault(operation);
            REQUIRE(ReadMultipleLevelMeshes(*file, 4, {}).empty());
            REQUIRE(fault.fired);
        }
    }
    {
        // A failed reload must leave no dangling pointers to the old scene roots.
        int destroyed = 0;
        const auto trackedInstance = [&] {
            auto mesh = std::shared_ptr<Mesh>(new Mesh(0, 0), [&](Mesh* value) {
                ++destroyed;
                delete value;
            });
            auto instance = std::make_unique<MeshInstance>();
            instance->setModel(std::move(mesh));
            return instance;
        };
        World world{};
        auto& scene = *world.scene;
        world.update_mesh_source = scene.addStaticMesh(trackedInstance());
        world.psrMeshes = {scene.addStaticMesh(trackedInstance()),
                           scene.addStaticMesh(trackedInstance()), nullptr};
        const auto empty = directory / "empty.lvl";
        wiz8::open_file(empty, wiz8::OpenMode::replace)->close();
        auto file = wiz8::open_file(empty);
        REQUIRE_FALSE(ReadLevel(&world, file.get(), false, ""));
        REQUIRE(destroyed == 3);
        REQUIRE(world.update_mesh_source == nullptr);
        REQUIRE(world.psrMeshes.empty());
        REQUIRE(world.scene->static_meshes.empty());
        REQUIRE_FALSE(ReadLevel(&world, file.get(), false, ""));
        REQUIRE(destroyed == 3);
    }
    REQUIRE(ok);
    std::puts(
        "ok: serialized mesh loading, linked successors, unloading, reloading and invalid links");
}

TEST_CASE("Animated meshes share source geometry and keep playback independent")
{
    ScopedTemporaryDirectory directory{"wiz8-animesh-identity"};
    const auto record = directory.path / "mesh.bin";
    const auto animation = directory.path / "animation.bin";
    WriteAnimatedFixture(record);
    {
        std::ifstream input(record, std::ios::binary);
        std::ofstream output(animation, std::ios::binary);
        output.put(2); // Two vertex frames stored in a single mesh record.
        output.put(0); // First frame index.
        output << input.rdbuf();
    }
    std::unique_ptr<AnimationMesh> first;
    std::unique_ptr<AnimationMesh> second;
    {
        auto input = wiz8::open_file(animation);
        first = ReadAnimatedMesh(*input, {});
        REQUIRE(first);
    }
    {
        auto input = wiz8::open_file(animation);
        second = ReadAnimatedMesh(*input, {});
        REQUIRE(second);
    }
    REQUIRE(first->frames.size() == 1);
    REQUIRE(second->frames.size() == 1);
    REQUIRE(first->frames[0].mesh == second->frames[0].mesh);
    REQUIRE(first->frame_count == 2);
    REQUIRE(second->frame_count == 2);
    REQUIRE(first->bounds.minimum == glm::vec3(0));
    REQUIRE(first->bounds.maximum == glm::vec3(3, 1, 0));
    REQUIRE(first->radius == std::sqrt(10.0f) * 0.5f);
    // Playback belongs to the instance showing a frame, not to the shared mesh.
    MeshInstance shown_first;
    MeshInstance shown_second;
    ShowFrame(shown_first, GetAniMeshFrame(*first, 1));
    ShowFrame(shown_second, GetAniMeshFrame(*second, 0));
    REQUIRE(shown_first.getModel() == shown_second.getModel());
    REQUIRE(shown_first.frame_index == 1);
    REQUIRE(shown_second.frame_index == 0);
}

TEST_CASE("Animated frame indices determine playback order before publication")
{
    ScopedTemporaryDirectory directory{"wiz8-animesh-frame-order"};
    const auto path = directory.path / "animation.bin";
    std::int64_t second_index = 0;
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        Write(*file, static_cast<unsigned char>(2));
        for (const unsigned char frame : {1, 0}) {
            if (frame == 0) second_index = file->tell();
            Write(*file, frame);
            Write(*file, std::array<int, 3>{1, 3, 1});
            const float offset = frame * 4.0f;
            Write(*file, std::array<float, 9>{offset, 0, 0, offset + 1, 0, 0, offset, 1, 0});
            W8SerializedMeshFace face{};
            face.vertices[1] = 1;
            face.vertices[2] = 2;
            Write(*file, face);
            Write(*file, short{1});
            W8MaterialRecord material{};
            material.version = 1;
            file->write(&material, 0x11a);
        }
    }
    auto file = wiz8::open_file(path);
    auto mesh = ReadAnimatedMesh(*file, {});
    REQUIRE(mesh);
    REQUIRE(mesh->frames.size() == 2);
    REQUIRE(GetAniMeshFrame(*mesh, 0).mesh->getBounds().box.minimum.x == 0);
    REQUIRE(GetAniMeshFrame(*mesh, 1).mesh->getBounds().box.minimum.x == 2000);
    {
        auto writer = wiz8::open_file(path, wiz8::OpenMode::update);
        writer->seek(second_index, wiz8::SeekOrigin::begin);
        Write(*writer, static_cast<unsigned char>(1)); // Duplicate frame, missing frame zero.
    }
    file->seek(0, wiz8::SeekOrigin::begin);
    REQUIRE_FALSE(ReadAnimatedMesh(*file, {}));
    REQUIRE(GetAniMeshFrame(*mesh, 0).mesh->getBounds().box.minimum.x == 0);
}

TEST_CASE("Mesh construction finishes material partitions, UV seams and vertex markers")
{
    ScopedTemporaryDirectory directory{"wiz8-mesh-partitions"};
    const auto path = directory.path / "partitions.mesh";
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        Write(*file, std::array<int, 3>{4, 6, 5}); // Version, vertices, faces.
        Write(*file, static_cast<unsigned char>(0));
        Write(*file, std::array<float, 10>{0, 0, 0, 0, 0, 1, 0, 1, 1, 1});
        Write(*file, static_cast<signed char>(2));
        Write(*file, short{10}); // Marker key, source vertex.
        Write(*file, short{0});
        Write(*file, short{20});
        Write(*file, short{3});
        Write(*file, std::array<float, 18>{1, 0, 0, 2, 0, 0, 1, 1, 0, 4, 0, 0, 5, 0, 0, 4, 1, 0});
        W8SerializedMeshFace face{};
        face.vertices[1] = 1;
        face.vertices[2] = 2;
        face.material_index = 1;
        Write(*file, face);
        face.texture_coordinates[0][0] = 0.5f; // One source vertex, different UV.
        Write(*file, face);
        face.material_index = 2; // Same shader, distinct material.
        Write(*file, face);
        face.material_index = 3; // Another shader creates a second part.
        face.vertices[0] = 3;
        face.vertices[1] = 4;
        face.vertices[2] = 5;
        Write(*file, face);
        face.material_index = 4; // Shared render state, distinct geometry sidedness.
        face.vertices[0] = 0;
        face.vertices[1] = 1;
        face.vertices[2] = 2;
        Write(*file, face);
        Write(*file, short{5});
        W8MaterialRecord material{};
        material.version = 1;
        file->write(&material, 0x11a); // Default slot.
        material.opacity = 1;
        material.diffuse.x = 1;
        material.surface_flags = W8_MATERIAL_TWO_SIDED;
        file->write(&material, 0x11a);
        material.diffuse.x = 0;
        material.diffuse.z = 1;
        material.surface_flags = 0;
        file->write(&material, 0x11a);
        material.opacity = 0.5f;
        file->write(&material, 0x11a);
        material.opacity = 1;
        material.diffuse.x = 1;
        material.diffuse.z = 0;
        file->write(&material, 0x11a); // Same renderer material as the two-sided record.
    }
    auto file = wiz8::open_file(path);
    auto instance = ReadSingleLevelMesh(*file, {});
    REQUIRE(instance);
    auto* head = static_cast<Mesh*>(instance->getModel());
    REQUIRE(head->parts.size() == 1);
    auto* part = head->parts[0].get();
    REQUIRE(head->polygon_count == 6); // Two two-sided faces, two single-sided.
    REQUIRE(part->polygon_count == 1);
    REQUIRE(head->vertex_location_count == 15); // Material split and back faces.
    REQUIRE(head->getVertexMaterialTable(0, Mesh::SIDE_FRONT, false));
    REQUIRE(head->FindMappedIndex(10) >= 0);
    REQUIRE(head->getVertexLoc()[head->FindMappedIndex(10)] == glm::vec3(500, 0, 0));
    REQUIRE(head->FindMappedIndex(20) == -1);
    REQUIRE(part->FindMappedIndex(10) == -1);
    REQUIRE(part->FindMappedIndex(20) >= 0);
    REQUIRE(part->getVertexLoc()[part->FindMappedIndex(20)] == glm::vec3(2000, 0, 0));
    // UV seams follow the optimized topology while retaining both authored UVs.
    bool seam = false;
    for (int polygon = 0; polygon < head->polygon_count; ++polygon) {
        const int vertex = head->getPolyVertex()[polygon][0];
        if (head->getVertexLoc()[vertex] != glm::vec3(500, 0, 0)) continue;
        const int uv = head->getPolyUVIndex(0, 0)[polygon][0];
        if (head->getVertexTexCoords(0, 0, 0)[uv].x == 0.5f) seam = true;
    }
    REQUIRE(seam);
    std::weak_ptr<Material> lifetime =
        (*head->getVertexMaterialTable(0, Mesh::SIDE_FRONT, false))[0];
    auto duplicate = std::make_unique<MeshInstance>(*instance);
    instance.reset();
    REQUIRE(duplicate->getModel() == head);
    REQUIRE_FALSE(lifetime.expired());
    duplicate.reset();
    REQUIRE(lifetime.expired());
}

TEST_CASE("Expanded geometry keeps runtime marker indices beyond the wire index range")
{
    ScopedTemporaryDirectory directory{"wiz8-expanded-marker"};
    const auto path = directory.path / "expanded.mesh";
    constexpr int split_faces = 10000;
    constexpr int vertices = split_faces * 3 + 3;
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        Write(*file, std::array<int, 3>{4, vertices, split_faces + 1});
        Write(*file, static_cast<unsigned char>(0));
        Write(*file, std::array<float, 10>{0, 0, 0, 0, 0, 1, 0, 1, 1, 1});
        Write(*file, static_cast<signed char>(1));
        Write(*file, short{10});
        Write(*file, short{0}); // Wire marker points to the first source vertex.
        std::vector<float> coordinates(vertices * 3);
        coordinates[0] = 1;
        file->write(coordinates.data(), coordinates.size() * sizeof(float));
        W8SerializedMeshFace face{};
        face.flags = 4; // Separate back-face vertices expand the runtime index space.
        for (int index = 0; index < split_faces; ++index) {
            for (int corner = 0; corner < 3; ++corner)
                face.vertices[corner] = (index + 1) * 3 + corner;
            Write(*file, face);
        }
        face.flags = 0;
        for (int corner = 0; corner < 3; ++corner) face.vertices[corner] = corner;
        Write(*file, face);
        Write(*file, short{1});
        W8MaterialRecord material{};
        material.version = 1;
        file->write(&material, 0x11a);
    }
    auto file = wiz8::open_file(path);
    auto instance = ReadSingleLevelMesh(*file, {});
    REQUIRE(instance);
    auto* mesh = static_cast<Mesh*>(instance->getModel());
    const int marker = mesh->FindMappedIndex(10);
    REQUIRE(marker >= split_faces * 6);
    REQUIRE(marker < mesh->vertex_location_count);
    REQUIRE(mesh->getVertexLoc()[marker] == glm::vec3(500, 0, 0));
}

TEST_CASE("Geometry sidedness does not split shared renderer material state")
{
    W8MaterialRecord record{};
    record.version = 1;
    record.opacity = 1;
    auto single_sided = LoadMaterial({}, record);
    record.surface_flags = W8_MATERIAL_TWO_SIDED;
    auto two_sided = LoadMaterial({}, record);
    REQUIRE(single_sided.material);
    REQUIRE(single_sided.material == two_sided.material);
    REQUIRE_FALSE(single_sided.two_sided);
    REQUIRE(two_sided.two_sided);
}

TEST_CASE("Mesh reuse distinguishes source files, record offsets and material directories")
{
    ScopedTemporaryDirectory directory{"wiz8-mesh-record-identity"};
    const auto a = directory.path / "a.mesh", b = directory.path / "b.mesh";
    const auto joined = directory.path / "joined.mesh";
    WriteAnimatedFixture(a);
    WriteAnimatedFixture(b, 8);
    {
        std::ifstream first(a, std::ios::binary), second(b, std::ios::binary);
        std::ofstream output(joined, std::ios::binary);
        output << first.rdbuf() << second.rdbuf();
    }
    auto file = wiz8::open_file(joined);
    auto first = ReadSingleLevelMesh(*file, directory.path / "textures-a");
    const auto second_offset = file->tell();
    auto second = ReadSingleLevelMesh(*file, directory.path / "textures-a");
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->getModel() != second->getModel());
    auto* second_model = static_cast<Mesh*>(second->getModel());
    REQUIRE(second_model->framePosition(0, 0)->x == 4);
    file->seek(second_offset, wiz8::SeekOrigin::begin);
    auto repeated = ReadSingleLevelMesh(*file, directory.path / "textures-a");
    REQUIRE(repeated);
    REQUIRE(repeated->getModel() == second->getModel());
    file->seek(second_offset, wiz8::SeekOrigin::begin);
    auto other_directory = ReadSingleLevelMesh(*file, directory.path / "textures-b");
    REQUIRE(other_directory);
    REQUIRE(other_directory->getModel() != second->getModel());
    auto other_file = wiz8::open_file(b);
    auto independent = ReadSingleLevelMesh(*other_file, directory.path / "textures-a");
    REQUIRE(independent);
    REQUIRE(independent->getModel() != second->getModel());
}

TEST_CASE("Texture sharing and lazy reads retain the resolved asset directory")
{
    ScopedTemporaryDirectory directory{"wiz8-texture-identity"};
    const auto a = directory.path / "a", b = directory.path / "b";
    std::filesystem::create_directories(a);
    std::filesystem::create_directories(b);
    for (const auto& [folder, red] : {std::pair{a, 10}, std::pair{b, 200}}) {
        auto file = wiz8::open_file(folder / "skin.tga", wiz8::OpenMode::replace);
        // Uncompressed 1x1, 24-bit TGA with one BGR pixel.
        Write(*file,
              std::array<unsigned char, 21>{0, 0, 2,  0, 0, 0, 0,
                                            0, 0, 0,  0, 0, 1, 0,
                                            1, 0, 24, 0, 0, 0, static_cast<unsigned char>(red)});
    }
    struct RestorePreload {
        bool previous = g_texture_cache_enabled;
        ~RestorePreload() { g_texture_cache_enabled = previous; }
    } restore;
    g_texture_cache_enabled = false;
    ScopedAssetRoots roots({wiz8::path_to_utf8(a), wiz8::path_to_utf8(directory.path / "user")});
    const auto first_path = wiz8::asset_path("skin.tga");
    auto first = LoadTextureFromPath(first_path, nullptr, false);
    auto same = LoadTextureFromPath(first_path, nullptr, false);
    REQUIRE(first == same);
    auto* first_file = ((first.get() && !first.get()->source_path.empty()) ? first.get() : nullptr);
    REQUIRE(first_file);
    REQUIRE_FALSE(first_file->isSurfaceLoaded());
    roots.configure({wiz8::path_to_utf8(b), wiz8::path_to_utf8(directory.path / "user")});
    auto second = LoadTextureFromPath(wiz8::asset_path("skin.tga"), nullptr, false);
    REQUIRE(first != second);
    REQUIRE(first->resolve());
    REQUIRE(second->resolve());
    unsigned char red, green, blue, alpha;
    REQUIRE(SDL_ReadSurfacePixel(first_file->getSurface(), 0, 0, &red, &green, &blue, &alpha));
    REQUIRE(red == 10); // Resolving under root B still reads the pinned file under A.
    auto* second_file =
        ((second.get() && !second.get()->source_path.empty()) ? second.get() : nullptr);
    REQUIRE(second_file);
    REQUIRE(SDL_ReadSurfacePixel(second_file->getSurface(), 0, 0, &red, &green, &blue, &alpha));
    REQUIRE(red == 200);

    // A static head must not make a linked animated part eligible for sharing.
    {
        auto file = wiz8::open_file(a / "cycle.ifl", wiz8::OpenMode::replace);
        constexpr char frames[] = "skin.tga\nskin.tga\n";
        file->write(frames, sizeof(frames) - 1);
    }
    const auto mesh_path = directory.path / "linked-animation.mesh";
    {
        auto file = wiz8::open_file(mesh_path, wiz8::OpenMode::replace);
        Write(*file, std::array<int, 3>{1, 3, 2}); // version, vertices, faces
        Write(*file, std::array<float, 9>{0, 0, 0, 1, 0, 0, 0, 1, 0});
        W8SerializedMeshFace face{};
        face.vertices[1] = 1;
        face.vertices[2] = 2;
        Write(*file, face); // Default, static material in the head.
        face.material_index = 1;
        Write(*file, face);
        Write(*file, short{2});
        W8MaterialRecord material{};
        material.version = 1;
        file->write(&material, 0x11a);
        std::string("cycle.ifl").copy(material.texture_names[0], 9);
        material.opacity = 1;
        file->write(&material, 0x11a);
    }
    auto stream = wiz8::open_file(mesh_path);
    auto animated_first = ReadSingleLevelMesh(*stream, a);
    stream->seek(0, wiz8::SeekOrigin::begin);
    auto animated_second = ReadSingleLevelMesh(*stream, a);
    REQUIRE(animated_first);
    REQUIRE(animated_second);
    auto* head = static_cast<Mesh*>(animated_first->getModel());
    REQUIRE(head->parts.size() == 1);
    REQUIRE_FALSE(MeshHasAnimatedTexture(head));
    REQUIRE(MeshHasAnimatedTexture(head->parts[0].get()));
    REQUIRE(animated_first->getModel() != animated_second->getModel());
}
