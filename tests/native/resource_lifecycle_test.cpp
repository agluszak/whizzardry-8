#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "local_code/NPCScripting.h"
#include "scoped_asset_roots.h"
#include "temporary_directory.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/compat/native.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"
#include "wiz8/entities/emitter_cycles.h"
#include "wiz8/entities/entity_assets.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/party.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/surface_storage.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/camera_pose.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"
#include "wiz8/xstatus.h"

extern std::shared_ptr<SDL_Surface> g_mouse_surface;
extern SDLSurfaceOwner g_automap_surface;

static void source_resource_owners()
{
    auto model = std::make_shared<Mesh>(0, 1);
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

    REQUIRE_THROWS_AS(
        [&] {
            WorldScene scene;
            auto* instance = scene.addStaticMesh(std::make_unique<MeshInstance>(nullptr));
            instance->setModel(model);
            throw std::runtime_error("Interrupted scene initialization");
        }(),
        std::runtime_error);

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
    AddDialogueTranscriptKeyword("Monastery", W8_DIALOGUE_CATEGORY_PLACES);
    AddDialogueTranscriptKeyword("Arnika", W8_DIALOGUE_CATEGORY_PLACES);
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
    struct RestoreParty {
        W8Party saved = std::exchange(g_status.party, W8Party{});
        ~RestoreParty() { g_status.party = std::move(saved); }
    } restore;
    auto& party = g_status.party.characters;
    party[0].fInParty = party[1].fInParty = true;
    // Declared last, so its remaining active event completes while the party is reachable.
    W8CharacterEventQueue queue;
    const auto queues = {&queue.pending_events, &queue.npc_deferred_events, &queue.active_events};
    for (auto* events : queues) {
        for (auto* character : {&party[0], &party[1]})
            events->push_back(std::make_unique<W8CharacterEvent>(character, 0, 0, 0));
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
        std::make_unique<W8CharacterEvent>(&character, 0, 0, 0));
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
    std::unique_ptr<World> previous_world = ReplaceWorld(std::make_unique<World>());
    World& world = *GetWorld();
    std::vector<std::unique_ptr<SpellVisual>>& visuals = world.spell_visuals;
    std::unique_ptr<LevelSpatialIndex>& octree = world.spatial;

    SpellVisualFixture()
    {
        world.spatial = std::make_unique<LevelSpatialIndex>("", &world);
        std::filesystem::create_directory(directory.path / "assets");
        std::filesystem::create_directory(directory.path / "user");
        roots.configure({(directory.path / "assets").string(), (directory.path / "user").string()});
    }

    ~SpellVisualFixture()
    {
        world.spell_visuals.clear();
        world.spatial.reset();
        ReplaceWorld(std::move(previous_world));
    }
};

