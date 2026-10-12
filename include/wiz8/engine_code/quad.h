#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "surrender/srMath.h"
#include "glm/ext/vector_float2.hpp"

class srModelInstance;

/* A grid of square cells over the level mesh, each listing the polygons whose
   centroid falls inside it. Rows run along x, columns along z. */
struct W8Quad {
    std::uint32_t row_count;
    std::uint32_t column_count;
    glm::vec2 origin; /* minimum x and z */
    float cell_size;
    std::vector<std::vector<std::int32_t>> cells; /* row-major polygon indices */

    std::vector<std::int32_t>& Cell(std::uint32_t row, std::uint32_t column)
    {
        return cells[row * column_count + column];
    }
};

std::unique_ptr<W8Quad> BuildWorldQuad(srModelInstance* instance, const srVector3T<float>& minimum,
                                       const srVector3T<float>& maximum);

/* Pick-angle helpers (GetHeadingAngle, GetElevationAngle, HeadingToTargetCPP,
   ElevationToTargetCPP) are declared in wiz8/engine_code/PolyPick.h. */
