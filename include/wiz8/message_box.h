#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "wiz8/data/skills.h"

struct W8NpcState;
struct W8NpcScriptFile;

struct W8NpcScriptBinding {
    std::shared_ptr<const W8NpcScriptFile> script;
    std::string voice_stem;
    bool npc_voice = false;
};

/* Deferred notices carry their complete values until the quote is dismissed. */
struct W8ExperienceNoticePayload {
    unsigned int amount;
    bool alternate_message;
};

struct W8SkillNoticePayload {
    struct Entry {
        unsigned int party_slot;
        W8Skill skill;
    };
    std::vector<Entry> entries;
};

struct W8LevelUpNotice {
    int party_slot;
};

/* Work a portrait bubble owes on dismissal; empty for plain text bubbles. */
using W8QuoteNotice =
    std::variant<std::monostate, W8ExperienceNoticePayload, W8SkillNoticePayload, W8LevelUpNotice>;

/* Quest-specific world changes a script fact or level script queues behind
   the conversation. RunNpcStoryAction (npc_story_events.cpp) performs them. */
enum class W8NpcStoryAction : unsigned char {
    spawn_alfie_chaos,
    spawn_alfie_life,
    spawn_alfie_know,
    path2_trigger,      /* run trigger "Path2Trigger" */
    move_savant,        /* MoveSavant.msf on monster group 0xc2 */
    move_bela,          /* MoveBela.msf + NP_DSExit teleport */
    move_golem,         /* MoveGolem.msf on monster group 0xb8 */
    aletheides_leaves,  /* RemoveAletheides() */
    call_henchman,      /* monster group 0x112 cycle 0x12 + callback */
    henchman_leaves,    /* monster group 0xdc cycle 0x12 + callback */
    pillargate_lure,    /* pillargate05 + fade Al-Lure (npc 0x3f) */
    pillargate_madeus,  /* pillargate04 + fade Al-Madeus (npc 0x3e) */
    pillargate_asaiz,   /* pillargate01 + fade Al-Asaiz (npc 0x3d) */
    prince_disappears,  /* fade group 0x1ab; hostile group 0x15d */
    remove_janette,     /* npc kind 0x62 */
    remove_marten,      /* npc kind 0x64 */
    move_gari,          /* MoveGari.msf on monster group 0x162 */
    milano_rat_door,    /* RatDoor02 + Milano.msf on group 0xcf */
    remove_shaman,      /* npc kind 0x4d */
    move_rubble,        /* MoveRubble.msf on monster group 0x83 */
    sedexus_leaves,     /* LezboDemonAppeared + fade npc 0x40 */
    trigger_fix,        /* run trigger "triggerFix" */
    remove_sedexus_rift, /* npc kind 0x42 */
    balbrak_home,       /* teleport npc 0x0c to NP_Balbrakhome */
    mook_comment,       /* race-10 party member event */
    savant_hack,        /* monster group 0x1b6 cycle 0x19 + callback */
    move_to_book,       /* belapath1.msf on group 0x1b4 + triggerplanes */
    move_to_book2,      /* CC_TRIGGERPLANE3 + notice npc 0x8d */
    savant_appears,     /* monster 0x234 at NP_DS1 + notice npc 0x84 */
    remove_rpc_vi,      /* spawn 0x1b9 at NP_VI1 when Vi (0x18) leads */
    phoonzang_split,    /* cycle npc 0x84 + NP_PHOONZANGLEE + npc 0x8d */
    begin_endgame,
    prince_not_home,
    phoonzang_notice,   /* notice npc kind 0x87 */
    turn_to_book,       /* BeginScreenFade + NpcScriptTurnToBook */
    remove_aletheides_ad, /* npc kind 0x2d */
    remove_aletheides_cm, /* npc kind 0x2e */
    remove_aletheides_dd, /* npc kind 0x2f */
    sedexus_passout,    /* ResolveSedexusCapture() */
    endgame_screen,     /* BeginScreenFade + NpcScriptEndgameScreen */
};

/* Queued actions that carry no data. */
enum class W8NpcCommand : unsigned char {
    close_dialogue,            /* close dialogue; tell the level script */
    close_and_resume_npc,      /* close dialogue; ResumeNpc on the dialogue NPC */
    reopen_transcript,         /* CloseNpcDialogueLayout + transcript layout */
    journal_quote,             /* quote bubble + journal-entry.wav */
    dispatch_pending_notice,
    remove_speaking_npc,       /* end the session and fade the speaker's monster */
    clear_target_state,
    begin_scripted_world_action,
    /* World messages bracket a portrait message with these two outside combat. */
    reset_level_data_vectors,
    clear_level_movement_stopped,
    spacer,                    /* holds its queue position for one frame; no effect */
};

/* Run `quote_index` of the queuing NPC's script. */
struct W8NpcQueuedQuote {
    W8NpcScriptBinding binding;
    unsigned char subquote_index;
    int quote_index;
    bool mark_pending;     /* record quote_index as pending */
    bool suppress_entries; /* skip the price-check and pending-quote entry scans */
};

/* Deferred entries retain the immutable script that supplied their indices. */
struct W8NpcQueuedEntry {
    W8NpcScriptBinding binding;
    int quote_index;
    int entry_index;
    int continuation_quote;
};

/* Portrait message text and the notice its bubble posts on dismissal. The
   notice also selects the dispatch: skill and level-up notices honour the
   skill_increase_messages setting. */
struct W8NpcQueuedNotice {
    std::string text;
    W8QuoteNotice notice;
};

/* Recruit the NPC of `npc_kind` and restart the conversation with it. */
struct W8NpcRecruit {
    int npc_kind;
};

/* Dismiss the NPC in `party_slot` from the party. */
struct W8NpcDismiss {
    int party_slot;
};

/* Queue character event `event_type` on a random party speaker. */
struct W8NpcPartySpeakerEvent {
    unsigned int event_type;
};

/* Queue `party_slot`'s level-up reaction (character event 1). */
struct W8NpcLevelUpReaction {
    int party_slot;
};

/* Queue `party_slot`'s scripted NPC line (character event 0x18). */
struct W8NpcPartyScriptLine {
    int party_slot;
};

/* Show Data().strings[`string_index`] as a portrait message. */
struct W8NpcPortraitString {
    int string_index;
};

/* Mark `party_slot` missing indefinitely (the condition-13 scene). */
struct W8NpcMarkMissing {
    int party_slot;
};

/* Take the NPC in `party_slot` out of combat and the party. */
struct W8NpcLeaveCombat {
    int party_slot;
};

/* Ask the player to confirm travel to `level`. */
struct W8NpcTravelConfirm {
    int level;
};

using W8NpcAction =
    std::variant<W8NpcQueuedQuote, W8NpcQueuedEntry, W8NpcQueuedNotice, W8NpcCommand,
                 W8NpcStoryAction, W8NpcRecruit, W8NpcDismiss, W8NpcPartySpeakerEvent,
                 W8NpcLevelUpReaction, W8NpcPartyScriptLine, W8NpcPortraitString, W8NpcMarkMissing,
                 W8NpcLeaveCombat, W8NpcTravelConfirm>;

/* One queued action and the speaker it was queued for. */
struct W8MessageBoxLine {
    int npc_kind; /* record kind of the speaking NPC, or -1; resolved when the line runs */
    W8NpcAction action;
};
