#include "wiz8/local_code/MonsterGroup.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/monster_generators.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/story.h"
#include "wiz8/utility.h"
#include "wiz8/world/world.h"

constexpr float kMonsterScatterHeadingBias = 0.8f;
constexpr float kMonsterScatterHeadingRandomScale = 0.001600000075995922f;

struct W8NpcState;

enum { W8_MONSTER_GROUP_ALLY_COUNT = 4 };

bool Game::placeMonsterGroup(W8MonsterGroup* group, const glm::vec3* position, float yaw,
                             bool proximity_check, bool include_allies, bool flatten_y,
                             bool alternate_radius)
{
    if (!group->members_active) {
        return false;
    }
    W8MonsterInfo* leader = level.loaded().population->findMonster(group->leader_location_id);
    float radius = alternate_radius ? leader->entity->nav.movement.alternate_radius
                                    : leader->entity->nav.radius;
    int location_ids[45];
    std::vector<glm::vec3> positions;
    unsigned int count = group->MemberCount();
    unsigned int index;
    for (index = 0; index < count; ++index) {
        location_ids[index] = group->MemberLocationId(index);
    }
    if (include_allies) {
        for (int allied_group_id : group->allied_group_ids) {
            if (allied_group_id != 0) {
                W8MonsterGroup* ally = level.loaded().population->findGroup(allied_group_id);
                W8MonsterInfo* ally_leader =
                    level.loaded().population->findMonster(ally->leader_location_id);
                float ally_radius = alternate_radius
                                        ? ally_leader->entity->nav.movement.alternate_radius
                                        : ally_leader->entity->nav.radius;
                if (radius < ally_radius) {
                    radius = ally_radius;
                }
                for (index = 0; index < ally->MemberCount(); ++index) {
                    location_ids[count++] = ally->MemberLocationId(index);
                }
            }
        }
    }
    if (proximity_check) {
        for (index = 0; index < count; ++index) {
            level.loaded().compiledIndex()->UnregisterLocationObjects(
                static_cast<short>(location_ids[index]));
        }
    }
    glm::vec3 target = *position;
    if (alternate_radius) {
        positions = level.loaded()
                        .compiledIndex()
                        ->navigatorPositions(target, yaw, radius + radius, count, proximity_check,
                                             flatten_y, false, 5, true,
                                             (&level.loaded())->party.collisionSphere())
                        .positions;
    } else {
        positions = level.loaded().compiledIndex()->scatterPositions(
            target, yaw, radius + radius, count, proximity_check, flatten_y,
            BoundingSphere{(&level.loaded())->party.position(), (&level.loaded())->party.radius()});
    }
    const auto found = positions.size();
    if (found == 0) {
        return false;
    }
    if (proximity_check) {
        while (found < count) {
            --count;
            this->retireMonster(level.loaded().population->findMonster(location_ids[count]),
                                W8MonsterRemoval::Despawn);
        }
    }
    target = positions[0];
    unsigned int position_index = 0;
    for (index = 0; index < count; ++index) {
        int location_id = location_ids[index];
        W8MonsterInfo* member = level.loaded().population->findMonster(location_id);
        member->entity->nav.SetAngles(yaw);
        member->entity->nav.position_dirty = true;
        if (location_id == group->leader_location_id) {
            member->entity->nav.SetPositionInternal(&target);
        } else {
            if (position_index < found - 1) {
                ++position_index;
            }
            const glm::vec3 member_position = positions[position_index];
            member->entity->nav.SetPositionInternal(&member_position);
        }
        glm::vec3 placed = member->entity->nav.GetPosition();
        level.loaded().compiledIndex()->UpdateMonsterLocation(
            static_cast<unsigned short>(location_id), placed);
    }
    return true;
}

bool W8LevelPopulation::allMembersDying(const W8MonsterGroup& group) const
{
    return std::ranges::all_of(group.MemberIds(), [this](int id) {
        const auto* member = findMonster(id);
        return !member || member->entity->IsDying();
    });
}

