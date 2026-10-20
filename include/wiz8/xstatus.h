#ifndef WIZ8_XSTATUS_H
#define WIZ8_XSTATUS_H

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "wiz8/character_event_queue.h"
#include "wiz8/item_location.h"
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
struct W8MonsterInfo;
struct W8WorldItem;

// Live gameplay state, not a disk record.
struct W8ItemDrag {
    W8ItemLocation item;
    int actor = -1;
    // Until the use-item picker opens on it; the drag is remembered afterwards.
    bool pending = true;
};

struct W8XStatus {
    W8MonsterManagerEntry monster_manager_entries[8];
    bool fNpcDialogueMode;
    bool fReviewCharacterMode;
    bool fCampMode;
    /* Re-check level-up notices when the UI is idle again. */
    bool level_up_notice;
    int item_manager_pending;
    bool fSurprisePossible;
    bool fPartyMovementUi;
    bool fPartyMovementMode;
    std::unique_ptr<W8CharacterEventQueue> character_event_queue;
    int iCurrentCursor;
    int current_cursor_frame;
    uint64_t current_cursor_time;
    W8TargetNeed iTargetingMode;
    /* The formation screen's edit buffer - MGSFormation snapshots the
       live formation here on open, edits the copy, and either reconciles it
       back or diffs it against live on accept. */
    W8PartyFormationState edited_formation;
    /* One-shot latch set when the combat/party panels refresh; the next
       UpdateMonsterSight pass consumes and clears it. */
    bool sight_refresh_pending;
    bool world_update_blocked;
    /* Explicit user-pause choice; closing an interaction does not clear it. */
    bool world_paused;
    std::unique_ptr<W8GameTimer> gameplay_timer;
    /* A container offered more than one item, so the item-choice
       dialog is pending; blocks the magic-effects tick until it opens. */
    bool item_pick_pending;
    /* Raised by BeginNpcScriptedScene until EndScriptedPortraitPick;
       gates the dialogue-cursor and NPC-script input paths. */
    bool scripted_scene;
    /* An item the camp's Use action hands to the main game's use-item picker. */
    std::optional<W8ItemDrag> item_drag;
    bool party_moving;
};

extern W8XStatus gXStatus;

/* Bounded queries into the current level; absent levels resolve to null. */
W8MonsterInfo* GetMonsterListEntry(std::ptrdiff_t index);

W8MonsterInfo* GetUnbornMonsterEntry(int index);

W8WorldItem* GetWorldItemListEntry(int index);

#endif
