#include <algorithm>
#include <ctime>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <fmt/chrono.h> // IWYU pragma: keep - std::tm formatter specialization.
#include <fmt/format.h>
#include <fmt/printf.h>

#include "asset_paths.h"
#include "monster_actions.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/file_time.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/npc_dialogue_view.h"
#include "wiz8/local_code/npc_story_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/npc_speech.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/story.h"
#include "wiz8/unicode.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

constexpr unsigned int kCharacterEventNpcScript = 0x40;

class W8DialogBase;

struct W8NpcScriptRegionName {
    std::string name;
    int region;
};

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

void ApplyScriptedDialogueKeyword(const ScriptContext& context, W8NpcQuoteEntryKind kind,
                                  const char* text)
{
    W8DialogueCategory category;
    switch (kind) {
    case W8_NPC_ENTRY_PERSON_KEYWORD:
    case W8_NPC_ENTRY_PERSON_KEYWORD_ALT:
        category = W8_DIALOGUE_CATEGORY_PEOPLE;
        break;
    case W8_NPC_ENTRY_ITEM_KEYWORD:
    case W8_NPC_ENTRY_ITEM_KEYWORD_ALT:
        category = W8_DIALOGUE_CATEGORY_ITEMS;
        break;
    case W8_NPC_ENTRY_PLACE_KEYWORD:
    case W8_NPC_ENTRY_PLACE_KEYWORD_ALT:
        category = W8_DIALOGUE_CATEGORY_PLACES;
        break;
    default:
        category = W8_DIALOGUE_CATEGORY_MISC;
        break;
    }
    if (g_settings.simplified_npc_interaction) {
        std::string keyword_text;
        keyword_text = text;
        if (!context.activity.conversation || !CurrentGame().npc_interaction.scripted_dialogue) {
            LearnDialogueKeyword(keyword_text, category, true);
        } else {
            LearnDialogueKeyword(keyword_text, category, false);
        }
    }
}

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
std::optional<W8NpcScriptItemQuote> FindNpcScriptItemQuote(int item_id)
{
    const W8NpcScriptFile* script = CurrentGame().npc_dialogue.binding().script.get();
    int entry_index;
    int quote_index;

    for (quote_index = 0; quote_index < std::ssize(script->quotes); ++quote_index) {
        const W8NpcScriptQuote* quote = script->quotes.data() + quote_index;
        for (entry_index = 0; entry_index < std::ssize(quote->entries); ++entry_index) {
            const W8NpcQuoteEntry* entry = quote->entries.data() + entry_index;
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
            band = CurrentGame().levelBand(GetLoadedLevelID());
            return g_npc_region_quotes[g_npc_script_region_names[index].region][band];
        }
    }
    if (CompareTextIgnoreAsciiCase(text, "Mt Gigas") == 0 ||
        CompareTextIgnoreAsciiCase(text, "Gigas") == 0) {
        band = CurrentGame().levelBand(GetLoadedLevelID());
        return g_npc_region_quotes[11][band];
    }
    if (CompareTextIgnoreAsciiCase(text, "the swamp") == 0) {
        band = CurrentGame().levelBand(GetLoadedLevelID());
        return g_npc_region_quotes[4][band];
    }
    if (CompareTextIgnoreAsciiCase(text, "the Monastery") != 0) {
        return -1;
    }
    band = CurrentGame().levelBand(GetLoadedLevelID());
    return g_npc_region_quotes[1][band];
}

static void ShowConsumedItemQuote(Game& game, std::string_view text)
{
    if (game.npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
        RebuildNpcTradeItemList(false);
        return;
    }
    DisplayNpcQuote(game, text, true);
    return;
}

/* Remove one stack unit of the matching item: the character backpacks first
   (the slot whose address equals `item` wins, or any slot carrying `item_id`
   when `match_item_id` is set), then the party pool. The first hit announces
   the loss through the quote bubble or the layout-4 path and returns; failing
   those, one unit of a matching offered item is used up. */
