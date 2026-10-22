#include "wiz8/local_screens/CharacterScreen.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "asset_paths.h"
#include "dialog_code/DialogBase.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/ProfRaceInfoDialog.h"
#include "wiz8/dialog_code/SpellInfoDialog.h"
#include "wiz8/dialog_code/StatInfoDialogs.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"
#include "wiz8/platform/input.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

unsigned short g_race_name_message_ids[W8_RACE_COUNT] = {
    0x284, 0x285, 0x286, 0x287, 0x288, 0x289, 0x28a, 0x28b,
    0x28c, 0x28d, 0x28e, 0x28f, 0x290, 0x291, 0x292, 0x293,
};
unsigned short g_profession_name_message_ids[32] = {
    0x2a4, 0x2a5, 0x2a6, 0x2a7, 0x2a8, 0x2a9, 0x2aa, 0x2ab, 0x2ac, 0x2ad, 0x2ae,
    0x2af, 0x2b0, 0x2b1, 0x2b2, 0,     0x2c2, 0x2c3, 0x2c4, 0x2c5, 0x2c6, 0x2c7,
    0x2c8, 0x2c9, 0x2ca, 0x2cb, 0x2cc, 0x2cd, 0x2ce, 0x2cf, 0x2d0, 0,
};
unsigned short g_personality_message_ids[10] = {
    0x3ad, 0x3ae, 0x3af, 0x3b0, 0x3b1, 0x3b2, 0x3b3, 0x3b4, 0x3b5, 0,
};
unsigned short g_profession_level_name_message_ids[15][9] = {
    {0x3b6, 0x3b7, 0x3b8, 0x3b9, 0x3ba, 0x3bb, 0x3bc, 0x3bd, 0x3be},
    {0x3b6, 0x3b7, 0x3bf, 0x3c0, 0x3c1, 0x3c2, 0x3c3, 0x3c4, 0x3c5},
    {0x3b6, 0x3b7, 0x3c6, 0x3b9, 0x3c7, 0x3c2, 0x3c8, 0x3c9, 0x3ca},
    {0x3b6, 0x3b7, 0x3cb, 0x3cc, 0x3cd, 0x3ce, 0x3cf, 0x3d0, 0x3d1},
    {0x3b6, 0x3b7, 0x3d2, 0x3d3, 0x3d4, 0x3d5, 0x3d6, 0x3bd, 0x3d7},
    {0x3b6, 0x3b7, 0x3d8, 0x3d9, 0x3da, 0x3db, 0x3dc, 0x3dd, 0x3de},
    {0x3b6, 0x3b7, 0x3f9, 0x3df, 0x3e0, 0x3e1, 0x3dc, 0x3e2, 0x3e3},
    {0x3b6, 0x3b7, 0x3e4, 0x3e5, 0x3e6, 0x3e7, 0x3e8, 0x3e9, 0x3ea},
    {0x3b6, 0x3b7, 0x3eb, 0x3ec, 0x3ed, 0x3ee, 0x3ef, 0x3f0, 0x3f1},
    {0x3b6, 0x3b7, 0x3f2, 0x3f3, 0x3f4, 0x3f5, 0x3f6, 0x3f7, 0x3f8},
    {0x3b6, 0x3b7, 0x3f9, 0x3fa, 0x3fb, 0x3fc, 0x3fd, 0x3fe, 0x3ff},
    {0x3b6, 0x3b7, 0x400, 0x401, 0x402, 0x403, 0x404, 0x405, 0x406},
    {0x3b6, 0x3b7, 0x407, 0x408, 0x409, 0x40a, 0x40b, 0x40c, 0x40d},
    {0x3b6, 0x3b7, 0x40e, 0x40f, 0x410, 0x411, 0x412, 0x413, 0x414},
    {0x3b6, 0x3b7, 0x415, 0x416, 0x417, 0x418, 0x419, 0x41a, 0x41b},
};
static int g_character_page_title_ids[4] = {0xcf, 0xd1, 0xd0, 0xd2};

