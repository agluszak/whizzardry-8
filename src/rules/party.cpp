#include "wiz8/rules/party.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <optional>
#include <span>

#include "wiz8/data/faction_ids.h"
#include "wiz8/data/skills.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/targeting.h"

std::optional<unsigned int> W8Party::slotOf(const W8Character& character) const
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (&characters[slot] == &character) return slot;
    return {};
}

int W8Party::npcIndex(unsigned int slot) const
{
    return members[slot] ? members[slot]->npc_index : -1;
}

int W8Party::orderIndexOf(unsigned int slot) const
{
    return members[slot] ? members[slot]->order_index : 0;
}

void SpendPartyGold(W8Party& party, unsigned int amount)
{
    party.gold = amount > party.gold ? 0 : party.gold - amount;
}

int W8Party::randomSlot(Rng& rng, int require_primary, int require_secondary, int excluded_slot,
                        signed char excluded_gender) const
{
    int skip;
    unsigned int slot;
    unsigned int scanned;
    bool matched;
    const W8Character* character;

retry:
    skip = rng.below(8);
    scanned = 0;
    slot = 0;
    do {
        matched = false;
        if (members[slot].has_value() && static_cast<int>(slot) != excluded_slot) {
            character = &characters[slot];
            if ((character->hp_current > 0 && character->highest_condition < W8_CONDITION_DEAD) ||
                require_primary == 2) {
                if (excluded_gender == -1 || excluded_gender != character->gender) {
                    if (character->highest_condition < W8_CONDITION_ASLEEP ||
                        require_secondary == 2) {
                        matched = true;
                        if (skip == 0) {
                            return slot;
                        }
                        skip--;
                        scanned = 0;
                    }
                }
            }
        }
        slot++;
        if (slot == 8) {
            slot = 0;
        }
        scanned++;
    } while (scanned <= 8);

    if (matched) {
        return slot;
    }
    if (require_secondary == 1) {
        require_secondary = 2;
        goto retry;
    }
    if (require_primary == 1) {
        require_primary = 2;
        goto retry;
    }
    return -1;
}

unsigned int W8Party::averageLevel() const
{
    unsigned int total = 0;
    unsigned int count = 0;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!members[slot]) continue;
        total += characters[slot].uiExpLevel;
        ++count;
    }
    return count ? total / count : 0;
}

std::optional<unsigned int> W8Party::randomLivingSlot(Rng& rng, int excluded_slot,
                                                      unsigned int first_slot) const
{
    std::array<unsigned int, W8_PARTY_SLOT_COUNT> eligible;
    unsigned int count = 0;
    for (unsigned int slot = first_slot; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const auto& character = characters[slot];
        if (members[slot] && static_cast<int>(slot) != excluded_slot && character.hp_current != 0 &&
            character.highest_condition < W8_CONDITION_DEAD)
            eligible[count++] = slot;
    }
    if (count == 0) return std::nullopt;
    return eligible[count == 1 ? 0 : rng.below(count)];
}

bool W8Party::canChangeFormation(unsigned int slot) const
{
    return slot < W8_PARTY_SLOT_COUNT && members[slot] && characters[slot].hp_current > 0 &&
           characters[slot].highest_condition < W8_CONDITION_TURNCOAT;
}

void W8Party::applyFormation(const W8PartyFormationState& proposal)
{
    W8PartyFormationState accepted;
    accepted.positions = formation.positions;
    accepted.unknown_74 = formation.unknown_74;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!canChangeFormation(slot)) continue;
        const auto cell = proposal.cellOf(slot);
        const bool moved = proposal.rowOf(slot) != formation.rowOf(slot);
        accepted.setPosition(slot, cell, false, moved);
    }
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!members[slot] || canChangeFormation(slot) ||
            characters[slot].highest_condition >= W8_CONDITION_DEAD)
            continue;
        const auto previous = formation.cellOf(slot);
        assert(previous);
        if (accepted.rowOccupants(previous->row) < W8_FORMATION_COLUMNS) {
            auto cell = *previous;
            if (accepted.occupant(cell.row, cell.column)) {
                cell.column = 0;
                while (accepted.occupant(cell.row, cell.column)) ++cell.column;
            }
            accepted.setPosition(slot, cell, false, false);
            continue;
        }
        bool placed = false;
        for (int row : {4, 2, 3}) {
            for (int column = 0; column < W8_FORMATION_COLUMNS; ++column) {
                if (accepted.occupant(row, column)) continue;
                accepted.setPosition(slot, W8FormationCell{row, column}, false, true);
                placed = true;
                break;
            }
            if (placed) break;
        }
        assert(placed);
    }
    for (int row = 0; row < W8_FORMATION_ROWS; ++row) accepted.compactRow(row);
    formation = accepted;
}