void RemoveNpcScriptItem(const ScriptContext& context, const W8ItemInstance* item,
                         int match_item_id, int item_id)
{
    std::string text;
    unsigned int slot;
    unsigned int index;
    const W8ItemInstance* slot_item;

    for (slot = 0; slot < 8; ++slot) {
        if (!context.party.members[slot].has_value()) {
            continue;
        }
        for (index = 0; index < 8; ++index) {
            slot_item = &context.party.characters[slot].backpack[index];
            if (slot_item->iItemNo != -1 &&
                (slot_item == item || (match_item_id != 0 && slot_item->iItemNo == item_id))) {
                context.inventory.removeQuantity({W8ItemStorage::Backpack, slot, index}, 1);
                text = fmt::sprintf(Data().strings[0x7ec], context.party.characters[slot].name);
                ShowConsumedItemQuote(context.game, text);
                return;
            }
        }
    }
    for (slot = 0; slot < context.inventory.pool().size(); ++slot) {
        slot_item = &context.inventory.pool()[slot];
        if (slot_item->iItemNo != -1 &&
            (slot_item == item || (match_item_id != 0 && slot_item->iItemNo == item_id))) {
            context.inventory.removeQuantity({W8ItemStorage::Pool, 0, slot}, 1);
            text = Data().strings[0x7ed];
            ShowConsumedItemQuote(context.game, text);
            return;
        }
    }
    auto& offer = CurrentGame().npc_interaction.offered_item;
    if (offer && item != 0 && offer->iItemNo == item->iItemNo) {
        if (offer->stack_count != 0) {
            --offer->stack_count;
        }
        if (offer->stack_count == 0) {
            offer.reset();
        }
    }
}

void StripNpcKeywordPunctuation(std::string& text)
{
    std::erase_if(text, [](char c) {
        return std::string_view("!?@#$,.\":").find(c) != std::string_view::npos;
    });
}

/* Alternates between the two string-list ids SpeakNpcSubquote substitutes for
   an "EMPTY"/"BLANK" quote on the closing quotes it allows. */

std::string FormatNpcVoiceSoundPath(const Story& story, W8NpcState* npc, bool script_event)
{
    const auto prefix = npc->is_grouped && !script_event ? "RPC_"
                        : npc->record->voice_script != 0 ? "VOC_"
                                                         : "NPC_";
    return fmt::format("{}{}", prefix, story.npcDisplayName(*npc));
}

std::unique_ptr<W8NpcScriptFile> LoadNpcScriptResources(const Story& story, W8NpcState& npc,
                                                        Monster* monster)
{
    const std::string script_name = FormatNpcVoiceSoundPath(story, &npc);
    auto script = LoadNpcScriptFile(fmt::format("Data\\NPC Scripts\\{}.nsf", script_name));
    if (script != 0 && monster != 0) {
        monster->SetScript(fmt::format("{}.msf", script_name).c_str(), true);
    }
    return script;
}

void ReloadNpcScriptResources(W8NpcState* npc)
{
    npc->script_file = LoadNpcScriptResources(CurrentGame().story, *npc, GetNpcMonster(npc));
}

void W8NpcDialogue::Advance(const ScriptContext& context)
{
    if (is_held_by_scene() || has_active_utterance()) {
        return;
    }
    if (reply_ != W8NpcReplyState::dialog_open) {
        RunNextAction(context);
        if (has_active_utterance()) {
            return;
        }
    }
    if (!queue_.empty()) {
        return;
    }
    if (suspended_) {
        session_ = *suspended_;
        suspended_.reset();
    }
    if (context.activity.conversation && context.status.world_cursor_gate == 0 &&
        !context.activity.scripted_scene && reply_ == W8NpcReplyState::none &&
        (context.activity.conversation && context.game.npc_interaction.scripted_dialogue)) {
        EndNpcDialogueSession(false);
    }
    if (reply_ == W8NpcReplyState::none && context.game.npc_interaction.dialogue_panel_hidden &&
        context.game.speech.IsMainQueueEmpty()) {
        SetNpcDialoguePanelVisible(1);
    }
}

void W8NpcDialogue::Begin(std::shared_ptr<W8NpcState> npc, W8NpcScriptBinding binding,
                          bool suspend_current)
{
    if (suspend_current) suspended_ = session_;
    session_.identity = ++session_identity_;
    session_.finished_quote_index = -1;
    session_.npc = std::move(npc);
    session_.binding = std::move(binding);
}

bool W8NpcDialogue::DefersCharacterEvents(bool require_group_entry) const
{
    if (is_held_by_scene()) {
        return false;
    }
    if (!has_active_utterance() && !has_queued_actions()) {
        return false;
    }
    W8NpcState* speaker = this->speaker();
    if (speaker == 0) {
        return false;
    }
    if (speaker->record->has_group && speaker->character && speaker->is_grouped &&
        !require_group_entry) {
        return false;
    }
    return true;
}

