#include "wiz8/local_screens/PartySelectionScreen.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "Font.h"
#include "asset_paths.h"
#include "compat/compiler.h"
#include "difficulty.h"
#include "file_time.h"
#include "image.h"
#include "input.h"
#include "sprite.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/PartyImport.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

struct W8PartySelectionCharacterCollection {
    W8PartySelectionCharacterCollection() : first_visible(0) {}
    ~W8PartySelectionCharacterCollection();
    W8PartySelectionCharacterCollection(const W8PartySelectionCharacterCollection&) = delete;
    W8Character* GetCharacter(int index);
    int FindPartySlot(int index);
    void DetachFromParty(int index);
    void DeleteAt(int index);
    void ClearCharacters();
    void ReloadCharacters();
    void LoadExternalCharacters();
    void SortCharactersByWriteTime();

    std::vector<W8Character*> characters;
    std::vector<std::string> names;
    int first_visible;
};

static std::unique_ptr<W8PartySelectionCharacterCollection> g_party_selection_character_collection;

W8PartySelectionCharacterCollection::~W8PartySelectionCharacterCollection()
{
    ClearCharacters();
}

void W8PartySelectionCharacterCollection::ClearCharacters()
{
    int index;
    for (index = 0; index < static_cast<int>(characters.size()); ++index) {
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
    for (int slot = 2; slot < 8; ++slot) {
        if (rows[slot].fOccupied) {
            characters.push_back(&g_status.buffers.Char[slot]);
        }
    }
    LoadExternalCharacters();
    SortCharactersByWriteTime();
}

W8Character* W8PartySelectionCharacterCollection::GetCharacter(int index)
{
    if (static_cast<std::size_t>(index) < characters.size()) {
        return characters[index];
    }
    return 0;
}

void W8PartySelectionCharacterCollection::DetachFromParty(int index)
{
    W8Character* previous = GetCharacter(index);
    int slot = FindPartySlot(index);
    W8Character* replacement = new W8Character;
    std::string path;
    path = BuildCharacterPath(previous->name, -1);
    if (!LoadCharacter(path, replacement, -1)) {
        *replacement = *previous;
    }
    RemoveCharacterFromParty(slot + 2);
    replacement->fInParty = false;
    characters[index] = replacement;
}

int W8PartySelectionCharacterCollection::FindPartySlot(int index)
{
    W8Character* character = GetCharacter(index);
    if (character && character->fInParty) {
        W8PartySlotRow* rows = g_status.buffers.XChar;
        for (int slot = 0; slot < 6; ++slot) {
            if (rows[slot + 2].fOccupied && &g_status.buffers.Char[slot + 2] == character) {
                return slot;
            }
        }
    }
    return -1;
}

void W8PartySelectionCharacterCollection::DeleteAt(int index)
{
    if (index >= 0 && std::cmp_less(index, characters.size())) {
        auto* object = characters[index];
        characters.erase(characters.begin() + index);
        delete object;
    }
}

void W8PartySelectionCharacterCollection::LoadExternalCharacters()
{
    std::string search_path;

    search_path = BuildCharacterFilePath(fmt::format("*.{}", "CHR"), -1);
    const std::string pattern(search_path);
    const auto separator = pattern.find_last_of("\\/");
    for (const auto& entry :
         wiz8::list_directory(wiz8::user_path(pattern.substr(0, separator)), "*.CHR")) {
        W8Character* character = new W8Character;
        if (!LoadCharacter(entry.c_str(), character, -1)) {
            delete character;
        } else {
            int slot;
            for (slot = 2; slot < 8; ++slot) {
                if (g_status.buffers.XChar[slot].fOccupied &&
                    strcmp(g_status.buffers.Char[slot].name.c_str(), character->name.c_str()) ==
                        0) {
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

static void SortPartySelectionCharactersByTime(W8Character** characters, wiz8::DiskFileTime* times,
                                               int first, int last)
{
    if (last - first < 9) {
        for (int next = first + 1; next <= last; ++next) {
            wiz8::DiskFileTime time = times[next];
            W8Character* character = characters[next];
            int insert = next;
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
    int left = first - 1;
    int right = last;
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

void W8PartySelectionCharacterCollection::SortCharactersByWriteTime()
{
    if (static_cast<int>(characters.size()) <= 1) {
        return;
    }

    std::vector<wiz8::DiskFileTime> times(characters.size());
    for (int index = 0; index < static_cast<int>(characters.size()); ++index) {
        std::string path;
        path = BuildCharacterPath(characters[index]->name, -1);
        std::unique_ptr<wiz8::File> handle = [&]() {
            try {
                return wiz8::open_file(wiz8::user_path(path), wiz8::OpenMode::read);
            } catch (const std::exception&) {
                return std::unique_ptr<wiz8::File>{};
            }
        }();
        if (handle) {
            times[index] = handle->times().modified;
            if (handle) handle->close();
            handle.reset();
        }
    }
    SortPartySelectionCharactersByTime(characters.data(), times.data(), 0,
                                       static_cast<int>(characters.size()) - 1);
}

class W8PartySelectionController {
public:
    void Setup();
    void SetMode(W8PartySelectionMode mode);
    void SetSelection(int selection, bool party_slot, bool refresh_other);
    void SetSelectedRow(int selection);
    void RefreshRows();
    void RefreshSlots();
    void OnPrimary(W8ButtonWidget* control);
    void OnDecision(bool cancelled);
    void OnToggle(bool enabled);
    bool HandleNameInput(const InputEvent& input);
    void OpenNotification(std::string_view message, bool allow_cancel,
                          W8PartyConfirmationAction action);
    void InvalidatePartySelectionComposition();
    void DrawPartySelectionComposition();
    void DrawSummary(SDL_Surface& target);
    void DrawCharacters(SDL_Surface& target);
    void DrawSlots(SDL_Surface& target);
    void DrawImportList(SDL_Surface& target);
    void DrawOptions(SDL_Surface& target);
    void SetOptionPanelMode(W8PartyCreationPage page);
    void ApplyPartySelectionConfirmation(W8PartyConfirmationAction action, bool accepted);
    void LoadImportedPartyFile(int selection);
    void TogglePartyMemberSelection();

    UiLayer input;
    W8PartySelectionMode m_mode = W8_PARTY_SELECT_CHARACTERS;
    W8PartySelectionMode m_previous_mode = W8_PARTY_SELECT_CHARACTERS;
    W8Character* m_character = nullptr;
    bool m_redraw_backdrop = true;
    bool summary_dirty = true, rows_dirty = true, slots_dirty = true;
    bool list_dirty = true, options_dirty = true;
    int m_selected_row = 0, selected_slot = -1, hovered_row = -1, hovered_slot = -1;
    int import_selection = 0, import_hovered = -1, import_first = 0;
    int difficulty = g_settings.difficulty;
    W8PartyCreationPage creation_page = W8_PARTY_CREATION_OPTIONS;
    W8RangeControl m_range{input, 0x1e9, 0x31, 0x1fb, 0x12b};
    std::array<HitTarget*, 6> rows{}, slots{};
    HitTarget* import_list = nullptr;
    std::array<std::unique_ptr<W8ButtonWidget>, 6> advance;
    std::array<std::unique_ptr<W8ButtonWidget>, 3> difficulty_buttons;
    std::unique_ptr<W8ButtonWidget> npc_toggle, ironman_toggle;
    std::array<std::unique_ptr<W8ButtonWidget>, 7> actions;
    W8ButtonWidget *m_create_button, *m_add_remove_button, *m_delete_button, *m_review_button,
        *m_reset_button, *m_confirm_button, *m_back_button;
    std::vector<std::unique_ptr<W8TextBuffer>> option_text;
    W8TextBuffer title;
    std::unique_ptr<TextFieldGroup> name_input;
    std::unique_ptr<W8MessageDialogBase> m_dialog;
    W8PartyConfirmationAction m_confirmation_action;
};

static std::unique_ptr<W8PartySelectionController> g_party_selection_controller;

static SDL_Point PartySlotPosition(int row)
{
    return {(row & 1) * 0x20b + 0x0e, (row / 2) * 0x82 + 0x3a};
}

void RefreshPartySelectionPortrait(unsigned int party_slot)
{
    const int row = party_slot >= 2 && party_slot < 8 ? party_slot - 2 : 0;
    const int portrait = g_status.buffers.Char[row + 2].portrait_index;
    const auto position = PartySlotPosition(row);
    SpriteDraw draw{};
    draw.mirror = g_portrait_descriptors[portrait].render_mode == ((row & 1) ? 2 : 1);
    BlitPartyPortraitAnimation(GetFrameBufferSurface(), portrait, position.x, position.y, draw,
                               row + 2, false);
}

bool PartySelectionInReviewMode()
{
    return g_party_selection_controller->m_mode == W8_PARTY_SELECT_IMPORT;
}

void W8PartySelectionController::RefreshRows()
{
    for (int row = 0; row < 6; ++row)
        rows[row]->setEnabled(m_mode == W8_PARTY_SELECT_CHARACTERS &&
                              g_party_selection_character_collection->GetCharacter(
                                  g_party_selection_character_collection->first_visible + row));
    rows_dirty = true;
}

void W8PartySelectionController::RefreshSlots()
{
    for (int slot = 0; slot < 6; ++slot) {
        const bool occupied = g_status.buffers.XChar[slot + 2].fOccupied;
        slots[slot]->setEnabled(m_mode <= W8_PARTY_SELECT_IMPORT && occupied);
        advance[slot]->SetVisible(m_mode == W8_PARTY_SELECT_IMPORT && occupied &&
                                  IsCharacterReadyToAdvance(slot + 2));
    }
    slots_dirty = true;
}

void W8PartySelectionController::SetSelectedRow(int selection)
{
    m_selected_row = selection;
    auto& collection = *g_party_selection_character_collection;
    const int visible = selection - collection.first_visible;
    if (visible < 0 || visible > 5 ||
        static_cast<int>(collection.characters.size()) < collection.first_visible + 6) {
        collection.first_visible =
            std::max(0, std::min(selection, static_cast<int>(collection.characters.size()) - 6));
        m_range.SetValue(collection.first_visible);
    }
    RefreshRows();
}

void W8PartySelectionController::DrawCharacters(SDL_Surface& target)
{
    if (m_mode != W8_PARTY_SELECT_CHARACTERS || !rows_dirty) return;
    rows_dirty = false;
    for (int row = 0; row < 6; ++row) {
        const int index = g_party_selection_character_collection->first_visible + row;
        const auto* character = g_party_selection_character_collection->GetCharacter(index);
        if (!character) continue;
        int left = 0x148, top = 0x31 + row * 0x2a;
        CatalogArtwork::Load(0xfb, 0).drawAndInvalidate(
            target, index == m_selected_row ? 1 : (row == hovered_row ? 2 : 0), left, top);
        CatalogArtwork::Load(0x13, character->portrait_index).draw(target, 0, left + 2, top + 2);
        if (character->fInParty) {
            ShadowRect(target, {left + 2, top + 2, 0x2d, 0x24});
            g_wiz_text_font_secondary.setShade(6);
        }
        left += 0x36;
        DrawText(target, g_wiz_text_font_secondary, {left, top + 4}, character->name);
        DrawText(target, g_wiz_text_font_secondary, {left, top + 0x0e},
                 fmt::format(
                     "{} {:d} {}", g_localized_strings[0x6b9], character->uiExpLevel,
                     g_localized_strings[g_profession_name_message_ids[character->iProfession]]));
        DrawText(target, g_wiz_text_font_secondary, {left, top + 0x18},
                 fmt::format("{} {}",
                             g_localized_strings[g_gender_name_message_rows[character->gender][0]],
                             g_localized_strings[g_race_name_message_ids[character->iRace]]));
        g_wiz_text_font_secondary.setShade(Font::kNormalShade);
    }
}

void W8PartySelectionController::DrawSlots(SDL_Surface& target)
{
    if (!slots_dirty) return;
    slots_dirty = false;
    for (int row = 0; row < 6; ++row) {
        const auto [left, top] = PartySlotPosition(row);
        const auto size = CatalogArtwork::Load(0x101, 0).size(0);
        const SDL_Rect area{left, top, size.x, size.y};
        const W8Character* character = nullptr;
        if (!g_status.buffers.XChar[row + 2].fOccupied)
            SDL_FillSurfaceRect(&target, &area, 0x8000);
        else {
            character = &g_status.buffers.Char[row + 2];
            const int portrait = character->portrait_index;
            SpriteDraw draw{};
            draw.mirror = g_portrait_descriptors[portrait].render_mode == ((row & 1) ? 2 : 1);
            RenderPartyPortrait(target, portrait, left, top, draw, 1, row + 2);
        }
        CatalogArtwork::Load(0x100, 0).drawAndInvalidate(target, 0, left - 13, top - 11);
        if (row == hovered_slot || row == selected_slot)
            CatalogArtwork::Load(0x101, 0).draw(target, row == hovered_slot ? 0 : 1, left, top);
        W8ControlsRect bounds{left - 8, top + 0x51, left + 0x61, top + 0x60};
        W8TextBuffer label(
            &bounds, character ? std::string_view(character->name) : std::string_view{},
            g_wiz_text_font_secondary, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
        label.RenderToTarget(0, false, target);
        advance[row]->m_dirty = true;
    }
}

void W8PartySelectionController::DrawImportList(SDL_Surface& target)
{
    if (m_mode != W8_PARTY_SELECT_IMPORT || !list_dirty) return;
    list_dirty = false;
    const SDL_Rect clip{0x148, 0x31, 0x9d, 0xfa};
    InvalidateRegion(clip.x, clip.y, clip.x + clip.w, clip.y + clip.h);
    CatalogArtwork::Load(0x1b6, 0).drawRect(target, SDL_Rect{0, 0, clip.w, clip.h}, clip.x, clip.y);
    ScopedClipRect clipping(target, clip);
    const auto& names = g_party_selection_character_collection->names;
    const int end = std::min(import_first + 17, static_cast<int>(names.size()));
    for (int row = import_first; row < end; ++row) {
        auto colour = g_wiz_text_font_secondary.glyphs->colors;
        if (row == import_selection)
            colour = g_font_state_palettes[W8_FONT_PALETTE_BLUE];
        else if (row == import_hovered)
            colour = g_font_state_palettes[W8_FONT_PALETTE_YELLOW];
        g_wiz_text_font_secondary.setPalette(colour);
        DrawText(target, g_wiz_text_font_secondary,
                 {clip.x + 2, clip.y + 1 + (row - import_first) * 14}, names[row]);
    }
    g_wiz_text_font_secondary.resetPalette();
}

void W8PartySelectionController::DrawSummary(SDL_Surface& target)
{
    if (m_mode > W8_PARTY_SELECT_IMPORT || !summary_dirty) {
        return;
    }
    summary_dirty = false;

    CatalogArtwork::Load(0xfc, 0).drawAndInvalidate(target, 0, 0x7f, 0xc5);
    CatalogArtwork::Load(0x1b6, 0).drawRect(
        target, SDL_Rect{0, 0, (0x139) - (0x85), (0xc0) - (0x30)}, 0x85, 0x30);
    InvalidateRegion(0x85, 0x30, 0x139, 0xc0);
    if (!m_character) {
        return;
    }

    W8Character* character = m_character;
    CatalogArtwork::Load(0x11, character->portrait_index).drawAndInvalidate(target, 0, 0x85, 0x30);
    if (character->fInParty) {
        W8ControlsRect bounds = {0x85, 0x30, 0x139, 0xba};
        W8TextBuffer overlay(&bounds, g_localized_strings[0x6b8], g_options_title_font,
                             g_W8TextBufferAlignBottom | g_W8TextBufferAlignCenter, 4);
        overlay.RenderToTarget(0, false, target);
    }

    {
        W8ControlsRect name_bounds = {0x84, 0xc8, 0x139, 0xed};
        W8TextBuffer name(
            &name_bounds, fmt::format("{} ({})", character->name_part_2, character->name),
            g_wiz_text_bold_font, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
        name.RenderToTarget(0, false, target);
    }

    g_wiz_text_font_secondary.setShade(Font::kNormalShade);

    std::string level_text = g_localized_strings[0x6b9];
    std::string profession =
        g_localized_strings[g_profession_name_message_ids[character->iProfession]];
    std::string level_line =
        fmt::format("{} {:d} {}", level_text, character->uiExpLevel, profession);
    int width = g_wiz_text_font_secondary.textWidth(level_line);
    DrawText(target, g_wiz_text_font_secondary, {(0xbf - width) / 2 + 0x78, 0xef},
             fmt::format("{} {:d} {}", level_text, character->uiExpLevel, profession));

    std::string gender = g_localized_strings[g_gender_name_message_rows[character->gender][0]];
    std::string race = g_localized_strings[g_race_name_message_ids[character->iRace]];
    std::string race_line = fmt::format("{} {}", gender, race);
    width = g_wiz_text_font_secondary.textWidth(race_line);
    DrawText(target, g_wiz_text_font_secondary, {(0xbf - width) / 2 + 0x7f, 0xfd},
             fmt::format("{} {}", gender, race));

    const std::string& personality =
        g_localized_strings[g_personality_message_ids[character->personality]];
    width = g_wiz_text_font_secondary.textWidth(personality);
    DrawText(target, g_wiz_text_font_secondary, {(0xbf - width) / 2 + 0x82, 0x10b}, personality);

    DrawText(target, g_wiz_text_font_secondary, {0x96, 0x127}, g_localized_strings[0x6ba]);
    DrawText(target, g_wiz_text_font_secondary, {0xbf, 0x127},
             fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_STRENGTH].base));
    DrawText(target, g_wiz_text_font_secondary, {0x96, 0x135}, g_localized_strings[0x6bb]);
    DrawText(target, g_wiz_text_font_secondary, {0xbf, 0x135},
             fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_INTELLIGENCE].base));
    DrawText(target, g_wiz_text_font_secondary, {0x96, 0x143}, g_localized_strings[0x6bc]);
    DrawText(target, g_wiz_text_font_secondary, {0xbf, 0x143},
             fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_PIETY].base));
    DrawText(target, g_wiz_text_font_secondary, {0x96, 0x151}, g_localized_strings[0x6bd]);
    DrawText(target, g_wiz_text_font_secondary, {0xbf, 0x151},
             fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_VITALITY].base));
    DrawText(target, g_wiz_text_font_secondary, {0x96, 0x15f}, g_localized_strings[0x6be]);
    DrawText(target, g_wiz_text_font_secondary, {0xbf, 0x15f},
             fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_DEXTERITY].base));
    DrawText(target, g_wiz_text_font_secondary, {0x96, 0x16d}, g_localized_strings[0x6bf]);
    DrawText(target, g_wiz_text_font_secondary, {0xbf, 0x16d},
             fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_SPEED].base));
    DrawText(target, g_wiz_text_font_secondary, {0x96, 0x17b}, g_localized_strings[0x6c0]);
    DrawText(target, g_wiz_text_font_secondary, {0xbf, 0x17b},
             fmt::format("{:d}", character->attributes[W8_ATTRIBUTE_SENSES].base));

    DrawText(target, g_wiz_text_font_secondary, {0xe3, 0x127}, g_localized_strings[0x6c1]);
    DrawText(target, g_wiz_text_font_secondary, {0x115, 0x127},
             fmt::format("{:d}", character->uiHPMax));
    DrawText(target, g_wiz_text_font_secondary, {0xe3, 0x135}, g_localized_strings[0x6c2]);
    DrawText(target, g_wiz_text_font_secondary, {0x115, 0x135},
             fmt::format("{:d}", SumCharacterSpellPoints(character)));
    DrawText(target, g_wiz_text_font_secondary, {0xe3, 0x143}, g_localized_strings[0x6c3]);
    DrawText(target, g_wiz_text_font_secondary, {0x115, 0x143},
             fmt::format("{:d}", character->uiStaminaMax));
    DrawText(target, g_wiz_text_font_secondary, {0xe3, 0x151}, g_localized_strings[0x6c4]);
    DrawText(target, g_wiz_text_font_secondary, {0x115, 0x151},
             fmt::format("{:d}", character->carrying_capacity / 10));
}

