#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "wiz8/audio/soundman.h"
#include "wiz8/canvas/image_assets.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/notices.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/utility.h"

/* Items of equipment class four are priced and carried by the bundle rather
   than singly, so a stack of them divides its bundle value out - rounding up,
   which is where the addend comes from. */
enum { W8_ITEM_BUNDLE_SIZE = 25 };

/* One item's weight, or nothing at all when the caller has no item. Unlike the
   stack form below this ignores how many are held. */
unsigned int GetItemUnitWeight(const W8ItemInstance* item)
{
    if (item != 0) {
        return Data().items[item->iItemNo].weight;
    }
    return 0;
}

/* What a whole stack is worth in gold. Bundled goods divide their bundle price
   across the count and round up; anything else that stacks multiplies, and
   everything else is worth exactly what one of it is worth. */
unsigned int GetItemStackValue(const W8ItemInstance* item)
{
    if (Data().items[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        return (item->stack_count * Data().items[item->iItemNo].value + (W8_ITEM_BUNDLE_SIZE - 1)) /
               W8_ITEM_BUNDLE_SIZE;
    }
    if (Data().items[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK &&
        item->stack_count > 1) {
        return Data().items[item->iItemNo].value * item->stack_count;
    }
    return Data().items[item->iItemNo].value;
}

/* The Data().strings message each item use kind shows for whatever the item
   was used on, read at every other entry. The last six ids, from
   W8_ITEM_PROPERTY_MESSAGE_FIRST, are the item property labels. */
unsigned short g_item_use_messages[25] = {
    0x4f6, 0x4f7, 0x4f8, 0x4f9, 0x4fa, 0x4fb, 0x4fc, 0x4fd, 0x4fe, 0x4ff, 0x500, 0x501, 0x502,
    0x503, 0x504, 0x505, 0x506, 0x507, 0x508, 0x509, 0x50a, 0x50b, 0x50c, 0x50d, 0x50e,
};
W8Skill g_item_spell_presentation[11] = {
    W8_SKILL_NONE,        W8_SKILL_ARTIFACTS,      W8_SKILL_ARTIFACTS, W8_SKILL_NONE,
    W8_SKILL_NONE,        W8_SKILL_NONE,           W8_SKILL_MUSIC,     W8_SKILL_THROWING_SLING,
    W8_SKILL_ENGINEERING, W8_SKILL_MODERN_WEAPONS, W8_SKILL_SWORD};

static const std::array<std::string_view, 145> g_item_video_object_fallback_names = {
    "Dagger.sti",
    "LongSword.sti",
    "Bipennis.sti",
    "BattleAxe.sti",
    "Flail.sti",
    "Mace.sti",
    "Hammer.sti",
    "ShortStaff.sti",
    "Halberd.sti",
    "Spear.sti",
    "BoStaff.sti",
    "LongBow.sti",
    "Crossbow.sti",
    "Sling.sti",
    "GreatSword.sti",
    "Rapier.sti",
    "Katana.sti",
    "LongStaff.sti",
    "Wand.sti",
    "MagicStave.sti",
    "QuestionMark.sti",
    "Shuriken.sti",
    "Arrow.sti",
    "SlingShot.sti",
    "KiteShield.sti",
    "Basnet.sti",
    "PlateTorso.sti",
    "PlateLegs.sti",
    "GauntletSteel.sti",
    "Sollerets.sti",
    "Amulet.sti",
    "Ring.sti",
    "BluePotion.sti",
    "Scroll.sti",
    "PowderPouch.sti",
    "Key.sti",
    "Locket.sti",
    "GreatBow.sti",
    "RoundShield.sti",
    "WizardCone.sti",
    "LeatherHelm.sti",
    "Cuirass.sti",
    "LeatherHauberk.sti",
    "Greaves.sti",
    "LeatherPants.sti",
    "Gloves.sti",
    "Sandals.sti",
    "Buskins.sti",
    "Boots.sti",
    "Book.sti",
    "Ankh.sti",
    "Ninjato.sti",
    "WarHammer.sti",
    "Flamberge.sti",
    "Bullwhip.sti",
    "Sai.sti",
    "Nunchuka.sti",
    "Glaive.sti",
    "BlackSword.sti",
    "FireSword.sti",
    "RobeUpper.sti",
    "RobeLower.sti",
    "HalterTop.sti",
    "Skirt.sti",
    "SkullCap.sti",
    "FeatheredCap.sti",
    "Mitre.sti",
    "QuestionMark.sti",
    "Kabuto.sti",
    "Do-MaruUpper.sti",
    "Tosei-GusokuLower.sti",
    "Burgonet.sti",
    "FurLegs.sti",
    "Cloak.sti",
    "QuestionMark.sti",
    "QuestionMark.sti",
    "Garland.sti",
    "CatONineTails.sti",
    "SilverCross.sti",
    "QuestionMark.sti",
    "QuestionMark.sti",
    "Bracelet.sti",
    "Necklace.sti",
    "Quarrel.sti",
    "QuestionMark.sti",
    "Coif.sti",
    "Hauberk.sti",
    "ChainLower.sti",
    "MailMittens.sti",
    "ChainHosen.sti",
    "Lance.sti",
    "Armet.sti",
    "NinjaCowl.sti",
    "NinjaGarbUpper.sti",
    "NinjaGarbLower.sti",
    "TabiBoots.sti",
    "Heaume.sti",
    "BoxHelm.sti",
    "DiamondRing.sti",
    "Bag.sti",
    "RedPotion.sti",
    "GreenPotion.sti",
    "PurplePotion.sti",
    "YellowPotion.sti",
    "QuestionMark.sti",
    "T'RangStaff.sti",
    "GauntletBronze.sti",
    "QuestionMark.sti",
    "LightSword.sti",
    "QuestionMark.sti",
    "QuestionMark.sti",
    "QuestionMark.sti",
    "Dart.sti",
    "PowderShot.sti",
    "Musket.sti",
    "QuestionMark.sti",
    "Stone.sti",
    "Stick.sti",
    "ShortSword.sti",
    "QuestionMark.sti",
    "Bagpipes.sti",
    "Lyre.sti",
    "Lute.sti",
    "Horn.sti",
    "QuestionMark.sti",
    "Bracelet2.sti",
    "Shuriken2.sti",
    "QuestionMark.sti",
    "Wand2.sti",
    "YellowPotion2.sti",
    "MediumShield.sti",
    "Omnigun_1.sti",
    "QuestionMark.sti",
    "QuestionMark.sti",
    "QuestionMark.sti",
    "QuestionMark.sti",
    "Saxaphone.sti",
    "Violin.sti",
    "Drum.sti",
    "Bullroar.sti",
    "QuestionMark.sti",
    "emptybottle.sti",
    "Stix.sti",
    "rocket.sti",
    "rocketlauncher.sti"};

std::shared_ptr<const SpriteSheet> ItemSprites(int item_id)
{
    if (item_id < 0 || static_cast<unsigned int>(item_id) >= Data().items.size())
        throw std::out_of_range(fmt::format("Item {} has no record", item_id));
    const auto& record = Data().items[item_id];
    std::string_view name = record.video_object_name;
    if (name.empty()) name = g_item_video_object_fallback_names.at(record.unidentified_name_index);
    std::string path = fmt::format("Data\\Items\\{}", name);
    if (path.find(".sti") == std::string::npos) path += ".sti";
    auto image = g_image_assets.sprites(path);
    if (!image) throw std::runtime_error(fmt::format("Cannot load item image {}", path));
    return image;
}

/* The equipment class an item belongs to, or zero when the caller has no
   item - which is indistinguishable from the first real class, so callers
   test the item themselves. */
unsigned char GetItemEquipClass(const W8ItemInstance* item)
{
    if (item != 0) {
        return Data().items[item->iItemNo].equip_class;
    }
    return 0;
}

/* Which generic name this item wears while unidentified. */
unsigned short GetItemUnidentifiedNameIndex(const W8ItemInstance* item)
{
    if (item != 0) {
        return Data().items[item->iItemNo].unidentified_name_index;
    }
    return 0;
}

/* Whether two items are of the same equipment class. */
bool ItemsShareEquipClass(const W8ItemInstance* first, const W8ItemInstance* second)
{
    if (first != 0 && second != 0) {
        return Data().items[first->iItemNo].equip_class ==
               Data().items[second->iItemNo].equip_class;
    }
    return false;
}

/* Whether two items look alike while unidentified. */
bool ItemsShareUnidentifiedName(const W8ItemInstance* first, const W8ItemInstance* second)
{
    if (first != 0 && second != 0) {
        return Data().items[first->iItemNo].unidentified_name_index ==
               Data().items[second->iItemNo].unidentified_name_index;
    }
    return false;
}

/* Whether an item is bound to whoever is wearing it, which is what stops it
   being taken off or swapped away. */
bool IsItemBoundToWearer(const W8ItemInstance* item)
{
    if (item->iItemNo != -1 && Data().items[item->iItemNo].binds_on_equip != 0 && item->bound) {
        return true;
    }
    return false;
}

/* Whether anybody in the party could use this item. Only occupied slots with a
   character who is conscious enough to act are asked. */
bool AnyPartyMemberCanUseItem(int item_id)
{
    int slot;

    for (slot = 0; slot < 8; ++slot) {
        if (CanPartySlotParticipate(slot)) {
            if (CanCharacterUseItem(Data(), &CurrentGame().party.characters[slot], item_id)) {
                return true;
            }
        }
    }
    return false;
}

/* Whether this character may activate this item right now. Anything with an
   equip home - flagged on the record or assigned a default slot - must
   actually be worn in a body slot rather than held, then the category gates:
   spell sources are refused here because they go through the cast path,
   category zero is dead weight, spell-less records fail unless they are the
   one exempt item, and an empty charge stack fails. */
bool CanCharacterActivateItem(W8Character* character, const W8ItemInstance* item)
{
    const W8ItemDatabaseRecord* record;

    assert(character != 0);
    assert(item != 0);
    assert(item->iItemNo >= -1);
    assert(item->iItemNo < static_cast<int>(Data().items.size()));
    if (item->iItemNo == -1) {
        return false;
    }
    if (!CanCharacterUseItem(Data(), character, item->iItemNo)) {
        return false;
    }

    record = &Data().items[item->iItemNo];
    if ((record->flags & W8_ITEM_FLAG_MUST_EQUIP_TO_USE) != 0 ||
        GetItemDefaultEquipSlot(Data(), item->iItemNo, GameStarted()) != W8_EQUIP_SLOT_NONE) {
        if (!IsItemWornByCharacter(character, item)) {
            return false;
        }
    }

    if (record->category == W8_ITEM_CATEGORY_SPELL_SOURCE || record->category == 0) {
        return false;
    }
    if (item->iItemNo != 0x29f && record->spell_id == W8_SPELL_NONE) {
        return false;
    }
    if (record->quantity_kind == W8_ITEM_QUANTITY_CHARGES && item->uses_or_charges == 0) {
        return false;
    }
    return true;
}

/* Whether both weapon sets are entirely empty. */
bool AreAllHandSlotsEmpty(const W8Character* character)
{
    return character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == -1 &&
           character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1 &&
           character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo == -1 &&
           character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo == -1;
}

/* Which of four groups an item's home slot belongs to. The four hand slots
   share one group and the rest split two ways. */
W8ItemEquipSlotGroup GetItemEquipSlotGroup(int item_id)
{
    switch (GetItemDefaultEquipSlot(Data(), item_id, GameStarted())) {
    case W8_EQUIP_SLOT_PRIMARY_WEAPON:
    case W8_EQUIP_SLOT_SECONDARY_WEAPON:
    case W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON:
    case W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON:
        return W8_ITEM_EQUIP_GROUP_HAND;
    case W8_EQUIP_SLOT_HEAD:
    case W8_EQUIP_SLOT_TORSO:
    case W8_EQUIP_SLOT_HANDS:
    case W8_EQUIP_SLOT_LEGS:
    case W8_EQUIP_SLOT_FEET:
        return W8_ITEM_EQUIP_GROUP_BODY;
    case W8_EQUIP_SLOT_MISC_1:
    case W8_EQUIP_SLOT_MISC_2:
    case W8_EQUIP_SLOT_CLOAK:
        return W8_ITEM_EQUIP_GROUP_ACCESSORY;
    default:
        return W8_ITEM_EQUIP_GROUP_OTHER;
    }
}

/* How the interface presents the spell an item carries, drawn from a per
   category table. Two particular spells are excluded and answer with nothing
   at all. */
W8Skill GetItemSpellPresentation(const W8ItemDatabaseRecord* record)
{
    if (record->spell_id != 'X' && record->spell_id != 't') {
        return g_item_spell_presentation[record->category];
    }
    return W8_SKILL_NONE;
}

/* Whether an item may be picked up out of wherever it is sitting. An
   unidentified item always may; an identified one only when it belongs in an
   equipment slot at all and has not bound itself to its wearer. */
bool CanItemLeaveItsSlot(const W8ItemInstance* item)
{
    if (item->iItemNo != -1) {
        if (!item->identified) {
            return true;
        }
        if (GetItemDefaultEquipSlot(Data(), item->iItemNo, GameStarted()) != W8_EQUIP_SLOT_NONE) {
            return !item->bound;
        }
    }
    return false;
}

/* How many of a character's twenty item slots hold something they could use
   right now - the twelve worn and the eight carried, walked as two runs
   rather than one. */
int CountUsableCharacterItems(W8Character* character)
{
    int count = 0;
    int index;

    for (index = 0; index < 12; ++index) {
        if (CanCharacterActivateItem(character, &character->EquippedItem[index])) {
            ++count;
        }
    }
    for (index = 0; index < 8; ++index) {
        if (CanCharacterActivateItem(character, &character->backpack[index])) {
            ++count;
        }
    }
    return count;
}

/* Whether an item's class is one of the three the target rules normalize for.
   The classes here are above the thirteen the equip-slot switch enumerates,
   so the class domain is wider than that switch covers. */
bool ItemClassNormalizesTarget(const W8ItemDatabaseRecord* record)
{
    unsigned char equip_class = record->equip_class;

    if (equip_class != W8_ITEM_EQUIP_CLASS_POTION &&
        (equip_class < W8_ITEM_EQUIP_CLASS_FOOD || equip_class > W8_ITEM_EQUIP_CLASS_DRINK)) {
        return false;
    }
    return true;
}

/* Whether one item sits in a character's equipment, excluding the two
   alternate-set hand slots - a slot in that set answers no even though the
   item is found there. */
bool IsItemWornByCharacter(W8Character* character, const W8ItemInstance* item)
{
    unsigned int slot;

    if (character == 0) {
        return false;
    }
    for (slot = 0; slot < 12; ++slot) {
        if (&character->EquippedItem[slot] == item) {
            return slot != W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON &&
                   slot != W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON;
        }
    }
    return false;
}

/* Whether one item sits in a character's own carried slots. */
bool IsItemCarriedByCharacter(W8Character* character, const W8ItemInstance* item)
{
    unsigned int slot;

    if (character == 0) {
        return false;
    }
    for (slot = 0; slot < 8; ++slot) {
        if (&character->backpack[slot] == item) {
            return true;
        }
    }
    return false;
}

/* The spell an item carries, with both bounds on the item id asserted - the
   second names the database count as Data().items.size(). */
int GetItemSpell(const W8ItemInstance* item)
{
    if (item == 0) {
        return 0;
    }
    assert(item->iItemNo != -1);
    assert(item->iItemNo < static_cast<int>(Data().items.size()));
    return Data().items[item->iItemNo].spell_id;
}

/* Let the whole party have a go at identifying one item. Everybody able
   tries, but only the first attempt's answer is reported - the rest still
   happen for whatever they do to the item. */
bool PartyAttemptsToIdentifyItem(W8ItemInstance* item, int)
{
    bool result = false;
    int party_slot;

    if (item->identified) {
        return false;
    }
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (CurrentGame().party.members[party_slot].has_value() &&
            CurrentGame().party.characters[party_slot].hp_current != 0 &&
            CurrentGame().party.characters[party_slot].highest_condition < W8_CONDITION_INSANE) {
            if (!result) {
                result = TryIdentifyItemFor(&CurrentGame().party.characters[party_slot], item);
            } else {
                TryIdentifyItemFor(&CurrentGame().party.characters[party_slot], item);
            }
        }
    }
    return result;
}

/* Score one identify attempt. Three times the attempt's strength, scaled by
   the attempter's percentage, has to reach the item's difficulty; clearing it
   reveals everything about the item at once. */
void ApplyIdentifyAttempt(W8ItemInstance* item, unsigned int strength, unsigned int percent)
{
    unsigned int score = strength * 3;

    AdjustIntegerByPercent(&score, percent);
    if (Data().items[item->iItemNo].identify_difficulty <= score) {
        assert(item != 0);
        item->identified = true;
        item->spell_hint = true;
        item->unknown_08 = 1;
        item->bound = true;
    }
}

/* How many attempts of a given strength it takes to clear an item's
   difficulty. Counted downwards from the ceiling division, so the answer is
   the smallest count that still clears it. */
unsigned int CountIdentifyAttemptsNeeded(const W8ItemInstance* item, unsigned int percent)
{
    unsigned int attempts;
    unsigned int candidate;
    unsigned int score;

    assert(item != 0);
    if (item->iItemNo == -1) {
        return 1;
    }

    attempts = Data().items[item->iItemNo].identify_difficulty / 3;
    if (Data().items[item->iItemNo].identify_difficulty % 3 != 0) {
        ++attempts;
    }
    candidate = attempts * 3;
    while (attempts > 1) {
        candidate -= 3;
        score = candidate;
        AdjustIntegerByPercent(&score, percent);
        if (score < Data().items[item->iItemNo].identify_difficulty) {
            return attempts;
        }
        --attempts;
    }
    return 1;
}

/* Reveal what one character's worn bindings are, as far as the attempt
   reaches. Nothing bound at all answers zero; some still hidden answers one
   and all revealed answers two. */
int RevealCharacterItemBindings(unsigned int party_slot, int strength, unsigned int percent)
{
    W8Character* character = &CurrentGame().party.characters[party_slot];
    unsigned int score = strength * 3;
    int revealed = 0;
    int still_hidden = 0;
    int slot;

    AdjustIntegerByPercent(&score, percent);
    for (slot = 0; slot < 12; ++slot) {
        W8ItemInstance* item = &character->EquippedItem[slot];

        if (item->iItemNo != -1 && Data().items[item->iItemNo].binds_on_equip != 0) {
            if (score < Data().items[item->iItemNo].identify_difficulty) {
                ++still_hidden;
            } else {
                ++revealed;
                item->bind_announced = true;
            }
        }
    }
    if (revealed != 0) {
        return (still_hidden == 0) + 1;
    }
    return 0;
}

/* At what range an item's spell works. An item with no spell has no range at
   all, which is a different answer from touch. */
W8RangeCategory GetItemSpellRange(const W8ItemInstance* item)
{
    if (item == nullptr) return W8_RANGE_NONE;
    const int spell_id = GetItemSpell(item);
    return spell_id == W8_SPELL_NONE ? W8_RANGE_NONE : Data().spells[spell_id].range_category;
}

/* One character's attempt at identifying an item. Their strength is a sixth of
   the identify skill's level; clearing the difficulty reveals the item, three
   further points also reveal its binding, and an attempt that only just came
   off practises the skill. */
bool TryIdentifyItemFor(W8Character* character, W8ItemInstance* item)
{
    signed char strength;
    int margin;

    assert(item != 0);
    if (character->hp_current == 0 || character->highest_condition >= W8_CONDITION_INSANE) {
        return false;
    }

    strength = static_cast<signed char>(character->skills[W8_SKILL_ARTIFACTS].level / 6);
    if (static_cast<signed char>(Data().items[item->iItemNo].identify_difficulty) > strength) {
        return false;
    }

    assert(item != 0);
    item->identified = true;
    item->spell_hint = true;
    item->unknown_08 = 1;
    if (static_cast<signed char>(Data().items[item->iItemNo].identify_difficulty + 3) <= strength) {
        item->bound = true;
    }

    margin = strength - Data().items[item->iItemNo].identify_difficulty;
    if (margin >= 0 && margin < 3) {
        PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_ARTIFACTS, 2, true);
    }
    return true;
}