/* How a monster group starts out disposed towards the party.

   A record flagged at +0xD0 answers from its NPC data instead of its faction:
   the group's first member is looked up, its NPC record fetched, and that
   record's own scale mapped onto this one. The two scales are not the same and
   the mapping is not the identity, which is why both are named separately.

   Otherwise the faction decides. The unaligned faction defers to the record's
   hostility range - -1 means neutral, zero means hostile, and anything else is
   a proximity threshold that still starts neutral - and the party's own faction
   is always friendly. Every other faction goes through the disposition table.

   Each of the three multi-way tests is a switch rather than a comparison chain:
   the original emits the dec/je ladder VC6 produces for small dense cases. */
W8Disposition Game::defaultGroupDisposition(W8MonsterGroup* monster_group) const
{
    const W8MonsterRecord* record;
    W8NpcState* npc_record;
    W8Disposition disposition = W8_DISPOSITION_NEUTRAL;

    assert(monster_group != 0);
    record = &data.monsters[monster_group->monster_id];
    if ((record->flags & W8_MONSTER_FLAG_NPC) != 0) {
        npc_record =
            story.npc((level.loaded().population->findMonster(monster_group->MemberLocationId(0)))
                          ->bound_npc_index);
        if (!npc_record || npc_record->binding_unavailable || record->npc_kind == 0xfa) {
            throw std::runtime_error(
                fmt::format("NPC monster {} has no NPC state", monster_group->monster_id));
        } else {
            switch (this->npcDispositionBand(npc_record)) {
            case W8_NPC_BAND_FRIENDLY:
                disposition = W8_DISPOSITION_FRIENDLY;
                break;
            case W8_NPC_BAND_NEUTRAL:
                break;
            case W8_NPC_BAND_HOSTILE:
                disposition = W8_DISPOSITION_HOSTILE;
                break;
            default:
                break;
            }
        }
    } else {
        switch (record->faction_id) {
        case W8_FACTION_UNALIGNED:
            if (record->hostility_radius == 0) disposition = W8_DISPOSITION_HOSTILE;
            break;
        case W8_FACTION_PARTY:
            disposition = W8_DISPOSITION_FRIENDLY;
            break;
        default:
            switch (this->story.factionDisposition(static_cast<signed char>(record->faction_id))) {
            case W8_FACTION_HOSTILE:
                disposition = W8_DISPOSITION_HOSTILE;
                break;
            case W8_FACTION_NEUTRAL:
                break;
            case W8_FACTION_FRIENDLY:
                disposition = W8_DISPOSITION_FRIENDLY;
                break;
            }
            break;
        }
    }
    return disposition;
}

/* Out-of-combat disposition refresh for a loaded group that still has a live
   member. Unaligned non-NPC neutrals that carry a finite hostility radius and
   can see the party within that scaled distance are promoted to hostile first.
   Then the default disposition is reapplied unless the group's hostility_set_at stamp
   is still inside the intelligence-squared cooldown and the faction band has
   not moved since that stamp. */
void RefreshMonsterGroupHostility(W8MonsterGroup* monster_group)
{
    const W8MonsterRecord* record;
    W8MonsterInfo* monster_info;
    unsigned int index;
    int cooldown;

    assert(monster_group != 0);
    record = MonsterDBFromSpecies(monster_group->monster_id);
    assert(monster_group != 0);
    index = 0;
    if (monster_group->MemberIds().empty()) {
        return;
    }
    while (true) {
        monster_info = FindMonster(monster_group->MemberLocationId(index));
        if (monster_info != 0 && !monster_info->entity->IsDying()) {
            break;
        }
        ++index;
        if (index < monster_group->MemberIds().size()) {
            continue;
        }
        return;
    }
    if (monster_group->ubDisposition != W8_DISPOSITION_HOSTILE &&
        (record->flags & W8_MONSTER_FLAG_NPC) == 0 && record->faction_id == W8_FACTION_UNALIGNED &&
        record->hostility_radius != 0 && record->hostility_radius != -1 &&
        GetGroupNearestDistance(monster_group) <= record->hostility_radius * kWorldScale &&
        MonsterGroupHasVisibleTarget(monster_group, true, W8_VISIBLE_TARGET_ANY, false)) {
        CurrentGame().setGroupHostility(monster_group, W8_DISPOSITION_HOSTILE, false);
    }
    cooldown = IntegerPower(record->attribute_values[W8_MONSTER_ATTRIBUTE_INTELLIGENCE], 2) * 0x3c;
    if (monster_group->hostility_set_at != 0 &&
        static_cast<unsigned int>(CurrentGame().status.world_clock -
                                  monster_group->hostility_set_at) <=
            static_cast<unsigned int>(cooldown)) {
        if (record->faction_id == W8_FACTION_UNALIGNED) {
            return;
        }
        if (static_cast<unsigned int>(monster_group->hostility_set_at) >=
            static_cast<unsigned int>(
                (CurrentGame()
                     .story.factions[(static_cast<signed char>(record->faction_id))]
                     .band_changed_clock))) {
            return;
        }
    }
    CurrentGame().setGroupHostility(monster_group,
                                    CurrentGame().defaultGroupDisposition(monster_group), false);
    monster_group->hostility_set_at = CurrentGame().status.world_clock;
}

