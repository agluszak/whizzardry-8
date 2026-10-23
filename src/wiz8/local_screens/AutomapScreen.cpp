#include "wiz8/local_screens/AutomapScreen.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int2.hpp"
#include "layouts/main_game_screen.h"
#include "local_code/Factions.h"
#include "mgs_commands.h"
#include "sight_state.h"
#include "wiz8/asset_paths.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/checked_integer.h"
#include "wiz8/chunk.h"
#include "wiz8/cursor.h"
#include "wiz8/design_rect.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/script.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/index_algorithms.h"
#include "wiz8/item_spawning.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/packed_bits.h"
#include "wiz8/platform/input.h"
#include "wiz8/pointer.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/quality.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/surface_storage.h"
#include "wiz8/ui_input.h"
#include "wiz8/unicode.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

static std::vector<std::unique_ptr<W8AutomapNote>> g_automap_notes;

/* The automap's module state. The marker sprites exist while the screen is
   open; the rest persists with the level. */

/* One bit per automap cell: raised when the party camera enters the cell. */

/* One bit per visited cell already turned into vertex lights. */

/* Packed cell keys read from the level file, indexed by cell number. */

/* Packed cell key -> cell number + 1. */

std::unique_ptr<OverlayQuad> g_automap_party_marker;
std::unique_ptr<OverlayQuad> g_automap_friendly_marker;
std::unique_ptr<OverlayQuad> g_automap_neutral_marker;
std::unique_ptr<OverlayQuad> g_automap_hostile_marker;
std::unique_ptr<OverlayQuad> g_automap_text_marker;

/* Leave releases the pointed-to objects and erases their vector entries. */
std::vector<std::unique_ptr<OverlayQuad>> g_automap_markers;

std::vector<std::unique_ptr<ClipPlane>> g_automap_created_layers;
std::unique_ptr<AutomapButtons> g_automap_buttons;
struct W8AutomapState {
    unsigned char pending_cell_lighting = 0;
};
static std::unique_ptr<W8AutomapState> g_automap_state;

SDLSurfaceOwner g_automap_surface{nullptr, SDL_DestroySurface};

void AutomapZoomInButton(void);
static void ZoomAutomapOut();
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
int g_automap_button_tooltips[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
/* Left-click actions. ResetAutomapZoom / RestoreAutomapCameraPosition are the
   already-recovered bodies at the retail callback addresses. */
void (*g_automap_button_callbacks[16])(void) = {
    AutomapZoomInButton,
    ZoomAutomapOut,
    ResetAutomapZoom,
    AutomapSelectNoteToolButton,
    AutomapSelectEraseToolButton,
    AutomapCyclePageButton,
    AutomapCyclePageButton,
    AutomapCyclePageButton,
    AutomapLayerDownButton,
    AutomapLayerUpButton,
    AutomapPanNorthButton,
    AutomapPanSouthButton,
    AutomapPanWestButton,
    AutomapPanEastButton,
    RestoreAutomapCameraPosition,
    AutomapExitButton,
};
/* Catalog entries supplying filenames and state-frame offsets for each button. */
int g_automap_button_catalogs[16] = {388, 392, 396, 380, 384, 348, 352, 356,
                                     340, 344, 360, 364, 368, 372, 376, 336};
/* Screen positions for the sixteen buttons. */
glm::ivec2 g_automap_button_positions[16] = {
    {490, 124}, {541, 124}, {591, 124}, {490, 70},  {541, 70},  {591, 70},  {591, 70},  {591, 70},
    {490, 164}, {591, 164}, {540, 218}, {540, 326}, {486, 272}, {594, 272}, {540, 272}, {588, 441},
};
glm::ivec2 g_automap_cursor_offsets[5] = {{0, 0}, {8, 7}, {1, 24}, {1, 24}, {8, 7}};
float g_automap_range = 10000.0f;
int g_automap_layer = -1;
bool g_automap_bounds_dirty = true;
std::vector<ClipPlane*> g_automap_layers;
glm::vec3 g_automap_bounds_max;
glm::vec3 g_automap_grid_max;
glm::vec3 g_automap_grid_min;
glm::vec3 g_automap_position;
float g_automap_top_y;
glm::vec3 g_automap_bounds_min;
DesignRect g_automap_viewport;
glm::vec3 g_automap_grid_origin;
// GLOBAL: WIZ8 0x0068f250
W8AutomapTool g_automap_tool = W8AutomapTool::Select;
// GLOBAL: WIZ8 0x0068f254
bool g_automap_cursor_inside;
bool g_automap_redraw;
unsigned int g_automap_page;
bool g_automap_overlay_redraw;
float g_automap_zoom;
/* The camera's near clip: how far above the selected layer it floats. */
float g_automap_near_clip = 1.0f;
enum class AutomapZoomMode { ZoomInOnly, Both, ZoomOutOnly };
// GLOBAL: WIZ8 0x0068f278
static AutomapZoomMode g_automap_zoom_mode = AutomapZoomMode::ZoomInOnly;

bool g_automap_position_initialized;
W8AutomapNote* g_automap_editing_note; // Non-owning; g_automap_notes owns the note.
W8AutomapNote* g_automap_hovered_note;

bool HandleAutomapKey(const InputEvent* input);
bool HandleAutomapNoteInput(const InputEvent* input);
bool ShowAutomapNoteTooltip(W8AutomapNote* note);
bool ZoomAutomapIn(const glm::vec2* point);

static glm::vec2 AutomapWorldPosition(const glm::vec2& cursor)
{
    return glm::vec2(g_automap_position.x, g_automap_position.z) +
           (cursor - 0.5f) * glm::vec2(g_automap_zoom, -g_automap_zoom);
}
void SetAutomapCameraPoint(glm::vec3* position);
void SetAutomapButtonMode(AutomapZoomMode update);
void RenderAutomapFrame(void);
void UpdateAutomapPageButtons(void);
void ResetAutomapLighting(void);
void LightAutomapCell(const glm::vec3* position);
unsigned int LightPendingAutomapCells(unsigned int max_count);
int RestoreAutomapRect(DesignRect* rect);
void SetAutomapLayer(int layer);
OverlayQuad* CreateAutomapItemMarker(int item_id);
OverlayQuad* CreateAutomapMonsterMarker(int type);
OverlayQuad* CreateAutomapTextMarker(void);

static void SetAutomapCursorImage(int tool)
{
    const auto artwork = CatalogArtwork::Load(tool + 0x14b);
    SetMouseCursorFromVideoObject(*artwork.spriteSheet(), artwork.imageOffset(),
                                  g_automap_cursor_offsets[tool].x,
                                  g_automap_cursor_offsets[tool].y);
    ScreenPointer().showCustom();
    RefreshMouseCursorTexture();
}

/* With no tool selected inside the map, use zoom-in only above the threshold.
   Retail's FNSTSW/TEST 0x41 also selects zoom-out for an unordered zoom. */
void SetAutomapToolCursor(W8AutomapTool tool)
{
    int cursor = static_cast<int>(tool);
    if (tool == W8AutomapTool::Select && g_automap_cursor_inside) {
        cursor = g_automap_zoom > 2.5e+04f ? 1 : 4;
    }
    SetAutomapCursorImage(cursor);
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
        if (HasAutomapLayer(layer) && g_automap_layers[layer]->transform.getLocation().y < height) {
            break;
        }
    }
    return layer;
}