/* Bind everything the party is wearing, one character at a time - but not
   during a fight the party has not yet been let out of, which says so
   instead. */
void BindEveryPartyItem(void)
{
    unsigned int party_slot;

    if (CurrentGame().combat.has_value() && !CurrentGame().combat->round_active &&
        !CurrentGame().party_movement_mode) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_WEAPON_SWAP_BLOCKED_COMBAT]);
        return;
    }
    for (party_slot = 0; party_slot < 8; ++party_slot) {
        if (!CurrentGame().party.rows[party_slot].remembered.weapon_sets_locked) {
            CurrentGame().inventory.requestWeaponSwap(party_slot, false);
        }
    }
    ShowNotice(W8_FONT_PALETTE_WHITE, Data().strings[0x1ed]);
}

/* The binding difficulty of a character's worn items: the worst identify
   difficulty among binds-on-equip pieces whose binding has not been announced
   yet, in thirds rounded up. Cure spells consult it when the target's worn
   items ask for more power than the condition does. */
unsigned int GetEquipmentBindingDifficulty(int character_index)
{
    unsigned char max_difficulty = 0;
    W8ItemInstance* slot = CurrentGame().party.characters[character_index].EquippedItem;

    for (int remaining = 12; remaining != 0; --remaining) {
        int item_id = slot->iItemNo;
        if (item_id != -1 && Data().items[item_id].binds_on_equip != 0 && !slot->bind_announced &&
            max_difficulty < Data().items[item_id].identify_difficulty) {
            max_difficulty = Data().items[item_id].identify_difficulty;
        }
        ++slot;
    }
    unsigned int level = max_difficulty / 3;
    if (level * 3 < max_difficulty) {
        ++level;
    }
    return level;
}

