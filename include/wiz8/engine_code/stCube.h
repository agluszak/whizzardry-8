#pragma once
struct W8Chunk;
struct W8World;

#include <array>
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

std::unique_ptr<W8WorldCursorNode> CreateWorldCursorCube();
/* Answer the next table node after `after` whose world-space bounds contain
   `point`; a null `after` starts the walk at the head of the table. */
W8WorldCursorNode* FindWorldCursorNodeAtPoint(W8WorldCursorNode* after, glm::vec3* point);
void ScaleWorldCursorNodeX(W8WorldCursorNode* entry, double scale);
void ScaleWorldCursorNodeY(W8WorldCursorNode* entry, double scale);
void ScaleWorldCursorNodeZ(W8WorldCursorNode* entry, double scale);
void SetWorldCursorNodeColorComponents(W8WorldCursorNode* entry, float red, float green,
                                       float blue);
/* 0x0048DCA0 forwards to the full label painter at 0x0048DCB0. */
void RefreshWorldCursorNodeLabel(W8WorldCursorNode* entry);
/* Return the indexed cursor node, or the first node when the index is past the end. */
W8WorldCursorNode* GetWorldCursorNode(int index);
void AttachWorldCursorNode(W8WorldCursorNode* entry, bool attached);
/* 0x0048F110: copy `name` into the node's 0x20-byte name with a forced
   terminator. */
void SetWorldCursorNodeName(W8WorldCursorNode* entry, const char* name);
void SetWorldCursorNodeColor(W8WorldCursorNode* entry, w8_ulong color);
/* 0x0048E3E0: the world-cursor node under the screen point, or 0. */
W8WorldCursorNode* PickWorldCursorNodeAtScreenPoint(int x, int y);
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
void BuildWorldCursorNodes(W8World& world, W8SavedCursorNodes nodes);
