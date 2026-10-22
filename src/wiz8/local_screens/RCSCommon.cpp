#include "wiz8/local_screens/RCSCommon.h"

#include <array>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "dialog_code/DialogBase.h"
#include "layouts/main_game_screen.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/learned_spells.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/RCSItemsPage.h"
#include "wiz8/local_screens/RCSStatsPage.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

int CampScreen::ReviewedPartySlot() const
{
    return reviewed ? CampPartySlot(*reviewed) : -1;
}

/* Select a party member and rebuild the active page for that character. */
void CampScreen::SelectCampCharacter(int slot)
{
    reviewed = &CurrentGame().party.characters[slot];
    SyncReviewCharInputRegion();
    switch (page) {
    case W8_CAMP_PAGE_ITEMS:
        items.ReviewedCharacterChanged(*reviewed);
        break;
    case W8_CAMP_PAGE_STATS:
        stats.ReviewedCharacterChanged(*reviewed);
        break;
    case W8_CAMP_PAGE_SKILLS:
        skills.ReviewedCharacterChanged(*reviewed);
        break;
    case W8_CAMP_PAGE_SPELLS:
        spells.ReviewedCharacterChanged(*reviewed);
        break;
    default:
        break;
    }
    InvalidateAll();
}

/* Give the held item to a camp portrait on right-click: refuse dead/insane/
   stoned with a notice, check eligibility and combat action allowance, then
   AddItemToCharacter. */
void TryGiveHeldItemToCampPortrait(int slot)
{
    W8Character* character;

    if (!CurrentGame().party.members[slot].has_value()) {
        return;
    }
    character = &CurrentGame().party.characters[slot];
    if (character->uiCondition[W8_CONDITION_MISSING] != 0) {
        ShowCampNoticeLine(Data().strings[0x907], 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_WEBBED] != 0) {
        ShowCampNoticeLine(Data().strings[0x908], 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_TURNCOAT] != 0) {
        ShowCampNoticeLine(Data().strings[0x909], 0, true, false);
        return;
    }
    if (!IsPartySlotEligible(slot)) {
        ShowCampNoticeLine(Data().strings[0x901], 0, true, false);
        return;
    }
    if (!IsCampActionAllowed(slot)) {
        return;
    }
    AddItemToCharacter(CurrentGame(), PartyMember(CurrentGame(), slot),
                       PartyItem(CurrentGame().party, {W8ItemStorage::Hand}), false, false, false);
}

bool CanSelectRcsPartySlot(int ui_slot)
{
    assert(CurrentGame().party.members[ui_slot].has_value());

    W8Character* character = &CurrentGame().party.characters[ui_slot];
    if (character->uiCondition[W8_CONDITION_MISSING] != 0) {
        return false;
    }
    if (character->uiCondition[W8_CONDITION_WEBBED] != 0) {
        return false;
    }
    if (character->uiCondition[W8_CONDITION_INSANE] != 0) {
        return false;
    }
    if (character->uiCondition[W8_CONDITION_TURNCOAT] != 0) {
        return false;
    }
    if (CurrentGame().combat.has_value()) {
        if (CurrentGame().combat->equip_phase == W8CombatEquipPhase::action_selection) {
            return false;
        }
        if (CurrentGame().party.rows[ui_slot].executing.kind != W8_ACTION_EQUIP) {
            return false;
        }
    }
    return true;
}

void DrawRcsText(std::string_view text, int left, int top, int width, unsigned int layout_mode)
{
    W8ControlsRect bounds = {left, top, left + width, top + 12};
    W8TextBuffer buffer(&bounds, text, g_wiz_text_font_secondary, layout_mode, 4);
    buffer.RenderToTarget(0, false, GetFrameBufferSurface());
}

void DrawRcsBoldText(std::string_view text, int left, int top, int width, unsigned int layout_mode)
{
    W8ControlsRect bounds = {left, top, left + width, top + 12};
    W8TextBuffer buffer(&bounds, text, g_wiz_text_bold_font, layout_mode, 4);
    buffer.RenderToTarget(0, false, GetFrameBufferSurface());
}

void DrawTallRcsText(std::string_view text, int left, int top, int width, unsigned int layout_mode)
{
    W8ControlsRect bounds = {left, top, left + width, top + 18};
    W8TextBuffer buffer(&bounds, text, g_wiz_text_font_secondary, layout_mode, 4);
    buffer.RenderToTarget(0, false, GetFrameBufferSurface());
}