/* Acquisition of a handful of plot items updates their paired facts. */
void UpdateFactsAfterAcquiringItem(const W8ItemInstance* item)
{
    switch (item->iItemNo) {
    case 0x243:
        if (!GetFact(W8_FACT_DESTINAE_POSSESS)) {
            SetFact(W8_FACT_DESTINAE_POSSESS, 1, false);
        }
        if (GetFact(W8_FACT_QUEST_GET_DD)) {
            SetFact(W8_FACT_QUEST_GET_DD, 0, false);
        }
        break;
    case 0x242:
        if (!GetFact(W8_FACT_ASTRAL_POSSESS)) {
            SetFact(W8_FACT_ASTRAL_POSSESS, 1, false);
        }
        if (GetFact(W8_FACT_QUEST_GET_AD)) {
            SetFact(W8_FACT_QUEST_GET_AD, 0, false);
        }
        break;
    case 0x244:
        if (!GetFact(W8_FACT_CHAOS_POSSESS)) {
            SetFact(W8_FACT_CHAOS_POSSESS, 1, false);
        }
        if (GetFact(W8_FACT_QUEST_GET_CM)) {
            SetFact(W8_FACT_QUEST_GET_CM, 0, false);
        }
        CurrentGame().status.fact_b8_pending = true;
        CurrentGame().status.fact_b8_clock = CurrentGame().status.world_clock;
        break;
    case 0x264:
        if (!GetFact(W8_FACT_ASTRAL_FAKE_POSSESS)) {
            SetFact(W8_FACT_ASTRAL_FAKE_POSSESS, 1, false);
            SetFact(W8_FACT_ASTRAL_FAKE_POSSESS1, 1, false);
        }
        break;
    case 0x201:
        SetFact(W8_FACT_QUEST_BRAFFIT_CIERDAN, 0, false);
        break;
    case 0x27c:
        SetFact(W8_FACT_QUEST_MYLES_FIND_DIAMOND, 0, false);
        break;
    case 0x1d2:
        if (GetFact(W8_FACT_QUEST_MYLES_WEAPONS_CACHE)) {
            SetFact(W8_FACT_QUEST_MYLES_WEAPONS_CACHE, 0, false);
        }
        break;
    }
}

