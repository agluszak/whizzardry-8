#include "wiz8/local_code/Magic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_skills.h"
#include "wiz8/compat/native.h"
#include "wiz8/compat/unaligned.h"
#include "wiz8/containers.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/difficulty.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/party_motion.h"

constexpr float kGroundSettleFail = -1000000.0f;

int GetProfessionCasterLevel(const W8Character* character, W8Profession profession_id)
{
    int magic_level_offset;

    if (profession_id == W8_PROFESSION_NONE) {
        profession_id = character->iProfession;
        if (profession_id == W8_PROFESSION_NONE) {
            Fail("iProfession != -1");
        }
    }

    magic_level_offset = g_profession_magic_level_offsets[profession_id];
    if (magic_level_offset == -255) {
        return -1;
    }
    return character->profession_levels[profession_id] + magic_level_offset;
}

/* The all-enemies target type costs three off the difficulty - hitting
   everything is priced easier than picking targets. */

/* How hard one spell is to bring off. Half the caster's own figure, plus the
   caller's bonus, plus half of the spell's level and half its point cost taken
   together - so the point cost counts a quarter and the level a half. One
   target type is three easier than the rest, and the answer never goes below
   zero. */
int GetSpellDifficulty(unsigned int caster_figure, int spell_id, int bonus)
{
    int difficulty =
        (caster_figure >> 1) + bonus +
        (Data().spells[spell_id].spell_point_cost / 2 + Data().spells[spell_id].spell_level) / 2;

    if (GetSpellTargetType(spell_id, false) == W8_TARGET_TYPE_ALL_ENEMIES) {
        difficulty -= 3;
    }
    if (difficulty < 0) {
        return 0;
    }
    return difficulty;
}

/* Whether one carried item can be cast from. It has to be of the spell-source
   kind, it has to be identified, and the caster has to be able to use it. */
bool CanCastFromItem(const W8Character* caster, const W8ItemInstance* item)
{
    if (Data().items[item->iItemNo].category != W8_ITEM_CATEGORY_SPELL_SOURCE) {
        return false;
    }
    if (!item->identified) {
        return false;
    }
    return CanCharacterUseItem(caster, item->iItemNo);
}

/* The three monster kinds whose alchemy survives a spellcasting block. */
enum {
    W8_MONSTER_KIND_ALCHEMY_LOW = 4,
    W8_MONSTER_KIND_ALCHEMY_HIGH = 5,
    W8_MONSTER_KIND_ALCHEMY_OTHER = 0xd
};

/* Whether a spellcasting block stops this character casting this spell. The
   block stops everything except alchemy in the hands of someone who has the
   skill for it. */
bool IsSpellBlockedForCharacter(const W8Character* character, int spell_id)
{
    if (character->uiCondition[W8_CONDITION_SILENCED] != 0) {
        if (Data().spells[spell_id].alchemy_spell == 0) {
            return true;
        }
        return character->skills[W8_SKILL_SPELLBOOK_ALCHEMY].level == 0;
    }
    return false;
}

/* The same question for a monster. The block stops everything except alchemy
   from the three kinds that keep it. */
bool IsSpellBlockedForMonster(W8MonsterInfo* monster_info, int spell_id)
{
    unsigned char kind;

    if (monster_info->uiCondition[W8_CONDITION_SILENCED] != 0) {
        if (Data().spells[spell_id].alchemy_spell == 0) {
            return true;
        }
        kind = GetMonsterDataForInfo(monster_info)->kind;
        if (kind < W8_MONSTER_KIND_ALCHEMY_LOW ||
            (kind > W8_MONSTER_KIND_ALCHEMY_HIGH && kind != W8_MONSTER_KIND_ALCHEMY_OTHER)) {
            return true;
        }
    }
    return false;
}

/* Everything that has to hold before a monster may start casting: the spell
   has to be one monsters can cast at all, the monster has to be in a state to
   cast it, it has to have somewhere to aim, it has to be able to act, and the
   two combat gates have to agree. */
bool MonsterOKToCastSpell(W8MonsterInfo* monster_info, int spell_id, int)
{
    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    W8CombatSlot* combat_slot;
    unsigned char kind;

    if (Data().spells[spell_id].monster_castable == 0) {
        Fail("FALSE", fmt::format("MonsterOKToCastSpell: ERROR - spell not monster castable: {}",
                                  Data().spells[spell_id].display_name));
        return false;
    }

    if (monster_info->uiCondition[W8_CONDITION_SILENCED] != 0) {
        if (Data().spells[spell_id].alchemy_spell == 0) {
            return false;
        }
        kind = GetMonsterDataForInfo(monster_info)->kind;
        if (kind < W8_MONSTER_KIND_ALCHEMY_LOW ||
            (kind > W8_MONSTER_KIND_ALCHEMY_HIGH && kind != W8_MONSTER_KIND_ALCHEMY_OTHER)) {
            return false;
        }
    }

    if (GetSpellTargetType(spell_id, false) == W8_TARGET_TYPE_CASTER) {
        SetMonsterCombatTarget(monster_info, monster_info->location_id);
    } else if (!MonsterTargetMatchesSpell(monster_info, spell_id)) {
        return false;
    }

    combat_slot = &monster_info->Target;
    if (!MonsterActionReachesTarget(monster_info, record, 0, combat_slot) &&
        !ClearMonsterCombatSlot(monster_info)) {
        return false;
    }
    if (DispatchWorldCursorNodeCommand(monster_info, 4, 0)) {
        return false;
    }
    if (!MonsterSpellTargetOK(monster_info, spell_id, combat_slot)) {
        return false;
    }
    return !SpellAreaHitsNeutralMonster(monster_info, spell_id, combat_slot);
}

/* Whether the spell may be cast in the situation the party is in now. Two
   spells are always allowed on the shop screen out of combat; otherwise the
   record's usable-when value picks which of camp, combat and the shop admit
   it. The second parameter doubles as the out-of-combat override and the
   default-case return. */
bool SpellUsableNow(int spell_id, bool allow_out_of_combat)
{
    W8SpellUsage usable_when;
    bool lock_or_trap;

    if (spell_id >= W8_SPELL_COUNT) {
        Fail("uiSpell < SPELL_COUNT");
    }
    usable_when = Data().spells[spell_id].usable_when;
    if (usable_when >= W8_SPELL_USAGE_COUNT) {
        Fail("uiSpellUsableWhen < SPELL_USAGE_COUNT");
    }

    if (CurrentGame().activity.in_camp() && !CurrentGame().combat.active() &&
        CurrentGame().activity.conversation_aside &&
        (spell_id == W8_SPELL_IDENTIFY_ITEM || spell_id == W8_SPELL_REMOVE_CURSE)) {
        return true;
    }

    lock_or_trap = LockInteractionSuspended() || TrapInteractionSuspended();

    switch (usable_when) {
    case W8_SPELL_USABLE_ANY_TIME:
        break;
    case W8_SPELL_USABLE_IN_COMBAT:
        if (!CurrentGame().combat.active() && !allow_out_of_combat) {
            return false;
        }
        break;
    case W8_SPELL_USABLE_OUT_OF_COMBAT:
        if (CurrentGame().combat.active()) {
            return false;
        }
        break;
    case W8_SPELL_USABLE_WHILE_CAMPED:
        if (!CurrentGame().activity.conversation_aside) {
            return false;
        }
        return !lock_or_trap;
    case W8_SPELL_USABLE_ON_LOCK_OR_TRAP:
        if (spell_id != W8_SPELL_KNOCK_KNOCK) {
            return spell_id == W8_SPELL_DIVINE_TRAP ? TrapInteractionSuspended() : 0;
        }
        if (LockInteractionSuspended()) {
            return true;
        }
        return TrapInteractionSuspended();
    default:
        Fail("FALSE", "SpellUsableNow: ERROR - Invalid uiSpellUsableWhen");
        return allow_out_of_combat;
    }

    if (!CurrentGame().activity.conversation_aside) {
        return !lock_or_trap;
    }
    return false;
}

/* What the interface has to ask the player to pick before a friendly spell can
   be cast. Nine target types map onto seven answers; the one that depends on
   the selection owner only needs a pick when there is nothing selected already
   and the caller is not in one of the two contexts that supply it. */
W8TargetNeed GetTargetNeededForSpellFriendly(int spell_id, bool normalize, bool needs_item_target)
{
    if (spell_id != W8_SPELL_NONE) {
        switch (GetSpellTargetType(spell_id, normalize)) {
        case W8_TARGET_TYPE_ALLY:
            return spell_id != W8_SPELL_RESURRECTION ? W8_TARGET_NEED_ALLY
                                                     : W8_TARGET_NEED_CHARACTER_INDIRECT;
        case W8_TARGET_TYPE_CASTER:
            return W8_TARGET_NEED_CASTER;
        case W8_TARGET_TYPE_ENEMY:
            return W8_TARGET_NEED_ENEMY;
        case W8_TARGET_TYPE_ENEMY_GROUP:
            return W8_TARGET_NEED_GROUP;
        case W8_TARGET_TYPE_CONE:
            return W8_TARGET_NEED_CONE;
        case W8_TARGET_TYPE_RADIUS:
        case W8_TARGET_TYPE_POINT:
            return W8_TARGET_NEED_PLACE;
        case W8_TARGET_TYPE_ITEM:
            if (needs_item_target) {
                return W8_TARGET_NEED_ITEM;
            }
            break;
        case W8_TARGET_TYPE_PARTY:
        case W8_TARGET_TYPE_ALL_ENEMIES:
        case W8_TARGET_TYPE_LOCK_OR_TRAP:
            break;
        default:
            Fail("FALSE",
                 fmt::format(
                     "GetTargetNeededForSpellFriendly: ERROR - Invalid spell target for {:d}",
                     spell_id));
            break;
        }
    }
    return W8_TARGET_NEED_NONE;
}

/* The same question for a hostile spell, which has fewer answers because a
   hostile spell never targets the party's own belongings. */
W8TargetNeed GetTargetNeededForSpellHostile(int spell_id)
{
    switch (GetSpellTargetType(spell_id, false)) {
    case W8_TARGET_TYPE_ALLY:
        return spell_id != W8_SPELL_RESURRECTION ? W8_TARGET_NEED_ALLY
                                                 : W8_TARGET_NEED_CHARACTER_INDIRECT;
    case W8_TARGET_TYPE_ENEMY:
        return W8_TARGET_NEED_ENEMY;
    case W8_TARGET_TYPE_CASTER:
        return W8_TARGET_NEED_CASTER;
    case W8_TARGET_TYPE_ENEMY_GROUP:
        return W8_TARGET_NEED_GROUP;
    case W8_TARGET_TYPE_PARTY:
    case W8_TARGET_TYPE_CONE:
    case W8_TARGET_TYPE_RADIUS:
    case W8_TARGET_TYPE_ALL_ENEMIES:
    case W8_TARGET_TYPE_POINT:
        break;
    default:
        Fail("FALSE",
             fmt::format("GetTargetNeededForSpellHostile: ERROR - Invalid spell target for {:d}",
                         spell_id));
        break;
    }
    return W8_TARGET_NEED_NONE;
}

/* Forwarder that narrows CanCharReBreathe's answer to a flag. Its argument is
   a party slot, not a spell - which is only visible once the underlying
   predicate is named. */
bool CanPartySlotReBreathe(int party_slot)
{
    if (!CanCharReBreathe(party_slot)) {
        return false;
    }
    return true;
}

/* One queued spell effect. Each entry counts down a turn at a time and is
   distinguished only by its kind; the effect body itself lives elsewhere. */
enum { W8_PARTY_CONDITION_SLOTS = 12, W8_COMBAT_CONDITION_SLOTS = 9 };

/* Debug switch: when set, every cast except 0x76 fizzles on a forced 100
   percent failure chance. */
bool g_force_spell_failure;
/* Whether every queued effect still has time left on it. */
bool AllSpellEffectsStillRunning(void)
{
    int index;

    for (index = 0; index < static_cast<int>(CurrentGame().spell_effects.size()); ++index) {
        if (CurrentGame().spell_effects[index]->turns_remaining == 0) {
            return false;
        }
    }
    return true;
}

W8SpellEffectEntry* FindMonsterControlSpellEffect(void)
{
    for (const auto& owned : CurrentGame().spell_effects) {
        auto& effect = *owned;
        if (effect.kind == W8_SPELL_HYPNOTIC_LURE) return &effect;
    }
    return nullptr;
}

/* Count every queued effect down by one turn. The monster-control effect
   running out is the one with a consequence: every monster that is still alive
   goes back to controlling itself. */
/* An expired or replaced hypnotic lure releases every living monster. */
static void ReleaseHypnoticLureControl()
{
    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
        W8MonsterInfo* monster = GetMonsterListEntry(index);
        if (monster && !monster->p3D->IsDying())
            SetMonsterControlState(monster, W8_MONSTER_CONTROL_NONE);
    }
}

void TickSpellEffects(void)
{
    for (const auto& effect : CurrentGame().spell_effects) {
        if (effect->turns_remaining != 0) {
            --effect->turns_remaining;
            if (effect->turns_remaining == 0 && effect->kind == W8_SPELL_HYPNOTIC_LURE)
                ReleaseHypnoticLureControl();
        }
    }
}

bool PartyHasCondition(int effect_id)
{
    W8EffectSlot* slot = CurrentGame().status.effect_slots;

    while (!slot->active || slot->effect_id != effect_id) {
        ++slot;
        if (slot > &CurrentGame().status.effect_slots[W8_PARTY_CONDITION_SLOTS - 1]) {
            return false;
        }
    }
    return true;
}

/* Whether either side of the current fight is under one particular condition.
   Both tables are searched, the party's first. */
bool CombatHasCondition(int effect_id)
{
    if (CurrentGame().combat.state()) {
        for (const auto& slot : CurrentGame().combat.state()->effect_slots) {
            if (slot.active && slot.effect_id == effect_id) return true;
        }
        for (const auto& slot : CurrentGame().combat.state()->effect_slots0) {
            if (slot.active && slot.effect_id == effect_id) return true;
        }
    }
    return false;
}

void SetPartySlotSpell(int party_slot, int spell_id, int power_level, const W8CombatSlot* target)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];

    row->remembered.spell.kind = W8_ACTION_CAST_SPELL;
    row->remembered.spell.detail = spell_id;
    row->remembered.spell.detail_block.spell.power_level = power_level;
    row->remembered.spell.detail_block.spell.unused = 0;
    row->remembered.spell.target = *target;
}

void SetCharacterSpell(const W8Character* character, const W8Action& spell)
{
    const int party_slot = CharacterPointerToPartySlot(character);
    auto& row = CurrentGame().party.rows[party_slot];
    row.remembered.spell = spell;
    if (CurrentGame().combat.active()) {
        SetCharacterCombatAction(party_slot, spell, 1);
    } else {
        row.executing.target = spell.target;
        ChooseAction(party_slot, spell.kind, spell.detail, &spell.detail_block, false, 1);
    }
    StartBreathCycle(party_slot, true);
}

/* Start one character's breath attack; the character must be able to
   breathe again. */
void StartCharacterBreathAttack(int party_slot)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];

    if (!CanCharReBreathe(party_slot)) {
        Fail("CanCharReBreathe(uiChar)");
    }
    ChooseAction(party_slot, W8_ACTION_BREATHE, -1, 0, false, 1);
    AimAtTarget(party_slot, &row->remembered.breath_target, GetEditedActionContext(party_slot));
    if (IsSpellTargetStillValid(party_slot, W8_SPELL_SPECIAL_ATTACK_CONE,
                                SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_BREATH))) {
        StartBreathCycle(party_slot, false);
        return;
    }
    FallbackFromUnreachableAction(party_slot);
}

/* Fold one missile's accumulated damage and reports into the queued effect
   that owns it. The owning effect is the one whose missile list still names
   the missile; nothing happens if it has already been detached. */
void AbsorbMissileDamage(Missile* missile)
{
    for (const auto& effect : CurrentGame().spell_effects) {
        for (int missile_index = 0; missile_index < static_cast<int>(effect->missiles.size());
             ++missile_index) {
            if (effect->missiles[missile_index] == missile) {
                effect->result.count += missile->result.count;
                effect->result.amount += missile->result.amount;
                for (int condition = 0; condition < W8_CONDITION_COUNT; ++condition) {
                    effect->result.condition_counts[condition] +=
                        missile->result.condition_counts[condition];
                }
                auto& source_reports = missile->result.reports;
                auto& destination_reports = effect->result.reports;
                destination_reports.insert(destination_reports.end(),
                                           std::make_move_iterator(source_reports.begin()),
                                           std::make_move_iterator(source_reports.end()));
                source_reports.clear();
                return;
            }
        }
    }
}

void FinishSpellEffect(W8SpellEffectEntry* effect);

/* Advance every queued spell effect one frame. An effect first checks that
   everything it owns is still live: its visuals have started, its missiles
   carry their launch flag, and its monster list still points at live manager
   entries. An effect whose activation byte is set turns its missiles into the
   spell's own visuals and clears that byte; an already-active effect applies
   its monster-control consequence to the combat selection. An effect that has
   run out is finalized: a spell that needed aiming and is not in the singled
   out set reports itself, its visuals and missiles are released, and the entry
   is removed from the queue and deleted. */
