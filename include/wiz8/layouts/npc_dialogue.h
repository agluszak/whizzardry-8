#pragma once

#include <string>

/* W8NpcInteractionState::dialogue_layout - which NPC dialogue layout is up. The
   layout-1 caption is "MAGIC" (Charm/Mindread/Use Item services) and the
   layout-5 caption "TRADE". Layout 6 owns no panels and is never opened. */
enum W8NpcDialogueLayout {
    W8_DIALOGUE_LAYOUT_UNSET = -1, /* spell view has no dialogue to reopen */
    W8_DIALOGUE_LAYOUT_NONE = 0,
    W8_DIALOGUE_LAYOUT_SERVICES = 1,
    W8_DIALOGUE_LAYOUT_TOPIC_MENU = 2,
    W8_DIALOGUE_LAYOUT_TRANSCRIPT = 3,
    W8_DIALOGUE_LAYOUT_MAIN_TEXT_BOX = 4,
    W8_DIALOGUE_LAYOUT_TRADE = 5,
    W8_DIALOGUE_LAYOUT_BARE = 6
};

/* Transcript keyword categories and the active category filter. */
enum W8DialogueCategory : signed char {
    W8_DIALOGUE_CATEGORY_ALL = -1,
    W8_DIALOGUE_CATEGORY_ITEMS = 0,
    W8_DIALOGUE_CATEGORY_PEOPLE = 1,
    W8_DIALOGUE_CATEGORY_PLACES = 2,
    W8_DIALOGUE_CATEGORY_MISC = 3
};

/* One saved transcript keyword. */
struct W8DialogueTranscriptRecord {
    std::string text;
    W8DialogueCategory category = W8_DIALOGUE_CATEGORY_MISC;
};