/* Deliver the one-time character reaction associated with exceptional items.
   Character creation (screen 5) deliberately suppresses every reaction. */
void DeliverExceptionalItemReaction(Game& game, W8ItemInstance* item, bool choose_character,
                                    W8Character* character)
{
    int message;

    if (CurrentScreenState().id == W8_SCREEN_PARTY_SELECTION) {
        return;
    }
    if (choose_character) {
        int party_slot = GetRandomCharacter(0, 0, -1, -1);
        if (party_slot == -1) {
            return;
        }
        character = &game.party.characters[party_slot];
    }
    if ((item->effect_used & 1) != 0) {
        return;
    }

    switch (item->iItemNo) {
    case 0x239:
        message = 132;
        break;
    case 0x242:
        message = 46;
        break;
    case 0x243:
        message = 47;
        break;
    case 0x244:
        message = game.story.factions[W8_FACTION_MOOK].disposition_score != 0 ? 48 : 49;
        break;
    case 0x264:
        message = 66;
        break;
    case 0x27c: {
        SetFact(W8_FACT_DIAMOND_FOUND, 1, false);
        if (!NpcLeadHasNameStyle(W8_NPC_MYLES) || !GetFact(W8_FACT_QUEST_MYLES_FIND_DIAMOND)) {
            return;
        }
        W8NpcState* npc = GetNpcStateByKind(7);
        if (npc == 0) {
            return;
        }
        signed char npc_slot = npc->group_index;
        QueueCharacterEvent(&game.party.characters[npc_slot], 65, 0, kCharacterEventFullVolume);
        return;
    }
    default:
        if (choose_character || !CanCharacterUseItem(game.data, character, item->iItemNo) ||
            game.data.items[item->iItemNo].value / character->uiExpLevel <= 500 ||
            !item->identified) {
            return;
        }
        message = 55;
        break;
    }

    QueueCharacterEvent(character, message, 0, kCharacterEventFullVolume);
    item->effect_used |= 1;
}

