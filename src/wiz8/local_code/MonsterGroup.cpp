#include "wiz8/local_code/MonsterGroup.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/script.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/world/world.h"
#include "wiz8/xstatus.h"

constexpr float kMonsterScatterHeadingBias = 0.8f;
constexpr float kMonsterScatterHeadingRandomScale = 0.001600000075995922f;

struct W8NpcState;

/* A group of one is named in the singular; any other count uses the plural
   form, which is the second entry of each name set. */
enum { W8_MONSTER_GROUP_SINGULAR = 1, W8_MONSTER_NAME_STRIDE = 24 };

enum { W8_MONSTER_GROUP_ALLY_COUNT = 4 };

bool MoveMonsterGroupToPosition(W8MonsterGroup* group, const glm::vec3* position, float yaw,
                                bool proximity_check, bool include_allies, bool flatten_y,
                                bool alternate_radius)
{
    if (!group->members_active) {
        return false;
    }
    W8MonsterInfo* leader = FindMonster(group->leader_location_id);
    float radius =
        alternate_radius ? leader->p3D->nav.movement.alternate_radius : leader->p3D->nav.radius;
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
                W8MonsterGroup* ally = FindMonsterGroup(allied_group_id);
                W8MonsterInfo* ally_leader = FindMonster(ally->leader_location_id);
                float ally_radius = alternate_radius
                                        ? ally_leader->p3D->nav.movement.alternate_radius
                                        : ally_leader->p3D->nav.radius;
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
            GetWorldSpatialIndex()->UnregisterLocationObjects(
                static_cast<short>(location_ids[index]));
        }
    }
    glm::vec3 target = *position;
    if (alternate_radius) {
        positions =
            GetWorldSpatialIndex()
                ->navigatorPositions(target, yaw, radius + radius, count, proximity_check,
                                     flatten_y, false, 5, true,
                                     BoundingSphere{PartyNavigator()->movement.position,
                                                    PartyNavigator()->movement.alternate_radius})
                .positions;
    } else {
        positions = GetWorldSpatialIndex()->scatterPositions(
            target, yaw, radius + radius, count, proximity_check, flatten_y,
            BoundingSphere{PartyNavigator()->movement.position, PartyNavigator()->radius});
    }
    const auto found = positions.size();
    if (found == 0) {
        return false;
    }
    if (proximity_check) {
        while (found < count) {
            --count;
            RemoveMonster(FindMonster(location_ids[count]), W8MonsterRemoval::Despawn);
        }
    }
    target = positions[0];
    unsigned int position_index = 0;
    for (index = 0; index < count; ++index) {
        int location_id = location_ids[index];
        W8MonsterInfo* member = FindMonster(location_id);
        member->p3D->nav.SetAngles(yaw);
        member->p3D->nav.position_dirty = true;
        if (location_id == group->leader_location_id) {
            member->p3D->nav.SetPositionInternal(&target);
        } else {
            if (position_index < found - 1) {
                ++position_index;
            }
            const glm::vec3 member_position = positions[position_index];
            member->p3D->nav.SetPositionInternal(&member_position);
        }
        glm::vec3 placed = member->p3D->nav.GetPosition();
        GetWorldSpatialIndex()->UpdateMonsterLocation(static_cast<unsigned short>(location_id),
                                                      &placed);
    }
    return true;
}

/* A group is done dying only when every live script record either has no
   engine Monster or reports the Monster death cycle.  Save-list location IDs
   are deliberately resolved through the canonical lookup path rather than
   treated as list indices. */
