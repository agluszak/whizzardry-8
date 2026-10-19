#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "Item.h"
#include "compat/native.h"
#include "dice.h"
#include "equipment_slots.h"
#include "gameplay_modifiers.h"
#include "glm/ext/vector_float3.hpp"
#include "layouts/main_game_screen.h"
#include "message_box.h"
#include "random.h"
#include "retail_records.h"
#include "surrender/srDebug.h"
#include "wiz8/character_skills.h"
#include "wiz8/chunk.h"
#include "wiz8/compat/unaligned.h"
#include "wiz8/conditions.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/party_encumbrance.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_items.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/retail_save_records.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/unicode.h"
#include "wiz8/xstatus.h"

static bool ProbeNpcPlacementNearParty(int party_slot, int mode, glm::vec3* position_out);
static bool InitializeNpcCharacter(W8NpcState* npc, W8Character& character);
static void InitializeNpcItemTable(W8NpcState* npc);
static bool RestoreNpcMonster(W8NpcState* npc, const char* entity_name);
static bool SaveNpcItemLists(wiz8::File& file);
static bool LoadNpcItemLists(W8Chunk& file);
static bool NpcOffersService(W8NpcState* npc, unsigned int service_id);

enum { W8_NPC_TOPIC_SLOTS = 5, W8_NPC_FACT_SLOTS = 14 };

/* The NPC kind that will not trade at all. */
enum { W8_NPC_KIND_NO_TRADE = 0x14 };

/* The one item a trader always accepts regardless of what it is worth, and the
   value everything else has to clear. */
enum { W8_NPC_ALWAYS_TRADED_ITEM = 0x29f, W8_NPC_MINIMUM_TRADE_VALUE = 300 };

/* The two thresholds the disposition band is cut at. */
enum { W8_NPC_DISPOSITION_HOSTILE = 0x21, W8_NPC_DISPOSITION_FRIENDLY = 0x42 };

/* Every NPC state, held in the shared growable vector. */
// GLOBAL: WIZ8 0x00689F94
std::vector<std::unique_ptr<W8NpcState>> g_npc_states;
// Script/dialogue callbacks can still hold a replaced state. Keep its address
// valid until the game resets, then dispose of it with the active registry.
static std::vector<std::unique_ptr<W8NpcState>> g_retired_npc_states;

/* Whether the NPC's database record names a trade pool. */
void QueueNpcHeldItemNotice(int npc_kind)
{
    auto* npc = GetNpcStateByKind(npc_kind);
    auto* item = g_status.item_in_cursor ? &g_status.item_in_hand : nullptr;
    QueueNpcScriptNotice(npc, item, std::nullopt, false, 0);
}

// FUNCTION: WIZ8 0x0050aa00
bool NpcRecordHasTradePool(W8NpcState* npc)
{
    return npc->record->trade_pool != 0;
}

/* The NPC's effective disposition: the state's byte plus its bound monster's
   modifier, forced fully hostile while that monster is turned. A factioned
   NPC instead reports its faction score unless the record keeps the answer
   static or the faction holds nothing toward the party; a factioned NPC whose
   allied faction a living front-rank party RPC shares answers fifty. */
// FUNCTION: WIZ8 0x0050A280
static char GetNpcDisposition(W8NpcState* npc)
{
    char disposition = npc->disposition;
    char faction_disposition;
    W8MonsterInfo* monster_info;
    int bound_index;
    unsigned int slot;

    if (npc->has_monster && npc->is_present) {
        monster_info = FindMonster(npc->location_id);
        if (monster_info != 0) {
            disposition += monster_info->charm_strength;
            if (monster_info->uiCondition[W8_CONDITION_TURNCOAT] > 0) {
                disposition = 0x64;
            }
        }
    }
    if (npc->record->faction == 0) {
        return disposition;
    }
    faction_disposition = g_factions[npc->record->faction].disposition_score;
    if (npc->record->monster_bound != 0) {
        return faction_disposition;
    }
    if (GetFactionDispositionToward(npc->record->faction, W8_FACTION_PARTY) == W8_FACTION_HOSTILE) {
        return faction_disposition;
    }
    if (npc->record->allied_faction != 0) {
        for (slot = 0; slot < 2; ++slot) {
            if (g_status.buffers.XChar[slot].fOccupied &&
                g_status.buffers.Char[slot].hp_current > 0) {
                bound_index = g_status.buffers.XChar[slot].npc_index;
                if (auto* bound = GetNpcState(bound_index);
                    bound && bound->record->faction == npc->record->allied_faction) {
                    return 0x32;
                }
            }
        }
    }
    return disposition;
}

/* Which of the three disposition bands the NPC falls in. The bands are cut at
   0x21 and 0x42, and the hostile band answers two rather than zero. */
/* The NPC's effective disposition: the stored byte adjusted by its monster's
   effect, pinned to 'd' while the monster carries condition 0x0d, then replaced
   by the faction score when the record names a faction - and finally pinned
   friendly while a bound lead NPC belongs to the record's ally faction. */

// FUNCTION: WIZ8 0x0050a500
unsigned char GetNpcDispositionBand(W8NpcState* npc)
{
    char disposition = GetNpcDisposition(npc);

    if (disposition < W8_NPC_DISPOSITION_HOSTILE) {
        return W8_NPC_BAND_HOSTILE;
    }
    return disposition < W8_NPC_DISPOSITION_FRIENDLY;
}

/* Test a placement near the party without retaining the position. */
// FUNCTION: WIZ8 0x0050b2d0
bool CanPlaceNpcNearParty(int party_slot)
{
    glm::vec3 position;

    return ProbeNpcPlacementNearParty(party_slot, 0, &position);
}

/* Whether the NPC will talk about one topic. Topics are stored one more than
   they name, so zero can mean an empty slot. */
// FUNCTION: WIZ8 0x0050c190
bool NpcHasTopic(W8NpcState* npc, int topic)
{
    int slot;

    for (slot = 0; slot < W8_NPC_TOPIC_SLOTS; ++slot) {
        if (npc->topics[slot] == topic + 1) {
            return true;
        }
    }
    return false;
}

/* Tell the NPC one fact, in the first empty slot. A full list silently drops
   it. */
// FUNCTION: WIZ8 0x0050dd50
void TellNpcFact(W8NpcState* npc, short fact)
{
    int slot;

    for (slot = 0; slot < W8_NPC_FACT_SLOTS; ++slot) {
        if (npc->known_facts[slot] == 0) {
            npc->known_facts[slot] = fact;
            return;
        }
    }
}

/* Whether the NPC has already been told a fact. Every slot is checked; an
   empty one is skipped rather than ending the scan, so a hole in the list
   does not hide the facts after it. */
// FUNCTION: WIZ8 0x0050dd10
bool NpcKnowsFact(W8NpcState* npc, W8FactId fact)
{
    int slot;

    for (slot = 0; slot < W8_NPC_FACT_SLOTS; ++slot) {
        if (npc->known_facts[slot] != 0 &&
            static_cast<unsigned short>(npc->known_facts[slot]) == fact) {
            return true;
        }
    }
    return false;
}

/* The NPC state at one index, skipping released bindings and invalid indices. */
// FUNCTION: WIZ8 0x0050b800
W8NpcState* GetNpcState(int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= g_npc_states.size()) return nullptr;
    auto* npc = g_npc_states[index].get();
    return npc && !npc->binding_unavailable ? npc : nullptr;
}

// FUNCTION: WIZ8 0x0050b830
W8NpcState* GetNpcStateByKind(int kind)
{
    for (const auto& state_owner : g_npc_states) {
        auto* npc = state_owner.get();
        if (npc->record->kind == kind) {
            return npc;
        }
    }
    return 0;
}

/* Whether the NPC attached to either of the first two party rows carries the
   given name style with an unreleased binding, while that row's lead stays
   under level fifteen. */
// FUNCTION: WIZ8 0x0050B8F0
bool NpcLeadHasNameStyle(W8NpcId kind)
{
    if (g_status.buffers.XChar[0].fOccupied) {
        W8NpcState* npc = GetNpcState(g_status.buffers.XChar[0].npc_index);
        if (npc->name_style == kind &&
            g_status.buffers.Char[0].highest_condition < W8_CONDITION_ASLEEP) {
            return true;
        }
    }
    if (g_status.buffers.XChar[1].fOccupied) {
        W8NpcState* npc = GetNpcState(g_status.buffers.XChar[1].npc_index);
        if (npc->name_style == kind &&
            g_status.buffers.Char[1].highest_condition < W8_CONDITION_ASLEEP) {
            return true;
        }
    }
    return false;
}

/* The monster standing in the world for this NPC, if one is. */
// FUNCTION: WIZ8 0x0050a3c0
W8MonsterInfo* GetNpcMonsterInfo(W8NpcState* npc)
{
    if (!npc->has_monster || !npc->is_present) {
        return 0;
    }
    return FindMonster(npc->location_id);
}

/* The monster manager entry this NPC's group occupies, if its database entry
   says it has a group and it is in one. */
// FUNCTION: WIZ8 0x0050b870
W8MonsterManagerEntry* GetNpcGroupEntry(W8NpcState* npc)
{
    if (npc->record->has_group == 0) {
        return 0;
    }
    if (!npc->is_grouped) {
        return 0;
    }
    return &gXStatus.monster_manager_entries[npc->group_index];
}

/* The party character occupying this NPC's group slot. */
// FUNCTION: WIZ8 0x0050b8b0
W8Character* GetNpcGroupCharacter(W8NpcState* npc)
{
    if (npc->record->has_group == 0) {
        return 0;
    }
    if (!npc->is_grouped) {
        return 0;
    }
    return &g_status.buffers.Char[npc->group_index];
}

/* Whether an NPC would take one item in trade. The kind that trades in nothing
   refuses outright, one particular item is always taken, and everything else
   has to be worth enough. */
// FUNCTION: WIZ8 0x0050a9c0
bool WillNpcTradeForItem(W8NpcState* npc, W8ItemInstance* item)
{
    if (npc->record->kind == W8_NPC_KIND_NO_TRADE) {
        return false;
    }
    if (item->iItemNo == W8_NPC_ALWAYS_TRADED_ITEM) {
        return true;
    }
    return GetItemStackValue(item) >= W8_NPC_MINIMUM_TRADE_VALUE;
}

/* How many of the two leading party slots are occupied. Written as nested
   tests rather than a count, which is why the first slot is read twice. */
// FUNCTION: WIZ8 0x0050b9b0
unsigned char CountLeadingPartySlots(void)
{
    if (g_status.buffers.XChar[0].fOccupied) {
        if (g_status.buffers.XChar[1].fOccupied) {
            return 2;
        }
        if (g_status.buffers.XChar[0].fOccupied) {
            return 1;
        }
    }
    if (g_status.buffers.XChar[1].fOccupied) {
        return 1;
    }
    return 0;
}

/* The NPC-index consumers at 0x0050CC14/0x0050E025 address the first
   word at 0x00619DF8. Service lookup starts one word into each row.
   The five mutable race bytes at 0x00619EAC are a separate object. */
struct W8NpcServiceRow {
    unsigned int npc_id;
    unsigned int service_id;
    unsigned int bit;
};
// GLOBAL: WIZ8 0x00619DF8
W8NpcServiceRow g_npc_services[] = {
    {0x46, 2, W8_NPC_SERVICE_ARNIKA},
    {0x47, 3, W8_NPC_SERVICE_TRYNTON},
    {0x50, 4, W8_NPC_SERVICE_SWAMP},
    {0x48, 5, W8_NPC_SERVICE_MARTEN_BLUFF},
    {0x49, 7, W8_NPC_SERVICE_SEA_CAVES},
    {0x4d, 8, W8_NPC_SERVICE_BAYJIN},
    {0x4c, 9, W8_NPC_SERVICE_RAPAX},
    {0x4b, 10, W8_NPC_SERVICE_RIFT},
    {0x4a, 11, W8_NPC_SERVICE_MT_GIGAS},
    {0x4e, 12, W8_NPC_SERVICE_ASCENSION},
    {0x51, 13, W8_NPC_SERVICE_RAPAX_CAMP},
    {0x4f, 14, W8_NPC_SERVICE_CIRCLE},
    {0x4d, 15, W8_NPC_SERVICE_GIGAS_CAVES},
    {0x52, 6, W8_NPC_SERVICE_MTN_PASS},
    {0, 0xffffffff, 0},
};