void UpdateSpellEffects(void)
{
    bool handled = false;
    int index;

    if (static_cast<int>(CurrentGame().spell_effects.size()) <= 0) {
        return;
    }
    for (index = 0; index < static_cast<int>(CurrentGame().spell_effects.size()); ++index) {
        auto& effect = *CurrentGame().spell_effects[index];
        bool alive = true;

        for (int visual_index = 0;
             visual_index < static_cast<int>(effect.spell_visuals.size()) && alive;
             ++visual_index) {
            SpellVisual* visual = effect.spell_visuals[visual_index];
            if (!visual->finished) {
                alive = false;
            }
        }
        for (int missile_index = 0;
             missile_index < static_cast<int>(effect.missiles.size()) && alive; ++missile_index) {
            Missile* missile = effect.missiles[missile_index];
            if (!missile->flight_done) {
                alive = false;
            }
        }
        for (int monster_index = 0;
             monster_index < static_cast<int>(effect.target_indices.size()) && alive;
             ++monster_index) {
            int entry = effect.target_indices[monster_index];
            if (CurrentGame().monster_manager_entries[entry].effect_icon_active) {
                alive = false;
            }
        }

        bool process =
            ((Data().spells[effect.kind].missile_delivered == 0 && !effect.missiles_pending) ||
             effect.sustained || alive) &&
            !effect.targets_resolved;
        if (process) {

            if (effect.missiles_pending) {
                Missile* missile = 0;

                for (int missile_index = 0;
                     missile_index < static_cast<int>(effect.missiles.size()); ++missile_index) {
                    missile = effect.missiles[missile_index];
                    missile->block_released = 1;
                    effect.missiles.erase(effect.missiles.begin() + missile_index);
                }
                if (missile != 0) {
                    glm::vec3 position = missile->nav.GetPosition();
                    position.y -= 1e+03f;
                    SpellVisual* visual =
                        SpawnSpellEffect(&position, Data().spells[effect.kind].resource_name,
                                         missile->definition.duration_scale, 0, 0);
                    if (visual != 0) {
                        visual->auto_release = false;
                        effect.spell_visuals.push_back(visual);
                        alive = false;
                    }
                }
                effect.missiles_pending = false;
                handled = true;
            } else {
                effect.targets_resolved = true;
                if (MonsterCanAimSpell(effect.kind) && !effect.Source.fBackfire &&
                    !effect.Source.fReflection) {
                    ProvokeListedMonsterGroups(&effect.Source, effect.monster_ids);
                }
                ProcessSpellEffectTargets(&effect);
                if (TargetSourceIsMonster(&effect.OrigSource, 0)) {
                    if (effect.OrigSource.iMonsterID == -1) {
                        Fail("pOrigSource->iMonsterID != -1");
                    }
                    /* The casting monster's turn finishes with its spell. */
                    if (CurrentGame().combat.active()) {
                        CurrentGame().combat.state()->setStepOf(
                            MonsterActor{effect.OrigSource.iMonsterID}, ActionStep::finishing);
                    }
                }
            }
        }

        if (effect.sustained) {
            if (effect.turns_remaining != 0) {
                continue;
            }
        } else if (!alive || !effect.targets_resolved) {
            continue;
        }

        if (!handled && Data().spells[effect.kind].needs_aim != 0 &&
            !IsCombatEffectSlotSpell(effect.kind)) {
            W8SpellTargetType target_type = GetSpellTargetType(effect.kind, false);
            if (target_type != W8_TARGET_TYPE_RADIUS && target_type != W8_TARGET_TYPE_PARTY) {
                if (g_settings.verbose_combat_messages == 0) {
                    ReportSpellResult(&effect);
                }
                FinishSpellEffectTargets(&effect);
            }
        }
        if (effect.kind == W8_SPELL_BOILING_BLOOD) {
            FinishSpellEffect(&effect);
        }
        for (int release_visual = 0; release_visual < static_cast<int>(effect.spell_visuals.size());
             ++release_visual) {
            SpellVisual* visual = effect.spell_visuals[release_visual];
            visual->auto_release = true;
            if (effect.sustained) {
                visual->finished = true;
            }
        }
        for (auto* missile : effect.missiles) {
            missile->block_released = 1;
        }
        CurrentGame().spell_effects.erase(CurrentGame().spell_effects.begin() + index);
        --index;
    }
}

/* Take one missile back off the spell effect that owns it. The first effect
   whose missile list contains it drops the entry and stops the walk. */
void DetachMissileReferences(Missile* missile)
{
    // The spell effects end with their game; a missile may outlive it.
    if (!GameInProgress()) return;
    for (const auto& effect : CurrentGame().spell_effects) {
        const bool was_removed = wiz8::erase_first(effect->missiles, missile);
        if (was_removed) {
            return;
        }
    }
}

/* Which spell a missile type carries, for the missile kinds that are spells
   at all. Anything else carries no spell. */
int MissileSpellId(int missile_type)
{
    switch (missile_type) {
    case 1:
        return 1;
    case 2:
        return 5;
    case 3:
        return 10;
    case 4:
        return 11;
    case 5:
        return 25;
    case 6:
        return 79;
    case 7:
        return 47;
    case 8:
        return 93;
    case 9:
        return 48;
    case 10:
        return 80;
    case 11:
        return 55;
    case 12:
        return 36;
    case 13:
        return 81;
    case 14:
        return 57;
    case 15:
        return 76;
    case 30:
        return 87;
    case 31:
        return 31;
    case 32:
        return 122;
    case 34:
        return 121;
    default:
        return W8_SPELL_NONE;
    }
}

/* The 0x49 teleport lands on the character's saved anchor, so without an
   anchor set it cannot run. Every other spell id clears this block. */
bool IsTeleportCastMissingAnchor(W8Character* character, int spell_id)
{
    if (spell_id != W8_SPELL_RETURN_TO_PORTAL) {
        return false;
    }
    return !character->has_saved_location;
}
/* The 0x4f spell's finalizer. Once its target is gone, the impact spell 0x76
   is cast at the target's last position and the matching notice is posted:
   the monster's own notice for a monster that has died, or the targeted
   character's notice for a character whose row is empty. A text box is only
   opened when the detailed combat messages are off, matching the ordinary
   damage path. */
void FinishSpellEffect(W8SpellEffectEntry* effect)
{
    W8MonsterInfo* monster_info = 0;
    W8CombatSlot target;
    glm::vec3 position;

    if (effect->target.iType == W8_TARGET_KIND_MONSTER) {
        monster_info = FindMonster(effect->target.iMonsterID);
    }
    if ((effect->target.iType == W8_TARGET_KIND_MONSTER && monster_info->hp_current == 0) ||
        (effect->target.iType == W8_TARGET_KIND_CHARACTER &&
         CurrentGame().party.characters[effect->target.iChar].hp_current == 0)) {
        target.iType = W8_TARGET_KIND_PLACE;
        if (effect->target.iType == W8_TARGET_KIND_MONSTER) {
            NavigatorMovement* movement = &monster_info->p3D->nav.movement;
            position = movement->position;
            position.y += movement->height_offset;
            position.y = probeGround(position).position.y;
            PostMonsterNotice(monster_info, Data().strings[0x195]);
        } else {
            position = cameraLocation();
            position.y -= g_default_world_height;
            PostCharacterNotice(effect->target.iChar, Data().strings[0x195]);
        }
        if (g_settings.verbose_combat_messages == 0) {
            SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
        }
        target.point = position;
        CastSpellFromSource(W8_SPELL_BOILING_BLOOD_EXPLOSION, &effect->Source, &target,
                            effect->definition.duration_scale, effect->definition.percent, 0, false,
                            0, W8SpellCastKind::Standard, 0, 0);
    }
}

/* Scale a value by how far ahead of the difficulty's own pace one combatant
   is. Out of combat nothing is scaled; in combat a combatant slower than the
   pace is left alone too, and so is every value under an unknown difficulty. */
void ScaleByCombatPace(int party_slot, unsigned int* value)
{
    unsigned int pace;
    unsigned int phase_clock;

    if (!CurrentGame().combat.active()) {
        return;
    }

    switch (g_settings.difficulty) {
    case W8_DIFFICULTY_NOVICE:
        pace = 0x50;
        break;
    case W8_DIFFICULTY_NORMAL:
        pace = 0x3c;
        break;
    case W8_DIFFICULTY_EXPERT:
        pace = 0x28;
        break;
    default:
        Fail("FALSE");
        return;
    }

    phase_clock = CurrentGame().combat.state()->characters[party_slot].phase_clock_stamp;
    if (pace <= phase_clock) {
        *value = ((0x32 - pace) + phase_clock) * *value * 2 / 100;
    }
}

/* How likely a spell is to fail outright. The spell's own cost band picks a
   difficulty off a seventeen-entry table, scaled by the caller's factor and
   divided by seven; a caster already at or past that has no chance of failing
   at all, and the rest is the shortfall as a percentage capped at a hundred. */
unsigned int GetSpellFailureChance(unsigned int skill, int spell_id, int factor)
{
    int band = Data().spells[spell_id].spell_point_cost / 2 + Data().spells[spell_id].spell_level;
    unsigned int needed;
    unsigned int chance;

    if (band > 0x10) {
        band = 0x10;
    }
    needed =
        static_cast<unsigned int>(g_combat_effect_slot_spells_and_cast_success[6 + band] * factor) /
        7;
    if (needed <= skill) {
        return 0;
    }
    chance = (needed * 70 - skill * 70) / needed;
    if (static_cast<int>(chance) < 0) {
        return 0;
    }
    if (static_cast<int>(chance) > 100) {
        return 100;
    }
    return chance;
}

unsigned char g_profession_spellbooks[15] = {
    0, 2, 2, 4, 1, 4, 8, 0, 0, 0, 2, 4, 15, 8, 1,
};

/* Repeat the remembered choice. A power override belongs to this cast;
   it does not rewrite the remembered power. */
void StartCharacterSpellCast(int party_slot, int power_level)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
    auto choice = row->remembered.spell;
    if (power_level != 0) choice.detail_block.spell.power_level = power_level;
    if (CurrentGame().combat.active()) {
        SetCharacterCombatAction(party_slot, choice, 1);
    } else {
        AimAtTarget(party_slot, &choice.target, GetEditedActionContext(party_slot));
        ChooseAction(party_slot, choice.kind, choice.detail, &choice.detail_block, false, 1);
    }
    AimAtTarget(party_slot, &choice.target, GetEditedActionContext(party_slot));

    if (IsSpellTargetStillValid(party_slot, row->remembered.spell.detail,
                                SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_SPELL))) {
        StartBreathCycle(party_slot, false);
        return;
    }
    FallbackFromUnreachableAction(party_slot);
}

/* The same for using an item. The item's own spell decides whether the target
   still holds, and where the item came from is recorded before the check so a
   failed use can put it back. */
void StartCharacterItemUse(int party_slot)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
    W8CombatSlot saved_target = row->remembered.item_target;

    if (!CurrentGame().combat.active()) {
        AimAtTarget(party_slot, &row->remembered.item_target, GetEditedActionContext(party_slot));
    }
    ChooseAction(party_slot, W8_ACTION_USE_ITEM, -1, &row->remembered.item_detail, false, 1);
    AimAtTarget(party_slot, &saved_target, GetEditedActionContext(party_slot));

    if (IsSpellTargetStillValid(
            party_slot,
            GetItemSpell(CurrentGame().party.itemAt(row->remembered.item_detail.item_use.item)),
            SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_ITEM))) {
        StartBreathCycle(party_slot, false);
        return;
    }
    FallbackFromUnreachableAction(party_slot);
}

/* A character's whole casting strength in one spellbook: their current
   profession's caster level, plus every other profession they have ever held
   that shares the book. Only positive contributions count, and a caster level
   of nothing at all short-circuits unless the caller asks for the sum
   anyway. */
int GetTotalCasterLevel(const W8Character* character, int spellbook, bool include_all)
{
    int total = GetProfessionCasterLevel(character, W8_PROFESSION_NONE);
    int other;
    int level;

    if (total < 1 && !include_all) {
        return total;
    }

    for (other = 0; other < 15; ++other) {
        if (character->profession_levels[other] != 0 && other != character->iProfession &&
            (g_profession_spellbooks[other] & spellbook) != 0) {
            level = GetProfessionCasterLevel(character, static_cast<W8Profession>(other));
            if (level > 0) {
                total += level;
            }
        }
    }
    return total;
}

/* The trait that stops a character learning anything at all. */

/* Which spellbooks a spell belongs to, as the mask the profession table is
   tested against. A spell in no book at all answers nothing, which is what
   makes the test below a membership test rather than a comparison. */
static unsigned char SpellbookMaskForSpell(int spell_id)
{
    return static_cast<unsigned char>(
        (Data().spells[spell_id].wizardry_spell != 0) |
        (Data().spells[spell_id].psionics_spell != 0 ? W8_SPELLBOOK_PSIONICS : W8_SPELLBOOK_NONE) |
        (Data().spells[spell_id].divinity_spell != 0 ? W8_SPELLBOOK_DIVINITY : W8_SPELLBOOK_NONE) |
        (Data().spells[spell_id].alchemy_spell != 0 ? W8_SPELLBOOK_ALCHEMY : W8_SPELLBOOK_NONE));
}

/* Whether the character knows any spell whose realm's remaining points still
   cover that spell's cost. */
bool CharacterHasCastableSpell(W8Character* character)
{
    int spell_id;

    for (spell_id = W8_SPELL_NONE; spell_id < 0x72; ++spell_id) {
        if (CanCharacterCastSpell(character, spell_id)) {
            return true;
        }
    }
    return false;
}

/* Whether a character is far enough along to take one spell on. Their whole
   caster level - the current profession's plus every other one that shares the
   spell's book - fixes the highest spell level they could ever hold, and their
   realm skill and spellbook skill together fix a second, lower ceiling. The
   spell has to be at or under both, the character's profession has to be in
   one of the spell's books, and the blocking trait has to be down.

   The two ceilings are computed even when the first alone would settle it,
   which is what shows them as one rule rather than two guards. */
bool CanCharacterLearnSpell(W8Character* character, int spell_id)
{
    unsigned char book = SpellbookMaskForSpell(spell_id);
    int caster_level;
    int other;
    int other_level;
    unsigned int level_ceiling = 0;
    unsigned int skill_ceiling;
    unsigned int ceiling;
    unsigned int spell_level;
    W8Skill spellbook_skill;

    if ((g_profession_spellbooks[character->iProfession] & book) == W8_SPELLBOOK_NONE) {
        return false;
    }
    if (CharacterHasTrait(character, W8_TRAIT_CANNOT_LEARN)) {
        return false;
    }

    caster_level = GetProfessionCasterLevel(character, W8_PROFESSION_NONE);
    if (caster_level > 0) {
        for (other = 0; other < 15; ++other) {
            if (character->profession_levels[other] != 0 && other != character->iProfession &&
                (g_profession_spellbooks[other] & book) != W8_SPELLBOOK_NONE) {
                other_level = GetProfessionCasterLevel(character, static_cast<W8Profession>(other));
                if (other_level > 0) {
                    caster_level += other_level;
                }
            }
        }
    }

    /* The highest spell level that caster level reaches, searched down from
       the top rather than up, so a caster who reaches nothing keeps zero. */
    for (spell_level = 7;; --spell_level) {
        if (MinimumCasterLevelForSpellLevel(spell_level) <= caster_level) {
            level_ceiling = spell_level;
            break;
        }
        if (static_cast<int>(spell_level - 1) < 0) {
            break;
        }
    }

    spellbook_skill = GetBestSpellbookSkillForSpell(character, spell_id, false, false, 0);
    skill_ceiling =
        (character->skills[static_cast<int>(W8_SKILL_FIRE_MAGIC) + Data().spells[spell_id].realm]
                 .points /
             10 +
         character->skills[spellbook_skill].level) /
            15 +
        1;
    ceiling = skill_ceiling < level_ceiling ? skill_ceiling : level_ceiling;

    return ceiling >= static_cast<unsigned int>(Data().spells[spell_id].spell_level);
}

/* One message-table index per realm, for the realm's name. */
// offset alias of the tail of g_attr_table1; shared retail storage.
unsigned short g_realm_message_offsets[W8_SPELL_REALM_COUNT] = {
    0x30b, 0x30c, 0x30d, 0x30e, 0x30f, 0x310,
};

/* Take one spell on. The spell is marked known, its realm's known count goes
   up, the spell-point pools are recomputed, and - when the caller asks for it -
   the character says so in a line built from the character's name, the spell's
   name and the realm's remaining points.

   The line is assembled twice over: once only to measure the three pieces so
   the buffer can be allocated, and once into it. */
void LearnSpell(W8Character* character, int spell_id, bool announce)
{
    W8SpellRealm realm;
    std::string realm_name;

    character->spell_learned[spell_id] = 1;
    realm = Data().spells[spell_id].realm;
    ++character->skill_unlocks[static_cast<int>(W8_SKILL_FIRE_MAGIC) + realm];
    character->skill_unlocks[W8_SKILL_IRON_WILL] =
        RebuildRealmSpellPointCeilings(Data(), character);

    if (!announce) {
        return;
    }

    realm_name = Data().strings[g_realm_message_offsets[realm]];
    const std::string line =
        fmt::format("{}: \"{}\" - {}", fmt::sprintf(Data().strings[0x1b9], character->name),
                    Data().spells[spell_id].display_name,
                    fmt::sprintf(Data().strings[0x1ba], realm_name, character->sp_max[realm]));
    ShowNoticeLine(line, 0, true, false);
}

int g_learn_sound = kFirstRemappedEvent + 16;
/* Learn the spell a scroll or book teaches, and consume it. The item has to
   carry a spell and the character has to be able to take it on; failing that the item is left alone
   and the refusal is shown.

   Learning practises three things at once: the learning skill at twice the
   spell's level, the spell's own realm skill at its level, and every spellbook
   skill the spell belongs to at the same. */
