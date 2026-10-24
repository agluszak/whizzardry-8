#pragma once

#include "wiz8/render/frame.h"

class Game;

// Presentation composition reads this game's worlds and clock, not an installed session.
SceneFrame GameSceneFrame(const Game* game);
std::optional<Pick> PresentGameFrame(Game* game, bool pick_world = false);
bool CaptureGameView(Game& game, SDL_Surface& target, const DesignRect& bounds);
// A layout change also updates the party camera's projection and sway once.
void SetWorldViewport(Game* game, const DesignRect& bounds);
