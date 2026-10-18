#pragma once
struct W8Chunk;
#include <string_view>

#include "surrender/srMath.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/npc_state.h"

enum W8NpcPickpocketResult {
    W8_PICKPOCKET_ITEM_TAKEN = 0,
    W8_PICKPOCKET_GOLD_TAKEN = 1,
    W8_PICKPOCKET_FAILED = 2,
    W8_PICKPOCKET_CAUGHT = 3,
    W8_PICKPOCKET_EMPTY = 4
};

enum W8NpcItemTheftResult {
    W8_ITEM_THEFT_SUCCEEDED = 0,
    W8_ITEM_THEFT_FAILED = 1,
    W8_ITEM_THEFT_CAUGHT = 2
};

enum W8NpcDispositionBand {
    W8_NPC_BAND_FRIENDLY = 0,
    W8_NPC_BAND_NEUTRAL = 1,
    W8_NPC_BAND_HOSTILE = 2
};

void QueueNpcHeldItemNotice(int npc_kind);

class W8Monster;
class Trigger;
struct W8GameplayModifierBlock;
struct W8MonsterInfo;
struct W8MonsterManagerEntry;

/* NPC database record indices used as name-style ids at runtime. */
enum W8NpcId {
    W8_NPC_MYLES = 7,
    W8_NPC_ZANT = 0x0f,
    W8_NPC_DRAZIC = 0x10,
    W8_NPC_RODAN = 0x11,
    W8_NPC_VI_DOMINA = 0x18,
    W8_NPC_RFS81_A = 0x20,
    W8_NPC_GLUMPH = 0x2b,
    W8_NPC_SPARKLE = 0x38,
    W8_NPC_AL_ADRYIAN = 0x3b,
    W8_NPC_MADRAS = 0x4a,
    W8_NPC_SEXUS = 0x4f,
    W8_NPC_PHOONZANG = 0x87,
    W8_NPC_ENDGAME2 = 0x8e,
};

enum W8NpcServiceFlag {
    W8_NPC_SERVICE_ARNIKA = 0x1,
    W8_NPC_SERVICE_TRYNTON = 0x2,
    W8_NPC_SERVICE_MARTEN_BLUFF = 0x4,
    W8_NPC_SERVICE_SEA_CAVES = 0x8,
    W8_NPC_SERVICE_MT_GIGAS = 0x10,
    W8_NPC_SERVICE_RIFT = 0x20,
    W8_NPC_SERVICE_RAPAX = 0x40,
    W8_NPC_SERVICE_BAYJIN = 0x80,
    W8_NPC_SERVICE_ASCENSION = 0x100,
    W8_NPC_SERVICE_CIRCLE = 0x200,
    W8_NPC_SERVICE_SWAMP = 0x400,
    W8_NPC_SERVICE_RAPAX_CAMP = 0x800,
    W8_NPC_SERVICE_GIGAS_CAVES = 0x1000,
    W8_NPC_SERVICE_MTN_PASS = 0x2000,
};

W8Monster* GetNpcMonster(W8NpcState* npc);

void ChooseNewGameStartLocation(int* level, int* entrance);
int SelectNewGameStartLevel(void);
void BindNpcToMonster(unsigned char value, bool enabled, int location_id);
bool RecruitNpcIntoParty(W8NpcState* npc);
int DismissNpcFromParty(int party_slot, int, bool skip_spawn, bool neutral);
/* How many leading party slots are occupied. */
unsigned char CountLeadingPartySlots(void);
bool NpcKnowsFact(W8NpcState* npc, W8FactId fact);
unsigned char FindNpcOfKind(int kind);
bool CanNpcJoinParty(W8NpcState* npc);
bool CanPlaceNpcNearParty(int party_slot);
W8MonsterInfo* GetNpcMonsterInfo(W8NpcState* npc);
W8NpcState* GetNpcStateForMonsterInfo(W8MonsterInfo* monster_info, bool allow_unavailable);
void ResumeNpc(W8NpcState* npc, int enabled);
void QueueNpcTravelRefusals(int destination_level);
void MarkNpcOfKind(int kind);
W8Character* GetNpcGroupCharacter(W8NpcState* npc);

