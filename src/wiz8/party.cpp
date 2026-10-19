#include "wiz8/party.h"

#include <utility>

W8ItemLocation W8Party::locateItem(const W8ItemInstance* item) const
{
    if (!item) return {};
    if (item == &held.item) return {W8ItemStorage::Hand};
    for (unsigned int slot = 0; slot < 500; ++slot)
        if (item == &item_pool[slot]) return {W8ItemStorage::Pool, 0, slot};
    for (unsigned int character = 0; character < W8_PARTY_SLOT_COUNT; ++character) {
        for (unsigned int slot = 0; slot < 12; ++slot)
            if (item == &characters[character].EquippedItem[slot])
                return {W8ItemStorage::Equipment, character, slot};
        for (unsigned int slot = 0; slot < 8; ++slot)
            if (item == &characters[character].backpack[slot])
                return {W8ItemStorage::Backpack, character, slot};
    }
    return {};
}

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
