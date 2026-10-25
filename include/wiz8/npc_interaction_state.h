#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/rules/item_instance.h"

struct W8NpcState;

struct W8PendingNoticeLine {
    std::string text;
    int npc_kind;
};

/* The NPC conversation's state the game keeps for one play-through: who is spoken to and
   by whom, the offered item, whether the conversation runs as a scripted scene, and the
   transcript's category filter. The game owns it (`Game::npc_interaction`); the dialogue's interface is the main game's
   (`W8NpcDialoguePanel`). */
struct W8NpcInteractionState {
    /* The dialogue's leading speaker: the occupied row whose character leads
       skill 0x16. */
    int dialogue_speaker{};
    int target_location_id{};
    std::shared_ptr<W8NpcState> dialogue_npc{};
    W8DialogueCategory dialogue_category_filter = W8_DIALOGUE_CATEGORY_ALL;
    /* The item the party is offering, taken out of the hand when the dialogue
       opens with it. Consuming it drains its stack; whatever remains goes back
       when the conversation ends. */
    std::optional<W8ItemInstance> offered_item{};
    std::vector<W8PendingNoticeLine> pending_notice_lines{};
    /* Cleared when the dialogue ends on a refusal/abrupt dismissal, in which
       case the close path queues a delayed party reaction event. */
    bool suppress_parting_reaction{};
    /* The dialogue session runs as queued script lines without the
       interactive panel. */
    bool scripted_dialogue{};
    int last_notice_npc_kind{};
    /* What the dialogue's interface shows that the conversation's progress waits on: the
       open layout, whether the panel or the whole dialogue is hidden, and a modal dialog
       over it. */
    W8NpcDialogueLayout dialogue_layout{};
    /* The whole dialogue UI is parked (right-click hide) and the world input
       path is live until it is shown again. */
    unsigned char dialogue_hidden{};
    /* A modal W8NpcDialog is up over the dialogue; transcript word clicks,
       layout keys other than Escape and layout leave paths bail. */
    bool modal_dialog_open{};
    bool dialogue_panel_hidden{};
};
