#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "wiz8/data/skills.h"
#include "wiz8/rules/targeting.h"

/* What happened in a game, in the order it happened. Rules append events to their
   game's log and know nothing about screens; the UI observes the log and presents
   each event. */

// A notice's defaults: the active mode's text box and its available line width.
constexpr short W8_NOTICE_TEXT_BOX_AUTOMATIC = -1;
constexpr unsigned int W8_NOTICE_WRAP_AUTOMATIC = ~0U;
// Wrap at the text box's full width.
constexpr unsigned int W8_NOTICE_WRAP_TEXT_BOX = ~0U - 1;

enum W8TextBoxMode : unsigned char {
    W8_TEXT_BOX_MODE_NOTICE = 0,
    // The next notice continues the current line.
    W8_TEXT_BOX_MODE_APPEND = 1,
    W8_TEXT_BOX_MODE_SPECIAL_PANEL = 3
};

/* Frames of the notice palette catalog (Data\Fonts\Palette*.sti). */
enum W8FontPaletteIndex {
    W8_FONT_PALETTE_RED = 0,
    W8_FONT_PALETTE_GREEN = 1,
    W8_FONT_PALETTE_PURPLE = 2,
    W8_FONT_PALETTE_BLUE = 3,
    W8_FONT_PALETTE_ORANGE = 4,
    W8_FONT_PALETTE_YELLOW = 5,
    W8_FONT_PALETTE_PINK = 6,
    W8_FONT_PALETTE_BROWN = 7,
    W8_FONT_PALETTE_WHITE = 8,
    W8_FONT_PALETTE_RUST = 9,
    W8_FONT_PALETTE_BRONZE = 10,
    W8_FONT_PALETTE_GRAY = 11,
    W8_FONT_PALETTE_BEIGE = 12,
    W8_FONT_PALETTE_OPTIONS_GREEN = 13,
    W8_FONT_PALETTE_OPTIONS_WHITE = 14,
    W8_FONT_PALETTE_TEXT_BOX = 15,
    W8_FONT_PALETTE_COUNT = 16
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

/* A member says one of their lines (a character event type), as a voice and a portrait
   quote. */
struct CharacterSpeech {
    unsigned int party_slot = 0;
    int event_type = 0;
};

// A completed hit's presentation, including the variant selected at the hit milestone.
struct CharacterDamaged {
    unsigned int party_slot = 0;
    unsigned int amount = 0;
    bool fatal = false;
};

// A member died.
struct CharacterDied {
    unsigned int party_slot = 0;
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
// The party's actions for the round start executing.
struct CombatExecutionBegan {};
// The party ended a fight that nothing kept going.
struct CombatCalledOff {};
// A fight began.
struct CombatBegan {};
// A fight ended and the world is restored around it.
struct CombatEnded {};
/* Who has acted in the round changed; with `whole_party`, every member's combat state
   changed with it. */
struct CombatProgressChanged {
    bool whole_party = false;
};
// The party's combat stance, or whether it may change, changed.
struct CombatStanceChanged {};
// The monsters about the party, their groups or their dispositions changed.
struct MonstersChanged {};
// The party's spell effects changed, or a round passed them by.
struct PartyEffectsChanged {};
// The spell effects on the party in a fight changed.
struct CombatEffectsChanged {};
// What the world view's frame shows about the party changed: conditions, quotes.
struct PartyPanelChanged {};
// A member joined or left; everything the party is shown in changes.
struct PartyMembershipChanged {};
// An item left the world.
struct WorldItemRemoved {
    int runtime_id = -1;
};
// The player is asked to pick a target of `need`; W8_TARGET_NEED_NONE stops asking.
struct TargetingRequested {
    W8TargetNeed need = W8_TARGET_NEED_NONE;
};

using Event =
    std::variant<TextNotice, CharacterNotice, NoticeDialog, TextBoxModeChange, CharacterSpeech,
                 CharacterDamaged, CharacterDied, PartySlotChanged, PartyLoadChanged,
                 CombatPanelChanged, CharacterActionChanged, EquipmentSettled, SkillImproved,
                 WorldAdvancementChanged, CombatantsMoved, CombatExecutionBegan, CombatCalledOff,
                 CombatBegan, CombatEnded, CombatProgressChanged, CombatStanceChanged,
                 MonstersChanged, PartyEffectsChanged, CombatEffectsChanged, PartyPanelChanged,
                 PartyMembershipChanged, WorldItemRemoved, TargetingRequested>;

/* A game's ordered event log. An observer sees each event as it is pushed, so its
   presentation interleaves with everything else the game does; without one, events
   wait in order until an observer is installed. Migrated operations defer delivery
   until their mandatory consequences finish, preserving the order of their outcomes. */
class Events {
public:
    using Observer = std::function<void(const Event&)>;

    // Migrated commands hold their outcomes until the outermost operation completes.
    // Simulation callbacks remain immediate; only presentation delivery is held.
    class Operation {
    public:
        explicit Operation(Events& events);
        ~Operation() noexcept(false);
        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;

    private:
        Events& events_;
        std::size_t pending_start_;
        int exceptions_;
    };

    void push(Event event);
    // Install or clear the observer; an installed one first receives what is pending.
    void observe(Observer observer);
    const std::vector<Event>& pending() const { return pending_; }

private:
    void deliver();
    unsigned int operations_ = 0;
    bool delivering_ = false;
    Observer observer_;
    std::vector<Event> pending_;
};
