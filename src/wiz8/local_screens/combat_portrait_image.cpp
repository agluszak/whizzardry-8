#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/records.h"
#include "wiz8/game.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/targeting.h"

/* The combat-strip portrait catalog index for one slot's chosen action and
   status. Most actions map to a fixed base; attacks pick per weapon skill and
   spells per realm, and the status selects the frame variant beside it. */
short GetCombatPortraitImage(W8ActionKind action, int detail, W8CombatPortraitStatus status,
                             short slot)
{
    int image;

    switch (action) {
    case W8_ACTION_ATTACK:
        image = 14 * GetAttackMenuWeaponOffset(
                         CurrentGame().party.characters[slot].Hand[0].weapon_skill);
        break;
    case W8_ACTION_BERSERK:
        image = 0x7e;
        break;
    case W8_ACTION_BREATHE:
        image = 0x8c;
        break;
    case W8_ACTION_TURN_UNDEAD:
        image = 0x9a;
        break;
    case W8_ACTION_PRAY:
        image = 0xa8;
        break;
    case W8_ACTION_DEFEND:
        image = 0xb6;
        break;
    case W8_ACTION_PROTECT:
        image = 0xc4;
        break;
    case W8_ACTION_USE_ITEM:
        image = 0x142;
        break;
    case W8_ACTION_EQUIP:
        image = 0x134;
        break;
    case W8_ACTION_CAST_SPELL:
        switch (Data().spells[detail].realm) {
        case W8_SPELL_REALM_FIRE:
            image = 0x126;
            break;
        case W8_SPELL_REALM_WATER:
            image = 0xe0;
            break;
        case W8_SPELL_REALM_AIR:
            image = 0x118;
            break;
        case W8_SPELL_REALM_EARTH:
            image = 0xee;
            break;
        case W8_SPELL_REALM_MENTAL:
            image = 0xfc;
            break;
        case W8_SPELL_REALM_DIVINE:
            image = 0x10a;
            break;
        default:
            image = 0xd2;
        }
        break;
    case W8_ACTION_WALK:
        image = 0x15e;
        break;
    case W8_ACTION_RUN:
        image = 0x16c;
        break;
    default:
        if (status == W8_COMBAT_PORTRAIT_INELIGIBLE) {
            return 0x17d;
        }
        return (GetCharAttackRange(CurrentGame().data, &CurrentGame().party.characters[slot], 2) !=
                W8_RANGE_NONE)
                   ? 0x17a
                   : 0x17c;
    }
    switch (status) {
    case W8_COMBAT_PORTRAIT_READY:
        break;
    case W8_COMBAT_PORTRAIT_CANNOT_SWITCH:
        return image + 2;
    case W8_COMBAT_PORTRAIT_INELIGIBLE:
        return image + 3;
    case W8_COMBAT_PORTRAIT_ACTING:
        return image + 4;
    default:
        image = 0x17a;
        break;
    }
    return image;
}
