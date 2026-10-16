#include "wiz8/engine_code/Spells.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "GrCycle.h"
#include "compat/native.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "monster_cycles.h"
#include "monster_spell_icons.h"
#include "retail_records.h"
#include "soundman.h"
#include "spell_ids.h"
#include "surrender/srMath.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/Emitter.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/SoundEvent.h"
#include "wiz8/engine_code/SpellEmitterHost.h"
#include "wiz8/engine_code/SpellVisual.h"
#include "wiz8/engine_code/emitter_cycles.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stSound3D.h"
#include "wiz8/filesystem.h"
#include "wiz8/float_constants.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/sr_api.h"
#include "wiz8/virtual_file.h"

// GLOBAL: WIZ8 0x0065BE40
std::vector<stSound3D*> g_sound3d_instances;

/* Names of the seven bitmap cycles in each of the four spell-visual groups,
   in load order: FLASH, EXPLOSION, TARGET and CONE. */
// GLOBAL: WIZ8 0x0060CF80
const char* g_spell_cycle_names[28] = {
    "FLASH1",     "FLASH2",     "FLASH3",     "FLASH4",     "FLASH5",     "FLASH6",
    "FLASH7",     "EXPLOSION1", "EXPLOSION2", "EXPLOSION3", "EXPLOSION4", "EXPLOSION5",
    "EXPLOSION6", "EXPLOSION7", "TARGET1",    "TARGET2",    "TARGET3",    "TARGET4",
    "TARGET5",    "TARGET6",    "TARGET7",    "CONE1",      "CONE2",      "CONE3",
    "CONE4",      "CONE5",      "CONE6",      "CONE7",
};

/* The persistent TargetCone visual the targeting code toggles on and off. */
// GLOBAL: WIZ8 0x0065BE20
static W8SpellVisual* g_target_cone_visual;

// FUNCTION: WIZ8 0x004ac9d0
W8SpellTargetType GetSpellTargetType(int spell_id, bool normalize_single_target)
{
    W8SpellTargetType target_type = g_spell_records[spell_id].target_type;

    if (target_type == W8_TARGET_TYPE_ALLY && normalize_single_target) {
        target_type = W8_TARGET_TYPE_CASTER;
    }
    return target_type;
}

// FUNCTION: WIZ8 0x004acb40
int MinimumCasterLevelForSpellLevel(int spell_level)
{
    switch (spell_level) {
    case 2:
        return 3;
    case 3:
        return 5;
    case 4:
        return 8;
    case 5:
        return 11;
    case 6:
        return 14;
    case 7:
        return 18;
    default:
        return 1;
    }
}

// FUNCTION: WIZ8 0x004acba0
int GetMinimumCasterLevelForSpell(int spell_id)
{
    return MinimumCasterLevelForSpellLevel(g_spell_records[spell_id].spell_level);
}

/* The emitter record a spell's visual hangs off. */
// FUNCTION: WIZ8 0x004ac890
W8EmitterHost* W8SpellVisual::GetRepresentation()
{
    return this->host;
}

/* The emitter it is currently coming out of. */
// FUNCTION: WIZ8 0x004ac820
W8AnimObj* W8SpellVisual::GetCurrentAnimation()
{
    return this->host->resources.cycles[this->host->current_cycle].animation.get();
}

/* That emitter's own value. */
// FUNCTION: WIZ8 0x004ac870
float W8SpellVisual::GetCurrentAnimationScale()
{
    return this->host->resources.cycles[this->host->current_cycle].animation->playback_scale;
}

// FUNCTION: WIZ8 0x004ac8a0
W8AniMesh* W8SpellVisual::GetCurrentAniMesh()
{
    W8AnimObj* emitter = this->host->resources.cycles[this->host->current_cycle].animation.get();

    if (emitter == 0) {
        srAssertFail("pao", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x653, 0);
    }
    return emitter->entries[this->host->m_bLOD].get();
}

/* How many emitters the host has, counted by testing each for null. */
// FUNCTION: WIZ8 0x004ac840
signed char W8SpellVisual::GetTotalAnimationCount()
{
    char count = 0;

    if (this->host->resources.cycles[0].animation != 0) {
        count = 1;
    }

    if (this->host->resources.cycles[1].animation != 0) {
        ++count;
    }
    return count;
}

/* Hand the host's LOD to the current animation. */
// FUNCTION: WIZ8 0x004ac360
signed char W8SpellVisual::GetNumSubCycles()
{
    W8AnimObj* animation = GetCurrentAnimation();

    return static_cast<signed char>(AnimObjValue(animation, host->m_bLOD));
}

// FUNCTION: WIZ8 0x004ac4e0
bool W8SpellVisual::IsCycleSupported(signed char cycle)
{
    if (cycle >= SPELL_NUM_CYCLES) {
        srAssertFail("bCycle<SPELL_NUM_CYCLES", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp",
                     0x55a, 0);
    }
    return host->resources.cycles[cycle].animation != 0;
}

/* Reset the host's two frame counters before the ordinary GrCycle step. */
// FUNCTION: WIZ8 0x004ac390
void W8SpellVisual::AdvanceAnimationFrame(int value, int arg_flags)
{
    W8SpellEmitterHost* representation_before;

    host->first_frame = 0;
    representation_before = host;
    representation_before->last_frame = GetNumSubCycles() - 1;
    W8GrCycle::AdvanceAnimationFrame(value, arg_flags);
}

/* Select one of the spell host's 28 emitters and rebuild its light and
   particle attachment state. */
