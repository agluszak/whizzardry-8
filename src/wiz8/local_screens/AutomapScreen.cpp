#include <SDL3/SDL_pixels.h>
#include "glm/ext/vector_double3.hpp"
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <memory>
#include <SDL3/SDL_rect.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)
#include "glm/ext/vector_float2.hpp"
#include "image.h"
#include "utf8/checked.h"
#include "wiz8/unicode.h"
#include "wiz8/containers.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/packed_bits.h"
#include "wiz8/index_algorithms.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/mipe.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/srClipPlane.h"
#include "surrender/srModelInstance.h"
#include "surrender/srCamera.h"
#include "surrender/srScene.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/UpdateMesh.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stScript.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/float_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "surrender/srColorSurface.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/utility.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/item_video_object_vector.h"
#include "wiz8/xstatus.h"
#include "wiz8/regions.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/fonts.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "input.h"
#include "wiz8/filesystem.h"
#include "Font.h"
#include "surrender/srMeshModel.h"
#include "surrender/srMath.h"
#include "AnimRep.hpp"
#include "Item.h"
#include "OctPreTree.h"
#include "compat/native.h"
#include "environment_colour.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "himage.h"
#include "layouts/item_instance.h"
#include "layouts/main_game_screen.h"
#include "layouts/world.h"
#include "local_code/Factions.h"
#include "mgs_commands.h"
#include "sight_state.h"
#include "surrender/srFilter.h"
#include "surrender/srFlags.h"
#include "surrender/srNode.h"
#include "surrender/srPixelConvert.h"
#include "surrender/srTextureIFace.h"

/* Lifecycle record 8; the automap screen. Its original screen-class name is
   unknown, so the existing compilation boundary is retained. */

// GLOBAL: WIZ8 0x0068F258
static std::vector<std::unique_ptr<W8AutomapNote>> g_automap_notes;

// GLOBAL: WIZ8 0x0068f104
bool g_mipe_menu_active;
// GLOBAL: WIZ8 0x0068f105
bool g_mipe_active;

/* Lifecycle record 8's own state, all of it released by the finalizer below and
   nothing here naming what any of it holds. The note list is created by this record's initializer at 0x0057E5D0. */

/* One bit per automap cell: raised when the party camera enters the cell. */
// GLOBAL: WIZ8 0x0068F288
std::vector<bool> g_automap_visited_cells;
/* One bit per visited cell already turned into vertex lights. */
// GLOBAL: WIZ8 0x0068F28C
std::vector<bool> g_automap_lit_cells;
/* Packed cell keys read from the level file, indexed by cell number. */
// GLOBAL: WIZ8 0x0068F280
std::vector<unsigned int> g_automap_cell_keys;

/* Packed cell key -> cell number + 1. */
// GLOBAL: WIZ8 0x0068F284
std::unordered_map<unsigned int, int> g_automap_cell_index;
// GLOBAL: WIZ8 0x0068F29C
stModelInstance2D* g_automap_party_marker;
// GLOBAL: WIZ8 0x0068F2A0
stModelInstance2D* g_automap_friendly_marker;
// GLOBAL: WIZ8 0x0068F2A4
stModelInstance2D* g_automap_neutral_marker;
// GLOBAL: WIZ8 0x0068F2A8
stModelInstance2D* g_automap_hostile_marker;
// GLOBAL: WIZ8 0x0068F2AC
stModelInstance2D* g_automap_text_marker;

/* Leave releases the pointed-to objects and erases their vector entries. */
// GLOBAL: WIZ8 0x0068F1F4
std::vector<srClass*> g_automap_markers;

// GLOBAL: WIZ8 0x0068f220
std::vector<std::unique_ptr<srClipPlane>> g_automap_created_layers;
// GLOBAL: WIZ8 0x0068f24c
std::vector<std::unique_ptr<W8ButtonWidget>> g_automap_buttons;
struct W8AutomapState {
    uint64_t blink_time = 0;
    bool blink_enabled = false;
    unsigned char pending_cell_lighting = 0;
};
// GLOBAL: WIZ8 0x0068f268
static std::unique_ptr<W8AutomapState> g_automap_state;
// GLOBAL: WIZ8 0x0068f274
std::unique_ptr<srColorSurface> g_automap_surface;

void AutomapZoomInButton(void);
void AutomapZoomOutButton(void);
void AutomapSelectNoteToolButton(void);
void AutomapSelectEraseToolButton(void);
void AutomapCyclePageButton(void);
void AutomapLayerDownButton(void);
void AutomapLayerUpButton(void);
void AutomapPanNorthButton(void);
void AutomapPanSouthButton(void);
void AutomapPanWestButton(void);
void AutomapPanEastButton(void);
void AutomapExitButton(void);
void ResetAutomapZoom(void);

