#include "wiz8/entities/missile.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "retail_records.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/emitter_cycles.h"
#include "wiz8/entities/entity_assets.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/legacy_prop_save_ids.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/simulation_sink.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

constexpr float kCharacterProjectileHeight = 81.25f;
constexpr float kCharacterProjectileHeightStep = 32.5f;
constexpr double kMissileGravityAcceleration = 0.009800000000000001;

/* Advance a homing missile one AI step: run the trajectory predictor for the
   clamped half-tick delta, fold the returned advance into the elapsed clock,
   steer the representation while the path is unobstructed, and end the flight
   at expiry or an early-impact limit. */
unsigned char UpdateMissileAI(MissileFlight* record)
{
    Missile* missile;
    glm::vec3 position;
    glm::vec3 out;
    float advance;
    float remaining;
    float pitch;
    float yaw;
    glm::mat3 rotation;
    unsigned int count;
    unsigned int delta;

    if (record == 0) {
        return 0;
    }
    missile = record->missile;
    if (missile->impacting) {
        if (!missile->align_explosion) {
            return 1;
        }
        position = missile->nav.GetPosition();
        pitch = ElevationToTargetCPP(&position);
        position = missile->nav.GetPosition();
        yaw = HeadingToTargetCPP(&position);
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        missile->visual.SetRotation(&rotation);
        return 1;
    }
    position = missile->nav.GetPosition();
    count = GameClockMilliseconds() >> 1;
    delta = count - record->last_half_tick;
    if (delta > 0xfa) {
        delta = 0xfa;
    }
    record->last_half_tick = count;
    if (CurrentGame().advancement.held) {
        return 1;
    }
    advance = AdvanceMissileAI(record, &out, delta);
    remaining = missile->duration - record->elapsed + 1.0f;
    if (remaining <= advance) {
        advance = remaining;
    }
    if (record->limit > 0.0f) {
        remaining = record->limit - record->elapsed + 1.0f;
        if (remaining <= advance) {
            advance = remaining;
        }
    }
    record->elapsed = advance + record->elapsed;
    missile->SetCyclePosition(&out);
    if (!missile->nav.CheckNavigatorCollision(&position, &out) && missile->align_camera) {
        pitch = ElevationToTargetCPP(&out);
        yaw = HeadingToTargetCPP(&out);
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        missile->visual.SetRotation(&rotation);
    }
    if (advance + record->elapsed <= missile->duration) {
        if (record->limit > 0.0f && record->limit < advance + record->elapsed &&
            !missile->nav.CheckNavigatorCollision(&position, &out)) {
            missile->EnterImpactCycle();
        }
        return 1;
    }
    missile->flight_done = true;
    if (missile->missile_table_index == 0x23 &&
        (CurrentGame().combat.state() == 0 ||
         CurrentGame().combat.state()->missile_hit_result != W8_MISSILE_HIT_DEFLECTED)) {
        missile->DetonateMissileSpell();
    }
    if (Data().missiles[missile->missile_table_index].spell_missile != 0) {
        AbsorbMissileDamage(missile);
    }
    return 1;
}

/* Predict the homing missile's next position `steps` half-ticks ahead and
   re-aim its representation at that point. A ray through the world octree
   flags the record when a prop blocks the path and fires the prop's
   missile trigger. Returns the distance the step covered. */
float AdvanceMissileAI(MissileFlight* record, glm::vec3* out, unsigned int steps)
{
    Missile* missile;
    Prop* prop;
    glm::vec3 direction;
    glm::vec3 position;
    glm::mat3 rotation;
    float advance;
    float pitch;
    float yaw;

    if (steps == 0) {
        *out = record->missile->visual.location;
        return 0.0f;
    }
    advance = steps * record->speed_per_step;
    direction = record->missile->nav.GetVelocity();
    *out = direction;
    position = record->missile->nav.GetPosition();
    (*out) = glm::vec3(position.x + direction.x * advance, position.y + direction.y * advance,
                       position.z + direction.z * advance);
    if (GetWorld()->compiledIndex() != 0) {
        if (const auto hit = GetWorld()->compiledIndex()->traceProps(position, (*out), {})) {
            *out = hit->position;
            record->limit = 1.0f;
            prop = GetWorld()->collidable_props[hit->prop_id];
            prop->RunMissileTrigger(record);
        }
    }
    if (record->gravity) {
        float dx;
        float dy;
        float dz;

        record->fall_speed =
            record->fall_speed - steps * static_cast<float>(kMissileGravityAcceleration);
        position = record->missile->nav.GetPosition();
        if (out->y != position.y) {
            dx = record->missile->visual.location.x - out->x;
            dy = record->missile->visual.location.y - out->y;
            dz = record->missile->visual.location.z - out->z;
            if (sqrtf(dx * dx + dy * dy + dz * dz) != 0.0f) {
                pitch = GetElevationAngle(&record->missile->visual.location, out);
                yaw = GetHeadingAngle(&record->missile->visual.location, out);
                missile = record->missile;
                rotation = glm::mat3(1);
                rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
                rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
                missile->visual.SetRotation(&rotation);
                missile->nav.SetTargetYaw(yaw);
                missile->nav.SetTargetPitch(pitch);
            }
        }
    }
    return advance;
}

