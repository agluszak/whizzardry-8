#include "wiz8/fact_state.h"

#include <utility>
#include <vector>

#include <fmt/format.h>

#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/npc_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/difficulty.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/message_box.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/world/world.h"

bool Game::hasLeadingNpc(W8NpcId kind) const
{
    for (unsigned int slot = 0; slot < 2; ++slot) {
        if (!party.members[slot]) continue;
        const auto* npc = story.npc(party.npcIndex(slot));
        if (npc && npc->name_style == kind &&
            party.characters[slot].highest_condition < W8_CONDITION_ASLEEP)
            return true;
    }
    return false;
}

unsigned char Game::testFact(W8FactId fact)
{
    std::vector<Event> outcomes;
    const auto value = testFact(fact, outcomes);
    events.publish(std::move(outcomes));
    return value;
}

unsigned char Game::testFact(W8FactId fact, std::vector<Event>& outcomes)
{
    if (static_cast<unsigned>(fact) >= W8_FACT_SLOT_COUNT) return 0;
    const auto value = evaluateFact(fact, outcomes);
    if (status.log_fact_checks && static_cast<unsigned>(fact) < data.facts.size())
        outcomes.emplace_back(
            TextNotice{W8_FONT_PALETTE_YELLOW,
                       fmt::format("Checking fact {} which is {}", data.facts[fact].symbolic_name,
                                   value ? "TRUE" : "FALSE")});
    return value;
}

/* Evaluate one derived fact from the party's live state. Most facts either
   read the stored fact array directly or combine a handful of primary facts;
   the hard-coded rules below are the original's. The helper evaluates
   recursively for the combined facts and logs each dependency when the
   fact-check log is enabled. */