bool W8Party::participates(unsigned int slot) const
{
    return slot < W8_PARTY_SLOT_COUNT && members[slot] && characters[slot].hp_current != 0 &&
           characters[slot].highest_condition < W8_CONDITION_DEAD;
}
W8Action W8Party::availableAction(unsigned int slot, W8Action action) const
{
    if (!participates(slot) && action.kind != W8_ACTION_WALK && action.kind != W8_ACTION_RUN)
        return {};
    return action;
}
W8Action W8Party::action(unsigned int slot, W8TargetingContext stored) const
{
    const auto& row = rows[slot];
    return availableAction(slot, stored == W8_TARGETING_CONTEXT_IN_COMBAT ? row.committed
                                                                          : row.executing.action());
}
bool W8Party::aimAction(unsigned int slot, W8TargetingContext stored, const W8CombatSlot& target,
                        bool remember)
{
    const auto selected = action(slot, stored);
    if (selected.target == target) return false;
    auto& row = rows[slot];
    (stored == W8_TARGETING_CONTEXT_IN_COMBAT ? row.committed.target : row.executing.target) =
        target;
    if (remember && target.iType != W8_TARGET_KIND_NONE) {
        switch (selected.kind) {
        case W8_ACTION_CAST_SPELL:
            row.remembered.spell.target = target;
            break;
        case W8_ACTION_USE_ITEM:
            row.remembered.item_target = target;
            break;
        case W8_ACTION_BREATHE:
            row.remembered.breath_target = target;
            break;
        default:
            break;
        }
    }
    return true;
}

bool W8Party::canAttack(unsigned int slot) const
{
    return slot < W8_PARTY_SLOT_COUNT && members[slot] && characters[slot].hp_current != 0 &&
           characters[slot].highest_condition <= W8_CONDITION_INSANE &&
           characters[slot].Hand[0].in_play;
}

W8Disposition W8Party::dispositionToward(unsigned int slot, W8Disposition other) const
{
    if (characters[slot].uiCondition[W8_CONDITION_TURNCOAT] == 0) return other;
    if (other == W8_DISPOSITION_HOSTILE) return W8_DISPOSITION_FRIENDLY;
    if (other == W8_DISPOSITION_FRIENDLY) return W8_DISPOSITION_HOSTILE;
    return other;
}
W8Disposition W8Party::dispositionBetween(unsigned int first, unsigned int second) const
{
    return (characters[first].uiCondition[W8_CONDITION_TURNCOAT] != 0) ==
                   (characters[second].uiCondition[W8_CONDITION_TURNCOAT] != 0)
               ? W8_DISPOSITION_FRIENDLY
               : W8_DISPOSITION_HOSTILE;
}

bool W8Party::canAct(unsigned int slot) const
{
    return participates(slot) && characters[slot].highest_condition <= W8_CONDITION_TURNCOAT;
}
W8ActionKind W8Party::commitAction(unsigned int slot, W8Action action)
{
    assert(action.kind <= W8_ACTION_RUN);
    const auto previous = rows[slot].committed.kind;
    rows[slot].committed = action;
    if (action.kind != W8_ACTION_ATTACK && action.kind != W8_ACTION_BERSERK &&
        action.kind != W8_ACTION_DEFEND && action.kind != W8_ACTION_PROTECT &&
        action.kind != W8_ACTION_WALK && action.kind != W8_ACTION_RUN)
        rows[slot].remembered.repeat_action = action.kind;
    return previous;
}
void W8Party::beginDefensiveAction(unsigned int slot, W8ActionKind action)
{
    assert(action == W8_ACTION_DEFEND || action == W8_ACTION_PROTECT);
    auto& row = rows[slot];
    row.executing.start(action, -1);
    if (action == W8_ACTION_PROTECT) row.executing.target = row.committed.target;
}

unsigned int W8Party::awardExperience(unsigned int amount)
{
    if (!amount) return 0;
    unsigned int recipients = 0;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!participates(slot)) continue;
        auto& experience = characters[slot].experience;
        experience += std::min(amount, std::numeric_limits<unsigned int>::max() - experience);
        ++recipients;
    }
    return recipients;
}

unsigned int W8Party::participantCount() const
{
    unsigned int count = 0;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (participates(slot)) ++count;
    return count;
}

void W8Party::refreshModifiers(std::span<const W8EffectSlot, 12> effects,
                               std::span<const W8EffectSlot> combat_effects,
                               std::span<const W8EffectSlot> radius_effects)
{
    modifiers = {};
    ApplyPartyEffectSlots(effects, &modifiers);
    unsigned char armor = modifiers.armor_matchup;
    for (const auto& effect : combat_effects) {
        if (effect.active && effect.effect_id == 0x31) {
            armor -= effect.amount;
            modifiers.armor_matchup = armor;
        }
    }
    ApplyCombatEffectSlots(radius_effects, &modifiers);
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const auto& character = characters[slot];
        if (members[slot] && character.hp_current != 0 &&
            character.highest_condition == W8_CONDITION_NONE &&
            CharacterHasTrait(&character, W8_TRAIT_CAMP_RECOVERY_BONUS)) {
            modifiers.boost_health_regen = 1;
            modifiers.boost_stamina_regen = 1;
            modifiers.boost_spell_regen = 1;
            break;
        }
    }
}
