#include "wiz8/local_code/MonsterManager.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "navigation_flags.h"
#include "sight_state.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/range.h"
#include "wiz8/containers.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/difficulty.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/ThingEditorShared.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/monster_runtime.h"
#include "wiz8/monster_spell_icons.h"
#include "wiz8/music_playlist.h"
#include "wiz8/notices.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/random.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/story.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

const float g_monster_record_float_scale = 20.0f;

/* The character array the alternate-name form indexes, and the slot it uses. */

// Finish the group's runtime and NPC bindings before presenting its activation.
void Game::activateGroup(W8MonsterGroup& group)
{
    auto& world = level.loaded();
    group.highlighted_member = -1;
    bool activated = false;
    for (int id : group.MemberIds()) {
        auto& info = *world.population->findMonster(id);
        if (info.fActive) continue;
        if (!info.entity) {
            info.entity = world.loadMonsterBody(data.monsters[info.monster_species], id, rng);
            info.configureBody(rng, status.level_progress[world.level_id].visited != 0);
        }
        world.prepareMonsterBody(info, rng, world.population->monster_roster.isUnborn(&info));
        bindMonsterNpc(world, info);
        activated = true;
    }
    group.members_active = true;
    if (activated) events.push(MonstersActivated{});
}

void World::prepareMonsterBody(W8MonsterInfo& info, Rng& rng, bool unborn)
{
    prepareMonsterBody(info, rng, unborn, party.camera.gravity());
}

void World::prepareMonsterBody(W8MonsterInfo& info, Rng& rng, bool unborn, float reference_gravity)
{
    auto* monster_info = &info;
    const W8MonsterRecord* record;
    glm::vec3 camera_position;

    assert(monster_info != 0);
    if (monster_info->fActive) {
        return;
    }

    if (!monster_info->entity) throw std::runtime_error("missing prepared monster representation");
    Monster& monster = *monster_info->entity;
    record = &monster.species();
    if (monster.IsPendingFinalize()) {
        monster.SetLocationId(monster_info->location_id);
        monster.SetAdjustedPosition(&monster_info->position, reference_gravity);

        if (unborn) {
            if (!monster.IsCycleSupported(0))
                throw std::runtime_error(
                    fmt::format("unborn monster {} has no birth cycle", monster.name));
            monster.visual.SetBehaviour(W8_ANIMATION_PLAY_ONCE);
            monster.SetCycle(0);
            assert(monster.Query(W8_MONSTER_QUERY_FRAME_COUNT) != -1);
            monster.visual.SetSubCycle(0);
            monster.SetAnimating(false);
            monster.nav.active = false;
            monster.inactive = true;
        } else {
            monster.SetCycle(1);
            monster.visual.SetBehaviour(W8_ANIMATION_NEVER_STOP);
            assert(monster.Query(W8_MONSTER_QUERY_FRAME_COUNT) != -1);
            monster.visual.SetSubCycle(rng.below(monster.Query(W8_MONSTER_QUERY_FRAME_COUNT)));
            monster.SetAnimating(!monster_info->fMotionless);
        }

        active_monsters.push_back(&monster);
        monster.SetFacing(monster_info->derived);
        RebuildMonsterDerivedStats(*monster_info, *record);
        monster.nav.movement.leadership_rank =
            static_cast<unsigned int>(record->effective_level) * 0x10000U +
            monster_info->location_id;
        monster.SetLocationId(monster_info->location_id);
        monster.pending_finalize = false;

        InitializeMonsterRangeCapabilities(monster_info, record);
    }

    camera_position = monster.owner_world.party.eye();
    monster.visual.SelectLOD(&camera_position);
    monster_info->entity->updateDamageAppearance(monster_info->hp_current, monster_info->uiHPMax);
    monster.UpdateRepresentation();
    compiledIndex()->UpdateMonsterLocation(static_cast<unsigned short>(monster_info->location_id),
                                           monster_info->position);
    monster_info->fActive = true;
    monster_info->monster_visibility = std::make_unique<W8MonsterVisibility>();
    if (record->can_open_doors) {
        monster.nav.movement.navigation_filter |= W8_NAV_THROUGH_DOORS;
    }
}

static float RollMonsterScale(W8MonsterInfo& info, const W8MonsterRecord& record, Rng& rng)
{
    const float minimum = info.entity->minimum_scale;
    const float maximum = info.entity->maximum_scale;
    if (minimum == 0.0f || maximum == 0.0f) {
        return info.entity->scale;
    }

    int minimum_hp = record.hit_points.Minimum();
    int maximum_hp = record.hit_points.Maximum();
    float scale = ((maximum - minimum) * (static_cast<unsigned int>(info.uiHPMax) - minimum_hp)) /
                      (maximum_hp - minimum_hp) +
                  minimum;
    float result = (maximum - minimum) * (rng.below(1000) * 0.0004f - 0.2f) + scale;

    if (result < minimum) {
        return minimum;
    }
    if (result > maximum) {
        return maximum;
    }
    return result;
}

void W8MonsterInfo::configureBody(Rng& rng, bool level_visited)
{
    const auto& record = entity->species();
    if (scale < 0.0f || !level_visited) {
        saved_mirror_x = entity->visual.mirror_x;
        scale = RollMonsterScale(*this, record, rng);
        entity->scale = scale;
    } else {
        entity->scale = scale;
        entity->visual.mirror_x = saved_mirror_x;
    }

    entity->ApplyRepresentationScale();
    entity->SetCycle(1);
    entity->RefreshStandingHeight();
}

void ClearMonsterPathAndResume(W8MonsterInfo* monster_info)
{
    int result;

    assert(monster_info != 0);
    monster_info->entity->visual.m_pAI.reset();
    monster_info->entity->nav.flags &= 0xdfffffff;
    monster_info->entity->nav.ClearMovement();
    if (!monster_info->fMotionless) {
        result = monster_info->entity->Query(W8_MONSTER_QUERY_CYCLE);
        if (result != 1 && result != 2 && monster_info->entity->visual.pending_cycle == -1) {
            monster_info->startCycle(W8_MONSTER_CYCLE_IDLE, W8_ANIMATION_NEVER_STOP);
        }
    }
}