W8MonsterGroup* FindMonsterGroup(int group_id, [[maybe_unused]] bool assert_on_failure)
{
    if (!GetWorld()) return nullptr;
    auto* group = CurrentLevelPopulation().findGroup(group_id);
    assert(group || !assert_on_failure);
    return group;
}

/* The database record behind a group's species. The assertion is the same one
   the disposition calculation opens with, at the same source line. */
const W8MonsterRecord* MonsterGroupGetRecord(W8MonsterGroup* monster_group)
{
    assert(monster_group != 0);
    return MonsterDBFromSpecies(monster_group->monster_id);
}

/* Destroys every member of a group, back to front so that the shrinking list
   does not move an entry past the cursor. It stops at the first removal that
   fails and reports that, which is why the loop is a do/while on the result
   rather than a counted walk.

   The despawn paths and the standalone emission share this canonical body. */
bool RemoveAllGroupMembers(W8MonsterGroup* monster_group)
{
    for (auto index = monster_group->MemberIds().size(); index > 0; --index) {
        const int location_id = monster_group->MemberLocationId(index - 1);
        if (!CurrentGame().retireMonster(FindMonster(location_id), W8MonsterRemoval::Despawn))
            return false;
    }
    return true;
}

/* Detaches a group from whatever is tracking it and marks it no longer loaded. */
void DetachMonsterGroup(W8MonsterGroup* monster_group)
{
    CurrentGame().invalidateAimsAt(W8CombatSlot::group(monster_group->group_id),
                                   TargetChange::removed);
    monster_group->members_active = false;
}

/* Whether a group is live: loaded, still flagged, and with members left. A
   group flagged at +0x2A is live on that alone; any other group also has to
   pass the global gate at 0x00547510. */
bool IsMonsterGroupLive(W8MonsterGroup* monster_group)
{
    if (monster_group->members_active && monster_group->fInCombat &&
        monster_group->MemberCount() > 0) {
        if (monster_group->ubDisposition != W8_DISPOSITION_HOSTILE && !CombatAllowsLiveGroups()) {
            return false;
        }
        return true;
    }
    return false;
}

/* Walk plsMonsterGroupList and return the Nth live combat group. */
W8MonsterGroup* GetLiveMonsterGroupAtIndex(int index)
{
    for (const auto& group : CurrentLevelPopulation().groups()) {
        if (IsMonsterGroupLive(group.get()) && index-- == 0) return group.get();
    }
    return nullptr;
}

/* Follows a group's leader chain to the group that actually leads the formation
   and acts on its lead member. A broken link - a leader id that resolves to no
   group - stops the walk and reports success anyway, as does a null group,
   which is why every path returns one. */
bool ApplyToMonsterGroupLeader(W8MonsterGroup* monster_group, const glm::vec3* position,
                               bool follow_leader)
{
    if (monster_group != 0) {
        while (follow_leader && monster_group->leader_group_id != 0) {
            monster_group = FindMonsterGroup(monster_group->leader_group_id);
            if (monster_group == 0) {
                return true;
            }
        }
        W8MonsterInfo* monster_info = FindMonster(monster_group->leader_location_id);
        monster_info->entity->nav.ConfigureMovementToPosition(position);
    }
    return true;
}

