#pragma once

#include <memory>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srNode.h"

glm::vec3 CurrentEnvironmentOffset();

namespace wiz8 {
class File;
}

struct W8World;
struct W8NamedPosition;
class stParticle;

struct W8ReadLevelInfo {
    W8World* world;            /* 0x00 */
    wiz8::File* hFile;                 /* 0x04 */
    const char* bitmap_folder; /* 0x08 */
    const char* mesh_filename; /* 0x0c */
};

unsigned char ReadLevel(W8World* world, wiz8::File* handle, bool use_octree,
                        const char* bitmap_folder);
unsigned char ReadWorldItems(W8ReadLevelInfo* info, W8World* world);
// The destination owns particles; scene attachment borrows them.
unsigned char ReadWorldParticles(W8ReadLevelInfo* info, srNode* scene,
                                 std::vector<std::unique_ptr<stParticle>>* particles);

// Append a complete named-position section; failure leaves the destination unchanged.
bool ReadNamedPositions(wiz8::File* file, std::vector<W8NamedPosition>& positions);

// Camera entries and paths are committed together after the full table loads.
unsigned char ReadWorldCameras(W8ReadLevelInfo* info, W8World* world);
