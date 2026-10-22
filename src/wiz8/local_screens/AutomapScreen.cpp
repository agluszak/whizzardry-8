#include "wiz8/local_screens/AutomapScreen.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
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
#include "local_code/Factions.h"
#include "mgs_commands.h"
#include "sight_state.h"
#include "wiz8/asset_paths.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/cursor.h"
#include "wiz8/design_rect.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/script.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/platform/input.h"
#include "wiz8/pointer.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/map_view.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/surface_storage.h"
#include "wiz8/ui_input.h"
#include "wiz8/unicode.h"
#include "wiz8/world/automap.h"
#include "wiz8/world/world.h"

namespace {
// UTF-16 units a note's text and its trailing cursor marker may take.
constexpr std::size_t kMaxNoteUnits = 40;
// The map's viewport in screen space.
constexpr DesignRect kViewport{12, 32, 467, 467};
constexpr SDL_Rect kViewportRegion{0xc, 0x20, 0x1d3 - 0xc, 0x1d3 - 0x20};

constexpr std::array<glm::ivec2, 5> kCursorOffsets{{{0, 0}, {8, 7}, {1, 24}, {1, 24}, {8, 7}}};

/* The sixteen chrome buttons: the catalog entry giving each one's artwork and state
   frames, and its position. A button's tooltip is its index in the string table. */
struct ButtonSpec {
    int catalog;
    glm::ivec2 position;
};
constexpr std::array<ButtonSpec, 16> kButtons{{
    {388, {490, 124}},
    {392, {541, 124}},
    {396, {591, 124}},
    {380, {490, 70}},
    {384, {541, 70}},
    {348, {591, 70}},
    {352, {591, 70}},
    {356, {591, 70}},
    {340, {490, 164}},
    {344, {591, 164}},
    {360, {540, 218}},
    {364, {540, 326}},
    {368, {486, 272}},
    {372, {594, 272}},
    {376, {540, 272}},
    {336, {588, 441}},
}};
enum ButtonSlot : std::size_t {
    kZoomInButton = 0,
    kZoomOutButton = 1,
    kFirstPageButton = 5,
    kLayerDownButton = 8,
    kLayerUpButton = 9,
};

void SetCursorImage(int tool)
{
    const auto artwork = CatalogArtwork::Load(tool + 0x14b);
    SetMouseCursorFromVideoObject(*artwork.spriteSheet(), artwork.imageOffset(),
                                  kCursorOffsets[tool].x, kCursorOffsets[tool].y);
    ScreenPointer().showCustom();
    RefreshMouseCursorTexture();
}

std::shared_ptr<Texture> LoadMarkerTexture(const char* path)
{
    auto texture = std::make_shared<Texture>(wiz8::asset_path(path), false);
    texture->setMipmap(Texture::MIPMAP_NONE);
    texture->setMagFilter(Texture::FILTER_BEST);
    texture->setMinFilter(Texture::FILTER_BEST);
    texture->loadSurface();
    return texture;
}

std::unique_ptr<OverlayQuad> CreateMarkerSprite(const char* path)
{
    std::shared_ptr<Texture> texture = LoadMarkerTexture(path);
    SDL_Surface* surface = texture->getSurface();
    if (!surface) return {};
    auto marker = makeOverlayFromTexture(texture, surface->w * g_viewport_x_scale,
                                         surface->h * g_viewport_y_scale, true, false);
    if (!marker) return {};
    marker->detach();
    return marker;
}

void ScaleMarker(OverlayQuad* marker, float factor)
{
    if (marker->scaledSize().y * factor < 1.0f) {
        marker->scale = {1.0, 1.0};
        factor = 1.0f / marker->scaledSize().y;
    }
    marker->scale = {factor, factor};
}

} // namespace

AutomapScreen::AutomapScreen(World& world)
    : m_world(world), m_automap(world.automap), m_grid(m_automap.grid())
{
}

AutomapScreen::~AutomapScreen() = default;

// The level's remembered view, which entering the screen places.
Automap::View& AutomapScreen::view()
{
    return *m_automap.view;
}

glm::vec2 AutomapScreen::WorldPosition(const glm::vec2& cursor)
{
    return glm::vec2(view().position.x, view().position.z) +
           (cursor - 0.5f) * glm::vec2(view().zoom, -view().zoom);
}

/* With no tool selected inside the map, use zoom-in only above the threshold.
   Retail's FNSTSW/TEST 0x41 also selects zoom-out for an unordered zoom. */
void AutomapScreen::SetToolCursor(W8AutomapTool tool)
{
    int cursor = static_cast<int>(tool);
    if (tool == W8AutomapTool::Select && m_cursor_inside) {
        cursor = view().zoom > 2.5e+04f ? 1 : 4;
    }
    SetCursorImage(cursor);
}

void AutomapScreen::SelectTool(W8AutomapTool tool)
{
    if (m_tool == tool) return;
    m_tool = tool;
    SetToolCursor(tool);
}

// Finishing a note drops the tool it was placed with.
void AutomapScreen::DropTool()
{
    if (m_tool == W8AutomapTool::Select) return;
    m_tool = W8AutomapTool::Select;
    SetCursorImage(0);
}

