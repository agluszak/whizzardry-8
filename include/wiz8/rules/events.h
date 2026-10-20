#pragma once

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "wiz8/data/skills.h"

/* What happened in a game, in the order it happened. Rules append events to their
   game's log and know nothing about screens; the UI observes the log and presents
   each event. */

// A notice's defaults: the active mode's text box and its available line width.
constexpr short W8_NOTICE_TEXT_BOX_AUTOMATIC = -1;
constexpr unsigned int W8_NOTICE_WRAP_AUTOMATIC = ~0U;

enum W8TextBoxMode : unsigned char {
    W8_TEXT_BOX_MODE_NOTICE = 0,
    // The next notice continues the current line.
    W8_TEXT_BOX_MODE_APPEND = 1,
    W8_TEXT_BOX_MODE_SPECIAL_PANEL = 3
};

// A line for the text box in a font palette, or a message dialog when forced.
struct TextNotice {
    unsigned int palette = 0;
    std::string text;
    short text_box = W8_NOTICE_TEXT_BOX_AUTOMATIC;
    unsigned int wrap_width = W8_NOTICE_WRAP_AUTOMATIC;
    bool force_dialog = false;
};

/* A party member's line, "Name: text", the name in the member's marching-order
   colour. The context selects the text box as a notice's does. */
struct CharacterNotice {
    unsigned int party_slot = 0;
    std::string text;
    short context = W8_NOTICE_TEXT_BOX_AUTOMATIC;
};

/* A message dialog the player closes; `on_closed`, when set, receives whether it was
   accepted. A confirmation offers OK, and `cancel` adds a cancel button. */
struct NoticeDialog {
    std::string text;
    std::function<void(bool)> on_closed;
    bool confirmation = true;
    bool cancel = false;
};

// How the text box takes the next notice; a value of -1 keeps the mode's current one.
struct TextBoxModeChange {
    W8TextBoxMode mode = W8_TEXT_BOX_MODE_NOTICE;
    int value = -1;
};

// A member's portrait, status or action display changed.
struct PartySlotChanged {
    unsigned int party_slot = 0;
};
// The shared load was redistributed across the party.
struct PartyLoadChanged {};
// The selected member's combat panel changed.
struct CombatPanelChanged {};
// A member's chosen action changed.
struct CharacterActionChanged {};
// The party's equipment settled after an inventory edit.
struct EquipmentSettled {};
// The hand holds something else.
struct HeldItemChanged {};
/* A skill became available or unavailable to a character; a character screen editing
   that character adjusts its draft. */
struct SkillAvailabilityChanged {
    W8Skill skill{};
    bool available = false;
};

/* A member's skill rose a point through practice. A quiet increase is announced
   later rather than at once. */
struct SkillImproved {
    unsigned int party_slot = 0;
    W8Skill skill{};
    bool quiet = false;
};

/* The world was held or released, or its time stopped or resumed. `held` is the new
   state; `hold_changed` tells a hold or a release apart from a change of time alone. */
struct WorldAdvancementChanged {
    bool held = false;
    bool hold_changed = false;
};
// The party or a monster moved during a fight, so the monster target panel recounts.
struct CombatantsMoved {};

using Event =
    std::variant<TextNotice, CharacterNotice, NoticeDialog, TextBoxModeChange, PartySlotChanged,
                 PartyLoadChanged, CombatPanelChanged, CharacterActionChanged, EquipmentSettled,
                 HeldItemChanged, SkillAvailabilityChanged, SkillImproved, WorldAdvancementChanged,
                 CombatantsMoved>;

/* A game's ordered event log. An observer sees each event as it is pushed, so its
   presentation interleaves with everything else the game does; without one, events
   wait in order until an observer is installed. */
class Events {
public:
    using Observer = std::function<void(const Event&)>;

    void push(Event event);
    // Install or clear the observer; an installed one first receives what is pending.
    void observe(Observer observer);
    const std::vector<Event>& pending() const { return pending_; }

private:
    Observer observer_;
    std::vector<Event> pending_;
};
