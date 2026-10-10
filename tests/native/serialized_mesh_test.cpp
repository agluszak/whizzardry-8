#include "surrender/srCore.h"
#include "surrender/srModelInstance.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "temporary_directory.h"
#include "wiz8/engine_code/World.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>

#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return false; } } while (0)

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
    W8ReadMeshFace face{};
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
    CHECK(ReadSingleLevelMesh(&info, &first, 0, 0, "animated fixture", true));
    CHECK(first);
    auto* model = static_cast<stMeshModel*>(first->getModel());
    CHECK(model->frames.size() == 2);
    CHECK(model->GetVertexLocations(0, true, 0)[1].x == 1.0f);
    CHECK(model->GetVertexLocations(1, true, 0)[1].x == 3.0f);
    srModelInstance* second = nullptr;
    // The first load left the stream at EOF. Reuse must share the cached
    // model without trying to read it again.
    CHECK(ReadSingleLevelMesh(&info, &second, 0, 0, "animated fixture", true));
    CHECK(second && second->getModel() == model);
    delete first;
    delete second;
    CHECK(g_mesh_models.empty() && g_decompressed_mesh_bytes == 0);
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
        CHECK(ReadMultipleLevelMeshes(&info, instances, 4, "serialized linked mesh"));
        CHECK(instances[0] && instances[1] && !instances[2] && !instances[3]);
        auto* head = static_cast<stMeshModel*>(instances[0]->getModel());
        CHECK(head->parts.size() == 2);
        CHECK(head->parts[0]->head == head && head->parts[1]->head == head);
        CHECK(head->getPolyVertex()[0].x == 0 && head->getPolyVertex()[0].z == 2);
        CHECK(head->getVertexLoc()[1].x == 1);
        CHECK(head->parts[0]->vertex_location_count == 3);
        delete instances[0];
        delete instances[1];
        CHECK(g_mesh_models.empty());
        // Later loads reuse the default snapshot taken by the first read.
        if (cycle == 2) {
            ReleaseOctMeshDefaults();
        }
        ReleaseReadMeshScratch();
        // Released meshes drop their references; only the default snapshot may remain.
        CHECK(cycle == 2 ? !g_oct_mesh_default.material
                         : g_oct_mesh_default.material.use_count() == 1);
    }
    return true;
}
}

int main()
{
    if (!srInit()) return 1;
    const std::filesystem::path directory = make_temporary_directory("wiz8-mesh-fixture");
    const auto good = directory / "linked.pvl", bad = directory / "duplicate.pvl";
    const auto invalid = directory / "invalid-texture.pvl";
    WriteFixture(good, false);
    WriteFixture(bad, true);
    WriteFixture(invalid, false, true);
    const auto animated = directory / "animated.mesh";
    WriteAnimatedFixture(animated);
    bool ok = LoadCycles(good) && LoadAnimated(animated);
    for (const auto& path : {bad, invalid}) {
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
    if (!ok) return 1;
    std::puts("ok: serialized mesh loading, linked successors, unloading, reloading and invalid links");
}