bool AutomapScreen::HasLayer(int layer) const
{
    return layer >= 0 && static_cast<std::size_t>(layer) < m_layers.size() && m_layers[layer];
}

/* The first live clip plane below a height, or the layer count when none
   qualifies. Notes and world-marker placement share this walk. */
int AutomapScreen::LayerAtHeight(double height) const
{
    int layer;
    for (layer = 0; layer < static_cast<int>(m_layers.size()); ++layer) {
        if (HasLayer(layer) && m_layers[layer]->transform.getLocation().y < height) break;
    }
    return layer;
}

void AutomapScreen::ClearMarkers()
{
    m_markers.clear();
}

W8AutomapNote* AutomapScreen::CreateNote(glm::vec2 position, int layer)
{
    auto* note = m_automap.addNote(position, layer);
    if (note) m_redraw = true;
    return note;
}

void AutomapScreen::EraseNote(W8AutomapNote* note)
{
    if (m_editing_note == note) m_editing_note = nullptr;
    if (m_hovered_note == note) m_hovered_note = nullptr;
    m_automap.eraseNote(note);
    m_redraw = true;
}

/* Center the map over the party. */
void AutomapScreen::CenterOnParty()
{
    const auto party = cameraLocation(m_world);
    auto position = view().position;
    position.x = party.x;
    position.z = party.z;
    SetCameraPoint(position);
}

/* Build the sixteen chrome buttons from their catalog artwork. */
void AutomapScreen::CreateButtons()
{
    const std::array<std::function<void()>, 16> actions{
        [this] { ZoomIn(glm::vec2(0.5f)); },
        [this] { ZoomOut(); },
        [this] { ResetZoom(); },
        [this] { SelectTool(W8AutomapTool::AddNote); },
        [this] { SelectTool(W8AutomapTool::EraseNote); },
        [this] { CyclePage(); },
        [this] { CyclePage(); },
        [this] { CyclePage(); },
        [this] {
            if (m_automap.layer > 0) SetLayer(m_automap.layer - 1);
        },
        [this] {
            if (m_automap.layer < static_cast<int>(m_layers.size())) SetLayer(m_automap.layer + 1);
        },
        [this] { Pan(0, 1); },
        [this] { Pan(0, -1); },
        [this] { Pan(-1, 0); },
        [this] { Pan(1, 0); },
        [this] { CenterOnParty(); },
        [this] {
            if (!m_editing_note) ReturnToPreviousScreen();
        },
    };
    auto buttons = std::make_unique<AutomapButtons>();
    for (std::size_t index = 0; index < buttons->buttons.size(); ++index) {
        auto artwork = CatalogArtwork::Load(kButtons[index].catalog);
        auto sprites = artwork.spriteSheet();
        if (!sprites) continue;
        const int base_frame = artwork.imageOffset();
        auto button = std::make_unique<W8ButtonWidget>(buttons->input);
        W8ButtonCallback callback = [action = actions[index]](W8ButtonWidget*) { action(); };
        if (button->Configure(ButtonGraphics::Load(std::move(sprites), base_frame + 3, base_frame,
                                                   base_frame + 1, base_frame + 2, base_frame + 2),
                              {.on_click = callback, .tooltip = static_cast<int>(index)})) {
            button->SetPosition(kButtons[index].position.x, kButtons[index].position.y);
        }
        buttons->buttons[index] = std::move(button);
    }
    m_buttons = std::move(buttons);
}

void AutomapScreen::Pan(float east, float north)
{
    auto position = view().position;
    position.x += east * 0.35f * view().zoom;
    position.z += north * 0.35f * view().zoom;
    SetCameraPoint(position);
}

/* Zoom out one step, or restore the full explored span from a halfway zoom. */
void AutomapScreen::ZoomOut()
{
    if (view().position.y >= m_top_y) return;
    if (m_zoom_mode == ZoomMode::Both) {
        ResetZoom();
        return;
    }
    const float ground_y = view().position.y - view().zoom;
    const float height = m_top_y - (m_top_y - ground_y) * 0.5f;
    if (view().position.y > height) {
        ResetZoom();
        return;
    }
    auto position = view().position;
    position.y = height;
    SetButtonMode(ZoomMode::Both);
    view().zoom = height - ground_y;
    SetCameraPoint(position);
    SetToolCursor(m_tool);
}

void AutomapScreen::CyclePage()
{
    m_automap.page = (m_automap.page + 1) % 3;
    UpdatePageButtons();
    m_redraw = true;
}

