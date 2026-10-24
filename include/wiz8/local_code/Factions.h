#ifndef WIZ8_LOCAL_CODE_FACTIONS_H
#define WIZ8_LOCAL_CODE_FACTIONS_H

#include "wiz8/data/faction_ids.h" // IWYU pragma: export

/* `other` as the party reads the live score band; any other
   target is answered by the static relation matrix. */
W8FactionDisposition GetFactionDispositionToward(signed char faction, signed char other);

void SetFactionDispositionBand(signed char faction, W8FactionDisposition band);

/* Faction change entry point - op 1 with a faction outside
   unaligned/party routes mode 0 to the witnessed-offense path and modes
   1..3 to a direct disposition delta. */
void ApplyFactionChange(signed char mode, signed char op, signed char faction, int value);
/* Bump the faction's offense count and apply the penalty the
   victim's record and the running count select. */
struct W8MonsterInfo;
void RecordFactionOffense(signed char faction, W8MonsterInfo* monster_info);
/* Clamp disposition_score + delta to 0..99, stamp the world clock
   when the band moved, and post the worsened/improved notice. */
void AdjustFactionDisposition(signed char faction, signed char delta);
#endif