const char* g_missile_cycle_names[] = {"FLY", "EXPLODE"};

/* Index of the cycle whose name starts the PAC name, or -1 when none does. */
static int missileCycleIndex(const char* pac_name)
{
    for (int index = 0; index < 2; ++index) {
        if (SDL_strncasecmp(pac_name, g_missile_cycle_names[index],
                            strlen(g_missile_cycle_names[index])) == 0)
            return index;
    }
    return -1;
}

/* True while this missile is still an in-flight engagement that must finish
   before combat can end: either it has not been marked done, or its animation
   state for mode 6 is not the terminal value. */
bool Missile::BlocksEndingCombat()
{
    if (!flight_done) {
        if (GetAnimationState(W8_MISSILE_ANIMATION_CYCLE_INDEX) != 1) {
            return true;
        }
    }
    return false;
}

void GetCharacterProjectilePosition(unsigned int character_index, glm::vec3* position)
{
    glm::vec3 camera = cameraLocation();
    const glm::vec3 offset(
        (character_index & 1) ? -75.0f : 75.0f,
        kCharacterProjectileHeight - character_index * 0.5f * kCharacterProjectileHeightStep, 0);
    const auto yaw = GetCameraYawRadians() - g_monster_rotation_offset;
    const auto pitch = -partyCamera().m_pitch;
    *position = camera + glm::rotateY(glm::rotateX(offset, pitch), yaw);
}

static int g_missile_iterator;

/* Iterate the world's missile vector. A nonzero argument restarts the shared
   cursor; a missing world or vector answers null. */
Missile* NextMissile(bool restart)
{
    Missile* missile = 0;

    if (GetWorld() != 0) {
        if (restart) {
            g_missile_iterator = 0;
        }
        if (g_missile_iterator < static_cast<int>(GetWorld()->missiles.size())) {
            missile = GetWorld()->missiles[g_missile_iterator].get();
            ++g_missile_iterator;
        }
    }
    return missile;
}

/* Step every live missile and drop the finished ones.

   A missile still starting, or not yet marked for removal, detaches its
   representation, starts if needed, and updates in place; a finished one
   leaves the world collection and is destroyed. */
void UpdateWorldMissiles(World* world)
{
    if (world == 0) {
        Fail("pWorld");
    }
    int index = 0;
    int count = static_cast<int>(world->missiles.size());
    while (index < count) {
        Missile* missile = world->missiles[index].get();
        if (missile != 0) {
            missile->visual.DetachRepresentation(world);
            if (!missile->flight_done || missile->block_released == 0) {
                missile->StartIfHostActive();
                missile->visual.UpdateRepresentation(world, missile->nav);
                missile->nav.UpdateNavigation(
                    0, false,
                    world == GetWorld()
                        ? std::optional{BoundingSphere{PartyNavigator()->movement.position,
                                                       PartyNavigator()->movement.alternate_radius}}
                        : std::nullopt);
            } else {
                missile->visual.RetireParticles(*world);
                world->missiles.erase(world->missiles.begin() + index);
                --index;
                --count;
            }
        }
        ++index;
    }
}

/* Derive the two launch angles from the source and target, then forward the
   remaining launch values to the missile factory. */
Missile* FireMissile(unsigned int missile_table_index, glm::vec3* source, glm::vec3* target,
                     float flight_speed, unsigned int trace_mask, unsigned int block_released,
                     float duration)
{
    return CreateMissile(missile_table_index, source, GetHeadingAngle(source, target),
                         GetElevationAngle(source, target), flight_speed, trace_mask,
                         block_released, duration);
}

