#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vector_relational.hpp>

#include "audio_resources.h"
#include "geometry.h"
#include "glm/ext/matrix_float2x2.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtx/projection.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/combat/fight.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/ground_probe.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/math.h"
#include "wiz8/packed_bits.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/segment_math.h"
#include "wiz8/render/triangle_projection.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/spatial/surface_index.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment_state.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/party_motion.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

constexpr float kCameraRadiansToDegrees = 57.295784f;
constexpr float kCollisionCeilingNormalThreshold = -0.8f;
constexpr double kCollisionCentroidScale = 0.33333298563957214;
constexpr float kCollisionContactNormalThreshold = -0.5f;
constexpr double kCollisionDirectionEpsilon = 0.0010000000474974513;
constexpr float kCollisionFractionFloor = -0.1f;
constexpr float kCollisionHeightLift = 165.0f;
constexpr float kCollisionOpposedNormalThreshold = -0.999f;
constexpr float kCollisionParallelNormalThreshold = 0.99999f;
constexpr float kCollisionSlopeMarginScale = 150.0f;

class Material;
class Texture;

GroundProbe probeGround(const glm::vec3& position)
{
    if (GetWorldSpatialIndex()) return GetWorldSpatialIndex()->settleToGround(position, true, 500);
    return {position, std::nullopt};
}

/* The ground height under `position` plus the footstep surface/material of
   the level surface the probe settled on; both stay none when the probe
   missed or a prop stopped it. */
GroundSurfaceInfo groundSurfaceInfo(const glm::vec3& position)
{
    const auto probe = probeGround(position);
    GroundSurfaceInfo result{probe.position.y, W8_FOOTSTEP_SURFACE_NONE, W8_FOOTSTEP_MATERIAL_NONE};
    const auto* contact =
        probe.contact ? std::get_if<GroundSurfaceContact>(&*probe.contact) : nullptr;
    if (GetWorldCollision() && contact) {
        const auto& hit = GetWorldCollision()->m_pSurfaces[contact->surface];
        result.surface = hit.footstep_surface;
        result.material = hit.footstep_material;
    }
    return result;
}

/* Apply world-render camera-motion flags into `rotation` and mirror the yaw
   rotation into `saved`. ECX is the owning LevelSpatialIndex; the body reads only
   globals. */
const double g_motion_vector_epsilon = 5.0;

void LevelEnvironment::SetScaledMotion(const glm::vec3& motion)
{
    vector = motion / scale;
    /* 0x00421843 raises the airborne flag with the vector, so the record counts
       as airborne from the frame the scaled motion is stored. */
    airborne = 1;
}

glm::vec3 LevelEnvironment::withScaledMotion(const glm::vec3& position) const
{
    return position + vector * scale;
}

W8GDSurface* LevelSpatialIndex::ProbePropsAlongMotion(
    glm::vec3* direction, glm::vec3* position, glm::vec3* scratch, float* nearest_distance,
    W8Plane& prop_hit_plane, PartyMotion& motion, bool& motion_active,
    std::map<const W8GDSurface*, W8GDSurface>& contacts)
{
    W8GDSurface* nearest_surface = nullptr;
    W8GDSurface* surface;
    Prop* prop;
    Prop* gd_prop;
    PartyMotion* level;
    unsigned int index;
    int surface_index;
    int hit_prop_id;
    bool direction_zero;
    float slope;
    float along_length;
    float residual_length;
    float facing;
    glm::vec3 prop_delta;
    glm::vec3 adjusted_direction;
    glm::vec3 probe;
    glm::vec3 test_direction;
    glm::vec3 hit_point;
    glm::vec3 normal;
    glm::vec3 projected;
    glm::vec3 residual;
    glm::vec3 along_normal;

    if (!owner_world || owner_world->collidable_props.empty()) {
        return nullptr;
    }
    const auto objects = this->queryAlongSegment((*position), (*direction), 504.0f, 8);
    for (index = 0; index < objects.size(); ++index) {
        prop = owner_world->collidable_props[objects[index]];
        if (prop->GetActivationState() != 0) {
            gd_prop = (prop->collision_ready ? prop : nullptr);
            prop->flags |= 0x10;
            if (gd_prop == 0) {
                prop->BuildOrRefreshPathingRepresentation(pathing.get());
                gd_prop = (prop->collision_ready ? prop : nullptr);
                if (gd_prop == 0) {
                    return nullptr;
                }
            }
            prop_delta = prop->GetDelta();
            adjusted_direction = *direction - prop_delta;
            probe = *position;
            if (adjusted_direction.x != 0.0f || adjusted_direction.y != 0.0f ||
                adjusted_direction.z != 0.0f) {
                direction_zero = false;
            } else {
                direction_zero = true;
            }
            test_direction = adjusted_direction;
            for (surface_index = 0; surface_index < static_cast<int>(gd_prop->m_pGDSurfaces.size());
                 ++surface_index) {
                const auto* definition = &gd_prop->m_pGDSurfaces[surface_index];
                surface = &contacts.try_emplace(definition, *definition).first->second;
                if (direction_zero) {
                    test_direction = surface->plane.normal;
                }
                if ((surface->flags & W8_GD_SURFACE_INACTIVE_MASK) != 0) continue;
                const auto result = surface->segmentContact(
                    probe, test_direction,
                    indexedTriangle(gd_prop->m_pVertices, surface->vertex_indices),
                    LiftsCrossings(motion));
                motion_active = motion_active && !result.stops_movement;
                if (const auto& contact = result.contact) {
                    if (contact->distance < *nearest_distance) {
                        *nearest_distance = contact->distance;
                        hit_point = contact->point;
                        *scratch = prop_delta;
                        nearest_surface = surface;
                        nearest_surface->distance = contact->surface_limit;
                        hit_prop_id = objects[index];
                    }
                    probe = *position;
                }
            }
        }
        level = &motion;
    }
    if (nearest_surface) {
        prop_hit_plane = nearest_surface->plane;
        slope = nearest_surface->slope;
        projected = residual = along_normal = (*scratch);
        normal = prop_hit_plane.normal;
        along_normal = ProjectVectorOntoVector(along_normal, normal);
        along_length = glm::length(along_normal);
        residual = glm::dvec3(residual - along_normal) * static_cast<double>(slope);
        residual_length = glm::length(residual);
        if (glm::length(projected) <= 1.0f) {
            facing = 0.0f;
        } else {
            facing = glm::dot(projected, normal) / glm::length(projected);
            if (facing < 0.0f) {
                along_length = -along_length;
            }
        }
        level = &motion;
        if (level->primary_contact_prop_id == -1) {
            level->primary_contact_prop_id = hit_prop_id;
        } else if (level->primary_contact_prop_id != hit_prop_id &&
                   level->secondary_contact_prop_id != hit_prop_id) {
            level->secondary_contact_prop_id = hit_prop_id;
        }
        if ((level->flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0 ||
            level->residual_contact_length < residual_length) {
            level->flags |= W8_LEVEL_FLAG_PROP_CONTACT;
            level->contact_motion = projected;
            level->residual_contact_length = residual_length;
            *direction += along_normal - residual;
            level->contact_displacement = residual;
            level->contact_velocity = residual / level->camera_scale;
        }
        if (level->contact_facing < facing) {
            level->contact_facing = facing;
            level->contact_normal = normal;
        }
        prop_hit_plane.w -= along_length;
        *position = hit_point + *scratch;
    }
    return nearest_surface;
}

unsigned char LevelSpatialIndex::ProbeMonstersAlongMotion(glm::vec3* direction, glm::vec3* position,
                                                          int)
{
    std::vector<std::uint32_t> objects;
    std::size_t count;
    unsigned int index;
    W8MonsterInfo* monster_info;
    glm::vec3 lower;
    glm::vec3 upper;
    glm::vec3 velocity;
    glm::vec3 monster_position;
    glm::vec3 adjustment;
    float extent;
    glm::vec2 adjusted;
    float horizontal;
    glm::vec3 delta;
    float planar;
    float radius;
    float push;
    float length_squared;
    float scale;
    double time_scale;
    bool hit;

    if (!owner_world || !owner_world->population) return false;
    hit = false;
    extent = glm::length((*direction)) + registry.maximum_navigation_radius + kWorldScale;
    lower = (*position) - extent;
    upper = (*position) + extent;
    objects = queryObjects({lower, upper}, W8_OCTREE_KIND_LOCATION, -1);
    count = objects.size();
    if (count == 0) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        if (objects[index] == 0) {
            return hit;
        }
        monster_info = owner_world->population->findMonster(objects[index]);
        if (monster_info != 0 && monster_info->entity && monster_info->entity->nav.active) {
            Monster& monster = *monster_info->entity;
            time_scale = g_rate * owner_world->clocks.motionDelta();
            velocity = monster.nav.GetVelocity();
            adjusted.x = direction->x - velocity.x * static_cast<float>(time_scale);
            adjusted.y = direction->z - velocity.z * static_cast<float>(time_scale);
            horizontal = sqrtf(adjusted.x * adjusted.x + adjusted.y * adjusted.y);
            monster_position = monster.nav.GetPosition();
            delta.x = monster_position.x - (adjusted.x + position->x);
            delta.y = monster_position.y - position->y;
            delta.z = monster_position.z - (adjusted.y + position->z);
            planar = sqrtf(delta.z * delta.z + delta.x * delta.x);
            radius = monster.nav.radius;
            if (monster_info->pCombat) {
                radius += kWorldScale;
            }
            const float abs_dy = std::abs(delta.y);
            if (abs_dy < 1e+03f && planar < radius + kWorldScale) {
                push = kMonsterMotionPush - (planar - radius) * 0.002f;
                if (kMonsterMotionPush < push) {
                    push = kMonsterMotionPush;
                }
                length_squared = delta.z * delta.z + delta.x * delta.x;
                adjustment = delta;
                adjustment.y = 0.0f;
                if (length_squared != static_cast<float>(0.0)) {
                    scale = -(push * horizontal) / sqrtf(length_squared);
                    adjustment.x = delta.x * scale;
                    adjustment.z = delta.z * scale;
                }
                hit = true;
                *direction = adjustment + *direction;
            }
        }
    }
    return hit;
}

