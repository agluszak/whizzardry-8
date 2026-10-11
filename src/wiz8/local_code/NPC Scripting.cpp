#include <fmt/chrono.h> // IWYU pragma: keep - std::tm formatter specialization.
#include <fmt/format.h>
#include <fmt/printf.h>
#include <string_view>
#include <vector>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <exception>
#include <utility>
#include <algorithm>
#include <variant>

#include "wiz8/unicode.h"
#include "wiz8/conditions.h"
#include "wiz8/fonts.h"
#include "wiz8/integer_constants.h"
#include "cursor.h"
#include "soundman.h"
#include "wiz8/layouts/character.h"
#include "wiz8/character_skills.h"
#include "wiz8/fact_state.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/filesystem.h"
#include "wiz8/file_time.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/regions.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/xstatus.h"
#include "layouts/item_instance.h"
#include "layouts/npc_dialogue.h"
#include "layouts/screen_state.h"
#include "layouts/targeting.h"
#include "local_code/Factions.h"
#include "local_code/MonsterManager.h"
#include "monster_actions.h"
#include "wiz8/npc_speech.h"
#include "wiz8/local_code/npc_story_events.h"
#include "audio_resources.h"
#include "wiz8/local_code/npc_dialogue_view.h"

static void ProcessMessageBoxQueue(void);
static void OnNpcTravelConfirmationClosed(W8DialogBase* dialog);

class W8DialogBase;

struct W8NpcScriptRegionName {
    std::string name;
    int region;
};

// GLOBAL: WIZ8 0x0061b788
static W8NpcScriptRegionName g_npc_script_region_names[] = {{"Monastery", 1},
                                                            {"Arnika", 2},
                                                            {"Trynton", 3},
                                                            {"Swamp", 4},
                                                            {"Marten's Bluff", 5},
                                                            {"Mine Tunnels", 6},
                                                            {"Sea Caves", 7},
                                                            {"Bayjin", 8},
                                                            {"Rapax Castle", 9},
                                                            {"Rapax Rift", 10},
                                                            {"Mt. Gigas", 11},
                                                            {"Ascension Peak", 12},
                                                            {"Rapax Away Camp", 13},
                                                            {"Cosmic Circle", 14},
                                                            {"", 0}};

void ApplyScriptedDialogueKeyword(int kind, const char* text)
{
    signed char category;
    switch (kind) {
    case W8_NPC_ENTRY_PERSON_KEYWORD:
    case 31:
        category = W8_DIALOGUE_CATEGORY_PEOPLE;
        break;
    case W8_NPC_ENTRY_ITEM_KEYWORD:
    case 32:
        category = W8_DIALOGUE_CATEGORY_ITEMS;
        break;
    case W8_NPC_ENTRY_PLACE_KEYWORD:
    case 33:
        category = W8_DIALOGUE_CATEGORY_PLACES;
        break;
    default:
        category = W8_DIALOGUE_CATEGORY_MISC;
        break;
    }
    if (g_settings.simplified_npc_interaction != 0) {
        std::string keyword_text;
        keyword_text = text;
        if (!gXStatus.fNpcDialogueMode || !g_npc_interaction_state->scripted_dialogue) {
            AddNpcDialogueKeyword(keyword_text, category, 1);
        } else {
            AddDialogueTranscriptKeyword(keyword_text, category);
        }
    }
}

// FUNCTION: WIZ8 0x00528f10
std::string GetNpcScriptRegionName(int region)
{
    for (const auto& entry : g_npc_script_region_names)
        if (entry.region == region) return entry.name;
    return {};
}

/* The named-person table FindNpcNameOrPlaceQuote resolves keywords against:
   the mask bit the speaking NPC's record must carry to answer as that person,
   and the quote id the keyword maps to. */
struct W8NpcNamedQuote {
    std::string name;
    unsigned int mask;
    int quote;
};

// GLOBAL: WIZ8 0x0061aea8
static W8NpcNamedQuote g_npc_named_quotes[] = {
    {"Phoozang", 0x4000, 116},    {"Cosmic Lords", 0x8000, 116}, {"Yamir", 0x10000, 111},
    {"Z'Ant", 0x20000, 109},      {"Vi Domina", 0x40000, 106},   {"Al-Sedexus", 0x80000, 113},
    {"Astral Dominae", 1, 106},   {"Destinae Dominus", 2, 112},  {"Chaos Moliri", 4, 106},
    {"Helm of Serinity", 8, 107}, {"Mook", 0x10, 106},           {"Umpani", 0x20, 111},
    {"T'Rang", 0x40, 109},        {"Higardi", 0x80, 106},        {"Trynnie", 0x100, 107},
    {"Rapax", 0x200, 114},        {"Rynjin", 0x400, 114},        {"Rattkin", 0x800, 107},
    {"Dark Savant", 0x1000, 106}, {"Marten", 0x2000, 112},
};
/* No full sentinel row exists in retail: the name[0] scan in
   FindNpcNameOrPlaceQuote reads one row past the end, where the following
   zero word happens to terminate the loop. */

/* Region-by-level-band quote table: one 0x12-int row per
   g_npc_script_region_names region id; band 0 is unused. */
