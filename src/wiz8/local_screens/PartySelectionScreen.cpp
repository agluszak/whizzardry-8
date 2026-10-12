#include <cstdint>
#include <SDL3/SDL_surface.h>
#include "himage.h"
#include <fmt/format.h>
#include <fmt/printf.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_rect.h>
#include <string_view>
#include <string>
#include <utility>
#include <memory>
#include <vector>
#include <array>
#include <cstring>
#include <exception>
#include <optional>

#include "image.h"
#include "sprite.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/ControlsRect.h"
#include "input.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/Widget.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_code/ControlSelection.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/layouts/character.h"
#include "wiz8/character_skills.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/PartyImport.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/cursor.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/music_playlist.h"
#include "wiz8/regions.h"
#include "wiz8/fonts.h"
#include "wiz8/text_input.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/filesystem.h"
#include "Font.h"
#include "compat/compiler.h"
#include "difficulty.h"
#include "file_time.h"

/* Party-selection and imported-character UI. The original translation-unit
   spelling is not established; this descriptive name is provisional. */

/* Two ordinary growable vectors and the scroll origin account for all 0x24
   bytes allocated at party-selection entry. The second vector supplies the names this
   control renders; the first owns the corresponding party records. */
struct W8PartySelectionCharacterCollection {
    W8PartySelectionCharacterCollection() : first_visible(0) {}
    ~W8PartySelectionCharacterCollection();
    W8Character* GetCharacter(std::int32_t index);
    std::int32_t FindPartySlot(std::int32_t index);
    void DetachFromParty(std::int32_t index);
    void DeleteAt(std::int32_t index);
    void ClearCharacters();
    void ReloadCharacters();
    void LoadExternalCharacters();
    void SortCharactersByWriteTime();

    std::vector<W8Character*> characters;
    std::vector<char*> names;
    std::int32_t first_visible;
};

// GLOBAL: WIZ8 0x0069C4EC
static std::unique_ptr<W8PartySelectionCharacterCollection> g_party_selection_character_collection;




/* Imported characters not installed in the active party are owned here. Name
   strings are separately owned by the second vector. Clearing the counts
   before the two ordinary vector destructors preserves their storage teardown
   without asking the vector template to own its pointer elements. */
// FUNCTION: WIZ8 0x005be270
W8PartySelectionCharacterCollection::~W8PartySelectionCharacterCollection()
{
    std::int32_t index;
    ClearCharacters();
    for (index = 0; index < static_cast<std::int32_t>(names.size()); ++index) {
        delete names[index];
    }
    names.clear();
}

void W8PartySelectionCharacterCollection::ClearCharacters()
{
    std::int32_t index;
    for (index = 0; index < static_cast<std::int32_t>(characters.size()); ++index) {
        W8Character* character = GetCharacter(index);
        if (!character->fInParty) {
            delete character;
        }
    }
    characters.clear();
}

void W8PartySelectionCharacterCollection::ReloadCharacters()
{
    ClearCharacters();
    W8PartySlotRow* rows = g_status.buffers.XChar;
    for (std::int32_t slot = 2; slot < 8; ++slot) {
        if (rows[slot].fOccupied) {
            characters.push_back(&g_status.buffers.Char[slot]);
        }
    }
    LoadExternalCharacters();
    SortCharactersByWriteTime();
}

// FUNCTION: WIZ8 0x005be4b0
W8Character* W8PartySelectionCharacterCollection::GetCharacter(std::int32_t index)
{
    if (static_cast<std::size_t>(index) < characters.size()) {
        return characters[index];
    }
    return 0;
}

// FUNCTION: WIZ8 0x005be4d0
void W8PartySelectionCharacterCollection::DetachFromParty(std::int32_t index)
{
    W8Character* previous = GetCharacter(index);
    std::int32_t slot = FindPartySlot(index);
    W8Character* replacement = new W8Character;
    std::string path;
    path = BuildCharacterPath(previous->name, -1);
    if (!LoadCharacter(path, replacement, -1, false)) {
        *replacement = *previous;
    }
    RemoveCharacterFromParty(slot + 2, false);
    replacement->fInParty = false;
    characters[index] = replacement;
}

// FUNCTION: WIZ8 0x005be5f0
std::int32_t W8PartySelectionCharacterCollection::FindPartySlot(std::int32_t index)
{
    W8Character* character = GetCharacter(index);
    if (character && character->fInParty) {
        W8PartySlotRow* rows = g_status.buffers.XChar;
        for (std::int32_t slot = 0; slot < 6; ++slot) {
            if (rows[slot + 2].fOccupied && &g_status.buffers.Char[slot + 2] == character) {
                return slot;
            }
        }
    }
    return -1;
}

// FUNCTION: WIZ8 0x005c34d0
void W8PartySelectionCharacterCollection::DeleteAt(std::int32_t index)
{
    if (index >= 0 && std::cmp_less(index, characters.size())) {
        auto* object = characters[index];
        characters.erase(characters.begin() + index);
        delete object;
    }
}

/* Enumerate loose CHR files, loading only characters that are not already in
   an occupied party slot.  The collection owns every record accepted here;
   records rejected by loading or duplicate-name detection are destroyed
   immediately. */
// FUNCTION: WIZ8 0x005be340
void W8PartySelectionCharacterCollection::LoadExternalCharacters()
{
    std::string search_path;

    search_path = BuildCharacterFilePath(fmt::format("*.{}", "CHR"), -1);
    const std::string pattern(search_path);
    const auto separator = pattern.find_last_of("\\/");
    for (const auto& entry : wiz8::list_directory(pattern.substr(0, separator), "*.CHR")) {
        W8Character* character = new W8Character;
        if (!LoadCharacter(entry.c_str(), character, -1, false)) {
            delete character;
        } else {
            std::int32_t slot;
            for (slot = 2; slot < 8; ++slot) {
                if (g_status.buffers.XChar[slot].fOccupied &&
                    strcmp(g_status.buffers.Char[slot].name.c_str(), character->name.c_str()) == 0) {
                    break;
                }
            }
            if (slot < 8) {
                delete character;
            } else {
                characters.push_back(character);
            }
        }
    }
}

/* Sort newest files first while carrying each character pointer with its file
   time. Small partitions use insertion sort; larger partitions use the last
   time as the quicksort pivot, matching the retail split at ten elements. */
// FUNCTION: WIZ8 0x005c3520
static void SortPartySelectionCharactersByTime(W8Character** characters, wiz8::DiskFileTime* times,
                                               std::int32_t first, std::int32_t last)
{
    if (last - first < 9) {
        for (std::int32_t next = first + 1; next <= last; ++next) {
            wiz8::DiskFileTime time = times[next];
            W8Character* character = characters[next];
            std::int32_t insert = next;
            while (insert > first && times[insert - 1].ticks() < time.ticks()) {
                times[insert] = times[insert - 1];
                characters[insert] = characters[insert - 1];
                --insert;
            }
            times[insert] = time;
            characters[insert] = character;
        }
        return;
    }

    wiz8::DiskFileTime pivot = times[last];
    std::int32_t left = first - 1;
    std::int32_t right = last;
    for (;;) {
        do {
            ++left;
        } while (left < last && times[left].ticks() > pivot.ticks());
        do {
            --right;
        } while (right > first && times[right].ticks() < pivot.ticks());
        if (left >= right) {
            break;
        }
        wiz8::DiskFileTime time = times[left];
        times[left] = times[right];
        times[right] = time;
        W8Character* character = characters[left];
        characters[left] = characters[right];
        characters[right] = character;
    }
    times[last] = times[left];
    times[left] = pivot;
    W8Character* character = characters[left];
    characters[left] = characters[last];
    characters[last] = character;
    if (first < left - 1) {
        SortPartySelectionCharactersByTime(characters, times, first, left - 1);
    }
    if (left + 1 < last) {
        SortPartySelectionCharactersByTime(characters, times, left + 1, last);
    }
}

// FUNCTION: WIZ8 0x005be650
void W8PartySelectionCharacterCollection::SortCharactersByWriteTime()
{
    if (static_cast<std::int32_t>(characters.size()) <= 1) {
        return;
    }

    std::vector<wiz8::DiskFileTime> times(characters.size());
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(characters.size()); ++index) {
        std::string path;
        path = BuildCharacterPath(characters[index]->name, -1);
        std::unique_ptr<wiz8::File> handle = [&]() { try { return wiz8::open_file(path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
        if (handle) {
            times[index] = handle->times().modified;
            if (handle) handle->close();
            handle.reset();
        }
    }
    SortPartySelectionCharactersByTime(characters.data(), times.data(), 0,
                                       static_cast<std::int32_t>(characters.size()) - 1);
}

class W8PartySelectionListControl;

class W8PartySelectionListSelectionListener {
public:
    virtual void OnSelectionChanged(W8PartySelectionListControl* control, std::int32_t selection) = 0;
};

/* State 5's scrolling party-name list. Its secondary vtable is the existing
   range callback; its own listener is the controller's independently observed
   +4 callback subobject. */
// VTABLE: WIZ8 0x005ef464
class W8PartySelectionListControl : public W8Widget, public W8RangeListener {
public:
    W8PartySelectionListControl(Controls* panel, std::uint32_t region, std::int32_t left, std::int32_t top, std::int32_t right,
                                std::int32_t bottom)
        : W8Widget(panel, region, left, top, right, bottom), m_visible_rows(0x11), m_selection(0),
          m_hovered(-1), m_first_visible(0), m_listener(0)
    {
    }

    virtual void Redraw(bool full_redraw) override;
    virtual void OnMouseLeave(std::int32_t event) override;
    virtual void OnMouseMove(std::int32_t event) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;
    virtual void OnRangeChanged(W8RangeControl* control) override;

    std::int32_t m_visible_rows;
    std::int32_t m_selection;
    std::int32_t m_hovered;
    std::int32_t m_first_visible;
    W8PartySelectionListSelectionListener* m_listener;
};

// FUNCTION: WIZ8 0x005bff60
void W8PartySelectionListControl::Redraw(bool full_redraw)
{
    if (m_active && (m_dirty || full_redraw)) {
        std::int32_t left = m_pPanel->m_bounds.left + m_left;
        std::int32_t top = m_pPanel->m_bounds.top + m_top;
        std::int32_t right = m_pPanel->m_bounds.left + m_right;
        std::int32_t bottom = m_pPanel->m_bounds.top + m_bottom;

        InvalidateRegion(left, top, right, bottom, 0);
        CatalogArtwork::Load(0x1b6, 0).drawRect(GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left, top);
        SDL_Rect clip{left, top, right - left, bottom - top};
        ScopedClipRect clipping(GetFrameBufferSurface(), clip);

        std::int32_t end = m_first_visible + m_visible_rows;
        if (static_cast<std::int32_t>(g_party_selection_character_collection->names.size()) <= end) {
            end = static_cast<std::int32_t>(g_party_selection_character_collection->names.size());
        }
        top += 1;
        for (std::int32_t row = m_first_visible; row < end; ++row) {
            Palette16 colour = g_font_state_palettes[W8_FONT_PALETTE_BLUE];
            if (row != m_selection) {
                colour = g_wiz_text_font_secondary.glyphs->colors;
                if (row == m_hovered) {
                    colour = g_font_state_palettes[W8_FONT_PALETTE_YELLOW];
                }
            }
            g_wiz_text_font_secondary.setPalette(colour);
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left + 2, top}, g_party_selection_character_collection->names[row]);
            top += 0x0e;
        }
        g_wiz_text_font_secondary.resetPalette();
        m_dirty = false;
    }
}

