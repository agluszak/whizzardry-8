#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <string>
#include "file_faults.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <ios>
#include <iterator>
#include <memory>
#include <vector>

#include "surrender/srCore.h"
#include "surrender/srModelInstance.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "temporary_directory.h"
#include "LevelFile.h"
#include "ReadMesh.h"
#include "filesystem.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"
#include <fstream>

#include "layouts/world.h"
#include "materials.h"
#include "quad.h"
#include "surrender/srMath.h"


namespace {
template<class T> void Write(wiz8::File& file, const T& value)
{
    file.write(&value, sizeof(value));
}

void WriteMesh(wiz8::File& file, int predecessor, int texture_index = 0)
{
    const std::array<int, 7> header{0, 3, 3, 1, predecessor, -1, 0};
    Write(file, header);
    const std::array<srVector3T<float>, 3> locations{
        srVector3T<float>(0, 0, 0), srVector3T<float>(1, 0, 0), srVector3T<float>(0, 1, 0)};
    Write(file, locations);
    const std::array<glm::vec2, 3> uv{
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0, 1)};
    Write(file, uv);
    const std::array<int, 3> triangle{0, 1, 2};
    Write(file, triangle); // UV indices.
    Write(file, triangle); // Vertex indices.
    Write(file, texture_index); // Texture index.
    const std::array<srVector3T<float>, 3> normals{
        srVector3T<float>(0, 0, 1), srVector3T<float>(0, 0, 1), srVector3T<float>(0, 0, 1)};
    Write(file, normals);
    Write(file, locations); // Static vertex lighting.
    const glm::vec4 equation(0, 0, 1, 0);
    Write(file, equation);
    Write(file, -1);
}

void WriteFixture(const std::filesystem::path& path, bool duplicate_successor, bool invalid_texture = false)
{
    auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
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

void WriteAnimatedFixture(const std::filesystem::path& path)
{
    auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
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
    const std::array<short, 9> first{0, 0, 0, 2, 0, 0, 0, 2, 0};
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
    auto file = wiz8::open_host_file(path);
    W8World world{};
    W8ReadLevelInfo info{&world, file.get(), "", nullptr};
    srModelInstance* first = nullptr;
    REQUIRE((ReadSingleLevelMesh(&info, &first, 0, 0, "animated fixture", true)));
    REQUIRE((first));
    auto* model = static_cast<stMeshModel*>(first->getModel());
    REQUIRE((model->frames.size() == 2));
    REQUIRE((model->GetVertexLocations(0, true, 0)[1].x == 1.0f));
    REQUIRE((model->GetVertexLocations(1, true, 0)[1].x == 3.0f));
    srModelInstance* second = nullptr;
    // Cached models still consume and validate their serialized record.
    file->seek(0, wiz8::SeekOrigin::begin);
    REQUIRE((ReadSingleLevelMesh(&info, &second, 0, 0, "animated fixture", true)));
    REQUIRE((second && second->getModel() == model));
    srModelInstance* truncated = nullptr;
    REQUIRE((!ReadSingleLevelMesh(&info, &truncated, 0, 0, "animated fixture", true)));
    REQUIRE((!truncated));
    delete first;
    delete second;
    REQUIRE((g_mesh_models.empty() && g_decompressed_mesh_bytes == 0));
    ReleaseReadMeshScratch();
    return true;
}

bool LoadCycles(const std::filesystem::path& path)
{
    for (int cycle = 0; cycle != 3; ++cycle) {
        auto file = wiz8::open_host_file(path);
        W8ReadLevelInfo info{};
        W8World world{};
        info.world = &world;
        info.bitmap_folder = "";
        info.hFile = file.get();
        srModelInstance* instances[4]{};
        REQUIRE((ReadMultipleLevelMeshes(&info, instances, 4, "serialized linked mesh")));
        REQUIRE((instances[0] && instances[1] && !instances[2] && !instances[3]));
        auto* head = static_cast<stMeshModel*>(instances[0]->getModel());
        REQUIRE((head->parts.size() == 2));
        REQUIRE((head->parts[0]->head == head && head->parts[1]->head == head));
        REQUIRE((head->getPolyVertex()[0].x == 0 && head->getPolyVertex()[0].z == 2));
        REQUIRE((head->getVertexLoc()[1].x == 1));
        REQUIRE((head->parts[0]->vertex_location_count == 3));
        delete instances[0];
        delete instances[1];
        REQUIRE((g_mesh_models.empty()));
        // Later loads reuse the default snapshot taken by the first read.
        if (cycle == 2) {
            ReleaseOctMeshDefaults();
        }
        ReleaseReadMeshScratch();
        // Released meshes drop their references; only the default snapshot may remain.
        REQUIRE((cycle == 2 ? !g_oct_mesh_default.material
                         : g_oct_mesh_default.material.use_count() == 1));
    }
    return true;
}
}