// GLOBAL: WIZ8 0x0061bda0
static int g_npc_region_quotes[15][0x12] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, 117, 210, 210, 210, 210, 210, 213, 210, 211, 211, 211, 211, 213, 210, -1, 210, 213},
    {-1, 211, 117, 210, 210, 210, 210, 213, 210, 213, 213, 211, 211, 213, 210, -1, 210, 213},
    {-1, 211, 211, 117, 213, 213, 213, 213, 213, 213, 213, 211, 211, 213, 213, -1, 212, 213},
    {-1, 211, 212, 212, 117, 210, 213, 213, 211, 211, 211, 211, 211, 213, 211, -1, 212, 211},
    {-1, 211, 212, 212, 211, 117, 211, 213, 211, 211, 211, 211, 211, 213, 211, -1, 212, 211},
    {-1, 211, 212, 212, 212, 212, 117, 213, 212, 211, 211, 211, 211, 213, 212, -1, 212, 211},
    {-1, 213, 212, 212, 212, 212, 212, 117, 212, 211, 211, 211, 211, 213, 212, -1, 212, 211},
    {-1, 211, 212, 212, 210, 210, 210, 213, 117, 211, 211, 212, 212, 213, 210, -1, 212, 213},
    {-1, 212, 212, 210, 210, 210, 210, 210, 212, 117, 210, 212, 212, 210, 210, -1, 212, 211},
    {-1, 212, 212, 210, 210, 210, 210, 210, 210, 211, 117, 212, 212, 210, 210, -1, 212, 211},
    {-1, 210, 210, 210, 210, 210, 210, 210, 210, 213, 213, 117, 213, 210, 210, -1, 210, 210},
    {-1, 212, 210, 210, 210, 210, 210, 210, 210, 213, 213, 212, 117, 210, 210, -1, 212, 212},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 211, 211, 211, 117, -1, -1, 212, 211},
    {-1, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 118, 212, 212},
};

/* Kind 0x0b consumes the item; kind 0x0f only mentions it. */
// FUNCTION: WIZ8 0x00528CD0
std::optional<W8NpcScriptItemQuote> FindNpcScriptItemQuote(int item_id)
{
    int entry_index;
    int quote_index;

    for (quote_index = 0;
         quote_index < static_cast<int>(g_npc_scripting.session.npc->script_file->quote_count);
         ++quote_index) {
        W8NpcScriptQuote* quote = g_npc_scripting.session.npc->script_file->quotes.data() + quote_index;
        for (entry_index = 0; entry_index < static_cast<int>(quote->entry_count); ++entry_index) {
            W8NpcQuoteEntry* entry = quote->entries.data() + entry_index;
            if ((entry->kind == W8_NPC_ENTRY_ITEM_REQUEST ||
                 entry->kind == W8_NPC_ENTRY_ITEM_MENTION) &&
                entry->operand0 == item_id) {
                return W8NpcScriptItemQuote{quote_index, entry->kind != W8_NPC_ENTRY_ITEM_MENTION};
            }
        }
    }
    return std::nullopt;
}


/* Resolve an NPC-name, named-person, or region keyword to the quote id the
   speaker answers with; -1 when nothing applies. The record's alias mask
   selects which named persons this NPC will speak for. */
// FUNCTION: WIZ8 0x00528D50
int FindNpcNameOrPlaceQuote(W8NpcState* npc, std::string_view text)
{
    int index;
    int band;

    if (CompareTextIgnoreAsciiCase(npc->record->source_name, text) == 0) {
        return 0x75;
    }
    for (index = 0; g_npc_named_quotes[index].name[0] != 0; ++index) {
        if (CompareTextIgnoreAsciiCase(text, g_npc_named_quotes[index].name) == 0) {
            if ((npc->record->name_alias_mask & g_npc_named_quotes[index].mask) == 0) {
                return -1;
            }
            return g_npc_named_quotes[index].quote;
        }
    }
    if (CompareTextIgnoreAsciiCase(text, "Trang") == 0 &&
        (npc->record->name_alias_mask & 0x40) != 0) {
        return 0x6d;
    }
    for (index = 0; g_npc_script_region_names[index].name[0] != 0; ++index) {
        if (CompareTextIgnoreAsciiCase(text, g_npc_script_region_names[index].name) == 0) {
            band = GetLevelBand(g_status.current_level);
            return g_npc_region_quotes[g_npc_script_region_names[index].region][band];
        }
    }
    if (CompareTextIgnoreAsciiCase(text, "Mt Gigas") == 0 ||
        CompareTextIgnoreAsciiCase(text, "Gigas") == 0) {
        band = GetLevelBand(g_status.current_level);
        return g_npc_region_quotes[11][band];
    }
    if (CompareTextIgnoreAsciiCase(text, "the swamp") == 0) {
        band = GetLevelBand(g_status.current_level);
        return g_npc_region_quotes[4][band];
    }
    if (CompareTextIgnoreAsciiCase(text, "the Monastery") != 0) {
        return -1;
    }
    band = GetLevelBand(g_status.current_level);
    return g_npc_region_quotes[1][band];
}

static void ShowConsumedItemQuote(std::string_view text)
{
    if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        RebuildNpcTradeItemList(false);
        return;
    }
    DisplayNpcQuote(text, true);
    return;
}