// GLOBAL: WIZ8 0x00619EAC
unsigned char g_npc_join_races[5] = {W8_RACE_UMPANI, W8_RACE_T_RANG, W8_RACE_RAPAX, W8_RACE_ANDROID,
                                     W8_RACE_TRYNNIE};

/* Whether the NPC wants the offered item: it matches one of the record's
   three wanted entries by id or by the shared 0x83 name kind, and a grouped
   NPC whose member already carries more than one declines. */
// FUNCTION: WIZ8 0x0050DC50
bool NpcWantsItem(W8NpcState* npc, W8ItemInstance* item)
{
    int index;
    const w8_unaligned_short* wanted = npc->record->character.wanted_item_ids;

    for (index = 0; index < 3; ++index) {
        int wanted_id = wanted[index] - 1;
        if (wanted_id < 0) {
            continue;
        }
        if (wanted_id == item->iItemNo) {
            break;
        }
        if (g_item_records[wanted_id].unidentified_name_index != 0x83) {
            continue;
        }
        if (g_item_records[item->iItemNo].unidentified_name_index == 0x83) {
            break;
        }
    }
    if (index == 3) {
        return false;
    }
    if (npc->is_grouped &&
        CountItemOnCharacter(&g_status.buffers.Char[npc->group_index], item->iItemNo, 0, 2) > 1) {
        return false;
    }
    return true;
}

// FUNCTION: WIZ8 0x0050ddc0
static void ReturnDismissedNpcItems(W8NpcState* npc, W8Character& character)
{
    bool returned = false;
    bool dropped = false;
    unsigned int slot;
    for (slot = 0; slot < 12; ++slot) {
        W8ItemInstance* item = &character.EquippedItem[slot];
        if (item->iItemNo != -1 && CanUnequipSlotItem(&character, static_cast<W8EquipSlot>(slot)) &&
            !NpcWantsItem(npc, item)) {
            if (AddItemToPartyOrDrop(item, false)) {
                returned = true;
            } else {
                dropped = true;
            }
        }
    }
    for (slot = 0; slot < 8; ++slot) {
        W8ItemInstance* item = &character.backpack[slot];
        if (item->iItemNo != -1 && !NpcWantsItem(npc, item)) {
            if (AddItemToPartyOrDrop(item, false)) {
                returned = true;
            } else {
                dropped = true;
            }
        }
    }
    if (returned) {
        ShowNotice(W8_FONT_PALETTE_RED,
                   fmt::sprintf(g_localized_strings[0x1fd], npc->record->source_name));
    }
    if (dropped) {
        ShowNotice(W8_FONT_PALETTE_RED,
                   fmt::sprintf(g_localized_strings[0x1fe], npc->record->source_name));
    }
}

// FUNCTION: WIZ8 0x0050b590
int DismissNpcFromParty(int party_slot, int /*unused*/, bool skip_spawn, bool neutral)
{
    W8PartySlotRow* row = &g_status.buffers.XChar[party_slot];
    if (row->npc_index == -1) {
        return 0;
    }
    glm::vec3 position;
    if (!skip_spawn) {
        glm::vec3 placement;
        ProbeNpcPlacementNearParty(party_slot, 1, &placement);
        position = placement;
    }
    W8NpcState* npc = GetNpcState(row->npc_index);
    if (npc == 0 || npc->binding_unavailable) {
        return 0;
    }
    W8Character* character = &g_status.buffers.Char[party_slot];
    *npc->character = *character;
    npc->is_grouped = false;
    npc->script_file.reset();
    RemoveCharacterFromParty(party_slot);
    *character = W8Character{};
    memset(row, 0, sizeof(*row));
    gXStatus.monster_manager_entries[party_slot] = {};
    row->npc_index = -1;
    if (npc->character->highest_condition != W8_CONDITION_DEAD && !skip_spawn) {
        std::unique_ptr<W8MonsterRecord[]> records;
        if (!LoadMonsterDatabase(&records)) return false;
        unsigned int species;
        for (species = 0; species < gXStatus.uiMonstersInDatabase; ++species) {
            if ((records[species].flags & W8_MONSTER_FLAG_NPC) != 0 &&
                records[species].npc_kind == npc->name_style) {
                break;
            }
        }
        if (species == gXStatus.uiMonstersInDatabase) {
            return 0;
        }
        W8MonsterGroup* group = CreateGroup(species, 1, &position, true, false, true);
        W8MonsterInfo* monster = FindMonster(group->leader_location_id);
        if (monster != 0) {
            CopyCharacterConditionsToTarget(*npc->character, &monster->location_id);
            if (monster->uiCondition[W8_CONDITION_UNCONSCIOUS] == W8_CONDITION_INDEFINITE) {
                unsigned int stamina = static_cast<unsigned int>(npc->character->uiStaminaMax);
                if (static_cast<unsigned int>(npc->character->stamina) < stamina) {
                    stamina = static_cast<unsigned int>(npc->character->stamina);
                }
                monster->stamina = stamina;
            }
            if (neutral) {
                SetMonsterGroupHostility(group, W8_DISPOSITION_NEUTRAL, false);
                group->forced_neutral = true;
            }
        }
    }
    RequestRedraw(W8_MAIN_REDRAW_ALL);
    ReturnDismissedNpcItems(npc, *npc->character);
    npc->dismissed_flag = true;
    memset(&npc->dismissed_timer, 0, sizeof(npc->dismissed_timer));
    npc->event_pending = 1;
    return 1;
}

/* The per-frame pass over a bound NPC's party slot: a dying member is handed
   its held item back and removed, a flagged one removed outright, then the
   NPC's placement is probed - failure complains, success runs the level's
   service check and queues either the scripted group action or the slot's
   ambient event. */
// FUNCTION: WIZ8 0x0050B3B0
void UpdateNpcPartyMember(int party_slot)
{
    W8NpcState* npc = GetNpcState(g_status.buffers.XChar[party_slot].npc_index);
    W8Character* character = &g_status.buffers.Char[party_slot];
    glm::vec3 position;

    if (character->highest_condition == W8_CONDITION_DEAD) {
        ShowNotice(W8_FONT_PALETTE_RED, fmt::sprintf(g_localized_strings[0x7d4], character->name));
        StashDepartingCharacterItems(character);
        RemoveCharacterFromParty(party_slot);
        return;
    }
    if (character->highest_condition == W8_CONDITION_MISSING) {
        RemoveCharacterFromParty(party_slot);
        return;
    }
    if (!ProbeNpcPlacementNearParty(party_slot, 0, &position)) {
        ShowNotice(W8_FONT_PALETTE_RED, g_localized_strings[0x7d5]);
        return;
    }
    if (NpcOffersService(npc, GetLevelBand(GetRequiredLoadedLevelID()))) {
        if (character->highest_condition > W8_CONDITION_WEBBED) {
            npc->event_pending = 1;
            BeginScriptedWorldAction();
            g_npc_dialogue.Queue(W8NpcDismiss{party_slot});
            return;
        }
        QueueCharacterEvent(character, 0x53, 0, kCharacterEventFullVolume);
        return;
    }
    if (character->highest_condition < W8_CONDITION_ASLEEP) {
        QueueCharacterEvent(character, 69, 0, kCharacterEventFullVolume);
    }
}

// FUNCTION: WIZ8 0x0050b160
bool RecruitNpcIntoParty(W8NpcState* npc)
{
    if (npc->record->has_group == 0) {
        return false;
    }
    const auto party_slot = AddCharacterToParty(*npc->character, npc->partner_index);
    if (!party_slot) {
        return false;
    }
    int index;
    for (index = 0; index < 0x29; ++index) {
        if (g_status.buffers.Char[*party_slot].skills[index].points > 0) {
            g_status.buffers.Char[*party_slot].skills[index].available = true;
        }
    }
    RefreshCharacterSkillAvailability(&g_status.buffers.Char[*party_slot]);
    if (npc->has_monster && npc->is_present) {
        W8MonsterInfo* monster = FindMonster(npc->location_id);
        if (monster != 0) {
            CopyMonsterConditionsToCharacter(&g_status.buffers.Char[*party_slot], monster);
            RemoveMonster(monster, W8MonsterRemoval::Despawn);
        }
    }
    npc->group_index = static_cast<signed char>(*party_slot);
    npc->is_grouped = true;
    npc->is_present = false;
    npc->event_pending = 0;
    npc->pending_restore = false;
    ReloadNpcScriptResources(npc, GetNpcMonster(npc));

    /* Retail writes the race into this initially populated list, then tests
       the value just written. Preserve that assignment and early exit. */
    unsigned char race = 0;
    for (index = 0; index < 5; ++index) {
        race = static_cast<unsigned char>(npc->record->character.race);
        g_npc_join_races[index] = race;
        if (race != 0) {
            break;
        }
    }
    if (index == 5) {
        return true;
    }
    for (index = 0; index < 5; ++index) {
        if (g_status.rpc_races[index] == race) {
            return true;
        }
    }
    for (index = 0; index < 5; ++index) {
        if (g_status.rpc_races[index] == 0) {
            g_status.rpc_races[index] = race;
            break;
        }
    }
    return true;
}

/* The name a fact substitutes, and 0x00689F60 the buffer it is
   copied into so the caller always gets a writable one. */
// GLOBAL: WIZ8 0x00619F18
char g_substituted_npc_name[] = "RFS81B";
// GLOBAL: WIZ8 0x00689F60
// The former fixed name scratch buffer is no longer needed by owned results.

/* The name style that admits a substituted name: the unfixed RFS-81 record,
   renamed to RFS81B once the fix fact is set. */

/* The engine object standing in the world for this NPC. */
// FUNCTION: WIZ8 0x0050a400
W8Monster* GetNpcMonster(W8NpcState* npc)
{
    W8MonsterInfo* monster_info;

    if (!npc->has_monster || !npc->is_present) {
        return 0;
    }
    monster_info = FindMonster(npc->location_id);
    if (monster_info == 0) {
        return 0;
    }
    return monster_info->p3D;
}

/* Move the NPC into one disposition band. Each band is written as one
   representative value rather than a range, and a band it is already in is
   left alone - which is why the current band is computed twice. */
// FUNCTION: WIZ8 0x0050a520
void SetNpcDispositionBand(W8NpcState* npc, char band)
{
    char current;

    GetNpcDisposition(npc);
    current = GetNpcDisposition(npc);
    if (current < W8_NPC_DISPOSITION_HOSTILE) {
        current = W8_NPC_BAND_HOSTILE;
    } else {
        current = current < W8_NPC_DISPOSITION_FRIENDLY;
    }
    if (current == band) {
        return;
    }
    if (band == W8_NPC_BAND_HOSTILE) {
        npc->disposition = 0x19;
        return;
    }
    npc->disposition = band == W8_NPC_BAND_NEUTRAL ? 0x32 : 0x4b;
}

/* Resume (or leave hostile) the NPC after its scripted pause: clamp the
   disposition into the friendly band, then re-flag its monster group's
   hostility from `enabled`. */
// FUNCTION: WIZ8 0x0050AE40
void ResumeNpc(W8NpcState* npc, int enabled)
{
    W8MonsterInfo* monster_info = GetNpcMonsterInfo(npc);

    GetNpcDisposition(npc);
    if (GetNpcDisposition(npc) > ' ') {
        npc->disposition = 0x19;
    }
    if (monster_info != 0) {
        const int group_id = monster_info->monster_group_id;
        if (static_cast<char>(enabled) != 0) {
            SetMonsterGroupHostilityByID(group_id, W8_DISPOSITION_HOSTILE, false);
            return;
        }
        SetMonsterGroupHostilityByID(group_id, W8_DISPOSITION_NEUTRAL, false);
    }
}

/* Whether any NPC of one kind is in the world, and what its own byte at 0x04
   says - the two answers are the same value, so a kind that is not there is
   indistinguishable from one whose byte is zero. */
// FUNCTION: WIZ8 0x0050dd80
unsigned char FindNpcOfKind(int kind)
{
    auto* npc = GetNpcStateByKind(kind);
    return npc ? static_cast<unsigned char>(npc->spawned) : 0;
}

/* Mark the NPC of one kind, raising both of the two flags that go together. */
// FUNCTION: WIZ8 0x0050ca30
void MarkNpcOfKind(int kind)
{
    if (auto* npc = GetNpcStateByKind(kind)) {
        npc->event_pending = true;
        npc->restore_done = true;
    }
}