/* Instantiate the missile for `missile_table_index`: load or clone the cycle
   the table row names, then stamp the index, register the result on the world
   missile list and give it the default navigation bounds. */
Missile* AllocateMissile(int missile_table_index)
{
    auto owner = LoadMissileCycle(GetWorld(), Data().missiles[missile_table_index].cycle_name);
    auto* missile = owner.get();
    if (missile == 0) {
        return 0;
    }
    missile->missile_table_index = missile_table_index;
    missile->visual.pending_cycle = 0;
    missile->flight_done = false;
    if (missile->flight_done) {
        if (missile->missile_table_index == 0x23 &&
            (CurrentGame().combat.state() == 0 ||
             CurrentGame().combat.state()->missile_hit_result != W8_MISSILE_HIT_DEFLECTED)) {
            missile->DetonateMissileSpell();
        }
        if (Data().missiles[missile->missile_table_index].spell_missile != 0) {
            AbsorbMissileDamage(missile);
        }
    }
    missile->impacting = false;
    GetWorld()->missiles.push_back(std::move(owner));
    missile->nav.SetBounds({{-125.0f, -125.0f, -125.0f}, {125.0f, 125.0f, 125.0f}});
    missile->nav.active = true;
    return missile;
}

/* Load the cycle a missile record names. A name already on the GrCycle
   registry clones the registered template into a fresh Missile; otherwise
   the "data\\Missiles\\<name>.mls" script is parsed - its align/explode/
   gravity/velocity keywords set the missile's tail flags - and the cycle
   lines load through the ordinary GrCycle path. */