/* Environment-load camera advance: integrate level vectors, clamp environ
   gravity into the path, then walk up to five collision attempts against
   props/octree/geometry before committing crossed surfaces. */
unsigned char LevelSpatialIndex::AdvanceEnvironmentMotion(PartyMotion& motion,
                                                          ActiveEnvironment& environment)
{
    auto* environ = &environment.value;
    PartyMotion* level;
    LevelEnvironment* environ_record;
    glm::vec3 camera_position;
    glm::vec3 adjusted_position;
    glm::vec3 motion_delta;
    glm::vec3 environ_delta;
    glm::vec3 probe_position;
    glm::vec3 hit_position;
    glm::vec3 gravity;
    glm::vec3 scaled;
    W8GDSurface* nearest_surface;
    W8GDSurface* surface;
    W8GDSurface* collisions[101];
    std::vector<std::uint32_t> octree_hits;
    std::vector<const W8GDSurface*> geometry_hits;
    std::ptrdiff_t hit_count = 0;
    int collision_count;
    int attempt;
    int index;
    int crossed_count;
    float nearest_distance;
    float motion_length;
    float scale;
    glm::vec3 scratch;
    bool first_pass;
    bool exhausted;
    bool forced_exit;
    bool prop_hit;
    glm::vec3 geometry_from;
    glm::vec3 geometry_to;

    bool motion_active = true;
    bool ground_latch = false;
    std::map<const W8GDSurface*, W8GDSurface> contacts;
    const auto contactSurface = [&](const W8GDSurface* definition) {
        auto& contact = contacts.try_emplace(definition, *definition).first->second;
        const auto id = definition->index;
        contact.flags = effectiveSurfaceFlags(contact, id < state.conditional_disabled.size()
                                                           ? state.conditional_disabled[id]
                                                           : std::nullopt);
        if (const auto it = state.crossing_margins.find(definition->index);
            it != state.crossing_margins.end())
            contact.contact_margin = it->second;
        return &contact;
    };
    CollisionResponse response;
    W8Plane prop_hit_plane;
    W8GDSurface* prop_surface = nullptr;
    prop_hit = false;
    if (motion.ClampCameraToBounds(*environ, &minimum, &maximum) != 0) {
        return 0;
    }

    level = &motion;
    level->flags &= ~(W8_LEVEL_FLAG_PROP_CONTACT | W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH);
    level->motion_velocity += level->contact_velocity;
    level->contact_displacement = level->contact_velocity * level->camera_scale;
    level->motion_displacement += level->contact_displacement;
    level->residual_contact_length = 0.0f;
    level->contact_facing = 0.0f;

    level = &motion;
    camera_position = level->camera_position;
    adjusted_position = camera_position;
    if (geometry_index == 0 && !compiled_available) {
        if (static_cast<float>(g_motion_delta_epsilon) < glm::length(level->motion_displacement)) {
            return 1;
        }
        return 0;
    }

    ground_latch = environ->ground_latch;
    motion_active = 1;
    if (glm::length(level->motion_velocity) <= static_cast<float>(g_motion_vector_epsilon)) {
        level->flags &= ~W8_LEVEL_FLAG_NEGLIGIBLE_MOTION;
    } else {
        level->flags |= W8_LEVEL_FLAG_NEGLIGIBLE_MOTION;
    }

    environ_record = environ;
    environ_record->motion_factor = environ_record->motion_step;
    environ_record->airborne = 0;
    environ_record->scale = level->camera_scale;
    gravity =
        glm::vec3(environ_record->gravity_x, environ_record->gravity_y, environ_record->gravity_z);
    scaled = glm::vec3(glm::dvec3(gravity) * static_cast<double>(environ_record->scale));
    environ_record->vector += scaled;
    if (g_camera_motion_divisor < glm::length(environ_record->vector)) {
        environ_record->vector = normalizedOrZero(environ_record->vector) * g_camera_motion_divisor;
    }
    level->motion_displacement = environ_record->withScaledMotion(level->motion_displacement);

    if (level->camera_scale == 0.0f) {
        level->motion_velocity = glm::vec3(0);
    } else {
        level->motion_velocity = level->motion_displacement / level->camera_scale;
    }

    environ_delta = level->motion_displacement;
    camera_position =
        glm::vec3(adjusted_position.x, (kWorldScale - environ->world_height) + adjusted_position.y,
                  adjusted_position.z);
    motion_delta = environ_delta;
    adjusted_position.y = camera_position.y;

    if (m_iNumTriggers != 0) {
        if (state.active_triggers.empty()) {
            IntegrateTriggers();
        } else {
            std::ranges::fill(state.active_triggers, false);
        }
    }

    level = &motion;
    nearest_distance = 1.0e8f;
    hit_position = glm::vec3(0);
    level->secondary_contact_prop_id = -1;
    level->primary_contact_prop_id = -1;
    level->flags &= ~W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH;
    level->contact_normal = glm::vec3(0);
    collision_count = -1;
    level->contact_facing = 0.0f;
    first_pass = true;
    nearest_surface = 0;
    forced_exit = false;
    exhausted = false;
    attempt = 0;

    const auto probe_octree_motion = [&] {
        if (attempt < 3) ProbeMonstersAlongMotion(&motion_delta, &probe_position, 1);
        nearest_surface =
            ProbePropsAlongMotion(&motion_delta, &probe_position, &scratch, &nearest_distance,
                                  prop_hit_plane, motion, motion_active, contacts);
        prop_surface = nearest_surface;
        prop_hit = nearest_surface != nullptr;
        if (prop_hit) hit_position = probe_position;
        probe_position = camera_position;
        octree_hits = this->queryAlongSegment(camera_position, motion_delta, 1000.0f, 3);
        hit_count = octree_hits.size();
    };

    for (;;) {
        ++attempt;
        if (attempt < 6) {
            motion_active = 1;
            probe_position = camera_position;
            first_pass = true;
            nearest_surface = 0;
            nearest_distance = 1.0e8f;
            if (geometry_index == 0) {
                if (compiled_available) {
                    probe_octree_motion();
                } else {
                    hit_count = 0;
                }
            } else {
                geometry_to = motion_delta;
                geometry_from = camera_position;
                nearest_surface = 0;
                geometry_hits =
                    geometry_index->queryAlongSegment(geometry_from, geometry_to, 2000.0f);
                hit_count = geometry_hits.size();
            }
            if (((hit_count == 0 && !prop_hit) || motion_active == 0)) {
                exhausted = true;
            }
        } else {
            motion_active = 0;
            exhausted = true;
            forced_exit = true;
            probe_position = camera_position;
            if ((motion.flags & W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH) != 0 &&
                motion.ToggleBoundProps() == 0) {
                environ_delta = glm::vec3(0);
            }
        }

        while ((hit_count != 0 || prop_hit) && motion_active != 0) {
            if (hit_count != 0) {
                probe_position = camera_position;
                for (index = 0; index < hit_count; ++index) {
                    if (!compiled_available) {
                        surface = contactSurface(geometry_hits[index]);
                    } else {
                        surface = contactSurface(&m_pSurfaces[octree_hits[index]]);
                    }
                    if ((surface->flags &
                         (W8_GD_SURFACE_CROSSING_MASK | W8_GD_SURFACE_INACTIVE_MASK)) != 0)
                        continue;
                    const auto result = surface->segmentContact(
                        probe_position, motion_delta,
                        indexedTriangle(m_pVertices, surface->vertex_indices),
                        LiftsCrossings(motion));
                    motion_active = motion_active && !result.stops_movement;
                    if (const auto& contact = result.contact) {
                        surface->distance = contact->surface_limit;
                        if (contact->distance < nearest_distance) {
                            nearest_distance = contact->distance;
                            hit_position = contact->point;
                            nearest_surface = surface;
                        }
                        probe_position = camera_position;
                    }
                }
                if (first_pass) {
                    if (nearest_surface == 0) {
                        exhausted = true;
                    }
                    first_pass = false;
                }
            }

            if (nearest_surface == 0) {
                hit_count = 0;
                prop_hit = false;
            } else {
                motion_active = nearest_surface->ResolveCollision(
                    motion, *environ, &camera_position, &hit_position, &motion_delta,
                    collision_count, response,
                    nearest_surface == prop_surface ? &prop_hit_plane : nullptr, m_pSurfaces,
                    m_pVertices);
                probe_position = camera_position + motion_delta - adjusted_position;
                if (collision_count < 0) {
                    collision_count = 0;
                }
                collisions[collision_count] = nearest_surface;
                ++collision_count;
                assert(collision_count < 100);
                if (motion_active == 0) {
                    hit_count = 0;
                    prop_hit = false;
                } else {
                    probe_position = camera_position;
                    nearest_surface = 0;
                    nearest_distance = 1.0e8f;
                    if (!compiled_available) {
                        if (geometry_index != 0) {
                            geometry_hits = geometry_index->queryAlongSegment(geometry_from,
                                                                              geometry_to, 2000.0f);
                            hit_count = geometry_hits.size();
                        } else {
                            hit_count = 0;
                        }
                    } else {
                        probe_octree_motion();
                    }
                }
            }
        }

        if (exhausted) {
            if (compiled_available) {
                probe_position = adjusted_position;
                motion_delta = environ_delta;
                octree_hits = this->queryAlongSegment(adjusted_position, environ_delta, 1000.0f, 3);
                hit_count = octree_hits.size();
                crossed_count = 0;
                for (index = 0; index < hit_count; ++index) {
                    surface = contactSurface(&m_pSurfaces[octree_hits[index]]);
                    if ((surface->flags & W8_GD_SURFACE_CROSSING_MASK) == 0 ||
                        (surface->flags & W8_GD_SURFACE_INACTIVE_MASK) != 0)
                        continue;
                    const auto result = surface->segmentContact(
                        probe_position, motion_delta,
                        indexedTriangle(m_pVertices, surface->vertex_indices),
                        LiftsCrossings(motion));
                    motion_active = motion_active && !result.stops_movement;
                    if (const auto& contact = result.contact) {
                        surface->distance = contact->surface_limit;
                        if (0 < crossed_count) {
                            for (int swap = 0; swap < crossed_count; ++swap) {
                                W8GDSurface* prior = collisions[swap];
                                if (fabsf(surface->distance) < fabsf(prior->distance)) {
                                    collisions[swap] = surface;
                                    surface = prior;
                                }
                            }
                        }
                        collisions[crossed_count] = surface;
                        ++crossed_count;
                    }
                }
                for (index = 0; index < crossed_count; ++index) {
                    ProcessCrossedSurface(collisions[index], environment);
                }
            }

            level = &motion;
            motion_active = 0;
            motion_length = glm::length(environ_delta);
            if (motion_length <= 0.1f ||
                (fabsf(environ_delta.y + motion_length) / motion_length <= kCameraSnapEpsilon &&
                 g_negative_one <= environ_delta.y)) {
                environ_delta = glm::vec3(0);
            } else {
                motion_active = 1;
            }

            scale = static_cast<float>(1.0) / motion.camera_scale;
            level->motion_velocity = environ_delta * scale;
            level->motion_displacement = environ_delta;
            if (motion_active == 0) {
                if ((motion.flags & W8_LEVEL_FLAG_NEGLIGIBLE_MOTION) != 0 && !forced_exit) {
                    ground_latch = true;
                }
            } else {
                ground_latch = false;
            }
            environ->ground_latch = ground_latch;

            if (m_iNumTriggers != 0) {
                for (unsigned int trigger_index = 0; trigger_index < state.pending_triggers.size();
                     ++trigger_index) {
                    if (state.pending_triggers[trigger_index] &&
                        !TestBit(state.active_triggers, trigger_index)) {
                        if (!m_ppTriggers.empty()) {
                            m_ppTriggers[trigger_index]->FinishAction();
                        }
                        state.pending_triggers[trigger_index] = false;
                    }
                }
            }
            return motion_active;
        }

        environ_delta = camera_position + motion_delta - adjusted_position;
        camera_position = adjusted_position;
        if (0 < collision_count) {
            for (index = 0; index < collision_count; ++index) {
                collisions[index]->flags &= ~W8_GD_SURFACE_COLLISION_PROCESSED;
            }
        }
        collision_count = 0;
        motion_delta = environ_delta;
    }
}