/* Sets a group's formation and pushes it straight onto every member's live
   Monster, so the group record and the members never disagree. */
void SetMonsterGroupFormation(W8MonsterGroup* monster_group, const glm::vec3* formation)
{
    std::size_t count;
    int index;

    if (monster_group == 0) {
        return;
    }
    monster_group->formation = *formation;
    count = monster_group->MemberIds().size();
    for (index = 0; index < static_cast<int>(count); ++index) {
        FindMonster(monster_group->MemberLocationId(index))->entity->formation = *formation;
    }
}

/* Retires a group and its allies from the encounter budget. The leader chain is
   followed first, so calling this on any member of a formation retires the
   whole formation; the retire itself is idempotent, gated on the flag it
   clears. All four ally slots are walked and the empty ones skipped. */
void RetireMonsterGroupAndAllies(W8MonsterGroup* monster_group)
{
    int index;
    W8MonsterGroup* ally;

    while (monster_group->leader_group_id != 0) {
        monster_group = FindMonsterGroup(monster_group->leader_group_id);
    }
    if (monster_group->encounter_registered) {
        SpendRandomEncounterBudget();
        monster_group->encounter_registered = false;
        monster_group->encounter_ended = true;
        for (index = 0; index < W8_MONSTER_GROUP_ALLY_COUNT; ++index) {
            if (monster_group->allied_group_ids[index] != 0) {
                ally = FindMonsterGroup(monster_group->allied_group_ids[index]);
                ally->encounter_registered = false;
                ally->encounter_ended = true;
            }
        }
    }
}

void Game::endGroupCombat(W8MonsterGroup& group)
{
    assert(combat);
    assert(group.fInCombat);
    std::vector<Event> outcomes;
    auto& population = *level.loaded().population;
    for (unsigned int index = 0; index < group.MemberIds().size(); ++index) {
        auto* monster = population.findMonster(group.MemberLocationId(index));
        assert(monster);
        removeMonsterFromCombat(*monster, outcomes);
    }
    group.fInCombat = false;
    outcomes.emplace_back(MonstersChanged{});
    auto* leader = population.findMonster(group.leader_location_id);
    assert(leader);
    leader->ai_mode |= W8_MONSTER_AI_REAPPLY_MODE;
    events.publish(std::move(outcomes));
}

/* Removes a group and everything allied to it from the world. Each ally is
   emptied and its slot cleared before the group itself is emptied, so the
   group's own members are the last to go and an ally cannot be reached twice. */
void DespawnMonsterGroup(W8MonsterGroup* monster_group)
{
    int index;

    for (index = 0; index < W8_MONSTER_GROUP_ALLY_COUNT; ++index) {
        if (monster_group->allied_group_ids[index] != 0) {
            RemoveAllGroupMembers(FindMonsterGroup(monster_group->allied_group_ids[index]));
            monster_group->allied_group_ids[index] = 0;
        }
    }
    RemoveAllGroupMembers(monster_group);
}

/* Clears the per-turn scratch on every loaded group and every monster entry.
   Both walks re-read their list length each iteration. */
void ResetMonsterGroupTurnState(void)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().groups().size(); ++index) {
        CurrentLevelPopulation().groups()[index].get()->hostility_set_at = 0;
    }
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        monster_info->player_visibility.last_seen_clock = 0;
        monster_info->party_threat.last_seen_clock = 0;
    }
}

/* Writes a control state onto every live member of the group; the Lure
   effect uses it to flip a whole out-of-combat group at once. */
void SetMonsterGroupControlState(W8MonsterGroup* monster_group, W8MonsterControlState control_state)
{
    for (unsigned int index = 0; index < monster_group->MemberIds().size(); ++index) {
        int location_id = monster_group->MemberLocationId(index);
        W8MonsterInfo* info = FindMonster(location_id);
        if (info != 0 && !info->entity->IsDying()) {
            SetMonsterControlState(info, control_state);
        }
    }
}