void CharacterScreen::BuildControls()
{
    const auto button = [this](int x, int frame, int help, std::function<void()> action) {
        auto result = std::make_unique<W8ButtonWidget>(m_input);
        result->Configure(ButtonGraphics::Load(CatalogArtwork::Load(0x106, 0).spriteSheet(),
                                               frame + 3, frame, frame + 1, frame + 2, frame + 2),
                          {.on_click = [action](auto*) { AfterUiDispatch(action); },
                           .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = help,
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});

        result->SetPosition(x, 0x1c2);
        return result;
    };
    m_next = button(0x254, 8, 0xdc, [this] { AdvancePage(false); });
    m_previous = button(0x228, 0xc, 0xdd, [this] {
        int index = m_page_index - 1;
        while (index >= 0 && !m_page_enabled[index]) --index;
        if (index >= 0) SelectPage(index);
    });
    m_exit = button(0x1fc, 0x14, 0xde, [this] { m_pages[m_page_index]->Accept(); });
    m_accept = button(0x1d0, 4, 0xdf, [this] {
        if (m_mode == W8_CHARACTER_EDIT && !m_exit->IsEnabled())
            ReturnToPreviousScreen();
        else
            ShowMessage(Data().strings[0xd3], 1, 2);
    });
    m_reset = button(0, 0x1d, 0xe1, [this] {
        std::ranges::fill(m_page_enabled, true);
        m_force_transition = true;
        InitializeCharacterCreation(&m_character, &m_creation_state);
        m_mode = W8_CHARACTER_CREATE;
        m_pages[3]->m_mode = W8_CHARACTER_CREATE;
        m_reset->SetVisible(false);
        SelectPage(0);
    });
    m_reset->SetVisible(false);
    if (m_mode == W8_CHARACTER_EDIT && GameStarted() &&
        CharacterPointerToPartySlot(m_original) > 1) {
        m_reset->SetVisible(true);
        if (m_game.combat.has_value()) {
            m_reset->SetEnabled(false);
        }
    }

    m_page_index = -1;
    int index = 0;
    while (index < 4 && !m_page_enabled[index]) {
        ++index;
    }
    SelectPage(index < 4 ? index : -1);
}

void CharacterScreen::UpdateDialog()
{
    if (m_dialog) {
        if (!m_dialog->ProcessInput()) {
            m_navigation_dirty = true;
            m_pages[m_page_index]->Prepare();
            m_header_dirty = true;
            bool accepted = false;
            if (m_capture_dialog_result) {
                accepted = static_cast<W8MessageDialogBase&>(*m_dialog).accepted;
                m_capture_dialog_result = false;
            }
            RetireModal(std::move(m_dialog), false);
            HandleDialogResult(m_dialog_response, accepted);
        }
    }
}

void CharacterScreen::UpdateNavigation(W8CharacterPage* page)
{
    bool next_enabled;
    bool exit_enabled;
    page->GetNavigationState(&next_enabled, &exit_enabled);
    if (!m_next->IsEnabled() && next_enabled) {
        wiz8::sound::play("Data\\Sound\\Misc\\Points Spent.wav");
    }
    if (m_next->IsEnabled() != next_enabled) {
        m_next->SetEnabled(next_enabled);
        m_next->m_dirty = true;
    }
    if (m_exit->IsEnabled() != exit_enabled) {
        m_exit->SetEnabled(exit_enabled);
        m_exit->m_dirty = true;
    }
}

void CharacterScreen::ShowSpellInfo(int value)
{
    m_dialog_response = 0;
    m_dialog = std::make_unique<W8SpellInfoDialog>(value);
    m_dialog->SetText("");
    m_dialog->InputLayer().setModal(true);
}

void CharacterScreen::ShowProfessionInfo(W8Profession profession)
{
    m_dialog_response = 0;
    m_dialog = std::make_unique<W8ProfessionInfoDialog>(profession);
    m_dialog->SetText("");
    m_dialog->InputLayer().setModal(true);
}

void CharacterScreen::ShowRaceInfo(W8Race race)
{
    m_dialog_response = 0;
    m_dialog = std::make_unique<W8RaceInfoDialog>(race);
    m_dialog->SetText("");
    m_dialog->InputLayer().setModal(true);
}

void CharacterScreen::ShowPrimaryAttributeInfo(W8Attribute attribute)
{
    m_dialog_response = 0;
    m_dialog = std::make_unique<W8AttributeInfoDialog>(attribute);
    m_dialog->SetText("");
    m_dialog->InputLayer().setModal(true);
}

