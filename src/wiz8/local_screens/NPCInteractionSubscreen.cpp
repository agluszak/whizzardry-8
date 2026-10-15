#include "wiz8/local_screens/NPCInteractionSubscreen.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <wiz8/filesystem.h>

#include "Font.h"
#include "compat/native.h"
#include "dialog_code/DialogBase.h"
#include "image.h"
#include "input.h"
#include "layouts/main_game_screen.h"
#include "layouts/npc_dialogue.h"
#include "local_code/ControlsRect.h"
#include "local_code/Factions.h"
#include "local_code/TextBuffer.h"
#include "message_box.h"
#include "mgs_commands.h"
#include "npc_trade_price.h"
#include "random.h"
#include "soundman.h"
#include "surrender/srMath.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/conditions.h"
#include "wiz8/containers.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/NpcDialog.h"
#include "wiz8/dialog_code/PortraitQuote.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/integer_constants.h"
#include "wiz8/item_video_object_vector.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/modal_content.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_items.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/spell_ids.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

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
    npc_quote_input = std::make_unique<UiLayer>(0x25);
    npc_quote_input->add(
        SDL_Rect{x1, y1, x2 - x1 + 1, y2 - y1 + 1}, [](const UiPointerEvent& event) {
            if (event.action == UiPointerAction::Down && event.button == UiPointerButton::Left)
                AfterUiDispatch([] { TryFinishNpcVoicePlayback(true); });
            return UiResult::Handled;
        });
}

void HideNpcQuoteBubbleRegion()
{
    npc_quote_input.reset();
}

static void ToggleNpcTradeCategory(int button, W8NpcTradeFilter filter);
static unsigned char OpenNpcDialoguePanel(W8NpcState* npc, W8ItemInstance* item, bool force);
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
static void SetNpcDialogueHidden(char value);
static void CloseNpcDialogueMode1Layout(void);
static void CloseNpcDialogueTranscriptLayout(void);
static void CloseNpcDialogueMode5Layout(void);
static void SetNpcDialogueLayoutMode(W8NpcDialogueLayout value);
static void SelectNpcPartyItems(void);
static void ResolveNpcPickpocket(int party_slot);
static void NpcDialogueTextBoxDoubleClick(int x, int y);
static void NpcDialogueTextBoxLeftUp(int x, int y);
static void NpcDialogueTextBoxRightUp(int x, int y);
static W8ItemInstance* ResolveNpcTradeRow(int index, bool, char, char);
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
static unsigned char AcceptNpcDialogueItem(W8NpcState* npc, W8ItemInstance* item, int mode);
void OnNpcDialogClosed(W8NpcDialog& dialog);

// GLOBAL: WIZ8 0x0068ee90
static W8NpcInteractionState g_npc_interaction_storage;
/* The global object's implicit constructor and destructor; 0x0056B930 is the
   retail static initializer that runs the former and registers the latter. */

// GLOBAL: WIZ8 0x00649f1c
W8NpcInteractionState* g_npc_interaction_state = &g_npc_interaction_storage;
/* The dialogue keyword tables. Element zero is the English file
   list and element one the translated one; each file list holds one line list
   per line and each line list one word per field. */
// GLOBAL: WIZ8 0x0068EE80
std::vector<KeywordFile> g_keyword_lists;
/* Both keyword files are loaded and the tables are usable. */
// GLOBAL: WIZ8 0x0068F0F8
bool g_keyword_lists_loaded;
/* The keyword subsystem's active flag, written absolutely by the
   screen reset and by the keyword panel helpers. */
// GLOBAL: WIZ8 0x0068F0F9
bool g_pending_notice_queued;
/* Empty wide string used to clear dialogue editor text. */
/* The queued NPC script notice; see the type comment in the
   header. */
// GLOBAL: WIZ8 0x0068EE60
W8PendingNotice g_pending_notice;
/* w8_get_ticks sample for the trade-item highlight timeout. */
// GLOBAL: WIZ8 0x0068EE78
static uint64_t g_trade_highlight_tick;

// GLOBAL: WIZ8 0x00649f20
static int g_dialogue_place_keyword_count = 15;
// GLOBAL: WIZ8 0x00649f24
static int g_dialogue_place_keyword_ids[15] = {0x751, 0x752, 0x753, 0x754, 0x755,
                                               0x756, 0x757, 0x758, 0x759, 0x75a,
                                               0x75b, 0x75c, 0x75d, 0x75e, 0x75f};
// GLOBAL: WIZ8 0x00649F64
static int g_dialogue_fallback_ids0[5] = {0x760, 0x761, 0x762, 0x763, 0x764};
// GLOBAL: WIZ8 0x00649F78
static int g_dialogue_fallback_ids1[5] = {0x765, 0x766, 0x767, 0x768, 0x769};
// GLOBAL: WIZ8 0x00649f8c
const char* g_dialogue_person_keywords[] = {"BALBRAK", "BILDUBLU", "EWAXX",  "KUNAR",
                                            "PANRACK", "RODAN",    "RUBBLE", "SAXX",
                                            "SPARKLE", "YAMIR",    ""};

NpcDialogueUi::~NpcDialogueUi() = default;

void SetNpcDialogueInputEnabled(int group, bool enabled)
{
    if (!g_npc_interaction_state->ui) return;
    auto& ui = *g_npc_interaction_state->ui;
    (group == 0   ? ui.transcript_input
     : group == 1 ? ui.trade_input
                  : ui.options_input)
        .setEnabled(enabled);
}

static void SetNpcActionVisible(int index, bool visible)
{
    auto& ui = *g_npc_interaction_state->ui;
    auto& action = ui.actions[index];
    action.visible = visible;
    action.dirty = true;
    if (action.target) action.target->setEnabled(visible && ui.panels[action.panel].active);
}

static void SetNpcActionEnabled(int index, bool enabled)
{
    auto& action = g_npc_interaction_state->ui->actions[index];
    if (action.enabled == enabled) return;
    action.enabled = enabled;
    action.dirty = true;
    if (action.frames[4] != -1) action.text.SetRenderMode(enabled ? 4 : 7);
}

static void SetNpcActionPressed(int index, bool selected)
{
    auto& action = g_npc_interaction_state->ui->actions[index];
    if (action.policy.primary_toggle == ButtonToggle::None) return;
    action.interaction.pressed = action.interaction.selected = selected;
    action.dirty = true;
}

static void SetNpcActionHelp(int index, int message)
{
    auto* target = g_npc_interaction_state->ui->actions[index].target;
    if (!target) return;
    if (message < 0 || !g_settings.tooltips_enabled)
        target->setTooltip({});
    else
        target->setTooltip([message] { return g_localized_strings[message]; });
}

static void SetNpcPanelActive(int index, bool active)
{
    auto& ui = *g_npc_interaction_state->ui;
    auto& panel = ui.panels[index];
    const bool was_active = panel.active;
    panel.active = active;
    panel.dirty = true;
    for (int i = 0; i < 39; ++i) {
        if (ui.actions[i].panel != index) continue;
        SetNpcActionVisible(i, active && !(i >= 25 && i <= 30 &&
                                           g_npc_interaction_state->dialogue_layout !=
                                               W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX));
    }
    if (index == 2) {
        ui.transcript->m_active = active;
        ui.transcript->input.setEnabled(active);
    }
    if (index == 6 && (!active || !was_active)) {
        if (active) {
            g_npc_text_fields.emplace(1);
            g_npc_text_fields->Add(0x1e5, 0x170, 0x7a, 0x12, 0x7f, "", 0xbe, 0xf, true);
        } else if (g_npc_text_fields)
            g_npc_text_fields->Remove(0);
    }
}

static void AddNpcAction(int index, int panel, SDL_Rect bounds, int catalog,
                         std::array<int, 5> frames)
{
    auto& ui = *g_npc_interaction_state->ui;
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
            auto& ui = *g_npc_interaction_state->ui;
            auto& action = ui.actions[index];
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
                PushButtonSoundScheme(0, true);
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
    const auto& ui = *g_npc_interaction_state->ui;
    if (ui.panels[index].dirty || (index == 2 && ui.transcript->m_dirty)) return true;
    for (const auto& action : ui.actions)
        if (action.panel == index && action.visible &&
            (action.dirty || action.text.m_geometryDirty))
            return true;
    return false;
}

