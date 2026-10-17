#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "GrCycle.h"
#include "SpellEmitterHost.h"
#include "SpellVisual.h"
#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "local_code/NPCScripting.h"
#include "scoped_asset_roots.h"
#include "surrender/srScene.h"
#include "temporary_directory.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/MonsterLight.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/emitter_cycles.h"
#include "wiz8/engine_code/emitter_resources.h"
#include "wiz8/engine_code/game_timer.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/world.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/surface_storage.h"
#include "wiz8/xstatus.h"
#include "world_fixture.h"

extern std::shared_ptr<SDL_Surface> g_mouse_surface;
extern SDLSurfaceOwner g_automap_surface;

static void source_resource_owners()
{
    auto model = std::make_shared<stMeshModel>(0, 1);
    model->InitializeVertexFrames(1);
    model->GetVertexLocations(0, true, 0.0f);
    // The model remains alive while an interrupted scene releases its adopted instance.
    auto survivor = model;
    std::unique_ptr<SDL_Palette, decltype(&SDL_DestroyPalette)> palette(SDL_CreatePalette(256),
                                                                        SDL_DestroyPalette);
    g_mouse_surface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(2, 2, SDL_PIXELFORMAT_INDEX8).release(), SDL_DestroySurface);
    REQUIRE(SDL_SetSurfacePalette(g_mouse_surface.get(), palette.get()));
    g_automap_surface = CreateCpuStorage(2, 2, SDL_PIXELFORMAT_INDEX8);
    REQUIRE(SDL_SetSurfacePalette(g_automap_surface.get(), palette.get()));

    struct InterruptedScene : srScene {
        explicit InterruptedScene(const std::shared_ptr<stMeshModel>& model)
        {
            auto* instance = adoptChild(std::make_unique<stModelInstance>(nullptr));
            instance->setModel(model);
            throw std::runtime_error("Interrupted scene initialization");
        }
    };
    try {
        auto scene = std::make_unique<InterruptedScene>(model);
        REQUIRE(false);
    } catch (const std::runtime_error&) {
    }

    // These are the actual global owners used by mouse and automap rendering.
    REQUIRE(SDL_FillSurfaceRect(g_mouse_surface.get(), nullptr, 17));
    REQUIRE(*static_cast<unsigned char*>(g_mouse_surface->pixels) == 17);
    g_automap_surface.reset();
    model.reset();
    survivor.reset();
    g_mouse_surface.reset();
    palette.reset();
    REQUIRE((g_mesh_models.empty() && g_decompressed_mesh_bytes == 0));
}

static void npc_script_teardown()
{
    auto owner = std::make_unique<W8NpcScriptFile>();
    owner->name.assign(80, 'n');
    owner->quote_count = 2;
    owner->quotes.resize(2);
    for (auto& quote : owner->quotes) {
        quote.subquote_count = 2;
        quote.subquotes.assign(2, std::string(80, 'q'));
        quote.entry_count = 2;
        quote.entries.resize(2);
        for (auto& entry : quote.entries) {
            entry.sub_entry_count = 2;
            entry.sub_entries.resize(2);
            for (auto& sub : entry.sub_entries) sub.text.assign(80, 's');
        }
    }
    owner.reset();
    owner = std::make_unique<W8NpcScriptFile>();
    owner->name.assign(80, 'n');
    // Zero-quote scripts still own their name and header.
}

static void queued_state_teardown()
{
    AddDialogueTranscriptKeyword("Monastery", 0);
    AddDialogueTranscriptKeyword("Arnika", 0);
    REQUIRE((static_cast<int>(g_npc_interaction_state->dialogue_transcript.size()) == 2));
    ClearNpcDialogueTranscript();
    ClearNpcDialogueTranscript();
    REQUIRE((g_npc_interaction_state->dialogue_transcript.size() == 0));
    g_npc_dialogue.QueuePortraitMessage(std::string(80, 't'));
    g_npc_dialogue.QueuePortraitMessage(std::string(80, 't'), W8ExperienceNoticePayload{});
    g_npc_dialogue.QueuePortraitMessage(std::string(80, 't'), W8SkillNoticePayload{});
    g_npc_dialogue.QueuePortraitMessage(std::string(80, 't'), W8LevelUpNotice{});
    ClearNpcMessageQueue();
    ClearNpcMessageQueue();
    REQUIRE((!g_npc_dialogue.has_queued_actions()));
}