bool AutomapScreen::enter(const ScreenRequest&)
{
    // A level's first map places its camera once the explored bounds are known.
    const bool placed = m_automap.view.has_value();
    if (!placed) m_automap.view.emplace(Automap::View{glm::vec3(0), 0.0f});
    m_layers.assign(1, nullptr);
    int layer_number = 1;
    while (auto* layer = m_world.scene->clipPlane(fmt::format("LAYER {:d}", layer_number))) {
        m_layers.push_back(layer);
        ++layer_number;
    }
    SetMouseMoveEventsEnabled(1);
    m_cursor_inside = IsCursorInsideViewport();
    m_tool = W8AutomapTool::Select;
    SetToolCursor(W8AutomapTool::Select);
    CreateMarkerSprites();
    CreateButtons();
    m_party_marker->detach();
    m_surface = CreateCpuStorage(640, 480, SDL_PIXELFORMAT_ARGB1555);
    SetUiCanvasAlphaTestEnabled(false);
    ClearSurfaceRect(0, 0, 640, 480);
    DisableCursorScene();
    CatalogArtwork::Load(0x14a, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0);
    UpdatePageButtons();
    for (auto& button : m_buttons->buttons) {
        if (button) {
            button->m_dirty = true;
            button->Draw(GetFrameBufferSurface());
        }
    }
    ResetTransientRenderScenes();
    RenderFrame();
    RenderFrame();
    EnableCursorScene();
    SetScaledViewport(kViewport);
    UpdateBounds();
    std::vector<std::string> excluded_textures;
    if (Script script; script.Load("Data\\Automap\\MapFilters.txt")) {
        const auto& lines = script.lines;
        std::size_t line = 0;
        int section = -1;
        while (section < m_world.level_id && line < lines.size()) {
            if (lines[line].text.contains('[')) ++section;
            ++line;
        }
        if (line < lines.size() && section == m_world.level_id) {
            for (; line < lines.size(); ++line) {
                const auto& text = lines[line].text;
                if (text.contains('[')) break;
                if (!text.contains("LAYER=")) {
                    excluded_textures.push_back(text);
                    continue;
                }
                const float height = static_cast<float>(atof(text.c_str() + 6));
                auto& clip = *m_created_layers.emplace_back(std::make_unique<ClipPlane>());
                clip.setName(fmt::format("LAYER {:d}", layer_number).c_str());
                clip.setClipPlane(glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
                clip.transform.setLocation(glm::dvec3(0.0, height * 500.0, 0.0));
                clip.setClipType(ClipPlane::CLIP_POSITIONAL_0);
                clip.visible = false;
                m_layers.push_back(&clip);
                ++layer_number;
            }
        }
    }
    m_world.scene->map.excludeTextures(std::move(excluded_textures));
    if (placed)
        CenterOnParty();
    else
        ResetZoom();
    return true;
}

void AutomapScreen::frame()
{
    PumpUiInput({
        .before =
            [this](const InputEvent& input) {
                if (m_editing_note) {
                    HandleNoteInput(input);
                    return InputRoute::Handled;
                }
                if (HandleKey(input)) return InputRoute::Handled;
                if (!IsCursorInsideViewport()) {
                    if (m_cursor_inside) {
                        m_cursor_inside = false;
                        SetToolCursor(m_tool);
                    }
                    const SDL_Point point = CursorPosition();
                    UpdateUiPointer(point.x, point.y);
                    return IsMouseButtonEvent(input) ? InputRoute::Pass : InputRoute::Handled;
                }
                if (!m_cursor_inside) {
                    m_cursor_inside = true;
                    SetToolCursor(m_tool);
                }
                HandleViewportInput(input);
                return InputRoute::Handled;
            },
        .track_hover = false,
    });
    const bool left = IsKeyHeld(SDLK_LEFT), right = IsKeyHeld(SDLK_RIGHT);
    const bool up = IsKeyHeld(SDLK_UP), down = IsKeyHeld(SDLK_DOWN);
    if (left || right || up || down)
        Pan(static_cast<float>(right) - static_cast<float>(left),
            static_cast<float>(up) - static_cast<float>(down));
    Present();
}

/* A click, release or move over the map itself. */
void AutomapScreen::HandleViewportInput(const InputEvent& input)
{
    if (input.type == InputEventType::LeftButtonUp) {
        glm::vec2 point;
        if (m_tool == W8AutomapTool::AddNote) {
            if (GetCursorPositionInViewport(&point)) {
                m_editing_note = CreateNote(WorldPosition(point), m_automap.layer);
                SetNativeTextInputEnabled(m_editing_note != nullptr);
            }
        } else if (m_tool == W8AutomapTool::EraseNote) {
            if (auto* note = NoteUnderCursor()) EraseNote(note);
        } else if (GetCursorPositionInViewport(&point)) {
            ZoomIn(point);
        }
    } else if (input.type == InputEventType::RightButtonUp) {
        if (m_tool == W8AutomapTool::Select) {
            ZoomOut();
        } else {
            m_tool = W8AutomapTool::Select;
            SetToolCursor(W8AutomapTool::Select);
        }
    } else if (input.type == InputEventType::MouseMove) {
        W8AutomapNote* previous = m_hovered_note;
        m_hovered_note = NoteUnderCursor();
        if (previous != m_hovered_note) {
            EraseNoteTooltip(previous);
            RenderMarkers();
        }
        if (const auto point = PositionUnderCursor()) {
            const auto party = cameraLocation(m_world);
            const glm::vec3 distance(point->x - party.x, 0.0f, point->z - party.z);
            if (glm::length(distance) < view().zoom * 0.05f && m_tool == W8AutomapTool::Select &&
                m_zoom_mode != ZoomMode::ZoomOutOnly) {
                DisableCursorScene();
                m_party_marker->scale = {0.44f, 0.44f};
                return;
            }
        }
        const float factor = (1.0f / (view().zoom * 0.00004f)) * 0.44f;
        m_party_marker->scale = {factor, factor};
        EnableCursorScene();
    }
}

bool AutomapScreen::leave(ScreenDeparture)
{
    SetNativeTextInputEnabled(false);
    SetMouseMoveEventsEnabled(0);
    ScreenPointer().reset();
    EnableCursorScene();
    return true;
}

/* Select which zoom buttons are enabled for the mode, then dirty and redraw both. */
void AutomapScreen::SetButtonMode(ZoomMode mode)
{
    m_zoom_mode = mode;
    auto& zoom_in = m_buttons->buttons[kZoomInButton];
    auto& zoom_out = m_buttons->buttons[kZoomOutButton];
    zoom_in->SetEnabled(mode != ZoomMode::ZoomOutOnly);
    zoom_out->SetEnabled(mode != ZoomMode::ZoomInOnly);
    zoom_in->m_dirty = true;
    zoom_in->Draw(GetFrameBufferSurface());
    zoom_out->m_dirty = true;
    zoom_out->Draw(GetFrameBufferSurface());
}

/* Park the map camera over the requested point with its xz clamped into the
   explored bounds and reselect the layer underneath. */
void AutomapScreen::SetCameraPoint(glm::vec3 position)
{
    position.x = std::min(std::max(position.x, m_bounds_min.x), m_bounds_max.x);
    position.z = std::min(std::max(position.z, m_bounds_min.z), m_bounds_max.z);
    m_world.scene->map.camera.transform.setLocation(glm::dvec3(position));
    view().position = position;
    SetLayer(m_automap.layer);
    m_redraw = true;
    m_overlay_redraw = true;
}

/* Select the layer, remember how far above it the camera floats and refresh the
   layer-up/layer-down buttons. */
void AutomapScreen::SetLayer(int layer)
{
    m_near_clip = 1.0f;
    if (HasLayer(layer)) {
        const float height =
            view().position.y - static_cast<float>(m_layers[layer]->transform.getLocation().y);
        m_near_clip = std::max(height, 1.0f);
    }
    m_automap.layer = layer;
    m_redraw = true;
    m_overlay_redraw = true;
    if (auto& up = m_buttons->buttons[kLayerUpButton])
        up->SetEnabled(layer != static_cast<int>(m_layers.size()) - 1);
    if (auto& down = m_buttons->buttons[kLayerDownButton]) down->SetEnabled(layer != 0);
}

/* Reset the zoom to the full explored span, recenter over the bounds and
   restore the neutral button mode and cursor. */
void AutomapScreen::ResetZoom()
{
    view().zoom = m_top_y - m_bounds_min.y;
    glm::vec3 position = (m_bounds_min + m_bounds_max) * 0.5f;
    position.y = m_top_y;
    SetCameraPoint(position);
    SetToolCursor(m_tool);
    SetButtonMode(ZoomMode::ZoomInOnly);
}

/* Left-click zoom: drop a sight line onto the clicked point, halve the
   remaining height above ground and move the camera there. */
void AutomapScreen::ZoomIn(const glm::vec2& point)
{
    const auto location = WorldPosition(point);
    glm::vec3 position(location.x, m_top_y, location.y);
    const glm::vec3 from(location.x, m_top_y + kWorldScale, location.y);
    glm::vec3 to(location.x, m_grid.minimum.y, location.y);

    float ground;
    const auto* collision = m_world.collisionIndex();
    if (!collision || !collision->traceSight(from, to)) {
        ground = HasLayer(1) ? static_cast<float>(m_layers[1]->transform.getLocationY())
                             : m_grid.minimum.y;
    } else {
        ground = to.y;
    }

    float height = (m_top_y - ground) * 0.5f;
    const float minimum = ground + 2.5e+04f;
    if (m_zoom_mode != ZoomMode::ZoomInOnly || m_top_y <= minimum || height < minimum) {
        SetButtonMode(ZoomMode::ZoomOutOnly);
        height = minimum;
    } else {
        SetButtonMode(ZoomMode::Both);
    }
    view().zoom = height - ground;
    position.y = height;
    SetCameraPoint(position);
    SetToolCursor(m_tool);
}

/* Reveal the visited cells and recompute the explored box around them. */
void AutomapScreen::UpdateBounds()
{
    m_automap.reveal();
    if (!m_world.compiledIndex()) {
        const glm::vec3 span = m_grid.maximum - m_grid.minimum;
        m_top_y = std::max(std::max(span.x, span.z), 2.5e+04f);
        return;
    }
    m_bounds_min = glm::vec3(1e+09f, m_grid.minimum.y, 1e+09f);
    m_bounds_max = glm::vec3(-1e+09f, m_grid.maximum.y, -1e+09f);
    for (std::size_t cell = 0; cell < m_automap.cellCount(); ++cell) {
        if (!m_automap.visited(cell)) continue;
        const glm::vec3 position = m_automap.cellPosition(cell);
        m_bounds_min.x = std::min(m_bounds_min.x, position.x);
        m_bounds_min.z = std::min(m_bounds_min.z, position.z);
        m_bounds_max.x = std::max(m_bounds_max.x, position.x);
        m_bounds_max.z = std::max(m_bounds_max.z, position.z);
    }
    m_bounds_min.x -= 5e+03f;
    m_bounds_max.x += 5e+03f;
    m_bounds_min.z -= 5e+03f;
    m_bounds_max.z += 5e+03f;
    m_top_y =
        std::max(m_bounds_max.x - m_bounds_min.x, m_bounds_max.z - m_bounds_min.z) + m_bounds_max.y;
}

/* Re-render the map view into the map surface when it is dirty, blit it into the
   frame buffer and redraw the buttons. The layer is the map camera's near clip: how
   far above the layer it floats. */
void AutomapScreen::Present()
{
    if (m_redraw) {
        if (m_overlay_redraw) {
            auto& camera = m_world.scene->map.camera;
            const double half = view().zoom * 0.5f;
            camera.setViewPlane(Camera3D::Rect{-half, -half, half, half}, m_near_clip);
            camera.setClipRange(m_near_clip, 1500000.0);
            RenderMapToSurface(*m_surface, *m_world.scene, kViewport);
            m_overlay_redraw = false;
        }
        SDL_BlitSurface(m_surface.get(), &kViewportRegion, &GetFrameBufferSurface(),
                        &kViewportRegion);
        RenderMarkers();
        InvalidateRegion(0xc, 0x20, 0x1d3, 0x1d3);
        m_redraw = false;
    }
    for (auto& button : m_buttons->buttons)
        if (button) button->Draw(GetFrameBufferSurface());
    RenderFrame();
}

void AutomapScreen::UpdatePageButtons()
{
    const auto page = m_automap.page;
    for (unsigned int index = 0; index != 3; ++index)
        m_buttons->buttons[kFirstPageButton + index]->SetVisible(page == index);
    auto& shown = m_buttons->buttons[kFirstPageButton + page];
    shown->m_dirty = true;
    shown->Draw(GetFrameBufferSurface());
}

/* Blit the map surface back over a screen rect to erase whatever was drawn there. */
void AutomapScreen::RestoreRect(const DesignRect& rect)
{
    SDL_Surface& screen = GetFrameBufferSurface();
    const SDL_Rect region{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    SDL_BlitSurface(m_surface.get(), &region, &screen, &region);
}

/* When the cursor leaves a hovered note, erase its tooltip rectangle and
   invalidate it for redraw. Returns true when a tooltip was dismissed. */
bool AutomapScreen::EraseNoteTooltip(const W8AutomapNote* note)
{
    if (!note) return false;
    const int layer = note->layer + 1;
    const float floor_y = HasLayer(layer)
                              ? static_cast<float>(m_layers[layer]->transform.getLocationY())
                              : m_grid.minimum.y;
    if (LayerAtHeight(floor_y) - 1 != m_automap.layer) return false;
    const float zoom = view().zoom;
    const float left = view().position.x - zoom * 0.5f;
    const float bottom = view().position.z - zoom * 0.5f;
    if (note->position.x < left || left + zoom < note->position.x || note->position.y < bottom ||
        bottom + zoom < note->position.y)
        return false;
    const int screen_x = static_cast<int>((note->position.x - left) / zoom * -455.0f);
    const int screen_y = static_cast<int>((1.0f - (note->position.y - bottom) / zoom) * -435.0f);
    int marker_width;
    if (note == m_editing_note) {
        marker_width = static_cast<int>(m_text_marker->scaledSize().x * 0.22);
    } else {
        marker_width = static_cast<int>((1.0f / (zoom * 0.00004f)) * 0.44f *
                                        static_cast<double>(m_text_marker->scaledSize().x) * 0.5f);
    }
    DesignRect rect;
    rect.top = (0x20 - screen_y) - ((unsigned)g_wiz_text_font_secondary.height() >> 1);
    rect.left = marker_width / 2 + (0xe - screen_x);
    rect.bottom = rect.top + g_wiz_text_font_secondary.height() + 3;
    rect.right = g_wiz_text_font_secondary.textWidth(note->text) + 3 + rect.left;
    rect.left = std::max(rect.left, 0xc);
    rect.top = std::max(rect.top, 0x20);
    rect.right = std::min(rect.right, 0x1d3);
    rect.bottom = std::min(rect.bottom, 0x1d3);
    if (rect.right <= rect.left || rect.bottom <= rect.top) return false;
    RestoreRect(rect);
    InvalidateScreenRects(&rect, 1);
    return true;
}

/* The cursor's world position on the layer below the current one, at the grid
   minimum when no clip plane exists there. */
std::optional<glm::vec3> AutomapScreen::PositionUnderCursor()
{
    glm::vec2 point;
    if (!GetCursorPositionInViewport(&point)) return std::nullopt;
    const int layer = m_automap.layer + 1;
    const float height = HasLayer(layer)
                             ? static_cast<float>(m_layers[layer]->transform.getLocationY())
                             : m_grid.minimum.y;
    const auto location = WorldPosition(point);
    return glm::vec3(location.x, height, location.y);
}

/* The note on the current layer nearest the cursor, within a zoom-scaled pick radius. */
W8AutomapNote* AutomapScreen::NoteUnderCursor()
{
    W8AutomapNote* result = nullptr;
    double best = 99999999999.9;
    glm::vec2 point;
    if (m_automap.page == 2 || !GetCursorPositionInViewport(&point)) return nullptr;
    const auto location = WorldPosition(point);
    for (const auto& note : m_automap.notes()) {
        if (note->layer != m_automap.layer) continue;
        // NOLINTNEXTLINE(misc-include-cleaner)
        const float distance = glm::distance(location, note->position);
        if (distance < best && distance < view().zoom * 0.05) {
            best = distance;
            result = note.get();
        }
    }
    return result;
}

/* Load the marker textures and build the party, monster and text marker sprites
   over them. */
void AutomapScreen::CreateMarkerSprites()
{
    std::shared_ptr<Texture> texture = LoadMarkerTexture("Data\\Automap\\map_partymarker_a.tga");
    if (SDL_Surface* surface = texture->getSurface()) {
        m_party_marker = makeOverlayFromTexture(texture, surface->w * g_viewport_x_scale,
                                                surface->h * g_viewport_x_scale, true, true);
        m_party_marker->detach();
        g_compositor.attach(*m_party_marker, OverlayLayer::SquareAspect);
        m_party_marker->glow_enabled = true;
        m_party_marker->glow_colors = {glm::vec4(0.0f, 0.25f, 0.0f, 1.0f),
                                       glm::vec4(0.0f, 0.75f, 0.0f, 1.0f)};
        m_party_marker->glow_period = 2000;
    }
    m_friendly_marker = CreateMarkerSprite("Data\\Automap\\map_monsterfriendly_a.tga");
    m_neutral_marker = CreateMarkerSprite("Data\\Automap\\map_monsterneutral_a.tga");
    m_hostile_marker = CreateMarkerSprite("Data\\Automap\\map_monsterhostile_a.tga");
    m_text_marker = CreateMarkerSprite("Data\\Automap\\map_textmarker_a.tga");
}

/* Redraw the transient party, monster, item and note markers over the map surface. */
void AutomapScreen::RenderMarkers()
{
    const bool detect_all = PartyHasCondition(0x40);
    ClearMarkers();
    const float zoom = view().zoom;
    const float left = view().position.x - zoom * 0.5f;
    const float top = view().position.z - zoom * 0.5f;
    const auto shown = [&](float x, float z) {
        return x >= left && x <= left + zoom && z >= top && z <= top + zoom;
    };
    const auto screen = [&](float x, float z) {
        return glm::ivec2(0xc - static_cast<int>((x - left) / zoom * -455.0f),
                          0x20 - static_cast<int>((1.0f - (z - top) / zoom) * -435.0f));
    };
    const auto party = cameraLocation(m_world);
    if (!shown(party.x, party.z)) {
        m_party_marker->visible = false;
    } else {
        const auto point = screen(party.x, party.z);
        const float factor = (1.0f / (zoom * 0.00004f)) * 0.44f;
        m_party_marker->scale = {factor, factor};
        if (m_party_marker->scaledSize().y < 1) {
            m_party_marker->scale = {1.0, 1.0};
            const float minimum = 1.0f / m_party_marker->scaledSize().y;
            m_party_marker->scale = {minimum, minimum};
        }
        const unsigned short width = m_party_marker->scaledSize().x;
        const unsigned short height = m_party_marker->scaledSize().y;
        positionOverlay(m_party_marker.get(), point.x - (width >> 1), point.y - (height >> 1),
                        false);
        m_party_marker->rotation = m_world.party_camera.m_yaw;
        m_party_marker->visible = true;
        m_party_marker->detach();
        g_compositor.attach(*m_party_marker, OverlayLayer::SquareAspect);
    }
    auto& population = *m_world.population;
    for (const auto& info : population.monster_roster.live()) {
        Monster& monster = *info->entity;
        const glm::vec3 location = monster.visual.GetLocation();
        if (!detect_all && (monster.disabled || info->party_threat.sight_state != W8_SIGHT_SEEN))
            continue;
        if (!shown(location.x, location.z)) continue;
        OverlayQuad* marker;
        switch (info->ubDisposition) {
        case W8_DISPOSITION_NEUTRAL:
            marker = MonsterMarker(1);
            break;
        case W8_DISPOSITION_HOSTILE:
            marker = MonsterMarker(2);
            break;
        case W8_DISPOSITION_FRIENDLY:
            marker = MonsterMarker(0);
            break;
        default:
            continue;
        }
        const auto point = screen(location.x, location.z);
        const unsigned short width = marker->scaledSize().x;
        const unsigned short height = marker->scaledSize().y;
        g_compositor.attach(*marker, OverlayLayer::Fullscreen);
        positionOverlay(marker, point.x - (width >> 1), point.y - (height >> 1), false);
    }
    for (const auto& world_item : population.world_items) {
        WorldItemEntity* item = world_item->entity.find();
        if (!item) continue;
        const glm::vec3 location = item->GetSearchPosition();
        const int layer = LayerAtHeight(location.y);
        if ((item->flags & W8_ITEM_ENTITY_NO_PICKUP) != 0 ||
            !(detect_all || (item->flags & W8_ITEM_ENTITY_RADAR_SEEN) != 0 ||
              m_automap.cameraSees(location)) ||
            layer - 1 != m_automap.layer || !shown(location.x, location.z))
            continue;
        if (OverlayQuad* marker = ItemMarker(world_item->item.iItemNo)) {
            const auto point = screen(location.x, location.z);
            const unsigned short width = marker->scaledSize().x;
            const unsigned short height = marker->scaledSize().y;
            positionOverlay(marker, point.x - (width >> 1), point.y - (height >> 1), false);
            g_compositor.attach(*marker, OverlayLayer::Fullscreen);
        }
    }
    if (m_automap.page == 2) return;
    ScopedClipRect clipped(GetFrameBufferSurface(), kViewportRegion);
    g_wiz_text_font_secondary.setShade(Font::kNormalShade);
    const int text_height = g_wiz_text_font_secondary.height() & 0xffff;
    for (const auto& owned : m_automap.notes()) {
        const W8AutomapNote* note = owned.get();
        if (!shown(note->position.x, note->position.y)) continue;
        auto point = screen(note->position.x, note->position.y);
        OverlayQuad* marker = TextMarker();
        if (note == m_editing_note) marker->scale = {0.22f, 0.22f};
        const unsigned short width = marker->scaledSize().x;
        const unsigned short height = marker->scaledSize().y;
        point.x -= width >> 1;
        point.y -= height >> 1;
        positionOverlay(marker, point.x, point.y, false);
        g_compositor.attach(*marker, OverlayLayer::Fullscreen);
        if (note->layer != m_automap.layer || note->text.empty() ||
            !(m_automap.page == 0 || note == m_hovered_note || note == m_editing_note))
            continue;
        point.y += (height >> 1) - (text_height >> 1);
        point.x += width + 2;
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                              {point.x, point.y}, note->text);
        if (note != m_editing_note) continue;
        const auto color =
            static_cast<Uint16>(Get16BPPColor(SDL_Color{239, 155, 86, SDL_ALPHA_OPAQUE}));
        const int length = g_wiz_text_font_secondary.textWidth(note->text);
        const std::array edges{
            SDL_Rect{point.x - 1, point.y, length + 4, 1},
            SDL_Rect{point.x - 1, point.y + text_height, length + 4, 1},
            SDL_Rect{point.x - 1, point.y, 1, text_height + 1},
            SDL_Rect{point.x + length + 2, point.y, 1, text_height + 1},
        };
        for (const auto& edge : edges) SDL_FillSurfaceRect(&GetFrameBufferSurface(), &edge, color);
    }
}

/* A scaled blip for a world item: its inventory icon blitted into a scratch surface
   and wrapped in a 2D brush, kept until the next marker pass. */
OverlayQuad* AutomapScreen::ItemMarker(int item_id)
{
    const auto sprites = ItemSprites(item_id);
    const SpriteSheet& video_object = *sprites;
    if (video_object.frames.empty()) return nullptr;
    const SpriteFrame& properties = video_object.frames.front();
    auto surface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(0x40, 0x40, SDL_PIXELFORMAT_ARGB1555).release(), SDL_DestroySurface);
    SDL_FillSurfaceRect(surface.get(), nullptr, 0);
    BlitVideoObjectToColorSurface(video_object, 0, *surface, (0x40 - properties.width) / 2,
                                  (0x40 - properties.height) / 2);
    const auto width = surface->w * g_viewport_x_scale;
    const auto height = surface->h * g_viewport_y_scale;
    auto owned =
        makeOverlayFromTexture(std::make_shared<Texture>(surface), width, height, true, false);
    if (!owned) return nullptr;
    auto* marker = m_markers.emplace_back(std::move(owned)).get();
    marker->detach();
    marker->glow_enabled = true;
    marker->glow_colors = {glm::vec4(0.0f, 0.0f, 0.25f, 1.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)};
    marker->glow_period = 2000;
    ScaleMarker(marker, (1.0f / (view().zoom * 0.00004f)) * 0.44f);
    return marker;
}