/* Rebuild the icons for the conditions, enchantments and effects a loaded
   monster carries. */
void W8MonsterInfo::restoreSpellIcons()
{
    auto& monster = *entity;
    if (highest_condition != W8_CONDITION_NONE) {
        for (int index = 0; index < W8_CONDITION_COUNT; ++index) {
            if (uiCondition[index] != 0)
                monster.addSpellIcon(static_cast<W8MonsterSpellIconId>(index - 1));
        }
    }
    for (int index = 0; index < 8; ++index) {
        if (enchantments[index].turns != 0)
            monster.addSpellIcon(static_cast<W8MonsterSpellIconId>(index + 0x10));
    }
    for (const auto& effect : effect_slots) {
        if (effect.duration != 0)
            monster.addSpellIcon(g_effect_visual_table[effect.effect_id].monster_icon);
    }
    if (charm_strength > 0) monster.addSpellIcon(SPELL_ICON_CHARMED);
    if (summoned != W8_MONSTER_SUMMON_NONE) monster.addSpellIcon(SPELL_ICON_SUMMONED);
}

void Game::startMonsterDeath(W8MonsterInfo* monster_info, bool display_message)
{
    if (!monster_info->entity->IsDying()) {
        monster_info->startCycle(W8_MONSTER_CYCLE_DIE, W8_ANIMATION_PLAY_ONCE);
        deactivateMonster(monster_info);
        recordMonsterKill(monster_info, display_message);
        retireMonster(monster_info, W8MonsterRemoval::Deactivate);
    }
}

/* The kill bookkeeping a monster's death runs: name the kill on the notice
   channel the killer's kind selects, clear the conditions the dead monster
   sourced, and - unless the record opts out - land the faction hit and bank
   the kill count and experience for a monster that fought the party. */
void Game::recordMonsterKill(W8MonsterInfo* monster_info, bool announce)
{
    auto& world = monster_info->owner_world;
    int killer_party_slot = -1;
    assert(monster_info != 0);
    unsigned int monster_species = monster_info->monster_species;
    const W8MonsterRecord* record = &data.monsters[monster_species];
    unsigned int notice_channel;
    if (TargetSourceIsCharacter(&monster_info->condition_target, 0)) {
        killer_party_slot = monster_info->condition_target.iChar;
        assert(killer_party_slot == -1 ||
               (killer_party_slot >= 0 && killer_party_slot < W8_PARTY_SLOT_COUNT &&
                party.members[killer_party_slot].has_value() &&
                party.characters[killer_party_slot].fInParty));
        notice_channel = (killer_party_slot == -1) + 8;
    } else {
        notice_channel = 9;
    }
    if (announce && !monster_info->death_processed &&
        monster_info->party_threat.sight_state != W8_SIGHT_UNSEEN) {
        events.push(
            TextNotice{notice_channel,
                       fmt::format("{} {}!", monsterName(*monster_info),
                                   data.strings[kConditionNotices[W8_CONDITION_DEAD].singular])});
    }
    releaseMonsterDependents(*monster_info);
    if (monster_info->summoned == W8_MONSTER_SUMMON_FRIENDLY) {
        return;
    }
    if (monster_info->death_processed) {
        return;
    }
    if (killer_party_slot == -1) {
        if (!TargetSourceIsMonster(&monster_info->condition_target, 0)) {
            goto done;
        }
        W8MonsterInfo* killer_info =
            world.population->findMonster(monster_info->condition_target.iMonsterID);
        if (killer_info == nullptr) {
            goto done;
        }
        assert(killer_info != 0);
        if (killer_info->ubDisposition != W8_DISPOSITION_FRIENDLY) {
            goto done;
        }
    }
    if (record->faction_id != W8_FACTION_UNALIGNED && record->faction_id != W8_FACTION_PARTY)
        recordFactionOffense(static_cast<signed char>(record->faction_id), monster_info);
done:
    MonsterKilled(MakeScriptContext(*this), record->record_id, killer_party_slot);
    if (partyCombatActive(world) && monster_info->pCombat &&
        ((monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
          monster_info->uiCondition[W8_CONDITION_TURNCOAT] == 0) ||
         (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY &&
          monster_info->uiCondition[W8_CONDITION_TURNCOAT] != 0))) {
        ++combat->combat_result;
        if (world.level_id >= 0 && world.level_id < W8_LEVEL_COUNT) {
            ++status.level_progress[world.level_id].monster_kill_count;
        }
        if (killer_party_slot != -1) {
            W8Character* killer = &party.characters[killer_party_slot];
            ++killer->kill_count;
            if (record->significant_kill != 0) {
                unsigned int level_total = 0;
                int occupied = 0;
                for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
                    if (party.members[slot].has_value()) {
                        level_total += party.characters[slot].uiExpLevel;
                        ++occupied;
                    }
                }
                int share = occupied == 0
                                ? 1
                                : static_cast<int>(level_total / static_cast<double>(occupied));
                if (share + 2 <= static_cast<int>(record->effective_level)) {
                    speech.Queue(killer, 37, 0, kCharacterEventFullVolume);
                }
            }
        }
        unsigned int experience = record->experience_override;
        if (experience == 0) {
            experience = record->experience;
        }
        combat->experience_pool += experience;
        if (status.status_ints[monster_species] == 0) {
            status.status_ints[monster_species] = 1;
        }
    }
}

/* A species' record, or null for a species the database does not hold. */
const W8MonsterRecord* MonsterDBFromSpecies(unsigned int monster_species)
{
    assert(monster_species < kMaxMonsterSpecies);
    const auto& monsters = Data().monsters;
    return monster_species < monsters.size() ? &monsters[monster_species] : nullptr;
}

W8RangeCategory MonsterAttackRange(const W8MonsterInfo& monster_info, const W8MonsterRecord& record,
                                   unsigned int attack)
{
    if (const auto* combat = monster_info.pCombat.get();
        combat && combat->attack_index == attack && combat->corrected_range)
        return *combat->corrected_range;
    return static_cast<W8RangeCategory>(record.attacks[attack].range_category);
}