void CharacterScreen::ShowSecondaryAttributeInfo(unsigned int attribute)
{
    m_dialog_response = 0;
    m_dialog = std::make_unique<W8SecondaryAttributeInfoDialog>(attribute);
    m_dialog->SetText("");
    m_dialog->InputLayer().setModal(true);
}

void CharacterScreen::ShowSkillInfo(W8Skill value)
{
    m_dialog_response = 0;
    if (value == g_profession_bonus_skills[m_character.iProfession]) {
        m_dialog = std::make_unique<W8SkillInfoDialog>(value, false, false, true);
    } else {
        m_dialog = std::make_unique<W8SkillInfoDialog>(value, false, false, false);
    }
    m_dialog->SetText("");
    m_dialog->InputLayer().setModal(true);
}

void CharacterScreen::DrawNavigation(SDL_Surface& target)
{
    const std::array buttons{m_previous.get(), m_next.get(), m_exit.get(), m_accept.get(),
                             m_reset.get()};
    for (auto* button : buttons) m_navigation_dirty |= button->TakeDirty();
    if (!m_navigation_dirty) return;
    CatalogArtwork::Load(0x107, 0).drawAndInvalidate(target, 4, 0, 0x1c2);
    for (auto* button : buttons) button->Draw(target);
    m_navigation_dirty = false;
}

void CharacterScreen::ShowDescription(int first, int second)
{
    ShowMessage(fmt::sprintf(Data().strings[0x1d6],
                             Data().strings[g_character_description_first_ids[first]],
                             m_character.name, Data().strings[g_character_skill_name_ids[second]]),
                0, 0);
}

void CharacterScreen::ShowCharacterSummary()
{
    m_dialog_response = 1;
    m_dialog = CreateCharacterSummaryDialog(&m_character);
    m_dialog->InputLayer().setModal(true);
}

void CharacterScreen::AdvancePage(bool forward)
{
    if (!forward || (m_page_index != 3 && m_next->IsEnabled())) {
        int index = m_page_index;
        if (index == 1 && m_creation_state.spell_points_remaining > 0 && !m_block_advance) {
            ShowMessage(Data().strings[0xc4], 1, 3);
            return;
        }
        if (index == 0) {
            if (m_mode == W8_CHARACTER_LEVEL_UP &&
                m_character.iProfession != m_original->iProfession && !m_confirm_profession) {
                int value = ComputeRealmSkillDebt(m_original, &m_character);
                if (value > 0) {
                    ShowMessage(
                        fmt::sprintf(
                            Data().strings[0xdb],
                            Data().strings[g_profession_name_message_ids[m_original->iProfession]],
                            Data().strings[g_profession_name_message_ids[m_character.iProfession]],
                            value),
                        1, 4);
                } else {
                    ShowMessage(
                        fmt::sprintf(
                            Data().strings[0xda],
                            Data().strings[g_profession_name_message_ids[m_original->iProfession]],
                            Data().strings[g_profession_name_message_ids[m_character.iProfession]]),
                        1, 4);
                }
                return;
            }
            m_page_enabled[1] = m_creation_state.spell_points_total > 0;
        }
        m_block_advance = false;
        m_confirm_profession = false;
        do {
            ++index;
        } while (index < 4 && !m_page_enabled[index]);
        if (index < 4) {
            SelectPage(index);
        } else if (!m_force_transition) {
            if (CommitCharacter()) {
                ReturnToPreviousScreen();
                if (m_mode == W8_CHARACTER_LEVEL_UP &&
                    m_character.experience_goal <= m_character.experience) {
                    RequestScreen({.id = W8_SCREEN_CHARACTER,
                                   .mode = W8_CHARACTER_LEVEL_UP_NOTICE,
                                   .character = m_original});
                }
            }
        } else if (ValidateName()) {
            ShowMessage(Data().strings[0xd6], 1, 5);
        }
    }
}