TEST_CASE_METHOD(SpellVisualFixture,
                 "Spell visual fallback uses world-owned sources of the correct type",
                 "[resources][spell]")
{
    auto& named = *world.spell_visuals.emplace_back(std::make_unique<SpellVisual>(&world));
    auto& generic = *world.spell_visuals.emplace_back(std::make_unique<SpellVisual>(&world));
    named.mode = generic.mode = W8_SPELL_VISUAL_EXPLOSION;
    named.asset_name = "Named";
    generic.asset_name = "Generic";
    // A projectile with the same name must never be mistaken for a spell visual.
    auto& projectile = *world.missiles.emplace_back(std::make_unique<Missile>(&world));
    projectile.asset_name = "Named";
    const int first_cycle = W8_SPELL_VISUAL_EXPLOSION * 7;
    generic.resources.cycles[first_cycle + 1].animation = std::make_unique<Animation>();
    const glm::vec3 position{11, 22, 33};

    // Missing resources and resources without a supported cycle both use Generic.
    for (const char* name : {"Missing", "Named"}) {
        auto* visual = SpawnSpellEffect(&position, name, 4, 17, 23);
        REQUIRE(visual);
        REQUIRE(visual->visual.pending_cycle == first_cycle + 1);
        REQUIRE(visual->asset_name == "Generic");
        REQUIRE(visual->effect_value == 17);
        REQUIRE(visual->flags0 == 23);
        REQUIRE(visual->billboard);
        REQUIRE(visual->nav.GetPosition() == position);
        REQUIRE(visuals.size() == 3);
        REQUIRE(visuals.back().get() == visual);
        visuals.pop_back();
    }
    REQUIRE(visuals.size() == 2);
    REQUIRE(findSpellVisualAsset(world, "nAmEd", W8_SPELL_VISUAL_EXPLOSION) == &named);

    // A supported named cycle takes precedence over the fallback.
    named.resources.cycles[first_cycle + 2].animation = std::make_unique<Animation>();
    auto* visual = SpawnSpellEffect(&position, "Named", 4, 31, 47);
    REQUIRE(visual);
    REQUIRE(visual->visual.pending_cycle == first_cycle + 2);
    REQUIRE(visual->asset_name == "Named");
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
    const int cycle = static_cast<int>(source->resources.cycles.size()) - 1;
    source->resources.cycles[cycle].animation = std::make_unique<Animation>();
    source->resources.cycles[cycle].animation->playback_scale = 3.5f;
    auto owned_light = CreateLight(nullptr, "EmitterClone");
    auto* light = owned_light.get();
    light->transform.setLocation(11, 22, 33);
    light->m_position = {11, 22, 33};
    source->resources.cycles[cycle].lights.lights.push_back(std::move(owned_light));
    auto copy = std::make_unique<Host>(*source);
    REQUIRE(copy->resources.cycles[cycle].animation != source->resources.cycles[cycle].animation);
    REQUIRE(copy->resources.cycles[cycle].lights.lights.front().get() != light);
    source.reset();
    REQUIRE(copy->resources.cycles[cycle].animation->playback_scale == 3.5f);
    REQUIRE(copy->resources.cycles[cycle].lights.lights.front()->positionalX() == 11);
    REQUIRE(copy->resources.cycles[cycle].lights.lights.front()->scene == nullptr);
    copy.reset();
    REQUIRE(GetWorld()->transient_lights.empty());
}

TEST_CASE_METHOD(SpellVisualFixture, "Emitter clones outlive their source", "[resources][spell]")
{
    CheckEmitterCloneLifetime<Missile>();
    CheckEmitterCloneLifetime<SpellVisual>();
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
    SpellVisual visual;
    auto& host = visual;
    host.visual.current_cycle = 2;
    host.resources.cycles[2].animation = std::make_unique<Animation>();
    auto* original = host.resources.cycles[2].animation.get();
    const auto original_timer = host.visual.timer;
    for (const auto [cycle, truncate] : {std::pair<unsigned char, bool>{2, true}, {255, false}}) {
        WriteEmitterFixture(cycle, truncate);
        auto file = wiz8::open_file(wiz8::asset_path("emitter.dat"));
        LevelLoadContext info{};
        info.hFile = file.get();
        REQUIRE_FALSE(ReadEmitterCycle(visual.visual, host.resources.cycles, &info, -1, false));
        REQUIRE(host.visual.current_cycle == 2);
        REQUIRE(host.visual.timer == original_timer);
        REQUIRE(host.resources.cycles[2].animation.get() == original);
        REQUIRE(visual.visual.m_plsLights == nullptr);
        REQUIRE(GetWorld()->transient_lights.empty());
    }
    WriteEmitterFixture(2, false);
    for (int i = 0; i < 2; ++i) {
        auto file = wiz8::open_file(wiz8::asset_path("emitter.dat"));
        LevelLoadContext info{};
        info.hFile = file.get();
        REQUIRE(ReadEmitterCycle(visual.visual, host.resources.cycles, &info, -1, false));
        REQUIRE(visual.visual.m_plsLights == host.resources.cycles[2].lightView());
        REQUIRE(visual.visual.m_plsLights->size() == 1);
    }
}

TEST_CASE("Shake instances retire before reentrant completion and can restart",
          "[resources][shake]")
{
    ShakeSystem system;
    ShakeDefinition definition(0, false, 1, 0, nullptr);
    int shake_callbacks = 0;
    definition.completion_callback = [&] {
        ++shake_callbacks;
        REQUIRE_FALSE(system.IsActive(definition));
        system.Start(ShakeDefinition(10, false, 1, 0, nullptr), &definition);
    };
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

    definition.duration = std::numeric_limits<float>::infinity();
    definition.completion_callback = {};
    system.Start(definition, &definition);
    for (int frame = 0; frame < 3; ++frame) {
        REQUIRE(system.Update({0, 0, 0}) == 1);
        REQUIRE(system.IsActive(definition));
    }
    system.Stop(definition);
    REQUIRE_FALSE(system.IsActive(definition));
    REQUIRE(system.Update({0, 0, 0}) == 0);
}