/* Draws text honoring the same layout mask pairs as the buffered variants
   above, through gprintfDirty with the current font: the centered and right
   masks shift the start by the measured string length and the baseline is
   centered on the caller-provided height. */
void DrawRcsTextJustified(std::string_view text, int left, int top, int width, int height,
                          unsigned int layout_mode)
{
    if (layout_mode == (g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter)) {
        left += (width - g_wiz_text_font_secondary.textWidth(text)) / 2;
    } else if (layout_mode == (g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle)) {
        left += width - g_wiz_text_font_secondary.textWidth(text);
    }
    top += (height - g_wiz_text_font_secondary.height()) / 2;
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left, top}, text);
}

// FUNCTION: WIZ8 0x005b6630
void CampScreen::OpenLevelUpCharacterScreen()
{
    assert(CurrentGame().party.members[ReviewedPartySlot()].has_value());
    RequestScreen({.id = W8_SCREEN_CHARACTER,
                   .mode = W8_CHARACTER_LEVEL_UP,
                   .character = &CurrentGame().party.characters[ReviewedPartySlot()]});
}

UiResult CampScreen::HandleCampDismissPortrait(const UiPointerEvent& event)
{
    if (event.action != UiPointerAction::Up || event.captured_buttons)
        PlayButtonSound(event.action);
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave) {
        portrait_hovered = event.action == UiPointerAction::Enter;
        header_redraw.portrait = true;
    }
    if (event.action == UiPointerAction::Up && event.inside &&
        event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK))
        AfterUiDispatch([screen = this] { screen->DismissSelectedPartyCharacter(); });
    return UiResult::Handled;
}

UiResult CampScreen::HandleCampPortrait(const UiPointerEvent& event, int target_slot)
{
    if (event.action != UiPointerAction::Up || event.captured_buttons)
        PlayButtonSound(event.action);
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave) {
        header_redraw.party_slots.set(target_slot);
        if ((items.action() == W8_CAMP_ITEM_ACTION_CAST_SPELL ||
             items.action() == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER) &&
            CurrentGame().party.characters[target_slot].uiCondition[W8_CONDITION_MISSING] == 0)
            items.UpdateItemCursorForState(event.action == UiPointerAction::Enter, nullptr,
                                           target_slot);
    }
    if (event.action != UiPointerAction::Up || !event.inside) return UiResult::Handled;
    if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK) &&
        (CurrentGame().party.held.item.iItemNo != -1))
        AfterUiDispatch([target_slot] { TryGiveHeldItemToCampPortrait(target_slot); });
    if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK) &&
        CurrentGame().party.members[target_slot].has_value()) {
        AfterUiDispatch([this, target_slot] {
            if (ScreenPointer().targeting() == W8_TARGET_NEED_ALLY) {
                if (page != W8_CAMP_PAGE_ITEMS ||
                    (items.action() != W8_CAMP_ITEM_ACTION_CAST_SPELL &&
                     items.action() != W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER)) {
                    if (!CanPartySlotParticipate(target_slot)) {
                        QueueCharacterEvent(&CurrentGame().party.characters[ReviewedPartySlot()],
                                            0x35, 0, kCharacterEventFullVolume);
                        return;
                    }
                    AimAtTarget(ReviewedPartySlot(), CharacterTarget(target_slot));
                    StartBreathCycle(ReviewedPartySlot(), false);
                    return;
                }
                if (CurrentGame().party.characters[target_slot].uiCondition[W8_CONDITION_MISSING] ==
                    0) {
                    if (items.action() != W8_CAMP_ITEM_ACTION_CAST_SPELL) {
                        items.TargetCharacterWithHeldItem(target_slot);
                        return;
                    }
                    items.ReportCastResult(target_slot);
                    return;
                }
            } else {
                if (ScreenPointer().targeting() == W8_TARGET_NEED_CHARACTER_INDIRECT) {
                    if (!IsDeadCharacterTargetable(target_slot)) {
                        QueueCharacterEvent(&CurrentGame().party.characters[ReviewedPartySlot()],
                                            0x35, 0, kCharacterEventFullVolume);
                        return;
                    }
                    AimAtTarget(ReviewedPartySlot(),
                                CharacterTarget(target_slot, W8_TARGET_KIND_CHARACTER_INDIRECT));
                    StartBreathCycle(ReviewedPartySlot(), false);
                    return;
                }
                if (ReviewedPartySlot() != target_slot) {
                    SelectCampCharacter(target_slot);
                    return;
                }
            }
        });
    }
    return UiResult::Handled;
}