void LearnSpellFromItem(W8Character* character, W8ItemInstance* item)
{
    unsigned int spell_id;
    int usage_points;
    unsigned int skill_id;

    if (Data().items[item->iItemNo].spell_id == W8_SPELL_NONE) {
        Fail("gpItemDB[pPCItem->iItemNo].ubSpellNumber != SPELL_NONE");
    }
    spell_id = Data().items[item->iItemNo].spell_id;

    if (!CanCharacterLearnSpell(character, spell_id)) {
        ShowNoticeLine(fmt::sprintf(Data().strings[0x1bb], character->name), 0, true, false);
        return;
    }

    LearnSpell(character, spell_id, true);
    usage_points = Data().spells[spell_id].spell_level;
    PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_ARTIFACTS, usage_points * 2, false);
    PracticeCharacterSkill(
        CurrentGame(), character,
        static_cast<W8Skill>(static_cast<int>(W8_SKILL_FIRE_MAGIC) + Data().spells[spell_id].realm),
        usage_points, false);
    for (skill_id = W8_SKILL_SPELLBOOK_WIZARDRY; skill_id < W8_SKILL_FIRE_MAGIC; ++skill_id) {
        if (Data().spells[spell_id].wizardry_spell != 0) {
            PracticeCharacterSkill(CurrentGame(), character, static_cast<W8Skill>(skill_id),
                                   usage_points, false);
        }
    }
    (void)TakeInventoryItem(CurrentGame(), CurrentGame().party.locateItem(item));
    QueueCharacterEvent(character, g_learn_sound, 0, kCharacterEventFullVolume);
}

/* Eight is not a power level but the request to cast at the highest one the
   caster can pay for; the walk below resolves it. */
enum { W8_SPELL_POWER_AS_AFFORDABLE = 8, W8_SPELL_POWER_MAX = 7 };

/* Whether the slot could go off with the spell it has recorded. The spell has
   to exist, to be one the character knows, to be usable now, and - out of
   combat - still to have a valid target; and the slot has to be able to pay
   for it at the power level it asked for.

   A spell asking for the affordable power level is priced at one here, so this
   answers whether the cast is possible at all rather than at the level the
   player picked. */
bool CanPartySlotCastRecordedSpell(int party_slot)
{
    const W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
    int spell_id = row->remembered.spell.detail;
    int power_level = row->remembered.spell.detail_block.spell.power_level;

    if (spell_id == W8_SPELL_NONE) {
        return false;
    }
    if (spell_id == W8_SPELL_SUMMON_ELEMENTAL &&
        GetConditionRecordFlag(party_slot, W8_DEPENDENCE_SUMMON)) {
        return false;
    }
    if (CurrentGame().party.characters[party_slot].spell_learned[spell_id] != 1) {
        return false;
    }
    if (row->remembered.spell.detail_block.spell.power_level == W8_SPELL_POWER_AS_AFFORDABLE &&
        Data().spells[spell_id].power_class == W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT &&
        CurrentGame().combat.active()) {
        return false;
    }

    if (power_level == W8_SPELL_POWER_AS_AFFORDABLE) {
        power_level = 1;
    }
    if (Data().spells[spell_id].spell_point_cost * power_level >
        CurrentGame().party.characters[party_slot].iSPLeft[Data().spells[spell_id].realm]) {
        return false;
    }
    if (!SpellUsableNow(spell_id, false)) {
        return false;
    }
    if (!CurrentGame().combat.active() &&
        !IsSpellTargetStillValid(party_slot, spell_id,
                                 SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_SPELL))) {
        return false;
    }
    return true;
}

/* The highest power level the slot can actually pay for, counting down from
   the one it asked for. Zero means the cast cannot happen at all - either
   because the spell fails the same checks as above, or because even one level
   is more than the pool holds. */
int GetAffordableSpellPowerLevel(int party_slot)
{
    const W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
    int spell_id = row->remembered.spell.detail;
    int power_level = row->remembered.spell.detail_block.spell.power_level;
    int cost;

    if (spell_id == W8_SPELL_NONE) {
        return 0;
    }
    if (spell_id == W8_SPELL_SUMMON_ELEMENTAL &&
        GetConditionRecordFlag(party_slot, W8_DEPENDENCE_SUMMON)) {
        return 0;
    }
    if (CurrentGame().party.characters[party_slot].spell_learned[spell_id] != 1) {
        return 0;
    }
    if (row->remembered.spell.detail_block.spell.power_level == W8_SPELL_POWER_AS_AFFORDABLE &&
        Data().spells[spell_id].power_class == W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT &&
        CurrentGame().combat.active()) {
        return 0;
    }
    if (!SpellUsableNow(spell_id, false)) {
        return 0;
    }
    if (!CurrentGame().combat.active() &&
        !IsSpellTargetStillValid(party_slot, spell_id,
                                 SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_SPELL))) {
        return 0;
    }

    if (power_level == W8_SPELL_POWER_AS_AFFORDABLE) {
        power_level = 1;
    }
    cost = Data().spells[spell_id].spell_point_cost * power_level;
    while (power_level != 0) {
        if (cost <=
            CurrentGame().party.characters[party_slot].iSPLeft[Data().spells[spell_id].realm]) {
            return power_level;
        }
        --power_level;
        cost -= Data().spells[spell_id].spell_point_cost;
    }
    return 0;
}

/* Whether the slot could go through with its recorded item use. Resolve the
   location and check the expected item id before use. A shared-pool item is refused
   in combat, where only what a character personally carries is reachable.

   A spell whose target type is not the self-only one may be aimed anywhere; the
   self-only one has to be aimed at the user. */
bool CanPartySlotUseRecordedItem(int party_slot)
{
    W8PartySlotRow* row = &CurrentGame().party.rows[party_slot];
    W8ItemInstance* item;
    int spell_id;
    bool normalize;

    const auto location = row->remembered.item_detail.item_use.item;
    if (location.storage == W8ItemStorage::None || location.storage == W8ItemStorage::Hand ||
        (location.storage == W8ItemStorage::Pool && CurrentGame().combat.active()))
        return false;
    item = CurrentGame().party.itemAt(location);
    if (item == 0 || item->iItemNo == -1 || item->iItemNo != row->remembered.item_id) {
        return false;
    }
    if (!CanUseItemForAction(party_slot, item)) {
        return false;
    }
    if (!CanCharacterActivateItem(&CurrentGame().party.characters[party_slot], item)) {
        return false;
    }

    spell_id = GetItemSpell(item);
    normalize = ItemClassNormalizesTarget(&Data().items[item->iItemNo]);
    if (GetSpellTargetType(spell_id, normalize) == W8_TARGET_TYPE_CASTER &&
        row->remembered.item_target.iChar != party_slot) {
        return false;
    }
    if (!SpellUsableNow(spell_id, false)) {
        return false;
    }
    if (!CurrentGame().combat.active() &&
        !IsSpellTargetStillValid(party_slot, spell_id,
                                 SelectCombatAction(party_slot, W8_TARGETING_CONTEXT_ITEM))) {
        return false;
    }
    return true;
}

/* Monster casting uses the same runtime-plus-database spell budget
   for power selection and the final failure roll. */
static unsigned int MonsterSpellPointBudget(W8MonsterInfo* monster_info,
                                            const W8MonsterRecord* record)
{
    unsigned int budget = monster_info->spell_points + record->sp_budget;
    if (record->sp_budget == 0) {
        DebugMessage(0, fmt::format("DATA ERROR: Monster {} casting spells with SP Budget of 0",
                                    GetMonsterName(monster_info, 0, 0)));
    }
    if (static_cast<int>(budget) < 0) budget = 0;
    return budget;
}

/* How hard a monster casts. It walks the power levels up from one until the
   cost of the next would leave it far enough short of its spell-point budget
   to matter - nine per cent of the cost - and then steps back to the last one
   it could comfortably pay for. A quarter of the time that answer is nudged
   one level down and a quarter one level up, so identical monsters do not all
   cast identically.

   The budget is the monster's database base plus its own runtime bonus; a base
   of zero is a data error the monster is named in. */
unsigned int ChooseMonsterSpellPowerLevel(W8MonsterInfo* monster_info, W8MonsterRecord*,
                                          int spell_id)
{
    unsigned int power_level;
    unsigned int budget;
    unsigned int cost;
    int band;

    if (spell_id == W8_SPELL_SPECIAL_ATTACK_CONE) {
        return 0;
    }

    power_level = 1;
    for (;;) {
        W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);

        budget = MonsterSpellPointBudget(monster_info, record);

        band = Data().spells[spell_id].spell_point_cost / 2 + Data().spells[spell_id].spell_level;
        if (band > 16) {
            band = 16;
        }
        cost = (g_combat_effect_slot_spells_and_cast_success[6 + band] * power_level) / 7;

        if (cost > budget) {
            unsigned int shortfall = (cost * 70 - budget * 70) / cost;
            if (static_cast<int>(shortfall) >= 0 &&
                (static_cast<int>(shortfall) >= 0x65 || shortfall >= 9)) {
                break;
            }
        }
        ++power_level;
        if (power_level >= 8) {
            break;
        }
    }
    if (power_level > 1) {
        --power_level;
    }

    switch (Random(4)) {
    case 0:
        if (power_level > 2) {
            --power_level;
        }
        break;
    case 1:
        if (power_level < W8_SPELL_POWER_MAX) {
            ++power_level;
        }
        break;
    }

    if (power_level == 0 || power_level > W8_SPELL_POWER_MAX) {
        Fail("( uiPowerLevel >= 1 ) && ( uiPowerLevel <= 7 )");
    }
    return power_level;
}

/* Where the cast lands, by the spell's own target type. The kind values are
   W8TargetKind's; the source-kind values are W8TargetSourceKind's. */

/* Cast one spell at the party from a point in the world - a trap, a rune, a
   scripted effect. The caller gives the point and the power level; who it hits
   comes from the spell's own target type, so this decides the target rather
   than taking one.

   A power level outside one through seven is silently taken as one, which is
   what makes a caller passing zero cast nothing at all rather than cast weakly. */
int PointCastSpell(glm::vec3 position, int spell_id, unsigned int power_level)
{
    W8TargetSource source;
    W8CombatSlot target;
    bool sight_probe;

    if (power_level == 0) {
        return 0;
    }
    if (power_level > W8_SPELL_POWER_MAX) {
        power_level = 1;
    }

    ResetTargetSource(&source);
    source.iType = W8_TARGET_SOURCE_INDIRECT;
    source.point = position;

    ResetCombatSlot(&target);
    switch (GetSpellTargetType(spell_id, false)) {
    case W8_TARGET_TYPE_CASTER:
    case W8_TARGET_TYPE_ALLY:
    case W8_TARGET_TYPE_ENEMY:
        target.iType = W8_TARGET_KIND_CHARACTER;
        target.iChar = GetRandomCharacter(0, 1, -1, -1);
        break;
    case W8_TARGET_TYPE_RADIUS:
    case W8_TARGET_TYPE_POINT:
        sight_probe = true;
        target.iType = W8_TARGET_KIND_PARTY;
        ResolveTargetPoint(&target, sight_probe);
        target.iType = W8_TARGET_KIND_PLACE;
        break;
    case W8_TARGET_TYPE_CONE:
        sight_probe = false;
        target.iType = W8_TARGET_KIND_PARTY;
        ResolveTargetPoint(&target, sight_probe);
        target.iType = W8_TARGET_KIND_PLACE;
        break;
    case W8_TARGET_TYPE_PARTY:
    case W8_TARGET_TYPE_ENEMY_GROUP:
        target.iType = W8_TARGET_KIND_PARTY;
        break;
    default:
        Fail("FALSE",
             fmt::format("PointCastSpell: ERROR - Invalid spell target for {:d}", spell_id));
        break;
    }

    CastSpellFromSource(spell_id, &source, &target, power_level, 0, 0, false, 0,
                        W8SpellCastKind::Standard, 0, 0);
    return 1;
}

unsigned int GetSpellCastingSkillLevel(const W8Character* character, W8Skill spellbook_skill,
                                       W8SpellRealm realm)
{
    return (character->skills[spellbook_skill].level +
            character->skills[static_cast<int>(W8_SKILL_FIRE_MAGIC) + realm].level * 4) /
           5;
}

W8Skill GetBestSpellbookSkillForSpell(W8Character* character, int spell_id, bool pricing,
                                      bool prefer_unlocked, unsigned int power_level)
{
    unsigned char book = SpellbookMaskForSpell(spell_id);
    unsigned char probe;
    unsigned int skill_id;
    W8Skill best_skill = W8_SKILL_NONE;
    unsigned int best_level = 0xffffffff;
    W8Skill unlocked_skill = W8_SKILL_NONE;
    unsigned int unlocked_level = 0xffffffff;
    unsigned int level;
    int party_slot;

    if (pricing && character->uiCondition[W8_CONDITION_SILENCED] != 0 &&
        Data().spells[spell_id].alchemy_spell != 0) {
        return W8_SKILL_SPELLBOOK_ALCHEMY;
    }

    probe = 1;
    for (skill_id = W8_SKILL_SPELLBOOK_WIZARDRY; skill_id < W8_SKILL_FIRE_MAGIC; ++skill_id) {
        if ((probe & book) != 0) {
            level = character->skills[skill_id].points;
            if (static_cast<int>(best_level) < static_cast<int>(level)) {
                best_skill = static_cast<W8Skill>(skill_id);
                best_level = level;
            }
            if (prefer_unlocked && character->skills[skill_id].active &&
                static_cast<int>(unlocked_level) < static_cast<int>(level)) {
                unlocked_skill = static_cast<W8Skill>(skill_id);
                unlocked_level = level;
            }
        }
        probe = static_cast<unsigned char>(probe << 1);
    }

    if (prefer_unlocked && unlocked_skill != W8_SKILL_NONE && best_skill != unlocked_skill) {
        if (pricing) {
            unsigned int failure;
            int shortfall;
            unsigned int skill_figure;

            party_slot = CharacterPointerToPartySlot(character);
            skill_figure =
                GetSpellCastingSkillLevel(character, unlocked_skill, Data().spells[spell_id].realm);
            failure = GetSpellFailureChance(skill_figure, spell_id, static_cast<int>(power_level));

            shortfall = GetMinimumCasterLevelForSpell(spell_id) -
                        GetTotalCasterLevel(character, book, true) - 1 + power_level;
            if (shortfall > 0) {
                failure += Data().spells[spell_id].spell_level * shortfall;
            }
            ScaleByCombatPace(party_slot, &failure);
            if (failure == 0) {
                best_skill = unlocked_skill;
            }
        } else {
            best_skill = unlocked_skill;
        }
    }

    if (best_skill == W8_SKILL_NONE) {
        Fail("(iHighestSkill != SKILL_NONE)",
             fmt::format("Failed on spell {:d}, usability byte {:d}", spell_id, book));
    }
    return best_skill;
}

/* One cast's failure chance used by the cast rating and power-level choosers: the
   chosen spellbook skill weighted four to one against the realm skill, priced
   against the spell's cost band, plus the spell's level for every caster level
   short of what it asks for, scaled by the caster's combat pace. */
static unsigned int GetCastFailureChance(W8Character* character, int spell_id,
                                         unsigned int power_level)
{
    const W8SpellRuntimeRecord* record = &Data().spells[spell_id];
    W8Skill skill = GetBestSpellbookSkillForSpell(character, spell_id, true, true, power_level);
    int party_slot = CharacterPointerToPartySlot(character);
    unsigned int skill_figure = GetSpellCastingSkillLevel(character, skill, record->realm);
    unsigned int chance;
    unsigned char book;
    int caster_level;
    int shortfall;

    chance = GetSpellFailureChance(skill_figure, spell_id, static_cast<int>(power_level));
    book = SpellbookMaskForSpell(spell_id);
    caster_level = GetTotalCasterLevel(character, book, true);
    shortfall = GetMinimumCasterLevelForSpell(spell_id) - caster_level - 1 + power_level;
    if (shortfall > 0) {
        chance += record->spell_level * shortfall;
    }
    ScaleByCombatPace(party_slot, &chance);
    return chance;
}

/* How safe one whole cast is, as the spell screen's one-to-five rating (five
   for a sure cast). Two things spoil it: a spellbook skill short of what the
   spell's cost band asks for at that power level, which the plain
   failure-chance body above answers, and a caster level short of what the
   spell asks for, charged flat at the spell's own level per level missing. The
   sum is scaled by how far ahead of the combat pace the caster is and banded
   at 0, 5, 15 and 40 percent.

   The skill this is measured against is the spell's own best spellbook skill
   weighted four to one against the realm skill, which is what makes the realm
   the larger part of it.

   Power level eight is the request to cast as high as affordable rather than a
   level, so it has no rating of its own. The power-level choosers carry the
   unbanded percentage from GetCastFailureChance rather than calling this. */
W8SpellCastRating GetSpellCastRating(W8Character* character, int spell_id, unsigned int power_level)
{
    if (power_level == W8_SPELL_POWER_AS_AFFORDABLE) {
        return W8_CAST_RATING_AUTOMATIC;
    }

    unsigned int chance = GetCastFailureChance(character, spell_id, power_level);
    if (chance == 0) {
        return W8_CAST_RATING_NO_FAILURE;
    }
    if (chance <= 5) {
        return W8_CAST_RATING_MINIMAL_RISK;
    }
    if (chance <= 15) {
        return W8_CAST_RATING_LOW_RISK;
    }
    if (chance <= 40) {
        return W8_CAST_RATING_MODERATE_RISK;
    }
    return W8_CAST_RATING_HIGH_RISK;
}

/* The average of one dice expression, taken as the midpoint of what it can
   roll: base plus the dice at one each, and base plus the dice at their
   faces. The die count is multiplied by the power level first, in a byte, so a
   high power level on a many-dice spell wraps rather than growing. */