static void PaintNpcPanel(int index, SDL_Surface& target, bool force)
{
    auto& ui = *g_npc_interaction_state->ui;
    auto& panel = ui.panels[index];
    if (!panel.active) return;
    force |= NpcPanelDirty(index);
    if (index == 2)
        ui.transcript->Redraw(target, force);
    else if (force) {
        const int frame = index == 0 && g_npc_interaction_state->dialogue_layout ==
                                            W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX
                              ? 17
                              : panel.background;
        CatalogArtwork::Load(0x1a9, 0).drawAndInvalidate(target, frame, panel.origin.x,
                                                         panel.origin.y);
        if (index == 6)
            CatalogArtwork::Load(0x1a9, 0).draw(
                target, 16, 0x1df, g_npc_interaction_state->where_is_query ? 0x19b : 0x18b);
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

static std::optional<std::string> ParseKeywordToken(std::string_view& line)
{
    const auto start = line.find_first_not_of(' ');
    if (start == std::string_view::npos) return {};
    line.remove_prefix(start);
    const auto end = line.find_first_of("/\r\n");
    auto word = line.substr(0, end);
    const auto last = word.find_last_not_of(' ');
    if (last == std::string_view::npos) return {};
    word = word.substr(0, last + 1);
    line = end == std::string_view::npos ? std::string_view{} : line.substr(end + 1);
    return std::string(word);
}

/* Load one keyword file into a file list. */
static bool LoadKeywordFile(std::string_view path, KeywordFile& file)
try {
    auto input = wiz8::open_file(path);
    if (input->size() < 0 || input->size() > 16 * 1024 * 1024) return false;
    const auto bytes = input->read_text();
    if (bytes.find('\0') != std::string::npos) return false;
    std::istringstream stream(bytes);
    std::string line;
    std::getline(stream, line);
    KeywordFile pending;
    while (std::getline(stream, line)) {
        // Retail's line limit and eleven-character metadata prefix are format constraints.
        if (wiz8::text::to_utf16(line).size() >= 1000) return false;
        std::string_view remaining(line);
        remaining.remove_prefix(wiz8::text::codepoint_prefix(remaining, 11));
        std::vector<std::string> words;
        while (auto word = ParseKeywordToken(remaining)) words.push_back(std::move(*word));
        pending.push_back(std::move(words));
    }
    file = std::move(pending);
    return true;
} catch (const std::exception&) {
    return false;
}

void ClearKeywordLists(void)
{
    g_keyword_lists.clear();
    g_keyword_lists_loaded = false;
}

void ReloadKeywordLists(void)
{
    ClearKeywordLists();
    KeywordFile english, translated;
    if (!LoadKeywordFile("Data\\Strings\\English_Keywords.txt", english) ||
        !LoadKeywordFile("Data\\Strings\\translated_Keywords.txt", translated))
        return;
    g_keyword_lists.push_back(std::move(english));
    g_keyword_lists.push_back(std::move(translated));
    g_keyword_lists_loaded = true;
}

std::string TranslateDialogueKeyword(std::string_view source)
{
    if (g_keyword_lists_loaded) {
        const auto& translated = g_keyword_lists[1];
        const auto& english = g_keyword_lists[0];
        for (std::size_t row = 0; row < translated.size(); ++row)
            for (std::size_t word = 0; word < translated[row].size(); ++word)
                if (CompareTextIgnoreAsciiCase(source, translated[row][word]) == 0 &&
                    row < english.size() && word < english[row].size())
                    return english[row][word];
    }
    return std::string(source);
}

/* Reset the screen state block: zero its 0x268 bytes, write its reset values,
   clear the keyword status byte, and reload the keyword lists. */
void ResetMainScreenStateBlock(void)
{
    int unset = -1;

    ClearNpcDialogueTranscript();
    *g_npc_interaction_state = {};
    g_npc_interaction_state->dialogue_category_filter = unset;
    g_npc_interaction_state->transcript_sorted = false;
    g_npc_interaction_state->pending_trade_toggle = false;
    g_npc_interaction_state->selected_trade_row = unset;
    g_npc_interaction_state->trade_pc_items = true;
    g_status.selected_party_member = 0xff;
    g_pending_notice_queued = false;
    ReloadKeywordLists();
}

/* Forward a monster-script notice to the targeting layer unless the screen is
   busy or this NPC kind suppresses it. */
void ForwardNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, int line, bool suppress)
{
    if (!gXStatus.fNpcDialogueMode && !gXStatus.fCombatMode &&
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
    if (g_pending_notice_queued) {
        return;
    }
    g_pending_notice.flag = flag;
    g_pending_notice.npc = npc;
    g_pending_notice.line = line;
    g_pending_notice.force = arg;
    if (item != 0) {
        g_pending_notice.item = *item;
    } else {
        EmptyItemRecord(&g_pending_notice.item, 0, true);
    }
    g_npc_dialogue.Queue(W8NpcCommand::dispatch_pending_notice);
    g_pending_notice_queued = true;
}

/* The NPC notice and dialogue dispatcher. Talking to a healer NPC (name
   styles 0x0f and 0x12) first lifts every active condition off the two party
   rows bound to RPC NPCs (name styles 0x10 and 0x11) whose condition set has
   reached 0x0f; clearing condition 0x12 - death - leaves them on ten hit
   points. A completed character event is finished off, the dialogue NPC is
   staged outside camp mode, and then the record's 0x054/0x2ea flags choose
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
    srVector3T<float> position;

    g_pending_notice_queued = false;
    g_npc_interaction_state->transcript_open_count = 0;
    if (npc->name_style == 0xf || npc->name_style == 0x12) {
        characters = g_status.buffers.Char;
        for (slot = 0; slot < 2; ++slot) {
            if (g_status.buffers.XChar[slot].fOccupied) {
                bound = GetNpcState(g_status.buffers.XChar[slot].npc_index);
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
    if (gXStatus.character_event_queue->HasActiveEvents()) {
        gXStatus.character_event_queue->CompleteFirstActiveEvent();
    }
    if (!gXStatus.fCampMode) {
        SelectNpcDialogueSpeaker(npc, flags);
    }
    if (force != 0) {
        OpenNpcDialoguePanel(npc, item, true);
        return;
    }
    if (npc->record->monster_bound == 0 && npc->record->voice_script == 0) {
        if (GetNpcDispositionBand(npc) == W8_NPC_BAND_HOSTILE && npc->record->merchant == 0) {
            g_npc_dialogue.QueueQuote(0x18, false, false, false);
            return;
        }
        if (flags == 0) {
            OpenNpcDialoguePanel(npc, item, false);
            return;
        }
    }
    if (npc->record->voice_script != 0) {
        if (item != 0) {
            state = g_npc_interaction_state;
            state->pending_item = *item;
            if (g_status.item_in_cursor) {
                state->held_item_pending = true;
            }
            HandleNpcDialogueItem(&state->pending_item);
        } else if (quote == -1) {
            HandleNpcDialogueDeparture(1);
        } else {
            g_npc_dialogue.QueueQuote(quote, false, false, false);
        }
    } else {
        if (quote == -1) {
            quote = 0;
        }
        g_npc_dialogue.QueueQuote(quote, false, false, false);
    }
    selected = g_npc_interaction_state->dialogue_npc;
    if (selected->name_style != 0x84 && selected->name_style != 0x85) {
        info = GetNpcMonsterInfo(selected);
        if (info != 0) {
            MonsterForwardReferencePosition(info->p3D, 0);
        }
    }
    PauseMainGameWorld();
    state = g_npc_interaction_state;
    state->scripted_dialogue = true;
    gXStatus.fNpcDialogueMode = true;
    state->transcript_open_count = 0;
    info = GetNpcMonsterInfo(state->dialogue_npc);
    if (info == 0) {
        return;
    }
    position = info->p3D->movement.position;
    position.y += info->p3D->movement.height_offset;
    g_gd_camera->LookAt(&position, false);
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
    if (g_pending_notice.item.iItemNo != -1) {
        item = &g_pending_notice.item;
    }
    BeginNpcDialogue(g_pending_notice.npc, item, g_pending_notice.line, g_pending_notice.flag,
                     g_pending_notice.force);
}

/* Open the NPC dialogue panel. After the shared screen reset and the
   dialogue-UI build, a carried item goes through the pending-item path -
   inspecting it decides between the trade switch and a disposition check -
   while everything else falls to the force gate and then the
   dispatch: a record-0x056 NPC takes the plain quote, otherwise the
   disposition band picks the hostile or friendly entry. */
static void CloseActiveNpcDialogueLayout();

static unsigned char OpenNpcDialoguePanel(W8NpcState* npc, W8ItemInstance* item, bool force)
{
    W8NpcInteractionState* state;
    W8MonsterInfo* info;
    W8MonsterInfo* dialogue_info;
    unsigned char band;
    bool greet;
    std::string space;
    srVector3T<float> position;

    UpdateScreenOverlays(0);
    gXStatus.fNpcDialogueMode = true;
    CloseMainGameOverlays();
    if (npc->record->owns_stock != 0) {
        RestockNpcInventory(npc);
    }
    state = g_npc_interaction_state;
    state->dialogue_layout = W8_DIALOGUE_LAYOUT_NONE;
    if (!gXStatus.fCampMode) {
        state->previous_dialogue_layout = W8_DIALOGUE_LAYOUT_NONE;
        state->trade_mode = W8_NPC_TRADE_NONE;
    }
    state->dialogue_panel_hidden = false;
    state->scripted_dialogue = false;
    state->trade_item = 0;
    state->last_mouse_x = -1;
    state->last_mouse_y = -1;
    state->hovered_text_line = -1;
    state->trade_filter = 0;
    state->value_000 = 0;
    state->where_is_query = false;
    state->held_item_pending = false;
    g_npc_dialogue.AbandonReply();
    state->dialogue_hidden = 0;
    state->modal_dialog_open = false;
    state->farewell_queued = false;
    state->reopen_topics = false;
    state->trade_gold = 0;
    state->pending_layout = W8_DIALOGUE_LAYOUT_NONE;
    state->suppress_parting_reaction = true;
    state->selected_trade_row = -1;
    state->transcript_open_count = 0;
    if (g_settings.main_ui_mode != W8_MAIN_UI_MODE_PORTRAITS) {
        ApplyMainGameModeFlag(W8_MAIN_UI_MODE_PORTRAITS, false);
    } else {
        SetViewportMode(GetMainGameViewportMode());
    }
    if (!gXStatus.fCampMode) {
        g_npc_interaction_state->saved_main_ui_mode = g_settings.main_ui_mode;
    }
    CreateNpcDialogueControls();
    g_level_block->text_box_visible = false;
    EnableDialogueTextBoxRegions();
    SetTextBoxScrollInput(true);
    g_level_block->action_panel_visible = 1;
    gXStatus.fCampMode = false;
    greet = !force;
    if (item != 0) {
        state = g_npc_interaction_state;
        state->pending_item = *item;
        if (g_status.item_in_cursor) {
            state->held_item_pending = true;
            ClearHeldItemDisplay();
        }
        if (npc->record->merchant != 0) {
            if (HandleNpcDialogueItem(&state->pending_item) == 0) {
                OpenNpcDialogueTranscriptLayout();
                greet = true;
            }
        } else if (HandleNpcDialogueItem(&state->pending_item) == 0) {
            CloseActiveNpcDialogueLayout();
            if (GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc) ==
                W8_NPC_BAND_FRIENDLY) {
                OpenNpcDialogueTranscriptLayout();
            } else {
                ShowNpcDialogueTopicMenu();
            }
            greet = true;
        }
    }
    if (greet) {
        if (g_npc_interaction_state->dialogue_npc->record->merchant != 0) {
            g_npc_dialogue.QueueQuote(0, false, false, false);
            OpenNpcDialogueTranscriptLayout();
        } else {
            band = GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc);
            if (g_npc_interaction_state->dialogue_npc->name_style == W8_NPC_ZANT &&
                GetFact(W8_FACT_TRANG_YOU_ARE_BUSTED) != 0 &&
                GetFact(W8_FACT_ALIGNMENT_UMPANI) == 0) {
                SetFact(W8_FACT_TRANG_YOU_ARE_BUSTED, 0, false);
            }
            if (band == W8_NPC_BAND_FRIENDLY) {
                HandleNpcDialogueDeparture(1);
                OpenNpcDialogueTranscriptLayout();
            } else if (band == W8_NPC_BAND_NEUTRAL) {
                g_npc_dialogue.QueueQuote(2, false, false, false);
                ShowNpcDialogueTopicMenu();
            }
        }
    }
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    CancelMouselook();
    info = GetNpcMonsterInfo(npc);
    if (info != 0) {
        g_npc_interaction_state->camera_redirected = true;
        g_npc_interaction_state->saved_camera_pitch = g_gd_camera->m_pitch;
        g_npc_interaction_state->saved_camera_yaw = g_gd_camera->m_yaw;
        dialogue_info = GetNpcMonsterInfo(g_npc_interaction_state->dialogue_npc);
        if (dialogue_info != 0) {
            position = dialogue_info->p3D->movement.position;
            position.y += dialogue_info->p3D->movement.height_offset;
            g_gd_camera->LookAt(&position, false);
        }
        MonsterForwardReferencePosition(info->p3D, 0);
    }
    PauseMainGameWorld();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    g_npc_interaction_state->text_box_collapsed = true;
    space = " ";
    ShowNotice(W8_FONT_PALETTE_YELLOW, space, 3);
    return 1;
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

    state = g_npc_interaction_state;
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
    g_npc_dialogue.Begin(state->dialogue_npc, false);
    if ((state->dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT ||
         state->dialogue_layout == W8_DIALOGUE_LAYOUT_TOPIC_MENU) &&
        gXStatus.fNpcDialogueMode) {
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
            state->dialogue_npc->record->source_name, g_wiz_text_bold_font);
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].dirty = true;
    }
    state->camera_redirected = false;
    for (slot = 0; slot < 8; ++slot) {
        if (g_status.buffers.XChar[slot].fOccupied) {
            if (speaker == -1) {
                speaker = slot;
            }
            if (g_status.buffers.Char[slot].skills[W8_SKILL_COMMUNICATION].level > best) {
                best = g_status.buffers.Char[slot].skills[W8_SKILL_COMMUNICATION].level;
                speaker = slot;
            }
        }
    }
    g_npc_interaction_state->dialogue_speaker = speaker;
    for (slot = 0; slot < 8; ++slot) {
        if (g_status.buffers.XChar[slot].fOccupied) {
            SetPortraitTargetPose(&gXStatus.monster_manager_entries[slot], 1);
        }
    }
    selected = g_npc_interaction_state->dialogue_npc;
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
    auto* state = g_npc_interaction_state;
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
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetLayoutMode(
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetLayoutMode(
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_6].text.SetLayoutBounds(0x5d, 0x197,
                                                                                     0xa0, 0x1a7);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 0].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 0, 0x7c2);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 1].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 1, 0x7c3);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 2].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 2, 0x7c4);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 3].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 3, 0x7c5);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 4].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 4, 0x7c6);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 5].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_OPTION_BUTTONS + 5, 0x7c7);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 0].primary = [] {
        ToggleNpcTradeCategory(0, W8_NPC_TRADE_HAND);
    };
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 1].primary = [] {
        ToggleNpcTradeCategory(1, W8_NPC_TRADE_ACCESSORY);
    };
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 2].primary =
        ToggleNpcCharacterUsabilityFilter;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 3].primary = [] {
        ToggleNpcTradeCategory(3, W8_NPC_TRADE_BODY);
    };
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 4].primary = [] {
        ToggleNpcTradeCategory(4, W8_NPC_TRADE_OTHER);
    };
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 5].primary =
        ToggleNpcPartyUsabilityFilter;

    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_12, 100);
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_13, 0x65);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_23].text.SetText(
        g_localized_strings[0x741], g_wiz_text_font_secondary);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_23].primary = SubmitNpcDialogueInput;
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_23, 0x66);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_22].text.SetText(
        g_localized_strings[0x742], g_wiz_text_font_secondary);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_22].primary = SubmitNpcWhereIsQuery;
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_22, 0x67);

    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetLayoutMode(
        g_W8TextBufferAlignBottom | g_W8TextBufferAlignRight);
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_31, 0x7c9);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_33].text.SetLayoutMode(
        g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_33, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_34].text.SetLayoutMode(
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_34, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetLayoutMode(
        g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_35, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_36].text.SetLayoutMode(
        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_36, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_7].primary = BackOutNpcDialogue;
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
    W8MonsterInfo* monster_info = GetNpcMonsterInfo(g_npc_interaction_state->dialogue_npc);
    if (monster_info != 0) {
        monster_info->p3D->UpdateAngles();
    }
    if (g_npc_interaction_state->scripted_dialogue) {
        return;
    }
    if (g_npc_interaction_state->dialogue_hidden != 0 && IsRightButtonHeld()) {
        SetNpcDialogueHidden(0);
    }
    field_text = NpcFieldText();
    if (g_npc_interaction_state->pending_layout != W8_DIALOGUE_LAYOUT_NONE) {
        g_level_block->text_box_visible = false;
        EnableDialogueTextBoxRegions();
        SetTextBoxScrollInput(true);
        g_level_block->action_panel_visible = 1;
        W8NpcDialogueLayout layout = g_npc_interaction_state->pending_layout;
        SwitchNpcDialogueLayout(W8_DIALOGUE_LAYOUT_NONE);
        OpenNpcDialogueLayout(layout);
        g_npc_interaction_state->pending_layout = W8_DIALOGUE_LAYOUT_NONE;
    }
    if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
        if (field_text.size() == 0) {
            if (g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_12].enabled) {
                SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_12, false);
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_12].dirty = true;
            }
        } else if (!g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_12].enabled) {
            SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_12, true);
            g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_12].dirty = true;
        }
        int selection = g_npc_interaction_state->ui->transcript->GetSelectedTranscriptEntryIndex();
        if (selection == -1) {
            if (g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_13].enabled) {
                SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_13, false);
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_13].dirty = true;
            }
        } else if (!g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_13].enabled) {
            SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_13, true);
            g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_13].dirty = true;
        }
        SetNpcActionEnabled(W8_NPC_CONTROL_SCROLL_UP_BUTTON,
                            g_npc_interaction_state->ui->transcript->HandleScrollUpCommand(true) !=
                                0);
        SetNpcActionEnabled(
            W8_NPC_CONTROL_SCROLL_DOWN_BUTTON,
            g_npc_interaction_state->ui->transcript->HandleScrollDownCommand(true) != 0);
    }
    if (g_npc_interaction_state->ui != nullptr &&
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.m_highlighted != 0 &&
        w8_get_ticks() - g_trade_highlight_tick > 500) {
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.m_highlighted = false;
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetGeometryDirty();
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].text.m_highlighted = false;
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetGeometryDirty();
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].dirty = true;
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
    switch (g_npc_interaction_state->dialogue_layout) {
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
    if (!gXStatus.fNpcDialogueMode) {
        return;
    }
    if (g_npc_interaction_state->scripted_dialogue) {
        gXStatus.fNpcDialogueMode = false;
        ResumeMainGameWorld();
        return;
    }
    if (gXStatus.fCampMode) {
        skip_exit_actions = true;
    }
    if (g_npc_interaction_state->dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    if (g_npc_interaction_state->dialogue_npc->record->monster_bound == 0) {
        g_npc_interaction_state->dialogue_npc->dismissed_flag = true;
        g_npc_interaction_state->dialogue_npc->disposition_at_open =
            GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc);
        W8NpcState* npc = g_npc_interaction_state->dialogue_npc;
        npc->dismissed_timer = 0;
    }
    gXStatus.fNpcDialogueMode = false;
    g_level_block->action_panel_visible = 0;
    DisableDialogueTextBoxRegions();
    SetTextBoxScrollInput(false);
    CloseActiveNpcDialogueLayout();
    SetNpcDialogueInputEnabled(2, false);
    SelectTextBox(0);
    g_level_block->text_box_visible = true;
    ApplyMainGameModeFlag(gXStatus.fCampMode ? g_settings.main_ui_mode
                                             : g_npc_interaction_state->saved_main_ui_mode,
                          true);
    g_npc_interaction_state->ui.reset();
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    for (unsigned int index = 0; index < gXStatus.monsters.size(); index++) {
        ClearMonsterCharm(MonsterGetScriptPartByLocationIndex(index));
    }
    GetNpcMonsterInfo(g_npc_interaction_state->dialogue_npc);
    g_npc_text_fields.reset();
    ResumeMainGameWorld();
    if (!skip_exit_actions) {
        if (!g_npc_interaction_state->held_item_pending) {
            SetTargetCursor(W8_CURSOR_NONE);
        } else {
            g_status.item_in_hand = g_npc_interaction_state->pending_item;
            SetItemCursor(0);
            g_npc_interaction_state->held_item_pending = false;
        }
        W8NpcState* npc = g_npc_interaction_state->dialogue_npc;
        if (!g_npc_interaction_state->suppress_parting_reaction && npc->record->merchant == 0 &&
            !npc->is_grouped && GetNpcMonsterInfo(npc) != 0) {
            int slot = GetRandomCharacter(1, 1, -1, -1);
            if (slot != -1) {
                W8CharacterEvent* event = QueueCharacterEvent(
                    &g_status.buffers.Char[slot], g_special_event8, g_character_event_no_preempt,
                    g_character_event_no_flags, g_character_event_full_volume);
                if (event != 0) {
                    event->DelayDispatch(1000);
                }
            }
        }
    }
    FlushPendingNoticeLines();
    if (g_npc_interaction_state->camera_redirected) {
        g_gd_camera->SetPitch(g_npc_interaction_state->saved_camera_pitch);
    }
    if (g_npc_interaction_state->dialogue_npc->name_style == 0xf &&
        GetFact(W8_FACT_ALIGNMENT_UMPANI) != 0) {
        g_status.trang_check_clock = g_status.world_clock;
        g_status.trang_check_pending = true;
    }
}

/* Whether the open NPC dialogue transcript covers the party slot's
   portrait: dialogue mode up, panel invalidation not suppressed and the
   controller enabled, then the slot's band check. Portrait and
   character-update paths skip the covered rows through this. */
