#include "wiz8/local_screens/NPCInteractionSubscreen.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "glm/ext/vector_float3.hpp"
#include "message_box.h"
#include "mgs_commands.h"
#include "npc_trade_price.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/image.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/cursor.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/NpcDialog.h"
#include "wiz8/dialog_code/PortraitQuote.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/message_log.h"
#include "wiz8/modal_content.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/npc_items.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/screen.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"

constexpr int kSplitDialogOriginX = 0x9f;
constexpr int kSplitDialogOriginY = 0xb8;

namespace {
// The typed-dialogue panel's keyword field, and the field scripted NPC dialogs type into.
std::optional<TextFieldGroup> g_npc_text_fields;

std::string NpcFieldText()
{
    auto* fields = TextFieldGroup::Focused();
    return fields ? fields->Text(0) : std::string{};
}

void SetNpcFieldText(std::string_view text)
{
    if (auto* fields = TextFieldGroup::Focused()) fields->SetText(0, text);
}

bool NpcEditingText()
{
    auto* fields = TextFieldGroup::Focused();
    return fields && fields->Editing();
}
} // namespace

static std::unique_ptr<UiLayer> npc_quote_input;

void ShowNpcQuoteBubbleRegion(int x1, int y1, int x2, int y2)
{
    npc_quote_input = std::make_unique<UiLayer>(UiLayerOrder::NpcQuoteBubble);
    npc_quote_input->add(
        SDL_Rect{x1, y1, x2 - x1 + 1, y2 - y1 + 1}, [](const UiPointerEvent& event) {
            if (event.action != UiPointerAction::Up || event.captured_buttons)
                PlayButtonSound(event.action);
            if (event.action == UiPointerAction::Down && event.button == UiPointerButton::Left)
                AfterUiDispatch([] { TryFinishNpcVoicePlayback(CurrentGame().status, true); });
            return UiResult::Handled;
        });
}

void HideNpcQuoteBubbleRegion()
{
    npc_quote_input.reset();
}

static void ToggleNpcTradeCategory(int button, W8NpcTradeFilter filter);
static bool OpenNpcDialoguePanel(W8NpcState* npc, W8ItemInstance* item, bool force);
static unsigned char HandleNpcDialogueItem(W8ItemInstance* item);
static void HandleNpcDialogueDeparture(unsigned char value);
static void CreateNpcDialogueControls(void);
static void ToggleNpcCharacterUsabilityFilter(void);
static void ToggleNpcPartyUsabilityFilter(void);
static void SubmitNpcDialogueInput(void);
static void SubmitNpcWhereIsQuery(void);
static void BackOutNpcDialogue(void);
static void OpenNpcDialogueMode1Layout(void);
static void OpenNpcDialogueOptionLayout(void);
static void OpenNpcDialogueMode5Layout(void);
static void SetNpcDialogueHidden(signed char value);
static void CloseNpcDialogueMode1Layout(void);
static void CloseNpcDialogueTranscriptLayout(void);
static void CloseNpcDialogueMode5Layout(void);
static void SetNpcDialogueLayoutMode(W8NpcDialogueLayout value);
static void SelectNpcPartyItems(void);
static void ResolveNpcPickpocket(int party_slot);
static void NpcDialogueTextBoxDoubleClick(int x, int y);
static void NpcDialogueTextBoxLeftUp(int x, int y);
static void NpcDialogueTextBoxRightUp(int x, int y);
static W8ItemInstance* ResolveNpcTradeRow(int index, bool, signed char, signed char);
static W8ItemInstance* GetNpcTradeSlotItem(int index);
static void OnNpcDispositionPromptClosed(bool accepted);
static void SelectNpcDialogueService(void);
static void SelectNpcDialogueTalk(void);
static void SelectNpcDialogueExit(void);
static void EnterNpcTradeOptions(void);
static void ShowNpcDialogueNotice(void);
static void RequestNpcJoinParty(void);
static void ScrollNpcDialogueUp(void);
static void ScrollNpcDialogueDown(void);
static void SubmitNpcDialogueKeyword(void);
static void RefreshNpcDialogueTranscript(void);
static void SyncNpcDialogueListFilter(void);
static void SelectNpcPeopleTopics(void);
static void SelectNpcPlaceTopics(void);
static void SelectNpcItemTopics(void);
static void SelectNpcMiscTopics(void);
static void SelectNpcDialogueCategoryAll(void);
static void SelectNpcSellMode(void);
static void SelectNpcGiveMode(void);
static void SelectNpcShopliftMode(void);
static void SelectNpcStockItems(void);
static void OpenNpcItemAssay(void);
static void ConfirmNpcTradeSlot(void);
static void OpenNpcGoldAmountDialog(void);
static void RequestNpcSpellService3(void);
static void RequestNpcSpellService41(void);
static void RequestNpcCharacterService(void);
static void ConfirmNpcTradePurchase(void);
static bool AcceptNpcDialogueItem(W8NpcState* npc, W8ItemInstance* item, int mode);
void OnNpcDialogClosed(W8NpcDialog& dialog);

W8NpcInteractionState::W8NpcInteractionState() = default;
W8NpcInteractionState::~W8NpcInteractionState() = default;

/* MonotonicMilliseconds sample for the trade-item highlight timeout. */
static uint64_t g_trade_highlight_tick;

static int g_dialogue_place_keyword_count = 15;
static int g_dialogue_place_keyword_ids[15] = {0x751, 0x752, 0x753, 0x754, 0x755,
                                               0x756, 0x757, 0x758, 0x759, 0x75a,
                                               0x75b, 0x75c, 0x75d, 0x75e, 0x75f};
static int g_dialogue_fallback_ids0[5] = {0x760, 0x761, 0x762, 0x763, 0x764};
static int g_dialogue_fallback_ids1[5] = {0x765, 0x766, 0x767, 0x768, 0x769};
const char* g_dialogue_person_keywords[] = {"BALBRAK", "BILDUBLU", "EWAXX",  "KUNAR",
                                            "PANRACK", "RODAN",    "RUBBLE", "SAXX",
                                            "SPARKLE", "YAMIR",    ""};

NpcDialogueUi::~NpcDialogueUi() = default;

void SetNpcDialogueInputEnabled(int group, bool enabled)
{
    if (!CurrentGame().npc_interaction.ui) return;
    auto& ui = *CurrentGame().npc_interaction.ui;
    (group == 0   ? ui.transcript_input
     : group == 1 ? ui.trade_input
                  : ui.options_input)
        .setEnabled(enabled);
}

static void SetNpcActionVisible(int index, bool visible)
{
    auto& ui = *CurrentGame().npc_interaction.ui;
    auto& action = ui.actions[index];
    action.visible = visible;
    action.dirty = true;
    if (action.target) action.target->setEnabled(visible && ui.panels[action.panel].active);
}

static void SetNpcActionEnabled(int index, bool enabled)
{
    auto& action = CurrentGame().npc_interaction.ui->actions[index];
    if (action.enabled == enabled) return;
    action.enabled = enabled;
    action.dirty = true;
    if (action.frames[4] != -1) action.text.SetRenderMode(enabled ? 4 : 7);
}

static void SetNpcActionPressed(int index, bool selected)
{
    auto& action = CurrentGame().npc_interaction.ui->actions[index];
    if (action.policy.primary_toggle == ButtonToggle::None) return;
    action.interaction.pressed = action.interaction.selected = selected;
    action.dirty = true;
}

static void SetNpcActionHelp(int index, int message)
{
    auto* target = CurrentGame().npc_interaction.ui->actions[index].target;
    if (!target) return;
    if (message < 0 || !g_settings.tooltips_enabled)
        target->setTooltip({});
    else
        target->setTooltip([message] { return Data().strings[message]; });
}

static void SetNpcPanelActive(int index, bool active)
{
    auto& ui = *CurrentGame().npc_interaction.ui;
    auto& panel = ui.panels[index];
    const bool was_active = panel.active;
    panel.active = active;
    panel.dirty = true;
    for (int i = 0; i < 39; ++i) {
        if (ui.actions[i].panel != index) continue;
        SetNpcActionVisible(i, active && !(i >= 25 && i <= 30 &&
                                           CurrentGame().npc_interaction.dialogue_layout !=
                                               W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX));
    }
    if (index == 2) {
        ui.transcript->m_active = active;
        ui.transcript->input.setEnabled(active);
    }
    if (index == 6 && (!active || !was_active)) {
        if (active) {
            g_npc_text_fields.emplace(ui.options_input, g_wiz_text_mono_font);
            g_npc_text_fields->UseGameStyle();
            g_npc_text_fields->Add(0x1e5, 0x170, 0x7a, 0x12, 0x7f, "", 0xbe, 0xf, true);
        } else if (g_npc_text_fields)
            g_npc_text_fields->Remove(0);
    }
}

static void AddNpcAction(int index, int panel, SDL_Rect bounds, int catalog,
                         std::array<int, 5> frames)
{
    auto& ui = *CurrentGame().npc_interaction.ui;
    auto& action = ui.actions[index];
    bounds.x += ui.panels[panel].origin.x;
    bounds.y += ui.panels[panel].origin.y;
    action.panel = panel;
    action.bounds = bounds;
    action.frames = frames;
    if (catalog != -1) {
        const auto art = CatalogArtwork::Load(catalog, 0);
        action.sprites = art.spriteSheet();
        action.image_offset = art.imageOffset();
    }
    const bool checkbox = index == 15 || (index >= 17 && index <= 21);
    action.text.SetLayoutBounds(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
    action.text.MarkGeometryDirty(checkbox ? 9 : 10);
    if (checkbox) {
        const auto [width, height] =
            GetSpriteSize(*action.sprites, action.image_offset + frames[0]);
        action.text.SetLayoutBounds(bounds.x + width + 2, bounds.y, bounds.x + bounds.w,
                                    bounds.y + bounds.h);
        action.policy.primary_toggle =
            index == 15 ? ButtonToggle::Release : ButtonToggle::StayLatched;
    }
    if (index == 0 || index == 38) return; // Paint-only captions.
    auto& input = index >= 9 && index <= 23    ? ui.transcript_input
                  : index >= 24 && index <= 37 ? ui.trade_input
                                               : ui.options_input;
    action.target = &input.add(
        SDL_Rect{bounds.x, bounds.y, bounds.w + 1, bounds.h + 1},
        [index](const UiPointerEvent& event) {
            auto& ui = *CurrentGame().npc_interaction.ui;
            auto& action = ui.actions[index];
            if ((action.enabled) && (event.action != UiPointerAction::Up || event.captured_buttons))
                PlayButtonSound(event.action);
            if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave) {
                action.dirty = true;
                if (event.action == UiPointerAction::Leave) action.interaction.leave(action.policy);
                if (index == 9) {
                    auto& transcript = *ui.transcript;
                    if (event.action == UiPointerAction::Enter)
                        transcript.HighlightFirstEntry();
                    else
                        transcript.text_area.ClearSelection();
                    transcript.InvalidateLayout();
                }
                return UiResult::Handled;
            }
            if (!action.enabled) {

                return UiResult::Handled;
            }
            const auto button =
                event.button == UiPointerButton::Left ? SDL_BUTTON_LMASK : SDL_BUTTON_RMASK;
            if (event.action == UiPointerAction::Down || event.action == UiPointerAction::Repeat) {
                action.interaction.press(button, action.policy);
                action.dirty = true;
                if (event.button == UiPointerButton::Left && action.down)
                    AfterUiDispatch(action.down);
            } else if (event.action == UiPointerAction::Up) {
                if (action.interaction.release(button, action.policy).activate) {
                    const auto callback =
                        event.button == UiPointerButton::Left ? action.primary : action.secondary;
                    if (callback) AfterUiDispatch(callback);
                }
                action.dirty = true;
            }
            return UiResult::Handled;
        });
    action.target->setEnabled(false);
}

static bool NpcPanelDirty(int index)
{
    const auto& ui = *CurrentGame().npc_interaction.ui;
    if (ui.panels[index].dirty || (index == 2 && ui.transcript->m_dirty)) return true;
    return std::ranges::any_of(ui.actions, [index](const auto& action) {
        return action.panel == index && action.visible &&
               (action.dirty || action.text.m_geometryDirty);
    });
}

static void PaintNpcPanel(int index, SDL_Surface& target, bool force)
{
    auto& ui = *CurrentGame().npc_interaction.ui;
    auto& panel = ui.panels[index];
    if (!panel.active) return;
    force |= NpcPanelDirty(index);
    if (index == 2)
        ui.transcript->Redraw(target, force);
    else if (force) {
        const int frame = index == 0 && CurrentGame().npc_interaction.dialogue_layout ==
                                            W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX
                              ? 17
                              : panel.background;
        CatalogArtwork::Load(0x1a9, 0).drawAndInvalidate(target, frame, panel.origin.x,
                                                         panel.origin.y);
        if (index == 6)
            CatalogArtwork::Load(0x1a9, 0).draw(
                target, 16, 0x1df, CurrentGame().npc_interaction.where_is_query ? 0x19b : 0x18b);
    }
    if (!force) return;
    for (int id = 0; id < 39; ++id) {
        auto& action = ui.actions[id];
        if (action.panel != index || !action.visible) continue;
        const bool checkbox = id == 15 || (id >= 17 && id <= 21);
        const bool hover = action.target && action.target->hovered();
        const bool pressed = action.interaction.pressed;
        int frame = !action.enabled ? action.frames[4] : action.frames[pressed ? 1 : 0];
        if (action.enabled && hover && action.frames[pressed ? 3 : 2] != -1)
            frame = action.frames[pressed ? 3 : 2];
        const auto& sprites = id == 24 ? ui.item_preview : action.sprites;
        if (id == 24 && sprites) frame = 0;
        const auto bounds = action.bounds;
        if (sprites && frame != -1) {
            const int measured = id == 24                 ? 0
                                 : action.frames[0] != -1 ? action.frames[0]
                                                          : action.frames[1];
            const auto [width, height] =
                measured < 0 ? std::pair{bounds.w, bounds.h}
                             : GetSpriteSize(*sprites, action.image_offset + measured);
            int x = checkbox || id == 24 ? bounds.x : bounds.x + bounds.w - width;
            int y = id == 24 ? bounds.y : bounds.y + (bounds.h - height) / 2;
            if (hover) {
                const auto [w, h] = GetSpriteSize(*sprites, action.image_offset + frame);
                x += std::max(0, w - bounds.w) / 2;
                y += std::max(0, h - bounds.h) / 2;
            }
            DrawSpriteAndInvalidate(target, *sprites, action.image_offset + frame, x, y);
        }
        action.text.RenderToTarget(pressed && !checkbox ? 1 : 0, true, target);
        action.text.m_geometryDirty = false;
        if (!action.enabled && sprites && frame == -1)
            ShadowRect(target, {bounds.x, bounds.y, bounds.w + 1, bounds.h + 1});
        action.dirty = false;
    }
    panel.dirty = false;
}

static void DisableNpcTradeActions()
{
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_5, false);
}

std::string TranslateDialogueKeyword(std::string_view source)
{
    const auto& [english, translated] = Data().keywords;
    for (std::size_t row = 0; row < translated.size(); ++row)
        for (std::size_t word = 0; word < translated[row].size(); ++word)
            if (CompareTextIgnoreAsciiCase(source, translated[row][word]) == 0 &&
                row < english.size() && word < english[row].size())
                return english[row][word];
    return std::string(source);
}

/* Forward a monster-script notice to the targeting layer unless the screen is
   busy or this NPC kind suppresses it. */
void ForwardNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, int line, bool suppress)
{
    if (!CurrentGame().activity.conversation && !CurrentGame().combat.has_value() &&
        (npc->record->kind != 7 || GetFact(W8_FACT_ARNIKA_MYLES_MEET_ONCE) != 1)) {
        QueueNpcScriptNotice(npc, item, line, suppress, 0);
    }
}

/* Queue one NPC script notice for the dispatch pass. A second NPC of the same
   kind already in the world suppresses the notice unless the record carries
   the 0x054 binding flag, a live monster with a condition at or above 0xf
   takes none, and a pending notice blocks the next until it drains. Kind
   0x10/0x11 NPCs with fact 0xbf substitute their own notice line and raise
   the flag byte. */
void QueueNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, int line, bool suppress,
                          unsigned char arg)
{
    W8MonsterInfo* info;
    bool flag;

    if (FindNpcOfKind(npc->name_style) != 0 && npc->record->monster_bound == 0) {
        return;
    }
    info = GetNpcMonsterInfo(npc);
    if (info != 0 && info->highest_condition >= W8_CONDITION_ASLEEP) {
        return;
    }
    flag = suppress;
    if ((npc->name_style == W8_NPC_DRAZIC || npc->name_style == W8_NPC_RODAN) &&
        GetFact(W8_FACT_PEACE_ACHIEVED) != 0) {
        flag = true;
        line = npc->name_style == W8_NPC_DRAZIC ? 0x23 : 0x1d;
    }
    if (CurrentGame().npc_interaction.pending_notice_queued) {
        return;
    }
    CurrentGame().npc_interaction.pending_notice.flag = flag;
    CurrentGame().npc_interaction.pending_notice.npc = npc;
    CurrentGame().npc_interaction.pending_notice.line = line;
    CurrentGame().npc_interaction.pending_notice.force = arg;
    if (item != 0) {
        CurrentGame().npc_interaction.pending_notice.item = *item;
    } else {
        CurrentGame().npc_interaction.pending_notice.item = W8ItemInstance{.iItemNo = -1};
    }
    CurrentGame().npc_dialogue.Queue(W8NpcCommand::dispatch_pending_notice);
    CurrentGame().npc_interaction.pending_notice_queued = true;
}

/* The NPC notice and dialogue dispatcher. Talking to a healer NPC (name
   styles 0x0f and 0x12) first lifts every active condition off the two party
   rows bound to RPC NPCs (name styles 0x10 and 0x11) whose condition set has
   reached 0x0f; clearing condition 0x12 - death - leaves them on ten hit
   points. A completed character event is finished off, the dialogue NPC is
   staged unless a set-aside conversation resumes, and then the record's 0x054/0x2ea flags choose
   between the item/quote branch and the plain quote branch; both end by
   pausing the world, raising the dialogue flags and pointing the camera at
   the NPC's monster. */
static void BeginNpcDialogueInternal(W8NpcState* npc, W8ItemInstance* item, int quote,
                                     unsigned char flags, unsigned char force)
{
    W8NpcInteractionState* state;
    W8MonsterInfo* info;
    W8NpcState* bound;
    W8NpcState* selected;
    W8Character* characters;
    int slot;
    int condition;
    glm::vec3 position;

    CurrentGame().npc_interaction.pending_notice_queued = false;
    CurrentGame().npc_interaction.transcript_open_count = 0;
    if (npc->name_style == 0xf || npc->name_style == 0x12) {
        characters = CurrentGame().party.characters;
        for (slot = 0; slot < 2; ++slot) {
            if (CurrentGame().party.members[slot].has_value()) {
                bound = GetNpcState(CurrentGame().party.npcIndex(slot));
                if ((bound->name_style == W8_NPC_RODAN || bound->name_style == W8_NPC_DRAZIC) &&
                    characters[slot].highest_condition >= W8_CONDITION_ASLEEP) {
                    for (condition = 0; condition <= 0x12; ++condition) {
                        if (characters[slot].uiCondition[condition] != 0) {
                            RemoveCharacterCondition(slot, static_cast<W8Condition>(condition),
                                                     false);
                            if (condition == W8_CONDITION_DEAD) {
                                characters[slot].hp_current = 10;
                            }
                        }
                    }
                }
            }
        }
    }
    if (CurrentGame().speech.HasActiveEvents()) {
        CurrentGame().speech.CompleteFirstActiveEvent();
    }
    if (!CurrentGame().activity.conversation_aside) {
        SelectNpcDialogueSpeaker(npc, flags);
    }
    if (force != 0) {
        OpenNpcDialoguePanel(npc, item, true);
        return;
    }
    if (npc->record->monster_bound == 0 && npc->record->voice_script == 0) {
        if (GetNpcDispositionBand(npc) == W8_NPC_BAND_HOSTILE && npc->record->merchant == 0) {
            CurrentGame().npc_dialogue.QueueQuote(0x18, false, false, false);
            return;
        }
        if (flags == 0) {
            OpenNpcDialoguePanel(npc, item, false);
            return;
        }
    }
    if (npc->record->voice_script != 0) {
        if (item != 0) {
            state = &CurrentGame().npc_interaction;
            state->pending_item = *item;
            if ((CurrentGame().party.held.item.iItemNo != -1)) {
                state->held_item_pending = true;
            }
            HandleNpcDialogueItem(&state->pending_item);
        } else if (quote == -1) {
            HandleNpcDialogueDeparture(1);
        } else {
            CurrentGame().npc_dialogue.QueueQuote(quote, false, false, false);
        }
    } else {
        if (quote == -1) {
            quote = 0;
        }
        CurrentGame().npc_dialogue.QueueQuote(quote, false, false, false);
    }
    selected = CurrentGame().npc_interaction.dialogue_npc;
    if (selected->name_style != 0x84 && selected->name_style != 0x85) {
        info = GetNpcMonsterInfo(selected);
        if (info != 0) {
            info->p3D->ForwardReferencePosition(0);
        }
    }
    state = &CurrentGame().npc_interaction;
    state->scripted_dialogue = true;
    CurrentGame().activity.conversation = true;
    state->transcript_open_count = 0;
    info = GetNpcMonsterInfo(state->dialogue_npc);
    if (info == 0) {
        return;
    }
    position = info->p3D->nav.movement.position;
    position.y += info->p3D->nav.movement.height_offset;
    partyCamera().LookAt(&position, false);
}