UiResult CampScreen::HandleCampCharacterHeader(const UiPointerEvent& event)
{
    if (event.action != UiPointerAction::Up || event.captured_buttons)
        PlayButtonSound(event.action);
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave)
        header_redraw.caption = true;
    if (event.action == UiPointerAction::Up && event.inside &&
        event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK)) {
        AfterUiDispatch([this] {
            RequestScreen(
                {.id = W8_SCREEN_CHARACTER, .mode = W8_CHARACTER_EDIT, .character = reviewed});
        });
    }
    return UiResult::Handled;
}

UiResult CampScreen::HandleCampProfessionHistory(const UiPointerEvent& event)
{
    if (event.action != UiPointerAction::Up || event.captured_buttons)
        PlayButtonSound(event.action);
    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave)
        header_redraw.caption = true;
    if (event.button == UiPointerButton::Left) {
        if (event.action == UiPointerAction::Down)
            SetCampHeaderMode(W8_CAMP_HEADER_PROFESSION_HISTORY);
        else if (event.action == UiPointerAction::Up && (event.captured_buttons & SDL_BUTTON_LMASK))
            SetCampHeaderMode(W8_CAMP_HEADER_SUMMARY);
        InvalidateHeader();
    }
    return UiResult::Handled;
}

void CampScreen::CreateCampHeaderButtons()
{
    const auto button = [this](int art, SDL_Rect bounds, int pressed_hover, int help,
                               std::function<void()> action) {
        auto result = std::make_unique<W8ButtonWidget>(header_buttons.input);
        result->Configure(ButtonGraphics::Load(CatalogArtwork::Load(art, 0).spriteSheet(), 3, 0, 1,
                                               2, pressed_hover),
                          {.on_click = [action](auto*) { AfterUiDispatch(action); },
                           .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = help,
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});

        result->SetPosition(bounds.x, bounds.y);
        result->Resize(bounds.w, bounds.h);
        result->SetVisible(false);
        return result;
    };
    header_buttons.level_up =
        button(0xa7, {0xe9, 0x3f, 18, 18}, 4, 1984, [this] { OpenLevelUpCharacterScreen(); });
    header_buttons.dismiss =
        button(0x113, {0xa7, 0x41, 23, 16}, 2, 2387, [this] { ShowDismissCharacterDialog(); });
}

void CampScreen::RedrawRcsLevelUpPanel()
{
    header_buttons.level_up->m_dirty = true;
}
void CampScreen::RedrawRcsDismissPanel()
{
    header_buttons.dismiss->m_dirty = true;
}

// FUNCTION: WIZ8 0x005b65a0
void CampScreen::UpdateRcsLevelUpPanel(void)
{
    bool enabled = IsCharacterReadyToAdvance(ReviewedPartySlot());
    if (!enabled || CurrentGame().combat.has_value() ||
        (!CurrentGame().party.members[ReviewedPartySlot()]->level_up_announced && GameStarted()) ||
        CurrentGame().activity.conversation_aside) {
        if (header_buttons.level_up->IsVisible()) {
            header_buttons.level_up->SetVisible(false);
        }
    } else if (!header_buttons.level_up->IsVisible()) {
        header_buttons.level_up->SetVisible(true);
        RedrawRcsLevelUpPanel();
    }
    header_buttons.level_up->Refresh();
}

// FUNCTION: WIZ8 0x005b6950
void CampScreen::ShowDismissCharacterDialog()
{
    assert(CurrentGame().party.members[ReviewedPartySlot()].has_value());

    auto dialog = CreateMessageDialog();
    dialog->SetClientExtent(0xfa, 200);

    W8Character* character = &CurrentGame().party.characters[ReviewedPartySlot()];
    std::string format;
    if (character->uiCondition[W8_CONDITION_MISSING] == 0) {
        if (character->uiCondition[W8_CONDITION_DEAD] == 0) {
            format = Data().strings[0x92d];
        } else {
            format = Data().strings[0x92e];
        }
    } else {
        format = Data().strings[0x92f];
    }
    dialog->SetMessage(fmt::sprintf(format, character->name), 1, 0x32, true, true, true, true, 0,
                       0x15e);
    dialog->on_complete = [this](bool accepted) { OnDismissCharacterDialogClosed(accepted); };
    dialogs.Display(std::move(dialog));
}