static int AverageEffectAtPower(W8Dice dice, unsigned int power_level)
{
    unsigned char count =
        static_cast<unsigned char>(dice.count * static_cast<unsigned char>(power_level));

    return static_cast<int>(
        ((dice.base + count * dice.sides) + static_cast<float>(dice.base + count)) * 0.5f);
}

/* The failure chance past which casting harder is not worth it. */
enum { W8_SPELL_FAILURE_ACCEPTABLE = 10 };

/* The three spells whose power level is decided by how much of a pool the
   target is missing. The third takes hit points first and falls back to
   stamina only when they are already full, which is what separates it from the
   other two rather than making it a combination of them. */

/* How hard to cast a spell that has to last a given number of turns. Each
   power level is priced at what it would really cost - the spell points for
   one cast, times how many casts the failure chance implies, times the level -
   and the cheapest wins. A power level the caster cannot pay for at all ends
   the walk, so the answer is zero when even the first is out of reach.

   A condition that never runs out cannot be out-waited, so it is answered with
   the lowest power level rather than the cheapest. */
unsigned int ChoosePowerLevelForDuration(W8Character* character, int spell_id,
                                         unsigned int turns_needed)
{
    W8SpellRealm realm = Data().spells[spell_id].realm;
    unsigned int power_level;
    unsigned int best_cost = 0;
    unsigned int best_power = 0;
    unsigned int failure;
    unsigned int per_turn;
    unsigned int casts;
    unsigned int cost;

    if (turns_needed == W8_CONDITION_INDEFINITE) {
        return 1;
    }

    for (power_level = 1; power_level < 8; ++power_level) {
        if (character->iSPLeft[realm] <
            static_cast<int>(Data().spells[spell_id].spell_point_cost * power_level)) {
            return best_power;
        }

        failure = GetCastFailureChance(character, spell_id, power_level);
        // A guaranteed failure delivers no duration and cannot be priced.
        if (failure >= 100) continue;

        /* What one cast at this level really delivers: the square of the power
           level, less the share of it the failure chance takes away. */
        per_turn = power_level * power_level - (failure * power_level * power_level) / 100;
        casts = turns_needed / per_turn;
        if (turns_needed % per_turn != 0) {
            ++casts;
        }
        cost = Data().spells[spell_id].spell_point_cost * casts * power_level;

        if (best_power == 0 || cost < best_cost) {
            best_power = power_level;
            best_cost = cost;
        }
    }
    return best_power;
}

/* How hard to cast a spell that has to restore a given amount. The power level
   climbs until either the failure chance stops being worth it - in which case
   the previous level is taken - or the average roll at that level covers what
   is missing. Nothing missing takes the lowest level.

   Which pool is missing comes from the spell: one restores hit points, one
   stamina, and one takes hit points first and falls back to stamina when they
   are already full. */
unsigned int ChoosePowerLevelToRestore(W8Character* character, int spell_id,
                                       const W8Character* target)
{
    int missing;
    unsigned int power_level;
    unsigned int failure;

    if (target == 0) {
        return 1;
    }

    if (spell_id == W8_SPELL_HEAL_WOUNDS) {
        missing = target->uiHPMax - static_cast<int>(target->hp_current);
    } else if (spell_id == W8_SPELL_RESTORATION) {
        missing = target->uiHPMax - static_cast<int>(target->hp_current);
        if (missing == 0) {
            missing = target->uiStaminaMax - target->stamina;
        }
    } else if (spell_id == W8_SPELL_STAMINA) {
        missing = target->uiStaminaMax - target->stamina;
    } else {
        return 1;
    }

    if (missing < 1) {
        return 1;
    }

    for (power_level = 1; power_level < 8; ++power_level) {
        failure = GetCastFailureChance(character, spell_id, power_level);
        if (failure > W8_SPELL_FAILURE_ACCEPTABLE) {
            if (power_level > 1) {
                --power_level;
            }
            return power_level;
        }
        if (missing < AverageEffectAtPower(Data().spells[spell_id].effect_dice, power_level)) {
            return power_level;
        }
    }
    return power_level;
}

/* The worst of the listed conditions: the largest value, ties going to the later entry. */
static unsigned int worstCondition(const w8_unaligned_uint* conditions,
                                   std::initializer_list<int> ids)
{
    unsigned int worst = conditions[*ids.begin()];
    for (const int id : ids) {
        if (worst <= static_cast<unsigned int>(conditions[id])) {
            worst = conditions[id];
        }
    }
    return worst;
}

/* Pick automatic power from this cast's target. Zero means that no viable
   power was found; the caller must not turn that into a minimum-power cast. */
static unsigned int ChooseSpellPowerLevelForTarget(W8Character* caster, int spell_id,
                                                   const W8CombatSlot& target, int identify_context)
{
    const w8_unaligned_uint* conditions = 0;
    const W8Character* target_character = 0;
    unsigned int power_level;
    unsigned int worst;

    if (target.iType == W8_TARGET_KIND_CHARACTER) {
        target_character = &CurrentGame().party.characters[target.iChar];
        conditions = target_character->uiCondition;
    } else if (target.iType == W8_TARGET_KIND_MONSTER) {
        if (const W8MonsterInfo* monster_info = FindMonster(target.iMonsterID)) {
            conditions = monster_info->uiCondition;
        }
    }

    switch (GetSpellTargetType(spell_id, false)) {
    case W8_TARGET_TYPE_ALLY:
        /* Every ally spell reads the target's conditions or character; a target
           without them (no character, no live monster) gets the minimum. */
        if (conditions == nullptr) {
            return 1;
        }
        switch (spell_id) {
        case W8_SPELL_HEAL_WOUNDS:
        case W8_SPELL_STAMINA:
        case W8_SPELL_RESTORATION:
            power_level = ChoosePowerLevelToRestore(caster, spell_id, target_character);
            break;
        case W8_SPELL_CURE_LESSER_COND:
            worst = worstCondition(conditions, {W8_CONDITION_NAUSEATED, W8_CONDITION_AFRAID,
                                                W8_CONDITION_ASLEEP, W8_CONDITION_BLIND});
            power_level = ChoosePowerLevelForDuration(caster, spell_id, worst);
            break;
        case W8_SPELL_CURE_PARALYSIS:
            power_level =
                ChoosePowerLevelForDuration(caster, spell_id, conditions[W8_CONDITION_PARALYZED]);
            break;
        case W8_SPELL_CURE_POISON:
            power_level =
                ChoosePowerLevelForDuration(caster, spell_id, conditions[W8_CONDITION_POISONED]);
            break;
        case W8_SPELL_CURE_DISEASE:
            power_level =
                ChoosePowerLevelForDuration(caster, spell_id, conditions[W8_CONDITION_DISEASED]);
            break;
        case W8_SPELL_SANE_MIND:
            worst = worstCondition(
                conditions, {W8_CONDITION_INSANE, W8_CONDITION_TURNCOAT, W8_CONDITION_ASLEEP});
            power_level = ChoosePowerLevelForDuration(caster, spell_id, worst);
            break;
        case W8_SPELL_REMOVE_CURSE:
            power_level =
                ChoosePowerLevelForDuration(caster, spell_id, conditions[W8_CONDITION_HEXED]);
            /* The one case where the target being a character says something
               the condition does not: the item they are carrying asks for more
               than the condition does. */
            if (target.iType == W8_TARGET_KIND_CHARACTER &&
                power_level <= GetEquipmentBindingDifficulty(target.iChar)) {
                power_level = GetEquipmentBindingDifficulty(target.iChar);
            }
            break;
        default:
            return 1;
        }
        break;

    case W8_TARGET_TYPE_PARTY:
        if (spell_id != W8_SPELL_REST_ALL && spell_id != W8_SPELL_HEAL_ALL) {
            return 1;
        }
        power_level = ChoosePowerLevelToRestore(caster, spell_id, 0);
        break;

    case W8_TARGET_TYPE_ITEM:
        if (spell_id != W8_SPELL_IDENTIFY_ITEM) {
            return 1;
        }
        power_level =
            CountIdentifyAttemptsNeeded(CurrentGame().party.itemAt(target.item), identify_context);
        break;

    default:
        return 1;
    }

    return power_level;
}

/* Message-table byte offsets: the "at %s" wrapper every named target goes
   through, and the fixed words. */
enum {
    W8_MESSAGE_TARGET_AT = 0x610,
    W8_MESSAGE_TARGET_PARTY = 0x614,
    W8_MESSAGE_TARGET_PLACE = 0x618,
    W8_MESSAGE_TARGET_DIRECTION = 0x61c,
    W8_MESSAGE_TARGET_ITEM = 0x620,
    W8_MESSAGE_TARGET_UNKNOWN = 0x624
};
/* The name-prefix table, holding message-table indexes. A character indexes
   it by sex and a monster by its name group. */
// offset alias of g_gender_name_message_rows; shared retail storage.
unsigned short g_name_prefix_messages[15] = {
    0x2da, 0x2d2, 0x2d5, 0x2d8, 0x2db, 0x2d3, 0x2d6, 0x2d9,
    0x2dc, 0x2dd, 0x2de, 0x2df, 0x2e0, 0x2e1, 0,
};
/* Say in words what a spell is aimed at. A character or a monster whose name
   the party does not have is described by its name prefix instead. Everything
   else is a fixed word. */
static std::string SpellTargetString(const W8TargetSource* source, const W8CombatSlot* target)
{
    unsigned short name_prefix;

    if (target == 0) {
        Fail("pTarget != NULL");
    }

    switch (target->iType) {
    case 0:
    case 6:
        return "";

    case 1:
    case 9:
        if (target->iChar == -1) {
            Fail("pTarget->iChar != BAD_INDEX");
        }
        if (!TargetSourceIsCharacter(source, 0) || source->name_known ||
            source->iChar != target->iChar) {
            return fmt::sprintf(Data().strings[W8_MESSAGE_TARGET_AT / 4],
                                CurrentGame().party.characters[target->iChar].name);
        }
        name_prefix =
            g_name_prefix_messages[CurrentGame().party.characters[target->iChar].gender * 4];
        break;

    case 2:
        return Data().strings[W8_MESSAGE_TARGET_PARTY / 4];

    case 3: {
        W8MonsterInfo* monster_info;
        W8MonsterRecord* record;

        if (target->iMonsterID == -1) {
            Fail("pTarget->iMonsterID != BAD_INDEX");
        }
        monster_info = FindMonster(target->iMonsterID);
        record = GetMonsterDataForInfo(monster_info);
        if (!TargetSourceIsMonster(source, 0) || source->iMonsterID != target->iMonsterID) {
            return fmt::sprintf(Data().strings[W8_MESSAGE_TARGET_AT / 4],
                                GetMonsterName(monster_info, record, 0));
        }
        name_prefix = g_name_prefix_messages[record->name_group * 4];
        break;
    }

    case 4:
        return fmt::sprintf(Data().strings[W8_MESSAGE_TARGET_AT / 4],
                            GetMonsterGroupName(FindMonsterGroup(target->iGroupID)));

    case W8_TARGET_KIND_ALL_ENEMIES:
        return Data().strings[W8_MESSAGE_TARGET_PLACE / 4];

    case 7:
        return fmt::sprintf(Data().strings[W8_MESSAGE_TARGET_ITEM / 4],
                            FormatItemDisplayName(CurrentGame().party.itemAt(target->item), false));

    case W8_TARGET_KIND_EIGHT:
        return Data().strings[W8_MESSAGE_TARGET_DIRECTION / 4];

    default:
        Fail("FALSE", fmt::format("SpellTargetString: ERROR - Invalid target type {:d}",
                                  fmt::underlying(target->iType)));
        return Data().strings[W8_MESSAGE_TARGET_UNKNOWN / 4];
    }

    return fmt::sprintf(Data().strings[W8_MESSAGE_TARGET_AT / 4], Data().strings[name_prefix]);
}

/* The two log lines a monster's cast is announced with: one that names the
   power level and one that does not. */
enum { W8_MESSAGE_MONSTER_CAST_VERBOSE = 0x638, W8_MESSAGE_MONSTER_CAST = 0x63c };

/* A monster casts. The line it is announced with names the caster, the spell
   and what it is aimed at; the quiet form leaves the power level out and puts
   the text box into its message mode, and the verbose form keeps the power
   level and does not.

   Its failure chance is the same rule the party's is, with the monster's
   spell-point budget standing in for a caster's spellbook skill, which is what
   makes the two one rule rather than a monster-specific one. */
unsigned int MonsterCastsSpell(W8MonsterInfo* monster_info, int spell_id, unsigned int power_level)
{
    W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    W8TargetSource source;
    unsigned int budget;
    unsigned int failure;
    int result;

    SetTargetSourceToMonster(monster_info, &source);

    if (g_settings.verbose_combat_messages == 0) {
        ShowNotice(W8_FONT_PALETTE_RUST,
                   fmt::sprintf(Data().strings[W8_MESSAGE_MONSTER_CAST / 4],
                                GetMonsterName(monster_info, record, 0),
                                Data().spells[spell_id].display_name,
                                SpellTargetString(&source, &monster_info->Target)));
        SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, 9);
    } else {
        ShowNotice(W8_FONT_PALETTE_RUST,
                   fmt::sprintf(Data().strings[W8_MESSAGE_MONSTER_CAST_VERBOSE / 4],
                                GetMonsterName(monster_info, record, 0),
                                Data().spells[spell_id].display_name, power_level,
                                SpellTargetString(&source, &monster_info->Target)));
    }

    record = GetMonsterDataForInfo(monster_info);
    budget = MonsterSpellPointBudget(monster_info, record);
    failure = GetSpellFailureChance(budget, spell_id, static_cast<int>(power_level));

    CastSpellFromSource(spell_id, &source, &monster_info->Target, power_level, 0, failure, false,
                        &result, W8SpellCastKind::Standard, 0, 0);
    return SpellCastFatigueCost(spell_id, result);
}

void W8SpellEffectEntry::AddVisual(SpellVisual* visual)
{
    if (visual != 0) {
        visual->auto_release = false;
        spell_visuals.push_back(visual);
    }
}

/* Put the lure's two effects a thousand units below the target. The target
   point itself moves down, so subsequent users see the lowered position.
   Only the named companion effect has its mode set. */
void SpawnLureEffects(W8SpellEffectEntry* owner, int argument, W8CombatSlot* target)
{
    glm::vec3 position;
    SpellVisual* effect;

    target->point.y -= 1000.0f;
    position = target->point;

    effect = SpawnSpellEffect(&position, Data().spells[W8_SPELL_HYPNOTIC_LURE].resource_name,
                              argument, 0, 0);
    owner->AddVisual(effect);

    position = target->point;
    effect = SpawnSpellEffect(&position, "hyp_lure2", argument, 0, 0);
    if (effect != 0) {
        effect->auto_release = false;
        effect->visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
        owner->spell_visuals.push_back(effect);
    }
}

/* Queued report records use the exhausted-condition notice. */

/* Post what an effect accumulated. The opening separator only appears once
   the text box already has something in it; the total is reported as
   "<count> <unit>" or, for a single hit, "<amount> points", and each nonzero
   condition appends its own affected-target count and condition name. Records still queued on
   the result are then drained: a character-slot record posts the named
   character's notice, a text record formats its own "%s %s" line, and the box
   is reset around each. With nothing reported at all the effect reports that
   its target took no damage. */
void ReportSpellResult(W8SpellEffectEntry* effect)
{

    if (GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
        AppendToLastTextLine(!effect->reported ? " -- " : ", ", -1);
        SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
    }
    if (effect->result.amount != 0) {
        if (effect->result.count == 1) {
            AppendToLastTextLine(fmt::sprintf(Data().strings[0x19a], effect->result.amount), -1);
        } else {
            AppendToLastTextLine(fmt::sprintf(Data().strings[0x199], effect->result.count,
                                              effect->result.amount / effect->result.count, -1),
                                 -1);
            SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
        }
        SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
        effect->reported = true;
    }
    for (int condition = W8_CONDITION_DRAINED; condition < W8_CONDITION_COUNT; ++condition) {
        if (effect->result.condition_counts[condition] != 0) {
            if (effect->reported && GetTextBoxMode() != W8_TEXT_BOX_MODE_NOTICE) {
                AppendToLastTextLine(", ", -1);
                SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
            }
            if (effect->result.condition_counts[condition] == 1) {
                if (effect->target.iType == W8_TARGET_KIND_CHARACTER) {
                    AppendToLastTextLine(
                        fmt::format("{} {}",
                                    CurrentGame().party.characters[effect->target.iChar].name,
                                    Data().strings[g_condition_notices[condition].singular]),
                        -1);
                } else if (effect->target.iType == W8_TARGET_KIND_MONSTER) {
                    W8MonsterInfo* monster_info = FindMonster(effect->target.iMonsterID);
                    if (monster_info != 0) {
                        AppendToLastTextLine(
                            fmt::format("{} {}", GetMonsterName(monster_info, 0, 0),
                                        Data().strings[g_condition_notices[condition].singular]),
                            -1);
                    }
                }
            } else {
                AppendToLastTextLine(
                    fmt::format("{:d} {}", effect->result.condition_counts[condition],
                                Data().strings[g_condition_notices[condition].plural]),
                    -1);
            }
            SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
            effect->reported = true;
        }
    }

    while (!effect->result.reports.empty()) {
        auto report = std::move(effect->result.reports.front());
        effect->result.reports.erase(effect->result.reports.begin());
        if (report != 0) {
            if (report->kind == W8_SPELL_DAMAGE_REPORT_CHARACTER_DEATH) {
                SetTextBoxMode(W8_TEXT_BOX_MODE_NOTICE, -1);
                PostCharacterNotice(
                    report->value,
                    fmt::format("{}!",
                                Data().strings[g_condition_notices[W8_CONDITION_DEAD].singular]));
                effect->reported = true;
            } else if (report->kind == W8_SPELL_DAMAGE_REPORT_MONSTER_DEATH) {
                SetTextBoxMode(W8_TEXT_BOX_MODE_NOTICE, -1);
                ShowNotice(
                    W8_FONT_PALETTE_RUST,
                    fmt::format("{} {}!", report->text,
                                Data().strings[g_condition_notices[W8_CONDITION_DEAD].singular]));
                effect->reported = true;
            }
        }
    }
    if (!effect->reported) {
        AppendToLastTextLine(Data().strings[0x1a5], -1);
    }
}

