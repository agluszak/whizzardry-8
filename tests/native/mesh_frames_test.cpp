#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/mesh.h"
#include "wiz8/render/texture.h"

static_assert(!std::is_copy_constructible_v<Mesh>);
static_assert(!std::is_copy_assignable_v<Mesh>);
static_assert(std::is_nothrow_move_constructible_v<Mesh::Frame>);
static_assert(std::is_nothrow_move_constructible_v<Mesh::Skin>);

TEST_CASE("Mesh frames decode from their compressed tables", "[mesh]")
{
    Mesh mesh(1, 3);
    REQUIRE(mesh.frames.empty());
    REQUIRE(mesh.GetVertex(0) == nullptr);
    REQUIRE(mesh.decodeFrame(0, W8_MESH_FRAME_LOCATIONS).empty());
    REQUIRE_FALSE(mesh.framePosition(0, 0));
    mesh.InitializeVertexFrames(2);
    mesh.vertex_compression_scale = 0.5f;
    REQUIRE(mesh.frames.size() == 2);
    for (const auto& frame : mesh.frames) {
        REQUIRE(frame.compressed_locations.size() == 9);
        REQUIRE(frame.compressed_vertex_normals.size() == 9);
        REQUIRE(frame.compressed_polygon_normals.size() == 3);
        REQUIRE(std::ranges::all_of(frame.compressed_locations,
                                    [](short value) { return value == 0; }));
    }
    *mesh.getPolyVertex() = {0, 1, 2};
    const std::array<short, 9> first{0, 0, 0, 2, 0, 0, 0, 2, 0};
    const std::array<short, 9> second{4, 0, 0, 6, 0, 0, 4, 2, 0};
    auto* compressed = mesh.GetVertex(0);
    std::ranges::copy(first, compressed);
    mesh.InitializeVertexFrames(2); // Repeated initialization retains existing storage.
    REQUIRE(mesh.GetVertex(0) == compressed);
    std::ranges::copy(second, mesh.GetVertex(1));
    mesh.FinalizeVertexFrame(0);
    mesh.FinalizeVertexFrame(1);
    const std::vector<unsigned char> vertex_normals{0, 0, 127, 0, 0, 127, 0, 0, 127};
    REQUIRE(mesh.frames[0].compressed_vertex_normals == vertex_normals);
    REQUIRE(mesh.frames[0].compressed_polygon_normals == std::vector<unsigned char>({0, 0, 127}));

    auto bounds = mesh.GetFrameBounds(1);
    REQUIRE(bounds.minimum == glm::vec3(2.0f, 0.0f, 0.0f));
    REQUIRE(bounds.maximum == glm::vec3(3.0f, 1.0f, 0.0f));
    bounds = mesh.GetFrameBounds(-1);
    REQUIRE((bounds.minimum == glm::vec3(0) && bounds.maximum == glm::vec3(0)));

    const auto still = mesh.frameGeometry(0, 0.0f);
    REQUIRE(still.positions[1] == glm::vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(still.normals[2] == glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(still.polygon_normals == std::vector{glm::vec3(0.0f, 0.0f, 1.0f)});
    REQUIRE(mesh.framePosition(1, 1) == glm::vec3(3.0f, 0.0f, 0.0f));
    REQUIRE_FALSE(mesh.framePosition(1, 3));
    REQUIRE_FALSE(mesh.framePosition(2, 0));

    // Positions interpolate toward the next frame; normals stay this frame's.
    const auto between = mesh.frameGeometry(0, 0.25f);
    REQUIRE(between.positions[0].x == Catch::Approx(0.5f).margin(1e-6));
    REQUIRE(between.positions[1].x == Catch::Approx(1.5f).margin(1e-6));
    REQUIRE(between.normals == still.normals);
    REQUIRE(mesh.frameGeometry(0, 1.0f).positions[0] == glm::vec3(2.0f, 0.0f, 0.0f));
    REQUIRE(mesh.frameGeometry(1, 0.75f).positions == mesh.frameGeometry(1, 0.0f).positions);
    REQUIRE_THROWS_AS(mesh.frameGeometry(2, 0.0f), std::out_of_range);
    REQUIRE(std::ranges::equal(first, std::span(mesh.GetVertex(0), 9)));
}

TEST_CASE("Mesh frame decoding covers the compressed value ranges", "[mesh]")
{
    Mesh mesh(0, 1);
    mesh.InitializeVertexFrames(1);
    mesh.vertex_compression_scale = 0.25f;
    auto* source = mesh.GetVertex(0);
    source[0] = -32768;
    source[1] = 32767;
    source[2] = -1;
    REQUIRE(mesh.decodeFrame(0, W8_MESH_FRAME_LOCATIONS) ==
            std::vector{glm::vec3(-8192.0f, 8191.75f, -0.25f)});
    mesh.frames[0].compressed_vertex_normals = {128, 255, 127};
    const auto normals = mesh.decodeFrame(0, W8_MESH_FRAME_VERTEX_NORMALS);
    REQUIRE(normals[0].x == -1.0f);
    REQUIRE(normals[0].y == Catch::Approx(-1.0f / 127.0f).margin(1e-6));
    REQUIRE(normals[0].z == 1.0f);
    REQUIRE(mesh.decodeFrame(0, W8_MESH_FRAME_POLYGON_NORMALS).empty());
}

static int destroyed_textures = 0;
static std::shared_ptr<Texture> countingTexture()
{
    return {new Texture, [](Texture* texture) {
                ++destroyed_textures;
                delete texture;
            }};
}

static bool skins_and_mapped_vertices()
{
    const int destroyed = destroyed_textures;
    {
        std::shared_ptr<Texture> blank = countingTexture();
        std::shared_ptr<Texture> visible = countingTexture();
        blank->setName("bLaNk-body");
        visible->setName(std::string(600, 'V')); // No fixed-size texture-name copies.

        {
            Mesh mesh(3, 1);
            auto* base = mesh.getPolygonTextureTable(0, 0, true);
            (*base)[0] = blank;
            (*base)[1] = visible;
            std::string cycle(400, 'c');
            std::string first_name = cycle + '0';
            auto* first = mesh.createSkin(first_name);
            REQUIRE(first);
            first_name[0] = 'x';
            REQUIRE(first->name == cycle + '0'); // Owned, not the caller's mutable name.
            REQUIRE_FALSE(mesh.createSkin(cycle + '0'));
            auto* second = mesh.createSkin("second1", first);
            REQUIRE(second);
            REQUIRE(second->textures[0] == blank);
            auto* third = mesh.createSkin("third2", second);
            REQUIRE(third);
            REQUIRE(!mesh.GetActivePolygons(second, false));
            auto selection = mesh.GetActivePolygons(second, true);
            REQUIRE((selection && selection->size() == 2 && (*selection)[0] == 1 &&
                     (*selection)[1] == 2));
            REQUIRE(mesh.GetActivePolygons(second, true).value().data() ==
                    selection.value().data());
            REQUIRE(mesh.findSkin("SECOND1") == second);
            const std::array<char, 9> bounded_name{'t', 'h', 'i', 'r', 'd', '2', 'X', 'X', 'X'};
            REQUIRE(mesh.findSkin(std::string_view(bounded_name.data(), 6)) == third);
            REQUIRE(mesh.createSkin("added3"));
            // Adding another skin moves neither tables nor their selections.
            REQUIRE(mesh.findSkin("second1") == second);
            REQUIRE(mesh.GetActivePolygons(second, true).value().data() ==
                    selection.value().data());
            REQUIRE(mesh.createSkin(""));
            REQUIRE(mesh.createSkin(cycle + '1'));
            REQUIRE(!mesh.GetActivePolygons(nullptr, false));
            selection = mesh.GetActivePolygons(nullptr, true);
            REQUIRE((selection && selection->size() == 2 && (*selection)[0] == 1 &&
                     (*selection)[1] == 2));
            (*base)[1] = blank;
            (*base)[2] = blank;
            auto* all_blank = mesh.createSkin("all-blank");
            REQUIRE(all_blank);
            const auto empty_skin = mesh.GetActivePolygons(all_blank, true);
            REQUIRE(empty_skin);
            REQUIRE(empty_skin.value().empty());
            mesh.SetMappedVertex(0, 10);
            REQUIRE(mesh.FindMappedIndex(10) == 0);
            mesh.SetMappedVertex(7, 10);
            mesh.SetMappedVertex(9, 10);
            REQUIRE((mesh.FindMappedIndex(10) == 9 && mesh.mapped_vertices.size() == 1));
            REQUIRE_FALSE(mesh.FindMappedIndex(-1));
            REQUIRE_FALSE(mesh.FindMappedIndex(11));
            auto& part = *mesh.parts.emplace_back(std::make_unique<Mesh>(0, 1));
            part.SetMappedVertex(0, 11);
            const auto child_marker = findMappedVertex(&mesh, 11);
            REQUIRE(child_marker);
            REQUIRE(child_marker->mesh == &part);
            REQUIRE(child_marker->index == 0);
            const auto root_marker = findMappedVertex(&mesh, 10);
            REQUIRE(root_marker);
            REQUIRE(root_marker->mesh == &mesh);
            REQUIRE_FALSE(findMappedVertex(&mesh, -1));
        }
        REQUIRE(blank.use_count() == 1);
    }
    REQUIRE(destroyed_textures == destroyed + 2);
    return true;
}

TEST_CASE("Mesh skins and mapped vertices", "[mesh][ownership]")
{
    REQUIRE(skins_and_mapped_vertices());
}