std::unique_ptr<Missile> LoadMissileCycle(World* world, std::string_view name)
{
    if (auto* found = findMissileAsset(*world, name)) {
        auto missile = std::make_unique<Missile>(*found, world);
        if (missile->flight_ai) missile->flight_ai->missile = missile.get();
        missile->asset_name = name;
        return missile;
    }
    PauseSharedGameTimers();
    auto output = std::make_unique<Missile>(world);
    output->visual.current_cycle = -1;
    auto handle = [&] {
        try {
            return wiz8::open_file(wiz8::asset_path(fmt::format("data/missiles/{}.mls", name)));
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (!handle) {
        const bool success = loadAnimationCycle(
            name, output.get()->visual, 0, 1, "Data\\Missiles",
            [&](LevelLoadContext& read_info, int read_cycle, int read_value) {
                return output.get()->ReadCycleData(&read_info, read_cycle, read_value) != 0;
            },
            "Data\\Spells\\Bitmaps");
        if (success) {
            output->asset_name = name;
        }
        ResumeSharedGameTimers();
        return success ? std::move(output) : nullptr;
    }
    bool gravity = false, align_camera = false, explode_ground = false, align_explosion = false;
    float velocity = 15000.0f;
    bool success = true;
    std::istringstream input(handle->read_text());
    std::string line;
    while (success && std::getline(input, line)) {
        std::istringstream fields(line);
        std::string command;
        if (!(fields >> command) || command.starts_with('#')) continue;
        if (SDL_strcasecmp(command.c_str(), "align_camera") == 0)
            align_camera = true;
        else if (SDL_strcasecmp(command.c_str(), "explode_ground") == 0)
            explode_ground = true;
        else if (SDL_strcasecmp(command.c_str(), "align_explosion") == 0)
            align_explosion = true;
        else if (SDL_strcasecmp(command.c_str(), "gravity") == 0)
            gravity = true;
        else if (command == "velocity") {
            if (!(fields >> velocity)) {
                success = false;
                break;
            }
        } else {
            bool cycle_line = false;
            for (int cycle = 0; cycle < 2; ++cycle) {
                if (SDL_strncasecmp(command.c_str(), g_missile_cycle_names[cycle],
                                    strlen(g_missile_cycle_names[cycle])) == 0) {
                    std::string resource;
                    if (!(fields >> resource)) {
                        success = false;
                        break;
                    }

                    success = loadAnimationCycle(
                        resource, output.get()->visual, cycle, 1, "Data\\Missiles",
                        [&](LevelLoadContext& read_info, int read_cycle, int read_value) {
                            return output.get()->ReadCycleData(&read_info, read_cycle,
                                                               read_value) != 0;
                        },
                        "Data\\Spells\\Bitmaps");
                    cycle_line = true;
                    break;
                }
            }
            if (cycle_line || !success) continue;
            std::string cycle_name;
            int frame = 0;
            if (SDL_strcasecmp(command.c_str(), "SHAKE_FRAME") == 0) {
                if (!output || !(fields >> cycle_name >> frame)) {
                    success = false;
                    break;
                }
                float intensity = 1.0f, duration = 1.0f, distance = 10.0f;
                fields >> intensity >> duration >> distance;
                ShakeDefinition effect(duration, true, intensity, distance * kWorldScale, 0);
                effect.cycle = missileCycleIndex(cycle_name.c_str());
                effect.frame = frame;
                effect.subcycle = 0;
                output->visual.AddShakeEffect(effect);
                continue;
            }
            W8SoundEventKind kind;
            if (SDL_strcasecmp(command.c_str(), "SOUND_FRAME") == 0)
                kind = W8_SOUND_EVENT_FRAME;
            else if (SDL_strcasecmp(command.c_str(), "SOUND_CYCLE") == 0)
                kind = W8_SOUND_EVENT_CYCLE;
            else
                continue;
            std::string resource, loop;
            if (!output || !(fields >> cycle_name >> frame >> resource)) {
                success = false;
                break;
            }
            fields >> loop;
            const auto wave = fmt::format("data/missiles/sounds/{}.WAV", resource);
            output->visual.AddSoundEvent(W8SoundEvent(kind, missileCycleIndex(cycle_name.c_str()),
                                                      frame, 0, wave.c_str(),
                                                      SDL_strcasecmp(loop.c_str(), "LOOP") == 0));
        }
    }
    if (output) {
        output->gravity = gravity;
        output->align_camera = align_camera;
        output->explode_ground = explode_ground;
        output->align_explosion = align_explosion;
        output->lifetime = velocity * kWorldScale;
        if (success) output->asset_name = name;
    } else
        success = false;
    ResumeSharedGameTimers();
    return success ? std::move(output) : nullptr;
}

/* Build the launch state for a fresh missile: rotate the forward direction by
   the heading and pitch, position and aim the navigator, probe the world
   octree for an early-impact limit, and attach the kind-3 AI record that
   drives the flight. */
Missile* CreateMissile(unsigned int missile_table_index, glm::vec3* source, float heading,
                       float pitch, float flight_speed, unsigned int trace_mask,
                       unsigned char block_released, float duration)
{
    Missile* missile;
    LevelSpatialIndex* octree;
    MissileFlight* ai;
    glm::vec3 direction(0.0f, 0.0f, 1.0f);
    glm::vec3 end;
    glm::mat3 rotation;
    glm::mat3 aim;
    float limit = -1.0f;

    missile = AllocateMissile(missile_table_index);
    if (missile != 0) {
        missile->visual.pending_cycle = 0;
        missile->SetCycle(0);
        missile->visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
        missile->visual.pending_subcycle = 0;
        missile->visual.subcycle = 0;
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(heading), glm::vec3(0, 1, 0)));
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        direction = rotation * direction;
        missile->nav.SetVelocity(&direction);
        missile->SetCyclePosition(source);
        aim = glm::mat3(1);
        aim *= glm::mat3_cast(glm::angleAxis(float(heading), glm::vec3(0, 1, 0)));
        aim *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        missile->visual.SetRotation(&aim);
        missile->nav.SetAngles(heading);
        missile->nav.SetPitch(pitch);
        octree = GetWorld()->compiledIndex();
        if (octree != 0) {
            end = glm::vec3(direction.x * duration, direction.y * duration, direction.z * duration);
            end = end + *source;
            if (const auto hit = octree->traceSight(*source, end, {.props = false})) {
                end = hit->point - *source;
                limit = glm::length(end);
                if (limit < 1.0f) {
                    limit = 1.0f;
                }
            }
        }
        if (flight_speed == 0.0f) {
            flight_speed = missile->lifetime;
        }
        ai = &missile->flight_ai.emplace();
        ai->speed_per_step = flight_speed * 0.001f;
        ai->gravity = missile->gravity;
        ai->limit = limit;
        ai->last_half_tick = GameClockMilliseconds() >> 1;
        ai->missile = missile;
        ai->fall_speed = static_cast<float>(sin(-pitch) * flight_speed * 0.001f);
        ai->limit = limit;
        missile->nav.trace_mask = trace_mask;
        missile->duration = duration;
        missile->block_released = block_released;
    }
    return missile;
}

