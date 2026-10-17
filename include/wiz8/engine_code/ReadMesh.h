#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace wiz8 {
class File;
}

#pragma pack(push, 1)
struct W8SerializedMeshFace {
    int vertices[3];
    float texture_coordinates[3][2];
    int material_index;
    unsigned char flags;
};
#pragma pack(pop)

static_assert(sizeof(W8SerializedMeshFace) == 0x29);

class stModelInstance;

// Loaders return detached owned instances; failure returns an empty result.
std::unique_ptr<stModelInstance>
ReadSingleLevelMesh(wiz8::File& file, const std::filesystem::path& texture_directory);
std::vector<std::unique_ptr<stModelInstance>>
ReadMultipleLevelMeshes(wiz8::File& file, std::size_t expected_count,
                        const std::filesystem::path& texture_directory);
// Decode and discard the record, sharing the full loader's wire-format interpretation.
bool ReadSingleLevelMeshHasVertexFrames(wiz8::File& file);