// FUNCTION: WIZ8 0x005c00c0
void W8PartySelectionListControl::OnMouseLeave(std::int32_t event)
{
    m_hovered = -1;
    Invalidate(static_cast<std::uint8_t>(event));
}

// FUNCTION: WIZ8 0x005c00e0
void W8PartySelectionListControl::OnMouseMove(std::int32_t)
{
    SDL_Point point;
    SGPMouseGetPos(&point);
    std::int32_t hovered = (point.x - m_pPanel->m_bounds.top - m_top) / 0x0e + m_first_visible;
    if (hovered != m_hovered) {
        m_hovered = hovered;
        Invalidate(false);
    }
}

// FUNCTION: WIZ8 0x005c0140
void W8PartySelectionListControl::OnLeftButtonUp(std::int32_t event)
{
    SDL_Point point;
    SGPMouseGetPos(&point);
    std::int32_t selection = (point.y - m_pPanel->m_bounds.top - m_top) / 0x0e + m_first_visible;
    if (selection != m_selection) {
        m_selection = selection;
        Invalidate(static_cast<std::uint8_t>(event));
        if (m_listener) {
            m_listener->OnSelectionChanged(this, m_selection);
        }
    }
}

// FUNCTION: WIZ8 0x005c01b0
void W8PartySelectionListControl::OnRangeChanged(W8RangeControl* control)
{
    m_first_visible = control->m_value;
    Invalidate(false);
}

class W8PartySelectionInputHandler;

class W8PartySelectionDecisionListener {
public:
    virtual void OnDecision(W8PartySelectionInputHandler* handler, bool accepted) = 0;
    virtual void OnToggle(std::int32_t value) = 0;
};

class W8PartySelectionPanelSelectionListener {
public:
    virtual void SelectPartyMemberRow(std::int32_t row) = 0;
    virtual void OpenCampForSelectedMember(std::int32_t row) = 0;
    virtual void AdjustPartyMemberRange(std::int32_t amount) = 0;
};

/* Each visible character row is the same text control that W8Control stores
   and selects. The three added dwords are the visible row, absolute character
   index, and the panel callback used for row/scroll actions. */
class W8PartySelectionCharacterRow : public W8TextControl {
public:
    W8PartySelectionCharacterRow(Controls* panel, std::int32_t top, std::int32_t row);
    virtual void Redraw(bool full_redraw) override;
    virtual void AdjustValue(std::int32_t amount) override;
    virtual void OnRightButtonUp(std::int32_t event) override;
    virtual void OnLeftButtonDoubleClick(std::int32_t event) override;

    std::int32_t m_row;
    std::int32_t m_character_index;
    W8PartySelectionPanelSelectionListener* m_selection_listener;
};

// VTABLE: WIZ8 0x005EF4BC W8PartySelectionInputHandler
// class W8PartySelectionInputHandler
class W8PartySelectionInputHandler {
public:
    W8PartySelectionInputHandler() {}
    virtual ~W8PartySelectionInputHandler()
    {
        RemoveTextInputField(0);
        KillTextInputMode();
    }
    std::uint8_t HandleInput(const InputEvent* input);

    W8PartySelectionDecisionListener* m_listener;
};

/* Controls owns the six row widgets. The three callback bases occupy +0x4c,
   +0x50 and +0x54; the complete W8Control member at +0x58 owns their selection
   vector. The selected absolute row at +0x80 is read directly by the party-selection
   keyboard handler. */
// VTABLE: WIZ8 0x005ef3b4 W8PartySelectionPanelSelectionListener
class W8PartySelectionCharacterPanel : public Controls,
                                       public W8ControlSelectionListener,
                                       public W8RangeListener,
                                       public W8PartySelectionPanelSelectionListener {
public:
    W8PartySelectionCharacterPanel();
    virtual ~W8PartySelectionCharacterPanel();
    virtual void OnSelectionChanged(W8ControlSelection* control, std::int32_t selected) override;
    virtual void OnRangeChanged(W8RangeControl* control) override;
    virtual void SelectPartyMemberRow(std::int32_t row) override;
    virtual void OpenCampForSelectedMember(std::int32_t row) override;
    virtual void AdjustPartyMemberRange(std::int32_t amount) override;
    /* Descriptive name for the visible character-row refresh operation. */
    void RefreshVisibleRows();
    void SynchronizeVisibleSelection();
    void SetSelectedRow(std::int32_t selection);

    W8ControlSelection m_control;
    W8RangeControl* m_range;
    std::int32_t m_selected_row;
};

// VTABLE: WIZ8 0x005ef448 W8TextControl::Listener
class W8PartySelectionCharacterGridPanel : public Controls, public W8TextControl::Listener {
public:
    W8PartySelectionCharacterGridPanel();
    virtual ~W8PartySelectionCharacterGridPanel();
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSecondary(W8TextControl*) override {}
};

class W8PartySelectionPartySlotRow : public W8TextControl {
public:
    W8PartySelectionPartySlotRow(Controls* panel, std::int32_t row);
    virtual void Redraw(bool full_redraw) override;
    virtual void OnRightButtonUp(std::int32_t event) override;
    virtual void OnLeftButtonDoubleClick(std::int32_t event) override;

    std::int32_t m_row;
    W8Widget* m_redraw_partner;
};

class W8PartySelectionPartySlotPanel : public Controls, public W8ControlSelectionListener {
public:
    friend class W8PartySelectionController;

    W8PartySelectionPartySlotPanel();
    virtual ~W8PartySelectionPartySlotPanel();
    virtual void OnSelectionChanged(W8ControlSelection* control, std::int32_t selected) override;

    W8ControlSelection m_control;
};

class W8PartySelectionCharacterSummaryPanel : public Controls {
public:
    W8PartySelectionCharacterSummaryPanel() : Controls(), m_character(0) {}
    virtual void Redraw() override;

    W8Character* m_character;
};

/* Unlike the two selection panels above, the option panel does not inherit
   W8Control.  Its constructor writes a mode word at +0x4c and constructs a
   complete W8Control member at +0x50.  The two toggle controls and the owned
   option-entry vector follow that member at the observed offsets. */
class W8PartySelectionOptionPanel : public Controls {
public:
    W8PartySelectionOptionPanel();
    virtual ~W8PartySelectionOptionPanel();
    virtual void Redraw() override;
    void SetOptionPanelMode(W8PartyCreationPage page);

    W8PartyCreationPage m_page;
    W8ControlSelection m_options;
    W8TextControl* m_toggle;
    W8TextControl* npc_interact_toggle;
    std::vector<std::unique_ptr<W8TextBuffer>> m_entries;
    std::int32_t m_render_left;
    std::int32_t m_render_top;
    std::int16_t m_image_width;
    std::int16_t m_image_height;
};

/* The party-selection owner is established by three construction-phase abstract
   vtables, seven registered text controls, the list-selection callback above,
   and independently observed owned state through +0x6c. The address-qualified
   name does not claim a source-era screen name. */
// VTABLE: WIZ8 0x005ef4cc
class W8PartySelectionController final : public W8TextControl::Listener,
                                   public W8PartySelectionListSelectionListener,
                                   public W8PartySelectionDecisionListener {
public:
    W8PartySelectionController() : m_character(0) {}

    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSecondary(W8TextControl*) override {}
    virtual void OnSelectionChanged(W8PartySelectionListControl* control, std::int32_t selection) override;
    virtual void OnDecision(W8PartySelectionInputHandler* handler, bool accepted) override;
    virtual void OnToggle(std::int32_t value) override;

    void SetMode(W8PartySelectionMode mode);
    void SetSelection(std::int32_t selection, bool highlighted, bool refresh);
    void OpenNotification(std::string_view message, bool allow_cancel,
                          W8PartyConfirmationAction action);
    void Setup();
    void InvalidatePartySelectionComposition();
    void DrawPartySelectionComposition();
    void ApplyPartySelectionConfirmation(W8PartyConfirmationAction action, bool accepted);
    void LoadImportedPartyFile(std::int32_t selection);
    void TogglePartyMemberSelection();

    W8PartySelectionMode m_mode;
    W8PartySelectionMode m_previous_mode;
    bool m_redraw_backdrop;
    std::uint8_t pad_15[3];
    W8Character* m_character;
    std::unique_ptr<W8RangeControl> m_range;
    std::unique_ptr<W8PartySelectionCharacterPanel> m_character_panel;
    std::unique_ptr<W8PartySelectionCharacterGridPanel> m_control0;
    std::unique_ptr<W8PartySelectionPartySlotPanel> m_control1;
    std::unique_ptr<W8PartySelectionCharacterSummaryPanel> m_control2;
    std::unique_ptr<W8PartySelectionOptionPanel> m_control3;
    std::unique_ptr<Controls> m_left_panel;
    std::unique_ptr<Controls> m_import_panel;
    std::unique_ptr<Controls> m_bottom_panel;
    W8TextControl* m_create_button;
    W8TextControl* m_add_remove_button;
    W8TextControl* m_delete_button;
    W8TextControl* m_review_button;
    W8TextControl* m_reset_button;
    W8TextControl* m_confirm_button;
    W8TextControl* m_back_button;
    W8PartySelectionListControl* m_list;
    std::unique_ptr<W8TextBuffer> m_text_buffer;
    std::unique_ptr<W8PartySelectionInputHandler> m_input_handler;
    std::unique_ptr<W8MessageDialogBase> m_dialog;
    W8PartyConfirmationAction m_confirmation_action;
};

