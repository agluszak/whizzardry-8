#include "wiz8/engine_code/Spells.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
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

#include "GrCycle.h"
#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "monster_cycles.h"
#include "monster_spell_icons.h"
#include "retail_records.h"
#include "spell_ids.h"
#include "surrender/srDebug.h"
#include "surrender/srModelInstance.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AnimRep.hpp"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/SpellVisual.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/emitter_cycles.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/legacy_prop_save_ids.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/retail_text_records.h"

/* Names of the seven bitmap cycles in each of the four spell-visual groups,
   in load order: FLASH, EXPLOSION, TARGET and CONE. */
const char* g_spell_cycle_names[28] = {
    "FLASH1",     "FLASH2",     "FLASH3",     "FLASH4",     "FLASH5",     "FLASH6",
    "FLASH7",     "EXPLOSION1", "EXPLOSION2", "EXPLOSION3", "EXPLOSION4", "EXPLOSION5",
    "EXPLOSION6", "EXPLOSION7", "TARGET1",    "TARGET2",    "TARGET3",    "TARGET4",
    "TARGET5",    "TARGET6",    "TARGET7",    "CONE1",      "CONE2",      "CONE3",
    "CONE4",      "CONE5",      "CONE6",      "CONE7",
};

/* The persistent TargetCone visual the targeting code toggles on and off. */
static W8SpellVisual* g_target_cone_visual;

W8SpellTargetType GetSpellTargetType(int spell_id, bool normalize_single_target)
{
    W8SpellTargetType target_type = g_spell_records[spell_id].target_type;

    if (target_type == W8_TARGET_TYPE_ALLY && normalize_single_target) {
        target_type = W8_TARGET_TYPE_CASTER;
    }
    return target_type;
}

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

int GetMinimumCasterLevelForSpell(int spell_id)
{
    return MinimumCasterLevelForSpellLevel(g_spell_records[spell_id].spell_level);
}

/* The emitter it is currently coming out of. */
W8AnimObj* W8SpellVisual::GetCurrentAnimation()
{
    return this->resources.cycles[this->current_cycle].animation.get();
}

/* That emitter's own value. */
float W8SpellVisual::GetCurrentAnimationScale()
{
    return this->resources.cycles[this->current_cycle].animation->playback_scale;
}

W8AniMesh* W8SpellVisual::GetCurrentAniMesh()
{
    W8AnimObj* emitter = this->resources.cycles[this->current_cycle].animation.get();

    if (emitter == 0) {
        Fail("pao");
    }
    return emitter->entries[this->m_bLOD].get();
}

/* How many emitters the host has, counted by testing each for null. */
signed char W8SpellVisual::GetTotalAnimationCount()
{
    char count = 0;

    if (this->resources.cycles[0].animation != 0) {
        count = 1;
    }

    if (this->resources.cycles[1].animation != 0) {
        ++count;
    }
    return count;
}

/* Hand the host's LOD to the current animation. */
signed char W8SpellVisual::GetNumSubCycles()
{
    W8AnimObj* animation = GetCurrentAnimation();

    return static_cast<signed char>(AnimObjValue(animation, this->m_bLOD));
}

bool W8SpellVisual::IsCycleSupported(signed char cycle)
{
    if (cycle >= SPELL_NUM_CYCLES) {
        Fail("bCycle<SPELL_NUM_CYCLES");
    }
    return this->resources.cycles[cycle].animation != 0;
}

/* Reset the host's two frame counters before the ordinary GrCycle step. */
void W8SpellVisual::AdvanceAnimationFrame(int value, int arg_flags)
{
    this->first_frame = 0;
    this->last_frame = GetNumSubCycles() - 1;
    W8GrCycle::AdvanceAnimationFrame(value, arg_flags);
}

/* Select one of the spell host's 28 emitters and rebuild its light and
   particle attachment state. */
void W8SpellVisual::SetCycle(signed char cycle)
{
    std::vector<std::unique_ptr<stLight>>* lights;

    if (cycle < SPELL_CYCLE_FIRST || cycle > SPELL_CYCLE_LAST) {
        Fail("bCycle >= SPELL_CYCLE_FIRST && bCycle <= SPELL_CYCLE_LAST");
    }

    lights = this->resources.cycles[this->current_cycle].lightView();
    DetachCycleLights(lights);

    StartEmitterCycle(*this, *this->resources.cycles[cycle].animation, cycle);

    lights = this->resources.cycles[cycle].lightView();
    SetLights(lights);
    AttachCycleLights(lights);

    if (!particles.empty()) {
        for (auto& attachment : particles) {
            auto* event = &attachment;
            event->SelectCycle(cycle);
        }
    }
}