// FUNCTION: WIZ8 0x004ac580
void W8SpellVisual::SetCycle(signed char cycle)
{
    std::vector<stLight*>* lights;

    if (cycle < SPELL_CYCLE_FIRST || cycle > SPELL_CYCLE_LAST) {
        srAssertFail("bCycle >= SPELL_CYCLE_FIRST && bCycle <= SPELL_CYCLE_LAST",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x5c1, 0);
    }

    lights = host->resources.cycles[host->current_cycle].lightView();
    DetachCycleLights(lights);

    StartEmitterCycle(*host, *host->resources.cycles[cycle].animation, cycle);

    lights = host->resources.cycles[cycle].lightView();
    SetLights(lights);
    AttachCycleLights(lights);

    if (m_plsParticles != 0) {
        for (W8GrCycleParticleAttachment* event : *m_plsParticles) {
            event->SelectCycle(cycle);
        }
    }
}

/* Position and orient each live spell visual according to its attachment
   mode, then let the common GrCycle update submit the resulting state. */
// FUNCTION: WIZ8 0x004abe00
void W8SpellVisual::UpdateRepresentation(W8World* world)
{
    glm::mat3 rotation;
    srVector3T<float> camera_position;
    srVector3T<float> position;
    bool apply_rotation = false;

    if (mode == W8_SPELL_VISUAL_FLASH) {
        float angle;
        float pitch;

        position = cameraLocation();
        SetCyclePosition(&position);
        rotation = glm::mat3(1);
        angle = GetCameraYawRadians() - g_monster_rotation_offset;
        rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        pitch = -GetCameraPitchRadians();
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        apply_rotation = true;
    } else if (mode == W8_SPELL_VISUAL_TARGET) {
        W8Monster* monster = GetMonsterByLocationID(location_id);

        if (monster != 0) {
            srModelInstance* instance = GetCurrentModelInstance();

            while (instance != 0) {
                glm::dvec3 scale(scale0, scale0, scale0);
                instance->setScale(scale);
                instance = static_cast<srModelInstance*>(instance->first_child_);
            }

            const auto monster_position = monster->GetPosition();
            if (const auto bounds = monster->GetAnimationBounds()) {
                position = monster_position;
                position.y += (bounds->maximum.y - bounds->minimum.y) * g_float_half;
                SetCyclePosition(&position);
            }
        }
    } else if (mode == W8_SPELL_VISUAL_CONE) {
        GetCurrentModelInstance();
        if (!fixed_transform) {
            camera_position = cameraLocation();
            if (location_id == 0) {
                SetCyclePosition(&camera_position);
                rotation = g_gd_camera->rotationMatrix();
                apply_rotation = true;
            } else {
                W8Monster* monster = GetMonsterByLocationID(location_id);

                if (monster != 0) {
                    if (monster->Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_SPELL &&
                        monster->GetSpellPosition(&position)) {
                        SetCyclePosition(&position);
                    }

                    rotation = glm::mat3(1);
                    float angle = monster->GetYaw();
                    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
                    float pitch = GetElevationAngle(&position, &camera_position);
                    rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
                    apply_rotation = true;
                }
            }
        }
    }

    if (apply_rotation) {
        host->SetRotation(&rotation);
    }

    if (host->billboard) {
        glm::mat3 billboard;
        srVector3T<float> visual_position = GetPosition();
        float angle;

        billboard = glm::mat3(1);
        camera_position = cameraLocation();
        angle =
            GetHeadingAngle(&visual_position, &camera_position) + static_cast<float>(g_camera_pi);
        billboard *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        host->SetRotation(&billboard);
    }

    srModelInstance* instance = GetCurrentModelInstance();
    if (instance != 0) {
        static_cast<stModelInstance*>(instance)->render_flags |= stModelInstance::RENDER_NO_PICK;
    }
    W8GrCycle::UpdateRepresentation(world);
}

static void DestroySpellVisual(W8World* world, W8SpellVisual* visual)
{
    wiz8::erase_first(world->spell_visuals, visual);
    if (visual->m_plsLights) {
        wiz8::clear_and_dispose(*visual->m_plsLights,
                                [world](stLight* light) { WorldRemoveLight(world, light); });
    }
    delete visual;
}

/* Step every live spell visual and drop the finished ones.

   A visual that has not finished, or that an owning spell effect still
   holds, starts and updates in place; a finished one the effect released is
   unlinked from the world collection, hands its lights back through the
   world light boundary, and deletes itself. */
// FUNCTION: WIZ8 0x004aab80
void UpdateWorldSpellVisuals(W8World* world)
{
    if (world == 0) {
        srAssertFail("pWorld", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x130, 0);
    }
    int index = 0;
    int count = static_cast<int>(world->spell_visuals.size());
    while (index < count) {
        W8SpellVisual* visual = world->spell_visuals[index];
        if (visual != 0) {
            visual->DetachRepresentation(world);
            if (!visual->finished || !visual->auto_release) {
                visual->StartIfHostActive();
                visual->UpdateRepresentation(world);
                visual->UpdateNavigation(0, false);
            } else {
                DestroySpellVisual(world, visual);
                --index;
                --count;
            }
        }
        ++index;
    }
}

W8SpellEmitterHost::W8SpellEmitterHost() : value_0ac(0), value_0b0(0), billboard(0) {}

/* Copy the two proven host values and the billboard flag, clone every
   populated animation, and deep-copy all optional per-emitter light
   vectors. */
// FUNCTION: WIZ8 0x004aad20
W8SpellEmitterHost::W8SpellEmitterHost(const W8SpellEmitterHost& other)
    : W8EmitterHost(other), value_0ac(other.value_0ac), value_0b0(other.value_0b0),
      resources(other.resources), billboard(other.billboard)
{
    active = 1;
    current_cycle = other.current_cycle;
}