TEST_CASE("Detached SurRender resources outlive shutdown and repeated initialization",
          "[resources][lifecycle]")
{
    source_resource_owners();
    source_resource_owners();
}

TEST_CASE("NPC script owns nested quotes until release", "[resources][lifecycle]")
{
    npc_script_teardown();
}

TEST_CASE("NPC message queues release their contents", "[resources][lifecycle]")
{
    queued_state_teardown();
}

TEST_CASE("A dead character's events leave every queue", "[resources][lifecycle]")
{
    std::array<W8Character, 8> party{};
    party[0].fInParty = party[1].fInParty = true;
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local restoration guard
    struct RestoreParty {
        W8Character* saved;
        ~RestoreParty() { g_status.buffers.Char = saved; }
    } restore{std::exchange(g_status.buffers.Char, party.data())};
    // Declared last, so its remaining active event completes while the party is reachable.
    W8CharacterEventQueue queue;
    const auto queues = {&queue.pending_events, &queue.npc_deferred_events, &queue.active_events};
    for (auto* events : queues) {
        for (auto* character : {&party[0], &party[1]})
            events->push_back(std::make_unique<W8CharacterEvent>(character, 0, 0, 0, 0));
    }
    gXStatus.monster_manager_entries[0].active_character_event = queue.active_events.front().get();

    queue.RemoveCharacterEvents(&party[0]);

    for (const auto* events : queues)
        REQUIRE((events->size() == 1 && events->front()->character == &party[1]));
    // Completion releases the portrait's borrowed pointer to the active event.
    REQUIRE(gXStatus.monster_manager_entries[0].active_character_event == nullptr);
}

TEST_CASE("Gameplay runtime reinitialization releases queued events", "[resources][lifecycle]")
{
    W8Character character{};
    const struct Cleanup {
        ~Cleanup() { DestroyGameplayObjects(); }
    } cleanup;
    InitializeGameplayRuntimeObjects();
    gXStatus.character_event_queue->pending_events.push_back(
        std::make_unique<W8CharacterEvent>(&character, 0, 0, 0, 0));
    gXStatus.gameplay_timer->SetDuration(1.0f);

    // Replacing the status must destroy the old owners, including their nested events.
    InitializeGameplayRuntimeObjects();
    REQUIRE(gXStatus.character_event_queue->IsMainQueueEmpty());
    REQUIRE(gXStatus.gameplay_timer->m_duration_seconds == 300.0f);
    DestroyGameplayObjects();
    DestroyGameplayObjects();
    REQUIRE_FALSE(gXStatus.character_event_queue);
    REQUIRE_FALSE(gXStatus.gameplay_timer);
}

struct SpellVisualFixture {
    ScopedTemporaryDirectory directory{"wiz8-spell-visuals"};
    ScopedAssetRoots roots;
    std::unique_ptr<W8World> previous_world = ReplaceWorld(std::make_unique<W8World>());
    W8World& world = *GetWorld();
    std::vector<std::unique_ptr<W8SpellVisual>>& visuals = world.spell_visuals;
    std::unique_ptr<W8Octree>& octree = world.octree;

    SpellVisualFixture()
    {
        world.octree = std::make_unique<W8Octree>(nullptr, nullptr);
        std::filesystem::create_directory(directory.path / "assets");
        std::filesystem::create_directory(directory.path / "user");
        roots.configure({(directory.path / "assets").string(), (directory.path / "user").string()});
    }

    ~SpellVisualFixture()
    {
        world.spell_visuals.clear();
        world.octree.reset();
        ReplaceWorld(std::move(previous_world));
    }
};