const W8MonsterRecord* GetMonsterDataForInfo(W8MonsterInfo* monster_info)
{
    assert(monster_info != 0);
    return MonsterDBFromSpecies(monster_info->monster_species);
}

W8MonsterInfo* FindMonster(int location_id, [[maybe_unused]] bool assert_on_failure)
{
    if (!GetWorld()) return nullptr;
    for (const auto* partition : {&CurrentLevelPopulation().monster_roster.live(),
                                  &CurrentLevelPopulation().monster_roster.unborn()}) {
        for (const auto& candidate : *partition) {
            if (candidate->location_id == location_id) {
                return candidate.get();
            }
        }
    }
    assert(!assert_on_failure);
    return nullptr;
}

const W8MonsterRecord* GetMonsterDataByLocationID(int location_id)
{
    W8MonsterInfo* monster;

    monster = FindMonster(location_id);
    if (monster == 0) {
        return 0;
    }
    return MonsterDBFromSpecies(monster->monster_species);
}

Monster* GetMonsterByLocationID(int location_id)
{
    if (location_id != -1) {
        if (W8MonsterInfo* monster_info = FindMonster(location_id, false)) {
            return monster_info->entity.get();
        }
    }
    return 0;
}

/* Scale the monster database's combat movement range by the shared factor at
   0x005ED4F0 before range/path consumers use it. */
float GetMonsterCombatMoveRange(W8MonsterInfo* monster_info)
{
    const W8MonsterRecord* record;
    float result;

    assert(monster_info != 0);
    record = MonsterDBFromSpecies(monster_info->monster_species);
    assert(record != 0);
    result = record->combat_move_range * g_monster_record_float_scale;
    return result;
}

void Monster::updateDamageAppearance(unsigned int hp, int maximum)
{
    if (damage_stage_count <= 1) return;
    const int stage = ((maximum - static_cast<int>(hp)) * damage_stage_count) / maximum;
    visual.damage_stage = std::min(stage, damage_stage_count - 1);
}

void ProcessMonstersAtCombatEnd(bool forced_cleanup)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        if (monster_info->fActive && monster_info->hp_current > 0 &&
            monster_info->uiCondition[W8_CONDITION_DEAD] == 0 &&
            monster_info->summoned != W8_MONSTER_SUMMON_NONE) {
            if (!forced_cleanup) {
                ShowNotice(W8_FONT_PALETTE_RUST,
                           fmt::sprintf(Data().strings[W8_NOTICE_MONSTER_SLAIN],
                                        CurrentGame().monsterName(*monster_info)),
                           0);
            }
            CurrentGame().releaseMonsterDependents(*monster_info);
            if (!forced_cleanup) {
                monster_info->death_processed = true;
                CurrentGame().startMonsterDeath(monster_info, true);
            }
        }
    }
}

W8MonsterInfo* FindMonsterInfoBySpecies(unsigned int monster_species)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->monster_species == monster_species) {
            return monster_info;
        }
    }
    return 0;
}

void World::resumeMonstersAfterCombat()
{
    for (const auto& monster : population->monster_roster.live()) {
        if (monster->hp_current == 0) continue;
        monster->entity->nav.finishCombat();
        if (monster->ai_mode > W8_RT_AI_IDLE && monster->ai_mode <= W8_RT_AI_APPROACH_PARTY)
            monster->ai_mode = W8_RT_AI_IDLE;
    }
}

void DestroyUngroupedMonsters(void)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        if (monster_info->monster_group_id == 0) {
            Monster* monster = monster_info->entity.get();

            if (monster != 0) {
                if ((monster->runtime_flags & W8_MONSTER_REMOVE_AFTER_FADE) != 0 &&
                    monster->removal_state != W8_MONSTER_REMOVAL_NONE) {
                    monster->ApplyRemovalStateEffects();
                }
            }
            CurrentGame().retireMonster(monster_info, W8MonsterRemoval::Despawn);
            --index;
        }
    }
}

bool W8MonsterInfo::heal(unsigned int amount)
{
    if (hp_current == 0 || hp_current == static_cast<unsigned int>(uiHPMax) || amount == 0)
        return false;
    hp_current += amount;
    hp_current = std::min(hp_current, static_cast<unsigned int>(uiHPMax));
    if (entity) entity->updateDamageAppearance(hp_current, uiHPMax);
    return true;
}

std::optional<unsigned int> W8MonsterInfo::absorbDamage(unsigned int amount)
{
    auto& shield = enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL];
    if (shield.turns == 0) return std::nullopt;
    const auto absorbed = std::min(amount, static_cast<unsigned int>(shield.magnitude));
    shield.magnitude = static_cast<unsigned short>(shield.magnitude - absorbed);
    return absorbed;
}

bool W8MonsterInfo::takeDamage(unsigned int amount, const W8TargetSource& source, bool poison)
{
    if (hp_current == 0 || amount == 0) return false;
    if (!source.fBackfire && !source.fReflection && !source.target_diverted && !poison) {
        condition_target = source;
        if (pCombat && TargetSourceIsCharacter(&source, 0) && source.iChar != -1)
            pCombat->character_hate[source.iChar] += amount;
    }
    hp_current -= std::min(amount, hp_current);
    return hp_current == 0;
}

void W8MonsterInfo::setControlState(W8MonsterControlState next)
{
    switch (next) {
    case W8_MONSTER_CONTROL_NONE:
    case W8_MONSTER_CONTROL_RESISTED:
        if (control_state == W8_MONSTER_CONTROL_LURED && entity->nav.linked_navigator == 0) {
            entity->nav.ClearMovement();
            ai_mode = W8_RT_AI_IDLE;
        }
        break;
    default:
        break;
    }
    control_state = next;
}