// GLOBAL: WIZ8 0x0069C4E8
static std::unique_ptr<W8PartySelectionController> g_party_selection_controller;

// FUNCTION: WIZ8 0x005C33C0
void RefreshPartySelectionPortrait(std::uint32_t party_slot)
{
    const auto& buttons = g_party_selection_controller->m_control1->m_control.m_lsButtons;
    if (buttons.empty()) {
        return;
    }
    std::uint32_t index = party_slot - 2;
    if (index >= buttons.size()) {
        index = 0;
    }
    auto* row = static_cast<W8PartySelectionPartySlotRow*>(buttons[index]);
    Controls* panel = row->m_pPanel;
    std::int32_t portrait = g_status.buffers.Char[row->m_row + 2].portrait_index;
    SpriteDraw draw{};
    if ((g_portrait_descriptors[portrait].render_mode == 1 && row->m_row % 2 == 0) ||
        (g_portrait_descriptors[portrait].render_mode == 2 && row->m_row % 2 != 0)) {
        draw.mirror = true;
    }
    BlitPartyPortraitAnimation(portrait, panel->m_bounds.left + row->m_left,
                               panel->m_bounds.top + row->m_top, draw, row->m_row + 2, false);
}

/* Whether the party selector is in its review-existing-character mode; the
   camp screen consults it when deciding if the level-up panel applies. */
// FUNCTION: WIZ8 0x005c3470
bool PartySelectionInReviewMode(void)
{
    return g_party_selection_controller->m_mode == W8_PARTY_SELECT_IMPORT;
}

W8PartySelectionCharacterRow::W8PartySelectionCharacterRow(Controls* panel, std::int32_t top, std::int32_t row)
    : W8TextControl(panel, 0xffffffff, 0, top, 0, 0, CatalogArtwork::Load(0xfb, 0), 0, 1, 2, 1, -1), m_row(row),
      m_character_index(0), m_selection_listener(0)
{
    AddLayoutFlags(0x11);
    m_character_index = g_party_selection_character_collection->first_visible + m_row;
    SetActive(m_character_index <
              static_cast<std::int32_t>(g_party_selection_character_collection->characters.size()));
    Invalidate(false);
}

// FUNCTION: WIZ8 0x005be9b0
void W8PartySelectionCharacterRow::Redraw(bool full_redraw)
{
    if (!m_active || (!full_redraw && !m_dirty)) {
        return;
    }

    W8TextControl::Redraw(full_redraw);
    W8Character* character =
        g_party_selection_character_collection->GetCharacter(m_character_index);
    std::int32_t left = m_pPanel->m_bounds.left + m_left;
    std::int32_t top = m_pPanel->m_bounds.top + m_top;
    CatalogArtwork::Load(0x13, character->portrait_index).draw(GetFrameBufferSurface(), 0, left + 2, top + 2);
    if (character->fInParty) {
        ShadowRect(GetFrameBufferSurface(), {left + 2, top + 2, 0x2d, 0x24});
        g_wiz_text_font_secondary.setShade(6);
    }

    left += 0x36;
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left, top + 4}, character->name);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left, top + 0x0e}, fmt::format("{} {:d} {}", g_localized_strings[0x6b9], character->uiExpLevel,
                    g_localized_strings[g_profession_name_message_ids[character->iProfession]]));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left, top + 0x18}, fmt::format("{} {}",
                             g_localized_strings[g_gender_name_message_rows[character->gender][0]],
                             g_localized_strings[g_race_name_message_ids[character->iRace]]));
    g_wiz_text_font_secondary.setShade(Font::kNormalShade);
}

// FUNCTION: WIZ8 0x005beb90
void W8PartySelectionCharacterRow::AdjustValue(std::int32_t amount)
{
    if (m_active && m_enabled && m_selection_listener) {
        m_selection_listener->AdjustPartyMemberRange(amount);
    }
}

// FUNCTION: WIZ8 0x005beb10
void W8PartySelectionCharacterRow::OnRightButtonUp(std::int32_t event)
{
    if (m_active && m_enabled) {
        SetAlternateTextEnabled(false);
        if (m_selection_listener) {
            m_selection_listener->OpenCampForSelectedMember(m_row);
        }
    }
    W8TextControl::OnRightButtonUp(event);
}

// FUNCTION: WIZ8 0x005beb50
void W8PartySelectionCharacterRow::OnLeftButtonDoubleClick(std::int32_t event)
{
    if (m_active && m_enabled && m_selection_listener) {
        m_selection_listener->SelectPartyMemberRow(m_row);
    }
    W8TextControl::OnLeftButtonDoubleClick(event);
}

void W8PartySelectionCharacterPanel::RefreshVisibleRows()
{
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(ControlCount()); ++index) {
        W8PartySelectionCharacterRow* row =
            static_cast<W8PartySelectionCharacterRow*>(ControlAt(index));
        row->m_character_index = g_party_selection_character_collection->first_visible + row->m_row;
        row->SetActive(row->m_character_index <
                       static_cast<std::int32_t>(g_party_selection_character_collection->characters.size()));
        row->Invalidate(false);
    }
}

void W8PartySelectionCharacterPanel::SynchronizeVisibleSelection()
{
    std::int32_t selection = m_selected_row - g_party_selection_character_collection->first_visible;
    if (selection < 0 || selection > 5) {
        selection = -1;
    }
    m_control.SetSelected(selection);
    m_control.m_selectionListener = this;
    RefreshVisibleRows();
}

// FUNCTION: WIZ8 0x005bebc0
W8PartySelectionCharacterPanel::W8PartySelectionCharacterPanel()
    : Controls(), m_range(0), m_selected_row(0)
{
    CreateRegionSet();
    m_bounds.left = 0x148;
    m_bounds.top = 0x31;

    std::int32_t top = 0;
    for (std::int32_t row = 0; row < 6; ++row) {
        W8PartySelectionCharacterRow* control = new W8PartySelectionCharacterRow(this, top, row);
        control->m_selection_listener = this;
        m_control.AddEntry(control);
        top += 0x2a;
    }

    SynchronizeVisibleSelection();
}

// FUNCTION: WIZ8 0x005bedf0
W8PartySelectionCharacterPanel::~W8PartySelectionCharacterPanel() = default;

// FUNCTION: WIZ8 0x005bee70
void W8PartySelectionCharacterPanel::OnSelectionChanged(W8ControlSelection*, std::int32_t selected)
{
    m_selected_row = selected + g_party_selection_character_collection->first_visible;
    g_party_selection_controller->SetSelection(m_selected_row, false, false);
}

// FUNCTION: WIZ8 0x005beea0
void W8PartySelectionCharacterPanel::OnRangeChanged(W8RangeControl* control)
{
    if (!m_fEnabled) {
        return;
    }
    g_party_selection_character_collection->first_visible = control->m_value;
    m_control.m_selectionListener = 0;
    SynchronizeVisibleSelection();
}

// FUNCTION: WIZ8 0x005bef60
void W8PartySelectionCharacterPanel::SetSelectedRow(std::int32_t selection)
{
    m_selected_row = selection;
    std::int32_t visible = selection - g_party_selection_character_collection->first_visible;
    if (visible < 0 || visible > 5 ||
        static_cast<std::int32_t>(g_party_selection_character_collection->characters.size()) <
            g_party_selection_character_collection->first_visible + 6) {
        std::int32_t maximum =
            static_cast<std::int32_t>(g_party_selection_character_collection->characters.size()) - 6;
        std::int32_t first = selection < maximum ? selection : maximum;
        if (first < 0) {
            first = 0;
        }
        g_party_selection_character_collection->first_visible = first;
        m_range->SetValue(first);
    }

    m_control.m_selectionListener = 0;
    SynchronizeVisibleSelection();
}

// FUNCTION: WIZ8 0x005bf0c0
void W8PartySelectionCharacterPanel::SelectPartyMemberRow(std::int32_t row)
{
    m_control.SetSelected(row);
    g_party_selection_controller->TogglePartyMemberSelection();
}

// FUNCTION: WIZ8 0x005bf050
void W8PartySelectionCharacterPanel::OpenCampForSelectedMember(std::int32_t row)
{
    m_control.SetSelected(row);
    std::int32_t slot;
    for (slot = 0; slot < 6; ++slot) {
        if (g_status.buffers.XChar[slot + 2].fOccupied &&
            &g_status.buffers.Char[slot + 2] == g_party_selection_controller->m_character) {
            break;
        }
    }
    g_pending_screen_state.parameter_2 = slot < 6 ? slot + 2 : -1;
    g_pending_screen_state.parameter_3 = g_party_selection_controller->m_character;
    SetPendingScreenState(W8_SCREEN_CAMP);
}

// FUNCTION: WIZ8 0x005bf0e0
void W8PartySelectionCharacterPanel::AdjustPartyMemberRange(std::int32_t amount)
{
    m_range->AdjustValue(amount);
}

// FUNCTION: WIZ8 0x005bf120
W8PartySelectionPartySlotRow::W8PartySelectionPartySlotRow(Controls* panel, std::int32_t row)
    : W8TextControl(panel, 0xffffffff, (row & 1) * 0x20b + 0x0e, (row / 2) * 0x82 + 0x3a, 0, 0, CatalogArtwork::Load(0x101, 0), -1, 1, 0, 0, -1),
      m_row(row), m_redraw_partner(0)
{
    AddLayoutFlags(0x11);
    UpdateTextBounds(m_left - 8, m_top + 0x51, m_left + 0x61, m_top + 0x60);
}

// FUNCTION: WIZ8 0x005bf280
void W8PartySelectionPartySlotRow::Redraw(bool full_redraw)
{
    if (!m_active || (!full_redraw && !m_dirty)) {
        return;
    }

    std::int32_t left = m_pPanel->m_bounds.left + m_left;
    std::int32_t top = m_pPanel->m_bounds.top + m_top;
    W8Character* character = 0;
    if (!g_status.buffers.XChar[m_row + 2].fOccupied) {
        const std::int32_t right = m_pPanel->m_bounds.left + m_right;
        const std::int32_t bottom = m_pPanel->m_bounds.top + m_bottom;
        const SDL_Rect area{left, top, right - left, bottom - top};
        SDL_FillSurfaceRect(&GetFrameBufferSurface(), &area, 0x8000);
    } else {
        character = &g_status.buffers.Char[m_row + 2];
        std::int32_t portrait = character->portrait_index;
        SpriteDraw draw{};
        if ((g_portrait_descriptors[portrait].render_mode == 1 && !(m_row & 1)) ||
            (g_portrait_descriptors[portrait].render_mode == 2 && (m_row & 1))) {
            draw.mirror = true;
        }
        RenderPartyPortrait(portrait, left, top, draw, 1, m_row + 2);
    }
    m_textBuffer.SetText(character ? std::string_view(character->name) : std::string_view{}, g_wiz_text_font_secondary);
    CatalogArtwork::Load(0x100, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, left - 0x0d, top - 0x0b);
    W8TextControl::Redraw(full_redraw);
    if (m_redraw_partner) {
        m_redraw_partner->Invalidate(false);
    }
}

