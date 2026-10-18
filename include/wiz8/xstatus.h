#ifndef WIZ8_XSTATUS_H
#define WIZ8_XSTATUS_H

#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "glm/ext/vector_float3.hpp"
struct W8MonsterGroup;
struct W8MonsterInfo;
struct W8WorldItem;

#include <algorithm>

#include <stddef.h>

#include "wiz8/character_event_queue.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/party_formation.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/platform/game_timer.h"

struct W8MonsterGroup;
struct W8WorldItem;

// Live gameplay state, not a disk record.
struct W8XStatus {
    W8MonsterManagerEntry monster_manager_entries[8];
    unsigned int uiItemTablesInDatabase;
    unsigned int uiItemTableCategories;
    unsigned int uiMonstersInDatabase;
    bool fCombatMode;
    bool fNpcDialogueMode;
    bool fLockInteractMode;
    bool fTrapInteractMode;
    bool fReviewCharacterMode;
    bool fCampMode;
    /* Dialogue NPC state needs re-syncing once the frame settles. */
    bool dialogue_sync_pending;
    /* Re-check level-up notices when the UI is idle again. */
    bool level_up_notice;
    bool fEncumbranceDirty; /* Pending party-weight recalc */
    int item_manager_pending;
    bool fSurprisePossible;
    /* Set when surprise starts with no character engaged; phase 1 waits
       only while this is clear. */
    bool surprise_unengaged;
    /* uiTurnsElapsed deadline the phase-1 hold compares against. */
    unsigned int surprise_deadline_turns;
    /* 0 = fade in, 1 = hold, 2 = fade out / resolve. */
    unsigned short surprise_phase;
    bool fPartyMovementUi;
    bool fPartyMovementMode;
    float flPartyMoveDistLimit;
    /* Accumulated party movement distance; the combat movement update
       converts it into the remaining percentage displayed by the panel. */
    float party_move_distance;
    std::unique_ptr<W8CharacterEventQueue> character_event_queue;
    int iCurrentCursor;
    int current_cursor_frame;
    uint64_t current_cursor_time;
    W8TargetNeed iTargetingMode;
    /* The formation screen's edit buffer - MGSFormation snapshots the
       live formation here on open, edits the copy, and either reconciles it
       back or diffs it against live on accept. */
    W8PartyFormationState edited_formation;
    int picked_monster;
    int picked_group;
    /* One-shot latch set when the combat/party panels refresh; the next
       UpdateMonsterSight pass consumes and clears it. */
    bool sight_refresh_pending;
    bool world_update_blocked;
    /* Explicit user-pause choice; closing an interaction does not clear it. */
    bool world_paused;
    unsigned short review_character_slot;
    int held_item_source;
    unsigned char held_item_origin;
    unsigned short held_item_slot;
    std::array<std::unique_ptr<W8MonsterRecord>, 1000> monster_record_cache;
    std::unique_ptr<W8GameTimer> gameplay_timer;
    bool save_notice_shown;
    bool npc_combat_notice_pending;
    bool deferred_skill_notices;
    /* A container offered more than one item, so the item-choice
       dialog is pending; blocks the magic-effects tick until it opens. */
    bool item_pick_pending;
    /* Raised by BeginNpcScriptedScene until EndScriptedPortraitPick;
       gates the dialogue-cursor and NPC-script input paths. */
    bool scripted_scene;
    /* Which Assay dialog tab is shown - set selects the profession
       icons/button, clear selects the race side. */
    bool assay_professions_tab;
    bool item_drag_active;
    W8ItemInstance* dragged_item;
    unsigned char dragged_item_origin;
    signed char dragged_character_slot;
    uint64_t spell_cooldown_clocks[14];
    uint64_t combat_countdown;
    unsigned char combat_difficulty;
    bool party_moving;
};

extern W8XStatus gXStatus;

/* Drop any item drag in progress; the camp, use-item and main-game paths
   all reset the four drag fields together. */
inline void ClearItemDrag(void)
{
    gXStatus.item_drag_active = false;
    gXStatus.dragged_item = 0;
    gXStatus.dragged_item_origin = W8_ITEM_ORIGIN_NONE;
    gXStatus.dragged_character_slot = -1;
}

/* Bounded queries into the current level; absent levels resolve to null. */
W8MonsterInfo* GetMonsterListEntry(int index);

W8MonsterInfo* GetUnbornMonsterEntry(int index);

W8WorldItem* GetWorldItemListEntry(int index);

#endif
