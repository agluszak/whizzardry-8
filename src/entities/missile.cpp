#include "wiz8/entities/missile.h"

#include <algorithm>
#include <cassert>
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
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/combat/attack.h"
#include "wiz8/data/game_data.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/emitter_cycles.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/unicode.h"
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
static float AdvanceMissileAI(MissileFlight* record, glm::vec3* out, unsigned int steps);

static void UpdateMissileAI(MissileFlight& record, bool held)
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

    missile = record.missile;
    if (missile->impacting) {
        if (!missile->align_explosion) {
            return;
        }
        position = missile->nav.GetPosition();
        const auto eye = missile->owner_world.party.eye();
        pitch = GetElevationAngle(&position, &eye);
        position = missile->nav.GetPosition();
        yaw = GetHeadingAngle(&position, &eye);
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        missile->visual.SetRotation(&rotation);
        return;
    }
    position = missile->nav.GetPosition();
    count = missile->owner_world.clocks.gameMilliseconds() >> 1;
    delta = count - record.last_half_tick;
    if (delta > 0xfa) {
        delta = 0xfa;
    }
    record.last_half_tick = count;
    if (held) {
        return;
    }
    advance = AdvanceMissileAI(&record, &out, delta);
    remaining = missile->duration - record.elapsed + 1.0f;
    if (remaining <= advance) {
        advance = remaining;
    }
    if (record.limit > 0.0f) {
        remaining = record.limit - record.elapsed + 1.0f;
        if (remaining <= advance) {
            advance = remaining;
        }
    }
    record.elapsed = advance + record.elapsed;
    missile->SetCyclePosition(&out);
    if (!missile->nav.CheckNavigatorCollision(&position, &out) && missile->align_camera) {
        const auto eye = missile->owner_world.party.eye();
        pitch = GetElevationAngle(&out, &eye);
        yaw = GetHeadingAngle(&out, &eye);
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(yaw), glm::vec3(0, 1, 0)));
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        missile->visual.SetRotation(&rotation);
    }
    if (advance + record.elapsed <= missile->duration) {
        if (record.limit > 0.0f && record.limit < advance + record.elapsed &&
            !missile->nav.CheckNavigatorCollision(&position, &out)) {
            missile->EnterImpactCycle();
        }
        return;
    }
    missile->Land();
    return;
}

/* Predict the homing missile's next position `steps` half-ticks ahead and
   re-aim its representation at that point. A ray through the world octree
   flags the record when a prop blocks the path and fires the prop's
   missile trigger. Returns the distance the step covered. */