void BeginNpcDialogue(W8NpcState* npc, W8ItemInstance* item, int quote, unsigned char flags,
                      unsigned char force)
{
    BeginNpcDialogueInternal(npc, item, quote, flags, force);
}

/* Dispatch the queued NPC script notice: the item goes across only while it
   still carries an id. The two notice flags remain independent byte values. */
void DispatchPendingNpcScriptNotice(void)
{
    W8ItemInstance* item;

    item = 0;
    if (CurrentGame().npc_interaction.pending_notice.item.iItemNo != -1) {
        item = &CurrentGame().npc_interaction.pending_notice.item;
    }
    BeginNpcDialogue(CurrentGame().npc_interaction.pending_notice.npc, item,
                     CurrentGame().npc_interaction.pending_notice.line,
                     CurrentGame().npc_interaction.pending_notice.flag,
                     CurrentGame().npc_interaction.pending_notice.force);
}

/* Open the NPC dialogue panel. After the shared screen reset and the
   dialogue-UI build, a carried item goes through the pending-item path -
   inspecting it decides between the trade switch and a disposition check -
   while everything else falls to the force gate and then the
   dispatch: a record-0x056 NPC takes the plain quote, otherwise the
   disposition band picks the hostile or friendly entry. */
static void CloseActiveNpcDialogueLayout();

static bool OpenNpcDialoguePanel(W8NpcState* npc, W8ItemInstance* item, bool force)
{
    W8NpcInteractionState* state;
    W8MonsterInfo* info;
    W8MonsterInfo* dialogue_info;
    unsigned char band;
    bool greet;
    std::string space;
    glm::vec3 position;

    UpdateScreenOverlays(0);
    CurrentGame().activity.conversation = true;
    CloseMainGameOverlays();
    if (npc->record->owns_stock != 0) {
        RestockNpcInventory(npc);
    }
    state = &CurrentGame().npc_interaction;
    state->dialogue_layout = W8_DIALOGUE_LAYOUT_NONE;
    if (!CurrentGame().activity.conversation_aside) {
        state->previous_dialogue_layout = W8_DIALOGUE_LAYOUT_NONE;
        state->trade_mode = W8_NPC_TRADE_NONE;
    }
    state->dialogue_panel_hidden = false;
    state->scripted_dialogue = false;
    state->trade_item = 0;
    state->last_mouse_x = -1;
    state->last_mouse_y = -1;
    state->hovered_text_line = -1;
    state->trade_filter = W8_NPC_TRADE_FILTER_NONE;
    state->value_000 = 0;
    state->where_is_query = false;
    state->held_item_pending = false;
    CurrentGame().npc_dialogue.AbandonReply();
    state->dialogue_hidden = 0;
    state->modal_dialog_open = false;
    state->reopen_topics = false;
    state->trade_gold = 0;
    state->pending_layout = W8_DIALOGUE_LAYOUT_NONE;
    state->suppress_parting_reaction = true;
    state->selected_trade_row = -1;
    state->transcript_open_count = 0;
    RefreshMainGameLayout();
    CreateNpcDialogueControls();
    MainGameHud()->text_box_visible = false;
    EnableDialogueTextBoxRegions();
    SetTextBoxScrollInput(true);
    MainGameHud()->action_panel_visible = 1;
    CurrentGame().activity.conversation_aside = false;
    greet = !force;
    if (item != 0) {
        state = &CurrentGame().npc_interaction;
        state->pending_item = *item;
        if ((CurrentGame().party.held.item.iItemNo != -1)) {
            state->held_item_pending = true;
            (void)TakeInventoryItem(CurrentGame(), {W8ItemStorage::Hand});
        }
        if (npc->record->merchant != 0) {
            if (HandleNpcDialogueItem(&state->pending_item) == 0) {
                OpenNpcDialogueTranscriptLayout();
                greet = true;
            }
        } else if (HandleNpcDialogueItem(&state->pending_item) == 0) {
            CloseActiveNpcDialogueLayout();
            if (GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc) ==
                W8_NPC_BAND_FRIENDLY) {
                OpenNpcDialogueTranscriptLayout();
            } else {
                ShowNpcDialogueTopicMenu();
            }
            greet = true;
        }
    }
    if (greet) {
        if (CurrentGame().npc_interaction.dialogue_npc->record->merchant != 0) {
            CurrentGame().npc_dialogue.QueueQuote(0, false, false, false);
            OpenNpcDialogueTranscriptLayout();
        } else {
            band = GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc);
            if (CurrentGame().npc_interaction.dialogue_npc->name_style == W8_NPC_ZANT &&
                GetFact(W8_FACT_TRANG_YOU_ARE_BUSTED) != 0 &&
                GetFact(W8_FACT_ALIGNMENT_UMPANI) == 0) {
                SetFact(W8_FACT_TRANG_YOU_ARE_BUSTED, 0, false);
            }
            if (band == W8_NPC_BAND_FRIENDLY) {
                HandleNpcDialogueDeparture(1);
                OpenNpcDialogueTranscriptLayout();
            } else if (band == W8_NPC_BAND_NEUTRAL) {
                CurrentGame().npc_dialogue.QueueQuote(2, false, false, false);
                ShowNpcDialogueTopicMenu();
            }
        }
    }
    if (auto* hud = MainGameHud()) {
        hud->repaint.roof_and_spell_icons = true;
        hud->repaint.submenu_buttons = true;
    }
    CancelMouselook();
    info = GetNpcMonsterInfo(npc);
    if (info != 0) {
        CurrentGame().npc_interaction.camera_redirected = true;
        CurrentGame().npc_interaction.saved_camera_pitch = partyCamera().m_pitch;
        dialogue_info = GetNpcMonsterInfo(CurrentGame().npc_interaction.dialogue_npc);
        if (dialogue_info != 0) {
            position = dialogue_info->p3D->nav.movement.position;
            position.y += dialogue_info->p3D->nav.movement.height_offset;
            partyCamera().LookAt(&position, false);
        }
        info->p3D->ForwardReferencePosition(0);
    }
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    CurrentGame().npc_interaction.text_box_collapsed = true;
    space = " ";
    ShowNotice(W8_FONT_PALETTE_YELLOW, space, 3);
    return true;
}

/* Stage `npc` as the dialogue NPC: bind its monster's location, clear its
   transient flags, kick the script dialogue, refresh the name caption while
   the dialogue UI is already up, and pick the speaking character - the first
   occupied row. The retail unsigned skill comparison starts at 0xffffffff,
   so later rows do not replace it. Every occupied portrait then takes target pose 1.
   A stale disposition snapshot on the NPC drops its 0x1c flag. */
void SelectNpcDialogueSpeaker(W8NpcState* npc, int flags)
{
    W8NpcInteractionState* state;
    W8MonsterInfo* info;
    W8NpcState* selected;
    int slot;
    int speaker;
    unsigned int best;

    state = &CurrentGame().npc_interaction;
    speaker = -1;
    state->target_location_id = -1;
    best = 0xffffffff;
    state->dialogue_npc = 0;
    info = GetNpcMonsterInfo(npc);
    if (info != 0) {
        state->target_location_id = info->location_id;
    }
    state->dialogue_npc = npc;
    state->dialogue_npc->flag = false;
    state->dialogue_npc->flag0 = false;
    if (state->dialogue_npc->greeting_pending) {
        state->dialogue_npc->suspicion = 0;
    }
    CurrentGame().npc_dialogue.Begin(state->dialogue_npc, false);
    if ((state->dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT ||
         state->dialogue_layout == W8_DIALOGUE_LAYOUT_TOPIC_MENU) &&
        CurrentGame().activity.conversation) {
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
            state->dialogue_npc->record->source_name, g_wiz_text_bold_font);
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].dirty = true;
    }
    state->camera_redirected = false;
    for (slot = 0; slot < 8; ++slot) {
        if (CurrentGame().party.members[slot].has_value()) {
            if (speaker == -1) {
                speaker = slot;
            }
            if (CurrentGame().party.characters[slot].skills[W8_SKILL_COMMUNICATION].level > best) {
                best = CurrentGame().party.characters[slot].skills[W8_SKILL_COMMUNICATION].level;
                speaker = slot;
            }
        }
    }
    CurrentGame().npc_interaction.dialogue_speaker = speaker;
    for (slot = 0; slot < 8; ++slot) {
        if (CurrentGame().party.members[slot].has_value()) {
            SetPortraitTargetPose(&CurrentGame().monster_manager_entries[slot], 1);
        }
    }
    selected = CurrentGame().npc_interaction.dialogue_npc;
    if (selected->dismissed_flag &&
        selected->disposition_at_open != GetNpcDispositionBand(selected)) {
        selected->dismissed_flag = false;
    }
}

/* Build the NPC dialogue UI: the option-button panel on the left, the
   secondary panel beside it, the scrolling text controller, the three
   right-hand panels and the text-input panel, then every text control each
   of them hosts. Buttons get their option masks, help ids and activation
   callbacks as they are created. */
static void CreateNpcDialogueControls()
{
    auto* state = &CurrentGame().npc_interaction;
    state->ui = std::make_unique<NpcDialogueUi>();
    auto& ui = *state->ui;
    ui.transcript = std::make_unique<W8NpcDialogueTextController>();
    AddNpcAction(W8_NPC_CONTROL_NPC_NAME, 0, {5, 2, (0x89) - (5), (0x12) - (2)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_38, 0, {5, 0x47, (0x89) - (5), (0x57) - (0x47)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_1, 0, {2, 0x11, (0x45) - (2), (0x21) - (0x11)}, 0x1a9,
                 {-1, 0xc, 10, 0xb, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_2, 0, {0x44, 0x11, (0x87) - (0x44), (0x21) - (0x11)}, 0x1a9,
                 {-1, 0xc, 10, 0xb, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_3, 0, {2, 0x21, (0x45) - (2), (0x31) - (0x21)}, 0x1a9,
                 {-1, 0xc, 10, 0xb, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_4, 0, {0x44, 0x21, (0x87) - (0x44), (0x31) - (0x21)}, 0x1a9,
                 {-1, 0xc, 10, 0xb, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_5, 0, {2, 0x31, (0x45) - (2), (0x41) - (0x31)}, 0x1a9,
                 {-1, 0xc, 10, 0xb, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_6, 0, {0x44, 0x31, (0x87) - (0x44), (0x41) - (0x31)}, 0x1a9,
                 {-1, 0xc, 10, 0xb, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_7, 0, {0x72, 0x47, (0x89) - (0x72), (0x57) - (0x47)}, 0x1aa,
                 {0, 4, 1, 2, 3});
    AddNpcAction(W8_NPC_CONTROL_OPTION_BUTTONS + 0, 0, {7, 0x46, (0x17) - (7), (0x56) - (0x46)},
                 0x1ab, {0, 1, 2, 4, 3});
    AddNpcAction(W8_NPC_CONTROL_OPTION_BUTTONS + 1, 0,
                 {0x19, 0x46, (0x29) - (0x19), (0x56) - (0x46)}, 0x1ab, {5, 6, 7, 9, 8});
    AddNpcAction(W8_NPC_CONTROL_OPTION_BUTTONS + 2, 0,
                 {0x2b, 0x46, (0x3b) - (0x2b), (0x56) - (0x46)}, 0x1ab, {10, 0xb, 0xc, 0xe, 0xd});
    AddNpcAction(W8_NPC_CONTROL_OPTION_BUTTONS + 3, 0,
                 {0x3e, 0x46, (0x4e) - (0x3e), (0x56) - (0x46)}, 0x1ab,
                 {0xf, 0x10, 0x11, 0x13, 0x12});
    AddNpcAction(W8_NPC_CONTROL_OPTION_BUTTONS + 4, 0,
                 {0x50, 0x46, (0x60) - (0x50), (0x56) - (0x46)}, 0x1ab,
                 {0x14, 0x15, 0x16, 0x18, 0x17});
    AddNpcAction(W8_NPC_CONTROL_OPTION_BUTTONS + 5, 0,
                 {0x62, 0x46, (0x72) - (0x62), (0x56) - (0x46)}, 0x1ab,
                 {0x19, 0x1a, 0x1b, 0x1d, 0x1c});
    AddNpcAction(W8_NPC_CONTROL_WIDGET, 1, {2, 4, (0x136) - (2), (0x57) - (4)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_SCROLL, 2, {6, 6, (0x7c) - (6), (0x11) - (6)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_SCROLL_UP_BUTTON, 2, {0x7e, 3, (0x88) - (0x7e), (0xb) - (3)}, 0x1aa,
                 {0x14, 0x18, 0x15, 0x16, 0x17});
    AddNpcAction(W8_NPC_CONTROL_SCROLL_DOWN_BUTTON, 2, {0x7e, 0xc, (0x88) - (0x7e), (0x14) - (0xc)},
                 0x1aa, {0x19, 0x1d, 0x1a, 0x1b, 0x1c});
    AddNpcAction(W8_NPC_CONTROL_TEXT_12, 6, {0x11, 0x23, (0x22) - (0x11), (0x30) - (0x23)}, 0x1aa,
                 {5, 9, 6, 7, 8});
    AddNpcAction(W8_NPC_CONTROL_TEXT_13, 6, {0x26, 0x23, (0x37) - (0x26), (0x30) - (0x23)}, 0x1aa,
                 {10, 0xe, 0xb, 0xc, 0xd});
    AddNpcAction(W8_NPC_CONTROL_TEXT_23, 6, {9, 0x33, (0x84) - (9), (0x44) - (0x33)}, 0x1a9,
                 {-1, -1, 0xe, 0xf, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_22, 6, {9, 0x43, (0x84) - (9), (0x52) - (0x43)}, 0x1a9,
                 {-1, -1, 0xe, 0xf, -1});
    AddNpcAction(W8_NPC_CONTROL_SORT_BUTTON, 3, {6, 2, (0x89) - (6), (0xe) - (2)}, 0x1a9,
                 {8, 9, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_PEOPLE_BUTTON, 3, {6, 0xf, (0x41) - (6), (0x1b) - (0xf)}, 0x1a9,
                 {8, 9, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_PLACES_BUTTON, 3, {0x43, 0xf, (0x89) - (0x43), (0x1b) - (0xf)},
                 0x1a9, {8, 9, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_ITEMS_BUTTON, 3, {6, 0x1b, (0x41) - (6), (0x27) - (0x1b)}, 0x1a9,
                 {8, 9, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_MISC_BUTTON, 3, {0x43, 0x1b, (0x89) - (0x43), (0x27) - (0x1b)},
                 0x1a9, {8, 9, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_ALL_BUTTON, 3, {6, 0x27, (0x41) - (6), (0x33) - (0x27)}, 0x1a9,
                 {8, 9, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_24, 5, {5, 0x14, (0x32) - (5), (0x49) - (0x14)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_31, 5, {0x35, 0x36, (0x51) - (0x35), (0x46) - (0x36)}, 0x1aa,
                 {0x1e, 0x22, 0x1f, 0x20, 0x21});
    AddNpcAction(W8_NPC_CONTROL_TEXT_33, 5, {0x53, 0x4d, (0x86) - (0x53), (0x58) - (0x4d)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_34, 5, {5, 0x4d, (0x51) - (5), (0x58) - (0x4d)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_35, 5, {0x53, 0x3a, (0x86) - (0x53), (0x45) - (0x3a)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_36, 5, {4, 4, (0x88) - (4), (0x11) - (4)}, -1,
                 {-1, -1, -1, -1, -1});
    AddNpcAction(W8_NPC_CONTROL_TEXT_37, 5, {0x38, 0x22, (0x88) - (0x38), (0x34) - (0x22)}, -1,
                 {-1, -1, -1, -1, -1});
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetLayoutMode(
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetLayoutMode(
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_6].text.SetLayoutBounds(
        0x5d, 0x197, 0xa0, 0x1a7);
    CurrentGame()
        .npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 0]
        .policy.primary_toggle = ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 0, 0x7c2);
    CurrentGame()
        .npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 1]
        .policy.primary_toggle = ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 1, 0x7c3);
    CurrentGame()
        .npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 2]
        .policy.primary_toggle = ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 2, 0x7c4);
    CurrentGame()
        .npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 3]
        .policy.primary_toggle = ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 3, 0x7c5);
    CurrentGame()
        .npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 4]
        .policy.primary_toggle = ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 4, 0x7c6);
    CurrentGame()
        .npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 5]
        .policy.primary_toggle = ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 5, 0x7c7);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 0].primary = [] {
        ToggleNpcTradeCategory(0, W8_NPC_TRADE_HAND);
    };
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 1].primary = [] {
        ToggleNpcTradeCategory(1, W8_NPC_TRADE_ACCESSORY);
    };
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 2].primary =
        ToggleNpcCharacterUsabilityFilter;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 3].primary = [] {
        ToggleNpcTradeCategory(3, W8_NPC_TRADE_BODY);
    };
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 4].primary = [] {
        ToggleNpcTradeCategory(4, W8_NPC_TRADE_OTHER);
    };
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 5].primary =
        ToggleNpcPartyUsabilityFilter;

    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_12, 100);
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_13, 0x65);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_23].text.SetText(
        Data().strings[0x741], g_wiz_text_font_secondary);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_23].primary =
        SubmitNpcDialogueInput;
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_23, 0x66);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_22].text.SetText(
        Data().strings[0x742], g_wiz_text_font_secondary);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_22].primary =
        SubmitNpcWhereIsQuery;
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_22, 0x67);

    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetLayoutMode(
        g_W8TextBufferAlignBottom | g_W8TextBufferAlignRight);
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_31, 0x7c9);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_33].text.SetLayoutMode(
        g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_33, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_34].text.SetLayoutMode(
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_34, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetLayoutMode(
        g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_35, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_36].text.SetLayoutMode(
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_36, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_7].primary = BackOutNpcDialogue;
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_7, 0x7c8);
}

static void OpenNpcDialogueLayout(W8NpcDialogueLayout layout)
{
    switch (layout) {
    case W8_DIALOGUE_LAYOUT_UNSET:
    case W8_DIALOGUE_LAYOUT_NONE:
    case W8_DIALOGUE_LAYOUT_BARE:
        return;
    case W8_DIALOGUE_LAYOUT_SERVICES:
        OpenNpcDialogueMode1Layout();
        return;
    case W8_DIALOGUE_LAYOUT_TOPIC_MENU:
        ShowNpcDialogueTopicMenu();
        return;
    case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
        OpenNpcDialogueTranscriptLayout();
        return;
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        OpenNpcDialogueOptionLayout();
        return;
    case W8_DIALOGUE_LAYOUT_TRADE:
        OpenNpcDialogueMode5Layout();
        return;
    }
}

/* The NPC dialogue per-frame update: keeps the monster's navigator angles
   fresh, services the pending layout switch, mirrors the transcript controls'
   enabled state to the field text and selection, and clears the 500ms trade
   highlight flash once it lapses. */
