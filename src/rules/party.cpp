#include "wiz8/rules/party.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_slot.h"

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