/* Remove one stack unit of the matching item: the character backpacks first
   (the slot whose address equals `item` wins, or any slot carrying `item_id`
   when `match_item_id` is set), then the party pool, then the pending
   trade item. The first hit announces the loss through the quote bubble or
   the layout-4 path and returns. */
// FUNCTION: WIZ8 0x00528FF0
void RemoveNpcScriptItem(W8ItemInstance* item, int match_item_id, int item_id)
{
    std::string text;
    unsigned int slot;
    unsigned int index;
    W8ItemInstance* slot_item;

    for (slot = 0; slot < 8; ++slot) {
        if (!g_status.buffers.XChar[slot].fOccupied) {
            continue;
        }
        for (index = 0; index < 8; ++index) {
            slot_item = &g_status.buffers.Char[slot].backpack[index];
            if (slot_item->iItemNo != -1 &&
                (slot_item == item || (match_item_id != 0 && slot_item->iItemNo == item_id))) {
                if (slot_item->stack_count != 0) {
                    --slot_item->stack_count;
                }
                if (slot_item->stack_count == 0) {
                    EmptyItemRecord(slot_item, &g_status.buffers.Char[slot], true);
                }
                text = fmt::sprintf(g_localized_strings[0x7ec], g_status.buffers.Char[slot].name);
                ShowConsumedItemQuote(text);
                return;
            }
        }
    }
    for (slot = 0; slot < g_status.party_item_count; ++slot) {
        slot_item = &g_status.party_item_pool[slot];
        if (slot_item->iItemNo != -1 &&
            (slot_item == item || (match_item_id != 0 && slot_item->iItemNo == item_id))) {
            if (slot_item->stack_count != 0) {
                --slot_item->stack_count;
            }
            if (slot_item->stack_count == 0) {
                EmptyPartyPoolEntry(slot);
            }
            text = g_localized_strings[0x7ed];
            ShowConsumedItemQuote(text);
            return;
        }
    }
    if (g_npc_interaction_state->held_item_pending && item != 0 &&
        g_npc_interaction_state->pending_item.iItemNo == item->iItemNo) {
        if (item->stack_count != 0) {
            --item->stack_count;
        }
        if (item->stack_count == 0) {
            g_npc_interaction_state->held_item_pending = false;
            if (!gXStatus.fNpcDialogueMode) {
                ClearHeldItemDisplay();
            }
        }
    }
}

// FUNCTION: WIZ8 0x00528f60
void StripNpcKeywordPunctuation(std::string& text)
{
    std::erase_if(text, [](char c) { return std::string_view("!?@#$,.\":").find(c) != std::string_view::npos; });
}

/* Alternates between the two string-list ids SpeakNpcSubquote substitutes for
   an "EMPTY"/"BLANK" quote on the closing quotes it allows. */
// GLOBAL: WIZ8 0x0068C4FC

// GLOBAL: WIZ8 0x0068c500
static unsigned char g_npc_script_event_active;
// GLOBAL: WIZ8 0x0068C501
static bool g_message_queue_idle;

// GLOBAL: WIZ8 0x0068C358
static int g_pending_npc_travel_level;

// GLOBAL: WIZ8 0x0061aea0
static wiz8::sound::Playback g_sedexus_sound_handle = nullptr;
// GLOBAL: WIZ8 0x0061c324
static char g_sedexus_moaning_sound[] = "Data\\Sound\\Ambients\\Al_Sedexus Moaning.wav";

// FUNCTION: WIZ8 0x00524BD0
std::string FormatNpcVoiceSoundPath(W8NpcState* npc)
{
    const auto prefix = npc->is_grouped && g_npc_script_event_active == 0 ? "RPC_" :
                        npc->record->voice_script != 0 ? "VOC_" : "NPC_";
    return fmt::format("{}{}", prefix, GetNpcDisplayName(npc));
}

// FUNCTION: WIZ8 0x00524CA0
void ReloadNpcScriptResources(W8NpcState* npc)
{
    const std::string script_name = FormatNpcVoiceSoundPath(npc);
    std::string resource_name = fmt::format("Data\\NPC Scripts\\{}.nsf", script_name);
    ReleaseNpcScriptFile(npc->script_file);
    npc->script_file = nullptr;
    npc->script_file = LoadNpcScriptFile(resource_name);
    if (npc->script_file != 0) {
        W8Monster* monster = GetNpcMonster(npc);
        if (monster != 0) {
            resource_name = fmt::format("{}.msf", script_name);
            monster->SetScript(resource_name.c_str(), true);
            W8MonsterInfo* monster_info = FindMonster(monster->location_id);
            if (monster_info != 0) {
                GetMonsterDataForInfo(monster_info);
            }
        }
    }
}


