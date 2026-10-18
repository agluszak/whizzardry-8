#include "wiz8/engine_code/Missile.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
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

#include "GDCamera.h"
#include "GrCycle.h"
#include "Navigator.h"
#include "asset_paths.h"
#include "compat/compiler.h"
#include "gameplay_modifiers.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "random.h"
#include "retail_records.h"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/Emitter.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/emitter_cycles.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/spell_ids.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

constexpr float kCharacterProjectileHeight = 81.25f;
constexpr float kCharacterProjectileHeightStep = 32.5f;
constexpr double kMissileGravityAcceleration = 0.009800000000000001;

std::unique_ptr<W8AIMissile> W8AIMissile::Clone() const
{
    auto copy = std::make_unique<W8AIMissile>(*this);
    copy->missile = nullptr;
    return copy;
}

/* Advance a homing missile one AI step: run the trajectory predictor for the
   clamped half-tick delta, fold the returned advance into the elapsed clock,
   steer the representation while the path is unobstructed, and end the flight
   at expiry or an early-impact limit. */
unsigned char UpdateMissileAI(W8AIMissile* record)
{
    W8Missile* missile;
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
        position = missile->GetPosition();
        pitch = ElevationToTargetCPP(&position);
        position = missile->GetPosition();
        yaw = HeadingToTargetCPP(&position);
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        missile->m_pRep->SetRotation(&rotation);
        return 1;
    }
    position = missile->GetPosition();
    count = GameClockMilliseconds() >> 1;
    delta = count - record->last_half_tick;
    if (delta > 0xfa) {
        delta = 0xfa;
    }
    record->last_half_tick = count;
    if (gXStatus.world_update_blocked) {
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
    if (!missile->CheckNavigatorCollision(&position, &out) && missile->align_camera) {
        pitch = ElevationToTargetCPP(&out);
        yaw = HeadingToTargetCPP(&out);
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        missile->m_pRep->SetRotation(&rotation);
    }
    if (advance + record->elapsed <= missile->duration) {
        if (record->limit > 0.0f && record->limit < advance + record->elapsed &&
            !missile->CheckNavigatorCollision(&position, &out)) {
            missile->EnterImpactCycle();
        }
        return 1;
    }
    missile->flight_done = true;
    if (missile->missile_table_index == 0x23 &&
        (g_combat_state == 0 || g_combat_state->missile_hit_result != W8_MISSILE_HIT_DEFLECTED)) {
        missile->DetonateMissileSpell();
    }
    if (g_missile_table[missile->missile_table_index].spell_missile != 0) {
        AbsorbMissileDamage(missile);
    }
    return 1;
}

/* Predict the homing missile's next position `steps` half-ticks ahead and
   re-aim its representation at that point. A ray through the world octree
   flags the record when a prop blocks the path and fires the prop's
   missile trigger. Returns the distance the step covered. */