/* A clone of the friendly, neutral or hostile blip, kept until the next marker pass. */
OverlayQuad* AutomapScreen::MonsterMarker(int type)
{
    const std::array prototypes{m_friendly_marker.get(), m_neutral_marker.get(),
                                m_hostile_marker.get()};
    const auto& prototype = *prototypes[type];
    auto* marker =
        m_markers
            .emplace_back(makeOverlayFromTexture(prototype.texture, prototype.size.x / 640.0,
                                                 prototype.size.y / 480.0, true, false))
            .get();
    marker->detach();
    const std::array colors{glm::vec4(0, .5f, 0, 1), glm::vec4(.25f, .25f, 0, 1),
                            glm::vec4(.5f, 0, 0, 1)};
    marker->glow_colors = {glm::vec4(0, 0, 0, 1), colors[type]};
    marker->glow_enabled = true;
    marker->glow_period = 2000;
    /* Retail scales by 0.85 as a step of its own; written as one product, VC6
       folds 0.44 * 0.85 into a single constant. */
    float factor = (1.0f / (view().zoom * 0.00004f)) * 0.44f;
    factor *= 0.85f;
    ScaleMarker(marker, factor);
    return marker;
}

/* A clone of the note blip, kept until the next marker pass. */
OverlayQuad* AutomapScreen::TextMarker()
{
    const auto& prototype = *m_text_marker;
    auto* marker =
        m_markers
            .emplace_back(makeOverlayFromTexture(prototype.texture, prototype.size.x / 640.0,
                                                 prototype.size.y / 480.0, true, false))
            .get();
    marker->detach();
    ScaleMarker(marker, (1.0f / (view().zoom * 0.00004f)) * 0.44f * 0.5f);
    marker->glow_enabled = true;
    marker->glow_colors = {glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(1.0f, 0.4f, 0.0f, 1.0f)};
    marker->glow_period = 2000;
    return marker;
}