// FUNCTION: WIZ8 0x0050C870
bool CanNpcJoinParty(W8NpcState* npc)
{
    int index;
    unsigned int count;
    unsigned int total;
    unsigned int average;

    if (npc->record->has_group == 0) {
        return false;
    }
    /* The service ids are the GetLevelBand region numbering: an NPC who offers
       the current region's service stays on duty and refuses to join. */
    if (NpcOffersService(npc, GetLevelBand(GetRequiredLoadedLevelID()))) {
        return false;
    }
    if (npc->name_style == W8_NPC_GLUMPH && GetFact(W8_FACT_UMISSION_SCUBA_DONE) != 0) {
        return false;
    }
    if (npc->name_style == W8_NPC_SEXUS && GetFact(W8_FACT_SEXUS_PAID) == 0) {
        return false;
    }
    /* Retail performs the identical Madras check twice in a row - the second
       test is dead but genuinely present, kept faithful. */
    if (npc->name_style == W8_NPC_MADRAS && GetFact(W8_FACT_TRYNNIE_MADRAS_WILL_JOIN) == 0) {
        return false;
    }
    if (npc->name_style == W8_NPC_MADRAS && GetFact(W8_FACT_TRYNNIE_MADRAS_WILL_JOIN) == 0) {
        return false;
    }
    if ((npc->name_style == W8_NPC_DRAZIC || npc->name_style == W8_NPC_RODAN) &&
        GetFact(W8_FACT_PEACE_ACHIEVED) != 0) {
        return false;
    }
    if (npc->record->min_party_level > 0) {
        total = 0;
        count = 0;
        average = 0;
        for (index = 0; index < 8; ++index) {
            if (g_status.buffers.XChar[index].fOccupied &&
                g_status.buffers.Char[index].hp_current > 0 &&
                g_status.buffers.Char[index].highest_condition < W8_CONDITION_ASLEEP) {
                ++count;
                total += g_status.buffers.Char[index].uiExpLevel;
            }
        }
        if (count > 0) {
            average = total / count;
        }
        if (average < npc->record->min_party_level) {
            return false;
        }
    }
    return true;
}

/* Whether the NPC offers one service. The service id is looked up in a table
   that pairs it with its bit, so the ids need not be contiguous. */
// FUNCTION: WIZ8 0x0050c9e0
static bool NpcOffersService(W8NpcState* npc, unsigned int service_id)
{
    int row = 0;

    if (g_npc_services[0].service_id == 0xffffffff) {
        return false;
    }
    while (g_npc_services[row].service_id != 0xffffffff) {
        if (g_npc_services[row].service_id == service_id) {
            return (npc->record->service_flags & g_npc_services[row].bit) != 0;
        }
        ++row;
    }
    return false;
}

/* Add a topic to the front of the NPC's five, pushing the oldest off the end
   when they are full. An empty slot is filled in place instead. */
// FUNCTION: WIZ8 0x0050c140
void AddNpcTopic(W8NpcState* npc, int topic)
{
    int slot;

    for (slot = 0; slot < W8_NPC_TOPIC_SLOTS; ++slot) {
        if (npc->topics[slot] == 0) {
            npc->topics[slot] = topic + 1;
            return;
        }
    }
    for (slot = W8_NPC_TOPIC_SLOTS - 1; slot > 0; --slot) {
        npc->topics[slot] = npc->topics[slot - 1];
    }
    npc->topics[0] = topic + 1;
}

/* What to call the NPC. One naming style takes a substituted name once the
   party has learned it, copied into a shared buffer so the caller always gets
   a writable string; everything else is named by its record. */
// FUNCTION: WIZ8 0x0050c770
std::string GetNpcDisplayName(W8NpcState* npc)
{
    if (npc->name_style == W8_NPC_RFS81_A && GetFact(W8_FACT_RFS81_HAS_BEEN_FIXED) != 0) {
        return g_substituted_npc_name;
    }
    return npc->record->display_name;
}

/* The live NPC state whose display (or fact-substituted) name matches
   case-insensitively; released bindings and non-matches are skipped. */
// FUNCTION: WIZ8 0x0050ADA0
W8NpcState* FindNpcStateByName(std::string_view name)
{
    std::string candidate;

    for (const auto& state_owner : g_npc_states) {
        auto* npc = state_owner.get();
        if (npc->binding_unavailable == 0) {
            if (npc->name_style == W8_NPC_RFS81_A && GetFact(W8_FACT_RFS81_HAS_BEEN_FIXED) != 0) {
                candidate = g_substituted_npc_name;
            } else {
                candidate = npc->record->display_name;
            }
            if (CompareTextIgnoreAsciiCase(candidate, name) == 0) {
                return npc;
            }
        }
    }
    return 0;
}

/* Age every bound NPC's timeout state once per game-time pass. The caller
   feeds elapsed minutes times ten: a dismissed NPC's flag clears once its
   accumulator passes 0x3c of those units, and each dialogue cooldown flag
   drops once its world-clock stamp is more than 0xa8c0 old. */
// FUNCTION: WIZ8 0x0050C7D0
void AdvanceNpcTimers(unsigned int elapsed)
{
    for (const auto& state_owner : g_npc_states) {
        auto* npc = state_owner.get();
        if (npc->binding_unavailable == 0) {
            if (npc->dismissed_flag) {
                npc->dismissed_timer += elapsed;
                if (npc->dismissed_timer > 0x3c) {
                    npc->dismissed_flag = false;
                }
            }
            if (npc->talk_cooldown_active &&
                static_cast<unsigned int>(g_status.world_clock - npc->talk_cooldown_clock) >
                    0xa8c0) {
                npc->talk_cooldown_active = false;
            }
            if (npc->trade_cooldown_active &&
                static_cast<unsigned int>(g_status.world_clock - npc->trade_cooldown_clock) >
                    0xa8c0) {
                npc->trade_cooldown_active = false;
            }
        }
    }
}

/* Consume the two pending NPC event marks once combat and surprise are both
   quiet. The 0x2446 mark resolves the kind-0x42 NPC: when a bound, flagged
   record is present the mark just clears; otherwise the named party slot
   becomes infatuated and fact 0x2a6 is raised unless the level band is 9 or
   10. The 0x242e mark replays the bound NPCs of the first two party rows
   after its 0x3c delay: a row whose character carries a highest condition of
   0xf or worse keeps its NPC bound and the whole mark stays pending, while
   clear rows fire the name-style group events or mark the NPC's service. */
// FUNCTION: WIZ8 0x0050CA80
void ProcessNpcPendingEvents(void)
{
    bool all_clear = true;

    if (!gXStatus.fCombatMode && !gXStatus.fSurprisePossible) {
        if (g_status.infatuation_pending) {
            bool flagged = false;
            for (const auto& state_owner : g_npc_states) {
                auto* candidate = state_owner.get();
                if (candidate->record->kind == 0x42) {
                    if (candidate != 0 && static_cast<unsigned char>(candidate->spawned) != 0) {
                        flagged = true;
                    }
                    break;
                }
            }
            if (flagged) {
                g_status.infatuation_pending = false;
            } else {
                char band = GetLevelBand(GetRequiredLoadedLevelID());
                if (band != 9 && band != 0xa) {
                    W8Character* character = &g_status.buffers.Char[g_status.sedexus_party_slot];
                    if (character->gender == W8_GENDER_MALE) {
                        QueueCharacterEvent(character, 44, 0, kCharacterEventFullVolume);
                    }
                    SetCharacterCondition(g_status.sedexus_party_slot, W8_CONDITION_INFATUATED,
                                          W8_CONDITION_INDEFINITE, 0, 0, 1);
                    g_status.infatuation_pending = false;
                    SetFact(W8_FACT_QUEST_KILL_ALSEDEXUS, 1, false);
                }
            }
        }
        if (g_status.binding_reset_pending &&
            (g_status.world_clock - g_status.binding_reset_clock) > 0x3c) {
            int service = 0;
            if (g_npc_services[0].service_id != 0xffffffff) {
                while (g_npc_services[service].service_id !=
                       static_cast<unsigned int>(GetLevelBand(GetRequiredLoadedLevelID()))) {
                    ++service;
                    if (g_npc_services[service].service_id == 0xffffffff) {
                        g_status.binding_reset_pending = false;
                        return;
                    }
                }
                for (int slot = 0; slot < 2; ++slot) {
                    W8PartySlotRow* row = &g_status.buffers.XChar[slot];
                    W8Character* character = &g_status.buffers.Char[slot];
                    if (!row->fOccupied || character->hp_current == 0) {
                        continue;
                    }
                    W8NpcState* npc = GetNpcState(row->npc_index);
                    if (character->highest_condition < W8_CONDITION_ASLEEP) {
                        int event = 0;
                        if (GetLevelBand(GetRequiredLoadedLevelID()) != 0xd) {
                            if (npc->name_style == W8_NPC_RODAN &&
                                (!g_status.buffers.XChar[0].fOccupied ||
                                 GetNpcState(g_status.buffers.XChar[0].npc_index)->name_style !=
                                     W8_NPC_DRAZIC ||
                                 g_status.buffers.Char[0].highest_condition >=
                                     W8_CONDITION_ASLEEP) &&
                                (!g_status.buffers.XChar[1].fOccupied ||
                                 GetNpcState(g_status.buffers.XChar[1].npc_index)->name_style !=
                                     W8_NPC_DRAZIC ||
                                 g_status.buffers.Char[1].highest_condition >=
                                     W8_CONDITION_ASLEEP)) {
                                event = 0x6c;
                            } else if (npc->name_style == W8_NPC_DRAZIC &&
                                       !NpcLeadHasNameStyle(W8_NPC_RODAN)) {
                                event = 0x67;
                            }
                        }
                        if (event != 0) {
                            QueueCharacterEvent(character, event, 0, kCharacterEventFullVolume);
                            BeginScriptedWorldAction();
                            g_npc_dialogue.Queue(W8NpcDismiss{slot});
                            continue;
                        }
                        W8NpcState* bound = GetNpcState(row->npc_index);
                        if (bound != 0 && bound->service_flags[service] == 0) {
                            bound->service_flags[service] = 1;
                            if (!NpcOffersService(bound,
                                                  GetLevelBand(GetRequiredLoadedLevelID())) &&
                                service != 0xc && service != 0xd) {
                                QueueCharacterEvent(
                                    character,
                                    static_cast<unsigned char>(g_npc_services[service].npc_id), 0,
                                    kCharacterEventFullVolume);
                            }
                        }
                        bound = GetNpcState(row->npc_index);
                        if (bound != 0) {
                            char band = GetLevelBand(GetRequiredLoadedLevelID());
                            for (int index = 0; g_npc_services[index].service_id != 0xffffffff;
                                 ++index) {
                                if (g_npc_services[index].service_id ==
                                    static_cast<unsigned int>(band)) {
                                    if ((bound->record->service_flags &
                                         g_npc_services[index].bit) != 0) {
                                        row->npc_bound = true;
                                        bound->event_clock = g_status.world_clock;
                                        RebuildConditionsAndDerivedStats(slot);
                                        QueueCharacterEvent(character, 0x56, 0,
                                                            kCharacterEventFullVolume);
                                    }
                                    break;
                                }
                            }
                        }
                        npc->incapacitated = 0;
                    } else if (character->highest_condition == W8_CONDITION_UNCONSCIOUS ||
                               character->highest_condition == W8_CONDITION_PARALYZED ||
                               character->highest_condition == W8_CONDITION_ASLEEP) {
                        all_clear = false;
                        npc->incapacitated = 1;
                    }
                }
                if (!all_clear) {
                    return;
                }
            }
            g_status.binding_reset_pending = false;
        }
    }
}

/* Choose the new-game start level and entrance, then bind the intro NPCs that
   belong to that campaign path. Import 0x4c is Gigas, 0x4b is the bluff, and
   0x4e or neither is the monastery. */