void ServiceNpcDialogue(void)
{
    std::string field_text;
    W8MonsterInfo* monster_info = GetNpcMonsterInfo(CurrentGame().npc_interaction.dialogue_npc);
    if (monster_info != 0) {
        monster_info->p3D->nav.UpdateAngles();
    }
    if (CurrentGame().npc_interaction.scripted_dialogue) {
        return;
    }
    if (CurrentGame().npc_interaction.dialogue_hidden != 0 && IsRightButtonHeld()) {
        SetNpcDialogueHidden(0);
    }
    field_text = NpcFieldText();
    if (CurrentGame().npc_interaction.pending_layout != W8_DIALOGUE_LAYOUT_NONE) {
        MainGameHud()->text_box_visible = false;
        EnableDialogueTextBoxRegions();
        SetTextBoxScrollInput(true);
        MainGameHud()->action_panel_visible = 1;
        W8NpcDialogueLayout layout = CurrentGame().npc_interaction.pending_layout;
        SwitchNpcDialogueLayout(W8_DIALOGUE_LAYOUT_NONE);
        OpenNpcDialogueLayout(layout);
        CurrentGame().npc_interaction.pending_layout = W8_DIALOGUE_LAYOUT_NONE;
    }
    if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
        if (field_text.size() == 0) {
            if (CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_12].enabled) {
                SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_12, false);
                CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_12].dirty = true;
            }
        } else if (!CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_12].enabled) {
            SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_12, true);
            CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_12].dirty = true;
        }
        int selection =
            CurrentGame().npc_interaction.ui->transcript->GetSelectedTranscriptEntryIndex();
        if (selection == -1) {
            if (CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_13].enabled) {
                SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_13, false);
                CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_13].dirty = true;
            }
        } else if (!CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_13].enabled) {
            SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_13, true);
            CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_13].dirty = true;
        }
        SetNpcActionEnabled(
            W8_NPC_CONTROL_SCROLL_UP_BUTTON,
            CurrentGame().npc_interaction.ui->transcript->HandleScrollUpCommand(true) != 0);
        SetNpcActionEnabled(
            W8_NPC_CONTROL_SCROLL_DOWN_BUTTON,
            CurrentGame().npc_interaction.ui->transcript->HandleScrollDownCommand(true) != 0);
    }
    if (CurrentGame().npc_interaction.ui != nullptr &&
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.m_highlighted != 0 &&
        MonotonicMilliseconds() - g_trade_highlight_tick > 500) {
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.m_highlighted =
            false;
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetGeometryDirty();
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].text.m_highlighted =
            false;
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetGeometryDirty();
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].dirty = true;
    }
}

/* Close the NPC dialogue: retire the active layout, release the panels and
   text controls, restore the held item or target cursor, queue the parting
   character event and hand control back to the world. */
/* Close the current dialogue layout before ending, switching or returning
   from trade. The name is descriptive; the shared dispatch retains each
   layout's existing teardown API. */
static void CloseActiveNpcDialogueLayout()
{
    switch (CurrentGame().npc_interaction.dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_SERVICES:
        CloseNpcDialogueMode1Layout();
        break;
    case W8_DIALOGUE_LAYOUT_TOPIC_MENU:
        CloseNpcDialogueLayout();
        break;
    case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
        CloseNpcDialogueTranscriptLayout();
        break;
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        CloseNpcDialogueOptionLayout();
        break;
    case W8_DIALOGUE_LAYOUT_TRADE:
        CloseNpcDialogueMode5Layout();
        break;
    case W8_DIALOGUE_LAYOUT_BARE:
        SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
        break;
    default:
        break;
    }
}

void EndNpcDialogueSession(bool skip_exit_actions)
{
    if (!CurrentGame().activity.conversation) {
        return;
    }
    if (CurrentGame().npc_interaction.scripted_dialogue) {
        CurrentGame().activity.conversation = false;
        return;
    }
    if (CurrentGame().activity.conversation_aside) {
        skip_exit_actions = true;
    }
    if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    if (CurrentGame().npc_interaction.dialogue_npc->record->monster_bound == 0) {
        CurrentGame().npc_interaction.dialogue_npc->dismissed_flag = true;
        CurrentGame().npc_interaction.dialogue_npc->disposition_at_open =
            GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc);
        W8NpcState* npc = CurrentGame().npc_interaction.dialogue_npc;
        npc->dismissed_timer = 0;
    }
    CurrentGame().activity.conversation = false;
    MainGameHud()->action_panel_visible = 0;
    DisableDialogueTextBoxRegions();
    SetTextBoxScrollInput(false);
    CloseActiveNpcDialogueLayout();
    SetNpcDialogueInputEnabled(2, false);
    SelectTextBox(0);
    MainGameHud()->text_box_visible = true;
    RefreshMainGameLayout();
    g_npc_text_fields.reset();
    CurrentGame().npc_interaction.ui.reset();
    if (auto* hud = MainGameHud()) {
        hud->repaint.roof_and_spell_icons = true;
        hud->repaint.submenu_buttons = true;
    }
    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         index++) {
        ClearMonsterCharm(GetMonsterListEntry(index));
    }
    GetNpcMonsterInfo(CurrentGame().npc_interaction.dialogue_npc);
    if (!skip_exit_actions) {
        if (!CurrentGame().npc_interaction.held_item_pending) {
            SetTargetCursor(W8_CURSOR_NONE);
        } else {
            CurrentGame().party.held.item = CurrentGame().npc_interaction.pending_item;
            CurrentGame().npc_interaction.held_item_pending = false;
        }
        W8NpcState* npc = CurrentGame().npc_interaction.dialogue_npc;
        if (!CurrentGame().npc_interaction.suppress_parting_reaction &&
            npc->record->merchant == 0 && !npc->is_grouped && GetNpcMonsterInfo(npc) != 0) {
            int slot = GetRandomCharacter(1, 1, -1, -1);
            if (slot != -1) {
                W8CharacterEvent* event =
                    QueueCharacterEvent(&CurrentGame().party.characters[slot], g_special_event8,
                                        kCharacterEventNoPreempt, kCharacterEventFullVolume);
                if (event != 0) {
                    event->DelayDispatch(std::chrono::milliseconds{1000});
                }
            }
        }
    }
    FlushPendingNoticeLines();
    if (CurrentGame().npc_interaction.camera_redirected) {
        partyCamera().SetPitch(CurrentGame().npc_interaction.saved_camera_pitch);
    }
    if (CurrentGame().npc_interaction.dialogue_npc->name_style == 0xf &&
        GetFact(W8_FACT_ALIGNMENT_UMPANI) != 0) {
        CurrentGame().status.trang_check_clock = CurrentGame().status.world_clock;
        CurrentGame().status.trang_check_pending = true;
    }
}

/* Whether the open NPC dialogue transcript covers the party slot's
   portrait: dialogue mode up, panel invalidation not suppressed and the
   controller enabled, then the slot's band check. Portrait and
   character-update paths skip the covered rows through this. */
bool IsPortraitObscuredByNpcDialogue(unsigned int party_slot)
{
    if (CurrentGame().activity.conversation && !CurrentGame().npc_interaction.scripted_dialogue &&
        CurrentGame().npc_interaction.ui->panels[2].active) {
        return CurrentGame().npc_interaction.ui->transcript->IsSlotPortraitTranscriptCovered(
            party_slot);
    }
    return false;
}

/* Forward one text/cursor rectangle to the action panel while the dialogue
   screen is not suppressing panel invalidation. */
void InvalidateMainGameActionPanelRect(const W8ControlsRect*)
{
    if (!CurrentGame().npc_interaction.scripted_dialogue && CurrentGame().npc_interaction.ui)
        CurrentGame().npc_interaction.ui->panels[1].dirty = true;
}

void ActivateNpcDialoguePanels(bool force)
{
    if (CurrentGame().npc_interaction.scripted_dialogue || !CurrentGame().npc_interaction.ui)
        return;
    for (int index = 0; index < 7; ++index) {
        const bool redraw_scroll = index == 1 &&
                                   CurrentGame().npc_interaction.ui->panels[1].active &&
                                   (force || NpcPanelDirty(1));
        PaintNpcPanel(index, GetFrameBufferSurface(), force);
        if (redraw_scroll) RedrawTextBoxScrollChrome();
    }
}

bool HasNpcDialogueDirtyPanels()
{
    if (CurrentGame().npc_interaction.scripted_dialogue || !CurrentGame().npc_interaction.ui)
        return false;
    for (int index = 0; index < 7; ++index)
        if (CurrentGame().npc_interaction.ui->panels[index].active && NpcPanelDirty(index))
            return true;
    return false;
}

/* Retire the current dialogue layout mode into previous_dialogue_layout and, when nonzero,
   install `value` as the new one; either way the dialogue cursor helper gets
   re-run while the flag is set. */
static void SetNpcDialogueLayoutMode(W8NpcDialogueLayout value)
{
    if (value == W8_DIALOGUE_LAYOUT_NONE) {
        CurrentGame().npc_interaction.previous_dialogue_layout =
            CurrentGame().npc_interaction.dialogue_layout;
        CurrentGame().npc_interaction.dialogue_layout = W8_DIALOGUE_LAYOUT_NONE;
    } else {
        CurrentGame().npc_interaction.dialogue_layout = value;
    }
    if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
}

static void RefreshNpcServiceAvailability(int party_slot)
{
    SetNpcActionEnabled(
        W8_NPC_CONTROL_TEXT_1,
        CanCharacterCastSpell(&CurrentGame().party.characters[party_slot], W8_SPELL_CHARM));
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_1].dirty = true;
    SetNpcActionEnabled(
        W8_NPC_CONTROL_TEXT_2,
        CanCharacterCastSpell(&CurrentGame().party.characters[party_slot], W8_SPELL_MINDREAD));
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_2].dirty = true;
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4,
                        CharacterHasServiceItem(&CurrentGame().party.characters[party_slot]));
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_4].dirty = true;
}

/* Whether the typed-text cursor of the active NPC dialogue is up; only the
   main-game action keys keep working while it owns input. */
/* Refresh the mode-1 service buttons and mode-4 item editor for the party
   slot that entered the dialogue. Mode 1 enables the heal/cure/service rows
   from what the character can actually use; mode 4 resets the editor and
   re-arms the trade prompt. */
void SyncNpcServiceButtons(int party_slot)
{
    if (CurrentGame().activity.conversation) {
        if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_SERVICES) {
            RefreshNpcServiceAvailability(party_slot);
        } else if (CurrentGame().npc_interaction.dialogue_layout ==
                   W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
            SelectNpcPartyItems();
            UpdateNpcDialogueSubMode();
        }
    }
}

unsigned char IsNpcDialogueCursorActive(void)
{
    if (!CurrentGame().activity.conversation) {
        return 0;
    }
    return CurrentGame().npc_interaction.dialogue_hidden;
}

/* Whether an NPC dialogue is up in the layout that posts to the main text
   box (mode 4) rather than to the dialogue's own pane. */
bool IsNpcDialogueTextBoxActive(void)
{
    if (!CurrentGame().activity.conversation) {
        return false;
    }
    return CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX;
}

void TryNpcDialoguePickpocket(int party_slot)
{
    if (CurrentGame().activity.conversation && CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
        ResolveNpcPickpocket(party_slot);
    }
}

UiResult HandleNpcTextBox(const UiPointerEvent& event)
{
    const auto point = event.position;
    if (event.action == UiPointerAction::Down && event.button == UiPointerButton::Left &&
        event.clicks == 2)
        AfterUiDispatch([point] { NpcDialogueTextBoxDoubleClick(point.x, point.y); });
    if (event.action == UiPointerAction::Up && event.inside) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK))
            AfterUiDispatch([point] { NpcDialogueTextBoxLeftUp(point.x, point.y); });
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK))
            AfterUiDispatch([point] { NpcDialogueTextBoxRightUp(point.x, point.y); });
    }
    if (event.action != UiPointerAction::Move && event.action != UiPointerAction::Enter &&
        event.action != UiPointerAction::Leave)
        return UiResult::Handled;
    int y, line;
    if (event.action == UiPointerAction::Leave) {
        ClearNoticeWordHover(3, true);
        ClearHoveredTextLine(2);
        return UiResult::Handled;
    }
    if (event.action == UiPointerAction::Enter) {
        CurrentGame().npc_interaction.last_mouse_x = event.position.x;
        CurrentGame().npc_interaction.last_mouse_y = event.position.y;
        if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
            y = event.position.y;
            if (y >= MainGameHud()->text_box_top && y <= MainGameHud()->text_box_bottom) {
                line = (y - MainGameHud()->text_box_top) / 11;
                HighlightNoticeRow(line);
                CurrentGame().npc_interaction.hovered_text_line = line;
            }
        }
        return UiResult::Handled;
    }
    if (event.position.x == CurrentGame().npc_interaction.last_mouse_x &&
        event.position.y == CurrentGame().npc_interaction.last_mouse_y) {
        return UiResult::Handled;
    }
    CurrentGame().npc_interaction.last_mouse_x = event.position.x;
    CurrentGame().npc_interaction.last_mouse_y = event.position.y;
    if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
        HighlightNoticeWordAt(3, event.position.x, event.position.y);
        return UiResult::Handled;
    }
    if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        y = event.position.y;
        if (y >= MainGameHud()->text_box_top && y <= MainGameHud()->text_box_bottom) {
            line = (y - MainGameHud()->text_box_top) / 11;
            if (line != CurrentGame().npc_interaction.hovered_text_line) {
                HighlightNoticeRow(line);
            }
            CurrentGame().npc_interaction.hovered_text_line = line;
        }
    }
    return UiResult::Handled;
}

/* Mouse-wheel line tracking on the main NPC text box: flag forces the redraw
   even when the hovered line has not changed. */
void NpcDialogueTextBoxWheelAt(short x, unsigned short y, bool flag)
{
    int line;

    if (CurrentGame().npc_interaction.dialogue_layout != W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        return;
    }
    if (static_cast<int>(y) < MainGameHud()->text_box_top ||
        static_cast<int>(y) > MainGameHud()->text_box_bottom) {
        return;
    }
    line = (static_cast<int>(y) - MainGameHud()->text_box_top) / 11;
    if (line != CurrentGame().npc_interaction.hovered_text_line || flag) {
        HighlightNoticeRow(line);
    }
    CurrentGame().npc_interaction.hovered_text_line = line;
}

static void SelectNpcDialogueKeyword(W8NoticeWord* word, int line)
{
    std::string word_text;
    std::string field_text;
    std::string combined;
    word->keyword = W8_NOTICE_WORD_SELECTED;
    word_text = CopyNoticeWordText(word, 3, line);
    field_text = NpcFieldText();
    StripNpcKeywordPunctuation(word_text);
    if (field_text.size() != 0) {
        combined = fmt::format("{} {}", field_text, word_text);
        SetNpcFieldText(combined);
    } else {
        SetNpcFieldText(word_text);
    }
    RedrawTextBoxBody(true);
}

/* Left release inside the NPC text box: in the transcript layout a hovered
   notice word becomes the selected keyword and is appended to the input field;
   in the item layouts the hovered slot is selected. */
static void NpcDialogueTextBoxLeftUp(int x, int y)
{
    std::string word_text;
    int line;
    W8NoticeWord* word;
    int slot;

    switch (CurrentGame().npc_interaction.dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
        word = HitTestNoticeWord(3, x, y, &line);
        if (word == 0 || word->keyword != W8_NOTICE_WORD_HOVERED) {
            return;
        }
        if (((GetInputState().modifiers & SHIFT_DOWN) == 0)) {
            ResetUsedNoticeWords(3, true);
            word_text.clear();
            SetNpcFieldText(word_text);
        }
        SelectNpcDialogueKeyword(word, line);
        return;
    case W8_DIALOGUE_LAYOUT_SERVICES:
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        slot = MainGameHud()->hovered_text_lines[2];
        if (slot == -1) {
            return;
        }
        UpdateNpcTradeSelection(slot, ((GetInputState().modifiers & SHIFT_DOWN) != 0) ? 1 : 0, 1);
        return;
    default:
        break;
    }
}

/* Right release inside the NPC text box: transcript layout adds the clicked
   keyword to the dialogue transcript; the item layouts select the slot and
   open the item assay dialog. */
static void NpcDialogueTextBoxRightUp(int x, int y)
{
    std::string word_text;
    int line;
    W8NoticeWord* word;
    int slot;
    W8ItemInstance* item;
    W8AssayDialog* dialog;

    switch (CurrentGame().npc_interaction.dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        slot = MainGameHud()->hovered_text_lines[2];
        if (slot == -1) {
            return;
        }
        UpdateNpcTradeSelection(slot, ((GetInputState().modifiers & SHIFT_DOWN) != 0) ? 1 : 0, 1);
        item = ResolveNpcTradeRow(slot, false, 0, 1);
        if (item == 0) {
            return;
        }
        dialog = new W8AssayDialog(
            *item, &CurrentGame().party.characters[CurrentGame().status.selected_character],
            {kInfoDialogX, 0x48});
        dialog->SetText("");

        dialog->on_close = [] {
            if (auto* hud = MainGameHud()) hud->repaint.everything = true;
        };
        OpenModal(std::unique_ptr<ModalContent>(dialog));
        return;
    case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
        word = HitTestNoticeWord(3, x, y, &line);
        if (word == 0) {
            return;
        }
        word_text = CopyNoticeWordText(word, 3, line);
        LearnDialogueKeyword(word_text, W8_DIALOGUE_CATEGORY_ALL, false);
        return;
    default:
        break;
    }
}

/* Left double-click inside the NPC text box: transcript layout selects the
   keyword and submits the input line; the item layout picks the hovered line's
   slot and uses the selected item. */
static void NpcDialogueTextBoxDoubleClick(int x, int y)
{
    std::string word_text;
    int line;
    W8NoticeWord* word;
    int slot;

    switch (CurrentGame().npc_interaction.dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        break;
    case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
        if (CurrentGame().npc_interaction.modal_dialog_open) {
            return;
        }
        word = HitTestNoticeWord(3, x, y, &line);
        if (word == 0) {
            return;
        }
        if (((GetInputState().modifiers & SHIFT_DOWN) == 0)) {
            ResetUsedNoticeWords(3, true);
            word_text.clear();
            SetNpcFieldText(word_text);
        }
        if (word->keyword != W8_NOTICE_WORD_SELECTED) {
            SelectNpcDialogueKeyword(word, line);
        }
        HandleNpcDialogueInput();
        return;
    default:
        return;
    }
    y &= 0xffff;
    if (y >= MainGameHud()->text_box_top && y <= MainGameHud()->text_box_bottom) {
        line = (y - MainGameHud()->text_box_top) / 11;
        HighlightNoticeRow(line);
        CurrentGame().npc_interaction.hovered_text_line = line;
    }
    slot = MainGameHud()->hovered_text_lines[2];
    if (slot != -1) {
        CurrentGame().npc_interaction.selected_trade_row = -1;
        UpdateNpcTradeSelection(slot, 0, 1);
        if (CurrentGame().npc_interaction.trade_item != 0) {
            ConfirmNpcTradeItem();
        }
    }
    NpcDialogueTextBoxWheelAt(0, static_cast<unsigned short>(y), true);
}

static void ShowNpcTradeGold()
{
    std::string text;

    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, true);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetFontStateIndex(1);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetGeometryDirty();
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, true);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_36].text.SetText(
        Data().strings[0x72d], g_wiz_text_font_secondary);
    text = fmt::format("{:d}g", CurrentGame().npc_interaction.trade_gold);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetText(
        text, g_wiz_text_font_secondary);
    CurrentGame().npc_interaction.ui->panels[5].dirty = true;
}

/* Select the item slot under the text-box cursor and refresh the item preview
   controls: layout 1 pulls from the equipment/backpack picker, layout 4 from
   the trade lists; the mode-2 row zero additionally fills in the purse readout. */