bool MonsterGroupHasRenderableMember(W8MonsterGroup* monster_group, bool require_threat)
{
    if (!monster_group->members_active) {
        return false;
    }
    for (unsigned int index = 0; index < monster_group->MemberIds().size(); ++index) {
        W8MonsterInfo* info = FindMonster(monster_group->MemberLocationId(index));
        if (info->entity->IsWithinWorldRange() && info->entity->IsRenderable(true) &&
            (!require_threat || info->party_threat.los_flags[1])) {
            return true;
        }
    }
    return false;
}

/* Nonzero when the group - or, recursing, one of its allied groups - has a
   member whose highest condition is in the 0x0e..0x11 incapacitated band. */
bool MonsterGroupHasIncapacitatedMember(int group_id)
{
    W8MonsterGroup* group = FindMonsterGroup(group_id);
    unsigned int index;
    for (index = 0; index < group->MemberIds().size(); ++index) {
        W8MonsterInfo* info = FindMonster(group->MemberLocationId(index));
        if (info->highest_condition > W8_CONDITION_TURNCOAT &&
            info->highest_condition < W8_CONDITION_DEAD) {
            return true;
        }
    }
    for (index = 0; index < 4; ++index) {
        if (group->allied_group_ids[index] != 0 &&
            MonsterGroupHasIncapacitatedMember(group->allied_group_ids[index])) {
            return true;
        }
    }
    return false;
}

/* The mean position of a group's members, recomputed only while the group is
   loaded and cached on the group itself; an unloaded group answers with
   whatever it last held. The out-parameter is optional, so the same call both
   refreshes the cache and reads it.

   The divisor is the member count captured before the walk, not re-read after
   it, which is what makes a member added during the walk skew the average
   rather than divide by the wrong count. Preserved as found. */
void GetMonsterGroupCentre(W8MonsterGroup* monster_group, glm::vec3* centre)
{
    std::size_t count;
    unsigned int index;
    glm::vec3 position;
    float divisor;

    if (monster_group == 0) {
        return;
    }
    if (monster_group->members_active) {
        monster_group->centre = glm::vec3(0);
        count = monster_group->MemberIds().size();
        for (index = 0; index < count; ++index) {
            position = FindMonster(monster_group->MemberLocationId(index))
                           ->entity->GetRepresentationLocation();
            monster_group->centre += position;
        }
        divisor = static_cast<float>(count);
        monster_group->centre /= divisor;
    }
    if (centre != 0) {
        *centre = monster_group->centre;
    }
}

// Reserve a destination before changing relationships. Refresh navigation and
// disposition only after both sides describe the completed formation.
bool Game::linkMonsterGroup(W8MonsterGroup* leader, W8MonsterGroup* group)
{
    if (leader == group) return 0;
    if (leader && group->leader_group_id == leader->group_id) return 1;
    int* slot = nullptr;
    if (leader) {
        slot = std::ranges::find(leader->allied_group_ids, 0);
        if (slot == std::end(leader->allied_group_ids)) return 0;
    }
    level.loaded().population->unlinkGroup(*group);
    if (leader) {
        *slot = group->group_id;
        group->leader_group_id = leader->group_id;
    }
    level.loaded().refreshGroupNavigation(*group, combat.has_value());
    if (leader) setGroupHostility(group, leader->ubDisposition, false);
    return 1;
}

/* Allocate one live monster group, create its members, activate them, and
   register it on the species or encounter list. */