/* Position and orient each live spell visual according to its attachment
   mode, then let the common GrCycle update submit the resulting state. */
void W8SpellVisual::UpdateRepresentation(W8World* world)
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
        pitch = -PartyCamera().m_pitch;
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        apply_rotation = true;
    } else if (mode == W8_SPELL_VISUAL_TARGET) {
        W8Monster* monster = GetMonsterByLocationID(location_id);

        if (monster != 0) {
            srModelInstance* instance = GetCurrentModelInstance();

            while (instance != 0) {
                glm::dvec3 scale(scale0, scale0, scale0);
                instance->setScale(scale);
                instance = static_cast<srModelInstance*>(instance->getChild());
            }

            const auto monster_position = monster->GetPosition();
            if (const auto bounds = monster->GetAnimationBounds()) {
                position = monster_position;
                position.y += (bounds->maximum.y - bounds->minimum.y) * 0.5f;
                SetCyclePosition(&position);
            }
        }
    } else if (mode == W8_SPELL_VISUAL_CONE) {
        GetCurrentModelInstance();
        if (!fixed_transform) {
            camera_position = cameraLocation();
            if (location_id == 0) {
                SetCyclePosition(&camera_position);
                rotation = PartyCamera().rotationMatrix();
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
        this->SetRotation(&rotation);
    }

    if (this->billboard) {
        glm::mat3 billboard;
        glm::vec3 visual_position = GetPosition();
        float angle;

        billboard = glm::mat3(1);
        camera_position = cameraLocation();
        angle =
            GetHeadingAngle(&visual_position, &camera_position) + static_cast<float>(g_camera_pi);
        billboard *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        this->SetRotation(&billboard);
    }

    srModelInstance* instance = GetCurrentModelInstance();
    if (instance != 0) {
        static_cast<stModelInstance*>(instance)->render_flags |= stModelInstance::RENDER_NO_PICK;
    }
    W8GrCycle::UpdateRepresentation(world);
}

static void DestroySpellVisual(W8World* world, W8SpellVisual* visual)
{
    visual->RetireParticles(*world);
    std::erase_if(world->spell_visuals,
                  [visual](const auto& owner) { return owner.get() == visual; });
}

/* Step every live spell visual and drop the finished ones.

   A visual that has not finished, or that an owning spell effect still
   holds, starts and updates in place; a finished one the effect released is
   unlinked from the world collection, hands its lights back through the
   world light boundary, and deletes itself. */
void UpdateWorldSpellVisuals(W8World* world)
{
    if (world == 0) {
        Fail("pWorld");
    }
    int index = 0;
    int count = static_cast<int>(world->spell_visuals.size());
    while (index < count) {
        W8SpellVisual* visual = world->spell_visuals[index].get();
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

unsigned char W8SpellVisual::ReadCycleData(W8ReadLevelInfo* info, int, int emitter_index)
{
    if (info == 0 || info->hFile == 0) {
        Fail("pInfo && pInfo->hFile && pSpell");
    }
    return ReadEmitterCycle(*this, resources.cycles, info, emitter_index, true);
}

static int FindSpellCycleByName(const char* name)
{
    if (name == 0) {
        Fail("pacName");
    }
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
std::unique_ptr<W8SpellVisual> LoadSpellVisualResource(W8World* world, std::string_view name,
                                                       W8SpellVisualMode group)
{
    W8SpellVisual* shared = static_cast<W8SpellVisual*>(FindFirstGrCycleByName(name));
    if (shared != 0 && shared->mode == group) {
        auto spell = std::make_unique<W8SpellVisual>(*shared, world);
        if (spell == 0) {
            Fail("pSpell");
        }
        if (!spell) {
            Fail("*ppSpell");
        }
        RegisterGrCycle(name, spell.get());
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
    auto visual = std::make_unique<W8SpellVisual>(world);
    visual->current_cycle = -1;
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

                    success = LoadGrCycle(resource, visual.get(), cycle, 1, "Data\\Spells", 2);
                    if (success) {
                        RegisterGrCycle(resource, visual.get());
                    }
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
                visual->AddShakeEffect(effect);
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
                visual->AddSoundEvent(
                    std::make_unique<W8SoundEvent>(kind, sound_cycle, frame, 0, wave.c_str(),
                                                   SDL_strcasecmp(loop.c_str(), "LOOP") == 0));
            }
        }
        if (!visual) {
            Fail("*ppSpell",
                 fmt::format("Spell {} missing cycle of type {:d}", name, fmt::underlying(group)));
        }
        visual->billboard = false;
        visual->pending_behaviour = W8_ANIMATION_PLAY_ONCE;
    }

    ResumeSharedGameTimers();
    return success ? std::move(visual) : nullptr;
}

W8SpellVisual::W8SpellVisual(W8World* owner)
    : W8GrCycle(owner), mode(W8_SPELL_VISUAL_NONE), finished(0), flag(0), auto_release(1),
      fixed_transform(0), scale0(1.0f), location_id(0)
{
    AllocateLegacyPropSaveId();
}

/* Release the emitter host, leave the world's spell collection, and unregister
   the common GrCycle identity before the base classes tear down. */
W8SpellVisual::~W8SpellVisual()
{
    SetLights(0);

    UnregisterGrCycle(this);
}

/* Tick the visual's animation, but only while the host is live. When the
   kind-7 query reports the current animation needs no tick, the visual is
   finished instead. */
void W8SpellVisual::StartIfHostActive()
{
    if (!this->rep_active) {
        return;
    }
    if (QueryHostState(W8SpellVisualHostStateQuery::AnimationStopped) != 0) {
        this->finished = true;
        return;
    }
    W8GrCycle::TickAnimation(1.0f);
}

/* Search backward from a subcycle for the first cycle this visual supports,
   seven cycles per group; -1 when none does. */
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

/* Query one host-side property of the spell visual. */
int W8SpellVisual::QueryHostState(W8SpellVisualHostStateQuery query)
{
    switch (query) {
    case W8SpellVisualHostStateQuery::EmitterSetting:
        return this->ApplyEmitterSetting(this->current_cycle);
    case W8SpellVisualHostStateQuery::AnimationCount:
        return GetTotalAnimationCount();
    case W8SpellVisualHostStateQuery::AtLastSubcycle:
        return this->subcycle == this->ApplyEmitterSetting(this->current_cycle) - 1;
    case W8SpellVisualHostStateQuery::IsFirstSubcycle:
        return this->subcycle == 0;
    case W8SpellVisualHostStateQuery::Subcycle:
        return this->subcycle;
    case W8SpellVisualHostStateQuery::HasEmitterSetting:
        return this->ApplyEmitterSetting(this->current_cycle) != 0xffffffff;
    case W8SpellVisualHostStateQuery::CurrentCycle:
        return this->current_cycle;
    case W8SpellVisualHostStateQuery::AnimationStopped:
        return this->animation_playing == 0;
    default:
        return -1;
    }
}

static W8SpellVisual* CreateSpellVisual(std::string_view name, W8SpellVisualMode mode,
                                        int power_level, int value, int flags)
{
    const auto load = [&](std::string_view resource) -> W8SpellVisual* {
        auto owner = LoadSpellVisualResource(GetWorld(), resource, mode);
        auto* visual = owner.get();
        if (!visual) {
            return nullptr;
        }
        visual->SetNavigationMode(W8_NAVIGATOR_MODE_PATH_ONLY);
        visual->active = false;
        visual->SetPitchRollEnabled(true, true);
        GetWorld()->spell_visuals.push_back(std::move(owner));
        return visual;
    };

    W8SpellVisual* visual = name.empty() ? nullptr : load(name);
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
        visual->pending_cycle = static_cast<signed char>(cycle);
        visual->effect_value = value;
        visual->flags0 = flags;
    }
    return visual;
}

/* Create one spell visual from a named bitmap resource, falling back to the
   Generic visual when the named one is missing or has no supported cycle.
   The visual joins the world's list, is positioned, and carries the caller's
   two extra arguments. The power level picks the EXPLOSION row. */
W8SpellVisual* SpawnSpellEffect(const glm::vec3* position, std::string_view resource_name,
                                int power_level, int value, int flags)
{
    if (resource_name.empty()) {
        Fail("pMLS && strlen(pMLS)");
    }
    W8SpellVisual* visual =
        CreateSpellVisual(resource_name, W8_SPELL_VISUAL_EXPLOSION, power_level, value, flags);
    if (visual != 0) {
        visual->billboard = true;
        visual->SetPositionInternal(position);
    }
    return visual;
}

/* Spawn a FLASH (camera-anchored) spell visual, falling back to the Generic
   resource when the named one is missing or has no supported cycle. The
   visual is placed at the camera and turned to face the camera's yaw/pitch;
   the power level picks the FLASH row. */
W8SpellVisual* SpawnCameraSpellEffect(std::string_view name, int power_level, int value, int flags)
{
    if (name.empty()) {
        Fail("pMLS && strlen(pMLS)");
    }
    W8SpellVisual* visual =
        CreateSpellVisual(name, W8_SPELL_VISUAL_FLASH, power_level, value, flags);
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
        pitch = -PartyCamera().m_pitch;
        rotation *= glm::mat3_cast(glm::angleAxis(float(pitch), glm::vec3(1, 0, 0)));
        visual->SetRotation(&rotation);
    }
    return visual;
}

