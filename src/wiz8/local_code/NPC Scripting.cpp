#include <fmt/chrono.h> // IWYU pragma: keep - std::tm formatter specialization.
#include <fmt/format.h>
#include <fmt/printf.h>
#include <string_view>
#include <vector>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <cstdint>
#include <exception>
#include <utility>
#include <algorithm>
#include <variant>

#include "wiz8/unicode.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/conditions.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/fonts.h"
#include "wiz8/integer_constants.h"
#include "cursor.h"
#include "random.h"
#include "soundman.h"
#include "wiz8/layouts/character.h"
#include "wiz8/character_skills.h"
#include "wiz8/fact_state.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
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
#include "wiz8/local_screens/Screens.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/Ascension.h"
#include "wiz8/message_box.h"
#include "wiz8/location_variables.h"
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
#include "compat/native.h"
#include "layouts/item_instance.h"
#include "layouts/npc_dialogue.h"
#include "layouts/screen_state.h"
#include "layouts/targeting.h"
#include "local_code/Factions.h"
#include "local_code/MonsterManager.h"
#include "monster_actions.h"
#include "wiz8/mouth_gap.h"
#include "wiz8/npc_speech.h"
#include "surrender/srMath.h"
#include "audio_resources.h"

static void FinishNpcVoicePlayback(bool resume_script);
static void ProcessMessageBoxQueue(void);
static void ResolveSedexusCapture(void);
static void NpcScriptHenchmanArrives(W8Monster* monster);
static void NpcScriptHenchmanDeparted(W8Monster* monster);
static void NpcScriptSavantHackDone(W8Monster* monster);
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

static void ShowNpcScriptNoticeBubble(std::string_view text)
{
    SetNpcQuoteBubbleVisible(true, text, 0, -1, 0x47);
    g_npc_scripting.speech.voice_playing = false;
    g_npc_scripting.speech.voice_started_at = w8_get_ticks();
    HoldNpcSpeech(g_npc_scripting.speech, 2000);
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
static int g_empty_quote_text_index;

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
            unsigned int list_index = MonsterGetIndexByLocationID(
                0x315, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                monster->location_id, true);
            W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(list_index);
            if (monster_info != 0) {
                GetMonsterDataForInfo(monster_info);
            }
        }
    }
}

