#include "wiz8/entities/spell_visual.h"

#include <cassert>
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
#include <glm/gtc/quaternion.hpp>

#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "monster_cycles.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/emitter_cycles.h"
#include "wiz8/entities/entity_assets.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/legacy_prop_save_ids.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/picking.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

/* Names of the seven bitmap cycles in each of the four spell-visual groups,
   in load order: FLASH, EXPLOSION, TARGET and CONE. */
const char* g_spell_cycle_names[28] = {
    "FLASH1",     "FLASH2",     "FLASH3",     "FLASH4",     "FLASH5",     "FLASH6",
    "FLASH7",     "EXPLOSION1", "EXPLOSION2", "EXPLOSION3", "EXPLOSION4", "EXPLOSION5",
    "EXPLOSION6", "EXPLOSION7", "TARGET1",    "TARGET2",    "TARGET3",    "TARGET4",
    "TARGET5",    "TARGET6",    "TARGET7",    "CONE1",      "CONE2",      "CONE3",
    "CONE4",      "CONE5",      "CONE6",      "CONE7",
};

/* The emitter it is currently coming out of. */
Animation* SpellVisual::GetCurrentAnimation()
{
    return this->resources.cycles[this->visual.current_cycle].animation.get();
}

/* That emitter's own value. */
float SpellVisual::GetCurrentAnimationScale()
{
    return this->resources.cycles[this->visual.current_cycle].animation->playback_scale;
}

AnimationMesh* SpellVisual::GetCurrentAniMesh()
{
    Animation* emitter = this->resources.cycles[this->visual.current_cycle].animation.get();

    assert(emitter != 0);
    return emitter->entries[this->visual.m_bLOD].get();
}

/* How many emitters the host has, counted by testing each for null. */
signed char SpellVisual::GetTotalAnimationCount()
{
    signed char count = 0;

    if (this->resources.cycles[0].animation != 0) {
        count = 1;
    }

    if (this->resources.cycles[1].animation != 0) {
        ++count;
    }
    return count;
}

/* Hand the host's LOD to the current animation. */
signed char SpellVisual::GetNumSubCycles()
{
    Animation* animation = GetCurrentAnimation();

    return static_cast<signed char>(AnimObjValue(animation, this->visual.m_bLOD));
}

bool SpellVisual::IsCycleSupported(signed char cycle)
{
    assert(cycle < SPELL_NUM_CYCLES);
    return this->resources.cycles[cycle].animation != 0;
}

/* Reset the host's two frame counters before the ordinary GrCycle step. */

/* Select one of the spell host's 28 emitters and rebuild its light and
   particle attachment state. */
void SpellVisual::SetCycle(signed char cycle)
{
    std::vector<std::unique_ptr<Light>>* lights;

    assert(cycle >= SPELL_CYCLE_FIRST && cycle <= SPELL_CYCLE_LAST);

    lights = this->resources.cycles[this->visual.current_cycle].lightView();
    visual.DetachCycleLights(lights);

    StartEmitterCycle(visual, *this->resources.cycles[cycle].animation, cycle);

    lights = this->resources.cycles[cycle].lightView();
    visual.SetLights(lights);
    visual.AttachCycleLights(lights);

    if (!visual.particles.empty()) {
        for (auto& attachment : visual.particles) {
            auto* event = &attachment;
            event->SelectCycle(cycle);
        }
    }
}

/* Position and orient each live spell visual according to its attachment
   mode, then let the common GrCycle update submit the resulting state. */