W8MonsterGroup* Game::createMonsterGroup(unsigned int species, unsigned int count,
                                         const glm::vec3* position, bool alternate_name)
{
    if (species >= data.monsters.size()) return nullptr;
    const auto& record = data.monsters[species];
    auto& world = level.loaded();
    auto group_owner = std::make_unique<W8MonsterGroup>();
    auto* group = group_owner.get();
    do {
        group->group_id = status.next_group_id++;
    } while (group->group_id == 0);
    group->monster_id = species;
    group->centre = *position;
    group->alternate_name = alternate_name || (record.flags & W8_MONSTER_FLAG_ALTERNATE_NAME) != 0;
    std::fill_n(group->group_state + 2, 4, 0xff);
    group->spawn_time = status.world_clock;
    world.population->adoptGroup(std::move(group_owner), record.unborn);
    for (unsigned int index = 0; index < std::min(count, 9u); ++index) {
        auto info = std::make_unique<W8MonsterInfo>();
        do {
            info->location_id = status.next_monster_location_id++;
        } while (info->location_id == 0);
        info->monster_group_id = group->group_id;
        info->monster_species = species;
        info->ubDisposition = group->ubDisposition;
        info->scale = -1.0f;
        info->position = *position;
        const auto eye = world.party.eye();
        info->derived = GetHeadingAngle(position, &eye);
        info->initializeVitals(record, rng);
        info->elemental_summon = -1;
        info->movement_watch_position = glm::vec3(0);
        const int id = info->location_id;
        group->monsters.reserve(group->monsters.size() + 1);
        world.population->monster_roster.add(std::move(info), record.unborn);
        group->monsters.push_back(id);
        events.push(MonstersChanged{});
        world.compiledIndex()->UpdateMonsterLocation(static_cast<unsigned short>(id), *position);
    }
    group->leader_location_id = status.next_monster_location_id - 1;
    activateGroup(*group);
    const auto eye = world.party.eye();
    placeMonsterGroup(group, position, GetHeadingAngle(position, &eye), false, false, false, false);
    world.refreshGroupNavigation(*group, combat.has_value());
    setGroupHostility(group, defaultGroupDisposition(group), false);
    return group;
}

/* The owning collection establishes membership before any relationship or
   budget changes. No id-to-index conversion or separate unregistration. */

/* Continue through live groups, then encounters, after the previous match. */

/* Elect a successor while the old formation is still intact, then publish
   the new links. No temporary independent encounters or registration repair. */

/* Bring the group - or the group leading it - into combat once every gate
   clears: an untargetable, dirty or dying member, or the startup navigator
   still settling, all defer the entry. The leader's members and its allies'
   members enter together, and a second live hostile group arms the
   ambush-warning event. */
void MonsterGroupEnterCombat(W8MonsterGroup* monster_group)
{
    while (true) {
        if (CurrentGame().status.world_suspended &&
            monster_group->ubDisposition == W8_DISPOSITION_HOSTILE &&
            !CurrentGame().combat.has_value()) {
            return;
        }

        if (monster_group->fInCombat) {
            return;
        }
        assert(monster_group != 0);
        const W8MonsterRecord* record = MonsterDBFromSpecies(monster_group->monster_id);
        if (record->untargetable != 0) {
            return;
        }
        assert(monster_group != 0);
        unsigned int index = 0;
        while (index < monster_group->MemberIds().size()) {
            int member_id = monster_group->MemberLocationId(index);
            W8MonsterInfo* member_info = FindMonster(member_id);
            if (member_info->entity->nav.position_dirty) {
                return;
            }
            ++index;
        }
        if (GetWorld()->party.navigator.position_dirty) {
            return;
        }
        assert(monster_group != 0);
        index = 0;
        while (index < monster_group->MemberIds().size()) {
            int member_id = monster_group->MemberLocationId(index);
            W8MonsterInfo* member_info = FindMonster(member_id);
            if ((member_info->entity->runtime_flags & W8_MONSTER_SCRIPT_WAIT) != 0) {
                return;
            }
            ++index;
        }
        assert(monster_group != 0);
        index = 0;
        if (monster_group->MemberIds().empty()) {
            return;
        }
        while (true) {
            int member_id = monster_group->MemberLocationId(index);
            W8MonsterInfo* member_info = FindMonster(member_id);
            if (member_info != 0 && !member_info->entity->IsDying()) {
                break;
            }
            ++index;
            if (monster_group->MemberIds().size() <= index) {
                return;
            }
        }
        if (monster_group->leader_group_id == 0) {
            index = 0;
            while (index < monster_group->MemberIds().size()) {
                int member_id = monster_group->MemberLocationId(index);
                W8MonsterInfo* member_info = FindMonster(member_id);
                MonsterInfoEnterCombat(member_info);
                ++index;
            }
            monster_group->fInCombat = true;
            if (monster_group->ubDisposition == W8_DISPOSITION_HOSTILE &&
                CurrentGame().combat.has_value() && CurrentGame().combat->round_count != 0) {
                unsigned int live_groups = 0;
                unsigned int group_list_index = 0;
                while (group_list_index < CurrentLevelPopulation().groups().size()) {
                    W8MonsterGroup* other =
                        CurrentLevelPopulation().groups()[group_list_index].get();
                    if (other->members_active && other->fInCombat && other->MemberCount() != 0 &&
                        (other->ubDisposition == W8_DISPOSITION_HOSTILE ||
                         CombatAllowsLiveGroups())) {
                        ++live_groups;
                    }
                    ++group_list_index;
                }
                if (live_groups > 1) {
                    CurrentGame().speech.QueueRandom(33, -1, 0);
                }
            }
            int* allies = monster_group->allied_group_ids;
            for (int ally_index = 0; ally_index < 4; ++ally_index) {
                if (allies[ally_index] != 0) {
                    W8MonsterGroup* ally = FindMonsterGroup(allies[ally_index]);
                    if (!ally->fInCombat) {
                        index = 0;
                        while (index < ally->MemberIds().size()) {
                            int member_id = ally->MemberLocationId(index);
                            W8MonsterInfo* member_info = FindMonster(member_id);
                            MonsterInfoEnterCombat(member_info);
                            ++index;
                        }
                        ally->fInCombat = true;
                    }
                }
            }
            CurrentGame().events.push(MonstersChanged{});
            SetMonsterGroupEngagementState(monster_group->group_id, false);
            return;
        }
        monster_group = FindMonsterGroup(monster_group->leader_group_id);
    }
}