/* Whether the item's equip class is directly usable (0x17/0x19). */
bool IsUsableItemClass(const W8ItemInstance* item)
{
    if (Data().items[item->iItemNo].equip_class != W8_ITEM_EQUIP_CLASS_KEY &&
        Data().items[item->iItemNo].equip_class != W8_ITEM_EQUIP_CLASS_OTHER) {
        return false;
    }
    return true;
}

/* Whether the party slot may go through with using this item right now.
   Class-0x17 and -0x19 items and anything the character can cast from answer
   with the out-of-combat gate alone; everything else is refused while the
   character's spellcasting is blocked and the item is a class-0x0d casting
   aid, and otherwise needs a living target for the record's spell plus a
   spell the moment admits. */
bool CanUseItemForAction(int party_slot, const W8ItemInstance* item)
{
    const W8ItemDatabaseRecord* record = &Data().items[item->iItemNo];
    W8Character* character = &CurrentGame().party.characters[party_slot];

    if (record->equip_class == W8_ITEM_EQUIP_CLASS_KEY ||
        record->equip_class == W8_ITEM_EQUIP_CLASS_OTHER || CanCastFromItem(character, item)) {
        return !CurrentGame().combat.has_value() && !CurrentGame().activity.conversation_aside &&
               !LockInteractionSuspended() && !TrapInteractionSuspended();
    }
    if (character->uiCondition[W8_CONDITION_SILENCED] != 0 &&
        record->equip_class == W8_ITEM_EQUIP_CLASS_INSTRUMENT) {
        return false;
    }

    return SpellHasAnyValidTarget(party_slot, record->spell_id,
                                  ItemClassNormalizesTarget(record)) &&
           SpellUsableNow(record->spell_id, false);
}