void UpdateNpcTradeSelection(int index, int increment, int commit)
{
    W8ItemInstance* item;
    std::string text;
    std::string price_text;
    bool wants_item;

    SetSelectedTextLine(index, 2);
    switch (CurrentGame().npc_interaction.dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        break;
    case W8_DIALOGUE_LAYOUT_SERVICES:
        item = GetNpcTradeSlotItem(index);
        CurrentGame().npc_interaction.trade_item = item;
        if (CurrentGame().npc_interaction.trade_item != 0) {
            SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, true);
        }
        return;
    default:
        return;
    }
    item = ResolveNpcTradeRow(index, true, increment, commit);
    CurrentGame().npc_interaction.trade_item = item;
    if (CurrentGame().npc_interaction.trade_item != 0) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31,
                            1 < CurrentGame().npc_interaction.trade_item->stack_count);
        if (Data().items[CurrentGame().npc_interaction.trade_item->iItemNo].equip_class ==
            W8_ITEM_EQUIP_CLASS_AMMUNITION) {
            SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, false);
        }
        text = ShortenTextToWidth(
            FormatItemDisplayName(Data(), CurrentGame().npc_interaction.trade_item, false), 0x7d,
            g_wiz_text_font_secondary);
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_36].text.SetText(
            text, g_wiz_text_font_secondary);
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetFontStateIndex(1);
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetGeometryDirty();
        switch (CurrentGame().npc_interaction.trade_mode) {
        case W8_NPC_TRADE_BUY:
            if (CurrentGame().party.gold <
                static_cast<unsigned int>(CalculateNpcTradeStackPrice(
                    CurrentGame().npc_interaction.dialogue_npc,
                    CurrentGame().npc_interaction.trade_item->iItemNo, W8_TRADE_PRICE_PARTY_BUYS,
                    CurrentGame().npc_interaction.trade_quantity,
                    CurrentGame().npc_interaction.trade_item->identified))) {
                CurrentGame()
                    .npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37]
                    .text.SetFontStateIndex(0);
                CurrentGame()
                    .npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37]
                    .text.SetGeometryDirty();
            }
            price_text = fmt::format(
                "{:d}g",
                CalculateNpcTradeStackPrice(CurrentGame().npc_interaction.dialogue_npc,
                                            CurrentGame().npc_interaction.trade_item->iItemNo,
                                            W8_TRADE_PRICE_PARTY_BUYS,
                                            CurrentGame().npc_interaction.trade_quantity,
                                            CurrentGame().npc_interaction.trade_item->identified));
            break;
        case W8_NPC_TRADE_SELL: {
            wants_item = NpcAcceptsTradeItem(CurrentGame().npc_interaction.dialogue_npc,
                                             CurrentGame().npc_interaction.trade_item);
            if (!wants_item) {
                CurrentGame()
                    .npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37]
                    .text.SetFontStateIndex(0);
                CurrentGame()
                    .npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37]
                    .text.SetGeometryDirty();
            }
            int price = CalculateNpcTradeStackPrice(
                CurrentGame().npc_interaction.dialogue_npc,
                CurrentGame().npc_interaction.trade_item->iItemNo, W8_TRADE_PRICE_PARTY_SELLS,
                CurrentGame().npc_interaction.trade_quantity,
                CurrentGame().npc_interaction.trade_item->identified);
            if (wants_item) {
                price_text = fmt::format("{:d}g", price);
            } else {
                price_text = "---";
            }
            break;
        }
        case W8_NPC_TRADE_GIVE:
            price_text = " ";
            break;
        default:
            return;
        }
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetText(
            price_text, g_wiz_text_font_secondary);
        return;
    }
    if (CurrentGame().npc_interaction.trade_mode == W8_NPC_TRADE_GIVE && index == 0) {
        ShowNpcTradeGold();
    }
}

/* Reset the secondary dialogue editor: drop the pending item, clear the item
   notice control and both secondary labels, and restore the option row. */
void ResetNpcDialogueItemEditor(void)
{
    ClearSelectedTextLine(2);
    CurrentGame().npc_interaction.trade_item = 0;
    CurrentGame().npc_interaction.trade_selection.reset();
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, false);
    CurrentGame().npc_interaction.ui->item_preview.reset();
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText("",
                                                                                   g_large_font);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetText(
        "", g_wiz_text_font_secondary);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_36].text.SetText(
        "", g_wiz_text_font_secondary);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, false);
    CurrentGame().npc_interaction.selected_trade_row = -1;
    CurrentGame().npc_interaction.trade_quantity = 1;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetFontStateIndex(-1);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetGeometryDirty();
}

/* Retire the current dialogue layout, then open the layout `interact_id`
   selects. */
/* Close the active sub-layout and reopen the layout it replaced: mode 1 goes
   back to the remembered previous_dialogue_layout layout, the option layout returns to the
   topic menu or the transcript by reopen_topics, and mode 5 always lands on the
   transcript. */
static void BackOutNpcDialogue(void)
{
    switch (CurrentGame().npc_interaction.dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_SERVICES:
        CloseNpcDialogueMode1Layout();
        switch (CurrentGame().npc_interaction.previous_dialogue_layout) {
        case W8_DIALOGUE_LAYOUT_TOPIC_MENU:
            ShowNpcDialogueTopicMenu();
            break;
        case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
            OpenNpcDialogueTranscriptLayout();
            break;
        default:
            break;
        }
        break;
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX: {
        bool flag = CurrentGame().npc_interaction.reopen_topics;
        CloseNpcDialogueOptionLayout();
        if (flag) {
            ShowNpcDialogueTopicMenu();
        } else {
            OpenNpcDialogueTranscriptLayout();
        }
        break;
    }
    case W8_DIALOGUE_LAYOUT_TRADE:
        CloseNpcDialogueMode5Layout();
        OpenNpcDialogueTranscriptLayout();
        break;
    default:
        break;
    }
}

void SwitchNpcDialogueLayout(W8NpcDialogueLayout layout)
{
    CloseActiveNpcDialogueLayout();
    OpenNpcDialogueLayout(layout);
}

static void LeaveNpcDialogueLayout(void)
{
    if (!CurrentGame().npc_interaction.modal_dialog_open) {
        if (GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc) ==
            W8_NPC_BAND_FRIENDLY) {
            CurrentGame().npc_interaction.suppress_parting_reaction = false;
            CurrentGame().npc_dialogue.QueueQuote(0x5c, false, false, false);
            return;
        }
        SwitchNpcDialogueLayout(W8_DIALOGUE_LAYOUT_NONE);
    }
    EndNpcDialogueSession(false);
}

static void PromptNpcDispositionChange(void)
{
    W8MessageDialogBase* dialog = CreateMessageDialog();

    SetDialogPrompt(dialog, Data().strings[0x798]);
    dialog->on_complete = OnNpcDispositionPromptClosed;
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

static void OnNpcDispositionPromptClosed(bool accepted)
{
    if (accepted) {
        SetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc, W8_NPC_BAND_HOSTILE);
        CurrentGame().npc_dialogue.QueueQuote(0x18, false, false, false);
        CurrentGame().npc_dialogue.Queue(W8NpcCommand::close_and_resume_npc);
    }
}

static void EnterNpcServiceLayout(void)
{
    SwitchNpcDialogueLayout(W8_DIALOGUE_LAYOUT_NONE);
    OpenNpcDialogueMode1Layout();
}

/* Reload the control after text allocation/layout, as the expanded callers do. */
static void SetNpcDialogueAction(W8NpcDialogueControlSlot slot, int text_id,
                                 std::function<void()> callback)
{
    CurrentGame().npc_interaction.ui->actions[slot].text.SetText(Data().strings[text_id],
                                                                 g_wiz_text_font_secondary);
    CurrentGame().npc_interaction.ui->actions[slot].primary = std::move(callback);
}

/* Bring up the mode-2 dialogue layout: the option panels come up, the caption
   takes the NPC's name, and the five topics get their strings and callbacks. */
void ShowNpcDialogueTopicMenu(void)
{
    CurrentGame().npc_interaction.dialogue_layout = W8_DIALOGUE_LAYOUT_TOPIC_MENU;
    if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(4, true);
    SetNpcDialogueInputEnabled(2, true);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, true);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
        CurrentGame().npc_interaction.dialogue_npc->record->source_name, g_wiz_text_bold_font);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].dirty = true;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText(
        Data().strings[0x726], g_wiz_text_bold_font);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_1, 0x727, SelectNpcDialogueService);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_2, 0x728, SelectNpcDialogueTalk);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_3, 0x729, SelectNpcDialogueExit);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_4, 0x723, PromptNpcDispositionChange);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_5, 0x724, EnterNpcServiceLayout);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_6, 0x725, LeaveNpcDialogueLayout);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_7, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2,
                        !CurrentGame().npc_interaction.dialogue_npc->talk_cooldown_active);
    if (!CurrentGame().npc_interaction.dialogue_npc->trade_cooldown_active) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
    } else {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
    }
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    SelectTextBox(3);
}

/* Open the mode-3 NPC dialogue layout: the caption resolves through the
   alternate-name style, the full text-control grid is loaded with its labels
   and callbacks, the transcript controller replays and re-expands, the scroll
   widgets follow the expansion state and the party portrait regions are
   parked for the duration. */
/* Fold the mode-2 menu panels away: clear the option row, drop the caption
   text, remember the layout being left and unhide the dialogue window. */
void CloseNpcDialogueLayout(void)
{
    SetNpcDialogueInputEnabled(2, false);
    SetNpcPanelActive(0, false);
    SetNpcPanelActive(1, false);
    SetNpcPanelActive(4, false);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText(
        "", g_wiz_text_bold_font);
    SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
}

/* The "Trade" option button: hand the NPC's trade answer to ApplyNpcInteraction,
   then react to the disposition band - friendly queues the trade quote and
   returns to the transcript, neutral queues the neutral answer, hostile sets
   the hostile band and queues the refusal. */
static void SelectNpcDialogueService(void)
{
    unsigned char band;

    ApplyNpcInteraction(CurrentGame().npc_interaction.dialogue_npc, 1,
                        CurrentGame().npc_interaction.dialogue_speaker, 0, 0);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
    band = GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc);
    if (band == W8_NPC_BAND_FRIENDLY) {
        CurrentGame().npc_dialogue.QueueQuote(4, false, false, false);
        CurrentGame().npc_dialogue.Queue(W8NpcCommand::reopen_transcript);
        return;
    }
    if (band == W8_NPC_BAND_NEUTRAL) {
        CurrentGame().npc_dialogue.QueueQuote(6, false, false, false);
        return;
    }
    SetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc, W8_NPC_BAND_HOSTILE);
    CurrentGame().npc_dialogue.QueueQuote(0x18, false, false, false);
    CurrentGame().npc_dialogue.Queue(W8NpcCommand::close_and_resume_npc);
}

/* The "Talk" option button: friendly NPCs leave the dialogue outright,
   neutral ones queue the talk quote, hostile ones set the hostile band and
   queue the refusal. */
static void SelectNpcDialogueTalk(void)
{
    unsigned char band;

    ApplyNpcInteraction(CurrentGame().npc_interaction.dialogue_npc, 0,
                        CurrentGame().npc_interaction.dialogue_speaker, 0, 0);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
    band = GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc);
    if (band == W8_NPC_BAND_FRIENDLY) {
        HandleNpcDialogueDeparture(0);
        OpenNpcDialogueTranscriptLayout();
        return;
    }
    if (band == W8_NPC_BAND_NEUTRAL) {
        CurrentGame().npc_dialogue.QueueQuote(3, false, false, false);
        return;
    }
    SetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc, W8_NPC_BAND_HOSTILE);
    CurrentGame().npc_dialogue.QueueQuote(0x18, false, false, false);
    CurrentGame().npc_dialogue.Queue(W8NpcCommand::close_and_resume_npc);
}

/* The "Exit" option button: close the option row, mark the dialogue as leaving
   through the transcript and open the option layout for the farewell. */
static void SelectNpcDialogueExit(void)
{
    CloseNpcDialogueLayout();
    CurrentGame().npc_interaction.trade_mode = W8_NPC_TRADE_GIVE;
    CurrentGame().npc_interaction.reopen_topics = true;
    OpenNpcDialogueOptionLayout();
}

void SyncNpcDialogueTranscriptScrollButtons()
{
    if (CurrentGame().npc_interaction.ui->transcript->IsExpanded()) {
        SetNpcActionEnabled(W8_NPC_CONTROL_SCROLL_UP_BUTTON, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_SCROLL_DOWN_BUTTON, true);
    } else {
        SetNpcActionEnabled(W8_NPC_CONTROL_SCROLL_UP_BUTTON, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_SCROLL_DOWN_BUTTON, false);
    }
}

void OpenNpcDialogueTranscriptLayout(void)
{
    int index;

    CurrentGame().npc_interaction.dialogue_layout = W8_DIALOGUE_LAYOUT_TRANSCRIPT;
    if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(2, true);
    SetNpcPanelActive(3, true);
    SetNpcPanelActive(6, true);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText(
        "", g_wiz_text_bold_font);
    SetNpcDialogueInputEnabled(2, true);
    SetNpcDialogueInputEnabled(0, true);
    if (CurrentGame().npc_interaction.dialogue_npc->name_style == 0x32) {
        CurrentGame().status.monster_name_buffer = fmt::format(
            "Al-{}", CurrentGame().party.characters[CurrentGame().status.sedexus_party_slot].name);
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
            CurrentGame().status.monster_name_buffer, g_wiz_text_bold_font);
    } else {
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
            CurrentGame().npc_interaction.dialogue_npc->record->source_name, g_wiz_text_bold_font);
    }
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_1, 0x735, EnterNpcTradeOptions);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_2, 0x738, ShowNpcDialogueNotice);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_3, 0x724, EnterNpcServiceLayout);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_4, 0x737, RequestNpcJoinParty);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_5, 0x723, PromptNpcDispositionChange);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_6, 0x725, LeaveNpcDialogueLayout);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_SCROLL_UP_BUTTON].down =
        ScrollNpcDialogueUp;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_SCROLL_DOWN_BUTTON].down =
        ScrollNpcDialogueDown;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_12].primary =
        SubmitNpcDialogueKeyword;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_13].primary =
        RefreshNpcDialogueTranscript;
    SetNpcDialogueAction(W8_NPC_CONTROL_SORT_BUTTON, 0x740, SyncNpcDialogueListFilter);
    SetNpcDialogueAction(W8_NPC_CONTROL_PEOPLE_BUTTON, 0x743, SelectNpcPeopleTopics);
    SetNpcDialogueAction(W8_NPC_CONTROL_PLACES_BUTTON, 0x744, SelectNpcPlaceTopics);
    SetNpcDialogueAction(W8_NPC_CONTROL_ITEMS_BUTTON, 0x745, SelectNpcItemTopics);
    SetNpcDialogueAction(W8_NPC_CONTROL_MISC_BUTTON, 0x746, SelectNpcMiscTopics);
    SetNpcDialogueAction(W8_NPC_CONTROL_ALL_BUTTON, 0x747, SelectNpcDialogueCategoryAll);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_7, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
    CurrentGame().npc_interaction.ui->transcript->ShowTranscriptEntries();
    CurrentGame().npc_interaction.ui->transcript->SetTranscriptCategoryFilter(
        CurrentGame().npc_interaction.dialogue_category_filter);
    CurrentGame().npc_interaction.ui->transcript->SetTranscriptSorted(
        CurrentGame().npc_interaction.transcript_sorted);
    CurrentGame().npc_interaction.ui->transcript->Expand();
    SyncNpcDialogueTranscriptScrollButtons();
    SyncDialogueCategoryButtons();
    if (CurrentGame().npc_interaction.transcript_sorted) {
        SetNpcActionPressed(W8_NPC_CONTROL_SORT_BUTTON, true);
    } else {
        SetNpcActionPressed(W8_NPC_CONTROL_SORT_BUTTON, false);
    }
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    for (index = 0; index < 8; ++index) {
        if (CurrentGame().party.members[index].has_value()) {
            DisableMainGamePortraitSlot(index);

            MainGame()->edge_targets[index]->setEnabled(false);
        }
    }
    if (CurrentGame().npc_interaction.dialogue_npc->record->merchant != 0) {
        DisableNpcTradeActions();
    }
    if (CurrentGame().npc_interaction.dialogue_npc->name_style == 0x17) {
        DisableNpcTradeActions();
    }
    CurrentGame().npc_interaction.transcript_open_count++;
    SelectTextBox(3);
}

/* Tear down the mode-3 transcript layout: fold the dialogue text controller
   back up, drop the panels, then re-enable whichever occupied party rows
   still own region slots. */
static void CloseNpcDialogueTranscriptLayout(void)
{
    int index;

    CurrentGame().npc_interaction.ui->transcript->Collapse();
    CurrentGame().npc_interaction.ui->transcript->ClearTranscriptEntries();
    SetNpcDialogueInputEnabled(2, false);
    SetNpcDialogueInputEnabled(0, false);
    SetNpcPanelActive(0, false);
    SetNpcPanelActive(1, false);
    SetNpcPanelActive(2, false);
    SetNpcPanelActive(3, false);
    SetNpcPanelActive(6, false);
    SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
    CurrentGame().npc_interaction.ui->transcript->ClearBackground();
    EraseRegion(0x1dc, 0x11b, 0x269, 0x1c2);
    for (index = 0; index < 8; ++index) {
        if (CurrentGame().party.members[index].has_value()) {
            EnableMainGamePortraitSlot(index);

            MainGame()->edge_targets[index]->setEnabled(true);
        }
    }
}

static void RequestNpcJoinParty(void)
{
    unsigned int slot;

    ShowNotice(W8_FONT_PALETTE_BRONZE, Data().strings[0x748], 3,
               (MainGameHud()->text_box_right - MainGameHud()->text_box_left));
    if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    if (CurrentGame().npc_interaction.dialogue_npc->record->has_group != 0) {
        slot = FindFreePartySlot(0, 2);
        if (slot == 0xffffffff) {
            CurrentGame().npc_dialogue.QueueQuote(0xc, false, false, false);
            return;
        }
        if (CanNpcJoinParty(CurrentGame().npc_interaction.dialogue_npc)) {
            CurrentGame().npc_dialogue.QueueQuote(0xd, false, false, false);
            CurrentGame().npc_dialogue.Queue(
                W8NpcRecruit{CurrentGame().npc_interaction.dialogue_npc->name_style});
            return;
        }
    }
    CurrentGame().npc_dialogue.QueueQuote(0xb, false, false, false);
}

static void ShowNpcDialogueNotice(void)
{
    ShowNotice(W8_FONT_PALETTE_BRONZE, Data().strings[0x749], 3,
               (MainGameHud()->text_box_right - MainGameHud()->text_box_left));
    SetNpcDialogueHidden(1);
}

static void EnterNpcTradeOptions(void)
{
    CloseNpcDialogueTranscriptLayout();
    CurrentGame().npc_interaction.trade_mode = W8_NPC_TRADE_BUY;
    OpenNpcDialogueOptionLayout();
}

static void ScrollNpcDialogueUp(void)
{
    CurrentGame().npc_interaction.ui->transcript->HandleScrollUpCommand(false);
}

static void ScrollNpcDialogueDown(void)
{
    CurrentGame().npc_interaction.ui->transcript->HandleScrollDownCommand(false);
}

static void SubmitNpcDialogueKeyword(void)
{
    std::string keyword;

    keyword = NpcFieldText();
    LearnDialogueKeyword(keyword, W8_DIALOGUE_CATEGORY_ALL, false);
}

static void RefreshNpcDialogueTranscript(void)
{
    CurrentGame().npc_interaction.ui->transcript->RemoveSelectedTranscriptEntry();
    CurrentGame().npc_interaction.ui->transcript->Collapse();
    CurrentGame().npc_interaction.ui->transcript->Expand();
    SyncNpcDialogueTranscriptScrollButtons();
}

static void SyncNpcDialogueListFilter(void)
{
    if (CurrentGame()
            .npc_interaction.ui->actions[W8_NPC_CONTROL_SORT_BUTTON]
            .interaction.selected) {
        CurrentGame().npc_interaction.transcript_sorted = true;
    } else {
        CurrentGame().npc_interaction.transcript_sorted = false;
    }
    CurrentGame().npc_interaction.ui->transcript->SetTranscriptSorted(
        CurrentGame().npc_interaction.transcript_sorted);
}

static void SelectNpcDialogueTopics(W8NpcDialogueControlSlot button, W8DialogueCategory category)
{
    if (CurrentGame().npc_interaction.ui->actions[button].interaction.selected) {
        CurrentGame().npc_interaction.dialogue_category_filter = category;
        SyncDialogueCategoryButtons();
    }
    CurrentGame().npc_interaction.ui->transcript->SetTranscriptCategoryFilter(category);
}