// FUNCTION: WIZ8 0x00524DA0
void UpdateNpcDialogueVoiceAndCursor(void)
{
    if (g_npc_scripting.portrait_message_active) {
        if (IsNpcSpeechHoldOver(g_npc_scripting.speech)) {
            FinishNpcVoicePlayback(true);
        }
    } else if (g_npc_scripting.session.quote_active) {
        if (!g_npc_scripting.speech.voice_playing && IsNpcSpeechHoldOver(g_npc_scripting.speech)) {
            FinishNpcVoicePlayback(true);
        }
        if (g_npc_scripting.speech.voice_playing && !g_npc_scripting.session.npc->is_grouped) {
            UpdateNpcMouth(g_npc_scripting.speech, GetNpcMonster(g_npc_scripting.session.npc));
        }
    }

    if (gXStatus.fNpcDialogueMode) {
        if (g_npc_interaction_state->dialogue_hidden != 0) {
            SetTargetCursor(1);
            return;
        }
        if (!g_npc_scripting.session.quote_active) {
            if (static_cast<int>(g_npc_scripting.message_lines.size()) < 1) {
                if (gXStatus.iCurrentCursor != W8_CURSOR_TEXT_INPUT) {
                    SetTargetCursor(W8_CURSOR_NONE);
                    return;
                }
            }
        } else if (!gXStatus.scripted_scene) {
            SetTargetCursor(W8_CURSOR_MAP_LOAD);
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

/* Completion callback armed on the NPC voice. A natural stream end finishes
   the quote but leaves the session active for a 700 ms hold. */
// FUNCTION: WIZ8 0x005251C0
static void NpcVoiceEosCallback()
{
    FinishNpcVoicePlayback(false);
    g_npc_scripting.session.quote_active = true;
    HoldNpcSpeech(g_npc_scripting.speech, 700);
}

/* Speak one subquote of the staged quote: build the voice path under
   Data\Sound\<PCs|NPCs>\<stem>\<stem>_<quote>[a..].wav, pick the display and
   plain text (EMPTY/BLANK/UNKNOWN/CLASSIFIED/SOUND are sentinels), start the
   sound with the EOS callback armed, and hang the mouth-gap track on the
   party slot's monster-manager entry or on g_npc_scripting for ungrouped
   NPCs. notice_only routes the text through ShowNotice instead. */
// FUNCTION: WIZ8 0x00525350
void SpeakNpcSubquote(W8NpcScriptQuote* quote, unsigned char subquote_index,
                      bool notice_only, bool force_npc_voice)
{
    W8MonsterManagerEntry* entry;
    W8Monster* monster;
    int quote_index;
    std::string voice_dir;
    std::string voice_path;
    std::string voice_stem;
    std::string display_text;
    std::string plain_text;
    std::string prefixed_text;

    static const int s_empty_quote_text_ids[2] = {1867, 1868};

    if (quote->dialogue_alert != 0) {
        voice_path = "Data\\Sound\\NPCs\\Dialogue Alert.wav";
        display_text = quote->subquotes[subquote_index];
        if (!notice_only) {
            g_npc_scripting.portrait_message_active = true;
            SetNpcQuoteBubbleVisible(true, display_text, 0, -1, -1);
            HoldNpcSpeech(g_npc_scripting.speech, wiz8::text::grapheme_count(display_text) * 60 + 2000);
            return;
        }
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, display_text, 0, GetTextBoxScrollRange());
        return;
    }

    if (g_npc_scripting.session.npc->is_grouped && !force_npc_voice) {
        voice_dir = "PCs";
    } else {
        voice_dir = "NPCs";
    }
    voice_stem = FormatNpcVoiceSoundPath(g_npc_scripting.session.npc);
    voice_path = fmt::format("Data\\Sound\\{}\\{}\\{}_{:03d}", voice_dir, voice_stem, voice_stem,
                             g_npc_scripting.session.current_quote_index);
    if (subquote_index > 0) {
        voice_path.push_back(static_cast<char>(subquote_index + 'a' - 1));
    }
    voice_path += ".wav";
    if (quote->subquotes.empty()) {
        display_text = fmt::format(" Missing quote #{:d} for {}.",
                                   g_npc_scripting.session.current_quote_index,
                                   g_npc_scripting.session.npc->record->source_name);
        plain_text = fmt::format(" Missing quote #{:d} for {}.",
                                 g_npc_scripting.session.current_quote_index,
                                 g_npc_scripting.session.npc->record->source_name);
    } else {
        display_text = fmt::format(" \"{}\"", quote->subquotes[subquote_index]);
        plain_text = quote->subquotes[subquote_index];
    }
    if (plain_text.size() == 0) {
        return;
    }
    if (!notice_only) {
        if (CompareTextIgnoreAsciiCase(plain_text, "EMPTY") == 0 ||
            CompareTextIgnoreAsciiCase(plain_text, "BLANK") == 0) {
            quote_index = g_npc_scripting.session.current_quote_index;
            if (quote_index != 0x67 && quote_index != 0x68 && quote_index != 0x69) {
                return;
            }
            plain_text =
                fmt::sprintf(g_localized_strings[s_empty_quote_text_ids[g_empty_quote_text_index]],
                             g_npc_scripting.session.npc->record->source_name);
            ++g_empty_quote_text_index;
            if (g_empty_quote_text_index == 2) {
                g_empty_quote_text_index = 0;
            }
            DisplayNpcQuote(plain_text, true);
            return;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "UNKNOWN") == 0) {
            quote_index = Random(100) < 50 ? 0x1c : 0x1d;
            QueueNpcScriptLine(quote_index, false, false, false);
            return;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "CLASSIFIED") == 0) {
            QueueNpcScriptLine(0x1e, false, false, false);
            return;
        }
        if (CompareTextIgnoreAsciiCase(plain_text, "SOUND") != 0 &&
            !g_npc_scripting.session.npc->is_grouped) {
            if (!gXStatus.fNpcDialogueMode && g_npc_scripting.session.npc->record->voice_script == 0) {
                prefixed_text =
                    fmt::format("{}: {}", g_npc_scripting.session.npc->record->source_name, display_text);
                display_text = prefixed_text;
            }
            SetNpcQuoteBubbleVisible(true, display_text, 0,
                                     g_npc_scripting.session.current_quote_index, -1, {},
                                     g_npc_scripting.session.npc->partner_index);
        }
        const bool voiced = PlayNpcVoice(g_npc_scripting.speech, voice_path, NpcVoiceEosCallback);
        monster = GetNpcMonster(g_npc_scripting.session.npc);
        if (monster != 0) {
            monster->StartTalking(true);
        }
        if (!voiced) {
            HoldNpcSpeech(g_npc_scripting.speech, wiz8::text::grapheme_count(display_text) * 60 + 2000);
            entry = GetNpcGroupEntry(g_npc_scripting.session.npc);
            if (entry != 0) {
                SetPartyPortraitEventState(g_npc_scripting.session.npc->group_index, true,
                                           g_npc_scripting.session.current_quote_index,
                                           display_text, 1);
                entry->voice_sound_handle = nullptr;
                entry->voice_time_remaining_ms = g_npc_scripting.speech.hold_ms;
            }
        } else {
            entry = GetNpcGroupEntry(g_npc_scripting.session.npc);
            if (entry != 0) {
                SetPartyPortraitEventState(g_npc_scripting.session.npc->group_index, true,
                                           g_npc_scripting.session.current_quote_index,
                                           display_text, 1);
                entry->voice_sound_handle = g_npc_scripting.speech.voice_handle;
                const auto playback_time =
                    wiz8::sound::playback_time(g_npc_scripting.speech.voice_handle)
                        .value_or(wiz8::sound::PlaybackTime{});
                entry->voice_time_remaining_ms = playback_time.total_ms;
                gXStatus.monster_manager_entries[g_npc_scripting.session.npc->group_index].mouth_gap =
                    LoadMouthGapTrack(voice_path);
                g_status.buffers.XChar[g_npc_scripting.session.npc->group_index].pending_event_type =
                    g_npc_scripting.session.current_quote_index;
                entry->pending_event_type = g_npc_scripting.session.current_quote_index;
                g_npc_interaction_state->last_notice_npc_kind = g_npc_scripting.session.npc->partner_index;
            } else {
                g_npc_scripting.speech.gap_track = LoadMouthGapTrack(voice_path);
            }
        }
        g_npc_scripting.session.quote_active = true;
        return;
    }
    prefixed_text = fmt::format("{}: {}", g_npc_scripting.session.npc->record->source_name, display_text);
    display_text = prefixed_text;
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, display_text, 0, GetTextBoxScrollRange());
}