/* Validate an item's embedded spell for use now; nonzero reports use blocked
   with the reason notice queued through the callback. */
bool ValidateItemSpellUse(int character_index, const W8ItemInstance* item,
                          std::function<void(bool)> callback, bool needs_target)
{
    const W8ItemDatabaseRecord* record = &Data().items[item->iItemNo];
    W8Character* character = &CurrentGame().party.characters[character_index];

    if (record->equip_class == W8_ITEM_EQUIP_CLASS_KEY ||
        record->equip_class == W8_ITEM_EQUIP_CLASS_OTHER || CanCastFromItem(character, item)) {
        if (CurrentGame().combat.has_value() || CurrentGame().activity.conversation_aside ||
            LockInteractionSuspended() || TrapInteractionSuspended()) {
            ShowNoticeLine(Data().strings[0x7a6], callback, true, false);
            return true;
        }
        return false;
    }
    if (character->uiCondition[W8_CONDITION_SILENCED] != 0 &&
        record->equip_class == W8_ITEM_EQUIP_CLASS_INSTRUMENT) {
        ShowNoticeLine(Data().strings[0x7a7], callback, true, false);
        return true;
    }
    if (needs_target && !SpellHasAnyValidTarget(character_index, record->spell_id,
                                                ItemClassNormalizesTarget(record))) {
        ShowNoticeLine(Data().strings[0x7a8], callback, true, false);
        return true;
    }
    if (!SpellUsableNow(record->spell_id, false)) {
        ShowNoticeLine(Data().strings[0x7a6], callback, true, false);
        return true;
    }
    return false;
}