void W8PartySelectionController::DrawOptions(SDL_Surface& target)
{
    if (m_mode <= W8_PARTY_SELECT_IMPORT) return;
    if (options_dirty) {
        options_dirty = false;
        CatalogArtwork::Load(0x102, 0).drawAndInvalidate(target, 0, 0x78, 0x28);
        for (auto& entry : option_text) entry->RenderToTarget(0, true, target);
        for (auto& button : difficulty_buttons) button->m_dirty = true;
        npc_toggle->m_dirty = ironman_toggle->m_dirty = true;
        if (creation_page == W8_PARTY_CREATION_SAVE_NAME) {
            const auto size = CatalogArtwork::Load(0x102, 0).size(1);
            CatalogArtwork::Load(0x102, 0).draw(target, 1, 0x90 + (0x160 - size.x) / 2, 0xa8);
        }
    }
    for (auto& button : difficulty_buttons) button->Refresh();
    npc_toggle->Refresh();
    ironman_toggle->Refresh();
}

bool W8PartySelectionController::HandleNameInput(const InputEvent& input)
{
    if (!name_input || !IsTypingEvent(input)) return false;
    if (input.key != SDLK_ESCAPE && input.key != SDLK_RETURN) {
        const char32_t character = input.type == InputEventType::Text ? input.text : 0;
        if (character && character < 0x80 && strchr("\\/:*?\"<>|", int(character))) return true;
        name_input->HandleInput(input);
        OnToggle(name_input->Text(0).size() != 0);
    } else if (input.key == SDLK_ESCAPE || name_input->Text(0).size()) {
        SetTargetCursor(W8_CURSOR_NONE);
        AfterUiDispatch([this, cancelled = input.key == SDLK_ESCAPE] { OnDecision(cancelled); });
    }
    return true;
}

