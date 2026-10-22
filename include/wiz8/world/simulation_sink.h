#pragma once

class Missile;
class Monster;
class Navigator;

/* What a world's entities report to the game that simulates them. An entity detects
   the moment (a missile touches a navigator, an animation reaches its release frame);
   the game decides what happens and resolves it. The game acts at once, inside the
   entity's update, so the order of random draws is unchanged. */
class SimulationSink {
public:
    virtual ~SimulationSink() = default;
    // A flying missile touched `struck`; answers whether it hit, ending its flight.
    virtual bool missileReached(Missile& missile, Navigator& struck) = 0;
    // A missile's flight ended: it struck something or ran its course.
    virtual void missileLanded(Missile& missile) = 0;
    // A monster's attack cycle reached the frame that launches its missile.
    virtual void missileReleased(Monster& monster) = 0;
    // A monster's casting cycle reached the frame that releases its spell.
    virtual void spellReleased(Monster& monster) = 0;
};