/* The missile and spell representations use the same ordinary AnimObj
   operations for these two vtable slots.  Retail points both final tables at
   the corresponding bodies at 0x004AB290 and 0x004AB310. */
MeshInstance* Missile::SetCycleFrameLod(signed char emitter, signed char frame, signed char lod)
{
    return AnimObjDispatch(resources.cycles[emitter].animation.get(), lod, frame);
}

AnimationMesh* Missile::GetEmitterAniMesh(signed char emitter)
{
    Animation* target = resources.cycles[emitter].animation.get();

    if (target == 0) {
        return 0;
    }
    return AnimObjEntry(target, visual.m_bLOD, 0);
}

/* Apply the representation's current LOD to one required animation. */
unsigned int Missile::ApplyEmitterSetting(signed char emitter)
{
    Animation* target = resources.cycles[emitter].animation.get();

    if (target == 0) {
        Fail("pao");
    }
    return AnimObjValue(target, visual.m_bLOD);
}

unsigned char Missile::ReadCycleData(LevelLoadContext* info, int cycle_index, int)
{
    if (info == 0 || info->hFile == 0) {
        Fail("pInfo && pInfo->hFile");
    }
    return ReadEmitterCycle(visual, resources.cycles, info, cycle_index, true);
}

/* Retail constructs the +0x2d8 growable vector normally, then clears the
   complete +0x280..+0x321 tail after allocating the representation.  That
   overwrites the vector and leaks its initial five-pointer allocation; the
   ordering is preserved because it is present explicitly in the product. */
Missile::Missile(World* owner)
    : owner_world(owner), nav(owner), visual(owner), missile_table_index(-1), flight_done(0),
      impacting(0), block_released(1), gravity(0), align_camera(0), explode_ground(0),
      align_explosion(0), flag(1), lifetime(15000.0f), flags0(0), retargeted(false)
{
    nav.radius = 1.0f;
    nav.movement.collision_radius = 1.0f;
    nav.movement.alternate_radius = 1.0f;
    nav.collision = [this](Navigator* other) { return OnCollision(other); };
    AllocateLegacyPropSaveId();

    result = {};
    ResetCombatSlot(&combat_slot);
}

/* The clone path reuses the registered template's flight configuration but
   drops its in-flight state: the detonate/hit flags clear, duration
   resets, and the AI record is not copied. Retail still clears the complete
   +0x280..+0x321 tail after result's member construction, leaking the
   growable vector's initial allocation exactly as the default constructor
   does. */
Missile::Missile(const Missile& other, World* owner)
    : owner_world(owner), nav(other.nav, owner), visual(other.visual, owner),
      missile_table_index(other.missile_table_index), resources(other.resources), flight_done(0),
      impacting(0), block_released(other.block_released), gravity(other.gravity),
      align_camera(other.align_camera), explode_ground(other.explode_ground),
      align_explosion(other.align_explosion), flag(other.flag), lifetime(other.lifetime),
      flags0(other.flags0), duration(0.0f), retargeted(false)
{
    nav.collision = [this](Navigator* other) { return OnCollision(other); };
    AllocateLegacyPropSaveId();
    asset_name = other.asset_name;
    if (resources.cycles[visual.current_cycle].animation)
        visual.active_animation = resources.cycles[visual.current_cycle].animation.get();
    flight_ai = other.flight_ai;
    if (flight_ai) flight_ai->missile = this;

    result = {};
    ResetCombatSlot(&combat_slot);
}

/* Mode-keyed query over the representation's emitter/cycle state; used by the
   missile script handlers. */
// FUNCTION: WIZ8 0x004A4640
std::uint32_t Missile::GetAnimationState(W8MissileAnimationStateQuery query)
{
    switch (query) {
    case W8_MISSILE_ANIMATION_EMITTER_SETTING:
        return ApplyEmitterSetting(visual.current_cycle);
    case W8_MISSILE_ANIMATION_TOTAL_COUNT:
        return GetTotalAnimationCount();
    case W8_MISSILE_ANIMATION_AT_FINAL_SUBCYCLE:
        return visual.subcycle == ApplyEmitterSetting(visual.current_cycle) - 1;
    case W8_MISSILE_ANIMATION_AT_INITIAL_SUBCYCLE:
        return visual.subcycle == 0;
    case W8_MISSILE_ANIMATION_SUBCYCLE_INDEX:
        return visual.subcycle;
    case W8_MISSILE_ANIMATION_HAS_EMITTER:
        return ApplyEmitterSetting(visual.current_cycle) != 0xffffffff;
    case W8_MISSILE_ANIMATION_CYCLE_INDEX:
        return visual.current_cycle;
    case W8_MISSILE_ANIMATION_FINISHED:
        return visual.animation_playing == 0;
    default:
        return 0xffffffff;
    }
}