bool MonsterGroupAllMembersDying(W8MonsterGroup* monster_group)
{
    unsigned int index;
    int location_id;
    W8MonsterInfo* monster_info;

    if (monster_group == 0) {
        Fail("pMonsterGroup");
    }
    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        location_id = monster_group->MemberLocationId(index);
        monster_info = FindMonster(location_id);
        if (monster_info != 0 && !monster_info->p3D->IsDying()) {
            return false;
        }
    }
    return true;
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
static W8Disposition MonsterGroupCalcDefaultDisposition(W8MonsterGroup* monster_group)
{
    W8MonsterRecord* record;
    W8NpcState* npc_record;
    W8Disposition disposition = W8_DISPOSITION_NEUTRAL;

    if (monster_group == 0) {
        Fail("pMonsterGroup != NULL");
    }
    record = MonsterDBFromSpecies(monster_group->monster_id);
    if ((record->flags & W8_MONSTER_FLAG_NPC) != 0) {
        npc_record = FindNpcBindingForMonster(FindMonster(monster_group->MemberLocationId(0)));
        if (npc_record == 0) {
            Fail("FALSE",
                 fmt::format("MonsterGroupCalcDefaultDisposition: Monster species {:d}({}) "
                             "NPC data not found",
                             monster_group->monster_id, record->name0));
        } else {
            switch (GetNpcDispositionBand(npc_record)) {
            case W8_NPC_BAND_FRIENDLY:
                disposition = W8_DISPOSITION_FRIENDLY;
                break;
            case W8_NPC_BAND_NEUTRAL:
                break;
            case W8_NPC_BAND_HOSTILE:
                disposition = W8_DISPOSITION_HOSTILE;
                break;
            default:
                Fail("FALSE");
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
            switch (GetFactionDisposition(static_cast<signed char>(record->faction_id))) {
            case W8_FACTION_HOSTILE:
                disposition = W8_DISPOSITION_HOSTILE;
                break;
            case W8_FACTION_NEUTRAL:
                break;
            case W8_FACTION_FRIENDLY:
                disposition = W8_DISPOSITION_FRIENDLY;
                break;
            default:
                Fail("FALSE");
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
    W8MonsterRecord* record;
    W8MonsterInfo* monster_info;
    unsigned int index;
    int cooldown;

    if (monster_group == 0) {
        Fail("pMonsterGroup != NULL");
    }
    record = MonsterDBFromSpecies(monster_group->monster_id);
    if (monster_group == 0) {
        Fail("pMonsterGroup");
    }
    index = 0;
    if (monster_group->MemberIds().empty()) {
        return;
    }
    while (true) {
        monster_info = FindMonster(monster_group->MemberLocationId(index));
        if (monster_info != 0 && !monster_info->p3D->IsDying()) {
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
        SetMonsterGroupHostility(monster_group, W8_DISPOSITION_HOSTILE, false);
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
                (g_factions[(static_cast<signed char>(record->faction_id))].band_changed_clock))) {
            return;
        }
    }
    SetMonsterGroupHostility(monster_group, MonsterGroupCalcDefaultDisposition(monster_group),
                             false);
    monster_group->hostility_set_at = CurrentGame().status.world_clock;
}

W8MonsterGroup* FindMonsterGroup(int group_id, bool assert_on_failure, std::source_location caller)
{
    if (!GetWorld()) return nullptr;
    auto* group = CurrentLevelPopulation().findGroup(group_id);
    if (!group && assert_on_failure)
        Fail("group", fmt::format("Group ID {:d} not found", group_id), caller);
    return group;
}

/* The database record behind a group's species. The assertion is the same one
   the disposition calculation opens with, at the same source line. */
W8MonsterRecord* MonsterGroupGetRecord(W8MonsterGroup* monster_group)
{
    if (monster_group == 0) {
        Fail("pMonsterGroup != NULL");
    }
    return MonsterDBFromSpecies(monster_group->monster_id);
}

/* Destroys every member of a group, back to front so that the shrinking list
   does not move an entry past the cursor. It stops at the first removal that
   fails and reports that, which is why the loop is a do/while on the result
   rather than a counted walk.

   The despawn paths and the standalone emission share this canonical body. */
unsigned char RemoveAllGroupMembers(W8MonsterGroup* monster_group)
{
    for (auto index = monster_group->MemberIds().size(); index > 0; --index) {
        const int location_id = monster_group->MemberLocationId(index - 1);
        if (!RemoveMonster(FindMonster(location_id), W8MonsterRemoval::Despawn)) return 0;
    }
    return 1;
}

/* Brings every member of a group into the world. Front to back, and the list
   length is re-read each time because activation can add to it. */
void ActivateGroupMembers(W8MonsterGroup* monster_group, W8MonsterActivationMode mode)
{
    unsigned int index;

    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        ActivateMonster(FindMonster(monster_group->MemberLocationId(index)), mode);
    }
}

void RefreshMonsterGroup(W8MonsterGroup* monster_group)
{
    Monster* leader;
    unsigned int index;

    if (monster_group == 0) {
        Fail("pMonsterGroup");
    }
    if (monster_group->leader_group_id == 0) {
        leader = GetMonsterByLocationID(monster_group->leader_location_id);
        if (leader) leader->nav.LinkGroupNavigator(nullptr, CurrentGame().combat.engineActive());
    } else {
        W8MonsterGroup* leader_group = FindMonsterGroup(monster_group->leader_group_id);
        leader = GetMonsterByLocationID(leader_group->leader_location_id);
    }
    if (!leader) return;
    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        Monster* member = GetMonsterByLocationID(monster_group->MemberLocationId(index));

        if (member && member != leader) {
            member->nav.LinkGroupNavigator(&leader->nav, CurrentGame().combat.engineActive());
        }
    }
}

/* Refreshes a group and every group allied to it, then the lead member's live
   monster. All four ally slots are walked and the empty ones skipped, so the
   array is fixed-size rather than terminated.

   The link-repair path calls the same canonical body. */
void RefreshMonsterGroupAndAllies(W8MonsterGroup* monster_group)
{
    int index;

    RefreshMonsterGroup(monster_group);
    for (index = 0; index < W8_MONSTER_GROUP_ALLY_COUNT; ++index) {
        if (monster_group->allied_group_ids[index] != 0) {
            RefreshMonsterGroup(FindMonsterGroup(monster_group->allied_group_ids[index]));
        }
    }
    if (auto* leader = GetMonsterByLocationID(monster_group->leader_location_id))
        leader->nav.PropagateGroupPosition();
}

/* Detaches a group from whatever is tracking it and marks it no longer loaded. */
void DetachMonsterGroup(W8MonsterGroup* monster_group)
{
    SetTargetToGroup(monster_group->group_id, false);
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
    for (const auto& group : CurrentLevelPopulation().monster_groups) {
        if (IsMonsterGroupLive(group.get()) && index-- == 0) return group.get();
    }
    return nullptr;
}

/* Follows a group's leader chain to the group that actually leads the formation
   and acts on its lead member. A broken link - a leader id that resolves to no
   group - stops the walk and reports success anyway, as does a null group,
   which is why every path returns one. */
unsigned char ApplyToMonsterGroupLeader(W8MonsterGroup* monster_group, const glm::vec3* position,
                                        bool follow_leader)
{
    if (monster_group != 0) {
        while (follow_leader && monster_group->leader_group_id != 0) {
            monster_group = FindMonsterGroup(monster_group->leader_group_id);
            if (monster_group == 0) {
                return 1;
            }
        }
        W8MonsterInfo* monster_info = FindMonster(monster_group->leader_location_id);
        monster_info->p3D->nav.ConfigureMovementToPosition(position);
    }
    return 1;
}

/* Sets a group's formation and pushes it straight onto every member's live
   Monster, so the group record and the members never disagree. */
void SetMonsterGroupFormation(W8MonsterGroup* monster_group, const glm::vec3* formation)
{
    std::size_t count;
    int index;
    Monster* monster;

    if (monster_group == 0) {
        return;
    }
    monster_group->formation = *formation;
    count = monster_group->MemberIds().size();
    for (index = 0; index < static_cast<int>(count); ++index) {
        monster = FindMonster(monster_group->MemberLocationId(index))->p3D;
        monster->formation = *formation;
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

/* The display name for a whole group, which is GetMonsterName's shape one level
   up: the same special-cased record id, the same choice between the record's
   two name sets, but the variant comes from the member count rather than a
   caller - a group of exactly one is named in the singular.

   Its opening assertion is followed immediately by MonsterGroupGetRecord's own. */
std::string GetMonsterGroupName(W8MonsterGroup* monster_group)
{
    W8MonsterRecord* record;
    unsigned int name_form;

    if (monster_group == 0) {
        Fail("pMonsterGroup != NULL");
    }
    record = MonsterGroupGetRecord(monster_group);
    name_form = monster_group->MemberCount() != W8_MONSTER_GROUP_SINGULAR;
    if (record->record_id == W8_MONSTER_RECORD_ALTERNATE_NAME) {
        CurrentGame().status.monster_name_buffer = fmt::format(
            "Al-{}", CurrentGame().party.characters[CurrentGame().status.sedexus_party_slot].name);
        return CurrentGame().status.monster_name_buffer;
    }
    if (monster_group->alternate_name) {
        return (name_form ? record->name1 : record->name0);
    }
    return (name_form ? record->name3 : record->name2);
}

/* Takes a whole group out of combat: every member leaves individually, the
   group's own fInCombat is lowered, and the lead member is marked. The member
   list length is re-read each iteration because leaving combat can change it.
   Both assertions name what they guard - the global combat mode and the group's
   own fInCombat, which is what gives +0x29 its name. */
void MonsterGroupLeaveCombat(W8MonsterGroup* monster_group)
{
    unsigned int index;
    W8MonsterInfo* lead;

    if (!CurrentGame().combat.active()) {
        Fail("CurrentGame().combat.active()");
    }
    if (!monster_group->fInCombat) {
        Fail("pMonsterGroup->fInCombat");
    }
    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        MonsterInfoLeaveCombat(FindMonster(monster_group->MemberLocationId(index)));
    }
    monster_group->fInCombat = false;
    RequestRedrawParty();
    lead = FindMonster(monster_group->leader_location_id);
    lead->ai_mode |= W8_MONSTER_AI_REAPPLY_MODE;
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

/* Brings a freshly loaded group's members into the world and marks the group
   loaded. Members that are already active are left alone. The sentinel at +0x9B
   is reset first, so a reload does not inherit the previous run's value. */
void LoadMonsterGroupMembers(W8MonsterGroup* monster_group)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    monster_group->highlighted_member = -1;
    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        monster_info = FindMonster(monster_group->MemberLocationId(index));
        if (!monster_info->fActive) {
            ActivateMonsterInWorld(monster_info);
        }
    }
    monster_group->members_active = true;
}

/* Clears the per-turn scratch on every loaded group and every monster entry.
   Both walks re-read their list length each iteration. */
void ResetMonsterGroupTurnState(void)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().monster_groups.size(); ++index) {
        CurrentLevelPopulation().monster_groups[index].get()->hostility_set_at = 0;
    }
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        monster_info->player_visibility.last_seen_clock = 0;
        monster_info->party_threat.last_seen_clock = 0;
    }
}

