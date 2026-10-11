#include <algorithm>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <utility>
#include <vector>
#include <memory>

#include "wiz8/engine_code/UpdateMesh.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/quad.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/sr_api.h"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srCamera.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "surrender/srMath.h"

/* Convert the mesh view half-angle from degrees to radians. */
// GLOBAL: WIZ8 0x005ED168
const float g_mesh_view_degrees_to_radians = 0.01745329424738884f;

/* Camera-visible quad-cell coordinates and count: rows[]/cells[] index pairs
   into W8Quad, filled by CollectViewQuadCells and consumed by
   UpdateWorldMeshFromQuads. */
// GLOBAL: WIZ8 0x0065BEB8
static w8_long g_visible_quad_rows[20000];
// GLOBAL: WIZ8 0x0066F738
static w8_long g_visible_quad_columns[20000];
// GLOBAL: WIZ8 0x00682FB8
static w8_long g_visible_quad_count;

static int ScanQuadTriangleBase(W8World* world, w8_long x1, w8_long y1, w8_long x2, w8_long y2, w8_long x3,
                                w8_long y3, w8_long* x_list, w8_long* y_list, w8_long* count);
static void ScanQuadTriangleTop(W8World* world, w8_long x1, w8_long y1, w8_long x2, w8_long y2, w8_long x3,
                                w8_long y3, w8_long* x_list, w8_long* y_list, w8_long* count,
                                int skip_boundary);
static void RasterizeQuadTriangle(W8World* world, w8_long x1, w8_long y1, w8_long x2, w8_long y2, w8_long x3,
                                  w8_long y3, w8_long* x_list, w8_long* y_list, w8_long* count);

/* Builds the camera view triangle in quad-cell space: the camera's own cell is
   the apex and the two far points sit view-distance ahead along the camera
   forward vector, optionally yawed by +/- (g_mesh_view_half_angle_degrees * g_mesh_view_degrees_to_radians).
   The two far vertices bound the visible wedge around the camera heading. */
// FUNCTION: WIZ8 0x004BA530
void CollectViewQuadCells(W8World* world, w8_long* x_list, w8_long* y_list, w8_long* count)
{
    const W8Quad& quad = *world->quads;
    float cell_size = quad.cell_size;

    w8_long cam_x = static_cast<w8_long>((world->camera->getLocationX() - quad.origin.x) / cell_size);
    w8_long cam_y = static_cast<w8_long>((world->camera->getLocationZ() - quad.origin.y) / cell_size);

    glm::mat3 rotation;
    world->camera->getRotation(rotation);

    srVector3T<float> direction(0.0f, 0.0f, world->render_range);
    glm::mat3 work = rotation;
    double angle = -g_mesh_view_half_angle_degrees * g_mesh_view_degrees_to_radians;
    work *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
    direction = work * direction.glmValue();
    w8_long edge1_x = cam_x + static_cast<w8_long>(direction.x / cell_size);
    w8_long edge1_y = cam_y + static_cast<w8_long>(direction.z / cell_size);

    direction.Set(0.0f, 0.0f, world->render_range);
    work = rotation;
    angle = g_mesh_view_half_angle_degrees * g_mesh_view_degrees_to_radians;
    work *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
    direction = work * direction.glmValue();
    w8_long edge2_x = cam_x + static_cast<w8_long>(direction.x / cell_size);
    w8_long edge2_y = cam_y + static_cast<w8_long>(direction.z / cell_size);

    *count = 0;
    RasterizeQuadTriangle(world, cam_x, cam_y, edge1_x, edge1_y, edge2_x, edge2_y, x_list, y_list,
                          count);
}

static bool AppendQuadRow(w8_long left, w8_long right, w8_long max_x, w8_long row, w8_long* x_list, w8_long* y_list,
                          w8_long* count)
{
    if (left > 0) {
        --left;
    }
    if (right < max_x) {
        ++right;
    }
    for (w8_long x = left; x <= right; ++x) {
        x_list[*count] = x;
        y_list[*count] = row;
        ++*count;
    }
    return left <= right;
}

