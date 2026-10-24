#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "wiz8/character_skills.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/monster_roster.h"
#include "wiz8/npc_items.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/random.h"
#include "wiz8/story.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

static void InitializeNpcCharacter(const GameData& data, Rng& rng,
                                   const W8NpcCharacterTemplate& source, W8Character& character)
{
    int index;

    character = W8Character{};
    character.level_band_base = 0;
    character.attribute_point_deficit = 0;
    character.highest_condition = W8_CONDITION_NONE;
    character.enchantment_top = W8_ENCHANTMENT_NONE;
    character.unknown_007d = -1;
    character.personality = -1;
    character.name = source.name;
    character.name_part_2 = source.name_part_2;
    character.iProfession = source.profession;
    character.original_profession = source.profession;
    character.profession_levels[source.profession] = source.level;
    character.iRace = source.race;
    character.gender = static_cast<W8Gender>(source.gender);
    character.portrait_index = source.table_value;
    for (index = 0; index < 7; ++index) {
        character.attributes[index].base = source.attributes[index];
    }
    for (index = 0; index < 0x29; ++index) {
        character.skills[index].points = source.skills[index];
    }
    for (index = 0; index < 12; ++index) {
        if (source.equipment_present[index] != 0 && source.equipment_ids[index] != 0xffff) {
            character.EquippedItem[index] = MakeItem(
                data, rng, static_cast<short>(source.equipment_ids[index]), true, true, false);
        }
    }
    for (index = 0; index < 8; ++index) {
        if (source.backpack_present[index] != 0 && source.backpack_ids[index] != 0xffff) {
            character.backpack[index] = MakeItem(
                data, rng, static_cast<short>(source.backpack_ids[index]), true, true, false);
        }
    }
    AdvanceCharacterToLevel(&character, source.level);
    CalcCharacterLevelBand(&character);
    RefreshCharacterSkillAvailability(data, &character);
    UpdateStandaloneCharacterStats(data, character, {});
    for (index = 1; index < 0x73; ++index) {
        if (source.spells[index - 1] != 0 && CanCharacterLearnSpell(data, &character, index)) {
            LearnSpell(data, &character, index);
        } else {
            character.spell_learned[index] = 0;
        }
    }
    character.hp_current = character.uiHPMax;
    character.stamina = character.uiStaminaMax;
    for (index = 0; index < W8_SPELL_REALM_COUNT; ++index) {
        character.iSPLeft[index] = character.sp_max[index];
    }
}

static void InitializeNpcItemTable(const GameData& data, W8NpcState* npc)
{
    unsigned int index;

    std::ranges::fill(npc->item_ids, -1);
    if (npc->record->item_table_id >= static_cast<int>(data.item_tables.size())) {
        return;
    }
    if (npc->record->item_table_id == 0) {
        return;
    }
    for (index = 0; index < 40; ++index) {
        if (data.item_tables[npc->record->item_table_id - 1].entries[index].selector != 0) {
            npc->item_ids[index] =
                data.item_tables[npc->record->item_table_id - 1].entries[index].item_id;
            npc->item_weights[index] =
                data.item_tables[npc->record->item_table_id - 1].entries[index].weight;
        }
    }
    npc->item_count_dice = data.item_tables[npc->record->item_table_id - 1].item_count_dice;
}

std::shared_ptr<W8NpcState> Game::makeNpcRuntimeNode(int npc_id)
{
    auto owner = std::make_shared<W8NpcState>();
    auto* npc = owner.get();
    npc->name_style = npc_id;
    npc->record = &data.npcs.at(npc_id);
    if (npc->record->has_group != 0) {
        InitializeNpcCharacter(data, rng, npc->record->character, npc->character.emplace());
    }
    if (npc->record->owns_stock != 0) {
        PopulateNpcStock(data, rng, party.averageLevel(), status.world_clock, npc);
    }
    InitializeNpcItemTable(data, npc);
    npc->location_id = 0;
    npc->is_present = false;
    npc->disposition = npc->record->disposition;
    npc->gold = npc->record->gold;
    npc->greeting_pending = true;
    npc->trade_pool = npc->record->trade_pool;

    return owner;
}

W8NpcState* Game::createNpcRuntimeNode(int npc_id)
{
    auto npc = makeNpcRuntimeNode(npc_id);
    return story.adoptNpc(std::move(npc));
}

