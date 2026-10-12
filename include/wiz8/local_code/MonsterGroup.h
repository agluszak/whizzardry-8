#ifndef WIZ8_LOCAL_CODE_MONSTER_GROUP_H
#define WIZ8_LOCAL_CODE_MONSTER_GROUP_H

#include <cstdint>
#include <source_location>

#include <string>
#include <cstddef>

#include "surrender/srMath.h"

#include <vector>
#include <span>

#include "wiz8/monster_actions.h"
#include "wiz8/local_code/Factions.h"

struct W8MonsterRecord;
struct W8MonsterGroup;
struct W8MonsterInfo;

/* The dispositions a group or member carries. */

bool DestroyMonsterGroup(W8MonsterGroup* monster_group, W8MonsterInfo* monster_info);

/* A live monster group. Saves store it as wiz8::retail::MonsterGroup. */
struct W8MonsterGroup {
    std::span<const std::int32_t> MemberIds() const
    {
        const std::vector<std::int32_t>* ids = monsters;
        if (!ids)
            return {};
        return *ids;
    }

    std::int32_t MemberLocationId(std::size_t index) const
    {
        const auto ids = MemberIds();
        if (index >= ids.size())
            return -1;
        return ids[index];
    }

    std::int32_t group_id;              /* GroupIndex ID lookup key */
    std::uint32_t member_count; /* decremented when members leave */
    std::vector<std::int32_t>* monsters; /* location IDs of live members */
    /* Refreshed together by the targeting visibility pass. */
    std::int32_t visible_member_count;
    std::int32_t selectable_member_count;
    std::int32_t active_member_count; /* recomputed from member conditions */
    std::int32_t monster_id;
    /* The mean of the live members' positions, recomputed on demand. */
    srVector3T<float> centre;
    /* Set once the group's members are activated and positioned in the
       world; targeting and the AI walk skip groups without it. Cleared at
       load/init. */
    bool members_active;
    /* Cleared after the record loads and again when the group leaves
       combat. */
    bool fInCombat;
    /* The group's disposition, the value SetMonsterGroupDisposition
       writes and each member's ubDisposition copies. At DISP_HOSTILE the group
       is live regardless of the global hostility gate; anything else has to
       pass that gate as well. */
    W8Disposition ubDisposition;
    std::uint8_t unknown_2b;
    /* Selects which of the record's two name sets a member is displayed
       under. Group creation presets it for alternate-name records, and the
       wandering-group detection pass sets it once the party spots the group. */
    bool alternate_name;
    /* Per-group state blob; [0] is the monster-awareness grant
       latch (MonsterManager fills each member's awareness once), [0x6d] is
       the MIPE-written tail byte. */
    std::uint8_t group_state[0x6e];
    /* Which member of the group the party currently has picked out,
       by location id, and -1 when none - which is how the group loads. Cycling
       through the group's targetable members reads it to know where it is and
       writes back where it got to. */
    std::int32_t highlighted_member;
    /* The location id of the member that leads the group. RemoveMonster
       compares it against the departing member's before handing leadership on. */
    std::int32_t leader_location_id;
    /* The group this one follows. Walking it is how a member request is
       redirected to the group that actually leads the formation, and a zero
       ends the walk. */
    std::int32_t leader_group_id;
    /* Up to four allied group ids. The notification pass walks all four
       unconditionally and skips the zero entries, so the array is fixed-size
       rather than terminated. */
    std::int32_t allied_group_ids[4];
    /* Copied verbatim onto every member's live Monster when the formation
       is re-applied. Unaligned inside this packed record, which is why the copy
       comes out as twelve byte moves rather than three dword ones. */
    srVector3T<float> formation;
    /* Set by MonGen when the group is spawned as an active encounter;
       cleared by UnregisterActiveEncounterGroup, which also raises
       encounter_ended. */
    bool encounter_registered;
    /* 0xc4 is a saved-record version: at 2 and above the loader reads one more
       byte, and below 3 it clears forced_neutral that older saves never wrote. */
    std::uint32_t version;
    /* Group engagement state; 0xc9 ticks spent in it (reset on change,
       the AI checks < 3 for "just engaged"). */
    bool engagement;
    std::uint8_t engagement_ticks;
    /* Set when a script/NPC pass forces the group neutral; cleared on
       load and when hostility is recomputed. Older saves never wrote it. */
    bool forced_neutral;
    /* World_clock stamp of the last hostility application; the
       faction-reaction pass refuses to reapply inside the record cooldown. */
    std::int32_t hostility_set_at;
    /* When this group was last budgeted. UpdateRandomEncounterBudget
       advances it by the elapsed time and the culling pass measures against it. */
    std::int32_t spawn_time;
    /* Raised as encounter_registered is cleared - the group was an
       active encounter once. */
    bool encounter_ended;
    std::uint8_t unknown_d4[0x57];
};

std::uint32_t GetMonsterGroupIndexByID(std::int32_t group_id, bool assert_on_failure = true,
                                      std::source_location caller = std::source_location::current());
W8MonsterGroup* GetMonsterGroupByListIndex(std::uint32_t group_list_index);
W8MonsterGroup* FindMonsterGroup(std::int32_t group_id, bool assert_on_failure = true,
                                 std::source_location caller = std::source_location::current());
bool GetMonsterGroupEngagementState(std::int32_t group_id);
std::uint8_t ApplyToMonsterGroupLeader(W8MonsterGroup* monster_group,
                                        const srVector3T<float>* position, bool follow_leader);
/* Whether the group is loaded, in combat, and still has members; hostile
   groups are live on that alone, others also need CombatAllowsLiveGroups. */
