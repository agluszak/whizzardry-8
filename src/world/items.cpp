#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_filesystem.h>
#include <fmt/format.h>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include "wiz8/asset_paths.h"
#include "wiz8/containers.h"
#include "wiz8/data/game_data.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/level_population.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/random.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

namespace {
constexpr std::array<std::string_view, 145> itemModelFallbackNames = {"Dagger",
                                                                      "Long Sword",
                                                                      "Bipennis",
                                                                      "Battle Axe",
                                                                      "Flail",
                                                                      "Mace",
                                                                      "Hammer",
                                                                      "Staff",
                                                                      "Halberd",
                                                                      "Spear",
                                                                      "Bo Stick",
                                                                      "Bow",
                                                                      "Crossbow",
                                                                      "Sling",
                                                                      "Great Sword",
                                                                      "Rapier",
                                                                      "Katana",
                                                                      "Long Staff",
                                                                      "Wand",
                                                                      "Magic Stave",
                                                                      "Spellbook",
                                                                      "Shuriken",
                                                                      "Arrows",
                                                                      "Bullets",
                                                                      "Kite Shield",
                                                                      "Basnet",
                                                                      "Plate Torso",
                                                                      "Plate Leggings",
                                                                      "Gauntlets",
                                                                      "Sollerets",
                                                                      "Amulet",
                                                                      "Ring",
                                                                      "Blue Potion",
                                                                      "Scroll",
                                                                      "Powder",
                                                                      "Key",
                                                                      "Locket",
                                                                      "Great Bow",
                                                                      "Round Shield",
                                                                      "Wizard Cone",
                                                                      "Leather Helm",
                                                                      "Cuirass",
                                                                      "Leather Top",
                                                                      "Greaves",
                                                                      "Leather Leggings",
                                                                      "Gloves",
                                                                      "Sandals",
                                                                      "Buskins",
                                                                      "Boots",
                                                                      "Book",
                                                                      "Ankh",
                                                                      "Ninjato",
                                                                      "War Hammer",
                                                                      "Flamberge",
                                                                      "Bullwhip",
                                                                      "Sai",
                                                                      "Nunchuka",
                                                                      "Glaive",
                                                                      "Black Sword",
                                                                      "Fire Sword",
                                                                      "Upper Robes",
                                                                      "Lower Robes",
                                                                      "Halter",
                                                                      "Skirt",
                                                                      "Skullcap",
                                                                      "Feathered Cap",
                                                                      "Mitre",
                                                                      "Breast Plate",
                                                                      "Kabuto",
                                                                      "Upper Toseido",
                                                                      "Lower Toseido",
                                                                      "Burgonet",
                                                                      "Fur Leggings",
                                                                      "Cloak",
                                                                      "Mantis Gloves",
                                                                      "Mantis Boots",
                                                                      "Garland",
                                                                      "Cat 'O Nine Tails",
                                                                      "Silver Cross",
                                                                      "Ruby Talisman",
                                                                      "Purple Amulet",
                                                                      "Bracelet",
                                                                      "Necklace",
                                                                      "Quarrels",
                                                                      "Jade Figure",
                                                                      "Chain Coif",
                                                                      "Upper Chain",
                                                                      "Lower Chain",
                                                                      "Mail Mittens",
                                                                      "Chain Hosen",
                                                                      "Lance",
                                                                      "Armet",
                                                                      "Ninja Cowl",
                                                                      "Upper Ninja Garb",
                                                                      "Lower Ninja Garb",
                                                                      "Tabi Boots",
                                                                      "Heaume",
                                                                      "Box Helm",
                                                                      "Diamond Ring",
                                                                      "Bag",
                                                                      "Red Potion",
                                                                      "Green Potion",
                                                                      "Purple Potion",
                                                                      "Yellow Potion",
                                                                      "Power Glove",
                                                                      "T'Rang Staff",
                                                                      "Bronze Gauntlets",
                                                                      "Comm Link",
                                                                      "Light Sword",
                                                                      "Magic Bag",
                                                                      "Phaser",
                                                                      "ID Card",
                                                                      "Darts",
                                                                      "Powder Shot",
                                                                      "Musket",
                                                                      "Coin",
                                                                      "Stone",
                                                                      "Stick",
                                                                      "Short Sword",
                                                                      "UNDEFINED",
                                                                      "Bagpipes",
                                                                      "Lyre",
                                                                      "Lute",
                                                                      "Horn",
                                                                      "Map",
                                                                      "Bracelet",
                                                                      "Shuriken",
                                                                      "UNDEFINED",
                                                                      "Wand",
                                                                      "Yellow Potion",
                                                                      "Medium Shield",
                                                                      "UNDEFINED",
                                                                      "UNDEFINED",
                                                                      "UNDEFINED",
                                                                      "UNDEFINED",
                                                                      "UNDEFINED",
                                                                      "Saxophone",
                                                                      "Violin",
                                                                      "Drum",
                                                                      "Bullroarer",
                                                                      "bomb",
                                                                      "emptybottle",
                                                                      "stix",
                                                                      "rocket",
                                                                      "rocketlauncher"};
}