TEST_CASE_METHOD(SpellVisualFixture, "Spell visual fallback and registration", "[resources][spell]")
{
    W8SpellVisual named, generic;
    named.mode = generic.mode = W8_SPELL_VISUAL_EXPLOSION;
    const int first_cycle = W8_SPELL_VISUAL_EXPLOSION * 7;
    generic.host->resources.cycles[first_cycle + 1].animation = std::make_unique<W8AnimObj>();
    RegisterGrCycle("Named", &named);
    RegisterGrCycle("Generic", &generic);
    const glm::vec3 position{11, 22, 33};

    // Missing resources and resources without a supported cycle both use Generic.
    for (const char* name : {"Missing", "Named"}) {
        auto* visual = SpawnSpellEffect(&position, name, 4, 17, 23);
        REQUIRE(visual);
        REQUIRE(visual->host->pending_cycle == first_cycle + 1);
        REQUIRE(std::string_view(visual->GetRegisteredName()) == "Generic");
        REQUIRE(visual->effect_value == 17);
        REQUIRE(visual->flags0 == 23);
        REQUIRE(visual->host->billboard);
        REQUIRE(visual->GetPosition() == position);
        REQUIRE(visuals.size() == 1);
        REQUIRE(visuals.front().get() == visual);
        visuals.clear();
    }
    REQUIRE(visuals.empty());
    REQUIRE(FindFirstGrCycleByName("Named") == &named);

    // A supported named cycle takes precedence over the fallback.
    named.host->resources.cycles[first_cycle + 2].animation = std::make_unique<W8AnimObj>();
    auto* visual = SpawnSpellEffect(&position, "Named", 4, 31, 47);
    REQUIRE(visual);
    REQUIRE(visual->host->pending_cycle == first_cycle + 2);
    REQUIRE(std::string_view(visual->GetRegisteredName()) == "Named");
}

TEST_CASE_METHOD(SpellVisualFixture, "Missing spell visual fallback", "[resources][spell]")
{
    glm::vec3 position{1, 2, 3};
    glm::mat3 rotation{1};
    REQUIRE(SpawnSpellEffect(&position, "Missing", 1, 0, 0) == nullptr);
    REQUIRE(SpawnCameraSpellEffect("Missing", 1, 0, 0) == nullptr);
    REQUIRE(CreateAttachedSpellEffect("Missing", 1, nullptr, 0, 0) == nullptr);
    REQUIRE(CreateAimedSpellEffect("Missing", 1, &position, &rotation, 0, 0) == nullptr);
    REQUIRE(visuals.empty());
}

// A cloned resource must own its animations and light vectors after its source is released.
template<class Host>
static void CheckEmitterCloneLifetime()
{
    auto source = std::make_unique<Host>();
    source->value_0ac = 11;
    source->value_0b0 = 22;
    const int cycle = static_cast<int>(source->resources.cycles.size()) - 1;
    source->resources.cycles[cycle].animation = std::make_unique<W8AnimObj>();
    source->resources.cycles[cycle].animation->playback_scale = 3.5f;
    auto owned_light = CreateLight(nullptr, "EmitterClone");
    auto* light = owned_light.get();
    light->setLocation(11, 22, 33);
    light->m_position = {11, 22, 33};
    source->resources.cycles[cycle].lights.lights.push_back(std::move(owned_light));
    auto copy = std::make_unique<Host>(*source);
    REQUIRE(copy->resources.cycles[cycle].animation != source->resources.cycles[cycle].animation);
    REQUIRE(copy->resources.cycles[cycle].lights.lights.front().get() != light);
    source.reset();
    REQUIRE(copy->resources.cycles[cycle].animation->playback_scale == 3.5f);
    REQUIRE(copy->value_0ac == 11);
    REQUIRE(copy->value_0b0 == 22);
    REQUIRE(copy->resources.cycles[cycle].lights.lights.front()->positionalX() == 11);
    REQUIRE(copy->resources.cycles[cycle].lights.lights.front()->getParent() == nullptr);
    copy.reset();
    REQUIRE(GetWorld()->transient_lights.empty());
}

TEST_CASE_METHOD(SpellVisualFixture, "Emitter clones outlive their source", "[resources][spell]")
{
    CheckEmitterCloneLifetime<W8MissileRep>();
    CheckEmitterCloneLifetime<W8SpellEmitterHost>();
}

TEST_CASE_METHOD(SpellVisualFixture, "Cycle registration owns stable names and ordered borrowers",
                 "[resources][registry]")
{
    W8SpellVisual first, second, other;
    RegisterGrCycle("MixedCase", &first);
    const char* borrowed_name = first.GetRegisteredName();
    RegisterGrCycle("mixedcase", &second);
    RegisterGrCycle("AnotherName", &other);
    REQUIRE(std::string_view(borrowed_name) == "MixedCase");
    REQUIRE(FindFirstGrCycleByName("MIXEDCASE") == &first);
    REQUIRE_FALSE(first.IsSoleRegisteredCycleForName());
    REQUIRE_FALSE(UnregisterGrCycle(&first));
    REQUIRE(FindFirstGrCycleByName("mixedcase") == &second);
    REQUIRE(second.IsSoleRegisteredCycleForName());
    REQUIRE(UnregisterGrCycle(&second));
    REQUIRE(FindFirstGrCycleByName("mixedcase") == nullptr);
    REQUIRE(first.GetRegisteredName() == nullptr);
    REQUIRE(FindFirstGrCycleByName("anothername") == &other);
}