bool IsMonsterGroupLive(W8MonsterGroup* monster_group);
/* The Nth live combat group in plsMonsterGroupList order; null when none. */
W8MonsterGroup* GetLiveMonsterGroupAtIndex(std::int32_t index);
/* Channel-12 notices summarizing a group's name, count, and visibility. */
void ShowMonsterGroupInfoNotice(std::int32_t group_id);
/* Whether the group has a member placed and rendered in the world; a nonzero
   second argument also demands the member's party-threat flag. */
bool MonsterGroupHasRenderableMember(W8MonsterGroup* monster_group, bool require_threat);
/* Write `state` into the group's engagement byte and propagate it to its four
   allied groups; while the byte is set, each call ticks the counter beside
   it. The record kinds the special encounter ids carry ignore a set. */
void SetMonsterGroupEngagementState(std::int32_t group_id, bool state);
bool MoveMonsterGroupToPosition(W8MonsterGroup* group, const srVector3T<float>* position, float yaw,
                                bool proximity_check, bool include_allies, bool flatten_y,
                                bool alternate_radius);
/* Place a monster group relative to the party camera: with flag clear the
   group moves straight to the camera position, and with flag set it picks a
   point at the requested distance on a random angle around the camera yaw,
   widened to the largest allied-group radius. Answers the movement call's
   result so callers can branch on success. */
bool PositionMonsterGroupNearCamera(W8MonsterGroup* group, float distance, float yaw, bool flag);
void RecountActiveMonsterGroupMembers(W8MonsterGroup* monster_group);
/* Refresh the group's cached centre; a null centre out-pointer
   keeps only the cache update, which is how SpawnMonsters uses it. */
void GetMonsterGroupCentre(W8MonsterGroup* monster_group, srVector3T<float>* centre);
W8MonsterGroup* FindFirstMonsterByID(std::int32_t monster_id);
W8MonsterGroup* FindNextExistingMonsterByID(std::int32_t monster_id, W8MonsterGroup* previous);
std::uint8_t GiveBirthToMonster(W8MonsterGroup* monster_group);
W8MonsterGroup* CreateGroup(std::uint32_t monster_id, std::uint32_t count,
                            const srVector3T<float>* position, bool use_alternate_name,
                            bool announce_spawn, bool place_on_ground);

void ResetMonsterGroupTurnState(void);
void RebindMonsterGroupScripts(void);

void DespawnMonsterGroup(W8MonsterGroup* monster_group);
void ActivateGroupMembers(W8MonsterGroup* monster_group, W8MonsterActivationMode mode);
std::string GetMonsterGroupName(W8MonsterGroup* monster_group);
void RefreshMonsterGroupAndAllies(W8MonsterGroup* monster_group);

W8MonsterRecord* MonsterGroupGetRecord(W8MonsterGroup* group);
void RefreshMonsterGroup(W8MonsterGroup* monster_group);
void DetachMonsterGroup(W8MonsterGroup* monster_group);

std::uint8_t RemoveAllGroupMembers(W8MonsterGroup* monster_group);
/* MonsterGroup.cpp: respawns a same-sized group of a different monster id
   beside the source group, deactivating the old members as each replacement
   activates; NULL on failure. */
W8MonsterGroup* ReplaceMonsterGroupSpecies(W8MonsterGroup* group, std::uint32_t monster_id);
void SetMonsterGroupNavigatorDirty(W8MonsterGroup* monster_group, bool flag);
bool MonsterGroupAllMembersDying(W8MonsterGroup* monster_group);
void LoadMonsterGroupMembers(W8MonsterGroup* monster_group);
/* Out-of-combat refresh: proximity hostility for unaligned neutrals, then
   default disposition on the intelligence-squared cooldown. */
void RefreshMonsterGroupHostility(W8MonsterGroup* monster_group);
void MonsterGroupEnterCombat(W8MonsterGroup* monster_group);
/* Re-elect the group's leader member: the live member carrying the highest
   navigator leadership_rank takes over leader_location_id, else the first member does, and
   the outgoing leader's script and heard-noise state move across. */
void ElectGroupLeaderMember(W8MonsterGroup* monster_group);
/* Re-elect the allied leader group when this group's leader falls: the allied
   group whose members hold the highest navigator leadership_rank leads the rest,
   and the fallen leader's script and heard-noise state carry to the new
   leader's MonsterInfo. */
void ElectAlliedLeaderGroup(W8MonsterGroup* monster_group, W8MonsterInfo* leader_info);
/* Marks every live member of the group and of its allied groups for removal. */
void MarkMonsterGroupForRemoval(std::int32_t group_id);
/* Write the control state onto every live member of the group. */
void SetMonsterGroupControlState(W8MonsterGroup* monster_group,
                                 W8MonsterControlState control_state);
/* Nonzero when the group - or one of its allied groups - has a member whose
   highest condition is in the 0x0d..0x11 incapacitated band. */
bool MonsterGroupHasIncapacitatedMember(std::int32_t group_id);
/* Retire a group and its allies from the encounter budget,
   following the leader chain first. */
void RetireMonsterGroupAndAllies(W8MonsterGroup* monster_group);

void MonsterGroupLeaveCombat(W8MonsterGroup* monster_group);

void SetMonsterGroupFormation(W8MonsterGroup* monster_group, const srVector3T<float>* formation);
void ReapplyMonsterGroupFormations(void);
void RepairMonsterGroupLeaderLinks(void);
void ApplyDefaultMonsterGroupSounds(void);

#endif