/* Create a TARGET spell visual anchored to a monster — it sits at the
   monster's position raised by half the bounds height and scales to the
   larger bound extent. Requires a monster; falls back to the Generic
   resource like above. The power level picks the TARGET row. */
W8SpellVisual* CreateMonsterSpellEffect(std::string_view mls_name, int power_level,
                                        W8Monster* monster, int value, int flags)
{
    if (mls_name.empty()) {
        Fail("pMLS && strlen(pMLS)");
    }
    if (monster == 0) {
        Fail("pMonster");
    }
    const auto bounds = monster->GetAnimationBounds();
    if (!bounds) return nullptr;
    W8SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_TARGET, power_level, value, flags);
    if (visual != 0) {
        const auto& [minimum, maximum] = *bounds;
        glm::vec3 position;
        float height;
        float width;

        visual->billboard = true;
        visual->location_id = monster->location_id;
        position = monster->GetPosition();
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
W8SpellVisual* CreateAttachedSpellEffect(std::string_view mls_name, int power_level,
                                         W8Monster* parent, int value, int flags)
{
    if (mls_name.empty()) {
        Fail("pMLS && strlen(pMLS)");
    }
    const auto bounds = parent ? parent->GetAnimationBounds() : std::nullopt;
    if (parent && !bounds) return nullptr;
    W8SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_CONE, power_level, value, flags);
    if (visual != 0) {
        glm::vec3 position;

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
            visual->scale0 = height * 0.001f;
            if (!parent->GetSpellPosition(&position)) {
                parent->GetMappedPosition(&position);
            }
            visual->SetCyclePosition(&position);
        } else {
            glm::mat3 rotation;

            position = cameraLocation();
            rotation = PartyCamera().rotationMatrix();
            visual->SetCyclePosition(&position);
            visual->SetRotation(&rotation);
        }
    }
    return visual;
}