bool IsPortraitObscuredByNpcDialogue(unsigned int party_slot)
{
    if (gXStatus.fNpcDialogueMode && !g_npc_interaction_state->scripted_dialogue &&
        g_npc_interaction_state->ui->panels[2].active) {
        return g_npc_interaction_state->ui->transcript->IsSlotPortraitTranscriptCovered(party_slot);
    }
    return false;
}

/* Forward one text/cursor rectangle to the action panel while the dialogue
   screen is not suppressing panel invalidation. */
void InvalidateMainGameActionPanelRect(const W8ControlsRect*)
{
    if (!g_npc_interaction_state->scripted_dialogue && g_npc_interaction_state->ui)
        g_npc_interaction_state->ui->panels[1].dirty = true;
}

void ActivateNpcDialoguePanels(bool force)
{
    if (g_npc_interaction_state->scripted_dialogue || !g_npc_interaction_state->ui) return;
    for (int index = 0; index < 7; ++index) {
        const bool redraw_scroll = index == 1 && g_npc_interaction_state->ui->panels[1].active &&
                                   (force || NpcPanelDirty(1));
        PaintNpcPanel(index, GetFrameBufferSurface(), force);
        if (redraw_scroll) RedrawTextBoxScrollChrome();
    }
}

bool HasNpcDialogueDirtyPanels()
{
    if (g_npc_interaction_state->scripted_dialogue || !g_npc_interaction_state->ui) return false;
    for (int index = 0; index < 7; ++index)
        if (g_npc_interaction_state->ui->panels[index].active && NpcPanelDirty(index)) return true;
    return false;
}

/* Retire the current dialogue layout mode into previous_dialogue_layout and, when nonzero,
   install `value` as the new one; either way the dialogue cursor helper gets
   re-run while the flag is set. */
static void SetNpcDialogueLayoutMode(W8NpcDialogueLayout value)
{
    if (value == W8_DIALOGUE_LAYOUT_NONE) {
        g_npc_interaction_state->previous_dialogue_layout =
            g_npc_interaction_state->dialogue_layout;
        g_npc_interaction_state->dialogue_layout = W8_DIALOGUE_LAYOUT_NONE;
    } else {
        g_npc_interaction_state->dialogue_layout = value;
    }
    if (g_npc_interaction_state->dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
}

static void RefreshNpcServiceAvailability(int party_slot)
{
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1,
                        CanCharacterCastSpell(&g_status.buffers.Char[party_slot], W8_SPELL_CHARM));
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_1].dirty = true;
    SetNpcActionEnabled(
        W8_NPC_CONTROL_TEXT_2,
        CanCharacterCastSpell(&g_status.buffers.Char[party_slot], W8_SPELL_MINDREAD));
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_2].dirty = true;
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4,
                        CharacterHasServiceItem(&g_status.buffers.Char[party_slot]));
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_4].dirty = true;
}

/* Whether the typed-text cursor of the active NPC dialogue is up; only the
   main-game action keys keep working while it owns input. */
/* Refresh the mode-1 service buttons and mode-4 item editor for the party
   slot that entered the dialogue. Mode 1 enables the heal/cure/service rows
   from what the character can actually use; mode 4 resets the editor and
   re-arms the trade prompt. */
void SyncNpcServiceButtons(int party_slot)
{
    if (gXStatus.fNpcDialogueMode) {
        if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_SERVICES) {
            RefreshNpcServiceAvailability(party_slot);
        } else if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
            SelectNpcPartyItems();
            UpdateNpcDialogueSubMode();
        }
    }
}

unsigned char IsNpcDialogueCursorActive(void)
{
    if (!gXStatus.fNpcDialogueMode) {
        return 0;
    }
    return g_npc_interaction_state->dialogue_hidden;
}

/* Whether an NPC dialogue is up in the layout that posts to the main text
   box (mode 4) rather than to the dialogue's own pane. */
bool IsNpcDialogueTextBoxActive(void)
{
    if (!gXStatus.fNpcDialogueMode) {
        return false;
    }
    return g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX;
}

void TryNpcDialoguePickpocket(int party_slot)
{
    if (gXStatus.fNpcDialogueMode && g_npc_interaction_state->dialogue_hidden != 0) {
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
        g_npc_interaction_state->last_mouse_x = event.position.x;
        g_npc_interaction_state->last_mouse_y = event.position.y;
        if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
            y = event.position.y;
            if (y >= g_level_block->text_box_top && y <= g_level_block->text_box_bottom) {
                line = (y - g_level_block->text_box_top) / 11;
                HighlightNoticeRow(line);
                g_npc_interaction_state->hovered_text_line = line;
            }
        }
        return UiResult::Handled;
    }
    if (event.position.x == g_npc_interaction_state->last_mouse_x &&
        event.position.y == g_npc_interaction_state->last_mouse_y) {
        return UiResult::Handled;
    }
    g_npc_interaction_state->last_mouse_x = event.position.x;
    g_npc_interaction_state->last_mouse_y = event.position.y;
    if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
        HighlightNoticeWordAt(3, event.position.x, event.position.y);
        return UiResult::Handled;
    }
    if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        y = event.position.y;
        if (y >= g_level_block->text_box_top && y <= g_level_block->text_box_bottom) {
            line = (y - g_level_block->text_box_top) / 11;
            if (line != g_npc_interaction_state->hovered_text_line) {
                HighlightNoticeRow(line);
            }
            g_npc_interaction_state->hovered_text_line = line;
        }
    }
    return UiResult::Handled;
}

/* Mouse-wheel line tracking on the main NPC text box: flag forces the redraw
   even when the hovered line has not changed. */
void NpcDialogueTextBoxWheelAt(short x, unsigned short y, bool flag)
{
    int line;

    if (g_npc_interaction_state->dialogue_layout != W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        return;
    }
    if (static_cast<int>(y) < g_level_block->text_box_top ||
        static_cast<int>(y) > g_level_block->text_box_bottom) {
        return;
    }
    line = (static_cast<int>(y) - g_level_block->text_box_top) / 11;
    if (line != g_npc_interaction_state->hovered_text_line || flag) {
        HighlightNoticeRow(line);
    }
    g_npc_interaction_state->hovered_text_line = line;
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

    switch (g_npc_interaction_state->dialogue_layout) {
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
        slot = GetHoveredTextLine(2);
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

    switch (g_npc_interaction_state->dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        slot = GetHoveredTextLine(2);
        if (slot == -1) {
            return;
        }
        UpdateNpcTradeSelection(slot, ((GetInputState().modifiers & SHIFT_DOWN) != 0) ? 1 : 0, 1);
        item = ResolveNpcTradeRow(slot, false, 0, 1);
        if (item == 0) {
            return;
        }
        dialog = new W8AssayDialog(item, &g_status.buffers.Char[g_status.selected_character]);
        dialog->SetText("");
        dialog->SetOrigin(g_info_dialog_x, 0x48);
        dialog->on_close = [] { RequestRedraw(W8_MAIN_REDRAW_ALL); };
        OpenModal(std::unique_ptr<ModalContent>(dialog));
        return;
    case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
        word = HitTestNoticeWord(3, x, y, &line);
        if (word == 0) {
            return;
        }
        word_text = CopyNoticeWordText(word, 3, line);
        AddNpcDialogueKeyword(word_text, -1, 0);
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

    switch (g_npc_interaction_state->dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        break;
    case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
        if (g_npc_interaction_state->modal_dialog_open) {
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
    if (y >= g_level_block->text_box_top && y <= g_level_block->text_box_bottom) {
        line = (y - g_level_block->text_box_top) / 11;
        HighlightNoticeRow(line);
        g_npc_interaction_state->hovered_text_line = line;
    }
    slot = GetHoveredTextLine(2);
    if (slot != -1) {
        g_npc_interaction_state->selected_trade_row = -1;
        UpdateNpcTradeSelection(slot, 0, 1);
        if (g_npc_interaction_state->trade_item != 0) {
            ConfirmNpcTradeItem();
        }
    }
    NpcDialogueTextBoxWheelAt(0, static_cast<unsigned short>(y), true);
}

static void ShowNpcTradeGold()
{
    std::string text;

    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, true);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetFontStateIndex(1);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetGeometryDirty();
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, true);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_36].text.SetText(
        g_localized_strings[0x72d], g_wiz_text_font_secondary);
    text = fmt::format("{:d}g", g_npc_interaction_state->trade_gold);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetText(
        text, g_wiz_text_font_secondary);
    g_npc_interaction_state->ui->panels[5].dirty = true;
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
    switch (g_npc_interaction_state->dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
        break;
    case W8_DIALOGUE_LAYOUT_SERVICES:
        item = GetNpcTradeSlotItem(index);
        g_npc_interaction_state->trade_item = item;
        if (g_npc_interaction_state->trade_item != 0) {
            SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, true);
        }
        return;
    default:
        return;
    }
    item = ResolveNpcTradeRow(index, true, increment, commit);
    g_npc_interaction_state->trade_item = item;
    if (g_npc_interaction_state->trade_item != 0) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31,
                            1 < g_npc_interaction_state->trade_item->stack_count);
        if (g_item_records[g_npc_interaction_state->trade_item->iItemNo].equip_class ==
            W8_ITEM_EQUIP_CLASS_AMMUNITION) {
            SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, false);
        }
        text = ShortenTextToWidth(FormatItemDisplayName(g_npc_interaction_state->trade_item, false),
                                  0x7d, g_wiz_text_font_secondary);
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_36].text.SetText(
            text, g_wiz_text_font_secondary);
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetFontStateIndex(1);
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetGeometryDirty();
        switch (g_npc_interaction_state->trade_mode) {
        case W8_NPC_TRADE_BUY:
            if (g_status.party_gold <
                static_cast<unsigned int>(CalculateNpcTradeStackPrice(
                    g_npc_interaction_state->dialogue_npc,
                    g_npc_interaction_state->trade_item->iItemNo, W8_TRADE_PRICE_PARTY_BUYS,
                    g_npc_interaction_state->trade_quantity,
                    g_npc_interaction_state->trade_item->identified))) {
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetFontStateIndex(
                    0);
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37]
                    .text.SetGeometryDirty();
            }
            price_text = fmt::format("{:d}g", CalculateNpcTradeStackPrice(
                                                  g_npc_interaction_state->dialogue_npc,
                                                  g_npc_interaction_state->trade_item->iItemNo,
                                                  W8_TRADE_PRICE_PARTY_BUYS,
                                                  g_npc_interaction_state->trade_quantity,
                                                  g_npc_interaction_state->trade_item->identified));
            break;
        case W8_NPC_TRADE_SELL: {
            wants_item = NpcAcceptsTradeItem(g_npc_interaction_state->dialogue_npc,
                                             g_npc_interaction_state->trade_item);
            if (!wants_item) {
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetFontStateIndex(
                    0);
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37]
                    .text.SetGeometryDirty();
            }
            int price = CalculateNpcTradeStackPrice(
                g_npc_interaction_state->dialogue_npc, g_npc_interaction_state->trade_item->iItemNo,
                W8_TRADE_PRICE_PARTY_SELLS, g_npc_interaction_state->trade_quantity,
                g_npc_interaction_state->trade_item->identified);
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
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetText(
            price_text, g_wiz_text_font_secondary);
        return;
    }
    if (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_GIVE && index == 0) {
        ShowNpcTradeGold();
    }
}

/* Reset the secondary dialogue editor: drop the pending item, clear the item
   notice control and both secondary labels, and restore the option row. */
void ResetNpcDialogueItemEditor(void)
{
    ClearSelectedTextLine(2);
    g_npc_interaction_state->trade_item = 0;
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, false);
    g_npc_interaction_state->ui->item_preview.reset();
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText("", g_large_font);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetText(
        "", g_wiz_text_font_secondary);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_36].text.SetText(
        "", g_wiz_text_font_secondary);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, false);
    g_npc_interaction_state->selected_trade_row = -1;
    g_npc_interaction_state->trade_quantity = 1;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetFontStateIndex(-1);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetGeometryDirty();
}

/* Retire the current dialogue layout, then open the layout `interact_id`
   selects. */
/* Close the active sub-layout and reopen the layout it replaced: mode 1 goes
   back to the remembered previous_dialogue_layout layout, the option layout returns to the
   topic menu or the transcript by reopen_topics, and mode 5 always lands on the
   transcript. */
static void BackOutNpcDialogue(void)
{
    switch (g_npc_interaction_state->dialogue_layout) {
    case W8_DIALOGUE_LAYOUT_SERVICES:
        CloseNpcDialogueMode1Layout();
        switch (g_npc_interaction_state->previous_dialogue_layout) {
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
        bool flag = g_npc_interaction_state->reopen_topics;
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
    if (!g_npc_interaction_state->modal_dialog_open) {
        if (GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc) == W8_NPC_BAND_FRIENDLY) {
            g_npc_interaction_state->suppress_parting_reaction = false;
            g_npc_interaction_state->farewell_queued = true;
            g_npc_dialogue.QueueQuote(0x5c, false, false, false);
            return;
        }
        SwitchNpcDialogueLayout(W8_DIALOGUE_LAYOUT_NONE);
    }
    EndNpcDialogueSession(false);
}

static void PromptNpcDispositionChange(void)
{
    W8MessageDialogBase* dialog = CreateMessageDialog();

    SetDialogPrompt(dialog, g_localized_strings[0x798]);
    dialog->on_complete = OnNpcDispositionPromptClosed;
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

static void OnNpcDispositionPromptClosed(bool accepted)
{
    if (accepted) {
        SetNpcDispositionBand(g_npc_interaction_state->dialogue_npc, W8_NPC_BAND_HOSTILE);
        g_npc_dialogue.QueueQuote(0x18, false, false, false);
        g_npc_dialogue.Queue(W8NpcCommand::close_and_resume_npc);
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
    g_npc_interaction_state->ui->actions[slot].text.SetText(g_localized_strings[text_id],
                                                            g_wiz_text_font_secondary);
    g_npc_interaction_state->ui->actions[slot].primary = std::move(callback);
}

/* Bring up the mode-2 dialogue layout: the option panels come up, the caption
   takes the NPC's name, and the five topics get their strings and callbacks. */
void ShowNpcDialogueTopicMenu(void)
{
    g_npc_interaction_state->dialogue_layout = W8_DIALOGUE_LAYOUT_TOPIC_MENU;
    if (g_npc_interaction_state->dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(4, true);
    SetNpcDialogueInputEnabled(2, true);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, true);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
        g_npc_interaction_state->dialogue_npc->record->source_name, g_wiz_text_bold_font);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].dirty = true;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText(
        g_localized_strings[0x726], g_wiz_text_bold_font);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_1, 0x727, SelectNpcDialogueService);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_2, 0x728, SelectNpcDialogueTalk);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_3, 0x729, SelectNpcDialogueExit);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_4, 0x723, PromptNpcDispositionChange);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_5, 0x724, EnterNpcServiceLayout);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_6, 0x725, LeaveNpcDialogueLayout);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_7, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2,
                        !g_npc_interaction_state->dialogue_npc->talk_cooldown_active);
    if (!g_npc_interaction_state->dialogue_npc->trade_cooldown_active) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
    } else {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
    }
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
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
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText("",
                                                                              g_wiz_text_bold_font);
    SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
}

/* The "Trade" option button: hand the NPC's trade answer to ApplyNpcInteraction,
   then react to the disposition band - friendly queues the trade quote and
   returns to the transcript, neutral queues the neutral answer, hostile sets
   the hostile band and queues the refusal. */