TEST_CASE("serialized_mesh_test")
{
    REQUIRE(srInit());
    const std::filesystem::path directory = make_temporary_directory("wiz8-mesh-fixture");
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
        auto file = wiz8::open_host_file(path);
        W8ReadLevelInfo info{};
        W8World world{};
        info.world = &world;
        info.bitmap_folder = "";
        info.hFile = file.get();
        srModelInstance* instances[4]{};
        ok = ok && !ReadMultipleLevelMeshes(&info, instances, 4, "invalid linked mesh");
        ok = ok && !instances[0] && !instances[1];
    }
    ReleaseReadMeshScratch();
    ReleaseOctMeshDefaults();
    std::filesystem::remove_all(directory);
    srExit();
    REQUIRE(ok);
    std::puts("ok: serialized mesh loading, linked successors, unloading, reloading and invalid links");
}

TEST_CASE("Animation frame streams retain morph and transform formats")
{
    ScopedTemporaryDirectory directory{"wiz8-animation-frames"};
    const auto input = directory.path / "input.bin", output = directory.path / "output.bin";
    const auto bytes_at = [](const auto& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    for (bool transform : {false, true}) for (bool lod : {false, true}) {
        {
            auto file = wiz8::open_host_file(input, wiz8::OpenMode::replace);
            Write(*file, std::array<unsigned char, 7>{1, 1, 1, 1, 0, 0,
                                                     static_cast<unsigned char>(transform)});
            Write(*file, std::array<char, 50>{});
            Write(*file, char{1}); // Group count.
            if (transform) Write(*file, char{1}); // Transform count.
            Write(*file, char{0}); // Channel.
            Write(*file, char{2}); // Frame count, including frames inside the LOD mesh.
            const std::array<srVector3, 3> vertices{srVector3(0, 0, 0), srVector3(1, 0, 0),
                                                  srVector3(0, 1, 0)};
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
            auto file = wiz8::open_host_file(input);
            REQUIRE(ReadAnimObjFile(file.get(), &animation));
            REQUIRE(file->tell() == static_cast<std::int64_t>(bytes.size()));
        }
        {
            auto file = wiz8::open_host_file(output, wiz8::OpenMode::replace);
            REQUIRE(WriteAnimObjFile(file.get(), &animation)); // Writer releases the loaded record.
        }
        REQUIRE(bytes_at(output) == bytes);
        REQUIRE(animation.pMorphs == nullptr);
        REQUIRE(animation.pTransforms == nullptr);
        // Every incomplete stream must fail and release any partially loaded frames.
        for (std::size_t end = 0; end < bytes.size(); ++end) {
            {
                std::ofstream file(input, std::ios::binary | std::ios::trunc);
                file.write(bytes.data(), end);
            }
            auto file = wiz8::open_host_file(input);
            W8LevelFileAnimObj partial{};
            REQUIRE_FALSE(ReadAnimObjFile(file.get(), &partial));
            REQUIRE(partial.pMorphs == nullptr);
            REQUIRE(partial.pTransforms == nullptr);
        }
    }
}


TEST_CASE("Octree mesh writer consumes arrays in disk order")
{
    ScopedTemporaryDirectory directory{"wiz8-octmesh-writer"};
    const auto expected_path = directory.path / "expected.bin";
    const auto output_path = directory.path / "output.bin";
    const std::array<srVector3T<float>, 3> locations{
        srVector3T<float>(1, 2, 3), srVector3T<float>(4, 5, 6), srVector3T<float>(7, 8, 9)};
    const std::array<glm::vec2, 3> uv{glm::vec2(1, 2), glm::vec2(3, 4), glm::vec2(5, 6)};
    const std::array<int, 3> materials{2, 3, 5};
    const std::array<srVector3i, 1> uv_indices{srVector3i(0, 2, 1)};
    const std::array<srVector3i, 1> vertices{srVector3i(2, 1, 0)};
    const std::array<int, 1> textures{7};
    const std::array<srVector3T<float>, 3> normals{
        srVector3T<float>(0, 0, 1), srVector3T<float>(0, 1, 0), srVector3T<float>(1, 0, 0)};
    const std::array<srVector3T<float>, 3> lights{
        srVector3T<float>(.1f, .2f, .3f), srVector3T<float>(.4f, .5f, .6f), srVector3T<float>(.7f, .8f, .9f)};
    const std::array<glm::vec4, 1> equations{glm::vec4(1, 2, 3, 4)};
    const std::array<float, 3> sun{.25f, .5f, .75f};
    auto copy = []<class T, std::size_t N>(const std::array<T, N>& source) {
        auto* result = static_cast<T*>(std::malloc(sizeof(source)));
        REQUIRE(result);
        std::copy_n(source.data(), N, result);
        return result;
    };
    auto bytes_at = [](const auto& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>{input}, {});
    };
    for (short version : {0, 1, 2}) {
        for (int material : {0, -1}) {
            {
                auto file = wiz8::open_host_file(expected_path, wiz8::OpenMode::replace);
                Write(*file, std::array<int, 7>{4 | (version << 8), 3, 3, 1, 11, 13, material});
                Write(*file, locations); Write(*file, uv);
                if (material < 0) Write(*file, materials);
                Write(*file, uv_indices); Write(*file, vertices); Write(*file, textures);
                Write(*file, normals); Write(*file, lights); Write(*file, equations);
                if (version) Write(*file, sun); // Only the first bank is serialized.
                Write(*file, -1);
            }
            for (bool inject_failure : {false, true}) {
                OctMeshModel mesh;
                mesh.version = version;
                mesh.m_packed_header = 4;
                mesh.m_vertex_count = mesh.m_map_count = 3;
                mesh.m_polygon_count = 1;
                mesh.m_link_index = 11;
                mesh.next_link = 13;
                mesh.m_material_index = material;
                mesh.m_vertex_locations = copy(locations); mesh.m_vertex_map = copy(uv);
                mesh.m_vertex_materials = copy(materials);
                mesh.m_poly_uv_index = copy(uv_indices); mesh.m_poly_vertices = copy(vertices);
                mesh.m_poly_textures = copy(textures); mesh.m_vertex_normals = copy(normals);
                mesh.m_vertex_lights = copy(lights); mesh.m_poly_equations = copy(equations);
                if (version) {
                    mesh.m_sun_lights = static_cast<float**>(std::calloc(version, sizeof(float*)));
                    REQUIRE(mesh.m_sun_lights);
                    for (short index = 0; index < version; ++index) mesh.m_sun_lights[index] = copy(sun);
                }
                auto file = wiz8::open_host_file(output_path, wiz8::OpenMode::replace);
                if (inject_failure) {
                    // Raw texture-array failure occurs after the index arrays have
                    // transferred, while normals, lights, and equations remain owned.
                    FileFaults fault(material < 0 ? 13 : 12);
                    REQUIRE_THROWS(mesh.Write(file.get()));
                    REQUIRE(fault.fired);
                    REQUIRE(mesh.m_poly_textures);
                    REQUIRE(mesh.m_vertex_normals);
                    REQUIRE(mesh.m_vertex_lights);
                    REQUIRE(mesh.m_poly_equations);

                } else {
                    REQUIRE(mesh.Write(file.get()));
                    file->close();
                    REQUIRE(bytes_at(output_path) == bytes_at(expected_path));
                    REQUIRE_FALSE(mesh.m_poly_textures);
                    REQUIRE_FALSE(mesh.m_vertex_normals);
                    REQUIRE_FALSE(mesh.m_vertex_lights);
                    REQUIRE_FALSE(mesh.m_poly_equations);
                    REQUIRE_FALSE(mesh.m_sun_lights);
                }
                REQUIRE_FALSE(mesh.m_vertex_locations);
                REQUIRE_FALSE(mesh.m_vertex_map);
                REQUIRE_FALSE(mesh.m_vertex_materials);
                REQUIRE_FALSE(mesh.m_poly_uv_index);
                REQUIRE_FALSE(mesh.m_poly_vertices);
            }
        }
    }
}