/* 0x005EBB34: one float constant with two independent readings - the level
   vector's "no value" here, and Controls.cpp's own range start. Neither is
   proven, so it keeps its address. */
/* Each collidable prop's surfaces are traced in prop space through the
   prop's position delta. Only the segment's end moves into prop space; the
   start stays in world space, as in retail. */
std::optional<LevelSpatialIndex::PropTraceHit>
LevelSpatialIndex::tracePropList(std::span<const std::uint32_t> ids, const PlaneSegment& segment,
                                 PropTraceFilter filter) const
{
    std::optional<PropTraceHit> nearest;
    auto remaining = segment;
    for (const auto id : ids) {
        const auto* prop = owner_world->collidable_props[id];
        if (!prop->collision_ready || (filter.attached_only && prop->collision_unattached))
            continue;
        const auto delta = prop->GetDelta();
        const auto hit = traceSurfaces(prop->m_pGDSurfaces, prop->m_pVertices,
                                       planeSegment(remaining.start, remaining.end - delta),
                                       {.skip_filtered = filter.skip_filtered});
        if (!hit) continue;
        nearest = PropTraceHit{hit->point + delta, static_cast<int>(id)};
        remaining = planeSegment(remaining.start, nearest->position);
    }
    return nearest;
}

std::optional<SurfaceRayHit>
LevelSpatialIndex::traceGeometry(const PlaneSegment& segment, SurfaceTraceFilter filter,
                                 std::span<const unsigned int> surface_ids) const
{
    return traceSurfaces(m_pSurfaces, m_pVertices, segment, filter, surface_ids);
}

