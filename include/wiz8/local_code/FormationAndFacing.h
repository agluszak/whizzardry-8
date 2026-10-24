#pragma once

#include "glm/ext/vector_float3.hpp"
#include "wiz8/rules/party_formation.h"

struct W8MonsterInfo;
struct W8CombatSlot;
struct W8PartySlotRow;

unsigned int TurnPartyTo(unsigned int degrees);
void TurnPartyToImmediate(unsigned int degrees, signed char snap);
/* Sync party facing/heading from the camera yaw and refresh the compass. */
void SyncPartyFacingFromCamera(void);

extern const double g_facing_tolerance1;
extern const float g_facing_tolerance0;

signed char DecideFacingForPosition(int position, int target_position);

/* Turn the position's formation facing toward the second position
   the way DecideFacingForPosition resolved it. */
void FacePositionAsDecided(int position, int target_position);
/* Whether the position already faces the second position the way
   DecideFacingForPosition resolved it. */
bool PositionFacesAsDecided(int position, int target_position);
/* Whether the position faces opposite to the second position the
   way DecideFacingForPosition resolved it. */
bool PositionFacesOppositeToDecided(int target_position, int position);
/* Whether the party's world position looks away from the
   monster. */
bool IsPartyLookingAwayFrom(int party_slot, W8MonsterInfo* monster_info);
/* Re-aim party_heading at the selected character's formation
   facing and swing the camera to match, while the camera is not being
   rotated by hand. */
void FaceCameraToSelection(int party_slot);
int GetQuadrantForPosition(glm::vec3 position);

/* Whether the monster faces the party's own position. */
int IsMonsterFacingParty(W8MonsterInfo* monster_info);
/* Whether the first monster faces the second. */
int IsMonsterFacingMonster(W8MonsterInfo* first, W8MonsterInfo* second);
/* Whether the party faces a world point. */
bool IsPartyLookingAt(W8MonsterInfo* monster_info, glm::vec3 point);
/* Whether the second monster looks away from the first. */
bool IsMonsterLookingAwayFrom(W8MonsterInfo* first, W8MonsterInfo* second);
/* Whether the monster sits on the screen side the character
   faces. */
bool IsCharacterFacingMonster(int party_slot, W8MonsterInfo* monster_info);
/* Turn the character's formation facing toward the monster. */
void TurnCharacterTowardMonster(int party_slot, W8MonsterInfo* monster_info);
/* Whether the monster is on the side opposite the character's
   facing. */
bool IsMonsterBehindCharacter(W8MonsterInfo* monster_info, int party_slot);
/* Face the character toward its committed combat slot's target;
   asserts the slot ids are not BAD_INDEX. */
void FaceCharacterTowardCombatTarget(int party_slot, W8CombatSlot* target);