// FUNCTION: WIZ8 0x005b6a60
void CampScreen::OnDismissCharacterDialogClosed(bool accepted)
{
    if (accepted && CurrentGame().party.npcIndex(ReviewedPartySlot()) != -1) {
        if (auto* main_game = MainGame()) main_game->dismissed_npc_slot = ReviewedPartySlot();
        DismissSelectedPartyCharacter();
    }
}

/* Bring the second panel up to date. Its widget is available only for the first
   two party slots, out of combat and out of camp; enabling it also invalidates the
   panel, disabling it does not. Either way the panel is then updated. */
// FUNCTION: WIZ8 0x005b68e0
void CampScreen::UpdateRcsDismissPanel(void)
{
    if ((ReviewedPartySlot() == 0 || ReviewedPartySlot() == 1) &&
        !CurrentGame().combat.has_value() && !CurrentGame().activity.conversation_aside) {
        if (!header_buttons.dismiss->IsVisible()) {
            header_buttons.dismiss->SetVisible(true);
            RedrawRcsDismissPanel();
        }
    } else if (header_buttons.dismiss->IsVisible()) {
        header_buttons.dismiss->SetVisible(false);
    }
    header_buttons.dismiss->Refresh();
}

/* Release the formation board, compass and overlay sprites. */
void ReleaseFormationBoard(void)
{
    if (MainGameHud()->formation_board_sprite != 0) {
        MainGameHud()->formation_board_sprite.reset();
        MainGameHud()->formation_board_sprite = 0;
    }
    if (MainGameHud()->formation_compass_sprite != 0) {
        MainGameHud()->formation_compass_sprite.reset();
        MainGameHud()->formation_compass_sprite = 0;
    }
    if (MainGameHud()->formation_overlay_sprite != 0) {
        MainGameHud()->formation_overlay_sprite.reset();
        MainGameHud()->formation_overlay_sprite = 0;
    }
}

/* The upper-left character block. While an input mode is up it lists the
   reviewed character's professions and their levels instead of the portrait;
   otherwise it repaints the portrait and vitals, the name/gender/profession
   caption, and the eight-slot party strip. */