void W8PartySelectionController::SetOptionPanelMode(W8PartyCreationPage page)
{
    creation_page = page;
    option_text.clear();
    options_dirty = true;
    for (auto& button : difficulty_buttons) button->SetVisible(page == W8_PARTY_CREATION_OPTIONS);
    npc_toggle->SetVisible(page == W8_PARTY_CREATION_OPTIONS);
    ironman_toggle->SetVisible(page == W8_PARTY_CREATION_OPTIONS);

    W8ControlsRect bounds = {0x78 + 0x22, 0x28 + 0x12, 0x78 + 0x16e, 0x28 + 0x171};

    if (page == W8_PARTY_CREATION_OPTIONS) {
        option_text.emplace_back(
            new W8TextBuffer(&bounds, g_localized_strings[0x7f7], g_options_detail_font,
                             g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));

        bounds.right = 0x78 + 0x155;
        option_text.emplace_back(
            new W8TextBuffer(&bounds, g_localized_strings[0x7f8], g_options_detail_font,
                             g_W8TextBufferAlignRight | g_W8TextBufferAlignTop, 4));
        bounds.top += 0x16;
        option_text.emplace_back(
            new W8TextBuffer(&bounds, g_localized_strings[0x7f9], g_options_detail_font,
                             g_W8TextBufferAlignRight | g_W8TextBufferAlignTop, 4));
        bounds.top += 0x16;
        option_text.emplace_back(
            new W8TextBuffer(&bounds, g_localized_strings[0x7fa], g_options_detail_font,
                             g_W8TextBufferAlignRight | g_W8TextBufferAlignTop, 4));

        bounds.right = 0x78 + 0x16e;
        bounds.top += 0x2c;
        option_text.emplace_back(
            new W8TextBuffer(&bounds, g_localized_strings[0x80b], g_options_detail_font,
                             g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));

        bounds.top += 0x2c;
        W8TextBuffer* text =
            new W8TextBuffer(&bounds, g_localized_strings[0x6cd], g_options_detail_font,
                             g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop, 4);
        text->SetLineHeight(0x16);
        option_text.emplace_back(text);

        bounds.top += 0x42;
        option_text.emplace_back(
            new W8TextBuffer(&bounds, g_localized_strings[0x6ce], g_options_detail_font,
                             g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));

        bounds.top += 0x2c;
        text = new W8TextBuffer(&bounds, g_localized_strings[0x6cf], g_options_detail_font,
                                g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop, 4);
        text->SetLineHeight(0x16);
        option_text.emplace_back(text);
        return;
    }

    if (page == W8_PARTY_CREATION_NOTICE) {
        bounds.top += 0x2c;
        W8TextBuffer* text =
            new W8TextBuffer(&bounds, g_localized_strings[0x6d0], g_options_detail_font,
                             g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop, 4);
        text->SetLineHeight(0x16);
        option_text.emplace_back(text);
        return;
    }

    if (page != W8_PARTY_CREATION_SAVE_NAME) {
        return;
    }

    bounds.left = 0x78 + 0x2c;
    bounds.right = 0x78 + 0x164;
    W8TextBuffer* text =
        new W8TextBuffer(&bounds, g_localized_strings[0x6d1], g_options_detail_font,
                         g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop, 4);
    text->SetLineHeight(0x16);
    option_text.emplace_back(text);

    if (!name_input) {
        const auto size = CatalogArtwork::Load(0x102, 0).size(1);
        name_input = std::make_unique<TextFieldGroup>(input, g_wiz_text_mono_font);
        name_input->UseGameStyle();
        name_input->Add(0x92 + (0x160 - size.x) / 2, 0xaa, size.x - 4, size.y - 4, 0x7f, "", 0x28,
                        0x0f, true);
        name_input->Focus(0);
        OnToggle(false);
    }
}