// FUNCTION: WIZ8 0x005bf410
void W8PartySelectionPartySlotRow::OnRightButtonUp(std::int32_t event)
{
    W8TextControl::OnRightButtonUp(event);
    if (!m_enabled || !m_active) {
        return;
    }
    SetAlternateTextEnabled(false);
    if (m_row == -1) {
        std::int32_t slot;
        for (slot = 0; slot < 6; ++slot) {
            if (g_status.buffers.XChar[slot + 2].fOccupied &&
                &g_status.buffers.Char[slot + 2] == g_party_selection_controller->m_character) {
                break;
            }
        }
        g_pending_screen_state.parameter_2 = slot < 6 ? slot + 2 : -1;
        g_pending_screen_state.parameter_3 = g_party_selection_controller->m_character;
    } else {
        g_pending_screen_state.parameter_2 = m_row + 2;
        g_pending_screen_state.parameter_3 = &g_status.buffers.Char[m_row + 2];
    }
    SetPendingScreenState(W8_SCREEN_CAMP);
}

// FUNCTION: WIZ8 0x005bf4e0
void W8PartySelectionPartySlotRow::OnLeftButtonDoubleClick(std::int32_t event)
{
    W8TextControl::OnLeftButtonDoubleClick(event);
    if (m_enabled && m_active) {
        g_party_selection_controller->SetSelection(m_row, true, false);
        g_party_selection_controller->TogglePartyMemberSelection();
    }
}

// FUNCTION: WIZ8 0x005bf6b0
void W8PartySelectionPartySlotPanel::OnSelectionChanged(W8ControlSelection*, std::int32_t)
{
    g_party_selection_controller->SetSelection(m_control.m_selectedIndex, true, false);
}

// FUNCTION: WIZ8 0x005bf520
W8PartySelectionPartySlotPanel::W8PartySelectionPartySlotPanel() : Controls()
{
    CreateRegionSet();
    for (std::int32_t row = 0; row < 6; ++row) {
        m_control.AddEntry(new W8PartySelectionPartySlotRow(this, row));
    }
    m_control.m_selectionListener = this;
    for (std::int32_t slot = 0; slot < 6; ++slot) {
        W8TextControl* control = m_control.m_lsButtons[slot];
        control->SetEnabled(g_status.buffers.XChar[slot + 2].fOccupied);
    }
    Invalidate(0);
}

// FUNCTION: WIZ8 0x005bf6d0
W8PartySelectionCharacterGridPanel::W8PartySelectionCharacterGridPanel() : Controls()
{
    CreateRegionSet();
    for (std::int32_t index = 0; index < 6; ++index) {
        std::int32_t left = (index & 1) ? 0x21d : 0x52;
        std::int32_t top = (index / 2) * 0x82 + 0x6c;
        W8TextControl* control =
            new W8TextControl(this, 0xffffffff, left, top, 0, 0, CatalogArtwork::Load(0xa7, 0), 0, 2, 1, 4, 3);
        control->m_listener = this;
    }
}

// FUNCTION: WIZ8 0x005bf840
void W8PartySelectionCharacterGridPanel::OnPrimary(W8TextControl* control)
{
    std::int32_t index;
    for (index = 0; index < 6; ++index) {
        if (ControlAt(index) == control) {
            break;
        }
    }
    control->SetAlternateTextEnabled(false);
    g_pending_screen_state.parameter_3 = &g_status.buffers.Char[index + 2];
    g_pending_screen_state.mode = 2;
    SetPendingScreenState(W8_SCREEN_CHARACTER);
}

// FUNCTION: WIZ8 0x005bf7e0
W8PartySelectionCharacterGridPanel::~W8PartySelectionCharacterGridPanel() = default;

// FUNCTION: WIZ8 0x005bf640
W8PartySelectionPartySlotPanel::~W8PartySelectionPartySlotPanel() = default;

/* Draw the selected character's portrait and complete summary card.  The
   seven attribute values and the four right-column derived values come from
   the canonical character record; no parallel presentation snapshot exists. */
// FUNCTION: WIZ8 0x005bf8b0
void W8PartySelectionCharacterSummaryPanel::Redraw()
{
    if (!m_fEnabled || !m_fDirty) {
        return;
    }
    m_fDirty = false;

    CatalogArtwork::Load(0xfc, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x7f, 0xc5);
    CatalogArtwork::Load(0x1b6, 0).drawRect(GetFrameBufferSurface(), SDL_Rect{0, 0, (0x139) - (0x85), (0xc0) - (0x30)}, 0x85, 0x30);
    InvalidateRegion(0x85, 0x30, 0x139, 0xc0, 0);
    if (!m_character) {
        return;
    }

    W8Character* character = m_character;
    CatalogArtwork::Load(0x11, character->portrait_index).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x85, 0x30);
    if (character->fInParty) {
        W8ControlsRect bounds = {0x85, 0x30, 0x139, 0xba};
        W8TextBuffer overlay(&bounds, g_localized_strings[0x6b8], g_options_title_font,
                             g_W8TextBufferAlignBottom | g_W8TextBufferAlignCenter, 4);
        overlay.RenderToTarget(0, false, GetFrameBufferSurface());
    }

    {
        W8ControlsRect name_bounds = {0x84, 0xc8, 0x139, 0xed};
        W8TextBuffer name(
            &name_bounds, fmt::format("{} ({})", character->name_part_2, character->name),
            g_wiz_text_bold_font, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
        name.RenderToTarget(0, false, GetFrameBufferSurface());
    }

    g_wiz_text_font_secondary.setShade(Font::kNormalShade);

    std::string level_text = g_localized_strings[0x6b9];
    std::string profession =
        g_localized_strings[g_profession_name_message_ids[character->iProfession]];
    std::string level_line =
        fmt::format("{} {:d} {}", level_text, character->uiExpLevel, profession);
    std::int32_t width = g_wiz_text_font_secondary.textWidth(level_line);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {(0xbf - width) / 2 + 0x78, 0xef}, fmt::format("{} {:d} {}", level_text, character->uiExpLevel, profession));

    std::string gender = g_localized_strings[g_gender_name_message_rows[character->gender][0]];
    std::string race = g_localized_strings[g_race_name_message_ids[character->iRace]];
    std::string race_line = fmt::format("{} {}", gender, race);
    width = g_wiz_text_font_secondary.textWidth(race_line);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {(0xbf - width) / 2 + 0x7f, 0xfd}, fmt::format("{} {}", gender, race));

    std::string personality = g_localized_strings[g_personality_message_ids[character->personality]];
    width = g_wiz_text_font_secondary.textWidth(personality);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {(0xbf - width) / 2 + 0x82, 0x10b}, personality);

    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x96, 0x127}, g_localized_strings[0x6ba]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xbf, 0x127}, fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_STRENGTH].base));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x96, 0x135}, g_localized_strings[0x6bb]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xbf, 0x135}, fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_INTELLIGENCE].base));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x96, 0x143}, g_localized_strings[0x6bc]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xbf, 0x143}, fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_PIETY].base));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x96, 0x151}, g_localized_strings[0x6bd]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xbf, 0x151}, fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_VITALITY].base));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x96, 0x15f}, g_localized_strings[0x6be]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xbf, 0x15f}, fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_DEXTERITY].base));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x96, 0x16d}, g_localized_strings[0x6bf]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xbf, 0x16d}, fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_SPEED].base));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x96, 0x17b}, g_localized_strings[0x6c0]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xbf, 0x17b}, fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_SENSES].base));

    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xe3, 0x127}, g_localized_strings[0x6c1]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x115, 0x127}, fmt::format("{:d}", character->uiHPMax));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xe3, 0x135}, g_localized_strings[0x6c2]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x115, 0x135}, fmt::format("{:d}", SumCharacterSpellPoints(character)));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xe3, 0x143}, g_localized_strings[0x6c3]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x115, 0x143}, fmt::format("{:d}", character->uiStaminaMax));
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0xe3, 0x151}, g_localized_strings[0x6c4]);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x115, 0x151}, fmt::format("{:d}", character->carrying_capacity / 10));
}



/* Build the three mutually selected difficulty controls and the two
   independent toggles used by the later creation modes.  The entry vector is
   separate: redraw walks it for the repeated option-detail render pass. */
// FUNCTION: WIZ8 0x005c01d0
W8PartySelectionOptionPanel::W8PartySelectionOptionPanel()
    : Controls(0x78, 0x28, 0, 0, CatalogArtwork::Load(0x102, 0), 0), m_options(), m_entries()
{
    CreateRegionSet();

    std::int16_t width;
    std::int16_t height;
    const SDL_Point _catalog_size_2 = CatalogArtwork::Load(0x102, 0).size(0);
        width = _catalog_size_2.x;
        height = _catalog_size_2.y;;
    m_bounds.right = m_bounds.left + static_cast<std::uint16_t>(width);
    m_bounds.bottom = m_bounds.top + static_cast<std::uint16_t>(height);

    std::int32_t top = 0x13;
    for (std::int32_t index = 0; index < 3; ++index) {
        m_options.AddEntry(
            new W8TextControl(this, 0xffffffff, 0x15f, top, 0, 0, CatalogArtwork::Load(0xf1, 0), 4, 6, 5, 7, -1));
        top += 0x16;
    }
    m_options.SetSelected(g_settings.difficulty);

    top += 0x16;
    npc_interact_toggle =
        new W8TextControl(this, 0xffffffff, 0x15b, top, 0, 0, CatalogArtwork::Load(0xf1, 0), 2, 0, 3, 1, -1);
    npc_interact_toggle->AddLayoutFlags(g_W8TextControlLayoutLatchedImage |
                                        g_W8TextControlLayoutToggle);
    if (g_settings.simplified_npc_interaction) {
        npc_interact_toggle->EnableSecondaryState(false);
    }

    m_toggle = new W8TextControl(this, 0xffffffff, 0x15b, 0xd6, 0, 0, CatalogArtwork::Load(0xf1, 0), 2, 0, 3, 1, -1);
    m_toggle->AddLayoutFlags(g_W8TextControlLayoutLatchedImage | g_W8TextControlLayoutToggle);

    const SDL_Point _catalog_size_3 = CatalogArtwork::Load(0x102, 0).size(1);
        m_image_width = _catalog_size_3.x;
        m_image_height = _catalog_size_3.y;;
    m_render_left = m_bounds.left + 0x18 + (0x160 - static_cast<std::uint16_t>(m_image_width)) / 2;
    m_render_top = m_bounds.top + 0x80;
}