// Valid retail animation header and light; optionally truncate after the light
// to exercise cleanup after resources have actually been acquired.
static void WriteEmitterFixture(unsigned char cycle, bool truncate)
{
    auto file = wiz8::open_file(wiz8::asset_path("emitter.dat"), wiz8::OpenMode::replace);
    const unsigned char header[] = {11, 0, 1, 0, 0, cycle, 0};
    file->write(header, sizeof(header));
    const float speed = 15, chance = 1;
    file->write(&speed, sizeof(speed));
    const unsigned char frames[] = {0, 0, 0};
    file->write(frames, sizeof(frames));
    file->write(&chance, sizeof(chance));
    const unsigned char reserved[50]{};
    file->write(reserved, sizeof(reserved));
    const unsigned char counts[] = {0, 1, 3}; // bounds, lights, light version
    file->write(counts, sizeof(counts));
    const float light[] = {0, 0, 0, 1, 1, 1, 1, 10}; // position, color, intensity, range
    file->write(light, sizeof(light));
    const unsigned char zero = 0;
    file->write(&zero, 1); // No definition.
    if (!truncate) {
        file->write(&zero, 1); // No standalone path.
        file->write(&zero, 1); // Reserved byte.
    }
}

TEST_CASE_METHOD(SpellVisualFixture, "Emitter loads publish only complete valid cycles",
                 "[resources][spell]")
{
    W8SpellVisual visual;
    auto& host = *visual.host;
    host.current_cycle = 2;
    host.resources.cycles[2].animation = std::make_unique<W8AnimObj>();
    auto* original = host.resources.cycles[2].animation.get();
    const auto original_timer = host.timer;
    for (const auto [cycle, truncate] : {std::pair<unsigned char, bool>{2, true}, {255, false}}) {
        WriteEmitterFixture(cycle, truncate);
        auto file = wiz8::open_file(wiz8::asset_path("emitter.dat"));
        W8ReadLevelInfo info{};
        info.hFile = file.get();
        REQUIRE_FALSE(ReadEmitterCycle(host, host.resources.cycles, &info, visual, -1, false));
        REQUIRE(host.current_cycle == 2);
        REQUIRE(host.timer == original_timer);
        REQUIRE(host.resources.cycles[2].animation.get() == original);
        REQUIRE(visual.m_plsLights == nullptr);
        REQUIRE(GetWorld()->transient_lights.empty());
    }
    WriteEmitterFixture(2, false);
    for (int i = 0; i < 2; ++i) {
        auto file = wiz8::open_file(wiz8::asset_path("emitter.dat"));
        W8ReadLevelInfo info{};
        info.hFile = file.get();
        REQUIRE(ReadEmitterCycle(host, host.resources.cycles, &info, visual, -1, false));
        REQUIRE(visual.m_plsLights == host.resources.cycles[2].lightView());
        REQUIRE(visual.m_plsLights->size() == 1);
    }
}

static ShakeSystem* callback_system;
static const ShakeDefinition* callback_key;
static int shake_callbacks;
static void RestartShakeFromCompletion()
{
    ++shake_callbacks;
    REQUIRE_FALSE(callback_system->IsActive(*callback_key));
    ShakeDefinition next(10, false, 1, 0, nullptr);
    callback_system->Start(next, callback_key);
}

TEST_CASE("Shake instances retire before reentrant completion and can restart",
          "[resources][shake]")
{
    ShakeSystem system;
    ShakeDefinition definition(0, false, 1, 0, nullptr);
    definition.completion_callback = RestartShakeFromCompletion;
    callback_system = &system;
    callback_key = &definition;
    shake_callbacks = 0;
    system.Start(definition, &definition);
    system.Start(definition, &definition); // A restart cannot duplicate the keyed instance.
    REQUIRE(system.IsActive(definition));
    REQUIRE(system.Update({0, 0, 0}) == 0);
    REQUIRE(shake_callbacks == 1);
    REQUIRE(system.IsActive(definition));
    REQUIRE(system.Update({0, 0, 0}) == 1);
    system.Stop(definition);
    REQUIRE_FALSE(system.IsActive(definition));
    REQUIRE(system.Update({0, 0, 0}) == 0);
    REQUIRE(shake_callbacks == 1);
}