bool ValidateSpellTarget(int party_slot, int spell_id, unsigned int power, bool item_cast,
                         bool skip_world_cursor)
{
    std::vector<int> monsters;
    std::vector<int> party;
    W8TargetSource source;
    SetTargetSourceToCharacter(party_slot, &source);
    PopulateSpellTargetMarkers(spell_id, power, &source,
                               &CurrentGame().party.rows[party_slot].executing.target, monsters,
                               party, W8AreaSightRule::RequireLineOfSight);

    bool valid = true;
    bool has_targets = spell_id == W8_SPELL_SONIC_BOOM || !monsters.empty() || !party.empty();
    if (!has_targets) {
        W8SpellTargetType target_type = GetSpellTargetType(spell_id, false);
        has_targets =
            target_type > W8_TARGET_TYPE_ALL_ENEMIES && target_type < W8_TARGET_TYPE_COUNT;
    }
    if (!has_targets) {
        if (!item_cast) {
            PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x1b7],
                                                         Data().spells[spell_id].display_name));
        } else {
            PostCharacterNotice(party_slot, Data().strings[0x1b8]);
        }
        valid = false;
    } else if (!skip_world_cursor && DispatchWorldCursorNodeCommand(0, 4, 1)) {
        valid = false;
    }

    if (spell_id == W8_SPELL_SET_PORTAL &&
        ((LevelMotion()->flags & W8_LEVEL_FLAG_PROP_CONTACT) != 0 || !HasLevelWalkableContact() ||
         LevelMovedThisUpdate())) {
        valid = false;
    }
    if (!valid && (!CurrentGame().combat.active() || !MonsterCanAimSpell(spell_id) ||
                   CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0 ||
                   !CurrentGame().combat.state()->enemies_engaged)) {
        QueueCharacterEvent(&CurrentGame().party.characters[party_slot], 0x35, 0,
                            kCharacterEventFullVolume);
    }
    return valid;
}

/* The spells whose casts share one three-minute cooldown slot each; the
   shared slot lives in the status block's clock array. */
int g_cooldown_gated_spells[14] = {30, 38, 75, 73, 32, 33, 17, 20, 8, 40, 26, 45, 64, 58};
static std::array<std::uint64_t, 14> g_spell_cooldown_clocks{};

void ResetSpellCooldowns()
{
    g_spell_cooldown_clocks = {};
}

/* A failed check still restarts the slot. */
static bool CheckAndRestartSpellCooldown(int spell_id)
{
    bool affected = true;
    for (unsigned int index = 0; index < 14; ++index) {
        if (g_cooldown_gated_spells[index] == spell_id) {
            if (TimeUntilDeadline(g_spell_cooldown_clocks[index]) != 0) {
                affected = false;
            }
            g_spell_cooldown_clocks[index] = w8_get_ticks() + (180000);
            break;
        }
    }
    return affected;
}

/* Whether the spell's current target would actually be affected by it - the
   per-spell rules the cast path checks before it spends the points. */
bool SpellAffectedTarget(W8Character* character, int spell_id, W8CombatSlot* aim,
                         unsigned int power)
{
    unsigned int duration;
    unsigned int index;
    const w8_unaligned_uint* conditions;
    const W8Enchantment* enchantments;
    W8MonsterInfo* monster_info;
    const W8Character* target;
    bool affected;

    duration =
        Data().spells[spell_id].duration * power + Data().spells[spell_id].duration_per_level;
    affected = true;
    if (duration != W8_CONDITION_INDEFINITE) {
        duration += 1;
    }
    switch (spell_id) {
    case W8_SPELL_ENCHANTED_BLADE:
    case W8_SPELL_MISSILE_SHIELD:
    case W8_SPELL_ARMORPLATE:
    case W8_SPELL_MAGIC_SCREEN:
        if (!CurrentGame().combat.active()) {
            affected = CheckAndRestartSpellCooldown(spell_id);
        } else {
            for (index = 0; index < 12; ++index) {
                if (g_being_effect_slot_spells[index] == spell_id) {
                    affected = CurrentGame().status.effect_slots[index].duration /
                                   static_cast<float>(duration) <=
                               g_navigator_vertical_phase_step;
                    break;
                }
            }
        }
        break;
    case W8_SPELL_BLESS:
    case W8_SPELL_ELEMENT_SHIELD:
    case W8_SPELL_SOUL_SHIELD:
    case W8_SPELL_RING_OF_FIRE:
        if (CurrentGame().combat.active() &&
            CurrentLevelPopulation().monster_roster.hostileCombatCount() > 0) {
            for (index = 0; index < 9; ++index) {
                if (g_combat_effect_slot_spells_and_cast_success[index] == spell_id) {
                    affected = CurrentGame().combat.state()->effect_slots0[index].duration /
                                   static_cast<float>(duration) <=
                               g_navigator_vertical_phase_step;
                    break;
                }
            }
            break;
        }
        affected = false;
        break;
    case W8_SPELL_DRACON_BREATH:
    case W8_SPELL_GUARDIAN_ANGEL:
    case W8_SPELL_RAZOR_CLOAK:
    case W8_SPELL_EYE_FOR_AN_EYE:
    case W8_SPELL_SUPERMAN:
    case W8_SPELL_BODY_OF_STONE:
        if (CurrentGame().combat.active() &&
            CurrentLevelPopulation().monster_roster.hostileCombatCount() > 0) {
            if (aim->iType == W8_TARGET_KIND_CHARACTER) {
                enchantments = CurrentGame().party.characters[aim->iChar].enchantments;
            } else {
                monster_info = FindMonster(aim->iMonsterID);
                enchantments = monster_info->enchantments;
            }
            W8EnchantmentSlot enchantment = GetConditionDisplaySlot(spell_id);
            affected = enchantments[enchantment].turns / static_cast<float>(duration) <=
                       g_navigator_vertical_phase_step;
            break;
        }
        affected = false;
        break;
    case W8_SPELL_HASTE:
        if (CurrentGame().combat.active() &&
            CurrentLevelPopulation().monster_roster.hostileCombatCount() > 0) {
            if (aim->iType != W8_TARGET_KIND_PARTY) {
                return true;
            }
            affected = false;
            for (index = 0; index < 8; ++index) {
                if (CurrentGame().party.rows[index].fOccupied &&
                    CurrentGame()
                                .party.characters[index]
                                .enchantments[W8_ENCHANTMENT_RAZOR_CLOAK]
                                .turns /
                            static_cast<float>(duration) <=
                        g_navigator_vertical_phase_step) {
                    affected = true;
                }
            }
            return affected;
        }
        affected = false;
        break;
    case W8_SPELL_HEAL_WOUNDS:
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            if (CurrentGame().party.characters[aim->iChar].hp_current ==
                static_cast<unsigned int>(CurrentGame().party.characters[aim->iChar].uiHPMax)) {
                return false;
            }
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            if (monster_info->hp_current == static_cast<unsigned int>(monster_info->uiHPMax)) {
                return false;
            }
        }
        break;
    case W8_SPELL_HEAL_ALL:
        for (index = 0; index < 8; ++index) {
            target = &CurrentGame().party.characters[index];
            if (CurrentGame().party.rows[index].fOccupied &&
                target->highest_condition <= W8_CONDITION_DEAD &&
                target->hp_current < static_cast<unsigned int>(target->uiHPMax)) {
                return true;
            }
        }
        return false;
    case W8_SPELL_STAMINA:
        if (CurrentGame().combat.active()) {
            if (aim->iType != W8_TARGET_KIND_CHARACTER) {
                monster_info = FindMonster(aim->iMonsterID);
                return monster_info->stamina != monster_info->stamina_max;
            }
            return CurrentGame().party.characters[aim->iChar].stamina !=
                   CurrentGame().party.characters[aim->iChar].uiStaminaMax;
        }
        affected = false;
        break;
    case W8_SPELL_REST_ALL:
        affected = false;
        if (CurrentGame().combat.active()) {
            for (index = 0; index < 8; ++index) {
                target = &CurrentGame().party.characters[index];
                if (CurrentGame().party.rows[index].fOccupied &&
                    target->highest_condition <= W8_CONDITION_DEAD &&
                    static_cast<unsigned int>(target->stamina) <
                        static_cast<unsigned int>(target->uiStaminaMax)) {
                    return true;
                }
            }
            return false;
        }
        break;
    case W8_SPELL_RESTORATION:
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            target = &CurrentGame().party.characters[aim->iChar];
            for (index = 1; index < 0x12; ++index) {
                if (target->uiCondition[index] != 0) {
                    return true;
                }
            }
            return static_cast<unsigned int>(target->stamina) <
                       static_cast<unsigned int>(target->uiStaminaMax) ||
                   target->hp_current < static_cast<unsigned int>(target->uiHPMax);
        }
        monster_info = FindMonster(aim->iMonsterID);
        for (index = 1; index < 0x12; ++index) {
            if (monster_info->uiCondition[index] != 0) {
                return true;
            }
        }
        return static_cast<unsigned int>(monster_info->stamina) <
                   static_cast<unsigned int>(monster_info->stamina_max) ||
               monster_info->hp_current < static_cast<unsigned int>(monster_info->uiHPMax);
    case W8_SPELL_CURE_PARALYSIS:
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            conditions = CurrentGame().party.characters[aim->iChar].uiCondition;
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            conditions = monster_info->uiCondition;
        }
        if (conditions[W8_CONDITION_PARALYZED] == 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_POISON:
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            conditions = CurrentGame().party.characters[aim->iChar].uiCondition;
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            conditions = monster_info->uiCondition;
        }
        if (conditions[W8_CONDITION_POISONED] == 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_DISEASE:
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            conditions = CurrentGame().party.characters[aim->iChar].uiCondition;
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            conditions = monster_info->uiCondition;
        }
        if (conditions[W8_CONDITION_DISEASED] == 0) {
            return false;
        }
        break;
    case W8_SPELL_SANE_MIND:
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            conditions = CurrentGame().party.characters[aim->iChar].uiCondition;
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            conditions = monster_info->uiCondition;
        }
        if (conditions[W8_CONDITION_INSANE] == 0 && conditions[W8_CONDITION_TURNCOAT] == 0) {
            return false;
        }
        break;
    case W8_SPELL_RENEWAL:
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            conditions = CurrentGame().party.characters[aim->iChar].uiCondition;
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            conditions = monster_info->uiCondition;
        }
        if (conditions[W8_CONDITION_DRAINED] == 0) {
            return false;
        }
        break;
    case W8_SPELL_RESURRECTION:
        if (aim->iType == W8_TARGET_KIND_CHARACTER_INDIRECT) {
            conditions = CurrentGame().party.characters[aim->iChar].uiCondition;
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            conditions = monster_info->uiCondition;
        }
        if (conditions[W8_CONDITION_DEAD] == 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_LESSER_COND:
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            conditions = CurrentGame().party.characters[aim->iChar].uiCondition;
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            conditions = monster_info->uiCondition;
        }
        if (conditions[W8_CONDITION_IRRITATED] == 0 && conditions[W8_CONDITION_NAUSEATED] == 0 &&
            conditions[W8_CONDITION_AFRAID] == 0 && conditions[W8_CONDITION_ASLEEP] == 0 &&
            conditions[W8_CONDITION_BLIND] == 0) {
            return false;
        }
        break;
    case W8_SPELL_PURIFY_AIR:
        affected = false;
        if (CurrentGame().combat.state() != 0) {
            for (index = 0; index < 9; ++index) {
                if (CurrentGame().combat.state()->effect_slots[index].active) {
                    return true;
                }
            }
            return false;
        }
        break;
    case W8_SPELL_SONIC_BOOM:
    case W8_SPELL_HYPNOTIC_LURE:
        if (CurrentGame().combat.state() != 0 &&
            CurrentLevelPopulation().monster_roster.hostileCombatCount() > 0) {
            return true;
        }
        [[fallthrough]];
    case W8_SPELL_LIGHT:
    case W8_SPELL_DETECT_SECRETS:
    case W8_SPELL_CHAMELEON:
    case W8_SPELL_SHADOW_HOUND:
    case W8_SPELL_X_RAY:
    case W8_SPELL_RETURN_TO_PORTAL:
    case W8_SPELL_SET_PORTAL:
        affected = CheckAndRestartSpellCooldown(spell_id);
        break;
    case W8_SPELL_IDENTIFY_ITEM:
        if (CurrentGame().party.itemAt(aim->item)->identified) {
            return false;
        }
        break;
    case W8_SPELL_REMOVE_CURSE:
        if (!CurrentGame().combat.active()) {
            affected = CheckAndRestartSpellCooldown(spell_id);
            if (!affected) {
                return false;
            }
        }
        if (aim->iType == W8_TARGET_KIND_CHARACTER) {
            conditions = CurrentGame().party.characters[aim->iChar].uiCondition;
        } else {
            monster_info = FindMonster(aim->iMonsterID);
            conditions = monster_info->uiCondition;
        }
        if (conditions[W8_CONDITION_HEXED] == 0 &&
            (aim->iType != W8_TARGET_KIND_CHARACTER ||
             GetEquipmentBindingDifficulty(aim->iChar) == 0)) {
            affected = false;
        }
        break;
    }
    return affected;
}

/* Charge the realm spell points one cast costs and run the cast itself:
   notices, the success-chance figure, the point spend and the skill practice
   all happen here. Answers the cast step's status; out_points gets the
   fatigue cost. */
int ExecuteCharacterSpellCast(int party_slot, int spell_id, unsigned int power_level,
                              int* out_points, bool continue_cast)
{
    W8Character* character;
    const W8SpellRuntimeRecord* record;
    W8TargetSource source;
    W8CombatSlot* aim;
    unsigned int sp_cost;
    int sp_needed;
    unsigned int chance;
    W8Skill best_skill;
    unsigned int skill_score;
    W8Skill realm_skill;
    int slot;
    W8SpellRealm realm;
    int level_index;
    int caster_level;
    int minimum_level;
    int power_cast_bonus;
    int spellbook;
    int profession;
    int index;
    unsigned int threshold;
    unsigned int morale;
    bool affected;
    int result;
    int cast_result;
    bool recast;
    bool clamp_power;

    character = &CurrentGame().party.characters[party_slot];
    record = &Data().spells[spell_id];
    realm = record->realm;
    sp_cost = record->spell_point_cost;
    *out_points = 0;
    switch (record->power_class) {
    case W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT:
        if (!CurrentGame().combat.active() && power_level == 8) {
            recast = true;
            clamp_power = false;
            break;
        }
        [[fallthrough]];
    case W8_SPELL_POWER_SELECTABLE:
        recast = false;
        clamp_power = true;
        break;
    case W8_SPELL_POWER_MAXIMUM:
        recast = false;
        clamp_power = power_level != 8;
        break;
    case W8_SPELL_POWER_FIXED:
        power_level = 1;
        recast = false;
        clamp_power = false;
        break;
    case W8_SPELL_POWER_CLASS_NONE:
        return 0;
    default:
        Fail("Unsupported spell power class");
    }
    if (clamp_power) {
        sp_needed = sp_cost * power_level;
        if (sp_needed > character->iSPLeft[realm]) {
            power_level = character->iSPLeft[realm] / sp_cost;
        }
    } else if (character->iSPLeft[realm] < static_cast<int>(sp_cost)) {
        PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x18a], record->display_name));
        return 0;
    }
    if (power_level == 0) {
        PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x18a], record->display_name));
        return 0;
    }
    if (!ValidateSpellTarget(party_slot, spell_id, power_level, false, false)) {
        return 0;
    }
    if (character->uiCondition[W8_CONDITION_SILENCED] != 0 &&
        (record->alchemy_spell == 0 || character->skills[W8_SKILL_SPELLBOOK_ALCHEMY].level == 0)) {
        return 0;
    }
    SetTargetSourceToCharacter(party_slot, &source);
    if (!character->skills[W8_SKILL_POWER_CAST].active) {
        power_cast_bonus = 0;
    } else {
        power_cast_bonus = (character->skills[W8_SKILL_POWER_CAST].level >> 2) + 1;
    }
    aim = &CurrentGame().party.rows[party_slot].executing.target;
    if (power_level == 8) {
        power_level = ChooseSpellPowerLevelForTarget(character, spell_id, *aim, power_cast_bonus);
        if (power_level == 0) {
            return 0;
        }
        sp_needed = sp_cost * power_level;
        if (sp_needed > character->iSPLeft[realm]) {
            power_level = character->iSPLeft[realm] / sp_cost;
        }
    }
    if (g_settings.verbose_combat_messages == 0) {
        if (!continue_cast) {
            PostCharacterNotice(party_slot,
                                fmt::sprintf(Data().strings[0x18c], record->display_name,
                                             SpellTargetString(&source, aim)));
            SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, 8);
        } else {
            PostCharacterNotice(party_slot, Data().strings[0x18d]);
            SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, 8);
        }
    } else {
        PostCharacterNotice(party_slot, fmt::sprintf(Data().strings[0x18b], record->display_name,
                                                     power_level, SpellTargetString(&source, aim)));
    }
    best_skill = GetBestSpellbookSkillForSpell(character, spell_id, true, true, power_level);
    realm_skill = static_cast<W8Skill>(realm + 0x1c);
    slot = CharacterPointerToPartySlot(character);
    level_index = record->spell_point_cost / 2 + record->spell_level;
    skill_score = GetSpellCastingSkillLevel(character, best_skill, realm);
    if (0x10 < level_index) {
        level_index = 0x10;
    }
    threshold = (g_combat_effect_slot_spells_and_cast_success[level_index + 6] * power_level) / 7;
    if (skill_score < threshold) {
        chance = ((threshold - skill_score) * 70) / threshold;
        if (static_cast<int>(chance) < 0) {
            chance = 0;
        } else if (100 < static_cast<int>(chance)) {
            chance = 100;
        }
    } else {
        chance = 0;
    }
    minimum_level = GetMinimumCasterLevelForSpell(spell_id);
    caster_level = GetProfessionCasterLevel(character, W8_PROFESSION_NONE);
    spellbook = (record->psionics_spell != 0 ? 8U : 0U) | (record->divinity_spell != 0 ? 2U : 0U) |
                (record->wizardry_spell != 0 ? 1U : 0U) | (record->alchemy_spell != 0 ? 4U : 0U);
    for (profession = W8_PROFESSION_FIGHTER; profession < W8_PROFESSION_COUNT; ++profession) {
        if (character->profession_levels[profession] != 0 && profession != character->iProfession &&
            (g_profession_spellbooks[profession] & spellbook) != 0 &&
            (index = GetProfessionCasterLevel(character, static_cast<W8Profession>(profession)),
             0 < index)) {
            caster_level += index;
        }
    }
    index = (minimum_level - caster_level) - 1 + power_level;
    if (0 < index) {
        chance += record->spell_level * index;
    }
    if (CurrentGame().combat.active()) {
        if (g_settings.difficulty == W8_DIFFICULTY_NOVICE) {
            morale = 0x50;
        } else if (g_settings.difficulty == W8_DIFFICULTY_NORMAL) {
            morale = 0x3c;
        } else {
            if (g_settings.difficulty != W8_DIFFICULTY_EXPERT) {
                Fail("FALSE");
                goto finish_difficulty_adjustment;
            }
            morale = 0x28;
        }
        threshold = CurrentGame().combat.state()->characters[slot].phase_clock_stamp;
        if (morale <= threshold) {
            chance = (((0x32 - morale) + threshold) * chance * 2) / 100;
        }
    }
