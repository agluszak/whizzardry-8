#include "wiz8/local_code/FormationAndFacing.h"

#include <cassert>
#include <cmath>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/combat/combat.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/utility.h"
#include "wiz8/world/world.h"

/* Original translation unit: Local Code\Formation & Facing.cpp. */

/* Fills in the party's own world position. */
/* A full turn, and the half-quadrant the bearing is biased by so that a
   quadrant is centred on its facing rather than starting at it. */
enum { W8_DEGREES_PER_TURN = 360, W8_DEGREES_PER_QUADRANT = 90 };

/* Which of the four quadrants around the party a world position falls in.
   The bearing to the position is taken relative to the party's facing, wrapped
   into a single turn, then biased by half a quadrant before the divide - so
   quadrant zero is the 90 degrees centred on straight ahead rather than the 90
   beginning there. */
int GetQuadrantForPosition(glm::vec3 position)
{
    glm::vec3 party;
    int bearing;

    party = cameraLocation();
    /* The heading comes back in radians; the party facing is kept in degrees. */
    bearing = static_cast<int>(NormalizeAngle(GetHeadingAngle(&party, &position)) * 57.295776f);
    bearing -= CurrentGame().status.party_facing;
    if (bearing < 0) {
        bearing += W8_DEGREES_PER_TURN;
    }
    return ((bearing + W8_DEGREES_PER_QUADRANT / 2) % W8_DEGREES_PER_TURN) /
           W8_DEGREES_PER_QUADRANT;
}

/* The facing answer that means "no preference", which never disagrees with
   whatever a position is already facing. */
enum { W8_FACING_ANY = 4 };

const double g_facing_tolerance1 = 2.3561944500000003;

const float g_facing_tolerance0 = 0.05f;

/* When set during combat, camera yaw updates party_heading only and leaves
   party_facing alone. Cleared, the free-look path also writes party_facing. */
static bool g_combat_preserve_party_facing;

/* Sync party facing/heading from the camera yaw and refresh the formation
   compass. Stopped level movement and combat-with-preserve skip writing facing. */
void SyncPartyFacingFromCamera(void)
{
    unsigned int yaw;
    bool movement_stopped;

    yaw = static_cast<unsigned int>(GetCameraYawDegrees()) % W8_DEGREES_PER_TURN;
    movement_stopped = IsLevelMovementStopped();
    if ((!CurrentGame().combat.has_value() || !g_combat_preserve_party_facing) &&
        !movement_stopped) {
        CurrentGame().status.party_facing = static_cast<int>(yaw);
        if (yaw != CurrentGame().status.party_heading) {
            CurrentGame().status.party_heading = yaw;
            UpdateFormationCompass();
            if (static_cast<unsigned int>(GetCameraYawDegrees()) % W8_DEGREES_PER_TURN != yaw) {
                SetCameraYawDegrees(static_cast<float>(yaw));
            }
        }
    } else if (yaw != CurrentGame().status.party_heading) {
        CurrentGame().status.party_heading = yaw;
        UpdateFormationCompass();
        if (static_cast<unsigned int>(GetCameraYawDegrees()) % W8_DEGREES_PER_TURN != yaw) {
            SetCameraYawDegrees(static_cast<float>(yaw));
        }
    }
}

/* Turn the party to a new heading, moving the camera with it unless it is
   already looking that way. */
unsigned int TurnPartyTo(unsigned int degrees)
{
    unsigned int previous;

    CurrentGame().status.party_facing = degrees;
    previous = CurrentGame().status.party_heading;
    if (degrees != CurrentGame().status.party_heading) {
        CurrentGame().status.party_heading = degrees;
        UpdateFormationCompass();
        unsigned int camera_yaw = static_cast<unsigned int>(GetCameraYawDegrees());
        previous = camera_yaw / W8_DEGREES_PER_TURN;
        if (camera_yaw % W8_DEGREES_PER_TURN != degrees) {
            SetCameraYawDegrees(static_cast<float>(degrees));
        }
    }
    return previous;
}

/* The same turn, with the option of snapping the camera round rather than
   swinging it. */
void TurnPartyToImmediate(unsigned int degrees, signed char snap)
{
    if (degrees == CurrentGame().status.party_heading) {
        return;
    }
    CurrentGame().status.party_heading = degrees;
    UpdateFormationCompass();
    if (static_cast<unsigned int>(GetCameraYawDegrees()) % W8_DEGREES_PER_TURN == degrees) {
        return;
    }
    if (snap) {
        TurnCameraToDegrees(static_cast<float>(degrees));
    } else {
        SetCameraYawDegrees(static_cast<float>(degrees));
    }
}

