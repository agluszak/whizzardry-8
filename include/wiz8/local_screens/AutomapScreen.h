#pragma once
struct W8Chunk;

#include <string>

#include "glm/ext/vector_float3.hpp"

struct W8AutomapCells;
struct W8World;

enum class W8AutomapTool { Select = 0, AddNote = 2, EraseNote = 3 };

#include <array>
#include <memory>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "input.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/ui_layer.h"

namespace wiz8 {
class File;
}

/* One map note with owned UTF-8 text. */
struct W8AutomapNote {
    glm::vec2 position;
    int layer;
    std::string text;
};

/* The sixteen chrome buttons; a slot stays empty when its artwork is missing. */
struct AutomapButtons {
    UiLayer input;
    std::array<std::unique_ptr<W8ButtonWidget>, 16> buttons;
};
extern std::unique_ptr<AutomapButtons> g_automap_buttons;

bool HasAutomapLayer(int layer);
void RestoreAutomapCameraPosition(void);

/* Recompute the automap's visible world bounds from lit cells. */
void UpdateAutomapBounds(void);
/* When the automap dirty flag is set, light a batch of pending
   visited cells through the table-1 vertex lights, then clear the flag once
   the batch finds nothing left. */
void RefreshDirtyAutomap(void);
/* Pack `position` into a cell key, mark it visited if known, and
   return 1 only when that mark was newly set (retry one cell higher on miss). */
bool AutomapHasCellAt(const glm::vec3* position);

void ResetAutomapView(void);
bool SaveAutomapNotes(wiz8::File& handle);
struct W8SavedAutomapNotes {
    std::vector<bool> visited;
    std::vector<std::unique_ptr<W8AutomapNote>> notes;
};
W8SavedAutomapNotes ReadAutomapNotes(W8Chunk& file, std::size_t cells);
void ApplyAutomapNotes(W8World& world, W8SavedAutomapNotes notes);
unsigned char GetAutomapPositionUnderCursor(glm::vec3* position);
void SetAutomapToolCursor(W8AutomapTool tool);
W8AutomapNote* FindAutomapNoteUnderCursor(void);
void CreateAutomapMarkerSprites(void);
void RenderAutomapMarkers(void);
void CreateAutomapButtons(void);

unsigned char ReadAutomapNodes(wiz8::File* hFile, W8AutomapCells& cells);
bool AutomapScreenEnter(void);
void AutomapScreenFrame(void);
bool AutomapScreenLeave(int leaving);
void AutomapScreenFinalize(void);
/* Packs a world position into an automap cell key. */
unsigned int AutomapNodeKey(const glm::vec3* position);