void CharacterScreen::SelectPage(int index)
{
    if (m_page_index >= 0) {
        m_pages[m_page_index]->SetActive(false);
        m_pages[m_page_index]->Deactivate();
    }
    SyncCharacterForPage(index);
    if (!m_pages[index]) {
        std::unique_ptr<W8CharacterPage> page;
        switch (index) {
        case 0:
            page = CreateCharacterStatsPage();
            break;
        case 1:
            page = CreateCharacterSpellsPage();
            break;
        case 2:
            page = CreateCharacterSkillsPage();
            break;
        case 3:
            page = CreateCharacterPersonalityPage();
            break;
        }
        page->m_screen = this;
        page->SetCharacter(&m_character, &m_creation_state, m_mode);
        m_pages[index] = std::move(page);
    }
    m_page_index = index;
    W8CharacterPage* page = m_pages[index].get();
    page->Activate();
    page->SetActive(true);

    int previous = index - 1;
    while (previous >= 0 && !m_page_enabled[previous]) --previous;
    m_previous->SetVisible(previous != -1);
    int next = index + 1;
    while (next < 4 && !m_page_enabled[next]) ++next;
    const int frame = next == 4 ? 0x10 : 8;
    m_next->SetImage(ButtonGraphics::Load(CatalogArtwork::Load(0x106, 0).spriteSheet(), frame + 3,
                                          frame, frame + 1, frame + 2, frame + 2));
    m_next->SetTooltipIndex(next == 4 ? 0xe0 : 0xdc);
    m_navigation_dirty = true;
    page->Prepare();
    m_header_dirty = true;
    UpdateNavigation(page);
}

void CharacterScreen::SyncCharacterForPage(int index)
{
    if (m_page_index > index) return;
    if (index == 0) {
        if (m_mode == W8_CHARACTER_CREATE)
            InitializeCharacterCreation(&m_character, &m_creation_state);
        else if (m_mode == W8_CHARACTER_LEVEL_UP)
            InitializeCharacterLevelUp(&m_character, &m_creation_state);
    } else if (index == 1) {
        CountRemainingSpellPoints(&m_character, &m_creation_state);
    } else if (index == 3) {
        if (m_character.personality < 0) DeriveCharacterPersonality(&m_character);
        if (m_character.portrait_index < 0) CalcCharacterTableValue(&m_character);
    }
}

void CharacterScreen::DrawHeader()
{
    W8TextBuffer text;
    CatalogArtwork::Load(0x107, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0xc3, 0);
    W8ControlsRect bounds = {0xc3, 0, 0x285, 0x2c};
    text.RenderString(&bounds, Data().strings[g_character_page_title_ids[m_page_index]],
                      g_options_detail_font, true, GetFrameBufferSurface());
    CatalogArtwork::Load(0x107, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, 0, 0);

    if (m_mode == W8_CHARACTER_CREATE) {
        CatalogArtwork::Load(0x107, 0).draw(GetFrameBufferSurface(), 2, 10, 0xc);
    } else {
        int frame = m_original == 0 ? m_character.portrait_index : m_original->portrait_index;
        CatalogArtwork::Load(0x11, frame).draw(GetFrameBufferSurface(), 0, 10, 0xc);
    }

    if (m_mode == W8_CHARACTER_CREATE && m_page_index == 0) {
        CatalogArtwork::Load(0x107, 0).draw(GetFrameBufferSurface(), 3, 5, 0xa5);
    } else {
        bounds.left = 5;
        bounds.top = 0xa5;
        bounds.right = 0xc1;
        bounds.bottom = 0xdf;
        text.SetLayoutMode(g_W8TextBufferAlignTop | g_W8TextBufferAlignCenter);
        if (m_mode != W8_CHARACTER_CREATE) {
            text.RenderString(&bounds, m_character.name, g_wiz_text_font_secondary, true,
                              GetFrameBufferSurface());
        }
        bounds.top += 0xe;
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        text.RenderString(
            fmt::format("{} {}", Data().strings[g_gender_name_message_rows[m_character.gender][0]],
                        Data().strings[g_race_name_message_ids[m_character.iRace]]),
            g_wiz_text_font_secondary, true, GetFrameBufferSurface());
        bounds.top += 0xe;
        text.RenderString(&bounds,
                          Data().strings[g_profession_name_message_ids[m_character.iProfession]],
                          g_wiz_text_font_secondary, true, GetFrameBufferSurface());
        bounds.top += 0xe;
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        text.RenderString(
            fmt::format(
                "{} {:d} ({})", Data().strings[0x6b9], m_character.uiExpLevel,
                Data().strings[g_profession_level_name_message_ids[m_character.iProfession]
                                                                  [m_character.level_band]]),
            g_wiz_text_font_secondary, true, GetFrameBufferSurface());
    }
    m_header_dirty = false;
}