TEST_CASE_METHOD(SpellVisualFixture, "Retiring a cycle cancels borrowed monster callbacks",
                 "[resources][particles]")
{
    {
        SpellVisual cycle(&world);
        auto particle = std::make_unique<ParticleSystem>((world.scene.get()), 1);
        particle->active_particle_count = 1;
        particle->callback = std::make_unique<W8MonsterShakeCallback>();
        cycle.visual.particles.emplace_back().m_pstParticles = std::move(particle);
        cycle.visual.RetireParticles(world);
    }
    REQUIRE(world.retired_particles.size() == 1);
    REQUIRE_FALSE(world.retired_particles.front()->callback);
}

TEST_CASE_METHOD(SpellVisualFixture,
                 "World teardown destroys cycle particles without retiring them",
                 "[resources][world]")
{
    auto previous = ReplaceWorld(std::make_unique<World>());
    auto* retiring = GetWorld();
    auto visual = std::make_unique<SpellVisual>(retiring);
    auto particle = std::make_unique<ParticleSystem>(retiring->scene.get(), 1);
    particle->active_particle_count = 1;
    auto texture = std::make_shared<Texture>();
    const std::weak_ptr<Texture> released = texture;
    particle->SetTexture(texture);
    texture.reset();
    visual->visual.particles.emplace_back().m_pstParticles = std::move(particle);
    retiring->spell_visuals.push_back(std::move(visual));
    ReplaceWorld(std::move(previous));
    REQUIRE(released.expired());
}

TEST_CASE("Monster light copies need no source scene attachment", "[lights][ownership][levels]")
{
    WorldScene scene;
    const glm::vec3 first(1.0f), second(0.0f);
    Light source(&scene, true, 500.0f, &first, &second);
    Light copy(source);
    copy.SetRange(250.0f);
    REQUIRE(source.far_end == 500.0f);
    REQUIRE(source.scene == &scene);
    REQUIRE(copy.scene == nullptr);
}

TEST_CASE_METHOD(SpellVisualFixture,
                 "Unpublished navigation cannot change the active world's movement or indexes",
                 "[resources][levels][navigation]")
{
    world.compiledIndex()->registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    Navigator published(&world);
    world.party_camera.SetOrientation(0.25f, 0.1f);
    world.party_camera.motion.movement_override = true;
    published.movement.location_id = 7;
    world.compiledIndex()->registry.cells->RegisterObjectCell(W8_OCTREE_KIND_NAVIGATOR, 8,
                                                              {0, 0, 0});
    World replacement;
    replacement.camera = &replacement.scene->camera;
    CameraPose prepared_pose;
    prepared_pose.position = {10, 20, 30};
    prepared_pose.orientation = {0.5f, 0.2f};
    prepared_pose.retail.yaw.fill(std::byte{0x7b});
    prepared_pose.retail.pitch.fill(std::byte{0x45});
    SetWorldCameraState(&replacement, &prepared_pose);
    REQUIRE(replacement.party_camera.m_position == prepared_pose.position);
    const auto snapshot = cameraState(replacement);
    REQUIRE(snapshot.orientation.yaw == 0.5f);
    REQUIRE(snapshot.retail.yaw == prepared_pose.retail.yaw);
    REQUIRE(snapshot.retail.pitch == prepared_pose.retail.pitch);
    REQUIRE(world.party_camera.m_yaw == 0.25f);
    REQUIRE(world.party_camera.motion.movement_override);
    REQUIRE_FALSE(replacement.party_camera.motion.movement_override);
    replacement.spatial = std::make_unique<LevelSpatialIndex>("", &replacement);
    replacement.compiledIndex()->registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    {
        Navigator prepared(&replacement);
        prepared.movement.location_id = 7;
        replacement.compiledIndex()->registry.cells->RegisterObjectCell(W8_OCTREE_KIND_NAVIGATOR, 8,
                                                                        {0, 0, 0});
        prepared.SetScale(3.0f);
        REQUIRE(replacement.compiledIndex()->registry.maximum_navigation_radius == 1500.0f);
        REQUIRE(world.compiledIndex()->registry.maximum_navigation_radius == 500.0f);
        StopAllNavigators();
        REQUIRE(published.halted);
        REQUIRE_FALSE(prepared.halted);
        prepared.halted = true;
        ResumeAllNavigators();
        REQUIRE_FALSE(published.halted);
        REQUIRE(prepared.halted);
    }
    REQUIRE(replacement.compiledIndex()->registry.cells->by_object.empty());
    REQUIRE(world.compiledIndex()->registry.cells->by_object.size() == 1);
}

