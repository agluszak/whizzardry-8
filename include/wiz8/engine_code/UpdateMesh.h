#pragma once

#include <array>
#include <vector>

#include "glm/ext/vector_int2.hpp"

struct W8World;

void UpdateWorldMesh(W8World* world);
void UpdateWorldMeshFromQuads(W8World* world);
void UpdateWorldOctree(W8World* world);

std::vector<glm::ivec2> visibleQuadCells(const W8World& world);
std::vector<glm::ivec2> rasterizeQuadTriangle(const glm::ivec2& dimensions,
                                              std::array<glm::ivec2, 3> points);
