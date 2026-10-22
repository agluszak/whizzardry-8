#pragma once

#include <chrono>
#include <optional>

class Events;
struct Combat;
struct Level;

/* The party's search mode: the player's saved choice and the sweep it runs. A fight
   suspends searching without changing the choice. */
class Search {
public:
    Search(const std::optional<Combat>& fight, Events& events, Level& level)
        : fight_(fight), events_(events), level_(level)
    {
    }

    // The player's choice, as a save records it.
    bool requested() const { return requested_; }
    // Whether the party searches now: the choice is on and no fight is in progress.
    bool active() const;
    // A load installs the saved choice.
    void restore(bool requested) { requested_ = requested; }

    /* The player toggles the choice: off posts the off notice, on starts searching; a
       fight blocks the toggle outright. */
    void toggle();
    // A fight began: searching stops, and says so when the choice is on.
    void fightBegan();
    // The fight ended: searching resumes when the choice is on.
    void fightEnded();

    // Run the sweep timer down by the time the step passed.
    void passTime(std::chrono::milliseconds elapsed) { sweep_due_ -= elapsed; }
    /* Outside a fight, sweep every 500ms: walk the in-view searchables, pick the best
       searcher for each, queue the found event and turn the camera toward the find. */
    void sweepIfDue();

private:
    // Post the on notice, arm the sweep and drop the active cursor node.
    void resume();

    const std::optional<Combat>& fight_;
    Events& events_;
    Level& level_;
    bool requested_ = false;
    std::chrono::milliseconds sweep_due_{};
};