/* Execute a queued quote entry's deferred effect: switch the dialogue to the
   named NPC (suspending the current session when the target is grouped), hand
   items or gold to the party with the notice bubble and chime, or open the
   modal dialog kinds. The party slot's pending portrait event clears once the
   entry has run. */
void W8NpcDialogue::RunQuoteEntry(const ScriptContext& context, const W8NpcQuoteEntry* entry,
                                  int continuation_quote)
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
                    session_.subquote_index = 0;
                }
                if (!target->is_grouped) {
                    SelectNpcDialogueSpeaker(target, 0);
                    if (GetLoadedLevelID() != 4 || context.status.world_cursor_gate == 0) {
                        LookAtDialogueNpc();
                    }
                } else {
                    Begin(target, true);
                }
                QueueQuote(entry->operand0, false, false, false);
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
                    if (GetLoadedLevelID() != 4 || context.status.world_cursor_gate == 0) {
                        LookAtDialogueNpc();
                    }
                } else {
                    Begin(target, true);
                    SetNpcDialoguePanelVisible(0);
                }
                QueueQuote(entry->operand0, false, false, false);
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
        OpenNpcDialog(session_.binding.script, entry, continuation_quote);
        break;
    case W8_NPC_ENTRY_GIVE_ITEM:
        item = MakeItem(Data(), context.game.rng, entry->operand0, true, true, false);
        notice_text = fmt::sprintf(Data().strings[0x7ea], GetItemDisplayName(Data(), &item));
        if (!context.activity.conversation && !context.inventory.held()) {
            context.inventory.hold(item);
        } else {
            context.inventory.storeOrDrop(item, false);
            if (item.iItemNo != -1) break;
            if (context.activity.conversation) DisplayNpcQuote(context.game, notice_text, true);
        }
        ClearNpcItemId(session_.npc.get(), entry->operand0);
        if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX) {
            RebuildNpcTradeItemList(false);
        }
        break;
    case W8_NPC_ENTRY_REMOVE_ITEM:
        RemoveNpcScriptItem(context, 0, 1, entry->operand0);
        break;
    case W8_NPC_ENTRY_GIVE_GOLD:
        notice_text =
            fmt::sprintf(Data().strings[0x7e9], session_.npc->record->source_name, entry->operand0);
        AddPartyGold(context.game, entry->operand0, false);
        DisplayNpcQuote(context.game, notice_text, true);
        break;
    case W8_NPC_ENTRY_GIVE_EXPERIENCE:
        AwardPartyExperience(entry->operand0, false);
        break;
    default:
        break;
    }
    if (session_.npc->character.has_value() && session_.npc->group_index != -1) {
        context.party.rows[session_.npc->group_index].pending_event_type = 0;
    }
}

