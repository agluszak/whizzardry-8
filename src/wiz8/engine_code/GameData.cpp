#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "compat/compiler.h"
#include "glm/common.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/geometric.hpp"
#include "glm/gtx/projection.hpp"
#include "glm/matrix.hpp"
#include "glm/trigonometric.hpp"
#include "glm/vector_relational.hpp"
#include "surrender/srMath.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/OctBuildTree.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/packed_bits.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/local_code/CombatPartyMovement.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/Noise.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/xstatus.h"
#include "soundman.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/startup_world.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/float_constants.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "surrender/srShader.h"
#include "wiz8/sr_api.h"
#include "random.h"
#include "wiz8/engine_code/3d.h"
#include "application.h"
#include "camera_motion.h"
#include "compat/ptr32.h"
#include "geometry.h"
#include "glm/ext/matrix_float2x2.hpp"
#include "glm/ext/vector_float2.hpp"
#include "layouts/world.h"
#include "local_code/Combat.h"
#include "surrender/srMeshModel.h"
#include "audio_resources.h"

class srMaterialIFace;
class srTextureIFace;

// GLOBAL: WIZ8 0x00652da8
unsigned int* g_level_flags;
// GLOBAL: WIZ8 0x00652dac
W8LevelDataRecord* g_level_data;
// GLOBAL: WIZ8 0x00652da7
bool g_mouselook_manual;

// FUNCTION: WIZ8 0x00420bd0
float SettlePositionToGround(const srVector3T<float>* position, bool* hit)
{
    srVector3T<float> candidate = *position;
    if (g_octree_game_data != 0 && g_octree_game_data->octree != 0) {
        return g_octree_game_data->octree->SettleToGround(&candidate, hit, 1, 500.0f);
    }
    float height = position->y;
    if (hit != 0) {
        *hit = false;
    }
    return height;
}

// FUNCTION: WIZ8 0x00420C30
float SettlePositionToGroundMutable(srVector3T<float>* position, bool* hit)
{
    return SettlePositionToGround(position, hit);
}

/* The ground height under `position` plus the footstep surface/material of
   the surface the last trace selected; both outputs stay zero when no
   surface was recorded. */
// FUNCTION: WIZ8 0x00420CA0
float GetGroundSurfaceInfo(const srVector3T<float>* position, char* surface, char* material)
{
    const float height = SettlePositionToGround(position, nullptr);
    *surface = *material = 0;
    if (g_octree_game_data && g_octree_game_data->last_hit_surface != 0) {
        const auto& hit = g_octree_game_data->m_pSurfaces[g_octree_game_data->last_hit_surface];
        *surface = hit.footstep_surface;
        *material = hit.footstep_material;
    }
    return height;
}

// GLOBAL: WIZ8 0x00652dba
static bool g_level_override;
// GLOBAL: WIZ8 0x00652dce
bool g_shared_timers_paused;

/* Resolve one surface's three vertex indices through the active processed
   GameData vertex table.  The retail comparison is signed and accepts an index
   equal to vertex_count, so that historical boundary behavior is preserved. */
// FUNCTION: WIZ8 0x004214d0
unsigned char LoadSurfaceVertices(srVector3T<float>* output, const int* vertex_indices)
{
    short index;
    for (index = 0; index < 3; ++index) {
        if (g_octree_game_data->VerticesCount() < vertex_indices[index]) {
            return 0;
        }
        output[index] = g_octree_game_data->m_pVertices[vertex_indices[index]];
    }
    return 1;
}

/* Ensure the shared game-data object exists, then run its update. */
// FUNCTION: WIZ8 0x0041F1F0
void UpdateSharedGameDataObject()
{
    if (g_game_time_accumulator == 0) {
        g_game_time_accumulator = new W8GameTimeAccumulator;
        if (g_game_time_accumulator == 0) {
            return;
        }
    }
    g_game_time_accumulator->Update();
}

// FUNCTION: WIZ8 0x0041F260
void UpdateGameDataRuntime()
{
    if (g_gd_camera == 0) {
        g_gd_camera = new GDCamera;
        if (g_gd_camera == 0) {
            srAssertFail("gpGDCamera", "C:\\Projects\\Wizardry 8\\Engine Code\\GameData.cpp", 2739,
                         0);
        }
    }
    if (g_game_time_accumulator == 0) {
        g_game_time_accumulator = new W8GameTimeAccumulator;
        if (g_game_time_accumulator == 0) {
            g_game_time_accumulator->Update();
            return;
        }
    }
    if (g_shared_timers_paused) {
        ResumeSharedGameTimers();
        g_shared_timers_paused = false;
    }
    g_game_time_accumulator->Update();
}

/* Apply world-render camera-motion flags into `rotation` and mirror the yaw
   rotation into `saved`. ECX is the owning W8GameData; the body reads only
   globals. */
// FUNCTION: WIZ8 0x0041F330
void W8GameData::ApplyCameraMotionFlags(unsigned int flags, glm::mat3* rotation, glm::mat3* saved)
{
    float pitch_input;
    float yaw_input;
    W8GameTimer::Flags timer_flags;
    unsigned int level_flags;
    W8LevelDataRecord* level;
    W8EnvironRecord* environ_record;

    if (g_environ == 0) {
        environ_record = new W8EnvironRecord;

        g_environ = environ_record;
        if (g_environ == 0) {
            ShutdownWithErrorBox("TrackRotation: Could not allocate gpEnviron.\n");
        }
    }

    timer_flags = g_game_time_accumulator->m_flags;
    if (timer_flags.paused) {
        return;
    }
    if (g_shared_timer_paused && !timer_flags.raw_time) {
        return;
    }
    if (g_shared_timer_flag0) {
        return;
    }
    if (timer_flags.slow_scale) {
        return;
    }

    if ((g_gd_camera->m_orientation_flags & W8_CAMERA_ORIENTATION_SNAPPED) != 0) {
        g_gd_camera->m_orientation_flags &= ~W8_CAMERA_ORIENTATION_SNAPPED;
        MarkRendererReady();
    }

    if (g_level_data != 0) {
        g_level_data->flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
        level = g_level_data;
        if (!AnyCharacterEngaged() || ((level_flags = level->flags) & 0xc0) != 0) {
            level_flags = level->flags;
            flags &= W8_CAMERA_MOTION_HIGH_BYTE_MASK;
            if ((level_flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE) == 0) {
                level->flags = level_flags & ~W8_LEVEL_FLAG_FAST_MOVEMENT;
            }
        } else if ((level_flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE) != 0) {
            if ((level_flags & W8_LEVEL_FLAG_FAST_MOVEMENT) != 0) {
                flags |= W8_CAMERA_MOTION_FAST;
            } else {
                flags &= ~W8_CAMERA_MOTION_FAST;
            }
        } else if ((flags & W8_CAMERA_MOTION_FAST) != 0) {
            level->flags = level_flags | W8_LEVEL_FLAG_FAST_MOVEMENT;
        } else {
            level->flags = level_flags & ~W8_LEVEL_FLAG_FAST_MOVEMENT;
        }
    }

    pitch_input = 0.0f;
    yaw_input = 0.0f;
    if ((flags & W8_CAMERA_MOTION_LOOK_DOWN) != 0) {
        pitch_input = 0.1745329350233078f;
    } else if ((flags & W8_CAMERA_MOTION_LOOK_UP) != 0) {
        pitch_input = -0.1745329350233078f;
    }
    if ((flags & W8_CAMERA_MOTION_TURN_LEFT) != 0) {
        yaw_input = -0.1745329350233078f;
    } else if ((flags & W8_CAMERA_MOTION_TURN_RIGHT) != 0) {
        yaw_input = 0.1745329350233078f;
    }
    if ((flags & W8_CAMERA_MOTION_SLOW_TURN) != 0) {
        g_camera_max_yaw_velocity = 0.116355285f;
        yaw_input *= g_float_one_tenth;
    } else {
        g_camera_max_yaw_velocity = 0.3490658700466156f;
    }

    if ((pitch_input != g_float_zero || yaw_input != g_float_zero) &&
        (g_level_data->flags & W8_LEVEL_FLAG_MOVEMENT_STOPPED) == 0) {
        g_navigator_position_changed = true;
    }
    if (g_mouselook_manual) {
        BeginManualCameraControl();
    }
    g_gd_camera->ApplyPitchInput(pitch_input);
    g_gd_camera->ApplyYawInput(yaw_input);
    g_gd_camera->Update(g_game_time_accumulator->GetFrameDelta());
    if ((flags & W8_CAMERA_MOTION_KEEP_ROTATION) == 0) {
        g_gd_camera->GetRotationMatrix(rotation);
    }
    *saved = g_gd_camera->m_yaw_rotation;
}

// GLOBAL: WIZ8 0x00652dcd
unsigned char g_level_motion_fast;
// GLOBAL: WIZ8 0x00652db9
bool g_level_footstep_pending;
// GLOBAL: WIZ8 0x00603ac0
float g_camera_motion_clamp = 1125.0f;
// GLOBAL: WIZ8 0x00603ac4
float g_camera_motion_divisor = 3000.0f;
// GLOBAL: WIZ8 0x00603ad4
SoundPlayback g_level_footstep_sound = nullptr;
// GLOBAL: WIZ8 0x00652dd0
float g_level_footstep_time;
// GLOBAL: WIZ8 0x005ebc50
const double g_motion_delta_epsilon = 0.10000000149011612;
// GLOBAL: WIZ8 0x005ebcd4
const float g_footstep_fall_threshold = -250.0f;
// GLOBAL: WIZ8 0x00652940
srVector3T<float> g_world_origin;

/* Advance the camera under world-render motion flags. ECX is the owning
   W8GameData; the body mostly reads globals. Zero elapsed (`camera_scale`)
   returns before the motion helpers. */
