#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "LevelFile.h"
#include "ReadMesh.h"
#include "file_faults.h"
#include "filesystem.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "layouts/world.h"
#include "materials.h"
#include "quad.h"
#include "scoped_asset_roots.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "temporary_directory.h"
#include "wiz8/asset_paths.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stTextureFile.h"

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
    auto* model = static_cast<stMeshModel*>(first->getModel());
    REQUIRE((model->frames.size() == 2));
    REQUIRE((model->GetVertexLocations(0, true, 0)[1].x == 1.0f));
    REQUIRE((model->GetVertexLocations(1, true, 0)[1].x == 3.0f));
    // A cache hit shares geometry but reads this record's own placement.
    {
        auto writer = wiz8::open_file(path, wiz8::OpenMode::update);
        writer->seek(13, wiz8::SeekOrigin::begin); // version/counts/flags
        Write(*writer, std::array<float, 10>{2, 3, 4, .5f, 0, 1, 0, 1, 1, 1});
    }
    file = wiz8::open_file(path);
    auto second = ReadSingleLevelMesh(*file, {});
    REQUIRE((second && second->getModel() == model));
    REQUIRE(first->getLocation() == glm::dvec3(0));
    REQUIRE(second->getLocation() == glm::dvec3(1000, 1500, 2000));
    REQUIRE(second->getRotation() != first->getRotation());
    REQUIRE(first->getParent() == nullptr);
    REQUIRE(second->getParent() == nullptr);
    auto truncated = ReadSingleLevelMesh(*file, {});
    REQUIRE_FALSE(truncated);
    REQUIRE(first->getModel() == model);
    first.reset();
    second.reset();
    REQUIRE((g_mesh_models.empty() && g_decompressed_mesh_bytes == 0));
    return true;
}

bool LoadCycles(const std::filesystem::path& path)
{
    for (int cycle = 0; cycle != 3; ++cycle) {
        auto file = wiz8::open_file(path);
        W8World world{};
        {
            auto instances = ReadMultipleLevelMeshes(*file, 4, {});
            REQUIRE(instances.size() == 4);
            REQUIRE((instances[0] && instances[1] && !instances[2] && !instances[3]));
            REQUIRE(instances[0]->getParent() == nullptr);
            REQUIRE(instances[1]->getParent() == nullptr);
            auto* head = static_cast<stMeshModel*>(instances[0]->getModel());
            REQUIRE((head->parts.size() == 2));
            REQUIRE((head->parts[0]->head == head && head->parts[1]->head == head));
            REQUIRE((head->getPolyVertex()[0].x == 0 && head->getPolyVertex()[0].z == 2));
            REQUIRE((head->getVertexLoc()[1].x == 1));
            REQUIRE((head->parts[0]->vertex_location_count == 3));
            // Transfer exactly once into the scene. The world table only observes.
            srScene scene(nullptr);
            world.psrMeshes.resize(5, nullptr);
            for (std::size_t index = 0; index < instances.size(); ++index) {
                world.psrMeshes[index] = scene.adoptChild(std::move(instances[index]));
            }
            REQUIRE(world.psrMeshes[0]->getParent() == &scene);
            REQUIRE(world.psrMeshes[1]->getParent() == &scene);
            REQUIRE(world.psrMeshes.back() == nullptr);
        }
        REQUIRE(g_mesh_models.empty());
        // Later loads reuse the default snapshot taken by the first read.
        if (cycle == 2) {
            ReleaseOctMeshDefaults();
        }
        // Released meshes drop their references; only the default snapshot may remain.
        REQUIRE((cycle == 2 ? !g_oct_mesh_default.material
                            : g_oct_mesh_default.material.use_count() == 1));
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
            REQUIRE(g_mesh_models.empty());
        }
    }
    {
        // A failed reload must leave no dangling pointers to the old scene roots.
        struct TrackedInstance : srModelInstance {
            int& destroyed;
            TrackedInstance(srNode* parent, int& destroyed)
                : srModelInstance(parent), destroyed(destroyed)
            {
            }
            ~TrackedInstance() override { ++destroyed; }
        };
        int destroyed = 0;
        W8World world{};
        world.static_scene = std::make_unique<srScene>();
        auto& scene = *world.static_scene;
        world.level = scene.adoptChild(std::make_unique<stLevel>(nullptr));
        world.update_mesh_source =
            world.level->adoptChild(std::make_unique<TrackedInstance>(nullptr, destroyed));
        world.psrMeshes = {
            world.level->adoptChild(std::make_unique<TrackedInstance>(nullptr, destroyed)),
            world.level->adoptChild(std::make_unique<TrackedInstance>(nullptr, destroyed)),
            nullptr};
        const auto empty = directory / "empty.lvl";
        wiz8::open_file(empty, wiz8::OpenMode::replace)->close();
        auto file = wiz8::open_file(empty);
        REQUIRE_FALSE(ReadLevel(&world, file.get(), false, ""));
        REQUIRE(destroyed == 3);
        REQUIRE(world.update_mesh_source == nullptr);
        REQUIRE(world.psrMeshes.empty());
        REQUIRE(world.level->getChildCount() == 0);
        REQUIRE_FALSE(ReadLevel(&world, file.get(), false, ""));
        REQUIRE(destroyed == 3);
    }
    ReleaseOctMeshDefaults();
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
    std::unique_ptr<W8AniMesh> first;
    std::unique_ptr<W8AniMesh> second;
    {
        auto input = wiz8::open_file(animation);
        first = ReadAnimatedMesh(*input, {}, true);
        REQUIRE(first);
    }
    {
        auto input = wiz8::open_file(animation);
        second = ReadAnimatedMesh(*input, {}, true);
        REQUIRE(second);
    }
    REQUIRE(first->meshes.size() == 1);
    REQUIRE(second->meshes.size() == 1);
    REQUIRE(first->meshes[0]->getModel() == second->meshes[0]->getModel());
    REQUIRE(first->frame_count == 2);
    REQUIRE(second->frame_count == 2);
    REQUIRE(GetAniMeshFrame(second.get(), 0)->frame_index == 0);
    auto* first_frame = GetAniMeshFrame(first.get(), 1);
    REQUIRE(first_frame->frame_index == 1);
    REQUIRE(second->meshes[0]->frame_index == 0);
    REQUIRE(GetAniMeshFrame(second.get(), 0)->frame_index == 0);
    REQUIRE(first_frame->frame_index == 1);
    REQUIRE(UnloadAniMesh(first.get(), true));
    REQUIRE(GetAniMeshFrame(first.get(), 0)); // Reopen the same physical source/offset.
    REQUIRE(first->meshes[0]->getModel() == second->meshes[0]->getModel());
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
    auto* second_model = static_cast<stMeshModel*>(second->getModel());
    REQUIRE(second_model->GetVertexLocations(0, true, 0)[0].x == 4);
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
    auto* first_file = dynamic_cast<stTextureFile*>(first.get());
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
    auto* second_file = dynamic_cast<stTextureFile*>(second.get());
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
    auto* head = static_cast<stMeshModel*>(animated_first->getModel());
    REQUIRE(head->parts.size() == 1);
    REQUIRE_FALSE(MeshHasAnimatedTexture(head));
    REQUIRE(MeshHasAnimatedTexture(head->parts[0].get()));
    REQUIRE(animated_first->getModel() != animated_second->getModel());
}

