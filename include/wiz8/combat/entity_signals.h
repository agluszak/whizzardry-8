#pragma once

class SimulationSink;

/* Combat's resolution of what the world's entities report: missile hits, and the
   frames on which monsters launch missiles and release spells. The game's level
   installs it into the worlds it owns. */
SimulationSink& CombatEntitySignals();
