#include <array>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_filesystem.h>
#include <fmt/format.h>

#include "asset_paths.h"
#include "audio_resources.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/attack_modes.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/missile.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/CombatSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/random.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/targeting.h"

static std::array<std::string, 38> g_weapon_attack_sounds;
static std::array<std::array<std::string, 12>, 28> g_material_impact_sounds;

/* Build the selected variant without modifying the borrowed sound name.
   Positive volume is scaled by the effects setting; other values use the default. */
void PlayCombatSound(std::string_view sound_name, unsigned int variant_count, bool store_handle,
                     int volume)
{
    std::string zSoundFileName;
    wiz8::sound::Playback handle;

    if (sound_name.empty()) return;
    const auto variant =
        variant_count > 1 ? fmt::format("{}", PresentationRng().below(variant_count) + 1) : "";
    zSoundFileName = fmt::format("Data\\Sound\\Combat\\{}{}.wav", sound_name, variant);
    if (![&]() {
            const auto status = wiz8::file_status(wiz8::asset_path(zSoundFileName));
            return status && status->info.type == SDL_PATHTYPE_FILE;
        }()) {
        Fail("[&]() { const auto status = "
             "wiz8::file_status(wiz8::asset_path(zSoundFileName)); return status "
             "&& status->info.type == SDL_PATHTYPE_FILE; }()",
             fmt::format("CombatSound: ERROR - Sound file {} not found", zSoundFileName));
    }
    if (volume > 0) {
        handle = wiz8::sound::play(
            zSoundFileName,
            {.volume = static_cast<unsigned int>(g_settings.sound_effects_volume * volume) / 127});
    } else {
        handle = wiz8::sound::play(zSoundFileName);
    }
    if (handle != nullptr && CurrentGame().combat.has_value() && store_handle) {
        CurrentGame().combat->hit_sound = handle;
        CurrentGame().combat->hit_sound_active = true;
    }
}

unsigned char LoadHitSoundDatabase(void)
try {
    std::istringstream input(
        wiz8::open_file(wiz8::asset_path("Data/Databases/HitSounds.txt"))->read_text());
    decltype(g_weapon_attack_sounds) attacks;
    decltype(g_material_impact_sounds) impacts;
    std::string line;
    auto trim = [&] {
        if (const auto comment = line.find('*'); comment != line.npos) line.resize(comment);
        line.resize(line.find_last_not_of(" \t\r\n\v\f") + 1);
    };
    std::size_t row = 0;
    while (row < attacks.size() && std::getline(input, line)) {
        trim();
        if (const auto marker = line.find('#'); marker != line.npos) line.resize(marker);
        trim();
        if (!line.empty()) attacks[row++] = line;
    }
    if (row != attacks.size()) return false;
    row = 0;
    int column = -1;
    while (std::getline(input, line)) {
        if (line.find('#') != line.npos) {
            ++column;
            row = 0;
            continue;
        }
        trim();
        if (line.empty()) continue;
        if (row >= impacts.size() || column < 0 || column >= 12) return false;
        impacts[row++][column] = line;
    }
    g_weapon_attack_sounds = std::move(attacks);
    g_material_impact_sounds = std::move(impacts);
    return true;
} catch (const std::exception&) {
    return false;
}

void ReleaseHitSoundDatabase(void)
{
    g_weapon_attack_sounds = {};
    g_material_impact_sounds = {};
}

// Missing weapon-specific entries inherit weapon class zero.
std::string_view GetMaterialImpactSound(int weapon_class, int target_material)
{
    if (weapon_class < 0 || weapon_class >= 28 || target_material < 0 || target_material >= 12)
        return "HIT";
    const auto& sound = g_material_impact_sounds[weapon_class][target_material];
    return sound.empty() ? g_material_impact_sounds[0][target_material] : sound;
}

/* The equipment slot covering one armour-class hit location, then the item
   worn there (-1 when that location is bare).  The inlined copies share the
   line-168 assertion. */
static int PCItemInACSlot(const W8Character* character, int hit_location)
{
    W8EquipSlot slot = W8_EQUIP_SLOT_HEAD;

    switch (hit_location) {
    case 0:
        slot = W8_EQUIP_SLOT_HEAD;
        break;
    case 1:
        slot = W8_EQUIP_SLOT_TORSO;
        break;
    case 2:
        slot = W8_EQUIP_SLOT_LEGS;
        break;
    case 3:
        slot = W8_EQUIP_SLOT_HANDS;
        break;
    case 4:
        slot = W8_EQUIP_SLOT_FEET;
        break;
    default:
        Fail("FALSE", "PCItemInACSlot: ERROR - Invalid AC location");
    }
    return character->EquippedItem[slot].iItemNo;
}

void MakePCAttackSound(W8CombatCharacterRow* row, const W8HandAttack* hand_attack, W8AttackMode,
                       bool store_handle, int volume)
{
    int weapon_class;

    if (hand_attack->uiHolds == HOLDS_NOTHING) {
        weapon_class = 9;
    } else {
        weapon_class = Data().items[row->weapon_item_id].weapon_sound_class;
        if (weapon_class < 0 || weapon_class >= 38) {
            return;
        }
    }
    PlayCombatSound(g_weapon_attack_sounds[weapon_class], 1, store_handle, volume);
}

static int GetCombatTargetMaterial(W8CombatSlot* target, int hit_location, int assertion_line)
{
    int target_material = -1;
    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        const W8Character* character = &CurrentGame().party.characters[target->iChar];
        int item = PCItemInACSlot(character, hit_location);
        target_material = item == -1 ? 0 : Data().items[item].material;
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        const W8MonsterRecord* record = GetMonsterDataByLocationID(target->iMonsterID);
        target_material = record == 0 ? 0 : record->material;
    } else {
        Fail("FALSE", "MakePCHitSound : Unknown target type");
    }
    return target_material;
}

void MakePCMeleeHitSound(int iChar, const W8HandAttack* hand_attack, W8CombatSlot* target,
                         int hit_location, int volume)
{
    int weapon_class;

    if (hand_attack->uiHolds == HOLDS_NOTHING) {
        weapon_class = 9;
    } else {
        weapon_class =
            Data().items[CurrentGame().combat->characters[iChar].paired_item_id].weapon_sound_class;
    }
    int target_material = GetCombatTargetMaterial(target, hit_location, 415);
    PlayCombatSound(GetMaterialImpactSound(weapon_class, target_material), 1, true, volume);
}

void MakePCHitSound(Missile* missile, W8CombatSlot* target, int hit_location, int volume)
{
    int weapon_class = Data().missiles[missile->missile_table_index].weapon_sound_class;

    int target_material = GetCombatTargetMaterial(target, hit_location, 465);
    PlayCombatSound(GetMaterialImpactSound(weapon_class, target_material), 1, true, volume);
}

void MakeMonsterHitSound(const W8MonsterAttack* attack, W8CombatSlot* target, int hit_location,
                         int volume)
{
    int weapon_class;

    if (attack == 0) {
        return;
    }
    if (target == 0) {
        return;
    }
    weapon_class = attack->weapon_class;
    int target_material = GetCombatTargetMaterial(target, hit_location, 504);
    PlayCombatSound(GetMaterialImpactSound(weapon_class, target_material), 1, true, volume);
}