void W8PartySelectionController::Setup()
{
    m_range.on_change = [this](auto* range) {
        if (m_mode == W8_PARTY_SELECT_CHARACTERS) {
            g_party_selection_character_collection->first_visible = range->m_value;
            RefreshRows();
        } else {
            import_first = range->m_value;
            list_dirty = true;
        }
    };
    const auto row_size = CatalogArtwork::Load(0xfb, 0).size(0);
    const auto slot_size = CatalogArtwork::Load(0x101, 0).size(0);
    for (int row = 0; row < 6; ++row) {
        rows[row] = &input.add(
            SDL_Rect{0x148, 0x31 + row * 0x2a, row_size.x + 1, row_size.y + 1},
            [this, row](const UiPointerEvent& event) {
                if (event.action != UiPointerAction::Up || event.captured_buttons)
                    PlayButtonSound(event.action);
                if (event.action == UiPointerAction::Enter ||
                    event.action == UiPointerAction::Leave) {
                    hovered_row = event.action == UiPointerAction::Enter ? row : -1;
                    rows_dirty = true;
                } else if (event.action == UiPointerAction::Wheel)
                    m_range.AdjustValue(event.wheel);
                else if ((event.action == UiPointerAction::Up && event.inside &&
                          (event.captured_buttons &
                           (event.button == UiPointerButton::Left ? SDL_BUTTON_LMASK
                                                                  : SDL_BUTTON_RMASK))) ||
                         (event.action == UiPointerAction::Down &&
                          event.button == UiPointerButton::Left && event.clicks == 2)) {
                    AfterUiDispatch([this, row, event] {
                        SetSelectedRow(g_party_selection_character_collection->first_visible + row);
                        SetSelection(m_selected_row, false, true);
                        if (event.button == UiPointerButton::Right)
                            OnPrimary(m_review_button);
                        else if (event.action == UiPointerAction::Down)
                            TogglePartyMemberSelection();
                    });
                }
                return UiResult::Handled;
            });
        const auto [x, y] = PartySlotPosition(row);
        slots[row] = &input.add(
            SDL_Rect{x, y, slot_size.x + 1, slot_size.y + 1},
            [this, row](const UiPointerEvent& event) {
                if (event.action != UiPointerAction::Up || event.captured_buttons)
                    PlayButtonSound(event.action);
                if (event.action == UiPointerAction::Enter ||
                    event.action == UiPointerAction::Leave) {
                    hovered_slot = event.action == UiPointerAction::Enter ? row : -1;
                    slots_dirty = true;
                } else if ((event.action == UiPointerAction::Up && event.inside &&
                            (event.captured_buttons &
                             (event.button == UiPointerButton::Left ? SDL_BUTTON_LMASK
                                                                    : SDL_BUTTON_RMASK))) ||
                           (event.action == UiPointerAction::Down &&
                            event.button == UiPointerButton::Left && event.clicks == 2)) {
                    AfterUiDispatch([this, row, event] {
                        SetSelection(row, true, true);
                        if (event.button == UiPointerButton::Right)
                            OnPrimary(m_review_button);
                        else if (event.action == UiPointerAction::Down)
                            TogglePartyMemberSelection();
                    });
                }
                return UiResult::Handled;
            });
        auto& button = advance[row] = std::make_unique<W8ButtonWidget>(input);
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0xa7, 0).spriteSheet(), 3, 0, 1, 2, 4),
            {.on_click =
                 [row](auto*) {
                     AfterUiDispatch([row] {
                         g_pending_screen_state.parameter_3 = &g_status.buffers.Char[row + 2];
                         g_pending_screen_state.mode = 2;
                         g_pending_screen_state.id = W8_SCREEN_CHARACTER;
                     });
                 },
             .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
             .priority = 1,
             .menu_artwork = true});

        button->SetPosition((row & 1) ? 0x21d : 0x52, (row / 2) * 0x82 + 0x6c);
    }
    import_list = &input.add(SDL_Rect{0x148, 0x31, 0x9e, 0xfb}, [this](
                                                                    const UiPointerEvent& event) {
        if (event.action != UiPointerAction::Up || event.captured_buttons)
            PlayButtonSound(event.action);
        if (event.action == UiPointerAction::Leave)
            import_hovered = -1;
        else if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Move)
            import_hovered = event.local.y / 14 + import_first;
        else if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Left &&
                 event.inside && (event.captured_buttons & SDL_BUTTON_LMASK)) {
            const int selection = event.local.y / 14 + import_first;
            if (selection != import_selection &&
                selection <
                    static_cast<int>(g_party_selection_character_collection->names.size())) {
                import_selection = selection;
                AfterUiDispatch([this, selection] { LoadImportedPartyFile(selection); });
            }
        }
        list_dirty = true;
        return UiResult::Handled;
    });
    for (int index = 0; index < 7; ++index) {
        auto& button = actions[index] = std::make_unique<W8ButtonWidget>(input);
        const int normal = index < 4 ? 0 : (index == 4 ? 24 : (index == 5 ? 0 : 4));
        const int x = index < 4 ? 0x145 : 0x178 + (index - 4) * 44;
        const int y = index < 4 ? 0x137 + index * 26 : 0x1b5;
        const auto art = CatalogArtwork::Load(index < 4 ? 0xfe : 0x106, 0);
        const auto size = art.size(normal);
        button->Configure(ButtonAppearance{.generic_image = DefaultGenericButtonImage(),
                                           .string = std::string(""),
                                           .usFont = &(index == 1 ? g_wiz_text_font_secondary
                                                                  : g_wiz_text_bold_font),
                                           .sForeColor = 4,
                                           .sShadowColor = -1},
                          {x, y, size.x, size.y},
                          {.on_click =
                               [this](auto* control) {
                                   AfterUiDispatch([this, control] { OnPrimary(control); });
                               },
                           .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
                           .menu_artwork = true});
        button->SetImage(ButtonGraphics::Load(art.spriteSheet(), normal + 3, normal, normal + 1,
                                              normal + 2, index == 4 ? 28 : normal + 2));

        if (index >= 4) button->SetTooltipIndex(index == 4 ? 0x6cc : (index == 5 ? 0x6cb : 0x6ca));
    }
    m_create_button = actions[0].get();
    m_add_remove_button = actions[1].get();
    m_delete_button = actions[2].get();
    m_review_button = actions[3].get();
    m_reset_button = actions[4].get();
    m_confirm_button = actions[5].get();
    m_back_button = actions[6].get();
    m_create_button->SetText(g_localized_strings[0x6c5]);
    m_delete_button->SetText(g_localized_strings[0x6c8]);
    m_review_button->SetText(g_localized_strings[0x6c9]);
    m_reset_button->Policy().primary_toggle = ButtonToggle::Release;
    for (int index = 0; index < 3; ++index) {
        auto& button = difficulty_buttons[index] = std::make_unique<W8ButtonWidget>(input);
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0xf1, 0).spriteSheet(), -1, 4, 5, 6, 7),
            {.on_click =
                 [this, index](auto*) {
                     difficulty = index;
                     for (int i = 0; i < 3; ++i) difficulty_buttons[i]->SetPressed(i == index);
                 },
             .policy = {.primary_toggle = ButtonToggle::StayLatched,
                        .input_rule = ButtonInputRule::PrimaryPressedState},
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        button->SetPosition(0x1d7, 0x3b + index * 22);
        button->SetPressed(index == difficulty);
    }
    npc_toggle = std::make_unique<W8ButtonWidget>(input);
    ironman_toggle = std::make_unique<W8ButtonWidget>(input);
    for (auto* button : {npc_toggle.get(), ironman_toggle.get()}) {
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0xf1, 0).spriteSheet(), -1, 2, 3, 0, 1),
            {.policy = {.primary_toggle = ButtonToggle::Release,
                        .input_rule = ButtonInputRule::PrimaryPressedState},
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        button->SetPosition(0x1d3, button == npc_toggle.get() ? 0x93 : 0xfe);
    }
    npc_toggle->SetPressed(g_settings.simplified_npc_interaction);
    W8ControlsRect bounds{0x7b, 6, 0x205, 0x23};
    title.SetClipBounds(bounds);
    title.SetLayoutBounds(&bounds, true);
    SetMode(W8_PARTY_SELECT_CHARACTERS);
    SetSelection(0, false, true);
}