void W8MonsterInfo::setMotionless(bool motionless)
{
    const bool previous = fMotionless;
    fMotionless = motionless;
    if (!motionless) {
        if (previous) {
            entity->SetAnimating(true);
            if (entity->visual.pending_cycle == W8_MONSTER_CYCLE_NONE) {
                startCycle(W8_MONSTER_CYCLE_IDLE, W8_ANIMATION_NEVER_STOP);
            }
        }
    } else if (!previous) {
        const auto cycle = entity->visual.pending_cycle;
        if (cycle != W8_MONSTER_CYCLE_NONE) {
            if (cycle == W8_MONSTER_CYCLE_GET_HIT) return;
            entity->SetPendingCycle(W8_MONSTER_CYCLE_NONE);
        }
        entity->SetAnimating(false);
    }
}

void W8MonsterInfo::initializeVitals(const W8MonsterRecord& record, Rng& rng)
{
    hp_current = uiHPMax = RollDice(rng, record.hit_points);
    stamina = stamina_max = RollDice(rng, record.stamina_dice);
    fatigue_band = FatigueBand(stamina, stamina_max);
}

// First-visit rolls remain distinct from body preparation and saved restoration.
void World::initializeMonsterVitals(Rng& rng)
{
    for (const auto* partition :
         {&population->monster_roster.live(), &population->monster_roster.unborn()}) {
        for (const auto& info : *partition) {
            const auto& record = info->entity->species();
            info->initializeVitals(record, rng);
            info->scale = RollMonsterScale(*info, record, rng);
            info->entity->scale = info->scale;
            info->entity->ApplyRepresentationScale();
            info->entity->RefreshStandingHeight();
        }
    }
}

// FUNCTION: WIZ8 0x004e67a0
static void TryStartMonsterCycle2(W8MonsterInfo* monster_info, W8MonsterCycle query_state)
{
    if (monster_info->fActive && monster_info->hp_current > 0 &&
        monster_info->uiCondition[W8_CONDITION_DEAD] == 0 &&
        monster_info->within_viewing_distance && query_state == W8_MONSTER_CYCLE_IDLE) {
        Monster& monster = *monster_info->entity;
        int result = monster.Query(W8_MONSTER_QUERY_AT_PLAYBACK_END);

        if (result != 0 && !monster_info->fMotionless) {
            monster.runtime_flags |= 0x80;
            if (monster.IsCycleSupported(2)) {
                signed char cycle = monster.visual.pending_cycle;

                if (cycle == 2 || !monster.IsCycleInterruptable(cycle) ||
                    (CurrentGame().combat.has_value() &&
                     CurrentGame().combat->holdsTurn(MonsterActor{monster_info->location_id}))) {
                    return;
                }
                W8MonsterGroup* group = FindMonsterGroup(monster_info->monster_group_id);
                unsigned int chance = group->MemberCount() * 20;

                if (!CurrentGame().activity.conversation && CurrentGame().rng.below(chance) == 0) {
                    monster_info->startCycle(W8_MONSTER_CYCLE_SPICE, W8_ANIMATION_PLAY_ONCE);
                }
            }
        }
    }
}

/* Return the monster experience value exposed by Monster Info. Retail prefers
   the optional +0x26b override and otherwise uses the +0x181 base. The same
   effective value is also used as an HP-weighted combat-difficulty proxy. */
unsigned int GetMonsterExperience(const W8MonsterRecord* record)
{
    unsigned int value = record->experience_override;

    if (value <= 0) {
        value = record->experience;
    }
    return value;
}

bool AnyMonsterDying(void)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info != 0 && monster_info->entity->IsDying()) {
            return true;
        }
    }
    return false;
}

bool Game::retireMonster(W8MonsterInfo* monster_info, W8MonsterRemoval removal)
{
    if (!monster_info) return false;
    auto& world = monster_info->owner_world;

    if (partyCombatActive(world) && monster_info->pCombat) {
        removeMonsterFromCombat(*monster_info);
    }
    if (monster_info->monster_group_id != 0) {
        W8MonsterGroup* monster_group = world.population->findGroup(monster_info->monster_group_id);

        wiz8::erase_first(monster_group->monsters, monster_info->location_id);
        if (monster_group->highlighted_member == monster_info->location_id)
            monster_group->highlighted_member = -1;

        events.push(MonstersChanged{});
        if (monster_group->MemberCount() == 0) {
            if (monster_group->members_active) {
                invalidateAimsAt(&world, W8CombatSlot::group(monster_group->group_id),
                                 TargetChange::removed);
                monster_group->members_active = false;
            }
            auto& population = *world.population;
            if (monster_group->leader_group_id != 0) {
                population.unlinkGroup(*monster_group);
            } else {
                if (monster_group->encounter_registered && partyCombatActive(world)) {
                    auto& budget = world.encounters.random_budget;
                    const auto& definition = data.levels[world.level_id];
                    budget = std::clamp(budget - 1, definition.minimum_encounter_budget,
                                        definition.maximum_encounter_budget);
                }
                const auto allies = std::to_array(monster_group->allied_group_ids);
                auto* leader = population.alliedSuccessor(*monster_group);
                if (leader) {
                    linkMonsterGroup(world, nullptr, leader);
                    for (int id : allies)
                        if (id != 0 && id != leader->group_id)
                            linkMonsterGroup(world, leader, population.findGroup(id));
                    population.findMonster(leader->leader_location_id)
                        ->inheritLeaderState(*monster_info);
                }
            }
            population.eraseEmptyGroup(*monster_group);
        } else {
            if (monster_group->leader_location_id == monster_info->location_id) {
                world.replaceGroupLeader(*monster_group, partyCombatActive(world));
            }
        }
        monster_info->monster_group_id = 0;
    }
    deactivateMonster(monster_info);
    if (partyCombatActive(world)) {
        combat->endTurnOf(MonsterActor{monster_info->location_id});
    }
    if (removal == W8MonsterRemoval::Deactivate) return true;
    auto binding = monster_info->npc_binding_;
    world.eraseMonster(*monster_info, removal == W8MonsterRemoval::Despawn);
    if (binding && !binding->binding_unavailable) binding->releaseWorldBinding();
    return true;
}