// This legacy script test is not universally observational: covert-training success
// consumes a one-shot latch. Preserve evaluation order until that command is separated.
unsigned char Game::evaluateFact(W8FactId fact_id, std::vector<Event>& outcomes)
{
    unsigned char value;
    unsigned char count;

    if (fact_id == W8_FACT_DEVICE_ONE || fact_id == W8_FACT_DEVICE_TWO ||
        fact_id == W8_FACT_DEVICE_THREE) {
        count = inventory.has(0x243);
        if (inventory.has(0x242)) {
            ++count;
        }
        if (inventory.has(0x244)) {
            ++count;
        }
        if (fact_id == W8_FACT_DEVICE_ONE) {
            return count == 1;
        }
        if (fact_id == W8_FACT_DEVICE_TWO) {
            return count == 2;
        }
        return count == 3;
    }

    if (fact_id < W8_FACT_FACTION_HIGARDI_FELLOW_FRIENDLY) {
        if (fact_id == W8_FACT_FACTION_HIGARDI_BANK_FRIENDLY) {
            return story.factionDisposition(W8_FACTION_HIGARDI_BANK) == W8_FACTION_FRIENDLY;
        }
        switch (fact_id) {
        case W8_FACT_ARNIKA_PARTY_HAS_MARTEN_BADGE:
            return inventory.has(0x27b);
        case W8_FACT_FACTION_HIGARDI_COMMON_FRIENDLY:
            return story.factionDisposition(W8_FACTION_HIGARDI_COMMON) == W8_FACTION_FRIENDLY;
        case W8_FACT_ALIGNMENT_NONE:
            value = testFact(W8_FACT_ALIGNMENT_TRANG, outcomes);
            if (value == 0) {
                value = testFact(W8_FACT_ALIGNMENT_UMPANI, outcomes);
                if (value == 0) {
                    return 1;
                }
            }
            return 0;
        case W8_FACT_FACTION_HIGARDI_HLL_FRIENDLY:
            return story.factionDisposition(W8_FACTION_HIGARDI_HLL) == W8_FACTION_FRIENDLY;
        case W8_FACT_TWO_IN_PARTY:
            return party.members[0].has_value() && party.members[1].has_value();
        case W8_FACT_MOOK_MOOK_IN_PARTY: {
            unsigned int slot = 0;
            while (!party.members[slot].has_value() ||
                   party.characters[slot].iRace != W8_RACE_MOOK ||
                   party.characters[slot].highest_condition > W8_CONDITION_WEBBED) {
                if (slot >= 7) {
                    return 0;
                }
                ++slot;
            }
            break;
        }
        case W8_FACT_PEACE_DRAZIC_IN_PARTY:
            return hasLeadingNpc(W8_NPC_DRAZIC);
        case W8_FACT_PEACE_RODAN_IN_PARTY:
            return hasLeadingNpc(W8_NPC_RODAN);
        case W8_FACT_PEACE_TWO_IN_PARTY:
            if (!hasLeadingNpc(W8_NPC_RODAN) || !hasLeadingNpc(W8_NPC_DRAZIC)) {
                return 0;
            }
            break;
        case W8_FACT_VI_IN_PARTY:
            return hasLeadingNpc(W8_NPC_VI_DOMINA);
        case W8_FACT_GLUMPH_DEAD: {
            W8NpcState* npc = story.npcOfKind(0x2b);
            if (npc == 0) {
                return 0;
            }
            return static_cast<unsigned char>(npc->spawned);
        }
        case W8_FACT_UMISSION_TRAIN_FIRE_NOAMMO:
            value = testFact(W8_FACT_UMISSION_TRAIN_COVERT_ASSIGN, outcomes);
            if (value == 0) {
                value = testFact(W8_FACT_UMISSION_TRAIN_FIRE_ASSIGN, outcomes);
                if (value != 0 && !inventory.has(0x271) && !inventory.has(0x272)) {
                    return 1;
                }
            }
            return 0;
        case W8_FACT_UMISSION_TRAIN_COVERT_DONE:
            if (!status.fact_88_latch) {
                if (inventory.count(0x1c4) < 5) {
                    return 0;
                }
                status.fact_88_latch = true;
                return 1;
            }
            break;
        case W8_FACT_UMISSION_TRAIN_WETSUIT:
            return EveryCharacterHasItem(party, 0x1e5, W8ItemReach::Worn);
        case W8_FACT_UMISSION_IUFPASS_LEVEL2:
            return inventory.has(0x268);
        case W8_FACT_RAPAX_AWAY_CAMP_EXISTS:
            count = inventory.has(0x242);
            if (inventory.has(0x243)) {
                ++count;
            }
            if (inventory.has(0x244)) {
                ++count;
            }
            return count >= 2;
        default:
            break;
        }
    } else if (fact_id < W8_FACT_TRYNNIE_FOUNTAIN_ANSWERED_CORRECT) {
        if (fact_id == W8_FACT_FACTION_TRYNNIE_COMMON_FRIENDLY) {
            return story.factionDisposition(W8_FACTION_TRYNNIE) == W8_FACTION_FRIENDLY;
        }
        switch (fact_id) {
        case W8_FACT_TRYNNIE_PC_HAS_HELM:
            return inventory.has(0x239);
        case W8_FACT_FACTION_UMPANI_COMMON_FRIENDLY:
            return story.factionDisposition(W8_FACTION_UMPANI) == W8_FACTION_FRIENDLY;
        case W8_FACT_FACTION_TRANG_COMMON_FRIENDLY:
            return story.factionDisposition(W8_FACTION_TRANG) == W8_FACTION_FRIENDLY;
        case W8_FACT_FACTION_HIGARDI_FELLOW_FRIENDLY:
            return story.factionDisposition(W8_FACTION_BROTHERHOOD) == W8_FACTION_FRIENDLY;
        case W8_FACT_MYLES_IN_PARTY:
            return hasLeadingNpc(W8_NPC_MYLES);
        case W8_FACT_FATHER_IN_PARTY:
            if (status.rpc_active) {
                for (unsigned int slot = 0; slot < 8; ++slot) {
                    if (party.members[slot].has_value() &&
                        slot == static_cast<unsigned int>(status.sedexus_party_slot)) {
                        return 1;
                    }
                }
            }
            return 0;
        case W8_FACT_PARTY_HAS_BLOODLUST_SWORD:
            return inventory.has(0x294);
        case W8_FACT_RAPAX_SAVANT_ALLIANCE:
            value = testFact(W8_FACT_ASTRAL_POSSESS, outcomes);
            if (value != 0) {
                return 1;
            }
            value = testFact(W8_FACT_DESTINAE_POSSESS, outcomes);
            if (value != 0) {
                return 1;
            }
            value = testFact(W8_FACT_CHAOS_POSSESS, outcomes);
            if (value != 0) {
                return 1;
            }
            return 0;
        default:
            break;
        }
    } else if (fact_id < W8_FACT_QUEST_SHAMAN_BRIDGE_EASY) {
        if (fact_id == W8_FACT_QUEST_SHAMAN_GET_HELM) {
            return !inventory.has(0x239);
        }
        switch (fact_id) {
        case W8_FACT_FACTION_RAPAX_COMMON_FRIENDLY:
            return story.factionDisposition(W8_FACTION_RAPAX_COMMON) == W8_FACTION_FRIENDLY;
        case W8_FACT_TRYNNIE_SPARKLE_IN_PARTY:
            return hasLeadingNpc(W8_NPC_SPARKLE);
        case W8_FACT_VI_IS_DEAD: {
            if (!hasLeadingNpc(W8_NPC_VI_DOMINA)) {
                return story.facts[fact_id];
            }
            W8NpcState* npc = story.npcOfKind(0x18);
            if (npc != 0 &&
                party.characters[npc->group_index].highest_condition >= W8_CONDITION_ASLEEP) {
                return 1;
            }
            return 0;
        }
        case W8_FACT_QUEST_SHAMAN_DESTINY_TWO:
            value = testFact(W8_FACT_QUEST_MARTEN_DIARY, outcomes);
            if (value == 0) {
                return 1;
            }
            value = testFact(W8_FACT_QUEST_MARTEN_IDOL, outcomes);
            if (value == 0) {
                return 1;
            }
            return 0;
        default:
            break;
        }
    } else {
        if (fact_id == W8_FACT_QUEST_PEACE_UNLOCK_CAGES) {
            value = testFact(W8_FACT_RODAN_LOCKED_IN_CAGE, outcomes);
            if (value != 0) {
                return 1;
            }
            value = testFact(W8_FACT_DRAZIC_LOCKED_IN_CAGE, outcomes);
            if (value != 0) {
                return 1;
            }
            return 0;
        }
        if (fact_id == W8_FACT_FACTION_RAPAX_TEMPLAR_FRIENDLY) {
            return story.factionDisposition(W8_FACTION_RAPAX_TEMPLAR) == W8_FACTION_FRIENDLY;
        }
        if (fact_id == W8_FACT_PARTY_DEACTIVATED_BOMB) {
            value = testFact(W8_FACT_DS_BOMB_DEACTIVATED, outcomes);
            return value;
        }
    }
    return story.facts[fact_id];
}