W8PreparedNpcBindings Game::prepareNpcBindings(World& world)
{
    W8PreparedNpcBindings pending;
    std::vector<bool> reserved(story.npcs.size(), false);
    const auto prepare = [&](W8MonsterInfo& info) {
        const auto* record = &info.entity->species();
        if ((record->flags & W8_MONSTER_FLAG_NPC) == 0) {
            info.bound_npc_index = -1;
            return;
        }
        const int kind = record->npc_kind;
        W8NpcState* source = nullptr;
        if (!data.npcs.at(kind).monster_bound)
            source = story.npcOfKind(kind);
        else if (info.bound_npc_index > 0) {
            auto* candidate = story.npc(info.bound_npc_index);
            if (candidate && candidate->location_id == info.location_id &&
                candidate->name_style == kind)
                source = candidate;
        }
        int slot = source ? source->partner_index : -1;
        if (slot == -1) {
            for (size_t index = 0; index < reserved.size(); ++index) {
                if (!reserved[index] && story.npcs[index] &&
                    story.npcs[index]->binding_unavailable) {
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
        if (existing == pending.records.end()) {
            pending.records.push_back(
                {.slot = slot,
                 .previous = static_cast<std::size_t>(slot) < story.npcs.size() ? story.npcs[slot]
                                                                                : nullptr});
            if (!source) pending.records.back().created = makeNpcRuntimeNode(kind);
            existing = pending.records.end() - 1;
        }
        reserved[slot] = true;
        existing->location_id = info.location_id;
        existing->level_band = GetLevelBand(world.level_id);
        existing->bound_level = static_cast<unsigned char>(world.level_id);
        // Without a created state the slot holds the story's own NPC (source).
        W8NpcState& npc = existing->created ? *existing->created : *existing->previous;
        existing->script = LoadNpcScriptResources(story, npc, info.entity.get());
        info.bound_npc_index = slot;
    };
    for (const auto* list :
         {&world.population->monster_roster.live(), &world.population->monster_roster.unborn()})
        for (const auto& info : *list) prepare(*info);
    return pending;
}

void Story::installNpcBindings(W8PreparedNpcBindings pending)
{
    // Validate the whole plan before changing any binding. Slot reuse must not
    // redirect a prepared operation to a different persistent NPC.
    auto final_size = npcs.size();
    for (const auto& binding : pending.records) {
        if (binding.slot < 0 || binding.slot > 255 || (!binding.created && !binding.previous))
            throw std::runtime_error("invalid prepared NPC binding");
        if (std::cmp_less(binding.slot, npcs.size())) {
            if (npcs[binding.slot] != binding.previous)
                throw std::runtime_error("NPC binding changed during level preparation");
        } else {
            if (!std::cmp_equal(binding.slot, final_size) || binding.previous || !binding.created)
                throw std::runtime_error("NPC slots changed during level preparation");
            ++final_size;
        }
    }
    // Shared states and scripts were allocated during preparation. Reserve all
    // append space before publishing, so allocation failure cannot half-apply it.
    npcs.reserve(final_size);
    for (auto& binding : pending.records) {
        if (binding.created) {
            // A new NPC takes the slot; work still holding a released one there keeps it.
            if (static_cast<size_t>(binding.slot) == npcs.size())
                npcs.push_back(std::move(binding.created));
            else
                npcs[binding.slot] = std::move(binding.created);
        }
        W8NpcState& npc = *npcs[binding.slot];
        npc.partner_index = static_cast<unsigned char>(binding.slot);
        npc.location_id = binding.location_id;
        npc.is_present = true;
        npc.has_monster = true;
        npc.binding_unavailable = false;
        npc.level_band = binding.level_band;
        npc.bound_level = binding.bound_level;
        npc.script_file = std::move(binding.script);
    }
}

W8NpcState* Story::npcOfKind(int kind) const
{
    for (const auto& state_owner : npcs) {
        auto* npc = state_owner.get();
        if (npc && npc->record->kind == kind) {
            return npc;
        }
    }
    return 0;
}

std::string Story::npcDisplayName(const W8NpcState& npc) const
{
    if (npc.name_style == W8_NPC_RFS81_A && facts[W8_FACT_RFS81_HAS_BEEN_FIXED] != 0) {
        return "RFS81B";
    }
    return npc.record->display_name;
}