static void SelectNpcPeopleTopics(void)
{
    SelectNpcDialogueTopics(W8_NPC_CONTROL_PEOPLE_BUTTON, W8_DIALOGUE_CATEGORY_PEOPLE);
}

static void SelectNpcPlaceTopics(void)
{
    SelectNpcDialogueTopics(W8_NPC_CONTROL_PLACES_BUTTON, W8_DIALOGUE_CATEGORY_PLACES);
}

static void SelectNpcItemTopics(void)
{
    SelectNpcDialogueTopics(W8_NPC_CONTROL_ITEMS_BUTTON, W8_DIALOGUE_CATEGORY_ITEMS);
}

static void SelectNpcDialogueCategoryAll(void)
{
    SelectNpcDialogueTopics(W8_NPC_CONTROL_ALL_BUTTON, W8_DIALOGUE_CATEGORY_ALL);
}

static void SelectNpcMiscTopics(void)
{
    SelectNpcDialogueTopics(W8_NPC_CONTROL_MISC_BUTTON, W8_DIALOGUE_CATEGORY_MISC);
}

static void ConfigureNpcTradeCommands()
{
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_1, 0x73d, SelectNpcBuyMode);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_2, 0x73e, SelectNpcSellMode);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_3, 0x73f, SelectNpcGiveMode);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_4, 0x72e, SelectNpcShopliftMode);
}

static void OpenNpcDialogueOptionLayout(void)
{
    std::string buffer;
    int index;

    CurrentGame().npc_interaction.dialogue_layout = W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX;
    if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText(
        "", g_wiz_text_bold_font);
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(5, true);
    SetNpcDialogueInputEnabled(2, true);
    SetNpcDialogueInputEnabled(1, true);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_34].text.SetText(
        Data().strings[0x72d], g_wiz_text_font_secondary);
    ConfigureNpcTradeCommands();
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_5, 0x72b, SelectNpcPartyItems);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_6, 0x72c, SelectNpcStockItems);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].primary = OpenNpcItemAssay;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].secondary = OpenNpcItemAssay;
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_31].primary = ConfirmNpcTradeSlot;
    buffer = fmt::format("{:d}g", CurrentGame().party.gold);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_33].text.SetText(
        buffer, g_wiz_text_font_secondary);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].primary = ConfirmNpcTradeItem;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_5].policy.primary_toggle =
        ButtonToggle::Release;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_6].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_5, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_6, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_1].policy.primary_toggle =
        ButtonToggle::Release;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_2].policy.primary_toggle =
        ButtonToggle::Release;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_3].policy.primary_toggle =
        ButtonToggle::Release;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_4].policy.primary_toggle =
        ButtonToggle::Release;
    UpdateNpcDialogueSubMode();
    CurrentGame().npc_interaction.pending_trade_toggle = true;
    if (CurrentGame().npc_interaction.reopen_topics) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, false);
    } else {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, true);
    }
    if (CurrentGame().npc_interaction.dialogue_npc->record->owns_stock == 0) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, false);
    }
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
    SelectTextBox(2);
    if (!CurrentGame().activity.conversation_aside) {
        ResetEditorStatusLine(2);
    }
    CurrentGame().npc_interaction.trade_filter = W8_NPC_TRADE_FILTER_NONE;
    for (index = 0; index < 6; ++index) {
        SetNpcActionEnabled(W8_NPC_CONTROL_OPTION_BUTTONS + index, false);
    }
    RebuildNpcTradeItemList(true);
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
}

static void SelectNpcTradePoolControls(W8NpcDialogueControlSlot previous,
                                       W8NpcDialogueControlSlot selected)
{
    SetNpcActionPressed(previous, false);
    CurrentGame().npc_interaction.ui->actions[previous].text.SetFontStateIndex(-1);
    CurrentGame().npc_interaction.ui->actions[previous].text.SetGeometryDirty();
    SetNpcActionPressed(selected, true);
    CurrentGame().npc_interaction.ui->actions[selected].text.SetFontStateIndex(3);
    CurrentGame().npc_interaction.ui->actions[selected].text.SetGeometryDirty();
}

static void SelectNpcTradeModeControls(W8NpcDialogueControlSlot selected,
                                       W8NpcDialogueControlSlot first,
                                       W8NpcDialogueControlSlot second,
                                       W8NpcDialogueControlSlot third)
{
    SetNpcActionPressed(selected, true);
    SetNpcActionPressed(first, false);
    SetNpcActionPressed(second, false);
    SetNpcActionPressed(third, false);
}

static void SetNpcTradeTitle(int text_id)
{
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
        Data().strings[text_id], g_wiz_text_bold_font);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetText(
        Data().strings[text_id], g_wiz_text_bold_font);
}

void UpdateNpcDialogueSubMode(void)
{
    switch (CurrentGame().npc_interaction.trade_mode) {
    case W8_NPC_TRADE_GIVE:
        SelectNpcTradeModeControls(W8_NPC_CONTROL_TEXT_3, W8_NPC_CONTROL_TEXT_1,
                                   W8_NPC_CONTROL_TEXT_2, W8_NPC_CONTROL_TEXT_4);
        SetNpcTradeTitle(0x73b);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_5, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, true);
        break;
    case W8_NPC_TRADE_SELL:
        SelectNpcTradeModeControls(W8_NPC_CONTROL_TEXT_2, W8_NPC_CONTROL_TEXT_1,
                                   W8_NPC_CONTROL_TEXT_3, W8_NPC_CONTROL_TEXT_4);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_5, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, true);
        SetNpcTradeTitle(0x73a);
        break;
    case W8_NPC_TRADE_BUY:
        SelectNpcTradeModeControls(W8_NPC_CONTROL_TEXT_1, W8_NPC_CONTROL_TEXT_2,
                                   W8_NPC_CONTROL_TEXT_3, W8_NPC_CONTROL_TEXT_4);
        SetNpcTradeTitle(0x73c);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_5, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, false);
        break;
    case W8_NPC_TRADE_SHOPLIFT:
        SelectNpcTradeModeControls(W8_NPC_CONTROL_TEXT_4, W8_NPC_CONTROL_TEXT_2,
                                   W8_NPC_CONTROL_TEXT_3, W8_NPC_CONTROL_TEXT_1);
        SetNpcTradeTitle(0x72e);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_5, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, false);
        break;
    default:
        break;
    }
    if (CurrentGame().npc_interaction.pending_trade_toggle &&
        (CurrentGame().npc_interaction.trade_mode == W8_NPC_TRADE_SELL ||
         CurrentGame().npc_interaction.trade_mode == W8_NPC_TRADE_GIVE)) {
        if (CurrentGame().npc_interaction.trade_pc_items) {
            SelectNpcTradePoolControls(W8_NPC_CONTROL_TEXT_6, W8_NPC_CONTROL_TEXT_5);
        } else {
            SelectNpcTradePoolControls(W8_NPC_CONTROL_TEXT_5, W8_NPC_CONTROL_TEXT_6);
        }
        CurrentGame().npc_interaction.pending_trade_toggle = false;
    }
    RebuildNpcTradeItemList(true);
}

/* Tear down the mode-4 option layout: the six option controls lose their
   secondary state and layout flags before everything is disabled. */
void CloseNpcDialogueOptionLayout(void)
{
    W8TextBuffer* text;

    ResetNpcDialogueItemEditor();
    if (!CurrentGame().activity.conversation_aside) {
        CurrentGame().npc_interaction.trade_mode = W8_NPC_TRADE_NONE;
    }
    SetNpcDialogueInputEnabled(2, false);
    SetNpcDialogueInputEnabled(1, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, true);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_1, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_2, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_3, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_4, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_5, false);
    text = &CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_5].text;
    text->SetFontStateIndex(-1);
    text->SetGeometryDirty();
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_6, false);
    text = &CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_6].text;
    text->SetFontStateIndex(-1);
    text->SetGeometryDirty();
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_1].policy.primary_toggle =
        ButtonToggle::None;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_2].policy.primary_toggle =
        ButtonToggle::None;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_3].policy.primary_toggle =
        ButtonToggle::None;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_4].policy.primary_toggle =
        ButtonToggle::None;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_5].policy.primary_toggle =
        ButtonToggle::None;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_6].policy.primary_toggle =
        ButtonToggle::None;
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_5, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, true);
    SetNpcPanelActive(0, false);
    SetNpcPanelActive(1, false);
    SetNpcPanelActive(5, false);
    SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
    CurrentGame().npc_interaction.reopen_topics = false;
    SelectTextBox(3);
    ScrollDialogueTextBoxToLine();
}

static void OpenNpcItemAssay(void)
{
    W8AssayDialog* dialog;
    const auto& image = CurrentGame().npc_interaction.ui->item_preview;

    if (CurrentGame().npc_interaction.trade_item != 0 && image &&
        image != CatalogArtwork::Load(0x1ac).spriteSheet()) {
        dialog = new W8AssayDialog(
            *CurrentGame().npc_interaction.trade_item,
            &CurrentGame().party.characters[CurrentGame().status.selected_character],
            {kInfoDialogX, 0x48});
        dialog->SetText("");

        dialog->on_close = [] {
            if (auto* hud = MainGameHud()) hud->repaint.everything = true;
        };
        OpenModal(std::unique_ptr<ModalContent>(dialog));
    }
}

static void SelectNpcPartyItems(void)
{
    ResetNpcDialogueItemEditor();
    SelectNpcTradePoolControls(W8_NPC_CONTROL_TEXT_6, W8_NPC_CONTROL_TEXT_5);
    CurrentGame().npc_interaction.trade_pc_items = true;
    RebuildNpcTradeItemList(true);
}

static void SelectNpcStockItems(void)
{
    ResetNpcDialogueItemEditor();
    SelectNpcTradePoolControls(W8_NPC_CONTROL_TEXT_5, W8_NPC_CONTROL_TEXT_6);
    CurrentGame().npc_interaction.trade_pc_items = false;
    RebuildNpcTradeItemList(true);
}

static void ConfirmNpcTradeSlot(void)
{
    int slot;

    if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        slot = MainGameHud()->selected_text_lines[2];
        if (slot != -1) {
            UpdateNpcTradeSelection(slot, 0, 0);
            if (CurrentGame().npc_interaction.trade_mode == W8_NPC_TRADE_GIVE && slot == 0) {
                OpenNpcGoldAmountDialog();
            } else if (CurrentGame().npc_interaction.trade_item->stack_count > 1) {
                OpenNpcTradeQuantityDialog();
            }
        }
    }
}

/* Open the mode-5 dialogue layout: the smaller control set shares the
   mode-4 labels and callbacks, the purse readout is refreshed and the
   mode-4-only controls are parked. */
/* The "Gold" split-amount entry: reset the item editor, dim the six option
   buttons and open the W8SplitAmountDialog seeded with the party purse. The
   result lands back through OnNpcTradeSplitDialogDestroy. */
static void OpenNpcGoldAmountDialog(void)
{
    W8SplitAmountDialog* dialog;

    ResetNpcDialogueItemEditor();
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_1, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_2, false);
    for (int index = 0; index < 6; ++index) {
        SetNpcActionEnabled(W8_NPC_CONTROL_OPTION_BUTTONS + index, false);
    }
    CurrentGame().npc_interaction.ui->panels[1].dirty = true;
    dialog = new W8SplitAmountDialog(CurrentGame().party.gold,
                                     {kSplitDialogOriginX, kSplitDialogOriginY});
    dialog->SetText("");

    dialog->on_complete = [](int amount) {
        CurrentGame().npc_interaction.trade_gold = amount;
        ShowNpcTradeGold();
    };
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

static void HighlightNpcTradeQuantity()
{
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.m_highlighted = true;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetGeometryDirty();
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].text.m_highlighted = true;
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetGeometryDirty();
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_35].dirty = true;
    g_trade_highlight_tick = MonotonicMilliseconds();
}

/* Resolve and select one trade-list row. trade_mode picks the pool: mode 2's
   leading row is the purse readout, the mode-3 views iterate either the
   selected character's backpack or the shared party item pool, and modes 4/5
   go through the NPC's own inventory. pick/commit drive quantity stepping,
   the click chime and the highlight tick. */
static void ShowSelectedNpcTradeItem(const W8ItemInstance* item)
{
    std::string count_text;
    std::string_view text;
    CurrentGame().npc_interaction.ui->item_preview = ItemSprites(item->iItemNo);
    if (item->stack_count < 2) {
        text = "";
    } else {
        count_text = fmt::format("{:d}", CurrentGame().npc_interaction.trade_quantity);
        text = count_text;
    }
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
        text, g_wiz_text_font_secondary);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
}

static void UpdatePartyTradeQuantity(const W8ItemInstance* item, int row, bool decrement)
{
    if (Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        CurrentGame().npc_interaction.trade_quantity = item->stack_count;
    } else if (CurrentGame().npc_interaction.selected_trade_row == row) {
        if (!decrement) {
            if (Data().items[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
                ++CurrentGame().npc_interaction.trade_quantity;
                if (item->stack_count < CurrentGame().npc_interaction.trade_quantity) {
                    CurrentGame().npc_interaction.trade_quantity = item->stack_count;
                }
            }
        } else {
            --CurrentGame().npc_interaction.trade_quantity;
            if (CurrentGame().npc_interaction.trade_quantity == 0) {
                CurrentGame().npc_interaction.trade_quantity = 1;
            }
        }
    } else {
        CurrentGame().npc_interaction.trade_quantity = 1;
    }
}

static W8ItemInstance* ResolveNpcTradeRow(int index, bool pick, signed char decrement,
                                          signed char commit)
{
    auto& state = CurrentGame().npc_interaction;
    if (index < 0 || std::cmp_greater_equal(index, state.trade_rows.size())) return nullptr;
    const auto selection = state.trade_rows[index];
    if (pick) state.trade_selection = selection;
    if (selection.source == W8TradeSource::Gold) {
        state.trade_gold = CurrentGame().party.gold;
        return nullptr;
    }
    auto* item = ResolveTradeSelection(state.dialogue_npc, selection);
    if (!item) {
        if (pick) state.trade_selection.reset();
        return nullptr;
    }
    if (pick) {
        if (commit) {
            const int previous_quantity = state.trade_quantity;
            UpdatePartyTradeQuantity(item, index, decrement != 0);
            if (selection.source == W8TradeSource::NpcStock && item->stack_count &&
                state.trade_quantity != previous_quantity)
                HighlightNpcTradeQuantity();
        }
        state.selected_trade_row = index;
        if (commit) wiz8::sound::play(g_button_click_1);
        ShowSelectedNpcTradeItem(item);
    }
    return item;
}

/* Whether the current trade filter rejects `item`: the Zant NPC hides item
   0x290 until fact 0x3c, the trade_filter low bits gate character/party
   usability, and bits 0x3c select the accepted equipment slot group. */
bool NpcTradeItemAllowed(W8ItemInstance* item)
{
    W8ItemEquipSlotGroup group;

    if (CurrentGame().npc_interaction.dialogue_npc->name_style == ',' &&
        GetFact(W8_FACT_ALIGNMENT_UMPANI) == 0 && item->iItemNo != 0x290) {
        return true;
    }
    if (CurrentGame().npc_interaction.trade_filter == 0) {
        return false;
    }
    if (item->iItemNo != -1) {
        if ((CurrentGame().npc_interaction.trade_filter & W8_NPC_TRADE_USABLE_BY_CHARACTER) == 0) {
            if ((CurrentGame().npc_interaction.trade_filter & W8_NPC_TRADE_USABLE_BY_PARTY) != 0 &&
                AnyPartyMemberCanUseItem(item->iItemNo)) {
                return true;
            }
        } else {
            if (!CanCharacterUseItem(
                    Data(),
                    &CurrentGame().party.characters[CurrentGame().status.selected_character],
                    item->iItemNo)) {
                return true;
            }
        }
        if ((CurrentGame().npc_interaction.trade_filter & W8_NPC_TRADE_CATEGORY_MASK) == 0) {
            return false;
        }
        group = GetItemEquipSlotGroup(item->iItemNo);
        if (group == W8_ITEM_EQUIP_GROUP_HAND) {
            if ((CurrentGame().npc_interaction.trade_filter & W8_NPC_TRADE_HAND) != 0) {
                return false;
            }
        } else if (group == W8_ITEM_EQUIP_GROUP_BODY) {
            if ((CurrentGame().npc_interaction.trade_filter & W8_NPC_TRADE_BODY) != 0) {
                return false;
            }
        } else if (group == W8_ITEM_EQUIP_GROUP_ACCESSORY) {
            if ((CurrentGame().npc_interaction.trade_filter & W8_NPC_TRADE_ACCESSORY) != 0) {
                return false;
            }
        } else if ((CurrentGame().npc_interaction.trade_filter & W8_NPC_TRADE_OTHER) != 0) {
            return false;
        }
    }
    return true;
}

static void OpenNpcDialogueMode5Layout(void)
{
    std::string buffer;
    int index;

    CurrentGame().npc_interaction.dialogue_layout = W8_DIALOGUE_LAYOUT_TRADE;
    if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(5, true);
    SetNpcDialogueInputEnabled(2, true);
    SetNpcDialogueInputEnabled(1, true);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
        Data().strings[0x739], g_wiz_text_bold_font);
    ConfigureNpcTradeCommands();
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_31].primary = ConfirmNpcTradeSlot;
    buffer = fmt::format("{:d}g", CurrentGame().party.gold);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_33].text.SetText(
        buffer, g_wiz_text_font_secondary);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_37].primary =
        RestockNpcTradeStock;
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_5, false);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_6, false);
    CurrentGame().npc_interaction.trade_filter = W8_NPC_TRADE_FILTER_NONE;
    for (index = 0; index < 6; ++index) {
        SetNpcActionEnabled(W8_NPC_CONTROL_OPTION_BUTTONS + index, false);
    }
    if (CurrentGame().npc_interaction.dialogue_npc->record->owns_stock == 0) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
    }
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    SelectTextBox(2);
    if (!CurrentGame().activity.conversation_aside) {
        ResetEditorStatusLine(2);
    }
}

/* Tear down the mode-5 dialogue layout and retire the current mode. */
static void CloseNpcDialogueMode5Layout(void)
{
    SetNpcDialogueInputEnabled(2, false);
    SetNpcDialogueInputEnabled(1, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, true);
    SetNpcPanelActive(0, false);
    SetNpcPanelActive(1, false);
    SetNpcPanelActive(5, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
    SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
}

/* Re-arm the six trade-filter option buttons after the list contents were
   rebuilt. */
void EnableNpcTradeFilterButtons(void)
{
    for (unsigned int index = 0; index <= 5; ++index) {
        SetNpcActionEnabled(W8_NPC_CONTROL_OPTION_BUTTONS + index, true);
    }
}

/* The six trade-filter option callbacks share one shape: when the button's
   secondary state is raised it becomes the exclusive slot-group bit in
   trade_filter (the previously active sibling is dimmed and cleared), and when it
   is lowered the bit comes off again; RebuildNpcTradeItemList(1) refreshes the list. */
static void UpdateNpcTradeFilterBits(int clear_mask, int set_mask = 0)
{
    const int bits = static_cast<int>(CurrentGame().npc_interaction.trade_filter);
    CurrentGame().npc_interaction.trade_filter =
        static_cast<W8NpcTradeFilter>((bits & ~clear_mask) | set_mask);
}

static void ToggleNpcTradeCategory(int button, W8NpcTradeFilter filter)
{
    const auto& control =
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + button];
    if (control.interaction.selected) {
        int previous = CurrentGame().npc_interaction.trade_filter & W8_NPC_TRADE_CATEGORY_MASK;
        if (previous != filter) {
            int previous_button;
            switch (previous) {
            case W8_NPC_TRADE_HAND:
                previous_button = 0;
                break;
            case W8_NPC_TRADE_BODY:
                previous_button = 3;
                break;
            case W8_NPC_TRADE_ACCESSORY:
                previous_button = 1;
                break;
            case W8_NPC_TRADE_OTHER:
                previous_button = 4;
                break;
            default:
                previous_button = -1;
                break;
            }
            if (previous_button != -1) {
                SetNpcActionPressed(W8_NPC_CONTROL_OPTION_BUTTONS + previous_button, false);
                UpdateNpcTradeFilterBits(previous);
            }
        }
        UpdateNpcTradeFilterBits(0, filter);
    } else {
        UpdateNpcTradeFilterBits(filter);
    }
    RebuildNpcTradeItemList(true);
}

