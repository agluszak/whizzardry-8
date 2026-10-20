#include "wiz8/local_screens/MGSRadarMap.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int2.hpp"
#include "layouts/main_game_screen.h"
#include "sight_state.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/game.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/targeting_presentation.h"
#include "wiz8/xstatus.h"

/* The blip palette, six classes of three distance rings. Items
   take class 4 and missiles class 5; monsters take their disposition-mapped
   class while a merely-sensed one and anything in the near field use class 3.
   AcquireRadarBlip's highlight path reuses rows group*3 and group*3+2 as the
   facing/up orientation vectors. */
static float g_radar_blip_colors[18][3] = {
    {0.5f, 0.5f, 0.0f}, {0.7f, 0.7f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.5f, 0.0f, 0.0f},
    {0.7f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 0.7f, 0.0f},
    {0.0f, 1.0f, 0.0f}, {0.2f, 0.2f, 0.2f}, {0.5f, 0.5f, 0.5f}, {0.7f, 0.7f, 0.7f},
    {0.5f, 0.5f, 0.5f}, {0.7f, 0.7f, 0.7f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.5f},
    {0.0f, 0.0f, 0.7f}, {0.0f, 0.0f, 1.0f},
};

/* UbDisposition to blip class for live monsters. */
static unsigned char g_radar_disposition_class[4] = {0, 1, 2, 0};

/* Screen offsets at which the zoomed map image gets each
   occupied formation cell's party-order chip painted. */
static glm::ivec2 g_radar_cell_offsets[15] = {
    {45, 37}, {42, 38}, {48, 38}, {53, 45}, {52, 42}, {52, 48}, {45, 53}, {48, 52},
    {42, 52}, {37, 45}, {38, 48}, {38, 42}, {45, 42}, {43, 46}, {47, 46},
};

/* The radar overlay's runtime state: the lazily built backdrop, the zoom
   preset flag, the three sprites, the eighteen sector blip pools with their
   per-sector reuse cursors, and the active range/scale band. */
static std::unique_ptr<OverlayQuad> g_radar_backdrop;
static bool g_radar_zoomed = false;
static std::unique_ptr<OverlayQuad> g_radar_compass;
static std::unique_ptr<OverlayQuad> g_radar_frame;
static std::vector<std::unique_ptr<OverlayQuad>> g_radar_icon_pools[18];
static int g_radar_icon_cursors[18];
static float g_radar_outer_radius;
static float g_radar_inner_radius;
static std::unique_ptr<OverlayQuad> g_radar_map;
static float g_radar_map_scale;
static bool g_radar_map_enabled = false;

const float g_radar_center_y = 404.0f;
const float g_radar_center_x = 75.0f;
const float g_radar_max_marker_scale = 38.0f;

static OverlayQuad* AcquireRadarBlip(int sector, bool lit);
static unsigned char PlaceRadarBlip(glm::vec3* delta, int group, bool lit);

static void ResetRadarBlips()
{
    for (int sector = 0; sector < 18; ++sector) {
        auto& pool = g_radar_icon_pools[sector];
        int count = static_cast<int>(pool.size());

        g_radar_icon_cursors[sector] = 0;
        for (int index = 0; index < count; ++index) {
            (pool[index])->visible = false;
        }
    }
}

void EnableRadarMap(bool enable)
{
    g_radar_map_enabled = enable;
    if (!enable) {
        ResetRadarBlips();
    }
}

void EnsureRadarMapOverlay(void)
{
    if (g_radar_backdrop == 0) {
        W8ControlsRect bounds;

        bounds.left = 0x17;
        bounds.top = 0x166;
        bounds.right = 0x80;
        bounds.bottom = 0x1c2;
        g_radar_backdrop = makeOverlayFromSurface(GetFrameBufferSurface(), &bounds, 0, 1);
        positionOverlay(g_radar_backdrop.get(), 0x17, 0x166, false);
    }
}

static OverlayQuad* AcquireRadarBlip(int sector, bool lit)
{
    auto& pool = g_radar_icon_pools[sector];
    OverlayQuad* icon;
    int cursor = g_radar_icon_cursors[sector];

    if (cursor < static_cast<int>(pool.size())) {
        g_radar_icon_cursors[sector] = cursor + 1;
        icon = pool[cursor].get();
    } else {
        auto owned = std::make_unique<OverlayQuad>(*pool.front());
        icon = owned.get();
        pool.push_back(std::move(owned));
    }
    if (icon != 0) {
        icon->visible = true;
        icon->detach();
        g_compositor.attach(*icon, OverlayLayer::SquareAspect);
        if (!lit) {
            icon->glow_enabled = false;
        } else {
            glm::vec4 first;
            glm::vec4 second;
            int group = sector - sector % 3;

            icon->glow_enabled = true;
            second.w = 1.0f;
            first.w = 1.0f;
            second.x = g_radar_blip_colors[group + 2][0];
            second.y = g_radar_blip_colors[group + 2][1];
            second.z = g_radar_blip_colors[group + 2][2];
            first.x = g_radar_blip_colors[group][0];
            first.y = g_radar_blip_colors[group][1];
            first.z = g_radar_blip_colors[group][2];
            icon->glow_colors = {first, second};
            icon->glow_period = 1000;
        }
    }
    return icon;
}

