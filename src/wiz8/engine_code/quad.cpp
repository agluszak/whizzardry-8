#include "wiz8/engine_code/quad.h"

#include <cmath>
#include <cstddef>
#include <memory>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "surrender/srMeshModel.h"
#include "surrender/srModelInstance.h"

// GLOBAL: WIZ8 0x0060E5D0
static float g_quad_cell_size = 5500.0f;

// FUNCTION: WIZ8 0x004BE200
std::unique_ptr<W8Quad> BuildWorldQuad(srModelInstance* instance, const glm::vec3& minimum,
                                       const glm::vec3& maximum)
{
    auto quad = std::make_unique<W8Quad>();
    quad->row_count = static_cast<unsigned int>((maximum.x - minimum.x) / g_quad_cell_size) + 1;
    quad->column_count = static_cast<unsigned int>((maximum.z - minimum.z) / g_quad_cell_size) + 1;
    quad->origin = glm::vec2(minimum.x, minimum.z);
    quad->cell_size = g_quad_cell_size;
    quad->cells.resize(static_cast<std::size_t>(quad->row_count) * quad->column_count);

    srMeshModel* model = static_cast<srMeshModel*>(instance->getModel());
    glm::ivec3* polygon_vertices = model->getPolyVertex();
    glm::vec3* vertices = model->getVertexLoc();
    for (int polygon = 0; polygon < model->polygon_count; ++polygon) {
        const glm::ivec3& corners = polygon_vertices[polygon];
        glm::vec3 center = vertices[corners.x] + vertices[corners.y] + vertices[corners.z];
        unsigned int row = static_cast<unsigned int>(
            fabs((center.x * 0.33333334f - minimum.x) / g_quad_cell_size));
        unsigned int column = static_cast<unsigned int>(
            fabs((center.z * 0.33333334f - minimum.z) / g_quad_cell_size));
        if (row < quad->row_count && column < quad->column_count) {
            quad->Cell(row, column).push_back(polygon);
        }
    }
    return quad;
}
