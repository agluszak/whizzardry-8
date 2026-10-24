#pragma once

#include "glm/ext/vector_float3.hpp"

struct W8MonsterInfo;
struct W8MonsterGroup;
struct W8VisibilityRecord;

enum class W8VisibilityCondition : unsigned char {
    SightState = 0,
    CanSee = 1,
    FirstLineOfSightFlag = 2,
    ThirdLineOfSightFlag = 3,
    ProjectileOriginLineOfSight = 4,
    SpellOriginLineOfSight = 5,
    GeneralLineOfSight = 6
};

void RefreshAllSight(void);
/* Put the sight subsystem back to its starting state. */
void RefreshOutwardSightForAllMonsters(void);
void RefreshInwardSightForAllMonsters(void);
void RefreshMonsterSight(W8MonsterInfo* monster_info);
void ResetAndRefreshAllSight(void);
void AgeAllMonsterSight(void);
void UpdateMonsterSight(W8MonsterInfo* monster_info, bool direction, bool use_bounds);

bool MonsterGroupHasVisibleThreat(W8MonsterGroup* group);
signed char GetMonsterSpellSightIndex(const W8MonsterInfo* monster);

bool IsVisibleUnderConditions(const W8MonsterInfo* monster, const W8VisibilityRecord* row,
                              W8VisibilityCondition condition);
/* Whether one group's leader member can see the other's: far-clip distance
   first, then the outward sight record and a line-of-sight trace. */
bool MonsterGroupCanSeeGroup(W8MonsterGroup* source, W8MonsterGroup* target);
W8VisibilityRecord* FindMonToMonVisibility(W8MonsterInfo* source, W8MonsterInfo* target);