/* While a note is being edited, typing edits its text, Return keeps it, Escape
   discards it, and a left click inside the map moves it to the cursor. */
void AutomapScreen::HandleNoteInput(const InputEvent& input)
{
    if (input.type != InputEventType::KeyDown && input.type != InputEventType::KeyRepeat &&
        input.type != InputEventType::Text) {
        glm::vec2 point;
        if (input.type == InputEventType::LeftButtonUp && IsCursorInsideViewport() &&
            GetCursorPositionInViewport(&point)) {
            m_editing_note->position = WorldPosition(point);
            m_redraw = true;
        }
        return;
    }
    const bool committed = input.type == InputEventType::Text;
    const char32_t key = committed ? input.text : 0;
    const SDL_Keycode pressed = committed ? 0 : input.key;
    EraseNoteTooltip(m_editing_note);
    auto& text = m_editing_note->text;
    const auto last = text.empty() ? 0 : text.size() - 1; // Trailing cursor marker.
    if (key >= SDLK_SPACE && key <= 0x10ffff &&
        wiz8::text::to_utf16(text).size() + (key > 0xffff ? 2 : 1) <= kMaxNoteUnits) {
        std::string inserted;
        wiz8::text::append(inserted, key);
        text.insert(last, inserted);
        m_redraw = true;
    } else if (pressed == SDLK_BACKSPACE) {
        if (last) {
            const auto previous = wiz8::text::previous_grapheme(text, last);
            text.erase(previous, last - previous);
        }
        m_redraw = true;
    } else if (pressed == SDLK_RETURN) {
        text.resize(last);
        if (text.empty()) EraseNote(m_editing_note);
        DropTool();
        m_editing_note = nullptr;
        m_redraw = true;
    } else if (pressed == SDLK_ESCAPE) {
        EraseNote(m_editing_note);
        DropTool();
    }
    if (m_editing_note) {
        RenderMarkers();
        return;
    }
    SetNativeTextInputEnabled(false);
    DropTool();
}