/* Selects a switch interface's state: the matching state group's conditional
   polygons clear surface flag 0x10 while every other state's polygons set
   it. Asserts the interface id and the state slice bounds. */
signed char LevelSpatialIndex::SetInterfaceState(int iID, int state)
{
    int iStates;
    int poly_count;
    W8GDInterface* interface_rec;
    W8GDInterfaceState* states;
    int* polys;

    assert(iID != 0 && InterfacesCount() > iID);
    this->state.conditional_disabled.resize(m_pSurfaces.size());
    interface_rec = m_pInterfaces.data() + iID;
    iStates = interface_rec->state_count;
    assert(StatesCount() >= interface_rec->iStates + iStates);
    states = m_pStates.data() + interface_rec->iStates;
    for (; iStates != 0; --iStates) {
        poly_count = states->poly_count;
        polys = m_piCondPolys.data() + states->poly_first;
        if (states->group == state) {
            for (; poly_count != 0; --poly_count) {
                this->state.conditional_disabled[*polys] = false;
                ++polys;
            }
        } else {
            for (; poly_count != 0; --poly_count) {
                this->state.conditional_disabled[*polys] = true;
                ++polys;
            }
        }
        ++states;
    }
    return 1;
}

/* Apply one surface the mover's trace crossed, nearest first: an environment
   boundary (flag 0x1000) swaps the active environment record, carrying the
   live blend fields across; a trigger surface runs its trigger in the
   crossing direction unless the bit sets already hold it. */
void LevelSpatialIndex::ProcessCrossedSurface(W8GDSurface* surface, ActiveEnvironment& environment)
{
    Trigger* trigger;
    int direction;

    if ((surface->flags & W8_GD_SURFACE_ENVIRONMENT) != 0) {
        const bool entering = surface->distance > 0.0f;
        if (!entering && environment.index != surface->trigger_index) return;
        const int index = entering ? surface->trigger_index : 0;
        if (index < 0 || static_cast<std::size_t>(index) >= environments.size()) return;
        selectEnvironment(environment, environments[index], index);
        return;
    }
    direction = 1;
    if (surface->distance < 0.0f) {
        direction = -1;
    }
    if (m_ppTriggers.empty() || SetBit(state.active_triggers, surface->trigger_index) ||
        SetBit(state.pending_triggers, surface->trigger_index)) {
        return;
    }
    trigger = m_ppTriggers[surface->trigger_index];
    if (trigger->initial_action == 0x10 && surface->contact_margin > 0.0f) {
        state.crossing_margins[surface->index] = 0.0f;
        surface->contact_margin = 0.0f;
        if (direction == 1) {
            ClearBit(state.active_triggers, surface->trigger_index);
            ClearBit(state.pending_triggers, surface->trigger_index);
            return;
        }
    }
    if (direction != 0) {
        trigger->Run(direction);
        if (trigger->action_state == W8TriggerActionState::inactive ||
            trigger->action_state == W8TriggerActionState::fallback_action) {
            ClearBit(state.active_triggers, surface->trigger_index);
            ClearBit(state.pending_triggers, surface->trigger_index);
        }
    }
}

/* Clip the motion segment against this surface's plane and triangle. On a hit
   the returned contact carries the point and travelled length; distance
   takes the surface's updated limit. Ordinary surfaces
   gate on the contact band below distance; flag-0x1080 surfaces (special)
   instead test whether the segment crossed the plane, and lift the probe by the
   level-flag-8 camera offset. */