// FUNCTION: WIZ8 0x005c0560
void W8PartySelectionOptionPanel::Redraw()
{
    bool redraw = m_fEnabled && m_fDirty;
    Controls::Redraw();
    if (!redraw) {
        return;
    }

    for (std::int32_t index = 0; index < static_cast<std::int32_t>(m_entries.size()); ++index) {
        m_entries[index]->RenderToTarget(0, true, GetFrameBufferSurface());
    }
    if (m_page == W8_PARTY_CREATION_SAVE_NAME) {
        CatalogArtwork::Load(0x102, 0).draw(GetFrameBufferSurface(), 1, m_render_left, m_render_top);
    }
}

/* Give the active string editor first refusal on keyboard events, reject the
   filename characters retail excludes, and report both edit-state and final
   Escape/Return decisions to the controller's decision-listener subobject. */
// FUNCTION: WIZ8 0x005c0e50
std::uint8_t W8PartySelectionInputHandler::HandleInput(const InputEvent* input)
{
    if (!IsTypingEvent(*input)) {
        return 0;
    }

    if (input->key != VK_ESCAPE) {
        if (input->key != VK_RETURN) {
            const char32_t character = TypedCharacter(*input);
            if (character != 0 && character < 0x80 && strchr("\\/:*?\"<>|", std::int32_t(character))) {
                return 1;
            }
            HandleTextInput(input);
            if (m_listener) {
                m_listener->OnToggle(GetTextInputFieldByteLength(0) != 0);
            }
            return 1;
        }
        if (!GetTextInputFieldByteLength(0)) {
            return 1;
        }
    }

    SetTargetCursor(W8_CURSOR_NONE);
    if (m_listener) {
        m_listener->OnDecision(this, input->key == VK_ESCAPE);
    }
    return 1;
}

/* Replace the option panel's complete detail composition. Mode zero owns the
   eight creation-summary buffers, mode one shows its single explanatory
   paragraph, and mode two owns the name prompt plus the real string-input
   session. The panel's three difficulty controls are interactive only in
   mode zero. */
// FUNCTION: WIZ8 0x005c05f0
void W8PartySelectionOptionPanel::SetOptionPanelMode(W8PartyCreationPage page)
{
    m_page = page;
    m_entries.clear();
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(ControlCount()); ++index) {
        ControlAt(index)->SetActive(page == W8_PARTY_CREATION_OPTIONS);
    }

    W8ControlsRect bounds = {m_bounds.left + 0x22, m_bounds.top + 0x12, m_bounds.left + 0x16e,
                             m_bounds.top + 0x171};

    if (page == W8_PARTY_CREATION_OPTIONS) {
        m_entries.emplace_back(new W8TextBuffer(&bounds, g_localized_strings[0x7f7], g_options_detail_font,
                                       g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));

        bounds.right = m_bounds.left + 0x155;
        m_entries.emplace_back(new W8TextBuffer(&bounds, g_localized_strings[0x7f8], g_options_detail_font,
                                       g_W8TextBufferAlignRight | g_W8TextBufferAlignTop, 4));
        bounds.top += 0x16;
        m_entries.emplace_back(new W8TextBuffer(&bounds, g_localized_strings[0x7f9], g_options_detail_font,
                                       g_W8TextBufferAlignRight | g_W8TextBufferAlignTop, 4));
        bounds.top += 0x16;
        m_entries.emplace_back(new W8TextBuffer(&bounds, g_localized_strings[0x7fa], g_options_detail_font,
                                       g_W8TextBufferAlignRight | g_W8TextBufferAlignTop, 4));

        bounds.right = m_bounds.left + 0x16e;
        bounds.top += 0x2c;
        m_entries.emplace_back(new W8TextBuffer(&bounds, g_localized_strings[0x80b], g_options_detail_font,
                                       g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));

        bounds.top += 0x2c;
        W8TextBuffer* text =
            new W8TextBuffer(&bounds, g_localized_strings[0x6cd], g_options_detail_font,
                             g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop, 4);
        text->SetLineHeight(0x16);
        m_entries.emplace_back(text);

        bounds.top += 0x42;
        m_entries.emplace_back(new W8TextBuffer(&bounds, g_localized_strings[0x6ce], g_options_detail_font,
                                       g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));

        bounds.top += 0x2c;
        text = new W8TextBuffer(&bounds, g_localized_strings[0x6cf], g_options_detail_font,
                                g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop, 4);
        text->SetLineHeight(0x16);
        m_entries.emplace_back(text);
        return;
    }

    if (page == W8_PARTY_CREATION_NOTICE) {
        bounds.top += 0x2c;
        W8TextBuffer* text =
            new W8TextBuffer(&bounds, g_localized_strings[0x6d0], g_options_detail_font,
                             g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop, 4);
        text->SetLineHeight(0x16);
        m_entries.emplace_back(text);
        return;
    }

    if (page != W8_PARTY_CREATION_SAVE_NAME) {
        return;
    }

    bounds.left = m_bounds.left + 0x2c;
    bounds.right = m_bounds.left + 0x164;
    W8TextBuffer* text = new W8TextBuffer(&bounds, g_localized_strings[0x6d1], g_options_detail_font,
                                          g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop, 4);
    text->SetLineHeight(0x16);
    m_entries.emplace_back(text);

    W8PartySelectionController* controller = g_party_selection_controller.get();
    if (controller->m_input_handler) {
        return;
    }
    W8PartySelectionInputHandler* input_handler = new W8PartySelectionInputHandler;
    InitTextInputModeWithScheme(1);
    AddTextInputField(m_render_left + 2, m_render_top + 2,
                      static_cast<std::uint16_t>(m_image_width) - 4,
                      static_cast<std::uint16_t>(m_image_height) - 4, 0x7f, "", 0x28, 0x0f, 1);
    SetActiveField(0);
    controller->m_input_handler.reset(input_handler);
    input_handler->m_listener = controller;
    input_handler->m_listener->OnToggle(0);
}

/* Construct the complete party-selection panel graph and attach every callback before
   the first mode transition.  The shared range is rebound by SetMode to either
   the character rows or import list; the six portrait rows invalidate their
   paired advance controls after drawing. */
// FUNCTION: WIZ8 0x005c0f30
void W8PartySelectionController::Setup()
{
    m_range = std::make_unique<W8RangeControl>(0x1e9, 0x31, 0x1fb, 0x12b);
    m_character_panel = std::make_unique<W8PartySelectionCharacterPanel>();
    m_control0 = std::make_unique<W8PartySelectionCharacterGridPanel>();
    m_control1 = std::make_unique<W8PartySelectionPartySlotPanel>();
    m_control2 = std::make_unique<W8PartySelectionCharacterSummaryPanel>();
    m_control3 = std::make_unique<W8PartySelectionOptionPanel>();

    for (std::int32_t slot = 0; slot < 6; ++slot) {
        W8PartySelectionPartySlotRow* row =
            static_cast<W8PartySelectionPartySlotRow*>(m_control1->m_control.m_lsButtons[slot]);
        row->m_redraw_partner = m_control0->ControlAt(slot);
    }

    m_left_panel = std::make_unique<Controls>(0x145, 0x137, 0, 0, std::nullopt, -1);
    m_left_panel->CreateRegionSet();
    m_create_button =
        new W8TextControl(m_left_panel.get(), 0xffffffff, 0, 0, 0, 0, CatalogArtwork::Load(0xfe, 0), 0, 2, 1, 2, 3);
    m_create_button->m_textBuffer.SetText(g_localized_strings[0x6c5], g_wiz_text_bold_font);
    m_create_button->m_listener = this;

    m_add_remove_button =
        new W8TextControl(m_left_panel.get(), 0xffffffff, 0, 0x1a, 0, 0, CatalogArtwork::Load(0xfe, 0), 0, 2, 1, 2, 3);
    m_add_remove_button->m_listener = this;

    m_delete_button =
        new W8TextControl(m_left_panel.get(), 0xffffffff, 0, 0x34, 0, 0, CatalogArtwork::Load(0xfe, 0), 0, 2, 1, 2, 3);
    m_delete_button->m_textBuffer.SetText(g_localized_strings[0x6c8], g_wiz_text_bold_font);
    m_delete_button->m_listener = this;

    m_review_button =
        new W8TextControl(m_left_panel.get(), 0xffffffff, 0, 0x4e, 0, 0, CatalogArtwork::Load(0xfe, 0), 0, 2, 1, 2, 3);
    m_review_button->m_textBuffer.SetText(g_localized_strings[0x6c9], g_wiz_text_bold_font);
    m_review_button->m_listener = this;

    m_import_panel = std::make_unique<Controls>(0x148, 0x31, 0, 0, std::nullopt, -1);
    m_import_panel->CreateRegionSet();
    m_list = new W8PartySelectionListControl(m_import_panel.get(), 0xffffffff, 0, 0, 0x9d, 0xfa);
    m_list->m_listener = this;

    m_bottom_panel = std::make_unique<Controls>(0x84, 0x1b5, 0, 0, std::nullopt, -1);
    m_bottom_panel->CreateRegionSet();
    m_back_button =
        new W8TextControl(m_bottom_panel.get(), 0xffffffff, 0x14c, 0, 0, 0, CatalogArtwork::Load(0x106, 0), 4, 6, 5, 6, 7);
    m_back_button->EnableRegionHelp(0x6ca);
    m_back_button->m_listener = this;

    m_confirm_button =
        new W8TextControl(m_bottom_panel.get(), 0xffffffff, 0x120, 0, 0, 0, CatalogArtwork::Load(0x106, 0), 0, 2, 1, 2, 3);
    m_confirm_button->EnableRegionHelp(0x6cb);
    m_confirm_button->m_listener = this;

    m_reset_button = new W8TextControl(m_bottom_panel.get(), 0xffffffff, 0xf4, 0, 0, 0, CatalogArtwork::Load(0x106, 0), 0x18, 0x1a, 0x19, 0x1c, 0x1b);
    m_reset_button->AddLayoutFlags(g_W8TextControlLayoutLatchedImage | g_W8TextControlLayoutToggle);
    m_reset_button->EnableRegionHelp(0x6cc);
    m_reset_button->m_listener = this;

    W8ControlsRect bounds = {0x7b, 6, 0x205, 0x23};
    m_text_buffer = std::make_unique<W8TextBuffer>();
    m_text_buffer->SetLayoutBounds(&bounds, true, true);

    SetMode(W8_PARTY_SELECT_CHARACTERS);
    SetSelection(0, false, true);
}