/* The "usable by the selected character" toggle is exclusive with the
   "usable by anyone" toggle: option control 2 lowers bit 0x40 and raises
   bit 1 while pressed, and drops bit 1 when released. */
static void ToggleNpcCharacterUsabilityFilter(void)
{
    if (CurrentGame()
            .npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 2]
            .interaction.selected) {
        SetNpcActionPressed(W8_NPC_CONTROL_OPTION_BUTTONS + 5, false);
        UpdateNpcTradeFilterBits(W8_NPC_TRADE_USABLE_BY_PARTY, W8_NPC_TRADE_USABLE_BY_CHARACTER);
        RebuildNpcTradeItemList(true);
        return;
    }
    UpdateNpcTradeFilterBits(W8_NPC_TRADE_USABLE_BY_CHARACTER);
    RebuildNpcTradeItemList(true);
}

static void ToggleNpcPartyUsabilityFilter(void)
{
    if (CurrentGame()
            .npc_interaction.ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 5]
            .interaction.selected) {
        SetNpcActionPressed(W8_NPC_CONTROL_OPTION_BUTTONS + 2, false);
        UpdateNpcTradeFilterBits(W8_NPC_TRADE_USABLE_BY_CHARACTER, W8_NPC_TRADE_USABLE_BY_PARTY);
        RebuildNpcTradeItemList(true);
        return;
    }
    UpdateNpcTradeFilterBits(W8_NPC_TRADE_USABLE_BY_PARTY);
    RebuildNpcTradeItemList(true);
}

static void SelectNpcSellMode(void)
{
    CurrentGame().npc_interaction.trade_mode = W8_NPC_TRADE_SELL;
    UpdateNpcDialogueSubMode();
}

static void SelectNpcGiveMode(void)
{
    CurrentGame().npc_interaction.trade_mode = W8_NPC_TRADE_GIVE;
    UpdateNpcDialogueSubMode();
}

static void SelectNpcShopliftMode(void)
{
    CurrentGame().npc_interaction.trade_mode = W8_NPC_TRADE_SHOPLIFT;
    UpdateNpcDialogueSubMode();
}

/* Open the mode-1 service layout: the caption and three service options, the
   mode-4-only controls parked, and each option enabled from what the selected
   character can actually cast or use. */
static void OpenNpcDialogueMode1Layout(void)
{
    CurrentGame().npc_interaction.dialogue_layout = W8_DIALOGUE_LAYOUT_SERVICES;
    if (CurrentGame().npc_interaction.dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(4, true);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText(
        "", g_wiz_text_bold_font);
    SetNpcDialogueInputEnabled(2, true);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
        Data().strings[0x72f], g_wiz_text_bold_font);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_1, 0x730, RequestNpcSpellService3);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_2, 0x732, RequestNpcSpellService41);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_4, 0x733, RequestNpcCharacterService);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_4].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_4, 0x7ca);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_3, false);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_5, false);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_6, false);
    if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_SERVICES) {
        RefreshNpcServiceAvailability(CurrentGame().status.selected_character);
    }
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    SelectTextBox(2);
    if (!CurrentGame().activity.conversation_aside) {
        ResetEditorStatusLine(2);
    }
}

/* Tear down the mode-1 dialogue layout; unless the conversation is being set
   aside, the mode is retired as well. */
static void CloseNpcDialogueMode1Layout(void)
{
    SetNpcDialogueInputEnabled(2, false);
    SetNpcDialogueInputEnabled(1, false);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_4].policy.primary_toggle =
        ButtonToggle::None;
    SetNpcPanelActive(0, false);
    SetNpcPanelActive(1, false);
    SetNpcPanelActive(4, false);
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_4, -1);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, true);
    if (!CurrentGame().activity.conversation_aside) {
        SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
    }
}

static void RequestNpcSpellService3(void)
{
    int location = CurrentGame().npc_interaction.target_location_id;
    W8NpcDialogueLayout mode = CurrentGame().npc_interaction.dialogue_layout;

    CurrentGame().activity.conversation_aside = true;
    CloseNpcDialogueMode1Layout();
    EndNpcDialogueSession(false);
    BeginSpellCast(W8_SPELL_CHARM, location, mode);
}

static void RequestNpcSpellService41(void)
{
    int location = CurrentGame().npc_interaction.target_location_id;
    W8NpcDialogueLayout mode = CurrentGame().npc_interaction.dialogue_layout;

    CurrentGame().activity.conversation_aside = true;
    CloseNpcDialogueMode1Layout();
    EndNpcDialogueSession(false);
    BeginSpellCast(W8_SPELL_MINDREAD, location, mode);
}

static void RequestNpcCharacterService(void)
{
    CurrentGame().activity.conversation_aside = true;
    CloseNpcDialogueMode1Layout();
    EndNpcDialogueSession(false);
    OpenUseItemSelectView(CurrentGame().status.selected_character);
}

/* Pick the index-th activatable item on the dialogue character: the twelve
   equipment slots first, then the eight backpack slots. The hit refreshes the
   dialogue_text_16c preview image and its stack-count text. */
static W8ItemInstance* GetNpcTradeSlotItem(int index)
{
    std::string count_text;
    std::string_view text;
    W8Character* character;
    int hit;
    int slot;

    character = &CurrentGame().party.characters[CurrentGame().status.selected_character];
    hit = 0;
    if (CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_4].interaction.selected ==
        0) {
        return 0;
    }
    for (slot = 0; slot < 12; ++slot) {
        if (character->EquippedItem[slot].iItemNo != -1 &&
            CanCharacterActivateItem(character, &character->EquippedItem[slot])) {
            if (index == hit) {
                CurrentGame().npc_interaction.ui->item_preview =
                    ItemSprites(character->EquippedItem[slot].iItemNo);
                if (character->EquippedItem[slot].stack_count < 2) {
                    text = "";
                } else {
                    count_text = fmt::format("{:d}", character->EquippedItem[slot].stack_count);
                    text = count_text;
                }
                CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
                    text, g_wiz_text_font_secondary);
                CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
                return &character->EquippedItem[slot];
            }
            ++hit;
        }
    }
    for (slot = 0; slot < 8; ++slot) {
        if (character->backpack[slot].iItemNo != -1 &&
            CanCharacterActivateItem(character, &character->backpack[slot])) {
            if (index == hit) {
                CurrentGame().npc_interaction.ui->item_preview =
                    ItemSprites(character->backpack[slot].iItemNo);
                if (character->backpack[slot].stack_count < 2) {
                    text = "";
                } else {
                    count_text = fmt::format("{:d}", character->backpack[slot].stack_count);
                    text = count_text;
                }
                CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
                    text, g_wiz_text_font_secondary);
                CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
                return &character->backpack[slot];
            }
            ++hit;
        }
    }
    return 0;
}

/* Hand the player's reply to the dialogue while one is pending (AnswerNpcReply
   resolves it); a nonzero echo posts the reply text back as a notice. */
static void HandleNpcDialogueReply(std::string_view text, bool echo)
{
    std::string notice;

    if (CurrentGame().npc_dialogue.reply() == W8NpcReplyState::none) {
        return;
    }
    CurrentGame().npc_dialogue.Answer(MakeScriptContext(CurrentGame()), text);
    if (echo) {
        notice = fmt::format("{}...", text);
        ShowNotice(W8_FONT_PALETTE_BRONZE, notice, 3,
                   (MainGameHud()->text_box_right - MainGameHud()->text_box_left));
    }
}

/* Copy the next space-separated word of `text` into `word`. Returns the text
   after the word, or 0 when no word is left. Retail expands this inline at all
   five uses in HandleNpcDialogueInput and keeps no out-of-line copy. */
static bool ReadNextWord(std::string_view& text, std::string& word)
{
    const auto start = text.find_first_not_of(' ');
    if (start == std::string_view::npos) {
        text = {};
        word.clear();
        return false;
    }
    text.remove_prefix(start);
    const auto end = text.find(' ');
    word = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end);
    return true;
}

/* The NPC dialogue's typed-input processor. Strips punctuation, tokenizes into
   words, and resolves the text to one or two quote ids: the "join"-style
   keywords go straight to RequestNpcJoinParty, name/place prefixes are stripped
   and resolved through FindNpcNameOrPlaceQuote, and otherwise every word pair
   then every single word is tried through FindNpcScriptQuoteByKeyword. Unresolved input
   shows a fallback notice built from the random reply tables; resolved input
   queues the script line(s). */
void HandleNpcDialogueInput(void)
{
    std::string field_text;
    std::string word;
    std::string buf;
    std::string word2;
    std::string notice;
    std::string_view cursor;
    bool has_word;
    int quote_id = -1;
    int second_quote_id = -1;
    bool show_fallback = true;
    bool plain_text = true;
    bool echo = false;
    int quote;
    int word_count;
    int matches;

    field_text = NpcFieldText();
    if (auto* fields = TextFieldGroup::Focused()) fields->ClearFocus();
    StripNpcKeywordPunctuation(field_text);
    if (field_text.size() == 0 && CurrentGame().npc_dialogue.reply() == W8NpcReplyState::none) {
        return;
    }
    if (CurrentGame().npc_dialogue.reply() != W8NpcReplyState::none) {
        HandleNpcDialogueReply(field_text, true);
        return;
    }
    word_count = 0;
    cursor = field_text;
    while (!cursor.empty()) {
        has_word = ReadNextWord(cursor, word);
        if (has_word) {
            ++word_count;
        }
    }
    if (word_count > 2) {
        show_fallback = false;
    }
    if (CompareTextIgnoreAsciiCase(
            std::string_view(field_text).substr(0, wiz8::text::codepoint_prefix(field_text, 4)),
            std::string_view(Data().strings[0x76b])
                .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x76b], 4))) == 0 ||
        CompareTextIgnoreAsciiCase(
            std::string_view(field_text).substr(0, wiz8::text::codepoint_prefix(field_text, 7)),
            std::string_view(Data().strings[0x76c])
                .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x76c], 7))) == 0) {
        RequestNpcJoinParty();
        return;
    }
    if (CurrentGame().npc_interaction.where_is_query) {
        if (CompareTextIgnoreAsciiCase(
                std::string_view(field_text).substr(0, wiz8::text::codepoint_prefix(field_text, 9)),
                std::string_view(Data().strings[0x76d])
                    .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x76d], 9))) == 0 ||
            CompareTextIgnoreAsciiCase(
                std::string_view(field_text)
                    .substr(0, wiz8::text::codepoint_prefix(field_text, 0xa)),
                std::string_view(Data().strings[0x76e])
                    .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x76e], 0xa))) == 0 ||
            CompareTextIgnoreAsciiCase(
                std::string_view(field_text).substr(0, wiz8::text::codepoint_prefix(field_text, 8)),
                std::string_view(Data().strings[0x76f])
                    .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x76f], 8))) == 0) {
            buf = field_text;
        } else {
            buf = fmt::sprintf(Data().strings[0x76a], field_text);
        }
        quote = FindNpcScriptQuoteByKeyword(TranslateDialogueKeyword(buf), 0, 0);
        if (quote != -1) {
            plain_text = false;
            quote_id = quote;
        }
    } else {
        buf = field_text;
        quote = FindNpcScriptQuoteByKeyword(TranslateDialogueKeyword(buf), 0, 0);
        if (quote != -1) {
            quote_id = quote;
        }
    }

    if (quote_id == -1) {
        buf = field_text;
        if (CompareTextIgnoreAsciiCase(
                std::string_view(buf).substr(0, wiz8::text::codepoint_prefix(buf, 0xb)),
                std::string_view(Data().strings[0x770])
                    .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x770], 0xb))) == 0) {
            field_text = buf.substr(wiz8::text::codepoint_prefix(buf, 0xb));
            show_fallback = false;
        } else if (CompareTextIgnoreAsciiCase(
                       std::string_view(buf).substr(0, wiz8::text::codepoint_prefix(buf, 9)),
                       std::string_view(Data().strings[0x76d])
                           .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x76d], 9))) ==
                       0 ||
                   CompareTextIgnoreAsciiCase(
                       std::string_view(buf).substr(0, wiz8::text::codepoint_prefix(buf, 0xa)),
                       std::string_view(Data().strings[0x76e])
                           .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x76e], 0xa))) ==
                       0 ||
                   CompareTextIgnoreAsciiCase(
                       std::string_view(buf).substr(0, wiz8::text::codepoint_prefix(buf, 8)),
                       std::string_view(Data().strings[0x76f])
                           .substr(0, wiz8::text::codepoint_prefix(Data().strings[0x76f], 8))) ==
                       0) {
            field_text = buf.substr(wiz8::text::codepoint_prefix(buf, 9));
            plain_text = false;
            show_fallback = false;
            quote_id =
                FindNpcNameOrPlaceQuote(CurrentGame().npc_interaction.dialogue_npc, field_text);
            if (quote_id == -1) {
                quote_id = 0x76;
            }
        } else if (CurrentGame().npc_interaction.where_is_query) {
            plain_text = false;
            quote_id =
                FindNpcNameOrPlaceQuote(CurrentGame().npc_interaction.dialogue_npc, field_text);
            if (quote_id == -1) {
                quote_id = 0x76;
            }
        } else {
            plain_text = true;
        }
    }

    /* Try each adjacent word pair. */
    if (quote_id == -1) {
        cursor = field_text;
        has_word = ReadNextWord(cursor, word);
        while (has_word && ReadNextWord(cursor, word2)) {
            buf = fmt::format("{} {}", word, word2);
            quote = FindNpcScriptQuoteByKeyword(TranslateDialogueKeyword(buf), 0, 0);
            if (quote != -1) {
                quote_id = quote;
                break;
            }
            word = word2;
        }
    }

    /* Then each single word: more than two hits echoes the text, otherwise the
       first hit (and a different second one) answer. */
    if (quote_id == -1) {
        matches = 0;
        cursor = field_text;
        while (!cursor.empty()) {
            has_word = ReadNextWord(cursor, word);
            if (has_word &&
                FindNpcScriptQuoteByKeyword(TranslateDialogueKeyword(word), 0, 0) != -1) {
                ++matches;
            }
        }
        if (matches > 2) {
            quote_id = 0x20;
            echo = true;
        } else if (matches > 0) {
            int found_count = 0;
            cursor = field_text;
            for (;;) {
                has_word = ReadNextWord(cursor, word);
                if (!has_word) {
                    break;
                }
                quote = FindNpcScriptQuoteByKeyword(TranslateDialogueKeyword(word), 0, 0);
                if (quote == -1) {
                    continue;
                }
                ++found_count;
                if (found_count == 1) {
                    quote_id = quote;
                } else if (found_count == 2 && quote != quote_id) {
                    second_quote_id = quote;
                }
                if (matches == 1) {
                    break;
                }
            }
        }
    }

    if (quote_id >= 0x59 && quote_id < 0x69 && second_quote_id == -1) {
        echo = true;
    }
    if (!echo && show_fallback) {
        const auto roll = CurrentGame().rng.below(5);
        const auto* replies = plain_text ? g_dialogue_fallback_ids0 : g_dialogue_fallback_ids1;
        const char punctuation = roll >= (plain_text ? 2u : 3u) ? '?' : '.';
        notice = fmt::format("{} {}{}", Data().strings[replies[roll]], field_text, punctuation);
        ShowNotice(W8_FONT_PALETTE_BRONZE, notice, 3,
                   (MainGameHud()->text_box_right - MainGameHud()->text_box_left));
    } else {
        ShowNotice(W8_FONT_PALETTE_BRONZE, field_text, 3,
                   (MainGameHud()->text_box_right - MainGameHud()->text_box_left));
    }
    if (quote_id == -1) {
        CurrentGame().npc_dialogue.QueueQuote(CurrentGame().rng.below(2) + 0x23, false, false,
                                              false);
        return;
    }
    if (quote_id == 0x59 || second_quote_id == 0x59) {
        CurrentGame().npc_dialogue.QueueQuote(0x59, true, false, false);
        return;
    }
    CurrentGame().npc_dialogue.QueueQuote(quote_id, true, false, false);
    if (second_quote_id == -1) {
        return;
    }
    CurrentGame().npc_dialogue.QueueQuote(0x21, false, false, false);
    CurrentGame().npc_dialogue.QueueQuote(second_quote_id, true, false, false);
}

void HandleNpcDialogueKeyEvent(const InputEvent* event)
{
    W8NpcInteractionState* state;
    W8MGSCommand command;

    if (ShouldDeferCharacterEventForNpcScript(false)) {
        return;
    }
    state = &CurrentGame().npc_interaction;
    if (state->modal_dialog_open && event->key != SDLK_ESCAPE) {
        return;
    }
    switch (event->key) {
    case SDLK_UP:
        if (state->dialogue_layout != W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
            return;
        }
        ScrollTextBoxUp(1);
        NpcDialogueTextBoxWheelAt(0, event->position.y, true);
        return;
    case SDLK_DOWN:
        if (state->dialogue_layout != W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
            return;
        }
        ScrollTextBoxDown(1);
        NpcDialogueTextBoxWheelAt(0, event->position.y, true);
        return;
    case SDLK_ESCAPE:
        if (state->dialogue_hidden != 0) {
            SetNpcDialogueHidden(0);
            return;
        }
        if (IsNpcScriptSessionActive()) {
            TryFinishNpcVoicePlayback(CurrentGame().status, true);
            return;
        }
        if (NpcEditingText()) {
            SetNpcFieldText("");
            if (auto* fields = TextFieldGroup::Focused()) fields->ClearFocus();
            return;
        }
        if (CurrentGame().npc_dialogue.reply() != W8NpcReplyState::none ||
            CurrentGame().npc_dialogue.has_queued_actions()) {
            return;
        }
        switch (CurrentGame().npc_interaction.dialogue_layout) {
        case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
            CurrentGame().npc_interaction.suppress_parting_reaction = false;
            CurrentGame().npc_dialogue.QueueQuote(0x5c, false, false, false);
            return;
        case W8_DIALOGUE_LAYOUT_TOPIC_MENU:
            EndNpcDialogueSession(false);
            return;
        case W8_DIALOGUE_LAYOUT_SERVICES: {
            W8NpcDialogueLayout prev = CurrentGame().npc_interaction.previous_dialogue_layout;
            CloseNpcDialogueMode1Layout();
            if (prev == W8_DIALOGUE_LAYOUT_TOPIC_MENU) {
                ShowNpcDialogueTopicMenu();
                return;
            }
            if (prev == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
                OpenNpcDialogueTranscriptLayout();
            }
            return;
        }
        case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX: {
            bool cursor = CurrentGame().npc_interaction.reopen_topics;
            CloseNpcDialogueOptionLayout();
            if (cursor) {
                ShowNpcDialogueTopicMenu();
            } else {
                OpenNpcDialogueTranscriptLayout();
            }
            return;
        }
        case W8_DIALOGUE_LAYOUT_TRADE:
            CloseNpcDialogueMode5Layout();
            OpenNpcDialogueTranscriptLayout();
            return;
        default:
            return;
        }
    case SDLK_RETURN:
        if (NpcEditingText()) {
            HandleNpcDialogueInput();
        } else {
            if (auto* fields = TextFieldGroup::Focused()) fields->Focus(0);
        }
        return;
    default:
        command = g_mgs_keyboard.FindCommandForEvent(event);
        if (command == W8_MGS_COMMAND_JOURNAL) {
            DispatchMGSCommand(command);
            return;
        }
        if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
            return;
        }
        if (command <= W8_MGS_COMMAND_INVENTORY) {
            if (command != W8_MGS_COMMAND_INVENTORY && command != W8_MGS_COMMAND_QUIT_GAME) {
                return;
            }
        } else if (command < W8_MGS_COMMAND_SELECT_RECRUITED_1 ||
                   command > W8_MGS_COMMAND_SELECT_PC_6) {
            return;
        }
        DispatchMGSCommand(command);
        return;
    }
}

void SetDialogueFieldKeyword(std::string keyword, bool append)
{
    std::string field_text;
    std::string combined;

    field_text = NpcFieldText();
    StripNpcKeywordPunctuation(keyword);
    if (field_text.size() != 0 && append) {
        combined = fmt::format("{} {}", field_text, keyword);
        SetNpcFieldText(combined);
    } else {
        SetNpcFieldText(keyword);
    }
}