static void SelectNpcDialogueService(void)
{
    unsigned char band;

    ApplyNpcInteraction(g_npc_interaction_state->dialogue_npc, 1,
                        g_npc_interaction_state->dialogue_speaker, 0, 0);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
    band = GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc);
    if (band == W8_NPC_BAND_FRIENDLY) {
        g_npc_dialogue.QueueQuote(4, false, false, false);
        g_npc_dialogue.Queue(W8NpcCommand::reopen_transcript);
        return;
    }
    if (band == W8_NPC_BAND_NEUTRAL) {
        g_npc_dialogue.QueueQuote(6, false, false, false);
        return;
    }
    SetNpcDispositionBand(g_npc_interaction_state->dialogue_npc, W8_NPC_BAND_HOSTILE);
    g_npc_dialogue.QueueQuote(0x18, false, false, false);
    g_npc_dialogue.Queue(W8NpcCommand::close_and_resume_npc);
}

/* The "Talk" option button: friendly NPCs leave the dialogue outright,
   neutral ones queue the talk quote, hostile ones set the hostile band and
   queue the refusal. */
static void SelectNpcDialogueTalk(void)
{
    unsigned char band;

    ApplyNpcInteraction(g_npc_interaction_state->dialogue_npc, 0,
                        g_npc_interaction_state->dialogue_speaker, 0, 0);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
    band = GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc);
    if (band == W8_NPC_BAND_FRIENDLY) {
        HandleNpcDialogueDeparture(0);
        OpenNpcDialogueTranscriptLayout();
        return;
    }
    if (band == W8_NPC_BAND_NEUTRAL) {
        g_npc_dialogue.QueueQuote(3, false, false, false);
        return;
    }
    SetNpcDispositionBand(g_npc_interaction_state->dialogue_npc, W8_NPC_BAND_HOSTILE);
    g_npc_dialogue.QueueQuote(0x18, false, false, false);
    g_npc_dialogue.Queue(W8NpcCommand::close_and_resume_npc);
}

/* The "Exit" option button: close the option row, mark the dialogue as leaving
   through the transcript and open the option layout for the farewell. */
static void SelectNpcDialogueExit(void)
{
    CloseNpcDialogueLayout();
    g_npc_interaction_state->trade_mode = W8_NPC_TRADE_GIVE;
    g_npc_interaction_state->reopen_topics = true;
    OpenNpcDialogueOptionLayout();
}

void SyncNpcDialogueTranscriptScrollButtons()
{
    if (g_npc_interaction_state->ui->transcript->IsExpanded()) {
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

    g_npc_interaction_state->dialogue_layout = W8_DIALOGUE_LAYOUT_TRANSCRIPT;
    if (g_npc_interaction_state->dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(2, true);
    SetNpcPanelActive(3, true);
    SetNpcPanelActive(6, true);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText("",
                                                                              g_wiz_text_bold_font);
    SetNpcDialogueInputEnabled(2, true);
    SetNpcDialogueInputEnabled(0, true);
    if (g_npc_interaction_state->dialogue_npc->name_style == 0x32) {
        g_status.monster_name_buffer =
            fmt::format("Al-{}", g_status.buffers.Char[g_status.sedexus_party_slot].name);
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
            g_status.monster_name_buffer, g_wiz_text_bold_font);
    } else {
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
            g_npc_interaction_state->dialogue_npc->record->source_name, g_wiz_text_bold_font);
    }
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_1, 0x735, EnterNpcTradeOptions);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_2, 0x738, ShowNpcDialogueNotice);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_3, 0x724, EnterNpcServiceLayout);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_4, 0x737, RequestNpcJoinParty);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_5, 0x723, PromptNpcDispositionChange);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_6, 0x725, LeaveNpcDialogueLayout);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_SCROLL_UP_BUTTON].down =
        ScrollNpcDialogueUp;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_SCROLL_DOWN_BUTTON].down =
        ScrollNpcDialogueDown;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_12].primary = SubmitNpcDialogueKeyword;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_13].primary =
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
    g_npc_interaction_state->ui->transcript->RestoreTranscriptEntries();
    g_npc_interaction_state->ui->transcript->SetTranscriptCategoryFilter(
        g_npc_interaction_state->dialogue_category_filter);
    g_npc_interaction_state->ui->transcript->SetTranscriptSorted(
        g_npc_interaction_state->transcript_sorted);
    g_npc_interaction_state->ui->transcript->Expand();
    SyncNpcDialogueTranscriptScrollButtons();
    SyncDialogueCategoryButtons();
    if (g_npc_interaction_state->transcript_sorted) {
        SetNpcActionPressed(W8_NPC_CONTROL_SORT_BUTTON, true);
    } else {
        SetNpcActionPressed(W8_NPC_CONTROL_SORT_BUTTON, false);
    }
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    for (index = 0; index < 8; ++index) {
        if (g_status.buffers.XChar[index].fOccupied) {
            DisableMainGamePortraitSlot(index);

            g_main_game_state->edge_targets[index]->setEnabled(false);
        }
    }
    if (g_npc_interaction_state->dialogue_npc->record->merchant != 0) {
        DisableNpcTradeActions();
    }
    if (g_npc_interaction_state->dialogue_npc->name_style == 0x17) {
        DisableNpcTradeActions();
    }
    g_npc_interaction_state->transcript_open_count++;
    SelectTextBox(3);
}

/* Tear down the mode-3 transcript layout: fold the dialogue text controller
   back up, drop the panels, then re-enable whichever occupied party rows
   still own region slots. */
static void CloseNpcDialogueTranscriptLayout(void)
{
    int index;

    g_npc_interaction_state->ui->transcript->SaveTranscriptEntries();
    g_npc_interaction_state->ui->transcript->Collapse();
    g_npc_interaction_state->ui->transcript->ClearTranscriptEntries();
    SetNpcDialogueInputEnabled(2, false);
    SetNpcDialogueInputEnabled(0, false);
    SetNpcPanelActive(0, false);
    SetNpcPanelActive(1, false);
    SetNpcPanelActive(2, false);
    SetNpcPanelActive(3, false);
    SetNpcPanelActive(6, false);
    SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
    g_npc_interaction_state->ui->transcript->ClearBackground();
    EraseRegion(0x1dc, 0x11b, 0x269, 0x1c2);
    for (index = 0; index < 8; ++index) {
        if (g_status.buffers.XChar[index].fOccupied) {
            EnableMainGamePortraitSlot(index);

            g_main_game_state->edge_targets[index]->setEnabled(true);
        }
    }
}

static void RequestNpcJoinParty(void)
{
    unsigned int slot;

    ShowNotice(W8_FONT_PALETTE_BRONZE, g_localized_strings[0x748], 3, GetTextBoxScrollRange());
    if (g_npc_interaction_state->dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    if (g_npc_interaction_state->dialogue_npc->record->has_group != 0) {
        slot = FindFreePartySlot(0, 2);
        if (slot == 0xffffffff) {
            g_npc_dialogue.QueueQuote(0xc, false, false, false);
            return;
        }
        if (CanNpcJoinParty(g_npc_interaction_state->dialogue_npc)) {
            g_npc_dialogue.QueueQuote(0xd, false, false, false);
            g_npc_dialogue.Queue(W8NpcRecruit{g_npc_interaction_state->dialogue_npc->name_style});
            return;
        }
    }
    g_npc_dialogue.QueueQuote(0xb, false, false, false);
}

static void ShowNpcDialogueNotice(void)
{
    ShowNotice(W8_FONT_PALETTE_BRONZE, g_localized_strings[0x749], 3, GetTextBoxScrollRange());
    SetNpcDialogueHidden(1);
}

static void EnterNpcTradeOptions(void)
{
    CloseNpcDialogueTranscriptLayout();
    g_npc_interaction_state->trade_mode = W8_NPC_TRADE_BUY;
    OpenNpcDialogueOptionLayout();
}

static void ScrollNpcDialogueUp(void)
{
    g_npc_interaction_state->ui->transcript->HandleScrollUpCommand(false);
}

static void ScrollNpcDialogueDown(void)
{
    g_npc_interaction_state->ui->transcript->HandleScrollDownCommand(false);
}

static void SubmitNpcDialogueKeyword(void)
{
    std::string keyword;

    keyword = NpcFieldText();
    AddNpcDialogueKeyword(keyword, -1, 0);
}

void AddNpcDialogueKeyword(std::string text, signed char category, int play_chime)
{
    W8NpcDialogueTextController* controller;
    unsigned int index;

    if (text.size() == 0) {
        return;
    }
    controller = g_npc_interaction_state->ui->transcript.get();
    StripNpcKeywordPunctuation(text);
    if (category == W8_DIALOGUE_CATEGORY_ALL) {
        for (index = 0; index < gXStatus.uiItemsInDatabase; ++index) {
            if (CompareTextIgnoreAsciiCase(text, g_item_records[index].display_name) == 0) {
                break;
            }
        }
        if (index < gXStatus.uiItemsInDatabase) {
            category = W8_DIALOGUE_CATEGORY_ITEMS;
        } else {
            for (index = 0; index < gXStatus.uiNpcsInDatabase; ++index) {
                if (CompareTextIgnoreAsciiCase(text, g_npc_records[index].source_name) == 0) {
                    break;
                }
            }
            if (index < gXStatus.uiNpcsInDatabase) {
                category = W8_DIALOGUE_CATEGORY_PEOPLE;
            } else {
                for (index = 0; g_dialogue_person_keywords[index][0] != 0; ++index) {
                    if (CompareTextIgnoreAsciiCase(text, g_dialogue_person_keywords[index]) == 0) {
                        break;
                    }
                }
                if (g_dialogue_person_keywords[index][0] != 0) {
                    category = W8_DIALOGUE_CATEGORY_PEOPLE;
                } else {
                    for (index = 0;
                         index < static_cast<unsigned int>(g_dialogue_place_keyword_count);
                         ++index) {
                        if (CompareTextIgnoreAsciiCase(
                                text, g_localized_strings[g_dialogue_place_keyword_ids[index]]) ==
                            0) {
                            break;
                        }
                    }
                    if (index < static_cast<unsigned int>(g_dialogue_place_keyword_count)) {
                        category = W8_DIALOGUE_CATEGORY_PLACES;
                    } else {
                        category = W8_DIALOGUE_CATEGORY_MISC;
                    }
                }
            }
        }
    }
    if (controller != 0) {
        if (controller->AddTranscriptEntry(text, category, play_chime) != 0) {
            if (g_npc_interaction_state->dialogue_category_filter != W8_DIALOGUE_CATEGORY_ALL &&
                g_npc_interaction_state->dialogue_category_filter != category) {
                g_npc_interaction_state->dialogue_category_filter = category;
                g_npc_interaction_state->ui->transcript->SetTranscriptCategoryFilter(
                    g_npc_interaction_state->dialogue_category_filter);
                SyncDialogueCategoryButtons();
            }
            if (play_chime != 0) {
                wiz8::sound::play(
                    reinterpret_cast<char*>(const_cast<char*>( // reinterpret-ok: SGP text ABI
                        "Data\\Sound\\Misc\\Keyword Chime.wav")));
            }
            controller->Collapse();
            controller->Expand();
        }
        controller->InvalidateAll();
        SyncNpcDialogueTranscriptScrollButtons();
    }
}

static void RefreshNpcDialogueTranscript(void)
{
    g_npc_interaction_state->ui->transcript->RemoveSelectedTranscriptEntry();
    g_npc_interaction_state->ui->transcript->Collapse();
    g_npc_interaction_state->ui->transcript->Expand();
    SyncNpcDialogueTranscriptScrollButtons();
}

static void SyncNpcDialogueListFilter(void)
{
    if (g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_SORT_BUTTON].interaction.selected) {
        g_npc_interaction_state->transcript_sorted = true;
    } else {
        g_npc_interaction_state->transcript_sorted = false;
    }
    g_npc_interaction_state->ui->transcript->SetTranscriptSorted(
        g_npc_interaction_state->transcript_sorted);
}

static void SelectNpcDialogueTopics(W8NpcDialogueControlSlot button, W8DialogueCategory category)
{
    if (g_npc_interaction_state->ui->actions[button].interaction.selected) {
        g_npc_interaction_state->dialogue_category_filter = category;
        SyncDialogueCategoryButtons();
    }
    g_npc_interaction_state->ui->transcript->SetTranscriptCategoryFilter(category);
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

    g_npc_interaction_state->dialogue_layout = W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX;
    if (g_npc_interaction_state->dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText("",
                                                                              g_wiz_text_bold_font);
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(5, true);
    SetNpcDialogueInputEnabled(2, true);
    SetNpcDialogueInputEnabled(1, true);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_34].text.SetText(
        g_localized_strings[0x72d], g_wiz_text_font_secondary);
    ConfigureNpcTradeCommands();
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_5, 0x72b, SelectNpcPartyItems);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_6, 0x72c, SelectNpcStockItems);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].primary = OpenNpcItemAssay;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].secondary = OpenNpcItemAssay;
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_31].primary = ConfirmNpcTradeSlot;
    buffer = fmt::format("{:d}g", g_status.party_gold);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_33].text.SetText(
        buffer, g_wiz_text_font_secondary);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].primary = ConfirmNpcTradeItem;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_5].policy.primary_toggle =
        ButtonToggle::Release;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_6].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_5, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_6, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_1].policy.primary_toggle =
        ButtonToggle::Release;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_2].policy.primary_toggle =
        ButtonToggle::Release;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_3].policy.primary_toggle =
        ButtonToggle::Release;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_4].policy.primary_toggle =
        ButtonToggle::Release;
    UpdateNpcDialogueSubMode();
    g_npc_interaction_state->pending_trade_toggle = true;
    if (g_npc_interaction_state->reopen_topics) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, false);
    } else {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, true);
    }
    if (g_npc_interaction_state->dialogue_npc->record->owns_stock == 0) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, false);
    }
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
    SelectTextBox(2);
    if (!gXStatus.fCampMode) {
        ResetEditorStatusLine(2);
    }
    g_npc_interaction_state->trade_filter = 0;
    for (index = 0; index < 6; ++index) {
        SetNpcActionEnabled(W8_NPC_CONTROL_OPTION_BUTTONS + index, false);
    }
    RebuildNpcTradeItemList(true);
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
}

static void SelectNpcTradePoolControls(W8NpcDialogueControlSlot previous,
                                       W8NpcDialogueControlSlot selected)
{
    SetNpcActionPressed(previous, false);
    g_npc_interaction_state->ui->actions[previous].text.SetFontStateIndex(-1);
    g_npc_interaction_state->ui->actions[previous].text.SetGeometryDirty();
    SetNpcActionPressed(selected, true);
    g_npc_interaction_state->ui->actions[selected].text.SetFontStateIndex(3);
    g_npc_interaction_state->ui->actions[selected].text.SetGeometryDirty();
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
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
        g_localized_strings[text_id], g_wiz_text_bold_font);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].text.SetText(
        g_localized_strings[text_id], g_wiz_text_bold_font);
}