// FUNCTION: WIZ8 0x004AB340
unsigned char W8SpellEmitterHost::ReadCycleData(W8ReadLevelInfo* info, W8SpellVisual* visual, int,
                                                int emitter_index)
{
    if (info == 0 || info->hFile == 0 || visual == 0) {
        srAssertFail("pInfo && pInfo->hFile && pSpell",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x25a, 0);
    }
    return ReadEmitterCycle(*this, resources.cycles, info, *visual, emitter_index, true);
}

static int FindSpellCycleByName(const char* name)
{
    if (name == 0) {
        srAssertFail("pacName", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x532, 0);
    }
    for (int index = 0; index < SPELL_NUM_CYCLES; ++index) {
        if (_strnicmp(name, g_spell_cycle_names[index], strlen(g_spell_cycle_names[index])) == 0) {
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
// FUNCTION: WIZ8 0x004ab580
bool LoadSpellVisualResource(const W8GrCycleLoadContext* context, const char* name,
                             W8SpellVisualMode group, W8SpellVisual** visual, int)
{
    W8SpellVisual* shared = static_cast<W8SpellVisual*>(FindFirstGrCycleByName(name));
    if (shared != 0 && shared->mode == group) {
        W8SpellVisual* spell = new W8SpellVisual(*shared);
        if (spell == 0) {
            srAssertFail("pSpell", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x406, 0);
        }
        *visual = spell;
        if (*visual == 0) {
            srAssertFail("*ppSpell", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x2f8, 0);
        }
        RegisterGrCycle(name, *visual);
        return true;
    }

    PauseSharedGameTimers();

    bool more = true;
    bool success = true;
    char path[100];
    snprintf(path, sizeof(path), "data\\Spells\\%s.mls", name);
    std::unique_ptr<wiz8::File> handle = [&]() {
        try {
            return wiz8::open_file(path, wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    *visual = 0;
    if (handle == 0) {
        success = false;
    } else {
        char line[100];
        char pac_command[52];
        char pac_name[52];
        char pac_value[52];
        char loop_name[128];
        char wave_path[256];
        int frame;
        int index;
        W8SoundEventKind sound_type;
        float intensity;
        float duration;
        float distance;
        std::unique_ptr<W8SoundEvent> event;

        while (more && success) {
            ReadTextLine(handle.get(), line, sizeof(line), more);
            sscanf(line, "%s %s", pac_name, pac_value);
            if (strlen(line) <= 2) {
                continue;
            }
            index = FindSpellCycleByName(pac_name);
            if (index != -1) {
                if (index / SPELL_CYCLES_PER_GROUP == group) {
                    W8GrCycle* loaded = *visual;
                    if (LoadGrCycle(context, pac_value, &loaded, index, 1, "Data\\Spells", 2, 0)) {
                        *visual = static_cast<W8SpellVisual*>(loaded);
                        RegisterGrCycle(pac_value, *visual);
                        success = true;
                    } else {
                        success = false;
                    }
                }
                continue;
            }

            sound_type = W8_SOUND_EVENT_NONE;
            if (_stricmp(pac_name, "SOUND_FRAME") == 0) {
                sound_type = W8_SOUND_EVENT_FRAME;
            } else if (_stricmp(pac_name, "SOUND_CYCLE") == 0) {
                sound_type = W8_SOUND_EVENT_CYCLE;
            } else {
                if (_stricmp(pac_name, "SHAKE_FRAME") == 0) {
                    intensity = 1.0f;
                    duration = 1.0f;
                    distance = 10.0f;
                    sscanf(line, "%s %s %d %f %f %f", pac_command, pac_name, &frame, &intensity,
                           &duration, &distance);
                    index = FindSpellCycleByName(pac_name);
                    ShakeDefinition effect(duration, true, intensity, distance * g_world_scale, 0);
                    effect.cycle = index;
                    effect.frame = frame;
                    effect.subcycle = 0;
                    (*visual)->AddShakeEffect(effect);
                }
                continue;
            }

            memcpy(loop_name, &g_empty_ambient_name, 2);
            memset(loop_name + 2, 0, sizeof(loop_name) - 2);
            sscanf(line, "%s %s %d %s %s", pac_command, pac_name, &frame, pac_value, loop_name);
            index = FindSpellCycleByName(pac_name);
            if (*visual != 0 && (*visual)->IsCycleSupported(static_cast<signed char>(index))) {
                snprintf(wave_path, sizeof(wave_path), "Data\\Spells\\Sounds\\%s.WAV", pac_value);
                event = std::make_unique<W8SoundEvent>(sound_type, index, frame, 0, wave_path,
                                                       _stricmp(loop_name, "LOOP") == 0);
                if (event != 0) {
                    (*visual)->AddSoundEvent(std::move(event));
                }
            }
        }

        if (handle) handle->close();

        handle.reset();
        if (*visual == 0) {
            srAssertFail(
                "*ppSpell", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x365,
                fmt::format("Spell {} missing cycle of type {:d}", name, fmt::underlying(group)));
        }
        (*visual)->host->billboard = false;
        (*visual)->host->pending_behaviour = W8_ANIMATION_PLAY_ONCE;
    }

    ResumeSharedGameTimers();
    return success;
}

// FUNCTION: WIZ8 0x004ABBB0
W8SpellVisual::W8SpellVisual()
    : mode(W8_SPELL_VISUAL_NONE), finished(0), flag(0), auto_release(1), fixed_transform(0),
      scale0(1.0f), location_id(0)
{
    W8GrObject::kind = 1;
    id = AllocateGrObjectId();
    host = new W8SpellEmitterHost;
    if (host == 0) {
        srAssertFail("m_pRep", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x3c0, 0);
    }
}

/* Release the emitter host, leave the world's spell collection, and unregister
   the common GrCycle identity before the base classes tear down. */
// FUNCTION: WIZ8 0x004abd00
W8SpellVisual::~W8SpellVisual()
{
    SetLights(0);
    delete host;

    wiz8::erase_first(g_world->spell_visuals, this);
    UnregisterGrCycle(this);
}

/* Delete every spell visual owned by a world. Its light list is first handed
   back through the world light boundary, exactly as the retail teardown does. */
// FUNCTION: WIZ8 0x004ac3d0
void DestroyAllSpellVisuals(W8World* world)
{
    while (!world->spell_visuals.empty()) {
        DestroySpellVisual(world, world->spell_visuals.front());
    }
}

/* Tick the visual's animation, but only while the host is live. When the
   kind-7 query reports the current animation needs no tick, the visual is
   finished instead. */
// FUNCTION: WIZ8 0x004abdc0
void W8SpellVisual::StartIfHostActive()
{
    if (this->host->active == 0) {
        return;
    }
    if (QueryHostStateByKind(7) != 0) {
        this->finished = true;
        return;
    }
    W8GrCycle::TickAnimation(1.0f);
}

/* Search backward from a subcycle for the first cycle this visual supports,
   seven cycles per group; -1 when none does. */
// FUNCTION: WIZ8 0x004ac530
int W8SpellVisual::FindSupportedCycle(int group, int subcycle)
{
    for (int index = subcycle; index >= 0; --index) {
        int cycle = group * SPELL_CYCLES_PER_GROUP + index;

        if (IsCycleSupported(static_cast<signed char>(cycle))) {
            return cycle;
        }
    }
    return -1;
}

/* Kind-indexed query over the host's emitter state; the only call site asks
   kind 7. */
// FUNCTION: WIZ8 0x004ac8f0
int W8SpellVisual::QueryHostStateByKind(int arg_kind)
{
    W8SpellEmitterHost* host = this->host;

    switch (arg_kind) {
    case 0:
        return host->ApplyEmitterSetting(host->current_cycle);
    case 1:
        return GetTotalAnimationCount();
    case 2:
        return host->subcycle == host->ApplyEmitterSetting(host->current_cycle) - 1;
    case 3:
        return host->subcycle == 0;
    case 4:
        return host->subcycle;
    case 5:
        return host->ApplyEmitterSetting(host->current_cycle) != 0xffffffff;
    case 6:
        return host->current_cycle;
    case 7:
        return host->animation_playing == 0;
    default:
        return -1;
    }
}

static W8SpellVisual* CreateSpellVisual(std::string_view name, W8SpellVisualMode mode,
                                        int power_level, int value, int flags)
{
    W8GrCycleLoadContext context;
    context.world = g_world;
    context.directory = "Data\\Spells\\Bitmaps";
    const auto load = [&](std::string_view resource) -> W8SpellVisual* {
        W8SpellVisual* visual = nullptr;
        if (!LoadSpellVisualResource(&context, std::string(resource).c_str(), mode, &visual, 1)) {
            return nullptr;
        }
        visual->SetNavigationMode(4);
        visual->active = false;
        visual->SetPitchRollEnabled(true, true);
        g_world->spell_visuals.push_back(visual);
        return visual;
    };

    W8SpellVisual* visual = name.empty() ? nullptr : load(name);
    int cycle = visual ? visual->FindSupportedCycle(mode, power_level - 1) : -1;
    if (visual && cycle == -1) {
        delete visual;
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
        visual->host->pending_cycle = static_cast<signed char>(cycle);
        visual->effect_value = value;
        visual->flags0 = flags;
    }
    return visual;
}

/* Create one spell visual from a named bitmap resource, falling back to the
   Generic visual when the named one is missing or has no supported cycle.
   The visual joins the world's list, is positioned, and carries the caller's
   two extra arguments. The power level picks the EXPLOSION row. */
// FUNCTION: WIZ8 0x004ad430
W8SpellVisual* SpawnSpellEffect(const srVector3T<float>* position, std::string_view resource_name,
                                int power_level, int value, int flags)
{
    if (resource_name.empty()) {
        srAssertFail("pMLS && strlen(pMLS)", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp",
                     0x8b4, 0);
    }
    W8SpellVisual* visual =
        CreateSpellVisual(resource_name, W8_SPELL_VISUAL_EXPLOSION, power_level, value, flags);
    if (visual != 0) {
        visual->host->billboard = true;
        visual->SetPositionInternal(position);
    }
    return visual;
}

/* Spawn a FLASH (camera-anchored) spell visual, falling back to the Generic
   resource when the named one is missing or has no supported cycle. The
   visual is placed at the camera and turned to face the camera's yaw/pitch;
   the power level picks the FLASH row. */
// FUNCTION: WIZ8 0x004ad080
W8SpellVisual* SpawnCameraSpellEffect(std::string_view name, int power_level, int value, int flags)
{
    if (name.empty()) {
        srAssertFail("pMLS && strlen(pMLS)", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp",
                     0x879, 0);
    }
    W8SpellVisual* visual =
        CreateSpellVisual(name, W8_SPELL_VISUAL_FLASH, power_level, value, flags);
    if (visual != 0) {
        srVector3T<float> position;
        glm::mat3 rotation;
        float angle;
        float pitch;

        position = cameraLocation();
        visual->SetCyclePosition(&position);
        rotation = glm::mat3(1);
        angle = GetCameraYawRadians() - g_monster_rotation_offset;
        rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        pitch = -GetCameraPitchRadians();
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        visual->host->SetRotation(&rotation);
    }
    return visual;
}

/* Create a TARGET spell visual anchored to a monster — it sits at the
   monster's position raised by half the bounds height and scales to the
   larger bound extent. Requires a monster; falls back to the Generic
   resource like above. The power level picks the TARGET row. */
// FUNCTION: WIZ8 0x004ad630
W8SpellVisual* CreateMonsterSpellEffect(std::string_view mls_name, int power_level,
                                        W8Monster* monster, int value, int flags)
{
    if (mls_name.empty()) {
        srAssertFail("pMLS && strlen(pMLS)", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp",
                     0x8e9, 0);
    }
    if (monster == 0) {
        srAssertFail("pMonster", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x8ea, 0);
    }
    const auto bounds = monster->GetAnimationBounds();
    if (!bounds) return nullptr;
    W8SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_TARGET, power_level, value, flags);
    if (visual != 0) {
        const auto& [minimum, maximum] = *bounds;
        srVector3T<float> position;
        float height;
        float width;

        visual->host->billboard = true;
        visual->location_id = monster->location_id;
        position = monster->GetPosition();
        position.y += (maximum.y - minimum.y) * g_float_half;
        visual->SetCyclePosition(&position);
        height = maximum.y - minimum.y;
        width = maximum.x - minimum.x;
        if (height <= width) {
            height = width;
        }
        visual->scale0 = height * g_float_one_thousandth;
    }
    return visual;
}

/* Create a CONE spell visual attached to a monster — its position comes from
   the monster's spell socket (or mapped position) and its scale from the
   monster's animation bounds. Without a parent it anchors at the camera with
   the camera's rotation. Falls back to the Generic resource like above. The
   power level picks the CONE row. */
// FUNCTION: WIZ8 0x004ad8a0
W8SpellVisual* CreateAttachedSpellEffect(std::string_view mls_name, int power_level,
                                         W8Monster* parent, int value, int flags)
{
    if (mls_name.empty()) {
        srAssertFail("pMLS && strlen(pMLS)", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp",
                     0x93f, 0);
    }
    const auto bounds = parent ? parent->GetAnimationBounds() : std::nullopt;
    if (parent && !bounds) return nullptr;
    W8SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_CONE, power_level, value, flags);
    if (visual != 0) {
        srVector3T<float> position;

        if (parent != 0) {
            const auto& [minimum, maximum] = *bounds;
            float height;
            float width;

            visual->location_id = parent->location_id;
            height = maximum.y - minimum.y;
            width = maximum.x - minimum.x;
            if (height <= width) {
                height = width;
            }
            visual->scale0 = height * g_float_one_thousandth;
            if (!parent->GetSpellPosition(&position)) {
                parent->GetMappedPosition(&position);
            }
            visual->SetCyclePosition(&position);
        } else {
            glm::mat3 rotation;

            position = cameraLocation();
            rotation = g_gd_camera->rotationMatrix();
            visual->SetCyclePosition(&position);
            visual->host->SetRotation(&rotation);
        }
    }
    return visual;
}

/* Create a CONE spell visual at a caller position with a caller rotation —
   the same named-load-then-Generic-fallback flow as the other spawn
   helpers. The supplied transform is fixed: mode-3 updates do not re-follow
   the monster or camera. */
// FUNCTION: WIZ8 0x004adb20
W8SpellVisual* CreateAimedSpellEffect(std::string_view mls_name, int power_level,
                                      srVector3T<float>* position, glm::mat3* rotation, int value,
                                      int flags)
{
    if (mls_name.empty()) {
        srAssertFail("pMLS && strlen(pMLS)", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp",
                     0x991, 0);
    }
    W8SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_CONE, power_level, value, flags);
    if (visual != 0) {
        visual->SetCyclePosition(position);
        visual->host->SetRotation(rotation);
        visual->fixed_transform = true;
    }
    return visual;
}

/* The persistent TargetCone targeting visual: created the first time the mode
   is enabled, deleted when it is disabled, and kept in representation mode 3
   while it lives. */
// FUNCTION: WIZ8 0x004add30
void SetTargetConeEnabled(bool enabled)
{
    if (enabled) {
        if (g_target_cone_visual == 0) {
            g_target_cone_visual = CreateAttachedSpellEffect("TargetCone", 1, 0, 0, 0);
            if (g_target_cone_visual != 0) {
                g_target_cone_visual->host->pending_behaviour = W8_ANIMATION_NEVER_STOP;
            }
        }
        return;
    }
    if (g_target_cone_visual != 0) {
        delete g_target_cone_visual;
        g_target_cone_visual = 0;
    }
}

/* Send something to one named emitter. The arguments are handed on in the
   reverse of the order they arrive. */
// FUNCTION: WIZ8 0x004ab290
srModelInstance* W8SpellEmitterHost::SetCycleFrameLod(signed char emitter, signed char frame,
                                                      signed char lod)
{
    return AnimObjDispatch(this->resources.cycles[emitter].animation.get(), lod, frame);
}

/* Apply the host setting to one required emitter.  The source assertion names
   that local pointer `pao`; assertions do not replace the following call. */
// FUNCTION: WIZ8 0x004ab2c0
unsigned int W8SpellEmitterHost::ApplyEmitterSetting(signed char emitter)
{
    W8AnimObj* target = this->resources.cycles[emitter].animation.get();

    if (target == 0) {
        srAssertFail("pao", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x200, 0);
    }
    return AnimObjValue(target, this->m_bLOD);
}

/* One named emitter's AniMesh, looked up with the host's own setting; an
   empty slot yields none. */
// FUNCTION: WIZ8 0x004ab310
W8AniMesh* W8SpellEmitterHost::GetEmitterAniMesh(signed char emitter)
{
    W8AnimObj* target = this->resources.cycles[emitter].animation.get();

    if (target == 0) {
        return 0;
    }
    return AnimObjEntry(target, this->m_bLOD, 0);
}

/* The clone slot owns both the 0x37c allocation and the copy-construction
   call. */
// FUNCTION: WIZ8 0x004ade70
W8AnimRepBase* W8SpellEmitterHost::Clone()
{
    return new W8SpellEmitterHost(*this);
}

W8SpellEmitterHost::~W8SpellEmitterHost() = default;

/* Whether one spell id is among the six that occupy a persistent combat
   effect slot - Armormelt, Acid Bomb, Toxic Cloud, Firestorm, Death Cloud and
   Draining Cloud, the spells that leave a standing hazard instead of resolving
   once. MonsterAI walks the same list through
   g_combat_effect_slot_spells to skip re-casting one that is still
   running, and the effect update loop uses it to keep the per-cast
   result/report path off the lingering spells. */
// FUNCTION: WIZ8 0x004aca00
bool IsCombatEffectSlotSpell(int spell_id)
{
    switch (spell_id) {
    case W8_SPELL_ACID_BOMB:
    case W8_SPELL_ARMORMELT:
    case W8_SPELL_TOXIC_CLOUD:
    case W8_SPELL_DRAINING_CLOUD:
    case W8_SPELL_FIRESTORM:
    case W8_SPELL_DEATH_CLOUD:
        return true;
    default:
        return false;
    }
}

/* Release the spell database and forget the version with it, so the two are
   never out of step. */
// FUNCTION: WIZ8 0x004acd50
void ReleaseSpellDatabase(void)
{
    if (g_spell_records != 0) {
        delete[] g_spell_records;
        g_spell_records = 0;
        g_spell_database_version = 0;
    }
}

/* The forty icon resource names under Data\Icons\MonsterSpells. Entries 8 and
   9 share the pooled "Hexed" literal in retail. */
// GLOBAL: WIZ8 0x0060d50c
const char* g_monster_spell_icon_names[40] = {
    "Hexed",           "Diseased",     "Irritated",      "Nauseated",      "Slowed",
    "Afraid",          "Poisoned",     "Silenced",       "Hexed",          "Hexed",
    "Insane",          "Blind",        "Turncoat",       "Webbed",         "Asleep",
    "Paralyzed",       "Unconscious",  "Dracon_Breath",  "Guardian_Angel", "Razor_Cloak",
    "Eye4Eye",         "Haste",        "Super_Man",      "Body_Stone",     "Armor_Plate",
    "Enchanted_Blade", "Magic_Screen", "Missile_Shield", "Armor_Melt",     "Acid_Cloud",
    "Toxic_Cloud",     "Fire_Storm",   "Death_Cloud",    "Draining_Cloud", "Bless",
    "Element_Shield",  "Soul_Shield",  "Ring_Of_Fire",   "Charmed",        "Summoned",
};

/* Attach or remove a spell/condition icon on a monster's representation. The
   add path builds the billboard's texture path, creates the icon item and
   adopts the link record; the remove path finds the record by icon id, pulls
   the item out of the world list, deletes it and frees the record. */
// FUNCTION: WIZ8 0x004acd80
void SetMonsterSpellIcon(W8Monster* pMonster, W8MonsterSpellIconId iIcon, bool add)
{
    W8MonsterRep* pMonRep;
    W8MonsterSpellIcon* pSpellMI;
    std::vector<W8MonsterSpellIcon*>* ppl;
    char path[260];
    int count;
    int index;

    if (pMonster == 0) {
        srAssertFail("pMonster", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x829, 0);
    }
    pMonRep = pMonster->m_pRep;
    if (pMonRep == 0) {
        srAssertFail("pMonRep", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x82b, 0);
    }
    if (pMonRep->GetSpellIcons() == 0) {
        srAssertFail("pMonRep->GetSpellIcons()",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x82c, 0);
    }
    ppl = pMonRep->GetSpellIcons();
    if (iIcon == SPELL_ICON_NONE) {
        srAssertFail("iIcon != SPELL_ICON_NONE",
                     "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x82f, 0);
    }
    if (!add) {
        if (ppl == 0) {
            return;
        }
        count = (ppl ? ppl->size() : 0);
        for (index = 0; index < count; ++index) {
            pSpellMI = (*ppl)[index];
            if (pSpellMI->icon == iIcon) {
                wiz8::erase_first(*ppl, pSpellMI);
                if (pSpellMI == 0) {
                    srAssertFail("pSpellMI", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp",
                                 0x81b, 0);
                }
                if (pSpellMI->psrBMO != 0) {
                    pSpellMI->psrBMO->DetachMesh(g_world);
                    wiz8::erase_first(g_world->plsItems, pSpellMI->psrBMO);
                    if (pSpellMI->psrBMO != 0) {
                        delete pSpellMI->psrBMO;
                    }
                }
                free(pSpellMI);
                index = 0;
                --count;
            }
        }
    } else {
        if (iIcon >= SPELL_NUM_ICONS) {
            srAssertFail("uiIcon < SPELL_NUM_ICONS",
                         "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x80a, 0);
        }
        pSpellMI = static_cast<W8MonsterSpellIcon*>(malloc(sizeof(*pSpellMI)));
        pSpellMI->icon = SPELL_ICON_DRAINED;
        pSpellMI->psrBMO = 0;
        snprintf(path, sizeof(path), "%s\\%s_A.TGA", "Data\\Icons\\MonsterSpells",
                 g_monster_spell_icon_names[iIcon]);
        pSpellMI->icon = iIcon;
        pSpellMI->psrBMO = CreateMonsterIconItem(g_world, path, 1);
        if (pSpellMI->psrBMO == 0) {
            srAssertFail("pSpellMI->psrBMO", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp",
                         0x814, 0);
        }
        if (pSpellMI == 0) {
            srAssertFail("pSpellMI", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x84a, 0);
        }
        ppl->push_back(pSpellMI);
    }
}

// VTABLE: WIZ8 0x005ecf40 W8GrObject
// VTABLE: WIZ8 0x005ecf2c W8Navigator
// class W8SpellVisual
// VTABLE: WIZ8 0x005ecf18 W8SpellEmitterHost
// class W8SpellEmitterHost
// VTABLE: WIZ8 0x005ecfb0
// class stSound3D
// VTABLE: WIZ8 0x005ecfe4
// FUNCTION: WIZ8 0x004AE6D0
stSound3D::stSound3D(const char* name, srNode* parent) : srNode(static_cast<srNode*>(0))
{
    if (parent != 0) {
        setParent(parent, 0);
    }
    wave_name = 0;
    if (name != nullptr) {
        wave_name = static_cast<char*>(malloc(strlen(name) + 1));
        strcpy(wave_name, name);
    }
    unknown_138 = 0;
    sound_handle = nullptr;
    volume = 0x7f;
    falloff = 25000.0f;
    auto_release = false;
    g_sound3d_instances.push_back(this);
}

// FUNCTION: WIZ8 0x004AEAA0
stSound3D::~stSound3D()
{
    if (wave_name != nullptr) {
        free(wave_name);
    }
    if (sound_handle != nullptr) {
        wiz8::sound::stop(sound_handle);
    }
    wiz8::erase_first(g_sound3d_instances, this);
}

// FUNCTION: WIZ8 0x004AEBF0
unsigned char stSound3D::Play(bool loop, bool release_when_done)
{
    if (wave_name == 0) {
        return 0;
    }
    auto options = BuildSoundOptions(cameraLocation());
    if (loop) {
        options.loops = 0;
    }
    sound_handle = wiz8::sound::play(wave_name, std::move(options));
    auto_release = release_when_done;
    return sound_handle != nullptr;
}

// FUNCTION: WIZ8 0x004AECC0
wiz8::sound::PlayOptions stSound3D::BuildSoundOptions(const srVector3T<float>& listener)
{
    const float angle = -GetCameraYawRadians();
    const unsigned int scaled_volume = (volume * g_settings.sound_effects_volume) / 0x7f;
    const glm::mat3 rotation = glm::mat3_cast(glm::angleAxis(angle, glm::vec3(0, 1, 0)));
    const srVector3T<float> node_position = glm::vec3(getLocation());
    const srVector3T<float> offset = node_position - listener;
    const srVector3T<float> listener_offset = listener - node_position;
    return {.volume = static_cast<unsigned int>((g_float_one - listener_offset.Length() / falloff) *
                                                scaled_volume),
            .position = rotation * offset.glmValue()};
}

// FUNCTION: WIZ8 0x004AEC70
bool stSound3D::IsPlaying()
{
    if (sound_handle != nullptr && wiz8::sound::is_playing(sound_handle)) {
        return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x004AEC90
void stSound3D::Stop()
{
    if (sound_handle != nullptr) {
        wiz8::sound::stop(sound_handle);
        sound_handle = nullptr;
    }
}

/* Per-frame 3D sound servicing for the instanced sound list: each live entry
   that stopped playing is dropped (releasing it removes it from the vector, so
   the walk count and index step back together); each playing one is re-aimed
   relative to the camera and re-volumed by distance and the configured
   effects-volume. */
// FUNCTION: WIZ8 0x004aefd0
void Update3DSounds()
{
    srVector3T<float> listener = cameraLocation();
    int index = 0;
    int count = static_cast<int>(g_sound3d_instances.size());
    if (count > 0) {
        do {
            stSound3D* sound = g_sound3d_instances[index];
            if (sound->sound_handle != nullptr) {
                srVector3T<double> world = sound->getWorldSpaceLocation();
                if (!wiz8::sound::is_playing(sound->sound_handle)) {
                    bool dead = sound->auto_release;
                    sound->sound_handle = nullptr;
                    if (dead) {
                        delete sound;
                        --count;
                        --index;
                    }
                } else {
                    srVector3T<float> to_listener;
                    srVector3T<float> offset;
                    srVector3T<float> transformed;
                    glm::mat3 rotation;
                    float distance;
                    float angle;
                    unsigned int volume;

                    to_listener.Set(listener.x - static_cast<float>(world.x),
                                    listener.y - static_cast<float>(world.y),
                                    listener.z - static_cast<float>(world.z));
                    distance = static_cast<float>(sqrt(to_listener.LengthSquared()));
                    if (sound->falloff <= distance) {
                        wiz8::sound::set_volume(sound->sound_handle, 0);
                    } else {
                        angle = -GetCameraYawRadians();
                        rotation = glm::mat3(1);
                        rotation *=
                            glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
                        offset.Set(static_cast<float>(world.x) - listener.x,
                                   static_cast<float>(world.y) - listener.y,
                                   static_cast<float>(world.z) - listener.z);
                        transformed = rotation * offset.glmValue();
                        wiz8::sound::set_position(sound->sound_handle, transformed.glmValue());
                        volume = (sound->volume * g_settings.sound_effects_volume) / 0x7f;
                        wiz8::sound::set_volume(
                            sound->sound_handle,
                            static_cast<std::uint32_t>((g_float_one - distance / sound->falloff) *
                                                       volume));
                    }
                }
            }
            ++index;
        } while (index < count);
    }
}

/* Detach every spell icon the monster's representation owns before the
   cycle goes away. Each icon entry carries the item at +4; the item is
   detached from the world, unlinked from the world's item list, and
   deleted, and the entry storage is freed. */
// FUNCTION: WIZ8 0x004ACF90
void ClearMonsterSpellIcons(W8Monster* monster)
{
    if (monster == 0) {
        srAssertFail("pMonster", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x852, 0);
    }
    W8MonsterRep* rep = monster->m_pRep;
    if (rep == 0) {
        srAssertFail("pMonRep", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x854, 0);
    }
    std::vector<W8MonsterSpellIcon*>* list = rep->spell_icons;
    if (list != 0) {
        unsigned int count = (list ? list->size() : 0);
        for (int index = 0; index < static_cast<int>(count); ++index) {
            W8MonsterSpellIcon* entry = (*list)[index];
            if (entry == 0) {
                srAssertFail("pSpellMI", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x81b,
                             0);
            }
            if (entry->psrBMO != 0) {
                entry->psrBMO->DetachMesh(g_world);
                wiz8::erase_first(g_world->plsItems, entry->psrBMO);
                delete entry->psrBMO;
            }
            free(entry);
        }
        list->clear();
    }
}

// FUNCTION: WIZ8 0x004aca60
bool CanSpellBackfire(int spell_id)
{
    switch (g_spell_records[spell_id].target_type) {
    case W8_TARGET_TYPE_ENEMY:
    case W8_TARGET_TYPE_ENEMY_GROUP:
    case W8_TARGET_TYPE_CONE:
    case W8_TARGET_TYPE_RADIUS:
    case W8_TARGET_TYPE_ALL_ENEMIES:
        /* Hostile spells backfire except Banish (0x4d) and the two creature
           abilities Boiling Blood Explosion (0x76) and Rocket Blast (0x83). */
        switch (spell_id) {
        case W8_SPELL_BANISH:
        case W8_SPELL_BOILING_BLOOD_EXPLOSION:
        case W8_SPELL_ROCKET_BLAST:
            break;
        default:
            return true;
        }
        break;
    case W8_TARGET_TYPE_CASTER:
    case W8_TARGET_TYPE_ALLY:
    case W8_TARGET_TYPE_PARTY:
        /* Friendly spells are safe except Charm (3 - unreachable here, it is
           enemy-targeted), Stamina (0xd), Cure Lesser Condition (0x10), Cure
           Paralysis (0x22), Rest All (0x2c), Haste (0x38), Remove Curse (0x3a)
           and Sane Mind (0x4a). */
        switch (spell_id) {
        case W8_SPELL_CHARM:
        case W8_SPELL_STAMINA:
        case W8_SPELL_CURE_LESSER_COND:
        case W8_SPELL_CURE_PARALYSIS:
        case W8_SPELL_REST_ALL:
        case W8_SPELL_HASTE:
        case W8_SPELL_REMOVE_CURSE:
        case W8_SPELL_SANE_MIND:
            return true;
        }
        break;
    case W8_TARGET_TYPE_LOCK_OR_TRAP:
        /* Only Knock Knock (0x27); Divine Trap (0x12) never backfires. */
        return spell_id == W8_SPELL_KNOCK_KNOCK;
    case W8_TARGET_TYPE_POINT:
    case W8_TARGET_TYPE_ITEM:
    case W8_TARGET_TYPE_NONE:
    case W8_TARGET_TYPE_COUNT:
        break;
    }
    return false;
}

// FUNCTION: WIZ8 0x004acc10
unsigned char InitializeSpellDatabase(void)
try {
    /* Retail reads allocation_count/database_version uninitialised when the
       header FileRead pair short-circuits; preserve that behavior. The global
       receives the allocation count despite its older misleading name. */
    std::unique_ptr<wiz8::File> handle;
    unsigned int index;
    bool ok;
    int allocation_count;
    unsigned int database_version;

    ReleaseSpellDatabase();
    handle = [&]() {
        try {
            return wiz8::open_file("Data\\Databases\\SpellTables.dbs", wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (handle == 0) {
        return 0;
    }
    ok = false;
    if ((handle->read(&allocation_count, 4).bytes == static_cast<std::size_t>(4)) &&
        (handle->read(&database_version, 4).bytes == static_cast<std::size_t>(4))) {
        ok = true;
    }
    g_spell_records = new W8SpellRuntimeRecord[allocation_count];
    if (g_spell_records == 0) {
        srAssertFail("s_pSpellTable", "C:\\Projects\\Wizardry 8\\Engine Code\\Spells.cpp", 0x774,
                     0);
    }
    for (index = 0; index < static_cast<unsigned int>(allocation_count); ++index) {
        if (!ok) {
            goto discard;
        }
        ok = false;
        if ((handle->seek(0x101, wiz8::SeekOrigin::current), true) &&
            (wiz8::retail::read(*handle, g_spell_records[index]).bytes ==
             wiz8::retail::kSpellRuntimeRecordBytes)) {
            ok = true;
        }
    }
    if (!ok) {
    discard:
        delete[] g_spell_records;
        g_spell_records = 0;
    }
    if (handle) handle->close();
    handle.reset();
    g_spell_database_version = allocation_count;
    return ok;
} catch (const std::exception&) {
    return false;
}