/* Places a group relative to the camera: flag clear moves it straight onto the
   camera position, flag set scatters it `distance` out on a random heading
   centred just off the camera's back, widened to the largest allied member
   radius. `yaw` is read by the prototype but the body never uses it. */
bool PositionMonsterGroupNearCamera(W8MonsterGroup* group, float distance, float yaw, bool flag)
{
    glm::vec3 camera;
    glm::vec3 target;
    W8MonsterInfo* member_info;
    W8MonsterGroup* ally;
    float radius;
    float angle;
    unsigned int index;

    camera = GetWorld()->party.position();
    if (!flag) {
        target = camera;
        return CurrentGame().placeMonsterGroup(group, &target, 0.0f, true, true, false, true);
    }
    member_info = FindMonster(group->leader_location_id);
    angle = NormalizeAngle(GetCameraYawRadians() +
                           CurrentGame().rng.below(1000) * kMonsterScatterHeadingRandomScale +
                           g_monster_rotation_offset - kMonsterScatterHeadingBias);
    radius = member_info->entity->nav.radius;
    for (index = 0; index < W8_MONSTER_GROUP_ALLY_COUNT; ++index) {
        if (group->allied_group_ids[index] != 0) {
            ally = FindMonsterGroup(group->allied_group_ids[index]);
            member_info = FindMonster(ally->leader_location_id);
            if (radius < member_info->entity->nav.radius) {
                radius = member_info->entity->nav.radius;
            }
        }
    }
    if (distance <= 0.0f) {
        distance = GetWorld()->party.radius() + radius + 1.0f;
    }
    target.y = camera.y;
    target.x = camera.x + distance * sin(angle);
    target.z = camera.z + distance * cos(angle);
    angle = GetCameraFacingYaw(&target);
    return CurrentGame().placeMonsterGroup(group, &target, angle, true, true, false, true);
}

/* Announces a group's composition on the notice line: the member count and the
   singular/plural record name - the alternate-name record takes the selected
   party member's name prefixed "Al-" - then the active, selectable and visible
   tallies, with the spotted flag adding its own line. */
void ShowMonsterGroupInfoNotice(int group_id)
{
    W8MonsterGroup* group = FindMonsterGroup(group_id);
    auto name = CurrentGame().monsterGroupName(*group);

    ShowNotice(W8_FONT_PALETTE_BEIGE, fmt::format("{:d} {}", group->MemberCount(), name));
    if (!group->alternate_name) {
        ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x1c5]);
    }
    ShowNotice(
        W8_FONT_PALETTE_BEIGE,
        fmt::sprintf(Data().strings[0x1c6], CurrentLevelPopulation().activeMemberCount(*group)));
    ShowNotice(W8_FONT_PALETTE_BEIGE,
               fmt::sprintf(Data().strings[0x1c7], group->selectable_member_count));
    ShowNotice(W8_FONT_PALETTE_BEIGE,
               fmt::sprintf(Data().strings[0x1c8], group->visible_member_count));
}

