/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

#include "wiz8/rules/random.h"

/* The presentation's random stream: sound variants and volumes, music, particles,
   animated textures and lights, prop animation, camera shake and portrait flourishes.
   It is seeded from the clock and never shared with a game (`Game::rng`), so what is
   drawn or played, and how often, cannot change a game's draws. */
Rng& PresentationRng();
