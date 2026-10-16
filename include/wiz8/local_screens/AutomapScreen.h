#pragma once

#include <string>

#include "glm/ext/vector_float3.hpp"

extern float g_automap_grid_cell_size;

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
extern int g_automap_zoom_mode;

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
bool LoadAutomapNotes(wiz8::File& handle);
unsigned char GetAutomapPositionUnderCursor(glm::vec3* position);
void SetAutomapToolCursor(int tool);
W8AutomapNote* FindAutomapNoteUnderCursor(void);
void CreateAutomapMarkerSprites(void);
void RenderAutomapMarkers(void);
void CreateAutomapButtons(void);

unsigned char ReadAutomapNodes(wiz8::File* hFile);
unsigned char AutomapScreenEnter(void);
void AutomapScreenFrame(void);
unsigned char AutomapScreenLeave(int leaving);
unsigned char AutomapScreenFinalize(void);
/* Full-screen dismiss: left-up after a held press leaves the automap. */
float GetAutomapGridCellSize(void);
void SetAutomapGridCellSize(float value);
/* Packs a world position into an automap cell key. */
unsigned int AutomapNodeKey(const glm::vec3* position);
bool AutomapLevelIsLarge(void);