finish_difficulty_adjustment:
    if (spell_id == W8_SPELL_SANE_MIND && aim->iType == W8_TARGET_KIND_CHARACTER &&
        aim->iChar == party_slot) {
        chance += 0x32;
    }
    for (index = 0; index < 0x72; ++index) {
        if (index != 0 && character->spell_learned[index] == 1 &&
            Data().spells[index].spell_point_cost <=
                character->iSPLeft[Data().spells[index].realm] &&
            SpellUsableNow(index, false)) {
            ++CurrentGame().status.spell_usage[index - 1].usable_cast_count;
        }
    }
    ++CurrentGame().status.spell_usage[spell_id - 1].cast_count;
    affected = SpellAffectedTarget(character, spell_id, aim, power_level);
    if (!continue_cast) {
        if (Random(2) != 0) {
            index = g_special_event6;
        } else {
            index = g_special_event5;
        }
        QueueCharacterEvent(character, index, 0, kCharacterEventFullVolume);
    }
    // Immediate effects can recursively cast again. Pay for this attempt
    // before they see the available mana, and refund a cancelled attempt.
    const int mana_cost = power_level * sp_cost;
    SpendCharacterSpellPoints(party_slot, realm, mana_cost);
    try {
        result = CastSpellFromSource(spell_id, &source, aim, power_level, power_cast_bonus, chance,
                                     recast, &cast_result, W8SpellCastKind::Standard, 0, 0);
    } catch (...) {
        RestoreCharacterRealmSpellPoints(party_slot, realm, mana_cost);
        throw;
    }
    if (cast_result == 0) RestoreCharacterRealmSpellPoints(party_slot, realm, mana_cost);
    if (cast_result != 0) {
        if (affected) {
            index = record->spell_level + cast_result;
            if (index < 2) {
                Fail("uiUsagePoints >= 2");
            }
            PracticeCharacterSkill(CurrentGame(), character, best_skill, (index + 2) >> 2, false);
            PracticeCharacterSkill(CurrentGame(), character, realm_skill, index, false);
            if (character->skills[W8_SKILL_POWER_CAST].active) {
                PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_POWER_CAST,
                                       (index + 2) >> 2, false);
            }
        }
    }
    *out_points = SpellCastFatigueCost(spell_id, cast_result);
    return result;
}

/* Resolve one cast into a queued spell effect: roll the fizzle and backfire
   chances, collect the target markers, spawn the visuals or fire the
   missiles that carry the effect, and either queue it for later or apply it
   immediately. The last two vectors let callers supply their own target
   lists instead of the aimed collection. Returns 0 when the effect could
   not be allocated, 1 on a normal cast, 2 when a fizzle consumed the cast
   and 3 when it backfired. */
int CastSpellFromSource(int spell_id, W8TargetSource* source, W8CombatSlot* target,
                        unsigned int power_level, int power_cast_bonus, unsigned int failure_chance,
                        bool recast, int* out_power, W8SpellCastKind cast_kind,
                        std::vector<int>* party_targets, std::vector<int>* monster_targets)
{
    glm::vec3 point;
    bool fizzled;
    bool quiet;
    bool forced;
    bool icon_flag;
    W8MonsterRecord* record_data;
    Missile* missile;
    Monster* monster;
    SpellVisual* visual;
    W8MonsterInfo* monster_info;
    unsigned int backfire_chance;
    int message;
    unsigned int floor_power;
    unsigned int roll;
    int caster_slot;
    int index;
    W8SpellTargetType target_type;
    int missile_index;
    std::vector<int> monster_markers;
    std::vector<int> party_markers;
    W8SpellEffectDefinition block;
    W8CombatSlot point_target;
    glm::vec3 ground_point;
    glm::vec3 forward_point;
    glm::vec3 origin;
    glm::vec3 direction;
    glm::mat3 local_90;
    double yaw;
    double pitch;
    W8Disposition disposition;
    float heading;
    unsigned int caster_figure;
    unsigned int adjusted_power;

    fizzled = false;
    quiet = cast_kind == W8SpellCastKind::ThrowingItemUse;
    forced = false;
    if (out_power != 0) {
        *out_power = 0;
    }
    auto pending = std::make_unique<W8SpellEffectEntry>();
    auto& owner = *pending;
    owner.OrigSource = *source;
    owner.OrigTarget = *target;
    if (TargetSourceIsCharacter(source, 0)) {
        caster_slot = source->iChar;
    } else {
        caster_slot = -1;
    }
    source->fBackfire = false;
    if (g_force_spell_failure && spell_id != W8_SPELL_BOILING_BLOOD_EXPLOSION) {
        forced = true;
        failure_chance = 100;
    }
    if (Data().spells[spell_id].realm == W8_SPELL_REALM_FIRE && g_camera_sway_active) {
        failure_chance = 100;
    }
    if (quiet && !g_force_spell_failure) {
        failure_chance /= 2;
    }
    CombatLog(fmt::format("Chance of FAILURE: {:d}", failure_chance));
    CombatLog("");
    if (failure_chance != 0 && (roll = Random(100)) < failure_chance) {
        backfire_chance = failure_chance;
        if (Data().spells[spell_id].realm == W8_SPELL_REALM_FIRE && g_camera_sway_active) {
            backfire_chance = 0;
        } else if (!quiet && !forced) {
            if (backfire_chance < 6) {
                backfire_chance = 0;
            } else {
                backfire_chance /= 3;
            }
        }
        if (!CanSpellBackfire(spell_id)) {
            backfire_chance = 0;
        }
        CombatLog(fmt::format("Chance of BACKFIRE: {:d} (roll {:d})", backfire_chance, roll));
        CombatLog("");
        if (roll < backfire_chance) {
            PrepareSpellTarget(spell_id, source, target);
            source->fBackfire = true;
            recast = false;
            wiz8::sound::play("Data\\Sound\\Misc\\Spell Backfire.wav");
        } else {
            fizzled = true;
            wiz8::sound::play("Data\\Sound\\Misc\\Spell Fizzle 01.wav");
            if (caster_slot != -1 && Random(100) < 0x46) {
                QueueCharacterEvent(&CurrentGame().party.characters[caster_slot], g_special_event13,
                                    0, kCharacterEventFullVolume);
            }
        }
    }
    if (!fizzled) {
        if (!source->fBackfire && !source->auto_cast && !source->fReflection &&
            Data().spells[spell_id].realm != W8_SPELL_REALM_MENTAL &&
            spell_id != W8_SPELL_ROCKET_BLAST) {
            CheckSpellBackfire(spell_id, source, target);
        }
        if (party_targets == 0) {
            if (monster_targets == 0) {
                PopulateSpellTargetMarkers(spell_id, power_level, source, target, monster_markers,
                                           party_markers, W8AreaSightRule::RequireLineOfSight);
                if (spell_id == W8_SPELL_SUMMON_ELEMENTAL) {
                    ground_point.y = target->point.y - 1e+03f;
                    target->point.y = ground_point.y;
                    ground_point.x = target->point.x;
                    ground_point.z = target->point.z;
                    if ((TargetSourceIsCharacter(source, 0) && !source->fBackfire) ||
                        (!TargetSourceIsCharacter(source, 0) && source->fBackfire)) {
                        disposition = W8_DISPOSITION_FRIENDLY;
                    } else {
                        disposition = W8_DISPOSITION_HOSTILE;
                    }
                    heading = HeadingTowardNearestMonster(ground_point, disposition, 0);
                    const auto placement = GetWorldSpatialIndex()->navigatorPositions(
                        ground_point, heading,
                        static_cast<float>(
                            g_world_cursor_extent_table
                                [g_spell_power_extent_index[power_level - 1] * 6 + 3]) +
                            kWorldScale,
                        1, true, false, false, 5, true,
                        BoundingSphere{PartyNavigator()->movement.position,
                                       PartyNavigator()->movement.alternate_radius});
                    ground_point = placement.source;
                    if (!placement.positions.empty()) target->point = placement.positions.front();
                }
            } else {
                monster_markers = *monster_targets;
            }
        } else {
            party_markers = *party_targets;
            if (monster_targets != 0) {
                monster_markers = *monster_targets;
            }
        }
        block = {};
        if (TargetSourceIsCharacter(source, 1)) {
            if (!source->name_known) {
                caster_figure =
                    GetTotalCasterLevel(&CurrentGame().party.characters[source->iChar],
                                        (Data().spells[spell_id].psionics_spell != 0 ? 8 : 0) |
                                            (Data().spells[spell_id].divinity_spell != 0 ? 2 : 0) |
                                            (Data().spells[spell_id].wizardry_spell != 0 ? 1 : 0) |
                                            (Data().spells[spell_id].alchemy_spell != 0 ? 4 : 0),
                                        true);
                caster_figure = GetSpellDifficulty(caster_figure, spell_id, power_level);
            } else {
                caster_figure = source->spell_difficulty;
            }
        } else if (TargetSourceIsMonster(source, 1)) {
            monster_info = FindMonster(source->iMonsterID);
            record_data = GetMonsterDataForInfo(monster_info);
            caster_figure = GetSpellDifficulty(record_data->effective_level, spell_id, power_level);
        } else {
            if (source->iType != W8_TARGET_SOURCE_INDIRECT) {
                Fail("pSource->iType == SOURCE_TYPE_3D_POINT");
            }
            caster_figure = (Data().spells[spell_id].spell_point_cost / 2 +
                             Data().spells[spell_id].spell_level) /
                                2 +
                            power_level;
            if (GetSpellTargetType(spell_id, false) == 7) {
                caster_figure -= 3;
            }
            if (static_cast<int>(caster_figure) < 0) {
                caster_figure = 0;
            }
        }
        switch (spell_id) {
        case W8_SPELL_INSTANT_DEATH:
            floor_power = 2;
            break;
        case W8_SPELL_QUICKSAND:
            floor_power = 4;
            break;
        case W8_SPELL_ASPHYXIATION:
        case W8_SPELL_DEATH_CLOUD:
        case W8_SPELL_DEATH_WISH:
            floor_power = 6;
            break;
        default:
            floor_power = 0;
            break;
        }
        if (caster_figure < floor_power) {
            block.power_level = 0;
        } else {
            block.power_level = caster_figure - floor_power;
        }
        adjusted_power = block.power_level;
        AdjustIntegerByPercent(&adjusted_power, static_cast<unsigned int>(power_cast_bonus) >> 1);
        block.power_level = adjusted_power;
        if (IsCombatEffectSlotSpell(spell_id)) {
            SetDice(&block.magnitude, 0, 0, 0);
        } else {
            block.magnitude = Data().spells[spell_id].effect_dice;
            block.magnitude.count = static_cast<char>(power_level) * block.magnitude.count;
        }
        block.duration_scale = power_level;
        block.percent = power_cast_bonus;
        block.duration_base = Data().spells[spell_id].duration_per_level;
        block.duration_per_power = Data().spells[spell_id].duration;
        if (Data().spells[spell_id].needs_aim != 0) {
            missile_index = Data().spells[spell_id].missile_index;
            if (GetSpellTargetType(spell_id, false) == 6) {
                ResetCombatSlot(&point_target);
                point_target.point =
                    glm::vec3(target->point.x, g_default_world_height * 0.5f + target->point.y,
                              target->point.z);
                point_target.iType = W8_TARGET_KIND_PLACE;
                missile = FireMissileSourceToTarget(missile_index, source, &point_target, &block,
                                                    true, W8_RANGE_NONE, 9999);
                if (missile != 0) {
                    owner.missiles.push_back(missile);
                }
                owner.missiles_pending = true;
            } else {
                std::ranges::copy(Data().missiles[missile_index].condition_chances,
                                  block.condition_chances);
                block.magnitude_base = Data().missiles[missile_index].magnitude_base;
                for (index = 0; index < static_cast<int>(monster_markers.size()); ++index) {
                    ResetCombatSlot(&point_target);
                    point_target.iType = W8_TARGET_KIND_MONSTER;
                    point_target.iMonsterID = monster_markers[index];
                    if (!source->fBackfire && !source->fReflection) {
                        MakeTargetGroupHostile(source, &point_target);
                    }
                    missile = FireMissileSourceToTarget(
                        missile_index, source, &point_target, &block, true,
                        Data().spells[spell_id].range_category, 9999);
                    if (missile != 0) {
                        owner.missiles.push_back(missile);
                    }
                }
                for (index = 0; index < static_cast<int>(party_markers.size()); ++index) {
                    ResetCombatSlot(&point_target);
                    point_target.iType = W8_TARGET_KIND_CHARACTER;
                    point_target.iChar = party_markers[index];
                    missile = FireMissileSourceToTarget(
                        missile_index, source, &point_target, &block, true,
                        Data().spells[spell_id].range_category, 9999);
                    if (missile != 0) {
                        owner.missiles.push_back(missile);
                    }
                }
            }
        } else {
            target_type = GetSpellTargetType(spell_id, false);
            if (spell_id == W8_SPELL_ARMORMELT) {
                target_type = W8_TARGET_TYPE_ENEMY;
            }
            switch (target_type) {
            case W8_TARGET_TYPE_RADIUS:
                if (spell_id == W8_SPELL_BOILING_BLOOD_EXPLOSION) {
                    if (party_markers.empty()) {
                        point.y = target->point.y - 1e+03f;
                        target->point.y = point.y;
                        point.x = target->point.x;
                        point.z = target->point.z;
                        visual = SpawnSpellEffect(
                            &point, Data().spells[W8_SPELL_BOILING_BLOOD_EXPLOSION].resource_name,
                            1, 0, 0);
                    } else {
                        GetCameraForwardPoint(1.0, &forward_point);
                        forward_point.y -= g_default_world_height;
                        visual = SpawnSpellEffect(
                            &forward_point,
                            Data().spells[W8_SPELL_BOILING_BLOOD_EXPLOSION].resource_name, 2, 0, 0);
                    }
                } else {
                    point.y = target->point.y - 1e+03f;
                    target->point.y = point.y;
                    point.x = target->point.x;
                    point.z = target->point.z;
                    visual = SpawnSpellEffect(&point, Data().spells[spell_id].resource_name,
                                              power_level, 0, 0);
                }
                owner.AddVisual(visual);
                break;
            case W8_TARGET_TYPE_CONE:
                if (TargetSourceIsCharacter(source, 0)) {
                    visual = CreateAttachedSpellEffect(Data().spells[spell_id].resource_name,
                                                       power_level, 0, 0, 0);
                    if (visual != 0) {
                        visual->auto_release = false;
                        visual->fixed_transform = true;
                        owner.spell_visuals.push_back(visual);
                    }
                } else {
                    if (TargetSourceIsMonster(source, 0)) {
                        auto* actor = GetMonsterByLocationID(source->iMonsterID);
                        visual =
                            actor ? CreateAttachedSpellEffect(Data().spells[spell_id].resource_name,
                                                              power_level, actor, 0, 0)
                                  : nullptr;
                    } else {
                        origin = source->point;
                        direction = target->point - origin;
                        direction = glm::dot(direction, direction) == 0
                                        ? direction
                                        : glm::normalize(direction) * static_cast<float>(1.0);
                        local_90 = glm::mat3(1);
                        yaw = atan2(direction.x, direction.z);
                        local_90 *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
                        direction = glm::transpose(local_90) * direction;
                        pitch = -atan2(direction.y, direction.z);
                        local_90 *=
                            glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
                        visual = CreateAimedSpellEffect(Data().spells[spell_id].resource_name,
                                                        power_level, &origin, &local_90, 0, 0);
                    }
                    owner.AddVisual(visual);
                }
                break;
            case W8_TARGET_TYPE_POINT:
                if (spell_id == W8_SPELL_HYPNOTIC_LURE) {
                    SpawnLureEffects(&owner, power_level, target);
                } else {
                    if (spell_id != W8_SPELL_SUMMON_ELEMENTAL) {
                        target->point.y -= 1e+03f;
                    }
                    point = target->point;
                    visual = SpawnSpellEffect(&point, Data().spells[spell_id].resource_name,
                                              power_level, 0, 0);
                    owner.AddVisual(visual);
                }
                break;
            case W8_TARGET_TYPE_CASTER:
            case W8_TARGET_TYPE_ALLY:
            case W8_TARGET_TYPE_ENEMY:
                if (spell_id != W8_SPELL_SET_PORTAL && spell_id != W8_SPELL_RETURN_TO_PORTAL) {
                    for (index = 0; index < static_cast<int>(monster_markers.size()); ++index) {
                        monster = GetMonsterByLocationID(monster_markers[index]);
                        if (!monster) continue;
                        visual = CreateMonsterSpellEffect(Data().spells[spell_id].resource_name,
                                                          power_level, monster, 0, 0);
                        owner.AddVisual(visual);
                    }
                    if (!party_markers.empty()) {
                        for (index = 0; index < static_cast<int>(party_markers.size()); ++index) {
                            if (MonsterCanAimSpell(spell_id)) {
                                icon_flag = source->fBackfire;
                            } else {
                                icon_flag = !source->fBackfire;
                            }
                            StageMonsterCastIcon(party_markers[index],
                                                 Data().spells[spell_id].realm, icon_flag,
                                                 spell_id);
                        }
                    }
                } else {
                    visual = SpawnCameraSpellEffect(Data().spells[spell_id].resource_name,
                                                    power_level, 0, 0);
                    owner.AddVisual(visual);
                }
                break;
            case W8_TARGET_TYPE_PARTY:
            case W8_TARGET_TYPE_ENEMY_GROUP:
            case W8_TARGET_TYPE_ALL_ENEMIES:
                if (spell_id == W8_SPELL_EARTHQUAKE) {
                    break;
                }
                if (spell_id == W8_SPELL_NUCLEAR_BLAST) {
                    point.y = target->point.y - 1e+03f;
                    target->point.y = point.y;
                    point.x = target->point.x;
                    point.z = target->point.z;
                    visual = SpawnSpellEffect(&point,
                                              Data().spells[W8_SPELL_NUCLEAR_BLAST].resource_name,
                                              power_level, 0, 0);
                    owner.AddVisual(visual);
                    break;
                }
                for (index = 0; index < static_cast<int>(monster_markers.size()); ++index) {
                    monster = GetMonsterByLocationID(monster_markers[index]);
                    if (!monster) continue;
                    visual = CreateMonsterSpellEffect(Data().spells[spell_id].resource_name,
                                                      power_level, monster, 0, 0);
                    owner.AddVisual(visual);
                }
                if (!party_markers.empty()) {
                    visual = SpawnCameraSpellEffect(Data().spells[spell_id].resource_name,
                                                    power_level, 0, 0);
                    owner.AddVisual(visual);
                }
                break;
            case W8_TARGET_TYPE_LOCK_OR_TRAP:
                visual = SpawnCameraSpellEffect(Data().spells[spell_id].resource_name, power_level,
                                                0, 0);
                owner.AddVisual(visual);
                break;
            default:
                SpawnCameraSpellEffect("Default", 0, 0, 0);
                wiz8::sound::play("Data\\Sound\\Misc\\GeneralMagic.wav");
                break;
            case W8_TARGET_TYPE_ITEM:
            case W8_TARGET_TYPE_NONE:
            case W8_TARGET_TYPE_COUNT:
                break;
            }
        }
        owner.kind = static_cast<W8SpellId>(spell_id);
        owner.Source = *source;
        owner.target = *target;
        owner.definition = block;
        owner.monster_ids = monster_markers;
        owner.target_indices = party_markers;
        owner.recast = recast;
        if (spell_id == W8_SPELL_HYPNOTIC_LURE) {
            owner.sustained = true;
            owner.turns_remaining = RollEffectDuration(&owner.definition);
            for (index = 0; index < static_cast<int>(CurrentGame().spell_effects.size()); ++index) {
                auto& previous = *CurrentGame().spell_effects[index];
                if (previous.kind == W8_SPELL_HYPNOTIC_LURE) {
                    previous.turns_remaining = 0;
                    ReleaseHypnoticLureControl();
                    break;
                }
            }
        }
        if (CurrentGame().activity.in_world() && !CurrentGame().activity.conversation &&
            !CurrentGame().activity.conversation_aside) {
            CurrentGame().spell_effects.push_back(std::move(pending));
        } else {
            ProcessSpellEffectTargets(&owner);
            // Immediate casts have no queued owner to retire their visuals later.
            for (auto* visual : owner.spell_visuals) visual->auto_release = true;
        }
        PointCameraAtCombatTarget(source, target);
        if (source->fBackfire) {
            if (quiet) {
                owner.reported = true;
            } else if (g_settings.verbose_combat_messages != 0) {
                ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x197]);
            } else {
                AppendToLastTextLine(fmt::format(" -- {}", Data().strings[0x197]), -1);
                owner.reported = true;
            }
            if (caster_slot != -1) {
                if (Random(2) == 0) {
                    QueueCharacterEvent(&CurrentGame().party.characters[caster_slot], 16, 0,
                                        kCharacterEventFullVolume);
                } else {
                    ApplyItemEffectToRandomCharacter(17, caster_slot, 0);
                }
            }
        }
    } else {
        message = 0;
        switch (cast_kind) {
        case W8SpellCastKind::MusicItemUse:
            if (source->iType != W8_TARGET_SOURCE_CHARACTER) {
                Fail("pSource->iType == SOURCE_TYPE_CHAR");
            }
            message = 0x1aa;
            [[fallthrough]];
        case W8SpellCastKind::EngineeringItemUse:
            if (source->iType != W8_TARGET_SOURCE_CHARACTER) {
                Fail("pSource->iType == SOURCE_TYPE_CHAR");
            }
            if (cast_kind == W8SpellCastKind::EngineeringItemUse) {
                message = 0x1ab;
            }
            if (g_settings.verbose_combat_messages != 0) {
                PostCharacterNotice(source->iChar, Data().strings[message]);
            } else {
                AppendToLastTextLine(fmt::format(" -- {}", Data().strings[message]), -1);
            }
            break;
        default:
            if (g_settings.verbose_combat_messages != 0) {
                ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x196]);
            } else {
                AppendToLastTextLine(fmt::format(" -- {}", Data().strings[0x196]), -1);
            }
            break;
        }
    }
    if (out_power != 0) {
        *out_power = power_level;
    }
    if (fizzled) {
        if (TargetSourceIsMonster(&owner.Source, 0)) {
            if (owner.Source.iMonsterID == -1) {
                Fail("pOrigSource->iMonsterID != -1");
            }
            if (CurrentGame().combat.active()) {
                CurrentGame().combat.state()->setStepOf(MonsterActor{owner.Source.iMonsterID},
                                                        ActionStep::finishing);
            }
        }
        return 2;
    }
    if (!source->fBackfire) {
        return 1;
    }
    return 3;
}