void SpellVisual::UpdateRepresentation(World* world)
{
    glm::mat3 rotation;
    glm::vec3 camera_position;
    glm::vec3 position;
    bool apply_rotation = false;

    if (mode == W8_SPELL_VISUAL_FLASH) {
        float angle;
        float pitch;

        position = cameraLocation();
        SetCyclePosition(&position);
        rotation = glm::mat3(1);
        angle = GetCameraYawRadians() - g_monster_rotation_offset;
        rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        pitch = -partyCamera().m_pitch;
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        apply_rotation = true;
    } else if (mode == W8_SPELL_VISUAL_TARGET) {
        Monster* monster = GetMonsterByLocationID(location_id);

        if (monster != 0) {
            MeshInstance* instance = visual.GetCurrentModelInstance();

            if (instance) instance->transform.setScale(scale0);

            const auto monster_position = monster->nav.GetPosition();
            if (const auto bounds = monster->GetAnimationBounds()) {
                position = monster_position;
                position.y += (bounds->maximum.y - bounds->minimum.y) * 0.5f;
                SetCyclePosition(&position);
            }
        }
    } else if (mode == W8_SPELL_VISUAL_CONE) {
        visual.GetCurrentModelInstance();
        if (!fixed_transform) {
            camera_position = cameraLocation();
            if (location_id == 0) {
                SetCyclePosition(&camera_position);
                rotation = partyCamera().rotationMatrix();
                apply_rotation = true;
            } else {
                Monster* monster = GetMonsterByLocationID(location_id);

                if (monster != 0) {
                    if (monster->Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_SPELL &&
                        monster->GetSpellPosition(&position)) {
                        SetCyclePosition(&position);
                    }

                    rotation = glm::mat3(1);
                    float angle = monster->nav.GetYaw();
                    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
                    float pitch = GetElevationAngle(&position, &camera_position);
                    rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
                    apply_rotation = true;
                }
            }
        }
    }

    if (apply_rotation) {
        this->visual.SetRotation(&rotation);
    }

    if (this->billboard) {
        glm::mat3 billboard;
        glm::vec3 visual_position = nav.GetPosition();
        float angle;

        billboard = glm::mat3(1);
        camera_position = cameraLocation();
        angle =
            GetHeadingAngle(&visual_position, &camera_position) + static_cast<float>(g_camera_pi);
        billboard *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        this->visual.SetRotation(&billboard);
    }

    MeshInstance* instance = visual.GetCurrentModelInstance();
    if (instance != 0) {
        static_cast<MeshInstance*>(instance)->render_flags |= MeshInstance::RENDER_NO_PICK;
    }
    visual.UpdateRepresentation(world, nav);
}

static void DestroySpellVisual(World* world, SpellVisual* visual)
{
    if (world->target_cone == visual) world->target_cone = nullptr;
    visual->visual.RetireParticles(*world);
    std::erase_if(world->spell_visuals,
                  [visual](const auto& owner) { return owner.get() == visual; });
}

/* Step every live spell visual and drop the finished ones.

   A visual that has not finished, or that an owning spell effect still
   holds, starts and updates in place; a finished one the effect released is
   unlinked from the world collection, hands its lights back through the
   world light boundary, and deletes itself. */
void UpdateWorldSpellVisuals(World* world)
{
    assert(world != 0);
    int index = 0;
    int count = static_cast<int>(world->spell_visuals.size());
    while (index < count) {
        SpellVisual* visual = world->spell_visuals[index].get();
        if (visual != 0) {
            visual->visual.DetachRepresentation(world);
            if (!visual->finished || !visual->auto_release) {
                visual->StartIfHostActive();
                visual->UpdateRepresentation(world);
                visual->nav.UpdateNavigation(
                    0, false,
                    world == GetWorld()
                        ? std::optional{BoundingSphere{PartyNavigator()->movement.position,
                                                       PartyNavigator()->movement.alternate_radius}}
                        : std::nullopt);
            } else {
                DestroySpellVisual(world, visual);
                --index;
                --count;
            }
        }
        ++index;
    }
}

unsigned char SpellVisual::ReadCycleData(LevelLoadContext* info, int, int emitter_index)
{
    assert(info != 0 && info->hFile != 0);
    return ReadEmitterCycle(visual, resources.cycles, info, emitter_index, true);
}

static int FindSpellCycleByName(const char* name)
{
    for (int index = 0; index < SPELL_NUM_CYCLES; ++index) {
        if (SDL_strncasecmp(name, g_spell_cycle_names[index], strlen(g_spell_cycle_names[index])) ==
            0) {
            return index;
        }
    }
    return -1;
}

/* Load one named spell visual resource. When a shared visual of the same
   cycle group is already registered the answer is a clone of it; otherwise
   the .mls text resource is parsed: each of the seven cycle rows of the
   requested group loads its bitmaps, SOUND_FRAME/SOUND_CYCLE rows attach timed
   sound events, and SHAKE_FRAME rows attach camera-shake effects. */