/* Release the representation and every external reference before ordinary
   vector and GrCycle teardown. */
Missile::~Missile()
{
    visual.SetLights(0);
    DetachMissileReferences(this);
}

/* Start or advance the missile while its launcher is live.

   When no scripted phase claims it, the ordinary path hands control to the
   missile's AI and ticks the animation. A scripted missile instead records
   the start, fires the one-shot launch for its table kind, and lets the
   combat boundary consume the animation once its table flag is set. */
void Missile::StartIfHostActive()
{
    if (!visual.rep_active) {
        return;
    }
    if (GetAnimationState(W8_MISSILE_ANIMATION_AT_FINAL_SUBCYCLE) == 0 || !impacting) {
        if (flight_ai) {
            UpdateMissileAI(&*flight_ai);
        } else if (visual.m_pAI.has_value()) {
            PathAIUpdate((visual.m_pAI ? &*visual.m_pAI : nullptr), 1);
        }
        TickAnimation(1.0f);
    } else {
        flight_done = true;
        if (missile_table_index == 0x23 &&
            (CurrentGame().combat.state() == 0 ||
             CurrentGame().combat.state()->missile_hit_result != W8_MISSILE_HIT_DEFLECTED)) {
            DetonateMissileSpell();
        }
        if (Data().missiles[missile_table_index].spell_missile) {
            AbsorbMissileDamage(this);
        }
    }
}

/* Copy the effect definition, then replace its radius from the selected
   0x1e5-byte missile database row. */
void Missile::SetEffectDefinition(const W8SpellEffectDefinition* definition)
{
    this->definition = *definition;
    this->definition.radius = Data().missiles[missile_table_index].radius;
}

/* The animation the representation is currently firing from. */
Animation* Missile::GetCurrentAnimation()
{
    return resources.cycles[visual.current_cycle].animation.get();
}

/* That animation's own playback scale. */
float Missile::GetCurrentAnimationScale()
{
    return resources.cycles[visual.current_cycle].animation->playback_scale;
}

AnimationMesh* Missile::GetCurrentAniMesh()
{
    Animation* animation = resources.cycles[visual.current_cycle].animation.get();

    if (animation == 0) {
        Fail("pao");
    }
    return animation->entries[visual.m_bLOD].get();
}

/* How many emitters the launcher has, counted by testing each for null rather
   than read from a stored count. */
signed char Missile::GetTotalAnimationCount()
{
    signed char count = 0;

    if (resources.cycles[0].animation != 0) {
        count = 1;
    }

    if (resources.cycles[1].animation != 0) {
        ++count;
    }
    return count;
}

/* The missile adds no work around the ordinary GrCycle update. */

/* Hand the representation's LOD to the current animation. */
signed char Missile::GetNumSubCycles()
{
    Animation* animation = GetCurrentAnimation();

    return static_cast<signed char>(AnimObjValue(animation, visual.m_bLOD));
}

bool Missile::IsCycleSupported(signed char cycle)
{
    if (cycle >= 2) {
        Fail("bCycle<MISSILE_NUM_CYCLES");
    }
    return resources.cycles[cycle].animation != 0;
}

/* Select one of the missile representation's two emitters and rebuild its
   light and particle attachment state. */
/* 0x004A4440 is a split address inside this body: it resumes at the
   animation-record field copy after the timestamp read, not a separate
   authored function. */
void Missile::SetCycle(signed char cycle)
{
    std::vector<std::unique_ptr<Light>>* lights;

    if (cycle < 0 || cycle >= 2) {
        Fail("bCycle >= MISSILE_CYCLE_FIRST && bCycle <= MISSILE_CYCLE_LAST");
    }

    lights = resources.cycles[visual.current_cycle].lightView();
    visual.DetachCycleLights(lights);

    StartEmitterCycle(visual, *resources.cycles[cycle].animation, cycle);

    lights = resources.cycles[cycle].lightView();
    visual.SetLights(lights);
    visual.AttachCycleLights(lights);

    if (!visual.particles.empty()) {
        int count = static_cast<int>(visual.particles.size());
        for (int index = 0; index < count; ++index) {
            ParticleAttachment* event = &visual.particles[index];
            event->SelectCycle(cycle);
        }
    }
}