float AdvanceMissileAI(W8AIMissile* record, glm::vec3* out, unsigned int steps)
{
    W8MissileRep* representation;
    W8Missile* missile;
    W8Prop* prop;
    glm::vec3 direction;
    glm::vec3 position;
    glm::mat3 rotation;
    float advance;
    float pitch;
    float yaw;

    representation = record->missile->m_pRep.get();
    if (steps == 0) {
        *out = representation->location;
        return 0.0f;
    }
    advance = steps * record->speed_per_step;
    direction = record->missile->GetVelocity();
    *out = direction;
    position = record->missile->GetPosition();
    (*out) = glm::vec3(position.x + direction.x * advance, position.y + direction.y * advance,
                       position.z + direction.z * advance);
    if (GetWorld()->octree != 0) {
        if (const auto hit = GetWorld()->octree->traceProps(position, (*out), 0, 0)) {
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
        position = record->missile->GetPosition();
        if (out->y != position.y) {
            dx = representation->location.x - out->x;
            dy = representation->location.y - out->y;
            dz = representation->location.z - out->z;
            if (sqrtf(dx * dx + dy * dy + dz * dz) != 0.0f) {
                pitch = GetElevationAngle(&representation->location, out);
                yaw = GetHeadingAngle(&representation->location, out);
                missile = record->missile;
                rotation = glm::mat3(1);
                rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
                rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
                missile->m_pRep->SetRotation(&rotation);
                missile->SetTargetYaw(yaw);
                missile->SetTargetPitch(pitch);
            }
        }
    }
    return advance;
}

std::vector<W8MissileTableRecord> g_missile_table;

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
/* Engine Code\\Missile.cpp's startup database load.  Each disk row has a
   0x101-byte editor prefix followed by the 0x1e5-byte runtime record. */
unsigned char LoadMissileDatabase(void)
try {
    auto handle = wiz8::open_file(wiz8::asset_path("Data\\Databases\\MissileTables.dbs"));
    std::int32_t count;
    std::uint32_t version;
    handle->read_exact(&count, sizeof(count));
    handle->read_exact(&version, sizeof(version));
    if (count < 0 || count > 100000) return false;
    std::vector<W8MissileTableRecord> rows(count);
    for (std::int32_t index = 0; index < count; ++index) {
        handle->seek(0x101, wiz8::SeekOrigin::current);
        if (wiz8::retail::read(*handle, rows[index]).bytes !=
            wiz8::retail::kMissileTableRecordBytes)
            return false;
    }
    g_missile_table = std::move(rows);
    return true;
} catch (const std::exception&) {
    return false;
}

/* True while this missile is still an in-flight engagement that must finish
   before combat can end: either it has not been marked done, or its animation
   state for mode 6 is not the terminal value. */
bool W8Missile::BlocksEndingCombat()
{
    if (!flight_done) {
        if (GetAnimationState(6) != 1) {
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
    const auto pitch = -PartyCamera().m_pitch;
    *position = camera + glm::rotateY(glm::rotateX(offset, pitch), yaw);
}

static int g_missile_iterator;

/* Iterate the world's missile vector. A nonzero argument restarts the shared
   cursor; a missing world or vector answers null. */
W8Missile* NextMissile(bool restart)
{
    W8Missile* missile = 0;

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
void UpdateWorldMissiles(W8World* world)
{
    if (world == 0) {
        Fail("pWorld");
    }
    int index = 0;
    int count = static_cast<int>(world->missiles.size());
    while (index < count) {
        W8Missile* missile = world->missiles[index].get();
        if (missile != 0) {
            missile->DetachRepresentation(world);
            if (!missile->flight_done || missile->block_released == 0) {
                missile->StartIfHostActive();
                missile->UpdateRepresentation(world);
                missile->UpdateNavigation(0, false);
            } else {
                missile->RetireParticles(*world);
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
W8Missile* FireMissile(unsigned int missile_table_index, glm::vec3* source, glm::vec3* target,
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
W8Missile* AllocateMissile(int missile_table_index)
{
    auto owner = LoadMissileCycle(GetWorld(), g_missile_table[missile_table_index].cycle_name);
    auto* missile = owner.get();
    if (missile == 0) {
        return 0;
    }
    missile->missile_table_index = missile_table_index;
    missile->m_pRep->pending_cycle = 0;
    missile->flight_done = false;
    if (missile->flight_done) {
        if (missile->missile_table_index == 0x23 &&
            (g_combat_state == 0 ||
             g_combat_state->missile_hit_result != W8_MISSILE_HIT_DEFLECTED)) {
            missile->DetonateMissileSpell();
        }
        if (g_missile_table[missile->missile_table_index].spell_missile != 0) {
            AbsorbMissileDamage(missile);
        }
    }
    missile->impacting = false;
    GetWorld()->missiles.push_back(std::move(owner));
    missile->SetBounds({{-125.0f, -125.0f, -125.0f}, {125.0f, 125.0f, 125.0f}});
    missile->active = true;
    return missile;
}

/* Load the cycle a missile record names. A name already on the GrCycle
   registry clones the registered template into a fresh W8Missile; otherwise
   the "data\\Missiles\\<name>.mls" script is parsed - its align/explode/
   gravity/velocity keywords set the missile's tail flags - and the cycle
   lines load through the ordinary GrCycle path. */
std::unique_ptr<W8Missile> LoadMissileCycle(W8World* world, std::string_view name)
{
    if (auto* found = FindFirstGrCycleByName(name)) {
        auto missile = std::make_unique<W8Missile>(*static_cast<W8Missile*>(found), world);
        if (!missile->flight_ai) Fail("missile AI");
        missile->flight_ai->missile = missile.get();
        RegisterGrCycle(name, missile.get());
        return missile;
    }
    PauseSharedGameTimers();
    auto output = std::make_unique<W8Missile>(world);
    output->m_pRep->current_cycle = -1;
    auto handle = [&] {
        try {
            return wiz8::open_file(wiz8::asset_path(fmt::format("data/missiles/{}.mls", name)));
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (!handle) {
        const bool success =
            LoadGrCycle(name, output.get(), 0, 1, "Data\\Missiles", 1, "Data\\Spells\\Bitmaps");
        if (success) {
            RegisterGrCycle(name, output.get());
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

                    success = LoadGrCycle(resource, output.get(), cycle, 1, "Data\\Missiles", 1,
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
                output->AddShakeEffect(effect);
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
            output->AddSoundEvent(std::make_unique<W8SoundEvent>(
                kind, missileCycleIndex(cycle_name.c_str()), frame, 0, wave.c_str(),
                SDL_strcasecmp(loop.c_str(), "LOOP") == 0));
        }
    }
    if (output) {
        output->gravity = gravity;
        output->align_camera = align_camera;
        output->explode_ground = explode_ground;
        output->align_explosion = align_explosion;
        output->lifetime = velocity * kWorldScale;
        if (success) RegisterGrCycle(name, output.get());
    } else
        success = false;
    ResumeSharedGameTimers();
    return success ? std::move(output) : nullptr;
}

/* Build the launch state for a fresh missile: rotate the forward direction by
   the heading and pitch, position and aim the navigator, probe the world
   octree for an early-impact limit, and attach the kind-3 AI record that
   drives the flight. */
W8Missile* CreateMissile(unsigned int missile_table_index, glm::vec3* source, float heading,
                         float pitch, float flight_speed, unsigned int trace_mask,
                         unsigned char block_released, float duration)
{
    W8Missile* missile;
    W8Octree* octree;
    W8AIMissile* ai;
    glm::vec3 direction(0.0f, 0.0f, 1.0f);
    glm::vec3 end;
    glm::mat3 rotation;
    glm::mat3 aim;
    float limit = -1.0f;

    missile = AllocateMissile(missile_table_index);
    if (missile != 0) {
        missile->m_pRep->pending_cycle = 0;
        missile->SetCycle(0);
        missile->m_pRep->pending_behaviour = W8_ANIMATION_NEVER_STOP;
        missile->m_pRep->pending_subcycle = 0;
        missile->m_pRep->subcycle = 0;
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(heading), glm::vec3(0, 1, 0)));
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        direction = rotation * direction;
        missile->SetVelocity(&direction);
        missile->SetCyclePosition(source);
        aim = glm::mat3(1);
        aim *= glm::mat3_cast(glm::angleAxis(float(heading), glm::vec3(0, 1, 0)));
        aim *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        missile->m_pRep->SetRotation(&aim);
        missile->SetAngles(heading);
        missile->SetPitch(pitch);
        octree = GetWorld()->octree.get();
        if (octree != 0) {
            end = glm::vec3(direction.x * duration, direction.y * duration, direction.z * duration);
            end = end + *source;
            if (octree->TraceLineOfSight(source, &end, false, -3, -3, true, 0) != 0) {
                end -= *source;
                limit = glm::length(end);
                if (limit < 1.0f) {
                    limit = 1.0f;
                }
            }
        }
        if (flight_speed == 0.0f) {
            flight_speed = missile->lifetime;
        }
        ai = new W8AIMissile{};
        ai->speed_per_step = flight_speed * 0.001f;
        ai->gravity = missile->gravity;
        ai->limit = limit;
        ai->last_half_tick = GameClockMilliseconds() >> 1;
        ai->missile = missile;
        missile->flight_ai.reset(ai);
        ai->fall_speed = static_cast<float>(sin(-pitch) * flight_speed * 0.001f);
        ai->limit = limit;
        missile->trace_mask = trace_mask;
        missile->duration = duration;
        missile->block_released = block_released;
    }
    return missile;
}

/* The missile and spell representations use the same ordinary AnimObj
   operations for these two vtable slots.  Retail points both final tables at
   the corresponding bodies at 0x004AB290 and 0x004AB310. */
srModelInstance* W8MissileRep::SetCycleFrameLod(signed char emitter, signed char frame,
                                                signed char lod)
{
    return AnimObjDispatch(resources.cycles[emitter].animation.get(), lod, frame);
}

W8AniMesh* W8MissileRep::GetEmitterAniMesh(signed char emitter)
{
    W8AnimObj* target = resources.cycles[emitter].animation.get();

    if (target == 0) {
        return 0;
    }
    return AnimObjEntry(target, m_bLOD, 0);
}

/* Apply the representation's current LOD to one required animation. */
unsigned int W8MissileRep::ApplyEmitterSetting(signed char emitter)
{
    W8AnimObj* target = resources.cycles[emitter].animation.get();

    if (target == 0) {
        Fail("pao");
    }
    return AnimObjValue(target, m_bLOD);
}

/* The two emitter slots start empty and at the source default playback value.
   Construction of the two light-list vectors is ordinary array-member
   construction and precedes these assignments in the retail body. */
W8MissileRep::W8MissileRep() = default;

/* Copy the two proven representation values, clone each animation, and deep
   copy every optional light vector. The copied lights are ordinary scene
   objects registered with the world but detached until the missile selects
   their emitter. */
W8MissileRep::W8MissileRep(const W8MissileRep& other) = default;

unsigned char W8MissileRep::ReadCycleData(W8ReadLevelInfo* info, W8Missile* missile,
                                          int cycle_index, int)
{
    if (info == 0 || info->hFile == 0 || missile == 0) {
        Fail("pInfo && pInfo->hFile && pMissile");
    }
    return ReadEmitterCycle(*this, resources.cycles, info, *missile, cycle_index,
                            missile->m_pRep != nullptr);
}

/* Retail constructs the +0x2d8 growable vector normally, then clears the
   complete +0x280..+0x321 tail after allocating the representation.  That
   overwrites the vector and leaks its initial five-pointer allocation; the
   ordering is preserved because it is present explicitly in the product. */
W8Missile::W8Missile(W8World* owner)
    : W8GrCycle(owner), missile_table_index(-1), flight_done(0), impacting(0), block_released(1),
      gravity(0), align_camera(0), explode_ground(0), align_explosion(0), flag(1), value_1e8(0),
      value_1ec(0), lifetime(15000.0f), flags0(0), retargeted(false)
{
    W8GrObject::kind = 1;
    radius = 1.0f;
    movement.collision_radius = 1.0f;
    movement.alternate_radius = 1.0f;
    id = AllocateGrObjectId();

    m_pRep = std::make_unique<W8MissileRep>();
    if (m_pRep == 0) {
        Fail("m_pRep");
    }

    memset(&definition, 0, sizeof(definition));
    result = {};
    ResetCombatSlot(&combat_slot);
}

/* The clone path reuses the registered template's flight configuration but
   drops its in-flight state: the detonate/hit flags clear, duration
   resets, and the AI record is not copied. Retail still clears the complete
   +0x280..+0x321 tail after result's member construction, leaking the
   growable vector's initial allocation exactly as the default constructor
   does. */
W8Missile::W8Missile(const W8Missile& other, W8World* owner)
    : W8GrCycle(other, owner), missile_table_index(other.missile_table_index), flight_done(0),
      impacting(0), block_released(other.block_released), gravity(other.gravity),
      align_camera(other.align_camera), explode_ground(other.explode_ground),
      align_explosion(other.align_explosion), flag(other.flag), value_1e8(other.value_1e8),
      value_1ec(other.value_1ec), lifetime(other.lifetime), flags0(other.flags0), duration(0.0f),
      retargeted(false)
{
    W8GrObject::kind = 1;
    id = AllocateGrObjectId();
    m_pRep = std::make_unique<W8MissileRep>(*other.m_pRep);
    flight_ai = other.flight_ai ? other.flight_ai->Clone() : nullptr;

    memset(&definition, 0, sizeof(definition));
    result = {};
    ResetCombatSlot(&combat_slot);
}

/* Mode-keyed query over the representation's emitter/cycle state; used by the
   missile script handlers. */
w8_ulong W8Missile::GetAnimationState(int mode)
{
    switch (mode) {
    case 0:
        return m_pRep->ApplyEmitterSetting(m_pRep->current_cycle);
    case 1:
        return GetTotalAnimationCount();
    case 2:
        return m_pRep->subcycle == m_pRep->ApplyEmitterSetting(m_pRep->current_cycle) - 1;
    case 3:
        return m_pRep->subcycle == 0;
    case 4:
        return m_pRep->subcycle;
    case 5:
        return m_pRep->ApplyEmitterSetting(m_pRep->current_cycle) != 0xffffffff;
    case 6:
        return m_pRep->current_cycle;
    case 7:
        return m_pRep->animation_playing == 0;
    default:
        return 0xffffffff;
    }
}

/* Release the representation and every external reference before ordinary
   vector and GrCycle teardown. */
W8Missile::~W8Missile()
{
    SetLights(0);
    m_pRep.reset();
    DetachMissileReferences(this);
    UnregisterGrCycle(this);
}

/* Start or advance the missile while its launcher is live.

   When no scripted phase claims it, the ordinary path hands control to the
   missile's AI and ticks the animation. A scripted missile instead records
   the start, fires the one-shot launch for its table kind, and lets the
   combat boundary consume the animation once its table flag is set. */
void W8Missile::StartIfHostActive()
{
    if (m_pRep->active == 0) {
        return;
    }
    if (GetAnimationState(2) == 0 || !impacting) {
        if (flight_ai) {
            UpdateMissileAI(flight_ai.get());
        } else if (m_pAI != 0) {
            PathAIUpdate(m_pAI.get(), 1);
        }
        W8GrCycle::TickAnimation(1.0f);
    } else {
        flight_done = true;
        if (missile_table_index == 0x23 &&
            (g_combat_state == 0 ||
             g_combat_state->missile_hit_result != W8_MISSILE_HIT_DEFLECTED)) {
            DetonateMissileSpell();
        }
        if (g_missile_table[missile_table_index].spell_missile) {
            AbsorbMissileDamage(this);
        }
    }
}

W8MissileRep::~W8MissileRep() = default;

/* Copy the effect definition, then replace its radius from the selected
   0x1e5-byte missile database row. */
void W8Missile::SetEffectDefinition(const W8SpellEffectDefinition* definition)
{
    memcpy(&this->definition, definition, sizeof(this->definition));
    this->definition.radius = g_missile_table[missile_table_index].radius;
}

/* The representation a missile was fired from. */
W8EmitterHost* W8Missile::GetRepresentation()
{
    return m_pRep.get();
}

/* The animation the representation is currently firing from. */
W8AnimObj* W8Missile::GetCurrentAnimation()
{
    return m_pRep->resources.cycles[m_pRep->current_cycle].animation.get();
}

/* That animation's own playback scale. */
float W8Missile::GetCurrentAnimationScale()
{
    return m_pRep->resources.cycles[m_pRep->current_cycle].animation->playback_scale;
}

W8AniMesh* W8Missile::GetCurrentAniMesh()
{
    W8AnimObj* animation = m_pRep->resources.cycles[m_pRep->current_cycle].animation.get();

    if (animation == 0) {
        Fail("pao");
    }
    return animation->entries[m_pRep->m_bLOD].get();
}

/* How many emitters the launcher has, counted by testing each for null rather
   than read from a stored count. */
signed char W8Missile::GetTotalAnimationCount()
{
    signed char count = 0;

    if (m_pRep->resources.cycles[0].animation != 0) {
        count = 1;
    }

    if (m_pRep->resources.cycles[1].animation != 0) {
        ++count;
    }
    return count;
}

/* The missile adds no work around the ordinary GrCycle update. */
void W8Missile::UpdateRepresentation(W8World* world)
{
    W8GrCycle::UpdateRepresentation(world);
}

/* Hand the representation's LOD to the current animation. */
signed char W8Missile::GetNumSubCycles()
{
    W8AnimObj* animation = GetCurrentAnimation();

    return static_cast<signed char>(AnimObjValue(animation, m_pRep->m_bLOD));
}

bool W8Missile::IsCycleSupported(signed char cycle)
{
    if (cycle >= 2) {
        Fail("bCycle<MISSILE_NUM_CYCLES");
    }
    return m_pRep->resources.cycles[cycle].animation != 0;
}

/* Select one of the missile representation's two emitters and rebuild its
   light and particle attachment state. */
/* 0x004A4440 is a split address inside this body: it resumes at the
   animation-record field copy after the timestamp read, not a separate
   authored function. */
void W8Missile::SetCycle(signed char cycle)
{
    std::vector<std::unique_ptr<stLight>>* lights;

    if (cycle < 0 || cycle >= 2) {
        Fail("bCycle >= MISSILE_CYCLE_FIRST && bCycle <= MISSILE_CYCLE_LAST");
    }

    lights = m_pRep->resources.cycles[m_pRep->current_cycle].lightView();
    DetachCycleLights(lights);

    StartEmitterCycle(*m_pRep, *m_pRep->resources.cycles[cycle].animation, cycle);

    lights = m_pRep->resources.cycles[cycle].lightView();
    SetLights(lights);
    AttachCycleLights(lights);

    if (!particles.empty()) {
        int count = static_cast<int>(particles.size());
        for (int index = 0; index < count; ++index) {
            W8GrCycleParticleAttachment* event = &particles[index];
            event->SelectCycle(cycle);
        }
    }
}

/* Reset the launcher's two counters at 0x94 and 0x95. The second is one less
   than the missile's own virtual answer, and the launcher pointer is taken
   before the virtual call rather than after. */
void W8Missile::AdvanceAnimationFrame(int value, int arg_flags)
{
    W8MissileRep* representation_before;

    m_pRep->first_frame = 0;
    representation_before = m_pRep.get();
    representation_before->last_frame = GetNumSubCycles() - 1;
    W8GrCycle::AdvanceAnimationFrame(value, arg_flags);
}

/* Cast spell 0x83 at the missile's own position on behalf of the character
   that fired it - the detonation a stored-spell missile (table index 0x23)
   releases once its flight ends. */
void W8Missile::DetonateMissileSpell()
{
    W8TargetSource source;
    W8CombatSlot target;
    glm::vec3 position;

    if (m_Source.iType != W8_TARGET_SOURCE_CHARACTER) {
        Fail("m_Source.iType == SOURCE_TYPE_CHAR");
    }
    position = GetPosition();
    ResetTargetSource(&source);
    source.iChar = m_Source.iChar;
    source.iType = W8_TARGET_SOURCE_CHARACTER;
    source.point = position;
    source.aim_resolved = true;
    ResetCombatSlot(&target);
    target.iType = W8_TARGET_KIND_PLACE;
    target.point = position;
    CastSpellFromSource(W8_SPELL_ROCKET_BLAST, &source, &target, 1, 0, 0, false, 0, 0, 0, 0);
}

/* Post "<source> hits <target>" to the notice box and colour the target's
   name with the target side's colour when it differs from the source's. */
void W8Missile::AnnounceCollisionTarget()
{
    std::string text;
    unsigned char target_start;
    unsigned char target_stop;
    unsigned char source_color;
    unsigned char target_color;

    if (!gXStatus.fCombatMode) {
        return;
    }
    text = fmt::format("{} ", g_localized_strings[0x1bf]);
    target_start = text.size();
    if (combat_slot.iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* monster_info = FindMonster(combat_slot.iMonsterID);
        text += GetMonsterName(monster_info, 0, 0);
    } else {
        text += g_status.buffers.Char[combat_slot.iChar].name;
    }
    target_stop = text.size();
    source_color = GetSourceNoticeColor(&m_Source);
    target_color = GetTargetNoticeColor(&m_Source, &combat_slot);
    text += " ";
    text += g_localized_strings[0x1c0];
    ShowNotice(source_color, text);
    if (target_color != source_color) {
        HighlightTextBoxRange(target_color, target_start, target_stop, -1);
    }
}

void W8Missile::EnterImpactCycle()
{
    if (IsCycleSupported(1)) {
        if (GetAnimationState(6) != 1) {
            W8MissileRep* representation = m_pRep.get();
            glm::vec3 position = representation->location;
            representation->pending_cycle = 1;
            representation->pending_behaviour = W8_ANIMATION_PLAY_ONCE;
            impacting = true;
            if (explode_ground) {
                representation->location.y = probeGround(position).position.y;
            }
        }
    } else {
        flight_done = true;
        if (missile_table_index == 0x23 &&
            (g_combat_state == 0 ||
             g_combat_state->missile_hit_result != W8_MISSILE_HIT_DEFLECTED)) {
            DetonateMissileSpell();
        }
        if (g_missile_table[missile_table_index].spell_missile) {
            AbsorbMissileDamage(this);
        }
    }
}

bool W8Missile::OnCollision(W8Navigator* other)
{
    char hit_result;
    unsigned char deflect_chance;

    if (PartyNavigator() == other) {
        if (TargetSourceIsCharacter(&m_Source, 0)) {
            goto miss;
        }
        if (combat_slot.iType != W8_TARGET_KIND_PARTY &&
            combat_slot.iType != W8_TARGET_KIND_CHARACTER) {
            if (g_missile_table[missile_table_index].spell_missile) {
                goto miss;
            }
            if (combat_slot.iType != W8_TARGET_KIND_NONE) {
                retargeted = true;
            }
            combat_slot.iType = W8_TARGET_KIND_CHARACTER;
            combat_slot.iChar = GetRandomCharacter(1, 1, -1, -1);
            combat_slot.iMonsterID = -1;
            if (gXStatus.fCombatMode) {
                AnnounceCollisionTarget();
            }
        }
    } else {
        if (other->movement.location_id <= 0) {
            goto miss;
        }
        W8Monster* monster = static_cast<W8Monster*>(other);
        int location_id = monster->location_id;
        W8MonsterInfo* monster_info = FindMonster(location_id);
        if (TargetSourceIsMonster(&m_Source, 0) && m_Source.iMonsterID == location_id) {
            goto miss;
        }
        if (monster_info->hp_current == 0) {
            goto miss;
        }
        if (combat_slot.iType != W8_TARGET_KIND_MONSTER || combat_slot.iMonsterID != location_id) {
            if (g_missile_table[missile_table_index].spell_missile) {
                goto miss;
            }
            if (combat_slot.iType != W8_TARGET_KIND_NONE) {
                retargeted = true;
            }
            combat_slot.iType = W8_TARGET_KIND_MONSTER;
            combat_slot.iChar = -1;
            combat_slot.iMonsterID = location_id;
            if (gXStatus.fCombatMode) {
                AnnounceCollisionTarget();
            }
        }
    }

    hit_result = W8_MISSILE_HIT;
    if (!g_missile_table[missile_table_index].spell_missile) {
        if (combat_slot.iType == W8_TARGET_KIND_CHARACTER) {
            deflect_chance = g_status.buffers.Char[combat_slot.iChar].bonus.missile_deflect_chance;
        } else {
            W8MonsterInfo* monster_info = FindMonster(combat_slot.iMonsterID);
            deflect_chance = monster_info->modifiers.missile_deflect_chance;
        }
        if (deflect_chance > 0 && Random(100) + 1 <= deflect_chance) {
            hit_result = W8_MISSILE_HIT_DEFLECTED;
        }
    }

    if (missile_table_index == 0x23) {
        g_combat_state->missile_hit_result = hit_result;
    } else if (g_combat_state != 0 && g_combat_state->engaged_missile != 0) {
        g_combat_state->missile_hit_result = hit_result;
        g_combat_state->TargetHit = combat_slot;
    } else if (g_missile_table[missile_table_index].spell_missile) {
        ResolveSpellMissileHit(this);
    } else {
        ResolveMissileHit(this, hit_result == W8_MISSILE_HIT_DEFLECTED);
    }
    EnterImpactCycle();
    active = false;
    return true;

miss:
    return false;
}

/* glm::mat3::RotateAboutX emitted for this TU; the primary template
   lives in srMath.h. */