// FUNCTION: WIZ8 0x005092f0
void ChooseNewGameStartLocation(int* level, int* entrance)
{
    unsigned char value;
    int start_level;

    value = GetFact(W8_FACT_VIRGIN);
    if (value != 0) {
        start_level = 8;
    } else {
        value = GetFact(W8_FACT_IMPORT_UMPANI);
        if (value != 0) {
            start_level = 0xe;
        } else {
            start_level = GetFact(W8_FACT_IMPORT_TRANG) != 0 ? 6 : 8;
        }
    }
    *level = start_level;
    *entrance = 0;
    g_status.greeting_pending = true;

    value = GetFact(W8_FACT_VIRGIN);
    if (value != 0) {
        return;
    }

    value = GetFact(W8_FACT_IMPORT_UMPANI);
    if (value != 0) {
        RestoreNamedNpcAtLevel(0x18, 0xe, "NP_ViGigas");
        RestoreNamedNpcAtLevel(0xc, 0xe, "NP_BalbrakIntro");
        return;
    }

    value = GetFact(W8_FACT_IMPORT_TRANG);
    if (value != 0) {
        RestoreNamedNpcAtLevel(0x18, 6, "NP_ViBluff");
        RestoreNamedNpcAtLevel(0x8c, 6, "NP_GuardBluff");
        return;
    }
    RestoreNamedNpcAtLevel(0x18, 8, "NP_ViMon");
}

/* Runs when the pending greeting_pending transition times out: new-game parties get
   the opening scripted effect, imported parties instead pull focus to the NPC
   their ending selected — ending 0x4c the kind-0x0c greeter, ending 0x4b the
   kind-0x8c greeter with the camera swung onto its head — and anything else
   falls back to the kind-0x18 greeter. */
// FUNCTION: WIZ8 0x00509560
static void SelectStartNpcGreeting(void)
{
    W8Monster* monster;
    W8NpcState* npc;
    W8MonsterInfo* monster_info;
    unsigned char value;
    glm::vec3 head;

    value = GetFact(W8_FACT_VIRGIN);
    if (value != 0) {
        ApplyItemEffectToRandomCharacter(g_fact_check_event, std::nullopt, 0);
        return;
    }

    value = GetFact(W8_FACT_IMPORT_UMPANI);
    if (value != 0) {
        npc = GetNpcStateByKind(0xc);
    } else {
        value = GetFact(W8_FACT_IMPORT_TRANG);
        if (value != 0) {
            npc = GetNpcStateByKind(0x8c);
            if (npc == 0) {
                return;
            }
            QueueNpcScriptNotice(npc, 0, 0, false, 0);
            monster_info = GetNpcMonsterInfo(npc);
            if (monster_info == 0) {
                return;
            }
            monster = monster_info->p3D;
            head = glm::vec3(monster->movement.position.x,
                             monster->movement.position.y + monster->movement.height_offset,
                             monster->movement.position.z);
            PartyCamera().LookAt(&head, false);
            return;
        }
        npc = GetNpcStateByKind(0x18);
    }
    if (npc != 0) {
        QueueNpcScriptNotice(npc, 0, std::nullopt, false, 0);
    }
}

/* New-game start level from the campaign facts InitializeFactState planted.
   Import path 0x4c is level 14, 0x4b is level 6, and 0x4e or neither is 8. */
// FUNCTION: WIZ8 0x00509750
int SelectNewGameStartLevel(void)
{
    unsigned char value;

    value = GetFact(W8_FACT_VIRGIN);
    if (value != 0) {
        return 8;
    }

    value = GetFact(W8_FACT_IMPORT_UMPANI);
    if (value != 0) {
        return 0xe;
    }

    value = GetFact(W8_FACT_IMPORT_TRANG);
    if (value != 0) {
        return 6;
    }
    return 8;
}

/* Empty the shared vector: release every state's monster binding and character,
   hand its stock to the item-list teardown, and delete the state itself. Then
   rebuild a runtime node for every database record that is not flagged at
   0x054, which is the state a fresh game starts from. */
// FUNCTION: WIZ8 0x00509920
void ResetNpcStates(void)
{
    unsigned int npc_id;

    ReleaseNpcStates();
    for (npc_id = 0; npc_id < g_npc_records.size(); ++npc_id) {
        if (g_npc_records[npc_id].monster_bound == 0) {
            CreateNpcRuntimeNode(npc_id);
        }
    }
}

// FUNCTION: WIZ8 0x005099D0
void ReleaseNpcStates(void)
{
    g_npc_states.clear();
    g_retired_npc_states.clear();
}

/* The NPCT section writer: a version byte, the state count, then each 0x13d
   state block followed by its 0x1862 character block when the NPC carries one.
   The per-state stock lists trail through the section's file handle. */
// FUNCTION: WIZ8 0x00509F00
bool SaveNpcStates(wiz8::File& file)
try {
    unsigned char version = 3;
    W8NpcState* npc;
    unsigned int count;
    unsigned int index;

    file.write(&version, 1);
    count = g_npc_states.size();
    file.write(&count, 4);
    for (index = 0; index < count; ++index) {
        npc = g_npc_states[index].get();
        wiz8::retail::write(file, *npc);
        if (npc->character) wiz8::retail::write_sized(file, *npc->character);
    }
    return SaveNpcItemLists(file);
} catch (const std::exception&) {
    return false;
}

/* The NPCT section reader: release the live states, then rebuild them from the
   saved blocks. Character blocks carry their size from version 3 on and older
   saves read as the legacy 0x185c length. Grouped NPCs re-bind to the loaded
   level; afterwards the missing runtime nodes for unflagged records are
   created. */
/* The stock-list tail of the NPCT section: for every NPC state the entry
   count, then each 0x14-byte stock entry in list order. */
// FUNCTION: WIZ8 0x0050AA10
static bool SaveNpcItemLists(wiz8::File& file)
try {
    for (const auto& state_owner : g_npc_states) {
        auto* npc = state_owner.get();
        const auto item_count = static_cast<unsigned int>(npc->items.size());
        file.write(&item_count, sizeof(item_count));
        file.write(npc->items.data(), npc->items.size() * sizeof(W8NpcItemEntry));
    }
    return true;
} catch (const std::exception&) {
    return false;
}

/* Build one runtime NPC state, reusing an unavailable slot or appending a new
   one. Save records and monster bindings refer to these stable slot indices. */
// FUNCTION: WIZ8 0x00509aa0
static std::unique_ptr<W8NpcState> MakeNpcRuntimeNode(int npc_id)
{
    auto owner = std::make_unique<W8NpcState>();
    auto* npc = owner.get();
    npc->name_style = npc_id;
    npc->record = &g_npc_records[npc_id];
    if (npc->record->has_group != 0) {
        InitializeNpcCharacter(npc, npc->character.emplace());
    }
    if (npc->record->owns_stock != 0) {
        PopulateNpcStock(npc);
    }
    InitializeNpcItemTable(npc);
    npc->location_id = 0;
    npc->is_present = false;
    npc->disposition = g_npc_records[npc_id].disposition;
    npc->gold = g_npc_records[npc_id].gold;
    npc->greeting_pending = true;
    npc->trade_pool = g_npc_records[npc_id].trade_pool;

    return owner;
}

W8NpcState* CreateNpcRuntimeNode(int npc_id)
{
    auto owner = MakeNpcRuntimeNode(npc_id);
    auto* npc = owner.get();
    W8NpcState* released;
    int index;
    for (index = 0; index < static_cast<int>(g_npc_states.size()); ++index) {
        released = g_npc_states[index].get();
        if (released != 0 && released->binding_unavailable != 0) {
            g_retired_npc_states.push_back(std::move(g_npc_states[index]));
            g_npc_states[index] = std::move(owner);
            npc->partner_index = static_cast<unsigned char>(index);
            return npc;
        }
    }
    g_npc_states.push_back(std::move(owner));
    npc->partner_index = static_cast<unsigned char>(static_cast<int>(g_npc_states.size()) - 1);
    return npc;
}

W8PreparedNpcBindings PrepareNpcBindings(W8World& world)
{
    W8PreparedNpcBindings pending;
    std::vector<bool> reserved(g_npc_states.size(), false);
    const auto prepare = [&](W8MonsterInfo& info) {
        const auto* record = GetMonsterDataForInfo(&info);
        if ((record->flags & W8_MONSTER_FLAG_NPC) == 0) {
            info.bound_npc_index = -1;
            return;
        }
        const int kind = record->npc_kind;
        W8NpcState* source = nullptr;
        if (!g_npc_records[kind].monster_bound)
            source = GetNpcStateByKind(kind);
        else if (info.bound_npc_index > 0) {
            auto* candidate = GetNpcState(info.bound_npc_index);
            if (candidate && candidate->location_id == info.location_id &&
                candidate->name_style == kind)
                source = candidate;
        }
        int slot = source ? source->partner_index : -1;
        if (slot == -1) {
            for (size_t index = 0; index < reserved.size(); ++index) {
                if (!reserved[index] && g_npc_states[index]->binding_unavailable) {
                    slot = static_cast<int>(index);
                    break;
                }
            }
            if (slot == -1) {
                slot = static_cast<int>(reserved.size());
                reserved.push_back(false);
            }
        }
        if (slot > 255) throw std::runtime_error("too many NPC bindings");
        auto existing = std::ranges::find(pending.records, slot, &W8PreparedNpcBinding::slot);
        W8NpcState* npc;
        if (existing != pending.records.end())
            npc = existing->state.get();
        else {
            std::unique_ptr<W8NpcState> owner;
            if (source) {
                owner = std::make_unique<W8NpcState>();
                wiz8::retail::decode<W8NpcState>(wiz8::retail::encode(*source), *owner);
                owner->record = source->record;
                owner->items = source->items;
                owner->character = source->character;
            } else
                owner = MakeNpcRuntimeNode(kind);
            npc = owner.get();
            pending.records.push_back({slot, std::move(owner)});
        }
        reserved[slot] = true;
        npc->partner_index = static_cast<unsigned char>(slot);
        npc->location_id = info.location_id;
        npc->is_present = true;
        npc->has_monster = true;
        npc->binding_unavailable = false;
        npc->level_band = GetLevelBand(world.level_id.value());
        npc->bound_level = static_cast<unsigned char>(world.level_id.value());
        info.bound_npc_index = slot;
        ReloadNpcScriptResources(npc, info.p3D);
    };
    for (const auto* list :
         {&world.population->monster_roster.live(), &world.population->monster_roster.unborn()})
        for (const auto& info : *list) prepare(*info);
    g_npc_states.reserve(reserved.size());
    g_retired_npc_states.reserve(g_retired_npc_states.size() + pending.records.size());
    return pending;
}

void InstallNpcBindings(W8PreparedNpcBindings pending)
{
    for (auto& binding : pending.records) {
        if (static_cast<size_t>(binding.slot) == g_npc_states.size())
            g_npc_states.push_back(std::move(binding.state));
        else {
            g_retired_npc_states.push_back(std::move(g_npc_states[binding.slot]));
            g_npc_states[binding.slot] = std::move(binding.state);
        }
    }
}

/* Bind an NPC runtime state to a monster location, creating the state when the
   database entry allows it. The dialogue path instead finds the existing state
   by its database kind and only refreshes its presence fields. */
// FUNCTION: WIZ8 0x00509cd0
void BindNpcToMonster(unsigned char npc_id, bool has_monster, int location_id)
{
    W8NpcState* npc = 0;
    W8MonsterInfo* monster_info = 0;

    if (has_monster) {
        monster_info = FindMonster(location_id);
        W8MonsterRecord* monster_record = GetMonsterDataForInfo(monster_info);
        if (monster_record == 0 || (monster_record->flags & W8_MONSTER_FLAG_NPC) == 0) {
            monster_info->bound_npc_index = -1;
            return;
        }

        if (g_npc_records[npc_id].monster_bound == 0) {
            npc = GetNpcStateByKind(npc_id);
        } else {
            if (monster_info->bound_npc_index > 0) {
                auto* candidate = GetNpcState(monster_info->bound_npc_index);
                if (candidate && candidate->location_id == location_id &&
                    candidate->name_style == npc_id) {
                    npc = candidate;
                }
            }
            if (npc == 0) {
                npc = CreateNpcRuntimeNode(npc_id);
            }
        }
        monster_info->bound_npc_index = npc->partner_index;
    } else {
        npc = GetNpcStateByKind(npc_id);
    }

    if (npc == 0) {
        srAssertFail("npc", __FILE__, __LINE__, 0);
    }
    npc->location_id = location_id;
    npc->is_present = has_monster;
    npc->has_monster = true;
    npc->level_band = GetLevelBand(GetRequiredLoadedLevelID());
    npc->bound_level = static_cast<unsigned char>(GetRequiredLoadedLevelID());
    ReloadNpcScriptResources(npc, GetNpcMonster(npc));
}

/* Expand the record's character block into a fresh group-member character:
   the name, profession and starting level, attributes, skills, known spells
   and worn/carried items, then the derived passes a level advance settles. */