/* The NPC-side global frame operation: timed world events and the per-frame
   NPC state passes. */
void UpdateNpcEvents(void);
void AdvanceNpcTimers(unsigned int elapsed);
void ProcessNpcPendingEvents(void);

/* Lazily create and then empty the shared NPC-state
   vector, recreating a runtime node for every database record still in use. */
void ResetNpcStates(void);
void ReleaseNpcStates(void);
/* Build one runtime state from its database record and return it,
   reusing the slot of a released node when one is free. */
W8NpcState* CreateNpcRuntimeNode(int npc_id);

/* The NPC-side consequence pass the sight code runs when a marked NPC's
   binding is released. */
void HandleMarkedNpcEvent(W8NpcState* npc, char mode);
/* The bound-NPC penalty the condition/enchantment rebuild folds
   into the character's modifier block while the slot's flag_fe is set. */
void ApplyBoundNpcPenalty(W8Character* character, W8GameplayModifierBlock* target);
/* The activation callback the rebinding installs on the level's
   NPC triggers. */
void ResetNpcBindingsForParty(void);
void ClearPendingNpcLevelFlags(void);
void ReleaseNpcMonsterBindings(void);
void ReleaseMarkedNpcBindings(void);
void RebindNpcLevelTriggers(void);
W8NpcState* GetNpcState(int index);
W8NpcState* GetNpcStateByKind(int kind);
/* Whether the NPC wants the offered item - it matches one of the
   record's wanted entries by id or by the shared 0x83 name kind, and a grouped
   NPC whose member already carries more than one declines. */
bool NpcWantsItem(W8NpcState* npc, W8ItemInstance* item);
bool NpcHasTopic(W8NpcState* npc, int topic);
bool NpcLeadHasNameStyle(W8NpcId kind);
/* Clear one NPC binding's monster link and hand the handle to the
   owned item-list teardown. */
void ReleaseNpcBinding(int value);
/* The NPC binding selected by a monster-list index, or null. */
W8NpcState* FindNpcBindingForMonster(W8MonsterInfo* monster_info);
unsigned char GetNpcDispositionBand(W8NpcState* npc);
void SetNpcDispositionBand(W8NpcState* npc, char band);
bool WillNpcTradeForItem(W8NpcState* npc, W8ItemInstance* item);
void ApplyNpcInteraction(W8NpcState* npc, int kind, int value, W8ItemInstance* item,
                         unsigned int gold);
/* Whether the NPC's database entry carries the value at 0x002. */
bool NpcRecordHasTradePool(W8NpcState* npc);
/* Push a topic onto the NPC's five-slot topic list. */
void AddNpcTopic(W8NpcState* npc, int topic);
/* Resolve one pickpocket attempt; the taken item goes to
   item_out and the taken gold to gold_out. */
W8NpcPickpocketResult AttemptNpcPickpocket(W8Character* character, W8NpcState* npc,
                                           W8ItemInstance* item_out, unsigned int* gold_out);
/* Clear the npc's item_ids slots matching the item the quote
   entry just handed out. */
void ClearNpcItemId(W8NpcState* npc, int item_id);
/* The live NPC state whose display (or fact-substituted) name
   matches case-insensitively; 0 when none does. */
W8NpcState* FindNpcStateByName(std::string_view name);
W8MonsterManagerEntry* GetNpcGroupEntry(W8NpcState* npc);
std::string GetNpcDisplayName(W8NpcState* npc);
void ReleaseNpcMonsterBinding(W8NpcState* npc, char level);
void RestoreNamedNpcAtLevel(int kind, char level, std::string_view entity_name);
bool ClearNpcScheduledItem(W8NpcState* npc, int item_id, W8ItemInstance* out);
void ReleaseNpcMonsterByKind(int kind);
/* Record that the NPC has told the party the given fact. */
void TellNpcFact(W8NpcState* npc, short fact);
bool SaveNpcStates(wiz8::File& file);
void LoadNpcStates(W8Chunk& file);
char AttemptNpcItemTheft(W8Character* character, W8NpcState* npc, int item_id, int count);
void UpdateNpcPartyMember(int party_slot);
bool QueueNpcDepartureEvents(int destination_level);