/* Redraw and screen teardown share ownership of transient markers. */
static void ClearAutomapMarkers()
{
    while (!g_automap_markers.empty()) {
        auto marker = std::move(g_automap_markers.front());
        g_automap_markers.erase(g_automap_markers.begin());
        marker.reset();
    }
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
    if (GetLoadedLevelID() == 0x18 || (GetLoadedLevelID() > 0x1a && GetLoadedLevelID() <= 0x22)) {
        g_automap_range = 30000.0f;
    } else {
        g_automap_range = 10000.0f;
    }
    if (GetWorldSpatialIndex() == 0) {
        g_automap_grid_min = glm::vec3(-250000.0f);
        g_automap_grid_max = glm::vec3(250000.0f);
    } else {
        const auto bounds = GetWorldSpatialIndex()->m_spatial.GetClippedBounds();
        g_automap_grid_min = bounds.minimum;
        g_automap_grid_max = bounds.maximum;
    }
    g_automap_grid_origin = g_automap_grid_min;
    g_automap_bounds_dirty = true;
    float span_x = g_automap_grid_max.x - g_automap_grid_min.x;
    float span_z = g_automap_grid_max.z - g_automap_grid_min.z;
    float largest = span_z < span_x ? span_x : span_z;
    if (largest <= 2.5e+04f) {
        g_automap_top_y = 25000.0f;
    } else {
        g_automap_top_y = largest;
    }
    g_automap_position_initialized = false;
    g_automap_layer = 0;

    glm::vec3 camera = cameraLocation();
    glm::vec3 relative = camera - g_automap_grid_origin;
    SDL_Keycode key = AutomapNodeKey(&relative);
    int cell = LookupIndexValue(GetWorld()->automap.index, key);
    if (cell > 1) {
        SetBit(GetWorld()->automap.visited, cell - 1);
        return;
    }
    relative.y = camera.y + GetWorld()->automap.grid_cell_size - g_automap_grid_origin.y;
    key = AutomapNodeKey(&relative);
    cell = LookupIndexValue(GetWorld()->automap.index, key);
    if (cell > 1) {
        SetBit(GetWorld()->automap.visited, cell - 1);
    }
}

/* Pack `position` into a cell key (retrying one cell higher on miss). When the
   record table knows the cell, mark it visited and return 1 only if that bit
   was newly raised — used by the world camera update to gate automap lighting. */
bool AutomapHasCellAt(const glm::vec3* position)
{
    glm::vec3 relative = *position - g_automap_grid_origin;
    SDL_Keycode key = AutomapNodeKey(&relative);
    int cell = LookupIndexValue(GetWorld()->automap.index, key);
    if (cell > 1) {
        if (!SetBit(GetWorld()->automap.visited, cell - 1)) {
            return true;
        }
        return false;
    }
    relative.y = position->y + GetWorld()->automap.grid_cell_size - g_automap_grid_origin.y;
    key = AutomapNodeKey(&relative);
    cell = LookupIndexValue(GetWorld()->automap.index, key);
    if (cell > 1) {
        if (!SetBit(GetWorld()->automap.visited, cell - 1)) {
            return true;
        }
    }
    return false;
}

/* Center the map over the party. */
void RestoreAutomapCameraPosition(void)
{
    const auto party = cameraLocation();
    g_automap_position.x = party.x;
    g_automap_position.z = party.z;
    SetAutomapCameraPoint(&g_automap_position);
}

/* Build the sixteen chrome buttons from the adjacent catalog / callback /
   tooltip / position tables. */
void CreateAutomapButtons(void)
{
    auto buttons = std::make_unique<AutomapButtons>();
    for (size_t index = 0; index < buttons->buttons.size(); ++index) {
        const int catalog_id = g_automap_button_catalogs[index];
        auto artwork = CatalogArtwork::Load(catalog_id);
        auto sprites = artwork.spriteSheet();
        if (!sprites) continue;
        const int base_frame = artwork.imageOffset();
        auto button = std::make_unique<W8ButtonWidget>(buttons->input);
        const auto action = g_automap_button_callbacks[index];
        W8ButtonCallback callback;
        if (action) callback = [action](W8ButtonWidget*) { action(); };
        if (button->Configure(
                ButtonGraphics::Load(std::move(sprites), base_frame + 3, base_frame, base_frame + 1,
                                     base_frame + 2, base_frame + 2),
                {.on_click = callback, .tooltip = g_automap_button_tooltips[index]})) {
            button->SetPosition(g_automap_button_positions[index].x,
                                g_automap_button_positions[index].y);
        }
        buttons->buttons[index] = std::move(button);
    }
    g_automap_buttons = std::move(buttons);
}

void AutomapZoomInButton(void)
{
    const glm::vec2 center(0.5f);
    ZoomAutomapIn(&center);
}

/* Zoom out one step or restore the full explored span. */
static void ZoomAutomapOut()
{
    if (g_automap_position.y < g_automap_top_y) {
        if (g_automap_zoom_mode == AutomapZoomMode::Both) {
            g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
            glm::vec3 position = (g_automap_bounds_max + g_automap_bounds_min) / 2.0f;
            position.y = g_automap_top_y;
            SetAutomapCameraPoint(&position);
            SetAutomapToolCursor(g_automap_tool);
            SetAutomapButtonMode(AutomapZoomMode::ZoomInOnly);
        } else {
            float ground_y = g_automap_position.y - g_automap_zoom;
            float height = g_automap_top_y - (g_automap_top_y - ground_y) * 0.5f;
            if (g_automap_position.y <= height) {
                g_automap_position.y = height;
                SetAutomapButtonMode(AutomapZoomMode::Both);
                g_automap_zoom = g_automap_position.y - ground_y;
                SetAutomapCameraPoint(&g_automap_position);
                SetAutomapToolCursor(g_automap_tool);
            } else {
                ResetAutomapZoom();
            }
        }
    }
}

void AutomapSelectNoteToolButton(void)
{
    if (g_automap_tool == W8AutomapTool::AddNote) {
        return;
    }
    g_automap_tool = W8AutomapTool::AddNote;
    SetAutomapToolCursor(W8AutomapTool::AddNote);
}

void AutomapSelectEraseToolButton(void)
{
    if (g_automap_tool == W8AutomapTool::EraseNote) {
        return;
    }
    g_automap_tool = W8AutomapTool::EraseNote;
    SetAutomapToolCursor(W8AutomapTool::EraseNote);
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
    g_automap_position.z += 0.35f * g_automap_zoom;
    SetAutomapCameraPoint(&g_automap_position);
}

void AutomapPanSouthButton(void)
{
    g_automap_position.z -= 0.35f * g_automap_zoom;
    SetAutomapCameraPoint(&g_automap_position);
}

void AutomapPanWestButton(void)
{
    g_automap_position.x -= 0.35f * g_automap_zoom;
    SetAutomapCameraPoint(&g_automap_position);
}

void AutomapPanEastButton(void)
{
    g_automap_position.x += 0.35f * g_automap_zoom;
    SetAutomapCameraPoint(&g_automap_position);
}

void AutomapExitButton(void)
{
    if (g_automap_editing_note == 0) {
        ReturnToPreviousScreen();
    }
}

namespace {
class AutomapScreen final : public Screen {
public:
    bool enter(const ScreenRequest& request) override;
    void frame() override;
    bool leave(ScreenDeparture departure) override;
};
} // namespace