// FUNCTION: WIZ8 0x0050aed0
static bool InitializeNpcCharacter(W8NpcState* npc, W8Character& character)
{
    W8NpcDatabaseRecord* record = npc->record;
    W8NpcCharacterTemplate* source;
    W8ItemInstance item;
    int index;

    if (record->has_group == 0) {
        return false;
    }
    source = &record->character;
    character = W8Character{};
    character.level_band_base = 0;
    character.attribute_point_deficit = 0;
    character.highest_condition = W8_CONDITION_NONE;
    character.enchantment_top = W8_ENCHANTMENT_NONE;
    character.unknown_007d = -1;
    character.personality = -1;
    for (index = 0; index < 12; ++index) {
        EmptyItemRecord(&character.EquippedItem[index], 0, true);
    }
    for (index = 0; index < 8; ++index) {
        EmptyItemRecord(&character.backpack[index], 0, true);
    }
    character.name = source->name;
    character.name_part_2 = source->name_part_2;
    character.iProfession = source->profession;
    character.original_profession = source->profession;
    character.profession_levels[source->profession] = source->level;
    character.iRace = source->race;
    character.gender = static_cast<W8Gender>(source->gender);
    character.portrait_index = source->table_value;
    for (index = 0; index < 7; ++index) {
        character.attributes[index].base = source->attributes[index];
    }
    for (index = 0; index < 0x29; ++index) {
        character.skills[index].points = source->skills[index];
    }
    for (index = 0; index < 12; ++index) {
        if (source->equipment_present[index] != 0 && source->equipment_ids[index] != 0xffff) {
            ReplaceOrCreateItem(&item, static_cast<short>(source->equipment_ids[index]), true, true,
                                false);
            character.EquippedItem[index] = item;
        }
    }
    for (index = 0; index < 8; ++index) {
        if (source->backpack_present[index] != 0 && source->backpack_ids[index] != 0xffff) {
            ReplaceOrCreateItem(&item, static_cast<short>(source->backpack_ids[index]), true, true,
                                false);
            character.backpack[index] = item;
        }
    }
    AdvanceCharacterToLevel(&character, source->level);
    AccumulateEquipmentModifiers(&character, &character.equipment_bonus);
    RebuildCharacterModifierBlock(&character);
    CalcCharacterLevelBand(&character);
    RefreshCharacterSkillAvailability(&character);
    RecalculateCharacterDerivedStats(&character);
    for (index = 1; index < 0x73; ++index) {
        if (source->spells[index - 1] != 0 && CanCharacterLearnSpell(&character, index)) {
            LearnSpell(&character, index, false);
        } else {
            character.spell_learned[index] = 0;
        }
    }
    character.hp_current = character.uiHPMax;
    character.stamina = character.uiStaminaMax;
    for (index = 0; index < W8_SPELL_REALM_COUNT; ++index) {
        character.iSPLeft[index] = character.sp_max[index];
    }
    return true;
}

/* Copy the record's one-based item table into the state's runtime arrays: the
   forty entry item ids and weights, and the table's item-count dice. A record
   whose table id is zero or past the table database keeps the -1 ids and
   leaves the dice untouched. */
// FUNCTION: WIZ8 0x0050b9e0
static void InitializeNpcItemTable(W8NpcState* npc)
{
    unsigned int index;

    memset(npc->item_ids, 0xff, sizeof(npc->item_ids));
    if (npc->record->item_table_id >= static_cast<int>(gXStatus.uiItemTablesInDatabase)) {
        return;
    }
    if (npc->record->item_table_id == 0) {
        return;
    }
    for (index = 0; index < 40; ++index) {
        if (g_item_tables[npc->record->item_table_id - 1].entries[index].selector != 0) {
            npc->item_ids[index] =
                g_item_tables[npc->record->item_table_id - 1].entries[index].item_id;
            npc->item_weights[index] =
                g_item_tables[npc->record->item_table_id - 1].entries[index].weight;
        }
    }
    npc->item_count_dice = g_item_tables[npc->record->item_table_id - 1].item_count_dice;
}

/* Release the monster binding and script held by an active NPC slot. */
// FUNCTION: WIZ8 0x00509EA0
void ReleaseNpcBinding(int value)
{
    auto* npc = GetNpcState(value);
    if (!npc) return;
    npc->has_monster = false;
    npc->script_file.reset();
    if (npc->record->monster_bound != 0) {
        npc->binding_unavailable = 1;
    }
}

/* Serialize every NPC state record into the open NPCT chunk: a version byte,
   the live count, then each 0x13d-byte state. A state whose embedded group
   character exists is followed by a sizeof(W8Character) marker and the
   character record itself. The item lists are a separate FileWrite pass. */

/* Read every NPC state record back from the open NPCT chunk: a version byte,
   the serialized count, then each 0x13d-byte state. A state whose serialized
   group-character pointer is non-null carries a size-prefixed character
   record behind it (format 3 and later; older saves store a raw 0x185c-byte
   block). After the pass every state rebinds to its database record through
   name_style, grouped states reload their script resources, and format two
   and later append the item lists through the second pass. The walk ends by
   backfilling runtime nodes for database records no loaded state claims. */
// FUNCTION: WIZ8 0x00509FC0
void LoadNpcStates(W8Chunk& file)
{
    unsigned char version = 3;
    unsigned int count;
    unsigned int loaded;
    unsigned int npc_id;
    unsigned int size;
    W8NpcState* npc;

    struct StateRollback {
        std::vector<std::unique_ptr<W8NpcState>> previous;
        bool committed = false;
        StateRollback() { previous.swap(g_npc_states); }
        StateRollback(const StateRollback&) = delete;
        ~StateRollback()
        {
            if (!committed) previous.swap(g_npc_states);
        }
    } rollback;
    file.ReadBytes(&version, 1);
    file.ReadBytes(&count, 4);
    if (count > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
        count > (file.RemainingBytes()) / sizeof(wiz8::retail::NpcState)) {
        throw std::runtime_error("invalid NPC state count");
    }
    for (loaded = 0; loaded < count; ++loaded) {
        const auto image = wiz8::retail::read_image<W8NpcState>(file);
        const bool has_character = image.character != 0;
        W8NpcState disk_state;
        wiz8::retail::decode_image(image, disk_state);
        const unsigned int record_id = disk_state.name_style == 0 ? loaded : disk_state.name_style;
        if (record_id >= g_npc_records.size() ||
            record_id > std::numeric_limits<unsigned char>::max()) {
            throw std::runtime_error("invalid NPC database index");
        }
        disk_state.name_style = static_cast<unsigned char>(record_id);
        disk_state.record = &g_npc_records[record_id];
        if (has_character) {
            // Version 3 sizes the character block; older saves store 0x185c bytes.
            size = 0x185c;
            if (version >= 3) file.ReadBytes(&size, sizeof(size));
            if (size > wiz8::retail::kCharacterBytes) {
                throw std::runtime_error("invalid NPC character size");
            }
            wiz8::retail::read_exact(file, disk_state.character.emplace(), size);
        }
        disk_state.partner_index = static_cast<unsigned char>(g_npc_states.size());
        g_npc_states.reserve(g_npc_states.size() + 1);
        g_npc_states.push_back(std::make_unique<W8NpcState>(std::move(disk_state)));
    }
    for (loaded = 0; loaded < count; ++loaded) {
        npc = g_npc_states[loaded].get();

        npc->script_file.reset();
        npc->has_monster = false;
        npc->record = &g_npc_records[npc->name_style];
        if (npc->is_grouped) {
            npc->is_present = false;
            npc->has_monster = true;
            npc->level_band = GetLevelBand(GetRequiredLoadedLevelID());
            npc->bound_level = GetRequiredLoadedLevelID();
            ReloadNpcScriptResources(npc, GetNpcMonster(npc));
        }
        if (npc->spawned == 0xffff) {
            npc->spawned = 0;
        }
    }
    if (version > 1) {
        if (!LoadNpcItemLists(file)) {
            throw std::runtime_error("invalid NPC stock lists");
        }
    }
    for (npc_id = 0; npc_id < g_npc_records.size(); ++npc_id) {
        if (g_npc_records[npc_id].monster_bound == 0 && GetNpcStateByKind(npc_id) == 0) {
            CreateNpcRuntimeNode(npc_id);
        }
    }
    rollback.committed = true;
}

/* Stage all stock lists before replacing any live NPC's inventory. */
// FUNCTION: WIZ8 0x0050AAF0
static bool LoadNpcItemLists(W8Chunk& file)
try {
    std::vector<std::vector<W8NpcItemEntry>> pending(g_npc_states.size());
    for (auto& items : pending) {
        unsigned int item_count;
        file.ReadBytes(&item_count, sizeof(item_count));
        if (item_count > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
            item_count > (file.RemainingBytes()) / sizeof(W8NpcItemEntry)) {
            return false;
        }
        items.resize(item_count);
        file.ReadBytes(items.data(), items.size() * sizeof(W8NpcItemEntry));
    }
    for (std::size_t npc_index = 0; npc_index < g_npc_states.size(); ++npc_index) {
        g_npc_states[npc_index]->items = std::move(pending[npc_index]);
    }
    return true;
} catch (const std::exception&) {
    return false;
}

/* Hand back the NPC binding selected by a monster-list index, or null when
   the monster's record is missing, is not NPC-routed, binds no NPC, or the
   binding has been released. */
// FUNCTION: WIZ8 0x0050A440
W8NpcState* FindNpcBindingForMonster(W8MonsterInfo* monster_info)
{
    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    W8NpcState* npc;

    if (record == 0 || (record->flags & W8_MONSTER_FLAG_NPC) == 0 || record->npc_kind == 0xfa) {
        return 0;
    }
    npc = g_npc_states[monster_info->bound_npc_index].get();
    if (npc->binding_unavailable == 0) {
        return npc;
    }
    return 0;
}

/* The NPC bound to a monster's script part while its binding is still
   available; `allow_unavailable` also hands back a released binding. */
// FUNCTION: WIZ8 0x0050A4A0
W8NpcState* GetNpcStateForMonsterInfo(W8MonsterInfo* monster_info, bool allow_unavailable)
{
    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    W8NpcState* npc;

    if (record == 0 || (record->flags & W8_MONSTER_FLAG_NPC) == 0 || record->npc_kind == 0xfa) {
        return 0;
    }
    npc = g_npc_states[monster_info->bound_npc_index].get();
    if (npc->binding_unavailable == 0 || allow_unavailable) {
        return npc;
    }
    return 0;
}

static void AdjustNpcDisposition(W8NpcState* npc, signed char delta)
{
    int sum = npc->disposition + delta;
    if (sum > 99) {
        npc->disposition = 99;
    } else if (sum < 0) {
        npc->disposition = 0;
    } else {
        npc->disposition += delta;
    }
}

static void DebitNpcTradePool(W8NpcState* npc, unsigned int price)
{
    int level = static_cast<int>(GetBestPartySkillLevel(W8_SKILL_COMMUNICATION).level);
    unsigned int adjusted = price + level * (static_cast<int>(price & 0xffff) / 5) / 100;
    if (static_cast<int>(npc->trade_pool - (adjusted & 0xffff)) < 0) {
        npc->trade_pool = 0;
    } else {
        npc->trade_pool = static_cast<unsigned short>(npc->trade_pool - adjusted);
    }
    if (npc->trade_pool != 0) {
        return;
    }
    GetNpcDisposition(npc);
    level = GetNpcDisposition(npc);
    if (level < W8_NPC_DISPOSITION_HOSTILE) {
        level = 2;
    } else {
        level = level < W8_NPC_DISPOSITION_FRIENDLY;
    }
    if (level != 0) {
        npc->disposition = 0x4b;
    }
    npc->trade_pool = g_npc_records[npc->name_style].trade_pool;
}

/* One dialogue interaction against an NPC. The action kind selects the path:
   talking and the level-scaled charm shift disposition through the record's
   signed scale bytes, paying gold and selling an item draw the record's trade
   pool down by the party's Communication-adjusted price, and the scripted
   kind adds its operand straight to disposition. */
