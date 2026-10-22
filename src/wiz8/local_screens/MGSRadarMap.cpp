#include "wiz8/local_screens/MGSRadarMap.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

#include <glm/geometric.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int2.hpp"
#include "sight_state.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/game.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/targeting_presentation.h"

// Six classes with three elevation shades; highlights cycle from dim to bright.
constexpr std::array<glm::vec3, 18> radar_blip_colors = {{
    {0.5f, 0.5f, 0.0f},
    {0.7f, 0.7f, 0.0f},
    {1.0f, 1.0f, 0.0f},
    {0.5f, 0.0f, 0.0f},
    {0.7f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f},
    {0.0f, 0.5f, 0.0f},
    {0.0f, 0.7f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.2f, 0.2f, 0.2f},
    {0.5f, 0.5f, 0.5f},
    {0.7f, 0.7f, 0.7f},
    {0.5f, 0.5f, 0.5f},
    {0.7f, 0.7f, 0.7f},
    {1.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 0.5f},
    {0.0f, 0.0f, 0.7f},
    {0.0f, 0.0f, 1.0f},
}};

/* UbDisposition to blip class for live monsters. */
constexpr std::array<W8RadarBlipClass, 4> radar_disposition_class = {
    W8_RADAR_BLIP_NEUTRAL, W8_RADAR_BLIP_HOSTILE, W8_RADAR_BLIP_FRIENDLY, W8_RADAR_BLIP_NEUTRAL};

/* Screen offsets at which the zoomed map image gets each
   occupied formation cell's party-order chip painted. */
constexpr glm::ivec2 radar_cell_offsets[15] = {
    {45, 37}, {42, 38}, {48, 38}, {53, 45}, {52, 42}, {52, 48}, {45, 53}, {48, 52},
    {42, 52}, {37, 45}, {38, 48}, {38, 42}, {45, 42}, {43, 46}, {47, 46},
};

RadarMap::RadarMap(Game& game, bool zoomed) : game_(game)
{
    setZoom(zoomed);
}
RadarMap::~RadarMap() = default;

float RadarMap::outerRadius() const
{
    return CalcRangeDistance(zoomed_ ? W8_RANGE_LONG : W8_RANGE_EXTREME) + party_radius_;
}

void RadarMap::captureBackdrop()
{
    if (backdrop_ == 0) {
        W8ControlsRect bounds;

        bounds.left = 0x17;
        bounds.top = 0x166;
        bounds.right = 0x80;
        bounds.bottom = 0x1c2;
        backdrop_ = makeOverlayFromSurface(GetFrameBufferSurface(), &bounds, 0, 1);
        positionOverlay(backdrop_.get(), 0x17, 0x166, false);
    }
}

void RadarMap::refresh()
{
    const auto map_artwork = CatalogArtwork::Load(zoomed_ ? 0xa3 : 0xa4);
    auto map_surface = RenderSpriteToImage(*map_artwork.spriteSheet(), 0);
    if (!map_surface) return;
    if (zoomed_) {
        for (int slot = 0; slot < 8; ++slot) {
            const auto seat = game_.party.formation.cellOf(slot);

            if (game_.party.members[slot].has_value() && seat) {
                const int cell = seat->row * 3 + seat->column;
                CatalogArtwork::Load(0xa5, 0).draw(*map_surface, game_.party.orderIndexOf(slot),
                                                   radar_cell_offsets[cell].x,
                                                   radar_cell_offsets[cell].y);
            }
        }
    }

    const auto frame_artwork = CatalogArtwork::Load(hovered_ ? 0xa2 : 0xa1);
    auto frame_surface = RenderSpriteToImage(*frame_artwork.spriteSheet(), 0);
    if (!frame_surface) return;
    const auto compass_artwork = CatalogArtwork::Load(0x9b);
    auto compass_surface = RenderSpriteToImage(*compass_artwork.spriteSheet(), 0);
    if (!compass_surface) return;

    compass_ = makeOverlayFromSurface(*compass_surface, 0, 1, 1);
    positionOverlay(compass_.get(), 0x1e, 0x167, false);
    frame_ = makeOverlayFromSurface(*frame_surface, 0, 1, 1);
    positionOverlay(frame_.get(), 0x1e, 0x167, false);
    map_ = makeOverlayFromSurface(*map_surface, 0, 1, 1);
    positionOverlay(map_.get(), 0x1e, 0x167, false);

    backdrop_.reset(); // Recapture only after the HUD has painted the new backdrop.
    update({});
}