void W8NpcDialogue::RunNextAction(const ScriptContext& context)
{
    W8NpcState* npc;
    int index;

    if (queue_.empty()) {
        if (!queue_idle_) {
            FlushPendingNoticeLines();
        }
        queue_idle_ = true;
        return;
    }

    auto current = queue_.begin();
    W8MessageBoxLine& line = *current;
    npc = line.npc_kind != -1 ? CurrentGame().story.npcOfKind(line.npc_kind) : 0;
    if (auto* quote_line = std::get_if<W8NpcQueuedQuote>(&line.action)) {
        if (npc != 0 && npc != session_.npc.get()) {
            Begin(npc, true);
        }
        if (session_.binding.script != quote_line->binding.script)
            session_.finished_quote_index = -1;
        session_.binding = quote_line->binding;
        if (quote_line->suppress_entries) {
            session_.subquote_index = quote_line->subquote_index;
            session_.finished_quote_index = quote_line->quote_index;
        }
        if (queue_idle_) {
            queue_idle_ = false;
            if (session_.npc->record->voice_script == 0) {
                for (const W8MessageBoxLine& queued_line : queue_) {
                    const auto* queued = std::get_if<W8NpcQueuedQuote>(&queued_line.action);
                    if (queued == 0 || !queued->binding.script || queued->quote_index < 0 ||
                        queued->quote_index >= std::ssize(queued->binding.script->quotes) ||
                        queued->suppress_entries ||
                        std::ranges::find(pending_quotes_, queued->quote_index) ==
                            pending_quotes_.end()) {
                        continue;
                    }
                    if (session_.npc->record->merchant == 0 && queued->quote_index != 0x76) {
                        const W8NpcScriptQuote& quote =
                            queued->binding.script->quotes[queued->quote_index];
                        if (std::ranges::none_of(quote.entries, [](const W8NpcQuoteEntry& entry) {
                                return entry.kind == W8_NPC_ENTRY_UNNAMED_29;
                            })) {
                            Speak(context, 0x1f, false);
                            return;
                        }
                    }
                }
            }
            pending_quotes_.clear();
        }

        const int quote_index = quote_line->quote_index;
        if (quote_line->mark_pending) {
            pending_quotes_.push_back(quote_index);
        }

        const W8NpcScriptFile* script = session_.binding.script.get();
        if (script != 0 && !quote_line->suppress_entries && quote_index >= 0 &&
            quote_index < std::ssize(script->quotes)) {
            const W8NpcScriptQuote* quote = &script->quotes[quote_index];
            for (index = 0; index < std::ssize(quote->entries); ++index) {
                W8NpcQuoteEntryKind kind = quote->entries[index].kind;
                if ((kind == W8_NPC_ENTRY_PRICE_CHECK || kind == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK) &&
                    (kind == W8_NPC_ENTRY_ALWAYS_PRICE_CHECK ||
                     !NpcKnowsFact(session_.npc.get(), static_cast<W8FactId>(quote_index)))) {
                    Speak(context, 0x12, false);
                    reply_ = W8NpcReplyState::pending;
                    QueueQuoteEntry(quote_index, index, quote_index, true);
                    queue_.erase(current);
                    return;
                }
            }
        }
        if (script != 0) {
            Speak(context, quote_index, false);
        }
        queue_.erase(current);
        return;
    }

    struct RunAction {
        W8NpcDialogue& dialogue;
        const ScriptContext& context;
        W8NpcState* npc;

        void operator()(const W8NpcQueuedQuote&) const {}
        void operator()(const W8NpcQueuedEntry& queued) const
        {
            const auto& binding = queued.binding;
            const W8NpcScriptFile* script = binding.script.get();
            if (npc && npc != dialogue.speaker()) context.game.beginNpcDialogue(npc, true);
            if (dialogue.session_.binding.script != binding.script)
                dialogue.session_.finished_quote_index = -1;
            dialogue.session_.binding = binding;
            dialogue.session_.current_quote_index = queued.quote_index;
            if (script != 0 && queued.quote_index >= 0 &&
                queued.quote_index < std::ssize(script->quotes) && queued.entry_index >= 0 &&
                queued.entry_index <
                    static_cast<int>(script->quotes[queued.quote_index].entries.size())) {
                dialogue.RunQuoteEntry(
                    context, &script->quotes[queued.quote_index].entries[queued.entry_index],
                    queued.continuation_quote);
            }
        }
        void operator()(const W8NpcQueuedNotice& message) const
        {
            if (const auto* skills = std::get_if<W8SkillNoticePayload>(&message.notice)) {
                if (!g_settings.skill_increase_messages) {
                    PostSkillIncreaseNotices(context.game, *skills);
                    return;
                }
            } else if (const auto* level_up = std::get_if<W8LevelUpNotice>(&message.notice)) {
                if (auto& member = context.party.members[level_up->party_slot])
                    member->level_up_announced = true;
                if (!g_settings.skill_increase_messages) {
                    wiz8::sound::play("Data\\Sound\\Misc\\GainLevel.wav");
                    return;
                }
            }
            ShowNpcPortraitMessage(context.game, message.text, message.notice);
        }
        void operator()(W8NpcStoryAction action) const { RunNpcStoryAction(context, action); }
        void operator()(W8NpcCommand command) const
        {
            switch (command) {
            case W8NpcCommand::close_dialogue:
                CloseNpcDialogueIfActive();
                if (context.world != nullptr && context.world->script) {
                    context.world->script->npc_dialogue_closed = true;
                }
                break;
            case W8NpcCommand::close_and_resume_npc:
                CloseNpcDialogueIfActive();
                if (CurrentGame().npc_interaction.dialogue_npc != 0) {
                    ResumeNpc(CurrentGame().npc_interaction.dialogue_npc.get(), 1);
                }
                break;
            case W8NpcCommand::reopen_transcript:
                CloseNpcDialogueLayout();
                OpenNpcDialogueTranscriptLayout();
                break;
            case W8NpcCommand::journal_quote:
                DisplayNpcQuote(context.game, context.game.data.strings[0x74a], false);
                wiz8::sound::play("Data\\Sound\\Misc\\Journal Entry.wav");
                break;
            case W8NpcCommand::dispatch_pending_notice:
                DispatchPendingNpcScriptNotice();
                break;
            case W8NpcCommand::remove_speaking_npc: {
                EndNpcDialogueSession(false);
                Monster* monster = GetNpcMonster(dialogue.session_.npc.get());
                if (monster != 0) {
                    monster->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
                }
                break;
            }
            case W8NpcCommand::clear_target_state:
                ClearMainGameTargetState();
                break;
            case W8NpcCommand::begin_scripted_world_action:
                BeginScriptedWorldAction();
                break;
            case W8NpcCommand::reset_level_data_vectors:
                if (!context.combat.has_value() || context.game.party_movement_mode) {
                    ResetLevelDataVectors();
                }
                break;
            case W8NpcCommand::clear_level_movement_stopped:
                if (!context.combat.has_value() || context.game.party_movement_mode) {
                    ClearLevelMovementStopped();
                }
                break;
            case W8NpcCommand::spacer:
                break;
            }
        }
        void operator()(W8NpcRecruit recruit) const
        {
            if (W8NpcState* recruited = CurrentGame().story.npcOfKind(recruit.npc_kind)) {
                RecruitNpcIntoParty(recruited);
            }
            EndNpcDialogueSession(false);
            dialogue.QueueQuote(0, false, false, false);
        }
        void operator()(W8NpcDismiss dismiss) const
        {
            ClearMainGameTargetState();
            DismissNpcFromParty(dismiss.party_slot, 0, false, false);
            if (CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT) {
                for (int party_slot = 0; party_slot < 8; ++party_slot) {
                    if (context.party.members[party_slot].has_value()) {
                        DisableMainGamePortraitSlot(party_slot);
                    }
                }
            }
        }
        void operator()(W8NpcPartySpeakerEvent event) const
        {
            int party_slot =
                PickRandomPartySpeaker(event.event_type, context.status.selected_party_member);
            if (party_slot != -1) {
                context.status.selected_party_member = static_cast<unsigned char>(party_slot);
                QueueCharacterEvent(&context.party.characters[party_slot], event.event_type,
                                    kCharacterEventNoNpcDefer, kCharacterEventFullVolume);
                SetNpcDialoguePanelVisible(0);
                if (CurrentGame().npc_interaction.modal_dialog_open) {
                    CurrentGame().npc_interaction.suppress_parting_reaction = true;
                }
            }
        }
        void operator()(W8NpcLevelUpReaction reaction) const
        {
            QueueCharacterEvent(&context.party.characters[reaction.party_slot], 1,
                                kCharacterEventNoNpcDefer, kCharacterEventFullVolume);
        }
        void operator()(W8NpcPartyScriptLine line) const
        {
            QueueCharacterEvent(&context.party.characters[line.party_slot], 0x18,
                                kCharacterEventNpcScript | kCharacterEventNoNpcDefer,
                                kCharacterEventFullVolume);
        }
        void operator()(W8NpcPortraitString message) const
        {
            ShowNpcPortraitMessage(context.game, Data().strings[message.string_index]);
        }
        void operator()(W8NpcMarkMissing missing) const
        {
            context.status.skip_next_condition_reaction = true;
            InflictCondition(CurrentGame(), missing.party_slot,
                             {.condition = W8_CONDITION_MISSING,
                              .duration = W8_CONDITION_INDEFINITE,
                              .announce = false});
            context.status.condition13_clock = 1;
            context.status.condition13_stamp = context.status.world_clock;
            context.status.pending_condition_party_slot = missing.party_slot;
        }
        void operator()(W8NpcLeaveCombat leave) const
        {
            if (!context.combat.has_value()) {
                return;
            }
            const int party_slot = leave.party_slot;
            context.combat->endTurnOf(PartyActor{party_slot});
            ClearMainGameTargetState();
            DismissNpcFromParty(party_slot, 0, false, true);
            context.game.invalidateAimsAt(W8CombatSlot::character(party_slot),
                                          TargetChange::removed);
            context.combat->npc_combat_script_pending[party_slot] = false;
        }
        void operator()(W8NpcTravelConfirm travel) const
        {
            ShowMainGameNoticeLine(
                Data().strings[0x7eb],
                [level = travel.level](bool accepted) {
                    if (accepted) {
                        QueueNpcTravelRefusals(level);
                    }
                },
                true, true);
        }
    };
    std::visit(RunAction{*this, context, npc}, line.action);

    queue_.erase(current);
}