/* Flags every live member of the group for removal, then recurses into each
   allied group. Members already dying are left alone. */
void MarkMonsterGroupForRemoval(int group_id)
{
    W8MonsterGroup* group;
    W8MonsterInfo* member_info;
    unsigned int index;
    int ally_index;

    group = FindMonsterGroup(group_id);
    for (index = 0; index < group->MemberIds().size(); ++index) {
        member_info = FindMonster(group->MemberLocationId(index));
        if (member_info != 0 && !member_info->entity->IsDying()) {
            member_info->entity->runtime_flags |= W8_MONSTER_REMOVE_NOW;
        }
    }
    for (ally_index = 0; ally_index < W8_MONSTER_GROUP_ALLY_COUNT; ++ally_index) {
        if (group->allied_group_ids[ally_index] != 0) {
            MarkMonsterGroupForRemoval(group->allied_group_ids[ally_index]);
        }
    }
}

/* Moves a group off the encounter list onto the live list and brings every one
   of its members into the live monster list. Answers 1 when the group was
   actually waiting on the encounter list. */
bool Game::giveBirthToMonster(W8MonsterGroup* group)
{
    if (!level.loaded().population->giveBirth(*group)) return false;
    refreshOutwardSight();
    return true;
}

/* Respawns a same-sized group of a different monster id beside the source
   group: the new group's members are created, each old member's position is
   overwritten by the replacement's before it is deactivated, and the new
   member is activated. NULL when creation fails or the counts disagree. */
W8MonsterGroup* ReplaceMonsterGroupSpecies(W8MonsterGroup* group, unsigned int monster_id)
{
    W8MonsterGroup* new_group;
    W8MonsterInfo* new_member;
    W8MonsterInfo* old_member;
    glm::vec3 position;
    glm::vec3 spawn;
    unsigned int index;

    new_group = CurrentGame().createMonsterGroup(monster_id, group->MemberCount(), &spawn, false);
    if (new_group == 0 || new_group->MemberCount() != group->MemberCount()) {
        return 0;
    }
    for (index = 0; index < new_group->MemberCount(); ++index) {
        new_member = FindMonster(new_group->MemberLocationId(index));
        old_member = FindMonster(group->MemberLocationId(index));
        if (new_member != 0 && old_member != 0) {
            position = new_member->entity->nav.GetPosition();
            old_member->entity->nav.SetPositionInternal(&position);
            CurrentGame().deactivateMonster(old_member);
        }
    }
    return new_group;
}

/* Whether any member of a spotted group is inside world range and renderable;
   a nonzero require_threat also demands the member's second sight flag. */

/* Writes `state` into the group's engagement byte and propagates it to its
   four allied groups; while the byte is set each call ticks the counter beside
   it. The two special record kinds ignore a set. */
void SetMonsterGroupEngagementState(int group_id, bool state)
{
    W8MonsterGroup* group;

    group = FindMonsterGroup(group_id);
    if ((group->monster_id == 0x1b6 || group->monster_id == 0x234) && state) {
        return;
    }
    if (group->engagement != state) {
        group->engagement = state;
        group->engagement_ticks = 0;
    }
    if (group->engagement) {
        ++group->engagement_ticks;
    }
    for (const int ally_id : group->allied_group_ids) {
        if (ally_id != 0) {
            auto* ally = FindMonsterGroup(ally_id);
            if (ally->engagement != state) {
                ally->engagement = state;
                ally->engagement_ticks = 0;
            }
            if (ally->engagement) {
                ++ally->engagement_ticks;
            }
        }
    }
}

void SetMonsterGroupNavigatorDirty(W8MonsterGroup* monster_group, bool flag)
{
    W8MonsterInfo* member_info;
    unsigned int index;

    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        member_info = FindMonster(monster_group->MemberLocationId(index));
        member_info->entity->nav.position_dirty = flag;
    }
}