SurfaceContactResult W8GDSurface::segmentContact(const glm::vec3& from, const glm::vec3& direction,
                                                 std::span<const glm::vec3, 3> vertices,
                                                 bool lift_crossings) const
{
    unsigned int flags = this->flags;
    bool special = false;
    if ((flags & W8_GD_SURFACE_INACTIVE_MASK) != 0) {
        return {};
    }
    auto point = from;
    if ((flags & W8_GD_SURFACE_CROSSING_MASK) != 0) {
        special = true;
        if (lift_crossings) {
            point.y += kCollisionHeightLift;
        }
    }
    const auto unit_dir = normalizedOrZero(direction);
    const auto normal = plane.normal;
    const float segment_length = glm::length(direction);
    if (segment_length < 0.0001f) {
        if (!special) {
            return {std::nullopt, true};
        }
    } else if (!special && glm::dot(normal, unit_dir) >= 0.0f) {
        return {};
    }
    const auto end = point + direction;
    float dist_start = SignedPlaneDistance(plane, point);
    if (!special && dist_start < 0.0f) {
        return {};
    }
    float limit = contact_margin;
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0 && !special) {
        limit = contact_margin - (1.0f - fabsf(normal.y)) * kCollisionSlopeMarginScale;
    }
    float dist_end = SignedPlaneDistance(plane, end);
    if (special) {
        if (dist_end * dist_start > kCameraSnapEpsilon) {
            if (contact_margin < 1.0f || dist_end < 0.0f) {
                return {};
            }
            if (dist_end >= limit + 1e+01f && dist_start >= limit + 1e+01f) {
                return {};
            }
        }
    } else {
        if (limit + 1e+01f <= dist_end) {
            return {};
        }
        if (dist_start - dist_end < g_camera_transition_epsilon) {
            return {};
        }
    }
    int axis = flags & W8_GD_SURFACE_AXIS_MASK;
    const auto start_proj = point - normal * dist_start;
    const auto end_proj = end - normal * dist_end;
    bool crossed = false;
    for (unsigned edge = 0; edge < vertices.size() && !crossed; ++edge)
        crossed = SegmentCrossesEdge(start_proj, end_proj, vertices[edge],
                                     vertices[(edge + 1) % vertices.size()], axis);
    const bool inside =
        !crossed && triangleProjection::contains(vertices, axis, end_proj,
                                                 triangleProjection::Boundary::collision);
    float t_span = dist_start - dist_end;
    float fraction;
    if (t_span < 1.0f) {
        fraction = 1.0f;
    } else {
        fraction = (dist_start - limit) / t_span;
    }
    if (fraction < kCollisionFractionFloor && !special) {
        fraction = kCollisionFractionFloor;
    }
    if (fraction > 1.0f) {
        return {};
    }
    float hit;
    if (!crossed && !inside) {
        if (special) {
            return {};
        }
        glm::vec3 chosen;
        if (dist_start / t_span <= 1.0f) {
            chosen = point;
        } else {
            chosen = end;
        }
        const auto edge_limit = edgeContactLimit(chosen, vertices, limit);
        if (!edge_limit) return {};
        limit = *edge_limit;
        fraction = (dist_start - limit) / t_span;
        if (fraction >= 1.0f) {
            return {};
        }
        if (fraction < kCollisionFractionFloor) {
            fraction = kCollisionFractionFloor;
        }
        hit = fraction * segment_length;
        if (segment_length - hit < g_camera_transition_epsilon) {
            return {};
        }
        return {SurfaceContact{point + unit_dir * hit, hit, limit}, false};
    }
    hit = fraction * segment_length;
    if (!special) {
        return {SurfaceContact{point + unit_dir * hit, hit, limit}, false};
    }
    const float signed_distance = dist_start <= 0 ? -fabsf(hit) : fabsf(hit);
    return {SurfaceContact{from, fabsf(hit), signed_distance}, false};
}

/* 2D segment-vs-edge test in the plane perpendicular to `axis`: the projected
   motion segment seg_start→seg_end must overlap edge_a→edge_b on both free
   axes and their line-crossing parameters must both fall inside [0,1]. */
bool SegmentCrossesEdge(const glm::vec3& seg_start, const glm::vec3& seg_end,
                        const glm::vec3& edge_a, const glm::vec3& edge_b, unsigned int axis)
{
    const auto start = triangleProjection::coordinates(seg_start, axis);
    const auto end = triangleProjection::coordinates(seg_end, axis);
    const auto first = triangleProjection::coordinates(edge_a, axis);
    const auto second = triangleProjection::coordinates(edge_b, axis);
    if (glm::any(glm::greaterThan(glm::min(start, end), glm::max(first, second))) ||
        glm::any(glm::greaterThan(glm::min(first, second), glm::max(start, end))))
        return false;

    const auto segment = end - start;
    const auto edge = second - first;
    const auto offset = first - start;
    const float denominator = glm::determinant(glm::mat2(segment, edge));
    if (denominator == 0)
        return glm::determinant(glm::mat2(offset, segment)) == 0 &&
               glm::determinant(glm::mat2(offset, edge)) == 0;
    const glm::vec2 fractions(glm::determinant(glm::mat2(offset, edge)) / denominator,
                              glm::determinant(glm::mat2(offset, segment)) / denominator);
    return glm::all(glm::greaterThanEqual(fractions, glm::vec2(0))) &&
           glm::all(glm::lessThanEqual(fractions, glm::vec2(1)));
}

/* Nearest-edge distance fixup for a segment that hit this surface's plane but
   missed the projected triangle. `limit` (contact_margin) shrinks
   to the remaining in-plane travel before the nearest edge; fails when no
   edge improves it. */
std::optional<float> W8GDSurface::edgeContactLimit(const glm::vec3& point,
                                                   std::span<const glm::vec3, 3> vertices,
                                                   float limit) const
{
    float nearest_dist = 10000000.0f;
    short nearest_edge = 0;
    for (short edge = 0; edge < 3; ++edge) {
        const float dist =
            segmentMath::project(point, vertices[edge], vertices[(edge + 1) % 3]).distance;
        if (dist < nearest_dist) {
            nearest_edge = edge;
            nearest_dist = dist;
        }
    }
    if (nearest_dist > limit) {
        return std::nullopt;
    }
    float offset = -(SignedPlaneDistance(plane, point));
    const auto projected = point + plane.normal * offset;
    float edge_dist =
        segmentMath::project(projected, vertices[nearest_edge], vertices[(nearest_edge + 1) % 3])
            .distance;
    if ((flags & W8_GD_SURFACE_WALKABLE) != 0) {
        float threshold = limit * 0.5f;
        if (threshold < offset) {
            return std::nullopt;
        }
    }
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0) {
        if (plane.normal.y < kCollisionCeilingNormalThreshold) {
            edge_dist *= g_navigator_linked_radius_scale;
        }
    } else {
        float scaled = limit * kNavigatorMode3Scale;
        if (scaled < edge_dist) {
            edge_dist = (edge_dist - scaled) * 6.0f + scaled;
        }
    }
    if (edge_dist < limit) {
        return sqrtf(limit * limit - edge_dist * edge_dist);
    }
    return std::nullopt;
}

/* Collision response for a hit surface: `origin` advances to `hit_point` and
   `direction` is bent along the contact plane; returns whether motion
   continues. The crossed contact planes persist across calls in the statics
   so sequential bounces wedge the slide between them. */