/* While the camera is not being rotated by hand, re-aim the party heading at
   the selected slot's formation facing and swing the camera to match -
   snapping or gliding per the rotation style. */
void FaceCameraToSelection(int party_slot)
{
    unsigned int heading;

    if (g_settings.camera_rotation_mode != W8_CAMERA_ROTATION_SELECTED_CHARACTER ||
        CurrentGame().status.selected_character != party_slot) {
        return;
    }
    heading =
        CurrentGame().status.party_facing +
        static_cast<unsigned char>(CurrentGame().party.formation.positions[party_slot].facing) *
            W8_DEGREES_PER_QUADRANT;
    TurnPartyToImmediate(heading, g_settings.camera_rotation_style);
}

/* Face one position the way the rules say it should, unless the rules have no
   preference or it already faces that way. */
void FacePositionAsDecided(int position, int target_position)
{
    signed char facing = DecideFacingForPosition(position, target_position);

    if (facing != W8_FACING_ANY &&
        CurrentGame().party.formation.positions[position].facing != facing) {
        CurrentGame().party.formation.positions[position].facing = facing;
        RefreshFormationBoard();
    }
}

/* Whether a position already faces the way the rules want. No preference
   always agrees. */
bool PositionFacesAsDecided(int position, int target_position)
{
    signed char facing = DecideFacingForPosition(position, target_position);

    if (facing == W8_FACING_ANY) {
        return true;
    }
    return facing == CurrentGame().party.formation.positions[position].facing;
}

/* Whether a position is facing exactly away from where the rules want it -
   two of the four facings apart. */
bool PositionFacesOppositeToDecided(int target_position, int position)
{
    signed char facing = DecideFacingForPosition(position, target_position);
    int difference;

    if (facing == W8_FACING_ANY) {
        return false;
    }
    difference = facing - CurrentGame().party.formation.positions[position].facing;
    if (difference < 0) {
        difference = -difference;
    }
    return difference == 2;
}

/* Whether the party is looking far enough away from a point to count as not
   facing it, measured as the shortest way round. */
bool IsPartyLookingAwayFrom(int, W8MonsterInfo* monster_info)
{
    glm::vec3 monster_position = monster_info->entity->nav.GetPosition();
    glm::vec3 party_position = monster_info->entity->owner_world.party.position();
    float bearing = NormalizeAngle(GetHeadingAngle(&monster_position, &party_position));
    float facing = monster_info->entity->nav.GetYaw();

    return ShortestAngleDistance(bearing, facing) >= g_facing_tolerance1;
}

/* The facing step from `position`'s quadrant toward `target_position`'s quadrant: the
   target quadrant verbatim for rows 0-3, the opposite quadrant when the
   target sits in the centre, and the no-facing code 4 when either side is
   unseated or both match. */
signed char DecideFacingForPosition(int position, int target_position)
{
    const int source = CurrentGame().party.formation.rowOf(position);
    const int target = CurrentGame().party.formation.rowOf(target_position);

    assert(source != -1);
    if (target == -1 || source == target) {
        return 4;
    }
    switch (target) {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 3;
    case 4:
        switch (source) {
        case 0:
            return 2;
        case 1:
            return 3;
        case 2:
            return 0;
        case 3:
            return 1;
        }
        break;
    }
    return 4;
}

/* Whether the party is looking at a point, measured as a plain difference
   rather than the shortest way round - so a bearing either side of the wrap
   answers no. */
bool IsPartyLookingAt(W8MonsterInfo* monster_info, glm::vec3 point)
{
    glm::vec3 monster_position = monster_info->entity->nav.GetPosition();
    float bearing = NormalizeAngle(GetHeadingAngle(&monster_position, &point));

    return fabsf(bearing - monster_info->entity->nav.GetYaw()) <= g_facing_tolerance0;
}

/* Whether the monster currently faces the party's own position, inside the
   usual facing tolerance - the mirror of IsPartyLookingAt. */
int IsMonsterFacingParty(W8MonsterInfo* monster_info)
{
    glm::vec3 party_position = monster_info->entity->owner_world.party.position();
    return IsPartyLookingAt(monster_info, party_position);
}

