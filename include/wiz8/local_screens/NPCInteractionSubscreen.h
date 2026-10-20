#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wiz8/canvas/Font.h"

void SyncNpcDialogueTranscriptScrollButtons();
void SetNpcDialogueInputEnabled(int group, bool enabled);
void ShowNpcQuoteBubbleRegion(int x1, int y1, int x2, int y2);
void HideNpcQuoteBubbleRegion();

#include <array>
#include <functional>

#include "wiz8/button_interaction.h"
#include "wiz8/game.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/message_box.h"
#include "wiz8/platform/input.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/ui_layer.h"


class W8DialogBase;
struct W8ControlsRect;
struct W8NpcQuoteEntry;
struct W8NpcScriptQuote;
struct W8NpcState;

class W8NpcDialogueTextController;
struct SpriteSheet;

// Fixed NPC dialogue composition: seven painted areas and their local actions.
// No visual parents, inherited hooks or arbitrary widget containers.
struct NpcDialogueUi {
    ~NpcDialogueUi();
    UiLayer transcript_input{UiLayerOrder::NpcTranscript};
    UiLayer trade_input{UiLayerOrder::NpcTrade};
    UiLayer options_input{UiLayerOrder::NpcOptions};
    struct Panel {
        SDL_Point origin;
        int background;
        bool active = false, dirty = true;
    };
    std::array<Panel, 7> panels{{{{0x17, 0x166}, 0},
                                 {{0xa4, 0x166}, 1},
                                 {{0x1dc, 0x11b}, 2},
                                 {{0x1dc, 0x12f}, 5},
                                 {{0x1dc, 0x166}, 7},
                                 {{0x1dc, 0x166}, 6},
                                 {{0x1dc, 0x166}, 13}}};
    struct Action {
        int panel = -1;
        SDL_Rect bounds{};
        std::shared_ptr<const SpriteSheet> sprites;
        int image_offset = 0;
        std::array<int, 5> frames{-1, -1, -1, -1, -1};
        HitTarget* target = nullptr;
        W8TextBuffer text;
        ButtonInteraction interaction;
        ButtonPolicy policy{.input_rule = ButtonInputRule::PrimaryPressedState};
        std::function<void()> primary, secondary, down;
        bool visible = true, enabled = true, dirty = true;
    };
    std::array<Action, 39> actions;
    std::shared_ptr<const SpriteSheet> item_preview;
    std::unique_ptr<W8NpcDialogueTextController> transcript;
};

enum W8NpcDialogueControlSlot {
    W8_NPC_CONTROL_NPC_NAME = 0,
    W8_NPC_CONTROL_TEXT_1 = 1,
    W8_NPC_CONTROL_TEXT_2 = 2,
    W8_NPC_CONTROL_TEXT_3 = 3,
    W8_NPC_CONTROL_TEXT_4 = 4,
    W8_NPC_CONTROL_TEXT_5 = 5,
    W8_NPC_CONTROL_TEXT_6 = 6,
    W8_NPC_CONTROL_TEXT_7 = 7,
    W8_NPC_CONTROL_WIDGET = 8,
    W8_NPC_CONTROL_SCROLL = 9,
    W8_NPC_CONTROL_SCROLL_UP_BUTTON = 10,
    W8_NPC_CONTROL_SCROLL_DOWN_BUTTON = 11,
    W8_NPC_CONTROL_TEXT_12 = 12,
    W8_NPC_CONTROL_TEXT_13 = 13,
    W8_NPC_CONTROL_SORT_BUTTON = 15,
    W8_NPC_CONTROL_PEOPLE_BUTTON = 17,
    W8_NPC_CONTROL_PLACES_BUTTON = 18,
    W8_NPC_CONTROL_ITEMS_BUTTON = 19,
    W8_NPC_CONTROL_MISC_BUTTON = 20,
    W8_NPC_CONTROL_ALL_BUTTON = 21,
    W8_NPC_CONTROL_TEXT_22 = 22,
    W8_NPC_CONTROL_TEXT_23 = 23,
    W8_NPC_CONTROL_TEXT_24 = 24,
    W8_NPC_CONTROL_OPTION_BUTTONS = 25,
    W8_NPC_CONTROL_TEXT_31 = 31,
    W8_NPC_CONTROL_TEXT_33 = 33,
    W8_NPC_CONTROL_TEXT_34 = 34,
    W8_NPC_CONTROL_TEXT_35 = 35,
    W8_NPC_CONTROL_TEXT_36 = 36,
    W8_NPC_CONTROL_TEXT_37 = 37,
    W8_NPC_CONTROL_TEXT_38 = 38,
};

void ForwardNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, int line, bool suppress);
void QueueNpcScriptNotice(W8NpcState* npc, W8ItemInstance* item, int line, bool suppress,
                          unsigned char arg);
void FlushPendingNoticeLines(void);

void SyncDialogueNpcState(void);
/* Store a transcript keyword, inferring its category when category is -1. */
void AddDialogueTranscriptKeyword(std::string_view name, W8DialogueCategory category);
void SetNpcQuoteBubbleVisible(bool visible, std::string_view text, W8NpcScriptQuote* quote,
                              int quote_id, unsigned int font_palette);
void SetNpcQuoteBubbleVisible(bool visible, std::string_view text, W8NpcScriptQuote* quote,
                              int quote_id, unsigned int font_palette, W8QuoteNotice notice,
                              int npc_kind);
void DrawNpcQuoteBubble(void);
void LookAtDialogueNpc(void);
void CloseNpcDialogueLayout(void);
void OpenNpcDialogueTranscriptLayout(void);
void DispatchPendingNpcScriptNotice(void);
bool IsNpcDialogueTextInputActive(void);
bool IsNpcDialogueTextBoxActive(void);
bool SetNpcDialoguePanelVisible(unsigned char value);
bool ProcessPendingEvent(void);
void SyncDialogueNpcStateAndMarkPending(void);
void SyncNpcServiceButtons(int party_slot);
void SwitchNpcDialogueLayout(W8NpcDialogueLayout layout);
void BeginNpcDialogue(W8NpcState* npc, W8ItemInstance* item, int quote, unsigned char flags,
                      unsigned char force);
void SelectNpcDialogueSpeaker(W8NpcState* npc, int flags);
void InvalidateMainGameActionPanelRect(const W8ControlsRect* rect);
void CloseNpcDialogueOptionLayout(void);
void ShowNpcDialogueTopicMenu(void);
std::string TranslateDialogueKeyword(std::string_view source);
void ResetNpcDialogueItemEditor(void);
/* While NPC script deferral holds character events, drain Escape / click so
   the open dialogue layout can dismiss without the normal input path. */
void DrainNpcDialogueDeferralInput(void);
/* When world_cursor_gate is set, discard queued input after a mouse-position hook so
   the world-cursor gate does not process stale events. */
void FlushInputWhileWorldCursorGate(void);
void HandleNpcDialogueInput(void);
void OpenNpcDialog(W8NpcQuoteEntry* request, int aux_data);
/* Learn one keyword into the dialogue transcript. category -1
   auto-classifies the text against items, NPC/named-monster names and the
   place-name table; a nonzero play_chime rings the keyword chime. */
void AddNpcDialogueKeyword(std::string text, W8DialogueCategory category, int play_chime);
void CloseNpcDialogueForCamp(void);
void UpdateNpcDialogueSubMode(void);
/* Restate the five transcript category buttons so only the
   active dialogue_category_filter's button shows its secondary state. */
void SyncDialogueCategoryButtons(void);
void EndNpcDialogueSession(bool skip_exit_actions);

inline void CloseNpcDialogueIfActive(void)
{
    if (CurrentGame().activity.conversation) {
        EndNpcDialogueSession(false);
    }
}
/* Whether an open NPC dialogue transcript covers the party slot's portrait:
   dialogue mode up, scripted_dialogue clear, the controller enabled, and its top
   edge above the slot's band. Portrait and character-update paths skip the
   covered rows through this. */
bool IsPortraitObscuredByNpcDialogue(unsigned int party_slot);
void RecordLevelEntryDialogueState(void);
unsigned char IsNpcDialogueCursorActive(void);
/* Forward a portrait pick into an active NPC dialogue. */
void TryNpcDialoguePickpocket(int party_slot);
std::string ShortenTextToWidth(std::string_view text, unsigned int width, Font& font);
void SetDialogueFieldKeyword(std::string keyword, bool append);
void ActivateNpcDialoguePanels(bool active);
bool HasNpcDialogueDirtyPanels(void);
UiResult HandleNpcTextBox(const UiPointerEvent& event);
void NpcDialogueTextBoxWheelAt(short x, unsigned short y, bool flag);
/* True when an NPC quote/portrait session is active: finish voice playback and
   report that the click was consumed. */
bool FinishNpcVoiceIfSessionActive(void);
void UpdateNpcTradeSelection(int index, int, int);
bool NpcTradeItemAllowed(W8ItemInstance* item);
void EnableNpcTradeFilterButtons(void);
void HandleNpcDialogueKeyEvent(const InputEvent* event);
void HandleNpcDialogueItemChoice(void);
void RefreshNpcTradePartyGold(void);
void RefreshNpcTradePrice(void);
