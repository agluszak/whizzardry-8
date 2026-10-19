#pragma once

#include <memory>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"

class MeshInstance;

/* A grid of square cells over the level mesh, each listing the polygons whose
   centroid falls inside it. Rows run along x, columns along z. */
struct MeshVisibilityGrid {
    unsigned int row_count;
    unsigned int column_count;
    glm::vec2 origin; /* minimum x and z */
    float cell_size;
    std::vector<std::vector<int>> cells; /* row-major polygon indices */

    std::vector<int>& Cell(unsigned int row, unsigned int column)
    {
        return cells[row * column_count + column];
    }
};

std::unique_ptr<MeshVisibilityGrid> BuildWorldQuad(MeshInstance* instance, const glm::vec3& minimum,
                                                   const glm::vec3& maximum);

/* Pick-angle helpers (GetHeadingAngle, GetElevationAngle, HeadingToTargetCPP,
   ElevationToTargetCPP) are declared in wiz8/render/picking.h. */