// FUNCTION: WIZ8 0x005c0480
W8PartySelectionOptionPanel::~W8PartySelectionOptionPanel()
{
    m_entries.clear();
}

/* Switch the party-builder as a complete screen mode.  Mode zero presents the
   loose-character collection, mode one presents the six active slots and the
   import list, and modes two through four hand the screen to the option panel.
   Every panel is invalidated before its enable/region state changes so the
   next frame redraws the new composition. */
// FUNCTION: WIZ8 0x005c2010
void W8PartySelectionController::SetMode(W8PartySelectionMode mode)
{
    m_mode = mode;
    InvalidatePartySelectionComposition();
    m_bottom_panel->SetEnabled(true);
    m_bottom_panel->EnableRegionSet(true);
    m_control1->SetEnabled(true);

    std::string label;
    switch (m_mode) {
    case W8_PARTY_SELECT_CHARACTERS: {
        m_range->SetEnabled(true);
        m_range->EnableRegionSet(true);
        m_left_panel->SetEnabled(true);
        m_left_panel->EnableRegionSet(true);
        m_character_panel->SetEnabled(true);
        m_character_panel->EnableRegionSet(true);

        m_character_panel->m_range = m_range.get();
        m_range->m_listener = m_character_panel.get();
        std::int32_t maximum =
            static_cast<std::int32_t>(g_party_selection_character_collection->characters.size()) - 6;
        if (maximum < 0) {
            maximum = 0;
        }
        m_range->SetRange(0, maximum);
        m_range->SetRangeEnabled(maximum > 0);
        m_range->Invalidate(0);

        m_character_panel->m_control.m_selectionListener = 0;
        std::int32_t selected = m_character_panel->m_selected_row -
                       g_party_selection_character_collection->first_visible;
        if (selected < 0 || selected > 5) {
            selected = -1;
        }
        m_character_panel->m_control.SetSelected(selected);
        m_character_panel->m_control.m_selectionListener = m_character_panel.get();
        for (std::int32_t index = 0; index < static_cast<std::int32_t>(m_character_panel->ControlCount());
             ++index) {
            W8PartySelectionCharacterRow* row =
                static_cast<W8PartySelectionCharacterRow*>(m_character_panel->ControlAt(index));
            row->m_character_index =
                g_party_selection_character_collection->first_visible + row->m_row;
            row->SetActive(
                row->m_character_index <
                static_cast<std::int32_t>(g_party_selection_character_collection->characters.size()));
            row->Invalidate(false);
        }

        m_control0->SetEnabled(false);
        m_control0->EnableRegionSet(false);
        m_control1->EnableRegionSet(true);
        m_import_panel->SetEnabled(false);
        m_import_panel->EnableRegionSet(false);
        m_control2->SetEnabled(true);
        m_control3->SetEnabled(false);
        m_control3->EnableRegionSet(false);
        m_confirm_button->SetEnabled(CountActiveCharacters() != 0);
        label = g_localized_strings[0x6b4];
        break;
    }
    case W8_PARTY_SELECT_IMPORT: {
        if (g_party_selection_character_collection->names.empty()) {
            for (const auto& entry : wiz8::list_directory("Saves\\Import", "*.*")) {
                const auto status = wiz8::file_status("Saves\\Import\\" + entry);
                if (status && status->info.type == SDL_PATHTYPE_FILE) {
                    size_t length = entry.size() + 1;
                    char* name = new char[length];
                    memcpy(name, entry.c_str(), length);
                    g_party_selection_character_collection->names.push_back(name);
                }
                    }
        }

        m_range->SetEnabled(true);
        m_range->EnableRegionSet(true);
        m_left_panel->SetEnabled(true);
        m_left_panel->EnableRegionSet(true);
        m_create_button->SetActive(false);
        m_add_remove_button->SetActive(false);
        m_delete_button->SetActive(false);
        m_character_panel->SetEnabled(false);
        m_character_panel->EnableRegionSet(false);
        m_control0->EnableRegionSet(true);
        m_control0->SetEnabled(true);
        for (std::int32_t slot = 0; slot < 6; ++slot) {
            bool occupied = g_status.buffers.XChar[slot + 2].fOccupied;
            m_control0->ControlAt(slot)->SetActive(m_mode == W8_PARTY_SELECT_IMPORT && occupied &&
                                                   IsCharacterReadyToAdvance(slot + 2));
        }
        m_control0->Invalidate(0);
        m_control1->EnableRegionSet(true);
        m_import_panel->SetEnabled(true);
        m_import_panel->EnableRegionSet(true);

        m_range->m_listener = m_list;
        std::int32_t maximum = static_cast<std::int32_t>(g_party_selection_character_collection->names.size()) -
                      m_list->m_visible_rows;
        if (maximum < 0) {
            maximum = 0;
        }
        m_range->SetRange(0, maximum);
        m_range->SetValue(m_list->m_selection);
        m_range->SetRangeEnabled(maximum > 0);
        m_range->Invalidate(0);

        m_control2->SetEnabled(true);
        m_control3->SetEnabled(false);
        m_control3->EnableRegionSet(false);
        label = g_localized_strings[0x6b5];
        break;
    }
    case W8_PARTY_SELECT_OPTIONS:
        m_range->SetEnabled(false);
        m_range->EnableRegionSet(false);
        m_left_panel->SetEnabled(false);
        m_left_panel->EnableRegionSet(false);
        m_control0->SetEnabled(false);
        m_control0->EnableRegionSet(false);
        m_control1->EnableRegionSet(false);
        m_character_panel->SetEnabled(false);
        m_character_panel->EnableRegionSet(false);
        m_import_panel->SetEnabled(false);
        m_import_panel->EnableRegionSet(false);
        m_control2->SetEnabled(false);
        m_reset_button->SetActive(false);
        m_control3->SetEnabled(true);
        m_control3->EnableRegionSet(true);
        m_control3->SetOptionPanelMode(W8_PARTY_CREATION_OPTIONS);
        label = g_localized_strings[0x6b6];
        break;
    case W8_PARTY_SELECT_CREATION_NOTICE:
        m_reset_button->SetActive(false);
        m_control3->SetOptionPanelMode(W8_PARTY_CREATION_NOTICE);
        label = g_localized_strings[0x6b7];
        break;
    case W8_PARTY_SELECT_SAVE_NAME:
        m_reset_button->SetActive(false);
        m_control3->SetOptionPanelMode(W8_PARTY_CREATION_SAVE_NAME);
        label = g_localized_strings[0x6b7];
        break;
    default:
        return;
    }
    m_text_buffer->SetText(label, g_wiz_text_bold_font);
}

// FUNCTION: WIZ8 0x005c1680
void W8PartySelectionController::SetSelection(std::int32_t selection, bool party_slot, bool refresh_other)
{
    if (!party_slot) {
        if (m_mode == W8_PARTY_SELECT_CHARACTERS) {
            m_character = g_party_selection_character_collection->GetCharacter(selection);
            std::int32_t selected_slot = -1;
            if (m_character && m_character->fInParty) {
                for (std::int32_t slot = 0; slot < 6; ++slot) {
                    if (g_status.buffers.XChar[slot + 2].fOccupied &&
                        &g_status.buffers.Char[slot + 2] == m_character) {
                        selected_slot = slot;
                        break;
                    }
                }
            }
            m_control1->m_control.m_selectionListener = 0;
            m_control1->m_control.SetSelected(selected_slot);
            m_control1->m_control.m_selectionListener = m_control1.get();
            if (refresh_other) {
                m_character_panel->SetSelectedRow(selection);
            }
        }
    } else {
        m_character = &g_status.buffers.Char[selection + 2];
        if (!m_character->fInParty) {
            m_character = 0;
            selection = -1;
        }
        if (m_mode == W8_PARTY_SELECT_CHARACTERS) {
            std::int32_t character_index = -1;
            if (selection >= 0 && g_status.buffers.XChar[selection + 2].fOccupied) {
                for (std::int32_t index = 0;
                     index <
                     static_cast<std::int32_t>(g_party_selection_character_collection->characters.size());
                     ++index) {
                    if (g_party_selection_character_collection->GetCharacter(index) ==
                        m_character) {
                        character_index = index;
                        break;
                    }
                }
            }
            m_character_panel->SetSelectedRow(character_index);
        }
        if (refresh_other) {
            m_control1->m_control.m_selectionListener = 0;
            m_control1->m_control.SetSelected(selection);
            m_control1->m_control.m_selectionListener = m_control1.get();
        }
    }

    m_control2->m_character = m_character;
    m_control2->Invalidate(0);
    std::string text = g_localized_strings[((!m_character || !m_character->fInParty) ? 0x1b18 : 0x1b1c) / 4];
    m_add_remove_button->m_textBuffer.SetText(text, g_wiz_text_font_secondary);
    bool have_character = m_character != 0;
    m_add_remove_button->SetEnabled(have_character);
    m_add_remove_button->Invalidate(false);
    m_delete_button->SetEnabled(have_character);
    m_delete_button->Invalidate(false);
    m_review_button->SetEnabled(have_character);
    m_review_button->Invalidate(false);
    if (m_character && !m_character->fInParty && FindFreePartySlot(2, 8) == 0xffffffff) {
        m_add_remove_button->SetEnabled(false);
    }
}

/* Dispatch the seven visible party-builder actions.  This is the screen's
   product decision layer: it owns back/finish behavior, edit/create/delete,
   the option-mode progression, and converting an active party back into loose
   character records before an imported party is selected. */