std::unique_ptr<SpellVisual> LoadSpellVisualResource(World* world, std::string_view name,
                                                     W8SpellVisualMode group)
{
    SpellVisual* shared = findSpellVisualAsset(*world, name, group);
    if (shared != 0 && shared->mode == group) {
        auto spell = std::make_unique<SpellVisual>(*shared, world);
        spell->asset_name = name;
        return spell;
    }

    PauseSharedGameTimers();

    bool success = true;
    const auto path = fmt::format("data/spells/{}.mls", name);
    std::unique_ptr<wiz8::File> handle = [&]() {
        try {
            return wiz8::open_file(wiz8::asset_path(path), wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    auto visual = std::make_unique<SpellVisual>(world);
    visual->asset_name = name;
    visual->visual.current_cycle = -1;
    if (handle == 0) {
        success = false;
    } else {
        std::istringstream input(handle->read_text());
        std::string line;
        while (success && std::getline(input, line)) {
            std::istringstream fields(line);
            std::string command;
            if (!(fields >> command) || command.starts_with('#') || line.size() <= 2) continue;
            const int cycle = FindSpellCycleByName(command.c_str());
            if (cycle != -1) {
                if (cycle / SPELL_CYCLES_PER_GROUP == group) {
                    std::string resource;
                    if (!(fields >> resource)) {
                        success = false;
                        break;
                    }

                    success = loadAnimationCycle(
                        resource, visual.get()->visual, cycle, 1, "Data\\Spells",
                        [&](LevelLoadContext& read_info, int read_cycle, int read_value) {
                            return visual.get()->ReadCycleData(&read_info, read_cycle,
                                                               read_value) != 0;
                        });
                }
                continue;
            }
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
                effect.cycle = FindSpellCycleByName(cycle_name.c_str());
                effect.frame = frame;
                effect.subcycle = 0;
                visual->visual.AddShakeEffect(effect);
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
            const auto sound_cycle = FindSpellCycleByName(cycle_name.c_str());
            if (visual && visual->IsCycleSupported(static_cast<signed char>(sound_cycle))) {
                const auto wave = fmt::format("data/spells/sounds/{}.WAV", resource);
                visual->visual.AddSoundEvent(
                    W8SoundEvent(kind, sound_cycle, frame, 0, wave.c_str(),
                                 SDL_strcasecmp(loop.c_str(), "LOOP") == 0));
            }
        }
        visual->billboard = false;
        visual->visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
    }

    ResumeSharedGameTimers();
    return success ? std::move(visual) : nullptr;
}

SpellVisual::SpellVisual(World* owner)
    : owner_world(owner), nav(owner), visual(owner), mode(W8_SPELL_VISUAL_NONE), finished(0),
      flag(0), auto_release(1), fixed_transform(0), scale0(1.0f), location_id(0)
{
    AllocateLegacyPropSaveId();
}

/* Release the emitter host, leave the world's spell collection, and unregister
   the common GrCycle identity before the base classes tear down. */
SpellVisual::~SpellVisual()
{
    if (owner_world && owner_world->target_cone == this) owner_world->target_cone = nullptr;
    visual.SetLights(0);
}

/* Tick the visual's animation, but only while the host is live. When the
   kind-7 query reports the current animation needs no tick, the visual is
   finished instead. */
void SpellVisual::StartIfHostActive()
{
    if (!this->visual.rep_active) {
        return;
    }
    if (QueryHostState(W8SpellVisualHostStateQuery::AnimationStopped) != 0) {
        this->finished = true;
        return;
    }
    TickAnimation(1.0f);
}

/* Search backward from a subcycle for the first cycle this visual supports,
   seven cycles per group; -1 when none does. */
int SpellVisual::FindSupportedCycle(int group, int subcycle)
{
    for (int index = subcycle; index >= 0; --index) {
        int cycle = group * SPELL_CYCLES_PER_GROUP + index;

        if (IsCycleSupported(static_cast<signed char>(cycle))) {
            return cycle;
        }
    }
    return -1;
}

/* Query one host-side property of the spell visual. */
int SpellVisual::QueryHostState(W8SpellVisualHostStateQuery query)
{
    switch (query) {
    case W8SpellVisualHostStateQuery::EmitterSetting:
        return this->ApplyEmitterSetting(this->visual.current_cycle);
    case W8SpellVisualHostStateQuery::AnimationCount:
        return GetTotalAnimationCount();
    case W8SpellVisualHostStateQuery::AtLastSubcycle:
        return this->visual.subcycle == this->ApplyEmitterSetting(this->visual.current_cycle) - 1;
    case W8SpellVisualHostStateQuery::IsFirstSubcycle:
        return this->visual.subcycle == 0;
    case W8SpellVisualHostStateQuery::Subcycle:
        return this->visual.subcycle;
    case W8SpellVisualHostStateQuery::HasEmitterSetting:
        return this->ApplyEmitterSetting(this->visual.current_cycle) != 0xffffffff;
    case W8SpellVisualHostStateQuery::CurrentCycle:
        return this->visual.current_cycle;
    case W8SpellVisualHostStateQuery::AnimationStopped:
        return this->visual.animation_playing == 0;
    default:
        return -1;
    }
}

static SpellVisual* CreateSpellVisual(std::string_view name, W8SpellVisualMode mode,
                                      int power_level, int value, int flags)
{
    const auto load = [&](std::string_view resource) -> SpellVisual* {
        auto owner = LoadSpellVisualResource(GetWorld(), resource, mode);
        auto* visual = owner.get();
        if (!visual) {
            return nullptr;
        }
        visual->nav.SetNavigationMode(W8_NAVIGATOR_MODE_PATH_ONLY);
        visual->nav.active = false;
        visual->nav.SetPitchRollEnabled(true, true);
        GetWorld()->spell_visuals.push_back(std::move(owner));
        return visual;
    };

    SpellVisual* visual = name.empty() ? nullptr : load(name);
    int cycle = visual ? visual->FindSupportedCycle(mode, power_level - 1) : -1;
    if (visual && cycle == -1) {
        DestroySpellVisual(GetWorld(), visual);
        visual = nullptr;
    }
    if (!visual) {
        visual = load("Generic");
        if (visual) {
            cycle = visual->FindSupportedCycle(mode, power_level - 1);
        }
    }
    if (visual) {
        visual->mode = mode;
        visual->visual.pending_cycle = static_cast<signed char>(cycle);
        visual->effect_value = value;
        visual->flags0 = flags;
    }
    return visual;
}

/* Create one spell visual from a named bitmap resource, falling back to the
   Generic visual when the named one is missing or has no supported cycle.
   The visual joins the world's list, is positioned, and carries the caller's
   two extra arguments. The power level picks the EXPLOSION row. */
SpellVisual* SpawnSpellEffect(const glm::vec3* position, std::string_view resource_name,
                              int power_level, int value, int flags)
{
    assert(!resource_name.empty());
    SpellVisual* visual =
        CreateSpellVisual(resource_name, W8_SPELL_VISUAL_EXPLOSION, power_level, value, flags);
    if (visual != 0) {
        visual->billboard = true;
        visual->nav.SetPositionInternal(position);
    }
    return visual;
}

/* Spawn a FLASH (camera-anchored) spell visual, falling back to the Generic
   resource when the named one is missing or has no supported cycle. The
   visual is placed at the camera and turned to face the camera's yaw/pitch;
   the power level picks the FLASH row. */
SpellVisual* SpawnCameraSpellEffect(std::string_view name, int power_level, int value, int flags)
{
    assert(!name.empty());
    SpellVisual* visual = CreateSpellVisual(name, W8_SPELL_VISUAL_FLASH, power_level, value, flags);
    if (visual != 0) {
        glm::vec3 position;
        glm::mat3 rotation;
        float angle;
        float pitch;

        position = cameraLocation();
        visual->SetCyclePosition(&position);
        rotation = glm::mat3(1);
        angle = GetCameraYawRadians() - g_monster_rotation_offset;
        rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        pitch = -partyCamera().m_pitch;
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        visual->visual.SetRotation(&rotation);
    }
    return visual;
}

/* Create a TARGET spell visual anchored to a monster — it sits at the
   monster's position raised by half the bounds height and scales to the
   larger bound extent. Requires a monster; falls back to the Generic
   resource like above. The power level picks the TARGET row. */
SpellVisual* CreateMonsterSpellEffect(std::string_view mls_name, int power_level, Monster* monster,
                                      int value, int flags)
{
    assert(!mls_name.empty());
    assert(monster != 0);
    const auto bounds = monster->GetAnimationBounds();
    if (!bounds) return nullptr;
    SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_TARGET, power_level, value, flags);
    if (visual != 0) {
        const auto& [minimum, maximum] = *bounds;
        glm::vec3 position;
        float height;
        float width;

        visual->billboard = true;
        visual->location_id = monster->location_id;
        position = monster->nav.GetPosition();
        position.y += (maximum.y - minimum.y) * 0.5f;
        visual->SetCyclePosition(&position);
        height = maximum.y - minimum.y;
        width = maximum.x - minimum.x;
        if (height <= width) {
            height = width;
        }
        visual->scale0 = height * 0.001f;
    }
    return visual;
}

/* Create a CONE spell visual attached to a monster — its position comes from
   the monster's spell socket (or mapped position) and its scale from the
   monster's animation bounds. Without a parent it anchors at the camera with
   the camera's rotation. Falls back to the Generic resource like above. The
   power level picks the CONE row. */
SpellVisual* CreateAttachedSpellEffect(std::string_view mls_name, int power_level, Monster* parent,
                                       int value, int flags)
{
    assert(!mls_name.empty());
    const auto bounds = parent ? parent->GetAnimationBounds() : std::nullopt;
    if (parent && !bounds) return nullptr;
    SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_CONE, power_level, value, flags);
    if (visual != 0) {
        glm::vec3 position;

        if (parent && bounds) {
            const auto& [minimum, maximum] = *bounds;
            float height;
            float width;

            visual->location_id = parent->location_id;
            height = maximum.y - minimum.y;
            width = maximum.x - minimum.x;
            if (height <= width) {
                height = width;
            }
            visual->scale0 = height * 0.001f;
            if (!parent->GetSpellPosition(&position)) {
                parent->GetMappedPosition(&position);
            }
            visual->SetCyclePosition(&position);
        } else {
            glm::mat3 rotation;

            position = cameraLocation();
            rotation = partyCamera().rotationMatrix();
            visual->SetCyclePosition(&position);
            visual->visual.SetRotation(&rotation);
        }
    }
    return visual;
}

/* Create a CONE spell visual at a caller position with a caller rotation —
   the same named-load-then-Generic-fallback flow as the other spawn
   helpers. The supplied transform is fixed: mode-3 updates do not re-follow
   the monster or camera. */
SpellVisual* CreateAimedSpellEffect(std::string_view mls_name, int power_level, glm::vec3* position,
                                    glm::mat3* rotation, int value, int flags)
{
    assert(!mls_name.empty());
    SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_CONE, power_level, value, flags);
    if (visual != 0) {
        visual->SetCyclePosition(position);
        visual->visual.SetRotation(rotation);
        visual->fixed_transform = true;
    }
    return visual;
}