W8WorldItem* World::adoptItem(std::unique_ptr<W8WorldItem> item)
{
    auto* result = item.get();
    if (item) population->world_items.push_back(std::move(item));
    return result;
}

void World::activateItem(const GameData& data, Rng& rng, W8WorldItem* item)
{
    assert(item && !item->entity);
    const auto& definition = data.items[item->item.iItemNo];
    std::string name = definition.internal_name;
    if (name.empty()) name = itemModelFallbackNames[definition.unidentified_name_index];
    const auto path = fmt::format("Data\\Items3D\\{}{}", name,
                                  name.find(".ITM") == std::string::npos ? ".ITM" : "");
    const auto status = wiz8::file_status(wiz8::asset_path(path));
    if (!status || status->info.type != SDL_PATHTYPE_FILE) name = "questionmark";

    auto entity = LoadItemFromFile(name);
    if (!entity) throw std::runtime_error(fmt::format("cannot load item model {}", name));
    item->entity = std::move(entity);
    if (ItemHasFlags(item, W8_WORLD_ITEM_RADAR_BLIP_LIT)) item->entity->LightRadarBlip();
    item->entity->SetLocation(&item->position);
    item->entity->SetYaw(
        static_cast<float>(rng.below(0x168) * 2 * g_camera_pi * kInverseFullTurnDegrees));
    item->entity->flags |= item->entity_flags;
    item->entity->AttachMesh(this);
    if (auto* sun = FindWorldLight(*this, "SUN")) {
        if (auto* mesh = item->entity->GetMesh()) {
            const auto sun_position = glm::vec3(sun->transform.getLocation());
            mesh->light_scale =
                glm::vec3(compiledIndex()->hasLineOfSight(item->position, sun_position) ? 1 : 0);
        }
    }
}

/* Drop one item onto the ground below where it is. The search starts one world
   unit up so an item already resting does not settle into the floor; landing
   moves it between sectors and clears its saved-marker flag. */
bool World::settleItem(W8WorldItem* item)
{
    glm::vec3 start;
    int sector;

    start = glm::vec3(item->position.x, item->position.y + kWorldScale, item->position.z);

    item->flags &= ~W8_WORLD_ITEM_FALLING;
    item->vertical_velocity = 0.0f;

    const auto ground_probe = compiledIndex()->settleToGround(start, 1, 250.0f);
    start = ground_probe.position;
    if (!ground_probe.contact) {
        return false;
    }

    sector = ground_probe.prop().value_or(-1);
    if (sector != item->sector_id) {
        if (item->sector_id >= 0) {
            removeItemFromSector(item->sector_id, item);
        }
        if (sector >= 0) {
            addItemToSector(sector, item);
        }
        item->sector_id = sector;
    }
    if (item->entity) {
        item->entity->SetLocation(&start);
    }
    item->position = start;
    return true;
}