/* String-table tooltip indexes for the sixteen automap chrome buttons. */
// GLOBAL: WIZ8 0x0064b7a4
int g_automap_button_tooltips[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
/* Left-click actions. ResetAutomapZoom / RestoreAutomapCameraPosition are the
   already-recovered bodies at the retail callback addresses. */
// GLOBAL: WIZ8 0x0064b7e4
void (*g_automap_button_callbacks[16])(void) = {
    AutomapZoomInButton,         AutomapZoomOutButton,         ResetAutomapZoom,
    AutomapSelectNoteToolButton, AutomapSelectEraseToolButton, AutomapCyclePageButton,
    AutomapCyclePageButton,      AutomapCyclePageButton,       AutomapLayerDownButton,
    AutomapLayerUpButton,        AutomapPanNorthButton,        AutomapPanSouthButton,
    AutomapPanWestButton,        AutomapPanEastButton,         RestoreAutomapCameraPosition,
    AutomapExitButton,
};
/* Catalog entries supplying filenames and state-frame offsets for each button. */
// GLOBAL: WIZ8 0x0064b824
int g_automap_button_catalogs[16] = {388, 392, 396, 380, 384, 348, 352, 356,
                                     340, 344, 360, 364, 368, 372, 376, 336};
/* Screen positions for the sixteen buttons. */
// GLOBAL: WIZ8 0x0064b864
srVector2i g_automap_button_positions[16] = {
    {490, 124}, {541, 124}, {591, 124}, {490, 70},  {541, 70},  {591, 70},  {591, 70},  {591, 70},
    {490, 164}, {591, 164}, {540, 218}, {540, 326}, {486, 272}, {594, 272}, {540, 272}, {588, 441},
};
// GLOBAL: WIZ8 0x0064b8e4
srVector2i g_automap_cursor_offsets[5] = {{0, 0}, {8, 7}, {1, 24}, {1, 24}, {8, 7}};
// GLOBAL: WIZ8 0x0064b910
float g_automap_range = 10000.0f;
/* Developer I-key latch; retail has no rendering consumer for this toggle. */
// GLOBAL: WIZ8 0x0064b90d
bool g_automap_developer_i_toggle = true;
// GLOBAL: WIZ8 0x0064b918
int g_automap_layer = -1;
// GLOBAL: WIZ8 0x0064b91c
bool g_automap_bounds_dirty = true;
// GLOBAL: WIZ8 0x0068f138
EnvironmentColour g_automap_saved_light_direction;
// GLOBAL: WIZ8 0x0068f144
EnvironmentColour g_automap_saved_ambient_light;
// GLOBAL: WIZ8 0x0068f150
bool g_automap_saved_sky;
// GLOBAL: WIZ8 0x0068f154
W8WorldCameraState g_automap_saved_camera;
// GLOBAL: WIZ8 0x0068f190
float g_automap_saved_far_clip;
// GLOBAL: WIZ8 0x0068f194
float g_automap_saved_world_value;
// GLOBAL: WIZ8 0x0068f198
unsigned char g_automap_saved_render_flags[4];
// GLOBAL: WIZ8 0x0068f19c
int g_automap_saved_texture_policy;
// GLOBAL: WIZ8 0x0068f1a8
std::vector<srClipPlane*> g_automap_layers;
// GLOBAL: WIZ8 0x0068f1b8
srVector3T<float> g_automap_bounds_max;
// GLOBAL: WIZ8 0x0068f1c8
srVector3T<float> g_automap_grid_max;
// GLOBAL: WIZ8 0x0068f1d8
srVector3T<float> g_automap_grid_min;
// GLOBAL: WIZ8 0x0068f1e8
srVector3T<float> g_automap_position;
// GLOBAL: WIZ8 0x0068f1f8
srVector3T<float> g_automap_grid_center;
// GLOBAL: WIZ8 0x0068f204
float g_automap_top_y;
// GLOBAL: WIZ8 0x0068f210
srVector3T<float> g_automap_bounds_min;
// GLOBAL: WIZ8 0x0068f230
W8ScreenRect g_automap_viewport;
// GLOBAL: WIZ8 0x0068f240
srVector3T<float> g_automap_grid_origin;
// GLOBAL: WIZ8 0x0068f250
int g_automap_tool;
// GLOBAL: WIZ8 0x0068f254
bool g_automap_cursor_inside;
// GLOBAL: WIZ8 0x0068f25c
bool g_automap_redraw;
// GLOBAL: WIZ8 0x0068f260
unsigned int g_automap_page;
// GLOBAL: WIZ8 0x0068f264
bool g_automap_show_all_monsters;
// GLOBAL: WIZ8 0x0068f25d
bool g_automap_overlay_redraw;
// GLOBAL: WIZ8 0x0068f26c
float g_automap_zoom;
// GLOBAL: WIZ8 0x0068f270
bool g_automap_surface_mode;
/* The camera's near clip: how far above the selected layer it floats. */
// GLOBAL: WIZ8 0x0064b920
float g_automap_near_clip = 1.0f;
// GLOBAL: WIZ8 0x0068f278
int g_automap_zoom_mode;
// GLOBAL: WIZ8 0x0068f27c
int g_automap_cell_count;
// GLOBAL: WIZ8 0x0068f290
bool g_automap_position_initialized;
// GLOBAL: WIZ8 0x0068f294
W8AutomapNote* g_automap_editing_note; // Non-owning; g_automap_notes owns the note.
// GLOBAL: WIZ8 0x0068f298
W8AutomapNote* g_automap_hovered_note;

unsigned char HandleAutomapKey(const InputEvent* input);
unsigned char HandleAutomapNoteInput(const InputEvent* input);
unsigned char ShowAutomapNoteTooltip(W8AutomapNote* note);
unsigned char ZoomAutomapIn(const glm::vec2* point);

static glm::vec2 AutomapWorldPosition(const glm::vec2& cursor)
{
    return g_automap_position.xz() +
           (cursor - g_float_half) * glm::vec2(g_automap_zoom, -g_automap_zoom);
}
void SetAutomapCameraPoint(srVector3T<float>* position);
void SetAutomapButtonMode(int update);
void RenderAutomapFrame(void);
void UpdateAutomapPageButtons(void);
void ResetAutomapLighting(void);
void LightAutomapCell(const srVector3T<float>* position);
unsigned int LightPendingAutomapCells(unsigned int max_count);
int RestoreAutomapRect(W8ScreenRect* rect);
void SetAutomapLayer(int layer);
stModelInstance2D* CreateAutomapItemMarker(int item_id);
stModelInstance2D* CreateAutomapMonsterMarker(int type);
stModelInstance2D* CreateAutomapTextMarker(void);

static void SetAutomapCursorImage(int tool)
{
    const auto artwork = CatalogArtwork::Load(tool + 0x14b);
    SetMouseCursorFromVideoObject(*artwork.spriteSheet(), artwork.imageOffset(),
                                  g_automap_cursor_offsets[tool].x,
                                  g_automap_cursor_offsets[tool].y);
    gXStatus.iCurrentCursor = W8_CURSOR_INVALID_TARGET;
    RefreshMouseCursorTexture();
}

/* With no tool selected inside the map, use zoom-in only above the threshold.
   Retail's FNSTSW/TEST 0x41 also selects zoom-out for an unordered zoom. */
void SetAutomapToolCursor(int tool)
{
    if (g_automap_tool == 0 && g_automap_cursor_inside) {
        tool = g_automap_zoom > g_float_twenty_five_thousand ? 1 : 4;
    }
    SetAutomapCursorImage(tool);
}

bool HasAutomapLayer(int layer)
{
    return static_cast<std::size_t>(layer) < g_automap_layers.size() &&
           g_automap_layers[layer] != 0;
}

/* The first live clip plane below a height, or the layer count when none
   qualifies. Notes and world-marker placement share this walk. */
static int FindAutomapLayerAtHeight(double height)
{
    int layer;
    for (layer = 0; layer < static_cast<int>(g_automap_layers.size()); ++layer) {
        if (HasAutomapLayer(layer) && g_automap_layers[layer]->getLocation().y < height) {
            break;
        }
    }
    return layer;
}

/* Redraw and screen teardown share ownership of transient markers. */
static void ClearAutomapMarkers()
{
    wiz8::remove_all_and_delete_in_order(g_automap_markers);
}

/* Full-screen automap background: arm on left-down, dismiss on left-up. */
unsigned char AutomapBackgroundRegionEvent(const InputEvent* event, W8Region* region, unsigned short)
{
    switch (event->type) {
    case InputEventType::LeftButtonDown:
        region->flags |= W8_REGION_LEFT_BUTTON_HELD;
        break;
    case InputEventType::LeftButtonUp:
        if ((region->flags & W8_REGION_LEFT_BUTTON_HELD) != 0) {
            RequestScreenTransition();
            return 1;
        }
        break;
    default:
        return 0;
    }
    return 1;
}

static void ClearAutomapNotes()
{
    g_automap_editing_note = nullptr;
    g_automap_hovered_note = nullptr;
    g_automap_notes.clear();
}

static void EraseAutomapNote(W8AutomapNote* note)
{
    if (g_automap_editing_note == note) g_automap_editing_note = nullptr;
    if (g_automap_hovered_note == note) g_automap_hovered_note = nullptr;
    std::erase_if(g_automap_notes, [note](const auto& owned) { return owned.get() == note; });
}

/* Rebuild the automap view for the level that just loaded: release every
   note, size the query range from the level, seed the cell grid from the
   octree bounds (or a fixed cube when there is no octree), and mark the
   camera's own cell in the visited bitmap, retrying one cell higher when the
   packed cell misses the record table. */
void ResetAutomapView(void)
{
    if (!g_automap_state) {
        g_automap_state = std::make_unique<W8AutomapState>();
    }
    ClearAutomapNotes();
    g_automap_redraw = true;
    if (g_status.current_level == 0x18 ||
        (g_status.current_level > 0x1a && g_status.current_level <= 0x22)) {
        g_automap_range = 30000.0f;
    } else {
        g_automap_range = 10000.0f;
    }
    if (g_octree == 0) {
        g_automap_grid_min = -250000.0f;
        g_automap_grid_max = 250000.0f;
    } else {
        const auto bounds = g_octree->m_spatial.GetClippedBounds();
        g_automap_grid_min = bounds.minimum;
        g_automap_grid_max = bounds.maximum;
    }
    g_automap_grid_origin = g_automap_grid_min;
    g_automap_grid_center.Set((g_automap_grid_min.x + g_automap_grid_max.x) * g_float_half, 0.0f,
                              (g_automap_grid_min.z + g_automap_grid_max.z) * g_float_half);
    g_automap_bounds_dirty = true;
    float span_x = g_automap_grid_max.x - g_automap_grid_min.x;
    float span_z = g_automap_grid_max.z - g_automap_grid_min.z;
    float largest = span_z < span_x ? span_x : span_z;
    if (largest <= g_float_twenty_five_thousand) {
        g_automap_top_y = 25000.0f;
    } else {
        g_automap_top_y = largest;
    }
    g_automap_position_initialized = false;
    g_automap_layer = 0;

    srVector3T<float> camera = cameraLocation();
    srVector3T<float> relative = camera - g_automap_grid_origin;
    unsigned int key = AutomapNodeKey(&relative);
    int cell = LookupIndexValue(g_automap_cell_index, key);
    if (cell > 1) {
        SetBit(g_automap_visited_cells, cell - 1);
        return;
    }
    relative.y = camera.y + g_automap_grid_cell_size - g_automap_grid_origin.y;
    key = AutomapNodeKey(&relative);
    cell = LookupIndexValue(g_automap_cell_index, key);
    if (cell > 1) {
        SetBit(g_automap_visited_cells, cell - 1);
    }
}

/* Pack `position` into a cell key (retrying one cell higher on miss). When the
   record table knows the cell, mark it visited and return 1 only if that bit
   was newly raised — used by the world camera update to gate automap lighting. */
bool AutomapHasCellAt(const srVector3T<float>* position)
{
    srVector3T<float> relative = *position - g_automap_grid_origin;
    unsigned int key = AutomapNodeKey(&relative);
    int cell = LookupIndexValue(g_automap_cell_index, key);
    if (cell > 1) {
        if (!SetBit(g_automap_visited_cells, cell - 1)) {
            return true;
        }
        return false;
    }
    relative.y = position->y + g_automap_grid_cell_size - g_automap_grid_origin.y;
    key = AutomapNodeKey(&relative);
    cell = LookupIndexValue(g_automap_cell_index, key);
    if (cell > 1) {
        if (!SetBit(g_automap_visited_cells, cell - 1)) {
            return true;
        }
    }
    return false;
}

bool CanUseCurrentAutomapTool(void)
{
    if (g_mipe_active) {
        switch (g_mipe_mode) {
        case 6:
        case 7:
        case 15:
        case 18:
        case 23:
        case 29:
            return false;
        }
    }
    return true;
}

void RestoreAutomapCameraPosition(void)
{
    g_automap_position.x = g_automap_saved_camera.position.x;
    g_automap_position.z = g_automap_saved_camera.position.z;
    SetAutomapCameraPoint(&g_automap_position);
}

/* Build the sixteen chrome buttons from the adjacent catalog / callback /
   tooltip / position tables. */
void CreateAutomapButtons(void)
{
    std::vector<std::unique_ptr<W8ButtonWidget>> buttons(16);
    for (size_t index = 0; index < buttons.size(); ++index) {
        const int catalog_id = g_automap_button_catalogs[index];
        auto artwork = CatalogArtwork::Load(catalog_id);
        auto sprites = artwork.spriteSheet();
        if (!sprites) continue;
        const int base_frame = artwork.imageOffset();
        auto button = std::make_unique<W8ButtonWidget>();
        const auto action = g_automap_button_callbacks[index];
        W8ButtonCallback callback;
        if (action) callback = [action](W8ButtonWidget*) { action(); };
        if (button->Configure(std::move(sprites), base_frame + 3, base_frame, base_frame + 1,
                              base_frame + 2, base_frame + 2, callback, nullptr, false,
                              REGION_PRIORITY_HIGHEST, g_automap_button_tooltips[index], nullptr, nullptr)) {
            button->SetPosition(g_automap_button_positions[index].x,
                                g_automap_button_positions[index].y);
        }
        buttons[index] = std::move(button);
    }
    g_automap_buttons = std::move(buttons);
}

void AutomapZoomInButton(void)
{
    const glm::vec2 center(0.5f);
    ZoomAutomapIn(&center);
}

/* Mirror of the right-click zoom-out path: one step when the camera is below
   the full top height, otherwise restore the full explored span. The one-step
   arm inlines SetAutomapToolCursor the same way retail does. */
void AutomapZoomOutButton(void)
{
    if (g_automap_position.y < g_automap_top_y) {
        if (g_automap_zoom_mode == 1) {
            g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
            srVector3T<float> position(g_automap_bounds_min.x + g_automap_bounds_max.x,
                                       g_automap_bounds_max.y + g_automap_bounds_min.y,
                                       g_automap_bounds_min.z + g_automap_bounds_max.z);
            position = position * 0.5;
            position.y = g_automap_top_y;
            SetAutomapCameraPoint(&position);
            SetAutomapToolCursor(g_automap_tool);
            SetAutomapButtonMode(0);
        } else {
            float ground_y = g_automap_position.y - g_automap_zoom;
            float height = g_automap_top_y - (g_automap_top_y - ground_y) * g_float_half;
            if (g_automap_position.y <= height) {
                g_automap_position.y = height;
                if (!g_automap_buttons.empty()) {
                    g_automap_buttons[0]->SetEnabled(true);
                    g_automap_buttons[1]->SetEnabled(true);
                    g_automap_buttons[0]->m_dirty = true;
                    g_automap_buttons[0]->Draw();
                    g_automap_buttons[1]->m_dirty = true;
                    g_automap_buttons[1]->Draw();
                }
                g_automap_zoom = g_automap_position.y - ground_y;
                g_automap_zoom_mode = 1;
                SetAutomapCameraPoint(&g_automap_position);
                SetAutomapToolCursor(g_automap_tool);
            } else {
                g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
                srVector3T<float> position(g_automap_bounds_min.x + g_automap_bounds_max.x,
                                           g_automap_bounds_max.y + g_automap_bounds_min.y,
                                           g_automap_bounds_min.z + g_automap_bounds_max.z);
                position = position * 0.5;
                position.y = g_automap_top_y;
                SetAutomapCameraPoint(&position);
                SetAutomapToolCursor(g_automap_tool);
                SetAutomapButtonMode(0);
            }
        }
    }
}

void AutomapSelectNoteToolButton(void)
{
    if (g_automap_tool == 2) {
        return;
    }
    g_automap_tool = 2;
    SetAutomapToolCursor(2);
}

void AutomapSelectEraseToolButton(void)
{
    if (g_automap_tool == 3) {
        return;
    }
    g_automap_tool = 3;
    SetAutomapToolCursor(3);
}

void AutomapCyclePageButton(void)
{
    g_automap_page = (g_automap_page + 1) % 3;
    UpdateAutomapPageButtons();
    g_automap_redraw = true;
}

void AutomapLayerDownButton(void)
{
    if (g_automap_layer > 0) {
        SetAutomapLayer(g_automap_layer - 1);
    }
}

void AutomapLayerUpButton(void)
{
    if (g_automap_layer < static_cast<int>(g_automap_layers.size())) {
        SetAutomapLayer(g_automap_layer + 1);
    }
}

void AutomapPanNorthButton(void)
{
    g_automap_position.z += g_float_thirty_five_hundredths * g_automap_zoom;
    SetAutomapCameraPoint(&g_automap_position);
}

void AutomapPanSouthButton(void)
{
    g_automap_position.z -= g_float_thirty_five_hundredths * g_automap_zoom;
    SetAutomapCameraPoint(&g_automap_position);
}

void AutomapPanWestButton(void)
{
    g_automap_position.x -= g_float_thirty_five_hundredths * g_automap_zoom;
    SetAutomapCameraPoint(&g_automap_position);
}

void AutomapPanEastButton(void)
{
    g_automap_position.x += g_float_thirty_five_hundredths * g_automap_zoom;
    SetAutomapCameraPoint(&g_automap_position);
}

void AutomapExitButton(void)
{
    if (g_automap_editing_note == 0) {
        RequestScreenTransition();
    }
}

unsigned char AutomapScreenEnter(void)
{
    stScript script;
    GetLightDirection(&g_automap_saved_light_direction);
    GetWorldLightValue(g_world, &g_automap_saved_ambient_light);
    g_automap_saved_camera = cameraState(*GetWorld());
    g_automap_saved_far_clip = static_cast<float>(WorldGetFarClip(g_world));
    g_automap_saved_world_value = WorldGetRenderRange(g_world);
    g_automap_saved_sky = g_sky_enabled;
    g_automap_saved_render_flags[0] = GetRenderOptionState(W8_RENDER_OPTION_HIGH_TEXTURE_DETAIL);
    g_automap_saved_render_flags[1] = GetRenderOptionState(W8_RENDER_OPTION_MESH_SKY);
    g_automap_saved_render_flags[2] = g_monster_shadow_updates_enabled;
    g_automap_saved_render_flags[3] = g_world_render_enabled;
    g_automap_saved_texture_policy = g_resident_texture_policy;
    EnvironmentColour direction;
    direction = 0.0;
    SetLightDirection(&direction);
    SetWorldEnvironmentColour(g_world, EnvironmentColour(0.0, 0.0, 0.0));
    DisableSky();
    DisableRenderOption(W8_RENDER_OPTION_MESH_SKY);
    g_monster_shadow_updates_enabled = 1;
    g_world_render_enabled = 1;
    DisableSky();
    g_world->camera->setClipRange(g_automap_near_clip, 1500000.0);
    WorldSetRenderRange(g_world, 1500000.0f);
    g_world->camera->setRotation(3.141592653589793 * (1.0f / 180.0f) * 90.0f, 0.0, 0.0);
    int layer_number = 1;
    g_world->camera->setProjectionType(srCamera::PROJECT_ORTHOGRAPHIC);
    RestoreAutomapCameraPosition();
    UpdateWorldMesh(g_world);
    SetWorldMeshVertexLightTable(g_world, 1);
    g_light_update_flags &= ~1u;
    SetWorldModelPickingEnabled(false);
    if (!g_automap_state) {
        g_automap_state = std::make_unique<W8AutomapState>();
    }
    g_automap_state->blink_time = w8_get_ticks();
    g_automap_viewport.left = 12;
    g_automap_viewport.top = 32;
    g_automap_viewport.right = 467;
    g_automap_viewport.bottom = 467;
    PauseMainGameWorld();
    g_automap_layers.clear();
    g_automap_layers.push_back(0);
    std::string layer_name;
    layer_name = fmt::format("LAYER {:d}", layer_number);
    srClipPlane* layer = FindWorldNode<srClipPlane>(layer_name.c_str());
    while (layer) {
        g_automap_layers.push_back(layer);
        ++layer_number;
        layer_name = fmt::format("LAYER {:d}", layer_number);
        layer = FindWorldNode<srClipPlane>(layer_name.c_str());
    }
    unsigned int monster_count = gXStatus.monsters.size();
    for (unsigned int index = 0; index < monster_count; ++index) {
        W8MonsterInfo* monster = GetMonsterListEntry(index);
        if (monster->p3D)
            monster->p3D->DetachRepresentation(g_world);
    }
    for (W8WorldItem* item = GetNextWorldItem(true); item; item = GetNextWorldItem(false)) {
        if (item->p3D)
            item->p3D->DetachMesh(g_world);
    }
    SetMouseMoveEventsEnabled(1);
    g_automap_cursor_inside = IsCursorInsideViewport();
    g_automap_tool = 0;
    SetAutomapToolCursor(0);
    CreateAutomapMarkerSprites();
    CreateAutomapButtons();
    g_automap_party_marker->setParent(0, 1);
    g_automap_surface_mode = RendererBufferIsLockable();
    if (g_automap_surface_mode) {
        g_world_render_enabled = 0;
        g_monster_shadow_updates_enabled = 0;
        if (!g_automap_surface) {
            g_automap_surface = std::make_unique<srColorSurface>(srPixelConvert::SURFACE_ARGB1555, 640, 480);
            auto* surface = g_automap_surface.get();
            if (surface)
                surface->setFilter(&srBoxFilter);
        }
    }
    g_automap_redraw = true;
    g_automap_overlay_redraw = true;
    SetPrimarySurfaceTextureHint2Enabled(false);
    ClearSurfaceRect(0, 0, 640, 480);
    DisableCursorScene();
    CatalogArtwork::Load(0x14a, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0);
    UpdateAutomapPageButtons();
    for (int button = 0; button < 16; ++button) {
        if (g_automap_buttons[button]) {
            g_automap_buttons[button]->m_dirty = true;
            g_automap_buttons[button]->Draw();
        }
    }
    ResetTransientRenderScenes();
    RenderFrame();
    RenderFrame();
    EnableCursorScene();
    SetAutomapScaledViewport(12, 32, 467, 467);
    UpdateAutomapBounds();
    if (script.Load("Data\\Automap\\MapFilters.txt")) {
        std::vector<std::string_view> excluded_textures;
        int line = 0;
        int section = -1;
        while (section < g_status.current_level && line < static_cast<int>(script.lines.size())) {
            if (script.lines[line].text.contains('['))
                ++section;
            ++line;
        }
        if (line < static_cast<int>(script.lines.size()) && section == g_status.current_level) {
            for (; line < static_cast<int>(script.lines.size()); ++line) {
                const auto& text = script.lines[line].text;
                if (text.contains('['))
                    break;
                if (!text.contains("LAYER=")) {
                    excluded_textures.push_back(text);
                } else {
                    float height = static_cast<float>(atof(text.c_str() + 6));
                    srClipPlane* clip =
                        g_automap_created_layers.emplace_back(std::make_unique<srClipPlane>()).get();
                    layer_name = fmt::format("LAYER {:d}", layer_number);
                    clip->setName(layer_name.c_str());
                    glm::vec4 plane;
                    plane = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
                    clip->setClipPlane(plane);
                    glm::dvec3 position(0.0, static_cast<double>(height * 500.0f), 0.0);
                    clip->setLocation(position);
                    clip->setFlag(static_cast<srNode::e_flag>(2));
                    clip->setClipType(srClipPlane::CLIP_POSITIONAL_0);
                    clip->setFlag(static_cast<srNode::e_flag>(0));
                    g_automap_layers.push_back(clip);
                    ++layer_number;
                }
            }
            for (unsigned int mesh = 0; mesh < g_world->octree->m_meshCount; ++mesh) {
                for (stMeshModel* model : stMeshModel::Meshes(static_cast<stMeshModel*>(g_world->psrMeshes[mesh]->getModel()))) {
                    model->ApplyAutomapPolygonFilter(excluded_textures);
                }
            }
        }
    }
    if (!g_automap_position_initialized) {
        g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
        g_automap_position_initialized = true;
        srVector3T<float> position = (g_automap_bounds_min + g_automap_bounds_max) / 2.0;
        position.y = g_automap_top_y;
        SetAutomapCameraPoint(&position);
        SetAutomapToolCursor(g_automap_tool);
        SetAutomapButtonMode(0);
    } else {
        RestoreAutomapCameraPosition();
    }
    return 1;
}

EnvironmentColour::EnvironmentColour(double red_value, double green_value, double blue_value)
{
    Set(red_value, green_value, blue_value);
}

void EnvironmentColour::Set(double red_value, double green_value, double blue_value)
{
    x = static_cast<float>(red_value);
    y = static_cast<float>(green_value);
    z = static_cast<float>(blue_value);
    if (x > 0.0f) {
        if (x >= 1.0f)
            x = 1.0f;
    } else {
        x = 0.0f;
    }
    if (y > 0.0f) {
        if (y >= 1.0f)
            y = 1.0f;
    } else {
        y = 0.0f;
    }
    if (z > 0.0f) {
        if (z >= 1.0f)
            z = 1.0f;
    } else {
        z = 0.0f;
    }
}

W8AutomapNote* CreateAutomapNote(const glm::vec2* position, int layer, std::string_view text)
{
    if (wiz8::text::to_utf16(text).size() < 40 && static_cast<int>(g_automap_notes.size()) < 200) {
        auto note = std::make_unique<W8AutomapNote>();
        note->position = *position;
        note->layer = layer;
        note->text = text;
        auto* result = note.get();
        g_automap_notes.push_back(std::move(note));
        g_automap_redraw = true;
        return result;
    }
    return 0;
}

void AutomapScreenFrame(void)
{
    PumpUiInput({
        .before = [](const InputEvent& input) {
        if (g_automap_editing_note) {
            HandleAutomapNoteInput(&input);
            return InputRoute::Handled;
        }
        if (HandleAutomapKey(&input))
            return InputRoute::Handled;
        if (!IsCursorInsideViewport()) {
            if (g_automap_cursor_inside) {
                g_automap_cursor_inside = false;
                SetAutomapToolCursor(g_automap_tool);
            }
            SDL_Point point;
            SGPMouseGetPos(&point);
            UpdateRegionMousePosition(point.x, point.y);
            return InputRoute::Pass;
        }
        if (!g_automap_cursor_inside) {
            g_automap_cursor_inside = true;
            SetAutomapToolCursor(g_automap_tool);
        }
        if (input.type == InputEventType::LeftButtonUp) {
            glm::vec2 point;
            if (g_automap_tool == 2) {
                if (GetCursorPositionInViewport(&point)) {
                    int layer = g_automap_layer + 1;
                    if (HasAutomapLayer(layer)) {
                        g_automap_layers[layer]->getLocationY();
                    }
                    const auto location = AutomapWorldPosition(point);
                    g_automap_editing_note = CreateAutomapNote(&location, g_automap_layer, "_");
                    SetNativeTextInputEnabled(g_automap_editing_note != nullptr);
                }
            } else if (g_automap_tool == 3) {
                W8AutomapNote* note = FindAutomapNoteUnderCursor();
                if (note) {
                    EraseAutomapNote(note);
                    g_automap_redraw = true;
                }
            } else if (GetCursorPositionInViewport(&point)) {
                ZoomAutomapIn(&point);
            }
        } else if (input.type == InputEventType::RightButtonUp) {
            if (g_automap_tool == 0) {
                if (g_automap_position.y < g_automap_top_y) {
                    if (g_automap_zoom_mode == 1) {
                        g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
                        srVector3T<float> position =
                            (g_automap_bounds_max + g_automap_bounds_min) / 2.0;
                        position.y = g_automap_top_y;
                        SetAutomapCameraPoint(&position);
                        SetAutomapToolCursor(g_automap_tool);
                        SetAutomapButtonMode(0);
                    } else {
                        float ground_y = g_automap_position.y - g_automap_zoom;
                        float height = g_automap_top_y - (g_automap_top_y - ground_y) * 0.5f;
                        if (g_automap_position.y <= height) {
                            g_automap_position.y = height;
                            SetAutomapButtonMode(1);
                            g_automap_zoom = g_automap_position.y - ground_y;
                            SetAutomapCameraPoint(&g_automap_position);
                            SetAutomapToolCursor(g_automap_tool);
                        } else {
                            ResetAutomapZoom();
                        }
                    }
                }
            } else {
                g_automap_tool = 0;
                SetAutomapToolCursor(0);
            }
        } else if (input.type == InputEventType::MouseMove) {
            W8AutomapNote* previous = g_automap_hovered_note;
            g_automap_hovered_note = FindAutomapNoteUnderCursor();
            if (previous != g_automap_hovered_note) {
                if (previous)
                    ShowAutomapNoteTooltip(previous);
                RenderAutomapMarkers();
            }
            srVector3 point;
            if (GetAutomapPositionUnderCursor(&point)) {
                point.y = 0.0f;
                srVector3T<float> distance(point.x - g_automap_saved_camera.position.x, 0.0f,
                                           point.z - g_automap_saved_camera.position.z);
                if (distance.Length() < g_automap_zoom * 0.05f && g_automap_tool == 0 &&
                    g_automap_zoom_mode != 2) {
                    DisableCursorScene();
                    glm::dvec3 scale(0.44f, 0.44f, 0.44f);
                    g_automap_party_marker->setScale(scale);
                    return InputRoute::Handled;
                }
            }
            float factor = (1.0f / (g_automap_zoom * 0.00004f)) * 0.44f;
            glm::dvec3 scale(factor, factor, factor);
            g_automap_party_marker->setScale(scale);
            EnableCursorScene();
        }
        return InputRoute::Handled;
        },
        .ui_accepts = IsMouseButtonEvent,
        .track_hover = false,
    });
    bool moved = false;
    if (IsKeyHeld(VK_LEFT)) {
        g_automap_position.x -= g_float_thirty_five_hundredths * g_automap_zoom;
        moved = true;
    }
    if (IsKeyHeld(VK_RIGHT)) {
        g_automap_position.x += g_float_thirty_five_hundredths * g_automap_zoom;
        moved = true;
    }
    if (IsKeyHeld(VK_UP)) {
        g_automap_position.z += g_float_thirty_five_hundredths * g_automap_zoom;
        moved = true;
    }
    if (IsKeyHeld(VK_DOWN)) {
        g_automap_position.z -= g_float_thirty_five_hundredths * g_automap_zoom;
        moved = true;
    }
    if (moved)
        SetAutomapCameraPoint(&g_automap_position);
    if (w8_get_ticks() - g_automap_state->blink_time > 500) {
        if (g_automap_state->blink_enabled)
            DrawVideoInspector(0xdc, 0x32);
        g_automap_state->blink_time = w8_get_ticks();
    }
    RenderAutomapFrame();
}

void RestoreAutomapWorldSettings(void)
{
    SetWorldEnvironmentColour(g_world, g_automap_saved_ambient_light);
    SetLightDirection(&g_automap_saved_light_direction);
    RestoreWorldCameraState(GetWorld(), 0, &g_automap_saved_camera);
    WorldSetFarClip(g_world, g_automap_saved_far_clip);
    WorldSetRenderRange(g_world, g_automap_saved_world_value);
    if (g_automap_saved_sky)
        EnableSky();
    SetRenderOption(W8_RENDER_OPTION_HIGH_TEXTURE_DETAIL, g_automap_saved_render_flags[0]);
    SetRenderOption(W8_RENDER_OPTION_MESH_SKY, g_automap_saved_render_flags[1]);
    g_monster_shadow_updates_enabled = g_automap_saved_render_flags[2];
    g_world_render_enabled = g_automap_saved_render_flags[3];
    g_world->camera->setRotation(0.0, 0.0, 0.0);
    g_world->camera->flags.value &= ~1ul;
    SetWorldMeshVertexLightTable(g_world, 0);
    g_light_update_flags |= 1u;
    SetResidentTexturePolicy(g_resident_texture_policy);
    SetWorldModelPickingEnabled(true);
}

unsigned char AutomapScreenLeave(int)
{
    SetNativeTextInputEnabled(false);
    RestoreAutomapWorldSettings();
    g_automap_state.reset();
    MarkRendererReady();
    SetOverlayViewport(0);
    ResumeMainGameWorld();
    SetMouseMoveEventsEnabled(0);
    g_automap_surface.reset();
    ClearAutomapMarkers();
    g_automap_party_marker->setParent(0, 1);
    g_automap_buttons.clear();
    UpdateHeldItemCursor();
    EnableCursorScene();
    for (unsigned int mesh = 0; mesh < g_world->octree->m_meshCount; ++mesh) {
        for (stMeshModel* model : stMeshModel::Meshes(static_cast<stMeshModel*>(g_world->psrMeshes[mesh]->getModel()))) {
            model->ClearAutomapPolygonFilter();
        }
    }
    g_automap_created_layers.clear();
    return 1;
}

/* Lifecycle record 8's finalizer, the fifth slot of its record and the third
   allocate/release pair that establishes what that slot is for. Every guard is
   the original's own, and each pointer is cleared after its release. */
unsigned char AutomapScreenFinalize(void)
{
    g_automap_visited_cells.clear();
    g_automap_lit_cells.clear();
    g_automap_cell_keys.clear();
    g_automap_cell_index.clear();
    ReleaseRendererObject(g_automap_party_marker);
    ReleaseRendererObject(g_automap_friendly_marker);
    ReleaseRendererObject(g_automap_neutral_marker);
    ReleaseRendererObject(g_automap_hostile_marker);
    return 1;
}

/* Select which automap buttons are enabled for the update mode, then dirty
   and redraw both and remember the mode. */
void SetAutomapButtonMode(int update)
{
    if (g_automap_buttons.empty()) {
        g_automap_zoom_mode = update;
        return;
    }
    if (update == 0) {
        g_automap_buttons[0]->SetEnabled(true);
        g_automap_buttons[1]->SetEnabled(false);
    } else if (update == 1) {
        g_automap_buttons[0]->SetEnabled(true);
        g_automap_buttons[1]->SetEnabled(true);
    } else if (update == 2) {
        g_automap_buttons[0]->SetEnabled(false);
        g_automap_buttons[1]->SetEnabled(true);
    }
    g_automap_buttons[0]->m_dirty = true;
    g_automap_buttons[0]->Draw();
    g_automap_buttons[1]->m_dirty = true;
    g_automap_buttons[1]->Draw();
    g_automap_zoom_mode = update;
}

// GLOBAL: WIZ8 0x0064b914
float g_automap_grid_cell_size = 2000.0f;
// GLOBAL: WIZ8 0x0068f2b0
W8MainUiMode g_ui_mode_current;
// GLOBAL: WIZ8 0x0068f2c4
W8MainUiMode g_ui_mode_saved;

void SetAutomapGridCellSize(float value)
{
    g_automap_grid_cell_size = value;
}

float GetAutomapGridCellSize(void)
{
    return g_automap_grid_cell_size;
}

void SetCurrentAutomapUiMode(W8MainUiMode value)
{
    g_ui_mode_current = value;
}

void SetSavedAutomapUiMode(W8MainUiMode value)
{
    g_ui_mode_saved = value;
}
/* Park the automap camera over the requested point with its xz clamped into
   the explored bounds, reselect the layer underneath and refresh the mesh. */
void SetAutomapCameraPoint(srVector3T<float>* position)
{
    srVector3T<float> clamped;
    clamped.x = g_automap_bounds_min.x <= position->x ? position->x : g_automap_bounds_min.x;
    clamped.x = clamped.x < g_automap_bounds_max.x
                    ? (g_automap_bounds_min.x <= position->x ? position->x : g_automap_bounds_min.x)
                    : g_automap_bounds_max.x;
    clamped.y = position->y;
    clamped.z = g_automap_bounds_min.z <= position->z ? position->z : g_automap_bounds_min.z;
    clamped.z = clamped.z < g_automap_bounds_max.z
                    ? (position->z < g_automap_bounds_min.z ? g_automap_bounds_min.z : position->z)
                    : g_automap_bounds_max.z;
    g_world->camera->setLocation(glm::dvec3(clamped.x, clamped.y, clamped.z));
    g_automap_position = clamped;
    SetAutomapLayer(g_automap_layer);
    g_automap_redraw = true;
    g_automap_overlay_redraw = true;
    if (g_world->octree != 0) {
        MarkRendererReady();
        UpdateWorldMeshAfterLoad();
    }
}

/* Select the automap layer, remember how far above it the camera floats and
   refresh the layer-up/layer-down buttons. */
void SetAutomapLayer(int layer)
{
    if (!HasAutomapLayer(layer)) {
        g_automap_near_clip = 1.0f;
    } else {
        float height =
            g_automap_position.y - static_cast<float>(g_automap_layers[layer]->getLocation().y);
        if (height < 1.0f) {
            g_automap_near_clip = 1.0f;
        } else {
            g_automap_near_clip = height;
        }
    }
    g_automap_layer = layer;
    g_automap_redraw = true;
    g_automap_overlay_redraw = true;
    if (!g_automap_buttons.empty()) {
        if (g_automap_buttons[9] != 0) {
            g_automap_buttons[9]->SetEnabled(layer !=
                                             static_cast<int>(g_automap_layers.size()) - 1);
        }
        if (g_automap_buttons[8] != 0) {
            g_automap_buttons[8]->SetEnabled(layer != 0);
        }
    }
}

/* Reset the zoom to the full explored span, recenter over the bounds and
   restore the neutral button mode and cursor. */
void ResetAutomapZoom(void)
{
    g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
    srVector3T<float> position;
    position.Set((g_automap_bounds_min.x + g_automap_bounds_max.x) * 0.5,
                 (g_automap_bounds_min.y + g_automap_bounds_max.y) * 0.5,
                 (g_automap_bounds_min.z + g_automap_bounds_max.z) * 0.5);
    position.y = g_automap_top_y;
    SetAutomapCameraPoint(&position);
    SetAutomapToolCursor(g_automap_tool);
    SetAutomapButtonMode(0);
}

/* Left-click zoom: drop a sight line onto the clicked point, halve the
   remaining height above ground and move the camera there. */
unsigned char ZoomAutomapIn(const glm::vec2* point)
{
    const auto location = AutomapWorldPosition(*point);
    srVector3 position(location.x, g_automap_top_y, location.y);
    srVector3 from(location.x, g_automap_top_y + g_world_scale, location.y);
    srVector3 to(location.x, g_automap_grid_min.y, location.y);

    float ground;
    if (g_octree_game_data->octree->TraceLineOfSight(&from, &to, true, -3, -3, true, 0) == 0) {
        ground = g_automap_grid_min.y;
        if (!g_automap_layers.empty() && 1 < static_cast<int>(g_automap_layers.size()) &&
            g_automap_layers[1] != 0) {
            ground = static_cast<float>(g_automap_layers[1]->getLocationY());
        }
    } else {
        ground = to.y;
    }

    float height = (g_automap_top_y - ground) * g_float_half;
    float minimum = ground + g_float_twenty_five_thousand;
    if (g_automap_zoom_mode != 0 || g_automap_top_y <= minimum || height < minimum) {
        SetAutomapButtonMode(2);
        height = minimum;
    } else {
        SetAutomapButtonMode(1);
    }
    g_automap_zoom = height - ground;
    position.y = height;
    SetAutomapCameraPoint(&position);
    SetAutomapToolCursor(g_automap_tool);
    return 1;
}

/* Zero every world mesh's vertex-light table and flag it dirty, then clear
   the cell-processed bitmap so UpdateAutomapBounds rebuilds the lighting for
   every visited cell. */
void ResetAutomapLighting(void)
{
    if (g_world->octree != 0) {
        for (unsigned int mesh = 0; mesh < g_world->octree->m_meshCount; ++mesh) {
            srModelInstance* instance = g_world->psrMeshes[mesh];
            if (instance != 0) {
                for (stMeshModel* model : stMeshModel::Meshes(static_cast<stMeshModel*>(instance->getModel()))) {
                    model->ClearVertexLights();
                }
            }
        }
    } else {
        for (stMeshModel* model : stMeshModel::Meshes(static_cast<stMeshModel*>(g_world->update_mesh_source->getModel()))) {
            model->ClearVertexLights();
        }
    }
    std::ranges::fill(g_automap_lit_cells, false);
}

static srVector3T<float> GetAutomapCellPosition(unsigned int index)
{
    srVector3T<float> cell;
    cell.SetZero();
    if (index < g_automap_cell_keys.size()) {
        unsigned int key = g_automap_cell_keys[index];
        float half = g_automap_grid_cell_size * g_float_half;
        cell.Set((key >> 0x15) * g_automap_grid_cell_size + half,
                 (key & 0x3ff) * g_automap_grid_cell_size + half,
                 ((key >> 10) & 0x7ff) * g_automap_grid_cell_size + half);
    }
    return cell + g_automap_grid_origin;
}

/* Light up to `max_count` visited cells that have not yet been processed into
   vertex lights. Returns how many cells were lit; a zero answer means the
   pending set is empty. */
unsigned int LightPendingAutomapCells(unsigned int max_count)
{
    unsigned int lit = 0;
    unsigned int bit = 0;
    if (g_automap_cell_count != 0) {
        do {
            if (max_count <= lit) {
                return lit;
            }
            if (TestBit(g_automap_visited_cells, bit)) {
                srVector3T<float> position = GetAutomapCellPosition(bit);
                if (!TestBit(g_automap_lit_cells, bit)) {
                    SetBit(g_automap_lit_cells, bit);
                    LightAutomapCell(&position);
                    ++lit;
                }
            }
            ++bit;
        } while (bit < static_cast<unsigned int>(g_automap_cell_count));
    }
    return lit;
}

void RefreshDirtyAutomap(void)
{
    if (g_automap_state != 0 && g_automap_state->pending_cell_lighting != 0) {
        SetWorldMeshVertexLightTable(g_world, 1);
        unsigned int lit = LightPendingAutomapCells(10);
        if (lit == 0) {
            g_automap_state->pending_cell_lighting = 0;
        }
        SetWorldMeshVertexLightTable(g_world, 0);
    }
}

/* Recompute the explored-bounds box from the visited-cell bitmap: mark new
   cells for the node builder and grow the box to cover each visited cell. */
void UpdateAutomapBounds(void)
{
    std::vector<stModelInstance*> models;
    PartyHasCondition(0x40);
    if (g_world->octree != 0) {
        if (g_automap_bounds_dirty) {
            ResetAutomapLighting();
            g_automap_bounds_dirty = false;
        }
        g_automap_bounds_min.y = g_automap_grid_min.y;
        g_automap_bounds_max.y = g_automap_grid_max.y;
        g_automap_bounds_min.x = 1e+09f;
        g_automap_bounds_min.z = 1e+09f;
        g_automap_bounds_max.x = -1e+09f;
        g_automap_bounds_max.z = -1e+09f;
        unsigned int bit = 0;
        if (g_automap_cell_count != 0) {
            do {
                if (TestBit(g_automap_visited_cells, bit)) {
                    srVector3T<float> position = GetAutomapCellPosition(bit);
                    if (position.x <= g_automap_bounds_min.x) {
                        g_automap_bounds_min.x = position.x;
                    }
                    if (position.z <= g_automap_bounds_min.z) {
                        g_automap_bounds_min.z = position.z;
                    }
                    if (g_automap_bounds_max.x <= position.x) {
                        g_automap_bounds_max.x = position.x;
                    }
                    if (g_automap_bounds_max.z <= position.z) {
                        g_automap_bounds_max.z = position.z;
                    }
                    if (!TestBit(g_automap_lit_cells, bit)) {
                        SetBit(g_automap_lit_cells, bit);
                        LightAutomapCell(&position);
                    }
                }
                ++bit;
            } while (bit < static_cast<unsigned int>(g_automap_cell_count));
        }
        g_automap_bounds_min.x -= g_float_five_thousand;
        g_automap_bounds_max.x += g_float_five_thousand;
        g_automap_bounds_min.z -= g_float_five_thousand;
        g_automap_bounds_max.z += g_float_five_thousand;
        float span = g_automap_bounds_max.x - g_automap_bounds_min.x;
        if (span <= g_automap_bounds_max.z - g_automap_bounds_min.z) {
            span = g_automap_bounds_max.z - g_automap_bounds_min.z;
        }
        g_automap_top_y = span + g_automap_bounds_max.y;
    }
}

/* Reveal one visited cell: collect the model instances the octree reports
   near the cell position, then for every linked mesh either flood its
   vertex-light table with 1.0 when the whole bounding box is inside the query
   range, or compute per-vertex distances and fill only the in-range runs. */
void LightAutomapCell(const srVector3T<float>* position)
{
    const auto center = position->glmValue();
    const auto instances = g_world->octree->modelsNearPoint(center, g_automap_range, 0, false);
    for (auto* instance : instances) {
        if (!instance)
            continue;
        auto world = instance->getWorldSpaceMatrix<glm::mat4>();
        for (auto* mesh = static_cast<stMeshModel*>(instance->getModel()); mesh; mesh = mesh->Next())
            mesh->RevealVerticesNear(center, g_automap_range, world);
    }
}

/* Per-frame automap refresh: re-render the world into the automap surface
   when it is dirty, blit it into the primary surface and redraw buttons. */
void RenderAutomapFrame(void)
{
    if (g_automap_redraw) {
        if (!g_automap_surface_mode) {
            RenderAutomapMarkers();
        } else {
            if (g_automap_overlay_redraw) {
                SetResidentTexturePolicy(3);
                float half = g_automap_zoom * g_float_half;
                srCamera::Rect view;
                view.left = static_cast<double>(-half);
                view.bottom = static_cast<double>(-half);
                view.right = static_cast<double>(half);
                view.top = static_cast<double>(half);
                g_world->camera->setViewPlane(view, static_cast<double>(g_automap_near_clip));
                g_world->camera->setClipRange(g_automap_near_clip, 1500000.0);
                RenderWorldToSurface(g_automap_surface.get(), &g_automap_viewport, false);
                g_automap_overlay_redraw = false;
                SetResidentTexturePolicy(0);
            }
            auto surface = std::make_unique<srColorSurface>(srPixelConvert::SURFACE_ARGB1555,
                                                             GetFrameBufferSurface());
            surface->setFilter(&srBoxFilter);
            surface->blit(0xc, 0x20, *g_automap_surface, 0xc, 0x20, 0x1d3, 0x1d3);
            RenderAutomapMarkers();
            InvalidateRegion(0xc, 0x20, 0x1d3, 0x1d3, 0);
            SetRendererAutoFlipEnabled(false);
            RenderFrame();
            SetRendererAutoFlipEnabled(true);
        }
        g_automap_redraw = false;
    }
    for (int button = 0; button < 16; ++button) {
        if (g_automap_buttons[button] != 0) {
            g_automap_buttons[button]->Draw();
        }
    }
    RenderFrame();
}

void UpdateAutomapPageButtons(void)
{
    g_automap_buttons[5]->SetVisible(g_automap_page == 0);
    g_automap_buttons[6]->SetVisible(g_automap_page == 1);
    g_automap_buttons[7]->SetVisible(g_automap_page == 2);
    g_automap_buttons[g_automap_page + 5]->m_dirty = true;
    g_automap_buttons[g_automap_page + 5]->Draw();
}

/* Blit the saved automap surface back over a screen rect to erase whatever
   was drawn there. */
int RestoreAutomapRect(W8ScreenRect* rect)
{
    if (g_automap_surface != 0) {
        srColorSurface screen(srPixelConvert::SURFACE_ARGB1555, GetFrameBufferSurface());
        screen.setFilter(&srBoxFilter);
        screen.blit(rect->left, rect->top, *g_automap_surface, rect->left, rect->top, rect->right,
                    rect->bottom);
    }
    return 1;
}

/* When the cursor leaves a hovered note, erase its tooltip rectangle and
   invalidate it for redraw. Returns nonzero when a tooltip was dismissed. */
unsigned char ShowAutomapNoteTooltip(W8AutomapNote* note)
{
    if (note == 0) {
        return 0;
    }
    float floor_y;
    int layer = note->layer + 1;
    if (!HasAutomapLayer(layer)) {
        floor_y = g_automap_grid_min.y;
    } else if (layer < static_cast<int>(g_automap_layers.size())) {
        floor_y = static_cast<float>(g_automap_layers[layer]->getLocationY());
    } else {
        floor_y = static_cast<float>(g_automap_layers.front()->getLocationY());
    }
    int index = FindAutomapLayerAtHeight(floor_y);
    if (index - 1 == g_automap_layer) {
        float left = g_automap_position.x - g_automap_zoom * g_float_half;
        float bottom = g_automap_position.z - g_automap_zoom * g_float_half;
        if (left <= note->position.x && note->position.x <= left + g_automap_zoom &&
            bottom <= note->position.y && note->position.y <= bottom + g_automap_zoom) {
            int screen_x = static_cast<int>((note->position.x - left) / g_automap_zoom * -455.0f);
            int screen_y =
                static_cast<int>((1.0f - (note->position.y - bottom) / g_automap_zoom) * -435.0f);
            int marker_width;
            if (note == g_automap_editing_note) {
                marker_width = static_cast<int>(g_automap_text_marker->GetScaledWidth() * 0.22);
            } else {
                marker_width = static_cast<int>(
                    (1.0f / (g_automap_zoom * 0.00004f)) * 0.44f *
                    static_cast<double>(g_automap_text_marker->GetScaledWidth()) * 0.5f);
            }
            g_automap_text_marker->GetScaledHeight();
            W8ScreenRect rect;
            rect.top =
                (0x20 - screen_y) - ((unsigned)g_wiz_text_font_secondary.height() >> 1);
            rect.left = marker_width / 2 + (0xe - screen_x);
            rect.bottom = rect.top + g_wiz_text_font_secondary.height() + 3;
            rect.right = g_wiz_text_font_secondary.textWidth(note->text) + 3 + rect.left;
            if (rect.left < 0xc) {
                rect.left = 0xc;
            }
            if (rect.top < 0x20) {
                rect.top = 0x20;
            }
            if (0x1d3 < rect.right) {
                rect.right = 0x1d3;
            }
            if (0x1d3 < rect.bottom) {
                rect.bottom = 0x1d3;
            }
            if (rect.right != rect.left && -1 < rect.right - rect.left && rect.bottom != rect.top &&
                -1 < rect.bottom - rect.top) {
                RestoreAutomapRect(&rect);
                InvalidateScreenRects(&rect, 1, 0);
                return 1;
            }
        }
    }
    return 0;
}

/* Persist the visited-cell bitmap and every note's position, layer and text.
   A 0xF00DF00D signature brackets the note records. */
bool SaveAutomapNotes(wiz8::File& handle)
{
    int signature = 0xf00df00d;
    unsigned char saved;
    if (!g_automap_visited_cells.empty()) {
        saved = SavePackedBits(handle, g_automap_visited_cells);
    } else {
        saved = true;
    }
    if (saved == 0) {
        return false;
    }
    if ((handle.write(&signature, 4), true) == 0) {
        return false;
    }
    unsigned int count = static_cast<int>(g_automap_notes.size());
    if ((handle.write(&count, 4), true) == 0) {
        return false;
    }
    for (unsigned int index = 0; index < count; ++index) {
        W8AutomapNote* note = g_automap_notes[index].get();
        if (note != 0) {
            bool ok = (handle.write(&note->position.x, 4), true) != 0 &&
                      (handle.write(&note->position.y, 4), true) != 0 &&
                      (handle.write(&note->layer, 4), true) != 0;
            const std::uint32_t length = wiz8::text::to_utf16(note->text).size() + 1;
            std::vector<std::byte> bytes(length * 2);
            wiz8::text::to_utf16le(note->text, bytes);
            if (!ok) {
                return false;
            }
            if ((handle.write(&length, 4), true) == 0) {
                return false;
            }
            if ((handle.write(bytes.data(), bytes.size()), true) == 0) {
                return false;
            }
        }
    }
    return (handle.write(&signature, 4), true) != 0;
}

/* Release every note, restore the visited-cell bitmap for the loaded level,
   then rebuild the notes from the saved records. */
bool LoadAutomapNotes(wiz8::File& handle)
try
{
    int signature = 0;
    ClearAutomapNotes();
    g_automap_redraw = true;
    if (1 < g_automap_visited_cells.size()) {
        LoadPackedBits(handle, g_automap_visited_cells);
        if (g_automap_visited_cells.size() == static_cast<std::size_t>(g_automap_cell_count)) {
            g_automap_state->pending_cell_lighting = 1;
        } else {
            g_automap_visited_cells.assign(g_automap_cell_count, false);
        }
        g_automap_bounds_dirty = true;
        unsigned int count;
        if ((handle.read(&signature, 4).bytes == static_cast<std::size_t>(4)) != 0 && signature == static_cast<int>(0xf00df00d) &&
            (handle.read(&count, 4).bytes == static_cast<std::size_t>(4)) != 0) {
            for (unsigned int index = 0; index < count; ++index) {
                glm::vec2 position;
                int layer = 0;
                int length = 0;
                /* Retail fed `length` to malloc even when the FileRead chain
                   short-circuited before filling it; natively it starts at zero. */
                bool ok = (handle.read(&position.x, 4).bytes == static_cast<std::size_t>(4)) != 0 &&
                          (handle.read(&position.y, 4).bytes == static_cast<std::size_t>(4)) != 0 &&
                          (handle.read(&layer, 4).bytes == static_cast<std::size_t>(4)) != 0 &&
                          (handle.read(&length, 4).bytes == static_cast<std::size_t>(4)) != 0;
                if (!ok || length < 1 || length > 40) return false;
                std::vector<std::byte> bytes(static_cast<std::size_t>(length) * 2);
                if (handle.read(bytes.data(), bytes.size()).bytes != bytes.size()) return false;
                CreateAutomapNote(&position, layer, wiz8::text::from_utf16le(bytes));
            }
            return (handle.read(&signature, 4).bytes == static_cast<std::size_t>(4)) != 0;
        }
    }
    return false;
} catch (const std::exception&) {
    return false;
}

/* Convert the cursor position to an automap world position on the layer
   below the current one, defaulting the height to the grid minimum when no
   clip plane exists there. */
unsigned char GetAutomapPositionUnderCursor(srVector3T<float>* position)
{
    glm::vec2 point;

    if (GetCursorPositionInViewport(&point) != 0) {
        int layer = g_automap_layer + 1;
        float height;
        if (HasAutomapLayer(layer)) {
            height = static_cast<float>(g_automap_layers[layer]->getLocationY());
        } else {
            height = g_automap_grid_min.y;
        }
        const auto location = AutomapWorldPosition(point);
        *position = glm::vec3(location.x, height, location.y);
        return 1;
    }
    return 0;
}

/* Find the note on the current layer nearest the cursor, limited to a
   zoom-scaled pick radius. */
W8AutomapNote* FindAutomapNoteUnderCursor(void)
{
    W8AutomapNote* result = 0;
    double best = 99999999999.9;
    glm::vec2 point;

    if (g_automap_page != 2 && GetCursorPositionInViewport(&point) != 0) {
        int layer = g_automap_layer + 1;
        if (HasAutomapLayer(layer)) {
            g_automap_layers[layer]->getLocationY();
        }
        const auto location = AutomapWorldPosition(point);
        for (unsigned int index = 0; index < static_cast<unsigned int>(g_automap_notes.size());
             ++index) {
            W8AutomapNote* note = g_automap_notes[index].get();
            if (note->layer == g_automap_layer) {
                // NOLINTNEXTLINE(misc-include-cleaner)
                const float distance = glm::distance(location, note->position);
                if (distance < best && distance < g_automap_zoom * 0.05) {
                    best = distance;
                    result = note;
                }
            }
        }
    }
    return result;
}

/* Descriptive name for the common marker-texture loading operation. */
static std::shared_ptr<stTextureFile> LoadAutomapMarkerTexture(const char* path)
{
    auto texture = std::make_shared<stTextureFile>(path, 0);
    texture->enableHint(srTextureIFace::HINT_ONE_BIT_ALPHA);
    texture->enableHint(srTextureIFace::HINT_NO_MIPMAPS);
    texture->setMipmap(srTextureIFace::MIPMAP_NONE);
    texture->setCorrection(srTextureIFace::CORRECTION_FASTEST);
    texture->setMagFilter(srTextureIFace::FILTER_BEST);
    texture->setMinFilter(srTextureIFace::FILTER_BEST);
    texture->loadSurface();
    return texture;
}

static void CreateAutomapMarkerSprite(const char* path, stModelInstance2D*& marker)
{
    std::shared_ptr<stTextureFile> texture = LoadAutomapMarkerTexture(path);
    srColorSurface* surface = texture->getSurface();
    if (surface != 0) {
        marker = CreateSpriteFromTexture(texture, surface->getWidth() * g_viewport_x_scale,
                                         surface->getHeight() * g_viewport_y_scale, true, false);
        static_cast<srMeshModel*>(marker->getModel())
            ->enable(srMeshModel::CONTROL_SORTED_RENDERING);
        surface->setFilter(&srBSplineFilter);
    }
}

/* Load the automap marker textures and build the party, monster and text
   marker sprites over them. */
void CreateAutomapMarkerSprites(void)
{
    if (g_automap_party_marker == 0) {
        std::shared_ptr<stTextureFile> texture =
            LoadAutomapMarkerTexture("Data\\Automap\\map_partymarker_a.tga");
        srColorSurface* surface = texture->getSurface();
        if (surface != 0) {
            g_automap_party_marker =
                CreateSpriteFromTexture(texture, surface->getWidth() * g_viewport_x_scale,
                                        surface->getHeight() * g_viewport_x_scale, true, true);
            g_automap_party_marker->setParent(g_scene_square, 1);
            static_cast<srMeshModel*>(g_automap_party_marker->getModel())
                ->enable(srMeshModel::CONTROL_SORTED_RENDERING);
            surface->setFilter(&srBSplineFilter);
            g_automap_party_marker->SetGlowEnabled(true);
            glm::vec4 first;
            glm::vec4 second;
            first = glm::vec4(0.0f, 0.25f, 0.0f, 1.0f);
            second = glm::vec4(0.0f, 0.75f, 0.0f, 1.0f);
            g_automap_party_marker->SetGlowColors(first, second);
            g_automap_party_marker->render_state.render_depth = 2000;
        }
    }
    if (g_automap_friendly_marker == 0) {
        CreateAutomapMarkerSprite("Data\\Automap\\map_monsterfriendly_a.tga",
                                  g_automap_friendly_marker);
    }
    if (g_automap_neutral_marker == 0) {
        CreateAutomapMarkerSprite("Data\\Automap\\map_monsterneutral_a.tga",
                                  g_automap_neutral_marker);
    }
    if (g_automap_hostile_marker == 0) {
        CreateAutomapMarkerSprite("Data\\Automap\\map_monsterhostile_a.tga",
                                  g_automap_hostile_marker);
    }
    if (g_automap_text_marker == 0) {
        CreateAutomapMarkerSprite("Data\\Automap\\map_textmarker_a.tga", g_automap_text_marker);
    }
}

/* Redraw the transient party, monster, item and note markers over the automap
   surface. */
void RenderAutomapMarkers(void)
{
    bool detect_all = PartyHasCondition(0x40);
    ClearAutomapMarkers();
    float left = g_automap_position.x - g_automap_zoom * g_float_half;
    srVector3T<float> point(g_automap_saved_camera.position.x, 1.0f,
                            g_automap_saved_camera.position.z);
    float top = g_automap_position.z - g_automap_zoom * g_float_half;
    if (point.x < left || left + g_automap_zoom < point.x || point.z < top ||
        top + g_automap_zoom < point.z) {
        g_automap_party_marker->setFlag(srNode::FLAG_DISABLE);
    } else {
        int x = 0xc - static_cast<int>((point.x - left) / g_automap_zoom * -455.0f);
        int y = 0x20 - static_cast<int>((1.0f - (point.z - top) / g_automap_zoom) * -435.0f);
        float factor = (g_float_one / (g_automap_zoom * 0.00004f)) * 0.44f;
        glm::dvec3 scale(factor, factor, factor);
        g_automap_party_marker->setScale(scale);
        if (g_automap_party_marker->GetScaledHeight() < 1) {
            g_automap_party_marker->setScale(glm::dvec3(1.0, 1.0, 1.0));
            factor = g_float_one / g_automap_party_marker->GetScaledHeight();
            scale = glm::dvec3(factor);
            g_automap_party_marker->setScale(scale);
        }
        unsigned short width = g_automap_party_marker->GetScaledWidth();
        unsigned short height = g_automap_party_marker->GetScaledHeight();
        PositionToolTipNode(g_automap_party_marker, x - (width >> 1), y - (height >> 1), false);
        g_automap_party_marker->setRotation(0.0, 0.0, -static_cast<double>(GetCameraYawRadians()));
        g_automap_party_marker->clearFlag(srNode::FLAG_DISABLE);
        g_automap_party_marker->setParent(0, 1);
        g_automap_party_marker->setParent(g_scene_square, 1);
    }
    unsigned int count = gXStatus.monsters.size();
    for (unsigned int index = 0; index < count; ++index) {
        W8MonsterInfo* info = GetMonsterListEntry(index);
        W8Monster* monster = info->p3D;
        srVector3T<float> location;
        location = 0.0f;
        location = monster->m_pRep->GetLocation();
        FindAutomapLayerAtHeight(location.y);
        if (g_automap_show_all_monsters || detect_all ||
            (!monster->disabled && info->party_threat.sight_state == W8_SIGHT_SEEN)) {
            left = g_automap_position.x - g_automap_zoom * g_float_half;
            top = g_automap_position.z - g_automap_zoom * g_float_half;
            if (location.x < left || left + g_automap_zoom < location.x || location.z < top ||
                top + g_automap_zoom < location.z) {
                continue;
            }
            int x = 0xc - static_cast<int>((location.x - left) / g_automap_zoom * -455.0f);
            int y = 0x20 - static_cast<int>((1.0f - (location.z - top) / g_automap_zoom) * -435.0f);
            stModelInstance2D* marker;
            switch (info->ubDisposition) {
            case W8_DISPOSITION_NEUTRAL:
                marker = CreateAutomapMonsterMarker(1);
                break;
            case W8_DISPOSITION_HOSTILE:
                marker = CreateAutomapMonsterMarker(2);
                break;
            case W8_DISPOSITION_FRIENDLY:
                marker = CreateAutomapMonsterMarker(0);
                break;
            }
            unsigned short width = marker->GetScaledWidth();
            x -= width >> 1;
            unsigned short height = marker->GetScaledHeight();
            y -= height >> 1;
            marker->setParent(g_scene_fullscreen, 1);
            Position2DNodeUnsnapped(marker, x, y);
        }
    }
    for (W8WorldItem* world_item = GetNextWorldItem(true); world_item != 0;
         world_item = GetNextWorldItem(false)) {
        W8Item* item = world_item->p3D;
        if (item == 0) {
            continue;
        }
        srVector3T<float> location;
        location = item->GetSearchPosition();
        int layer = FindAutomapLayerAtHeight(location.y);
        if ((static_cast<W8ItemRep*>(item->m_pRep)->flags & W8_ITEM_ENTITY_NO_PICKUP) == 0 &&
            (detect_all ||
             (static_cast<W8ItemRep*>(item->m_pRep)->flags & W8_ITEM_ENTITY_RADAR_SEEN) != 0 ||
             HasCameraLineOfSight(&location)) &&
            layer - 1 == g_automap_layer) {
            left = g_automap_position.x - g_automap_zoom * g_float_half;
            top = g_automap_position.z - g_automap_zoom * g_float_half;
            if (location.x < left || left + g_automap_zoom < location.x || location.z < top ||
                top + g_automap_zoom < location.z) {
                continue;
            }
            int x = 0xc - static_cast<int>((location.x - left) / g_automap_zoom * -455.0f);
            int y = 0x20 - static_cast<int>((1.0f - (location.z - top) / g_automap_zoom) * -435.0f);
            stModelInstance2D* marker = CreateAutomapItemMarker(world_item->item.iItemNo);
            if (marker != 0) {
                unsigned short width = marker->GetScaledWidth();
                unsigned short height = marker->GetScaledHeight();
                PositionToolTipNode(marker, x - (width >> 1), y - (height >> 1), false);
                marker->setParent(g_scene_fullscreen, 1);
            }
        }
    }
    if (g_automap_page != 2) {
        SDL_Rect clip = {0xc, 0x20, 0x1d3 - 0xc, 0x1d3 - 0x20};
        ScopedClipRect clipped(GetFrameBufferSurface(), clip);
        g_wiz_text_font_secondary.setShade(Font::kNormalShade);
        int font_height = g_wiz_text_font_secondary.height();
        unsigned int count = static_cast<int>(g_automap_notes.size());
        for (unsigned int index = 0; index < count; ++index) {
            W8AutomapNote* note = g_automap_notes[index].get();
            int layer = note->layer + 1;
            if (HasAutomapLayer(layer)) {
                g_automap_layers[layer]->getLocationY();
            }
            left = g_automap_position.x - g_automap_zoom * g_float_half;
            top = g_automap_position.z - g_automap_zoom * g_float_half;
            if (note->position.x < left || left + g_automap_zoom < note->position.x ||
                note->position.y < top || top + g_automap_zoom < note->position.y) {
                continue;
            }
            int x = 0xc - static_cast<int>((note->position.x - left) / g_automap_zoom * -455.0f);
            int y = 0x20 -
                    static_cast<int>((1.0f - (note->position.y - top) / g_automap_zoom) * -435.0f);
            stModelInstance2D* marker = CreateAutomapTextMarker();
            if (note == g_automap_editing_note)
                marker->setScale(glm::dvec3(0.22f, 0.22f, 0.22f));
            if (marker != 0) {
                unsigned short width = marker->GetScaledWidth();
                x -= width >> 1;
                unsigned short height = marker->GetScaledHeight();
                y -= height >> 1;
                Position2DNodeUnsnapped(marker, x, y);
                marker->setParent(g_scene_fullscreen, 1);
                if (note->layer == g_automap_layer && !note->text.empty() &&
                    (g_automap_page == 0 || note == g_automap_hovered_note ||
                     note == g_automap_editing_note)) {
                    height = marker->GetScaledHeight();
                    y += (height >> 1) - ((font_height & 0xffff) >> 1);
                    width = marker->GetScaledWidth();
                    x += width + 2;
                    DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                                          {x, y}, note->text);
                    if (note == g_automap_editing_note) {
                        marker->setScale(glm::dvec3(0.22f, 0.22f, 0.22f));
                        int color = Get16BPPColor(SDL_Color{239, 155, 86, SDL_ALPHA_OPAQUE});
                        int length = g_wiz_text_font_secondary.textWidth(note->text);
                        const SDL_Rect top_edge{x - 1, y, length + 4, 1};
                        const SDL_Rect bottom_edge{x - 1, y + (font_height & 0xffff),
                                                   length + 4, 1};
                        const SDL_Rect left_edge{x - 1, y, 1, (font_height & 0xffff) + 1};
                        const SDL_Rect right_edge{x + length + 2, y, 1,
                                                  (font_height & 0xffff) + 1};
                        auto& framebuffer = GetFrameBufferSurface();
                        SDL_FillSurfaceRect(&framebuffer, &top_edge, static_cast<Uint16>(color));
                        SDL_FillSurfaceRect(&framebuffer, &bottom_edge, static_cast<Uint16>(color));
                        SDL_FillSurfaceRect(&framebuffer, &left_edge, static_cast<Uint16>(color));
                        SDL_FillSurfaceRect(&framebuffer, &right_edge, static_cast<Uint16>(color));
                    }
                }
            }
        }
    }
}