// FUNCTION: WIZ8 0x00525C50
static void FinishNpcVoicePlayback(bool resume_script)
{
    if (g_npc_scripting.portrait_message_active) {
        g_npc_scripting.portrait_message_active = false;
        SetNpcQuoteBubbleVisible(false, {}, 0, -1, -1);
        g_npc_scripting.session.finished_quote_index =
            g_npc_scripting.session.current_quote_index;
        return;
    }
    if (g_npc_scripting.session.quote_active) {
        StopNpcVoice(g_npc_scripting.speech);
        W8Monster* monster = GetNpcMonster(g_npc_scripting.session.npc);
        if (monster != 0) {
            monster->StopTalking();
        }
        W8MonsterManagerEntry* entry = GetNpcGroupEntry(g_npc_scripting.session.npc);
        if (entry != 0) {
            if (entry->active_character_event == 0) {
                SetPartyPortraitEventState(g_npc_scripting.session.npc->group_index, false, -1, {}, 1);
            } else {
                gXStatus.character_event_queue->CompleteActiveEvent(entry->active_character_event);
            }
        }
        g_npc_scripting.session.quote_active = false;
        g_npc_scripting.speech.voice_handle = nullptr;
        g_npc_scripting.session.finished_quote_index =
            g_npc_scripting.session.current_quote_index;
        if (g_npc_scripting.reply == W8NpcReplyState::none && resume_script) {
            if (g_npc_scripting.session.npc != 0 && g_npc_scripting.session.npc->script_file != 0 &&
                g_npc_scripting.session.current_quote_index <
                    g_npc_scripting.session.npc->script_file->quote_count) {
                W8NpcScriptQuote* quotes = g_npc_scripting.session.npc->script_file->quotes.data();
                SetNpcQuoteBubbleVisible(false, {}, &quotes[g_npc_scripting.session.current_quote_index], g_npc_scripting.session.current_quote_index, -1);
                return;
            }
            SetNpcQuoteBubbleVisible(false, {}, 0, -1, -1);
        }
    }
}

/* Guarded finish entry point: suppressed outright while the level-4 gate
   flag is set; a non-forced call inside 500 ms of the last script tick is a
   no-op, so the tick path can poll without re-finishing. */
// FUNCTION: WIZ8 0x00525D90
void TryFinishNpcVoicePlayback(bool force)
{
    if (g_status.world_cursor_gate == 0 || g_status.current_level != 4) {
        if (!force && w8_get_ticks() - g_npc_scripting.speech.voice_started_at <= 500) {
            return;
        }
        FinishNpcVoicePlayback(true);
    }
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

static void ShowNpcPortraitMessage(std::string_view text, W8QuoteNotice notice = {})
{
    g_npc_scripting.portrait_message_active = true;
    SetNpcQuoteBubbleVisible(true, text, 0, -1, -1, std::move(notice), -1);
    HoldNpcSpeech(g_npc_scripting.speech, ComputePortraitMessageDuration(text));
}

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

/* The fade-completion callback NpcScriptTurnToBook schedules: while either
   queued endgame stage is pending - and stage three still finds PHOONZANG -
   ENDGAME2's script notice runs the book sequence. */
// FUNCTION: WIZ8 0x00526DF0
static void NpcScriptQueueEndgame(void)
{
    if (g_status.endgame2_queued != 0 ||
        (g_status.endgame3_queued != 0 && FindNpcOfKind(W8_NPC_PHOONZANG))) {
        W8NpcState* npc = GetNpcStateByKind(W8_NPC_ENDGAME2);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, -1, false, 0);
        }
    }
}

/* CameraPath4 swings the view to the ascension book; the endgame queue runs
   when the fade completes. */
// FUNCTION: WIZ8 0x00526E40
static void NpcScriptTurnToBook(void)
{
    UpdateCameraPathStateByName(GetWorld(), "CameraPath4", 1);
    BeginScreenFade(0, 1, 1000, NpcScriptQueueEndgame, false, 0);
}

// FUNCTION: WIZ8 0x00526E70
static void NpcScriptEndgameScreen(void)
{
    SetIntroVideoIndex(5);
    SetPendingScreenState(W8_SCREEN_INTRO);
}

