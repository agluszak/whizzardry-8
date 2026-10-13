#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "GrCycle.h"
#include "SpellVisual.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srScene.h"
#include "surrender/srClock.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/xstatus.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/AniMesh.h"
#include "wiz8/engine_code/emitter_cycles.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/filesystem.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/game_timer.h"
#include "temporary_directory.h"
#include "scoped_asset_roots.h"
#include "local_code/NPCScripting.h"
#include "surrender/srPixelConvert.h"

extern std::shared_ptr<srColorSurface> g_mouse_surface;
extern std::unique_ptr<srColorSurface> g_automap_surface;


static void shutdown_survivors()
{
    REQUIRE((srInit()));
    auto model = std::make_shared<stMeshModel>(0, 1);
    model->InitializeVertexFrames(1);
    model->GetVertexLocations(0, true, 0.0f);
    // A model whose owner survives srExit() is destroyed after reinitialization.
    auto survivor = model;
    auto palette = srCore.getPalette();
    g_mouse_surface = std::make_shared<srColorSurface>(srPixelConvert::SURFACE_P8, 2, 2);
    g_mouse_surface->setPalette(palette);
    g_automap_surface = std::make_unique<srColorSurface>(srPixelConvert::SURFACE_P8, 2, 2);
    g_automap_surface->setPalette(palette);

    struct InterruptedScene : srScene {
        explicit InterruptedScene(const std::shared_ptr<stMeshModel>& model)
        {
            auto* instance = new stModelInstance(this);
            instance->setModel(model);
            throw std::runtime_error("Interrupted scene initialization");
        }
    };
    try {
        auto scene = std::make_unique<InterruptedScene>(model);
        REQUIRE((false));
    } catch (const std::runtime_error&) {}

    // These are the actual global owners used by mouse and automap rendering.
    REQUIRE((srExit()));
    REQUIRE((srCore.getRegistry() == nullptr));
    g_mouse_surface->setName("Detached mouse surface");
    REQUIRE((g_mouse_surface->getName() == "Detached mouse surface"));
    g_automap_surface.reset(); // Destruction without a live registry.
    REQUIRE((srInit()));
    model.reset();
    survivor.reset();
    g_mouse_surface.reset();
    palette.reset();
    REQUIRE((g_mesh_models.empty() && g_decompressed_mesh_bytes == 0));
    REQUIRE((srExit()));
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
    ReleaseNpcScriptFile(owner.release());
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
    QueueNpcPortraitMessage(std::string(80, 't'));
    QueueNpcPortraitMessage(std::string(80, 't'),
                          W8ExperienceNoticePayload{});
    QueueNpcPortraitMessage(std::string(80, 't'), W8SkillNoticePayload{});
    QueueNpcPortraitMessage(std::string(80, 't'), W8LevelUpNotice{});
    ClearNpcMessageQueue();
    ClearNpcMessageQueue();
    REQUIRE((IsMessageBoxLineQueueEmpty()));
}

