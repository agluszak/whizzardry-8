#include "wiz8/local_code/spell_database.h"

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "monster_spell_icons.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/entity_assets.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/render/light.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

W8SpellTargetType GetSpellTargetType(int spell_id, bool normalize_single_target)
{
    W8SpellTargetType target_type = Data().spells[spell_id].target_type;

    if (target_type == W8_TARGET_TYPE_ALLY && normalize_single_target) {
        target_type = W8_TARGET_TYPE_CASTER;
    }
    return target_type;
}

int GetMinimumCasterLevelForSpell(int spell_id)
{
    return MinimumCasterLevelForSpellLevel(Data().spells[spell_id].spell_level);
}

/* Whether one spell id is among the six that occupy a persistent combat
   effect slot - Armormelt, Acid Bomb, Toxic Cloud, Firestorm, Death Cloud and
   Draining Cloud, the spells that leave a standing hazard instead of resolving
   once. MonsterAI walks the same list through
   g_combat_effect_slot_spells to skip re-casting one that is still
   running, and the effect update loop uses it to keep the per-cast
   result/report path off the lingering spells. */
bool IsCombatEffectSlotSpell(int spell_id)
{
    switch (spell_id) {
    case W8_SPELL_ACID_BOMB:
    case W8_SPELL_ARMORMELT:
    case W8_SPELL_TOXIC_CLOUD:
    case W8_SPELL_DRAINING_CLOUD:
    case W8_SPELL_FIRESTORM:
    case W8_SPELL_DEATH_CLOUD:
        return true;
    default:
        return false;
    }
}

/* The forty icon resource names under Data\Icons\MonsterSpells. Entries 8 and
   9 share the pooled "Hexed" literal in retail. */
const char* g_monster_spell_icon_names[40] = {
    "Hexed",           "Diseased",     "Irritated",      "Nauseated",      "Slowed",
    "Afraid",          "Poisoned",     "Silenced",       "Hexed",          "Hexed",
    "Insane",          "Blind",        "Turncoat",       "Webbed",         "Asleep",
    "Paralyzed",       "Unconscious",  "Dracon_Breath",  "Guardian_Angel", "Razor_Cloak",
    "Eye4Eye",         "Haste",        "Super_Man",      "Body_Stone",     "Armor_Plate",
    "Enchanted_Blade", "Magic_Screen", "Missile_Shield", "Armor_Melt",     "Acid_Cloud",
    "Toxic_Cloud",     "Fire_Storm",   "Death_Cloud",    "Draining_Cloud", "Bless",
    "Element_Shield",  "Soul_Shield",  "Ring_Of_Fire",   "Charmed",        "Summoned",
};

void AddMonsterSpellIcon(World& world, Monster* monster, W8MonsterSpellIconId icon)
{
    assert(monster && icon >= 0 && icon < SPELL_NUM_ICONS);
    auto& icons = monster->spell_icons;
    icons.reserve(icons.size() + 1);
    const auto path =
        fmt::format("data/icons/monsterspells/{}_A.TGA", g_monster_spell_icon_names[icon]);
    icons.push_back({icon, CreateMonsterIconItem(world, path.c_str())});
}

void SetMonsterSpellIcon(Monster* monster, W8MonsterSpellIconId icon, bool add)
{
    assert(monster && icon >= 0 && icon < SPELL_NUM_ICONS);
    auto& icons = monster->spell_icons;
    if (add) {
        AddMonsterSpellIcon(*GetWorld(), monster, icon);
    } else {
        for (auto entry = icons.begin(); entry != icons.end();) {
            if (entry->icon != icon) {
                ++entry;
                continue;
            }
            entry->item->DetachMesh();
            entry = icons.erase(entry);
        }
    }
}

void ClearMonsterSpellIcons(Monster* monster)
{
    assert(monster);
    for (const auto& entry : monster->spell_icons) {
        entry.item->DetachMesh();
    }
    monster->spell_icons.clear();
}

bool CanSpellBackfire(int spell_id)
{
    switch (Data().spells[spell_id].target_type) {
    case W8_TARGET_TYPE_ENEMY:
    case W8_TARGET_TYPE_ENEMY_GROUP:
    case W8_TARGET_TYPE_CONE:
    case W8_TARGET_TYPE_RADIUS:
    case W8_TARGET_TYPE_ALL_ENEMIES:
        /* Hostile spells backfire except Banish (0x4d) and the two creature
           abilities Boiling Blood Explosion (0x76) and Rocket Blast (0x83). */
        switch (spell_id) {
        case W8_SPELL_BANISH:
        case W8_SPELL_BOILING_BLOOD_EXPLOSION:
        case W8_SPELL_ROCKET_BLAST:
            break;
        default:
            return true;
        }
        break;
    case W8_TARGET_TYPE_CASTER:
    case W8_TARGET_TYPE_ALLY:
    case W8_TARGET_TYPE_PARTY:
        /* Friendly spells are safe except Charm (3 - unreachable here, it is
           enemy-targeted), Stamina (0xd), Cure Lesser Condition (0x10), Cure
           Paralysis (0x22), Rest All (0x2c), Haste (0x38), Remove Curse (0x3a)
           and Sane Mind (0x4a). */
        switch (spell_id) {
        case W8_SPELL_CHARM:
        case W8_SPELL_STAMINA:
        case W8_SPELL_CURE_LESSER_COND:
        case W8_SPELL_CURE_PARALYSIS:
        case W8_SPELL_REST_ALL:
        case W8_SPELL_HASTE:
        case W8_SPELL_REMOVE_CURSE:
        case W8_SPELL_SANE_MIND:
            return true;
        }
        break;
    case W8_TARGET_TYPE_LOCK_OR_TRAP:
        /* Only Knock Knock (0x27); Divine Trap (0x12) never backfires. */
        return spell_id == W8_SPELL_KNOCK_KNOCK;
    case W8_TARGET_TYPE_POINT:
    case W8_TARGET_TYPE_ITEM:
    case W8_TARGET_TYPE_NONE:
    case W8_TARGET_TYPE_COUNT:
        break;
    }
    return false;
}