void UpdateNpcDialogueSubMode(void)
{
    switch (g_npc_interaction_state->trade_mode) {
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
    if (g_npc_interaction_state->pending_trade_toggle &&
        (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_SELL ||
         g_npc_interaction_state->trade_mode == W8_NPC_TRADE_GIVE)) {
        if (g_npc_interaction_state->trade_pc_items) {
            SelectNpcTradePoolControls(W8_NPC_CONTROL_TEXT_6, W8_NPC_CONTROL_TEXT_5);
        } else {
            SelectNpcTradePoolControls(W8_NPC_CONTROL_TEXT_5, W8_NPC_CONTROL_TEXT_6);
        }
        g_npc_interaction_state->pending_trade_toggle = false;
    }
    RebuildNpcTradeItemList(true);
}

/* Tear down the mode-4 option layout: the six option controls lose their
   secondary state and layout flags before everything is disabled. */
void CloseNpcDialogueOptionLayout(void)
{
    W8TextBuffer* text;

    ResetNpcDialogueItemEditor();
    if (!gXStatus.fCampMode) {
        g_npc_interaction_state->trade_mode = W8_NPC_TRADE_NONE;
    }
    SetNpcDialogueInputEnabled(2, false);
    SetNpcDialogueInputEnabled(1, false);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, true);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_1, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_2, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_3, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_4, false);
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_5, false);
    text = &g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_5].text;
    text->SetFontStateIndex(-1);
    text->SetGeometryDirty();
    SetNpcActionPressed(W8_NPC_CONTROL_TEXT_6, false);
    text = &g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_6].text;
    text->SetFontStateIndex(-1);
    text->SetGeometryDirty();
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_1].policy.primary_toggle =
        ButtonToggle::None;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_2].policy.primary_toggle =
        ButtonToggle::None;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_3].policy.primary_toggle =
        ButtonToggle::None;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_4].policy.primary_toggle =
        ButtonToggle::None;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_5].policy.primary_toggle =
        ButtonToggle::None;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_6].policy.primary_toggle =
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
    g_npc_interaction_state->reopen_topics = false;
    SelectTextBox(3);
    ScrollDialogueTextBoxToLine();
}

static void OpenNpcItemAssay(void)
{
    W8AssayDialog* dialog;
    const auto& image = g_npc_interaction_state->ui->item_preview;

    if (g_npc_interaction_state->trade_item != 0 && image &&
        image != CatalogArtwork::Load(0x1ac).spriteSheet()) {
        dialog = new W8AssayDialog(g_npc_interaction_state->trade_item,
                                   &g_status.buffers.Char[g_status.selected_character]);
        dialog->SetText("");
        dialog->SetOrigin(g_info_dialog_x, 0x48);
        dialog->on_close = [] { RequestRedraw(W8_MAIN_REDRAW_ALL); };
        OpenModal(std::unique_ptr<ModalContent>(dialog));
    }
}

static void SelectNpcPartyItems(void)
{
    ResetNpcDialogueItemEditor();
    SelectNpcTradePoolControls(W8_NPC_CONTROL_TEXT_6, W8_NPC_CONTROL_TEXT_5);
    RebuildNpcTradeItemList(true);
    g_npc_interaction_state->trade_pc_items = true;
}

static void SelectNpcStockItems(void)
{
    ResetNpcDialogueItemEditor();
    SelectNpcTradePoolControls(W8_NPC_CONTROL_TEXT_5, W8_NPC_CONTROL_TEXT_6);
    RebuildNpcTradeItemList(true);
    g_npc_interaction_state->trade_pc_items = false;
}

static void ConfirmNpcTradeSlot(void)
{
    int slot;

    if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        slot = GetSelectedTextLine(2);
        if (slot != -1) {
            UpdateNpcTradeSelection(slot, 0, 0);
            if (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_GIVE && slot == 0) {
                OpenNpcGoldAmountDialog();
            } else if (g_npc_interaction_state->trade_item->stack_count > 1) {
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
    g_npc_interaction_state->ui->panels[1].dirty = true;
    dialog = new W8SplitAmountDialog(g_status.party_gold);
    dialog->SetText("");
    dialog->SetOrigin(g_split_dialog_origin_x, g_split_dialog_origin_y);
    dialog->on_complete = [](int amount) {
        g_npc_interaction_state->trade_gold = amount;
        ShowNpcTradeGold();
    };
    OpenModal(std::unique_ptr<ModalContent>(dialog));
}

static void HighlightNpcTradeQuantity()
{
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.m_highlighted = true;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetGeometryDirty();
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].text.m_highlighted = true;
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].text.SetGeometryDirty();
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_35].dirty = true;
    g_trade_highlight_tick = w8_get_ticks();
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
    g_npc_interaction_state->ui->item_preview = g_item_video_objects.SharedSprites(item->iItemNo);
    if (item->stack_count < 2) {
        text = "";
    } else {
        count_text = fmt::format("{:d}", g_npc_interaction_state->trade_quantity);
        text = count_text;
    }
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
        text, g_wiz_text_font_secondary);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
}

static void UpdatePartyTradeQuantity(const W8ItemInstance* item, int row, bool decrement)
{
    if (g_item_records[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        g_npc_interaction_state->trade_quantity = item->stack_count;
    } else if (g_npc_interaction_state->selected_trade_row == row) {
        if (!decrement) {
            if (g_item_records[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
                ++g_npc_interaction_state->trade_quantity;
                if (item->stack_count < g_npc_interaction_state->trade_quantity) {
                    g_npc_interaction_state->trade_quantity = item->stack_count;
                }
            }
        } else {
            --g_npc_interaction_state->trade_quantity;
            if (g_npc_interaction_state->trade_quantity == 0) {
                g_npc_interaction_state->trade_quantity = 1;
            }
        }
    } else {
        g_npc_interaction_state->trade_quantity = 1;
    }
}

static W8ItemInstance* ResolveNpcTradeRow(int index, bool pick, char decrement, char commit)
{
    W8NpcItemEntry* entry;
    int selected;
    int hit;
    int i;

    selected = g_status.selected_character;
    hit = 0;
    if (g_npc_interaction_state->trade_mode != W8_NPC_TRADE_BUY &&
        g_npc_interaction_state->trade_mode != W8_NPC_TRADE_SHOPLIFT) {
        if (g_npc_interaction_state->trade_mode == W8_NPC_TRADE_GIVE) {
            g_npc_interaction_state->trade_gold = 0;
            if (index == 0) {
                g_npc_interaction_state->trade_gold = g_status.party_gold;
                g_npc_interaction_state->ui->item_preview =
                    CatalogArtwork::Load(0x1ac).spriteSheet();
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
                    "", g_wiz_text_font_secondary);
                return 0;
            }
            --index;
        }
        if (g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_5].interaction.selected == 0) {
            if (g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_6].interaction.selected &&
                g_status.party_item_count != 0) {
                i = 0;
                do {
                    if (g_status.party_item_pool[i].iItemNo != -1 &&
                        !NpcTradeItemAllowed(&g_status.party_item_pool[i])) {
                        if (index == hit) {
                            if (!pick) {
                                return &g_status.party_item_pool[i];
                            }
                            if (commit != 0) {
                                UpdatePartyTradeQuantity(&g_status.party_item_pool[i], hit,
                                                         decrement != 0);
                            }
                            g_npc_interaction_state->selected_trade_row = hit;
                            if (commit != 0) {
                                wiz8::sound::play(g_button_click_1);
                            }
                            ShowSelectedNpcTradeItem(&g_status.party_item_pool[i]);
                            return &g_status.party_item_pool[i];
                        }
                        ++hit;
                    }
                    ++i;
                    if (g_status.party_item_count <= static_cast<unsigned int>(i)) {
                        return 0;
                    }
                } while (true);
            }
        } else {
            W8Character* character = &g_status.buffers.Char[selected];
            for (i = 0; i < 8; ++i) {
                if (character->backpack[i].iItemNo != -1 &&
                    !NpcTradeItemAllowed(&character->backpack[i])) {
                    if (index == hit) {
                        if (!pick) {
                            return &character->backpack[i];
                        }
                        if (commit != 0) {
                            UpdatePartyTradeQuantity(&character->backpack[i], hit, decrement != 0);
                        }
                        g_npc_interaction_state->selected_trade_row = hit;
                        if (commit != 0) {
                            wiz8::sound::play(g_button_click_1);
                        }
                        ShowSelectedNpcTradeItem(&character->backpack[i]);
                        return &character->backpack[i];
                    }
                    ++hit;
                }
            }
        }
        return 0;
    }
    i = ResolveNpcTradeStockIndex(index);
    if (i == -1) {
        return 0;
    }
    entry = GetNpcItemAt(g_npc_interaction_state->dialogue_npc, i);
    if (entry == 0) {
        return 0;
    }
    if (commit != 0) {
        if (g_item_records[entry->item.iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION) {
            g_npc_interaction_state->trade_quantity = entry->item.stack_count;
        } else if (g_npc_interaction_state->selected_trade_row == i) {
            if (decrement == 0) {
                if (g_item_records[entry->item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
                    ++g_npc_interaction_state->trade_quantity;
                    if (entry->item.stack_count < g_npc_interaction_state->trade_quantity) {
                        g_npc_interaction_state->trade_quantity = entry->item.stack_count;
                    } else if (entry->item.stack_count != 0) {
                        HighlightNpcTradeQuantity();
                    }
                }
            } else {
                --g_npc_interaction_state->trade_quantity;
                if (g_npc_interaction_state->trade_quantity == 0) {
                    g_npc_interaction_state->trade_quantity = 1;
                } else if (entry->item.stack_count != 0) {
                    HighlightNpcTradeQuantity();
                }
            }
        } else {
            g_npc_interaction_state->trade_quantity = 1;
        }
    }
    g_npc_interaction_state->selected_trade_row = i;
    if (pick) {
        if (commit != 0) {
            wiz8::sound::play(g_button_click_1);
        }
        ShowSelectedNpcTradeItem(&entry->item);
    }
    return &entry->item;
}

/* Whether the current trade filter rejects `item`: the Zant NPC hides item
   0x290 until fact 0x3c, the trade_filter low bits gate character/party
   usability, and bits 0x3c select the accepted equipment slot group. */
bool NpcTradeItemAllowed(W8ItemInstance* item)
{
    W8ItemEquipSlotGroup group;

    if (g_npc_interaction_state->dialogue_npc->name_style == ',' &&
        GetFact(W8_FACT_ALIGNMENT_UMPANI) == 0 && item->iItemNo != 0x290) {
        return true;
    }
    if (g_npc_interaction_state->trade_filter == 0) {
        return false;
    }
    if (item->iItemNo != -1) {
        if ((g_npc_interaction_state->trade_filter & W8_NPC_TRADE_USABLE_BY_CHARACTER) == 0) {
            if ((g_npc_interaction_state->trade_filter & W8_NPC_TRADE_USABLE_BY_PARTY) != 0 &&
                AnyPartyMemberCanUseItem(item->iItemNo)) {
                return true;
            }
        } else {
            if (!CanCharacterUseItem(&g_status.buffers.Char[g_status.selected_character],
                                     item->iItemNo)) {
                return true;
            }
        }
        if ((g_npc_interaction_state->trade_filter & W8_NPC_TRADE_CATEGORY_MASK) == 0) {
            return false;
        }
        group = GetItemEquipSlotGroup(item->iItemNo);
        if (group == W8_ITEM_EQUIP_GROUP_HAND) {
            if ((g_npc_interaction_state->trade_filter & W8_NPC_TRADE_HAND) != 0) {
                return false;
            }
        } else if (group == W8_ITEM_EQUIP_GROUP_BODY) {
            if ((g_npc_interaction_state->trade_filter & W8_NPC_TRADE_BODY) != 0) {
                return false;
            }
        } else if (group == W8_ITEM_EQUIP_GROUP_ACCESSORY) {
            if ((g_npc_interaction_state->trade_filter & W8_NPC_TRADE_ACCESSORY) != 0) {
                return false;
            }
        } else if ((g_npc_interaction_state->trade_filter & W8_NPC_TRADE_OTHER) != 0) {
            return false;
        }
    }
    return true;
}

static void OpenNpcDialogueMode5Layout(void)
{
    std::string buffer;
    int index;

    g_npc_interaction_state->dialogue_layout = W8_DIALOGUE_LAYOUT_TRADE;
    if (g_npc_interaction_state->dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(5, true);
    SetNpcDialogueInputEnabled(2, true);
    SetNpcDialogueInputEnabled(1, true);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
        g_localized_strings[0x739], g_wiz_text_bold_font);
    ConfigureNpcTradeCommands();
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_31, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_31].primary = ConfirmNpcTradeSlot;
    buffer = fmt::format("{:d}g", g_status.party_gold);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_33].text.SetText(
        buffer, g_wiz_text_font_secondary);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_37, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_37].primary = RestockNpcTradeStock;
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_5, false);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_6, false);
    g_npc_interaction_state->trade_filter = 0;
    for (index = 0; index < 6; ++index) {
        SetNpcActionEnabled(W8_NPC_CONTROL_OPTION_BUTTONS + index, false);
    }
    if (g_npc_interaction_state->dialogue_npc->record->owns_stock == 0) {
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, false);
    }
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    SelectTextBox(2);
    if (!gXStatus.fCampMode) {
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
static void ToggleNpcTradeCategory(int button, W8NpcTradeFilter filter)
{
    const auto& control =
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + button];
    if (control.interaction.selected) {
        int previous = g_npc_interaction_state->trade_filter & W8_NPC_TRADE_CATEGORY_MASK;
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
                g_npc_interaction_state->trade_filter &= ~previous;
            }
        }
        g_npc_interaction_state->trade_filter |= filter;
    } else {
        g_npc_interaction_state->trade_filter &= ~filter;
    }
    RebuildNpcTradeItemList(true);
}

/* The "usable by the selected character" toggle is exclusive with the
   "usable by anyone" toggle: option control 2 lowers bit 0x40 and raises
   bit 1 while pressed, and drops bit 1 when released. */
static void ToggleNpcCharacterUsabilityFilter(void)
{
    if (g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 2]
            .interaction.selected) {
        SetNpcActionPressed(W8_NPC_CONTROL_OPTION_BUTTONS + 5, false);
        g_npc_interaction_state->trade_filter &= ~W8_NPC_TRADE_USABLE_BY_PARTY;
        g_npc_interaction_state->trade_filter |= W8_NPC_TRADE_USABLE_BY_CHARACTER;
        RebuildNpcTradeItemList(true);
        return;
    }
    g_npc_interaction_state->trade_filter &= ~W8_NPC_TRADE_USABLE_BY_CHARACTER;
    RebuildNpcTradeItemList(true);
}

static void ToggleNpcPartyUsabilityFilter(void)
{
    if (g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_OPTION_BUTTONS + 5]
            .interaction.selected) {
        SetNpcActionPressed(W8_NPC_CONTROL_OPTION_BUTTONS + 2, false);
        g_npc_interaction_state->trade_filter &= ~W8_NPC_TRADE_USABLE_BY_CHARACTER;
        g_npc_interaction_state->trade_filter |= W8_NPC_TRADE_USABLE_BY_PARTY;
        RebuildNpcTradeItemList(true);
        return;
    }
    g_npc_interaction_state->trade_filter &= ~W8_NPC_TRADE_USABLE_BY_PARTY;
    RebuildNpcTradeItemList(true);
}

static void SelectNpcSellMode(void)
{
    g_npc_interaction_state->trade_mode = W8_NPC_TRADE_SELL;
    UpdateNpcDialogueSubMode();
}