// FUNCTION: WIZ8 0x005b4000
void CampScreen::DrawCampHeader(void)
{
    std::string text;
    W8Character* character = reviewed;
    unsigned int row;
    unsigned int slot;
    int profession;
    int name_x;
    int level_x;
    int y;
    int left;
    int top;
    int band_x;
    int band_y;
    int image;
    int sub_image;

    if (header_mode != W8_CAMP_HEADER_SUMMARY) {
        if (full_redraw || header_redraw.any()) {
            InvalidateRegion(0, 0, 0x136, 0xa5);
            const SDL_Rect bounds{0, 0, 0x136, 0xa5};
            SDL_FillSurfaceRect(&GetFrameBufferSurface(), &bounds, 0x8000);
            CatalogArtwork::Load(0x123, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
            DrawRcsText(Data().strings[0x8c2], 0xb, 0xd, 0x11e,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
            row = 0;
            for (profession = 0; profession < W8_PROFESSION_COUNT; ++profession) {
                if (character->profession_levels[profession] != 0 ||
                    character->iProfession == profession) {
                    if ((row & ~7u) == 0) {
                        name_x = 0xf;
                        level_x = 0x78;
                    } else {
                        name_x = 0xa1;
                        level_x = 0x10a;
                    }
                    y = (row & 7) * 0xe + 0x24;
                    DrawRcsText(Data().strings[g_profession_name_message_ids[profession]], name_x,
                                y, 0x68, g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
                    text = fmt::format("{:d}", character->profession_levels[profession]);
                    DrawRcsText(text, level_x, y, 0x20,
                                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
                    ++row;
                }
            }
        }
        return;
    }

    if (full_redraw || header_redraw.background) {
        InvalidateRegion(0, 0, 0x136, 0xa5);
        const SDL_Rect bounds{0, 0, 0x136, 0xa5};
        SDL_FillSurfaceRect(&GetFrameBufferSurface(), &bounds, 0x8000);
        CatalogArtwork::Load(0x10f, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
    }
    if (full_redraw || header_redraw.portrait) {
        if (character->hp_current == 0) {
            CatalogArtwork::Load(g_dead_portrait_catalog_ids[character->iRace][1], 0)
                .draw(GetFrameBufferSurface(), 0, 0xa4, 0xc);
        } else {
            RenderPartyPortrait(GetFrameBufferSurface(), character->portrait_index, 0xa4, 0xc, {},
                                1, ReviewedPartySlot());
        }
        CatalogArtwork::Load(0x10f, 0).draw(GetFrameBufferSurface(), 9, 0xa4, 0xc);
        if (portrait_hovered != 0) {
            CatalogArtwork::Load(0x116, 0).draw(GetFrameBufferSurface(), 0, 0xa4, 0xc);
        }
        if (!CurrentGame().combat.has_value()) {
            RedrawRcsLevelUpPanel();
            RedrawRcsDismissPanel();
        }
        if (character->highest_condition == W8_CONDITION_NONE) {
            CatalogArtwork::Load(0x2f, 0).draw(GetFrameBufferSurface(), 0, 0xa5, 0xe);
        } else {
            CatalogArtwork::Load(character->highest_condition + 0xb6, 0)
                .draw(GetFrameBufferSurface(), 0, 0xa5, 0xe);
            CatalogArtwork::Load(0x61, 0).draw(GetFrameBufferSurface(), 0, 0xa4, 0xd);
        }
        if (character->enchantment_top == W8_ENCHANTMENT_NONE) {
            CatalogArtwork::Load(0x30, 0).draw(GetFrameBufferSurface(), 0, 0xee, 0xe);
        } else {
            CatalogArtwork::Load(character->enchantment_top + 0xc9, 0)
                .draw(GetFrameBufferSurface(), 0, 0xee, 0xe);
            CatalogArtwork::Load(0x61, 0).draw(GetFrameBufferSurface(), 0, 0xed, 0xd);
        }
        InvalidateRegion(0xa4, 0xc, 0xfe, 0x54);
        DrawCampVitals();
        DrawCampHands();
    }
    if (full_redraw || header_redraw.caption) {
        g_wiz_text_bold_font.setShade(Font::kNormalShade);
        if (main_targets[kCampCharacterHeaderRegionOffset]->hovered()) {
            g_wiz_text_bold_font.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        }
        text = character->name;
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_bold_font,
                              {(0xba - g_wiz_text_bold_font.textWidth(text)) / 2 + 0x74, 0x60},
                              text);
        g_wiz_text_bold_font.resetPalette();
        g_wiz_text_font_secondary.setShade(Font::kNormalShade);
        text = Data().strings[g_gender_name_message_rows[character->gender][0]];
        text += " ";
        text += Data().strings[g_race_name_message_ids[character->iRace]];
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                              {(0xba - g_wiz_text_font_secondary.textWidth(text)) / 2 + 0x74, 0x6f},
                              text);
        if (main_targets[10]->hovered()) {
            g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        }
        text = Data().strings[g_profession_name_message_ids[character->iProfession]];
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                              {(0xba - g_wiz_text_font_secondary.textWidth(text)) / 2 + 0x74, 0x7c},
                              text);
        g_wiz_text_font_secondary.resetPalette();
        g_wiz_text_font_secondary.setShade(Font::kNormalShade);
        text =
            fmt::format("{} {:d} ({})", Data().strings[0x91a], character->uiExpLevel,
                        Data().strings[g_profession_level_name_message_ids[character->iProfession]
                                                                          [character->level_band]]);
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                              {(0xba - g_wiz_text_font_secondary.textWidth(text)) / 2 + 0x74, 0x89},
                              text);
    }
    for (slot = 0; slot < 8; ++slot) {
        if (full_redraw || header_redraw.party_slots[slot]) {
            band_y = (slot >> 1) * 0x27;
            band_x = (slot & 1) * 0x30;
            left = band_x + 6;
            top = band_y + 5;
            if (ReviewedPartySlot() == -1 || !CurrentGame().party.members[slot].has_value()) {
                CatalogArtwork::Load(0x10f, 0).draw(GetFrameBufferSurface(), slot + 1, band_x + 5,
                                                    band_y + 4);
            } else {
                W8Character* member = &CurrentGame().party.characters[slot];
                if (member->hp_current == 0) {
                    image = g_dead_portrait_catalog_ids[member->iRace][0];
                    sub_image = 0;
                } else {
                    sub_image = member->portrait_index;
                    image = 0x13;
                }
                CatalogArtwork::Load(image, sub_image).draw(GetFrameBufferSurface(), 0, left, top);
                if (!CanSelectRcsPartySlot(slot)) {
                    const SDL_Rect bounds{left, top, band_x + 0x32 - left + 1,
                                          band_y + 0x28 - top + 1};
                    ShadowRect(GetFrameBufferSurface(), bounds);
                    ShadowRect(GetFrameBufferSurface(), bounds);
                }
                if (main_targets[kCampPartyPortraitRegionOffset + slot]->hovered()) {
                    CatalogArtwork::Load(0x117, 0).draw(GetFrameBufferSurface(), 0, left, top);
                } else if (static_cast<int>(slot) == ReviewedPartySlot()) {
                    CatalogArtwork::Load(0x117, 0).draw(GetFrameBufferSurface(), 1, left, top);
                }
            }
            InvalidateRegion(left, top, band_x + 0x33, band_y + 0x29);
        }
    }
}

/* The health/stamina/spell bars beside the portrait: the three frame images,
   the shaded depletion on each, the current-profession label and, under the
   numeric-hit-points option, the raw hit-point value. */
// FUNCTION: WIZ8 0x005b4790
void CampScreen::DrawCampVitals(void)
{
    int image;
    int numeric;
    int frame_column;
    int hp_row;
    int stamina_row;
    int spell_column;
    int hp_frame;
    int stamina_frame;
    int spell_frame;
    int bar_top;
    unsigned int hp_fill;
    unsigned int stamina_fill;
    unsigned int spell_fill;
    int spell_left;
    unsigned int gap;
    W8ControlsRect bounds;
    W8TextBuffer text;
    std::string formatted;

    if (!g_settings.options.numeric_hit_points) {
        image = 1;
    } else {
        image = 3;
    }
    CatalogArtwork::Load(0x80, 0).draw(GetFrameBufferSurface(), image, 0x10c, 0xb);
    if (reviewed->hp_current == 0) {
        return;
    }
    if (!g_settings.options.numeric_hit_points) {
        frame_column = 0x17;
        numeric = 0;
        hp_row = 6;
        stamina_row = 0xa;
        spell_column = 0xe;
        hp_frame = 0x4f;
        stamina_frame = 0x50;
        spell_frame = 0x51;
    } else {
        frame_column = 0x11;
        numeric = 1;
        hp_row = 3;
        stamina_row = 9;
        spell_column = 0xf;
        hp_frame = 0x52;
        stamina_frame = 0x53;
        spell_frame = 0x54;
    }
    assert(reviewed->uiHPMax != 0);
    hp_fill = reviewed->hp_current * 0x2d / reviewed->uiHPMax;
    assert(reviewed->uiStaminaMax != 0);
    stamina_fill = (reviewed->stamina < 0 ? 0 : reviewed->stamina) * 0x2d / reviewed->uiStaminaMax;
    if (SumCharacterSpellPoints(reviewed) == 0) {
        spell_fill = 0;
    } else {
        spell_left = SumCharacterSpellPointsLeft(reviewed);
        if (spell_left < 0) {
            spell_left = 0;
        } else {
            spell_left = SumCharacterSpellPointsLeft(reviewed);
        }
        spell_fill = spell_left * 0x2d / SumCharacterSpellPoints(reviewed);
    }
    bar_top = frame_column + 0xb;
    CatalogArtwork::Load(hp_frame, 0).draw(GetFrameBufferSurface(), 0, hp_row + 0x10c, bar_top);
    gap = 0x2d - hp_fill;
    if (gap != 0) {
        ShadeStatusBarGap(gap, hp_row + 0x10c, frame_column - numeric + 0xb);
    }
    CatalogArtwork::Load(stamina_frame, 0)
        .draw(GetFrameBufferSurface(), 0, stamina_row + 0x10c, bar_top);
    gap = 0x2d - stamina_fill;
    if (gap != 0) {
        ShadeStatusBarGap(gap, stamina_row + 0x10c, frame_column - numeric + 0xb);
    }
    if (SumCharacterSpellPoints(reviewed) != 0) {
        CatalogArtwork::Load(spell_frame, 0)
            .draw(GetFrameBufferSurface(), 0, spell_column + 0x10c, bar_top);
        gap = 0x2d - spell_fill;
        if (gap != 0) {
            ShadeStatusBarGap(gap, spell_column + 0x10c, frame_column - numeric + 0xb);
        }
    }
    bounds.left = 0x10e;
    bounds.right = 0x121;
    bounds.top = 0xd;
    bounds.bottom = 0x17;
    text.SetClipBounds(bounds);
    text.SetLayoutBounds(&bounds, true);
    text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    if (!GameStarted()) {
        text.SetFontStateIndex(8);
    } else {
        text.SetFontStateIndex(CurrentGame().party.orderIndexOf(ReviewedPartySlot()));
    }
    text.RenderString(Data().strings[g_profession_name_message_ids[reviewed->iProfession + 0x10]],
                      g_smfnt_font, false, GetFrameBufferSurface());
    if (g_settings.options.numeric_hit_points) {
        bounds.left = 0x10e;
        bounds.right = 0x120;
        bounds.top = 0x49;
        bounds.bottom = 0x51;
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, false);
        text.SetFontStateIndex(-1);
        formatted = fmt::format("{:d}", reviewed->hp_current);
        text.RenderString(formatted, g_smfnt_font, false, GetFrameBufferSurface());
    }
    InvalidateRegion(0x10c, bar_top, 0x124, frame_column + 0x38);
}