// FUNCTION: WIZ8 0x0050A570
void ApplyNpcInteraction(W8NpcState* npc, int kind, std::optional<int> value, W8ItemInstance* item,
                         unsigned int gold)
{
    switch (static_cast<char>(kind)) {
    case 0: {
        int scale = npc->record->talk_scale;
        int quotient;
        int level;
        int delta;

        npc->talk_cooldown_active = true;
        npc->talk_cooldown_clock = g_status.world_clock;
        const BestPartySkill best_skill = GetBestPartySkillLevel(W8_SKILL_COMMUNICATION);
        level = static_cast<int>(best_skill.level);
        if (scale < 1) {
            quotient = -scale / 5;
        } else {
            quotient = scale / 5;
        }
        delta = scale + level * quotient / 100;
        AdjustNpcDisposition(npc, static_cast<signed char>(delta));
        if (best_skill.party_slot) {
            PracticeCharacterSkill(&g_status.buffers.Char[*best_skill.party_slot],
                                   W8_SKILL_COMMUNICATION, 8, false);
        }
        GetNpcDisposition(npc);
        return;
    }
    case 1: {
        W8MonsterInfo* monster_info;
        W8MonsterRecord* monster_record;
        unsigned int monster_level;
        unsigned int total_level = 0;
        unsigned int count = 0;
        unsigned int average_level;
        int scale;
        int quotient;
        int level;
        int delta;
        int index;

        npc->trade_cooldown_active = true;
        npc->trade_cooldown_clock = g_status.world_clock;
        if (!npc->has_monster || !npc->is_present) {
            monster_info = 0;
        } else {
            monster_info = FindMonster(npc->location_id);
        }
        monster_record = GetMonsterDataForInfo(monster_info);
        monster_level = monster_record->effective_level;
        if (monster_level < 1) {
            monster_level = 1;
        }
        for (index = 0; index < 8; ++index) {
            W8Character* character = &g_status.buffers.Char[index];
            if (g_status.buffers.XChar[index].fOccupied && character->hp_current != 0 &&
                character->highest_condition < W8_CONDITION_ASLEEP) {
                ++count;
                total_level += character->uiExpLevel;
            }
        }
        if (count == 0) {
            average_level = 1;
        } else {
            average_level = total_level / count;
        }
        scale = npc->record->charm_scale;
        const BestPartySkill best_skill = GetBestPartySkillLevel(W8_SKILL_COMMUNICATION);
        level = static_cast<int>(best_skill.level);
        if (scale < 1) {
            quotient = -scale / 5;
        } else {
            quotient = scale / 5;
        }
        delta = scale + level * quotient / 100;
        if (delta > 0) {
            delta = static_cast<int>(average_level * delta / monster_level);
        }
        AdjustNpcDisposition(npc, static_cast<signed char>(delta));
        if (best_skill.party_slot) {
            PracticeCharacterSkill(&g_status.buffers.Char[*best_skill.party_slot],
                                   W8_SKILL_COMMUNICATION, 5, false);
        }
        GetNpcDisposition(npc);
        return;
    }
    case 4: {
        AdjustNpcDisposition(npc, static_cast<signed char>(gold));
        GetNpcDisposition(npc);
        return;
    }
    case 2: {
        SpendPartyGold(gold);
        DebitNpcTradePool(npc, gold);
        break;
    }
    case 3: {
        unsigned int price = GetItemStackValue(item);
        DebitNpcTradePool(npc, price);
        break;
    }
    default:
        break;
    }
    GetNpcDisposition(npc);
}

/* The 0..127 theft score the pickpocket resolutions compare against a d100
   roll: the character's stealth skill (rogues add a dexterity share), a
   quarter of the NPC's disposition swing, a random penalty scaled by the
   NPC's suspicion byte, the level gap to the NPC's monster, and - when an
   item is offered - its weight and price penalties, all scaled by the
   character's difficulty. */
// FUNCTION: WIZ8 0x0050BAF0
static char ScoreNpcTheft(W8Character* character, W8NpcState* npc, int item_id, int count)
{
    W8ItemInstance item;
    int score;

    unsigned int skill = character->skills[W8_SKILL_PICKPOCKET].level;
    if (character->iProfession == W8_PROFESSION_ROGUE) {
        skill += character->attributes[W8_ATTRIBUTE_DEXTERITY].effective / 10;
    } else if (static_cast<int>(skill) >= 1) {
        skill += character->attributes[W8_ATTRIBUTE_DEXTERITY].effective / 0x14;
    }
    score = skill + (GetNpcDisposition(npc) - 0x32) / 4;
    score -= (Random(5) + 10) * static_cast<signed char>(npc->suspicion);
    W8MonsterInfo* monster_info = GetNpcMonsterInfo(npc);
    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    unsigned int penalty = character->skills[W8_SKILL_PICKPOCKET].level >> 2;
    score += (character->uiExpLevel - record->effective_level) * 5;
    if (item_id != -1) {
        unsigned int weight = g_item_records[item_id].weight * count;
        ReplaceOrCreateItem(&item, item_id, false, false, false);
        item.stack_count = static_cast<unsigned char>(count);
        penalty = penalty - weight / 10 - GetItemStackValue(&item) / 100;
    }
    if (static_cast<int>(penalty) > 0) {
        penalty = 0;
    }
    score += penalty;
    unsigned int slot = CharacterPointerToPartySlot(character);
    ScaleValueForCharacterDifficulty(slot, &score);
    unsigned int result = score < 0 ? 0 : score;
    if (static_cast<int>(result) > 0x7e) {
        result = 0x7f;
    }
    return result;
}

static void SeedNpcTheftRoll(W8Character* character, W8NpcState* npc)
{
    int spins = CharacterPointerToPartySlot(character) + npc->name_style * 0xb +
                static_cast<signed char>(npc->suspicion) * 7;
    unsigned int seed = 0;
    for (int index = 2; index < 8; ++index) {
        if (g_status.buffers.XChar[index].fOccupied) {
            seed += g_status.buffers.Char[index].experience;
        }
    }
    SeedRandom(seed);
    for (; spins != 0; --spins) {
        Random(100);
    }
}

/* Resolve one pickpocket attempt against the NPC's stock: the PRNG is reseeded
   from the living party's experience and spun by the NPC's naming style and
   suspicion, then either the purse or a random eligible item is scored.
   Results: 0 item taken, 1 gold taken, 2 refused, 3 caught, 4 nothing left. */
// FUNCTION: WIZ8 0x0050BC90
W8NpcPickpocketResult AttemptNpcPickpocket(W8Character* character, W8NpcState* npc,
                                           W8ItemInstance* item_out, unsigned int* gold_out)
{
    bool empty_pick = false;
    std::vector<int> candidates;
    unsigned int picked = 0xffffffff;
    int index;

    SeedNpcTheftRoll(character, npc);
    for (index = 0; index < 40; ++index) {
        short item_id = npc->item_ids[index];
        if (item_id != -1 && item_id != 0x242 && item_id != 0x244 && item_id != 0x243) {
            candidates.push_back(index);
        }
    }
    char score;
    if (candidates.empty() || Random(100) < 0x21) {
        empty_pick = true;
        score = ScoreNpcTheft(character, npc, -1, 0);
    } else {
        W8ItemInstance item;
        picked = candidates[Random(static_cast<int>(candidates.size()))];
        ReplaceOrCreateItem(&item, npc->item_ids[picked], true, true, false);
        score = ScoreNpcTheft(character, npc, item.iItemNo, 1);
    }
    if (static_cast<signed char>(npc->suspicion) < 'd') {
        ++npc->suspicion;
    }
    char roll = static_cast<char>(Random(100));
    if (roll > '_' || score * 2 < roll) {
        return W8_PICKPOCKET_CAUGHT;
    }
    if (score <= roll) {
        return W8_PICKPOCKET_FAILED;
    }
    if (empty_pick) {
        if (npc->gold != 0) {
            PracticeCharacterSkill(character, W8_SKILL_PICKPOCKET, 5, false);
            unsigned int taken = Random(100) * 7;
            if (static_cast<unsigned int>(npc->gold) < taken) {
                taken = npc->gold;
            }
            npc->gold -= taken;
            if (npc->gold < 0) {
                npc->gold = 0;
            }
            *gold_out = taken;
            return W8_PICKPOCKET_GOLD_TAKEN;
        }
        if (candidates.empty()) {
            return W8_PICKPOCKET_EMPTY;
        }
        return W8_PICKPOCKET_FAILED;
    }
    if (npc == 0) {
        srAssertFail("npc", __FILE__, __LINE__, 0);
    }
    short item_id = npc->item_ids[picked & 0xff];
    if (item_id != -1) {
        if (item_out != 0) {
            ReplaceOrCreateItem(item_out, item_id, true, true, false);
        }
        npc->item_ids[picked & 0xff] = -1;
        PracticeCharacterSkill(character, W8_SKILL_PICKPOCKET, 5, false);
    }
    return W8_PICKPOCKET_ITEM_TAKEN;
}

/* The trade-screen steal of one offered item, scored by the same pickpocket
   roll: 0 takes it (and practices the skill), 1 is refused, 2 is caught. */
// FUNCTION: WIZ8 0x0050C040
char AttemptNpcItemTheft(W8Character* character, W8NpcState* npc, int item_id, int count)
{
    SeedNpcTheftRoll(character, npc);
    char score = ScoreNpcTheft(character, npc, item_id, count);
    if (static_cast<signed char>(npc->suspicion) < 'd') {
        ++npc->suspicion;
    }
    char roll = static_cast<char>(Random(100));
    if (roll < '`' && roll <= score * 2) {
        if (roll < score) {
            PracticeCharacterSkill(character, W8_SKILL_PICKPOCKET, 5, false);
            return W8_ITEM_THEFT_SUCCEEDED;
        }
        return W8_ITEM_THEFT_FAILED;
    }
    return W8_ITEM_THEFT_CAUGHT;
}

/* Probe the navigator from the party eye at three height bands, reporting
   whether any band reaches. */
// FUNCTION: WIZ8 0x0050B2F0
static bool ProbeNpcPlacementNearParty(int /*party_slot*/, int /*mode*/, glm::vec3* position_out)
{
    glm::vec3 party_position;
    float yaw;

    party_position = cameraLocation();
    party_position.y -= g_default_world_height;
    yaw = GetCameraYawRadians() + kTargetingQuarterPi;
    for (const int mode : {10, 20, 30}) {
        const auto placement = GetWorldOctree()->navigatorPositions(party_position, yaw, 1000.0f, 1,
                                                                    true, false, true, mode, false);
        party_position = placement.source;
        if (!placement.positions.empty()) {
            *position_out = placement.positions.front();
            // The final band writes a result but does not report success.
            return mode != 30;
        }
    }
    return false;
}

/* The frame-0x10 callback the 0x1b6 NPC cycle installs: mark the monster,
   reset its navigator to the origin, and fire the VOC_BELA_CC voice event on
   the 0x89 NPC kind sharing the queue. */
// FUNCTION: WIZ8 0x0050D480
void TriggerBelaVoice(W8Monster* monster)
{
    glm::vec3 position;

    monster->runtime_flags |= W8_MONSTER_PARKED;
    position = glm::vec3(0);
    monster->SetPosition(&position);

    W8NpcState* npc = 0;

    npc = GetNpcStateByKind(0x89);
    if (npc != 0) {
        QueueNpcScriptNotice(npc, 0, std::nullopt, false, 0);
        return;
    }
    srAssertFail("npc", __FILE__, __LINE__, "Cannot find VOC_BELA_CC");
}

/* The NPC side of the global frame. Two timed world events first: one retires
   NPC monster 0x1b3 through its dying state, the next starts the 0x1b6 cycle
   with a callback. Then a one-shot pass over the NPC states releases the
   monster binding of the partner each candidate names. Afterwards the group
   event counter can consume a fact and run the NPC 0x8d teardown, the party's
   portrait rows can trigger their NPC's spoken event, and two long reward
   timers set their facts. */
