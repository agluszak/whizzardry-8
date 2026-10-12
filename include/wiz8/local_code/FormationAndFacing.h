#pragma once

#include <cstdint>
#include "surrender/srMath.h"

struct W8MonsterInfo;
struct W8CombatSlot;
struct W8PartySlotRow;
struct W8PartyFormationState;

std::uint32_t TurnPartyTo(std::uint32_t degrees);
void TurnPartyToImmediate(std::uint32_t degrees, char snap);
/* Sync party facing/heading from the camera yaw and refresh the compass. */
void SyncPartyFacingFromCamera(void);

extern const double g_facing_tolerance1;
extern const float g_facing_tolerance0;

std::int8_t DecideFacingForPosition(std::int32_t position, std::int32_t target_position);

void RebuildPartyStatus(W8PartyFormationState* status, const W8PartySlotRow* party_rows);

/* Turn the position's formation facing toward the second position
   the way DecideFacingForPosition resolved it. */
void FacePositionAsDecided(std::int32_t position, std::int32_t target_position);
/* Whether the position already faces the second position the way
   DecideFacingForPosition resolved it. */
bool PositionFacesAsDecided(std::int32_t position, std::int32_t target_position);
/* Whether the position faces opposite to the second position the
   way DecideFacingForPosition resolved it. */
bool PositionFacesOppositeToDecided(std::int32_t target_position, std::int32_t position);
/* Whether the party's world position looks away from the
   monster. */
bool IsPartyLookingAwayFrom(std::int32_t party_slot, W8MonsterInfo* monster_info);
/* Whether the character can hold a formation place at all: alive
   and in better shape than the hostile conditions. */
bool CanHoldFormationPlace(std::int32_t party_slot);
void RestoreCombatFormation(void);
/* Give one joining party slot its formation position, scanning
   the row order for the first free one. */
void PlaceCharacterInFormation(W8PartyFormationState* formation, std::int32_t slot);
/* Move one party position into a formation row and column,
   updating both rows' occupant lists. */
void SetFormationPosition(W8PartyFormationState* formation, std::int32_t slot, std::int8_t new_row,
                          std::int8_t new_column, bool announce, bool detach, bool update_facing);
/* Re-place the positions left in a row after one moved out: a
   single occupant goes to column 0, two pack toward column 2. */
void CompactFormationRow(W8PartyFormationState* formation, std::uint8_t row);

void InitializePartyFormation(W8PartyFormationState* state);
void CopyPartyFormationState(W8PartyFormationState* dst, const W8PartyFormationState* src);
/* Remember the formation combat started with. */
void SaveCombatFormation(void);
/* Reconcile an edited formation against the live one, re-seating
   characters that can no longer hold their edited place and writing the
   result back into both formations. */
void ReconcilePartyFormation(W8PartyFormationState* edited, W8PartyFormationState* live);
/* Keep one slot's seat in step with its liveness - death stashes
   its quadrant in bOldQuadrant and unseats it, revival re-seats it there or
   in a fallback row. */
void UpdateFormationSlotState(W8PartyFormationState* formation, std::int32_t slot);
/* Seat one party slot in a formation row, shuffling its occupants
   to make room, or refuse a full row with a notice. */
void SeatFormationSlotInRow(W8PartyFormationState* formation, std::int32_t slot, std::int32_t row);
/* Swap two party slots' formation positions. */
void SwapFormationSlots(W8PartyFormationState* formation, std::int32_t slot_a, std::int32_t slot_b);
/* Re-aim party_heading at the selected character's formation
   facing and swing the camera to match, while the camera is not being
   rotated by hand. */
void FaceCameraToSelection(std::int32_t party_slot);
std::int32_t GetQuadrantForPosition(srVector3T<float> position);

/* Whether the monster faces the party's own position. */
bool IsMonsterFacingParty(W8MonsterInfo* monster_info);
/* Whether the first monster faces the second. */
bool IsMonsterFacingMonster(W8MonsterInfo* first, W8MonsterInfo* second);
/* Whether the party faces a world point. */
bool IsPartyLookingAt(W8MonsterInfo* monster_info, srVector3T<float> point);
/* Whether the second monster looks away from the first. */
bool IsMonsterLookingAwayFrom(W8MonsterInfo* first, W8MonsterInfo* second);
/* Whether the monster sits on the screen side the character
   faces. */
bool IsCharacterFacingMonster(std::int32_t party_slot, W8MonsterInfo* monster_info);
/* Turn the character's formation facing toward the monster. */
void TurnCharacterTowardMonster(std::int32_t party_slot, W8MonsterInfo* monster_info);
/* Whether the monster is on the side opposite the character's
   facing. */
bool IsMonsterBehindCharacter(W8MonsterInfo* monster_info, std::int32_t party_slot);
/* Face the character toward its committed combat slot's target;
   asserts the slot ids are not BAD_INDEX. */
void FaceCharacterTowardCombatTarget(std::int32_t party_slot, W8CombatSlot* target);