void ReleaseRadarMap(void)
{
    if (g_radar_map != 0) {
        g_radar_map.reset();
        g_radar_map = 0;
    }
    if (g_radar_frame != 0) {
        g_radar_frame.reset();
        g_radar_frame = 0;
    }
    if (g_radar_compass != 0) {
        g_radar_compass.reset();
        g_radar_compass = 0;
    }
    for (auto& pool : g_radar_icon_pools) {
        pool.clear();
    }
    g_radar_backdrop.reset();
}

void RefreshRadarMap(void)
{
    int sector;

    if (!g_radar_map_enabled) {
        return;
    }
    ReleaseRadarMap();

    const auto map_artwork = CatalogArtwork::Load(g_radar_zoomed ? 0xa3 : 0xa4);
    auto map_surface = RenderSpriteToImage(*map_artwork.spriteSheet(), 0);
    if (!map_surface) return;
    if (g_radar_zoomed) {
        for (int slot = 0; slot < 8; ++slot) {
            W8PartySlotRow* row = &CurrentGame().party.rows[slot];
            W8PartyFormationPosition* position = &CurrentGame().party.formation.positions[slot];

            if (row->fOccupied && position->bQuadrant != -1) {
                int cell = position->bQuadrant * 3 + position->bQuadrantSlot;
                CatalogArtwork::Load(0xa5, 0).draw(*map_surface, row->party_order_index,
                                                   g_radar_cell_offsets[cell].x,
                                                   g_radar_cell_offsets[cell].y);
            }
        }
    }

    const auto frame_artwork =
        CatalogArtwork::Load(g_level_block->radar_map_alternate ? 0xa2 : 0xa1);
    auto frame_surface = RenderSpriteToImage(*frame_artwork.spriteSheet(), 0);
    if (!frame_surface) return;
    const auto compass_artwork = CatalogArtwork::Load(0x9b);
    auto compass_surface = RenderSpriteToImage(*compass_artwork.spriteSheet(), 0);
    if (!compass_surface) return;

    g_radar_compass = makeOverlayFromSurface(*compass_surface, 0, 1, 1);
    positionOverlay(g_radar_compass.get(), 0x1e, 0x167, false);
    g_radar_frame = makeOverlayFromSurface(*frame_surface, 0, 1, 1);
    positionOverlay(g_radar_frame.get(), 0x1e, 0x167, false);
    g_radar_map = makeOverlayFromSurface(*map_surface, 0, 1, 1);
    positionOverlay(g_radar_map.get(), 0x1e, 0x167, false);

    for (sector = 0; sector < 18; ++sector) {
        glm::vec4 color;
        color.x = g_radar_blip_colors[sector][0];
        color.y = g_radar_blip_colors[sector][1];
        color.z = g_radar_blip_colors[sector][2];
        color.w = 1.0f;
        g_radar_icon_pools[sector].push_back(makeColorOverlay(2, 2, color, false));
    }
    UpdateRadarBlips({});
}