static void SetMonsterGroupScript(int species, const char* script, int caller_line)
{
    W8MonsterGroup* group = FindFirstMonsterByID(species);
    if (group != 0) {
        unsigned int index = MonsterGetIndexByLocationID(
            caller_line, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
            group->leader_location_id, true);
        W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(index);
        monster_info->p3D->SetScript(script, true);
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
    npc = line.npc;
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
    switch (GetMessageBoxLineKind(line)) {
    case W8_NPC_MSG_BEGIN_ENDGAME:
        ClearMainGameTargetState();
        BeginEndgameSequence();
        break;
    case W8_NPC_MSG_TURN_TO_BOOK:
        BeginScreenFade(0, 0, 500, NpcScriptTurnToBook, true, 1);
        break;
    case W8_NPC_MSG_PHOONZANG_NOTICE:
        npc = GetNpcStateByKind(0x87);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, -1, false, 0);
        }
        break;
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
    case W8_NPC_MSG_MOVE_GARI: {
        EndNpcDialogueSession(false);
        BeginScriptedWorldAction();
        SetMonsterGroupScript(0x162, "MoveGari.msf", 0x78d);
        break;
    }
    case W8_NPC_MSG_DISPATCH_PENDING_NOTICE:
        DispatchPendingNpcScriptNotice();
        break;
    case W8_NPC_MSG_MOVE_RUBBLE: {
        EndNpcDialogueSession(false);
        BeginScriptedWorldAction();
        SetMonsterGroupScript(0x83, "MoveRubble.msf", 0x7a8);
        break;
    }
    case W8_NPC_MSG_MILANO_RAT_DOOR: {
        Trigger* door = FindRequiredTriggerByName("RatDoor02");
        door->CompleteItemInteraction();
        EndNpcDialogueSession(false);
        SetMonsterGroupScript(0xcf, "Milano.msf", 0x7c1);
        break;
    }
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
    case W8_NPC_MSG_SPAWN_ALFIE_CHAOS:
        SpawnAlfieChaos(0);
        break;
    case W8_NPC_MSG_SPAWN_ALFIE_LIFE:
        SpawnAlfieLife(0);
        break;
    case W8_NPC_MSG_SPAWN_ALFIE_KNOW:
        SpawnAlfieKnow(0);
        break;
    case W8_NPC_MSG_REMOVE_ALETHEIDES_AD: {
        EndNpcDialogueSession(false);
        npc = GetNpcStateByKind(0x2e);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0x8c2, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                monster_info->location_id, true);
            RemoveMonster(monster_index, true);
        }
        break;
    }
    case W8_NPC_MSG_REMOVE_ALETHEIDES_CM: {
        EndNpcDialogueSession(false);
        npc = GetNpcStateByKind(0x2d);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0x8dd, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                monster_info->location_id, true);
            RemoveMonster(monster_index, true);
        }
        break;
    }
    case W8_NPC_MSG_REMOVE_ALETHEIDES_DD: {
        EndNpcDialogueSession(false);
        npc = GetNpcStateByKind(0x2f);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0x8f7, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                monster_info->location_id, true);
            RemoveMonster(monster_index, true);
        }
        break;
    }
    case W8_NPC_MSG_MOVE_SAVANT: {
        EndNpcDialogueSession(false);
        ResetLevelDataVectors();
        SetMonsterGroupScript(0xc2, "MoveSavant.msf", 0x916);
        break;
    }
    case W8_NPC_MSG_ALETHEIDES_LEAVES:
        RemoveAletheides();
        break;
    case W8_NPC_MSG_MOOK_COMMENT: {
        unsigned int eligible = 0;
        int party_slot;
        for (party_slot = 2; party_slot < 8; ++party_slot) {
            if (g_status.buffers.XChar[party_slot].fOccupied &&
                g_status.buffers.Char[party_slot].highest_condition < W8_CONDITION_ASLEEP) {
                ++eligible;
            }
        }
        if (eligible > 1) {
            for (party_slot = 0; party_slot < 8; ++party_slot) {
                W8Character* character = &g_status.buffers.Char[party_slot];
                if (g_status.buffers.XChar[party_slot].fOccupied &&
                    character->iRace == W8_RACE_MOOK &&
                    character->highest_condition < W8_CONDITION_ASLEEP) {
                    QueueCharacterEvent(character, g_effect31, g_character_event_no_npc_defer,
                                        g_character_event_no_flags, g_character_event_full_volume);
                    break;
                }
            }
        }
        break;
    }
    case W8_NPC_MSG_PARTY_SLOT_EVENT_18: {
        int party_slot = argument;
        QueueCharacterEvent(&g_status.buffers.Char[party_slot], 0x18,
                            g_character_event_npc_script | g_character_event_no_npc_defer,
                            g_character_event_no_flags, g_character_event_full_volume);
        break;
    }
    case W8_NPC_MSG_SEDEXUS_PASSOUT:
        ResolveSedexusCapture();
        break;
    case W8_NPC_MSG_HENCHMAN_LEAVES: {
        EndNpcDialogueSession(false);
        W8MonsterGroup* group = FindFirstMonsterByID(0xdc);
        if (group != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0x968, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                group->leader_location_id, true);
            W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(monster_index);
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_ATTACK_SPECIAL,
                              W8_ANIMATION_PLAY_ONCE);
            monster_info->p3D->SetCycleCallback(0x12, NpcScriptHenchmanDeparted);
        }
        ClearMainGameTargetState();
        break;
    }
    case W8_NPC_MSG_PRINCE_DISAPPEARS: {
        EndNpcDialogueSession(false);
        W8MonsterGroup* group = FindFirstMonsterByID(0x1ab);
        if (group != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0x983, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                group->leader_location_id, true);
            W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(monster_index);
            monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        group = FindFirstMonsterByID(0x15d);
        if (group != 0) {
            SetMonsterGroupHostility(group, 1, false);
        }
        break;
    }
    case W8_NPC_MSG_PRINCE_NOT_HOME: {
        EndNpcDialogueSession(false);
        W8MonsterGroup* group = FindFirstMonsterByID(0x1aa);
        if (group != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0x9a0, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                group->leader_location_id, true);
            W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(monster_index);
            monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
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
    case W8_NPC_MSG_PILLARGATE_LURE: {
        RunNamedTrigger("pillargate05", -1);
        npc = GetNpcStateByKind(0x3f);
        W8MonsterInfo* monster_info = GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        break;
    }
    case W8_NPC_MSG_PILLARGATE_MADEUS: {
        RunNamedTrigger("pillargate04", -1);
        npc = GetNpcStateByKind(0x3e);
        W8MonsterInfo* monster_info = GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        break;
    }
    case W8_NPC_MSG_PILLARGATE_ASAIZ: {
        RunNamedTrigger("pillargate01", -1);
        npc = GetNpcStateByKind(0x3d);
        W8MonsterInfo* monster_info = GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        break;
    }
    case W8_NPC_MSG_BALBRAK_HOME: {
        npc = GetNpcStateByKind(0xc);
        W8MonsterInfo* monster_info = GetNpcMonsterInfo(npc);
        srVector3T<float> position;
        if (monster_info != 0 && FindEntityByName("NP_Balbrakhome", &position, 0, 0)) {
            monster_info->p3D->SetPosition(&position);
        }
        break;
    }
    case W8_NPC_MSG_CALL_HENCHMAN: {
        EndNpcDialogueSession(false);
        BeginScriptedWorldAction();
        W8MonsterGroup* group = FindFirstMonsterByID(0x112);
        if (group != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0xa3a, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                group->leader_location_id, true);
            W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(monster_index);
            monster_info->p3D->SetCycleCallback(0x12, NpcScriptHenchmanArrives);
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_ATTACK_SPECIAL,
                              W8_ANIMATION_PLAY_ONCE);
        }
        break;
    }
    case W8_NPC_MSG_TRAVEL_CONFIRM:
        ShowMainGameNoticeLine(g_localized_strings[0x7eb], OnNpcTravelConfirmationClosed, true, true);
        g_pending_npc_travel_level = argument;
        break;
    case W8_NPC_MSG_MOVE_TO_BOOK: {
        EndNpcDialogueSession(false);
        SetMonsterGroupScript(0x1b4, "belapath1.msf", 0xa57);
        Trigger* trigger = FindTriggerByName("CC_TRIGGERPLANE3");
        if (trigger != 0) {
            trigger->flags &= ~W8_TRIGGER_ON;
        }
        trigger = FindTriggerByName("CC_TRIGGERPLANE2");
        if (trigger != 0) {
            trigger->flags |= W8_TRIGGER_ON;
            trigger->Run(-1);
        }
        break;
    }
    case W8_NPC_MSG_MOVE_TO_BOOK2: {
        Trigger* trigger = FindTriggerByName("CC_TRIGGERPLANE3");
        if (trigger != 0) {
            trigger->flags |= W8_TRIGGER_ON;
            trigger->Run(-1);
        }
        npc = GetNpcStateByKind(0x8d);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, 7, false, 0);
        }
        break;
    }
    case W8_NPC_MSG_SAVANT_APPEARS: {
        srVector3T<float> position;
        if (FindEntityByName("NP_DS1", &position, 0, 0)) {
            W8MonsterGroup* group = SpawnMonsters(0x234, 1, &position, 0, true, false, false);
            int location_id = group->MemberLocationId(0);
            if (location_id != 0) {
                unsigned int monster_index = MonsterGetIndexByLocationID(
                    0xa9e, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp", location_id,
                    true);
                W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(monster_index);
                MonsterForwardReferencePosition(monster_info->p3D, 0);
            }
            npc = GetNpcStateByKind(0x84);
            if (npc != 0) {
                QueueNpcScriptNotice(npc, 0, -1, false, 0);
            }
        }
        break;
    }
    case W8_NPC_MSG_PHOONZANG_SPLIT: {
        npc = GetNpcStateByKind(0x84);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_SPECIAL, W8_ANIMATION_PLAY_ONCE);
        }
        srVector3T<float> position;
        W8MonsterGroup* group;
        if (FindEntityByName("NP_PHOONZANGLEE", &position, 0, 0) &&
            (group = FindFirstMonsterByID(0x197)) != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0xacf, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                group->leader_location_id, true);
            monster_info = MonsterGetScriptPartByLocationIndex(monster_index);
            monster_info->p3D->SetPosition(&position);
            MonsterForwardReferencePosition(monster_info->p3D, 0);
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_ATTACK_SPECIAL,
                              W8_ANIMATION_PLAY_ONCE);
        }
        npc = GetNpcStateByKind(0x8d);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, 0x12, false, 0);
        }
        break;
    }
    case W8_NPC_MSG_REMOVE_RPC_VI:
        if (NpcLeadHasNameStyle(W8_NPC_VI_DOMINA)) {
            npc = GetNpcStateByKind(0x18);
            if (npc != 0) {
                DismissNpcFromParty(npc->group_index, 0, true, false);
            }
            srVector3T<float> position;
            if (FindEntityByName("NP_VI1", &position, 0, 0)) {
                W8MonsterGroup* group = SpawnMonsters(0x1b9, 1, &position, 2, true, false, false);
                int location_id = group->MemberLocationId(0);
                if (location_id != 0) {
                    unsigned int monster_index = MonsterGetIndexByLocationID(
                        0xafc, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                        location_id, true);
                    W8MonsterInfo* monster_info =
                        MonsterGetScriptPartByLocationIndex(monster_index);
                    if (FindEntityByName("NP_DS1", &position, 0, 0)) {
                        monster_info->p3D->AimAtPosition(&position);
                    }
                }
            }
        }
        break;
    case W8_NPC_MSG_MOVE_GOLEM: {
        EndNpcDialogueSession(false);
        BeginScriptedWorldAction();
        SetMonsterGroupScript(0x13e, "MoveGolem.msf", 0xb1b);
        break;
    }
    case W8_NPC_MSG_SAVANT_HACK: {
        W8MonsterGroup* group = FindFirstMonsterByID(0x1b6);
        if (group != 0) {
            unsigned int monster_index = MonsterGetIndexByLocationID(
                0xb2e, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                group->leader_location_id, true);
            W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(monster_index);
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_SPELL, W8_ANIMATION_PLAY_ONCE);
            monster_info->p3D->SetCycleCallback(0x19, NpcScriptSavantHackDone);
        }
        break;
    }
    case W8_NPC_MSG_TRIGGER_FIX: {
        RunNamedTrigger("triggerFix", -1);
        break;
    }
    case W8_NPC_MSG_ENDGAME_SCREEN:
        BeginScreenFade(0, 0, 500, NpcScriptEndgameScreen, true, 1);
        break;
    case W8_NPC_MSG_MOVE_BELA: {
        EndNpcDialogueSession(false);
        ResetLevelDataVectors();
        SetMonsterGroupScript(0x18c, "MoveBela.msf", 0xb5e);
        srVector3T<float> position;
        if (FindEntityByName("NP_DSExit", &position, 0, 0)) {
            PointCameraAtTarget(&position, false, true);
        }
        break;
    }
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
    case W8_NPC_MSG_PATH2_TRIGGER: {
        RunNamedTrigger("Path2Trigger", -1);
        break;
    }
    case W8_NPC_MSG_REMOVE_SHAMAN: {
        EndNpcDialogueSession(false);
        npc = GetNpcStateByKind(0x4d);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            if (!monster_info->fActive) {
                unsigned int monster_index = MonsterGetIndexByLocationID(
                    0xba8, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                    monster_info->location_id, true);
                RemoveMonster(monster_index, true);
            } else {
                monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
            }
        }
        break;
    }
    case W8_NPC_MSG_REMOVE_JANETTE: {
        EndNpcDialogueSession(false);
        npc = GetNpcStateByKind(0x62);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            if (monster_info->fActive) {
                MonsterStartsDying(monster_info, true);
            } else {
                unsigned int monster_index = MonsterGetIndexByLocationID(
                    0xbbf, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                    monster_info->location_id, true);
                RemoveMonster(monster_index, true);
            }
        }
        break;
    }
    case W8_NPC_MSG_SEDEXUS_LEAVES: {
        EndNpcDialogueSession(false);
        SetTriggerVariableByName("LezboDemonAppeared", 0);
        npc = GetNpcStateByKind(0x40);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            if (monster_info->fActive) {
                monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
            } else {
                unsigned int monster_index = MonsterGetIndexByLocationID(
                    0xbd8, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                    monster_info->location_id, true);
                RemoveMonster(monster_index, true);
            }
        }
        break;
    }
    case W8_NPC_MSG_REMOVE_SEDEXUS_RIFT: {
        EndNpcDialogueSession(false);
        npc = GetNpcStateByKind(0x42);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            if (monster_info->fActive) {
                monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
            } else {
                unsigned int monster_index = MonsterGetIndexByLocationID(
                    0xbef, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                    monster_info->location_id, true);
                RemoveMonster(monster_index, true);
            }
        }
        break;
    }
    case W8_NPC_MSG_REMOVE_MARTEN: {
        EndNpcDialogueSession(false);
        npc = GetNpcStateByKind(100);
        W8MonsterInfo* monster_info = npc == 0 ? 0 : GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            if (!monster_info->fActive) {
                unsigned int monster_index = MonsterGetIndexByLocationID(
                    0xc06, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp",
                    monster_info->location_id, true);
                RemoveMonster(monster_index, true);
            } else if (monster_info->p3D != 0) {
                monster_info->p3D->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
            }
        }
        break;
    }
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