// FUNCTION: WIZ8 0x0041F5F0
unsigned char W8GameData::ApplyCameraMotion(unsigned int flags, srVector3T<float>* position,
                                            srVector3T<float>* delta, glm::mat3* saved)
{
    W8GameTimer::Flags timer_flags;
    unsigned int level_flags;
    W8LevelDataRecord* level;
    float forward_scale;
    float component;
    bool fast_move;
    char moved;
    srVector3T<float> new_position;
    srVector3T<float> from_origin;

    if (g_level_data == 0) {
        level = new W8LevelDataRecord;
        g_level_flags = &level->flags;
        g_level_data = level;
        if (g_level_flags == 0) {
            ShutdownWithErrorBox("TrackMovement: Could not allocate gpMovement.\n");
        }
        PauseSharedGameTimers();
        g_level_motion_resume_pending = true;
        g_level_motion_fast = 0;
    }

    timer_flags = g_game_time_accumulator->m_flags;
    if (timer_flags.paused || (g_shared_timer_paused && !timer_flags.raw_time) ||
        g_shared_timer_flag0) {
        if (!g_level_motion_resume_pending) {
            return 0;
        }
        if (timer_flags.raw_time) {
            return 0;
        }
        g_level_motion_resume_pending = false;
        if (!g_shared_timer_flag0) {
            ResumeSharedGameTimers();
        }
    }

    level = g_level_data;
    level_flags = level->flags;
    if ((((level_flags & W8_LEVEL_FLAG_MOVEMENT_STOPPED) != 0 &&
          (level_flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) != 0) &&
         ((level_flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0 && !g_animated_prop_present)) ||
        g_game_time_accumulator->m_flags.slow_scale) {
        return 0;
    }

    if (!AnyCharacterEngaged() || ((level_flags = level->flags) & 0xc0) != 0) {
        level_flags = level->flags;
        flags &= W8_CAMERA_MOTION_HIGH_BYTE_MASK;
        if ((level_flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE) == 0) {
            level_flags &= ~W8_LEVEL_FLAG_FAST_MOVEMENT;
            level->flags = level_flags;
        }
    } else if ((level_flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE) == 0) {
        if ((flags & W8_CAMERA_MOTION_FAST) == 0) {
            level->flags = level_flags & ~W8_LEVEL_FLAG_FAST_MOVEMENT;
        } else {
            level->flags = level_flags | W8_LEVEL_FLAG_FAST_MOVEMENT;
        }
    } else if ((level_flags & W8_LEVEL_FLAG_FAST_MOVEMENT) == 0) {
        flags &= ~W8_CAMERA_MOTION_FAST;
    } else {
        flags |= W8_CAMERA_MOTION_FAST;
    }

    level = g_level_data;
    forward_scale = g_camera_level_forward_scale;
    level->flags &= ~(W8_LEVEL_FLAG_PROP_CONTACT | W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH |
                      W8_LEVEL_FLAG_WALKABLE_CONTACT);
    level->camera_scale = g_game_time_accumulator->GetFrameDelta();
    level->camera_position = *position;
    level->motion_velocity.SetZero();
    level->motion_displacement.SetZero();
    level->motion_input.SetZero();
    level->sound_environment = -1;
    level->sound_environment_alt = -1;

    if (g_level_data->camera_scale == g_float_zero) {
        return 0;
    }

    if (flags == 0) {
        g_level_data->motion_input.SetZero();
        level->integrated_motion.SetZero();
    }

    delta->SetZero();
    level = g_level_data;
    if ((flags & W8_CAMERA_MOTION_FORWARD) != 0) {
        component = forward_scale + g_level_data->motion_input.z;
        g_level_data->motion_input.z = component;
        if (component > g_camera_motion_clamp) {
            level->motion_input.z = g_camera_motion_clamp;
        } else if (component < -g_camera_motion_clamp) {
            level->motion_input.z = -g_camera_motion_clamp;
        }
    }
    level = g_level_data;
    if ((flags & W8_CAMERA_MOTION_BACKWARD) != 0) {
        component = g_level_data->motion_input.z - forward_scale;
        g_level_data->motion_input.z = component;
        if (component > g_camera_motion_clamp) {
            level->motion_input.z = g_camera_motion_clamp;
        } else if (component < -g_camera_motion_clamp) {
            level->motion_input.z = -g_camera_motion_clamp;
        }
    }
    level = g_level_data;
    if ((flags & W8_CAMERA_MOTION_STRAFE_LEFT) != 0) {
        component = g_level_data->motion_input.x - forward_scale;
        g_level_data->motion_input.x = component;
        if (component > g_camera_motion_clamp) {
            level->motion_input.x = g_camera_motion_clamp;
        } else if (component < -g_camera_motion_clamp) {
            level->motion_input.x = -g_camera_motion_clamp;
        }
    }
    level = g_level_data;
    if ((flags & W8_CAMERA_MOTION_STRAFE_RIGHT) != 0) {
        component = forward_scale + g_level_data->motion_input.x;
        g_level_data->motion_input.x = component;
        if (component > g_camera_motion_clamp) {
            level->motion_input.x = g_camera_motion_clamp;
        } else if (component < -g_camera_motion_clamp) {
            level->motion_input.x = -g_camera_motion_clamp;
        }
    }
    level = g_level_data;
    if ((flags & W8_CAMERA_MOTION_UP) != 0 && (g_environment_load_flag == 0 || g_environ == 0)) {
        component = forward_scale + g_level_data->motion_input.y;
        g_level_data->motion_input.y = component;
        if (component > g_camera_motion_clamp) {
            level->motion_input.y = g_camera_motion_clamp;
        } else if (component < -g_camera_motion_clamp) {
            level->motion_input.y = -g_camera_motion_clamp;
        }
    }
    level = g_level_data;
    if ((flags & W8_CAMERA_MOTION_DOWN) != 0 && g_environment_load_flag == 0) {
        component = g_level_data->motion_input.y - forward_scale;
        g_level_data->motion_input.y = component;
        if (component > g_camera_motion_clamp) {
            level->motion_input.y = g_camera_motion_clamp;
        } else if (component < -g_camera_motion_clamp) {
            level->motion_input.y = -g_camera_motion_clamp;
        }
    }

    fast_move = false;
    if ((flags & W8_CAMERA_MOTION_FAST) != 0) {
        fast_move = true;
    }
    if (g_level_data->motion_input.Length() > g_camera_motion_clamp) {
        g_level_data->motion_input.SetLength(g_camera_motion_clamp);
    }
    g_level_motion_fast =
        g_level_data->ApplySavedMotionMatrix(g_level_motion_fast, fast_move, saved);

    if (g_environment_load_flag == 0) {
        *delta = g_level_data->motion_displacement;
        moved = static_cast<float>(g_motion_delta_epsilon) < delta->Length();
        g_level_data->UpdateMotionProgress(g_level_motion_fast, moved);
        if (moved == 0) {
            goto after_move;
        }
    } else {
        moved = AdvanceEnvironmentMotion();
        g_level_data->UpdateMotionProgress(g_level_motion_fast, moved);
        *delta = g_level_data->motion_displacement;
        if (delta->Length() <= static_cast<float>(g_motion_delta_epsilon)) {
            if (moved == 0) {
                goto after_move;
            }
        } else {
            moved = 1;
        }
    }

    new_position = *position + *delta;
    from_origin = new_position - g_world_origin;
    if (from_origin.LengthSquared() != 0) {
        MarkRendererReady();
        g_gd_camera->m_position = new_position.glmValue();
    }

after_move:
    if (g_environ->airborne == 0) {
        g_level_footstep_pending = true;
    } else if (g_level_footstep_pending) {
        if (g_level_data->motion_velocity.y <= g_footstep_fall_threshold) {
            PlayFootstep(g_level_data->sound_environment, g_level_data->sound_environment_alt,
                         W8_FOOTSTEP_KIND_JUMP);
            g_level_data->footstep_accumulator = 0;
        }
        g_level_footstep_pending = false;
    }
    UpdateLevelMovementAudio();
    if (moved == 0) {
        g_level_data->flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
    } else {
        g_level_data->flags |= W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
    }
    return moved;
}

// GLOBAL: WIZ8 0x005ebc48
const double g_motion_vector_epsilon = 5.0;
// GLOBAL: WIZ8 0x00603ad1
unsigned char g_environment_motion_active = 1;
// GLOBAL: WIZ8 0x00652db8
bool g_environ_ground_latch;

// FUNCTION: WIZ8 0x00421800
void W8EnvironRecord::SetScaledMotion(const glm::vec3& motion)
{
    vector = motion / scale;
    /* 0x00421843 raises the airborne flag with the vector, so the record counts
       as airborne from the frame the scaled motion is stored. */
    airborne = 1;
}

// FUNCTION: WIZ8 0x00421850
void W8EnvironRecord::AddScaledMotion(srVector3T<float>* position)
{
    position->x = vector.x * scale + position->x;
    position->y = vector.y * scale + position->y;
    position->z = vector.z * scale + position->z;
}

// FUNCTION: WIZ8 0x0041FE20
unsigned char W8LevelDataRecord::ClampCameraToBounds(const srVector3T<float>* minimum,
                                                     const srVector3T<float>* maximum)
{
    bool clamped = false;
    bool below_min_y = false;

    if (camera_position.y < minimum->y) {
        if (g_status.world_suspended) {
            motion_displacement.y = maximum->y - minimum->y;
        }
        clamped = true;
        below_min_y = true;
    }
    if (camera_position.x < minimum->x) {
        clamped = true;
        motion_displacement.x = maximum->x - minimum->x;
    }
    if (camera_position.z < minimum->z) {
        clamped = true;
        motion_displacement.z = maximum->z - minimum->z;
    }
    if (maximum->x < camera_position.x) {
        clamped = true;
        motion_displacement.x = minimum->x - maximum->x;
    }
    if (camera_position.z <= maximum->z) {
        if (!clamped) {
            return 0;
        }
    } else {
        clamped = true;
        motion_displacement.z = minimum->z - maximum->z;
    }

    g_environ->vector.SetZero();
    if (below_min_y) {
        if (g_status.world_suspended) {
            g_level_override = false;
            return clamped;
        }
        BeginPartyMovement();
    }
    return clamped;
}

// GLOBAL: WIZ8 0x005ebc5c
const float g_monster_motion_push = 1.05f;

/* Probe active collidable props along the motion segment. On a hit, rewrites
   the caller's position into world space, may nudge `direction`, and latches
   plane / level-data contact fields used by the collision response pass. */
// FUNCTION: WIZ8 0x0041B770
W8GDSurface* W8GameData::ProbePropsAlongMotion(srVector3T<float>* direction,
                                               srVector3T<float>* position,
                                               srVector3T<float>* scratch, float* nearest_distance)
{
    W8GDSurface* nearest_surface;
    W8GDSurface* surface;
    W8Prop* prop;
    GDProp* gd_prop;
    W8LevelDataRecord* level;
    w8_ulong* objects;
    unsigned int count;
    unsigned int index;
    int surface_index;
    int hit_prop_id;
    bool direction_zero;
    float hit_distance;
    float slope;
    float along_length;
    float residual_length;
    float facing;
    srVector3T<float> prop_delta;
    srVector3T<float> adjusted_direction;
    srVector3T<float> probe;
    srVector3T<float> test_direction;
    srVector3T<float> hit_point;
    srVector3T<float> normal;
    srVector3T<float> projected;
    srVector3T<float> residual;
    srVector3T<float> along_normal;

    nearest_surface = 0;
    if (g_world->collidable_props->empty()) {
        return 0;
    }
    /* Function-local static plane ResolveCollision reads through hit_plane;
       atexit thunk at 0x0041BD50. Declared after the early-out so the guard
       matches retail control flow. */
    static W8Plane s_prop_hit_plane;
    objects = 0;
    count = static_cast<unsigned int>(
        octree->CollectObjectsAlongSegment(&objects, position, direction, 504.0f, 8));
    for (index = 0; index < count; ++index) {
        prop = (*g_world->collidable_props)[objects[index]];
        if (prop->GetActivationState() != 0) {
            gd_prop = prop->m_gd_prop;
            prop->flags |= 0x10;
            if (gd_prop == 0) {
                prop->BuildOrRefreshPathingRepresentation();
                gd_prop = prop->m_gd_prop;
                if (gd_prop == 0) {
                    return 0;
                }
            }
            prop->GetDelta(&prop_delta, position);
            adjusted_direction = *direction - prop_delta;
            probe = *position;
            if (adjusted_direction.x != g_float_zero || adjusted_direction.y != g_float_zero ||
                adjusted_direction.z != g_float_zero) {
                direction_zero = false;
            } else {
                direction_zero = true;
            }
            test_direction = adjusted_direction;
            for (surface_index = 0; surface_index < gd_prop->m_surface_count; ++surface_index) {
                surface = &gd_prop->m_pGDSurfaces[surface_index];
                surface->hit_plane = 0;
                if (direction_zero) {
                    test_direction = surface->plane.normal;
                }
                if (surface->TestSegment(&probe, &test_direction, &hit_distance,
                                         gd_prop->m_pVertices)) {
                    if (hit_distance < *nearest_distance) {
                        *nearest_distance = hit_distance;
                        hit_point = probe;
                        *scratch = prop_delta;
                        nearest_surface = surface;
                        hit_prop_id = objects[index];
                    }
                    probe = *position;
                }
            }
        }
        level = g_level_data;
    }
    if (nearest_surface != 0) {
        s_prop_hit_plane = nearest_surface->plane;
        slope = nearest_surface->slope;
        projected = residual = along_normal = *scratch;
        normal = s_prop_hit_plane.normal;
        along_normal = ProjectVectorOntoVector(along_normal.glmValue(), normal.glmValue());
        along_length = along_normal.Length();
        residual = (residual - along_normal) * static_cast<double>(slope);
        residual_length = residual.Length();
        if (projected.Length() <= g_float_one) {
            facing = 0.0f;
        } else {
            facing = glm::dot(projected.glmValue(), normal.glmValue()) / projected.Length();
            if (facing < g_float_zero) {
                along_length = -along_length;
            }
        }
        level = g_level_data;
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
            level->contact_velocity = residual / static_cast<double>(level->camera_scale);
        }
        if (level->contact_facing < facing) {
            level->contact_facing = facing;
            level->contact_normal = normal;
        }
        s_prop_hit_plane.w -= along_length;
        nearest_surface->hit_plane = &s_prop_hit_plane;
        *position = hit_point + *scratch;
    }
    return nearest_surface;
}