// FUNCTION: WIZ8 0x005c1920
void W8PartySelectionController::OnPrimary(W8TextControl* control)
{
    if (control == m_back_button) {
        if (m_input_handler) {
            OnDecision(m_input_handler.get(), 1);
            return;
        }
        switch (m_mode) {
        case W8_PARTY_SELECT_CHARACTERS:
        case W8_PARTY_SELECT_IMPORT:
            if (CountActiveCharacters() != 0) {
                OpenNotification(g_localized_strings[0x6d3], true, W8_PARTY_CONFIRM_LEAVE);
                return;
            }
            RequestScreenTransition();
            return;
        case W8_PARTY_SELECT_OPTIONS:
        case W8_PARTY_SELECT_CREATION_NOTICE:
        case W8_PARTY_SELECT_SAVE_NAME:
            SetMode(m_previous_mode);
            SetSelection(0, m_previous_mode == W8_PARTY_SELECT_IMPORT, true);
            return;
        default:
            return;
        }
    }

    if (control == m_add_remove_button) {
        TogglePartyMemberSelection();
        return;
    }
    if (control == m_delete_button) {
        OpenNotification(fmt::format("{} {} {}?", g_localized_strings[0x6d4], m_character->name,
                                     g_localized_strings[0x6d5]),
                         true, W8_PARTY_CONFIRM_DELETE_CHARACTER);
        return;
    }
    if (control == m_review_button) {
        std::int32_t slot;
        for (slot = 0; slot < 6; ++slot) {
            if (g_status.buffers.XChar[slot + 2].fOccupied &&
                &g_status.buffers.Char[slot + 2] == m_character) {
                break;
            }
        }
        g_pending_screen_state.parameter_2 = slot < 6 ? slot + 2 : -1;
        g_pending_screen_state.parameter_3 = m_character;
        SetPendingScreenState(W8_SCREEN_CAMP);
        return;
    }
    if (control == m_create_button) {
        g_pending_screen_state.mode = 0;
        g_pending_screen_state.parameter_3 = 0;
        SetPendingScreenState(W8_SCREEN_CHARACTER);
        return;
    }

    if (control == m_confirm_button) {
        switch (m_mode) {
        case W8_PARTY_SELECT_CHARACTERS:
            if (static_cast<std::uint32_t>(CountActiveCharacters()) < 6) {
                OpenNotification(fmt::sprintf(g_localized_strings[0x6d9], CountActiveCharacters()),
                                 true, W8_PARTY_CONFIRM_PROCEED_TO_OPTIONS);
                return;
            }
            [[fallthrough]];
        case W8_PARTY_SELECT_IMPORT:
            m_previous_mode = m_mode;
            SetSelection(-1, true, true);
            SetMode(W8_PARTY_SELECT_OPTIONS);
            return;
        case W8_PARTY_SELECT_OPTIONS:
            g_settings.difficulty =
                static_cast<W8Difficulty>(m_control3->m_options.m_selectedIndex);
            g_settings.simplified_npc_interaction = static_cast<std::uint8_t>(
                m_control3->npc_interact_toggle->m_stateFlags & g_W8TextControlStateSecondary);
            if ((m_control3->m_toggle->m_stateFlags & g_W8TextControlStateSecondary) != 0) {
                SetMode(W8_PARTY_SELECT_CREATION_NOTICE);
            } else {
                RunNewGameOpeningSequence(true, {});
            }
            return;
        case W8_PARTY_SELECT_CREATION_NOTICE:
            SetMode(W8_PARTY_SELECT_SAVE_NAME);
            return;
        case W8_PARTY_SELECT_SAVE_NAME:
            OnDecision(m_input_handler.get(), 0);
            return;
        default:
            return;
        }
    }

    if (control != m_reset_button) {
        return;
    }
    if ((m_reset_button->m_stateFlags & g_W8TextControlStateSecondary) == 0) {
        ResetForNewGame();
        SetMode(W8_PARTY_SELECT_CHARACTERS);
        SetSelection(0, false, true);
        return;
    }
    if (CountActiveCharacters() != 0) {
        OpenNotification(g_localized_strings[0x6d6], true, W8_PARTY_CONFIRM_IMPORT);
        return;
    }

    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    for (std::int32_t index = 0; index < static_cast<std::int32_t>(collection->characters.size()); ++index) {
        W8Character* previous = collection->GetCharacter(index);
        if (!previous->fInParty) {
            continue;
        }

        collection->DetachFromParty(index);
    }
    SetMode(W8_PARTY_SELECT_IMPORT);
    LoadImportedPartyFile(m_list->m_selection);
}

// FUNCTION: WIZ8 0x005c1d60
void W8PartySelectionController::OnSelectionChanged(W8PartySelectionListControl*, std::int32_t selection)
{
    LoadImportedPartyFile(selection);
}

// FUNCTION: WIZ8 0x005c1d70
void W8PartySelectionController::OnDecision(W8PartySelectionInputHandler*, bool accepted)
{
    if (!accepted) {
        std::string slot_name;
        slot_name = GetTextFromField(0);
        if (SaveSlotFileExists(slot_name)) {
            OpenNotification(g_localized_strings[0x829], true, W8_PARTY_CONFIRM_START_WITH_SAVE_NAME);
            return;
        }
        m_input_handler.reset();
        RunNewGameOpeningSequence(true, slot_name);
    } else {
        m_input_handler.reset();
        switch (m_mode) {
        case W8_PARTY_SELECT_CHARACTERS:
        case W8_PARTY_SELECT_IMPORT:
            if (CountActiveCharacters() == 0) {
                RequestScreenTransition();
                return;
            }
            OpenNotification(g_localized_strings[0x6d3], true, W8_PARTY_CONFIRM_LEAVE);
            return;
        case W8_PARTY_SELECT_OPTIONS:
        case W8_PARTY_SELECT_CREATION_NOTICE:
        case W8_PARTY_SELECT_SAVE_NAME:
            SetMode(m_previous_mode);
            SetSelection(0, m_previous_mode == W8_PARTY_SELECT_IMPORT, true);
            return;
        }
    }
}

// FUNCTION: WIZ8 0x005c1ea0
void W8PartySelectionController::OnToggle(std::int32_t value)
{
    if (static_cast<char>(m_confirm_button->m_enabled) != static_cast<char>(value)) {
        m_confirm_button->SetEnabled(static_cast<std::uint8_t>(value));
        m_confirm_button->Invalidate(false);
    }
}

// FUNCTION: WIZ8 0x005c1ed0
void W8PartySelectionController::InvalidatePartySelectionComposition()
{
    m_redraw_backdrop = true;
    m_range->Invalidate(0);
    m_control2->Invalidate(0);
    m_control3->Invalidate(0);
    m_character_panel->Invalidate(0);
    m_left_panel->Invalidate(0);
    m_control0->Invalidate(0);
    m_control1->Invalidate(0);
    m_import_panel->Invalidate(0);
    m_bottom_panel->Invalidate(0);
    m_text_buffer->SetGeometryDirty();
}

/* Draw the party-selection composition in owner order.  The one-shot backdrop is
   refreshed only after a mode/dialog change; panel redraws and modal overlay
   dispatch still run every frame. */
// FUNCTION: WIZ8 0x005c1f40
void W8PartySelectionController::DrawPartySelectionComposition()
{
    if (m_redraw_backdrop) {
        CatalogArtwork::Load(0xfa, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0);
        if (m_mode == W8_PARTY_SELECT_IMPORT) {
            CatalogArtwork::Load(0x103, 0).draw(GetFrameBufferSurface(), 0, 0x140, 0x12e);
        }
        m_redraw_backdrop = false;
    }

    m_range->Redraw();
    m_control2->Redraw();
    m_control3->Redraw();
    m_character_panel->Redraw();
    m_left_panel->Redraw();
    m_control1->Redraw();
    m_control0->Redraw();
    m_import_panel->Redraw();
    m_bottom_panel->Redraw();
    m_text_buffer->RenderToTarget(0, false, GetFrameBufferSurface());
    if (m_dialog) {
        m_dialog->Draw();
    }
    if (m_input_handler) {
        RenderActiveTextField();
    }
}

/* Install the party-selection confirmation/notification dialog and retain the value
   consumed by ApplyPartySelectionConfirmation after the modal closes. */
// FUNCTION: WIZ8 0x005c25e0
void W8PartySelectionController::OpenNotification(std::string_view message, bool allow_cancel,
                                                  W8PartyConfirmationAction action)
{
    m_confirmation_action = action;
    m_dialog = std::make_unique<W8MessageDialogBase>();
    m_dialog->SetOrigin(0xf0, 0xbe);
    m_dialog->SetExtent(0xa0, 100);
    m_dialog->SetBackground("Data\\Dialogs\\DialogBackground.sti", 0);
    m_dialog->SetClientExtent(0xfa, 200);
    m_dialog->SetMessage(message, 1, 0x32, true, allow_cancel, true, true, 0,
                         0x15e);
    CaptureInputSink();
}

/* Apply the result of a party-selection confirmation dialog.  The dialog value selects
   delete, save-slot creation, option entry, imported-party replacement, or
   leaving the screen; cancellation only restores the mode-4 toggle. */
// FUNCTION: WIZ8 0x005c26c0
void W8PartySelectionController::ApplyPartySelectionConfirmation(W8PartyConfirmationAction,
                                                                 bool accepted)
{
    if (!accepted) {
        if (m_confirmation_action == W8_PARTY_CONFIRM_IMPORT) {
            m_reset_button->DisableSecondaryState(false);
        }
        return;
    }

    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    switch (m_confirmation_action) {
    case W8_PARTY_CONFIRM_DELETE_CHARACTER: {
        std::int32_t selected = m_character_panel->m_selected_row;
        W8Character* character = collection->GetCharacter(selected);
        if (character->fInParty) {
            collection->DetachFromParty(selected);
            character = collection->GetCharacter(selected);
        }
        std::string path;
        path = BuildCharacterPath(character->name, -1);
        wiz8::remove_file(path);
        collection->DeleteAt(selected);

        m_character_panel->m_range = m_range.get();
        m_range->m_listener = m_character_panel.get();
        std::int32_t maximum = static_cast<std::int32_t>(collection->characters.size()) - 6;
        if (maximum < 0) {
            maximum = 0;
        }
        m_range->SetRange(0, maximum);
        m_range->SetRangeEnabled(maximum > 0);
        m_range->Invalidate(0);
        if (selected > 0 || collection->characters.empty()) {
            --selected;
        }
        SetSelection(selected, false, true);
        return;
    }
    case W8_PARTY_CONFIRM_START_WITH_SAVE_NAME: {
        std::string slot_name;
        slot_name = GetTextFromField(0);
        m_input_handler.reset();
        RunNewGameOpeningSequence(true, slot_name);
        return;
    }
    case W8_PARTY_CONFIRM_PROCEED_TO_OPTIONS:
        m_previous_mode = m_mode;
        SetSelection(-1, true, true);
        SetMode(W8_PARTY_SELECT_OPTIONS);
        return;
    case W8_PARTY_CONFIRM_IMPORT: {
        for (std::int32_t index = 0; index < static_cast<std::int32_t>(collection->characters.size()); ++index) {
            W8Character* previous = collection->GetCharacter(index);
            if (!previous->fInParty) {
                continue;
            }
            collection->DetachFromParty(index);
        }
        SetMode(W8_PARTY_SELECT_IMPORT);
        LoadImportedPartyFile(m_list->m_selection);
        return;
    }
    case W8_PARTY_CONFIRM_LEAVE:
        RequestScreenTransition();
        return;
    default:
        break;
    }
}