void W8PartySelectionController::SetMode(W8PartySelectionMode mode)
{
    m_mode = mode;
    input.setEnabled(true);
    InvalidatePartySelectionComposition();
    const bool characters = mode == W8_PARTY_SELECT_CHARACTERS;
    const bool importing = mode == W8_PARTY_SELECT_IMPORT;
    m_range.SetActive(characters || importing);
    for (int i = 0; i < 7; ++i)
        actions[i]->SetVisible(i >= 5 || characters || (importing && (i == 3 || i == 4)));
    import_list->setEnabled(importing);
    RefreshRows();
    RefreshSlots();
    if (importing && g_party_selection_character_collection->names.empty()) {
        for (const auto& entry : wiz8::list_directory(wiz8::user_path("saves\\import"), "*.*")) {
            const auto status = wiz8::file_status(wiz8::user_path("saves\\import\\" + entry));
            if (status && status->info.type == SDL_PATHTYPE_FILE)
                g_party_selection_character_collection->names.push_back(entry);
        }
    }
    if (characters || importing) {
        const auto& collection = *g_party_selection_character_collection;
        const int maximum = std::max(0, static_cast<int>(characters ? collection.characters.size()
                                                                    : collection.names.size()) -
                                            (characters ? 6 : 17));
        m_range.SetRange(0, maximum);
        m_range.SetValue(characters ? collection.first_visible : import_selection);
        m_range.SetEnabled(maximum > 0);
        m_confirm_button->SetEnabled(CountActiveCharacters() != 0);
        for (auto& button : difficulty_buttons) button->SetVisible(false);
        npc_toggle->SetVisible(false);
        ironman_toggle->SetVisible(false);
    } else {
        SetOptionPanelMode(mode == W8_PARTY_SELECT_OPTIONS
                               ? W8_PARTY_CREATION_OPTIONS
                               : (mode == W8_PARTY_SELECT_CREATION_NOTICE
                                      ? W8_PARTY_CREATION_NOTICE
                                      : W8_PARTY_CREATION_SAVE_NAME));
    }
    const int message = characters                        ? 0x6b4
                        : importing                       ? 0x6b5
                        : mode == W8_PARTY_SELECT_OPTIONS ? 0x6b6
                                                          : 0x6b7;
    title.SetText(g_localized_strings[message], g_wiz_text_bold_font);
}