void RadarMap::update(const W8TargetingPresentation& presentation)
{
    glm::vec3 position;
    glm::vec3 delta;
    glm::vec3 center;
    glm::vec3 party;
    bool detect_all;

    for (auto& blip : blips_) blip->visible = false;
    std::size_t next_blip = 0;
    if (game_.surprise_possible) {
        return;
    }
    if (compass_ != 0) {
        compass_->rotation = static_cast<float>(
            (game_.status.party_facing - static_cast<int>(game_.status.party_heading) + 0x168) *
            -0.017453292519943295);
    }
    if (frame_ != 0) {
        frame_->rotation = static_cast<float>((game_.status.party_facing) * -0.017453292519943295);
    }
    detect_all = PartyHasCondition(0x40);

    for (const auto& world_item : CurrentLevelPopulation().world_items) {
        WorldItemEntity* item = world_item->entity.find();

        if (item != 0) {
            if ((item->flags & W8_ITEM_ENTITY_NO_PICKUP) == 0) {
                position = item->GetLocation();
                const auto bounds = item->GetCachedLocalBounds();
                center = bounds.center();
                position += center;
                party = PartyNavigator()->GetPosition();
                delta = position - party;
                if ((detect_all || (item->flags & W8_ITEM_ENTITY_RADAR_SEEN) != 0 ||
                     hasCameraLineOfSight(position)) &&
                    glm::length(delta) <= outerRadius()) {
                    placeBlip(delta, W8_RADAR_BLIP_ITEM, item->IsRadarBlipLit(), next_blip++);
                    item->flags |= W8_ITEM_ENTITY_RADAR_SEEN;
                }
            }
        }
    }

    for (const auto& monster_owner : CurrentLevelPopulation().monster_roster.live()) {
        auto* info = monster_owner.get();
        Monster* monster = info->entity.find();

        if (monster != 0 && info->fActive && info->within_viewing_distance &&
            (!monster->disabled || detect_all)) {
            bool hostile = false;

            const auto mark = presentation.monsters.find(info->location_id);
            if (game_.combat.has_value() && game_.status.selected_character != -1 &&
                mark != presentation.monsters.end() &&
                game_.party.members[game_.status.selected_character].has_value() &&
                (static_cast<unsigned char>(1 << game_.status.selected_character) &
                 mark->second.party_targets) != 0) {
                hostile = true;
            }
            if (!monster->IsRenderable(true) && !detect_all) {
                if (info->party_threat.sight_state == W8_SIGHT_RECENT) {
                    const auto bounds = monster->GetAnimationBounds();
                    if (!bounds) {

                        continue;
                    }
                    center = bounds->center();
                    position = center + info->party_threat.own_position;
                    party = PartyNavigator()->GetPosition();
                    delta = position - party;
                    float distance = glm::length(delta);
                    if (distance - monster->nav.radius < outerRadius()) {
                        distance = (distance - monster->nav.radius) / distance;
                        delta.x *= distance;
                        delta.z *= distance;
                        placeBlip(delta, W8_RADAR_BLIP_LAST_SEEN, hostile, next_blip++);
                    } else {
                        delta.y = 0.0f;
                        distance = glm::length(delta);
                        if (distance - monster->nav.radius > outerRadius()) {
                            distance = outerRadius() / (distance - monster->nav.radius);
                            delta.x *= distance;
                            delta.z *= distance;
                            placeBlip(delta, W8_RADAR_BLIP_LAST_SEEN, hostile, next_blip++);
                        }
                    }
                }
            } else {
                const auto bounds = monster->GetAnimationBounds();
                if (!bounds) {

                    continue;
                }
                center = bounds->center();
                party = monster->nav.GetPosition();
                position = center + party;
                party = PartyNavigator()->GetPosition();
                delta = position - party;
                float distance = glm::length(delta);
                if (distance - monster->nav.radius < outerRadius()) {
                    distance = (distance - monster->nav.radius) / distance;
                    delta.x *= distance;
                    delta.z *= distance;
                    placeBlip(delta, radar_disposition_class[info->ubDisposition], hostile,
                              next_blip++);
                } else {
                    delta.y = 0.0f;
                    distance = glm::length(delta);
                    if (distance - monster->nav.radius > outerRadius()) {
                        distance = outerRadius() / (distance - monster->nav.radius);
                        delta.x *= distance;
                        delta.z *= distance;
                        placeBlip(delta, radar_disposition_class[info->ubDisposition], hostile,
                                  next_blip++);
                    }
                }
            }
        }
    }

    Missile* missile = NextMissile(true);
    while (missile != 0) {
        if (!missile->impacting) {
            const auto bounds = missile->visual.GetAnimationBounds();
            if (!bounds) {
                missile = NextMissile(false);
                continue;
            }
            center = bounds->center();
            party = missile->nav.GetPosition();
            position = center + party;
            party = PartyNavigator()->GetPosition();
            delta = position - party;
            if (glm::length(delta) < outerRadius()) {
                placeBlip(delta, W8_RADAR_BLIP_MISSILE, false, next_blip++);
            }
        }
        missile = NextMissile(false);
    }
}