static bool IsDialoguePlaceKeyword(std::string_view name)
{
    for (int index = 0; index < g_dialogue_place_keyword_count; ++index) {
        if (CompareTextIgnoreAsciiCase(name, Data().strings[g_dialogue_place_keyword_ids[index]]) ==
            0) {
            return true;
        }
    }
    return false;
}

/* Restate the five transcript category buttons so only the active
   dialogue_category_filter's button shows its secondary state. */
void SyncDialogueCategoryButtons(void)
{
    W8DialogueCategory category = CurrentGame().npc_interaction.dialogue_category_filter;
    switch (category) {
    case W8_DIALOGUE_CATEGORY_ITEMS:
    case W8_DIALOGUE_CATEGORY_PEOPLE:
    case W8_DIALOGUE_CATEGORY_PLACES:
    case W8_DIALOGUE_CATEGORY_MISC:
    case W8_DIALOGUE_CATEGORY_ALL:
        break;
    default:
        return;
    }
    if (category == W8_DIALOGUE_CATEGORY_PEOPLE) {
        SetNpcActionPressed(W8_NPC_CONTROL_PEOPLE_BUTTON, true);
    } else {
        SetNpcActionPressed(W8_NPC_CONTROL_PEOPLE_BUTTON, false);
    }
    if (category == W8_DIALOGUE_CATEGORY_PLACES) {
        SetNpcActionPressed(W8_NPC_CONTROL_PLACES_BUTTON, true);
    } else {
        SetNpcActionPressed(W8_NPC_CONTROL_PLACES_BUTTON, false);
    }
    if (category == W8_DIALOGUE_CATEGORY_ITEMS) {
        SetNpcActionPressed(W8_NPC_CONTROL_ITEMS_BUTTON, true);
    } else {
        SetNpcActionPressed(W8_NPC_CONTROL_ITEMS_BUTTON, false);
    }
    if (category == W8_DIALOGUE_CATEGORY_MISC) {
        SetNpcActionPressed(W8_NPC_CONTROL_MISC_BUTTON, true);
    } else {
        SetNpcActionPressed(W8_NPC_CONTROL_MISC_BUTTON, false);
    }
    if (category == W8_DIALOGUE_CATEGORY_ALL) {
        SetNpcActionPressed(W8_NPC_CONTROL_ALL_BUTTON, true);
    } else {
        SetNpcActionPressed(W8_NPC_CONTROL_ALL_BUTTON, false);
    }
}

/* A confirmed purchase returns the NPC dialogue to the layout appropriate
   for the NPC's disposition. A cancelled dialog leaves the layout alone. */
static void OnNpcTradeDialogClosed(bool accepted)
{
    if (!accepted) {
        return;
    }
    ConfirmNpcTradePurchase();
    CloseActiveNpcDialogueLayout();
    if (GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc) == W8_NPC_BAND_FRIENDLY) {
        OpenNpcDialogueTranscriptLayout();
    } else {
        ShowNpcDialogueTopicMenu();
    }
}

static void ConfirmNpcTradePurchase(void)
{
    if (GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc) == W8_NPC_BAND_FRIENDLY) {
        SpendPartyGold(CurrentGame().party, CurrentGame().npc_interaction.trade_gold);
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
        CurrentGame().npc_dialogue.QueueQuote(0x10, false, false, false);
        RebuildNpcTradeItemList(false);
        return;
    }
    if (NpcRecordHasTradePool(CurrentGame().npc_interaction.dialogue_npc)) {
        ApplyNpcInteraction(CurrentGame().npc_interaction.dialogue_npc, 2,
                            CurrentGame().npc_interaction.dialogue_speaker, 0,
                            CurrentGame().npc_interaction.trade_gold);
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
        CurrentGame().npc_dialogue.QueueQuote(
            GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc) ==
                    W8_NPC_BAND_FRIENDLY
                ? 0x10
                : 7,
            false, false, false);
        RebuildNpcTradeItemList(false);
        return;
    }
    CurrentGame().npc_dialogue.QueueQuote(7, false, false, false);
}

static unsigned char HandleNpcDialogueItem(W8ItemInstance* item)
{
    W8MessageDialogBase* dialog;
    std::string message;
    bool result;

    result = true;
    if (CurrentGame().npc_interaction.trade_gold != 0 && item == 0) {
        if (CurrentGame().npc_interaction.trade_gold !=
            static_cast<int>(CurrentGame().party.gold)) {
            ConfirmNpcTradePurchase();
            return 1;
        }
        dialog = CreateMessageDialog();
        dialog->SetClientExtent(0xfa, 200);
        message = Data().strings[0x7d6];
        dialog->SetMessage(message, 1, 0x32, true, true, true, true, 0, 0x15e);
        dialog->on_complete = OnNpcTradeDialogClosed;
        OpenModal(std::unique_ptr<ModalContent>(dialog));
        return 1;
    }
    if (GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc) == W8_NPC_BAND_FRIENDLY ||
        CurrentGame().npc_interaction.dialogue_npc->record->voice_script != 0) {
        if (item != 0) {
            const auto match = FindNpcScriptItemQuote(item->iItemNo);
            if (!match) {
                if (CurrentGame().npc_interaction.dialogue_npc->record->voice_script != 0) {
                    return 1;
                }
                if ((Data().items[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
                    CurrentGame().npc_dialogue.QueueQuote(0x11, false, false, false);
                    return 1;
                }
                if (AcceptNpcDialogueItem(CurrentGame().npc_interaction.dialogue_npc, item, 1) !=
                    0) {
                    CurrentGame().npc_dialogue.QueueQuote(0x10, false, false, false);
                    RemoveNpcScriptItem(MakeScriptContext(CurrentGame()), item, 0, -1);
                    return result;
                }
                CurrentGame().npc_dialogue.QueueQuote(0x11, false, false, false);
            } else {
                CurrentGame().npc_dialogue.QueueQuote(match->quote_index, false, false, false);
                result = false;
                if (match->consumes_item) {
                    RemoveNpcScriptItem(MakeScriptContext(CurrentGame()), item, 0, -1);
                    return result;
                }
                if (CurrentGame().npc_interaction.held_item_pending &&
                    CurrentGame().npc_interaction.pending_item.iItemNo == item->iItemNo) {
                    CurrentGame().npc_interaction.held_item_pending = false;
                    AddItemToParty(CurrentGame(),
                                   OwnedItem(CurrentGame().npc_interaction.pending_item), true,
                                   false);
                    (void)TakeInventoryItem(CurrentGame(), {W8ItemStorage::Hand});
                    return 0;
                }
            }
        }
    } else if (item != 0) {
        if ((Data().items[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0) {
            if (!WillNpcTradeForItem(CurrentGame().npc_interaction.dialogue_npc, item)) {
                CurrentGame().npc_dialogue.QueueQuote(7, false, false, false);
                return 1;
            }
            const auto match = FindNpcScriptItemQuote(item->iItemNo);
            if (!match) {
                ApplyNpcInteraction(CurrentGame().npc_interaction.dialogue_npc, 3,
                                    CurrentGame().npc_interaction.dialogue_speaker, item, 0);
                CurrentGame().npc_dialogue.QueueQuote(
                    GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc) ==
                            W8_NPC_BAND_FRIENDLY
                        ? 0x10
                        : 7,
                    false, false, false);
            } else {
                CurrentGame().npc_dialogue.QueueQuote(match->quote_index, false, false, false);
                result = false;
            }
            RemoveNpcScriptItem(MakeScriptContext(CurrentGame()), item, 0, -1);
            return result;
        }
        CurrentGame().npc_dialogue.QueueQuote(0x11, false, false, false);
        return 1;
    }
    return result;
}

/* Retail ICF shares this constant-return callback with the retained body at
   0x005b1740; this typed source callback has no separate address marker. */
static bool AcceptNpcDialogueItem(W8NpcState*, W8ItemInstance*, int)
{
    return true;
}

/* While NPC script deferral holds character events during an open dialogue,
   pump Escape and left-click so layout dismissals still run. */
static void SubmitNpcWhereIsQuery(void)
{
    CurrentGame().npc_interaction.where_is_query = true;
    CurrentGame().npc_interaction.ui->panels[6].dirty = true;
    HandleNpcDialogueInput();
    CurrentGame().npc_interaction.where_is_query = false;
}

static void SubmitNpcDialogueInput(void)
{
    CurrentGame().npc_interaction.where_is_query = false;
    CurrentGame().npc_interaction.ui->panels[6].dirty = true;
    HandleNpcDialogueInput();
}

void HandleNpcDialogueItemChoice(void)
{
    auto& state = CurrentGame().npc_interaction;
    if (!state.trade_selection) return;
    auto* item = ResolveTradeSelection(state.dialogue_npc, *state.trade_selection);
    if (state.trade_selection->source != W8TradeSource::Gold && !item) return;
    HandleNpcDialogueItem(item);
    if (CurrentGame().npc_interaction.reopen_topics &&
        GetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc) == W8_NPC_BAND_FRIENDLY) {
        CloseNpcDialogueOptionLayout();
        OpenNpcDialogueTranscriptLayout();
        HandleNpcDialogueDeparture(0);
    }
}

void RefreshNpcTradePartyGold(void)
{
    std::string text;

    text = fmt::format("{:d}g", CurrentGame().party.gold);
    CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_33].text.SetText(
        text, g_wiz_text_font_secondary);
}

void RefreshNpcTradePrice(void)
{
    std::string text;

    if (CurrentGame().npc_interaction.trade_item != 0) {
        text = fmt::format("{:d}", CurrentGame().npc_interaction.trade_quantity);
        CurrentGame().npc_interaction.ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
            text, g_wiz_text_font_secondary);
    }
}

void DrainNpcDialogueDeferralInput(void)
{
    SDL_Point mouse;
    InputEvent input;
    W8NpcDialogueLayout prior_layout;
    bool reopen_topics;

    if (!ShouldDeferCharacterEventForNpcScript(true) || !CurrentGame().activity.conversation) {
        return;
    }
    mouse = CursorPosition();
    UpdateUiPointer(mouse.x, mouse.y);
    while (auto next_input = PollInputEvent()) {
        input = *next_input;
        if (input.type == InputEventType::KeyDown) {
            if (input.key == SDLK_ESCAPE) {
                if (CurrentGame().npc_interaction.dialogue_hidden == 0) {
                    if (!IsNpcScriptSessionActive()) {
                        switch (CurrentGame().npc_interaction.dialogue_layout) {
                        case W8_DIALOGUE_LAYOUT_TOPIC_MENU:
                        case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
                            EndNpcDialogueSession(false);
                            break;
                        case W8_DIALOGUE_LAYOUT_SERVICES:
                            prior_layout = CurrentGame().npc_interaction.previous_dialogue_layout;
                            CloseNpcDialogueMode1Layout();
                            if (prior_layout == 2) {
                                ShowNpcDialogueTopicMenu();
                            } else if (prior_layout == 3) {
                                OpenNpcDialogueTranscriptLayout();
                            }
                            break;
                        case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
                            reopen_topics = CurrentGame().npc_interaction.reopen_topics;
                            CloseNpcDialogueOptionLayout();
                            if (reopen_topics) {
                                ShowNpcDialogueTopicMenu();
                            } else {
                                OpenNpcDialogueTranscriptLayout();
                            }
                            break;
                        case W8_DIALOGUE_LAYOUT_TRADE:
                            CloseNpcDialogueMode5Layout();
                            OpenNpcDialogueTranscriptLayout();
                            break;
                        default:
                            break;
                        }
                    } else {
                        TryFinishNpcVoicePlayback(CurrentGame().status, false);
                    }
                } else {
                    SetNpcDialogueHidden(0);
                }
            }
        } else if (input.type == InputEventType::LeftButtonDown) {
            TryFinishNpcVoicePlayback(CurrentGame().status, false);
        }
    }
}

/* Open the modal NPC sub-dialog for a script request: option list (0x05),
   price check (0x12/0x1e) or keyword entry (0x13). The text-input stack is
   suspended while the modal is up unless the dialogue stays live; for the
   price-check opcodes the base price in the request is discounted by the
   NPC's effect percentage and the party's best haggle skill. */
void OpenNpcDialog(W8NpcQuoteEntry* request, int aux_data)
{
    W8MonsterInfo* monster_info;

    if (CurrentGame().activity.conversation && !CurrentGame().npc_interaction.scripted_dialogue) {
        SetNpcDialoguePanelVisible(0);
    }
    std::optional<W8NpcPriceCheck> price_check;
    if (request->kind == W8_NPC_ENTRY_PRICE_CHECK ||
        request->kind == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK) {
        int price = request->operand0;
        monster_info = GetNpcMonsterInfo(CurrentGame().npc_interaction.dialogue_npc);
        if (monster_info != 0) {
            price -= static_cast<int>(monster_info->charm_strength * 0.01f * price);
        }
        price -= GetBestPartySkillLevel(W8_SKILL_COMMUNICATION, 0) * price / 500;
        if (price < 1) {
            price = 1;
        }
        if (price > 0x1e) {
            price = (price * 10 + 9) / 10;
        }
        price_check = W8NpcPriceCheck{CurrentGame().npc_interaction.dialogue_npc, aux_data, price,
                                      request->kind != W8_NPC_ENTRY_ALWAYS_PRICE_CHECK};
    }
    CurrentGame().npc_dialogue.ReplyDialogOpened(price_check);
    OpenModal(std::make_unique<W8NpcDialog>(request, aux_data, OnNpcDialogClosed));
    SetTargetCursor(W8_CURSOR_NONE);
}

void SetNpcQuoteBubbleVisible(bool visible, std::string_view text, W8NpcScriptQuote* quote,
                              int quote_id, unsigned int font_palette)
{
    SetNpcQuoteBubbleVisible(visible, text, quote, quote_id, font_palette, {}, -1);
}

void SetNpcQuoteBubbleVisible(bool visible, std::string_view text, W8NpcScriptQuote* quote,
                              int quote_id, unsigned int font_palette, W8QuoteNotice notice,
                              int npc_kind)
{
    if (visible == CurrentGame().npc_interaction.quote_visible) {
        return;
    }
    if (visible) {
        std::string normalized;
        std::string error_text;

        CancelMouselook();

        for (unsigned int index = 0; index < text.size(); ++index) {
            if (text[index] == '\n' && index > 0 && text[index - 1] != ' ') {
                normalized += ' ';
            }
            normalized += text[index];
        }
        auto& bubble = CurrentGame().npc_interaction.quote_bubble;
        bubble = QuoteBubble::layout(
            normalized, 280,
            font_palette == 0xffffffff ? std::nullopt : std::optional<std::uint8_t>(font_palette));
        if (!bubble) {
            if (CurrentGame().npc_interaction.dialogue_npc != 0) {
                error_text = fmt::format(
                    "Error creating box - most likely text too large: NPC {}, quote {:d}",
                    CurrentGame().npc_interaction.dialogue_npc->record->source_name, quote_id);
            } else {
                error_text = "Error creating box - most likely text too large";
            }
            bubble = QuoteBubble::layout(error_text, 300, std::nullopt);
            ShowNotice(W8_FONT_PALETTE_BEIGE, error_text, 0,
                       (MainGameHud()->text_box_right - MainGameHud()->text_box_left));
        }
        CurrentGame().npc_interaction.quote_width =
            static_cast<short>(bubble ? bubble->width() : 0);
        CurrentGame().npc_interaction.quote_height =
            static_cast<short>(bubble ? bubble->height() : 0);
        CurrentGame().npc_interaction.quote_x = 320 - CurrentGame().npc_interaction.quote_width / 2;
        CurrentGame().npc_interaction.quote_y =
            quote_id < 0 ? 350 - CurrentGame().npc_interaction.quote_height : 20;
        ShowNpcQuoteBubbleRegion(
            CurrentGame().npc_interaction.quote_x, CurrentGame().npc_interaction.quote_y,
            CurrentGame().npc_interaction.quote_x + CurrentGame().npc_interaction.quote_width,
            CurrentGame().npc_interaction.quote_y + CurrentGame().npc_interaction.quote_height);
        CurrentGame().npc_interaction.quote_visible = true;
        if (std::holds_alternative<std::monostate>(notice)) {
            CurrentGame().npc_interaction.pending_notice_lines.push_back({normalized, npc_kind});
        }
        bool level_up = std::holds_alternative<W8LevelUpNotice>(notice);
        CurrentGame().npc_interaction.quote_notice = notice;
        if (level_up) {
            wiz8::sound::play(
                reinterpret_cast<char*>(const_cast<char*>( // reinterpret-ok: SGP text ABI
                    "Data\\Sound\\Misc\\GainLevel.wav")));
        }
        return;
    }

    bool flush_notices =
        (quote_id == 0x12 || quote_id < 0) &&
        std::holds_alternative<std::monostate>(CurrentGame().npc_interaction.quote_notice);
    W8QuoteNotice dismissed = std::exchange(CurrentGame().npc_interaction.quote_notice, {});
    if (const auto* experience = std::get_if<W8ExperienceNoticePayload>(&dismissed)) {
        ShowNotice(W8_FONT_PALETTE_BEIGE,
                   fmt::sprintf(Data().strings[experience->alternate_message ? 0x231 : 0x232],
                                experience->amount),
                   0);
    } else if (const auto* skills = std::get_if<W8SkillNoticePayload>(&dismissed)) {
        PostSkillIncreaseNotices(CurrentGame(), skills);
    } else if (const auto* level_up = std::get_if<W8LevelUpNotice>(&dismissed)) {
        PostCharacterNotice(level_up->party_slot, Data().strings[0x773]);
    }
    if (quote != 0) {
        /* index is int, not unsigned int: entry_count is an unsigned short,
           which promotes to int here, and the retail tests the count signed
           (0x00576521 mov ax,word ptr [ecx+0x9]; 0x00576525 test eax,eax;
           jle 0x00576543). An unsigned index would make the comparison
           unsigned and lose that. */
        for (int index = 0; index < quote->entry_count; ++index) {
            if (quote->entries[index].kind == W8_NPC_ENTRY_KEYWORD_INPUT ||
                quote->entries[index].kind == W8_NPC_ENTRY_OPTIONS) {
                flush_notices = true;
            }
        }
    }
    if (flush_notices) {
        FlushPendingNoticeLines();
    }
    CurrentGame().npc_interaction.quote_visible = false;
    const bool had_bubble = CurrentGame().npc_interaction.quote_bubble.has_value();
    CurrentGame().npc_interaction.quote_bubble.reset();
    HideNpcQuoteBubbleRegion();
    if (CurrentScreenState().id == W8_SCREEN_MAIN_GAME) {
        if (had_bubble) {
            ClearSurfaceRect(
                CurrentGame().npc_interaction.quote_x, CurrentGame().npc_interaction.quote_y,
                CurrentGame().npc_interaction.quote_x + CurrentGame().npc_interaction.quote_width,
                CurrentGame().npc_interaction.quote_y + CurrentGame().npc_interaction.quote_height);
            InvalidateRegion(
                CurrentGame().npc_interaction.quote_x, CurrentGame().npc_interaction.quote_y,
                CurrentGame().npc_interaction.quote_x + CurrentGame().npc_interaction.quote_width,
                CurrentGame().npc_interaction.quote_y + CurrentGame().npc_interaction.quote_height);
        }
        if (auto* hud = MainGameHud()) {
            hud->repaint.layout = true;
            hud->repaint.portraits.set();
            hud->repaint.portrait_panel = true;
        }
    } else if (auto* camp = FindScreen<CampScreen>()) {
        camp->InvalidateAll();
    }
}

void DrawNpcQuoteBubble(void)
{
    IsModalOpen();
    if (CurrentGame().npc_interaction.quote_visible && CurrentGame().npc_interaction.quote_bubble) {
        CurrentGame().npc_interaction.quote_bubble->draw(GetFrameBufferSurface(),
                                                         CurrentGame().npc_interaction.quote_x,
                                                         CurrentGame().npc_interaction.quote_y);
    }
}

