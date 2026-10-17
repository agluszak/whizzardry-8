#pragma once
struct W8Chunk;

#include <cstddef>
#include <memory>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/engine_code/stModelInstance.h"

namespace wiz8 {
class File;
}

// A cursor node owns its saved scratch bytes and its labelled cube.
struct W8WorldCursorNode {
    ~W8WorldCursorNode();

    std::unique_ptr<stModelInstance> node;
    unsigned int value_08 = 0;
    int parameters[3]{}; /* label values; slot 2 selects the master-function handler */
    std::vector<std::byte> userdata;
    w8_ulong color = 0;
    char name[0x20]{};
};

extern const double g_world_cursor_scale;
extern std::vector<std::unique_ptr<W8WorldCursorNode>> g_world_cursor_nodes;

std::unique_ptr<W8WorldCursorNode> CreateWorldCursorCube();
/* Answer the next table node after `after` whose world-space bounds contain
   `point`; a null `after` starts the walk at the head of the table. */
W8WorldCursorNode* FindWorldCursorNodeAtPoint(W8WorldCursorNode* after, glm::vec3* point);
void SaveWorldCursorNodeStates(wiz8::File& handle);
void SaveWorldCursorNodes(wiz8::File& handle);
void LoadWorldCursorNodes(W8Chunk& handle);
unsigned int LoadWorldCursorNodeStates(W8Chunk& handle);