/* The persistent TargetCone targeting visual: created the first time the mode
   is enabled, deleted when it is disabled, and kept in representation mode 3
   while it lives. */
void SetTargetConeEnabled(bool enabled)
{
    auto* world = GetWorld();
    if (!world) return;
    if (enabled) {
        if (world->target_cone == 0) {
            world->target_cone = CreateAttachedSpellEffect("TargetCone", 1, 0, 0, 0);
            if (world->target_cone != 0) {
                world->target_cone->visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
            }
        }
        return;
    }
    if (world->target_cone != 0) {
        DestroySpellVisual(world, world->target_cone);
    }
}

/* Send something to one named emitter. The arguments are handed on in the
   reverse of the order they arrive. */
MeshInstance* SpellVisual::SetCycleFrameLod(signed char emitter, signed char frame, signed char lod)
{
    return AnimObjDispatch(this->resources.cycles[emitter].animation.get(), lod, frame);
}

/* Apply the host setting to one required emitter.  The source assertion names
   that local pointer `pao`; assertions do not replace the following call. */
unsigned int SpellVisual::ApplyEmitterSetting(signed char emitter)
{
    Animation* target = this->resources.cycles[emitter].animation.get();

    assert(target != 0);
    return AnimObjValue(target, this->visual.m_bLOD);
}

/* One named emitter's AniMesh, looked up with the host's own setting; an
   empty slot yields none. */
AnimationMesh* SpellVisual::GetEmitterAniMesh(signed char emitter)
{
    Animation* target = this->resources.cycles[emitter].animation.get();

    if (target == 0) {
        return 0;
    }
    return AnimObjEntry(target, this->visual.m_bLOD, 0);
}

void SpellVisual::TickAnimation(float time_scale)
{
    ApplyPendingCycle();
    if (!visual.animation_playing) return;
    visual.TickAnimation(
        resources.cycles[visual.current_cycle].animation->playback_scale * time_scale,
        nav.GetPosition(), true,
        [this]([[maybe_unused]] unsigned char previous_frame) { return ApplyPendingCycle(); });
}

bool SpellVisual::ApplyPendingCycle()
{
    if (visual.pending_cycle == -1) return false;
    SetCycle(visual.pending_cycle);
    visual.finishCycleSelection(nav.GetPosition());
    return true;
}

void SpellVisual::SetCyclePosition(glm::vec3* position)
{
    visual.SetLocation(position);
    nav.SetPositionInternal(position);
}