void FlushPendingNoticeLines(void)
{
    std::string npc_name;
    int index;

    for (index = 0;
         index < static_cast<int>(CurrentGame().npc_interaction.pending_notice_lines.size());
         ++index) {
        const auto* line = &CurrentGame().npc_interaction.pending_notice_lines[index];
        if (line->npc_kind != -1 &&
            line->npc_kind != CurrentGame().npc_interaction.last_notice_npc_kind) {
            W8NpcState* npc = GetNpcState(line->npc_kind);
            if (npc != 0) {
                npc_name = npc->record->source_name;
                ShowNotice(W8_FONT_PALETTE_GREEN, npc_name, 3);
                CurrentGame().npc_interaction.last_notice_npc_kind = line->npc_kind;
            }
        }
        ShowNotice(line->npc_kind == -1 ? 0xb : 0xf, line->text, 3,
                   (MainGameHud()->text_box_right - MainGameHud()->text_box_left));
    }
    CurrentGame().npc_interaction.pending_notice_lines.clear();
}

void LookAtDialogueNpc(void)
{
    W8MonsterInfo* info = GetNpcMonsterInfo(CurrentGame().npc_interaction.dialogue_npc);
    if (info != 0) {
        glm::vec3 position = info->p3D->nav.movement.position;
        position.y += info->p3D->nav.movement.height_offset;
        partyCamera().LookAt(&position, false);
    }
}

/* Show (0) or hide (nonzero) the NPC dialogue UI: on show the party portrait
   region sets lose input, the dialogue regions and text controls come up, and
   the transcript expands; on hide the party sets come back, the transcript
   collapses, its background is cleared and the rectangle invalidated. The new
   state lands in dialogue_hidden. */
static void SetNpcDialogueHidden(signed char value)
{
    int index;

    if (value != 0) {
        RefreshMainGameLayout();
        SetNpcPanelActive(2, false);
        CurrentGame().npc_interaction.ui->transcript->Collapse();
        SetNpcPanelActive(3, false);
        for (index = 0; index < 8; ++index) {
            if (CurrentGame().party.members[index].has_value()) {
                EnableMainGamePortraitSlot(index);
            }
        }
        SetNpcDialogueInputEnabled(0, false);
        CurrentGame().npc_interaction.ui->transcript->ClearBackground();
        EraseRegion(0x1dc, 0x11b, 0x269, 0x1c2);
        if (auto* hud = MainGameHud()) {
            hud->repaint.portraits.set(1);
            hud->repaint.portraits.set(3);
            hud->repaint.portraits.set(5);
            hud->repaint.portraits.set(7);
        }
        if (auto* fields = TextFieldGroup::Focused()) fields->SetBlocksMouse(0, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
        DisableNpcTradeActions();
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, false);
    } else {
        RefreshMainGameLayout();
        for (index = 0; index < 8; ++index) {
            if (CurrentGame().party.members[index].has_value()) {
                DisableMainGamePortraitSlot(index);
            }
        }
        SetNpcDialogueInputEnabled(0, true);
        SetNpcDialogueInputEnabled(2, true);
        EnableDialogueTextBoxRegions();
        SetTextBoxScrollInput(true);
        MainGameHud()->action_panel_visible = 1;
        SetNpcPanelActive(2, true);
        SetNpcPanelActive(3, true);
        CurrentGame().npc_interaction.ui->transcript->Expand();
        SyncNpcDialogueTranscriptScrollButtons();
        if (auto* fields = TextFieldGroup::Focused()) fields->SetBlocksMouse(0, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_5, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, true);
    }
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    CurrentGame().npc_interaction.dialogue_hidden = value;
}

/* Destroy callback OpenNpcDialog installs on the modal: pops or re-schemes the
   text-input level the dialog pushed, restores the quote bubble, and routes the
   choice back to the script - the picked option's label, the price-check
   yes/no string, or the typed keyword text. Inside a live dialogue the text is
   injected into input field 0 and processed as if typed; otherwise it is
   submitted to the script line queue directly. */
static void ResolveNpcPickpocket(int party_slot)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int gold;
    W8ItemInstance item;
    std::string text;
    int slot;

    switch (
        AttemptNpcPickpocket(character, CurrentGame().npc_interaction.dialogue_npc, &item, &gold)) {
    case W8_PICKPOCKET_ITEM_TAKEN:
        text =
            fmt::sprintf(Data().strings[0x74d], character->name, GetItemDisplayName(Data(), &item));
        for (slot = 0; slot < 8; ++slot) {
            if (character->backpack[slot].iItemNo == -1) {
                AddItemToCharacter(CurrentGame(), PartyMember(CurrentGame(), party_slot),
                                   OwnedItem(item), false, false, false);
                DisplayNpcQuote(text, true);
                return;
            }
        }
        AddItemToParty(CurrentGame(), OwnedItem(item), false, false);
        DisplayNpcQuote(text, true);
        return;
    case W8_PICKPOCKET_GOLD_TAKEN:
        text = fmt::sprintf(Data().strings[0x74e], character->name, gold);
        AddPartyGold(CurrentGame(), gold, false);
        DisplayNpcQuote(text, true);
        return;
    case W8_PICKPOCKET_FAILED:
        text = fmt::sprintf(Data().strings[0x74f], character->name);
        DisplayNpcQuote(text, false);
        return;
    case W8_PICKPOCKET_CAUGHT:
        CurrentGame().npc_dialogue.QueueQuote(0x17, false, false, false);
        SetNpcDispositionBand(CurrentGame().npc_interaction.dialogue_npc, W8_NPC_BAND_NEUTRAL);
        ApplyFactionChange(3, 1, CurrentGame().npc_interaction.dialogue_npc->record->faction, -5);
        CloseNpcDialogueTranscriptLayout();
        ShowNpcDialogueTopicMenu();
        return;
    case W8_PICKPOCKET_EMPTY:
        DisplayNpcQuote(Data().strings[0x750], false);
        return;
    default:
        return;
    }
}

void QueueDialogueNpcRefusal(void)
{
    unsigned int flags = CurrentGame().npc_interaction.dialogue_npc->refusal_flags;
    if ((flags & 1) == 0) {
        CurrentGame().npc_dialogue.QueueQuote(0x67, false, false, false);
        CurrentGame().npc_interaction.dialogue_npc->refusal_flags |= 1;
        return;
    }
    if ((flags & 2) == 0) {
        CurrentGame().npc_dialogue.QueueQuote(0x68, false, false, false);
        CurrentGame().npc_interaction.dialogue_npc->refusal_flags |= 2;
        return;
    }
    CurrentGame().npc_dialogue.QueueQuote(0x69, false, false, false);
}

void OnNpcDialogClosed(W8NpcDialog& dialog)
{
    W8NpcDialog* npc_dialog = &dialog;
    W8NpcQuoteEntry* request = npc_dialog->m_message;
    std::string field_text;
    std::string entry_text;
    int index;

    if (auto* fields = TextFieldGroup::Focused()) {
        fields->UseGameStyle();
    }
    RestoreCurrentNpcQuoteBubble();
    if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        CloseNpcDialogueOptionLayout();
        OpenNpcDialogueTranscriptLayout();
    }
    if (npc_dialog->m_cancelled) {
        CurrentGame().npc_dialogue.ReplyDialogClosed();
        return;
    }
    if (request->kind == W8_NPC_ENTRY_OPTIONS) {
        for (index = 0; index < request->sub_entry_count; ++index) {
            if (index == npc_dialog->m_selected_option) {
                entry_text = request->sub_entries[index].text.c_str();
                if (!CurrentGame().activity.conversation ||
                    CurrentGame().npc_interaction.scripted_dialogue) {
                    HandleNpcDialogueReply(entry_text, false);
                } else {
                    field_text = NpcFieldText();
                    StripNpcKeywordPunctuation(entry_text);
                    static_cast<void>(field_text.size());
                    SetNpcFieldText(entry_text);
                    HandleNpcDialogueInput();
                }
                break;
            }
        }
    } else if (request->kind == W8_NPC_ENTRY_PRICE_CHECK ||
               request->kind == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK) {
        if (npc_dialog->m_selected_option == 0) {
            entry_text = Data().strings[0x7df];
        } else {
            entry_text = Data().strings[0x7e0];
        }
        if (!CurrentGame().activity.conversation ||
            CurrentGame().npc_interaction.scripted_dialogue) {
            HandleNpcDialogueReply(entry_text, false);
        } else {
            field_text = NpcFieldText();
            StripNpcKeywordPunctuation(entry_text);
            static_cast<void>(field_text.size());
            SetNpcFieldText(entry_text);
            HandleNpcDialogueInput();
        }
    } else if (request->kind == W8_NPC_ENTRY_KEYWORD_INPUT) {
        if (!CurrentGame().activity.conversation ||
            CurrentGame().npc_interaction.scripted_dialogue) {
            HandleNpcDialogueReply(npc_dialog->m_input_text, false);
        } else {
            field_text = NpcFieldText();
            StripNpcKeywordPunctuation(npc_dialog->m_input_text);
            static_cast<void>(field_text.size());
            SetNpcFieldText(npc_dialog->m_input_text);
            HandleNpcDialogueInput();
        }
    }
    CurrentGame().npc_dialogue.ReplyDialogClosed();
}

/* The camp-side mirror of SwitchNpcDialogueLayout: the conversation is set aside, the current
   dialogue layout is retired, and a still-pending item goes back onto the
   item cursor. */
void CloseNpcDialogueForCamp(void)
{
    CurrentGame().activity.conversation_aside = true;
    SwitchNpcDialogueLayout(W8_DIALOGUE_LAYOUT_NONE);
    EndNpcDialogueSession(false);
    if (CurrentGame().npc_interaction.held_item_pending) {
        CurrentGame().party.held.item = CurrentGame().npc_interaction.pending_item;
        return;
    }
    SetTargetCursor(W8_CURSOR_NONE);
}

void SyncDialogueNpcStateAndMarkPending(void)
{
    SyncDialogueNpcState();
    CurrentGame().npc_interaction.pending_trade_toggle = true;
}

void SyncDialogueNpcState(void)
{
    BeginNpcDialogueInternal(CurrentGame().npc_interaction.dialogue_npc, 0, -1, 0, 1);
    CurrentGame().npc_interaction.pending_layout =
        CurrentGame().npc_interaction.previous_dialogue_layout;
}

/* The dialogue NPC takes the guard script when it is '*' styled and the party
   walks away; otherwise its record flags drive either a combat notice or the
   queued scripted action named by the record. While the dialogue is still up
   the named-action queue hands the speaker's name to 0x00571660 instead.
   Every occupied living character without a maxed condition practices
   communication (skill 0x16). */
static void HandleNpcDialogueDeparture(unsigned char value)
{
    W8MonsterInfo* info;
    W8Character* character;
    int index;

    if (CurrentGame().npc_interaction.dialogue_npc->name_style == 0x2a &&
        (info = GetNpcMonsterInfo(CurrentGame().npc_interaction.dialogue_npc)) != 0) {
        info->p3D->SetScript("Guard.msf", true);
    }
    if ((value == 0 || !CurrentGame().npc_interaction.dialogue_npc->dismissed_flag ||
         CurrentGame().npc_interaction.dialogue_npc->record->allow_dismissed_departure_dialogue !=
             0) &&
        CurrentGame().npc_interaction.transcript_open_count < 1) {
        if (!CurrentGame().npc_interaction.dialogue_npc->greeting_pending) {
            CurrentGame().npc_dialogue.QueueQuote(1, false, false, false);
        } else {
            CurrentGame().npc_dialogue.QueueQuote(0, false, false, false);
            CurrentGame().npc_interaction.dialogue_npc->greeting_pending = false;
            if (CurrentGame().npc_interaction.dialogue_npc->record->monster_bound == 0 &&
                CurrentGame().npc_interaction.dialogue_npc->record->voice_script == 0 &&
                CurrentGame().npc_interaction.dialogue_npc->record->merchant == 0) {
                for (index = 0; index < 8; ++index) {
                    character = &CurrentGame().party.characters[index];
                    if (CurrentGame().party.members[index].has_value() &&
                        character->hp_current != 0 &&
                        character->highest_condition < W8_CONDITION_ASLEEP) {
                        PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_COMMUNICATION,
                                               0xf, false);
                    }
                }
            }
            if (CurrentGame().activity.conversation &&
                !CurrentGame().npc_interaction.scripted_dialogue) {
                LearnDialogueKeyword(
                    CurrentGame().npc_interaction.dialogue_npc->record->source_name,
                    W8_DIALOGUE_CATEGORY_ALL, true);
                return;
            }
            if (CurrentGame().npc_interaction.dialogue_npc->record->monster_bound == 0 &&
                (CurrentGame().npc_interaction.dialogue_npc->record->voice_script == 0 ||
                 CurrentGame().npc_interaction.dialogue_npc->is_present)) {
                LearnDialogueKeyword(
                    CurrentGame().npc_interaction.dialogue_npc->record->source_name,
                    W8_DIALOGUE_CATEGORY_ALL, false);
            }
        }
    }
}

/* Shared by the main-game screen and dialog text entries; it lives with the
   main-game text helpers, not with UtilityFunctions.cpp. */
std::string ShortenTextToWidth(std::string_view text, unsigned int width, Font& font)
{
    std::string shortened(text);
    if (static_cast<unsigned int>(font.textWidth(shortened)) <= width) return shortened;
    while (!shortened.empty()) {
        shortened.resize(wiz8::text::previous_grapheme(shortened, shortened.size()));
        const auto candidate = shortened + "...";
        if (static_cast<unsigned int>(font.textWidth(candidate)) <= width) return candidate;
    }
    return {};
}

void BeginScriptedWorldAction(void)
{
    CurrentGame().status.world_cursor_gate = 1;
    ResetLevelDataVectors();
    SetTargetCursor(W8_CURSOR_MAP_LOAD);
}

void ClearMainGameTargetState(void)
{
    CurrentGame().status.world_cursor_gate = 0;
    ClearLevelMovementStopped();
    SetTargetCursor(W8_CURSOR_NONE);
}

/* When the world-cursor gate (world_cursor_gate) is raised, discard queued input
   after refreshing the region position so stale events do not fire. */
void FlushInputWhileWorldCursorGate(void)
{
    SDL_Point mouse;
    InputEvent input;

    if (CurrentGame().status.world_cursor_gate == 0) {
        return;
    }
    mouse = CursorPosition();
    UpdateUiPointer(mouse.x, mouse.y);
    while (auto next_input = PollInputEvent()) {
        input = *next_input;
    }
}

/* The notebook category a keyword belongs to: an item, a person (an NPC,
   a named monster or a listed person), a place, or anything else. */
static W8DialogueCategory ClassifyDialogueKeyword(std::string_view keyword)
{
    const auto matches = [&](std::string_view name) {
        return CompareTextIgnoreAsciiCase(keyword, name) == 0;
    };
    if (std::ranges::any_of(Data().items,
                            [&](const auto& item) { return matches(item.display_name); })) {
        return W8_DIALOGUE_CATEGORY_ITEMS;
    }
    if (std::ranges::any_of(Data().npcs,
                            [&](const auto& npc) { return matches(npc.source_name); })) {
        return W8_DIALOGUE_CATEGORY_PEOPLE;
    }
    for (int index = 0; g_dialogue_person_keywords[index][0] != 0; ++index) {
        if (matches(g_dialogue_person_keywords[index])) return W8_DIALOGUE_CATEGORY_PEOPLE;
    }
    return IsDialoguePlaceKeyword(keyword) ? W8_DIALOGUE_CATEGORY_PLACES
                                           : W8_DIALOGUE_CATEGORY_MISC;
}

void LearnDialogueKeyword(std::string_view text, W8DialogueCategory category, bool chime)
{
    std::string keyword{text};
    StripNpcKeywordPunctuation(keyword);
    if (keyword.empty()) {
        return;
    }
    if (category == W8_DIALOGUE_CATEGORY_ALL) {
        category = ClassifyDialogueKeyword(keyword);
    }
    auto& state = CurrentGame().npc_interaction;
    auto& notebook = CurrentGame().story.keywords;
    if (std::ranges::any_of(notebook, [&](const W8DialogueTranscriptRecord& record) {
            return CompareTextIgnoreAsciiCase(record.text, keyword) == 0;
        })) {
        return;
    }
    std::erase_if(notebook, [](const W8DialogueTranscriptRecord& record) {
        return record.text == W8_NO_KEYWORDS_PLACEHOLDER;
    });
    notebook.push_back({keyword, category});

    auto* controller = state.ui ? state.ui->transcript.get() : nullptr;
    if (controller == nullptr || !controller->showing_notebook) {
        return;
    }
    controller->AddTranscriptEntry(keyword, category, chime);
    if (state.dialogue_category_filter != W8_DIALOGUE_CATEGORY_ALL &&
        state.dialogue_category_filter != category) {
        state.dialogue_category_filter = category;
        controller->SetTranscriptCategoryFilter(state.dialogue_category_filter);
        SyncDialogueCategoryButtons();
    }
    if (chime) {
        wiz8::sound::play(reinterpret_cast<char*>(const_cast<char*>( // reinterpret-ok: SGP text ABI
            "Data\\Sound\\Misc\\Keyword Chime.wav")));
    }
    controller->Collapse();
    controller->Expand();
    controller->InvalidateAll();
    SyncNpcDialogueTranscriptScrollButtons();
}

void RecordLevelEntryDialogueState(void)
{
    std::string region_name;
    int region = GetLevelBand(GetLoadedLevelID());
    region_name = GetNpcScriptRegionName(region);
    if (!region_name.empty()) {
        LearnDialogueKeyword(region_name, W8_DIALOGUE_CATEGORY_PLACES, false);
    }
    if (region == 14) {
        SetFact(W8_FACT_QUEST_ASCEND_TO_CIRCLE, 0, false);
        if (!NpcLeadHasNameStyle(W8_NPC_VI_DOMINA)) {
            SetFact(W8_FACT_VI_IS_DEAD, 1, false);
        }
    }
}

/* Same NPC-dialogue text-box-layout predicate as IsNpcDialogueTextBoxActive,
   emitted as a second copy for the dialogue text input callers. */
bool IsNpcDialogueTextInputActive(void)
{
    return CurrentGame().activity.conversation &&
           CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX;
}
bool SetNpcDialoguePanelVisible(unsigned char value)
{
    W8NpcDialogueTextController* controller;

    if (CurrentGame().activity.conversation && !CurrentGame().npc_interaction.scripted_dialogue &&
        CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
        if (value == 0) {
            controller = CurrentGame().npc_interaction.ui->transcript.get();
            controller->Collapse();
            controller = CurrentGame().npc_interaction.ui->transcript.get();
            controller->m_active = false;
            controller->input.setEnabled(false);
            SetNpcPanelActive(3, false);
            controller = CurrentGame().npc_interaction.ui->transcript.get();
            controller->ClearBackground();
            EraseRegion(0x1dc, 0x11b, 0x269, 0x1c2);
            if (auto* hud = MainGameHud()) {
                hud->repaint.portraits.set(1);
                hud->repaint.portraits.set(3);
                hud->repaint.portraits.set(5);
                hud->repaint.portraits.set(7);
                hud->repaint.layout = true;
            }
            CurrentGame().npc_interaction.dialogue_panel_hidden = true;
            return true;
        }

        controller = CurrentGame().npc_interaction.ui->transcript.get();
        controller->m_active = true;
        controller->input.setEnabled(true);
        SetNpcPanelActive(3, true);
        controller = CurrentGame().npc_interaction.ui->transcript.get();
        controller->Expand();
        SyncNpcDialogueTranscriptScrollButtons();
        if (auto* hud = MainGameHud()) hud->repaint.layout = true;
        CurrentGame().npc_interaction.dialogue_panel_hidden = false;
        return true;
    }
    return false;
}
bool FinishNpcVoiceIfSessionActive(void)
{
    if (!IsNpcScriptSessionActive()) {
        return false;
    }
    TryFinishNpcVoicePlayback(CurrentGame().status, false);
    return true;
}

bool ProcessPendingEvent(void)
{
    if (IsNpcScriptSessionActive()) {
        TryFinishNpcVoicePlayback(CurrentGame().status, true);
        return true;
    }
    if (CurrentGame().speech.HasActiveEvents()) {
        CurrentGame().speech.CompleteFirstActiveEvent();
        return true;
    }
    return false;
}

/* The keyword-list instantiations' char element type canonicalizes to
   unsigned short under the VC6 ABI spelling used by the PDB names. */