/* Each hand uses the same equipment icon and stack-count rendering. The
   caller supplies the two layouts and decides whether the off-hand is visible. */
void CampScreen::DrawCampHand(W8EquipSlot slot, int hand_index, int top, int count_center)
{
    int item_id = reviewed->EquippedItem[slot].iItemNo;
    if (item_id == -1) {
        CatalogArtwork::Load(g_empty_hand_catalog_ids[reviewed->iRace * W8_RACE_DWARF + hand_index],
                             0)
            .draw(GetFrameBufferSurface(), 0, 0x85, top);
    } else {
        DrawSprite(GetFrameBufferSurface(), *ItemSprites(item_id), 2, 0x84, top);
        unsigned char count = reviewed->EquippedItem[slot].stack_count;
        if (count != 0) {
            std::string text;
            text = fmt::format("{:d}", count);
            g_smfnt_font.resetPalette();
            DrawText(GetFrameBufferSurface(), g_smfnt_font,
                     {count_center - (g_smfnt_font.textWidth(text) + 1) / 2, top + 0xf}, text);
        }
    }
}

/* The two hands under the vitals: each draws its equipment icon (or the
   race's empty-hand image), the stack count when the item has one, and the
   armor-class line under the left hand in the load-category palette. The
   off-hand is skipped while a two-handed item fills the primary slot. */
