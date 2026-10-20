#ifndef WIZ8_XSTATUS_H
#define WIZ8_XSTATUS_H

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "wiz8/data/records.h"
#include "wiz8/item_spawning.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/targeting.h"

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
    bool fReviewCharacterMode;
    /* Re-check level-up notices when the UI is idle again. */
    bool level_up_notice;
    bool fPartyMovementUi;
    int iCurrentCursor;
    int current_cursor_frame;
    uint64_t current_cursor_time;
    W8TargetNeed iTargetingMode;
    /* The formation screen's edit buffer - MGSFormation snapshots the
       live formation here on open, edits the copy, and either reconciles it
       back or diffs it against live on accept. */
    W8PartyFormationState edited_formation;
    bool world_update_blocked;
    /* Explicit user-pause choice; closing an interaction does not clear it. */
    bool world_paused;
    /* An item the camp's Use action hands to the main game's use-item picker. */
    std::optional<W8ItemDrag> item_drag;
};

extern W8XStatus gXStatus;

/* Bounded queries into the current level; absent levels resolve to null. */
W8MonsterInfo* GetMonsterListEntry(std::ptrdiff_t index);

W8MonsterInfo* GetUnbornMonsterEntry(int index);

W8WorldItem* GetWorldItemListEntry(int index);

#endif