/* Whether the first monster faces the second, inside the usual tolerance. */
int IsMonsterFacingMonster(W8MonsterInfo* first, W8MonsterInfo* second)
{
    glm::vec3 second_position = second->entity->nav.GetPosition();
    return IsPartyLookingAt(first, second_position);
}

/* Whether the second monster is looking away from the first, measured the
   shortest way round like IsPartyLookingAwayFrom. */
bool IsMonsterLookingAwayFrom(W8MonsterInfo* first, W8MonsterInfo* second)
{
    glm::vec3 first_position = first->entity->nav.GetPosition();
    glm::vec3 second_position = second->entity->nav.GetPosition();
    float bearing = NormalizeAngle(GetHeadingAngle(&second_position, &first_position));
    float facing = second->entity->nav.GetYaw();

    if (static_cast<float>(g_facing_tolerance1) <= ShortestAngleDistance(bearing, facing)) {
        return true;
    }
    return false;
}

/* The monster's bearing off the camera direction folded into one of the four
   screen sides - the same 0..3 facing values the formation positions carry.
   The facing checks use GetQuadrantForPosition and share the same
   invalid-quadrant assertion. */
bool IsCharacterFacingMonster(int party_slot, W8MonsterInfo* monster_info)
{
    glm::vec3 monster_position = monster_info->entity->nav.GetPosition();
    signed char side;

    switch (GetQuadrantForPosition(monster_position)) {
    case 0:
        side = 0;
        break;
    case 1:
        side = 1;
        break;
    case 2:
        side = 2;
        break;
    case 3:
        side = 3;
        break;
    default:
        return true;
    }
    return side == CurrentGame().party.formation.positions[party_slot].facing;
}

/* Turn the character's formation facing toward the side the monster is on. */
void TurnCharacterTowardMonster(int party_slot, W8MonsterInfo* monster_info)
{
    glm::vec3 monster_position = monster_info->entity->nav.GetPosition();
    signed char side;

    switch (GetQuadrantForPosition(monster_position)) {
    case 0:
        side = 0;
        break;
    case 1:
        side = 1;
        break;
    case 2:
        side = 2;
        break;
    case 3:
        side = 3;
        break;
    default:
        return;
    }
    if (CurrentGame().party.formation.positions[party_slot].facing != side) {
        CurrentGame().party.formation.positions[party_slot].facing = side;
        RefreshFormationBoard();
    }
}

/* Whether the monster sits on the side opposite the one the character faces -
   the being-snuck-up-on test. */
bool IsMonsterBehindCharacter(W8MonsterInfo* monster_info, int party_slot)
{
    glm::vec3 monster_position = monster_info->entity->nav.GetPosition();
    signed char side;
    int difference;

    switch (GetQuadrantForPosition(monster_position)) {
    case 0:
        side = 0;
        break;
    case 1:
        side = 1;
        break;
    case 2:
        side = 2;
        break;
    case 3:
        side = 3;
        break;
    default:
        return false;
    }
    difference = side - CurrentGame().party.formation.positions[party_slot].facing;
    if (difference < 0) {
        difference = -difference;
    }
    return difference == 2;
}

void FaceCharacterTowardCombatTarget(int party_slot, W8CombatSlot* target)
{
    signed char facing;
    glm::vec3 position;
    bool has_position = false;

    switch (target->iType) {
    case W8_TARGET_KIND_CHARACTER:
    case W8_TARGET_KIND_CHARACTER_INDIRECT:
        assert(target->iChar != -1);
        facing = DecideFacingForPosition(party_slot, target->iChar);
        if (facing == W8_FACING_ANY) {
            return;
        }
        break;
    case W8_TARGET_KIND_MONSTER:
        assert(target->iMonsterID != -1);
        if (auto* monster = GetMonsterByLocationID(target->iMonsterID))
            position = monster->nav.GetPosition();
        else
            return;
        has_position = true;
        break;
    case W8_TARGET_KIND_PLACE:
        position = target->point;
        has_position = true;
        break;
    default:
        return;
    }
    if (has_position) {
        switch (GetQuadrantForPosition(position)) {
        case 0:
            facing = 0;
            break;
        case 1:
            facing = 1;
            break;
        case 2:
            facing = 2;
            break;
        case 3:
            facing = 3;
            break;
        default:
            return;
        }
    }
    if (CurrentGame().party.formation.positions[party_slot].facing != facing) {
        CurrentGame().party.formation.positions[party_slot].facing = facing;
        RefreshFormationBoard();
    }
}