/* Repost the NPC's current quote bubble after a modal sub-dialog closes, or
   clear it when the dialogue NPC/script went away. */
// FUNCTION: WIZ8 0x00529510
void RestoreCurrentNpcQuoteBubble(void)
{
    W8NpcScriptFile* script_file;

    if (g_npc_scripting.session.npc != 0 && (script_file = g_npc_scripting.session.npc->script_file) != 0 &&
        g_npc_scripting.session.current_quote_index <
            static_cast<int>(script_file->quote_count)) {
        SetNpcQuoteBubbleVisible(false, {}, script_file->quotes.data() + g_npc_scripting.session.current_quote_index, g_npc_scripting.session.current_quote_index, 0xffffffff);
        return;
    }
    SetNpcQuoteBubbleVisible(false, {}, 0, -1, 0xffffffff);
}

/* Raise the quote bubble over `text` and, when `play_sound` is set, kick off
   the generic start-game click that accompanies a silent text notice. */
// FUNCTION: WIZ8 0x00529570
void DisplayNpcQuote(std::string_view text, bool play_sound)
{
    std::string sound_path;

    ShowNpcScriptNoticeBubble(text);
    g_npc_scripting.session.quote_active = true;
    if (play_sound) {
        sound_path = "Data\\Sound\\misc\\startgame.wav";
        wiz8::sound::play(sound_path);
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
/* Fact 0x1bf: raise the scripted-scene gate, drop the level's transient data
   vectors, force the single-target mode, and reopen the party-member region
   sets (7 + slot, member region 0x5a + slot) for every occupied slot. */
// FUNCTION: WIZ8 0x00529BE0
void BeginNpcScriptedScene(void)
{
    int party_slot;

    SetScriptedSceneActive();
    ResetLevelDataVectors();
    gXStatus.scripted_scene = true;
    SetTargetingMode(W8_TARGET_NEED_ALLY);
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (g_status.buffers.XChar[party_slot].fOccupied) {
            RegionSetEnable(party_slot + 7);
            EnableRegionSetInput(party_slot + 7);
            EnableRegionInput(party_slot + 0x5a);
        }
    }
}
/* The scripted-scene wind-down: clears the targeting/level state raised by
   BeginNpcScriptedScene, resets the script facts, then classifies the picked
   character. A female pick with any non-female party member present raises
   fact 0x1c0 and queues the special event; an unanimated pick is further
   tested for the three 0x1fd-0x1ff items (success leaves fact 0x1c1 at zero
   and fills the alternate-name display), and anything else falls back to
   facts 0x1c1 or 0x227. The party-member region sets reopen on the way out. */
// FUNCTION: WIZ8 0x00529C40
void EndScriptedPortraitPick(int party_slot)
{
    W8ItemInstance* found;
    W8Character* character;
    /* 0x00529EDC compares the slot counter as slot - 7 against 8 with a
       signed jump, so the counter is not unsigned. */
    int slot;
    bool other_gender_present;

    if (!gXStatus.scripted_scene) {
        return;
    }
    if (!gXStatus.fCombatMode || gXStatus.fPartyMovementMode) {
        ClearLevelMovementStopped();
    }
    SetTargetingMode(W8_TARGET_NEED_NONE);
    for (slot = 0; slot < 8; ++slot) {
        if (g_status.buffers.XChar[slot].fOccupied) {
            RegionSetDisable(slot + 7);
            DisableRegionSetInput(slot + 7);
            DisableRegionInput(slot + 0x5a);
        }
    }
    ClearScriptedSceneActive();
    gXStatus.scripted_scene = false;
    other_gender_present = false;
    SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_WOMAN, 0, false);
    SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_ALL_WOMAN, 0, false);
    SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_DRESSED, 0, false);
    SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_ABLE, 0, false);
    for (slot = 0; slot < 8; ++slot) {
        if (g_status.buffers.XChar[slot].fOccupied &&
            g_status.buffers.Char[slot].gender != W8_GENDER_FEMALE) {
            other_gender_present = true;
            break;
        }
    }
    character = &g_status.buffers.Char[party_slot];
    if (character->gender == W8_GENDER_FEMALE && other_gender_present) {
        SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_WOMAN, 1, false);
        QueueCharacterEvent(character, g_effect29, g_character_event_no_npc_defer,
                            g_character_event_no_flags, g_character_event_full_volume);
        return;
    }
    if (g_status.buffers.XChar[party_slot].npc_index == -1 &&
        character->highest_condition < W8_CONDITION_ASLEEP) {
        if (FindItemOnCharacter(character, 0x1fd, &found, 0, 0) &&
            FindItemOnCharacter(character, 0x1fe, &found, 0, 0) &&
            FindItemOnCharacter(character, 0x1ff, &found, 0, 0)) {
            SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_DRESSED, 0, false);
            g_status.monster_name_buffer = fmt::format("Al-{}", character->name);
            g_status.sedexus_party_slot = party_slot;
            g_status.rpc_active = true;
            g_status.infatuation_pending = true;
            QueueCharacterEvent(character, g_special_event2, 0, g_character_event_no_flags,
                                g_character_event_full_volume);
        } else {
            SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_DRESSED, 1, false);
        }
    } else {
        SetFact(W8_FACT_ALSEDEXUS_SACRIFICE_NOT_ABLE, 1, false);
    }
    for (slot = 0; slot < 8; ++slot) {
        if (g_status.buffers.XChar[slot].fOccupied) {
            RegionSetEnable(slot + 7);
            EnableRegionSetInput(slot + 7);
            EnableRegionInput(slot + 0x5a);
        }
    }
}
/* Al-Sedexus takes her pick: the scripted scene opens, the lighting fades,
   and every still-living occupied slot other than the selected character is
   put under condition 0x11. */