// FUNCTION: WIZ8 0x005b4bd0
void CampScreen::DrawCampHands(void)
{
    bool two_handed;
    int item_id;
    int hand_image;
    std::string text;
    Palette16 palette;

    item_id = reviewed->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
    if (item_id == -1 || (Data().items[item_id].flags & W8_ITEM_FLAG_TWO_HANDED) == 0) {
        two_handed = false;
        hand_image = 0;
    } else {
        two_handed = true;
        hand_image = 2;
    }
    CatalogArtwork::Load(0x80, 0).draw(GetFrameBufferSurface(), hand_image, 0x81, 0xb);
    DrawCampHand(W8_EQUIP_SLOT_PRIMARY_WEAPON, 0, 0x22, 0x89);
    if (!two_handed) {
        DrawCampHand(W8_EQUIP_SLOT_SECONDARY_WEAPON, 1, 0x3a, 0x8c);
    }
    text = fmt::format("{:d}", reviewed->armor_class_average);
    palette = g_smfnt_font.glyphs->colors;
    if (reviewed->load_category != 0) {
        palette = g_font_state_palettes[g_load_category_palettes[reviewed->load_category]];
    }
    g_smfnt_font.setPalette(palette);
    DrawText(GetFrameBufferSurface(), g_smfnt_font,
             {(0xd - g_smfnt_font.textWidth(text)) / 2 + 0x84, 0xe}, text);
    InvalidateRegion(0x81, 0xb, 0x95, 0x53);
}