static void SelectNpcGiveMode(void)
{
    g_npc_interaction_state->trade_mode = W8_NPC_TRADE_GIVE;
    UpdateNpcDialogueSubMode();
}

static void SelectNpcShopliftMode(void)
{
    g_npc_interaction_state->trade_mode = W8_NPC_TRADE_SHOPLIFT;
    UpdateNpcDialogueSubMode();
}

/* Open the mode-1 service layout: the caption and three service options, the
   mode-4-only controls parked, and each option enabled from what the selected
   character can actually cast or use. */
static void OpenNpcDialogueMode1Layout(void)
{
    g_npc_interaction_state->dialogue_layout = W8_DIALOGUE_LAYOUT_SERVICES;
    if (g_npc_interaction_state->dialogue_hidden != 0) {
        SetNpcDialogueHidden(0);
    }
    SetNpcPanelActive(0, true);
    SetNpcPanelActive(1, true);
    SetNpcPanelActive(4, true);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_38, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_38].text.SetText("",
                                                                              g_wiz_text_bold_font);
    SetNpcDialogueInputEnabled(2, true);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_NPC_NAME].text.SetText(
        g_localized_strings[0x72f], g_wiz_text_bold_font);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_1, 0x730, RequestNpcSpellService3);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_2, 0x732, RequestNpcSpellService41);
    SetNpcDialogueAction(W8_NPC_CONTROL_TEXT_4, 0x733, RequestNpcCharacterService);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_4].policy.primary_toggle =
        ButtonToggle::Release;
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_4, 0x7ca);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_3, false);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_5, false);
    SetNpcActionVisible(W8_NPC_CONTROL_TEXT_6, false);
    if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_SERVICES) {
        RefreshNpcServiceAvailability(g_status.selected_character);
    }
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    SelectTextBox(2);
    if (!gXStatus.fCampMode) {
        ResetEditorStatusLine(2);
    }
}

/* Tear down the mode-1 dialogue layout; outside camp the mode is retired as
   well. */
static void CloseNpcDialogueMode1Layout(void)
{
    SetNpcDialogueInputEnabled(2, false);
    SetNpcDialogueInputEnabled(1, false);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_4].policy.primary_toggle =
        ButtonToggle::None;
    SetNpcPanelActive(0, false);
    SetNpcPanelActive(1, false);
    SetNpcPanelActive(4, false);
    SetNpcActionHelp(W8_NPC_CONTROL_TEXT_4, -1);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
    SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, true);
    if (!gXStatus.fCampMode) {
        SetNpcDialogueLayoutMode(W8_DIALOGUE_LAYOUT_NONE);
    }
}

static void RequestNpcSpellService3(void)
{
    int location = g_npc_interaction_state->target_location_id;
    W8NpcDialogueLayout mode = g_npc_interaction_state->dialogue_layout;

    gXStatus.fCampMode = true;
    CloseNpcDialogueMode1Layout();
    EndNpcDialogueSession(false);
    BeginSpellCast(W8_SPELL_CHARM, location, mode);
}

static void RequestNpcSpellService41(void)
{
    int location = g_npc_interaction_state->target_location_id;
    W8NpcDialogueLayout mode = g_npc_interaction_state->dialogue_layout;

    gXStatus.fCampMode = true;
    CloseNpcDialogueMode1Layout();
    EndNpcDialogueSession(false);
    BeginSpellCast(W8_SPELL_MINDREAD, location, mode);
}

static void RequestNpcCharacterService(void)
{
    gXStatus.fCampMode = true;
    CloseNpcDialogueMode1Layout();
    EndNpcDialogueSession(false);
    OpenUseItemSelectView(g_status.selected_character);
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

    character = &g_status.buffers.Char[g_status.selected_character];
    hit = 0;
    if (g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_4].interaction.selected == 0) {
        return 0;
    }
    for (slot = 0; slot < 12; ++slot) {
        if (character->EquippedItem[slot].iItemNo != -1 &&
            CanCharacterActivateItem(character, &character->EquippedItem[slot])) {
            if (index == hit) {
                g_npc_interaction_state->ui->item_preview =
                    g_item_video_objects.SharedSprites(character->EquippedItem[slot].iItemNo);
                if (character->EquippedItem[slot].stack_count < 2) {
                    text = "";
                } else {
                    count_text = fmt::format("{:d}", character->EquippedItem[slot].stack_count);
                    text = count_text;
                }
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
                    text, g_wiz_text_font_secondary);
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
                return &character->EquippedItem[slot];
            }
            ++hit;
        }
    }
    for (slot = 0; slot < 8; ++slot) {
        if (character->backpack[slot].iItemNo != -1 &&
            CanCharacterActivateItem(character, &character->backpack[slot])) {
            if (index == hit) {
                g_npc_interaction_state->ui->item_preview =
                    g_item_video_objects.SharedSprites(character->backpack[slot].iItemNo);
                if (character->backpack[slot].stack_count < 2) {
                    text = "";
                } else {
                    count_text = fmt::format("{:d}", character->backpack[slot].stack_count);
                    text = count_text;
                }
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
                    text, g_wiz_text_font_secondary);
                g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
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

    if (g_npc_dialogue.reply() == W8NpcReplyState::none) {
        return;
    }
    g_npc_dialogue.Answer(text);
    if (echo) {
        notice = fmt::format("{}...", text);
        ShowNotice(W8_FONT_PALETTE_BRONZE, notice, 3, GetTextBoxScrollRange());
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
    if (field_text.size() == 0 && g_npc_dialogue.reply() == W8NpcReplyState::none) {
        return;
    }
    if (g_npc_dialogue.reply() != W8NpcReplyState::none) {
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
            std::string_view(g_localized_strings[0x76b])
                .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x76b], 4))) == 0 ||
        CompareTextIgnoreAsciiCase(
            std::string_view(field_text).substr(0, wiz8::text::codepoint_prefix(field_text, 7)),
            std::string_view(g_localized_strings[0x76c])
                .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x76c], 7))) == 0) {
        RequestNpcJoinParty();
        return;
    }
    if (g_npc_interaction_state->where_is_query) {
        if (CompareTextIgnoreAsciiCase(
                std::string_view(field_text).substr(0, wiz8::text::codepoint_prefix(field_text, 9)),
                std::string_view(g_localized_strings[0x76d])
                    .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x76d], 9))) == 0 ||
            CompareTextIgnoreAsciiCase(
                std::string_view(field_text)
                    .substr(0, wiz8::text::codepoint_prefix(field_text, 0xa)),
                std::string_view(g_localized_strings[0x76e])
                    .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x76e], 0xa))) ==
                0 ||
            CompareTextIgnoreAsciiCase(
                std::string_view(field_text).substr(0, wiz8::text::codepoint_prefix(field_text, 8)),
                std::string_view(g_localized_strings[0x76f])
                    .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x76f], 8))) == 0) {
            buf = field_text;
        } else {
            buf = fmt::sprintf(g_localized_strings[0x76a], field_text);
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
                std::string_view(g_localized_strings[0x770])
                    .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x770], 0xb))) ==
            0) {
            field_text = buf.substr(wiz8::text::codepoint_prefix(buf, 0xb));
            show_fallback = false;
        } else if (CompareTextIgnoreAsciiCase(
                       std::string_view(buf).substr(0, wiz8::text::codepoint_prefix(buf, 9)),
                       std::string_view(g_localized_strings[0x76d])
                           .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x76d],
                                                                   9))) == 0 ||
                   CompareTextIgnoreAsciiCase(
                       std::string_view(buf).substr(0, wiz8::text::codepoint_prefix(buf, 0xa)),
                       std::string_view(g_localized_strings[0x76e])
                           .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x76e],
                                                                   0xa))) == 0 ||
                   CompareTextIgnoreAsciiCase(
                       std::string_view(buf).substr(0, wiz8::text::codepoint_prefix(buf, 8)),
                       std::string_view(g_localized_strings[0x76f])
                           .substr(0, wiz8::text::codepoint_prefix(g_localized_strings[0x76f],
                                                                   8))) == 0) {
            field_text = buf.substr(wiz8::text::codepoint_prefix(buf, 9));
            plain_text = false;
            show_fallback = false;
            quote_id = FindNpcNameOrPlaceQuote(g_npc_interaction_state->dialogue_npc, field_text);
            if (quote_id == -1) {
                quote_id = 0x76;
            }
        } else if (g_npc_interaction_state->where_is_query) {
            plain_text = false;
            quote_id = FindNpcNameOrPlaceQuote(g_npc_interaction_state->dialogue_npc, field_text);
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
        word.clear();
        while (!cursor.empty()) {
            if (word.size() == 0) {
                has_word = ReadNextWord(cursor, word);
            } else {
                word = word2;
            }
            if (!has_word) {
                break;
            }
            has_word = ReadNextWord(cursor, word2);
            if (!has_word) {
                break;
            }
            buf = fmt::format("{} {}", word, word2);
            quote = FindNpcScriptQuoteByKeyword(TranslateDialogueKeyword(buf), 0, 0);
            if (quote != -1) {
                quote_id = quote;
                break;
            }
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
        const auto roll = Random(5);
        const auto* replies = plain_text ? g_dialogue_fallback_ids0 : g_dialogue_fallback_ids1;
        const char punctuation = roll >= (plain_text ? 2u : 3u) ? '?' : '.';
        notice =
            fmt::format("{} {}{}", g_localized_strings[replies[roll]], field_text, punctuation);
        ShowNotice(W8_FONT_PALETTE_BRONZE, notice, 3, GetTextBoxScrollRange());
    } else {
        ShowNotice(W8_FONT_PALETTE_BRONZE, field_text, 3, GetTextBoxScrollRange());
    }
    if (quote_id == -1) {
        g_npc_dialogue.QueueQuote(Random(2) + 0x23, false, false, false);
        return;
    }
    if (quote_id == 0x59 || second_quote_id == 0x59) {
        g_npc_dialogue.QueueQuote(0x59, true, false, false);
        return;
    }
    g_npc_dialogue.QueueQuote(quote_id, true, false, false);
    if (second_quote_id == -1) {
        return;
    }
    g_npc_dialogue.QueueQuote(0x21, false, false, false);
    g_npc_dialogue.QueueQuote(second_quote_id, true, false, false);
}

void HandleNpcDialogueKeyEvent(const InputEvent* event)
{
    W8NpcInteractionState* state;
    W8MGSCommand command;

    if (ShouldDeferCharacterEventForNpcScript(false)) {
        return;
    }
    state = g_npc_interaction_state;
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
            TryFinishNpcVoicePlayback(true);
            return;
        }
        if (NpcEditingText()) {
            SetNpcFieldText("");
            if (auto* fields = TextFieldGroup::Focused()) fields->ClearFocus();
            return;
        }
        if (g_npc_dialogue.reply() != W8NpcReplyState::none ||
            g_npc_dialogue.has_queued_actions()) {
            return;
        }
        switch (g_npc_interaction_state->dialogue_layout) {
        case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
            g_npc_interaction_state->suppress_parting_reaction = false;
            g_npc_interaction_state->farewell_queued = true;
            g_npc_dialogue.QueueQuote(0x5c, false, false, false);
            return;
        case W8_DIALOGUE_LAYOUT_TOPIC_MENU:
            EndNpcDialogueSession(false);
            return;
        case W8_DIALOGUE_LAYOUT_SERVICES: {
            W8NpcDialogueLayout prev = g_npc_interaction_state->previous_dialogue_layout;
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
            bool cursor = g_npc_interaction_state->reopen_topics;
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
        command = g_mgs_keyboard->FindCommandForEvent(event);
        if (command == W8_MGS_COMMAND_JOURNAL) {
            DispatchMGSCommand(command);
            return;
        }
        if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
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
        if (CompareTextIgnoreAsciiCase(
                name, g_localized_strings[g_dialogue_place_keyword_ids[index]]) == 0) {
            return true;
        }
    }
    return false;
}

void ClearNpcDialogueTranscript(void)
{
    g_npc_interaction_state->dialogue_transcript.clear();
}

unsigned char LoadNpcDialogueTranscript(wiz8::File& file)
try {
    unsigned char version;
    int record_count;
    file.read_exact(&version, sizeof(version));
    file.read_exact(&record_count, sizeof(record_count));
    if (version < 1 || version > 2 || record_count < 0 ||
        record_count > (file.size() - file.tell() - (version > 1 ? 2 : 0)) / 7) {
        return 0;
    }
    using RecordOwner = std::unique_ptr<W8DialogueTranscriptRecord>;
    std::vector<RecordOwner> pending;
    pending.reserve(record_count);
    for (int index = 0; index < record_count; ++index) {
        int text_length;
        file.read_exact(&text_length, sizeof(text_length));
        if (text_length < 0 || text_length >= 100) return 0;
        auto record = std::make_unique<W8DialogueTranscriptRecord>();
        std::vector<std::byte> bytes((std::size_t(text_length) + 1) * 2);
        file.read_exact(bytes.data(), bytes.size());
        record->text = wiz8::text::from_utf16le(bytes);
        if (bytes[bytes.size() - 1] != std::byte{} || bytes[bytes.size() - 2] != std::byte{} ||
            wiz8::text::to_utf16(record->text).size() != std::size_t(text_length))
            return false;
        file.read_exact(&record->category, sizeof(record->category));
        pending.push_back(std::move(record));
    }
    signed char category_filter = g_npc_interaction_state->dialogue_category_filter;
    unsigned char sorted = g_npc_interaction_state->transcript_sorted;
    if (version > 1) {
        file.read_exact(&category_filter, sizeof(category_filter));
        file.read_exact(&sorted, sizeof(sorted));
        if (category_filter < W8_DIALOGUE_CATEGORY_ALL ||
            category_filter > W8_DIALOGUE_CATEGORY_MISC || sorted > 1)
            return 0;
    }
    auto& transcript = g_npc_interaction_state->dialogue_transcript;
    transcript.reserve(record_count);
    ClearNpcDialogueTranscript();
    for (auto& record : pending) {
        transcript.push_back(std::move(record));
    }
    g_npc_interaction_state->dialogue_category_filter = category_filter;
    g_npc_interaction_state->transcript_sorted = sorted != 0;
    return 1;
} catch (const std::exception&) {
    return false;
}

unsigned char SaveNpcDialogueTranscript(wiz8::File& file)
try {
    const unsigned char version = 2;
    const int record_count = static_cast<int>(g_npc_interaction_state->dialogue_transcript.size());
    file.write(&version, sizeof(version));
    file.write(&record_count, sizeof(record_count));
    for (int index = 0; index < record_count; ++index) {
        W8DialogueTranscriptRecord* record =
            g_npc_interaction_state->dialogue_transcript[index].get();
        const int text_length = wiz8::text::to_utf16(record->text).size();
        if (text_length >= 100) return false;
        std::vector<std::byte> bytes((std::size_t(text_length) + 1) * 2);
        wiz8::text::to_utf16le(record->text, bytes);
        file.write(&text_length, sizeof(text_length));
        file.write(bytes.data(), bytes.size());
        file.write(&record->category, sizeof(record->category));
    }
    file.write(&g_npc_interaction_state->dialogue_category_filter, 1);
    const unsigned char sorted = g_npc_interaction_state->transcript_sorted;
    file.write(&sorted, sizeof(sorted));
    return 1;
} catch (const std::exception&) {
    return false;
}