/* Register the item on the collidable prop's sector list, building the path
   representation and the list itself on first use. */
void World::addItemToSector(int sector, W8WorldItem* item)
{
    if (!item || sector < 0) return;
    auto& prop = *collidable_props[sector];
    if (!prop.collision_ready) prop.BuildOrRefreshPathingRepresentation(WorldPathing(*this));
    if (prop.collision_ready && !std::ranges::contains(prop.m_supported_items, item))
        prop.m_supported_items.push_back(item);
}

/* Drop the item from the collidable prop's sector list when both exist. */
void World::removeItemFromSector(int sector, W8WorldItem* item)
{
    if (item != 0 && sector >= 0) {
        Prop* prop = collidable_props[sector];
        if (prop != 0 && prop->collision_ready) {
            wiz8::erase_first(prop->m_supported_items, item);
        }
    }
}

void W8WorldItem::deactivate(Events& events)
{
    assert(entity);
    position = entity->GetLocation();
    entity_flags = entity->flags;
    entity.reset();
    events.push(WorldItemRemoved{runtime_id});
}

void World::updateNearbyItems(const GameData& data, Rng& rng, Events& events, float gravity)
{
    const auto camera = party.eye();
    // Outcome observers can change this collection; retain the existing live-index order.
    for (std::size_t index = 0; index < population->world_items.size(); ++index) {
        auto& item = *population->world_items[index];
        if (item.sector_id < -1) {
            item.sector_id = -1;
            settleItem(&item);
        }
        if (ItemHasFlags(&item, W8_WORLD_ITEM_FALLING)) advanceFallingItem(item, gravity);
        if (!item.entity) {
            if (!ItemHasFlags(&item, W8_WORLD_ITEM_HIDDEN) &&
                glm::distance(item.position, camera) < 20000.0f)
                activateItem(data, rng, &item);
        } else if (glm::distance(item.entity->GetLocation(), camera) > 25000.0f) {
            item.deactivate(events);
        }
    }
}

/* Advance one falling world item (flag bit 1) toward the ground for this
   frame. Clears the falling flag once it has landed or the settle misses. */
bool World::advanceFallingItem(W8WorldItem& item, float gravity)
{
    glm::vec3 probe;
    float ground;
    float previous_y;
    float dt;
    int sector;

    if (!ItemHasFlags(&item, W8_WORLD_ITEM_FALLING)) {
        return false;
    }

    previous_y = item.position.y;
    probe = glm::vec3(item.position.x, previous_y + kWorldScale, item.position.z);
    dt = clocks.motionDelta();
    if (kCameraSnapEpsilon < item.vertical_velocity) {
        probe.y = dt * item.vertical_velocity + probe.y;
    }

    const auto ground_probe = compiledIndex()->settleToGround(probe, 1, 250.0f);
    probe = ground_probe.position;
    ground = ground_probe.position.y;
    if (!ground_probe.contact || fabs(ground - previous_y) < kCameraSnapEpsilon) {
        item.flags &= ~W8_WORLD_ITEM_FALLING;
        item.vertical_velocity = 0.0f;
        return false;
    }

    sector = ground_probe.prop().value_or(-1);
    if (sector != item.sector_id) {
        if (item.sector_id >= 0) {
            removeItemFromSector(item.sector_id, &item);
        }
        if (sector >= 0) {
            addItemToSector(sector, &item);
        }
        item.sector_id = sector;
    }

    if (previous_y <= ground) {
        item.vertical_velocity = (ground - previous_y) / dt;
        probe.y = ground;
    } else {
        item.vertical_velocity = dt * gravity * 0.5f + item.vertical_velocity;
        probe.y = probe.y - dt * item.vertical_velocity;
        if (probe.y < ground) {
            probe.y = ground;
        }
    }

    if (item.entity) {
        item.entity->SetLocation(&probe);
    }
    item.position = probe;
    return true;
}