// Logical death stops participation; the loaded body remains for death, script and fade work.
bool W8MonsterInfo::deactivate()
{
    if (!fActive) return false;
    assert(entity);
    uiCondition[W8_CONDITION_DEAD] = W8_CONDITION_INDEFINITE;
    highest_condition = W8_CONDITION_DEAD;
    hp_current = 0;
    stamina = 0;
    entity->nav.active = false;
    entity->nav.flags = 0x200000;
    entity->clearSpellIcons();
    owner_world.population->releaseMonsterVisibility(*this);
    position = entity->GetRepresentationLocalLocation();
    fActive = false;
    return true;
}

void Game::deactivateMonster(W8MonsterInfo* monster_info)
{
    if (!monster_info->deactivate()) return;
    if (partyCombatActive(monster_info->owner_world)) {
        refreshSight();
        invalidateAimsAt(&monster_info->owner_world,
                         W8CombatSlot::monster(monster_info->location_id), TargetChange::removed);
        RefreshFlaggedMainGameState();
        refreshCombatEngagement();
        combat->endTurnOf(MonsterActor{monster_info->location_id});
    }
}

/* Prepare fresh combat state and reset the monster's target before it joins combat. */
void MonsterInfoEnterCombat(W8MonsterInfo* monster_info)
{
    assert(monster_info != 0);
    assert(!monster_info->pCombat);
    ClearMonsterPathAndResume(monster_info);
    monster_info->pCombat = std::make_unique<W8MonsterCombatState>();
    if (monster_info->player_visibility.sight_state == W8_SIGHT_UNSEEN) {
        monster_info->player_visibility.sight_state = W8_SIGHT_RECENT;
        monster_info->player_visibility.last_seen_clock = CurrentGame().status.world_clock;
    }
    ResetCombatSlot(&monster_info->Target);
    monster_info->entity->nav.flags = 0;
    if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE) {
        CurrentGame().refreshCombatEngagement();
    }
    if (CurrentGame().combat.has_value()) {
        EndMonsterTurn(monster_info);
    }
}

void W8MonsterInfo::endEngagement(const GameData& data)
{
    assert(pCombat);
    for (auto& effect : pCombat->combat_effects)
        if (effect.active) clearEffect(effect, data.monsters[monster_species]);
    for (auto& effect : pCombat->combat_effects_2)
        if (effect.active) clearEffect(effect, data.monsters[monster_species]);
    pCombat.reset();
}

void Game::removeMonsterFromCombat(W8MonsterInfo& monster)
{
    std::vector<Event> outcomes;
    removeMonsterFromCombat(monster, outcomes);
    events.publish(std::move(outcomes));
}

void Game::removeMonsterFromCombat(W8MonsterInfo& monster, std::vector<Event>& outcomes)
{
    assert(combat);
    combat->endTurnOf(MonsterActor{monster.location_id});
    monster.endEngagement(data);
    if (monster.ubDisposition == W8_DISPOSITION_HOSTILE) refreshCombatEngagement(outcomes);
}

/* Toggles the party's combat-ready posture. Outside combat it only flips the
   flag; inside it also swaps the two engine bits at +0x001 and +0xa62 of the
   state block, and in engine mode 7 it announces the new posture with the
   message the block at 0x0068C09C holds for that direction. */
void TogglePartyCombatStance(void)
{
    std::string message;

    if (!GameStarted()) {
        g_settings.continuous_combat = (!g_settings.continuous_combat);
        return;
    }
    if (g_settings.continuous_combat) {
        g_settings.continuous_combat = false;
        if (CurrentGame().combat.has_value()) {
            CurrentGame().combat->round_active = (CurrentGame().combat->execution_active == 0);
            CurrentGame().combat->combat_ready = true;
        }
        if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
            return;
        }
        message = Data().strings[W8_NOTICE_COMBAT_STANCE_RELAXED];
    } else {
        g_settings.continuous_combat = true;
        if (CurrentGame().combat.has_value() && CurrentGame().combat->combat_ready) {
            CurrentGame().combat->round_active = true;
            CurrentGame().combat->combat_ready = false;
        }
        if (CurrentScreenState().id != W8_SCREEN_MAIN_GAME) {
            return;
        }
        message = Data().strings[W8_NOTICE_COMBAT_STANCE_READY];
    }
    ShowNotice(W8_FONT_PALETTE_BEIGE, message);
    CurrentGame().events.push(CombatStanceChanged{});
}

/* The combat toggle the interface drives: out of combat it starts one, and in
   combat it refuses to end it while any loaded group still qualifies, while any
   character is still engaged, or while the state block's leading flag is
   raised, announcing the reason in each case. Only when all three clear does it
   end combat and, if the level record's field at +0x2ca is not -1, run the
   trailing notification. */
void ToggleCombatMode(void)
{
    unsigned int group_list_index;
    W8MonsterGroup* monster_group;

    if (!CurrentGame().combat.has_value()) {
        CurrentGame().beginCombat(1);
        return;
    }
    if (CurrentLevelPopulation().monster_roster.hostileCombatCount() != 0 ||
        CurrentGame().combat->enemies_engaged) {
        if (CurrentGame().combat->round_count != 0) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_CANNOT_END]);
            return;
        }
        for (group_list_index = 0; group_list_index < CurrentLevelPopulation().groups().size();
             ++group_list_index) {
            monster_group = CurrentLevelPopulation().groups()[group_list_index].get();
            if (monster_group->members_active && monster_group->fInCombat &&
                MonsterGroupCanEngage(monster_group)) {
                ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_CANNOT_END]);
                return;
            }
        }
    }
    for (const auto& missile : CurrentGame().level.loaded().missiles()) {
        if ((missile.get() == CurrentGame().combat->engaged_missile ||
             Data().missiles[missile->missile_table_index].spell_missile != 0) &&
            missile->BlocksEndingCombat()) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_CANNOT_END_ENGAGED]);
            return;
        }
    }
    if (CurrentGame().combat->execution_active != 0) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_CANNOT_END_PENDING],
                   W8_NOTICE_TEXT_BOX_AUTOMATIC, W8_NOTICE_WRAP_AUTOMATIC, true);
        return;
    }
    CurrentGame().endCombat(false);
    ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[W8_NOTICE_COMBAT_ENDED]);
    CurrentGame().events.push(CombatCalledOff{});
}

