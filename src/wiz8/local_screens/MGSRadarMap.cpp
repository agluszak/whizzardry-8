#include "sprite.h"
#include "surrender/srScene.h" // IWYU pragma: keep - complete scene type for conversion to srNode.
#include <glm/gtx/rotate_vector.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "wiz8/containers.h"
#include "wiz8/local_screens/MGSRadarMap.h"
#include "surrender/srMath.h"
#include "surrender/srNode.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/float_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/item_spawning.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/startup_world.h"
#include "wiz8/video_object_catalog.h"
#include "wiz8/xstatus.h"
#include "wiz8/engine_code/GameData.h"
#include "AnimRep.hpp"
#include "glm/ext/vector_float4.hpp"
#include "layouts/gameplay_databases.h"
#include "layouts/main_game_screen.h"
#include "layouts/party_formation.h"
#include "sight_state.h"

/* The blip palette, six classes of three distance rings. Items
   take class 4 and missiles class 5; monsters take their disposition-mapped
   class while a merely-sensed one and anything in the near field use class 3.
   AcquireRadarBlip's highlight path reuses rows group*3 and group*3+2 as the
   facing/up orientation vectors. */
// GLOBAL: WIZ8 0x0064ca90
static float g_radar_blip_colors[18][3] = {
    {0.5f, 0.5f, 0.0f}, {0.7f, 0.7f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.5f, 0.0f, 0.0f},
    {0.7f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 0.7f, 0.0f},
    {0.0f, 1.0f, 0.0f}, {0.2f, 0.2f, 0.2f}, {0.5f, 0.5f, 0.5f}, {0.7f, 0.7f, 0.7f},
    {0.5f, 0.5f, 0.5f}, {0.7f, 0.7f, 0.7f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.5f},
    {0.0f, 0.0f, 0.7f}, {0.0f, 0.0f, 1.0f},
};

/* UbDisposition to blip class for live monsters. */
// GLOBAL: WIZ8 0x0064cb68
static unsigned char g_radar_disposition_class[4] = {0, 1, 2, 0};

/* Screen offsets at which the zoomed map image gets each
   occupied formation cell's party-order chip painted. */
// GLOBAL: WIZ8 0x0064cb6c
static srVector2i g_radar_cell_offsets[15] = {
    {45, 37}, {42, 38}, {48, 38}, {53, 45}, {52, 42}, {52, 48}, {45, 53}, {48, 52},
    {42, 52}, {37, 45}, {38, 48}, {38, 42}, {45, 42}, {43, 46}, {47, 46},
};

/* The radar overlay's runtime state: the lazily built backdrop, the zoom
   preset flag, the three sprites, the eighteen sector blip pools with their
   per-sector reuse cursors, and the active range/scale band. */
// GLOBAL: WIZ8 0x0069bf58
static stModelInstance2D* g_radar_backdrop = 0;
// GLOBAL: WIZ8 0x0069bf5c
static bool g_radar_zoomed = false;
// GLOBAL: WIZ8 0x0069bf60
static stModelInstance2D* g_radar_compass = 0;
// GLOBAL: WIZ8 0x0069c088
static stModelInstance2D* g_radar_frame = 0;
// GLOBAL: WIZ8 0x0069bf68
static std::vector<stModelInstance2D*> g_radar_icon_pools[18];
// GLOBAL: WIZ8 0x0069c08c
static int g_radar_icon_cursors[18];
// GLOBAL: WIZ8 0x0069c0d4
static float g_radar_outer_radius;
// GLOBAL: WIZ8 0x0069c0d8
static float g_radar_inner_radius;
// GLOBAL: WIZ8 0x0069c0dc
static stModelInstance2D* g_radar_map = 0;
// GLOBAL: WIZ8 0x0069c0e0
static float g_radar_map_scale;
// GLOBAL: WIZ8 0x0069c0e4
static bool g_radar_map_enabled = false;

// GLOBAL: WIZ8 0x005eecd8
const double g_radar_pi = 3.141592653589793;
// GLOBAL: WIZ8 0x005eece8
const float g_radar_center_y = 404.0f;
// GLOBAL: WIZ8 0x005eecec
const float g_radar_center_x = 75.0f;
// GLOBAL: WIZ8 0x005eecf0
const float g_radar_max_marker_scale = 38.0f;

