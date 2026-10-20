#ifndef WIZ8_LOCAL_CODE_FACTIONS_H
#define WIZ8_LOCAL_CODE_FACTIONS_H

#include "wiz8/data/faction_ids.h"

struct W8Chunk;

namespace wiz8 {
class File;
}

enum W8FactionDisposition : unsigned char {
    W8_FACTION_HOSTILE = 0,
    W8_FACTION_NEUTRAL = 1,
    W8_FACTION_FRIENDLY = 2
};

enum W8Disposition : unsigned char {
    W8_DISPOSITION_NEUTRAL = 0,
    W8_DISPOSITION_HOSTILE = 1,
    W8_DISPOSITION_FRIENDLY = 2
};

W8FactionDisposition GetFactionDisposition(signed char faction);

/* `other` as the party reads the live score band; any other
   target is answered by the static relation matrix. */
W8FactionDisposition GetFactionDispositionToward(signed char faction, signed char other);

void SetFactionDispositionBand(signed char faction, W8FactionDisposition band);

/* Faction change entry point - op 1 with a faction outside
   unaligned/party routes mode 0 to the witnessed-offense path and modes
   1..3 to a direct disposition delta. */
void ApplyFactionChange(char mode, char op, signed char faction, int value);
/* Bump the faction's offense count and apply the penalty the
   victim's record and the running count select. */
struct W8MonsterInfo;
void RecordFactionOffense(signed char faction, W8MonsterInfo* monster_info);
/* Clamp disposition_score + delta to 0..99, stamp the world clock
   when the band moved, and post the worsened/improved notice. */
void AdjustFactionDisposition(signed char faction, char delta);
/* FATA section save and load. */
void SaveFactionState(wiz8::File& file);
void LoadFactionState(W8Chunk& file);
#endif