/* QA audit over every `Data\NPC Scripts\*.nsf`: load each script file, print
   every quote line that is not a placeholder sentinel through ShowNotice, log
   each notice that wraps past seven lines to data\longquotes.txt, and write
   per-script plus grand totals to data\quotereport.txt. Raising
   status.quote_audit makes ShowNotice maintain g_notice_line_count and
   status.long_quote. The report streams are owned and failures leave
   quote-audit mode disabled. */
void AuditNpcScriptQuotes(GameStatus& status)
{
    const W8NpcScriptFile* script;
    const W8NpcScriptQuote* quote;
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
        auto log_file =
            wiz8::open_file(wiz8::user_path("logs\\longquotes.txt"), wiz8::OpenMode::replace);
        auto file =
            wiz8::open_file(wiz8::user_path("logs\\quotereport.txt"), wiz8::OpenMode::replace);
        const auto today = wiz8::current_local_time();
        std::tm calendar{};
        calendar.tm_year = today.year - 1900;
        calendar.tm_mon = today.month - 1;
        calendar.tm_mday = today.day;
        calendar.tm_wday = today.day_of_week;
        date = fmt::format("{:%A, %B %d, %Y}", calendar);
        const auto write_report = [&](const std::string& text) {
            file->write(text.data(), text.size());
        };
        status.quote_audit = 1;
        write_report(std::string("Script Report File: ") + date + "\n");
        write_report("-------------------------------------------------\n");
        for (const auto& name :
             wiz8::list_directory(wiz8::asset_path("Data\\NPC Scripts"), "*.nsf")) {
            path = fmt::format("Data\\NPC Scripts\\{}", name);
            auto script_owner = LoadNpcScriptFile(path);
            script = script_owner.get();
            if (script != 0) {
                file_lines = 0;
                file_quotes = 0;
                for (record_index = 0; record_index < std::ssize(script->quotes); ++record_index) {
                    quote = &script->quotes[record_index];
                    for (sub_index = 0; sub_index < std::ssize(quote->subquotes); ++sub_index) {
                        text = quote->subquotes[sub_index];
                        if (text.size() != 0 && CompareTextIgnoreAsciiCase(text, "EMPTY") != 0 &&
                            CompareTextIgnoreAsciiCase(text, "BLANK") != 0 &&
                            CompareTextIgnoreAsciiCase(text, "UNKNOWN") != 0 &&
                            CompareTextIgnoreAsciiCase(text, "CLASSIFIED") != 0) {
                            display = fmt::format(" \"{}\"", text);
                            ShowNotice(W8_FONT_PALETTE_RED, display, 0, W8_NOTICE_WRAP_TEXT_BOX);
                            if (status.long_quote != 0) {
                                const std::string line =
                                    "Long Quote: #" + std::to_string(record_index) +
                                    ", subquote: #" + std::to_string(sub_index) +
                                    ", script file: " + name + " \n\n";
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
                write_report("  #quotes: " + std::to_string(file_quotes) +
                             "\n  #lines: " + std::to_string(file_lines) + "\n");
                ++total_scripts;
            }
        }
        write_report("Total lines: " + std::to_string(total_lines) +
                     "\nTotal Quotes: " + std::to_string(total_quotes) +
                     "\nTotal Scripts: " + std::to_string(total_scripts));
        file->close();
        log_file->close();
        ShowNotice(W8_FONT_PALETTE_RED, "Quote test complete. See log file for results",
                   W8_NOTICE_TEXT_BOX_AUTOMATIC, W8_NOTICE_WRAP_TEXT_BOX);
    } catch (const std::exception&) {
        ShowNotice(W8_FONT_PALETTE_RED, "Quote test failed. Could not write report",
                   W8_NOTICE_TEXT_BOX_AUTOMATIC, W8_NOTICE_WRAP_TEXT_BOX);
    }
    status.quote_audit = 0;
}
/* Queue `text` as a floating portrait message: the string is copied so the
   queue owns it, and while the level-data flag is clear the message is
   bracketed by RESET_LEVEL_STATE markers so the dispatcher restores state
   around it. */
void ShowString(std::string_view text)
{
    if (!IsLevelMovementStopped()) {
        CurrentGame().npc_dialogue.Queue(W8NpcCommand::reset_level_data_vectors);
    }
    CurrentGame().npc_dialogue.QueuePortraitMessage(std::string(text));
    if (!IsLevelMovementStopped()) {
        CurrentGame().npc_dialogue.Queue(W8NpcCommand::clear_level_movement_stopped);
    }
}