void Game::setFact(W8FactId fact_id, unsigned char value, bool notify)
{
    if (static_cast<unsigned>(fact_id) >= W8_FACT_SLOT_COUNT) return;
    const auto previous = story.facts[fact_id];
    story.facts[fact_id] = value;
    if (value != previous) recordFactChange(fact_id, notify);
    // Repeated writes still run mandatory consequences, without another journal entry.
    HandleFactChange(MakeScriptContext(*this), fact_id, value);
    if (status.log_fact_checks && static_cast<unsigned>(fact_id) < data.facts.size()) {
        events.push(TextNotice{W8_FONT_PALETTE_YELLOW,
                               fmt::format("{} set to {}", data.facts[fact_id].symbolic_name,
                                           value ? "TRUE" : "FALSE")});
    }
}

void Game::recordFactChange(W8FactId fact_id, bool notify)
{
    story.journal.push_back({.level = level.primary ? level.primary->level_id : -1,
                             .fact = fact_id,
                             .alternate_text = testFact(fact_id) != 0});
    if (!notify || static_cast<unsigned>(fact_id) >= data.facts.size()) return;
    const int visibility = status.difficulty == W8_DIFFICULTY_NOVICE   ? 2
                           : status.difficulty == W8_DIFFICULTY_NORMAL ? 1
                           : status.difficulty == W8_DIFFICULTY_EXPERT ? 0
                                                                       : static_cast<int>(fact_id);
    const auto& record = data.facts[fact_id];
    if (record.visibility > visibility) return;
    // Evaluation may consume a one-shot latch, so retain both original evaluation points.
    const auto& description = testFact(fact_id) ? record.alternate_description : record.description;
    if (description.empty()) return;
    if (activity.conversation)
        npc_dialogue.Queue(W8NpcCommand::journal_quote);
    else
        events.push(JournalEntryAdded{data.strings[0x74a]});
}