bool W8GDSurface::ResolveCollision(PartyMotion& level, LevelEnvironment& environ, glm::vec3* origin,
                                   const glm::vec3* hit_point, glm::vec3* direction,
                                   int collision_index, CollisionResponse& response,
                                   const W8Plane* contact_override,
                                   std::span<const W8GDSurface> surfaces,
                                   std::span<const glm::vec3> vertices)
{
    bool recomputed = false;
    bool crossed = false;
    glm::vec3 crease_a(0);
    if ((flags & W8_GD_SURFACE_WALKABLE) != 0) {
        const World* world = GetWorld();
        if ((flags & W8_GD_SURFACE_EXPLICIT_SLOPE) != 0 && world != nullptr && world->script &&
            world->script->slopeOverrideActive() &&
            CurrentGame().inventory.has(kSlopeOverrideItem, W8ItemReach::Worn)) {
            environ.motion_factor = 1.0f;
        } else if (environ.motion_factor < slope) {
            environ.motion_factor = slope;
        }
        environ.airborne = 1;
        level.flags |= W8_LEVEL_FLAG_WALKABLE_CONTACT;
        level.sound_environment = footstep_surface;
        level.sound_environment_alt = footstep_material;
    }
    float direction_length = glm::length((*direction));
    if (direction_length == 0.0f) {
        return false;
    }
    /* Prop contacts borrow the movement step's plane; level contacts use their own plane. */
    const auto& contact_plane = contact_override ? *contact_override : plane;
    const auto& normal = contact_plane.normal;
    float adjusted_d = contact_plane.w - distance;
    if (level.primary_contact_prop_id > -1 &&
        glm::dot(normal, level.contact_normal) < kCollisionContactNormalThreshold) {
        level.flags |= W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH;
    }
    flags |= W8_GD_SURFACE_COLLISION_PROCESSED;
    const float depth = -(glm::dot((*origin + *direction), normal) + adjusted_d);
    distance = depth;
    if (depth < 0.0f) {
        return true;
    }
    glm::vec3 unit;
    if (collision_index < 1) {
        if (collision_index != 0) {
            unit = (*direction) / direction_length;
            response.entry_direction = unit;
        }
        response.state = 0;
    }

    const auto step = (*hit_point) - (*origin);
    *direction = (*direction) - step;
    *origin = *hit_point;
    if (glm::dot(normal, response.entry_direction) < kCollisionOpposedNormalThreshold) {
        (*direction) = glm::vec3(0);
        environ.vector = glm::vec3(0);
        return false;
    }
    unit = (*direction) / direction_length;
    glm::vec3 slide = (*direction) + normal * distance;
    ApplyEnvironContact(level, environ, slide);
    glm::vec3 slide_unit = slide;
    slide_unit = normalizedOrZero(slide_unit);
    float approach = glm::dot(slide_unit, response.entry_direction);
    if (fabsf(approach) < kCameraSnapEpsilon) {
        (*direction) = glm::vec3(0);
        return false;
    }
    if (approach >= 0.0001f) {
        if (response.state != 0) {
            glm::vec3 ortho = response.entry_direction;
            ortho = ProjectVectorOntoVector(ortho, normal);
            glm::vec3 deflect = response.entry_direction - ortho;
            deflect = normalizedOrZero(deflect);
            if (glm::dot(unit, deflect) < 0.0f && response.state == 1 &&
                CentroidsDiverging(response.first_surface, normal, response.first_normal, surfaces,
                                   vertices)) {
                crossed = true;
            }
        }
    } else if (response.state != 0) {
        crossed = true;
    }
    if (response.state != 0) {
        if (kCollisionParallelNormalThreshold < glm::dot(normal, response.first_normal)) {
            if (response.first_limit <= adjusted_d) {
                return true;
            }
            response.first_limit = adjusted_d;
            response.first_surface = index;
            *direction = slide;
            return true;
        }
        if (crossed) {
            crease_a = glm::cross(normal, response.first_normal);
            if (glm::dot(crease_a, unit) < 0.0f) {
                crease_a = -crease_a;
            }
        } else {
            recomputed = false;
            if (glm::dot(slide_unit, response.first_normal) > 0.0001f) {
                response.first_normal = normal;
                response.first_surface = index;
                recomputed = true;
                response.first_limit = adjusted_d;
            }
        }
        if (response.state == 2) {
            if (kCollisionParallelNormalThreshold < glm::dot(normal, response.second_normal)) {
                if (response.second_limit <= adjusted_d) {
                    return true;
                }
                response.second_limit = adjusted_d;

                *direction = slide;
                return true;
            }
            if (glm::dot((response.second_normal + response.first_normal), normal) <
                kCameraSnapEpsilon) {
                (*direction) = glm::vec3(0);
                return false;
            }
            if (crossed) {
                glm::vec3 crease_b = glm::cross(normal, response.second_normal);
                if (glm::dot(crease_b, unit) < g_camera_transition_epsilon) {
                    crease_b = -crease_b;
                }
                float ahead_a = glm::dot(crease_a, response.entry_direction);
                float ahead_b = glm::dot(crease_b, response.entry_direction);
                if (ahead_a < kCameraSnapEpsilon && ahead_b < kCameraSnapEpsilon) {
                    (*direction) = glm::vec3(0);
                    return false;
                }
                if (ahead_b <= ahead_a) {
                    slide = ProjectVectorOntoVector(slide, crease_b);
                    response.first_normal = normal;
                    response.first_limit = adjusted_d;
                    response.first_surface = index;
                } else {
                    slide = ProjectVectorOntoVector(slide, crease_a);
                    response.second_normal = normal;

                    response.second_limit = adjusted_d;
                }
            } else if (glm::dot(slide_unit, response.second_normal) >= 0.0f) {
                if (recomputed) {
                    response.state = 1;
                } else {
                    response.second_normal = normal;

                    response.second_limit = adjusted_d;
                }
            }
        } else if (crossed) {
            if (glm::dot(crease_a, response.entry_direction) < kCameraSnapEpsilon) {
                (*direction) = glm::vec3(0);
                return false;
            }
            slide = ProjectVectorOntoVector(slide, crease_a);
            response.second_normal = normal;
            ++response.state;

            response.second_limit = adjusted_d;
        }
    } else {
        response.first_normal = normal;
        response.first_surface = index;
        response.state = 1;
        response.first_limit = adjusted_d;
    }
    *direction = slide;
    if (glm::length((*direction)) >= kCollisionDirectionEpsilon) {
        return true;
    }
    return false;
}

/* Whether `surface_index`'s triangle centroid sits farther from this surface's
   centroid than the from→to normal offset: used to tell genuinely different
   contact planes apart when wedging a slide. */