/* Reset the launcher's two counters at 0x94 and 0x95. The second is one less
   than the missile's own virtual answer, and the launcher pointer is taken
   before the virtual call rather than after. */

/* Cast spell 0x83 at the missile's own position on behalf of the character
   that fired it - the detonation a stored-spell missile (table index 0x23)
   releases once its flight ends. */
void Missile::DetonateMissileSpell()
{
    W8TargetSource source;
    W8CombatSlot target;
    glm::vec3 position;

    if (m_Source.iType != W8_TARGET_SOURCE_CHARACTER) {
        Fail("m_Source.iType == SOURCE_TYPE_CHAR");
    }
    position = nav.GetPosition();
    ResetTargetSource(&source);
    source.iChar = m_Source.iChar;
    source.iType = W8_TARGET_SOURCE_CHARACTER;
    source.point = position;
    source.aim_resolved = true;
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    target.point = position;
    CastSpellFromSource(W8_SPELL_ROCKET_BLAST, &source, &target, 1, 0, 0, false, 0,
                        W8SpellCastKind::Standard, 0, 0);
}

/* Post "<source> hits <target>" to the notice box and colour the target's
   name with the target side's colour when it differs from the source's. */
void Missile::AnnounceCollisionTarget()
{
    std::string text;
    unsigned char target_start;
    unsigned char target_stop;
    unsigned char source_color;
    unsigned char target_color;

    if (!CurrentGame().combat.active()) {
        return;
    }
    text = fmt::format("{} ", Data().strings[0x1bf]);
    target_start = text.size();
    if (combat_slot.iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* monster_info = FindMonster(combat_slot.iMonsterID);
        text += GetMonsterName(monster_info, 0, 0);
    } else {
        text += CurrentGame().party.characters[combat_slot.iChar].name;
    }
    target_stop = text.size();
    source_color = GetSourceNoticeColor(&m_Source);
    target_color = GetTargetNoticeColor(&m_Source, &combat_slot);
    text += " ";
    text += Data().strings[0x1c0];
    ShowNotice(source_color, text);
    if (target_color != source_color) {
        HighlightTextBoxRange(target_color, target_start, target_stop, -1);
    }
}

void Missile::EnterImpactCycle()
{
    if (IsCycleSupported(1)) {
        if (GetAnimationState(W8_MISSILE_ANIMATION_CYCLE_INDEX) != 1) {
            glm::vec3 position = visual.location;
            visual.pending_cycle = 1;
            visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
            impacting = true;
            if (explode_ground) {
                visual.location.y = probeGround(position).position.y;
            }
        }
    } else {
        flight_done = true;
        if (missile_table_index == 0x23 &&
            (CurrentGame().combat.state() == 0 ||
             CurrentGame().combat.state()->missile_hit_result != W8_MISSILE_HIT_DEFLECTED)) {
            DetonateMissileSpell();
        }
        if (Data().missiles[missile_table_index].spell_missile) {
            AbsorbMissileDamage(this);
        }
    }
}

bool Missile::OnCollision(Navigator* other)
{
    if (!owner_world || !owner_world->simulation ||
        !owner_world->simulation->missileReached(*this, *other)) {
        return false;
    }
    EnterImpactCycle();
    nav.active = false;
    return true;
}

/* glm::mat3::RotateAboutX emitted for this TU; the primary template
   lives in srMath.h. */

void Missile::TickAnimation(float time_scale)
{
    ApplyPendingCycle();
    if (!visual.animation_playing) return;
    visual.TickAnimation(
        resources.cycles[visual.current_cycle].animation->playback_scale * time_scale,
        nav.GetPosition(), true,
        [this]([[maybe_unused]] unsigned char previous_frame) { return ApplyPendingCycle(); });
}

bool Missile::ApplyPendingCycle()
{
    if (visual.pending_cycle == -1) return false;
    SetCycle(visual.pending_cycle);
    visual.finishCycleSelection(nav.GetPosition());
    return true;
}

void Missile::SetCyclePosition(glm::vec3* position)
{
    visual.SetLocation(position);
    nav.SetPositionInternal(position);
}