// A hit reaction may finish while immobilized; ordinary movement must wait.
void W8MonsterInfo::startCycle(int cycle, W8AnimationBehaviour behavior)
{
    auto& monster = *entity;
    if (behavior == W8_ANIMATION_NEVER_STOP &&
        !monster.IsCycleInterruptable(static_cast<signed char>(cycle))) {
        assert(false && "NEVER_STOP needs an interruptable cycle");
        return;
    }
    const auto pending = monster.visual.pending_cycle;
    if (pending != W8_MONSTER_CYCLE_GET_HIT && pending != W8_MONSTER_CYCLE_NONE &&
        !monster.IsCycleInterruptable(pending)) {
        if (static_cast<signed char>(cycle) == W8_MONSTER_CYCLE_GET_HIT) return;
        assert(false && "an uninterruptable cycle is pending");
        return;
    }
    if (static_cast<signed char>(cycle) != W8_MONSTER_CYCLE_GET_HIT &&
        static_cast<signed char>(cycle) != W8_MONSTER_CYCLE_DIE &&
        static_cast<signed char>(cycle) != 0 && fMotionless) {
        assert(false && "a motionless monster can only be born, react to a hit or die");
        return;
    }
    monster.SetAnimating(true);
    monster.SetRuntimeBehaviour(behavior);
    monster.SetPendingCycle(cycle);
    monster.visual.pending_subcycle = 0;
    monster.visual.SubmitTargetValue();
}

/* Advance every live monster once. Removal is done in place, so each branch
   that shortens the PList decrements the unsigned index before the common
   increment. A delayed-removal monster is first detached from combat and the
   world; an ordinary monster advances its cycle state or stops animating while
   motionless. */
void ProcessMonsterManagerFrame(void)
{
    unsigned int monster_list_index;

    for (monster_list_index = 0;
         monster_list_index < CurrentLevelPopulation().monster_roster.live().size();
         ++monster_list_index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(monster_list_index);
        Monster& monster = *monster_info->entity;

        if ((monster.runtime_flags & W8_MONSTER_REMOVE_AFTER_FADE) != 0) {
            if (monster.fade_state == W8_MONSTER_FADE_IDLE) {
                if (monster.removal_state != W8_MONSTER_REMOVAL_NONE) {
                    monster.ApplyRemovalStateEffects();
                }
                CurrentGame().retireMonster(monster_info, W8MonsterRemoval::Despawn);
                --monster_list_index;
            }
        } else if ((monster.runtime_flags & (W8_MONSTER_REMOVE_NOW | W8_MONSTER_SCRIPT_WAIT)) !=
                   0) {
            CurrentGame().retireMonster(monster_info, W8MonsterRemoval::Despawn);
            --monster_list_index;
        } else {
            if ((monster.runtime_flags & W8_MONSTER_PARKED) == 0) {
                const auto query_state =
                    static_cast<W8MonsterCycle>(monster.Query(W8_MONSTER_QUERY_CYCLE));
                TryStartMonsterCycle2(monster_info, query_state);
                if (monster.Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                    if (CurrentGame().combat.has_value() &&
                        query_state == W8_MONSTER_CYCLE_ATTACK_SPECIAL) {
                        monster_info->pCombat->special_ready = true;
                    }
                    switch (query_state) {
                    case W8_MONSTER_CYCLE_IDLE:
                    case W8_MONSTER_CYCLE_TRANSITION:
                    case W8_MONSTER_CYCLE_WALK:
                    case W8_MONSTER_CYCLE_TALK_SPICE:
                    case W8_MONSTER_CYCLE_TALK:
                        break;
                    case W8_MONSTER_CYCLE_DIE:
                        if ((monster.runtime_flags & W8_MONSTER_REMOVE_AFTER_FADE) == 0) {
                            monster_info = GetMonsterListEntry(monster_list_index);
                            HandleScriptedNpcDeath(MakeScriptContext(CurrentGame()),
                                                   monster_list_index);
                            if (monster_info->monster_group_id != 0) {
                                CurrentGame().retireMonster(monster_info,
                                                            W8MonsterRemoval::Deactivate);
                            }
                            DropMonsterLoot(monster_info, -1);
                            monster_info->entity->BeginDelayedRemoval();
                        }
                        break;
                    default:
                        if (!monster_info->fMotionless) {
                            if (monster.visual.pending_cycle == W8_MONSTER_CYCLE_NONE) {
                                monster_info->startCycle(W8_MONSTER_CYCLE_IDLE,
                                                         W8_ANIMATION_NEVER_STOP);
                            }
                        } else if (monster.IsAnimating() != 0) {
                            monster.SetAnimating(false);
                        }
                        break;
                    }
                }
            }
        }
    }
}

void PostMonsterNotice(W8MonsterInfo* monster_info, std::string_view text)
{
    const std::string_view separator =
        (text.starts_with("'") || text.starts_with(":")) ? std::string_view{} : g_W8TextSeparator;
    CurrentGame().events.push(TextNotice{
        W8_FONT_PALETTE_RUST,
        fmt::format("{}{}{}", CurrentGame().monsterName(*monster_info), separator, text)});
}

std::string Game::monsterName(const W8MonsterRecord& record, bool alternate, bool plural) const
{
    if (record.record_id == W8_MONSTER_RECORD_ALTERNATE_NAME)
        return fmt::format("Al-{}", party.characters[status.sedexus_party_slot].name);
    if (alternate) return plural ? record.name1 : record.name0;
    return plural ? record.name3 : record.name2;
}

std::string Game::monsterName(const W8MonsterInfo& monster) const
{
    const auto& record = data.monsters[monster.monster_species];
    bool alternate = false;
    if (record.record_id != W8_MONSTER_RECORD_ALTERNATE_NAME && monster.monster_group_id != 0) {
        const auto* group = monster.owner_world.population->findGroup(monster.monster_group_id);
        assert(group);
        alternate = group->alternate_name;
    }
    return monsterName(record, alternate, false);
}

std::string Game::monsterGroupName(const W8MonsterGroup& group) const
{
    return monsterName(data.monsters[group.monster_id], group.alternate_name,
                       group.MemberCount() != 1);
}