void W8PartySelectionController::SetSelection(int selection, bool party_slot, bool refresh_other)
{
    if (!party_slot) {
        if (m_mode == W8_PARTY_SELECT_CHARACTERS) {
            m_character = g_party_selection_character_collection->GetCharacter(selection);
            int selected_slot = -1;
            if (m_character && m_character->fInParty) {
                for (int slot = 0; slot < 6; ++slot) {
                    if (g_status.buffers.XChar[slot + 2].fOccupied &&
                        &g_status.buffers.Char[slot + 2] == m_character) {
                        selected_slot = slot;
                        break;
                    }
                }
            }
            this->selected_slot = selected_slot;
            slots_dirty = true;
            if (refresh_other) {
                SetSelectedRow(selection);
            }
        }
    } else {
        m_character = &g_status.buffers.Char[selection + 2];
        if (!m_character->fInParty) {
            m_character = 0;
            selection = -1;
        }
        if (m_mode == W8_PARTY_SELECT_CHARACTERS) {
            int character_index = -1;
            if (selection >= 0 && g_status.buffers.XChar[selection + 2].fOccupied) {
                for (int index = 0;
                     index <
                     static_cast<int>(g_party_selection_character_collection->characters.size());
                     ++index) {
                    if (g_party_selection_character_collection->GetCharacter(index) ==
                        m_character) {
                        character_index = index;
                        break;
                    }
                }
            }
            SetSelectedRow(character_index);
        }
        if (refresh_other) {
            selected_slot = selection;
            slots_dirty = true;
        }
    }

    summary_dirty = true;
    const std::string& text =
        g_localized_strings[((!m_character || !m_character->fInParty) ? 0x1b18 : 0x1b1c) / 4];
    m_add_remove_button->SetText(text);
    bool have_character = m_character != 0;
    m_add_remove_button->SetEnabled(have_character);
    m_add_remove_button->m_dirty = true;
    m_delete_button->SetEnabled(have_character);
    m_delete_button->m_dirty = true;
    m_review_button->SetEnabled(have_character);
    m_review_button->m_dirty = true;
    if (m_character && !m_character->fInParty && FindFreePartySlot(2, 8) == 0xffffffff) {
        m_add_remove_button->SetEnabled(false);
    }
}