bool AutomapScreen::enter(const ScreenRequest&)
{
    Script script;
    int layer_number = 1;
    if (!g_automap_state) {
        g_automap_state = std::make_unique<W8AutomapState>();
    }
    g_automap_viewport.left = 12;
    g_automap_viewport.top = 32;
    g_automap_viewport.right = 467;
    g_automap_viewport.bottom = 467;
    g_automap_layers.clear();
    g_automap_layers.push_back(0);
    std::string layer_name;
    layer_name = fmt::format("LAYER {:d}", layer_number);
    ClipPlane* layer = FindWorldClipPlane(layer_name.c_str());
    while (layer) {
        g_automap_layers.push_back(layer);
        ++layer_number;
        layer_name = fmt::format("LAYER {:d}", layer_number);
        layer = FindWorldClipPlane(layer_name.c_str());
    }
    SetMouseMoveEventsEnabled(1);
    g_automap_cursor_inside = IsCursorInsideViewport();
    g_automap_tool = W8AutomapTool::Select;
    SetAutomapToolCursor(W8AutomapTool::Select);
    CreateAutomapMarkerSprites();
    CreateAutomapButtons();
    g_automap_party_marker->detach();
    if (!g_automap_surface)
        g_automap_surface = CreateCpuStorage(640, 480, SDL_PIXELFORMAT_ARGB1555);
    g_automap_redraw = true;
    g_automap_overlay_redraw = true;
    SetUiCanvasAlphaTestEnabled(false);
    ClearSurfaceRect(0, 0, 640, 480);
    DisableCursorScene();
    CatalogArtwork::Load(0x14a, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0);
    UpdateAutomapPageButtons();
    for (int button = 0; button < 16; ++button) {
        if (g_automap_buttons->buttons[button]) {
            g_automap_buttons->buttons[button]->m_dirty = true;
            g_automap_buttons->buttons[button]->Draw(GetFrameBufferSurface());
        }
    }
    ResetTransientRenderScenes();
    RenderFrame();
    RenderFrame();
    EnableCursorScene();
    SetScaledViewport({12, 32, 467, 467});
    UpdateAutomapBounds();
    std::vector<std::string> excluded_textures;
    if (script.Load("Data\\Automap\\MapFilters.txt")) {
        int line = 0;
        int section = -1;
        while (section < GetLoadedLevelID() && line < static_cast<int>(script.lines.size())) {
            if (script.lines[line].text.contains('[')) ++section;
            ++line;
        }
        if (line < static_cast<int>(script.lines.size()) && section == GetLoadedLevelID()) {
            for (; line < static_cast<int>(script.lines.size()); ++line) {
                const auto& text = script.lines[line].text;
                if (text.contains('[')) break;
                if (!text.contains("LAYER=")) {
                    excluded_textures.push_back(text);
                } else {
                    float height = static_cast<float>(atof(text.c_str() + 6));
                    ClipPlane* clip =
                        g_automap_created_layers.emplace_back(std::make_unique<ClipPlane>()).get();
                    layer_name = fmt::format("LAYER {:d}", layer_number);
                    clip->setName(layer_name.c_str());
                    glm::vec4 plane;
                    plane = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
                    clip->setClipPlane(plane);
                    glm::dvec3 position(0.0, static_cast<double>(height * 500.0f), 0.0);
                    clip->transform.setLocation(position);
                    clip->setClipType(ClipPlane::CLIP_POSITIONAL_0);
                    clip->visible = false;
                    g_automap_layers.push_back(clip);
                    ++layer_number;
                }
            }
        }
    }
    GetWorld()->scene->map.excludeTextures(std::move(excluded_textures));
    if (!g_automap_position_initialized) {
        g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
        g_automap_position_initialized = true;
        glm::vec3 position = (g_automap_bounds_min + g_automap_bounds_max) / 2.0f;
        position.y = g_automap_top_y;
        SetAutomapCameraPoint(&position);
        SetAutomapToolCursor(g_automap_tool);
        SetAutomapButtonMode(AutomapZoomMode::ZoomInOnly);
    } else {
        RestoreAutomapCameraPosition();
    }
    return true;
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

void AutomapScreen::frame()
{
    PumpUiInput({
        .before =
            [](const InputEvent& input) {
                if (g_automap_editing_note) {
                    HandleAutomapNoteInput(&input);
                    return InputRoute::Handled;
                }
                if (HandleAutomapKey(&input)) return InputRoute::Handled;
                if (!IsCursorInsideViewport()) {
                    if (g_automap_cursor_inside) {
                        g_automap_cursor_inside = false;
                        SetAutomapToolCursor(g_automap_tool);
                    }
                    SDL_Point point;
                    point = CursorPosition();
                    UpdateUiPointer(point.x, point.y);
                    return IsMouseButtonEvent(input) ? InputRoute::Pass : InputRoute::Handled;
                }
                if (!g_automap_cursor_inside) {
                    g_automap_cursor_inside = true;
                    SetAutomapToolCursor(g_automap_tool);
                }
                if (input.type == InputEventType::LeftButtonUp) {
                    glm::vec2 point;
                    if (g_automap_tool == W8AutomapTool::AddNote) {
                        if (GetCursorPositionInViewport(&point)) {
                            int layer = g_automap_layer + 1;
                            if (HasAutomapLayer(layer)) {
                                g_automap_layers[layer]->transform.getLocationY();
                            }
                            const auto location = AutomapWorldPosition(point);
                            g_automap_editing_note =
                                CreateAutomapNote(&location, g_automap_layer, "_");
                            SetNativeTextInputEnabled(g_automap_editing_note != nullptr);
                        }
                    } else if (g_automap_tool == W8AutomapTool::EraseNote) {
                        W8AutomapNote* note = FindAutomapNoteUnderCursor();
                        if (note) {
                            EraseAutomapNote(note);
                            g_automap_redraw = true;
                        }
                    } else if (GetCursorPositionInViewport(&point)) {
                        ZoomAutomapIn(&point);
                    }
                } else if (input.type == InputEventType::RightButtonUp) {
                    if (g_automap_tool == W8AutomapTool::Select) {
                        ZoomAutomapOut();
                    } else {
                        g_automap_tool = W8AutomapTool::Select;
                        SetAutomapToolCursor(W8AutomapTool::Select);
                    }
                } else if (input.type == InputEventType::MouseMove) {
                    W8AutomapNote* previous = g_automap_hovered_note;
                    g_automap_hovered_note = FindAutomapNoteUnderCursor();
                    if (previous != g_automap_hovered_note) {
                        if (previous) ShowAutomapNoteTooltip(previous);
                        RenderAutomapMarkers();
                    }
                    glm::vec3 point;
                    if (GetAutomapPositionUnderCursor(&point)) {
                        const auto party = cameraLocation();
                        glm::vec3 distance(point.x - party.x, 0.0f, point.z - party.z);
                        if (glm::length(distance) < g_automap_zoom * 0.05f &&
                            g_automap_tool == W8AutomapTool::Select &&
                            g_automap_zoom_mode != AutomapZoomMode::ZoomOutOnly) {
                            DisableCursorScene();
                            glm::dvec3 scale(0.44f, 0.44f, 0.44f);
                            g_automap_party_marker->scale = {scale.x, scale.z};
                            return InputRoute::Handled;
                        }
                    }
                    float factor = (1.0f / (g_automap_zoom * 0.00004f)) * 0.44f;
                    glm::dvec3 scale(factor, factor, factor);
                    g_automap_party_marker->scale = {scale.x, scale.z};
                    EnableCursorScene();
                }
                return InputRoute::Handled;
            },
        .track_hover = false,
    });
    bool moved = false;
    if (IsKeyHeld(SDLK_LEFT)) {
        g_automap_position.x -= 0.35f * g_automap_zoom;
        moved = true;
    }
    if (IsKeyHeld(SDLK_RIGHT)) {
        g_automap_position.x += 0.35f * g_automap_zoom;
        moved = true;
    }
    if (IsKeyHeld(SDLK_UP)) {
        g_automap_position.z += 0.35f * g_automap_zoom;
        moved = true;
    }
    if (IsKeyHeld(SDLK_DOWN)) {
        g_automap_position.z -= 0.35f * g_automap_zoom;
        moved = true;
    }
    if (moved) SetAutomapCameraPoint(&g_automap_position);
    RenderAutomapFrame();
}

bool AutomapScreen::leave(ScreenDeparture)
{
    if (!g_automap_state) return true; // Already released when it was suspended.
    SetNativeTextInputEnabled(false);
    g_automap_state.reset();
    SetMouseMoveEventsEnabled(0);
    g_automap_surface.reset();
    ClearAutomapMarkers();
    g_automap_party_marker->detach();
    g_automap_buttons.reset();
    ScreenPointer().reset();
    EnableCursorScene();
    g_automap_created_layers.clear();
    g_automap_party_marker.reset();
    g_automap_friendly_marker.reset();
    g_automap_neutral_marker.reset();
    g_automap_hostile_marker.reset();
    g_automap_text_marker.reset();
    return true;
}

std::unique_ptr<Screen> CreateAutomapScreen()
{
    return std::make_unique<AutomapScreen>();
}

/* Select which automap buttons are enabled for the update mode, then dirty
   and redraw both and remember the mode. */
void SetAutomapButtonMode(AutomapZoomMode update)
{
    if (!g_automap_buttons) {
        g_automap_zoom_mode = update;
        return;
    }
    g_automap_buttons->buttons[0]->SetEnabled(update != AutomapZoomMode::ZoomOutOnly);
    g_automap_buttons->buttons[1]->SetEnabled(update != AutomapZoomMode::ZoomInOnly);
    g_automap_buttons->buttons[0]->m_dirty = true;
    g_automap_buttons->buttons[0]->Draw(GetFrameBufferSurface());
    g_automap_buttons->buttons[1]->m_dirty = true;
    g_automap_buttons->buttons[1]->Draw(GetFrameBufferSurface());
    g_automap_zoom_mode = update;
}

/* Park the map camera over the requested point with its xz clamped into the
   explored bounds and reselect the layer underneath. */
void SetAutomapCameraPoint(glm::vec3* position)
{
    glm::vec3 clamped;
    clamped.x = g_automap_bounds_min.x <= position->x ? position->x : g_automap_bounds_min.x;
    clamped.x = clamped.x < g_automap_bounds_max.x
                    ? (g_automap_bounds_min.x <= position->x ? position->x : g_automap_bounds_min.x)
                    : g_automap_bounds_max.x;
    clamped.y = position->y;
    clamped.z = g_automap_bounds_min.z <= position->z ? position->z : g_automap_bounds_min.z;
    clamped.z = clamped.z < g_automap_bounds_max.z
                    ? (position->z < g_automap_bounds_min.z ? g_automap_bounds_min.z : position->z)
                    : g_automap_bounds_max.z;
    GetWorld()->scene->map.camera.transform.setLocation(
        glm::dvec3(clamped.x, clamped.y, clamped.z));
    g_automap_position = clamped;
    SetAutomapLayer(g_automap_layer);
    g_automap_redraw = true;
    g_automap_overlay_redraw = true;
}

/* Select the automap layer, remember how far above it the camera floats and
   refresh the layer-up/layer-down buttons. */
void SetAutomapLayer(int layer)
{
    if (!HasAutomapLayer(layer)) {
        g_automap_near_clip = 1.0f;
    } else {
        float height = g_automap_position.y -
                       static_cast<float>(g_automap_layers[layer]->transform.getLocation().y);
        if (height < 1.0f) {
            g_automap_near_clip = 1.0f;
        } else {
            g_automap_near_clip = height;
        }
    }
    g_automap_layer = layer;
    g_automap_redraw = true;
    g_automap_overlay_redraw = true;
    if (g_automap_buttons) {
        if (g_automap_buttons->buttons[9] != 0) {
            g_automap_buttons->buttons[9]->SetEnabled(
                layer != static_cast<int>(g_automap_layers.size()) - 1);
        }
        if (g_automap_buttons->buttons[8] != 0) {
            g_automap_buttons->buttons[8]->SetEnabled(layer != 0);
        }
    }
}

/* Reset the zoom to the full explored span, recenter over the bounds and
   restore the neutral button mode and cursor. */
void ResetAutomapZoom(void)
{
    g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
    glm::vec3 position;
    position = glm::vec3((g_automap_bounds_min.x + g_automap_bounds_max.x) * 0.5,
                         (g_automap_bounds_min.y + g_automap_bounds_max.y) * 0.5,
                         (g_automap_bounds_min.z + g_automap_bounds_max.z) * 0.5);
    position.y = g_automap_top_y;
    SetAutomapCameraPoint(&position);
    SetAutomapToolCursor(g_automap_tool);
    SetAutomapButtonMode(AutomapZoomMode::ZoomInOnly);
}

/* Left-click zoom: drop a sight line onto the clicked point, halve the
   remaining height above ground and move the camera there. */
bool ZoomAutomapIn(const glm::vec2* point)
{
    const auto location = AutomapWorldPosition(*point);
    glm::vec3 position(location.x, g_automap_top_y, location.y);
    glm::vec3 from(location.x, g_automap_top_y + kWorldScale, location.y);
    glm::vec3 to(location.x, g_automap_grid_min.y, location.y);

    float ground;
    if (!GetWorldCollision()->traceSight(from, to)) {
        ground = g_automap_grid_min.y;
        if (!g_automap_layers.empty() && 1 < static_cast<int>(g_automap_layers.size()) &&
            g_automap_layers[1] != 0) {
            ground = static_cast<float>(g_automap_layers[1]->transform.getLocationY());
        }
    } else {
        ground = to.y;
    }

    float height = (g_automap_top_y - ground) * 0.5f;
    float minimum = ground + 2.5e+04f;
    if (g_automap_zoom_mode != AutomapZoomMode::ZoomInOnly || g_automap_top_y <= minimum ||
        height < minimum) {
        SetAutomapButtonMode(AutomapZoomMode::ZoomOutOnly);
        height = minimum;
    } else {
        SetAutomapButtonMode(AutomapZoomMode::Both);
    }
    g_automap_zoom = height - ground;
    position.y = height;
    SetAutomapCameraPoint(&position);
    SetAutomapToolCursor(g_automap_tool);
    return true;
}

/* Forget the map's explored vertices, then clear the cell-processed bitmap so
   UpdateAutomapBounds reveals every visited cell again. */
void ResetAutomapLighting(void)
{
    GetWorld()->scene->map.forgetExplored();
    std::ranges::fill(GetWorld()->automap.lit, false);
}

static glm::vec3 GetAutomapCellPosition(unsigned int index)
{
    glm::vec3 cell;
    cell = glm::vec3(0);
    if (index < GetWorld()->automap.keys.size()) {
        SDL_Keycode key = GetWorld()->automap.keys[index];
        float half = GetWorld()->automap.grid_cell_size * 0.5f;
        cell = glm::vec3((key >> 0x15) * GetWorld()->automap.grid_cell_size + half,
                         (key & 0x3ff) * GetWorld()->automap.grid_cell_size + half,
                         ((key >> 10) & 0x7ff) * GetWorld()->automap.grid_cell_size + half);
    }
    return cell + g_automap_grid_origin;
}

/* Reveal up to `max_count` visited cells that the map has not yet lit.
   Returns how many cells were lit; a zero answer means the pending set is
   empty. */
unsigned int LightPendingAutomapCells(unsigned int max_count)
{
    unsigned int lit = 0;
    unsigned int bit = 0;
    if (GetWorld()->automap.keys.size() != 0) {
        do {
            if (max_count <= lit) {
                return lit;
            }
            if (TestBit(GetWorld()->automap.visited, bit)) {
                glm::vec3 position = GetAutomapCellPosition(bit);
                if (!TestBit(GetWorld()->automap.lit, bit)) {
                    SetBit(GetWorld()->automap.lit, bit);
                    LightAutomapCell(&position);
                    ++lit;
                }
            }
            ++bit;
        } while (bit < static_cast<unsigned int>(GetWorld()->automap.keys.size()));
    }
    return lit;
}

void RefreshDirtyAutomap(void)
{
    if (g_automap_state != 0 && g_automap_state->pending_cell_lighting != 0 &&
        LightPendingAutomapCells(10) == 0) {
        g_automap_state->pending_cell_lighting = 0;
    }
}

/* Recompute the explored-bounds box from the visited-cell bitmap: mark new
   cells for the node builder and grow the box to cover each visited cell. */
void UpdateAutomapBounds(void)
{
    std::vector<MeshInstance*> models;
    CurrentGame().spellcasting.partyHasCondition(0x40);
    if (GetWorld()->compiledIndex() != 0) {
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
        if (GetWorld()->automap.keys.size() != 0) {
            do {
                if (TestBit(GetWorld()->automap.visited, bit)) {
                    glm::vec3 position = GetAutomapCellPosition(bit);
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
                    if (!TestBit(GetWorld()->automap.lit, bit)) {
                        SetBit(GetWorld()->automap.lit, bit);
                        LightAutomapCell(&position);
                    }
                }
                ++bit;
            } while (bit < static_cast<unsigned int>(GetWorld()->automap.keys.size()));
        }
        g_automap_bounds_min.x -= 5e+03f;
        g_automap_bounds_max.x += 5e+03f;
        g_automap_bounds_min.z -= 5e+03f;
        g_automap_bounds_max.z += 5e+03f;
        float span = g_automap_bounds_max.x - g_automap_bounds_min.x;
        if (span <= g_automap_bounds_max.z - g_automap_bounds_min.z) {
            span = g_automap_bounds_max.z - g_automap_bounds_min.z;
        }
        g_automap_top_y = span + g_automap_bounds_max.y;
    }
}

/* Reveal one visited cell: the map lights the static vertices of every model
   instance the octree reports within the query range of the cell. */
void LightAutomapCell(const glm::vec3* position)
{
    auto& map = GetWorld()->scene->map;
    for (auto* instance : GetWorld()->compiledIndex()->modelsNearPoint(*position, g_automap_range))
        if (instance) map.reveal(*instance, *position, g_automap_range);
}

/* Per-frame automap refresh: re-render the map view into the automap surface
   when it is dirty, blit it into the primary surface and redraw buttons. The
   layer is the map camera's near clip: how far above the layer it floats. */
void RenderAutomapFrame(void)
{
    if (g_automap_redraw) {
        if (g_automap_overlay_redraw) {
            auto& camera = GetWorld()->scene->map.camera;
            const double half = g_automap_zoom * 0.5f;
            camera.setViewPlane(Camera3D::Rect{-half, -half, half, half}, g_automap_near_clip);
            camera.setClipRange(g_automap_near_clip, 1500000.0);
            RenderMapToSurface(*g_automap_surface, g_automap_viewport);
            g_automap_overlay_redraw = false;
        }
        SDL_Rect region{0xc, 0x20, 0x1d3 - 0xc, 0x1d3 - 0x20};
        SDL_BlitSurface(g_automap_surface.get(), &region, &GetFrameBufferSurface(), &region);
        RenderAutomapMarkers();
        InvalidateRegion(0xc, 0x20, 0x1d3, 0x1d3);
        g_automap_redraw = false;
    }
    for (int button = 0; button < 16; ++button) {
        if (g_automap_buttons->buttons[button] != 0) {
            g_automap_buttons->buttons[button]->Draw(GetFrameBufferSurface());
        }
    }
    RenderFrame();
}

void UpdateAutomapPageButtons(void)
{
    g_automap_buttons->buttons[5]->SetVisible(g_automap_page == 0);
    g_automap_buttons->buttons[6]->SetVisible(g_automap_page == 1);
    g_automap_buttons->buttons[7]->SetVisible(g_automap_page == 2);
    g_automap_buttons->buttons[g_automap_page + 5]->m_dirty = true;
    g_automap_buttons->buttons[g_automap_page + 5]->Draw(GetFrameBufferSurface());
}

/* Blit the saved automap surface back over a screen rect to erase whatever
   was drawn there. */
int RestoreAutomapRect(DesignRect* rect)
{
    if (g_automap_surface != 0) {
        SDL_Surface& screen = GetFrameBufferSurface();
        SDL_Rect region{rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top};
        SDL_BlitSurface(g_automap_surface.get(), &region, &screen, &region);
    }
    return 1;
}

/* When the cursor leaves a hovered note, erase its tooltip rectangle and
   invalidate it for redraw. Returns true when a tooltip was dismissed. */
bool ShowAutomapNoteTooltip(W8AutomapNote* note)
{
    if (note == 0) {
        return false;
    }
    float floor_y;
    int layer = note->layer + 1;
    if (!HasAutomapLayer(layer)) {
        floor_y = g_automap_grid_min.y;
    } else if (layer < static_cast<int>(g_automap_layers.size())) {
        floor_y = static_cast<float>(g_automap_layers[layer]->transform.getLocationY());
    } else {
        floor_y = static_cast<float>(g_automap_layers.front()->transform.getLocationY());
    }
    int index = FindAutomapLayerAtHeight(floor_y);
    if (index - 1 == g_automap_layer) {
        float left = g_automap_position.x - g_automap_zoom * 0.5f;
        float bottom = g_automap_position.z - g_automap_zoom * 0.5f;
        if (left <= note->position.x && note->position.x <= left + g_automap_zoom &&
            bottom <= note->position.y && note->position.y <= bottom + g_automap_zoom) {
            int screen_x = static_cast<int>((note->position.x - left) / g_automap_zoom * -455.0f);
            int screen_y =
                static_cast<int>((1.0f - (note->position.y - bottom) / g_automap_zoom) * -435.0f);
            int marker_width;
            if (note == g_automap_editing_note) {
                marker_width = static_cast<int>(g_automap_text_marker->scaledSize().x * 0.22);
            } else {
                marker_width = static_cast<int>(
                    (1.0f / (g_automap_zoom * 0.00004f)) * 0.44f *
                    static_cast<double>(g_automap_text_marker->scaledSize().x) * 0.5f);
            }
            DesignRect rect;
            rect.top = (0x20 - screen_y) - ((unsigned)g_wiz_text_font_secondary.height() >> 1);
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
                InvalidateScreenRects(&rect, 1);
                return true;
            }
        }
    }
    return false;
}