/* Load the selected imported-party file, report its two failure classes, and
   refresh every control whose state depends on the resulting six party slots. */
// FUNCTION: WIZ8 0x005c2c60
void W8PartySelectionController::LoadImportedPartyFile(std::int32_t selection)
{
    ResetForNewGame();
    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    if (selection >= 0 && selection < static_cast<std::int32_t>(collection->names.size())) {
        std::string path;
        path = fmt::format("{}\\{}", "Saves\\Import", collection->names[selection]);
        std::int32_t result = ImportWizardry7Party(path);
        if (result != 0) {
            ResetForNewGame();
            OpenNotification(g_localized_strings[(result == 2 ? 0x1b60 : 0x1b5c) / 4], false,
                             W8_PARTY_CONFIRM_NONE);
        }
    }

    m_confirm_button->SetEnabled(CountActiveCharacters() != 0);
    m_confirm_button->Invalidate(false);
    std::int32_t slot;
    for (slot = 0; slot < 6; ++slot) {
        bool occupied = g_status.buffers.XChar[slot + 2].fOccupied;
        m_control1->m_control.m_lsButtons[slot]->SetEnabled(occupied);
    }
    m_control1->Invalidate(0);
    for (slot = 0; slot < 6; ++slot) {
        bool occupied = g_status.buffers.XChar[slot + 2].fOccupied;
        m_control0->ControlAt(slot)->SetActive(m_mode == W8_PARTY_SELECT_IMPORT && occupied &&
                                               IsCharacterReadyToAdvance(slot + 2));
    }
    m_control0->Invalidate(0);
    SetSelection(0, true, true);
}

/* Toggle the selected loose character into the active party, or detach an
   active member back into an owned loose record.  The selection then follows
   the nearest remaining active slot and both six-row panels are refreshed. */
// FUNCTION: WIZ8 0x005c2970
void W8PartySelectionController::TogglePartyMemberSelection()
{
    std::int32_t selected = m_character_panel->m_selected_row;
    if (selected == -1) {
        return;
    }

    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    W8Character* character = collection->GetCharacter(selected);
    if (!character->fInParty) {
        std::int32_t slot = AddCharacterToParty(character, -1);
        if (slot != -1) {
            delete character;
            collection->characters[selected] = &g_status.buffers.Char[slot];
        }
        if (selected < static_cast<std::int32_t>(collection->characters.size()) - 1) {
            ++selected;
        }
        SetSelection(selected, false, true);
    } else {
        std::int32_t previous_slot = collection->FindPartySlot(selected);
        collection->DetachFromParty(selected);

        std::int32_t next_slot = previous_slot + 1;
        bool found = false;
        while (next_slot != previous_slot) {
            if (next_slot > 5) {
                next_slot = 0;
                if (previous_slot == 0) {
                    break;
                }
            }
            if (g_status.buffers.XChar[next_slot + 2].fOccupied) {
                found = true;
                break;
            }
            ++next_slot;
        }
        if (found) {
            SetSelection(next_slot, true, true);
        } else {
            SetSelection(selected, false, false);
        }
    }

    for (std::int32_t slot = 0; slot < 6; ++slot) {
        bool occupied = g_status.buffers.XChar[slot + 2].fOccupied;
        m_control1->m_control.m_lsButtons[slot]->SetEnabled(occupied);
    }
    m_control1->Invalidate(0);
    m_character_panel->Invalidate(0);
    m_confirm_button->SetEnabled(CountActiveCharacters() != 0);
    m_confirm_button->Invalidate(false);
}

/* Enter the party-selection party builder.  The persistent collection is rebuilt from
   the six occupied player slots whenever this is a fresh entry or a return
   from state 3 that is not preserving mode 1.  Loose CHR files are then merged
   and ordered before the controller presents them. */
// FUNCTION: WIZ8 0x005c2de0
bool PartySelectionScreenEnter(void)
{
    SetViewport(0, 0, 0x280, 0x1e0);
    SetPrimarySurfaceTextureHint2Enabled(false);
    ResetRegions();
    UpdateHeldItemCursor();
    g_wiz_text_font_secondary.resetPalette();
    g_wiz_text_bold_font.resetPalette();

    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    if (!collection) {
        ResetForNewGame();
        g_party_selection_character_collection = std::make_unique<W8PartySelectionCharacterCollection>();
        collection = g_party_selection_character_collection.get();

        collection->ReloadCharacters();

        g_party_selection_controller = std::make_unique<W8PartySelectionController>();
        g_party_selection_controller->Setup();
    } else {
        if (g_previous_screen_id == W8_SCREEN_CHARACTER &&
            g_party_selection_controller->m_mode != W8_PARTY_SELECT_IMPORT) {
            collection->ReloadCharacters();
            g_party_selection_controller->SetSelection(0, false, true);
        }
        g_party_selection_controller->SetMode(g_party_selection_controller->m_mode);
    }
    StartMusicResource("MainMenu.MPL", 1, 1);
    return true;
}

/* The state persists its controller and imported-character collection while
   another screen is temporarily stacked over it.  A leaving tick destroys
   both; an ordinary tick only performs the common display/region reset. */
// FUNCTION: WIZ8 0x005c30b0
bool PartySelectionScreenLeave(std::int32_t leaving)
{
    if (leaving) {
        g_party_selection_character_collection.reset();
        g_party_selection_controller.reset();
    }
    NoOp();
    ResetRegions();
    return true;
}

/* Run modal close-out, cursor/region dispatch and the party-selection keyboard layer.
   Region handling gets first refusal.  The optional party-selection input handler gets
   second refusal, after which the screen owns Return, Escape, left/right row
   movement and Delete. */
// FUNCTION: WIZ8 0x005c3120
void PartySelectionScreenFrame(void)
{
    InputEvent input;
    SDL_Point point;

    if (g_dev_mode) {
        RequestExitScreen();
    }
    SGPMouseGetPos(&point);
    W8PartySelectionController* controller = g_party_selection_controller.get();
    if (controller->m_dialog) {
        controller->m_dialog->ProcessInput();
        if (!controller->m_dialog->is_open) {
            bool result = controller->m_dialog->accepted;
            controller->m_dialog.reset();
            ReleaseInputSinkCapture();
            controller->InvalidatePartySelectionComposition();
            controller->ApplyPartySelectionConfirmation(controller->m_confirmation_action, result);
        }
    }
    if (controller->m_input_handler) {
        UpdateRegionMousePosition(point.x, point.y);
    }
    UpdateRegionMousePosition(point.x, point.y);
    while (DequeueEvent(&input) == 1) {
        if (!DispatchRegionInput(&input) &&
            (!controller->m_input_handler || !controller->m_input_handler->HandleInput(&input)) &&
            (input.type == InputEventType::KeyDown || input.type == InputEventType::KeyRepeat)) {
            switch (input.key) {
            case VK_RETURN:
                controller->TogglePartyMemberSelection();
                break;
            case VK_ESCAPE:
                switch (controller->m_mode) {
                case W8_PARTY_SELECT_CHARACTERS:
                case W8_PARTY_SELECT_IMPORT:
                    if (CountActiveCharacters() == 0) {
                        RequestScreenTransition();
                    } else {
                        controller->OpenNotification(g_localized_strings[0x6d3], true,
                                                     W8_PARTY_CONFIRM_LEAVE);
                    }
                    break;
                case W8_PARTY_SELECT_OPTIONS:
                case W8_PARTY_SELECT_CREATION_NOTICE:
                case W8_PARTY_SELECT_SAVE_NAME:
                    controller->SetMode(controller->m_previous_mode);
                    controller->SetSelection(
                        0, controller->m_previous_mode == W8_PARTY_SELECT_IMPORT, true);
                    break;
                }
                break;
            case VK_UP:
                if (controller->m_mode == W8_PARTY_SELECT_CHARACTERS &&
                    controller->m_character_panel->m_selected_row > 0) {
                    controller->SetSelection(controller->m_character_panel->m_selected_row - 1,
                                             false, true);
                }
                break;
            case VK_DOWN:
                if (controller->m_mode == W8_PARTY_SELECT_CHARACTERS &&
                    controller->m_character_panel->m_selected_row <
                        static_cast<std::int32_t>(
                            g_party_selection_character_collection->characters.size()) -
                            1) {
                    controller->SetSelection(controller->m_character_panel->m_selected_row + 1,
                                             false, true);
                }
                break;
            case VK_DELETE:
                if (controller->m_mode == W8_PARTY_SELECT_CHARACTERS && controller->m_character) {
                    controller->OnPrimary(controller->m_delete_button);
                }
                break;
            }
        }
    }
    UpdateCharacterEventState();
    controller->DrawPartySelectionComposition();
    RenderFrame();
}

/* Lifecycle record 2's frame close-out. The record's other slots only report
   success. Every path asks for a screen
   transition, records one of four codes and queues screen 0, so the record is a
   pure router - it selects which of the four the transition reports and then
   leaves. Nothing here names the four codes or the two globals that pick them. */
// FUNCTION: WIZ8 0x005c3800
void GameStartRouterFrame(void)
{
    std::uint32_t code;

    RequestScreenTransition();
    if (!g_status.skip_loose_character_check) {
        code = 4;
    } else {
        switch (g_wiz7_ending) {
        case 1:
            code = 2;
            break;
        case 2:
            code = 3;
            break;
        default:
            code = 1;
            break;
        }
    }
    SetIntroVideoIndex(code);
    SetPendingScreenState(W8_SCREEN_INTRO);
}

/* Lifecycle record 2 has nothing to release. Retail's linker folded this body
   into ScreenLifecycleSuccess, which compiles to the same bytes. */
bool GameStartRouterLeave(std::int32_t leaving)
{
    return true;
}

/* One byte per portrait; the retail table is one for every portrait. */
// GLOBAL: WIZ8 0x0061cbc0
std::uint8_t g_portrait_frame_flags[0x50] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