static void ScaleAutomapMarker(stModelInstance2D* marker, float factor)
{
    if (marker->GetScaledHeight() * factor < g_float_one) {
        marker->setScale(glm::dvec3(1.0, 1.0, 1.0));
        factor = g_float_one / marker->GetScaledHeight();
    }
    marker->setScale(glm::dvec3(factor, factor, factor));
}

/* Create a scaled automap blip for a world item by blitting its inventory icon
   into a scratch surface and wrapping it in a 2D brush. */
stModelInstance2D* CreateAutomapItemMarker(int item_id)
{
    auto& video_object = g_item_video_objects.Sprites(item_id);
    if (video_object.frames.empty()) {
        return 0;
    }
    const SpriteFrame& properties = video_object.frames.front();
    auto surface = std::make_shared<srColorSurface>(srPixelConvert::SURFACE_ARGB1555, 0x40, 0x40);
    surface->fill(0);
    surface->setFilter(&srBSplineFilter);
    int y = (0x40 - properties.height) / 2;
    int x = (0x40 - properties.width) / 2;
    BlitVideoObjectToColorSurface(video_object, 0, *surface, x, y);
    float factor = (g_float_one / (g_automap_zoom * 0.00004f)) * 0.44f;
    stModelInstance2D* marker = static_cast<stModelInstance2D*>(
        MakePolygonBrush(g_scene_fullscreen, surface, surface->getWidth() * g_viewport_x_scale,
                         surface->getHeight() * g_viewport_y_scale, 0.0f, 0.0f, 1.0f, 1.0f, 1));
    if (marker == 0) {
        return 0;
    }
    static_cast<srMeshModel*>(marker->getModel())->enable(srMeshModel::CONTROL_SORTED_RENDERING);
    g_automap_markers.push_back(marker);
    marker->SetGlowEnabled(true);
    glm::vec4 first;
    glm::vec4 second;
    second = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    first = glm::vec4(0.0f, 0.0f, 0.25f, 1.0f);
    marker->SetGlowColors(first, second);
    marker->setRenderDepth(2000);
    ScaleAutomapMarker(marker, factor);
    return marker;
}