// FUNCTION: WIZ8 0x0050D530
void UpdateNpcEvents(void)
{
    W8MonsterGroup* group;
    W8MonsterInfo* monster_info;
    W8NpcState* npc;

    if (g_status.trang_check_pending &&
        static_cast<unsigned int>(g_status.world_clock - g_status.trang_check_clock) > 0x2a30) {
        if (GetFact(W8_FACT_ALIGNMENT_UMPANI) != 0 && Random(100) < 6) {
            SetFact(W8_FACT_TRANG_YOU_ARE_BUSTED, 1, false);
        }
        g_status.trang_check_pending = false;
    }
    /* The ticks live in the serialized status block at 32 bits wide, so the
       elapsed check stays a wrap-tolerant 32-bit difference. */
    if (g_status.savant_hack_tick != 0 &&
        int32_t(uint32_t(w8_get_ticks()) - uint32_t(g_status.savant_hack_tick)) > 0x32) {
        group = FindFirstMonsterByID(0x1b3);
        if (group != 0) {
            monster_info = FindMonster(group->leader_location_id);
            MonsterStartsDying(monster_info, true);
        }
        g_status.savant_hack_tick = 0;
        g_status.bela_cycle_tick = int(uint32_t(w8_get_ticks()));
    }
    if (g_status.bela_cycle_tick != 0 &&
        int32_t(uint32_t(w8_get_ticks()) - uint32_t(g_status.bela_cycle_tick)) > 0x1388) {
        group = FindFirstMonsterByID(0x1b6);
        if (group != 0) {
            monster_info = FindMonster(group->leader_location_id);
            StartMonsterCycle(monster_info, W8_MONSTER_CYCLE_ATTACK_LASH, W8_ANIMATION_PLAY_ONCE);
            monster_info->p3D->SetCycleCallback(0x10, TriggerBelaVoice);
        }
        g_status.bela_cycle_tick = 0;
    }

    if (g_status.npc_restore_pending) {
        for (int slot = 0; slot < static_cast<int>(g_npc_states.size()); ++slot) {
            npc = g_npc_states[slot].get();
            if (npc->binding_unavailable != 0 || !npc->restored) {
                g_status.npc_restore_pending = false;
                continue;
            }
            W8NpcState* partner = GetNpcStateByKind(npc->name_style);
            if (partner == nullptr || !partner->has_monster) {
                g_status.npc_restore_pending = false;
                continue;
            }
            if (partner->is_present) {
                monster_info = FindMonster(partner->location_id);
                if (monster_info != 0) {
                    RemoveMonster(monster_info, W8MonsterRemoval::Despawn);
                }
            }
            ReleaseNpcBinding(partner->partner_index);
            g_status.npc_restore_pending = false;
        }
    }

    if (!gXStatus.fCombatMode && g_status.vi_event_stage > 1) {
        bool run_event = GetFact(W8_FACT_VI_IS_DEAD) != 0;

        if (!run_event) {
            for (const auto& state_owner : g_npc_states) {
                auto* candidate = state_owner.get();
                if (candidate->record->kind == 99) {
                    if (candidate != 0 && static_cast<unsigned char>(candidate->spawned) != 0) {
                        run_event = true;
                    }
                    break;
                }
            }
        }
        if (run_event) {
            g_status.vi_event_stage = 0;
            for (const auto& state_owner : g_npc_states) {
                auto* candidate = state_owner.get();
                if (candidate->record->kind == 0x8d) {
                    if (candidate != 0) {
                        BeginNpcDialogue(candidate, 0, 0, 0, 0);
                    }
                    break;
                }
            }
            g_npc_dialogue.Queue(W8NpcStoryAction::endgame_screen);
        }
    }

    if (!gXStatus.fSurprisePossible) {
        for (int slot = 0; slot < 2; ++slot) {
            W8PartySlotRow* row = &g_status.buffers.XChar[slot];
            W8Character* character = &g_status.buffers.Char[slot];

            if (row->fOccupied && character->hp_current != 0) {
                W8NpcState* npc_state = GetNpcState(row->npc_index);
                if (row->npc_bound && npc_state &&
                    (g_status.world_clock - npc_state->event_clock) > 0x168) {
                    if (Random(2) == 0) {
                        npc_state->event_clock = g_status.world_clock + Random(6) * 0x3c;
                    } else {
                        int event = Random(2) == 0 ? 0x57 : 0x58;
                        QueueCharacterEvent(character, event, 0, kCharacterEventFullVolume);
                        npc_state->event_clock = g_status.world_clock;
                    }
                }
            }
        }
    }

    if (g_status.fact_b8_pending &&
        static_cast<unsigned int>(g_status.world_clock - g_status.fact_b8_clock) > 0x2a300) {
        g_status.fact_b8_pending = false;
        SetFact(W8_FACT_MOOK_CHAOS_STOLEN, 1, false);
    }
    if (g_status.greeting_pending && (g_status.world_clock - g_status.binding_reset_clock) > 0x3c) {
        g_status.greeting_pending = false;
        SelectStartNpcGreeting();
    }
}

/* Reset the NPC bindings of the first party slots at level entry: stamp the
   sight clock mark, then for each occupied, living slot clear the bound NPC's
   release flag and the slot's own flag and run the per-slot companion reset.
   The lookup is GetNpcState's; the binary's own null test only zeroes the
   result before the store still writes through it, so retail leaves that
   invariant-violating path as a null write. */
// FUNCTION: WIZ8 0x0050db50
void ResetNpcBindingsForParty(void)
{
    g_status.binding_reset_clock = g_status.world_clock;
    g_status.binding_reset_pending = true;
    for (int party_slot = 0; party_slot < 2; ++party_slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[party_slot];
        W8Character* character = &g_status.buffers.Char[party_slot];

        if (row->fOccupied && character->hp_current != 0) {
            GetNpcState(row->npc_index)->incapacitated = 0;
            row->npc_bound = false;
            RebuildConditionsAndDerivedStats(party_slot);
        }
    }
}

/* Subtract 0x14 from every attribute adjustment and the 0x13 byte run of the
   character's modifier block while the slot's bound-NPC flag is set. The
   condition/enchantment rebuild at 0x0050E650 runs it as the last source. */
// FUNCTION: WIZ8 0x0050dbf0
void ApplyBoundNpcPenalty(W8Character* character, W8GameplayModifierBlock* target)
{
    unsigned int index;

    if (g_status.buffers.XChar[CharacterPointerToPartySlot(character)].npc_bound == 0) {
        return;
    }
    for (index = 0; index < 7; ++index) {
        target->attribute_adjustments[index] -= 0x14;
    }
    for (index = 0; index < 0x29; ++index) {
        target->skill_bonus[index] -= 0x14;
    }
}

/* Restore the named entity's NPC on its level, or stamp the pending restore
   for the level-entry pass. The runtime node is created when no state for the
   record kind exists. */
// FUNCTION: WIZ8 0x0050C1C0
void RestoreNamedNpcAtLevel(int kind, char level, std::string_view entity_name)
{
    auto* npc = GetNpcStateByKind(kind);
    if (npc == 0) {
        npc = CreateNpcRuntimeNode(kind);
    }
    if (level == GetRequiredLoadedLevelID()) {
        RestoreNpcMonster(npc, std::string(entity_name).c_str());
        npc->restored = false;
        return;
    }
    npc->pending_restore = true;
    wiz8::text::write_utf8_field(entity_name, npc->restore_entity_name);
    npc->pending_restore_level = level;
}

/* Drop the pending-restore flag from every NPC bound to the loaded level whose
   restore check passes. The state vector is re-read after the check because it
   can remove an entry. */
// FUNCTION: WIZ8 0x0050c270
void ClearPendingNpcLevelFlags(void)
{
    for (std::size_t index = 0; index < g_npc_states.size(); ++index) {
        auto* npc = g_npc_states[index].get();
        if (npc->pending_restore && npc->binding_unavailable == 0 &&
            npc->pending_restore_level == GetRequiredLoadedLevelID()) {
            if (RestoreNpcMonster(npc, npc->restore_entity_name)) {
                npc->pending_restore = false;
            }
        }
    }
}

static void ReleaseNpcCompanionMonster(W8NpcState* npc)
{
    if (npc->has_monster) {
        if (npc->is_present) {
            W8MonsterInfo* monster = FindMonster(npc->location_id);
            if (monster != 0) {
                RemoveMonster(monster, W8MonsterRemoval::Despawn);
            }
        }
        ReleaseNpcBinding(npc->partner_index);
    }
}

/* Release the monster binding of every NPC whose stamped release flag matches
   the loaded level. The companion NPC is found by the record kind matching the
   NPC's naming style; its live monster is destroyed and its own binding is
   handed back. */
// FUNCTION: WIZ8 0x0050c2e0
void ReleaseNpcMonsterBindings(void)
{
    for (std::size_t index = 0; index < g_npc_states.size(); ++index) {
        auto* npc = g_npc_states[index].get();
        if (npc->pending_release && npc->binding_unavailable == 0 &&
            npc->pending_release_level == GetRequiredLoadedLevelID()) {
            W8NpcState* companion = GetNpcStateByKind(npc->name_style);
            ReleaseNpcCompanionMonster(companion);
        }
    }
}

/* Release the monster binding held under this NPC's naming style, now or when
   the stamped level is loaded: the same companion lookup the level-entry
   release pass runs - the NPC whose record kind is this state's name_style. */
// FUNCTION: WIZ8 0x0050C440
void ReleaseNpcMonsterBinding(W8NpcState* npc, char level)
{
    if (level == GetRequiredLoadedLevelID()) {
        W8NpcState* companion = GetNpcStateByKind(npc->name_style);
        ReleaseNpcCompanionMonster(companion);
    } else {
        npc->pending_release = true;
        npc->pending_release_level = level;
    }
}

/* Release the monster binding of the first NPC whose record kind matches:
   its live monster is destroyed and the partner node its index names gets its
   binding handed back. */
// FUNCTION: WIZ8 0x0050C680
void ReleaseNpcMonsterByKind(int kind)
{
    W8NpcState* npc = GetNpcStateByKind(kind);
    ReleaseNpcCompanionMonster(npc);
}

/* Place or move this NPC's monster at the named world entity. Without a live
   monster it loads MONSTERS.DBS, finds the NPC-linked species whose name-style
   byte matches, and asks CreateGroup to create it; with a live monster it
   repositions the Navigator subobject. */
// FUNCTION: WIZ8 0x0050c560
static bool RestoreNpcMonster(W8NpcState* npc, const char* entity_name)
{
    glm::vec3 position;
    glm::vec3 copied;

    if (!npc->has_monster) {
        std::unique_ptr<W8MonsterRecord[]> records;
        unsigned int index = 0;

        if (!LoadMonsterDatabase(&records)) return false;
        if (gXStatus.uiMonstersInDatabase != 0) {
            for (; index < gXStatus.uiMonstersInDatabase; ++index) {
                if ((records[index].flags & W8_MONSTER_FLAG_NPC) != 0 &&
                    records[index].npc_kind == npc->name_style) {
                    break;
                }
            }
        }
        if (index == gXStatus.uiMonstersInDatabase) {
            return false;
        }
        if (!FindEntityByName(entity_name, &position, 0, 0)) {
            return false;
        }
        copied = position;
        CreateGroup(index, 1, &copied, true, false, true);
        return true;
    }
    if (!npc->is_present) {
        return false;
    }
    {
        W8MonsterInfo* monster_info = FindMonster(npc->location_id);

        if (monster_info == 0) {
            return false;
        }
        if (!FindEntityByName(entity_name, &position, 0, 0)) {
            return false;
        }
        monster_info->p3D->SetPosition(&position);
        return true;
    }
}

/* Run one marked NPC's scripted event step. The special naming styles run
   first - Vi Domina's fact, Sgt Rubble's two-stage teleport on level 13 and
   Glumph's mission item - then mode 0 arms the one-shot event pass while any
   other mode releases the companion monster binding, and the NPC's own
   restore is scheduled last. */