// FUNCTION: WIZ8 0x00524EB0
void ProcessNpcScriptingFrame(void)
{
    W8Character* character;
    bool can_open_dialogue;
    bool dialogue_ready;
    int environment;
    int party_slot;
    int sedexus_party_slot;

    if ((g_npc_scripting.sedexus_capture_pending || g_npc_scripting.sedexus_release_pending) &&
        ((environment = GetEnvironmentValue(), environment == 0) ||
         (environment = GetEnvironmentValue(), environment == 2))) {
        ClearScriptedSceneActive();
        if (!g_npc_scripting.sedexus_release_pending) {
            g_sedexus_sound_handle = wiz8::sound::stream(g_sedexus_moaning_sound);
        } else {
            ClearMainGameTargetState();
            sedexus_party_slot = g_status.sedexus_party_slot;
            for (party_slot = 0; party_slot < 8; ++party_slot) {
                W8PartySlotRow* row = &g_status.buffers.XChar[party_slot];
                character = &g_status.buffers.Char[party_slot];
                if (row->fOccupied && ((character->hp_current > 0 ||
                                        character->highest_condition < W8_CONDITION_DEAD) &&
                                       party_slot != sedexus_party_slot)) {
                    RemoveCharacterCondition(party_slot, W8_CONDITION_UNCONSCIOUS, false);
                    sedexus_party_slot = g_status.sedexus_party_slot;
                }
            }
            character = &g_status.buffers.Char[sedexus_party_slot];
            if (character->gender == W8_GENDER_MALE) {
                QueueCharacterEvent(character, g_effect29, 0, g_character_event_no_flags,
                                    g_character_event_full_volume);
            }
            g_npc_scripting.sedexus_capture_active = false;
            if (g_sedexus_sound_handle != nullptr) {
                wiz8::sound::stop(g_sedexus_sound_handle);
                g_sedexus_sound_handle = nullptr;
            }
        }
        g_npc_scripting.sedexus_capture_pending = false;
        g_npc_scripting.sedexus_release_pending = false;
    }
    if (!g_npc_scripting.scripted_scene_active && !g_npc_scripting.session.quote_active &&
        !g_npc_scripting.portrait_message_active) {
        if (g_npc_scripting.reply != W8NpcReplyState::dialog_open) {
            ProcessMessageBoxQueue();
            if (g_npc_scripting.session.quote_active) {
                return;
            }
            if (g_npc_scripting.portrait_message_active) {
                return;
            }
        }
        if (g_npc_scripting.message_lines.empty()) {
            if (g_npc_scripting.suspended_session) {
                g_npc_scripting.session = *g_npc_scripting.suspended_session;
                g_npc_scripting.suspended_session.reset();
            }
            if (gXStatus.fNpcDialogueMode && g_status.world_cursor_gate == 0 &&
                !gXStatus.scripted_scene && g_npc_scripting.reply == W8NpcReplyState::none &&
                (can_open_dialogue = CanOpenNpcDialogue(), can_open_dialogue)) {
                EndNpcDialogueSession(false);
            }
            if (g_npc_scripting.reply == W8NpcReplyState::none &&
                g_npc_interaction_state->dialogue_panel_hidden &&
                (dialogue_ready = gXStatus.character_event_queue->IsMainQueueEmpty(),
                 dialogue_ready)) {
                SetNpcDialoguePanelVisible(1);
            }
        }
    }
}

// FUNCTION: WIZ8 0x00525110
void BeginNpcScriptDialogue(W8NpcState* npc, unsigned char preserve_state)
{
    if (preserve_state != 0) {
        g_npc_scripting.suspended_session = g_npc_scripting.session;
    }
    if (!npc->has_monster) {
        BindNpcToMonster(npc->name_style, false, -1);
    }
    g_npc_scripting.session.quote_active = false;
    g_npc_scripting.session.finished_quote_index = -1;
    g_npc_scripting.session.npc = npc;
    g_npc_scripting.session.script_file = npc->script_file;
}





// FUNCTION: WIZ8 0x00525DD0
bool IsNpcScriptSessionActive(void)
{
    return g_npc_scripting.session.quote_active || g_npc_scripting.portrait_message_active;
}

// FUNCTION: WIZ8 0x00525DF0
bool ShouldDeferCharacterEventForNpcScript(bool require_group_entry)
{
    if (g_npc_scripting.scripted_scene_active) {
        return false;
    }
    if (!g_npc_scripting.session.quote_active && !g_npc_scripting.portrait_message_active &&
        g_npc_scripting.message_lines.empty()) {
        return false;
    }
    if (g_npc_scripting.session.npc == 0) {
        return false;
    }
    if (GetNpcGroupEntry(g_npc_scripting.session.npc) != 0 && !require_group_entry) {
        return false;
    }
    return true;
}

// FUNCTION: WIZ8 0x00525E50
bool IsMessageBoxLineQueueEmpty(void) { return g_npc_scripting.message_lines.empty(); }


/* Execute a queued quote entry's deferred effect: switch the dialogue to the
   named NPC (suspending the current session when the target is grouped), hand
   items or gold to the party with the notice bubble and chime, or open the
   modal dialog kinds. The party slot's pending portrait event clears once the
   entry has run. */