// FUNCTION: WIZ8 0x00529EF0
void BeginSedexusCapture(void)
{
    unsigned int party_slot;

    SetScriptedSceneActive();
    BeginScriptedWorldAction();
    g_npc_scripting.sedexus_capture_pending = true;
    g_npc_scripting.sedexus_capture_active = true;
    BeginWorldLightingFade(-1000.0f);
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (g_status.buffers.XChar[party_slot].fOccupied &&
            (g_status.buffers.Char[party_slot].hp_current > 0 ||
             g_status.buffers.Char[party_slot].highest_condition < W8_CONDITION_DEAD) &&
            party_slot != static_cast<unsigned int>(g_status.sedexus_party_slot)) {
            SetCharacterCondition(party_slot, W8_CONDITION_UNCONSCIOUS, W8_CONDITION_INDEFINITE, 0,
                                  0, 0);
        }
    }
}
/* The capture resolves into the templar spawn: the two NPC kinds are
   released, the LezboDemonAppeared trigger variable drops, monster 0xd8 is
   spawned on np_al-adryian51 and given proximitytemplar.msf, and the two
   gate triggers fire while the lighting fades back up. */
// FUNCTION: WIZ8 0x00529F90
static void ResolveSedexusCapture(void)
{
    W8MonsterGroup* group;
    srVector3T<float> position;
    int location_id;
    unsigned int monster_list_index;
    W8MonsterInfo* info;

    ReleaseNpcMonsterByKind(0x3b);
    ReleaseNpcMonsterByKind(0x40);
    SetTriggerVariableByName("LezboDemonAppeared", 0);
    if (FindEntityByName("np_al-adryian51", &position, 0, 0)) {
        group = SpawnMonsters(0xd8, 1, &position, 2, true, false, false);
        location_id = group->MemberLocationId(0);
        if (location_id != 0) {
            monster_list_index = MonsterGetIndexByLocationID(
                0x10e0, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp", location_id,
                true);
            info = MonsterGetScriptPartByLocationIndex(monster_list_index);
            if (info != 0) {
                info->p3D->SetScript("proximitytemplar.msf", true);
            }
        }
    }
    SetScriptedSceneActive();
    g_npc_scripting.sedexus_release_pending = true;
    BeginWorldLightingFade(1000.0f);
    RunNamedTrigger("al-seduxusgate", -1);
    RunNamedTrigger("templargate14", -1);
}
// FUNCTION: WIZ8 0x0052A070
bool IsSedexusCaptureActive(void)
{
    return g_npc_scripting.sedexus_capture_active;
}
/* Cycle-0x12 arrival callback: the henchmen group spawns on the NP_HENCHMEN
   waypoint and its first member's bound NPC gets the script notice; the
   callback monster itself is parked at the origin with its position flagged
   dirty either way. */
