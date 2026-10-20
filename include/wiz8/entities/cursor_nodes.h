#pragma once
struct W8Chunk;
struct World;

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/mesh_instance.h"

namespace wiz8 {
class File;
}

// A cursor node owns its saved scratch bytes and its labelled cube.
struct W8WorldCursorNode {
    ~W8WorldCursorNode();

    std::unique_ptr<MeshInstance> node;
    unsigned int value_08 = 0;
    int parameters[3]{}; /* label values; slot 2 selects the master-function handler */
    std::vector<std::byte> userdata;
    std::uint32_t color = 0;
    char name[0x20]{};
};

extern const double g_world_cursor_scale;

std::unique_ptr<W8WorldCursorNode> CreateWorldCursorCube();
/* Answer the next table node after `after` whose world-space bounds contain
   `point`; a null `after` starts the walk at the head of the table. */
W8WorldCursorNode* FindWorldCursorNodeAtPoint(W8WorldCursorNode* after, glm::vec3* point);
void SaveWorldCursorNodeStates(wiz8::File& handle);
void SaveWorldCursorNodes(wiz8::File& handle);
struct W8SavedCursorNode {
    std::array<char, 32> name{};
    std::array<int, 3> parameters{};
    glm::vec3 minimum{0.0f}, maximum{0.0f}, location{0.0f};
    unsigned int value = 0;
    std::vector<std::byte> userdata;
};
struct W8SavedCursorNodes {
    int next_serial = 100;
    std::vector<W8SavedCursorNode> nodes;
};
W8SavedCursorNodes ReadWorldCursorNodes(W8Chunk& file);
void ReadWorldCursorNodeStates(W8Chunk& file, W8SavedCursorNodes& nodes);
void BuildWorldCursorNodes(World& world, W8SavedCursorNodes nodes);
