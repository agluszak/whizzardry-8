#include "wiz8/local_code/Noise.h"

#include <vector>

#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "local_code/Factions.h"
#include "wiz8/entities/monster.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/platform/random.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/xstatus.h"

/* Retail Local Code\Noise.cpp. */

/* The remaining audibility of a noise after distance and the per-hop region
   penalty: positive means the monster group still hears it. */
int NoiseHearingMargin(int radius, int range, int hops)
{
    return radius + hops * -25000 - range;
}

void AlertMonsterGroupsToNoise(const glm::vec3* position, int radius, int flag)
{
    if (g_status.world_suspended) {
        return;
    }
    glm::vec3 noise_position = *position;
    for (unsigned int group_index = 0; group_index < CurrentLevelPopulation().monster_groups.size();
         ++group_index) {
        W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[group_index].get();
        W8MonsterInfo* info = FindMonster(group->leader_location_id);
        if (info->p3D->deaf) {
            continue;
        }
        if (flag == 1) {
            if (g_combat.active() && group->fInCombat) {
                continue;
            }
        } else if (flag == 0 && info->ubDisposition != W8_DISPOSITION_HOSTILE) {
            continue;
        }
        W8MonsterRecord* record = GetMonsterDataForInfo(info);
        glm::vec3 monster_position = info->p3D->nav.GetPosition();
        int remaining = radius - static_cast<int>(glm::length((monster_position - noise_position)));
        if (remaining <= 0) {
            continue;
        }
        if (Random(100) >=
            remaining * 0.001f + record->attribute_values[W8_MONSTER_ATTRIBUTE_SENSES]) {
            continue;
        }
        if (group->leader_group_id != 0) {
            W8MonsterGroup* leader = FindMonsterGroup(group->leader_group_id);
            info = FindMonster(leader->leader_location_id);
            if (info->p3D->deaf) {
                continue;
            }
        }
        if (flag == 1 && g_combat.active()) {
            float range = static_cast<float>(radius);
            int hops;
            if (GetWorldSpatialIndex()->TestNoiseLineOfSight(&monster_position, &noise_position,
                                                             &range, &hops) == 0) {
                continue;
            }
            // Truncate the float move-range to int before scaling, matching retail.
            if (static_cast<int>(GetMonsterCombatMoveRange(info)) * 1000 < range) {
                continue;
            }
        }
        if (remaining <= info->heard_noise_margin) {
            continue;
        }
        if (flag == 1) {
            MonsterGroupEnterCombat(group);
        }
        info->heard_noise_radius = radius;
        info->heard_noise_margin = remaining;
        info->heard_noise_position = noise_position;
    }
}

void AlertWorldNoise(void)
{
    glm::vec3 position = PartyNavigator()->GetPosition();
    AlertMonsterGroupsToNoise(&position, 50000, 1);
}

void AlertCombatNoise(bool large_radius)
{
    if (g_status.world_suspended) {
        return;
    }
    glm::vec3 position = PartyNavigator()->GetPosition();
    AlertMonsterGroupsToNoise(&position, large_radius ? 0x927c : 25000, 0);
}