TEST_CASE_METHOD(SpellVisualFixture, "All emitter animations release before any cycle lights",
                 "[resources][spell]")
{
    struct ObservedLight : stLight {
        explicit ObservedLight(std::size_t& count) : count(count) { ++count; }
        ~ObservedLight() override { --count; }
        std::size_t& count;
    };
    struct ObservedInstance : stModelInstance {
        ObservedInstance(std::vector<std::size_t>& observations, std::size_t& lights)
            : stModelInstance(nullptr), observations(observations), lights(lights)
        {
        }
        ~ObservedInstance() override { observations.push_back(lights); }
        std::vector<std::size_t>& observations;
        std::size_t& lights;
    };
    std::size_t lights = 0;
    std::vector<std::size_t> observations;
    observations.reserve(2);
    {
        EmitterResources<2> resources;
        for (auto& cycle : resources.cycles) {
            auto light = std::make_unique<ObservedLight>(lights);
            cycle.lights.lights.push_back(std::move(light));
            cycle.animation = std::make_unique<W8AnimObj>();
            auto mesh = std::make_unique<W8AniMesh>();
            mesh->meshes.push_back(std::make_unique<ObservedInstance>(observations, lights));
            cycle.animation->entries[0] = std::move(mesh);
        }
    }
    REQUIRE(observations == std::vector<std::size_t>{2, 2});
    REQUIRE(world.transient_lights.empty());
}

TEST_CASE_METHOD(SpellVisualFixture, "Retiring a cycle cancels borrowed monster callbacks",
                 "[resources][particles]")
{
    {
        W8SpellVisual cycle(&world);
        auto particle = std::make_unique<stParticle>(world.dynamic_scene, 1);
        particle->active_particle_count = 1;
        particle->callback = std::make_unique<W8MonsterShakeCallback>();
        cycle.particles.emplace_back().m_pstParticles = std::move(particle);
        cycle.RetireParticles(world);
    }
    REQUIRE(world.retired_particles.size() == 1);
    REQUIRE_FALSE(world.retired_particles.front()->callback);
}

TEST_CASE_METHOD(SpellVisualFixture,
                 "World teardown destroys cycle particles without retiring them",
                 "[resources][world]")
{
    struct ObservedParticle : stParticle {
        ObservedParticle(W8World& owner, bool& destroyed, bool& scene_alive)
            : stParticle(owner.dynamic_scene, 1), owner(owner), destroyed(destroyed),
              scene_alive(scene_alive)
        {
            active_particle_count = 1;
        }
        ObservedParticle(const ObservedParticle&) = delete;
        ~ObservedParticle() override
        {
            destroyed = true;
            // Teardown destroys particles directly while supporting scene data lives.
            scene_alive = owner.static_scene != nullptr && owner.retired_particles.empty();
        }
        W8World& owner;
        bool& destroyed;
        bool& scene_alive;
    };
    bool destroyed = false, scene_alive = false;
    auto previous = ReplaceWorld(std::make_unique<W8World>());
    auto* retiring = GetWorld();
    auto visual = std::make_unique<W8SpellVisual>(retiring);
    auto* attachment = &visual->particles.emplace_back();
    attachment->m_pstParticles =
        std::make_unique<ObservedParticle>(*retiring, destroyed, scene_alive);
    retiring->spell_visuals.push_back(std::move(visual));

    ReplaceWorld(std::move(previous));

    REQUIRE(destroyed);
    REQUIRE(scene_alive);
    REQUIRE(world.retired_particles.empty());
}

TEST_CASE("Monster light copies need no source scene attachment", "[lights][ownership][levels]")
{
    srScene scene(nullptr);
    const glm::vec3 first(1.0f), second(0.0f);
    MonsterLight source(&scene, true, 500.0f, &first, &second);
    MonsterLight copy(source);
    copy.SetRange(250.0f);
    REQUIRE(source.far_end == 500.0f);
    REQUIRE(source.getParent() == &scene);
    REQUIRE(copy.getParent() == nullptr);
}
