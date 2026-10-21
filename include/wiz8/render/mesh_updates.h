#pragma once

#include <array>
#include <vector>

#include "glm/ext/vector_int2.hpp"

struct World;

void UpdateWorldMeshFromQuads(World* world);
void UpdateWorldOctree(World* world);

std::vector<glm::ivec2> visibleQuadCells(const World& world);
std::vector<glm::ivec2> rasterizeQuadTriangle(const glm::ivec2& dimensions,
                                              std::array<glm::ivec2, 3> points);