/* Create a CONE spell visual at a caller position with a caller rotation —
   the same named-load-then-Generic-fallback flow as the other spawn
   helpers. The supplied transform is fixed: mode-3 updates do not re-follow
   the monster or camera. */
W8SpellVisual* CreateAimedSpellEffect(std::string_view mls_name, int power_level,
                                      glm::vec3* position, glm::mat3* rotation, int value,
                                      int flags)
{
    if (mls_name.empty()) {
        Fail("pMLS && strlen(pMLS)");
    }
    W8SpellVisual* visual =
        CreateSpellVisual(mls_name, W8_SPELL_VISUAL_CONE, power_level, value, flags);
    if (visual != 0) {
        visual->SetCyclePosition(position);
        visual->SetRotation(rotation);
        visual->fixed_transform = true;
    }
    return visual;
}

/* The persistent TargetCone targeting visual: created the first time the mode
   is enabled, deleted when it is disabled, and kept in representation mode 3
   while it lives. */
void SetTargetConeEnabled(bool enabled)
{
    if (enabled) {
        if (g_target_cone_visual == 0) {
            g_target_cone_visual = CreateAttachedSpellEffect("TargetCone", 1, 0, 0, 0);
            if (g_target_cone_visual != 0) {
                g_target_cone_visual->pending_behaviour = W8_ANIMATION_NEVER_STOP;
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
srModelInstance* W8SpellVisual::SetCycleFrameLod(signed char emitter, signed char frame,
                                                 signed char lod)
{
    return AnimObjDispatch(this->resources.cycles[emitter].animation.get(), lod, frame);
}

/* Apply the host setting to one required emitter.  The source assertion names
   that local pointer `pao`; assertions do not replace the following call. */
unsigned int W8SpellVisual::ApplyEmitterSetting(signed char emitter)
{
    W8AnimObj* target = this->resources.cycles[emitter].animation.get();

    if (target == 0) {
        Fail("pao");
    }
    return AnimObjValue(target, this->m_bLOD);
}

/* One named emitter's AniMesh, looked up with the host's own setting; an
   empty slot yields none. */
W8AniMesh* W8SpellVisual::GetEmitterAniMesh(signed char emitter)
{
    W8AnimObj* target = this->resources.cycles[emitter].animation.get();

    if (target == 0) {
        return 0;
    }
    return AnimObjEntry(target, this->m_bLOD, 0);
}

/* Whether one spell id is among the six that occupy a persistent combat
   effect slot - Armormelt, Acid Bomb, Toxic Cloud, Firestorm, Death Cloud and
   Draining Cloud, the spells that leave a standing hazard instead of resolving
   once. MonsterAI walks the same list through
   g_combat_effect_slot_spells to skip re-casting one that is still
   running, and the effect update loop uses it to keep the per-cast
   result/report path off the lingering spells. */
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

void ReleaseSpellDatabase()
{
    g_spell_records = {};
}

/* The forty icon resource names under Data\Icons\MonsterSpells. Entries 8 and
   9 share the pooled "Hexed" literal in retail. */
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

void AddMonsterSpellIcon(W8World& world, W8Monster* monster, W8MonsterSpellIconId icon)
{
    if (!monster || icon < 0 || icon >= SPELL_NUM_ICONS) Fail("monster spell icon");
    auto& icons = monster->spell_icons;
    icons.reserve(icons.size() + 1);
    const auto path =
        fmt::format("data/icons/monsterspells/{}_A.TGA", g_monster_spell_icon_names[icon]);
    icons.push_back({icon, CreateMonsterIconItem(world, path.c_str())});
}

void SetMonsterSpellIcon(W8Monster* monster, W8MonsterSpellIconId icon, bool add)
{
    if (!monster || icon < 0 || icon >= SPELL_NUM_ICONS) Fail("monster spell icon");
    auto& icons = monster->spell_icons;
    if (add) {
        AddMonsterSpellIcon(*GetWorld(), monster, icon);
    } else {
        for (auto entry = icons.begin(); entry != icons.end();) {
            if (entry->icon != icon) {
                ++entry;
                continue;
            }
            entry->item->DetachMesh();
            entry = icons.erase(entry);
        }
    }
}

void ClearMonsterSpellIcons(W8Monster* monster)
{
    if (!monster) Fail("monster representation");
    for (const auto& entry : monster->spell_icons) {
        entry.item->DetachMesh();
    }
    monster->spell_icons.clear();
}

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

unsigned char InitializeSpellDatabase(void)
try {
    auto handle = wiz8::open_file(wiz8::asset_path("data/databases/spelltables.dbs"));
    std::int32_t count;
    std::uint32_t version;
    handle->read_exact(&count, sizeof(count));
    handle->read_exact(&version, sizeof(version));
    // Spell IDs in retail records are 16-bit. Each entry starts with a 257-byte label.
    constexpr std::int64_t entry_bytes = 0x101 + wiz8::retail::kSpellRuntimeRecordBytes;
    if (count <= 0 || count > std::numeric_limits<std::uint16_t>::max() ||
        std::int64_t(count) * entry_bytes > handle->size() - handle->tell())
        return false;
    std::vector<W8SpellRuntimeRecord> records(count);
    for (int index = 0; index < count; ++index) {
        handle->seek(0x101, wiz8::SeekOrigin::current);
        if (wiz8::retail::read(*handle, records[index]).bytes !=
            wiz8::retail::kSpellRuntimeRecordBytes)
            return false;
    }
    g_spell_records = std::move(records);
    return true;
} catch (const std::exception&) {
    return false;
}