bool W8GDSurface::CentroidsDiverging(int surface_index, const glm::vec3& from, const glm::vec3& to,
                                     std::span<const W8GDSurface> surfaces,
                                     std::span<const glm::vec3> vertices_bank) const
{
    if (surface_index < 0 || static_cast<std::size_t>(surface_index) >= surfaces.size())
        return false;
    const auto* other = &surfaces[surface_index];
    const auto* vertices = vertices_bank.data();
    glm::vec3 this_centroid(0.0f, 0.0f, 0.0f);
    glm::vec3 other_centroid(0.0f, 0.0f, 0.0f);
    for (int vertex = 0; vertex < 3; ++vertex) {
        const glm::vec3* this_vertex = vertices + vertex_indices[vertex];
        this_centroid += *this_vertex;
        const glm::vec3* other_vertex = vertices + other->vertex_indices[vertex];
        other_centroid += *other_vertex;
    }
    const glm::vec3 delta =
        (this_centroid - other_centroid) * static_cast<float>(kCollisionCentroidScale);
    const glm::vec3 adjusted = delta + from - to;
    return glm::dot(delta, delta) >= glm::dot(adjusted, adjusted);
}

/* Environment contact response for a flag-4 (walkable) surface hit: pushes
   the slide back out of the plane by `depth`, removes the into-plane residual
   scaled by value_20 and the vertical attenuation, and reports the combined
   motion to the environ record. Direction tests against the surface normal
   decide whether any correction applies. */
bool W8GDSurface::ApplyEnvironContact(PartyMotion& level, LevelEnvironment& environ,
                                      glm::vec3& direction)
{
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0) {
        level.motion_input = glm::vec3(0);
        level.integrated_motion = glm::vec3(0);
        return false;
    }
    level.flags |= W8_LEVEL_FLAG_WALKABLE_CONTACT;
    level.sound_environment = footstep_surface;
    level.sound_environment_alt = footstep_material;
    glm::vec3 normal;
    normal = plane.normal;
    float factor = environ.motion_factor;
    glm::vec3 slide = environ.vector * environ.scale;
    glm::vec3 unit = slide;
    unit = normalizedOrZero(unit);
    if (glm::dot(unit, normal) >= kCollisionOpposedNormalThreshold) {
        glm::vec3 proj = slide;
        proj = ProjectVectorOntoVector(proj, normal);
        if (glm::dot(proj, normal) <= 0.0f) {
            float depth = glm::length(proj);
            if (distance < depth) {
                depth = distance;
            }
            float attenuation = 0.25f;
            if (slide.y < 0.0f) {
                attenuation =
                    fabsf(slide.y) / glm::length(slide) * 0.75f + g_navigator_vertical_phase_step;
            }
            glm::vec3 residual = slide - proj;
            residual *= factor * attenuation;
            slide += normal * depth;
            slide -= residual;
            direction -= residual * factor;
            environ.SetScaledMotion(slide);
            return true;
        }
        return false;
    }
    slide = glm::vec3(0);
    environ.vector = slide / environ.scale;
    environ.airborne = 1;
    return false;
}

/* VC6 vector constructor iterator, emitted for an ordinary array construction.
   This is compiler support, not an authored Wizardry callback wrapper. */

void ResetLevelMovement(float movement_limit, bool reset, bool fast_move)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        level->movement_limit = movement_limit;
        level->real_elapsed = 0.0f;
        level->frame_elapsed = 0.0f;
        level->movement_progress = 0.0f;
        level->camera_motion_velocity = glm::vec3(0);
        level->camera_motion_displacement = glm::vec3(0);
        level->flags |= W8_LEVEL_FLAG_MOVEMENT_ACTIVE;
        if (reset) {
            level->flags |= W8_LEVEL_FLAG_MOVEMENT_RESET;
        } else {
            level->flags &= ~W8_LEVEL_FLAG_MOVEMENT_RESET;
        }
        if (fast_move) {
            level->flags |= W8_LEVEL_FLAG_FAST_MOVEMENT;
        } else {
            level->flags &= ~W8_LEVEL_FLAG_FAST_MOVEMENT;
        }
    }
}

void ResetInactiveLevelDataVectors(void)
{
    PartyMotion* level = LevelMotion();

    if (level != 0 && (level->flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0) {
        level->motion_input = glm::vec3(0);
        level->camera_motion_velocity = glm::vec3(0);
        level->camera_motion_displacement = glm::vec3(0);
        level->motion_velocity = glm::vec3(0);
        level->integrated_motion = glm::vec3(0);
        level->motion_displacement = glm::vec3(0);
    }
}

/* Fast movement is shared by camera motion, stamina and combat movement. */
bool IsLevelFastMovement(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        return (level->flags & W8_LEVEL_FLAG_FAST_MOVEMENT) != 0;
    }
    return false;
}

void ClearLevelFastMovement(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        level->flags &= ~W8_LEVEL_FLAG_FAST_MOVEMENT;
    }
}

void SetLevelFastMovement(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        level->flags |= W8_LEVEL_FLAG_FAST_MOVEMENT;
    }
}

bool LevelMovedThisUpdate(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        return (level->flags & W8_LEVEL_FLAG_MOVED_THIS_UPDATE) != 0;
    }
    return false;
}

/* Whether the last motion update found walkable-surface contact. */
bool HasLevelWalkableContact(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        return (level->flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) != 0;
    }
    return false;
}

/* Clear active, stopped and reset movement state together. */
void ClearLevelMovementState(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        level->flags &= ~W8_LEVEL_MOVEMENT_STATE_MASK;
    }
}

bool IsLevelMovementStopped(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        return (level->flags & W8_LEVEL_FLAG_MOVEMENT_STOPPED) != 0;
    }
    return false;
}

void ClearLevelMovementStopped(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        level->flags &= ~W8_LEVEL_FLAG_MOVEMENT_STOPPED;
    }
}

/* Hand the level's pending real/frame elapsed times to the caller, fold them
   into the session accumulators, clear the pending pair, and report whether
   either was above the camera-transition epsilon. */
unsigned char ConsumeLevelElapsedTime(float* real_elapsed, float* frame_elapsed)
{
    PartyMotion* level = LevelMotion();
    bool elapsed = false;
    if (level != 0) {
        *real_elapsed = level->real_elapsed;
        *frame_elapsed = level->frame_elapsed;
        elapsed = level->real_elapsed > g_camera_transition_epsilon ||
                  level->frame_elapsed > g_camera_transition_epsilon;
        level->frame_elapsed = 0.0f;
        level->real_elapsed = 0.0f;
        CurrentGame().status.real_elapsed += *real_elapsed;
        CurrentGame().status.frame_elapsed += *frame_elapsed;
    }
    return elapsed;
}

/* Options and save/load may interrupt motion while grounded or before
   the falling-state override has latched. Without a loaded level, it is refused. */
bool CanInterruptLevelMovement(void)
{
    PartyMotion* level = LevelMotion();
    if (level == 0) {
        return false;
    }
    if ((level->flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) == 0 && level->movement_override) {
        return false;
    }
    return true;
}

/* Whether the level has a live vector at 0x88: bit zero has to be up and at
   least one of the three floats has to differ from the default. */
