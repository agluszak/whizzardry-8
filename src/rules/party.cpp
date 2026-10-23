#include "wiz8/rules/party.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <utility>

#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/random.h"

const W8ItemInstance* W8Party::itemAt(W8ItemLocation location) const
{
    switch (location.storage) {
    case W8ItemStorage::Hand:
        return &held.item;
    case W8ItemStorage::Pool:
        return location.slot < 500 ? &item_pool[location.slot] : nullptr;
    case W8ItemStorage::Equipment:
        return location.character < W8_PARTY_SLOT_COUNT && location.slot < 12
                   ? &characters[location.character].EquippedItem[location.slot]
                   : nullptr;
    case W8ItemStorage::Backpack:
        return location.character < W8_PARTY_SLOT_COUNT && location.slot < 8
                   ? &characters[location.character].backpack[location.slot]
                   : nullptr;
    case W8ItemStorage::None:
        return nullptr;
    }
    return nullptr;
}

W8ItemInstance* W8Party::itemAt(W8ItemLocation location)
{
    return const_cast<W8ItemInstance*>(std::as_const(*this).itemAt(location));
}

std::optional<unsigned int> W8Party::slotOf(const W8Character& character) const
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (&characters[slot] == &character) return slot;
    return {};
}

std::optional<int> W8Party::randomSlot(Rng& rng, PartySelectionFallback fallback,
                                       std::optional<int> excluded_slot) const
{
    const auto pick = [&](bool require_living, bool require_awake) -> std::optional<int> {
        // Retail walks the eligible slots cyclically after a draw from eight, not
        // uniformly from the candidate count. Empty stages still consume a draw.
        const auto draw = rng.below(8);
        std::array<int, W8_PARTY_SLOT_COUNT> candidates;
        unsigned int count = 0;
        for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
            if (!members[slot] || slot == excluded_slot) continue;
            const auto& character = characters[slot];
            if (require_living &&
                (character.hp_current <= 0 || character.highest_condition >= W8_CONDITION_DEAD))
                continue;
            if (require_awake && character.highest_condition >= W8_CONDITION_ASLEEP) continue;
            candidates[count++] = slot;
        }
        if (count == 0) return {};
        return candidates[draw % count];
    };
    auto selected = pick(true, true);
    if (selected || fallback == PartySelectionFallback::None) return selected;
    selected = pick(true, false);
    if (selected || fallback == PartySelectionFallback::Living) return selected;
    return pick(false, false);
}

std::optional<int> W8Party::randomLivingSlot(Rng& rng, std::optional<int> excluded_slot,
                                             bool skip_first_two) const
{
    std::array<int, W8_PARTY_SLOT_COUNT> candidates;
    unsigned int count = 0;
    for (int slot = skip_first_two ? 2 : 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const auto& character = characters[slot];
        if (members[slot] && slot != excluded_slot && character.hp_current != 0 &&
            character.highest_condition < W8_CONDITION_DEAD)
            candidates[count++] = slot;
    }
    if (count == 0) return {};
    return candidates[count == 1 ? 0 : rng.below(count)];
}

std::optional<unsigned int> W8Party::firstSelectableSlot() const
{
    std::optional<unsigned int> first_occupied;
    std::optional<unsigned int> first_living;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!members[slot]) continue;
        if (!first_occupied) first_occupied = slot;
        const auto& character = characters[slot];
        if (character.hp_current > 0 && character.highest_condition < W8_CONDITION_DEAD) {
            if (character.highest_condition < W8_CONDITION_ASLEEP) return slot;
            if (!first_living) first_living = slot;
        }
    }
    return first_living ? first_living : first_occupied;
}

int W8Party::npcIndex(unsigned int slot) const
{
    return members[slot] ? members[slot]->npc_index : -1;
}

int W8Party::orderIndexOf(unsigned int slot) const
{
    return members[slot] ? members[slot]->order_index : 0;
}

W8ItemInstance W8Party::take(W8ItemLocation location)
{
    auto* item = itemAt(location);
    if (!item) return {};
    auto result = std::exchange(*item, W8ItemInstance{});
    if (location.storage == W8ItemStorage::Hand) held = {};
    if (location.storage == W8ItemStorage::Pool && location.slot < item_count) {
        std::move(item + 1, item_pool + item_count, item);
        item_pool[--item_count] = {};
    }
    return result;
}

void W8Party::exchange(W8ItemLocation first, W8ItemLocation second, int actor)
{
    auto* first_item = itemAt(first);
    auto* second_item = itemAt(second);
    if (!first_item || !second_item || first_item == second_item) return;
    std::swap(*first_item, *second_item);
    first_item->bind_announced = second_item->bind_announced = false;
    if (first.storage == W8ItemStorage::Hand || second.storage == W8ItemStorage::Hand) {
        held.origin = first.storage == W8ItemStorage::Hand ? second : first;
        held.actor = actor;
        if (held.item.iItemNo == -1) held = {};
    }
    // Close the later pool entry first so the earlier index stays valid.
    if (first.storage == W8ItemStorage::Pool && second.storage == W8ItemStorage::Pool &&
        first.slot < second.slot)
        std::swap(first, second);
    for (const auto location : {first, second})
        if (location.storage == W8ItemStorage::Pool && location.slot < item_count &&
            item_pool[location.slot].iItemNo == -1)
            take(location);
}

bool W8Party::insertIntoPool(unsigned int index, W8ItemInstance item)
{
    if (item_count >= std::size(item_pool) || index > item_count) return false;
    std::move_backward(item_pool + index, item_pool + item_count, item_pool + item_count + 1);
    item_pool[index] = item;
    ++item_count;
    return true;
}