/* Clone the friendly, neutral or hostile blip sprite for one monster entry and
   register the clone for release after the pass. */
stModelInstance2D* CreateAutomapMonsterMarker(int type)
{
    stModelInstance2D* marker = new stModelInstance2D(0);
    if (marker == 0) {
        return 0;
    }
    switch (type) {
    case 0: {
            glm::vec4 first;
            glm::vec4 second;
        *marker = *g_automap_friendly_marker;
        second = glm::vec4(0.0f, 0.5f, 0.0f, 1.0f);
        first = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        marker->SetGlowColors(first, second);
        break;
    }
    case 1: {
            glm::vec4 first;
            glm::vec4 second;
        *marker = *g_automap_neutral_marker;
        second = glm::vec4(0.25f, 0.25f, 0.0f, 1.0f);
        first = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        marker->SetGlowColors(first, second);
        break;
    }
    case 2: {
            glm::vec4 first;
            glm::vec4 second;
        *marker = *g_automap_hostile_marker;
        second = glm::vec4(0.5f, 0.0f, 0.0f, 1.0f);
        first = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        marker->SetGlowColors(first, second);
        break;
    }
    }
    g_automap_markers.push_back(marker);
    marker->SetGlowEnabled(true);
    marker->setRenderDepth(2000);
    /* Retail scales by 0.85 as a step of its own; written as one product, VC6
       folds 0.44 * 0.85 into a single constant. */
    float factor = (g_float_one / (g_automap_zoom * 0.00004f)) * 0.44f;
    factor *= 0.85f;
    ScaleAutomapMarker(marker, factor);
    return marker;
}

