#include "wiz8/render/mesh_updates.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/visibility_grid.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/world.h"

/* Convert the mesh view half-angle from degrees to radians. */
const float g_mesh_view_degrees_to_radians = 0.01745329424738884f;

static bool AppendQuadRow(int left, int right, int max_x, int row, std::vector<glm::ivec2>& cells)
{
    if (left > 0) --left;
    if (right < max_x) ++right;
    for (int x = left; x <= right; ++x) cells.emplace_back(x, row);
    return left <= right;
}

struct QuadRowEdges {
    glm::vec2 position;
    glm::vec2 step;
};

static bool ScanQuadTriangleRows(const glm::ivec2& dimensions, int first_row, int last_row,
                                 QuadRowEdges edges, bool clip_x, bool skip_boundary,
                                 std::vector<glm::ivec2>& cells)
{
    if (first_row < 0) {
        edges.position -= float(first_row) * edges.step;
        first_row = 0;
    }
    last_row = std::min(last_row, dimensions.y - 1);
    const int max_x = dimensions.x - 1;
    bool emitted = false;
    for (int row = first_row; row <= last_row; ++row) {
        if (!clip_x && skip_boundary && row == first_row) {
            edges.position += edges.step;
            continue;
        }
        int left = static_cast<int>(edges.position.x);
        int right = static_cast<int>(edges.position.y);
        if (clip_x) {
            edges.position += edges.step;
            if (left < 0) {
                left = 0;
                if (right < 0) continue;
            }
            if (right > max_x) {
                right = max_x;
                if (left > max_x) continue;
            }
            if (skip_boundary && row == first_row) continue;
            emitted |= AppendQuadRow(left, right, max_x, row, cells);
        } else {
            AppendQuadRow(left, right, max_x, row, cells);
            edges.position += edges.step;
            emitted = true;
        }
    }
    return emitted;
}

// Scan either a flat edge converging to its tip or a tip fanning out to the edge.
static bool ScanQuadTriangleHalf(const glm::ivec2& dimensions, glm::ivec2 left, glm::ivec2 right,
                                 const glm::ivec2& tip, bool flat_top, bool skip_boundary,
                                 std::vector<glm::ivec2>& cells)
{
    if (right.x < left.x) std::swap(left, right);
    const int first_row = flat_top ? left.y : tip.y;
    const int last_row = flat_top ? tip.y : left.y;
    const glm::ivec2 start = flat_top ? glm::ivec2(left.x, right.x) : glm::ivec2(tip.x);
    const glm::ivec2 end = flat_top ? glm::ivec2(tip.x) : glm::ivec2(left.x, right.x);
    const float height = static_cast<float>(last_row - first_row);
    const bool clip_x = left.x < 0 || left.x >= dimensions.x || right.x < 0 ||
                        right.x >= dimensions.x || tip.x < 0 || tip.x >= dimensions.x;
    return ScanQuadTriangleRows(
        dimensions, first_row, last_row,
        {glm::vec2(start) + glm::vec2(0, 0.5f), glm::vec2(end - start) / height}, clip_x,
        skip_boundary, cells);
}

std::vector<glm::ivec2> rasterizeQuadTriangle(const glm::ivec2& dimensions,
                                              std::array<glm::ivec2, 3> points)
{
    auto& [first, middle, last] = points;
    if ((first.x == middle.x && middle.x == last.x) || (first.y == middle.y && middle.y == last.y))
        return {};
    if (middle.y < first.y) std::swap(first, middle);
    if (last.y < first.y) std::swap(first, last);
    if (last.y < middle.y) std::swap(middle, last);
    std::vector<glm::ivec2> cells;
    if (first.y == middle.y) {
        ScanQuadTriangleHalf(dimensions, first, middle, last, true, false, cells);
    } else if (middle.y == last.y) {
        ScanQuadTriangleHalf(dimensions, middle, last, first, false, false, cells);
    } else {
        const int split = static_cast<int>(static_cast<float>(last.x - first.x) *
                                           (middle.y - first.y) / (last.y - first.y));
        const glm::ivec2 edge(first.x + split, middle.y);
        const bool emitted =
            ScanQuadTriangleHalf(dimensions, edge, middle, first, false, false, cells);
        ScanQuadTriangleHalf(dimensions, middle, edge, last, true, emitted, cells);
    }
    return cells;
}

// The camera cell is the apex; the far endpoints bound its yawed visibility wedge.
std::vector<glm::ivec2> visibleQuadCells(const World& world)
{
    const MeshVisibilityGrid& quad = *world.quads;
    const float cell_size = quad.cell_size;
    const glm::vec3 eye = cameraLocation(world);
    const glm::ivec2 camera_cell(static_cast<int>((eye.x - quad.origin.x) / cell_size),
                                 static_cast<int>((eye.z - quad.origin.y) / cell_size));
    const auto rotation = cameraRotation(world);
    const auto edge = [&](float angle) {
        const auto direction = rotation *
                               glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0))) *
                               glm::vec3(0, 0, world.render_range);
        return camera_cell + glm::ivec2(static_cast<int>(direction.x / cell_size),
                                        static_cast<int>(direction.z / cell_size));
    };
    const auto left = edge(-g_mesh_view_half_angle_degrees * g_mesh_view_degrees_to_radians);
    const auto right = edge(g_mesh_view_half_angle_degrees * g_mesh_view_degrees_to_radians);
    return rasterizeQuadTriangle({quad.row_count, quad.column_count}, {camera_cell, left, right});
}

void UpdateWorldMeshFromQuads(World* world)
{
    assert(world != 0);
    assert(world->quads != 0);
    Mesh* mesh = static_cast<Mesh*>(world->update_mesh_source->getModel());
    MeshVisibilityGrid& quad = *world->quads;
    const auto cells = visibleQuadCells(*world);
    std::int32_t polygon_count = 0;
    for (const auto& cell : cells) polygon_count += quad.Cell(cell.x, cell.y).size();
    mesh->setActivePolygonCount(polygon_count);
    std::uint32_t* table = mesh->getActivePolygonTable(1);
    for (const auto& cell : cells) {
        for (int polygon : quad.Cell(cell.x, cell.y)) {
            *table++ = polygon;
        }
    }
}

void UpdateWorldOctree(World* world)
{
    world->compiledIndex()->UpdateCameraVisibility();
}
