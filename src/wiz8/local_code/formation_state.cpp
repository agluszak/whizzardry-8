#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/rules/party_formation.h"

/* This initializer lies in the reviewed attribution gap between Magic
   Effects.cpp and Formation & Facing.cpp, 0x70 below the anchored Formation
   & Facing hull start at 0x005545F0. Keep the file classified as an
   unresolved fragment; the descriptive name does not prove original-TU
   identity. */

/* A constructed formation - nobody seated - whose positions all start at
   facing 4. */
void InitializePartyFormation(W8PartyFormationState* formation)
{
    *formation = {};
    for (auto& position : formation->positions) position.facing = 4;
}