/* An identified casting item the character can activate whose record holds
   a service spell (0x03/0x29). Every carried location applies this test. */
static bool IsActivatableServiceItem(W8Character* character, const W8ItemInstance* item)
{
    if (item->iItemNo != -1 && CanCharacterActivateItem(character, item)) {
        const W8ItemDatabaseRecord* record = &Data().items[item->iItemNo];
        if (record->equip_class != W8_ITEM_EQUIP_CLASS_KEY &&
            record->equip_class != W8_ITEM_EQUIP_CLASS_OTHER && item->identified &&
            (record->spell_id == W8_SPELL_CHARM || record->spell_id == W8_SPELL_MINDREAD)) {
            return true;
        }
    }
    return false;
}

/* Whether the character carries an activatable service item across the
   backpack, equipment and the party pool. */
bool CharacterHasServiceItem(W8Character* character)
{
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        if (IsActivatableServiceItem(character, &character->backpack[index])) {
            return true;
        }
    }
    for (index = 0; index < 0xc; ++index) {
        if (IsActivatableServiceItem(character, &character->EquippedItem[index])) {
            return true;
        }
    }
    for (const auto& item : CurrentGame().inventory.pool()) {
        if (IsActivatableServiceItem(character, &item)) {
            return true;
        }
    }
    return false;
}

/* Rate how hard one attempt at an item's spell is. The base is the ordinary
   spell failure chance for the character's level in the skill that presents
   the spell; the two casting-aid skills then add a shortfall term - fifteen
   times the spell's own level against that skill level, in thirds - and a term
   for each power level the caster is short of the spell's minimum. Skill
   twenty is the most forgiving of the four and takes ten percent off, while
   skill nine takes twenty. */
unsigned int GetItemUseDifficulty(const W8Character* character, W8Skill skill,
                                  unsigned int skill_level, unsigned int spell_id,
                                  unsigned int power)
{
    unsigned int failure = GetSpellFailureChance(skill_level, spell_id, power);
    unsigned int shortfall;
    int minimum_caster_level;
    int caster_level;
    int adjusted_power;

    switch (skill) {
    case W8_SKILL_THROWING_SLING:
        return failure * 8 / 10;
    case W8_SKILL_MUSIC:
    case W8_SKILL_ENGINEERING:
        shortfall = Data().spells[spell_id].spell_level * 0xf;
        if (skill_level < shortfall) {
            failure += (shortfall - skill_level) / 3;
        }
        minimum_caster_level = GetMinimumCasterLevelForSpell(spell_id);
        caster_level = skill == W8_SKILL_MUSIC
                           ? character->profession_levels[W8_PROFESSION_BARD]
                           : character->profession_levels[W8_PROFESSION_GADGETEER];
        adjusted_power = (minimum_caster_level - caster_level) - 1 + static_cast<int>(power);
        if (adjusted_power > 0) {
            return failure + Data().spells[spell_id].spell_level * adjusted_power;
        }
        break;
    case W8_SKILL_ARTIFACTS:
        failure = failure * 9 / 10;
        break;
    default:
        break;
    }
    return failure;
}

/* Publish the complete selected item action before executing it. Identify
   opens its second selection step instead of executing immediately. */
void SetCharacterItemUse(int party_slot, const W8Action& action)
{
    const auto* item = CurrentGame().inventory.itemAt(action.detail_block.item_use.item);
    auto detail = action.detail_block;
    detail.item_use.kind = -1;
    StagePartySlotItemUse(party_slot, action.detail_block.item_use.item, &action.target);
    const bool identify = Data().items[item->iItemNo].spell_id == W8_SPELL_IDENTIFY_ITEM;
    if (CurrentGame().combat.has_value()) {
        SetCharacterCombatAction(party_slot, {W8_ACTION_USE_ITEM, -1, action.target, detail}, 1);
    } else {
        CurrentGame().party.rows[party_slot].executing.target = action.target;
        ChooseAction(party_slot, W8_ACTION_USE_ITEM, -1, &detail, identify, 1);
    }
    if (identify) {
        OpenCharacterScreenForPartySlot(party_slot, true);
    }
}