// FUNCTION: WIZ8 0x0041BD60
unsigned char W8GameData::ProbeMonstersAlongMotion(srVector3T<float>* direction,
                                                   srVector3T<float>* position, int)
{
    w8_ulong* objects;
    unsigned int count;
    unsigned int index;
    unsigned int monster_list_index;
    W8MonsterInfo* monster_info;
    W8Monster* monster;
    srVector3T<float> lower;
    srVector3T<float> upper;
    srVector3T<float> velocity;
    srVector3T<float> monster_position;
    srVector3T<float> adjustment;
    float extent;
    glm::vec2 adjusted;
    float horizontal;
    srVector3T<float> delta;
    float planar;
    float radius;
    float push;
    float length_squared;
    float scale;
    double time_scale;
    bool hit;

    objects = 0;
    hit = false;
    extent = direction->Length() + g_runtime_world_scale + g_world_scale;
    lower = position->glmValue() - extent;
    upper = position->glmValue() + extent;
    count = static_cast<unsigned int>(
        g_octree->QueryObjects(&objects, &lower, &upper, W8_OCTREE_KIND_LOCATION, -1));
    if (count == 0) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        if (objects[index] == 0) {
            return hit;
        }
        monster_list_index = MonsterGetIndexByLocationID(
            0x3a6, "C:\\Projects\\Wizardry 8\\Engine Code\\GameData.cpp", objects[index], true);
        monster_info = MonsterGetScriptPartByLocationIndex(monster_list_index);
        if (monster_info != 0 && monster_info->p3D != 0 && monster_info->p3D->active) {
            monster = monster_info->p3D;
            time_scale = g_rate * g_game_time_accumulator->GetFrameDelta();
            monster->GetVelocity(&velocity);
            adjusted.x = direction->x - velocity.x * static_cast<float>(time_scale);
            adjusted.y = direction->z - velocity.z * static_cast<float>(time_scale);
            horizontal = sqrtf(adjusted.x * adjusted.x + adjusted.y * adjusted.y);
            monster_position = monster->GetPosition();
            delta.x = monster_position.x - (adjusted.x + position->x);
            delta.y = monster_position.y - position->y;
            delta.z = monster_position.z - (adjusted.y + position->z);
            planar = sqrtf(delta.z * delta.z + delta.x * delta.x);
            radius = monster->radius;
            if (monster_info->fInCombat) {
                radius += g_world_scale;
            }
            const float abs_dy = std::abs(delta.y);
            if (abs_dy < g_float_one_thousand && planar < radius + g_world_scale) {
                push = g_monster_motion_push - (planar - radius) * g_float_one_five_hundredth;
                if (g_monster_motion_push < push) {
                    push = g_monster_motion_push;
                }
                length_squared = delta.z * delta.z + delta.x * delta.x;
                adjustment = delta;
                adjustment.y = 0.0f;
                if (length_squared != static_cast<float>(g_double_zero)) {
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
// FUNCTION: WIZ8 0x0041AB40
unsigned char W8GameData::AdvanceEnvironmentMotion()
{
    W8LevelDataRecord* level;
    W8EnvironRecord* environ_record;
    srVector3T<float> camera_position;
    srVector3T<float> adjusted_position;
    srVector3T<float> motion_delta;
    srVector3T<float> environ_delta;
    srVector3T<float> probe_position;
    srVector3T<float> hit_position;
    srVector3T<float> gravity;
    srVector3T<float> scaled;
    W8GDSurface* nearest_surface;
    W8GDSurface* surface;
    W8GDSurface* collisions[101];
    w8_ulong* octree_hits;
    W8GDSurface** geometry_hits;
    int hit_count;
    int collision_count;
    int attempt;
    int index;
    int crossed_count;
    float nearest_distance;
    float hit_distance;
    float motion_length;
    float scale;
    srVector3T<float> scratch;
    bool first_pass;
    bool exhausted;
    bool forced_exit;
    bool prop_hit;
    srVector3T<float> geometry_from;
    srVector3T<float> geometry_to;

    octree_hits = 0;
    geometry_hits = 0;
    prop_hit = false;
    if (g_level_data->ClampCameraToBounds(&minimum, &maximum) != 0) {
        return 0;
    }

    level = g_level_data;
    level->flags &= ~(W8_LEVEL_FLAG_PROP_CONTACT | W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH);
    level->motion_velocity += level->contact_velocity;
    level->contact_displacement = level->contact_velocity * level->camera_scale;
    level->motion_displacement += level->contact_displacement;
    level->residual_contact_length = 0.0f;
    level->contact_facing = 0.0f;

    level = g_level_data;
    camera_position = level->camera_position;
    adjusted_position = camera_position;
    if (geometry_index == 0 && octree == 0) {
        if (static_cast<float>(g_motion_delta_epsilon) < level->motion_displacement.Length()) {
            return 1;
        }
        return 0;
    }

    g_environ_ground_latch = g_environ->ground_latch;
    g_environment_motion_active = 1;
    if (level->motion_velocity.Length() <= static_cast<float>(g_motion_vector_epsilon)) {
        level->flags &= ~W8_LEVEL_FLAG_NEGLIGIBLE_MOTION;
    } else {
        level->flags |= W8_LEVEL_FLAG_NEGLIGIBLE_MOTION;
    }

    environ_record = g_environ;
    environ_record->motion_factor = environ_record->motion_step;
    environ_record->airborne = 0;
    environ_record->scale = level->camera_scale;
    gravity.Set(environ_record->gravity_x, environ_record->gravity_y, environ_record->gravity_z);
    scaled = gravity * static_cast<double>(environ_record->scale);
    environ_record->vector += scaled;
    if (g_camera_motion_divisor < environ_record->vector.Length()) {
        environ_record->vector.SetLength(g_camera_motion_divisor);
    }
    environ_record->AddScaledMotion(&level->motion_displacement);

    if (level->camera_scale == g_float_zero) {
        level->motion_velocity.SetZero();
    } else {
        level->motion_velocity =
            level->motion_displacement / static_cast<double>(level->camera_scale);
    }

    environ_delta = level->motion_displacement;
    camera_position.Set(adjusted_position.x,
                        (g_world_scale - g_environ->world_height) + adjusted_position.y,
                        adjusted_position.z);
    motion_delta = environ_delta;
    adjusted_position.y = camera_position.y;

    if (m_iNumTriggers != 0) {
        if (active_trigger_bits.empty()) {
            IntegrateTriggers();
        } else {
            std::ranges::fill(active_trigger_bits, false);
        }
    }

    level = g_level_data;
    nearest_distance = 1.0e8f;
    hit_position.SetZero();
    level->secondary_contact_prop_id = -1;
    level->primary_contact_prop_id = -1;
    level->flags &= ~W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH;
    level->contact_normal.SetZero();
    collision_count = -1;
    level->contact_facing = 0.0f;
    first_pass = true;
    nearest_surface = 0;
    forced_exit = false;
    exhausted = false;
    attempt = 0;

    for (;;) {
        ++attempt;
        if (attempt < 6) {
            g_environment_motion_active = 1;
            probe_position = camera_position;
            first_pass = true;
            nearest_surface = 0;
            nearest_distance = 1.0e8f;
            if (geometry_index == 0) {
                if (octree != 0) {
                    if (attempt < 3) {
                        ProbeMonstersAlongMotion(&motion_delta, &probe_position, 1);
                    }
                    nearest_surface = ProbePropsAlongMotion(&motion_delta, &probe_position,
                                                            &scratch, &nearest_distance);
                    prop_hit = nearest_surface != 0;
                    if (prop_hit) {
                        hit_position = probe_position;
                    }
                    probe_position = camera_position;
                    hit_count = octree->CollectObjectsAlongSegment(&octree_hits, &camera_position,
                                                                   &motion_delta, 1000.0f, 3);
                } else {
                    hit_count = 0;
                }
            } else {
                geometry_to = motion_delta;
                geometry_from = camera_position;
                nearest_surface = 0;
                hit_count = geometry_index->CollectObjectsAlongSegment(
                    &geometry_hits, &geometry_from, &geometry_to, 1.57079637f, 2000.0f, 3);
            }
            if (((hit_count == 0 && !prop_hit) || g_environment_motion_active == 0)) {
                exhausted = true;
            }
        } else {
            g_environment_motion_active = 0;
            exhausted = true;
            forced_exit = true;
            probe_position = camera_position;
            if ((g_level_data->flags & W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH) != 0 &&
                g_level_data->ToggleBoundProps() == 0) {
                environ_delta.SetZero();
            }
        }

        while ((hit_count != 0 || prop_hit) && g_environment_motion_active != 0) {
            if (hit_count != 0) {
                probe_position = camera_position;
                for (index = 0; index < hit_count; ++index) {
                    if (octree == 0) {
                        surface = geometry_hits[index];
                    } else {
                        surface = &m_pSurfaces[octree_hits[index]];
                    }
                    surface->hit_plane = 0;
                    if ((surface->flags & W8_GD_SURFACE_CROSSING_MASK) == 0 &&
                        surface->TestSegment(&probe_position, &motion_delta, &hit_distance,
                                             m_pVertices.data())) {
                        if (hit_distance < nearest_distance) {
                            nearest_distance = hit_distance;
                            hit_position = probe_position;
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
                g_environment_motion_active = nearest_surface->ResolveCollision(
                    &camera_position, &hit_position, &motion_delta, collision_count);
                probe_position = camera_position + motion_delta - adjusted_position;
                if (collision_count < 0) {
                    collision_count = 0;
                }
                collisions[collision_count] = nearest_surface;
                ++collision_count;
                if (99 < collision_count) {
                    srAssertFail("lCollisions < 100",
                                 "C:\\Projects\\Wizardry 8\\Engine Code\\GameData.cpp", 0x253, 0);
                }
                if (g_environment_motion_active == 0) {
                    hit_count = 0;
                    prop_hit = false;
                } else {
                    probe_position = camera_position;
                    nearest_surface = 0;
                    nearest_distance = 1.0e8f;
                    if (octree == 0) {
                        if (geometry_index != 0) {
                            geometry_hits = 0;
                            hit_count = geometry_index->CollectObjectsAlongSegment(
                                &geometry_hits, &geometry_from, &geometry_to, 1.57079637f, 2000.0f,
                                3);
                        } else {
                            hit_count = 0;
                        }
                    } else {
                        if (attempt < 3) {
                            ProbeMonstersAlongMotion(&motion_delta, &probe_position, 1);
                        }
                        nearest_surface = ProbePropsAlongMotion(&motion_delta, &probe_position,
                                                                &scratch, &nearest_distance);
                        prop_hit = nearest_surface != 0;
                        if (prop_hit) {
                            hit_position = probe_position;
                        }
                        probe_position = camera_position;
                        hit_count = octree->CollectObjectsAlongSegment(
                            &octree_hits, &camera_position, &motion_delta, 1000.0f, 3);
                    }
                }
            }
        }

        if (exhausted) {
            if (octree != 0) {
                probe_position = adjusted_position;
                motion_delta = environ_delta;
                hit_count = octree->CollectObjectsAlongSegment(&octree_hits, &adjusted_position,
                                                               &environ_delta, 1000.0f, 3);
                crossed_count = 0;
                for (index = 0; index < hit_count; ++index) {
                    surface = &m_pSurfaces[octree_hits[index]];
                    if ((surface->flags & W8_GD_SURFACE_CROSSING_MASK) != 0 &&
                        surface->TestSegment(&probe_position, &motion_delta, &hit_distance,
                                             m_pVertices.data())) {
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
                    ProcessCrossedSurface(collisions[index]);
                }
            }

            level = g_level_data;
            g_environment_motion_active = 0;
            motion_length = environ_delta.Length();
            if (motion_length <= g_float_one_tenth ||
                (fabsf(environ_delta.y + motion_length) / motion_length <= g_camera_snap_epsilon &&
                 g_negative_one <= environ_delta.y)) {
                environ_delta.SetZero();
            } else {
                g_environment_motion_active = 1;
            }

            scale = static_cast<float>(g_double_one) / g_level_data->camera_scale;
            level->motion_velocity = environ_delta * scale;
            level->motion_displacement = environ_delta;
            if (g_environment_motion_active == 0) {
                if ((g_level_data->flags & W8_LEVEL_FLAG_NEGLIGIBLE_MOTION) != 0 && !forced_exit) {
                    g_environ_ground_latch = true;
                }
            } else {
                g_environ_ground_latch = false;
            }
            g_environ->ground_latch = g_environ_ground_latch;

            if (m_iNumTriggers != 0) {
                for (unsigned int trigger_index = 0; trigger_index < pending_trigger_bits.size();
                     ++trigger_index) {
                    if (pending_trigger_bits[trigger_index] &&
                        !TestBit(active_trigger_bits, trigger_index)) {
                        if (!m_ppTriggers.empty()) {
                            m_ppTriggers[trigger_index]->FinishAction();
                        }
                        pending_trigger_bits[trigger_index] = false;
                    }
                }
            }
            return g_environment_motion_active;
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
/* Run the buffered prop id list through TestProp and report the id of the
   last prop that hit; an empty list reports -1. */
// FUNCTION: WIZ8 0x0041c0d0
int W8GameData::TestPropSurfaces(int count, w8_ulong* ids, W8OctreeTrace* trace,
                                 char skip_flag, char gate)
{
    int last_hit = -1;
    if (count == 0) {
        return -1;
    }
    do {
        if (TestProp(*ids, trace, skip_flag, gate)) {
            last_hit = *ids;
        }
        ++ids;
        --count;
    } while (count != 0);
    return last_hit;
}

/* Point the shared surface/vertex arrays at one collidable prop's GDProp
   tables, ray-test them and put the arrays back. Without a pre-tree the prop
   comes out of the world's collidable list, its pathing representation is
   built on demand, and the ray is moved into prop space through the prop's
   position delta; on a hit the caller's record is reseeded from the start to
   the world-space contact. `gate` skips flag-4 props when set. */
// FUNCTION: WIZ8 0x0041c140
bool W8GameData::TestProp(int prop_id, W8OctreeTrace* trace, char skip_flag, char gate)
{
    bool hit = false;
    GDProp* gd_prop;
    W8Prop* prop;

    if (g_oct_pre_tree == 0) {
        prop = (*g_world->collidable_props)[prop_id];
        gd_prop = prop->m_gd_prop;
        prop->flags |= 0x10;
        if (gd_prop == 0) {
            prop->BuildOrRefreshPathingRepresentation();
            gd_prop = prop->m_gd_prop;
        }
    } else {
        gd_prop = (*g_oct_pre_tree->props)[prop_id];
    }
    const std::span surfaces(gd_prop->m_pGDSurfaces, gd_prop->m_surface_count);
    const std::span vertices(gd_prop->m_pVertices, gd_prop->m_vertex_count);
    if (g_oct_pre_tree == 0) {
        if (gate == 0 || !gd_prop->m_flags.unattached) {
            srVector3T<float> start = trace->start;
            srVector3T<float> end = trace->end;
            srVector3T<float> delta;
            prop->GetDelta(&delta, &start);
            end -= delta;
            W8OctreeTrace prop_trace(&start, &end);
            hit = TestGeometryTrace(surfaces, vertices, gd_prop->m_surface_count, 0, &prop_trace, skip_flag, 0);
            if (hit) {
                end = prop_trace.end + delta;
                trace->Reseed(&start, &end);
            }
        }
    } else {
        hit = TestGeometryTrace(surfaces, vertices, gd_prop->m_surface_count, 0, trace, skip_flag, 0);
    }
    return hit;
}

/* Ray the trace record against `count` surfaces: all of m_pSurfaces in order
   when `surface_ids` is null, else just the listed indexes. The trace_flag4_gate flag
   admits flag-4 surfaces only, 0x1080-marked surfaces are skipped outright,
   `skip_flag` drops 0x8000-marked ones, and a nonzero chance needs a
   passing `mode` roll. Each accepted surface's plane is tested both sides of
   the segment; a point-in-triangle pass on the contact keeps the closest hit,
   storing index into last_hit_surface, the contact into end and the hit
   distance into hit_limit/length. */
// FUNCTION: WIZ8 0x0041c330
bool W8GameData::TestTraceResult(int count, w8_ulong* surface_ids, W8OctreeTrace* trace,
                                 char skip_flag, int mode)
{ return TestGeometryTrace(m_pSurfaces, m_pVertices, count, surface_ids, trace, skip_flag, mode); }

bool W8GameData::TestGeometryTrace(std::span<W8GDSurface> surfaces,
    std::span<const srVector3T<float>> vertices_bank, int count, w8_ulong* surface_ids,
    W8OctreeTrace* trace, char skip_flag, int mode)
{
    bool hit = false;
    srVector3T<float> best_contact;

    last_hit_surface = 0;
    if (count != 0) {
        w8_ulong* id = surface_ids;
        int index = 0;
        int remaining = count;
        do {
            W8GDSurface* surface;
            if (surface_ids == 0) {
                surface = surfaces.data() + index;
            } else {
                surface = surfaces.data() + *id;
            }
            if (((!trace_flag4_gate || (surface->flags & W8_GD_SURFACE_WALKABLE) != 0) &&
                 (surface->flags & W8_GD_SURFACE_CROSSING_MASK) == 0 &&
                 (skip_flag == 0 || (surface->flags & W8_GD_SURFACE_SKIP_FILTERED_TRACE) == 0)) &&
                (surface->chance == 0 ||
                 (mode != -1 && (mode < 2 || static_cast<int>(surface->chance) < mode) &&
                  (mode != 1 ||
                   (static_cast<int>(surface->chance) < 100 &&
                    static_cast<int>(surface->chance) < static_cast<int>(Random(100)))))) &&
                glm::dot((surface->plane.normal).glmValue(), (trace->step).glmValue()) <=
                    g_float_zero) {
                float hit_distance = SignedPlaneDistance(surface->plane, trace->start);
                if (hit_distance <= trace->hit_limit && g_float_zero < hit_distance) {
                    srVector3T<float> contact;
                    if (g_float_one <= hit_distance) {
                        float back = SignedPlaneDistance(surface->plane, trace->end);
                        if (g_float_one <= back) {
                            goto next;
                        }
                        back = -back;
                        if (g_float_one <= back || trace->hit_limit < trace->length) {
                            hit_distance = (hit_distance / (back + hit_distance)) * trace->length;
                            contact = trace->start + trace->step * hit_distance;
                        } else {
                            contact = trace->end;
                            hit_distance = trace->length;
                        }
                    } else {
                        contact = trace->start;
                    }
                    srVector3T<float> vertices[3];
                    vertices[0] = vertices_bank[surface->vertex_indices[0]];
                    vertices[1] = vertices_bank[surface->vertex_indices[1]];
                    vertices[2] = vertices_bank[surface->vertex_indices[2]];
                    if (PointInsideTriangle(vertices, surface->flags & W8_GD_SURFACE_AXIS_MASK,
                                            &contact) &&
                        hit_distance < trace->hit_limit) {
                        last_hit_surface = surface->index;
                        hit = true;
                        trace->hit_limit = hit_distance;
                        best_contact = contact;
                    }
                }
            }
        next:
            /* Retail verified at 0x0041C627: the cursor advances
               unconditionally even when `surface_ids` is null, so `++id` on a
               null pointer is the retail behavior rather than a defect. */
            if (surface_ids != 0) {
                ++id;
            }
            ++index;
            --remaining;
        } while (remaining != 0);
        if (hit) {
            trace->end = best_contact;
            trace->length = trace->hit_limit;
            return hit;
        }
    }
    return false;
}

/* Selects a switch interface's state: the matching state group's conditional
   polygons clear surface flag 0x10 while every other state's polygons set
   it. Asserts the interface id and the state slice bounds. */
// FUNCTION: WIZ8 0x0041c680
char W8GameData::SetInterfaceState(int iID, int state)
{
    int iStates;
    int poly_count;
    W8GDInterface* interface_rec;
    W8GDInterfaceState* states;
    int* polys;

    if (iID == 0 || InterfacesCount() <= iID) {
        srAssertFail("iID && iID < InterfacesCount()",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\GameData.cpp", 0x4e0, 0);
    }
    interface_rec = m_pInterfaces.data() + iID;
    iStates = interface_rec->state_count;
    if (StatesCount() < interface_rec->iStates + iStates) {
        srAssertFail("(m_pInterfaces[iID].iStates + iStates) <= StatesCount()",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\GameData.cpp", 0x4e2, 0);
    }
    states = m_pStates.data() + interface_rec->iStates;
    for (; iStates != 0; --iStates) {
        poly_count = states->poly_count;
        polys = m_piCondPolys.data() + states->poly_first;
        if (states->group == state) {
            for (; poly_count != 0; --poly_count) {
                m_pSurfaces[*polys].flags &= ~W8_GD_SURFACE_CONDITIONAL_DISABLED;
                ++polys;
            }
        } else {
            for (; poly_count != 0; --poly_count) {
                m_pSurfaces[*polys].flags |= W8_GD_SURFACE_CONDITIONAL_DISABLED;
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
// FUNCTION: WIZ8 0x0041c770
void W8GameData::ProcessCrossedSurface(W8GDSurface* surface)
{
    Trigger* trigger;
    int direction;

    if ((surface->flags & W8_GD_SURFACE_ENVIRONMENT) != 0) {
        const bool entering = surface->distance > 0.0f;
        if (!entering && g_environ != m_ppEnvirons[surface->trigger_index].get())
            return;

        W8EnvironRecord* record = m_ppEnvirons[entering ? surface->trigger_index : 0].get();
        const W8EnvironRecord* current = g_environ;
        record->ground_latch = current->ground_latch;
        record->airborne = current->airborne;
        record->value_08 = current->value_08;
        record->scale = current->scale;
        record->motion_factor = current->motion_factor;
        record->vector = current->vector;
        g_environ = record;
        return;
    }
    direction = 1;
    if (surface->distance < 0.0f) {
        direction = -1;
    }
    if (m_ppTriggers.empty() || SetBit(active_trigger_bits, surface->trigger_index) ||
        SetBit(pending_trigger_bits, surface->trigger_index)) {
        return;
    }
    trigger = m_ppTriggers[surface->trigger_index];
    if (trigger->initial_action == 0x10 && surface->contact_margin > 0.0f) {
        surface->contact_margin = 0.0f;
        if (direction == 1) {
            ClearBit(active_trigger_bits, surface->trigger_index);
            ClearBit(pending_trigger_bits, surface->trigger_index);
            return;
        }
    }
    if (direction != 0) {
        trigger->Run(direction);
        if (trigger->action_state == 1 || trigger->action_state == 4) {
            ClearBit(active_trigger_bits, surface->trigger_index);
            ClearBit(pending_trigger_bits, surface->trigger_index);
        }
    }
}

/* Builds the octree trace model and answers its scene node. Every surface
   becomes one polygon whose three vertices copy the surface's corner
   positions; flag-4 surfaces mark their vertices in the bit set. The DIG pass
   then writes a direction vector per vertex: marked vertices keep the surface
   normal (flattened to point straight up when it tilts below 0.5), unmarked
   vertices get a scaled/biased horizontal direction renormalized to 0.3. */
// FUNCTION: WIZ8 0x0041c930
stModelInstance* W8GameData::CreateTraceModel()
{
    std::vector<bool> selected(SurfacesCount() * 3 + 10);
    auto mesh = std::make_shared<stMeshModel>(SurfacesCount(), SurfacesCount() * 3);
    if (mesh == 0) {
        srAssertFail("pstMeshModel", "C:\\Projects\\Wizardry 8\\Engine Code\\GameData.cpp", 0x56d,
                     "ModelGameData::Read -- Could not create pstMeshModel.\n");
    }
    mesh->flags &= ~W8_MESH_SORTED_RENDERING;
    srVector3i* poly_vertices = mesh->getPolyVertex();
    std::shared_ptr<srTextureIFace>* poly_textures = mesh->getPolyTexture(0, 0, 1);
    srVector3T<float>* vertex_locs = mesh->getVertexLoc();
    glm::vec2* texcoords = mesh->getVertexTexCoords(0, 0, 1);
    std::shared_ptr<srMaterialIFace>* vertex_materials =
        mesh->getVertexMaterial(0, srMeshModel::SIDE_FRONT, 1);
    w8_ulong* shade_indices = mesh->getVertexShadeIndex(1);
    int vertex = 0;
    for (int index = 0; index < SurfacesCount(); ++index) {
        W8GDSurface* surface = m_pSurfaces.data() + index;
        poly_textures[index] = g_oct_mesh_default.texture;
        if ((surface->flags & W8_GD_SURFACE_WALKABLE) != 0) {
            selected[vertex] = true;
            selected[vertex + 1] = true;
            selected[vertex + 2] = true;
        }
        texcoords[vertex] = glm::vec2(0);
        vertex_materials[vertex] = g_oct_mesh_default.material;
        poly_vertices[index].x = vertex;
        shade_indices[vertex] = vertex;
        vertex_locs[vertex] = m_pVertices[surface->vertex_indices[0]];
        texcoords[vertex + 1] = glm::vec2(0);
        vertex_materials[vertex + 1] = g_oct_mesh_default.material;
        poly_vertices[index].y = vertex + 1;
        shade_indices[vertex + 1] = vertex + 1;
        vertex_locs[vertex + 1] = m_pVertices[surface->vertex_indices[1]];
        texcoords[vertex + 2] = glm::vec2(0);
        vertex_materials[vertex + 2] = g_oct_mesh_default.material;
        poly_vertices[index].z = vertex + 2;
        shade_indices[vertex + 2] = vertex + 2;
        vertex_locs[vertex + 2] = m_pVertices[surface->vertex_indices[2]];
        vertex += 3;
    }
    mesh->setShader(g_oct_mesh_default.render_flags, 0);
    mesh->setDirty(srMeshModel::DIRTY_TRI_MESH);
    srVector3T<float>* normals = mesh->getVertexNormal();
    srVector3T<float>* dig = mesh->getVertexDIG(0, 1);
    for (int i = 0; i < vertex; ++i) {
        if (selected[i]) {
            dig[i] = normals[i];
            if (dig[i].y < g_float_half) {
                dig[i].y = 0.0f;
                dig[i].Normalize();
                dig[i].y = 1.0f;
                dig[i].Normalize();
            }
        } else {
            dig[i].Set(normals[i].x * g_float_fifteen_hundredths + g_float_fifteen_hundredths, 0.0f,
                       normals[i].z * g_float_fifteen_hundredths + g_float_fifteen_hundredths);
            float length = dig[i].Length();
            if (g_double_one_ten_thousandth <= length) {
                if (length < 0.3) {
                    dig[i].SetLength(0.3);
                }
            } else {
                dig[i].x = 0.15f;
                dig[i].z = 0.15f;
            }
        }
    }
    mesh->setName("GameData Mesh");
    mesh->flags &= ~W8_MESH_VERTEX_LIGHTING_DIRTY;
    stModelInstance* instance = CreateModelInstance(mesh);
    instance->setName("GameData Mesh");
    return instance;
}

// GLOBAL: WIZ8 0x005ebc80
const float g_collision_fraction_floor = -0.1f;
// GLOBAL: WIZ8 0x005ebc8c
const float g_collision_slope_margin_scale = 150.0f;
// GLOBAL: WIZ8 0x005ebc94
const float g_collision_height_lift = 165.0f;
// GLOBAL: WIZ8 0x005ebc9c
const float g_collision_ceiling_normal_threshold = -0.8f;
// GLOBAL: WIZ8 0x005ebca8
const double g_collision_direction_epsilon = 0.0010000000474974513;
// GLOBAL: WIZ8 0x005ebcb0
const float g_collision_parallel_normal_threshold = 0.99999f;
// GLOBAL: WIZ8 0x005ebcb4
const float g_collision_opposed_normal_threshold = -0.999f;
// GLOBAL: WIZ8 0x005ebcb8
const float g_collision_contact_normal_threshold = -0.5f;
// GLOBAL: WIZ8 0x005ebcc0
const double g_collision_centroid_scale = 0.33333298563957214;

/* Clip the motion segment against this surface's plane and triangle. On a hit
   `from` advances to the contact point, `hit_distance` returns the travelled
   length and distance takes the surface's updated limit. Ordinary surfaces
   gate on the contact band below distance; flag-0x1080 surfaces (special)
   instead test whether the segment crossed the plane, and lift `from` by the
   level-flag-8 camera offset. */
// FUNCTION: WIZ8 0x0041CF90
bool W8GDSurface::TestSegment(srVector3T<float>* from, const srVector3T<float>* direction,
                              float* hit_distance, srVector3T<float>* vertices)
{
    unsigned int flags = this->flags;
    bool special = false;
    bool crossed = false;
    bool inside = false;
    if ((flags & W8_GD_SURFACE_INACTIVE_MASK) != 0) {
        return false;
    }
    srVector3T<float> point = *from;
    if ((flags & W8_GD_SURFACE_CROSSING_MASK) != 0) {
        special = true;
        if ((g_level_data->flags & W8_LEVEL_FLAG_NO_SOUND_ENVIRONMENT) != 0) {
            point.y += g_collision_height_lift;
        }
    }
    srVector3T<float> unit_dir = *direction;
    unit_dir.Normalize();
    srVector3T<float> normal;
    normal = plane.normal;
    float segment_length = direction->Length();
    if (segment_length < g_float_one_ten_thousandth) {
        if (!special) {
            g_environment_motion_active = special;
            return false;
        }
    } else if (!special && normal.x * unit_dir.x + unit_dir.y * normal.y + unit_dir.z * normal.z >=
                               g_float_zero) {
        return false;
    }
    srVector3T<float> end = point + *direction;
    float dist_start = SignedPlaneDistance(plane, point);
    if (!special && dist_start < g_float_zero) {
        return false;
    }
    float limit = contact_margin;
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0 && !special) {
        limit = contact_margin - (g_float_one - fabsf(normal.y)) * g_collision_slope_margin_scale;
    }
    float dist_end = SignedPlaneDistance(plane, end);
    if (special) {
        if (dist_end * dist_start > g_camera_snap_epsilon) {
            if (contact_margin < g_float_one || dist_end < g_float_zero) {
                return false;
            }
            if (dist_end >= limit + g_float_ten && dist_start >= limit + g_float_ten) {
                return false;
            }
        }
    } else {
        if (limit + g_float_ten <= dist_end) {
            return false;
        }
        if (dist_start - dist_end < g_camera_transition_epsilon) {
            return false;
        }
    }
    int axis = flags & W8_GD_SURFACE_AXIS_MASK;
    short comp_u = (axis + 1) % 3;
    short comp_v = (axis + 2) % 3;
    const auto start_proj = point.glmValue() - normal.glmValue() * dist_start;
    const auto end_proj = end.glmValue() - normal.glmValue() * dist_end;
    const glm::vec3 verts[] = {vertices[vertex_indices[0]].glmValue(),
                               vertices[vertex_indices[1]].glmValue(),
                               vertices[vertex_indices[2]].glmValue()};
    for (short edge = 0; edge < 3; ++edge) {
        if (crossed) {
            break;
        }
        short next = (edge + 1) % 3;
        if (SegmentCrossesEdge(start_proj, end_proj, verts[edge], verts[next], axis)) {
            crossed = true;
        }
        float edge_low = verts[edge][comp_v];
        float edge_high = verts[next][comp_v];
        if ((edge_low <= end_proj[comp_v] && end_proj[comp_v] < edge_high) ||
            (edge_high <= end_proj[comp_v] && end_proj[comp_v] < edge_low)) {
            float crossing = (verts[next][comp_u] - verts[edge][comp_u]) *
                                 (end_proj[comp_v] - edge_low) / (edge_high - edge_low) +
                             verts[edge][comp_u];
            if (crossing > end_proj[comp_u]) {
                inside = !inside;
            }
        }
    }
    float t_span = dist_start - dist_end;
    float fraction;
    if (t_span < g_float_one) {
        fraction = g_float_one;
    } else {
        fraction = (dist_start - limit) / t_span;
    }
    if (fraction < g_collision_fraction_floor && !special) {
        fraction = g_collision_fraction_floor;
    }
    if (fraction > g_float_one) {
        return false;
    }
    float hit;
    if (!crossed && !inside) {
        if (special) {
            return false;
        }
        srVector3T<float> chosen;
        if (dist_start / t_span <= g_float_one) {
            chosen = point;
        } else {
            chosen = end;
        }
        if (!ClampHitToEdge(&chosen, vertices, &limit)) {
            return false;
        }
        fraction = (dist_start - limit) / t_span;
        if (fraction >= g_float_one) {
            return false;
        }
        if (fraction < g_collision_fraction_floor) {
            fraction = g_collision_fraction_floor;
        }
        hit = fraction * segment_length;
        if (segment_length - hit < g_camera_transition_epsilon) {
            return false;
        }
        *hit_distance = hit;
        distance = limit;
        *from = point + unit_dir * hit;
        return true;
    }
    hit = fraction * segment_length;
    *hit_distance = hit;
    if (!special) {
        distance = limit;
        *from = point + unit_dir * hit;
        return true;
    }
    distance = hit;
    if (dist_start <= g_float_zero) {
        distance = -fabsf(hit);
    } else {
        distance = fabsf(hit);
    }
    *hit_distance = fabsf(*hit_distance);
    return true;
}

/* 2D segment-vs-edge test in the plane perpendicular to `axis`: the projected
   motion segment seg_start→seg_end must overlap edge_a→edge_b on both free
   axes and their line-crossing parameters must both fall inside [0,1]. */
// FUNCTION: WIZ8 0x0041D7A0
bool SegmentCrossesEdge(const glm::vec3& seg_start, const glm::vec3& seg_end,
                        const glm::vec3& edge_a, const glm::vec3& edge_b, unsigned int axis)
{
    const unsigned u = (axis + 1) % 3;
    const unsigned v = (axis + 2) % 3;
    const auto start = glm::vec2(seg_start[u], seg_start[v]);
    const auto end = glm::vec2(seg_end[u], seg_end[v]);
    const auto first = glm::vec2(edge_a[u], edge_a[v]);
    const auto second = glm::vec2(edge_b[u], edge_b[v]);
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
// FUNCTION: WIZ8 0x0041D9D0
bool W8GDSurface::ClampHitToEdge(const srVector3T<float>* point, const srVector3T<float>* vertices,
                                 float* limit)
{
    float nearest_dist = 10000000.0f;
    short nearest_edge = 0;
    for (short edge = 0; edge < 3; ++edge) {
        srVector3T<float> probe = *point;
        float dist = PointToSegmentDistance(&probe, vertices + vertex_indices[edge],
                                            vertices + vertex_indices[(edge + 1) % 3], false, 0);
        if (dist < nearest_dist) {
            nearest_edge = edge;
            nearest_dist = dist;
        }
    }
    if (nearest_dist > *limit) {
        return false;
    }
    float offset = -(SignedPlaneDistance(plane, *point));
    srVector3 projected = *point + plane.normal * offset;
    float edge_dist =
        PointToSegmentDistance(&projected, vertices + vertex_indices[nearest_edge],
                               vertices + vertex_indices[(nearest_edge + 1) % 3], false, 0);
    if ((flags & W8_GD_SURFACE_WALKABLE) != 0) {
        float threshold = *limit * g_float_half;
        if (threshold < offset) {
            return false;
        }
    }
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0) {
        if (plane.normal.y < g_collision_ceiling_normal_threshold) {
            edge_dist *= g_navigator_linked_radius_scale;
        }
    } else {
        float scaled = *limit * g_navigator_mode3_scale;
        if (scaled < edge_dist) {
            edge_dist = (edge_dist - scaled) * g_float_six + scaled;
        }
    }
    if (edge_dist < *limit) {
        *limit = sqrtf(*limit * *limit - edge_dist * edge_dist);
        return true;
    }
    return false;
}

/* Collision response for a hit surface: `origin` advances to `hit_point` and
   `direction` is bent along the contact plane; returns whether motion
   continues. The crossed contact planes persist across calls in the statics
   so sequential bounces wedge the slide between them. */
// FUNCTION: WIZ8 0x0041DC10
bool W8GDSurface::ResolveCollision(srVector3T<float>* origin, const srVector3T<float>* hit_point,
                                   srVector3T<float>* direction, int collision_index)
{
    /* Statics 0x652d50/0x652d68/0x652d80 carry the latched entry direction and
       the first/second contact normals; their atexit thunks are the SYNTHETIC
       markers at 0x41e8d0/0x41e8c0/0x41e8b0. */
    static glm::vec3 s_entry_direction;
    static glm::vec3 s_second_normal;
    static glm::vec3 s_first_normal;
    static short s_collision_state;
    static float s_second_limit;
    static int s_first_surface;
    static float s_first_limit;

    bool recomputed = false;
    bool crossed = false;
    glm::vec3 crease_a(0);
    if ((flags & W8_GD_SURFACE_WALKABLE) != 0) {
        if ((flags & W8_GD_SURFACE_EXPLICIT_SLOPE) != 0 && g_party_has_slope_override_item != 0 &&
            g_sea_caves_slope_override_enabled) {
            g_environ->motion_factor = 1.0f;
        } else if (g_environ->motion_factor < slope) {
            g_environ->motion_factor = slope;
        }
        g_environ->airborne = 1;
        g_level_data->flags |= W8_LEVEL_FLAG_WALKABLE_CONTACT;
        g_level_data->sound_environment = footstep_surface;
        g_level_data->sound_environment_alt = footstep_material;
    }
    float direction_length = direction->Length();
    if (direction_length == g_float_zero) {
        return false;
    }
    /* ProbePropsAlongMotion stores the prop's hit plane here; level surfaces
       leave it zero and use the embedded plane. */
    const W8Plane* override_plane = hit_plane;
    glm::vec3 normal;
    float plane_distance;
    if (override_plane == 0) {
        normal = plane.normal.glmValue();
        plane_distance = plane.w;
    } else {
        normal = override_plane->normal.glmValue();
        plane_distance = override_plane->w;
    }
    float adjusted_d = plane_distance - distance;
    W8LevelDataRecord* level = g_level_data;
    if (level->primary_contact_prop_id > -1 &&
        glm::dot(normal, (level->contact_normal).glmValue()) <
            g_collision_contact_normal_threshold) {
        level->flags |= W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH;
    }
    flags |= W8_GD_SURFACE_COLLISION_PROCESSED;
    const float depth = -(glm::dot((*origin + *direction).glmValue(), normal) + adjusted_d);
    distance = depth;
    if (depth < g_float_zero) {
        return true;
    }
    glm::vec3 unit;
    if (collision_index < 1) {
        if (collision_index != 0) {
            unit = direction->glmValue() / direction_length;
            s_entry_direction = unit;
        }
        s_collision_state = 0;
    }

    const auto step = hit_point->glmValue() - origin->glmValue();
    *direction = direction->glmValue() - step;
    *origin = *hit_point;
    if (glm::dot(normal, s_entry_direction) < g_collision_opposed_normal_threshold) {
        direction->SetZero();
        g_environ->vector = 0.0f;
        return false;
    }
    unit = direction->glmValue() / direction_length;
    glm::vec3 slide = direction->glmValue() + normal * distance;
    ApplyEnvironContact(slide);
    glm::vec3 slide_unit = slide;
    slide_unit = normalizedOrZero(slide_unit);
    float approach = glm::dot(slide_unit, s_entry_direction);
    if (fabsf(approach) < g_camera_snap_epsilon) {
        direction->SetZero();
        return false;
    }
    if (approach >= g_float_one_ten_thousandth) {
        if (s_collision_state != 0) {
            glm::vec3 ortho = s_entry_direction;
            ortho = ProjectVectorOntoVector(ortho, normal);
            glm::vec3 deflect = s_entry_direction - ortho;
            deflect = normalizedOrZero(deflect);
            if (glm::dot(unit, deflect) < g_float_zero && s_collision_state == 1 &&
                CentroidsDiverging(s_first_surface, normal, s_first_normal)) {
                crossed = true;
            }
        }
    } else if (s_collision_state != 0) {
        crossed = true;
    }
    if (s_collision_state != 0) {
        if (g_collision_parallel_normal_threshold < glm::dot(normal, s_first_normal)) {
            if (s_first_limit <= adjusted_d) {
                return true;
            }
            s_first_limit = adjusted_d;
            s_first_surface = index;
            *direction = slide;
            return true;
        }
        if (crossed) {
            crease_a = glm::cross(normal, s_first_normal);
            if (glm::dot(crease_a, unit) < g_float_zero) {
                crease_a = -crease_a;
            }
        } else {
            recomputed = false;
            if (glm::dot(slide_unit, s_first_normal) > g_float_one_ten_thousandth) {
                s_first_normal = normal;
                s_first_surface = index;
                recomputed = true;
                s_first_limit = adjusted_d;
            }
        }
        if (s_collision_state == 2) {
            if (g_collision_parallel_normal_threshold < glm::dot(normal, s_second_normal)) {
                if (s_second_limit <= adjusted_d) {
                    return true;
                }
                s_second_limit = adjusted_d;

                *direction = slide;
                return true;
            }
            if (glm::dot((s_second_normal + s_first_normal), normal) < g_camera_snap_epsilon) {
                direction->SetZero();
                return false;
            }
            if (crossed) {
                glm::vec3 crease_b = glm::cross(normal, s_second_normal);
                if (glm::dot(crease_b, unit) < g_camera_transition_epsilon) {
                    crease_b = -crease_b;
                }
                float ahead_a = glm::dot(crease_a, s_entry_direction);
                float ahead_b = glm::dot(crease_b, s_entry_direction);
                if (ahead_a < g_camera_snap_epsilon && ahead_b < g_camera_snap_epsilon) {
                    direction->SetZero();
                    return false;
                }
                if (ahead_b <= ahead_a) {
                    slide = ProjectVectorOntoVector(slide, crease_b);
                    s_first_normal = normal;
                    s_first_limit = adjusted_d;
                    s_first_surface = index;
                } else {
                    slide = ProjectVectorOntoVector(slide, crease_a);
                    s_second_normal = normal;

                    s_second_limit = adjusted_d;
                }
            } else if (glm::dot(slide_unit, s_second_normal) >= g_float_zero) {
                if (recomputed) {
                    s_collision_state = 1;
                } else {
                    s_second_normal = normal;

                    s_second_limit = adjusted_d;
                }
            }
        } else if (crossed) {
            if (glm::dot(crease_a, s_entry_direction) < g_camera_snap_epsilon) {
                direction->SetZero();
                return false;
            }
            slide = ProjectVectorOntoVector(slide, crease_a);
            s_second_normal = normal;
            ++s_collision_state;

            s_second_limit = adjusted_d;
        }
    } else {
        s_first_normal = normal;
        s_first_surface = index;
        s_collision_state = 1;
        s_first_limit = adjusted_d;
    }
    *direction = slide;
    if (direction->Length() >= g_collision_direction_epsilon) {
        return true;
    }
    return false;
}

/* Whether `surface_index`'s triangle centroid sits farther from this surface's
   centroid than the from→to normal offset: used to tell genuinely different
   contact planes apart when wedging a slide. */
// FUNCTION: WIZ8 0x0041E8E0
bool W8GDSurface::CentroidsDiverging(int surface_index, const glm::vec3& from, const glm::vec3& to)
{
    const W8GDSurface* other = g_octree_game_data->m_pSurfaces.data() + surface_index;
    const srVector3T<float>* vertices = g_octree_game_data->m_pVertices.data();
    glm::vec3 this_centroid(g_float_zero, g_float_zero, g_float_zero);
    glm::vec3 other_centroid(g_float_zero, g_float_zero, g_float_zero);
    for (int vertex = 0; vertex < 3; ++vertex) {
        const srVector3T<float>* this_vertex = vertices + vertex_indices[vertex];
        this_centroid += this_vertex->glmValue();
        const srVector3T<float>* other_vertex = vertices + other->vertex_indices[vertex];
        other_centroid += other_vertex->glmValue();
    }
    const glm::vec3 delta =
        (this_centroid - other_centroid) * static_cast<float>(g_collision_centroid_scale);
    const glm::vec3 adjusted = delta + from - to;
    return glm::dot(delta, delta) >= glm::dot(adjusted, adjusted);
}

/* Environment contact response for a flag-4 (walkable) surface hit: pushes
   the slide back out of the plane by `depth`, removes the into-plane residual
   scaled by value_20 and the vertical attenuation, and reports the combined
   motion to the environ record. Direction tests against the surface normal
   decide whether any correction applies. */
// FUNCTION: WIZ8 0x0041EA90
bool W8GDSurface::ApplyEnvironContact(glm::vec3& direction)
{
    W8LevelDataRecord* level = g_level_data;
    if ((flags & W8_GD_SURFACE_WALKABLE) == 0) {
        level->motion_input = 0.0f;
        level->integrated_motion.SetZero();
        return false;
    }
    level->flags |= W8_LEVEL_FLAG_WALKABLE_CONTACT;
    level->sound_environment = footstep_surface;
    level->sound_environment_alt = footstep_material;
    glm::vec3 normal;
    normal = plane.normal.glmValue();
    float factor = g_environ->motion_factor;
    glm::vec3 slide = g_environ->vector.glmValue() * g_environ->scale;
    glm::vec3 unit = slide;
    unit = normalizedOrZero(unit);
    if (glm::dot(unit, normal) >= g_collision_opposed_normal_threshold) {
        glm::vec3 proj = slide;
        proj = ProjectVectorOntoVector(proj, normal);
        if (glm::dot(proj, normal) <= g_float_zero) {
            float depth = glm::length(proj);
            if (distance < depth) {
                depth = distance;
            }
            float attenuation = 0.25f;
            if (slide.y < g_float_zero) {
                attenuation = fabsf(slide.y) / glm::length(slide) * g_float_three_quarters +
                              g_navigator_vertical_phase_step;
            }
            glm::vec3 residual = slide - proj;
            residual *= factor * attenuation;
            slide += normal * depth;
            slide -= residual;
            direction -= residual * factor;
            g_environ->SetScaledMotion(slide);
            return true;
        }
        return false;
    }
    slide = glm::vec3(0);
    g_environ->vector = slide / g_environ->scale;
    g_environ->airborne = 1;
    return false;
}

/* VC6 vector constructor iterator, emitted for an ordinary array construction.
   This is compiler support, not an authored Wizardry callback wrapper. */

// FUNCTION: WIZ8 0x0041EEE0
void ResetLevelMovement(float movement_limit, bool reset, bool fast_move)
{
    W8LevelDataRecord* level = g_level_data;
    if (level != 0) {
        level->movement_limit = movement_limit;
        level->real_elapsed = 0.0f;
        level->frame_elapsed = 0.0f;
        level->movement_progress = 0.0f;
        level->camera_motion_velocity.SetZero();
        level->camera_motion_displacement.SetZero();
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

// FUNCTION: WIZ8 0x0041ef50
void ResetInactiveLevelDataVectors(void)
{
    W8LevelDataRecord* data = g_level_data;

    if (data != 0 && (data->flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0) {
        data->motion_input.SetZero();
        data->camera_motion_velocity.SetZero();
        data->camera_motion_displacement.SetZero();
        data->motion_velocity.SetZero();
        data->integrated_motion.SetZero();
        data->motion_displacement.SetZero();
    }
}

/* Fast movement is shared by camera motion, stamina and combat movement. */
// FUNCTION: WIZ8 0x0041efb0
bool IsLevelFastMovement(void)
{
    if (g_level_data != 0) {
        return (g_level_data->flags & W8_LEVEL_FLAG_FAST_MOVEMENT) != 0;
    }
    return false;
}

// FUNCTION: WIZ8 0x0041efd0
void ClearLevelFastMovement(void)
{
    if (g_level_data != 0) {
        g_level_data->flags &= ~W8_LEVEL_FLAG_FAST_MOVEMENT;
    }
}

// FUNCTION: WIZ8 0x0041efe0
void SetLevelFastMovement(void)
{
    if (g_level_data != 0) {
        g_level_data->flags |= W8_LEVEL_FLAG_FAST_MOVEMENT;
    }
}

// FUNCTION: WIZ8 0x0041eff0
bool LevelMovedThisUpdate(void)
{
    if (g_level_data != 0) {
        return (g_level_data->flags & W8_LEVEL_FLAG_MOVED_THIS_UPDATE) != 0;
    }
    return false;
}

/* Whether the last motion update found walkable-surface contact. */
// FUNCTION: WIZ8 0x0041f070
bool HasLevelWalkableContact(void)
{
    if (g_level_data != 0) {
        return (g_level_data->flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) != 0;
    }
    return false;
}

/* Clear active, stopped and reset movement state together. */
// FUNCTION: WIZ8 0x0041f0c0
void ClearLevelMovementState(void)
{
    if (g_level_data != 0) {
        g_level_data->flags &= ~W8_LEVEL_MOVEMENT_STATE_MASK;
    }
}

// FUNCTION: WIZ8 0x0041f140
bool IsLevelMovementStopped(void)
{
    if (g_level_data != 0) {
        return (g_level_data->flags & W8_LEVEL_FLAG_MOVEMENT_STOPPED) != 0;
    }
    return false;
}

// FUNCTION: WIZ8 0x0041f160
void ClearLevelMovementStopped(void)
{
    if (g_level_data != 0) {
        g_level_data->flags &= ~W8_LEVEL_FLAG_MOVEMENT_STOPPED;
    }
}

/* Hand the level's pending real/frame elapsed times to the caller, fold them
   into the session accumulators, clear the pending pair, and report whether
   either was above the camera-transition epsilon. */
// FUNCTION: WIZ8 0x0041f170
unsigned char ConsumeLevelElapsedTime(float* real_elapsed, float* frame_elapsed)
{
    W8LevelDataRecord* record = g_level_data;
    bool elapsed = false;
    if (record != 0) {
        *real_elapsed = record->real_elapsed;
        *frame_elapsed = record->frame_elapsed;
        elapsed = record->real_elapsed > g_camera_transition_epsilon ||
                  record->frame_elapsed > g_camera_transition_epsilon;
        record->frame_elapsed = 0.0f;
        record->real_elapsed = 0.0f;
        g_status.real_elapsed += *real_elapsed;
        g_status.frame_elapsed += *frame_elapsed;
    }
    return elapsed;
}

/* Options and save/load may interrupt motion while grounded or before
   the falling-state override has latched. Without a loaded level, it is refused. */
// FUNCTION: WIZ8 0x0041f090
bool CanInterruptLevelMovement(void)
{
    if (g_level_data == 0) {
        return false;
    }
    if ((g_level_data->flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) == 0 && g_level_override) {
        return false;
    }
    return true;
}

/* Whether the level has a live vector at 0x88: bit zero has to be up and at
   least one of the three floats has to differ from the default. */
// FUNCTION: WIZ8 0x0041f010
bool HasLevelDataVector(void)
{
    if (g_level_data == 0) {
        return false;
    }
    if ((g_level_data->flags & W8_LEVEL_FLAG_PROP_CONTACT) != 0 &&
        (g_level_data->contact_motion.x != g_float_zero ||
         g_level_data->contact_motion.y != g_float_zero ||
         g_level_data->contact_motion.z != g_float_zero)) {
        return true;
    }
    return false;
}

// GLOBAL: WIZ8 0x00652db4
W8EnvironRecord* g_environ;
// GLOBAL: WIZ8 0x00652dcc
bool g_level_data_teardown_flag;

/* The camera-sway mode halves navigator gravity, mirrors it into the active
   environment record and swaps the camera forward scale; the flag guards both
   transitions so repeated triggers are idempotent. */
// FUNCTION: WIZ8 0x0041a960
void BeginCameraSway(void)
{
    if (g_camera_sway_active) {
        return;
    }
    g_camera_forward_scale = g_camera_level_forward_scale;
    g_navigator_gravity = 93.75f;
    if (g_environ != 0) {
        g_environ->gravity_y = -93.75f;
    }
    g_camera_sway_active = true;
}

// FUNCTION: WIZ8 0x0041a9a0
void EndCameraSway(void)
{
    if (!g_camera_sway_active) {
        return;
    }
    g_camera_forward_scale = g_camera_default_forward_scale;
    g_navigator_gravity = 187.5f;
    if (g_environ != 0) {
        g_environ->gravity_y = -187.5f;
    }
    g_camera_sway_active = false;
}

/* Release the level-data record and its companion globals: free the 0xf4-byte
   record (whose destructor only tears down the +0xc4 interval gate), drop the
   shared game-time accumulator through its deleting destructor, and clear the
   environ, octree-data and secondary record pointers plus the teardown flag.
   ~W8GameData runs this first. */
// FUNCTION: WIZ8 0x0041a9e0
void W8GameData::ReleaseLevelData()
{
    g_environ = 0;
    if (g_level_data != 0) {
        delete g_level_data;
    }
    g_level_data = 0;
    g_level_flags = 0;
    if (g_game_time_accumulator != 0) {
        delete g_game_time_accumulator;
    }
    g_game_time_accumulator = 0;
    SetOctreeGameData(0);
    if (g_level_data_teardown_flag) {
        g_level_data_teardown_flag = false;
    }
}

// FUNCTION: WIZ8 0x0041AA40
void ResetCurrentEnvironment(void)
{
    if (g_environ != 0) {
        if (g_octree_game_data != 0 && !g_octree_game_data->m_ppEnvirons.empty()) {
            g_environ = g_octree_game_data->m_ppEnvirons[0].get();
        }
        g_environ->vector.SetZero();
        if (g_environment_load_flag != 0) {
            g_environ->motion_factor = 1.0f;
        }
        g_environment_load_flag = g_environment_load_flag == 0;
        if (g_environment_load_flag == 0) {
            g_level_override = false;
        }
        return;
    }
    g_environment_load_flag = 0;
    g_level_override = false;
}

// FUNCTION: WIZ8 0x0041AAE0
unsigned char SetEnvironmentLoadFlag(unsigned char flag)
{
    srVector3T<float> zero_vector(0.0f, 0.0f, 0.0f);
    unsigned char previous = g_environment_load_flag;
    if (g_environ != 0) {
        g_environ->vector = zero_vector;
        if (flag == 0) {
            g_environ->motion_factor = 1.0f;
        }
        g_environment_load_flag = flag;
    }
    return previous;
}

// FUNCTION: WIZ8 0x0041F0D0
void ResetLevelDataVectors(void)
{
    if (g_level_data != 0) {
        g_level_data->flags |= W8_LEVEL_FLAG_MOVEMENT_STOPPED;
        if ((g_level_data->flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0) {
            g_level_data->motion_input.SetZero();
            g_level_data->camera_motion_velocity.SetZero();
            g_level_data->motion_velocity.SetZero();
            g_level_data->integrated_motion.SetZero();
            g_level_data->camera_motion_displacement.SetZero();
            g_level_data->motion_displacement.SetZero();
        }
        g_level_data->flags &= ~W8_LEVEL_FLAG_FAST_MOVEMENT;
    }
}

/* Camera facade, move timer and the party placement entry. */

// GLOBAL: WIZ8 0x005ebc18
const double g_game_data_pi = 3.141592653589793;
// GLOBAL: WIZ8 0x005ebcf0
const float g_camera_radians_to_degrees = 57.295784f;
// GLOBAL: WIZ8 0x005ebca0
const float g_float_six = 6.0f;

// FUNCTION: WIZ8 0x0041FCE0
void GetLevelSoundEnvironment(char* environment, char* secondary)
{
    W8LevelDataRecord* level = g_level_data;
    if ((level->flags & W8_LEVEL_FLAG_NO_SOUND_ENVIRONMENT) != 0) {
        *secondary = -1;
        *environment = -1;
        return;
    }
    *environment = level->sound_environment;
    *secondary = level->sound_environment_alt;
}

// FUNCTION: WIZ8 0x00420b40
float MoveTimer(int value)
{
    if (g_game_time_accumulator == 0) {
        g_game_time_accumulator = new W8GameTimeAccumulator;
        if (g_game_time_accumulator == 0) {
            return g_float_zero;
        }
    }
    if (g_shared_timers_paused) {
        if ((value == 8 && g_current_screen_state.id == W8_SCREEN_MAIN_GAME) || value == 4) {
            ResumeSharedGameTimers();
            g_shared_timers_paused = false;
        } else {
            return g_float_zero;
        }
    }
    if (value == 1) {
        PauseSharedGameTimers();
        g_shared_timers_paused = true;
    }
    return g_game_time_accumulator->GetFrameDelta();
}

// FUNCTION: WIZ8 0x00420D40
srCamera* CreateOrSetGameCamera(srNode* parent, srCamera* camera)
{
    if (g_gd_camera == 0) {
        g_gd_camera = new GDCamera();
    }
    return g_gd_camera->CreateOrAttachCamera(parent, camera);
}

// FUNCTION: WIZ8 0x00420DC0
float GetCameraYawInDegrees()
{
    return g_gd_camera->m_yaw * g_camera_radians_to_degrees;
}

// FUNCTION: WIZ8 0x00420DD0
float GetCameraYawRadians()
{
    return g_gd_camera->m_yaw;
}

// FUNCTION: WIZ8 0x00420DE0
float GetCameraPitchInDegrees()
{
    return g_gd_camera->m_pitch * g_camera_radians_to_degrees;
}

// FUNCTION: WIZ8 0x00420DF0
float GetCameraPitchRadians()
{
    return g_gd_camera->m_pitch;
}

// FUNCTION: WIZ8 0x00420E00
void BeginManualCameraControl()
{
    g_gd_camera->SetManualControlActive(true);
}

/* 0x00420F40: camera yaw in whole degrees, plus an optional copy of the
   yaw-rotation matrix. The diagnostics dump passes null and uses only the
   yaw. */
// FUNCTION: WIZ8 0x00420F40
int GetCameraYawAndRotation(glm::mat3* rotation)
{
    float degrees = g_gd_camera->m_yaw * g_camera_radians_to_degrees;
    if (rotation != 0) {
        *rotation = g_gd_camera->m_yaw_rotation;
    }
    return static_cast<int>(degrees);
}

// FUNCTION: WIZ8 0x00420F70
void LevelCamera()
{
    g_gd_camera->BeginLeveling();
    g_mouselook_manual = false;
}

// FUNCTION: WIZ8 0x00420F90
void CameraLookAt(const srVector3T<float>* position)
{
    g_gd_camera->LookAt(position, false);
}

// FUNCTION: WIZ8 0x00420FB0
void CameraSnapToTarget(const srVector3T<float>* target)
{
    g_gd_camera->SnapToTarget(target);
}

// FUNCTION: WIZ8 0x00420FD0
void TurnCameraToDegrees(float degrees)
{
    double scale = g_game_data_pi * g_float_inverse_half_turn_degrees;
    g_gd_camera->BeginOrientationTransition(0.0f, static_cast<float>(scale * degrees), false);
}

// FUNCTION: WIZ8 0x00421000
void SetCameraYawDegrees(float degrees)
{
    double scale = g_game_data_pi * g_float_inverse_half_turn_degrees;
    g_gd_camera->SetOrientationImmediate(0.0f, static_cast<float>(scale * degrees));
}

// FUNCTION: WIZ8 0x00421030
void ApplyCameraRotation(glm::mat3* rotation)
{
    g_gd_camera->ApplyRotationMatrix(rotation, g_level_data);
}

// FUNCTION: WIZ8 0x00421070
void GetCameraPosition(srVector3T<float>* position)
{
    *position = g_gd_camera->m_position;
}

/* Zero the two six-float CamPos angle records, then store the live yaw in the
   first and the live pitch in the second. GetWorldCameraState passes the yaw
   record at +0x24 as angle and the pitch record at +0x0c as pitch. */
// FUNCTION: WIZ8 0x004213A0
void GetCameraOrientation(void* angle, void* pitch)
{
    W8CameraAngleRecord yaw_values = {}, pitch_values = {};
    yaw_values[0] = g_gd_camera->m_yaw;
    pitch_values[0] = g_gd_camera->m_pitch;
    memcpy(angle, yaw_values, sizeof(yaw_values));
    memcpy(pitch, pitch_values, sizeof(pitch_values));
}

// FUNCTION: WIZ8 0x004213E0
void SetCameraOrientation(void* angle, void* pitch, glm::mat3* rotation)
{
    float yaw_value, pitch_value;
    memcpy(&yaw_value, angle, sizeof(yaw_value));
    memcpy(&pitch_value, pitch, sizeof(pitch_value));
    g_gd_camera->SetYaw(yaw_value);
    g_gd_camera->SetPitch(pitch_value);
    memcpy(angle, &g_gd_camera->m_yaw, sizeof(yaw_value));
    memcpy(pitch, &g_gd_camera->m_pitch, sizeof(pitch_value));
    if (rotation != 0) {
        g_gd_camera->GetRotationMatrix(rotation);
    }
}

/* Project onto a direction; leave the vector unchanged for a degenerate target. */
// FUNCTION: WIZ8 0x00421440
glm::vec3 ProjectVectorOntoVector(const glm::vec3& vector, const glm::vec3& onto)
{
    const float length_squared = glm::dot(onto, onto);
    if (!(length_squared > g_vector_length_squared_epsilon))
        return vector;
    return glm::proj(vector, onto);
}

/* Apply saved yaw and pitch after a world reload. */
// FUNCTION: WIZ8 0x00421570
void RestoreWorldCameraOrientation(void* angle, void* pitch, W8World*)
{
    SetCameraOrientation(angle, pitch, nullptr);
}

// FUNCTION: WIZ8 0x00421550
int GetCameraYawDegrees(void)
{
    return static_cast<int>(glm::degrees(g_gd_camera->m_yaw));
}

/* Point-visibility query: returns whether the camera has line of sight to the
   given position through the world octree. Fails with no world loaded; with a
   world but no octree there is nothing to occlude, so it returns true. */
// FUNCTION: WIZ8 0x004215e0
bool HasCameraLineOfSight(const srVector3T<float>* position)
{
    srVector3T<float> to = *position;
    if (g_world == 0) {
        return false;
    }
    srVector3T<float> from;
    GetCameraPosition(&from);
    if (g_world->octree != 0) {
        return g_world->octree->HasLineOfSight(&from, &to, true);
    }
    return true;
}

/* Mark the renderer ready and copy the point into the game camera when it
   sits anywhere but the origin. */
// FUNCTION: WIZ8 0x00421090
void PlacePartyAtPoint(const srVector3T<float>* point)
{
    srVector3T<float> delta = *point - g_world_origin;
    if (delta.LengthSquared() != 0) {
        MarkRendererReady();
        g_gd_camera->m_position = point->glmValue();
    }
}

// FUNCTION: WIZ8 0x0041FD10
W8LevelDataRecord::W8LevelDataRecord() : interval_gate()
{
    flags = 0;
    camera_scale = 0;
    sound_environment = 0;
    sound_environment_alt = 0;
    residual_contact_length = 0.0f;
    contact_facing = 0.0f;
    speed = 0;
    real_elapsed = 0;
    frame_elapsed = 0;
    movement_limit = 0;
    movement_progress = 0;
    flag5 = false;
    flag6 = false;
    vertical_motion = 0;
    footstep_accumulator = -2000.0f;
    primary_contact_prop_id = -1;
    secondary_contact_prop_id = -1;
    camera_position.SetZero();
    motion_input.SetZero();
    camera_motion_velocity.SetZero();
    contact_velocity.SetZero();
    motion_velocity.SetZero();
    integrated_motion.SetZero();
    camera_motion_displacement.SetZero();
    contact_motion.SetZero();
    contact_displacement.SetZero();
    motion_displacement.SetZero();
    contact_normal.SetZero();
    /* 0x0041FDEF clears the whole 12-byte group from contact_normal_scale
       through unknown_bc in one run before raising the scale, so the scale's own
       zero is part of that run rather than a separate dead store. */
    memset(&contact_normal_scale, 0, sizeof(contact_normal_scale) + sizeof(unknown_bc));
    contact_normal_scale = 1.0f;
    g_level_override = false;
}

// FUNCTION: WIZ8 0x00420470
bool W8LevelDataRecord::IntegrateCameraForward()
{
    float forward_length;
    float limit;
    float delta_length;
    float scale;
    bool cleared_vector;
    srVector3T<float> adjustment;
    srVector3T<float> combined;

    cleared_vector = false;
    forward_length = camera_motion_velocity.Length();
    limit = g_environ->motion_limit * camera_scale;
    if (limit <= motion_input.Length()) {
        adjustment = camera_motion_velocity;
        adjustment = ProjectVectorOntoVector(adjustment.glmValue(), integrated_motion.glmValue());
        adjustment -= camera_motion_velocity;
        delta_length = adjustment.Length();
        limit = g_environ->motion_factor * limit;
        if (delta_length <= limit) {
            if (delta_length < limit * g_camera_snap_epsilon) {
                adjustment.SetZero();
            }
        } else {
            adjustment.SetLength(limit);
        }
        scale = g_environ->momentum_scale * g_environ->motion_factor;
        integrated_motion *= scale;
    } else {
        if (forward_length < limit) {
            motion_velocity.SetZero();
            return false;
        }
        limit = g_environ->motion_factor * limit;
        adjustment = -camera_motion_velocity;
        if (limit < forward_length) {
            adjustment.SetLength(limit);
        }
        integrated_motion.SetZero();
        cleared_vector = true;
    }

    combined = camera_motion_velocity + integrated_motion + adjustment;
    motion_velocity = combined;
    integrated_motion = combined;
    if (cleared_vector) {
        if (glm::dot(motion_input.glmValue(), adjustment.glmValue()) >
            g_camera_transition_epsilon) {
            motion_velocity.SetZero();
            return false;
        }
    }
    return true;
}

// FUNCTION: WIZ8 0x00420810
unsigned char W8LevelDataRecord::ApplySavedMotionMatrix(unsigned char prior_fast, bool fast_move,
                                                        const glm::mat3* saved)
{
    float horizontal;
    float clamp_scale;
    if (prior_fast != 0 && !fast_move) {
        motion_input.SetZero();
        integrated_motion.SetZero();
    }
    motion_input = (*saved) * motion_input.glmValue();
    integrated_motion = motion_input * camera_scale;
    if (!IntegrateCameraForward()) {
        return 0;
    }

    horizontal = glm::length(motion_velocity.xz());
    if (fast_move)
        prior_fast = 1;
    else if (horizontal <= g_camera_level_forward_scale)
        prior_fast = 0;
    clamp_scale = prior_fast ? g_camera_default_forward_scale : g_camera_level_forward_scale;
    if (horizontal > clamp_scale) {
        const auto limited = motion_velocity.xz() * (clamp_scale / horizontal);
        motion_velocity.x = limited.x;
        motion_velocity.z = limited.y;
    }
    const float vertical_limit =
        g_environment_load_flag ? g_camera_forward_scale : g_camera_level_forward_scale;
    motion_velocity.y = glm::clamp(motion_velocity.y, -vertical_limit, vertical_limit);
    motion_displacement = motion_velocity * camera_scale;
    return prior_fast;
}

// FUNCTION: WIZ8 0x00420A60
bool W8LevelDataRecord::UpdateFootstepFromMotion()
{
    float dx;
    float dy;
    float dz;
    float distance;
    bool large_radius;

    if ((flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0) {
        dx = camera_motion_displacement.x;
        dy = camera_motion_displacement.y;
        dz = camera_motion_displacement.z;
    } else {
        dx = camera_motion_displacement.x - contact_motion.x;
        dy = camera_motion_displacement.y - contact_motion.y;
        dz = camera_motion_displacement.z - contact_motion.z;
    }
    if ((flags & W8_LEVEL_FLAG_NO_SOUND_ENVIRONMENT) != 0 ||
        (flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) == 0) {
        return false;
    }
    distance = sqrtf(dx * dx + dy * dy + dz * dz) + footstep_accumulator;
    footstep_accumulator = distance;
    if (g_float_two_thousand < distance) {
        if (g_status.search_mode == 0 && (flags & W8_LEVEL_FLAG_FAST_MOVEMENT) == 0) {
            large_radius = false;
        } else {
            large_radius = true;
        }
        AlertCombatNoise(large_radius);
        if (sound_environment >= 0 && sound_environment_alt >= 0) {
            PlayFootstep(sound_environment, sound_environment_alt, W8_FOOTSTEP_KIND_STEP);
            while (g_float_two_thousand < footstep_accumulator) {
                footstep_accumulator -= g_float_two_thousand;
            }
        }
    }
    return true;
}

/* Flip the two props the last motion contact bound: the primary prop toggles
   only while its setting-6f2 latch is set, and the secondary follows when it
   is still set on the far side; a secondary that fails the check reports 0. */
// FUNCTION: WIZ8 0x0041FF00
unsigned char W8LevelDataRecord::ToggleBoundProps()
{
    W8Prop* prop;
    bool toggled = false;

    BeginPartyMovement();
    if (primary_contact_prop_id >= 0) {
        prop = (*g_world->collidable_props)[primary_contact_prop_id];
        if (prop->IsAnimationPingPong()) {
            prop->ReverseAnimationDirection();
            toggled = true;
            if (secondary_contact_prop_id >= 0) {
                prop = (*g_world->collidable_props)[secondary_contact_prop_id];
                if (!prop->IsAnimationPingPong()) {
                    return 0;
                }
                prop->ReverseAnimationDirection();
            }
        }
    }
    return toggled;
}

// FUNCTION: WIZ8 0x0041FF90
void W8LevelDataRecord::UpdateMotionProgress(unsigned char fast_move, unsigned char moved)
{
    bool allow_override;
    float length;
    float scale;
    srVector3T<float> projected;
    srVector3T<float> gravity;
    srVector3T<float> environ_vector;

    allow_override = true;
    if ((flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0) {
        contact_velocity.SetZero();
        contact_displacement.SetZero();
        if (g_float_zero < motion_velocity.y) {
            length = motion_velocity.Length();
            const float limit =
                fast_move ? g_camera_default_forward_scale : g_camera_level_forward_scale;
            if (length > limit) {
                motion_velocity.SetLength(limit);
                motion_displacement = motion_velocity * camera_scale;
            }
        }
    }
    if (moved == 0) {
        if ((flags & W8_LEVEL_FLAG_NO_SOUND_ENVIRONMENT) != 0 ||
            (flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) == 0) {
            allow_override = false;
        }
        camera_motion_velocity.SetZero();
        camera_motion_displacement.SetZero();
        footstep_accumulator = 1800.0f;
    } else {
        camera_motion_velocity = motion_velocity - contact_velocity;
        camera_motion_displacement = camera_motion_velocity * camera_scale;
        allow_override = UpdateFootstepFromMotion();
        camera_motion_velocity -= g_environ->vector;
        projected = contact_velocity;
        gravity.Set(g_environ->gravity_x, g_environ->gravity_y, g_environ->gravity_z);
        projected = ProjectVectorOntoVector(projected.glmValue(), gravity.glmValue());
        environ_vector = g_environ->vector;
        if (environ_vector.Length() < projected.Length()) {
            g_environ->vector = projected;
        }
        motion_input.SetZero();
        integrated_motion.SetZero();
    }

    motion_velocity = motion_displacement * (static_cast<float>(g_double_one) / camera_scale);
    speed = motion_displacement.Length();
    if (speed < g_camera_transition_epsilon) {
        speed = 0;
        motion_displacement.SetZero();
        motion_velocity.SetZero();
    }
    if ((flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE) == 0) {
        if ((flags & W8_LEVEL_FLAG_FAST_MOVEMENT) == 0) {
            real_elapsed += speed;
        } else {
            frame_elapsed += speed;
        }
    } else if ((flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) != 0) {
        if ((flags & W8_LEVEL_FLAG_FAST_MOVEMENT) == 0) {
            real_elapsed += speed;
        } else {
            frame_elapsed += speed;
        }
        scale = movement_progress;
        movement_progress = speed + scale;
        if (movement_limit < speed + scale) {
            camera_motion_velocity.SetZero();
            camera_motion_displacement.SetZero();
            footstep_accumulator = 2000.0f;
            flags |= W8_LEVEL_FLAG_MOVEMENT_STOPPED;
            if (gXStatus.fPartyMovementMode) {
                UpdateActivePartyMovement();
            }
        }
    }

    if (!g_camera_sway_active) {
        if (!g_level_override) {
            if (allow_override && vertical_motion < g_float_one_tenth) {
                g_level_override = true;
            }
        } else if (allow_override && g_float_thirty_five_hundredths < vertical_motion) {
            HandleLevelOverride(vertical_motion);
        }
        vertical_motion = -(motion_velocity.y / g_camera_motion_divisor);
        return;
    }
    vertical_motion = 0;
}

// FUNCTION: WIZ8 0x00420E20
void UpdateLevelMovementAudio(void)
{
    float now;

    if (g_level_data == 0) {
        return;
    }
    if ((g_gd_camera->m_orientation_flags & W8_CAMERA_YAW_MOVING) == 0 ||
        (g_level_data->flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) == 0 ||
        g_level_data->motion_velocity.x != g_float_zero ||
        g_level_data->motion_velocity.y != g_float_zero ||
        g_level_data->motion_velocity.z != g_float_zero) {
        if (g_level_footstep_sound != nullptr) {
            SoundSetFadeVolume(g_level_footstep_sound, 0, 500, 1);
            g_level_footstep_sound = nullptr;
        }
        return;
    }
    now = g_game_time_accumulator->GetElapsed();
    if (g_facing_tolerance0 < now - g_level_footstep_time &&
        (g_level_footstep_time = now,
         g_level_footstep_sound == nullptr || SoundIsPlaying(g_level_footstep_sound) == 0)) {
        g_level_footstep_sound =
            PlayFootstep(g_level_data->sound_environment, g_level_data->sound_environment_alt,
                         W8_FOOTSTEP_KIND_SCUFF);
    }
}