bool HasLevelDataVector(void)
{
    PartyMotion* level = LevelMotion();
    if (level == 0) {
        return false;
    }
    if ((level->flags & W8_LEVEL_FLAG_PROP_CONTACT) != 0 &&
        (level->contact_motion.x != 0.0f || level->contact_motion.y != 0.0f ||
         level->contact_motion.z != 0.0f)) {
        return true;
    }
    return false;
}

PartyMotion* LevelMotion()
{
    auto* data = GetWorldCollision();
    return data ? &GetWorld()->party.camera.motion : nullptr;
}

LevelEnvironment* LevelEnviron()
{
    auto* data = GetWorldCollision();
    return data ? &GetWorld()->party.camera.environment.value : nullptr;
}

/* The camera-sway mode halves navigator gravity, mirrors it into the active
   environment record and swaps the camera forward scale; the flag guards both
   transitions so repeated triggers are idempotent. */
void BeginCameraSway(void)
{
    LevelEnvironment* environ = LevelEnviron();
    if (g_camera_sway_active) {
        return;
    }
    g_camera_forward_scale = g_camera_level_forward_scale;
    g_navigator_gravity = 93.75f;
    if (environ != 0) {
        environ->gravity_y = -93.75f;
    }
    g_camera_sway_active = true;
}

void EndCameraSway(void)
{
    LevelEnvironment* environ = LevelEnviron();
    if (!g_camera_sway_active) {
        return;
    }
    g_camera_forward_scale = g_camera_default_forward_scale;
    g_navigator_gravity = 187.5f;
    if (environ != 0) {
        environ->gravity_y = -187.5f;
    }
    g_camera_sway_active = false;
}

void ResetCurrentEnvironment(void)
{
    auto* data = GetWorldCollision();
    if (data != 0) {
        auto& active = GetWorld()->party.camera.environment;
        selectEnvironment(active, data->environments.front(), 0);
        active.value.vector = glm::vec3(0);
        if (g_environment_load_flag != 0) {
            active.value.motion_factor = 1.0f;
        }
        g_environment_load_flag = g_environment_load_flag == 0;
        if (g_environment_load_flag == 0) {
            partyCamera().motion.movement_override = false;
        }
        return;
    }
    g_environment_load_flag = 0;
    partyCamera().motion.movement_override = false;
}

unsigned char SetEnvironmentLoadFlag(unsigned char flag)
{
    LevelEnvironment* environ = LevelEnviron();
    glm::vec3 zero_vector(0.0f, 0.0f, 0.0f);
    unsigned char previous = g_environment_load_flag;
    if (environ != 0) {
        environ->vector = zero_vector;
        if (flag == 0) {
            environ->motion_factor = 1.0f;
        }
        g_environment_load_flag = flag;
    }
    return previous;
}

void ResetLevelDataVectors(void)
{
    PartyMotion* level = LevelMotion();
    if (level != 0) {
        level->flags |= W8_LEVEL_FLAG_MOVEMENT_STOPPED;
        if ((level->flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0) {
            level->motion_input = glm::vec3(0);
            level->camera_motion_velocity = glm::vec3(0);
            level->motion_velocity = glm::vec3(0);
            level->integrated_motion = glm::vec3(0);
            level->camera_motion_displacement = glm::vec3(0);
            level->motion_displacement = glm::vec3(0);
        }
        level->flags &= ~W8_LEVEL_FLAG_FAST_MOVEMENT;
    }
}

/* Camera facade, move timer and the party placement entry. */

const double g_game_data_pi = 3.141592653589793;

// FUNCTION: WIZ8 0x00420b40
float MoveTimer(W8MoveTimerAction action)
{
    if (!GameInProgress()) return 0.0f;
    auto& game = CurrentGame();
    if (game.move_timer_suspended) {
        if ((action == W8_MOVE_TIMER_RESUME_MAIN_GAME && game.activity.in_world() &&
             !game.advancement.hold.covered) ||
            action == W8_MOVE_TIMER_RESUME) {
            game.timerClocks().resume();
            game.move_timer_suspended = false;
        } else {
            return 0.0f;
        }
    }
    if (action == W8_MOVE_TIMER_SUSPEND) {
        game.timerClocks().pause();
        game.move_timer_suspended = true;
    }
    return game.motion_clock.GetFrameDelta();
}

float GetCameraYawRadians()
{
    return partyCamera().m_yaw;
}

void BeginManualCameraControl()
{
    partyCamera().SetManualControlActive(true);
}

/* 0x00420F40: camera yaw in whole degrees, plus an optional copy of the
   yaw-rotation matrix. The diagnostics dump passes null and uses only the
   yaw. */
int GetCameraYawAndRotation(glm::mat3* rotation)
{
    float degrees = partyCamera().m_yaw * kCameraRadiansToDegrees;
    if (rotation != 0) {
        *rotation = partyCamera().m_yaw_rotation;
    }
    return static_cast<int>(degrees);
}

void LevelCamera()
{
    partyCamera().BeginLeveling();
    partyCamera().manual_mouse_input = false;
}

void CameraLookAt(const glm::vec3* position)
{
    partyCamera().LookAt(position, false);
}

void CameraSnapToTarget(const glm::vec3* target)
{
    partyCamera().SnapToTarget(target);
}

void TurnCameraToDegrees(float degrees)
{
    double scale = g_game_data_pi * 0.0055555557f;
    partyCamera().BeginOrientationTransition(0.0f, static_cast<float>(scale * degrees), false);
}

void SetCameraYawDegrees(float degrees)
{
    double scale = g_game_data_pi * 0.0055555557f;
    partyCamera().SetOrientationImmediate(0.0f, static_cast<float>(scale * degrees));
}

void ApplyCameraRotation(glm::mat3* rotation)
{
    partyCamera().ApplyRotationMatrix(rotation, LevelMotion());
}

glm::vec3 cameraLocation()
{
    return partyCamera().m_position;
}

CameraOrientation cameraOrientation()
{
    return {partyCamera().m_yaw, partyCamera().m_pitch};
}

CameraOrientation setCameraOrientation(CameraOrientation orientation)
{
    partyCamera().SetYaw(orientation.yaw);
    partyCamera().SetPitch(orientation.pitch);
    return cameraOrientation();
}

/* Project onto a direction; leave the vector unchanged for a degenerate target. */
glm::vec3 ProjectVectorOntoVector(const glm::vec3& vector, const glm::vec3& onto)
{
    const float length_squared = glm::dot(onto, onto);
    if (!(length_squared > kVectorLengthSquaredEpsilon)) return vector;
    return glm::proj(vector, onto);
}

int GetCameraYawDegrees(void)
{
    return static_cast<int>(glm::degrees(partyCamera().m_yaw));
}

/* Point-visibility query: returns whether the camera has line of sight to the
   given position through the world octree. Fails with no world loaded; with a
   world but no octree there is nothing to occlude, so it returns true. */
bool hasCameraLineOfSight(const glm::vec3& position)
{
    if (GetWorld() == 0) {
        return false;
    }
    if (auto* spatial = GetWorld()->compiledIndex()) {
        return spatial->hasLineOfSight(cameraLocation(), position);
    }
    return true;
}