/* Key handling: the automap command and Escape leave, the zoom keys, page cycling,
   the tool keys and the layer keys. */
bool AutomapScreen::HandleKey(const InputEvent& input)
{
    if (input.type != InputEventType::KeyDown) return false;
    const MGSKeyBinding* binding =
        g_mgs_keyboard.GetBinding(g_mgs_keyboard.FindBinding(W8_MGS_COMMAND_AUTOMAP));
    if (input.key == binding->key && input.modifiers == binding->modifiers) {
        ReturnToPreviousScreen();
        return true;
    }
    switch (input.key) {
    case SDLK_BACKSPACE:
        ZoomOut();
        return true;
    case SDLK_RETURN:
        ZoomIn(glm::vec2(0.5f));
        return true;
    case SDLK_ESCAPE:
        /* Escape drops the tool, or leaves the screen when none is held. */
        if (m_tool == W8AutomapTool::Select) {
            ReturnToPreviousScreen();
            return true;
        }
        m_tool = W8AutomapTool::Select;
        SetToolCursor(W8AutomapTool::Select);
        return true;
    case SDLK_SPACE:
        CyclePage();
        return true;
    case SDLK_END:
        CenterOnParty();
        break;
    case SDLK_HOME:
        ResetZoom();
        return true;
    case SDLK_INSERT:
        SelectTool(W8AutomapTool::AddNote);
        return true;
    case SDLK_DELETE:
        SelectTool(W8AutomapTool::EraseNote);
        return true;
    case SDLK_1:
    case SDLK_2:
    case SDLK_3:
    case SDLK_4:
        SetLayer(static_cast<int>(input.key - SDLK_1));
        return true;
    }
    return false;
}