/* Stage the slot row's item-use detail block, aimed target and provenance for
   an item use that is about to commit. */
void StagePartySlotItemUse(int party_slot, W8ItemLocation location, const W8CombatSlot* target)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];

    row->remembered.item_detail.item_use = CurrentGame().inventory.useAt(location);
    row->remembered.item_target = *target;
}

/* Cast the spell an item carries and spend the uses it cost. The attempt is
   refused outright when the record cannot carry a spell at all; the difficulty
   comes from the skill that presents the spell, paced by the combat clock and
   raised for an item nobody has identified yet; the sound is keyed by the
   item's own resource name; and the effect's outcome decides whether the item
   is marked used, reported as a finding, or practised on afterwards. */
int CastItemSpell(W8Character* character, W8ItemInstance* item, unsigned int power)
{
    const W8ItemDatabaseRecord* record = &Data().items[item->iItemNo];
    unsigned int party_slot = CharacterPointerToPartySlot(character);
    unsigned int spell_id = record->spell_id;
    W8TargetSource target;
    bool rejected_spell;
    unsigned int difficulty = 0;
    W8Skill skill;
    int effect;
    int caster_figure;
    W8SpellCastKind difficulty_kind;

    /* A record with no spell, an off-hand-only class and the two casting-aid
       classes are the ones the spell engine is told about rather than cast. */
    rejected_spell =
        (record->spell_id == W8_SPELL_NONE || record->equip_class == W8_ITEM_EQUIP_CLASS_GADGET ||
         (record->equip_class > W8_ITEM_EQUIP_CLASS_SCROLL &&
          record->equip_class < W8_ITEM_EQUIP_CLASS_KEY))
            ? 1
            : 0;
    if (!ValidateSpellTarget(party_slot, spell_id, power, true, rejected_spell)) {
        return 0;
    }

    if (record->spell_id == 'X' || record->spell_id == 't') {
        skill = W8_SKILL_NONE;
    } else {
        skill = g_item_spell_presentation[record->category];
        if (skill == W8_SKILL_NONE) {
            difficulty = 0;
        } else {
            difficulty = GetItemUseDifficulty(character, skill, character->skills[skill].level,
                                              spell_id, power);
            ScaleByCombatPace(party_slot, &difficulty);
            if (!item->identified) {
                difficulty += 0x1e;
            }
        }
    }

    SetTargetSourceToCharacter(party_slot, &target);
    target.item_cast = true;
    TrackItemSpellSource(character, spell_id);

    if (skill == W8_SKILL_THROWING_SLING) {
        difficulty_kind = W8SpellCastKind::ThrowingItemUse;
        caster_figure = character->uiExpLevel;
    } else if (skill == W8_SKILL_MUSIC) {
        difficulty_kind = W8SpellCastKind::MusicItemUse;
        caster_figure = character->profession_levels[W8_PROFESSION_BARD];
    } else if (skill == W8_SKILL_ENGINEERING) {
        difficulty_kind = W8SpellCastKind::EngineeringItemUse;
        caster_figure = character->profession_levels[W8_PROFESSION_GADGETEER];
    } else {
        difficulty_kind = W8SpellCastKind::ItemUse;
        caster_figure = character->uiExpLevel;
    }
    target.spell_difficulty =
        static_cast<unsigned char>(GetSpellDifficulty(caster_figure, spell_id, power));

    if (record->sound_name.size() != 0) {

        wiz8::sound::play(fmt::format("Data\\Spells\\Sounds\\{}.wav", record->sound_name));
    }

    {
        int power_out = static_cast<int>(power);
        effect = CastSpellFromSource(spell_id, &target,
                                     &CurrentGame().party.rows[party_slot].executing.target, power,
                                     0, difficulty, false, &power_out, difficulty_kind, 0, 0);
        power = static_cast<unsigned int>(power_out);
    }

    if (effect == 1) {
        if (g_settings.verbose_combat_messages || !item->identified) {
            ShowNotice(W8_FONT_PALETTE_WHITE,
                       fmt::sprintf(Data().strings[0x1f4], GetItemDisplayName(Data(), item),
                                    Data().spells[spell_id].display_name, power),
                       -1);
            if (!g_settings.verbose_combat_messages) {
                SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
            }
        }
        item->spell_hint = true;
    } else if (effect == 3) {
        PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x1a9],
                                                     FormatItemDisplayName(Data(), item, false)));
    }

    if (skill != W8_SKILL_NONE) {
        if (AttemptSpellPractice(spell_id, &CurrentGame().party.rows[party_slot].executing.target,
                                 power, static_cast<std::uint64_t>(CurrentGame().time.count()))) {
            PracticeCharacterSkill(CurrentGame(), character, skill,
                                   Data().spells[spell_id].spell_level + (power >> 1), false);
        }
    }
    return effect;
}
