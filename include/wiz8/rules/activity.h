#pragma once

#include <cstdint>

/* What the party is doing. Gameplay rules branch on this, not on the current screen or on
   interface flags; the game holds the current value (`Game::activity`).

   Its parts are independent because the game lets them overlap: a scripted scene holds a
   conversation, and a scripted exchange can begin while a conversation is set aside.

   A screen covering the world is not part of it either: a covered world is not stepped,
   and the interface reports it as covered (`WorldHold::covered`).

   Combat is not part of it. `Game::combat` says whether a fight is underway, and a fight goes
   on in the world, in camp (where the party changes equipment) and while an NPC talks, so
   it is orthogonal to everything here.

   Party creation is not part of it either: it lasts until the opening sequence, which the
   saved status records (`GameStarted()`). */
struct Activity {
    /* Where the party is. The camp commands set it: opening the camp, from the world or
       from party creation's review, and leaving it. */
    enum class Place : std::uint8_t {
        // In the world: game time passes there.
        World,
        /* In camp, where only speech goes on. The camp screen also reviews characters
           during party creation, and counts as camp there. */
        Camp,
    };

    Place place = Place::World;
    // Talking with an NPC, through the dialogue panel or a scripted exchange.
    bool conversation = false;
    /* The party stepped out of a conversation to camp, read the journal or use one of the
       NPC's spell or item services, and returns to it. Opening a dialogue panel clears it. */
    bool conversation_aside = false;
    // A story scene holds the conversation while the player picks a party member.
    bool scripted_scene = false;

    // In the world: game time passes and, while it is shown, monsters look about.
    [[nodiscard]] constexpr bool in_world() const { return place == Place::World; }
    [[nodiscard]] constexpr bool in_camp() const { return place == Place::Camp; }
};