// FUNCTION: WIZ8 0x00526810
static void ProcessNpcQuoteEntry(W8NpcQuoteEntry* entry, int continuation_quote)
{
    W8NpcState* target;
    W8ItemInstance item;
    std::string action_name;
    std::string notice_text;

    switch (entry->kind) {
    case W8_NPC_ENTRY_NEXT_QUOTE:
        if (entry->operand2 == 4 && entry->operand1 != 2) {
            action_name = entry->sub_entries[0].text.c_str() + 4;
            target = FindNpcStateByName(action_name);
            if (target != 0) {
                if (target->name_style == W8_NPC_VI_DOMINA && entry->operand0 == 0) {
                    g_npc_scripting.session.subquote_index = 0;
                }
                if (!target->is_grouped) {
                    SelectNpcDialogueSpeaker(target, 0);
                    if (g_status.current_level != 4 || g_status.world_cursor_gate == 0) {
                        LookAtDialogueNpc();
                    }
                } else {
                    BeginNpcScriptDialogue(target, 1);
                }
                QueueNpcScriptLine(entry->operand0, false, false, false);
                if (target->is_grouped) {
                    SetNpcDialoguePanelVisible(0);
                }
                FlushPendingNoticeLines();
            }
        }
        break;
    case W8_NPC_ENTRY_FACT_CONDITIONAL_QUOTE:
        if (entry->operand2 == 4 && entry->operand1 != 2) {
            action_name = entry->sub_entries[0].text.c_str() + 4;
            target = FindNpcStateByName(action_name);
            if (target != 0) {
                if (!target->is_grouped) {
                    SelectNpcDialogueSpeaker(target, 0);
                    if (g_status.current_level != 4 || g_status.world_cursor_gate == 0) {
                        LookAtDialogueNpc();
                    }
                } else {
                    BeginNpcScriptDialogue(target, 1);
                    SetNpcDialoguePanelVisible(0);
                }
                QueueNpcScriptLine(entry->operand0, false, false, false);
                if (target->is_grouped) {
                    SetNpcDialoguePanelVisible(0);
                }
                FlushPendingNoticeLines();
            }
        }
        break;
    case W8_NPC_ENTRY_OPTIONS:
    case W8_NPC_ENTRY_KEYWORD_INPUT:
        continuation_quote = -1;
        [[fallthrough]];
    case W8_NPC_ENTRY_PRICE_CHECK:
    case W8_NPC_ENTRY_ALWAYS_PRICE_CHECK:
        OpenNpcDialog(entry, continuation_quote);
        break;
    case W8_NPC_ENTRY_GIVE_ITEM:
        ReplaceOrCreateItem(&item, entry->operand0, true, true, false);
        notice_text = fmt::sprintf(g_localized_strings[0x7ea], GetItemDisplayName(&item));
        if (!gXStatus.fNpcDialogueMode) {
            g_status.item_in_hand = item;
            SetItemCursor(0);
        } else {
            AddItemToPartyOrDrop(&item, false);
            DisplayNpcQuote(notice_text, true);
        }
        ClearNpcItemId(g_npc_scripting.session.npc, entry->operand0);
        if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
            RebuildNpcTradeItemList(false);
        }
        break;
    case W8_NPC_ENTRY_REMOVE_ITEM:
        RemoveNpcScriptItem(0, 1, entry->operand0);
        break;
    case W8_NPC_ENTRY_GIVE_GOLD:
        notice_text = fmt::sprintf(g_localized_strings[0x7e9],
                                   g_npc_scripting.session.npc->record->source_name, entry->operand0);
        AddPartyGold(entry->operand0, false);
        DisplayNpcQuote(notice_text, true);
        break;
    case W8_NPC_ENTRY_GIVE_EXPERIENCE:
        AwardPartyExperience(entry->operand0, 0);
        break;
    }
    if (g_npc_scripting.session.npc->character != 0 && g_npc_scripting.session.npc->group_index != -1) {
        g_status.buffers.XChar[g_npc_scripting.session.npc->group_index].pending_event_type = 0;
    }
}

