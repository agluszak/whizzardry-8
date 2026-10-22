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

/* The AUTO section of a level's saved state: the visited cells, when the map has
   cells, then the notes between two 0xF00DF00D signatures. */
struct W8SavedAutomapNotes {
    std::vector<bool> visited;
    std::vector<std::unique_ptr<W8AutomapNote>> notes;

    static W8SavedAutomapNotes read(W8Chunk& file);
    // Write failures throw.
    void write(wiz8::File& file) const;
};

/* The box the cell keys count from: the level's clipped octree bounds, or a fixed cube
   for a level without an octree. */
struct AutomapGrid {
    glm::vec3 minimum;
    glm::vec3 maximum;
};

/* A level's map, owned by its world: the level file's cells, which of them the party
   has visited and which of those the world's map view already reveals, the player's
   notes, and where the map was last looked at. The automap screen edits the notes and
   the view while it is open. */
class Automap {
public:
    explicit Automap(World& world) : world_(world) {}
    Automap(const Automap&) = delete;
    Automap& operator=(const Automap&) = delete;

    /* Read the level file's cell size and cell keys. A level with no cells still gets
       one zero cell, so the visited bits stay valid. */
    bool readCells(wiz8::File& file);

    AutomapGrid grid() const;
    std::size_t cellCount() const { return keys_.size(); }
    bool visited(std::size_t cell) const { return cell < visited_.size() && visited_[cell]; }
    // The world position at the centre of a cell.
    glm::vec3 cellPosition(std::size_t cell) const;

    /* Mark the cell under `position` visited (retrying one cell higher on a miss).
       Returns true only when that mark was newly raised. */
    bool visit(const glm::vec3& position);
    /* Reveal in the world's map view up to `limit` visited cells it does not show yet,
       lighting the static vertices near each. Returns how many it revealed. */
    std::size_t reveal(std::size_t limit = SIZE_MAX);

    W8SavedAutomapNotes capture() const;
    /* Take the saved notes, and the saved visited cells when they describe this map's
       cells. The map view reveals them over the following frames. */
    void restore(W8SavedAutomapNotes records);

    // Owned one by one: the screen points at the note it edits and the one hovered.
    const std::vector<std::unique_ptr<W8AutomapNote>>& notes() const { return notes_; }
    // A new note with an empty text cursor, or null when the map holds all it can.
    W8AutomapNote* addNote(glm::vec2 position, int layer);
    void eraseNote(const W8AutomapNote* note);

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

private:
    // The one-based cell under a grid-relative position; 0 and 1 name no cell it keeps.
    int cellAt(glm::vec3 relative) const;

    World& world_;
    float grid_cell_size_ = 2000.0f;
    std::vector<unsigned int> keys_;
    std::unordered_map<unsigned int, int> index_;
    std::vector<bool> visited_;
    std::vector<bool> lit_;
    std::vector<std::unique_ptr<W8AutomapNote>> notes_;
};
