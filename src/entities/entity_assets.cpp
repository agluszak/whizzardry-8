#include "wiz8/entities/entity_assets.h"

#include <algorithm>
#include <string_view>

#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/unicode.h"
#include "wiz8/world/world.h"

Monster* findMonsterAsset(const World& world, std::string_view name)
{
    const auto found =
        std::find_if(world.monsters.begin(), world.monsters.end(), [&](const auto& monster) {
            return CompareTextIgnoreAsciiCase(monster->asset_name, name) == 0;
        });
    return found == world.monsters.end() ? nullptr : found->get();
}
Missile* findMissileAsset(const World& world, std::string_view name)
{
    const auto found =
        std::find_if(world.missiles.begin(), world.missiles.end(), [&](const auto& missile) {
            return CompareTextIgnoreAsciiCase(missile->asset_name, name) == 0;
        });
    return found == world.missiles.end() ? nullptr : found->get();
}
SpellVisual* findSpellVisualAsset(const World& world, std::string_view name, W8SpellVisualMode mode)
{
    const auto found = std::find_if(
        world.spell_visuals.begin(), world.spell_visuals.end(), [&](const auto& visual) {
            return visual->mode == mode &&
                   CompareTextIgnoreAsciiCase(visual->asset_name, name) == 0;
        });
    return found == world.spell_visuals.end() ? nullptr : found->get();
}