/* Clone the text blip sprite and register the clone for release after the
   pass. */
stModelInstance2D* CreateAutomapTextMarker(void)
{
    stModelInstance2D* marker = new stModelInstance2D(0);
    if (marker == 0) {
        return 0;
    }
    *marker = *g_automap_text_marker;
    g_automap_markers.push_back(marker);
    float factor = (g_float_one / (g_automap_zoom * 0.00004f)) * 0.44f * g_float_half;
    ScaleAutomapMarker(marker, factor);
    marker->SetGlowEnabled(true);
    glm::vec4 first;
    glm::vec4 second;
    second = glm::vec4(1.0f, 0.4f, 0.0f, 1.0f);
    first = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    marker->SetGlowColors(first, second);
    marker->setRenderDepth(2000);
    return marker;
}
/* While a note is being edited, forward key events into its text buffer and
   let left-button-up drop the note at the cursor. */
unsigned char HandleAutomapNoteInput(const InputEvent* input)
{
    if (input->type != InputEventType::KeyDown && input->type != InputEventType::KeyRepeat &&
        input->type != InputEventType::Text) {
        if (input->type == InputEventType::LeftButtonUp && IsCursorInsideViewport()) {
            glm::vec2 point;
            if (GetCursorPositionInViewport(&point) != 0) {
                int layer = g_automap_layer + 1;
                if (HasAutomapLayer(layer)) {
                    g_automap_layers[layer]->getLocationY();
                }
                g_automap_editing_note->position = AutomapWorldPosition(point);
                g_automap_redraw = true;
            }
        }
        return 0;
    }
    const bool committed = input->type == InputEventType::Text;
    const char32_t key = committed ? input->text : 0;
    InputEvent normalized = *input;
    if (committed) { normalized.type = InputEventType::KeyDown; normalized.key = 0; input = &normalized; }
    ShowAutomapNoteTooltip(g_automap_editing_note);
    auto& text = g_automap_editing_note->text;
    const auto last = text.empty() ? 0 : text.size() - 1; // Trailing cursor marker.
    if (key >= 32 && key <= 0x10ffff &&
        wiz8::text::to_utf16(text).size() + (key > 0xffff ? 2 : 1) <= 40) {
        std::string inserted;
        utf8::append(key, std::back_inserter(inserted));
        text.insert(last, inserted);
        g_automap_redraw = true;
    } else {
        if (input->key == VK_BACK) {
            if (last) text.erase(wiz8::text::previous_grapheme(text, last), last - wiz8::text::previous_grapheme(text, last));
            g_automap_redraw = true;
        } else if (input->key == VK_RETURN) {
            text.resize(last);
            if (text.empty()) {
                EraseAutomapNote(g_automap_editing_note);
                g_automap_redraw = true;
            }
            if (g_automap_tool != 0) { g_automap_tool = 0; SetAutomapCursorImage(0); }
            g_automap_editing_note = 0;
            g_automap_redraw = true;
        } else if (input->key == VK_ESCAPE) {
            EraseAutomapNote(g_automap_editing_note);
            g_automap_redraw = true;
            if (g_automap_tool != 0) {
                g_automap_tool = 0;
                SetAutomapCursorImage(0);
            }
        }
    }
    if (g_automap_editing_note != 0) {
        RenderAutomapMarkers();
        return 1;
    }
    SetNativeTextInputEnabled(false);
    if (g_automap_tool != 0) {
        g_automap_tool = 0;
        SetAutomapCursorImage(0);
    }
    return 1;
}