/* The page buttons and the exit button on the bottom panel. */
// FUNCTION: WIZ8 0x005b4eb0
void CampScreen::CreateCampButtonPanel()
{
    const auto button = [this](int artwork, int frame, int x, int help, bool page,
                               std::function<void()> action) {
        auto result = std::make_unique<W8ButtonWidget>(page_buttons.input);
        result->Configure(ButtonGraphics::Load(CatalogArtwork::Load(artwork, 0).spriteSheet(),
                                               frame + 3, frame, frame + 1,
                                               page ? frame + 4 : frame + 2,
                                               page ? frame + 2 : frame + 4),
                          {.on_click = [action](auto*) { AfterUiDispatch(action); },
                           .policy = {.primary_toggle = ButtonToggle::Release,
                                      .input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = help,
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});

        result->SetPosition(x, 0x1c2);
        result->Resize(44, 30);
        return result;
    };
    constexpr W8CampPage pages[] = {W8_CAMP_PAGE_ITEMS, W8_CAMP_PAGE_SPELLS, W8_CAMP_PAGE_SKILLS,
                                    W8_CAMP_PAGE_STATS};
    constexpr int frames[] = {0, 10, 15, 5};
    constexpr int help[] = {2364, 2367, 2366, 2365};
    for (int index = 0; index < 4; ++index)
        page_buttons.pages[index] =
            button(0x111, frames[index], 0x1a4 + index * 44, help[index], true,
                   [this, index, page = pages[index]] { SelectCampPageButton(index, page); });
    auto& exit = page_buttons.pages[4];
    exit = button(0x106, 16, 0x254, 2368, false, [this] { DismissSelectedPartyCharacter(); });
    exit->Policy().primary_toggle = ButtonToggle::None;
    exit->SetImage(
        ButtonGraphics::Load(CatalogArtwork::Load(0x106, 0).spriteSheet(), 19, 16, 17, -1, 18));
    page_buttons.pages[0]->SetPressed(true);
}

/* The bottom panel: its backdrop under the page buttons and the items page's
   action buttons, repainted whole when any of them changed. */
// FUNCTION: WIZ8 0x005b5670
void CampScreen::DrawButtonPanel(bool invalidate)
{
    items.UpdateActionButtons(page == W8_CAMP_PAGE_ITEMS);
    button_panel_dirty |= invalidate;
    for (const auto& button : page_buttons.pages) button_panel_dirty |= button->m_dirty;
    button_panel_dirty |= items.ActionButtonsDirty();
    if (!button_panel_dirty) return;
    CatalogArtwork::Load(0x110, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0x1c2);
    for (const auto& button : page_buttons.pages) {
        button->m_dirty = true;
        button->Refresh();
    }
    items.RefreshActionButtons();
    button_panel_dirty = false;
}

void CampScreen::InvalidateHeader()
{
    header_redraw.background = true;
    header_redraw.caption = true;
    header_redraw.portrait = true;
    header_redraw.party_slots.set();
}

void CampScreen::RefreshPartySlot(int party_slot)
{
    if (ReviewedPartySlot() != party_slot || header_mode != W8_CAMP_HEADER_SUMMARY) return;
    InvalidatePortrait();
    if (!portrait_hovered) {
        RedrawRcsLevelUpPanel();
        RedrawRcsDismissPanel();
    }
}

void CampScreen::RefreshPartyLoad()
{
    items.InvalidateCharacterInfo();
    InvalidatePortrait();
}

/* A page button's click: while its secondary state is up, clear it on every
   page button and switch to the button's page; then raise the pressed
   button's secondary state. */
void CampScreen::SelectCampPageButton(int button, W8CampPage page)
{
    if (page_buttons.pages[button]->IsPressed()) {
        for (int index = 0; index < 5; ++index) {
            if (page_buttons.pages[index]->IsPressed()) {
                page_buttons.pages[index]->SetPressed(false);
            }
        }
        SwitchCampPage(page);
    }
    page_buttons.pages[button]->SetPressed(true);
}