float GetAveragePartyMemberLevel(void)
{
    float total = 0.0f;
    float count = 0.0f;
    for (int party_slot = 0; party_slot < 6; ++party_slot) {
        if (CurrentGame().party.members[party_slot].has_value()) {
            total += CurrentGame().party.characters[party_slot].uiExpLevel;
            count += 1.0f;
        }
    }
    if (count == 0.0f) return 1.0f;
    return total / count;
}

unsigned int GetBestPartySkillLevel(W8Skill skill_index, int* party_slot)
{
    unsigned int best_level = 0;
    int best_slot = -1;
    for (int index = 0; index < 8; ++index) {
        W8Character* character = &CurrentGame().party.characters[index];
        if (CurrentGame().party.members[index].has_value() && character->hp_current != 0 &&
            character->highest_condition < W8_CONDITION_TURNCOAT &&
            (character->skills[skill_index].level > best_level || best_slot == -1)) {
            best_level = character->skills[skill_index].level;
            best_slot = index;
        }
    }
    if (party_slot != 0) *party_slot = best_slot;
    return best_level;
}

/* Format the health knowledge the party has earned for one monster. NPC-backed
   records can suppress exact values, and ordinary monsters expose current and
   maximum HP independently at knowledge thresholds ten and five. */
std::string FormatMonsterHealth(W8MonsterInfo* monster_info)
{
    bool suppress_exact_health = false;
    unsigned int health_knowledge;

    if (monster_info->ubDisposition != W8_DISPOSITION_HOSTILE) {
        const W8MonsterRecord* record;
        W8NpcState* npc;

        record = GetMonsterDataForInfo(monster_info);
        if ((record->flags & W8_MONSTER_FLAG_NPC) != 0) {
            npc = CurrentGame().story.npcOfKind(record->npc_kind);
            if (npc != 0 && npc->record->has_group != 0) {
                suppress_exact_health = true;
            }
        }
    }

    if (monster_info->summoned == W8_MONSTER_SUMMON_FRIENDLY) {
        health_knowledge = 125;
    } else {
        float average_party_level = GetAveragePartyMemberLevel();
        const W8MonsterRecord* record;
        int best_party_slot;
        int monster_level;

        record = GetMonsterDataForInfo(monster_info);
        monster_level = record->display_level;
        health_knowledge = GetBestPartySkillLevel(W8_SKILL_MYTHOLOGY, &best_party_slot);
        if (static_cast<int>(average_party_level) < monster_level) {
            float adjusted_knowledge =
                health_knowledge - (monster_level - average_party_level) * 3.0f + 0.5f;
            if (adjusted_knowledge < 0.0f) {
                adjusted_knowledge = 0.0f;
            }
            health_knowledge = static_cast<unsigned int>(adjusted_knowledge);
        }
    }

    const auto current = health_knowledge < 10 || suppress_exact_health
                             ? "?"
                             : fmt::format("{:d}", monster_info->hp_current);
    const auto maximum = health_knowledge < 5 || suppress_exact_health
                             ? "?"
                             : fmt::format("{:d}", monster_info->uiHPMax);
    return fmt::format("{}/{}", current, maximum);
}

/* Blind, incapacitated and unoccupied party members cannot sense monsters. */
static bool CanNoticeMonsterGroup(unsigned int slot)
{
    const W8Character& character = CurrentGame().party.characters[slot];
    return CurrentGame().party.members[slot].has_value() && character.hp_current != 0 &&
           character.highest_condition < W8_CONDITION_INSANE &&
           character.uiCondition[W8_CONDITION_BLIND] == 0;
}

void DetectMonsterGroups(void)
{
    bool noticed = false;

    for (unsigned int group_index = 0; group_index < CurrentLevelPopulation().groups().size();
         ++group_index) {
        W8MonsterGroup* group = CurrentLevelPopulation().groups()[group_index].get();
        if (!group->members_active || (group->alternate_name && group->group_state[0] != 0) ||
            (CurrentGame().combat.has_value() && !group->fInCombat) ||
            !MonsterGroupHasRenderableMember(group, false)) {
            continue;
        }
        if (!group->alternate_name) {
            const W8MonsterRecord* record = MonsterGroupGetRecord(group);
            int best_slot = -1;
            unsigned int best_margin = 0;
            for (unsigned int slot = 0; slot < 8; ++slot) {
                if (!CanNoticeMonsterGroup(slot)) continue;
                W8Character* character = &CurrentGame().party.characters[slot];
                int score =
                    (character->monster_awareness[group->monster_id] - record->effective_level) *
                        3 +
                    character->attributes[W8_ATTRIBUTE_INTELLIGENCE].effective / 10 +
                    character->skills[W8_SKILL_MYTHOLOGY].level;
                int roll = CurrentGame().rng.below(100);
                if (roll >= score) {
                    continue;
                }
                unsigned int margin = score - roll;
                if (character->skills[W8_SKILL_MYTHOLOGY].active) {
                    PracticeCharacterSkill(CurrentGame(), character, W8_SKILL_MYTHOLOGY, 10, false);
                }
                if (margin > best_margin) {
                    best_slot = slot;
                    best_margin = margin;
                }
            }
            if (best_slot != -1) {
                PostCharacterNotice(
                    best_slot,
                    fmt::sprintf(Data().strings[0x1ca], CurrentGame().monsterGroupName(*group)));
                group->alternate_name = true;
                bool vowel;
                switch (toupper(
                    static_cast<unsigned char>(CurrentGame().monsterGroupName(*group).front()))) {
                case 'A':
                case 'E':
                case 'I':
                case 'O':
                case 'U':
                    vowel = true;
                    break;
                default:
                    vowel = false;
                    break;
                }
                std::string article;
                if (group->MemberCount() == 1) {
                    article = vowel ? Data().strings[0x1cc] : Data().strings[0x1cb];
                } else {
                    article = Data().strings[0x1cd];
                }
                ShowNotice(W8_FONT_PALETTE_WHITE,
                           fmt::format("{} {}!", article, CurrentGame().monsterGroupName(*group)));
                noticed = true;
            }
        }
        if (group->group_state[0] == 0) {
            for (unsigned int slot = 0; slot < 8; ++slot) {
                if (!CanNoticeMonsterGroup(slot)) continue;
                W8Character* character = &CurrentGame().party.characters[slot];
                unsigned int awareness =
                    character->monster_awareness[group->monster_id] + group->MemberCount();
                if (awareness > 0xff) {
                    awareness = 0xff;
                }
                character->monster_awareness[group->monster_id] =
                    static_cast<unsigned char>(awareness);
            }
            group->group_state[0] = 1;
        }
    }
    if (noticed) {
        CurrentGame().events.push(MonstersChanged{});
    }
}