bool CharacterScreen::CommitCharacter()
{
    if (!ValidateName()) return false;

    const bool creating = m_mode == W8_CHARACTER_CREATE;
    W8Character backup;
    backup = m_character;
    if (m_mode != W8_CHARACTER_EDIT) {
        FinalizeCreatedCharacter(&m_character, &m_creation_state, creating);
    }
    if (!GameStarted() && !m_game.status.skip_loose_character_check) {
        if (m_original != 0) {
            std::string path;
            path = BuildCharacterPath(m_original->name, -1);
            wiz8::remove_file(wiz8::user_path(path));
        }
        m_character.fInParty = false;
        if (!SaveCharacter(&m_character, -1)) {
            m_character = std::move(backup);
            ShowMessage(Data().strings[0xd4], 0, 0);
            return false;
        }
        m_character.fInParty = backup.fInParty;
    }
    if (m_original != 0) {
        bool was_in_party = m_original->fInParty;
        *m_original = m_character;
        if (was_in_party) m_original->fInParty = true;
        if (m_original->iProfession == W8_PROFESSION_GADGETEER)
            UpgradeProfessionClassItem(
                m_game, was_in_party ? PartyMember(m_game, CharacterPointerToPartySlot(m_original))
                                     : Outsider(*m_original));
        RecalculateCharacterDerivedStats(m_game, *m_original);
    }
    return true;
}

void CharacterScreen::SkillAvailabilityChanged(W8Skill skill, bool available)
{
    if (available)
        ResetSkillContribution(&m_character, &m_creation_state, skill);
    else
        RefundSkillAllocation(&m_character, &m_creation_state, skill);
    if (m_pages[2] != 0) {
        m_pages[2]->Refresh();
    }
}

void CharacterScreen::ShowMessage(std::string_view text, int confirmation, int response)
{
    m_dialog_response = response;
    auto dialog = std::make_unique<W8MessageDialogBase>();
    dialog->SetExtent(0xf0, 0xbe);
    dialog->SetOrigin(0xa0, 100);
    dialog->SetBackground("Data\\Dialogs\\DialogBackground.sti", 0);
    dialog->SetClientExtent(0xfa, 200);
    dialog->SetMessage(text, 1, 0x32, true, confirmation, true, true, 0, 0x15e);
    dialog->InputLayer().setModal(true);
    m_dialog = std::move(dialog);
    m_capture_dialog_result = true;
}

void CharacterScreen::HandleDialogResult(int response, unsigned char accepted)
{
    if (accepted) {
        switch (response) {
        case 2:
            ReturnToPreviousScreen();
            break;
        case 3:
            m_block_advance = true;
            AdvancePage(false);
            break;
        case 4:
            m_confirm_profession = true;
            AdvancePage(false);
            break;
        case 5: {
            int value = ComputeStartingEquipmentCost(&m_character);
            int next_response;
            std::string format;
            if (CanAffordStartingEquipment(&m_character)) {
                format = Data().strings[0xd7];
                next_response = 6;
            } else {
                format = Data().strings[0xd8];
                next_response = 7;
            }
            ShowMessage(fmt::sprintf(format, value), 1, next_response);
            break;
        }
        case 6:
            FinalizeCreatedCharacter(&m_character, &m_creation_state, false);
            RecruitCharacterIntoParty(m_original, &m_character, true);
            ReturnToPreviousScreen();
            break;
        case 7:
            FinalizeCreatedCharacter(&m_character, &m_creation_state, false);
            RecruitCharacterIntoParty(m_original, &m_character, false);
            ReturnToPreviousScreen();
            break;
        }
    } else if (response == 6) {
        FinalizeCreatedCharacter(&m_character, &m_creation_state, false);
        RecruitCharacterIntoParty(m_original, &m_character, false);
        ReturnToPreviousScreen();
    }
}

