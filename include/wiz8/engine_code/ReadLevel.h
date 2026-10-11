#pragma once

#include <vector>

namespace wiz8 { class File; }

struct W8World;
class srNode;
class stParticle;

struct W8ReadLevelInfo {
    W8World* world;            /* 0x00 */
    wiz8::File* hFile;                 /* 0x04 */
    const char* bitmap_folder; /* 0x08 */
    const char* mesh_filename; /* 0x0c */
};

unsigned char ReadLevel(W8World* world, wiz8::File* handle, bool use_octree, const char* bitmap_folder);
unsigned char ReadWorldParticles(W8ReadLevelInfo* info, srNode* scene,
                                 std::vector<stParticle*>* particles);