static stModelInstance2D* AcquireRadarBlip(int sector, bool lit);
static unsigned char PlaceRadarBlip(srVector3T<float>* delta, int group, bool lit);

static void ResetRadarBlips()
{
    for (int sector = 0; sector < 18; ++sector) {
        auto& pool = g_radar_icon_pools[sector];
        int count = static_cast<int>(pool.size());

        g_radar_icon_cursors[sector] = 0;
        for (int index = 0; index < count; ++index) {
            (pool[index])->setFlag(srNode::FLAG_DISABLE);
        }
    }
}

// FUNCTION: WIZ8 0x005a20e0
void EnableRadarMap(bool enable)
{
    g_radar_map_enabled = enable;
    if (!enable) {
        ResetRadarBlips();
    }
}

// FUNCTION: WIZ8 0x005a2140
void EnsureRadarMapOverlay(void)
{
    if (g_radar_backdrop == 0) {
        W8ControlsRect bounds;

        bounds.left = 0x17;
        bounds.top = 0x166;
        bounds.right = 0x80;
        bounds.bottom = 0x1c2;
        g_radar_backdrop = CreateSpriteFromVideoSurface(GetFrameBufferSurface(), &bounds, 0, 0, 1);
        Position2DNodeUnsnapped(g_radar_backdrop, 0x17, 0x166);
        SetModelInstance2DDisplayState(g_radar_backdrop, 4);
    }
}

// FUNCTION: WIZ8 0x005a21b0
static stModelInstance2D* AcquireRadarBlip(int sector, bool lit)
{
    auto& pool = g_radar_icon_pools[sector];
    stModelInstance2D* icon;
    int cursor = g_radar_icon_cursors[sector];

    if (cursor < static_cast<int>(pool.size())) {
        g_radar_icon_cursors[sector] = cursor + 1;
        icon = pool[cursor];
    } else {
        icon = new stModelInstance2D(0);
        *icon = *pool[0];
        pool.push_back(icon);
    }
    if (icon != 0) {
        icon->clearFlag(srNode::FLAG_DISABLE);
        icon->setParent(0, 1);
        icon->setParent(g_scene_square, 1);
        icon->overlay_scene_flag |= 1;
        if (!lit) {
            icon->SetGlowEnabled(false);
        } else {
            glm::vec4 first;
            glm::vec4 second;
            int group = sector - sector % 3;

            icon->SetGlowEnabled(true);
            second.w = 1.0f;
            first.w = 1.0f;
            second.x = g_radar_blip_colors[group + 2][0];
            second.y = g_radar_blip_colors[group + 2][1];
            second.z = g_radar_blip_colors[group + 2][2];
            first.x = g_radar_blip_colors[group][0];
            first.y = g_radar_blip_colors[group][1];
            first.z = g_radar_blip_colors[group][2];
            icon->SetGlowColors(first, second);
            icon->render_state.render_depth = 1000;
        }
    }
    return icon;
}

// FUNCTION: WIZ8 0x005a23e0
void ReleaseRadarMap(void)
{
    if (g_radar_map != 0) {
        ReleaseObject(g_radar_map);
        g_radar_map = 0;
    }
    if (g_radar_frame != 0) {
        ReleaseObject(g_radar_frame);
        g_radar_frame = 0;
    }
    if (g_radar_compass != 0) {
        ReleaseObject(g_radar_compass);
        g_radar_compass = 0;
    }
    for (int sector = 0; sector < 18; ++sector) {
        auto& pool = g_radar_icon_pools[sector];

        wiz8::remove_all_and_delete_in_order(pool);
    }
    ReleaseRendererObject(g_radar_backdrop);
}