void EvaluateCombatDifficulty(void)
{
    unsigned int hostile_experience = 0;
    unsigned int eligible_count = 0;
    unsigned int party_levels = 0;
    unsigned int monster_index;
    int slot;

    for (monster_index = 0; monster_index < CurrentLevelPopulation().monster_roster.live().size();
         ++monster_index) {
        W8MonsterInfo* monster = GetMonsterListEntry(monster_index);
        if (monster->pCombat && monster->ubDisposition == W8_DISPOSITION_HOSTILE &&
            monster->hp_current > 0 && monster->highest_condition < W8_CONDITION_DEAD) {
            unsigned int health_percent = monster->hp_current * 100 / monster->uiHPMax;
            hostile_experience +=
                GetMonsterExperience(GetMonsterDataForInfo(monster)) * health_percent / 100;
        }
    }
    unsigned int threat_level = EstimateCombatThreatLevel(hostile_experience);
    if (threat_level == 0) {
        threat_level = 1;
    }

    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (!CurrentGame().party.participates(slot)) {
            continue;
        }
        bool count_character = true;
        if (slot < 2) {
            W8NpcState* npc = GetNpcState(CurrentGame().party.npcIndex(slot));
            if (npc != 0) {
                int faction = npc->record->faction;
                if (faction != 0 && faction != 1) {
                    for (unsigned int group_index = 0;
                         group_index < CurrentLevelPopulation().groups().size(); ++group_index) {
                        W8MonsterGroup* group =
                            CurrentLevelPopulation().groups()[group_index].get();
                        if (group->members_active && group->fInCombat &&
                            group->ubDisposition == W8_DISPOSITION_HOSTILE &&
                            MonsterGroupGetRecord(group)->faction_id == faction) {
                            count_character = false;
                            break;
                        }
                    }
                }
            }
        }
        if (count_character) {
            ++eligible_count;
            party_levels += character->uiExpLevel;
        }
    }

    unsigned int party_power = 0;
    if (eligible_count > 0) {
        party_power = static_cast<unsigned int>(
            party_levels / pow(static_cast<double>(eligible_count) * 6.0f, 0.5) + 0.5);
    }
    int relative_strength = static_cast<int>(party_power * 100 / threat_level) - 100;
    W8Difficulty difficulty;
    if (relative_strength >= 20) {
        difficulty = W8_DIFFICULTY_NOVICE;
    } else if (relative_strength <= -20) {
        difficulty = W8_DIFFICULTY_EXPERT;
    } else {
        difficulty = W8_DIFFICULTY_NORMAL;
    }

    switch (difficulty) {
    case W8_DIFFICULTY_NOVICE:
        StartMusicResource("CombatEasy.MPL", 0, 1);
        break;
    case W8_DIFFICULTY_NORMAL:
        StartMusicResource("Combat.MPL", 0, 1);
        break;
    case W8_DIFFICULTY_EXPERT:
        StartMusicResource("CombatLousy.MPL", 0, 1);
        break;
    }
    if ((CurrentGame().combat.has_value() && CurrentGame().combat->party_surprised) ||
        CurrentGame().time >= CurrentGame().recent_combat.until ||
        difficulty != CurrentGame().recent_combat.difficulty) {
        unsigned int event_type;
        switch (difficulty) {
        case W8_DIFFICULTY_NOVICE:
            event_type = 29;
            break;
        case W8_DIFFICULTY_NORMAL:
            event_type = 30;
            break;
        default:
            event_type = 31;
            break;
        }
        CurrentGame().speech.QueueRandom(event_type, -1, 0);
    }
    ++CurrentGame().status.combat_difficulty_counts[difficulty];
    CurrentGame().recent_combat.difficulty = difficulty;

    for (slot = 0; slot < 2; ++slot) {
        if (CurrentGame().party.members[slot].has_value()) {
            W8NpcState* npc = GetNpcState(CurrentGame().party.npcIndex(slot));
            if (npc != 0) {
                W8Character* character = &CurrentGame().party.characters[slot];
                if (character->highest_condition == W8_CONDITION_DEAD) {
                    npc->item_assist = 0;
                } else if (npc->name_style == W8_NPC_VI_DOMINA ||
                           npc->name_style == W8_NPC_DRAZIC || npc->name_style == W8_NPC_RODAN) {
                    npc->healer_assist = 1;
                } else {
                    npc->item_assist = 1;
                }
            }
        }
    }
    for (slot = 0; slot < 8; ++slot) {
        W8Character* character = &CurrentGame().party.characters[slot];
        if (CurrentGame().party.members[slot].has_value() &&
            character->uiCondition[W8_CONDITION_DEAD] == 0) {
            CurrentGame().monster_manager_entries[slot].condition_19_latch =
                character->uiCondition[W8_CONDITION_MISSING] != 0;
        }
    }
}

W8MonsterInfo* GetMonsterListEntry(std::ptrdiff_t index)
{
    if (!GetWorld() || index < 0 ||
        std::cmp_greater_equal(index, CurrentLevelPopulation().monster_roster.live().size())) {
        return nullptr;
    }
    return CurrentLevelPopulation().monster_roster.live()[index].get();
}

W8MonsterInfo* GetUnbornMonsterEntry(int index)
{
    if (!GetWorld() || index < 0 ||
        std::cmp_greater_equal(index, CurrentLevelPopulation().monster_roster.unborn().size())) {
        return nullptr;
    }
    return CurrentLevelPopulation().monster_roster.unborn()[index].get();
}