void RadarMap::placeBlip(glm::vec3 delta, W8RadarBlipClass group, bool lit, std::size_t index)
{
    const float inner_radius = party_radius_ + (zoomed_ ? CalcRangeDistance(W8_RANGE_TOUCH) : 0);
    const float map_scale = zoomed_ ? 13.0f : 2.0f;
    int ring = 0;
    int left;
    int top;

    if (fabs(delta.y) >= 1500.0) {
        if (delta.y > 0.0) {
            ring = 2;
        }
    } else {
        ring = 1;
    }

    const float angle =
        static_cast<float>(360 - game_.status.party_facing) * (std::numbers::pi_v<float> / 180.0f);
    delta = glm::rotateY((delta), angle);
    delta.y = 0.0f;

    float distance = glm::length((delta));
    if (distance <= inner_radius) {
        float scale;

        if (group == W8_RADAR_BLIP_MISSILE || group == W8_RADAR_BLIP_ITEM) {
            scale = map_scale / inner_radius;
        } else {
            scale = map_scale / distance;
        }
        left = static_cast<int>(scale * delta.x) + 0x4b;
        top = 0x194 - static_cast<int>(scale * delta.z);
    } else {
        float scale = map_scale + (distance - inner_radius) * (38.0f - map_scale) /
                                      (outerRadius() - inner_radius);

        if (scale > 38.0f) {
            scale = 38.0f;
        }
        scale /= distance;
        left = static_cast<int>(scale * delta.x + 75.0f);
        top = static_cast<int>(404.0f - scale * delta.z);
    }
    const int sector = ring + group * 3;
    const glm::vec4 color(radar_blip_colors[sector], 1);
    if (index == blips_.size()) blips_.push_back(makeColorOverlay(2, 2, color, false));
    auto& blip = *blips_[index];
    blip.color = color;
    blip.visible = true;
    blip.glow_enabled = lit;
    if (lit) {
        blip.glow_colors = {glm::vec4(radar_blip_colors[group * 3], 1),
                            glm::vec4(radar_blip_colors[group * 3 + 2], 1)};
        blip.glow_period = 1000;
    }
    // Reattach in traversal order, preserving the old draw/overlap order across classes.
    g_compositor.attach(blip, OverlayLayer::SquareAspect);
    positionOverlay(&blip, left, top, false);
}

void RadarMap::setZoom(bool zoomed)
{
    zoomed_ = zoomed;
    party_radius_ = PartyNavigator()->radius;
    refresh();
}

void RadarMap::setHovered(bool hovered)
{
    hovered_ = hovered;
    refresh();
}