// FUNCTION: WIZ8 0x005a24a0
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
            W8PartySlotRow* row = &g_status.buffers.XChar[slot];
            W8PartyFormationPosition* position = &g_status.formation.positions[slot];

            if (row->fOccupied && position->bQuadrant != -1) {
                int cell = position->bQuadrant * 3 + position->bQuadrantSlot;
                CatalogArtwork::Load(0xa5, 0).draw(*map_surface, row->party_order_index, g_radar_cell_offsets[cell].x, g_radar_cell_offsets[cell].y);
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

    g_radar_compass = CreateSpriteFromSurface(*compass_surface, 0, 1, 0, 1);
    Position2DNodeUnsnapped(g_radar_compass, 0x1e, 0x167);
    SetModelInstance2DDisplayState(g_radar_compass, 4);
    g_radar_frame = CreateSpriteFromSurface(*frame_surface, 0, 1, 0, 1);
    Position2DNodeUnsnapped(g_radar_frame, 0x1e, 0x167);
    SetModelInstance2DDisplayState(g_radar_frame, 4);
    g_radar_map = CreateSpriteFromSurface(*map_surface, 0, 1, 0, 1);
    Position2DNodeUnsnapped(g_radar_map, 0x1e, 0x167);
    SetModelInstance2DDisplayState(g_radar_map, 4);

    for (sector = 0; sector < 18; ++sector) {
        glm::vec4 color;
        stModelInstance2D* icon;

        color.x = g_radar_blip_colors[sector][0];
        color.y = g_radar_blip_colors[sector][1];
        color.z = g_radar_blip_colors[sector][2];
        color.w = 1.0f;
        icon = CreateColoredPolygonSprite(2, 2, &color, false);
        g_radar_icon_pools[sector].push_back(icon);
        icon->overlay_scene_flag |= 1;
    }
    UpdateRadarBlips();
}

// FUNCTION: WIZ8 0x005a2800
void UpdateRadarBlips(void)
{
    srVector3T<float> camera;
    srVector3T<float> position;
    srVector3T<float> delta;
    srVector3T<float> center;
    srVector3T<float> party;
    bool detect_all;

    ResetRadarBlips();
    if (!g_radar_map_enabled || gXStatus.fSurprisePossible) {
        return;
    }
    if (g_radar_compass != 0) {
        RotateNodeInDegrees(g_radar_compass, g_status.party_facing -
                                                 static_cast<int>(g_status.party_heading) + 0x168);
    }
    if (g_radar_frame != 0) {
        RotateNodeInDegrees(g_radar_frame, g_status.party_facing);
    }
    camera = cameraLocation();
    detect_all = PartyHasCondition(0x40);

    W8WorldItem* world_item = GetNextWorldItem(true);
    while (world_item != 0) {
        W8Item* item = world_item->p3D;

        if (item != 0) {
            W8ItemRep* rep = static_cast<W8ItemRep*>(item->m_pRep);
            if ((rep->flags & W8_ITEM_ENTITY_NO_PICKUP) == 0) {
                position = rep->GetLocation();
                const auto bounds = item->GetCachedLocalBounds();
                center = bounds.center();
                position += center;
                party = g_startup_world->GetPosition();
                delta = position - party;
                if ((detect_all || (rep->flags & W8_ITEM_ENTITY_RADAR_SEEN) != 0 ||
                     HasCameraLineOfSight(&position)) &&
                    delta.Length() <= g_radar_outer_radius) {
                    if (PlaceRadarBlip(&delta, 4, item->IsRadarBlipLit()) != 0) {
                        rep->flags |= W8_ITEM_ENTITY_RADAR_SEEN;
                    }
                }
            }
        }
        world_item = GetNextWorldItem(false);
    }

    W8MonsterInfo* info = GetNextMonsterInfo(true);
    while (info != 0) {
        W8Monster* monster = info->p3D;

        if (monster != 0 && info->fActive && info->within_viewing_distance &&
            (!monster->disabled || detect_all)) {
            bool hostile = false;

            if (gXStatus.fCombatMode && g_status.selected_character != -1 &&
                g_status.buffers.XChar[g_status.selected_character].fOccupied != 0 &&
                (static_cast<unsigned char>(1 << g_status.selected_character) &
                 MonsterGetHighlightMask(monster)) != 0) {
                hostile = true;
            }
            if (!monster->IsRenderable(true) && !detect_all) {
                if (info->party_threat.sight_state == W8_SIGHT_RECENT) {
                    const auto bounds = monster->GetAnimationBounds();
                    if (!bounds) {
                        info = GetNextMonsterInfo(false);
                        continue;
                    }
                    center = bounds->center();
                    position = center + info->party_threat.own_position;
                    party = g_startup_world->GetPosition();
                    delta = position - party;
                    float distance = delta.Length();
                    if (distance - monster->radius < g_radar_outer_radius) {
                        distance = (distance - monster->radius) / distance;
                        delta.x *= distance;
                        delta.z *= distance;
                        PlaceRadarBlip(&delta, 3, hostile);
                    } else {
                        delta.y = 0.0f;
                        distance = delta.Length();
                        if (distance - monster->radius > g_radar_outer_radius) {
                            distance = g_radar_outer_radius / (distance - monster->radius);
                            delta.x *= distance;
                            delta.z *= distance;
                            PlaceRadarBlip(&delta, 3, hostile);
                        }
                    }
                }
            } else {
                const auto bounds = monster->GetAnimationBounds();
                if (!bounds) {
                    info = GetNextMonsterInfo(false);
                    continue;
                }
                center = bounds->center();
                party = monster->GetPosition();
                position = center + party;
                party = g_startup_world->GetPosition();
                delta = position - party;
                float distance = delta.Length();
                if (distance - monster->radius < g_radar_outer_radius) {
                    distance = (distance - monster->radius) / distance;
                    delta.x *= distance;
                    delta.z *= distance;
                    PlaceRadarBlip(&delta, g_radar_disposition_class[info->ubDisposition], hostile);
                } else {
                    delta.y = 0.0f;
                    distance = delta.Length();
                    if (distance - monster->radius > g_radar_outer_radius) {
                        distance = g_radar_outer_radius / (distance - monster->radius);
                        delta.x *= distance;
                        delta.z *= distance;
                        PlaceRadarBlip(&delta, g_radar_disposition_class[info->ubDisposition],
                                       hostile);
                    }
                }
            }
        }
        info = GetNextMonsterInfo(false);
    }

    W8Missile* missile = NextMissile(true);
    while (missile != 0) {
        if (!missile->impacting) {
            const auto bounds = missile->GetAnimationBounds();
            if (!bounds) {
                missile = NextMissile(false);
                continue;
            }
            center = bounds->center();
            party = missile->GetPosition();
            position = center + party;
            party = g_startup_world->GetPosition();
            delta = position - party;
            if (delta.Length() < g_radar_outer_radius) {
                PlaceRadarBlip(&delta, 5, false);
            }
        }
        missile = NextMissile(false);
    }
    SetRendererModePair();
}