// FUNCTION: WIZ8 0x00526E90
static void ProcessMessageBoxQueue(void)
{
    W8NpcState* npc;
    int index;

    if (static_cast<int>(g_npc_scripting.message_lines.size()) < 1) {
        if (!g_message_queue_idle) {
            FlushPendingNoticeLines();
        }
        g_message_queue_idle = true;
        return;
    }

    auto current = g_npc_scripting.message_lines.begin();
    W8MessageBoxLine& line = *current;
    npc = line.npc_kind != -1 ? GetNpcStateByKind(line.npc_kind) : 0;
    if (auto* quote_line = std::get_if<W8NpcQueuedQuote>(&line.action)) {
        if (npc != 0 && npc != g_npc_scripting.session.npc) {
            BeginNpcScriptDialogue(npc, 1);
        }
        if (g_message_queue_idle) {
            g_message_queue_idle = false;
            if (g_npc_scripting.session.npc->record->voice_script == 0) {
                for (const W8MessageBoxLine& queued_line : g_npc_scripting.message_lines) {
                    const auto* queued = std::get_if<W8NpcQueuedQuote>(&queued_line.action);
                    if (queued == 0 || queued->suppress_entries ||
                        std::ranges::find(g_npc_scripting.pending_script_values,
                                          queued->quote_index) ==
                            g_npc_scripting.pending_script_values.end()) {
                        continue;
                    }
                    if (g_npc_scripting.session.npc->record->merchant == 0 && queued->quote_index != 0x76) {
                        const W8NpcScriptQuote& quote =
                            g_npc_scripting.session.npc->script_file->quotes[queued->quote_index];
                        if (std::ranges::none_of(quote.entries, [](const W8NpcQuoteEntry& entry) {
                                return entry.kind == 0x1d;
                            })) {
                            RunNpcScriptLine(0x1f, false);
                            return;
                        }
                    }
                }
            }
            g_npc_scripting.pending_script_values.clear();
        }

        const int quote_index = quote_line->quote_index;
        if (quote_line->mark_pending) {
            g_npc_scripting.pending_script_values.push_back(quote_index);
        }

        W8NpcScriptFile* script = g_npc_scripting.session.npc->script_file;
        if (script != 0 && !quote_line->suppress_entries && quote_index < script->quote_count) {
            W8NpcScriptQuote* quote = &script->quotes[quote_index];
            for (index = 0; index < quote->entry_count; ++index) {
                W8NpcQuoteEntryKind kind = quote->entries[index].kind;
                if ((kind == W8_NPC_ENTRY_PRICE_CHECK || kind == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK) &&
                    (kind == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK ||
                     !NpcKnowsFact(g_npc_scripting.session.npc, static_cast<W8FactId>(quote_index)))) {
                    RunNpcScriptLine(0x12, false);
                    g_npc_scripting.reply = W8NpcReplyState::pending;
                    QueueNpcQuoteEntry(quote_index, index, quote_index, true);
                    g_npc_scripting.message_lines.erase(current);
                    return;
                }
            }
        }
        if (script != 0) {
            RunNpcScriptLine(quote_index, false);
        }
        g_npc_scripting.message_lines.erase(current);
        return;
    }

    const auto* command = std::get_if<W8NpcQueuedCommand>(&line.action);
    const int argument = command != 0 ? command->argument : 0;
    auto* notice = std::get_if<W8NpcQueuedNotice>(&line.action);
    const W8NpcMessageKind kind = GetMessageBoxLineKind(line);
    if (RunNpcStoryEvent(kind, argument)) {
        g_npc_scripting.message_lines.erase(current);
        return;
    }
    switch (kind) {
    case W8_NPC_MSG_FOCUS_NPC: {
        int npc_kind = argument;
        npc = GetNpcStateByKind(npc_kind);
        if (npc != 0) {
            RecruitNpcIntoParty(npc);
        }
        EndNpcDialogueSession(false);
        QueueNpcScriptLine(0, false, false, false);
        break;
    }
    case W8_NPC_MSG_GROUP_ACTION: {
        int group = argument;
        ClearMainGameTargetState();
        DismissNpcFromParty(group, 0, false, false);
        if (g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
            for (int party_slot = 0; party_slot < 8; ++party_slot) {
                if (g_status.buffers.XChar[party_slot].fOccupied) {
                    RegionSetDisable(party_slot + 7);
                    DisableRegionSetInput(party_slot + 7);
                }
            }
        }
        break;
    }
    case W8_NPC_MSG_PARTY_MEMBER_EVENT: {
        int party_slot = argument;
        QueueCharacterEvent(&g_status.buffers.Char[party_slot], g_effect1,
                            g_character_event_no_npc_defer, g_character_event_no_flags,
                            g_character_event_full_volume);
        break;
    }
    case W8_NPC_MSG_PARTY_SPEAKER_EVENT: {
        unsigned int event_type = static_cast<unsigned int>(argument);
        int party_slot = PickRandomPartySpeaker(event_type, g_status.selected_party_member);
        if (party_slot != -1) {
            g_status.selected_party_member = static_cast<unsigned char>(party_slot);
            QueueCharacterEvent(&g_status.buffers.Char[party_slot], event_type,
                                g_character_event_no_npc_defer, g_character_event_no_flags,
                                g_character_event_full_volume);
            SetNpcDialoguePanelVisible(0);
            if (g_npc_interaction_state->modal_dialog_open) {
                g_npc_interaction_state->suppress_parting_reaction = true;
            }
        }
        break;
    }
    case W8_NPC_MSG_DISPATCH_PENDING_NOTICE:
        DispatchPendingNpcScriptNotice();
        break;
    case W8_NPC_MSG_CLOSE_DIALOGUE:
        CloseNpcDialogueIfActive();
        g_npc_dialogue_closed = true;
        break;
    case W8_NPC_MSG_JOURNAL_QUOTE:
        ShowNpcScriptNoticeBubble(g_localized_strings[0x74a]);
        g_npc_scripting.session.quote_active = true;
        wiz8::sound::play("Data\\Sound\\Misc\\Journal Entry.wav");
        break;
    case W8_NPC_MSG_PORTRAIT_STRING: {
        int string_index = argument;
        ShowNpcPortraitMessage(g_localized_strings[string_index]);
        break;
    }
    case W8_NPC_MSG_SKILL_NOTICES: {
        if (g_settings.skill_increase_messages == 0) {
            PostSkillIncreaseNotices(&std::get<W8SkillNoticePayload>(notice->notice));
        } else {
            ShowNpcPortraitMessage(notice->text, std::move(notice->notice));
        }
        break;
    }
    case W8_NPC_MSG_SET_CONDITION_13: {
        int party_slot = argument;
        g_status.skip_next_condition_reaction = 1;
        SetCharacterCondition(party_slot, W8_CONDITION_MISSING, W8_CONDITION_INDEFINITE, 0, 0, 0);
        g_status.condition13_clock = 1;
        g_status.condition13_stamp = g_status.world_clock;
        g_status.pending_condition_party_slot = party_slot;
        break;
    }
    case W8_NPC_MSG_PORTRAIT_MESSAGE:
        ShowNpcPortraitMessage(notice->text);
        break;
    case W8_NPC_MSG_CLEAR_NPC_COMBAT:
        if (g_combat_state != 0) {
            int party_slot = argument;
            if (g_combat_state->iActionChar == party_slot) {
                g_combat_state->eCombatActionStatus = 0;
                g_combat_state->iActionChar = -1;
            }
            ClearMainGameTargetState();
            DismissNpcFromParty(party_slot, 0, false, true);
            SetTargetToCharacter(party_slot, false);
            g_combat_state->npc_combat_script_pending[party_slot] = false;
        }
        break;
    case W8_NPC_MSG_PORTRAIT_EXTRA:
        ShowNpcPortraitMessage(notice->text, std::move(notice->notice));
        break;
    case W8_NPC_MSG_LEVEL_UP: {
        int party_slot = std::get<W8LevelUpNotice>(notice->notice).party_slot;
        g_status.buffers.XChar[party_slot].portrait_advance = 1;
        if (g_settings.skill_increase_messages == 0) {
            wiz8::sound::play("Data\\Sound\\Misc\\GainLevel.wav");
        } else {
            ShowNpcPortraitMessage(notice->text, std::move(notice->notice));
        }
        break;
    }
    case W8_NPC_MSG_CLOSE_RESUME_NPC:
        CloseNpcDialogueIfActive();
        if (g_npc_interaction_state->dialogue_npc != 0) {
            ResumeNpc(g_npc_interaction_state->dialogue_npc, 1);
        }
        break;
    case W8_NPC_MSG_SHOW_DIALOGUE_PANEL:
        SetNpcDialoguePanelVisible(1);
        break;
    case W8_NPC_MSG_QUOTE_ENTRY: {
        const auto& queued = std::get<W8NpcQueuedEntry>(line.action);
        W8NpcScriptFile* script = npc != 0 ? npc->script_file : 0;
        if (script != 0 && queued.quote_index >= 0 &&
            queued.quote_index < static_cast<int>(script->quotes.size()) &&
            queued.entry_index >= 0 &&
            queued.entry_index <
                static_cast<int>(script->quotes[queued.quote_index].entries.size())) {
            ProcessNpcQuoteEntry(&script->quotes[queued.quote_index].entries[queued.entry_index],
                                 queued.continuation_quote);
        }
        break;
    }
    case W8_NPC_MSG_REOPEN_TRANSCRIPT:
        CloseNpcDialogueLayout();
        OpenNpcDialogueTranscriptLayout();
        break;
    case W8_NPC_MSG_PARTY_SLOT_EVENT_18: {
        int party_slot = argument;
        QueueCharacterEvent(&g_status.buffers.Char[party_slot], 0x18,
                            g_character_event_npc_script | g_character_event_no_npc_defer,
                            g_character_event_no_flags, g_character_event_full_volume);
        break;
    }
    case W8_NPC_MSG_REMOVE_SELF: {
        EndNpcDialogueSession(false);
        W8Monster* monster = GetNpcMonster(g_npc_scripting.session.npc);
        if (monster != 0) {
            monster->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        break;
    }
    case W8_NPC_MSG_TRAVEL_CONFIRM:
        ShowMainGameNoticeLine(g_localized_strings[0x7eb], OnNpcTravelConfirmationClosed, true, true);
        g_pending_npc_travel_level = argument;
        break;
    case W8_NPC_MSG_FINISH_ACTION:
        if (argument == 0) {
            ClearMainGameTargetState();
        } else {
            BeginScriptedWorldAction();
        }
        break;
    case W8_NPC_MSG_RESET_LEVEL_STATE:
        if (!gXStatus.fCombatMode || gXStatus.fPartyMovementMode) {
            if (argument == 0) {
                ClearLevelMovementStopped();
            } else {
                ResetLevelDataVectors();
            }
        }
        break;
    default:
        break;
    }

    g_npc_scripting.message_lines.erase(current);
}

// FUNCTION: WIZ8 0x0052a1b0
static void OnNpcTravelConfirmationClosed(W8DialogBase* dialog)
{
    if (GetDialogResult(dialog)) {
        QueueNpcTravelRefusals(g_pending_npc_travel_level);
    }
}



/* QA audit over every `Data\NPC Scripts\*.nsf`: load each script file, print
   every quote line that is not a placeholder sentinel through ShowNotice, log
   each notice that wraps past seven lines to data\longquotes.txt, and write
   per-script plus grand totals to data\quotereport.txt. Raising
   g_status.quote_audit makes ShowNotice maintain
   g_notice_line_count and g_status.long_quote. The
   report streams are owned and failures leave quote-audit mode disabled. */
// FUNCTION: WIZ8 0x00529660
void AuditNpcScriptQuotes(void)
{
    W8NpcScriptFile* script;
    W8NpcScriptQuote* quote;
    std::string date;
    std::string path;
    std::string display;
    std::string_view text;
    int file_lines;
    int file_quotes;
    int total_lines;
    int total_quotes;
    int total_scripts;
    int record_index;
    int sub_index;

    total_lines = 0;
    total_quotes = 0;
    total_scripts = 0;
    try {
        auto log_file = wiz8::open_file("data\\longquotes.txt", wiz8::OpenMode::replace);
        auto file = wiz8::open_file("data\\quotereport.txt", wiz8::OpenMode::replace);
        const auto today = wiz8::current_local_time();
        std::tm calendar{};
        calendar.tm_year = today.year - 1900;
        calendar.tm_mon = today.month - 1;
        calendar.tm_mday = today.day;
        calendar.tm_wday = today.day_of_week;
        date = fmt::format("{:%A, %B %d, %Y}", calendar);
        const auto write_report = [&](const std::string& text) { file->write(text.data(), text.size()); };
        g_status.quote_audit = 1;
        write_report(std::string("Script Report File: ") + date + "\n");
        write_report("-------------------------------------------------\n");
        for (const auto& name : wiz8::list_directory("Data\\NPC Scripts", "*.nsf")) {
            path = fmt::format("Data\\NPC Scripts\\{}", name);
            script = LoadNpcScriptFile(path);
            std::unique_ptr<W8NpcScriptFile, decltype(&ReleaseNpcScriptFile)> script_owner(script, &ReleaseNpcScriptFile);
            if (script != 0) {
                file_lines = 0;
                file_quotes = 0;
                for (record_index = 0; record_index < script->quote_count; ++record_index) {
                    quote = &script->quotes[record_index];
                    for (sub_index = 0; sub_index < quote->subquote_count; ++sub_index) {
                        text = quote->subquotes[sub_index];
                        if (text.size() != 0 && CompareTextIgnoreAsciiCase(text, "EMPTY") != 0 &&
                            CompareTextIgnoreAsciiCase(text, "BLANK") != 0 && CompareTextIgnoreAsciiCase(text, "UNKNOWN") != 0 &&
                            CompareTextIgnoreAsciiCase(text, "CLASSIFIED") != 0) {
                            display = fmt::format(" \"{}\"", text);
                            ShowNotice(W8_FONT_PALETTE_RED, display, 0, GetTextBoxScrollRange());
                            if (g_status.long_quote != 0) {
                                const std::string line = "Long Quote: #" + std::to_string(record_index) +
                                    ", subquote: #" + std::to_string(sub_index) + ", script file: " +
                                    name + " \n\n";
                                log_file->write(line.data(), line.size());
                            }
                            file_lines += g_notice_line_count;
                            total_lines += g_notice_line_count;
                            ++total_quotes;
                            ++file_quotes;
                        }
                    }
                }
                write_report("Script: " + name + "\n");
                write_report("  #quotes: " + std::to_string(file_quotes) + "\n  #lines: " +
                             std::to_string(file_lines) + "\n");
                ++total_scripts;
            }
        }
        write_report("Total lines: " + std::to_string(total_lines) + "\nTotal Quotes: " +
                     std::to_string(total_quotes) + "\nTotal Scripts: " + std::to_string(total_scripts));
        file->close();
        log_file->close();
        ShowNotice(W8_FONT_PALETTE_RED, "Quote test complete. See log file for results",
                   W8_NOTICE_TEXT_BOX_AUTOMATIC, GetTextBoxScrollRange());
    } catch (const std::exception&) {
        ShowNotice(W8_FONT_PALETTE_RED, "Quote test failed. Could not write report",
                   W8_NOTICE_TEXT_BOX_AUTOMATIC, GetTextBoxScrollRange());
    }
    g_status.quote_audit = 0;
}
/* Queue `text` as a floating portrait message: the string is copied so the
   queue owns it, and while the level-data flag is clear the message is
   bracketed by RESET_LEVEL_STATE markers so the dispatcher restores state
   around it. */
// FUNCTION: WIZ8 0x005299B0
void ShowString(std::string_view text)
{
    if (!IsLevelMovementStopped()) {
        QueueNpcMessageLine(W8_NPC_MSG_RESET_LEVEL_STATE, 1);
    }
    AddTextMessageBoxLine(W8_NPC_MSG_PORTRAIT_MESSAGE, std::string(text));
    if (!IsLevelMovementStopped()) {
        QueueNpcMessageLine(W8_NPC_MSG_RESET_LEVEL_STATE, 0);
    }
}
// FUNCTION: WIZ8 0x00529BC0
void SetScriptedSceneActive(void)
{
    g_npc_scripting.scripted_scene_active = true;
}
// FUNCTION: WIZ8 0x00529BD0
void ClearScriptedSceneActive(void)
{
    g_npc_scripting.scripted_scene_active = false;
}
// FUNCTION: WIZ8 0x0052A070
bool IsSedexusCaptureActive(void)
{
    return g_npc_scripting.sedexus_capture_active;
}
// FUNCTION: WIZ8 0x0052A1A0
void SetNpcScriptEventActive(unsigned char value)
{
    g_npc_script_event_active = value;
}
