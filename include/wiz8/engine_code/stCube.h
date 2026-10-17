#pragma once
struct W8Chunk;

#include <cstddef>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"

struct W8World;
class srNode;
namespace wiz8 {
class File;
}

// A cursor node owns its saved scratch bytes; scene references are borrowed.
class W8WorldCursorNode {
public:
    virtual ~W8WorldCursorNode() = default;
    /* 0x0048D050: copy the node's world location out; answers 0 when the node
       chain is absent. */
    unsigned char GetLocation(glm::vec3* position);
    srNode* node; /* 0x04 */
    unsigned int value_08;
    int parameters[3];   /* 0x0c, 0x10, 0x14 */
    std::vector<std::byte> userdata;
    w8_ulong color; /* 0x20 */
    char name[0x20];
};

extern const double g_world_cursor_scale;

W8WorldCursorNode* CreateWorldCursorCube(void);
void DrawWorldCursorNodeLabel(W8WorldCursorNode* entry);
void MoveWorldCursorNode(W8WorldCursorNode* entry, glm::vec3* position);
void DestroyWorldCursorCube(W8WorldCursorNode* entry);
int GetWorldCursorNodeParameter(W8WorldCursorNode* entry, int index);
void SetWorldCursorNodeParameter(W8WorldCursorNode* entry, int index, int value);
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
unsigned char SaveWorldCursorNodeStates(wiz8::File& handle);
unsigned char SaveWorldCursorNodes(wiz8::File& handle);
unsigned int LoadWorldCursorNodes(W8Chunk& handle);
unsigned int LoadWorldCursorNodeStates(W8Chunk& handle);