// FUNCTION: WIZ8 0x005a3060
static unsigned char PlaceRadarBlip(srVector3T<float>* delta, int group, bool lit)
{
    int ring = 0;
    int left;
    int top;

    if (fabs(delta->y) >= g_double_fifteen_hundred) {
        if (delta->y > g_double_zero) {
            ring = 2;
        }
    } else {
        ring = 1;
    }

    const float angle = static_cast<float>(360 - g_status.party_facing) *
                        (std::numbers::pi_v<float> / 180.0f);
    *delta = glm::rotateY(delta->glmValue(), angle);
    delta->y = 0.0f;

    float distance = delta->Length();
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
    stModelInstance2D* blip = AcquireRadarBlip(ring + group * 3, lit);
    Position2DNodeUnsnapped(blip, left, top);
    return 1;
}

// FUNCTION: WIZ8 0x005a3360
void ToggleRadarMapZoom(void)
{
    if (!g_radar_zoomed) {
        float radius = g_startup_world->radius;

        g_radar_zoomed = true;
        g_radar_map_scale = 13.0f;
        g_radar_inner_radius = CalcRangeDistance(W8_RANGE_TOUCH) + radius;
        g_radar_outer_radius = CalcRangeDistance(W8_RANGE_LONG) + radius;
        RefreshRadarMap();
        return;
    }
    ZoomRadarMapOut();
}

// FUNCTION: WIZ8 0x005a3410
void ZoomRadarMapIn(void)
{
    g_radar_zoomed = true;
    g_radar_map_scale = 13.0f;
    g_radar_inner_radius = CalcRangeDistance(W8_RANGE_TOUCH) + g_startup_world->radius;
    g_radar_outer_radius = CalcRangeDistance(W8_RANGE_LONG) + g_startup_world->radius;
    RefreshRadarMap();
}

// FUNCTION: WIZ8 0x005a3470
void ZoomRadarMapOut(void)
{
    float radius = g_startup_world->radius;

    g_radar_zoomed = false;
    g_radar_map_scale = 2.0f;
    g_radar_inner_radius = radius;
    g_radar_outer_radius = CalcRangeDistance(W8_RANGE_EXTREME) + radius;
    RefreshRadarMap();
}
