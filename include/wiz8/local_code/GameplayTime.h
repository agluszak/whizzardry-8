#pragma once

class Game;
struct W8Character;
struct W8MonsterInfo;
struct World;

/* What the interface holds still. The main game screen reports it to every step and covers
   the world when another screen replaces it; the camp steps the game with the world
   covered. */
struct WorldHold {
    /* The world is not shown: another screen covers it, from the camp or a menu to a
       level change's please-wait screen. Monsters do not look about. A game's world is
       covered until the main game first steps it. */
    bool covered = false;
    /* The world is shown but held: a dialog, a picker, dialogue, the formation panel, the
       targeting cone, the review transition or a minimised window. */
    bool world = false;
    // The party's movement: it is working a lock or a trap.
    bool party = false;
};

/* Whether the world moves on. The player's pause and the interface's hold stop it; the
   environment clock also stops for a fight, whose rounds advance time themselves. */
struct WorldAdvancement {
    // The player's pause. Closing an interaction does not lift it.
    bool paused = false;
    // The interface's hold, as last reported.
    WorldHold hold{.covered = true};
    // As last applied: paused or held by the interface, so the world's simulation waits.
    bool held = true;
    // As last applied: the environment clock runs, as nothing holds the world or fights.
    bool time_passes = false;
    // The world the environment state was applied to; a new world takes it up again.
    const World* world = nullptr;
    // The pause the move timer was last suspended or resumed for.
    bool timer_paused = false;
};

/* Apply the player's pause and the interface's hold to the world. Only edges act: the
   move timer follows the pause; the environment clock, the navigators and the party's
   motion follow whether the world is held or a fight is on, and are applied again to a
   new world. A change is reported as a `WorldAdvancementChanged` event. The step applies
   the hold it is given; rules that start or end a fight or lift the pause apply the last
   hold again. */
void ApplyWorldAdvancement(Game& game, WorldHold hold);

// The camping surprise sequence while `Game::surprise_possible` is set.
struct SurpriseSequence {
    enum class Phase : unsigned char { fade_in, hold, fade_out_and_resolve };
    // Surprise started with no character engaged; the hold then waits for one.
    bool unengaged = false;
    // World-clock deadline the hold compares against; zero before it starts.
    unsigned int deadline_turns = 0;
    Phase phase = Phase::fade_in;
};

void UpdateGameClock(int elapsed);
/* The 120-second aging tick - wait-state machine, per-character
   aging, combat/monster effect expiry and NPC-side aging. */
void AdvanceTimedEffects(unsigned int minutes);
/* Tear down the surprise sequence's world-state overrides. */
void RestoreSurpriseView(void);
void ResolveSurpriseWake(void);
void EndSurprise(void); /* End surprise and post the notice */
/* End a holding surprise sequence when combat starts. */
void ResolveSurpriseHold(void);
/* Per-character share of the aging tick - damage/heal/
   stamina/spell-point modifiers, disease progression, regen accumulators and
   the condition/enchantment countdowns. */
void GameTurnsPassedChar(int party_slot, unsigned int minutes);
/* Camping fatigue tick - rolls fatigue dice per character. */
void UpdateCampFatigue(int ticks);
/* Stamina driver - refreshes the wait state then ticks each
   eligible character. */
void UpdatePartyStamina(int ticks);
/* Per-character stamina regen/fatigue tick. */
void RegenCharacterStamina(int party_slot, unsigned int elapsed);
void AgeMonsterSight(W8MonsterInfo* monster_info, unsigned int minutes,
                     unsigned char full_health_regeneration);
/* The monster counterpart - hit points and stamina only, plus the
   modifier block's regen channels. */
void RebuildMonsterRegenRates(W8MonsterInfo* monster_info);
/* Advance the surprise fade/hold/resolve sequence while
   fSurprisePossible is set. */
void UpdateSurpriseMode(void);
/* The surprise sequence is still fading the view in. */
bool SurpriseFadingIn(void);
/* The cancel command while surprise runs - reposts the surprise
   notice while the ambush is unengaged, otherwise performs the phase-1
   transition that restores the view, time scale and music. */
void AcknowledgeSurprise(void);
/* Scripted-surprise entry - completes the pending character
   events, resets the occupied slots' portrait poses, takes the menu button
   banks down and redraws. */
void BeginSurprise(void);
/* The Camp command's request to enter the surprise-possible camp state. */
void RequestCamp(void);