void W8PartySelectionController::OnPrimary(W8ButtonWidget* control)
{
    if (control == m_back_button) {
        if (name_input) {
            OnDecision(true);
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
        int slot;
        for (slot = 0; slot < 6; ++slot) {
            if (g_status.buffers.XChar[slot + 2].fOccupied &&
                &g_status.buffers.Char[slot + 2] == m_character) {
                break;
            }
        }
        g_pending_screen_state.parameter_2 = slot < 6 ? slot + 2 : -1;
        g_pending_screen_state.parameter_3 = m_character;
        g_pending_screen_state.id = W8_SCREEN_CAMP;
        return;
    }
    if (control == m_create_button) {
        g_pending_screen_state.mode = 0;
        g_pending_screen_state.parameter_3 = 0;
        g_pending_screen_state.id = W8_SCREEN_CHARACTER;
        return;
    }

    if (control == m_confirm_button) {
        switch (m_mode) {
        case W8_PARTY_SELECT_CHARACTERS:
            if (static_cast<unsigned int>(CountActiveCharacters()) < 6) {
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
            g_settings.difficulty = static_cast<W8Difficulty>(difficulty);
            g_settings.simplified_npc_interaction = npc_toggle->IsPressed();
            if (ironman_toggle->IsPressed()) {
                SetMode(W8_PARTY_SELECT_CREATION_NOTICE);
            } else {
                RunNewGameOpeningSequence(true, {});
            }
            return;
        case W8_PARTY_SELECT_CREATION_NOTICE:
            SetMode(W8_PARTY_SELECT_SAVE_NAME);
            return;
        case W8_PARTY_SELECT_SAVE_NAME:
            OnDecision(false);
            return;
        default:
            return;
        }
    }

    if (control != m_reset_button) {
        return;
    }
    if (!m_reset_button->IsPressed()) {
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
    for (int index = 0; index < static_cast<int>(collection->characters.size()); ++index) {
        W8Character* previous = collection->GetCharacter(index);
        if (!previous->fInParty) {
            continue;
        }

        collection->DetachFromParty(index);
    }
    SetMode(W8_PARTY_SELECT_IMPORT);
    LoadImportedPartyFile(import_selection);
}

void W8PartySelectionController::OnDecision(bool accepted)
{
    if (!accepted) {
        std::string slot_name;
        slot_name = name_input->Text(0);
        if (SaveSlotFileExists(slot_name)) {
            OpenNotification(g_localized_strings[0x829], true,
                             W8_PARTY_CONFIRM_START_WITH_SAVE_NAME);
            return;
        }
        name_input.reset();
        RunNewGameOpeningSequence(true, slot_name);
    } else {
        name_input.reset();
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

void W8PartySelectionController::OnToggle(bool enabled)
{
    m_confirm_button->SetEnabled(enabled);
}

void W8PartySelectionController::InvalidatePartySelectionComposition()
{
    m_redraw_backdrop = summary_dirty = rows_dirty = slots_dirty = list_dirty = options_dirty =
        true;
    m_range.InvalidateAll();
    for (auto& button : actions) button->m_dirty = true;
    for (auto& button : advance) button->m_dirty = true;
    title.SetGeometryDirty();
}

void W8PartySelectionController::DrawPartySelectionComposition()
{
    auto& target = GetFrameBufferSurface();
    if (m_redraw_backdrop) {
        CatalogArtwork::Load(0xfa, 0).drawAndInvalidate(target, 0, 0, 0);
        if (m_mode == W8_PARTY_SELECT_IMPORT)
            CatalogArtwork::Load(0x103, 0).draw(target, 0, 0x140, 0x12e);
        m_redraw_backdrop = false;
    }
    m_range.Redraw(target, false);
    DrawSummary(target);
    DrawOptions(target);
    DrawCharacters(target);
    DrawSlots(target);
    for (auto& button : advance) button->Refresh();
    DrawImportList(target);
    for (auto& button : actions) button->Refresh();
    title.RenderToTarget(0, false, target);
    if (m_dialog) m_dialog->Draw(target);
    if (name_input) name_input->RenderAll(target);
}

void W8PartySelectionController::OpenNotification(std::string_view message, bool allow_cancel,
                                                  W8PartyConfirmationAction action)
{
    m_confirmation_action = action;
    m_dialog = std::make_unique<W8MessageDialogBase>();
    m_dialog->SetOrigin(0xf0, 0xbe);
    m_dialog->SetExtent(0xa0, 100);
    m_dialog->SetBackground("Data\\Dialogs\\DialogBackground.sti", 0);
    m_dialog->SetClientExtent(0xfa, 200);
    m_dialog->SetMessage(message, 1, 0x32, true, allow_cancel, true, true, 0, 0x15e);
    m_dialog->InputLayer().setModal(true);
}

void W8PartySelectionController::ApplyPartySelectionConfirmation(W8PartyConfirmationAction,
                                                                 bool accepted)
{
    if (!accepted) {
        if (m_confirmation_action == W8_PARTY_CONFIRM_IMPORT) {
            m_reset_button->SetPressed(false);
        }
        return;
    }

    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    switch (m_confirmation_action) {
    case W8_PARTY_CONFIRM_DELETE_CHARACTER: {
        int selected = m_selected_row;
        W8Character* character = collection->GetCharacter(selected);
        if (character->fInParty) {
            collection->DetachFromParty(selected);
            character = collection->GetCharacter(selected);
        }
        std::string path;
        path = BuildCharacterPath(character->name, -1);
        wiz8::remove_file(wiz8::user_path(path));
        collection->DeleteAt(selected);

        int maximum = static_cast<int>(collection->characters.size()) - 6;
        if (maximum < 0) {
            maximum = 0;
        }
        m_range.SetRange(0, maximum);
        m_range.SetEnabled(maximum > 0);
        m_range.InvalidateAll();
        if (selected > 0 || collection->characters.empty()) {
            --selected;
        }
        SetSelection(selected, false, true);
        return;
    }
    case W8_PARTY_CONFIRM_START_WITH_SAVE_NAME: {
        std::string slot_name;
        slot_name = name_input->Text(0);
        name_input.reset();
        RunNewGameOpeningSequence(true, slot_name);
        return;
    }
    case W8_PARTY_CONFIRM_PROCEED_TO_OPTIONS:
        m_previous_mode = m_mode;
        SetSelection(-1, true, true);
        SetMode(W8_PARTY_SELECT_OPTIONS);
        return;
    case W8_PARTY_CONFIRM_IMPORT: {
        for (int index = 0; index < static_cast<int>(collection->characters.size()); ++index) {
            W8Character* previous = collection->GetCharacter(index);
            if (!previous->fInParty) {
                continue;
            }
            collection->DetachFromParty(index);
        }
        SetMode(W8_PARTY_SELECT_IMPORT);
        LoadImportedPartyFile(import_selection);
        return;
    }
    case W8_PARTY_CONFIRM_LEAVE:
        RequestScreenTransition();
        return;
    default:
        break;
    }
}

void W8PartySelectionController::LoadImportedPartyFile(int selection)
{
    ResetForNewGame();
    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    if (selection >= 0 && selection < static_cast<int>(collection->names.size())) {
        std::string path;
        path = fmt::format("{}\\{}", "saves\\import", collection->names[selection]);
        int result = ImportWizardry7Party(path);
        if (result != 0) {
            ResetForNewGame();
            OpenNotification(g_localized_strings[(result == 2 ? 0x1b60 : 0x1b5c) / 4], false,
                             W8_PARTY_CONFIRM_NONE);
        }
    }

    m_confirm_button->SetEnabled(CountActiveCharacters() != 0);
    m_confirm_button->m_dirty = true;
    RefreshSlots();
    SetSelection(0, true, true);
}

void W8PartySelectionController::TogglePartyMemberSelection()
{
    int selected = m_selected_row;
    if (m_mode != W8_PARTY_SELECT_CHARACTERS || selected == -1) {
        return;
    }

    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    W8Character* character = collection->GetCharacter(selected);
    if (!character) return;
    if (!character->fInParty) {
        int slot = AddCharacterToParty(*character, -1);
        if (slot != -1) {
            delete character;
            collection->characters[selected] = &g_status.buffers.Char[slot];
        }
        if (selected < static_cast<int>(collection->characters.size()) - 1) {
            ++selected;
        }
        SetSelection(selected, false, true);
    } else {
        int previous_slot = collection->FindPartySlot(selected);
        collection->DetachFromParty(selected);

        int next_slot = previous_slot + 1;
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

    RefreshSlots();
    RefreshRows();
    m_confirm_button->SetEnabled(CountActiveCharacters() != 0);
    m_confirm_button->m_dirty = true;
}

bool PartySelectionScreenEnter(void)
{
    SetViewport({0, 0, 0x281, 0x1e1});
    SetPrimarySurfaceTextureHint2Enabled(false);
    UpdateHeldItemCursor();
    g_wiz_text_font_secondary.resetPalette();
    g_wiz_text_bold_font.resetPalette();

    W8PartySelectionCharacterCollection* collection = g_party_selection_character_collection.get();
    if (!collection) {
        ResetForNewGame();
        g_party_selection_character_collection =
            std::make_unique<W8PartySelectionCharacterCollection>();
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
    return 1;
}

bool PartySelectionScreenLeave(int leaving)
{
    if (leaving) {
        g_party_selection_character_collection.reset();
        g_party_selection_controller.reset();
    }
    return 1;
}

void PartySelectionScreenFrame(void)
{
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
            RetireModal(std::move(controller->m_dialog), false);
            controller->InvalidatePartySelectionComposition();
            controller->ApplyPartySelectionConfirmation(controller->m_confirmation_action, result);
        }
    }
    if (!controller->m_dialog)
        PumpUiInput({.unhandled = [&](const InputEvent& input) {
            if (!controller->HandleNameInput(input) && (input.type == InputEventType::KeyDown ||
                                                        input.type == InputEventType::KeyRepeat)) {
                switch (input.key) {
                case SDLK_RETURN:
                    controller->TogglePartyMemberSelection();
                    break;
                case SDLK_ESCAPE:
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
                case SDLK_UP:
                    if (controller->m_mode == W8_PARTY_SELECT_CHARACTERS &&
                        controller->m_selected_row > 0) {
                        controller->SetSelection(controller->m_selected_row - 1, false, true);
                    }
                    break;
                case SDLK_DOWN:
                    if (controller->m_mode == W8_PARTY_SELECT_CHARACTERS &&
                        controller->m_selected_row <
                            static_cast<int>(
                                g_party_selection_character_collection->characters.size()) -
                                1) {
                        controller->SetSelection(controller->m_selected_row + 1, false, true);
                    }
                    break;
                case SDLK_DELETE:
                    if (controller->m_mode == W8_PARTY_SELECT_CHARACTERS &&
                        controller->m_character) {
                        controller->OnPrimary(controller->m_delete_button);
                    }
                    break;
                }
            }
            return InputRoute::Handled;
        }});
    UpdateCharacterEventState();
    controller->DrawPartySelectionComposition();
    RenderFrame();
}

void GameStartRouterFrame(void)
{
    w8_ulong code;

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
    g_pending_screen_state.id = W8_SCREEN_INTRO;
}

bool GameStartRouterLeave(int leaving)
{
    return 1;
}

unsigned char g_portrait_frame_flags[0x50] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};