// FUNCTION: WIZ8 0x0050CF70
void HandleMarkedNpcEvent(W8NpcState* npc, char mode)
{
    W8MonsterInfo* monster_info = GetNpcMonsterInfo(npc);

    if (monster_info != 0 && monster_info->highest_condition > W8_CONDITION_WEBBED) {
        return;
    }
    if (npc->name_style == ' ' || npc->name_style == '!') {
        npc->event_pending = 0;
        npc->restore_done = 0;
        return;
    }
    if (npc->name_style == W8_NPC_VI_DOMINA) {
        SetFact(W8_FACT_INTRO_VI_DISAPPEARS, 0, false);
    }
    if (npc->name_style == ')' && GetRequiredLoadedLevelID() == 0xd) {
        glm::vec3 position;

        if (!GetLocationVarIDByName("CODESgtRubbleTeleport") ||
            GetLocationVarValueByName("CODESgtRubbleTeleport") == 0) {
            if (monster_info != nullptr && FindEntityByName("RubbleCovert", &position, 0, 0)) {
                monster_info->p3D->SetPosition(&position);
                if (!GetLocationVarIDByName("CODESgtRubbleTeleport")) {
                    CreateLocationVar("CODESgtRubbleTeleport", 1);
                } else {
                    SetTriggerVariableByName("CODESgtRubbleTeleport", 1);
                }
                RunNamedTrigger("door08", -1);
            }
            SetFact(W8_FACT_UMISSION_MOVE_RUBBLE_COVERT, 0, false);
            npc->event_pending = 0;
            npc->restore_done = 0;
            return;
        }
        if (GetLocationVarValueByName("CODESgtRubbleTeleport") == 1) {
            if (monster_info != nullptr && FindEntityByName("rubbleUnderWater", &position, 0, 0)) {
                monster_info->p3D->SetPosition(&position);
                SetTriggerVariableByName("CODESgtRubbleTeleport", 2);
            }
            npc->event_pending = 0;
            npc->restore_done = 0;
            return;
        }
    }
    if (npc->name_style == W8_NPC_GLUMPH && GetFact(W8_FACT_UMISSION_SCUBA_DONE) == 0) {
        glm::vec3 position;

        monster_info = GetNpcMonsterInfo(npc);
        if (monster_info != 0) {
            position = monster_info->p3D->GetPosition();
            W8WorldItem* item = AddWorldItem(
                SpawnItem(0x1e6, &position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));

            if (item != 0) {
                ActivateItem(item);
            }
        } else if (FindEntityByName("NP_Glumph", &position, 0, 0)) {
            W8WorldItem* item = AddWorldItem(
                SpawnItem(0x1e6, &position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));

            if (item != 0) {
                ActivateItem(item);
            }
        }
    }
    if (mode != 0) {
        W8NpcState* companion = GetNpcStateByKind(npc->name_style);
        ReleaseNpcCompanionMonster(companion);
    } else {
        npc->restored = true;
        g_status.npc_restore_pending = true;
    }
    if (npc->restore_done == 0) {
        if (npc->name_style == W8_NPC_MYLES && GetFact(W8_FACT_RAPAX_MYLES_IN_JAIL) != 0) {
            RestoreNamedNpcAtLevel(npc->name_style, 0x11, "NP_MylesCell");
        } else {
            RestoreNamedNpcAtLevel(npc->name_style, npc->record->restore_level,
                                   npc->record->restore_entity_name);
        }
    }
    npc->event_pending = 0;
    npc->restore_done = 0;
}

/* Hand back the monster binding of every marked NPC, then find the companion
   whose record kind matches the NPC's naming style and release its live
   monster and its own binding. The state vector is re-read after every
   callback. */
// FUNCTION: WIZ8 0x0050da00
void ReleaseMarkedNpcBindings(void)
{
    for (std::size_t index = 0; index < g_npc_states.size(); ++index) {
        auto* npc = g_npc_states[index].get();
        if (npc->binding_unavailable == 0) {
            if (npc->event_pending != 0) {
                HandleMarkedNpcEvent(npc, 1);
            }
            if (npc->pending_restore) {
                W8NpcState* companion = GetNpcStateByKind(npc->name_style);
                ReleaseNpcCompanionMonster(companion);
            }
        }
    }
}

/* The activation callback RebindNpcLevelTriggers installs on every NPC
   trigger: queue the NPC's script notice, handing it the item on the cursor
   when the trigger's 0x100 flag or the NPC's '{' naming style asks for it. */
// FUNCTION: WIZ8 0x0050abf0
bool NotifyNpcTriggerActivation(Trigger* trigger)
{
    W8ItemInstance* item = 0;

    if (!gXStatus.fNpcDialogueMode) {
        W8NpcState* npc = g_npc_states[trigger->m_lData1].get();

        if ((trigger->flags & W8_TRIGGER_ENABLED) != 0 || npc->name_style == '{') {
            if (g_status.item_in_cursor) {
                item = &g_status.item_in_hand;
            }
        }
        QueueNpcScriptNotice(npc, item, std::nullopt, false, 0);
    }
    return false;
}

/* Rebuild the level's NPC bindings: first drop the followers whose record or
   presence rules changed, then re-install each NPC trigger's activation
   callback and re-stamp the NPC from the loaded level. */
// FUNCTION: WIZ8 0x0050ac60
void RebindNpcLevelTriggers(void)
{
    unsigned int npc_index;

    for (std::size_t index = 0; index < g_npc_states.size(); ++index) {
        auto* npc = g_npc_states[index].get();
        if (npc->has_monster &&
            (npc->record->merchant != 0 || (npc->record->voice_script != 0 && !npc->is_present))) {
            npc->has_monster = false;
            npc->script_file.reset();
            if (npc->record->monster_bound != 0) {
                npc->binding_unavailable = 1;
            }
        }
    }

    for (npc_index = 0;
         npc_index < static_cast<unsigned int>(static_cast<int>(g_npc_states.size()));
         ++npc_index) {
        W8NpcState* npc = g_npc_states[npc_index].get();
        std::string trigger_name;

        if (npc->record->merchant != 0 || npc->record->voice_script != 0) {
            trigger_name = fmt::format("_{}", npc->record->source_name);
            Trigger* trigger = FindTriggerByName(trigger_name.c_str());

            if (trigger != 0) {
                trigger->activation_callback = NotifyNpcTriggerActivation;
                trigger->m_lData1 = static_cast<int>(npc_index);
                npc->has_monster = true;
                npc->level_band =
                    static_cast<unsigned char>(GetLevelBand(GetRequiredLoadedLevelID()));
                npc->bound_level = static_cast<unsigned char>(GetRequiredLoadedLevelID());
                ReloadNpcScriptResources(npc, GetNpcMonster(npc));
                npc->is_present = false;
            }
        }
    }
}

/* Clear the first item_ids slot matching `item_id`, optionally building the
   item into `out` first - the scheduled-stock handoff the pickpocket and trade
   resolutions run. */
// FUNCTION: WIZ8 0x0050BA80
bool ClearNpcScheduledItem(W8NpcState* npc, int item_id, W8ItemInstance* out)
{
    if (npc == 0) {
        srAssertFail("npc", __FILE__, __LINE__, 0);
    }
    for (int index = 0; index < 40; ++index) {
        if (npc->item_ids[index] == item_id) {
            if (out != 0) {
                ReplaceOrCreateItem(out, npc->item_ids[index], true, true, false);
            }
            npc->item_ids[index] = -1;
            return true;
        }
    }
    return false;
}

static bool HasHealthyNpcPartner(unsigned char kind)
{
    if (g_status.buffers.XChar[0].fOccupied) {
        W8NpcState* lead = GetNpcState(g_status.buffers.XChar[0].npc_index);
        if (lead->name_style == kind &&
            g_status.buffers.Char[0].highest_condition < W8_CONDITION_ASLEEP) {
            return true;
        }
    }
    if (g_status.buffers.XChar[1].fOccupied) {
        W8NpcState* lead = GetNpcState(g_status.buffers.XChar[1].npc_index);
        if (lead->name_style == kind &&
            g_status.buffers.Char[1].highest_condition < W8_CONDITION_ASLEEP) {
            return true;
        }
    }
    return false;
}

/* Scan the two bound lead NPCs for ones that refuse the destination level: an
   NPC that serves the destination region but not the current one queues its
   departure event, and on level 13 a Rodan or Drazic travelling without its
   healthy partner does the same. Any queued event raises the travel-confirm
   message and resets the level data vectors. */
// FUNCTION: WIZ8 0x0050DEC0
bool QueueNpcDepartureEvents(int destination_level)
{
    bool queued = false;

    for (int slot = 0; slot < 2; ++slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[slot];
        W8Character* character = &g_status.buffers.Char[slot];

        if (!row->fOccupied || character->hp_current == 0) {
            continue;
        }
        W8NpcState* npc = GetNpcState(row->npc_index);
        if (character->highest_condition >= W8_CONDITION_ASLEEP) {
            continue;
        }
        int service = 0;
        if (g_npc_services[0].service_id != 0xffffffff) {
            while (g_npc_services[service].service_id != 0xffffffff) {
                if (g_npc_services[service].service_id ==
                    static_cast<unsigned int>(GetLevelBand(destination_level))) {
                    if ((npc->record->service_flags & g_npc_services[service].bit) != 0) {
                        int other = 0;
                        bool serves_here = false;
                        if (g_npc_services[0].service_id != 0xffffffff) {
                            while (g_npc_services[other].service_id != 0xffffffff) {
                                if (g_npc_services[other].service_id ==
                                    static_cast<unsigned int>(
                                        GetLevelBand(GetRequiredLoadedLevelID()))) {
                                    if ((npc->record->service_flags & g_npc_services[other].bit) !=
                                        0) {
                                        serves_here = true;
                                    }
                                    break;
                                }
                                ++other;
                            }
                        }
                        if (!serves_here) {
                            int departure = 0;
                            if (g_npc_services[0].service_id != 0xffffffff) {
                                while (g_npc_services[departure].service_id != 0xffffffff) {
                                    if (g_npc_services[departure].service_id ==
                                        static_cast<unsigned int>(
                                            GetLevelBand(destination_level))) {
                                        QueueCharacterEvent(character,
                                                            g_npc_services[departure].npc_id, 0,
                                                            kCharacterEventFullVolume);
                                        queued = true;
                                        break;
                                    }
                                    ++departure;
                                }
                            }
                        }
                    }
                    break;
                }
                ++service;
            }
        }
        if (GetLevelBand(GetRequiredLoadedLevelID()) == 0xd) {
            int event = 0;
            if (npc->name_style == W8_NPC_RODAN) {
                bool paired = HasHealthyNpcPartner(W8_NPC_DRAZIC);
                if (!paired) {
                    event = 0x6c;
                }
            } else if (npc->name_style == W8_NPC_DRAZIC) {
                bool paired = HasHealthyNpcPartner(W8_NPC_RODAN);
                if (!paired) {
                    event = 0x67;
                }
            }
            if (event != 0) {
                QueueCharacterEvent(character, event, 0, kCharacterEventFullVolume);
                queued = true;
            }
        }
    }
    if (queued) {
        g_npc_dialogue.Queue(W8NpcTravelConfirm{destination_level});
        ResetLevelDataVectors();
    }
    return queued;
}

/* Refuse the destination level on behalf of each bound lead NPC: an NPC that
   serves the destination region speaks the group-action line, and a Rodan or
   Drazic whose healthy partner is not also in the party does the same. */
// FUNCTION: WIZ8 0x0050E230
void QueueNpcTravelRefusals(int destination_level)
{
    ClearLevelMovementStopped();
    for (int slot = 0; slot < 2; ++slot) {
        W8PartySlotRow* row = &g_status.buffers.XChar[slot];
        W8Character* character = &g_status.buffers.Char[slot];

        if (!row->fOccupied || character->hp_current == 0) {
            continue;
        }
        W8NpcState* npc = GetNpcState(row->npc_index);
        if (character->highest_condition >= W8_CONDITION_ASLEEP) {
            continue;
        }
        if (NpcOffersService(npc, GetLevelBand(destination_level))) {
            BeginScriptedWorldAction();
            g_npc_dialogue.Queue(W8NpcDismiss{slot});
        }
        if (npc->name_style == W8_NPC_RODAN) {
            if (!HasHealthyNpcPartner(W8_NPC_DRAZIC)) {
                BeginScriptedWorldAction();
                g_npc_dialogue.Queue(W8NpcDismiss{slot});
                return;
            }
        }
        if (npc->name_style == W8_NPC_DRAZIC) {
            if (!HasHealthyNpcPartner(W8_NPC_RODAN)) {
                BeginScriptedWorldAction();
                g_npc_dialogue.Queue(W8NpcDismiss{slot});
                return;
            }
        }
    }
}

/* Clear every item_ids slot that still holds `item_id` - quote entries
   call this after the item leaves the npc's stock so it cannot be sold
   twice. The npc null check is the original's own dead assert: the pointer
   was already dereferenced by the scan. */
// FUNCTION: WIZ8 0x0050E4B0
void ClearNpcItemId(W8NpcState* npc, int item_id)
{
    int index;
    unsigned char slot;

    for (index = 0; index < 40; ++index) {
        if (npc->item_ids[index] == item_id) {
            if (npc == 0) {
                srAssertFail("npc", __FILE__, __LINE__, 0);
            }
            slot = index;
            if (npc->item_ids[slot] != -1) {
                npc->item_ids[slot] = -1;
            }
        }
    }
}