/* A backfiring spell swaps roles: the intended target becomes the source and
   the concrete thing to aim back at is resolved here - the caster for a
   character target, the first live party member for a party target, and so
   on. */
void RedirectBackfiredSpellTarget(W8TargetSource* source, W8CombatSlot* target)
{
    W8TargetSource source_copy;
    W8CombatSlot target_copy;
    W8MonsterInfo* monster_info;
    W8MonsterGroup* group;
    int slot;

    if (source->fBackfire) {
        Fail("!pSource->fBackfire");
    }
    source_copy = *source;
    target_copy = *target;
    switch (target_copy.iType) {
    case W8_TARGET_KIND_CHARACTER:
    case W8_TARGET_KIND_CHARACTER_INDIRECT:
        SetTargetSourceToCharacter(target_copy.iChar, source);
        break;
    case W8_TARGET_KIND_MONSTER:
        monster_info = FindMonster(target_copy.iMonsterID);
        SetTargetSourceToMonster(monster_info, source);
        break;
    case W8_TARGET_KIND_PARTY:
        for (slot = 0; slot < 8; ++slot) {
            if (CanPartySlotParticipate(slot)) {
                break;
            }
        }
        if (slot >= 8) {
            Fail("FALSE");
            return;
        }
        SetTargetSourceToCharacter(slot, source);
        ResetCombatSlot(target);
        if (source_copy.iType == W8_TARGET_SOURCE_CHARACTER) {
            target->iType = W8_TARGET_KIND_PARTY;
        } else if (source_copy.iType == W8_TARGET_SOURCE_MONSTER) {
            target->iType = W8_TARGET_KIND_GROUP;
            monster_info = FindMonster(source_copy.iMonsterID);
            target->iGroupID = monster_info->monster_group_id;
        } else {
            Fail("FALSE");
        }
        source->point_source = true;
        return;
    case W8_TARGET_KIND_GROUP:
        group = FindMonsterGroup(target_copy.iGroupID);
        monster_info = FindMonster(group->leader_location_id);
        SetTargetSourceToMonster(monster_info, source);
        ResetCombatSlot(target);
        if (source_copy.iType == W8_TARGET_SOURCE_CHARACTER) {
            target->iType = W8_TARGET_KIND_PARTY;
        } else if (source_copy.iType == W8_TARGET_SOURCE_MONSTER) {
            target->iType = W8_TARGET_KIND_GROUP;
            monster_info = FindMonster(source_copy.iMonsterID);
            target->iGroupID = monster_info->monster_group_id;
        } else {
            Fail("FALSE");
        }
        source->point_source = true;
        return;
    default:
        Fail("FALSE");
        return;
    }
    if (TargetSourceIsCharacter(&source_copy, 0)) {
        target->iType = W8_TARGET_KIND_CHARACTER;
        target->iChar = source_copy.iChar;
    } else if (TargetSourceIsMonster(&source_copy, 0)) {
        target->iType = W8_TARGET_KIND_MONSTER;
        target->iMonsterID = source_copy.iMonsterID;
    } else {
        Fail("FALSE");
    }
    source->point_source = true;
}

/* Assert and route the source/target pair a cast is about to use. Target
   kinds that aim at a single combatant re-resolve both ends through
   RedirectBackfiredSpellTarget; the point kinds trace the aim to where the
   spell actually lands and turn the source into a point. */
void PrepareSpellTarget(int spell_id, W8TargetSource* source, W8CombatSlot* target)
{
    glm::vec3 from;
    glm::vec3 trace;
    glm::vec3 saved;
    W8MonsterInfo* monster_info;
    Navigator* navigator;
    W8SpellTargetType target_type;

    if (source->iType <= W8_TARGET_SOURCE_NONE || source->iType >= W8_TARGET_SOURCE_COUNT) {
        Fail("(pSource->iType > SOURCE_TYPE_NONE) && (pSource->iType < SOURCE_TYPE_COUNT)");
    }
    if (target->iType <= W8_TARGET_KIND_NONE || target->iType >= W8_TARGET_KIND_COUNT) {
        Fail("(pTarget->iType > TARGET_TYPE_NONE) && (pTarget->iType < TARGET_TYPE_COUNT)");
    }
    if (source->fBackfire) {
        Fail("!pSource->fBackfire");
    }
    const bool monster_source = TargetSourceIsMonster(source, 0);
    monster_info = 0;
    if (monster_source) {
        monster_info = FindMonster(source->iMonsterID);
    }
    target_type = GetSpellTargetType(spell_id, false);
    switch (target_type) {
    case 2:
    case 7:
    case 10:
        return;
    case 3:
        if (spell_id == W8_SPELL_CHARM) {
            return;
        }
        if (spell_id == W8_SPELL_MINDREAD) {
            return;
        }
        [[fallthrough]];
    case 1:
    case 4:
        RedirectBackfiredSpellTarget(source, target);
        return;
    case 5:
        if (TargetSourceIsCharacter(source, 0)) {
            from = cameraLocation();
        } else if (monster_source) {
            if (!monster_info->p3D->GetSpellPosition(&from)) {
                monster_info->p3D->GetMappedPosition(&from);
            }
        } else {
            from = source->point;
        }
        trace = target->point;
        if (const auto hit = GetWorldSpatialIndex()->traceSight(from, trace)) trace = hit->point;
        target->point = trace;
        saved = source->point;
        source->point = target->point;
        if (TargetSourceIsCharacter(source, 0)) {
            from = cameraLocation();
            target->point = from;
        } else if (monster_source) {
            navigator = &(monster_info->p3D)->nav;
            target->point = navigator->movement.position;
            target->point.y += navigator->movement.height_offset;
        } else {
            target->point = saved;
        }
        source->iType = W8_TARGET_SOURCE_INDIRECT;
        return;
    case 6:
        break;
    default:
        Fail("FALSE",
             fmt::format("SpellBackfires: ERROR - Invalid spell target type for spell {:d}",
                         spell_id));
        return;
    }
    saved = source->point;
    source->point = target->point;
    if (TargetSourceIsCharacter(source, 0)) {
        navigator = PartyNavigator();
    } else if (monster_source) {
        navigator = &(monster_info->p3D)->nav;
    } else {
        target->point = saved;
        source->iType = W8_TARGET_SOURCE_INDIRECT;
        return;
    }
    trace = navigator->GetPosition();
    target->point = trace;
    source->iType = W8_TARGET_SOURCE_INDIRECT;
}

/* Pick one random in-combat participant other than the source and write it as
   the backfired spell's new target. A single-target spell keeps it a single
   slot; a group spell takes the whole monster group or the party instead. */
int PickBackfireTarget(int spell_id, W8TargetSource* source, W8CombatSlot* target)
{
    W8MonsterInfo* monster_info;
    unsigned int candidates;
    unsigned int pick;
    unsigned int index;
    int slot;

    candidates = 0;
    for (slot = 0; slot < 8; ++slot) {
        if (CurrentGame().party.rows[slot].fOccupied &&
            (!TargetSourceIsCharacter(source, 0) || source->iChar != slot)) {
            ++candidates;
        }
    }
    index = 0;
    while (index < CurrentLevelPopulation().monster_roster.live().size()) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->fActive && monster_info->pCombat && monster_info->hp_current != 0 &&
            (!TargetSourceIsMonster(source, 0) ||
             source->iMonsterID != monster_info->location_id)) {
            ++candidates;
        }
        ++index;
    }
    if (candidates == 0) {
        return 0;
    }
    pick = Random(candidates) + 1;
    for (slot = 0; slot < 8; ++slot) {
        if (CurrentGame().party.rows[slot].fOccupied &&
            (!TargetSourceIsCharacter(source, 0) || source->iChar != slot) && (--pick == 0)) {
            if (GetSpellTargetType(spell_id, false) == 4) {
                target->iType = W8_TARGET_KIND_PARTY;
                return 1;
            }
            target->iChar = slot;
            target->iType = W8_TARGET_KIND_CHARACTER;
            return 1;
        }
    }
    index = 0;
    while (index < CurrentLevelPopulation().monster_roster.live().size()) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->fActive && monster_info->pCombat && monster_info->hp_current != 0 &&
            (!TargetSourceIsMonster(source, 0) ||
             source->iMonsterID != monster_info->location_id) &&
            (--pick == 0)) {
            if (GetSpellTargetType(spell_id, false) == 4) {
                target->iType = W8_TARGET_KIND_GROUP;
                target->iGroupID = monster_info->monster_group_id;
                return 1;
            }
            target->iType = W8_TARGET_KIND_MONSTER;
            target->iMonsterID = monster_info->location_id;
            return 1;
        }
        ++index;
    }
    return 0;
}

/* Scatter a backfired point target to a random reachable spot inside the
   spell's range around its source: random offsets off the source's position,
   line of sight traced, the result settled onto the ground and clamped back
   inside range. */
void ScatterSpellPointTarget(int spell_id, W8TargetSource* source, W8CombatSlot* target)
{
    glm::vec3 origin;
    glm::vec3 point;
    glm::vec3 delta;
    W8MonsterInfo* monster_info;
    Navigator* navigator;
    double range;
    int attempt;

    range = CalcRangeDistance(Data().spells[spell_id].range_category);
    navigator = PartyNavigator();
    if (source->iType != W8_TARGET_SOURCE_CHARACTER) {
        if (source->iType != W8_TARGET_SOURCE_MONSTER) {
            Fail("pSource->iType == SOURCE_TYPE_MONSTER");
        }
        monster_info = FindMonster(source->iMonsterID);
        navigator = &(monster_info->p3D)->nav;
    }
    origin = navigator->GetPosition();
    for (attempt = 0; attempt < 5; ++attempt) {
        point.x = static_cast<float>(
            (Random(0x7d1) - g_monster_poster_max_distance) * range * 0.001 + origin.x);
        point.z = static_cast<float>(
            (Random(0x7d1) - g_monster_poster_max_distance) * range * 0.001 + origin.z);
        point.y = Random(0x3e9) * range * 0.001 + origin.y;
        if (const auto hit = GetWorldSpatialIndex()->traceSight(origin, point)) point = hit->point;
        point.y = probeGround(point).position.y;
        if (point.y != kGroundSettleFail) {
            break;
        }
    }
    delta = origin - point;
    if (range < glm::length(delta)) {
        delta =
            glm::dot(delta, delta) == 0 ? delta : glm::normalize(delta) * static_cast<float>(range);
        point = delta + origin;
    }
    target->point = point;
}