struct QuadRowEdges {
    float left;
    float right;
    float left_step;
    float right_step;
};

static int ScanQuadTriangleRows(W8World* world, w8_long first_row, w8_long last_row,
                                QuadRowEdges edges, bool clip_x, bool skip_boundary,
                                w8_long* x_list, w8_long* y_list, w8_long* count)
{
    if (first_row < 0) {
        edges.left -= first_row * edges.left_step;
        edges.right -= first_row * edges.right_step;
        first_row = 0;
    }
    last_row = std::min(last_row, static_cast<w8_long>(world->quads->column_count) - 1);
    const w8_long max_x = static_cast<w8_long>(world->quads->row_count) - 1;
    int emitted = 0;
    for (w8_long row = first_row; row <= last_row; ++row) {
        if (!clip_x && skip_boundary && row == first_row) {
            edges.left += edges.left_step;
            edges.right += edges.right_step;
            continue;
        }
        w8_long left = static_cast<w8_long>(edges.left);
        w8_long right = static_cast<w8_long>(edges.right);
        if (clip_x) {
            edges.left += edges.left_step;
            edges.right += edges.right_step;
            if (left < 0) {
                left = 0;
                if (right < 0) continue;
            }
            if (right > max_x) {
                right = max_x;
                if (left > max_x) continue;
            }
            if (skip_boundary && row == first_row) continue;
            if (AppendQuadRow(left, right, max_x, row, x_list, y_list, count)) emitted = 1;
        } else {
            AppendQuadRow(left, right, max_x, row, x_list, y_list, count);
            edges.left += edges.left_step;
            edges.right += edges.right_step;
            emitted = 1;
        }
    }
    return emitted;
}

/* Scan a flat top edge converging to the apex, optionally skipping its first row. */
// FUNCTION: WIZ8 0x004BA800
static void ScanQuadTriangleTop(W8World* world, w8_long x1, w8_long y1, w8_long x2, w8_long y2, w8_long x3,
                                w8_long y3, w8_long* x_list, w8_long* y_list, w8_long* count, int skip_boundary)
{
    if (x2 < x1) std::swap(x1, x2);
    const float height = static_cast<float>(y3 - y1);
    const w8_long max_x = static_cast<w8_long>(world->quads->row_count) - 1;
    const bool clip_x = x1 < 0 || x1 > max_x || x2 < 0 || x2 > max_x || x3 < 0 || x3 > max_x;
    ScanQuadTriangleRows(world, y1, y3,
        {static_cast<float>(x1), x2 + 0.5f, (x3 - x1) / height, (x3 - x2) / height},
        clip_x, skip_boundary != 0, x_list, y_list, count);
}

/* Scan an apex fanning out to a flat base, returning whether a row survived clipping. */
// FUNCTION: WIZ8 0x004BAA00
static int ScanQuadTriangleBase(W8World* world, w8_long x1, w8_long y1, w8_long x2, w8_long y2, w8_long x3,
                                w8_long y3, w8_long* x_list, w8_long* y_list, w8_long* count)
{
    if (x3 < x2) std::swap(x2, x3);
    const float height = static_cast<float>(y3 - y1);
    const w8_long max_x = static_cast<w8_long>(world->quads->row_count) - 1;
    const bool clip_x = x1 < 0 || x1 > max_x || x2 < 0 || x2 > max_x || x3 < 0 || x3 > max_x;
    return ScanQuadTriangleRows(world, y1, y3,
        {static_cast<float>(x1), x1 + 0.5f, (x2 - x1) / height, (x3 - x1) / height},
        clip_x, false, x_list, y_list, count);
}

/* Rasterizes a cell-space triangle into (x,y) coordinate pairs: sorts the
   vertices by y, then scans a flat-top half, a flat-bottom half, or both halves
   split at the middle vertex's row. */
