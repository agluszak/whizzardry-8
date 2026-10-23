#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;
class Inventory;

/* Level 1 (Ascension Peak): the relic encounters, the avalanche and the
   Aletheides relic exchanges. */
std::unique_ptr<LevelScript> MakeAscensionScript(const ScriptContext& context);

/* How many of the three Ascension Peak relic items (0x242-0x244) the party
   carries; the type-5 world-cursor handler gates "AP_SeenBodies" on it. */
int CountAscensionPeakItems(const Inventory& inventory); /* 0x004DF810 */
/* The Alfie spawns the Aletheides exchanges queue as story actions; the chaos
   Alfie needs the Astral Dominae (0x244) on the party. */
void SpawnAlfieChaos(const Inventory& inventory); /* 0x004DFAE0 */
void SpawnAlfieLife(void);                  /* 0x004DFB40 */
void SpawnAlfieKnow(void);                  /* 0x004DFB80 */
// Remove the monster bound to whichever Aletheides NPC is present.
void RemoveAletheides(void); /* 0x004E0430 */