/* Automap key handling: screen exit, zoom keys, page cycling and the
   per-tool and cheat-gated bindings. */
unsigned char HandleAutomapKey(const InputEvent* input)
{
    if (input->type != InputEventType::KeyDown) {
        return 0;
    }
    MGSKeyBinding* binding =
        g_mgs_keyboard->GetBinding(g_mgs_keyboard->FindBinding(W8_MGS_COMMAND_AUTOMAP));
    if (input->key == binding->key && input->modifiers == binding->modifiers) {
        RequestScreenTransition();
        return 1;
    }
    switch (input->key) {
    case 8:
        /* Backspace zooms out one step, or all the way from the full view. */
        if (g_automap_position.y < g_automap_top_y) {
            if (g_automap_zoom_mode == 1) {
                g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
                srVector3T<float> position = (g_automap_bounds_max + g_automap_bounds_min) / 2.0;
                position.y = g_automap_top_y;
                SetAutomapCameraPoint(&position);
                SetAutomapToolCursor(g_automap_tool);
                SetAutomapButtonMode(0);
                return 1;
            }
            float floor = g_automap_position.y - g_automap_zoom;
            float height = g_automap_top_y - (g_automap_top_y - floor) * 0.5f;
            if (height < g_automap_position.y) {
                ResetAutomapZoom();
                return 1;
            }
            g_automap_position.y = height;
            SetAutomapButtonMode(1);
            g_automap_zoom = g_automap_position.y - floor;
            SetAutomapCameraPoint(&g_automap_position);
            SetAutomapToolCursor(g_automap_tool);
        }
        return 1;
    case 0xd: {
        AutomapZoomInButton();
        return 1;
    }
    case 0x1b:
    drop_tool:
        /* Escape drops the tool, or leaves the screen when none is held. */
        if (g_automap_tool == 0) {
            RequestScreenTransition();
            return 1;
        }
        g_automap_tool = 0;
        SetAutomapToolCursor(0);
        return 1;
    case 0x20:
        AutomapCyclePageButton();
        return 1;
    case 0x23:
        RestoreAutomapCameraPosition();
        break;
    case 0x24: {
        g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
        {
            srVector3T<float> center(g_automap_bounds_min.x + g_automap_bounds_max.x,
                                     g_automap_bounds_max.y + g_automap_bounds_min.y,
                                     g_automap_bounds_min.z + g_automap_bounds_max.z);
            srVector3T<float> position = center / 2.0;
            position.y = g_automap_top_y;
            SetAutomapCameraPoint(&position);
        }
        SetAutomapToolCursor(g_automap_tool);
        SetAutomapButtonMode(0);
        return 1;
    }
    case 0x2d:
        if (g_automap_tool == 2) {
            return 1;
        }
        g_automap_tool = 2;
        SetAutomapToolCursor(2);
        return 1;
    case 0x2e:
        if (g_automap_tool == 3) {
            return 1;
        }
        g_automap_tool = 3;
        SetAutomapToolCursor(3);
        return 1;
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
        SetAutomapLayer(input->key - 0x31);
        return 1;
    case 0x41:
        /* Developer: reveal the whole map. */
        if (g_dev_mode) {
            std::ranges::fill(g_automap_visited_cells, true);
            g_automap_bounds_dirty = true;
            g_automap_redraw = true;
            g_automap_overlay_redraw = true;
            UpdateAutomapBounds();
            g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
            {
                srVector3T<float> center(g_automap_bounds_min.x + g_automap_bounds_max.x,
                                         g_automap_bounds_max.y + g_automap_bounds_min.y,
                                         g_automap_bounds_min.z + g_automap_bounds_max.z);
                srVector3T<float> position = center / 2.0;
                position.y = g_automap_top_y;
                SetAutomapCameraPoint(&position);
            }
            SetAutomapToolCursor(g_automap_tool);
            SetAutomapButtonMode(0);
            RenderAutomapFrame();
            return 1;
        }
        break;
    case 0x43:
        /* Developer: forget the whole map. */
        if (g_dev_mode) {
            std::ranges::fill(g_automap_visited_cells, false);
            g_automap_bounds_dirty = true;
            UpdateAutomapBounds();
            return 1;
        }
        break;
    case 0x49:
        if (g_dev_mode) {
            g_automap_developer_i_toggle = !g_automap_developer_i_toggle;
            g_automap_redraw = true;
            return 1;
        }
        break;
    case 0x53:
        if (g_dev_mode) {
            g_automap_redraw = true;
            g_automap_show_all_monsters = !g_automap_show_all_monsters;
            return 1;
        }
        break;
    case 0x70:
        if (g_dev_mode) {
            g_automap_saved_camera.position = g_automap_position;
            ResetCurrentEnvironment();
            ResetCurrentEnvironment();
            goto drop_tool;
        }
        break;
    case 0x79:
        if (g_dev_mode) {
            g_automap_state->blink_enabled = !g_automap_state->blink_enabled;
            return 1;
        }
        break;
    }
    return 0;
}