TEST_CASE_METHOD(
    SpellVisualFixture,
    "Prepared ground placement uses replacement geometry without changing active geometry",
    "[resources][levels][geometry]")
{
    world.spatial = std::make_unique<LevelSpatialIndex>("", &world);
    world.spatial->collision_available = true;
    World replacement;
    replacement.spatial = std::make_unique<LevelSpatialIndex>("", &replacement);
    replacement.spatial->collision_available = true;
    auto& spatial = replacement.compiledIndex()->m_spatial;
    spatial.m_bounds.minimum = glm::vec3(0);
    spatial.m_clipped_bounds.maximum = glm::vec3(10000);
    spatial.m_node_extent = 1000;
    const auto ground = replacement.compiledIndex()->settleToGround({0, 1500, 0}, false, 0);
    REQUIRE(ground.position.y == 1500);
}

TEST_CASE_METHOD(SpellVisualFixture,
                 "Prepared monster visibility resolves records in its owning population",
                 "[resources][levels][rendering]")
{
    struct RestoreRecord {
        std::unique_ptr<W8MonsterRecord> previous =
            std::exchange(gXStatus.monster_record_cache[0], std::make_unique<W8MonsterRecord>());
        ~RestoreRecord() { gXStatus.monster_record_cache[0] = std::move(previous); }
    } restore;
    auto active = std::make_unique<W8MonsterInfo>();
    active->location_id = 7;
    active->within_viewing_distance = false;
    world.population->monster_roster.add(std::move(active), false);
    World replacement;
    auto record = std::make_unique<W8MonsterInfo>();
    record->location_id = 7;
    record->within_viewing_distance = true;
    record->party_threat.visible_to_player = false;
    replacement.population->monster_roster.add(std::move(record), false);
    Monster prepared(&replacement);
    prepared.location_id = 7;
    REQUIRE(prepared.IsRenderable(false));
    REQUIRE_FALSE(prepared.IsRenderable(true));
    REQUIRE_FALSE(world.population->findMonster(7)->within_viewing_distance);
}

TEST_CASE("Ambient exchanges wait for completion and choose a different respondent", "[events]")
{
    auto& characters = g_status.party.characters;
    auto& slots = g_status.party.rows;
    struct Restore {
        W8Party party = std::exchange(g_status.party, W8Party{});
        bool environment = g_environment_time_enabled;
        bool greeting = g_status.greeting_pending;
        std::unique_ptr<W8CharacterEventQueue> queue = std::move(gXStatus.character_event_queue);
        ~Restore()
        {
            gXStatus.character_event_queue.reset();
            g_status.party = std::move(party);
            g_environment_time_enabled = environment;
            g_status.greeting_pending = greeting;
            gXStatus.character_event_queue = std::move(queue);
        }
    } restore;
    g_environment_time_enabled = true;
    g_status.greeting_pending = false;
    gXStatus.character_event_queue = std::make_unique<W8CharacterEventQueue>();
    auto& queue = *gXStatus.character_event_queue;
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.empty()); // No eligible speaker: retry later.
    for (int i : {0, 1}) {
        slots[i].fOccupied = true;
        characters[i].fInParty = true;
        characters[i].hp_current = 10;
    }
    g_status.greeting_pending = true;
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.empty()); // Rejection must not disarm the exchange.
    g_status.greeting_pending = false;
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.size() == 1);
    const auto* first = queue.pending_events.front()->character;
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.size() == 1); // No new line until completion.

    queue.npc_deferred_events.push_back(std::move(queue.pending_events.front()));
    queue.pending_events.clear();
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.empty());
    queue.active_events.push_back(std::move(queue.npc_deferred_events.front()));
    queue.npc_deferred_events.clear();
    auto& active = *queue.active_events.front();
    active.original_event_type = active.event_type;
    active.event_type = 10; // Coalescing changes the spoken line, not the exchange.
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.empty());
    const auto now = w8_get_ticks();
    auto completed = std::move(queue.active_events.front());
    queue.active_events.clear();
    queue.RestartFollowUpClock(completed.get());
    REQUIRE(queue.follow_up.value().ready_at >= now + 2000);
    REQUIRE(queue.follow_up.value().ready_at <= w8_get_ticks() + 8000);
    queue.follow_up.value().ready_at = 0; // Expire the response delay without sleeping.
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.size() == 1);
    REQUIRE(queue.pending_events.back()->character != first);
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.size() == 1);
    completed = std::move(queue.pending_events.back());
    queue.pending_events.clear();
    queue.RestartFollowUpClock(completed.get());
    REQUIRE(queue.follow_up.value().ready_at >= now + 300000);
    REQUIRE(queue.follow_up.value().ready_at <= w8_get_ticks() + 360000);
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.empty());
    queue.follow_up.value().ready_at = 0;
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.size() == 1);
    queue.pending_events.clear(); // A discarded line does not leave a waiting latch behind.
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.size() == 1);
    queue.follow_up.reset();
    queue.RestartFollowUpClock(queue.pending_events.back().get());
    REQUIRE_FALSE(queue.follow_up); // A late completion cannot re-enable idle dialogue.
}