/* On a level's first visit, rebind every group monster's script from the name
   of the script it already carries, so reloaded monsters resume their
   level-local script objects. */
void RebindMonsterGroupScripts(void)
{
    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_groups.size(); ++index) {
        W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[index].get();
        W8MonsterInfo* info = FindMonster(group->leader_location_id);
        if (info->p3D->script != 0) {
            char script_name[256];
            strcpy(script_name, info->p3D->script->getName().c_str());
            info->p3D->SetScript(script_name, true);
        }
    }
}

/* Writes a control state onto every live member of the group; the Lure
   effect uses it to flip a whole out-of-combat group at once. */
void SetMonsterGroupControlState(W8MonsterGroup* monster_group, W8MonsterControlState control_state)
{
    for (unsigned int index = 0; index < monster_group->MemberIds().size(); ++index) {
        int location_id = monster_group->MemberLocationId(index);
        W8MonsterInfo* info = FindMonster(location_id);
        if (info != 0 && !info->p3D->IsDying()) {
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
        if (info->p3D->IsWithinWorldRange() && info->p3D->IsRenderable(true) &&
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
                           ->p3D->GetRepresentationLocation();
            monster_group->centre += position;
        }
        divisor = static_cast<float>(count);
        monster_group->centre /= divisor;
    }
    if (centre != 0) {
        *centre = monster_group->centre;
    }
}

static void UnlinkMonsterGroupFromLeader(W8MonsterGroup& group)
{
    if (group.leader_group_id == 0) return;
    auto* previous = FindMonsterGroup(group.leader_group_id);
    std::ranges::replace(previous->allied_group_ids, group.group_id, 0);
    group.leader_group_id = 0;
}

// Reserve a destination before changing relationships. Refresh navigation and
// disposition only after both sides describe the completed formation.
bool LinkMonsterGroupToLeader(W8MonsterGroup* leader, W8MonsterGroup* group)
{
    if (leader == group) return 0;
    if (leader && group->leader_group_id == leader->group_id) return 1;
    int* slot = nullptr;
    if (leader) {
        slot = std::ranges::find(leader->allied_group_ids, 0);
        if (slot == std::end(leader->allied_group_ids)) return 0;
    }
    UnlinkMonsterGroupFromLeader(*group);
    if (leader) {
        *slot = group->group_id;
        group->leader_group_id = leader->group_id;
    }
    RefreshMonsterGroup(group);
    if (leader) SetMonsterGroupHostility(group, leader->ubDisposition, false);
    return 1;
}

/* Allocate one live monster group, create its members, activate them, and
   register it on the species or encounter list. */
W8MonsterGroup* CreateGroup(unsigned int monster_id, unsigned int count, const glm::vec3* position,
                            bool use_alternate_name, bool announce_spawn, bool place_on_ground)
{
    W8MonsterGroup* group;
    W8MonsterRecord* record;
    std::vector<std::unique_ptr<W8MonsterGroup>>* list;
    unsigned int index;
    unsigned int created;
    float yaw;

    if (count > 9) {
        count = 9;
    }
    record = MonsterDBFromSpecies(monster_id);
    if (record == 0) {
        return 0;
    }
    auto owner = std::make_unique<W8MonsterGroup>();
    group = owner.get();

    do {
        group->group_id = CurrentGame().status.next_group_id;
        ++CurrentGame().status.next_group_id;
    } while (group->group_id == 0);

    group->leader_group_id = 0;
    group->allied_group_ids[0] = 0;
    group->allied_group_ids[1] = 0;
    group->allied_group_ids[2] = 0;
    group->allied_group_ids[3] = 0;

    group->monster_id = monster_id;
    group->centre = *position;
    group->members_active = false;
    group->fInCombat = false;
    if (use_alternate_name || (record->flags & W8_MONSTER_FLAG_ALTERNATE_NAME) != 0) {
        group->alternate_name = true;
    } else {
        group->alternate_name = false;
    }
    group->group_state[0] = 0;
    group->group_state[1] = 0;
    group->group_state[2] = 0xff;
    group->group_state[3] = 0xff;
    group->group_state[4] = 0xff;
    group->group_state[5] = 0xff;
    group->spawn_time = CurrentGame().status.world_clock;

    list = &CurrentLevelPopulation().monster_groups;
    if (record->unborn != 0) {
        list = &CurrentLevelPopulation().encounter_groups;
    }
    list->push_back(std::move(owner));

    for (created = 0; created < count; ++created) {
        if (CreateMonsterInfo(group, record, const_cast<glm::vec3*>(position)) == 0) {
            // Failure retires each adopted member through the same relationship boundary.
            const auto members = group->monsters;
            if (members.empty())
                std::erase_if(*list, [group](const auto& entry) { return entry.get() == group; });
            else
                for (int id : members)
                    if (auto* member = FindMonster(id, false))
                        RemoveMonster(member, W8MonsterRemoval::Unload);
            return 0;
        }
    }

    group->leader_location_id = CurrentGame().status.next_monster_location_id - 1;
    group->highlighted_member = -1;

    index = 0;
    while (index < group->MemberIds().size()) {
        int location_id = group->MemberLocationId(index);
        W8MonsterInfo* monster_info = FindMonster(location_id);

        if (!monster_info->fActive) {
            ActivateMonsterInWorld(monster_info);
        }
        ++index;
    }

    group->members_active = true;
    yaw = GetCameraFacingYaw(position);
    MoveMonsterGroupToPosition(group, position, yaw, false, false, false, false);
    RefreshMonsterGroup(group);
    SetMonsterGroupHostility(group, MonsterGroupCalcDefaultDisposition(group), false);

    if (announce_spawn && g_dev_mode) {
        const char* verb = count == 1 ? "appears" : "appear";
        ShowNotice(W8_FONT_PALETTE_RUST,
                   fmt::format("{:d} {} {} nearby!", count, GetMonsterGroupName(group), verb));
    }

    return group;
}

/* The owning collection establishes membership before any relationship or
   budget changes. No id-to-index conversion or separate unregistration. */
bool DestroyMonsterGroup(W8MonsterGroup* group, W8MonsterInfo* monster_info)
{
    auto& population = CurrentLevelPopulation();
    for (auto* groups : {&population.monster_groups, &population.encounter_groups}) {
        const auto found = std::ranges::find(*groups, group, &std::unique_ptr<W8MonsterGroup>::get);
        if (found == groups->end()) continue;
        if (group->members_active) DetachMonsterGroup(group);
        if (group->leader_group_id == 0) {
            if (group->encounter_registered) SpendRandomEncounterBudget();
            ElectAlliedLeaderGroup(group, monster_info);
        } else {
            UnlinkMonsterGroupFromLeader(*group);
        }
        groups->erase(found);
        return true;
    }
    return false;
}

W8MonsterGroup* FindFirstMonsterByID(int monster_id)
{
    unsigned int index;
    W8MonsterGroup* group;

    for (index = 0; index < CurrentLevelPopulation().monster_groups.size(); ++index) {
        group = CurrentLevelPopulation().monster_groups[index].get();
        if (group->monster_id == monster_id) {
            return group;
        }
    }
    for (index = 0; index < CurrentLevelPopulation().encounter_groups.size(); ++index) {
        group = CurrentLevelPopulation().encounter_groups[index].get();
        if (group->monster_id == monster_id) {
            return group;
        }
    }
    return 0;
}

/* Continue through live groups, then encounters, after the previous match. */
W8MonsterGroup* FindNextExistingMonsterByID(int monster_id, W8MonsterGroup* previous)
{
    bool after_previous = previous == nullptr;
    for (const auto* groups :
         {&CurrentLevelPopulation().monster_groups, &CurrentLevelPopulation().encounter_groups}) {
        for (const auto& owner : *groups) {
            if (after_previous && owner->monster_id == monster_id) return owner.get();
            if (owner.get() == previous) after_previous = true;
        }
    }
    return nullptr;
}

/* Pick the group's new leader member: the live member carrying the highest
   navigator leadership_rank, or the first member when none qualify, then hand the
   outgoing leader's script and heard-noise state to the new leader's
   MonsterInfo. */
void ElectGroupLeaderMember(W8MonsterGroup* monster_group)
{
    if (monster_group == 0) {
        Fail("pMonsterGroup");
    }
    if (monster_group->MemberIds().empty()) {
        Fail("(pMonsterGroup->plsMonsterIDList ? pMonsterGroup->plsMonsterIDList->size() : 0)");
    }
    W8MonsterInfo* old_info = FindMonster(monster_group->leader_location_id);
    Monster* old_monster = old_info->p3D;
    int leader_id = 0;
    unsigned int best = 0;
    if (!monster_group->MemberIds().empty()) {
        unsigned int index = 0;
        while (index < monster_group->MemberIds().size()) {
            int member_id = monster_group->MemberLocationId(index);
            Monster* member = GetMonsterByLocationID(member_id);
            W8MonsterInfo* member_info = FindMonster(member_id);
            if (member && member_info->highest_condition < W8_CONDITION_TURNCOAT &&
                (member->runtime_flags & W8_MONSTER_REMOVE_NOW) == 0 &&
                best < static_cast<unsigned int>(member->nav.movement.leadership_rank)) {
                best = member->nav.movement.leadership_rank;
                leader_id = member_id;
            }
            ++index;
        }
        if (leader_id != 0) {
            monster_group->leader_location_id = leader_id;
            goto done;
        }
    }
    monster_group->leader_location_id = monster_group->MemberLocationId(0);
done:
    W8MonsterInfo* leader_info = FindMonster(monster_group->leader_location_id);
    Script* script = old_monster ? old_monster->script.get() : nullptr;
    Monster* leader_monster = leader_info->p3D;
    if (script && leader_monster) {
        leader_monster->SetScript(script->getName(), true);
    }
    leader_info->heard_noise_radius = old_info->heard_noise_radius;
    leader_info->heard_noise_position = old_info->heard_noise_position;
}

/* Elect a successor while the old formation is still intact, then publish
   the new links. No temporary independent encounters or registration repair. */
void ElectAlliedLeaderGroup(W8MonsterGroup* monster_group, W8MonsterInfo* leader_info)
{
    const auto allies = std::to_array(monster_group->allied_group_ids);
    W8MonsterGroup* leader = nullptr;
    unsigned int best = 0;
    for (int id : allies) {
        if (id == 0) continue;
        auto* candidate = FindMonsterGroup(id);
        if (!candidate) continue;
        unsigned int high = 0;
        for (int member_id : candidate->MemberIds()) {
            if (auto* member = GetMonsterByLocationID(member_id))
                high =
                    std::max(high, static_cast<unsigned int>(member->nav.movement.leadership_rank));
        }
        if (!leader || high > best) {
            leader = candidate;
            best = high;
        }
    }
    if (!leader) return;
    LinkMonsterGroupToLeader(nullptr, leader);
    for (int id : allies)
        if (id != 0 && id != leader->group_id)
            LinkMonsterGroupToLeader(leader, FindMonsterGroup(id));

    auto* new_leader_info = FindMonster(leader->leader_location_id);
    if (auto* script = leader_info->p3D->script.get())
        new_leader_info->p3D->SetScript(script->getName(), true);
    new_leader_info->heard_noise_radius = leader_info->heard_noise_radius;
    new_leader_info->heard_noise_position = leader_info->heard_noise_position;
}

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
            !CurrentGame().combat.active()) {
            return;
        }

        if (monster_group->fInCombat) {
            return;
        }
        if (monster_group == 0) {
            Fail("pMonsterGroup != NULL");
        }
        W8MonsterRecord* record = MonsterDBFromSpecies(monster_group->monster_id);
        if (record->untargetable != 0) {
            return;
        }
        if (monster_group == 0) {
            Fail("pMonsterGroup");
        }
        unsigned int index = 0;
        while (index < monster_group->MemberIds().size()) {
            int member_id = monster_group->MemberLocationId(index);
            W8MonsterInfo* member_info = FindMonster(member_id);
            if (member_info->p3D->nav.position_dirty) {
                return;
            }
            ++index;
        }
        if (PartyNavigator()->position_dirty) {
            return;
        }
        if (monster_group == 0) {
            Fail("pMonsterGroup");
        }
        index = 0;
        while (index < monster_group->MemberIds().size()) {
            int member_id = monster_group->MemberLocationId(index);
            W8MonsterInfo* member_info = FindMonster(member_id);
            if ((member_info->p3D->runtime_flags & W8_MONSTER_SCRIPT_WAIT) != 0) {
                return;
            }
            ++index;
        }
        if (monster_group == 0) {
            Fail("pMonsterGroup");
        }
        index = 0;
        if (monster_group->MemberIds().empty()) {
            return;
        }
        while (true) {
            int member_id = monster_group->MemberLocationId(index);
            W8MonsterInfo* member_info = FindMonster(member_id);
            if (member_info != 0 && !member_info->p3D->IsDying()) {
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
                CurrentGame().combat.active() && CurrentGame().combat.state()->round_count != 0) {
                unsigned int live_groups = 0;
                unsigned int group_list_index = 0;
                while (group_list_index < CurrentLevelPopulation().monster_groups.size()) {
                    W8MonsterGroup* other =
                        CurrentLevelPopulation().monster_groups[group_list_index].get();
                    if (other->members_active && other->fInCombat && other->MemberCount() != 0 &&
                        (other->ubDisposition == W8_DISPOSITION_HOSTILE ||
                         CombatAllowsLiveGroups())) {
                        ++live_groups;
                    }
                    ++group_list_index;
                }
                if (live_groups > 1) {
                    ApplyItemEffectToRandomCharacter(33, -1, 0);
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
            RequestRedrawParty();
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

    camera = PartyNavigator()->GetPosition();
    if (!flag) {
        target = camera;
        return MoveMonsterGroupToPosition(group, &target, 0.0f, true, true, false, true);
    }
    member_info = FindMonster(group->leader_location_id);
    angle =
        NormalizeAngle(GetCameraYawRadians() + Random(1000) * kMonsterScatterHeadingRandomScale +
                       g_monster_rotation_offset - kMonsterScatterHeadingBias);
    radius = member_info->p3D->nav.radius;
    for (index = 0; index < W8_MONSTER_GROUP_ALLY_COUNT; ++index) {
        if (group->allied_group_ids[index] != 0) {
            ally = FindMonsterGroup(group->allied_group_ids[index]);
            member_info = FindMonster(ally->leader_location_id);
            if (radius < member_info->p3D->nav.radius) {
                radius = member_info->p3D->nav.radius;
            }
        }
    }
    if (distance <= 0.0f) {
        distance = PartyNavigator()->radius + radius + 1.0f;
    }
    target.y = camera.y;
    target.x = camera.x + distance * sin(angle);
    target.z = camera.z + distance * cos(angle);
    angle = GetCameraFacingYaw(&target);
    return MoveMonsterGroupToPosition(group, &target, angle, true, true, false, true);
}

/* Announces a group's composition on the notice line: the member count and the
   singular/plural record name - the alternate-name record takes the selected
   party member's name prefixed "Al-" - then the active, selectable and visible
   tallies, with the spotted flag adding its own line. */
void ShowMonsterGroupInfoNotice(int group_id)
{
    W8MonsterGroup* group = FindMonsterGroup(group_id);
    auto name = GetMonsterGroupName(group);

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
        if (member_info != 0 && !member_info->p3D->IsDying()) {
            member_info->p3D->runtime_flags |= W8_MONSTER_REMOVE_NOW;
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
unsigned char GiveBirthToMonster(W8MonsterGroup* monster_group)
{
    W8MonsterInfo* member_info;
    unsigned int index;
    int location_id;

    if (monster_group == 0) {
        Fail("pMonsterGroup");
    }
    auto& encounters = CurrentLevelPopulation().encounter_groups;
    const auto found = std::ranges::find_if(
        encounters, [monster_group](const auto& owner) { return owner.get() == monster_group; });
    if (found != encounters.end()) {
        CurrentLevelPopulation().monster_groups.push_back(std::move(*found));
        encounters.erase(found);
        for (index = 0; index < monster_group->MemberIds().size(); ++index) {
            location_id = monster_group->MemberLocationId(index);
            member_info = FindMonster(location_id);
            if (member_info != 0) {
                MoveMonsterToLiveList(member_info);
            }
        }
        RefreshOutwardSightForAllMonsters();
        return 1;
    }
    return 0;
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

    new_group = CreateGroup(monster_id, group->MemberCount(), &spawn, false, false, false);
    if (new_group == 0 || new_group->MemberCount() != group->MemberCount()) {
        return 0;
    }
    for (index = 0; index < new_group->MemberCount(); ++index) {
        new_member = FindMonster(new_group->MemberLocationId(index));
        old_member = FindMonster(group->MemberLocationId(index));
        if (new_member != 0 && old_member != 0) {
            position = new_member->p3D->nav.GetPosition();
            old_member->p3D->nav.SetPositionInternal(&position);
            DeactivateMonster(old_member);
            ActivateMonsterInWorld(new_member);
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

bool GetMonsterGroupEngagementState(int group_id)
{
    return FindMonsterGroup(group_id)->engagement;
}

void SetMonsterGroupNavigatorDirty(W8MonsterGroup* monster_group, bool flag)
{
    W8MonsterInfo* member_info;
    unsigned int index;

    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        member_info = FindMonster(monster_group->MemberLocationId(index));
        member_info->p3D->nav.position_dirty = flag;
    }
}
