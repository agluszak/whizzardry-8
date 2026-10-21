#pragma once
#include <string_view>
struct World;
class Missile;
class SpellVisual;
enum W8SpellVisualMode : int;

// Find a live source to clone within this world's owned entities.
Missile* findMissileAsset(const World& world, std::string_view name);
SpellVisual* findSpellVisualAsset(const World& world, std::string_view name,
                                  W8SpellVisualMode mode);