TEST_CASE("Animation frame streams retain morph and transform formats")
{
    ScopedTemporaryDirectory directory{"wiz8-animation-frames"};
    const auto input = directory.path / "input.bin", output = directory.path / "output.bin";
    const auto bytes_at = [](const auto& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    for (bool transform : {false, true})
        for (bool lod : {false, true}) {
            {
                auto file = wiz8::open_file(input, wiz8::OpenMode::replace);
                Write(*file, std::array<unsigned char, 7>{1, 1, 1, 1, 0, 0,
                                                          static_cast<unsigned char>(transform)});
                Write(*file, std::array<char, 50>{});
                Write(*file, char{1}); // Group count.
                if (transform) Write(*file, char{1}); // Transform count.
                Write(*file, char{0}); // Channel.
                Write(*file, char{2}); // Frame count, including frames inside the LOD mesh.
                const std::array<glm::vec3, 3> vertices{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0),
                                                        glm::vec3(0, 1, 0)};
                const int serialized_frames = !transform && lod ? 1 : 2;
                for (int frame = 0; frame < serialized_frames; ++frame) {
                    Write(*file, static_cast<unsigned char>(frame));
                    Write(*file, std::array<int, 3>{3, 3, 1});
                    Write(*file, static_cast<unsigned char>(lod ? W8_LEVEL_MESH_LOD_VERTICES : 0));
                    Write(*file, std::array<float, 10>{0, 0, 0, 0, 0, 1, 0, 1, 1, 1});
                    if (lod) {
                        Write(*file, char{0});
                        Write(*file, short{2});
                        Write(*file, vertices);
                    }
                    Write(*file, vertices);
                    W8SerializedMeshFace face{};
                    face.vertices[1] = 1;
                    face.vertices[2] = 2;
                    Write(*file, face);
                    Write(*file, short{1});
                    W8MaterialRecord material{};
                    material.version = 1;
                    file->write(&material, 0x11a);
                }
                if (transform) Write(*file, std::array<char, 14>{}); // Empty path AI record.
            }
            const std::string bytes = bytes_at(input);
            W8LevelFileAnimObj animation{};
            {
                auto file = wiz8::open_file(input);
                REQUIRE(ReadAnimObjFile(*file, animation));
                REQUIRE(file->tell() == static_cast<std::int64_t>(bytes.size()));
            }
            {
                auto file = wiz8::open_file(output, wiz8::OpenMode::replace);
                REQUIRE(WriteAnimObjFile(*file, animation));
            }
            REQUIRE(bytes_at(output) == bytes);
        // Every incomplete stream must fail and leave the record empty.
            for (std::size_t end = 0; end < bytes.size(); ++end) {
                {
                    std::ofstream file(input, std::ios::binary | std::ios::trunc);
                    file.write(bytes.data(), end);
                }
                auto file = wiz8::open_file(input);
                W8LevelFileAnimObj partial{};
                REQUIRE_FALSE(ReadAnimObjFile(*file, partial));
                REQUIRE(partial.morphs.empty());
                REQUIRE(partial.transforms.empty());
            }
        }
}