/* The once-per-cast backfire roll for a spell whose source is under the
   confusion condition: a trait save can skip it, then roughly one cast in
   five has its target re-picked at random or scattered to a random point,
   with a notice to whoever the source is. */
void CheckSpellBackfire(int spell_id, W8TargetSource* source, W8CombatSlot* target)
{
    W8Character* character;
    W8MonsterInfo* monster_info;
    W8SpellTargetType target_type;

    monster_info = 0;
    if (source->iType == W8_TARGET_SOURCE_CHARACTER) {
        character = &CurrentGame().party.characters[source->iChar];
        if (character->uiCondition[W8_CONDITION_BLIND] == 0) {
            return;
        }
        if (target->iType == W8_TARGET_KIND_CHARACTER && target->iChar == source->iChar) {
            return;
        }
        if (CharacterHasTrait(character, W8_TRAIT_EFFECTIVE_WHILE_BLIND)) {
            if (Random(100) <
                static_cast<unsigned int>(static_cast<int>(ScaleValueByProfessionLevel(
                    character, W8_TRAIT_EFFECTIVE_WHILE_BLIND, 50.0)))) {
                return;
            }
        }
    } else {
        if (source->iType != W8_TARGET_SOURCE_MONSTER) {
            return;
        }
        monster_info = FindMonster(source->iMonsterID);
        if (monster_info->uiCondition[W8_CONDITION_BLIND] == 0) {
            return;
        }
        if (target->iType == W8_TARGET_KIND_MONSTER && target->iMonsterID == source->iMonsterID) {
            return;
        }
    }
    if (0x13 < Random(100)) {
        return;
    }
    target_type = GetSpellTargetType(spell_id, false);
    switch (target_type) {
    case 1:
    case 3:
    case 4:
        if (PickBackfireTarget(spell_id, source, target) == 0) {
            return;
        }
        break;
    case 5:
    case 6:
    case 8:
        ScatterSpellPointTarget(spell_id, source, target);
        break;
    default:
        return;
    }
    if (TargetSourceIsCharacter(source, 0)) {
        PostCharacterNotice(source->iChar, Data().strings[0x191]);
        return;
    }
    if (TargetSourceIsMonster(source, 0)) {
        PostMonsterNotice(monster_info, Data().strings[0x191]);
    }
}

/* The source and target validation paths report the same cast context. */
static void ReportInvalidMagicReference(const char* type, int spell_id,
                                        const W8TargetSource& source, const W8CombatSlot& target)
{
    DebugMessage(1,
                 fmt::format("{}: Spell {:d}({}), Target Type {:d}(char {:d}, "
                             "monster ID {:d}, group ID {:d}), Source Type {:d}(char {:d},ID {:d})",
                             type, spell_id, Data().spells[spell_id].display_name,
                             fmt::underlying(target.iType), target.iChar, target.iMonsterID,
                             target.iGroupID, fmt::underlying(source.iType), source.iChar,
                             source.iMonsterID));
}

/* Resolve valid spell targets for one cast and append location ids to the
   caller's vectors. The source's position and eye are picked per source kind,
   then the spell's target type decides whether the single aimed slot, a
   radius, a cone or a whole side goes into the vectors. */
void PopulateSpellTargetMarkers(int spell_id, int power_level, W8TargetSource* source,
                                W8CombatSlot* target, std::vector<int>& monster_markers,
                                std::vector<int>& party_markers, W8AreaSightRule sight_rule)
{
    glm::vec3 player_pos;
    glm::vec3 camera;
    glm::vec3 centre;
    glm::vec3 eye;
    glm::vec3 target_point;
    glm::vec3 trace;
    W8MonsterInfo* monster_info;
    W8MonsterInfo* member;
    W8MonsterGroup* group;
    Monster* monster;
    float radius;
    float heading;
    float elevation;
    float distance;
    W8SpellTargetType target_type;
    int side = 0;
    int sight_flag;
    int slot;
    unsigned int index;
    bool marked;
    bool fVertextAvail;

    const float cone_range = GetGroundTargetRange();

    monster = 0;
    marked = false;
    monster_info = 0;
    sight_flag = 0;
    if (!source->fBackfire && !source->fReflection &&
        !SourceActionReachesTarget(source, target,
                                   W8Action{W8_ACTION_CAST_SPELL, spell_id, *target, {}})) {
        return;
    }
    player_pos = PartyNavigator()->GetPosition();
    camera = cameraLocation();
    if (TargetSourceIsMonster(source, 1)) {
        if (source->iMonsterID == -1) {
            ReportInvalidMagicReference("InvalidMagicSource", spell_id, *source, *target);
            return;
        }
        monster_info = FindMonster(source->iMonsterID);
        if (monster_info == nullptr) {
            ReportInvalidMagicReference("InvalidMagicSource", spell_id, *source, *target);
            return;
        }
        GetMonsterDataForInfo(monster_info);
        monster = monster_info->p3D;
    }
    /* A monster source, direct or (for backfire and reflection) indirect, was
       resolved above; a direct one is also where the spell starts from. */
    const bool monster_source = monster_info != nullptr;
    const bool direct_monster_source = monster_source && TargetSourceIsMonster(source, 0);
    if (TargetSourceIsCharacter(source, 0)) {
        centre = player_pos;
        eye = camera;
    } else if (direct_monster_source) {
        centre = monster->nav.GetPosition();
        fVertextAvail = false;
        if (!source->point_source && spell_id != W8_SPELL_SPECIAL_ATTACK_CONE &&
            monster_info->has_spell_origin) {
            fVertextAvail = monster->GetSpellPosition(&eye);
            if (!fVertextAvail) {
                Fail("fVertextAvail");
            }
        }
        if (fVertextAvail) {
            sight_flag = 3;
        } else {
            eye = monster->nav.movement.position;
            eye.y += monster->nav.movement.height_offset;
            sight_flag = 2;
        }
    } else {
        eye = source->point;
        centre = source->point;
    }
    target_point = target->point;
    target_type = GetSpellTargetType(spell_id, false);
    switch (target_type) {
    case W8_TARGET_TYPE_CASTER:
    case W8_TARGET_TYPE_ENEMY:
        if (target->iType == W8_TARGET_KIND_CHARACTER) {
            party_markers.push_back(target->iChar);
        } else if (target->iType == W8_TARGET_KIND_MONSTER) {
            monster_markers.push_back(target->iMonsterID);
        } else if (target_type == W8_TARGET_TYPE_ENEMY) {
            ReportInvalidMagicReference("InvalidMagicTarget", spell_id, *source, *target);
            return;
        }
        break;
    case W8_TARGET_TYPE_ALLY:
        if (target->iType == W8_TARGET_KIND_CHARACTER ||
            target->iType == W8_TARGET_KIND_CHARACTER_INDIRECT) {
            party_markers.push_back(target->iChar);
        } else if (target->iType == W8_TARGET_KIND_MONSTER) {
            monster_markers.push_back(target->iMonsterID);
        } else {
            ReportInvalidMagicReference("InvalidMagicTarget", spell_id, *source, *target);
            return;
        }
        break;
    case W8_TARGET_TYPE_PARTY:
        radius = (Data().spells[spell_id].radius_per_level * power_level +
                  Data().spells[spell_id].effect_radius) *
                 kWorldScale;
        if (TargetSourceIsCharacter(source, 0)) {
            marked = true;
            side = 2;
            if (0.0f < radius) {
                radius += PartyNavigator()->radius;
            }
        } else if (direct_monster_source) {
            if (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY) {
                distance = (centre.x - player_pos.x) * (centre.x - player_pos.x) +
                           (centre.y - player_pos.y) * (centre.y - player_pos.y) +
                           (centre.z - player_pos.z) * (centre.z - player_pos.z);
                if (sqrtf(distance) <= radius &&
                    monster_info->player_visibility.los_flags[sight_flag] != '\0') {
                    marked = true;
                }
            } else if (monster_info->ubDisposition != W8_DISPOSITION_HOSTILE) {
                ReportInvalidMagicReference("InvalidMagicSource", spell_id, *source, *target);
                return;
            }
            side = monster_info->ubDisposition;
            radius += monster->nav.radius;
        } else {
            side = 2;
            distance = (centre.x - player_pos.x) * (centre.x - player_pos.x) +
                       (centre.y - player_pos.y) * (centre.y - player_pos.y) +
                       (centre.z - player_pos.z) * (centre.z - player_pos.z);
            if (sqrtf(distance) <= radius && !GetWorldSpatialIndex()->traceSight(eye, camera)) {
                marked = true;
            }
        }
        CollectMonstersWithinRadius(&centre, &eye, monster_markers, radius, static_cast<char>(side),
                                    sight_rule);
        break;
    case W8_TARGET_TYPE_ENEMY_GROUP:
        if (target->iType == W8_TARGET_KIND_GROUP) {
            group = FindMonsterGroup(target->iGroupID);
            if (group == 0) {
                ReportInvalidMagicReference("InvalidMagicTarget", spell_id, *source, *target);
                return;
            }
            index = 0;
            while (index < group->MemberIds().size()) {
                slot = group->MemberLocationId(index);
                member = FindMonster(slot);
                if (member->ubDisposition == group->ubDisposition) {
                    monster_markers.push_back(slot);
                }
                ++index;
            }
        } else if (target->iType == W8_TARGET_KIND_PARTY) {
            marked = true;
        } else {
            ReportInvalidMagicReference("InvalidMagicTarget", spell_id, *source, *target);
            return;
        }
        break;
    case W8_TARGET_TYPE_CONE:
        heading = GetHeadingAngle(&eye, &target_point);
        elevation = GetElevationAngle(&eye, &target_point);
        if (TargetSourceIsCharacter(source, 0)) {
            side = 1;
        } else if (direct_monster_source) {
            if (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY) {
                side = 1;
            } else if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE) {
                side = 2;
                if (TargetInRangeAndArcs(&camera, PartyNavigator()->radius, &eye,
                                         monster->nav.radius, heading, elevation, cone_range) &&
                    monster_info->player_visibility.los_flags[sight_flag] != '\0') {
                    marked = true;
                }
            } else {
                ReportInvalidMagicReference("InvalidMagicSource", spell_id, *source, *target);
                return;
            }
        } else {
            side = 3;
            if (source->fBackfire || source->fReflection) {
                if (source->iChar != -1) {
                    side = 2;
                } else if (source->iMonsterID != -1) {
                    if (FindMonster(source->iMonsterID)->ubDisposition != W8_DISPOSITION_FRIENDLY) {
                        side = 1;
                    } else {
                        side = 2;
                    }
                }
            }
        }
        if (!marked &&
            TargetInRangeAndArcs(&camera, PartyNavigator()->radius, &eye, 0, heading, elevation,
                                 cone_range) &&
            !GetWorldSpatialIndex()->traceSight(eye, camera)) {
            marked = true;
        }
        CollectConeMonsterTargets(source, &eye, heading, elevation, monster_markers,
                                  static_cast<unsigned char>(side), sight_flag, cone_range);
        if (monster_markers.empty() &&
            (CurrentGame().combat.state() == 0 || !CurrentGame().combat.state()->enemies_engaged) &&
            TargetSourceIsCharacter(source, 0) && static_cast<char>(side) == 1 &&
            !AnyMonsterEngaged()) {
            CollectConeMonsterTargets(source, &eye, heading, elevation, monster_markers, 3,
                                      sight_flag, cone_range);
        }
        break;
    case W8_TARGET_TYPE_RADIUS:
        trace = glm::vec3(target_point.x, target_point.y - 1e+03f, target_point.z);
        radius = (Data().spells[spell_id].radius_per_level * power_level +
                  Data().spells[spell_id].effect_radius) *
                 kWorldScale;
        if (TargetSourceIsCharacter(source, 1)) {
            side = (source->fBackfire || source->fReflection) ? 1 : 2;
        } else if (monster_source) {
            if (monster_info->ubDisposition != W8_DISPOSITION_FRIENDLY &&
                monster_info->ubDisposition != W8_DISPOSITION_HOSTILE) {
                ReportInvalidMagicReference("InvalidMagicSource", spell_id, *source, *target);
                return;
            }
            side = (source->fBackfire || source->fReflection) ? 1 : 2;
        } else {
            side = 3;
        }
        if (side != 1) {
            distance = (trace.x - player_pos.x) * (trace.x - player_pos.x) +
                       (trace.y - player_pos.y) * (trace.y - player_pos.y) +
                       (trace.z - player_pos.z) * (trace.z - player_pos.z);
            if (sqrtf(distance) <= radius &&
                !GetWorldSpatialIndex()->traceSight(target_point, camera)) {
                marked = true;
            }
        }
        CollectMonstersWithinRadius(&trace, &target_point, monster_markers, radius,
                                    static_cast<char>(side), sight_rule);
        if (monster_markers.empty() &&
            (CurrentGame().combat.state() == 0 || !CurrentGame().combat.state()->enemies_engaged) &&
            TargetSourceIsCharacter(source, 0) && static_cast<char>(side) == 1 &&
            !AnyMonsterEngaged()) {
            CollectMonstersWithinRadius(&trace, &target_point, monster_markers, radius, 3,
                                        sight_rule);
        }
        break;
    case W8_TARGET_TYPE_ALL_ENEMIES:
        target->point = eye;
        radius = CalcRangeDistance(Data().spells[spell_id].range_category, source);
        if (TargetSourceIsCharacter(source, 1) ||
            (monster_source && monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY)) {
            side = (source->fBackfire || source->fReflection) ? 2 : 1;
        } else if (monster_source) {
            side = (source->fBackfire || source->fReflection) ? 1 : 2;
        } else {
            side = 3;
        }
        if (side != 1) {
            distance = (centre.x - player_pos.x) * (centre.x - player_pos.x) +
                       (centre.y - player_pos.y) * (centre.y - player_pos.y) +
                       (centre.z - player_pos.z) * (centre.z - player_pos.z);
            if (sqrtf(distance) <= radius && !GetWorldSpatialIndex()->traceSight(eye, camera)) {
                marked = true;
            }
        }
        CollectMonstersWithinRadius(&centre, &eye, monster_markers, radius, static_cast<char>(side),
                                    sight_rule);
        if (monster_markers.empty() &&
            (CurrentGame().combat.state() == 0 || !CurrentGame().combat.state()->enemies_engaged) &&
            TargetSourceIsCharacter(source, 0) && static_cast<char>(side) == 1 &&
            !AnyMonsterEngaged()) {
            CollectMonstersWithinRadius(&centre, &eye, monster_markers, radius, 3, sight_rule);
        }
        break;
    default:
        break;
    }
    if (marked && spell_id != W8_SPELL_HOLY_WATER && spell_id != W8_SPELL_BANISH) {
        for (slot = 0; slot < 8; ++slot) {
            if (CanPartySlotParticipate(slot) &&
                (CurrentGame().party.characters[slot].uiCondition[W8_CONDITION_TURNCOAT] == 0 ||
                 !MonsterCanAimSpell(spell_id) || static_cast<char>(side) == 3)) {
                party_markers.push_back(slot);
            }
        }
    }
    PruneSpellTargetMarkers(spell_id, monster_markers);
}

/* Drop every marker that no longer names a live, targetable monster. Spells
   0x16, 0x4d and 0x81 also drop markers whose monster kind they do not affect. */
void PruneSpellTargetMarkers(int spell_id, std::vector<int>& monster_markers)
{
    std::erase_if(monster_markers, [spell_id](int monster_id) {
        W8MonsterInfo* monster = FindMonster(monster_id);
        W8MonsterRecord* record = GetMonsterDataForInfo(monster);
        if (!monster->fActive || monster->hp_current == 0 ||
            monster->uiCondition[W8_CONDITION_DEAD] > 0 || record->untargetable != 0) {
            return true;
        }
        switch (spell_id) {
        case W8_SPELL_HOLY_WATER:
            return record->kind != 0x14 && record->kind != 0x15;
        case W8_SPELL_BANISH:
            return record->kind != 0x14 && record->kind != 0x15 && record->kind != 0x1c &&
                   monster->summoned == W8_MONSTER_SUMMON_NONE;
        case W8_SPELL_DISPEL_UNDEAD:
            return record->kind != 0x14;
        default:
            return false;
        }
    });
}

void TrackItemSpellSource(W8Character* character, int spell_id)
{
    bool has_spell[0x96] = {false};

    // Equipped slots precede backpack slots in the original spell-use scan.
    for (int index = 0; index < 20; ++index) {
        W8ItemInstance* item =
            index < 12 ? &character->EquippedItem[index] : &character->backpack[index - 12];
        const int item_id = item->iItemNo;
        if (item_id != -1 && Data().items[item_id].spell_id != W8_SPELL_NONE &&
            CanCharacterActivateItem(character, item) &&
            ((Data().items[item_id].quantity_kind != W8_ITEM_QUANTITY_SHOTS &&
              Data().items[item_id].quantity_kind != W8_ITEM_QUANTITY_CHARGES) ||
             item->uses_or_charges != 0)) {
            has_spell[Data().items[item_id].spell_id] = 1;
        }
    }
    W8ItemSpellUsageRecord* record = CurrentGame().status.item_spell_usage;
    int index = 0;
    while (record < CurrentGame().status.item_spell_usage + 150) {
        if (has_spell[index]) {
            ++record->usable_cast_count;
        }
        ++index;
        ++record;
    }
    ++CurrentGame().status.item_spell_usage[spell_id].cast_count;
}