// FUNCTION: WIZ8 0x004BABE0
static void RasterizeQuadTriangle(W8World* world, w8_long x1, w8_long y1, w8_long x2, w8_long y2, w8_long x3,
                                  w8_long y3, w8_long* x_list, w8_long* y_list, w8_long* count)
{
    if ((x1 == x2 && x2 == x3) || (y1 == y2 && y2 == y3)) {
        return;
    }
    w8_long temp;
    if (y2 < y1) {
        temp = x1;
        x1 = x2;
        x2 = temp;
        temp = y1;
        y1 = y2;
        y2 = temp;
    }
    if (y3 < y1) {
        temp = x1;
        x1 = x3;
        x3 = temp;
        temp = y1;
        y1 = y3;
        y3 = temp;
    }
    if (y3 < y2) {
        temp = x2;
        x2 = x3;
        x3 = temp;
        temp = y2;
        y2 = y3;
        y3 = temp;
    }
    if (y1 == y2) {
        ScanQuadTriangleTop(world, x1, y1, x2, y2, x3, y3, x_list, y_list, count, 0);
        return;
    }
    if (y2 == y3) {
        ScanQuadTriangleBase(world, x1, y1, x2, y2, x3, y3, x_list, y_list, count);
        return;
    }
    w8_long split = static_cast<w8_long>(static_cast<float>(x3 - x1) * (y2 - y1) / (y3 - y1));
    int emitted =
        ScanQuadTriangleBase(world, x1, y1, x1 + split, y2, x2, y2, x_list, y_list, count);
    ScanQuadTriangleTop(world, x2, y2, x1 + split, y2, x3, y3, x_list, y_list, count, emitted != 0);
}

// FUNCTION: WIZ8 0x004BAD40
void UpdateWorldMeshFromQuads(W8World* world)
{
    if (world == 0) {
        srAssertFail("pWorld", "C:\\Projects\\Wizardry 8\\Engine Code\\UpdateMesh.cpp", 0x21f, 0);
    }
    if (world->quads == 0) {
        srAssertFail("pWorld->Tree.pQuads", "C:\\Projects\\Wizardry 8\\Engine Code\\UpdateMesh.cpp",
                     0x220, 0);
    }
    srMeshModel* mesh = static_cast<srMeshModel*>(world->update_mesh_source->getModel());
    W8Quad& quad = *world->quads;
    g_visible_quad_count = 0;
    CollectViewQuadCells(world, g_visible_quad_rows, g_visible_quad_columns, &g_visible_quad_count);
    w8_long polygon_count = 0;
    for (w8_long index = 0; index < g_visible_quad_count; ++index) {
        polygon_count += quad.Cell(g_visible_quad_rows[index], g_visible_quad_columns[index]).size();
    }
    mesh->setActivePolygonCount(polygon_count);
    w8_ulong* table = mesh->getActivePolygonTable(1);
    for (w8_long index = 0; index < g_visible_quad_count; ++index) {
        for (int polygon : quad.Cell(g_visible_quad_rows[index], g_visible_quad_columns[index])) {
            *table++ = polygon;
        }
    }
    mesh->setDirty(srMeshModel::DIRTY_TRI_MESH);
}

// FUNCTION: WIZ8 0x004BAF50
void UpdateWorldOctree(W8World* world)
{
    world->octree->UpdateCameraVisibility();
}

// FUNCTION: WIZ8 0x004baf60
void UpdateWorldMesh(W8World* world)
{
    srModelInstance* source;
    srMeshModel* mesh;
    w8_ulong* table;
    w8_long polygon_count;
    w8_ulong index;

    if (world == 0) {
        srAssertFail("pWorld", "C:\\Projects\\Wizardry 8\\Engine Code\\UpdateMesh.cpp", 0x2a2, 0);
    }
    source = world->update_mesh_source;
    if (source != 0) {
        mesh = static_cast<srMeshModel*>(source->getModel());
        polygon_count = mesh->polygon_count;
        mesh->setActivePolygonCount(polygon_count);
        table = mesh->getActivePolygonTable(1);
        for (index = 0; static_cast<w8_long>(index) < polygon_count; ++index) {
            table[index] = index;
        }
        mesh->setDirty(srMeshModel::DIRTY_TRI_MESH);
    }
}