bool CharacterScreen::ValidateName()
{
    if (m_original != 0 && strcmp(m_original->name.c_str(), m_character.name.c_str()) == 0) {
        return true;
    }
    if (!GameStarted() && !m_game.status.skip_loose_character_check) {
        std::string path;
        path = BuildCharacterPath(m_character.name, -1);
        if ([&]() {
                const auto status = wiz8::file_status(wiz8::user_path(path));
                return status && status->info.type == SDL_PATHTYPE_FILE;
            }()) {
            ShowMessage(Data().strings[0xd5], 0, 0);
            return false;
        }
    }
    for (int index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
        if (m_game.party.members[index].has_value() &&
            strcmp(m_game.party.characters[index].name.c_str(), m_character.name.c_str()) == 0) {
            ShowMessage(Data().strings[0xd5], 0, 0);
            return false;
        }
    }
    return true;
}

bool CharacterScreen::HasDialog()
{
    return m_dialog != nullptr;
}

W8Character* CharacterScreen::GetOriginalCharacter()
{
    return m_original;
}

bool CharacterScreen::enter(const ScreenRequest& request)
{
    const W8CharacterScreenMode mode = std::get<W8CharacterScreenMode>(request.mode);
    SetViewport({0, 0, 0x281, 0x1e1});
    SetUiCanvasAlphaTestEnabled(false);
    ScreenPointer().reset();
    g_wiz_text_font_secondary.resetPalette();
    g_wiz_text_bold_font.resetPalette();
    m_mode = mode;
    m_original = request.character;
    if (m_original != 0) {
        m_character = *m_original;
        m_character.fInParty = false;
    }
    if (m_mode == W8_CHARACTER_LEVEL_UP_NOTICE) {
        m_mode = W8_CHARACTER_LEVEL_UP;
        wiz8::sound::play("Data\\Sound\\Misc\\GainLevel.wav");
        ShowMessage(fmt::sprintf(Data().strings[0xd9], m_character.name, 0, 0), 0, 0);
    }
    m_page_enabled[0] = m_mode != W8_CHARACTER_EDIT;
    m_page_enabled[1] = m_mode != W8_CHARACTER_EDIT;
    m_page_enabled[2] = m_mode != W8_CHARACTER_EDIT;
    m_page_enabled[3] = m_mode != W8_CHARACTER_LEVEL_UP;
    BuildControls();
    if (!GameStarted() && (mode == W8_CHARACTER_CREATE || mode == W8_CHARACTER_LEVEL_UP)) {
        StartMusicResource("Menus.MPL", 1, 1);
    }
    return true;
}

CharacterScreen::CharacterScreen(Game& game) : m_game(game) {}

CharacterScreen::~CharacterScreen() = default;

bool CharacterScreen::leave(ScreenDeparture)
{
    if (m_page_index >= 0 && m_pages[m_page_index]) m_pages[m_page_index]->Deactivate();
    return true;
}

void CharacterScreen::frame()
{
    UpdateDialog();
    if (!m_dialog)
        PumpUiInput({
            .before =
                [this](const InputEvent& input) {
                    m_pages[m_page_index]->HandleInput(&input);
                    return InputRoute::Pass;
                },
            .unhandled =
                [this](const InputEvent& input) {
                    if (input.type == InputEventType::KeyDown) {
                        if (input.key == SDLK_RETURN || input.key == SDLK_RIGHT ||
                            input.key == SDLK_N) {
                            AdvancePage(true);
                        } else if (input.key == SDLK_ESCAPE) {
                            if (m_mode == W8_CHARACTER_EDIT && !m_exit->IsEnabled()) {
                                ReturnToPreviousScreen();
                            } else {
                                ShowMessage(Data().strings[0xd3], 1, 2);
                            }
                        } else if ((input.key == SDLK_LEFT || input.key == SDLK_B) &&
                                   m_page_index != 3) {
                            int index = m_page_index - 1;
                            while (index >= 0 && !m_page_enabled[index]) --index;
                            if (index != -1) SelectPage(index);
                        }
                    }
                    return InputRoute::Handled;
                },
        });
    if (m_header_dirty) DrawHeader();
    m_pages[m_page_index]->Redraw(GetFrameBufferSurface(), false);
    DrawNavigation(GetFrameBufferSurface());
    if (m_dialog) m_dialog->RefreshModal();
    RenderFrame();
}