TEST_CASE("Cycle frame callbacks observe advances and can stop queued steps",
          "[resources][animation]")
{
    Animation animation;
    animation.path_lists = true;
    animation.frame_count = 4;
    CyclePlayer player;
    player.active_animation = &animation;
    player.animation_playing = true;
    player.frame_direction = W8_ANIMATION_FORWARD;
    player.frame_method = W8_ANIMATION_WRAP;
    player.animation_behaviour = W8_ANIMATION_NEVER_STOP;
    player.first_frame = 0;
    player.last_frame = 3;
    player.timer = GameClockMilliseconds() - 100;
    unsigned events = 0;
    player.TickAnimation(100.f, glm::vec3(0), false, [&](unsigned char previous_frame) {
        REQUIRE(previous_frame == events);
        ++events;
        REQUIRE(player.subcycle == events);
        return events == 2;
    });
    REQUIRE(events == 2);
    REQUIRE(player.subcycle == 2);
}

TEST_CASE_METHOD(SpellVisualFixture, "Target cone is removed through its world owner",
                 "[resources][spell][ownership]")
{
    world.camera = &world.scene->camera;
    auto& source = *world.spell_visuals.emplace_back(std::make_unique<SpellVisual>(&world));
    source.asset_name = "TargetCone";
    source.mode = W8_SPELL_VISUAL_CONE;
    source.resources.cycles[static_cast<int>(W8_SPELL_VISUAL_CONE) * SPELL_CYCLES_PER_GROUP]
        .animation = std::make_unique<Animation>();
    for (int attempt = 0; attempt < 2; ++attempt) {
        SetTargetConeEnabled(true);
        REQUIRE(world.target_cone);
        REQUIRE(world.spell_visuals.size() == 2);
        SetTargetConeEnabled(true);
        REQUIRE(world.spell_visuals.size() == 2);
        SetTargetConeEnabled(false);
        REQUIRE_FALSE(world.target_cone);
        REQUIRE(world.spell_visuals.size() == 1);
        SetTargetConeEnabled(false);
    }
}

TEST_CASE("Collecting monster models preserves the selected playback cycle",
          "[resources][animation]")
{
    World world;
    Monster monster(&world);
    for (int cycle = 0; cycle < 2; ++cycle) {
        auto animation = std::make_unique<Animation>();
        animation->path_lists = true;
        animation->frame_count = cycle == 0 ? 4 : 2;
        monster.cycles[cycle].emplace_back(EmitterLights{}, std::move(animation), 1.0f);
    }
    monster.visual.current_cycle = 0;
    monster.visual.current_subcycle = 0;
    monster.visual.active_animation = monster.cycles[0][0].animation.get();
    monster.visual.animation_playing = true;
    std::vector<MeshInstance*> instances;
    monster.CollectModelInstances(instances);
    // A lighting/skin walk must not replace the current four-frame animation
    // with the unrelated two-frame cycle visited last.
    monster.visual.SetSubCycle(3);
    REQUIRE(monster.visual.subcycle == 3);
    REQUIRE(monster.visual.frameCount() == 4);
}