/* Restate the five transcript category buttons so only the active
   dialogue_category_filter's button shows its secondary state. */
void SyncDialogueCategoryButtons(void)
{
    int category = g_npc_interaction_state->dialogue_category_filter;
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
    if (GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc) == W8_NPC_BAND_FRIENDLY) {
        OpenNpcDialogueTranscriptLayout();
    } else {
        ShowNpcDialogueTopicMenu();
    }
}

static void ConfirmNpcTradePurchase(void)
{
    if (GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc) == W8_NPC_BAND_FRIENDLY) {
        SpendPartyGold(g_npc_interaction_state->trade_gold);
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
        g_npc_dialogue.QueueQuote(0x10, false, false, false);
        RebuildNpcTradeItemList(false);
        return;
    }
    if (NpcRecordHasTradePool(g_npc_interaction_state->dialogue_npc)) {
        ApplyNpcInteraction(g_npc_interaction_state->dialogue_npc, 2,
                            g_npc_interaction_state->dialogue_speaker, 0,
                            g_npc_interaction_state->trade_gold);
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].dirty = true;
        g_npc_dialogue.QueueQuote(GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc) ==
                                          W8_NPC_BAND_FRIENDLY
                                      ? 0x10
                                      : 7,
                                  false, false, false);
        RebuildNpcTradeItemList(false);
        return;
    }
    g_npc_dialogue.QueueQuote(7, false, false, false);
}

static unsigned char HandleNpcDialogueItem(W8ItemInstance* item)
{
    W8MessageDialogBase* dialog;
    std::string message;
    bool result;

    result = true;
    if (g_npc_interaction_state->trade_gold != 0 && item == 0) {
        if (g_npc_interaction_state->trade_gold != static_cast<int>(g_status.party_gold)) {
            ConfirmNpcTradePurchase();
            return 1;
        }
        dialog = CreateMessageDialog();
        dialog->SetClientExtent(0xfa, 200);
        message = g_localized_strings[0x7d6];
        dialog->SetMessage(message, 1, 0x32, true, true, true, true, 0, 0x15e);
        dialog->on_complete = OnNpcTradeDialogClosed;
        OpenModal(std::unique_ptr<ModalContent>(dialog));
        return 1;
    }
    if (GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc) == W8_NPC_BAND_FRIENDLY ||
        g_npc_interaction_state->dialogue_npc->record->voice_script != 0) {
        if (item != 0) {
            const auto match = FindNpcScriptItemQuote(item->iItemNo);
            if (!match) {
                if (g_npc_interaction_state->dialogue_npc->record->voice_script != 0) {
                    return 1;
                }
                if ((g_item_records[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
                    g_npc_dialogue.QueueQuote(0x11, false, false, false);
                    return 1;
                }
                if (AcceptNpcDialogueItem(g_npc_interaction_state->dialogue_npc, item, 1) != 0) {
                    g_npc_dialogue.QueueQuote(0x10, false, false, false);
                    RemoveNpcScriptItem(item, 0, -1);
                    return result;
                }
                g_npc_dialogue.QueueQuote(0x11, false, false, false);
            } else {
                g_npc_dialogue.QueueQuote(match->quote_index, false, false, false);
                result = false;
                if (match->consumes_item) {
                    RemoveNpcScriptItem(item, 0, -1);
                    return result;
                }
                if (g_npc_interaction_state->held_item_pending &&
                    g_npc_interaction_state->pending_item.iItemNo == item->iItemNo) {
                    g_npc_interaction_state->held_item_pending = false;
                    AddItemToParty(&g_npc_interaction_state->pending_item, true, false);
                    ClearHeldItemDisplay();
                    return 0;
                }
            }
        }
    } else if (item != 0) {
        if ((g_item_records[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0) {
            if (!WillNpcTradeForItem(g_npc_interaction_state->dialogue_npc, item)) {
                g_npc_dialogue.QueueQuote(7, false, false, false);
                return 1;
            }
            const auto match = FindNpcScriptItemQuote(item->iItemNo);
            if (!match) {
                ApplyNpcInteraction(g_npc_interaction_state->dialogue_npc, 3,
                                    g_npc_interaction_state->dialogue_speaker, item, 0);
                g_npc_dialogue.QueueQuote(
                    GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc) ==
                            W8_NPC_BAND_FRIENDLY
                        ? 0x10
                        : 7,
                    false, false, false);
            } else {
                g_npc_dialogue.QueueQuote(match->quote_index, false, false, false);
                result = false;
            }
            RemoveNpcScriptItem(item, 0, -1);
            return result;
        }
        g_npc_dialogue.QueueQuote(0x11, false, false, false);
        return 1;
    }
    return result;
}

/* Retail ICF shares this constant-return callback with the retained body at
   0x005b1740; this typed source callback has no separate address marker. */
static unsigned char AcceptNpcDialogueItem(W8NpcState*, W8ItemInstance*, int)
{
    return 1;
}

/* While NPC script deferral holds character events during an open dialogue,
   pump Escape and left-click so layout dismissals still run. */
static void SubmitNpcWhereIsQuery(void)
{
    g_npc_interaction_state->where_is_query = true;
    g_npc_interaction_state->ui->panels[6].dirty = true;
    HandleNpcDialogueInput();
    g_npc_interaction_state->where_is_query = false;
}

static void SubmitNpcDialogueInput(void)
{
    g_npc_interaction_state->where_is_query = false;
    g_npc_interaction_state->ui->panels[6].dirty = true;
    HandleNpcDialogueInput();
}

void HandleNpcDialogueItemChoice(void)
{
    HandleNpcDialogueItem(g_npc_interaction_state->trade_item);
    if (g_npc_interaction_state->reopen_topics &&
        GetNpcDispositionBand(g_npc_interaction_state->dialogue_npc) == W8_NPC_BAND_FRIENDLY) {
        CloseNpcDialogueOptionLayout();
        OpenNpcDialogueTranscriptLayout();
        HandleNpcDialogueDeparture(0);
    }
}

void RefreshNpcTradePartyGold(void)
{
    std::string text;

    text = fmt::format("{:d}g", g_status.party_gold);
    g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_33].text.SetText(
        text, g_wiz_text_font_secondary);
}

void RefreshNpcTradePrice(void)
{
    std::string text;

    if (g_npc_interaction_state->trade_item != 0) {
        text = fmt::format("{:d}", g_npc_interaction_state->trade_quantity);
        g_npc_interaction_state->ui->actions[W8_NPC_CONTROL_TEXT_24].text.SetText(
            text, g_wiz_text_font_secondary);
    }
}

void DrainNpcDialogueDeferralInput(void)
{
    SDL_Point mouse;
    InputEvent input;
    W8NpcDialogueLayout prior_layout;
    bool reopen_topics;

    if (!ShouldDeferCharacterEventForNpcScript(true) || !gXStatus.fNpcDialogueMode) {
        return;
    }
    SGPMouseGetPos(&mouse);
    UpdateUiPointer(mouse.x, mouse.y);
    while (auto next_input = PollInputEvent()) {
        input = *next_input;
        if (input.type == InputEventType::KeyDown) {
            if (input.key == SDLK_ESCAPE) {
                if (g_npc_interaction_state->dialogue_hidden == 0) {
                    if (!IsNpcScriptSessionActive()) {
                        switch (g_npc_interaction_state->dialogue_layout) {
                        case W8_DIALOGUE_LAYOUT_TOPIC_MENU:
                        case W8_DIALOGUE_LAYOUT_TRANSCRIPT:
                            EndNpcDialogueSession(false);
                            break;
                        case W8_DIALOGUE_LAYOUT_SERVICES:
                            prior_layout = g_npc_interaction_state->previous_dialogue_layout;
                            CloseNpcDialogueMode1Layout();
                            if (prior_layout == 2) {
                                ShowNpcDialogueTopicMenu();
                            } else if (prior_layout == 3) {
                                OpenNpcDialogueTranscriptLayout();
                            }
                            break;
                        case W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX:
                            reopen_topics = g_npc_interaction_state->reopen_topics;
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
                        TryFinishNpcVoicePlayback(false);
                    }
                } else {
                    SetNpcDialogueHidden(0);
                }
            }
        } else if (input.type == InputEventType::LeftButtonDown) {
            TryFinishNpcVoicePlayback(false);
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
    W8NpcDialog* dialog;

    if (gXStatus.fNpcDialogueMode && !g_npc_interaction_state->scripted_dialogue) {
        SetNpcDialoguePanelVisible(0);
    }
    dialog = new W8NpcDialog(request, aux_data, OnNpcDialogClosed);
    OpenModal(std::unique_ptr<ModalContent>(dialog));
    std::optional<W8NpcPriceCheck> price_check;
    if (request->kind == W8_NPC_ENTRY_PRICE_CHECK ||
        request->kind == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK) {
        int price = request->operand0;
        monster_info = GetNpcMonsterInfo(g_npc_interaction_state->dialogue_npc);
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
        price_check = W8NpcPriceCheck{g_npc_interaction_state->dialogue_npc, aux_data, price,
                                      request->kind != W8_NPC_ENTRY_ALWAYS_PRICE_CHECK};
    }
    g_npc_dialogue.ReplyDialogOpened(price_check);
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
    if (visible == g_npc_interaction_state->quote_visible) {
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
        auto& bubble = g_npc_interaction_state->quote_bubble;
        bubble = QuoteBubble::layout(
            normalized, 280,
            font_palette == 0xffffffff ? std::nullopt : std::optional<std::uint8_t>(font_palette));
        if (!bubble) {
            if (g_npc_interaction_state->dialogue_npc != 0) {
                error_text = fmt::format(
                    "Error creating box - most likely text too large: NPC {}, quote {:d}",
                    g_npc_interaction_state->dialogue_npc->record->source_name, quote_id);
            } else {
                error_text = "Error creating box - most likely text too large";
            }
            bubble = QuoteBubble::layout(error_text, 300, std::nullopt);
            ShowNotice(W8_FONT_PALETTE_BEIGE, error_text, 0, GetTextBoxScrollRange());
        }
        g_npc_interaction_state->quote_width = static_cast<short>(bubble ? bubble->width() : 0);
        g_npc_interaction_state->quote_height = static_cast<short>(bubble ? bubble->height() : 0);
        g_npc_interaction_state->quote_x = 320 - g_npc_interaction_state->quote_width / 2;
        g_npc_interaction_state->quote_y =
            quote_id < 0 ? 350 - g_npc_interaction_state->quote_height : 20;
        ShowNpcQuoteBubbleRegion(
            g_npc_interaction_state->quote_x, g_npc_interaction_state->quote_y,
            g_npc_interaction_state->quote_x + g_npc_interaction_state->quote_width,
            g_npc_interaction_state->quote_y + g_npc_interaction_state->quote_height);
        g_npc_interaction_state->quote_visible = true;
        if (std::holds_alternative<std::monostate>(notice)) {
            W8PendingNoticeLine* line = new W8PendingNoticeLine;
            line->npc_kind = npc_kind;
            line->text = normalized;
            g_npc_interaction_state->pending_notice_lines.push_back(line);
        }
        bool level_up = std::holds_alternative<W8LevelUpNotice>(notice);
        g_npc_interaction_state->quote_notice = notice;
        if (level_up) {
            wiz8::sound::play(
                reinterpret_cast<char*>(const_cast<char*>( // reinterpret-ok: SGP text ABI
                    "Data\\Sound\\Misc\\GainLevel.wav")));
        }
        return;
    }

    bool flush_notices =
        (quote_id == 0x12 || quote_id < 0) &&
        std::holds_alternative<std::monostate>(g_npc_interaction_state->quote_notice);
    W8QuoteNotice dismissed = std::exchange(g_npc_interaction_state->quote_notice, {});
    if (const auto* experience = std::get_if<W8ExperienceNoticePayload>(&dismissed)) {
        ShowNotice(W8_FONT_PALETTE_BEIGE,
                   fmt::sprintf(g_localized_strings[experience->alternate_message ? 0x231 : 0x232],
                                experience->amount),
                   0);
    } else if (const auto* skills = std::get_if<W8SkillNoticePayload>(&dismissed)) {
        PostSkillIncreaseNotices(skills);
    } else if (const auto* level_up = std::get_if<W8LevelUpNotice>(&dismissed)) {
        PostCharacterNotice(level_up->party_slot, g_localized_strings[0x773]);
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
    g_npc_interaction_state->quote_visible = false;
    const bool had_bubble = g_npc_interaction_state->quote_bubble.has_value();
    g_npc_interaction_state->quote_bubble.reset();
    HideNpcQuoteBubbleRegion();
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
        if (had_bubble) {
            ClearSurfaceRect(
                g_npc_interaction_state->quote_x, g_npc_interaction_state->quote_y,
                g_npc_interaction_state->quote_x + g_npc_interaction_state->quote_width,
                g_npc_interaction_state->quote_y + g_npc_interaction_state->quote_height);
            InvalidateRegion(
                g_npc_interaction_state->quote_x, g_npc_interaction_state->quote_y,
                g_npc_interaction_state->quote_x + g_npc_interaction_state->quote_width,
                g_npc_interaction_state->quote_y + g_npc_interaction_state->quote_height);
        }
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAITS);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_PANEL);
    } else if (g_current_screen_state.id == W8_SCREEN_CAMP) {
        g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_ALL;
    }
}

void DrawNpcQuoteBubble(void)
{
    IsModalOpen();
    if (g_npc_interaction_state->quote_visible && g_npc_interaction_state->quote_bubble) {
        g_npc_interaction_state->quote_bubble->draw(GetFrameBufferSurface(),
                                                    g_npc_interaction_state->quote_x,
                                                    g_npc_interaction_state->quote_y);
    }
}

void FlushPendingNoticeLines(void)
{
    std::string npc_name;
    int index;

    for (index = 0; index < static_cast<int>(g_npc_interaction_state->pending_notice_lines.size());
         ++index) {
        W8PendingNoticeLine* line = g_npc_interaction_state->pending_notice_lines[index];
        if (line->npc_kind != -1 &&
            line->npc_kind != g_npc_interaction_state->last_notice_npc_kind) {
            W8NpcState* npc = GetNpcState(line->npc_kind);
            if (npc != 0) {
                npc_name = npc->record->source_name;
                ShowNotice(W8_FONT_PALETTE_GREEN, npc_name, 3);
                g_npc_interaction_state->last_notice_npc_kind = line->npc_kind;
            }
        }
        ShowNotice(line->npc_kind == -1 ? 0xb : 0xf, line->text, 3, GetTextBoxScrollRange());
    }
    wiz8::remove_all_and_delete_in_order(g_npc_interaction_state->pending_notice_lines);
}

void LookAtDialogueNpc(void)
{
    W8MonsterInfo* info = GetNpcMonsterInfo(g_npc_interaction_state->dialogue_npc);
    if (info != 0) {
        srVector3T<float> position = info->p3D->movement.position;
        position.y += info->p3D->movement.height_offset;
        g_gd_camera->LookAt(&position, false);
    }
}

/* Show (0) or hide (nonzero) the NPC dialogue UI: on show the party portrait
   region sets lose input, the dialogue regions and text controls come up, and
   the transcript expands; on hide the party sets come back, the transcript
   collapses, its background is cleared and the rectangle invalidated. The new
   state lands in dialogue_hidden. */
static void SetNpcDialogueHidden(char value)
{
    int index;

    if (value != 0) {
        g_npc_interaction_state->saved_mode = g_settings.main_ui_mode;
        ApplyMainGameModeFlag(W8_MAIN_UI_MODE_PORTRAITS, false);
        SetNpcPanelActive(2, false);
        g_npc_interaction_state->ui->transcript->Collapse();
        SetNpcPanelActive(3, false);
        for (index = 0; index < 8; ++index) {
            if (g_status.buffers.XChar[index].fOccupied) {
                EnableMainGamePortraitSlot(index);
            }
        }
        SetNpcDialogueInputEnabled(0, false);
        g_npc_interaction_state->ui->transcript->ClearBackground();
        EraseRegion(0x1dc, 0x11b, 0x269, 0x1c2);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_1);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_3);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_5);
        RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
        if (auto* fields = TextFieldGroup::Focused()) fields->SetBlocksMouse(0, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, false);
        DisableNpcTradeActions();
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, false);
    } else {
        ApplyMainGameModeFlag(g_npc_interaction_state->saved_mode, false);
        for (index = 0; index < 8; ++index) {
            if (g_status.buffers.XChar[index].fOccupied) {
                DisableMainGamePortraitSlot(index);
            }
        }
        SetNpcDialogueInputEnabled(0, true);
        SetNpcDialogueInputEnabled(2, true);
        EnableDialogueTextBoxRegions();
        SetTextBoxScrollInput(true);
        g_level_block->action_panel_visible = 1;
        SetNpcPanelActive(2, true);
        SetNpcPanelActive(3, true);
        g_npc_interaction_state->ui->transcript->Expand();
        SyncNpcDialogueTranscriptScrollButtons();
        if (auto* fields = TextFieldGroup::Focused()) fields->SetBlocksMouse(0, false);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_1, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_2, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_3, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_4, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_5, true);
        SetNpcActionEnabled(W8_NPC_CONTROL_TEXT_6, true);
    }
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    g_npc_interaction_state->dialogue_hidden = value;
}

