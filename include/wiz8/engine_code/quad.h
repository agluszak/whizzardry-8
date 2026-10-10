#pragma once

#include <vector>
struct W8QuadObject;

#include "surrender/srMath.h"

#include <algorithm>

class srModelInstance;
class srScene;

struct W8QuadCell {
    std::vector<int>* polygon_indices;
    std::vector<W8QuadObject*>* objects;
    unsigned int dirty_stamp;
    bool occupied;
    unsigned char padding_0d[3];
};

W8_ABI_ASSERT(sizeof(W8QuadCell) == 0x10, "W8QuadCell_must_be_0x10");

struct W8QuadRow {
    W8QuadCell* cells;
    unsigned int count;
};

W8_ABI_ASSERT(sizeof(W8QuadRow) == 0x8, "W8QuadRow_must_be_0x8");

struct W8Quad {
    unsigned int row_count;
    unsigned int column_count;
    glm::vec2 origin;
    float cell_size;
    W8QuadRow* rows;
    unsigned int dirty;
};

W8_ABI_ASSERT(sizeof(W8Quad) == 0x1c, "W8Quad_must_be_0x1c");

void DestroyWorldQuad(W8Quad* quad);
W8Quad* BuildWorldQuad(srModelInstance* instance, int, float minimum_x, float minimum_y,
                       float minimum_z, float maximum_x, float maximum_y, float maximum_z,
                       srScene* scene, int);

/* Pick-angle helpers (GetHeadingAngle, GetElevationAngle, HeadingToTargetCPP,
   ElevationToTargetCPP) are declared in wiz8/engine_code/PolyPick.h. */
