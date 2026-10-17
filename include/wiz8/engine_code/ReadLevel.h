#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srNode.h"

extern glm::vec3 g_environment_offset;

namespace wiz8 {
class File;
}

struct W8World;
struct W8NamedPosition;
class stParticle;

struct W8ReadLevelInfo {
    W8World* world = nullptr;
    wiz8::File* hFile = nullptr;
    std::filesystem::path bitmap_folder;
};

unsigned char ReadLevel(W8World* world, wiz8::File* handle, bool use_octree,
                        const std::filesystem::path& texture_directory);
unsigned char ReadWorldItems(W8ReadLevelInfo* info, W8World* world);
// The destination owns particles; scene attachment borrows them.
unsigned char ReadWorldParticles(W8ReadLevelInfo* info, srNode* scene,
                                 std::vector<std::unique_ptr<stParticle>>* particles);

// Append a complete named-position section; failure leaves the destination unchanged.
bool ReadNamedPositions(wiz8::File* file, std::vector<W8NamedPosition>& positions);

// Camera entries and paths are committed together after the full table loads.
unsigned char ReadWorldCameras(W8ReadLevelInfo* info, W8World* world);
