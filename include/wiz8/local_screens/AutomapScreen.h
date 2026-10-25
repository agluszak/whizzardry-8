#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <SDL3/SDL_surface.h>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/design_rect.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/screen.h"
#include "wiz8/surface_storage.h"
#include "wiz8/ui_layer.h"
#include "wiz8/world/automap.h"

class ClipPlane;
struct InputEvent;
struct OverlayQuad;
struct World;
class Game;

enum class W8AutomapTool : unsigned char { Select = 0, AddNote = 2, EraseNote = 3 };

/* The sixteen chrome buttons; a slot stays empty when its artwork is missing. */
struct AutomapButtons {
    UiLayer input;
    std::array<std::unique_ptr<W8ButtonWidget>, 16> buttons;
};

/* The level map of the world it is built with: its revealed cells, the party, the
   creatures and items it may show, and the level's notes, which it edits. It borrows
   that world and is rebuilt on every visit, so no suspended map outlives its level.
   The notes, the visited cells and the last view stay with the world; the controls,
   markers, surfaces and the note being edited are the screen's. */
class AutomapScreen final : public Screen {
public:
    static constexpr W8ScreenId id = W8_SCREEN_AUTOMAP;

    explicit AutomapScreen(Game& game);
    ~AutomapScreen() override;

    bool enter(const ScreenRequest& request) override;
    void frame() override;
    bool leave(ScreenDeparture departure) override;
    std::optional<ScreenRequest> rebuildRequest(const ScreenRequest& entered) const override
    {
        return entered;
    }

    const AutomapButtons* buttons() const { return m_buttons.get(); }

private:
    enum class ZoomMode { ZoomInOnly, Both, ZoomOutOnly };

    Automap::View& view();
    glm::vec2 WorldPosition(const glm::vec2& cursor);
    bool HasLayer(int layer) const;
    int LayerAtHeight(double height) const;
    void CreateButtons();
    void CreateMarkerSprites();
    void UpdateBounds();

    void SetToolCursor(W8AutomapTool tool);
    void SelectTool(W8AutomapTool tool);
    void DropTool();
    void SetButtonMode(ZoomMode mode);
    void SetCameraPoint(glm::vec3 position);
    void SetLayer(int layer);
    void CenterOnParty();
    void ResetZoom();
    void ZoomIn(const glm::vec2& point);
    void ZoomOut();
    void CyclePage();
    void Pan(float east, float north);

    void Present();
    void RenderMarkers();
    void ClearMarkers();
    OverlayQuad* ItemMarker(int item_id);
    OverlayQuad* MonsterMarker(int type);
    OverlayQuad* TextMarker();
    void UpdatePageButtons();
    void RestoreRect(const DesignRect& rect);
    bool EraseNoteTooltip(const W8AutomapNote* note);

    std::optional<glm::vec3> PositionUnderCursor();
    W8AutomapNote* NoteUnderCursor();
    W8AutomapNote* CreateNote(glm::vec2 position, int layer);
    void EraseNote(W8AutomapNote* note);
    bool HandleKey(const InputEvent& input);
    void HandleNoteInput(const InputEvent& input);
    void HandleViewportInput(const InputEvent& input);

    Game& m_game;
    World& m_world;
    Automap& m_automap;
    AutomapGrid m_grid;
    // The explored cells' box, padded, and the height that frames all of it.
    glm::vec3 m_bounds_min{0};
    glm::vec3 m_bounds_max{0};
    float m_top_y = 25000.0f;
    // The camera's near clip: how far above the selected layer it floats.
    float m_near_clip = 1.0f;
    ZoomMode m_zoom_mode = ZoomMode::ZoomInOnly;
    W8AutomapTool m_tool = W8AutomapTool::Select;
    bool m_cursor_inside = false;
    bool m_redraw = true;
    bool m_overlay_redraw = true;

    // Index 0 is the ground; the rest are the level's and MapFilters.txt's layer planes.
    std::vector<ClipPlane*> m_layers;
    std::vector<std::unique_ptr<ClipPlane>> m_created_layers;
    std::unique_ptr<AutomapButtons> m_buttons;
    SDLSurfaceOwner m_surface{nullptr, SDL_DestroySurface};
    std::unique_ptr<OverlayQuad> m_party_marker;
    std::unique_ptr<OverlayQuad> m_friendly_marker;
    std::unique_ptr<OverlayQuad> m_neutral_marker;
    std::unique_ptr<OverlayQuad> m_hostile_marker;
    std::unique_ptr<OverlayQuad> m_text_marker;
    // The creature, item and note markers of the last marker pass.
    std::vector<std::unique_ptr<OverlayQuad>> m_markers;
    // Notes of the world's map, which owns them.
    W8AutomapNote* m_editing_note = nullptr;
    W8AutomapNote* m_hovered_note = nullptr;
};
