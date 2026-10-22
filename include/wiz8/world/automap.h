#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"

struct W8Chunk;
struct World;

namespace wiz8 {
class File;
}

/* One map note with owned UTF-8 text. */
struct W8AutomapNote {
    glm::vec2 position;
    int layer;
    std::string text;
};

/* The level's map, kept with its world: the level file's cells, which of them the party
   has visited and which of those the map view already reveals, the player's notes, and
   where the map was last looked at. The automap screen borrows it while it is open. */
struct W8Automap {
    float grid_cell_size = 2000.0f;
    std::vector<unsigned int> keys;
    std::unordered_map<unsigned int, int> index;
    std::vector<bool> visited;
    std::vector<bool> lit;
    // Owned one by one: the screen points at the note it edits and the one hovered.
    std::vector<std::unique_ptr<W8AutomapNote>> notes;

    /* The map camera the screen last placed, kept until the level is replaced. The
       screen centres it over the party again when it reopens, at the same height. */
    struct View {
        glm::vec3 position;
        float zoom;
    };
    std::optional<View> view;
    int layer = 0;
    // Which notes show their text: all, the hovered or edited one, or none.
    unsigned int page = 0;
};

/* The box the cell keys count from: the level's clipped octree bounds, or a fixed cube
   for a level without an octree. */
struct AutomapGrid {
    glm::vec3 minimum;
    glm::vec3 maximum;
};
AutomapGrid automapGrid(const World& world);

/* The world position at the centre of a cell. */
glm::vec3 automapCellPosition(const World& world, std::size_t cell);

/* Mark the cell under `position` visited (retrying one cell higher on a miss). Returns
   true only when that mark was newly raised. */
bool VisitAutomapCell(World& world, const glm::vec3& position);

/* Reveal in the world's map view up to `limit` visited cells it does not show yet,
   lighting the static vertices near each. Returns how many it revealed. */
std::size_t RevealAutomapCells(World& world, std::size_t limit = SIZE_MAX);

/* The level file's cell size and cell keys. A level with no cells still gets one zero
   cell, so the visited bits stay valid. */
bool ReadAutomapNodes(wiz8::File& file, W8Automap& automap);

/* The AUTO section of a level's saved state: the visited cells and the notes. */
struct W8SavedAutomapNotes {
    std::vector<bool> visited;
    std::vector<std::unique_ptr<W8AutomapNote>> notes;
};
W8SavedAutomapNotes CaptureAutomapNotes(const W8Automap& automap);
void WriteAutomapNotes(wiz8::File& handle, const W8SavedAutomapNotes& records);
W8SavedAutomapNotes ReadAutomapNotes(W8Chunk& file);
void ApplyAutomapNotes(W8Automap& automap, W8SavedAutomapNotes records);