/* Persist the visited-cell bitmap and every note's position, layer and text.
   A 0xF00DF00D signature brackets the note records. Write failures throw. */
void SaveAutomapNotes(wiz8::File& handle)
{
    const std::uint32_t signature = 0xf00df00d;
    if (!GetWorld()->automap.visited.empty()) SavePackedBits(handle, GetWorld()->automap.visited);
    handle.write(&signature, sizeof(signature));
    const auto count = wiz8::checked_integer<std::uint32_t>(g_automap_notes.size());
    handle.write(&count, sizeof(count));
    for (const auto& note : g_automap_notes) {
        const auto length =
            wiz8::checked_integer<std::uint32_t>(wiz8::text::to_utf16(note->text).size() + 1);
        std::vector<std::byte> bytes(length * 2);
        wiz8::text::to_utf16le(note->text, bytes);
        handle.write(&note->position.x, sizeof(note->position.x));
        handle.write(&note->position.y, sizeof(note->position.y));
        handle.write(&note->layer, sizeof(note->layer));
        handle.write(&length, sizeof(length));
        handle.write(bytes.data(), bytes.size());
    }
    handle.write(&signature, sizeof(signature));
}

/* The visited-cell bitmap, present when the level's automap had cells, then the notes. */
W8SavedAutomapNotes ReadAutomapNotes(W8Chunk& file)
{
    W8SavedAutomapNotes result;
    const auto first = file.ReadValue<std::uint32_t>();
    file.RewindCurrentChunk();
    if (first == kPackedBitsMagic && !LoadPackedBits(file, result.visited))
        throw std::runtime_error("invalid automap visited cells");
    unsigned int signature, count;
    file.ReadBytes(&signature, sizeof(signature));
    if (signature != 0xf00df00d) throw std::runtime_error("invalid automap notes signature");
    file.ReadBytes(&count, sizeof(count));
    if (count > file.RemainingBytes() / 18) throw std::runtime_error("invalid automap note count");
    result.notes.reserve(std::min(count, 200u));
    for (unsigned int index = 0; index < count; ++index) {
        auto note = std::make_unique<W8AutomapNote>();
        int length;
        file.ReadBytes(&note->position.x, sizeof(float));
        file.ReadBytes(&note->position.y, sizeof(float));
        file.ReadBytes(&note->layer, sizeof(note->layer));
        file.ReadBytes(&length, sizeof(length));
        if (length < 1 || length > 40) throw std::runtime_error("invalid automap note length");
        std::vector<std::byte> bytes(static_cast<std::size_t>(length) * 2);
        file.ReadBytes(bytes.data(), bytes.size());
        note->text = wiz8::text::from_utf16le(bytes);
        if (result.notes.size() < 200) result.notes.push_back(std::move(note));
    }
    file.ReadBytes(&signature, sizeof(signature));
    if (signature != 0xf00df00d) throw std::runtime_error("invalid automap notes terminator");
    return result;
}