static float AdvanceMissileAI(MissileFlight* record, glm::vec3* out, unsigned int steps)
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
    World& world = record->missile->owner_world;
    if (world.compiledIndex() != 0) {
        if (const auto hit = world.compiledIndex()->traceProps(position, (*out), {})) {
            *out = hit->position;
            record->limit = 1.0f;
            prop = world.collidable_props[hit->prop_id];
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

bool Missile::BlocksEndingCombat() const
{
    return !flight_done && visual.current_cycle != 1;
}

glm::vec3 World::characterProjectilePosition(unsigned int character_index) const
{
    const glm::vec3 offset(
        (character_index & 1) ? -75.0f : 75.0f,
        kCharacterProjectileHeight - character_index * 0.5f * kCharacterProjectileHeightStep, 0);
    const auto yaw = party.camera.m_yaw - g_monster_rotation_offset;
    const auto pitch = -party.camera.m_pitch;
    return party.eye() + glm::rotateY(glm::rotateX(offset, pitch), yaw);
}

void World::updateMissiles(bool held)
{
    // Callbacks can append projectiles. Only the original batch advances this tick.
    int count = static_cast<int>(missiles_.size());
    for (int index = 0; index < count;) {
        auto& missile = *missiles_[index];
        missile.visual.DetachRepresentation();
        if (!missile.flight_done || !missile.block_released) {
            missile.StartIfHostActive(held);
            missile.visual.UpdateRepresentation(missile.nav);
            missile.nav.UpdateNavigation(0, false, party.collisionSphere());
            ++index;
        } else {
            missile.visual.RetireParticles();
            missiles_.erase(missiles_.begin() + index);
            --count;
        }
    }
}

/* Load a missile definition: the "data\\Missiles\\<name>.mls" script is
   parsed - its align/explode/gravity/velocity keywords set the missile's tail
   flags - and the cycle lines load through the ordinary GrCycle path. Without
   a script the name itself is loaded as the cycle. */
static std::unique_ptr<MissileDefinition> LoadMissileDefinition(std::string_view name)
{
    auto output = std::make_unique<MissileDefinition>();
    const auto read_cycle = [&](LevelLoadContext& read_info, int cycle) {
        return ReadEmitterCycle(output->resources.cycles, read_info, cycle);
    };
    auto handle = [&] {
        try {
            return wiz8::open_file(wiz8::asset_path(fmt::format("data/missiles/{}.mls", name)));
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (!handle) {
        const bool success = loadAnimationCycle(name, output->events, 0, "Data\\Missiles",
                                                read_cycle, "Data\\Spells\\Bitmaps");
        if (success) {
            output->asset_name = name;
        }
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

                    success = loadAnimationCycle(resource, output->events, cycle, "Data\\Missiles",
                                                 read_cycle, "Data\\Spells\\Bitmaps");
                    cycle_line = true;
                    break;
                }
            }
            if (cycle_line || !success) continue;
            std::string cycle_name;
            int frame = 0;
            if (SDL_strcasecmp(command.c_str(), "SHAKE_FRAME") == 0) {
                if (!(fields >> cycle_name >> frame)) {
                    success = false;
                    break;
                }
                float intensity = 1.0f, duration = 1.0f, distance = 10.0f;
                fields >> intensity >> duration >> distance;
                ShakeDefinition effect(duration, true, intensity, distance * kWorldScale, 0);
                effect.cycle = missileCycleIndex(cycle_name.c_str());
                effect.frame = frame;
                effect.subcycle = 0;
                output->events.shake_events.push_back(effect);
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
            if (!(fields >> cycle_name >> frame >> resource)) {
                success = false;
                break;
            }
            fields >> loop;
            const auto wave = fmt::format("data/missiles/sounds/{}.WAV", resource);
            output->events.sound_events.emplace_back(kind, missileCycleIndex(cycle_name.c_str()),
                                                     frame, 0, wave.c_str(),
                                                     SDL_strcasecmp(loop.c_str(), "LOOP") == 0);
        }
    }
    output->gravity = gravity;
    output->align_camera = align_camera;
    output->explode_ground = explode_ground;
    output->align_explosion = align_explosion;
    output->lifetime = velocity * kWorldScale;
    output->asset_name = name;
    return success ? std::move(output) : nullptr;
}

// Load resources and initialize the entire projectile before publishing its owner.
Missile* World::launchMissile(const GameData& data, const MissileLaunch& launch)
{
    const auto& record = data.missiles[launch.kind];
    auto definition = std::ranges::find_if(missile_definitions, [&](const auto& candidate) {
        return CompareTextIgnoreAsciiCase(candidate->asset_name, record.cycle_name) == 0;
    });
    if (definition == missile_definitions.end()) {
        auto loaded = LoadMissileDefinition(record.cycle_name);
        if (!loaded) return nullptr;
        missile_definitions.push_back(std::move(loaded));
        definition = missile_definitions.end() - 1;
    }
    auto missile = std::make_unique<Missile>(**definition, *this);
    missile->asset_name = record.cycle_name;
    missile->missile_table_index = launch.kind;
    missile->nav.SetBounds({{-125.0f, -125.0f, -125.0f}, {125.0f, 125.0f, 125.0f}});
    missile->nav.active = true;
    missile->visual.pending_cycle = 0;
    missile->SetCycle(0);
    missile->visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
    missile->visual.pending_subcycle = 0;
    missile->visual.subcycle = 0;
    const auto heading = GetHeadingAngle(&launch.origin, &launch.destination);
    const auto pitch = GetElevationAngle(&launch.origin, &launch.destination);
    auto rotation = glm::mat3(1);
    rotation *= glm::mat3_cast(glm::angleAxis(heading, glm::vec3(0, 1, 0)));
    rotation *= glm::mat3_cast(glm::angleAxis(pitch, glm::vec3(1, 0, 0)));
    const auto direction = rotation * glm::vec3(0, 0, 1);
    missile->nav.SetVelocity(&direction);
    auto origin = launch.origin;
    missile->SetCyclePosition(&origin);
    missile->visual.SetRotation(&rotation);
    missile->nav.SetAngles(heading);
    missile->nav.SetPitch(pitch);
    float limit = -1.0f;
    if (const auto* index = compiledIndex()) {
        const auto end = direction * launch.distance + launch.origin;
        if (const auto hit = index->traceSight(launch.origin, end, {.props = false})) {
            limit = glm::length(hit->point - launch.origin);
            if (limit < 1.0f) limit = 1.0f;
        }
    }
    auto& flight = missile->flight_ai.emplace();
    flight.speed_per_step = missile->lifetime * 0.001f;
    flight.gravity = missile->gravity;
    flight.limit = limit;
    flight.last_half_tick = clocks.gameMilliseconds() >> 1;
    flight.missile = missile.get();
    flight.fall_speed = static_cast<float>(sin(-pitch) * missile->lifetime * 0.001f);
    missile->nav.trace_mask = launch.trace_mask;
    missile->duration = launch.distance;
    missile->block_released = launch.released;
    missile->m_Source = launch.source;
    missile->combat_slot = launch.target;
    if (launch.effect) {
        missile->definition = *launch.effect;
        missile->definition.radius = record.radius;
    }
    auto* result = missile.get();
    missiles_.push_back(std::move(missile));
    return result;
}

/* Apply the representation's current LOD to one required animation. */
unsigned int Missile::ApplyEmitterSetting(signed char emitter)
{
    Animation* target = resources.cycles[emitter].animation.get();

    assert(target != 0);
    return target->frameCount(visual.m_bLOD);
}

Missile::Missile(World& owner)
    : owner_world(owner), nav(owner), visual(owner), missile_table_index(-1), flight_done(0),
      impacting(0), block_released(1), gravity(0), align_camera(0), explode_ground(0),
      align_explosion(0), flag(1), lifetime(15000.0f), flags0(0), retargeted(false)
{
    nav.radius = 1.0f;
    nav.movement.collision_radius = 1.0f;
    nav.movement.alternate_radius = 1.0f;
    nav.collision = [this](Navigator* other) { return OnCollision(other); };

    result = {};
    ResetCombatSlot(&combat_slot);
}

Missile::Missile(const MissileDefinition& definition, World& owner)
    : owner_world(owner), nav(owner), visual(definition.events, owner),
      asset_name(definition.asset_name), missile_table_index(-1), resources(definition.resources),
      flight_done(0), impacting(0), block_released(1), gravity(definition.gravity),
      align_camera(definition.align_camera), explode_ground(definition.explode_ground),
      align_explosion(definition.align_explosion), flag(1), lifetime(definition.lifetime),
      flags0(0), duration(0.0f), retargeted(false)
{
    nav.radius = 1.0f;
    nav.movement.collision_radius = 1.0f;
    nav.movement.alternate_radius = 1.0f;
    nav.collision = [this](Navigator* other) { return OnCollision(other); };
    if (auto* animation = resources.cycles[0].animation.get()) {
        StartEmitterCycle(visual, *animation, 0);
        visual.animation_behaviour = animation->behaviour;
    }

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
    owner_world.detachMissile(*this);
}

/* Start or advance the missile while its launcher is live.

   When no scripted phase claims it, the ordinary path hands control to the
   missile's AI and ticks the animation. A scripted missile instead records
   the start, fires the one-shot launch for its table kind, and lets the
   combat boundary consume the animation once its table flag is set. */
void Missile::StartIfHostActive(bool held)
{
    if (!visual.rep_active) {
        return;
    }
    if (GetAnimationState(W8_MISSILE_ANIMATION_AT_FINAL_SUBCYCLE) == 0 || !impacting) {
        if (flight_ai) {
            UpdateMissileAI(*flight_ai, held);
        } else if (visual.m_pAI.has_value()) {
            PathAIUpdate((visual.m_pAI ? &*visual.m_pAI : nullptr), 1,
                         owner_world.clocks.worldMilliseconds());
        }
        TickAnimation(1.0f);
    } else {
        Land();
    }
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

    return static_cast<signed char>(animation->frameCount(visual.m_bLOD));
}

bool Missile::IsCycleSupported(signed char cycle)
{
    assert(cycle < 2);
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

    assert(cycle >= 0 && cycle < 2);

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

/* Post "<source> hits <target>" to the notice box and colour the target's
   name with the target side's colour when it differs from the source's. */
void Missile::AnnounceCollisionTarget()
{
    std::string text;
    unsigned char target_start;
    unsigned char target_stop;
    unsigned char source_color;
    unsigned char target_color;

    if (!CurrentGame().combat.has_value()) {
        return;
    }
    text = fmt::format("{} ", Data().strings[0x1bf]);
    target_start = text.size();
    if (combat_slot.iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* monster_info = owner_world.population->findMonster(combat_slot.iMonsterID);
        text += CurrentGame().monsterName(*monster_info);
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
                if (const auto* index = owner_world.compiledIndex())
                    visual.location.y = index->settleToGround(position, true, 500).position.y;
            }
        }
    } else {
        Land();
    }
}

void Missile::Land()
{
    flight_done = true;
    if (owner_world.simulation) owner_world.simulation->missileLanded(*this);
}

bool Missile::OnCollision(Navigator* other)
{
    if (!owner_world.simulation || !owner_world.simulation->missileReached(*this, *other)) {
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