/* Destroy callback OpenNpcDialog installs on the modal: pops or re-schemes the
   text-input level the dialog pushed, restores the quote bubble, and routes the
   choice back to the script - the picked option's label, the price-check
   yes/no string, or the typed keyword text. Inside a live dialogue the text is
   injected into input field 0 and processed as if typed; otherwise it is
   submitted to the script line queue directly. */
static void ResolveNpcPickpocket(int party_slot)
{
    W8Character* character = &g_status.buffers.Char[party_slot];
    unsigned int gold;
    W8ItemInstance item;
    std::string text;
    int slot;

    switch (AttemptNpcPickpocket(character, g_npc_interaction_state->dialogue_npc, &item, &gold)) {
    case W8_PICKPOCKET_ITEM_TAKEN:
        text = fmt::sprintf(g_localized_strings[0x74d], character->name, GetItemDisplayName(&item));
        for (slot = 0; slot < 8; ++slot) {
            if (character->backpack[slot].iItemNo == -1) {
                AddItemToCharacter(character, &item, false, false, false);
                DisplayNpcQuote(text, true);
                return;
            }
        }
        AddItemToParty(&item, false, false);
        DisplayNpcQuote(text, true);
        return;
    case W8_PICKPOCKET_GOLD_TAKEN:
        text = fmt::sprintf(g_localized_strings[0x74e], character->name, gold);
        AddPartyGold(gold, false);
        DisplayNpcQuote(text, true);
        return;
    case W8_PICKPOCKET_FAILED:
        text = fmt::sprintf(g_localized_strings[0x74f], character->name);
        DisplayNpcQuote(text, false);
        return;
    case W8_PICKPOCKET_CAUGHT:
        g_npc_dialogue.QueueQuote(0x17, false, false, false);
        SetNpcDispositionBand(g_npc_interaction_state->dialogue_npc, W8_NPC_BAND_NEUTRAL);
        ApplyFactionChange(3, 1, g_npc_interaction_state->dialogue_npc->record->faction, -5);
        CloseNpcDialogueTranscriptLayout();
        ShowNpcDialogueTopicMenu();
        return;
    case W8_PICKPOCKET_EMPTY:
        DisplayNpcQuote(g_localized_strings[0x750], false);
        return;
    default:
        return;
    }
}

void QueueDialogueNpcRefusal(void)
{
    unsigned int flags = g_npc_interaction_state->dialogue_npc->refusal_flags;
    if ((flags & 1) == 0) {
        g_npc_dialogue.QueueQuote(0x67, false, false, false);
        g_npc_interaction_state->dialogue_npc->refusal_flags |= 1;
        return;
    }
    if ((flags & 2) == 0) {
        g_npc_dialogue.QueueQuote(0x68, false, false, false);
        g_npc_interaction_state->dialogue_npc->refusal_flags |= 2;
        return;
    }
    g_npc_dialogue.QueueQuote(0x69, false, false, false);
}

void OnNpcDialogClosed(W8NpcDialog& dialog)
{
    W8NpcDialog* npc_dialog = &dialog;
    W8NpcQuoteEntry* request = npc_dialog->m_message;
    std::string field_text;
    std::string entry_text;
    int index;

    if (auto* fields = TextFieldGroup::Focused()) {
        fields->SetScheme(1);
    }
    RestoreCurrentNpcQuoteBubble();
    if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        CloseNpcDialogueOptionLayout();
        OpenNpcDialogueTranscriptLayout();
    }
    if (npc_dialog->m_cancelled) {
        g_npc_dialogue.ReplyDialogClosed();
        return;
    }
    if (request->kind == W8_NPC_ENTRY_OPTIONS) {
        for (index = 0; index < request->sub_entry_count; ++index) {
            if (index == npc_dialog->m_selected_option) {
                entry_text = request->sub_entries[index].text.c_str();
                if (!gXStatus.fNpcDialogueMode || g_npc_interaction_state->scripted_dialogue) {
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
            entry_text = g_localized_strings[0x7df];
        } else {
            entry_text = g_localized_strings[0x7e0];
        }
        if (!gXStatus.fNpcDialogueMode || g_npc_interaction_state->scripted_dialogue) {
            HandleNpcDialogueReply(entry_text, false);
        } else {
            field_text = NpcFieldText();
            StripNpcKeywordPunctuation(entry_text);
            static_cast<void>(field_text.size());
            SetNpcFieldText(entry_text);
            HandleNpcDialogueInput();
        }
    } else if (request->kind == W8_NPC_ENTRY_KEYWORD_INPUT) {
        if (!gXStatus.fNpcDialogueMode || g_npc_interaction_state->scripted_dialogue) {
            HandleNpcDialogueReply(npc_dialog->m_input_text, false);
        } else {
            field_text = NpcFieldText();
            StripNpcKeywordPunctuation(npc_dialog->m_input_text);
            static_cast<void>(field_text.size());
            SetNpcFieldText(npc_dialog->m_input_text);
            HandleNpcDialogueInput();
        }
    }
    g_npc_dialogue.ReplyDialogClosed();
}

/* The camp-side mirror of SwitchNpcDialogueLayout: camp mode is raised, the current
   dialogue layout is retired, and a still-pending item goes back onto the
   item cursor. */
void CloseNpcDialogueForCamp(void)
{
    gXStatus.fCampMode = true;
    SwitchNpcDialogueLayout(W8_DIALOGUE_LAYOUT_NONE);
    EndNpcDialogueSession(false);
    if (g_npc_interaction_state->held_item_pending) {
        g_status.item_in_hand = g_npc_interaction_state->pending_item;
        SetItemCursor(0);
        return;
    }
    SetTargetCursor(W8_CURSOR_NONE);
}

void SyncDialogueNpcStateAndMarkPending(void)
{
    SyncDialogueNpcState();
    g_npc_interaction_state->pending_trade_toggle = true;
}

void SyncDialogueNpcState(void)
{
    BeginNpcDialogueInternal(g_npc_interaction_state->dialogue_npc, 0, -1, 0, 1);
    g_npc_interaction_state->pending_layout = g_npc_interaction_state->previous_dialogue_layout;
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

    if (g_npc_interaction_state->dialogue_npc->name_style == 0x2a &&
        (info = GetNpcMonsterInfo(g_npc_interaction_state->dialogue_npc)) != 0) {
        info->p3D->SetScript("Guard.msf", true);
    }
    if ((value == 0 || !g_npc_interaction_state->dialogue_npc->dismissed_flag ||
         g_npc_interaction_state->dialogue_npc->record->allow_dismissed_departure_dialogue != 0) &&
        g_npc_interaction_state->transcript_open_count < 1) {
        if (!g_npc_interaction_state->dialogue_npc->greeting_pending) {
            g_npc_dialogue.QueueQuote(1, false, false, false);
        } else {
            g_npc_dialogue.QueueQuote(0, false, false, false);
            g_npc_interaction_state->dialogue_npc->greeting_pending = false;
            if (g_npc_interaction_state->dialogue_npc->record->monster_bound == 0 &&
                g_npc_interaction_state->dialogue_npc->record->voice_script == 0 &&
                g_npc_interaction_state->dialogue_npc->record->merchant == 0) {
                for (index = 0; index < 8; ++index) {
                    character = &g_status.buffers.Char[index];
                    if (g_status.buffers.XChar[index].fOccupied && character->hp_current != 0 &&
                        character->highest_condition < W8_CONDITION_ASLEEP) {
                        PracticeCharacterSkill(character, W8_SKILL_COMMUNICATION, 0xf, false);
                    }
                }
            }
            if (gXStatus.fNpcDialogueMode && !g_npc_interaction_state->scripted_dialogue) {
                AddNpcDialogueKeyword(g_npc_interaction_state->dialogue_npc->record->source_name,
                                      -1, 1);
                return;
            }
            if (g_npc_interaction_state->dialogue_npc->record->monster_bound == 0 &&
                (g_npc_interaction_state->dialogue_npc->record->voice_script == 0 ||
                 g_npc_interaction_state->dialogue_npc->is_present)) {
                AddDialogueTranscriptKeyword(
                    g_npc_interaction_state->dialogue_npc->record->source_name, -1);
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
    g_status.world_cursor_gate = 1;
    ResetLevelDataVectors();
    SetTargetCursor(W8_CURSOR_MAP_LOAD);
}

void ClearMainGameTargetState(void)
{
    g_status.world_cursor_gate = 0;
    ClearLevelMovementStopped();
    SetTargetCursor(W8_CURSOR_NONE);
}

/* When the world-cursor gate (world_cursor_gate) is raised, discard queued input
   after refreshing the region position so stale events do not fire. */
void FlushInputWhileWorldCursorGate(void)
{
    SDL_Point mouse;
    InputEvent input;

    if (g_status.world_cursor_gate == 0) {
        return;
    }
    SGPMouseGetPos(&mouse);
    UpdateUiPointer(mouse.x, mouse.y);
    while (auto next_input = PollInputEvent()) {
        input = *next_input;
    }
}

void AddDialogueTranscriptKeyword(std::string_view name, signed char category)
{
    std::string keyword;
    keyword = name;
    StripNpcKeywordPunctuation(keyword);
    if (category == -1) {
        unsigned int index;
        for (index = 0; index < gXStatus.uiItemsInDatabase; ++index) {
            if (CompareTextIgnoreAsciiCase(keyword, g_item_records[index].display_name) == 0) {
                category = W8_DIALOGUE_CATEGORY_ITEMS;
                break;
            }
        }
        if (category == -1) {
            for (index = 0; index < gXStatus.uiNpcsInDatabase; ++index) {
                if (CompareTextIgnoreAsciiCase(keyword, g_npc_records[index].source_name) == 0) {
                    category = W8_DIALOGUE_CATEGORY_PEOPLE;
                    break;
                }
            }
        }
        if (category == -1) {
            for (index = 0; g_dialogue_person_keywords[index][0] != 0; ++index) {
                if (CompareTextIgnoreAsciiCase(keyword, g_dialogue_person_keywords[index]) == 0) {
                    category = W8_DIALOGUE_CATEGORY_PEOPLE;
                    break;
                }
            }
        }
        if (category == -1) {
            category = IsDialoguePlaceKeyword(keyword) ? W8_DIALOGUE_CATEGORY_PLACES
                                                       : W8_DIALOGUE_CATEGORY_MISC;
        }
    }
    for (int index = 0;
         index < static_cast<int>(g_npc_interaction_state->dialogue_transcript.size()); ++index) {
        if (CompareTextIgnoreAsciiCase((g_npc_interaction_state->dialogue_transcript[index])->text,
                                       keyword) == 0) {
            return;
        }
    }
    auto record = std::make_unique<W8DialogueTranscriptRecord>();
    record->text = keyword;
    record->category = category;
    g_npc_interaction_state->dialogue_transcript.push_back(std::move(record));
}

void RecordLevelEntryDialogueState(void)
{
    std::string region_name;
    int region = GetLevelBand(g_status.current_level);
    region_name = GetNpcScriptRegionName(region);
    if (!region_name.empty()) {
        AddDialogueTranscriptKeyword(region_name, W8_DIALOGUE_CATEGORY_PLACES);
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
    return gXStatus.fNpcDialogueMode &&
           g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX;
}

bool CanOpenNpcDialogue(void)
{
    return gXStatus.fNpcDialogueMode && g_npc_interaction_state->scripted_dialogue;
}

unsigned char SetNpcDialoguePanelVisible(unsigned char value)
{
    W8NpcDialogueTextController* controller;

    if (gXStatus.fNpcDialogueMode && !g_npc_interaction_state->scripted_dialogue &&
        g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
        if (value == 0) {
            controller = g_npc_interaction_state->ui->transcript.get();
            controller->Collapse();
            controller = g_npc_interaction_state->ui->transcript.get();
            controller->m_active = false;
            controller->input.setEnabled(false);
            SetNpcPanelActive(3, false);
            controller = g_npc_interaction_state->ui->transcript.get();
            controller->ClearBackground();
            EraseRegion(0x1dc, 0x11b, 0x269, 0x1c2);
            RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_1);
            RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_3);
            RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_5);
            RequestRedraw(W8_MAIN_REDRAW_PORTRAIT_7);
            RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
            g_npc_interaction_state->dialogue_panel_hidden = true;
            return 1;
        }

        controller = g_npc_interaction_state->ui->transcript.get();
        controller->m_active = true;
        controller->input.setEnabled(true);
        SetNpcPanelActive(3, true);
        controller = g_npc_interaction_state->ui->transcript.get();
        controller->Expand();
        SyncNpcDialogueTranscriptScrollButtons();
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
        g_npc_interaction_state->dialogue_panel_hidden = false;
        return 1;
    }
    return 0;
}
bool FinishNpcVoiceIfSessionActive(void)
{
    if (!IsNpcScriptSessionActive()) {
        return false;
    }
    TryFinishNpcVoicePlayback(false);
    return true;
}

bool ProcessPendingEvent(void)
{
    if (IsNpcScriptSessionActive()) {
        TryFinishNpcVoicePlayback(true);
        return true;
    }
    if (gXStatus.character_event_queue->HasActiveEvents()) {
        gXStatus.character_event_queue->CompleteFirstActiveEvent();
        return true;
    }
    return false;
}

/* The keyword-list instantiations' char element type canonicalizes to
   unsigned short under the VC6 ABI spelling used by the PDB names. */