void UpdateRadarBlips(const W8TargetingPresentation& presentation)
{
    glm::vec3 camera;
    glm::vec3 position;
    glm::vec3 delta;
    glm::vec3 center;
    glm::vec3 party;
    bool detect_all;

    ResetRadarBlips();
    if (!g_radar_map_enabled || CurrentGame().surprise_possible) {
        return;
    }
    if (g_radar_compass != 0) {
        g_radar_compass->rotation =
            static_cast<float>((CurrentGame().status.party_facing -
                                static_cast<int>(CurrentGame().status.party_heading) + 0x168) *
                               -0.017453292519943295);
    }
    if (g_radar_frame != 0) {
        g_radar_frame->rotation =
            static_cast<float>((CurrentGame().status.party_facing) * -0.017453292519943295);
    }
    camera = cameraLocation();
    detect_all = PartyHasCondition(0x40);

    for (const auto& world_item : CurrentLevelPopulation().world_items) {
        WorldItemEntity* item = world_item->p3D;

        if (item != 0) {
            if ((item->flags & W8_ITEM_ENTITY_NO_PICKUP) == 0) {
                position = item->GetLocation();
                const auto bounds = item->GetCachedLocalBounds();
                center = bounds.center();
                position += center;
                party = PartyNavigator()->GetPosition();
                delta = position - party;
                if ((detect_all || (item->flags & W8_ITEM_ENTITY_RADAR_SEEN) != 0 ||
                     HasCameraLineOfSight(&position)) &&
                    glm::length(delta) <= g_radar_outer_radius) {
                    if (PlaceRadarBlip(&delta, 4, item->IsRadarBlipLit()) != 0) {
                        item->flags |= W8_ITEM_ENTITY_RADAR_SEEN;
                    }
                }
            }
        }
    }

    for (const auto& monster_owner : CurrentLevelPopulation().monster_roster.live()) {
        auto* info = monster_owner.get();
        Monster* monster = info->p3D;

        if (monster != 0 && info->fActive && info->within_viewing_distance &&
            (!monster->disabled || detect_all)) {
            bool hostile = false;

            const auto mark = presentation.monsters.find(info->location_id);
            if (CurrentGame().combat.active() && CurrentGame().status.selected_character != -1 &&
                mark != presentation.monsters.end() &&
                CurrentGame().party.rows[CurrentGame().status.selected_character].fOccupied != 0 &&
                (static_cast<unsigned char>(1 << CurrentGame().status.selected_character) &
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
                    if (distance - monster->nav.radius < g_radar_outer_radius) {
                        distance = (distance - monster->nav.radius) / distance;
                        delta.x *= distance;
                        delta.z *= distance;
                        PlaceRadarBlip(&delta, 3, hostile);
                    } else {
                        delta.y = 0.0f;
                        distance = glm::length(delta);
                        if (distance - monster->nav.radius > g_radar_outer_radius) {
                            distance = g_radar_outer_radius / (distance - monster->nav.radius);
                            delta.x *= distance;
                            delta.z *= distance;
                            PlaceRadarBlip(&delta, 3, hostile);
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
                if (distance - monster->nav.radius < g_radar_outer_radius) {
                    distance = (distance - monster->nav.radius) / distance;
                    delta.x *= distance;
                    delta.z *= distance;
                    PlaceRadarBlip(&delta, g_radar_disposition_class[info->ubDisposition], hostile);
                } else {
                    delta.y = 0.0f;
                    distance = glm::length(delta);
                    if (distance - monster->nav.radius > g_radar_outer_radius) {
                        distance = g_radar_outer_radius / (distance - monster->nav.radius);
                        delta.x *= distance;
                        delta.z *= distance;
                        PlaceRadarBlip(&delta, g_radar_disposition_class[info->ubDisposition],
                                       hostile);
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
            if (glm::length(delta) < g_radar_outer_radius) {
                PlaceRadarBlip(&delta, 5, false);
            }
        }
        missile = NextMissile(false);
    }
}

static unsigned char PlaceRadarBlip(glm::vec3* delta, int group, bool lit)
{
    int ring = 0;
    int left;
    int top;

    if (fabs(delta->y) >= 1500.0) {
        if (delta->y > 0.0) {
            ring = 2;
        }
    } else {
        ring = 1;
    }

    const float angle = static_cast<float>(360 - CurrentGame().status.party_facing) *
                        (std::numbers::pi_v<float> / 180.0f);
    *delta = glm::rotateY((*delta), angle);
    delta->y = 0.0f;

    float distance = glm::length((*delta));
    if (distance <= g_radar_inner_radius) {
        float scale;

        if (group == 5 || group == 4) {
            scale = g_radar_map_scale / g_radar_inner_radius;
        } else {
            scale = g_radar_map_scale / distance;
        }
        left = static_cast<int>(scale * delta->x) + 0x4b;
        top = 0x194 - static_cast<int>(scale * delta->z);
    } else {
        float scale = g_radar_map_scale + (distance - g_radar_inner_radius) *
                                              (g_radar_max_marker_scale - g_radar_map_scale) /
                                              (g_radar_outer_radius - g_radar_inner_radius);

        if (scale > g_radar_max_marker_scale) {
            scale = g_radar_max_marker_scale;
        }
        scale /= distance;
        left = static_cast<int>(scale * delta->x + g_radar_center_x);
        top = static_cast<int>(g_radar_center_y - scale * delta->z);
    }
    OverlayQuad* blip = AcquireRadarBlip(ring + group * 3, lit);
    positionOverlay(blip, left, top, false);
    return 1;
}

void ToggleRadarMapZoom(void)
{
    if (!g_radar_zoomed) {
        float radius = PartyNavigator()->radius;

        g_radar_zoomed = true;
        g_radar_map_scale = 13.0f;
        g_radar_inner_radius = CalcRangeDistance(W8_RANGE_TOUCH) + radius;
        g_radar_outer_radius = CalcRangeDistance(W8_RANGE_LONG) + radius;
        RefreshRadarMap();
        return;
    }
    ZoomRadarMapOut();
}

void ZoomRadarMapIn(void)
{
    g_radar_zoomed = true;
    g_radar_map_scale = 13.0f;
    g_radar_inner_radius = CalcRangeDistance(W8_RANGE_TOUCH) + PartyNavigator()->radius;
    g_radar_outer_radius = CalcRangeDistance(W8_RANGE_LONG) + PartyNavigator()->radius;
    RefreshRadarMap();
}

void ZoomRadarMapOut(void)
{
    float radius = PartyNavigator()->radius;

    g_radar_zoomed = false;
    g_radar_map_scale = 2.0f;
    g_radar_inner_radius = radius;
    g_radar_outer_radius = CalcRangeDistance(W8_RANGE_EXTREME) + radius;
    RefreshRadarMap();
}
