#pragma once

#include "wiz8/world/simulation_sink.h"

class Game;
struct World;

/* Combat's resolution of what one game's level reports: missile hits and landings, and
   the frames on which monsters launch missiles and release spells. The game owns it and
   its level installs it into the primary world, so every report acts on that game, and
   on the world of the entity that reported it. */
class CombatSignals final : public SimulationSink {
public:
    explicit CombatSignals(Game& game) : game_(game) {}
    CombatSignals(const CombatSignals&) = delete;
    CombatSignals& operator=(const CombatSignals&) = delete;

    bool missileReached(Missile& missile, Navigator& struck) override;
    void missileLanded(Missile& missile) override;
    void missileReleased(Monster& monster) override;
    void spellReleased(Monster& monster) override;

private:
    // Post "<source> hits <target>" when a fight is on.
    void announceCollisionTarget(const World& world, const Missile& missile);
    /* A physical missile reached its combat target: announce the hit, roll penetration
       against the target's armour and apply the damage (combat/attack.cpp). */
    void resolveMissileHit(const World& world, Missile& missile, bool deflected);
    /* A spell missile reached its combat target: play its impact sound and apply the
       carried spell effect to the target or the area (combat/attack.cpp). */
    void resolveSpellMissileHit(const World& world, Missile& missile);
    // Fold a spell missile's damage and reports into the queued effect that owns it.
    void absorbMissileDamage(Missile& missile);

    Game& game_;
};