/* The opening sequence consumes only the imported story choices. The decoded
   Wizardry 7 records have already been discarded after character conversion. */
void Game::seedNewGameFacts()
{
    if (story.imported_origin) {
        setFact(W8_FACT_IMPORT, 1, false);
        switch (story.imported_origin->alignment) {
        case Story::ImportedOrigin::Alignment::Umpani:
            setFact(W8_FACT_IMPORT_UMPANI, 1, false);
            break;
        case Story::ImportedOrigin::Alignment::Trang:
            setFact(W8_FACT_IMPORT_TRANG, 1, false);
            break;
        default:
            setFact(W8_FACT_IMPORT_NOALIGN, 1, false);
            break;
        }
        if (story.imported_origin->barlone_dead) {
            setFact(W8_FACT_BARLONE_WAS_DEAD, 1, false);
        }
        if (story.imported_origin->rodan_dead) {
            setFact(W8_FACT_RODAN_WAS_DEAD, 1, false);
        }
    } else {
        setFact(W8_FACT_VIRGIN, 1, false);
        setFact(W8_FACT_QUEST_VIRGIN_1, 1, false);
        setFact(W8_FACT_QUEST_VIRGIN_2, 1, false);
        setFact(W8_FACT_QUEST_VIRGIN_3, 1, false);
    }
    story.imported_origin.reset();
}

void Game::acquireItemFacts(const W8ItemInstance& item)
{
    switch (item.iItemNo) {
    case 0x243:
        if (!testFact(W8_FACT_DESTINAE_POSSESS)) {
            setFact(W8_FACT_DESTINAE_POSSESS, 1);
        }
        if (testFact(W8_FACT_QUEST_GET_DD)) {
            setFact(W8_FACT_QUEST_GET_DD, 0);
        }
        break;
    case 0x242:
        if (!testFact(W8_FACT_ASTRAL_POSSESS)) {
            setFact(W8_FACT_ASTRAL_POSSESS, 1);
        }
        if (testFact(W8_FACT_QUEST_GET_AD)) {
            setFact(W8_FACT_QUEST_GET_AD, 0);
        }
        break;
    case 0x244:
        if (!testFact(W8_FACT_CHAOS_POSSESS)) {
            setFact(W8_FACT_CHAOS_POSSESS, 1);
        }
        if (testFact(W8_FACT_QUEST_GET_CM)) {
            setFact(W8_FACT_QUEST_GET_CM, 0);
        }
        status.fact_b8_pending = true;
        status.fact_b8_clock = status.world_clock;
        break;
    case 0x264:
        if (!testFact(W8_FACT_ASTRAL_FAKE_POSSESS)) {
            setFact(W8_FACT_ASTRAL_FAKE_POSSESS, 1);
            setFact(W8_FACT_ASTRAL_FAKE_POSSESS1, 1);
        }
        break;
    case 0x201:
        setFact(W8_FACT_QUEST_BRAFFIT_CIERDAN, 0);
        break;
    case 0x27c:
        setFact(W8_FACT_QUEST_MYLES_FIND_DIAMOND, 0);
        break;
    case 0x1d2:
        if (testFact(W8_FACT_QUEST_MYLES_WEAPONS_CACHE)) {
            setFact(W8_FACT_QUEST_MYLES_WEAPONS_CACHE, 0);
        }
        break;
    }
}