// FUNCTION: WIZ8 0x0052A080
static void NpcScriptHenchmanArrives(W8Monster* monster)
{
    srVector3T<float> position;
    srVector3T<float> origin;

    if (FindEntityByName("NP_HENCHMEN", &position, 0, 0)) {
        PointCameraAtTarget(&position, false, true);
        W8MonsterGroup* group = SpawnMonsters(0xdc, 1, &position, 2, true, false, false);
        int location_id = group->MemberLocationId(0);
        if (location_id != 0) {
            W8MonsterInfo* monster_info =
                MonsterGetScriptPartByLocationIndex(MonsterGetIndexByLocationID(
                    0x111f, "C:\\Projects\\Wizardry 8\\Local Code\\NPC Scripting.cpp", location_id,
                    true));
            if (monster_info != 0) {
                W8NpcState* npc = GetNpcStateForMonsterInfo(monster_info, false);
                if (npc != 0) {
                    QueueNpcScriptNotice(npc, 0, -1, false, 0);
                }
            }
        }
    }
    origin.SetZero();
    monster->SetPosition(&origin);
    monster->runtime_flags |= W8_MONSTER_PARKED;
}

/* Cycle-0x12 departure callback: park the monster at the origin and flag its
   position dirty. */
// FUNCTION: WIZ8 0x0052A150
static void NpcScriptHenchmanDeparted(W8Monster* monster)
{
    srVector3T<float> origin;

    monster->runtime_flags |= W8_MONSTER_PARKED;
    origin.SetZero();
    monster->SetPosition(&origin);
}

/* Cycle-0x19 completion callback: stamping savant_hack_tick lets
   UpdateNpcEvents retire NPC group 0x1b3 fifty ticks later. */
// FUNCTION: WIZ8 0x0052A190
static void NpcScriptSavantHackDone(W8Monster* monster)
{
    g_status.savant_hack_tick = int(uint32_t(w8_get_ticks()));
}

// FUNCTION: WIZ8 0x0052A1A0
void SetNpcScriptEventActive(unsigned char value)
{
    g_npc_script_event_active = value;
}