TEST_CASE("Detached SurRender resources outlive shutdown and repeated initialization", "[resources][lifecycle]")
{
    shutdown_survivors();
    shutdown_survivors();
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

struct SpellVisualFixture
{
    ScopedTemporaryDirectory directory{"wiz8-spell-visuals"};
    ScopedAssetRoots roots;
    W8World* previous_world = GetWorld();
    srClock* previous_clock = g_shared_timer_base;
    srClock clock;
    W8Octree* previous_octree = g_octree;
    std::unique_ptr<W8Octree> octree;
    W8World world{};
    std::vector<W8SpellVisual*>& visuals = world.spell_visuals;

    SpellVisualFixture()
    {
        REQUIRE(srInit());
        g_shared_timer_base = &clock;
        octree = std::make_unique<W8Octree>(nullptr, nullptr);
        g_octree = octree.get();
        std::filesystem::create_directory(directory.path / "assets");
        std::filesystem::create_directory(directory.path / "user");
        roots.configure({(directory.path / "assets").string(),
                         (directory.path / "user").string(), {"", "", ""}});
        SetCurrentWorld(&world);
    }

    ~SpellVisualFixture()
    {
        DestroyAllSpellVisuals(&world);
        octree.reset();
        g_octree = previous_octree;
        g_shared_timer_base = previous_clock;
        SetCurrentWorld(previous_world);
        srExit();
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
    const srVector3T<float> position{11, 22, 33};

    // Missing resources and resources without a supported cycle both use Generic.
    for (const char* name : {"Missing", "Named"}) {
        std::unique_ptr<W8SpellVisual> visual(SpawnSpellEffect(&position, name, 4, 17, 23));
        REQUIRE(visual);
        REQUIRE(visual->host->pending_cycle == first_cycle + 1);
        REQUIRE(std::string_view(visual->GetRegisteredName()) == "Generic");
        REQUIRE(visual->effect_value == 17);
        REQUIRE(visual->flags0 == 23);
        REQUIRE(visual->host->billboard);
        REQUIRE(visual->GetPosition() == position);
        REQUIRE(visuals == std::vector<W8SpellVisual*>{visual.get()});
    }
    REQUIRE(visuals.empty());
    REQUIRE(FindFirstGrCycleByName("Named") == &named);

    // A supported named cycle takes precedence over the fallback.
    named.host->resources.cycles[first_cycle + 2].animation = std::make_unique<W8AnimObj>();
    std::unique_ptr<W8SpellVisual> visual(SpawnSpellEffect(&position, "Named", 4, 31, 47));
    REQUIRE(visual);
    REQUIRE(visual->host->pending_cycle == first_cycle + 2);
    REQUIRE(std::string_view(visual->GetRegisteredName()) == "Named");
}

TEST_CASE_METHOD(SpellVisualFixture, "Missing spell visual fallback", "[resources][spell]")
{
    srVector3T<float> position{1, 2, 3};
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
    auto* light = CreateWorldLight(nullptr, "EmitterClone");
    light->setLocation(11, 22, 33);
    light->m_position = {11, 22, 33};
    source->resources.cycles[cycle].lights.lights.push_back(light);
    auto copy = std::make_unique<Host>(*source);
    REQUIRE(copy->resources.cycles[cycle].animation != source->resources.cycles[cycle].animation);
    REQUIRE(copy->resources.cycles[cycle].lights.lights.front() != light);
    source.reset();
    REQUIRE(copy->resources.cycles[cycle].animation->playback_scale == 3.5f);
    REQUIRE(copy->value_0ac == 11);
    REQUIRE(copy->value_0b0 == 22);
    REQUIRE(copy->resources.cycles[cycle].lights.lights.front()->positionalX() == 11);
    REQUIRE(g_world->transient_lights.size() == 1);
    copy.reset();
    REQUIRE(g_world->transient_lights.empty());
}

TEST_CASE_METHOD(SpellVisualFixture, "Emitter clones outlive their source", "[resources][spell]")
{
    CheckEmitterCloneLifetime<W8MissileRep>();
    CheckEmitterCloneLifetime<W8SpellEmitterHost>();
}

// Valid retail animation header and light; optionally truncate after the light
// to exercise cleanup after resources have actually been acquired.
static void WriteEmitterFixture(unsigned char cycle, bool truncate)
{
    auto file = wiz8::open_file("emitter.dat", wiz8::OpenMode::replace);
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

TEST_CASE_METHOD(SpellVisualFixture, "Emitter loads publish only complete valid cycles", "[resources][spell]")
{
    W8SpellVisual visual;
    auto& host = *visual.host;
    host.current_cycle = 2;
    host.resources.cycles[2].animation = std::make_unique<W8AnimObj>();
    auto* original = host.resources.cycles[2].animation.get();
    const auto original_timer = host.timer;
    for (const auto [cycle, truncate] : {std::pair<unsigned char, bool>{2, true}, {255, false}}) {
        WriteEmitterFixture(cycle, truncate);
        auto file = wiz8::open_file("emitter.dat");
        W8ReadLevelInfo info{};
        info.hFile = file.get();
        REQUIRE_FALSE(ReadEmitterCycle(host, host.resources.cycles, &info, visual, -1, false));
        REQUIRE(host.current_cycle == 2);
        REQUIRE(host.timer == original_timer);
        REQUIRE(host.resources.cycles[2].animation.get() == original);
        REQUIRE(visual.m_plsLights == nullptr);
        REQUIRE(g_world->transient_lights.empty());
        REQUIRE(g_world->lights_to_update.empty());
    }
    WriteEmitterFixture(2, false);
    for (int i = 0; i < 2; ++i) {
        auto file = wiz8::open_file("emitter.dat");
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

TEST_CASE("Shake instances retire before reentrant completion and can restart", "[resources][shake]")
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

TEST_CASE_METHOD(SpellVisualFixture, "All emitter animations release before any cycle lights", "[resources][spell]")
{
    struct ObservedInstance : stModelInstance {
        explicit ObservedInstance(std::vector<std::size_t>& observations)
            : stModelInstance(nullptr), observations(observations) {}
        ~ObservedInstance() override { observations.push_back(g_world->transient_lights.size()); }
        std::vector<std::size_t>& observations;
    };
    std::vector<std::size_t> observations;
    observations.reserve(2);
    {
        EmitterResources<2> resources;
        for (auto& cycle : resources.cycles) {
            cycle.lights.lights.push_back(CreateWorldLight(&world, "TeardownOrder"));
            cycle.animation = std::make_unique<W8AnimObj>();
            auto mesh = std::make_unique<W8AniMesh>();
            mesh->meshes.push_back(std::make_unique<ObservedInstance>(observations));
            cycle.animation->entries[0] = std::move(mesh);
        }
    }
    REQUIRE(observations == std::vector<std::size_t>{2, 2});
    REQUIRE(world.transient_lights.empty());
}
