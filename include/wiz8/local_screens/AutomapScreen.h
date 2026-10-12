#pragma once

#include <cstdint>
#include <string>

extern float g_automap_grid_cell_size;

#include <vector>
#include <memory>

#include "input.h"
#include "wiz8/layouts/main_game_screen.h"
#include "glm/ext/vector_float2.hpp"

namespace wiz8 { class File; }
template <class T> class srVector3T;

/* One map note with owned UTF-8 text. */
struct W8AutomapNote {
    glm::vec2 position;
    std::int32_t layer;
    std::string text;
};

extern W8MainUiMode g_ui_mode_current;
extern W8MainUiMode g_ui_mode_saved;
void SetCurrentAutomapUiMode(W8MainUiMode value);
void SetSavedAutomapUiMode(W8MainUiMode value);

class W8ButtonWidget;

extern std::vector<std::unique_ptr<W8ButtonWidget>> g_automap_buttons;
extern std::int32_t g_automap_zoom_mode;

extern bool g_mipe_menu_active;
extern bool g_mipe_active;

bool HasAutomapLayer(std::int32_t layer);
void RestoreAutomapCameraPosition(void);
bool CanUseCurrentAutomapTool(void);

/* Recompute the automap's visible world bounds from lit cells. */
void UpdateAutomapBounds(void);
/* When the automap dirty flag is set, light a batch of pending
   visited cells through the table-1 vertex lights, then clear the flag once
   the batch finds nothing left. */
void RefreshDirtyAutomap(void);
/* Pack `position` into a cell key, mark it visited if known, and
   return 1 only when that mark was newly set (retry one cell higher on miss). */
bool AutomapHasCellAt(const srVector3T<float>* position);

void ResetAutomapView(void);
bool SaveAutomapNotes(wiz8::File& handle);
bool LoadAutomapNotes(wiz8::File& handle);
bool GetAutomapPositionUnderCursor(srVector3T<float>* position);
void SetAutomapToolCursor(std::int32_t tool);
W8AutomapNote* FindAutomapNoteUnderCursor(void);
void CreateAutomapMarkerSprites(void);
void RenderAutomapMarkers(void);
void CreateAutomapButtons(void);

std::uint8_t ReadAutomapNodes(wiz8::File* hFile);
bool AutomapScreenEnter(void);
void AutomapScreenFrame(void);
bool AutomapScreenLeave(std::int32_t leaving);
bool AutomapScreenFinalize(void);
/* Full-screen dismiss: left-up after a held press leaves the automap. */
bool AutomapBackgroundRegionEvent(const InputEvent* event, struct W8Region* region);
float GetAutomapGridCellSize(void);
void SetAutomapGridCellSize(float value);
/* Packs a world position into an automap cell key. */
std::uint32_t AutomapNodeKey(const srVector3T<float>* position);
bool AutomapLevelIsLarge(void);