void ApplyAutomapNotes(World& world, W8SavedAutomapNotes records)
{
    ClearAutomapNotes();
    g_automap_redraw = true;
    if (world.automap.keys.size() > 1) {
        if (records.visited.size() == world.automap.keys.size()) {
            world.automap.visited = std::move(records.visited);
            if (g_automap_state) g_automap_state->pending_cell_lighting = 1;
        } else
            world.automap.visited.assign(world.automap.keys.size(), false);
        g_automap_bounds_dirty = true;
    }
    g_automap_notes = std::move(records.notes);
}

/* Convert the cursor position to an automap world position on the layer
   below the current one, defaulting the height to the grid minimum when no
   clip plane exists there. */
bool GetAutomapPositionUnderCursor(glm::vec3* position)
{
    glm::vec2 point;

    if (GetCursorPositionInViewport(&point) != 0) {
        int layer = g_automap_layer + 1;
        float height;
        if (HasAutomapLayer(layer)) {
            height = static_cast<float>(g_automap_layers[layer]->transform.getLocationY());
        } else {
            height = g_automap_grid_min.y;
        }
        const auto location = AutomapWorldPosition(point);
        *position = glm::vec3(location.x, height, location.y);
        return true;
    }
    return false;
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
            g_automap_layers[layer]->transform.getLocationY();
        }
        const auto location = AutomapWorldPosition(point);
        for (const auto& automap_note : g_automap_notes) {
            W8AutomapNote* note = automap_note.get();
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
static std::shared_ptr<Texture> LoadAutomapMarkerTexture(const char* path)
{
    auto texture = std::make_shared<Texture>(wiz8::asset_path(path), false);

    texture->setMipmap(Texture::MIPMAP_NONE);
    texture->setMagFilter(Texture::FILTER_BEST);
    texture->setMinFilter(Texture::FILTER_BEST);
    texture->loadSurface();
    return texture;
}

static std::unique_ptr<OverlayQuad> CreateAutomapMarkerSprite(const char* path)
{
    std::shared_ptr<Texture> texture = LoadAutomapMarkerTexture(path);
    SDL_Surface* surface = texture->getSurface();
    if (!surface) {
        return {};
    }
    auto marker = std::unique_ptr<OverlayQuad>(makeOverlayFromTexture(
        texture, surface->w * g_viewport_x_scale, surface->h * g_viewport_y_scale, true, false));
    if (!marker) return {};
    marker->detach();
    return marker;
}

/* Load the automap marker textures and build the party, monster and text
   marker sprites over them. */
void CreateAutomapMarkerSprites(void)
{
    if (g_automap_party_marker == 0) {
        std::shared_ptr<Texture> texture =
            LoadAutomapMarkerTexture("Data\\Automap\\map_partymarker_a.tga");
        SDL_Surface* surface = texture->getSurface();
        if (surface != 0) {
            g_automap_party_marker =
                makeOverlayFromTexture(texture, surface->w * g_viewport_x_scale,
                                       surface->h * g_viewport_x_scale, true, true);
            g_automap_party_marker->detach();
            g_compositor.attach(*g_automap_party_marker, OverlayLayer::SquareAspect);
            g_automap_party_marker->glow_enabled = true;
            glm::vec4 first;
            glm::vec4 second;
            first = glm::vec4(0.0f, 0.25f, 0.0f, 1.0f);
            second = glm::vec4(0.0f, 0.75f, 0.0f, 1.0f);
            g_automap_party_marker->glow_colors = {first, second};
            g_automap_party_marker->glow_period = 2000;
        }
    }
    if (g_automap_friendly_marker == 0) {
        g_automap_friendly_marker =
            CreateAutomapMarkerSprite("Data\\Automap\\map_monsterfriendly_a.tga");
    }
    if (g_automap_neutral_marker == 0) {
        g_automap_neutral_marker =
            CreateAutomapMarkerSprite("Data\\Automap\\map_monsterneutral_a.tga");
    }
    if (g_automap_hostile_marker == 0) {
        g_automap_hostile_marker =
            CreateAutomapMarkerSprite("Data\\Automap\\map_monsterhostile_a.tga");
    }
    if (g_automap_text_marker == 0) {
        g_automap_text_marker = CreateAutomapMarkerSprite("Data\\Automap\\map_textmarker_a.tga");
    }
}

/* Whether nothing blocks the sight line from the map camera to a point. */
static bool MapCameraSees(const glm::vec3& location)
{
    const auto* spatial = GetWorld()->compiledIndex();
    const auto eye = glm::vec3(GetWorld()->scene->map.camera.transform.getLocation());
    return !spatial || spatial->hasLineOfSight(eye, location);
}

/* Redraw the transient party, monster, item and note markers over the automap
   surface. */
void RenderAutomapMarkers(void)
{
    bool detect_all = CurrentGame().spellcasting.partyHasCondition(0x40);
    ClearAutomapMarkers();
    float left = g_automap_position.x - g_automap_zoom * 0.5f;
    const auto party = cameraLocation();
    glm::vec3 point(party.x, 1.0f, party.z);
    float top = g_automap_position.z - g_automap_zoom * 0.5f;
    if (point.x < left || left + g_automap_zoom < point.x || point.z < top ||
        top + g_automap_zoom < point.z) {
        g_automap_party_marker->visible = false;
    } else {
        int x = 0xc - static_cast<int>((point.x - left) / g_automap_zoom * -455.0f);
        int y = 0x20 - static_cast<int>((1.0f - (point.z - top) / g_automap_zoom) * -435.0f);
        float factor = (1.0f / (g_automap_zoom * 0.00004f)) * 0.44f;
        glm::dvec3 scale(factor, factor, factor);
        g_automap_party_marker->scale = {scale.x, scale.z};
        if (g_automap_party_marker->scaledSize().y < 1) {
            g_automap_party_marker->scale = {1.0, 1.0};
            factor = 1.0f / g_automap_party_marker->scaledSize().y;
            scale = glm::dvec3(factor);
            g_automap_party_marker->scale = {scale.x, scale.z};
        }
        unsigned short width = g_automap_party_marker->scaledSize().x;
        unsigned short height = g_automap_party_marker->scaledSize().y;
        positionOverlay(g_automap_party_marker.get(), x - (width >> 1), y - (height >> 1), false);
        g_automap_party_marker->rotation = GetCameraYawRadians();
        g_automap_party_marker->visible = true;
        g_automap_party_marker->detach();
        g_compositor.attach(*g_automap_party_marker, OverlayLayer::SquareAspect);
    }
    const auto count = CurrentLevelPopulation().monster_roster.live().size();
    for (unsigned int index = 0; index < count; ++index) {
        W8MonsterInfo* info = GetMonsterListEntry(index);
        Monster& monster = *info->entity;
        glm::vec3 location;
        location = monster.visual.GetLocation();
        FindAutomapLayerAtHeight(location.y);
        if (detect_all || (!monster.disabled && info->party_threat.sight_state == W8_SIGHT_SEEN)) {
            left = g_automap_position.x - g_automap_zoom * 0.5f;
            top = g_automap_position.z - g_automap_zoom * 0.5f;
            if (location.x < left || left + g_automap_zoom < location.x || location.z < top ||
                top + g_automap_zoom < location.z) {
                continue;
            }
            int x = 0xc - static_cast<int>((location.x - left) / g_automap_zoom * -455.0f);
            int y = 0x20 - static_cast<int>((1.0f - (location.z - top) / g_automap_zoom) * -435.0f);
            OverlayQuad* marker;
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
            default:
                continue;
            }
            unsigned short width = marker->scaledSize().x;
            x -= width >> 1;
            unsigned short height = marker->scaledSize().y;
            y -= height >> 1;
            g_compositor.attach(*marker, OverlayLayer::Fullscreen);
            positionOverlay(marker, x, y, false);
        }
    }
    for (const auto& world_item : CurrentLevelPopulation().world_items) {
        WorldItemEntity* item = world_item->entity.find();
        if (item == 0) {
            continue;
        }
        glm::vec3 location;
        location = item->GetSearchPosition();
        int layer = FindAutomapLayerAtHeight(location.y);
        if ((item->flags & W8_ITEM_ENTITY_NO_PICKUP) == 0 &&
            (detect_all || (item->flags & W8_ITEM_ENTITY_RADAR_SEEN) != 0 ||
             MapCameraSees(location)) &&
            layer - 1 == g_automap_layer) {
            left = g_automap_position.x - g_automap_zoom * 0.5f;
            top = g_automap_position.z - g_automap_zoom * 0.5f;
            if (location.x < left || left + g_automap_zoom < location.x || location.z < top ||
                top + g_automap_zoom < location.z) {
                continue;
            }
            int x = 0xc - static_cast<int>((location.x - left) / g_automap_zoom * -455.0f);
            int y = 0x20 - static_cast<int>((1.0f - (location.z - top) / g_automap_zoom) * -435.0f);
            OverlayQuad* marker = CreateAutomapItemMarker(world_item->item.iItemNo);
            if (marker != 0) {
                unsigned short width = marker->scaledSize().x;
                unsigned short height = marker->scaledSize().y;
                positionOverlay(marker, x - (width >> 1), y - (height >> 1), false);
                g_compositor.attach(*marker, OverlayLayer::Fullscreen);
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
                g_automap_layers[layer]->transform.getLocationY();
            }
            left = g_automap_position.x - g_automap_zoom * 0.5f;
            top = g_automap_position.z - g_automap_zoom * 0.5f;
            if (note->position.x < left || left + g_automap_zoom < note->position.x ||
                note->position.y < top || top + g_automap_zoom < note->position.y) {
                continue;
            }
            int x = 0xc - static_cast<int>((note->position.x - left) / g_automap_zoom * -455.0f);
            int y = 0x20 -
                    static_cast<int>((1.0f - (note->position.y - top) / g_automap_zoom) * -435.0f);
            OverlayQuad* marker = CreateAutomapTextMarker();
            if (note == g_automap_editing_note) marker->scale = {0.22f, 0.22f};
            if (marker != 0) {
                unsigned short width = marker->scaledSize().x;
                x -= width >> 1;
                unsigned short height = marker->scaledSize().y;
                y -= height >> 1;
                positionOverlay(marker, x, y, false);
                g_compositor.attach(*marker, OverlayLayer::Fullscreen);
                if (note->layer == g_automap_layer && !note->text.empty() &&
                    (g_automap_page == 0 || note == g_automap_hovered_note ||
                     note == g_automap_editing_note)) {
                    height = marker->scaledSize().y;
                    y += (height >> 1) - ((font_height & 0xffff) >> 1);
                    width = marker->scaledSize().x;
                    x += width + 2;
                    DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                                          {x, y}, note->text);
                    if (note == g_automap_editing_note) {
                        marker->scale = {0.22f, 0.22f};
                        int color = Get16BPPColor(SDL_Color{239, 155, 86, SDL_ALPHA_OPAQUE});
                        int length = g_wiz_text_font_secondary.textWidth(note->text);
                        const SDL_Rect top_edge{x - 1, y, length + 4, 1};
                        const SDL_Rect bottom_edge{x - 1, y + (font_height & 0xffff), length + 4,
                                                   1};
                        const SDL_Rect left_edge{x - 1, y, 1, (font_height & 0xffff) + 1};
                        const SDL_Rect right_edge{x + length + 2, y, 1, (font_height & 0xffff) + 1};
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

static void ScaleAutomapMarker(OverlayQuad* marker, float factor)
{
    if (marker->scaledSize().y * factor < 1.0f) {
        marker->scale = {1.0, 1.0};
        factor = 1.0f / marker->scaledSize().y;
    }
    marker->scale = {factor, factor};
}

/* Create a scaled automap blip for a world item by blitting its inventory icon
   into a scratch surface and wrapping it in a 2D brush. */
OverlayQuad* CreateAutomapItemMarker(int item_id)
{
    const auto sprites = ItemSprites(item_id);
    const SpriteSheet& video_object = *sprites;
    if (video_object.frames.empty()) {
        return 0;
    }
    const SpriteFrame& properties = video_object.frames.front();
    auto surface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(0x40, 0x40, SDL_PIXELFORMAT_ARGB1555).release(), SDL_DestroySurface);
    SDL_FillSurfaceRect(surface.get(), nullptr, 0);
    int y = (0x40 - properties.height) / 2;
    int x = (0x40 - properties.width) / 2;
    BlitVideoObjectToColorSurface(video_object, 0, *surface, x, y);
    float factor = (1.0f / (g_automap_zoom * 0.00004f)) * 0.44f;
    const auto width = surface->w * g_viewport_x_scale;
    const auto height = surface->h * g_viewport_y_scale;
    auto owned =
        makeOverlayFromTexture(std::make_shared<Texture>(surface), width, height, true, false);
    auto* marker = owned.get();
    if (marker == 0) {
        return 0;
    }
    marker->detach();
    g_automap_markers.push_back(std::move(owned));
    marker->glow_enabled = true;
    glm::vec4 first;
    glm::vec4 second;
    second = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    first = glm::vec4(0.0f, 0.0f, 0.25f, 1.0f);
    marker->glow_colors = {first, second};
    marker->glow_period = 2000;
    ScaleAutomapMarker(marker, factor);
    return marker;
}

/* Clone the friendly, neutral or hostile blip sprite for one monster entry and
   register the clone for release after the pass. */
OverlayQuad* CreateAutomapMonsterMarker(int type)
{
    const std::array prototypes{g_automap_friendly_marker.get(), g_automap_neutral_marker.get(),
                                g_automap_hostile_marker.get()};
    if (type < 0 || type >= static_cast<int>(prototypes.size())) return nullptr;
    const auto& prototype = *prototypes[type];
    auto owned = makeOverlayFromTexture(prototype.texture, prototype.size.x / 640.0,
                                        prototype.size.y / 480.0, true, false);
    auto* marker = owned.get();
    marker->detach();
    const std::array colors{glm::vec4(0, .5f, 0, 1), glm::vec4(.25f, .25f, 0, 1),
                            glm::vec4(.5f, 0, 0, 1)};
    marker->glow_colors = {glm::vec4(0, 0, 0, 1), colors[type]};
    g_automap_markers.push_back(std::move(owned));
    marker->glow_enabled = true;
    marker->glow_period = 2000;
    /* Retail scales by 0.85 as a step of its own; written as one product, VC6
       folds 0.44 * 0.85 into a single constant. */
    float factor = (1.0f / (g_automap_zoom * 0.00004f)) * 0.44f;
    factor *= 0.85f;
    ScaleAutomapMarker(marker, factor);
    return marker;
}

/* Clone the text blip sprite and register the clone for release after the
   pass. */
OverlayQuad* CreateAutomapTextMarker(void)
{
    const auto& prototype = *g_automap_text_marker;
    auto owned = makeOverlayFromTexture(prototype.texture, prototype.size.x / 640.0,
                                        prototype.size.y / 480.0, true, false);
    auto* marker = owned.get();
    marker->detach();
    g_automap_markers.push_back(std::move(owned));
    float factor = (1.0f / (g_automap_zoom * 0.00004f)) * 0.44f * 0.5f;
    ScaleAutomapMarker(marker, factor);
    marker->glow_enabled = true;
    glm::vec4 first;
    glm::vec4 second;
    second = glm::vec4(1.0f, 0.4f, 0.0f, 1.0f);
    first = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    marker->glow_colors = {first, second};
    marker->glow_period = 2000;
    return marker;
}
/* While a note is being edited, forward key events into its text buffer and
   let left-button-up drop the note at the cursor. */
bool HandleAutomapNoteInput(const InputEvent* input)
{
    if (input->type != InputEventType::KeyDown && input->type != InputEventType::KeyRepeat &&
        input->type != InputEventType::Text) {
        if (input->type == InputEventType::LeftButtonUp && IsCursorInsideViewport()) {
            glm::vec2 point;
            if (GetCursorPositionInViewport(&point) != 0) {
                int layer = g_automap_layer + 1;
                if (HasAutomapLayer(layer)) {
                    g_automap_layers[layer]->transform.getLocationY();
                }
                g_automap_editing_note->position = AutomapWorldPosition(point);
                g_automap_redraw = true;
            }
        }
        return false;
    }
    const bool committed = input->type == InputEventType::Text;
    const char32_t key = committed ? input->text : 0;
    InputEvent normalized = *input;
    if (committed) {
        normalized.type = InputEventType::KeyDown;
        normalized.key = 0;
        input = &normalized;
    }
    ShowAutomapNoteTooltip(g_automap_editing_note);
    auto& text = g_automap_editing_note->text;
    const auto last = text.empty() ? 0 : text.size() - 1; // Trailing cursor marker.
    if (key >= SDLK_SPACE && key <= 0x10ffff &&
        wiz8::text::to_utf16(text).size() + (key > 0xffff ? 2 : 1) <= 40) {
        std::string inserted;
        wiz8::text::append(inserted, key);
        text.insert(last, inserted);
        g_automap_redraw = true;
    } else {
        if (input->key == SDLK_BACKSPACE) {
            if (last)
                text.erase(wiz8::text::previous_grapheme(text, last),
                           last - wiz8::text::previous_grapheme(text, last));
            g_automap_redraw = true;
        } else if (input->key == SDLK_RETURN) {
            text.resize(last);
            if (text.empty()) {
                EraseAutomapNote(g_automap_editing_note);
                g_automap_redraw = true;
            }
            if (g_automap_tool != W8AutomapTool::Select) {
                g_automap_tool = W8AutomapTool::Select;
                SetAutomapCursorImage(0);
            }
            g_automap_editing_note = 0;
            g_automap_redraw = true;
        } else if (input->key == SDLK_ESCAPE) {
            EraseAutomapNote(g_automap_editing_note);
            g_automap_redraw = true;
            if (g_automap_tool != W8AutomapTool::Select) {
                g_automap_tool = W8AutomapTool::Select;
                SetAutomapCursorImage(0);
            }
        }
    }
    if (g_automap_editing_note != 0) {
        RenderAutomapMarkers();
        return true;
    }
    SetNativeTextInputEnabled(false);
    if (g_automap_tool != W8AutomapTool::Select) {
        g_automap_tool = W8AutomapTool::Select;
        SetAutomapCursorImage(0);
    }
    return true;
}

/* Automap key handling: screen exit, zoom keys, page cycling and the
   per-tool and cheat-gated bindings. */
bool HandleAutomapKey(const InputEvent* input)
{
    if (input->type != InputEventType::KeyDown) {
        return false;
    }
    MGSKeyBinding* binding =
        g_mgs_keyboard.GetBinding(g_mgs_keyboard.FindBinding(W8_MGS_COMMAND_AUTOMAP));
    if (input->key == binding->key && input->modifiers == binding->modifiers) {
        ReturnToPreviousScreen();
        return true;
    }
    switch (input->key) {
    case SDLK_BACKSPACE:
        /* Backspace zooms out one step, or all the way from the full view. */
        if (g_automap_position.y < g_automap_top_y) {
            if (g_automap_zoom_mode == AutomapZoomMode::Both) {
                g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
                glm::vec3 position = (g_automap_bounds_max + g_automap_bounds_min) / 2.0f;
                position.y = g_automap_top_y;
                SetAutomapCameraPoint(&position);
                SetAutomapToolCursor(g_automap_tool);
                SetAutomapButtonMode(AutomapZoomMode::ZoomInOnly);
                return true;
            }
            float floor = g_automap_position.y - g_automap_zoom;
            float height = g_automap_top_y - (g_automap_top_y - floor) * 0.5f;
            if (height < g_automap_position.y) {
                ResetAutomapZoom();
                return true;
            }
            g_automap_position.y = height;
            SetAutomapButtonMode(AutomapZoomMode::Both);
            g_automap_zoom = g_automap_position.y - floor;
            SetAutomapCameraPoint(&g_automap_position);
            SetAutomapToolCursor(g_automap_tool);
        }
        return true;
    case SDLK_RETURN: {
        AutomapZoomInButton();
        return true;
    }
    case SDLK_ESCAPE:
        /* Escape drops the tool, or leaves the screen when none is held. */
        if (g_automap_tool == W8AutomapTool::Select) {
            ReturnToPreviousScreen();
            return true;
        }
        g_automap_tool = W8AutomapTool::Select;
        SetAutomapToolCursor(W8AutomapTool::Select);
        return true;
    case SDLK_SPACE:
        AutomapCyclePageButton();
        return true;
    case SDLK_END:
        RestoreAutomapCameraPosition();
        break;
    case SDLK_HOME: {
        g_automap_zoom = g_automap_top_y - g_automap_bounds_min.y;
        {
            glm::vec3 center(g_automap_bounds_min.x + g_automap_bounds_max.x,
                             g_automap_bounds_max.y + g_automap_bounds_min.y,
                             g_automap_bounds_min.z + g_automap_bounds_max.z);
            glm::vec3 position = center / 2.0f;
            position.y = g_automap_top_y;
            SetAutomapCameraPoint(&position);
        }
        SetAutomapToolCursor(g_automap_tool);
        SetAutomapButtonMode(AutomapZoomMode::ZoomInOnly);
        return true;
    }
    case SDLK_INSERT:
        if (g_automap_tool == W8AutomapTool::AddNote) {
            return true;
        }
        g_automap_tool = W8AutomapTool::AddNote;
        SetAutomapToolCursor(W8AutomapTool::AddNote);
        return true;
    case SDLK_DELETE:
        if (g_automap_tool == W8AutomapTool::EraseNote) {
            return true;
        }
        g_automap_tool = W8AutomapTool::EraseNote;
        SetAutomapToolCursor(W8AutomapTool::EraseNote);
        return true;
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
        SetAutomapLayer(input->key - 0x31);
        return true;
    }
    return false;
}

/* Release the previous level's automap query state, then read the cell size
   and record count. A level with no records still gets one zero cell so the
   visited-bit arrays and record table stay valid; otherwise the key list is
   read whole and every cell key is inserted with its one-based index. */
unsigned char ReadAutomapNodes(wiz8::File* file, W8AutomapCells& cells)
try {
    W8AutomapCells decoded;
    unsigned int count;
    file->read_exact(&decoded.grid_cell_size, sizeof(decoded.grid_cell_size));
    file->read_exact(&count, sizeof(count));
    if (count > static_cast<std::uint64_t>(file->size() - file->tell()) / sizeof(unsigned int))
        return false;
    decoded.keys.resize(count ? count : 1, 0);
    if (count) file->read_exact(decoded.keys.data(), count * sizeof(unsigned int));
    decoded.visited.resize(decoded.keys.size(), false);
    decoded.lit.resize(decoded.keys.size(), false);
    for (unsigned int i = 0; i < count; ++i) decoded.index.insert_or_assign(decoded.keys[i], i + 1);
    cells = std::move(decoded);
    return true;
} catch (const std::exception&) {
    return false;
}

/* Pack a world position into an automap node key: eleven bits of z, then
   eleven of x, then ten of y, each scaled to grid cells. */
unsigned int AutomapNodeKey(const glm::vec3* position)
{
    return ((static_cast<unsigned int>(position->z / GetWorld()->automap.grid_cell_size) & 0x7ff) |
            static_cast<unsigned int>(position->x / GetWorld()->automap.grid_cell_size) << 11)
               << 10 |
           (static_cast<unsigned int>(position->y / GetWorld()->automap.grid_cell_size) & 0x3ff);
}