/* Release the previous level's automap query state, then read the cell size
   and record count. A level with no records still gets one zero cell so the
   visited-bit arrays and record table stay valid; otherwise the key list is
   read whole and every cell key is inserted with its one-based index. */
unsigned char ReadAutomapNodes(wiz8::File* hFile)
try
{
    g_automap_visited_cells.clear();
    g_automap_lit_cells.clear();
    g_automap_cell_keys.clear();
    g_automap_cell_index.clear();

    hFile->read_exact(&g_automap_grid_cell_size, 4);
    unsigned char ok = (hFile->read(&g_automap_cell_count, 4).bytes == static_cast<std::size_t>(4));
    if (g_automap_cell_count == 0) {
        g_automap_visited_cells.assign(1, false);
        g_automap_lit_cells.assign(1, false);
        g_automap_cell_keys.assign(1, 0);
        g_automap_cell_count = 1;
        return ok;
    }

    g_automap_visited_cells.assign(g_automap_cell_count, false);
    g_automap_lit_cells.assign(g_automap_cell_count, false);
    g_automap_cell_keys.assign(g_automap_cell_count, 0);

    const std::size_t key_bytes = g_automap_cell_keys.size() * sizeof(unsigned int);
    unsigned char success = 0;
    if (ok != 0) {
        success = hFile->read(g_automap_cell_keys.data(), key_bytes).bytes == key_bytes;
    }
    for (int index = 0; index < g_automap_cell_count; ++index) {
        g_automap_cell_index.insert_or_assign(g_automap_cell_keys[index], index + 1);
    }
    return success;
} catch (const std::exception&) {
    return false;
}

/* Pack a world position into an automap node key: eleven bits of z, then
   eleven of x, then ten of y, each scaled to grid cells. */
unsigned int AutomapNodeKey(const srVector3T<float>* position)
{
    return ((static_cast<unsigned int>(position->z / g_automap_grid_cell_size) & 0x7ff) |
            static_cast<unsigned int>(position->x / g_automap_grid_cell_size) << 11)
               << 10 |
           (static_cast<unsigned int>(position->y / g_automap_grid_cell_size) & 0x3ff);
}

/* The large-map level set: the same levels that get the 30000-unit automap
   range in ResetAutomapView also get 4000-unit cells instead of 2000. */
bool AutomapLevelIsLarge(void)
{
    if (g_status.current_level == 0x18 ||
        (g_status.current_level > 0x1a && g_status.current_level <= 0x22)) {
        return true;
    }
    return false;
}
