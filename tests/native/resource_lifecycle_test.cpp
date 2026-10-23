#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
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
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "temporary_directory.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"
#include "wiz8/entities/emitter_cycles.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/surface_storage.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/camera_pose.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

extern std::shared_ptr<SDL_Surface> g_mouse_surface;
extern SDLSurfaceOwner g_automap_surface;

static void source_resource_owners()
{
    auto model = std::make_shared<Mesh>(0, 1);
    model->InitializeVertexFrames(1);
    const std::weak_ptr<Mesh> lifetime = model;
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
    REQUIRE(lifetime.expired());
}

static void npc_script_teardown()
{
    auto owner = std::make_unique<W8NpcScriptFile>();
    owner->name.assign(80, 'n');
    owner->quotes.resize(2);
    for (auto& quote : owner->quotes) {
        quote.subquotes.assign(2, std::string(80, 'q'));
        quote.entries.resize(2);
        for (auto& entry : quote.entries) {
            entry.sub_entries.resize(2);
            for (auto& sub : entry.sub_entries) sub.text.assign(80, 's');
        }
    }
    owner.reset();
    owner = std::make_unique<W8NpcScriptFile>();
    owner->name.assign(80, 'n');
    // Zero-quote scripts still own their name and header.
}

// A game's transcript and queued NPC messages end with it; the next game starts empty.
static void queued_state_teardown(GameSession& session)
{
    LearnDialogueKeyword("Monastery", W8_DIALOGUE_CATEGORY_PLACES, false);
    LearnDialogueKeyword("Arnika", W8_DIALOGUE_CATEGORY_PLACES, false);
    REQUIRE((static_cast<int>(CurrentGame().story.keywords.size()) == 2));
    auto& dialogue = CurrentGame().npc_dialogue;
    dialogue.QueuePortraitMessage(std::string(80, 't'));
    dialogue.QueuePortraitMessage(std::string(80, 't'), W8ExperienceNoticePayload{});
    dialogue.QueuePortraitMessage(std::string(80, 't'), W8SkillNoticePayload{});
    dialogue.QueuePortraitMessage(std::string(80, 't'), W8LevelUpNotice{});
    REQUIRE(dialogue.has_queued_actions());
    session.begin();
    REQUIRE(CurrentGame().story.keywords.empty());
    REQUIRE((!CurrentGame().npc_dialogue.has_queued_actions()));
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
    ScopedGame game;
    queued_state_teardown(game.session);
}

TEST_CASE("A dead character's events leave every queue", "[resources][lifecycle]")
{
    const ScopedGame game;
    auto& party = CurrentGame().party.characters;
    party[0].fInParty = party[1].fInParty = true;
    // Declared last, so its remaining active event completes while the party is reachable.
    W8CharacterEventQueue queue;
    const auto queues = {&queue.pending_events, &queue.npc_deferred_events, &queue.active_events};
    for (auto* events : queues) {
        for (auto* character : {&party[0], &party[1]})
            events->push_back(std::make_unique<W8CharacterEvent>(character, 0, 0, 0));
    }
    CurrentGame().monster_manager_entries[0].active_character_event =
        queue.active_events.front().get();

    queue.RemoveCharacterEvents(&party[0]);

    for (const auto* events : queues)
        REQUIRE((events->size() == 1 && events->front()->character == &party[1]));
    // Completion releases the portrait's borrowed pointer to the active event.
    REQUIRE(CurrentGame().monster_manager_entries[0].active_character_event == nullptr);
}

TEST_CASE("A new game releases the previous game's queued events", "[resources][lifecycle]")
{
    W8Character character{};
    ScopedGame game;
    CurrentGame().speech.pending_events.push_back(
        std::make_unique<W8CharacterEvent>(&character, 0, 0, 0));
    CurrentGame().autosave_due = std::chrono::seconds{1};
    CurrentGame().level.replacePrimary(std::make_unique<World>());

    // Beginning a game destroys the old one, including its nested events and its worlds.
    game.session.begin();
    REQUIRE(GetWorld() == nullptr);
    REQUIRE(CurrentGame().speech.IsMainQueueEmpty());
    REQUIRE(CurrentGame().autosave_due == Game::kAutosaveInterval);
    game.session.end();
    REQUIRE_FALSE(GameInProgress());
}

static World& PublishEmptyWorld(Game& game)
{
    game.level.replacePrimary(std::make_unique<World>());
    return *game.level.primary;
}

struct SpellVisualFixture {
    ScopedTemporaryDirectory directory{"wiz8-spell-visuals"};
    ScopedAssetRoots roots;
    ScopedGame game;
    World& world = PublishEmptyWorld(game.game);
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
        world.spell_visual_definitions.clear();
        world.spatial.reset();
    }
};

TEST_CASE_METHOD(SpellVisualFixture,
                 "Spell visual fallback uses world-owned sources of the correct type",
                 "[resources][spell]")
{
    auto& definitions = world.spell_visual_definitions;
    auto& named = *definitions.emplace_back(std::make_unique<SpellVisualDefinition>());
    auto& generic = *definitions.emplace_back(std::make_unique<SpellVisualDefinition>());
    named.mode = generic.mode = W8_SPELL_VISUAL_EXPLOSION;
    named.asset_name = "Named";
    generic.asset_name = "Generic";
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
        REQUIRE(visuals.size() == 1);
        REQUIRE(visuals.back().get() == visual);
        visuals.pop_back();
    }
    REQUIRE(definitions.size() == 2);

    // A supported named cycle takes precedence over the fallback, whatever
    // the case of the requested name.
    named.resources.cycles[first_cycle + 2].animation = std::make_unique<Animation>();
    auto* visual = SpawnSpellEffect(&position, "nAmEd", 4, 31, 47);
    REQUIRE(visual);
    REQUIRE(visual->visual.pending_cycle == first_cycle + 2);
    REQUIRE(visual->asset_name == "nAmEd");
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

// An actor must own its animations and light vectors after its definition is released.
template<class Definition, class Host>
static void CheckEmitterCloneLifetime()
{
    auto source = std::make_unique<Definition>();
    const int cycle = static_cast<int>(source->resources.cycles.size()) - 1;
    source->resources.cycles[cycle].animation = std::make_unique<Animation>();
    source->resources.cycles[cycle].animation->playback_scale = 3.5f;
    auto owned_light = CreateLight(nullptr, "EmitterClone");
    auto* light = owned_light.get();
    light->transform.setLocation(11, 22, 33);
    light->m_position = {11, 22, 33};
    source->resources.cycles[cycle].lights.lights.push_back(std::move(owned_light));
    World owner;
    auto copy = std::make_unique<Host>(*source, owner);
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
    CheckEmitterCloneLifetime<MissileDefinition, Missile>();
    CheckEmitterCloneLifetime<SpellVisualDefinition, SpellVisual>();
}

// Valid retail animation header and light; optionally truncate after the light
// to exercise cleanup after resources have actually been acquired.
static void WriteEmitterRecord(wiz8::File& file, unsigned char cycle, bool truncate)
{
    const unsigned char header[] = {11, 0, 1, 0, 0, cycle, 0};
    file.write(header, sizeof(header));
    const float speed = 15, chance = 1;
    file.write(&speed, sizeof(speed));
    const unsigned char frames[] = {0, 0, 0};
    file.write(frames, sizeof(frames));
    file.write(&chance, sizeof(chance));
    const unsigned char reserved[50]{};
    file.write(reserved, sizeof(reserved));
    const unsigned char counts[] = {0, 1, 3}; // bounds, lights, light version
    file.write(counts, sizeof(counts));
    const float light[] = {0, 0, 0, 1, 1, 1, 1, 10}; // position, color, intensity, range
    file.write(light, sizeof(light));
    const unsigned char zero = 0;
    file.write(&zero, 1); // No definition.
    if (!truncate) {
        file.write(&zero, 1); // No standalone path.
        file.write(&zero, 1); // Reserved byte.
    }
}

static void WriteEmitterFixture(unsigned char cycle, bool truncate)
{
    auto file = wiz8::open_file(wiz8::asset_path("emitter.dat"), wiz8::OpenMode::replace);
    WriteEmitterRecord(*file, cycle, truncate);
}

TEST_CASE_METHOD(SpellVisualFixture, "Emitter loads publish only complete valid cycles",
                 "[resources][spell]")
{
    SpellVisualDefinition host;
    host.resources.cycles[2].animation = std::make_unique<Animation>();
    auto* original = host.resources.cycles[2].animation.get();
    for (const auto [cycle, truncate] : {std::pair<unsigned char, bool>{2, true}, {255, false}}) {
        WriteEmitterFixture(cycle, truncate);
        auto file = wiz8::open_file(wiz8::asset_path("emitter.dat"));
        LevelLoadContext info{};
        info.hFile = file.get();
        REQUIRE_FALSE(ReadEmitterCycle(host.resources.cycles, info, -1));
        REQUIRE(host.resources.cycles[2].animation.get() == original);
        REQUIRE(GetWorld()->transient_lights.empty());
    }
    WriteEmitterFixture(2, false);
    for (int i = 0; i < 2; ++i) {
        auto file = wiz8::open_file(wiz8::asset_path("emitter.dat"));
        LevelLoadContext info{};
        info.hFile = file.get();
        REQUIRE(ReadEmitterCycle(host.resources.cycles, info, -1));
        REQUIRE(host.resources.cycles[2].lights.lights.size() == 1);
        REQUIRE(host.resources.cycles[2].lights.lights.front()->scene == nullptr);
    }
}

// Each .mls row loads into the cycle it names, so a visual offers the rows it has.
TEST_CASE_METHOD(SpellVisualFixture, "Spell visual rows load into their named cycles",
                 "[resources][spell]")
{
    std::filesystem::create_directories(directory.path / "assets" / "data" / "spells");
    {
        auto file =
            wiz8::open_file(wiz8::asset_path("data/spells/Boom.mls"), wiz8::OpenMode::replace);
        const std::string text = "EXPLOSION3 BoomCycle\n";
        file->write(text.data(), text.size());
    }
    {
        auto file =
            wiz8::open_file(wiz8::asset_path("data/spells/BoomCycle.mon"), wiz8::OpenMode::replace);
        const unsigned char prefix[] = {1, 0, 0}; // Version, no path, no particles.
        file->write(prefix, sizeof(prefix));
        WriteEmitterRecord(*file, 0, false);
    }
    const glm::vec3 position{1, 2, 3};
    auto* visual = SpawnSpellEffect(&position, "Boom", 5, 0, 0);
    REQUIRE(visual);
    REQUIRE(visual->asset_name == "Boom");
    REQUIRE(visual->visual.pending_cycle == W8_SPELL_VISUAL_EXPLOSION * 7 + 2); // EXPLOSION3
}

// Animations hold shared frame meshes; each player shows every frame on a model of
// its own, so players of one animation animate independently.
TEST_CASE("A cycle player shows every frame on its own model", "[resources][animation]")
{
    Animation animation;
    auto frames = std::make_unique<AnimationMesh>();
    frames->frame_count = 2;
    frames->frames = {{std::make_shared<Mesh>(), {}}, {std::make_shared<Mesh>(), {}}};
    animation.parts[2].push_back({std::move(*frames), std::nullopt});
    const auto* first_frame = animation.parts[2][0].mesh.frames[0].mesh.get();
    const auto* second_frame = animation.parts[2][0].mesh.frames[1].mesh.get();

    World owner;
    CyclePlayer first(owner);
    first.active_animation = &animation;
    CyclePlayer second(owner);
    second.active_animation = &animation;
    auto* shown = first.SelectCycleFrameLod(0, 2);
    REQUIRE(first.SelectCycleFrameLod(1, 2) == shown);
    REQUIRE(shown->getModel() == second_frame);
    auto* other = second.SelectCycleFrameLod(0, 2);
    REQUIRE(other != shown);
    REQUIRE(other->getModel() == first_frame);
    REQUIRE(shown->getModel() == second_frame);
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
        SpellVisual cycle(world);
        auto particle = std::make_unique<ParticleSystem>((world.scene.get()), 1);
        particle->particles[0].active = true;
        particle->callback = std::make_unique<W8MonsterShakeCallback>();
        cycle.visual.particles.emplace_back().m_pstParticles = std::move(particle);
        cycle.visual.RetireParticles();
    }
    REQUIRE(world.retired_particles.size() == 1);
    REQUIRE_FALSE(world.retired_particles.front()->callback);
}

TEST_CASE_METHOD(SpellVisualFixture,
                 "World teardown destroys cycle particles without retiring them",
                 "[resources][world]")
{
    auto previous = game.game.level.replacePrimary(std::make_unique<World>());
    auto* retiring = GetWorld();
    auto visual = std::make_unique<SpellVisual>(*retiring);
    auto particle = std::make_unique<ParticleSystem>(retiring->scene.get(), 1);
    particle->particles[0].active = true;
    auto texture = std::make_shared<Texture>();
    const std::weak_ptr<Texture> released = texture;
    particle->SetTexture(texture);
    texture.reset();
    visual->visual.particles.emplace_back().m_pstParticles = std::move(particle);
    retiring->spell_visuals.push_back(std::move(visual));
    game.game.level.replacePrimary(std::move(previous));
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

TEST_CASE("A destroyed navigator leaves no mover targeting or following it",
          "[resources][navigation]")
{
    World world;
    Navigator follower(world);
    {
        Navigator leader(world);
        follower.target_navigator = &leader;
        follower.linked_navigator = &leader;
    }
    REQUIRE(follower.target_navigator == nullptr);
    REQUIRE(follower.linked_navigator == nullptr);
}

TEST_CASE_METHOD(SpellVisualFixture,
                 "Unpublished navigation cannot change the active world's movement or indexes",
                 "[resources][levels][navigation]")
{
    world.compiledIndex()->registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    Navigator published(world);
    world.party.camera.SetOrientation(0.25f, 0.1f);
    world.party.camera.motion.movement_override = true;
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
    replacement.party.restore(prepared_pose);
    REQUIRE(replacement.party.camera.m_position == prepared_pose.position);
    // The prepared world's party proxy follows its own pose; the active one stays put.
    REQUIRE(replacement.party.position() == navigationPosition(prepared_pose.position));
    REQUIRE(world.party.position() == glm::vec3(0));
    const auto snapshot = replacement.party.pose();
    REQUIRE(snapshot.orientation.yaw == 0.5f);
    REQUIRE(snapshot.retail.yaw == prepared_pose.retail.yaw);
    REQUIRE(snapshot.retail.pitch == prepared_pose.retail.pitch);
    REQUIRE(world.party.camera.m_yaw == 0.25f);
    REQUIRE(world.party.camera.motion.movement_override);
    REQUIRE_FALSE(replacement.party.camera.motion.movement_override);
    replacement.spatial = std::make_unique<LevelSpatialIndex>("", &replacement);
    replacement.compiledIndex()->registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    {
        Navigator prepared(replacement);
        prepared.movement.location_id = 7;
        replacement.compiledIndex()->registry.cells->RegisterObjectCell(W8_OCTREE_KIND_NAVIGATOR, 8,
                                                                        {0, 0, 0});
        prepared.SetScale(3.0f);
        REQUIRE(replacement.compiledIndex()->registry.maximum_navigation_radius == 1500.0f);
        REQUIRE(world.compiledIndex()->registry.maximum_navigation_radius == 500.0f);
        StopAllNavigators();
        REQUIRE(published.halted);
        REQUIRE(world.party.navigator.halted);
        REQUIRE_FALSE(prepared.halted);
        REQUIRE_FALSE(replacement.party.navigator.halted);
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
    const ScopedGameData installed(GameData{.monsters = std::vector<W8MonsterRecord>(1)});
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
    Monster prepared(replacement);
    prepared.location_id = 7;
    REQUIRE(prepared.IsRenderable(false));
    REQUIRE_FALSE(prepared.IsRenderable(true));
    REQUIRE_FALSE(world.population->findMonster(7)->within_viewing_distance);
}

TEST_CASE("Ambient exchanges wait for completion and choose a different respondent", "[events]")
{
    const ScopedGame game;
    auto& characters = CurrentGame().party.characters;
    CurrentGame().advancement.time_passes = true;
    auto& queue = CurrentGame().speech;
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.empty()); // No eligible speaker: retry later.
    for (int i : {0, 1}) {
        CurrentGame().party.members[i].emplace();
        characters[i].fInParty = true;
        characters[i].hp_current = 10;
    }
    CurrentGame().status.greeting_pending = true;
    queue.ProcessFollowUpEvents();
    REQUIRE(queue.pending_events.empty()); // Rejection must not disarm the exchange.
    CurrentGame().status.greeting_pending = false;
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
    const auto now = MonotonicMilliseconds();
    auto completed = std::move(queue.active_events.front());
    queue.active_events.clear();
    queue.RestartFollowUpClock(completed.get());
    REQUIRE(queue.follow_up.value().ready_at >= now + 2000);
    REQUIRE(queue.follow_up.value().ready_at <= MonotonicMilliseconds() + 8000);
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
    REQUIRE(queue.follow_up.value().ready_at <= MonotonicMilliseconds() + 360000);
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
    animation.positioned_parts = true;
    animation.frame_count = 4;
    World owner;
    CyclePlayer player(owner);
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
    auto& source =
        *world.spell_visual_definitions.emplace_back(std::make_unique<SpellVisualDefinition>());
    source.asset_name = "TargetCone";
    source.mode = W8_SPELL_VISUAL_CONE;
    source.resources.cycles[static_cast<int>(W8_SPELL_VISUAL_CONE) * SPELL_CYCLES_PER_GROUP]
        .animation = std::make_unique<Animation>();
    for (int attempt = 0; attempt < 2; ++attempt) {
        SetTargetConeEnabled(true);
        REQUIRE(world.target_cone);
        REQUIRE(world.spell_visuals.size() == 1);
        SetTargetConeEnabled(true);
        REQUIRE(world.spell_visuals.size() == 1);
        SetTargetConeEnabled(false);
        REQUIRE_FALSE(world.target_cone);
        REQUIRE(world.spell_visuals.empty());
        SetTargetConeEnabled(false);
    }
}

TEST_CASE("Rendering derives the views from the party and changes nothing it reads",
          "[resources][camera]")
{
    ScopedGame game;
    World& world = PublishEmptyWorld(game.game);
    world.camera = &world.scene->camera;
    Light light(nullptr);
    world.camera_light = &light;
    World sky;
    sky.camera = &sky.scene->camera;
    world.party.place({10, 20, 30});
    world.party.camera.SetOrientation(.5f, -.25f);
    world.camera->setViewPlane(1.0, 0.75);
    const auto pose = world.party.pose();
    const glm::vec3 shake{1, 2, 3};
    // Two shaken frames, as an extra render or a second view would place.
    for (int frame = 0; frame < 2; ++frame) {
        placeWorldCameras(world, &sky, shake);
        REQUIRE(world.camera->transform.getLocation() == glm::dvec3(11, 22, 33));
        REQUIRE(light.transform.getLocation() == glm::dvec3(11, 22, 33));
        REQUIRE(world.camera->transform.getRotation<glm::mat3>() ==
                world.party.camera.rotationMatrix());
        REQUIRE(sky.camera->transform.getLocation() == glm::dvec3(0));
        REQUIRE(sky.camera->transform.getRotation<glm::mat3>() ==
                world.party.camera.rotationMatrix());
        REQUIRE(sky.camera->getViewPlane().rectangle.right ==
                world.camera->getViewPlane().rectangle.right);
        REQUIRE(sky.camera->getViewPlane().rectangle.top ==
                world.camera->getViewPlane().rectangle.top);
        REQUIRE(world.party.pose() == pose);
        REQUIRE(cameraLocation() == glm::vec3(10, 20, 30));
    }
    placeWorldCameras(world, nullptr, {});
    REQUIRE(world.camera->transform.getLocation() == glm::dvec3(10, 20, 30));
    world.camera_light = nullptr;
}

TEST_CASE("The party navigator stands the world height below the eye",
          "[resources][camera][navigation]")
{
    World world;
    world.party.camera.SetOrientation(.5f, -.25f);
    world.party.place({100, 200, 300});
    const auto& navigator = world.party.navigator;
    REQUIRE(navigator.GetPosition() == glm::vec3(100, 200 - g_default_world_height, 300));
    REQUIRE(navigator.GetPosition() == navigationPosition(world.party.eye()));
    REQUIRE(navigationPosition({0, g_default_world_height, 0}) == glm::vec3(0));
}

TEST_CASE("Queued dialogue keeps its script and voice when the NPC is rebound", "[npc][lifetime]")
{
    ScopedGame scope;
    W8NpcDatabaseRecord record{};
    record.display_name = "Test";
    auto npc = std::make_shared<W8NpcState>();
    npc->record = &record;
    npc->has_monster = true;
    auto script = std::make_shared<W8NpcScriptFile>();

    script->quotes.resize(1);

    script->quotes[0].subquotes.emplace_back("");
    npc->script_file = script;
    auto& dialogue = scope.game.npc_dialogue;
    dialogue.Begin(npc.get(), false);
    dialogue.QueueQuote(0, false, false, false);
    npc->script_file = std::make_shared<W8NpcScriptFile>();
    npc->is_grouped = true;
    dialogue.Begin(npc.get(), false);
    dialogue.Advance(MakeScriptContext(scope.game));
    REQUIRE(dialogue.binding().script == script);
    REQUIRE(dialogue.binding().voice_stem == "NPC_Test");
    REQUIRE(dialogue.binding().npc_voice);
    REQUIRE_FALSE(dialogue.has_queued_actions());
}

TEST_CASE("Skill notices stay with the supplied game", "[resources][notices]")
{
    const ScopedGame active;
    Game detached;
    GameData data;
    data.strings.resize(1024);
    data.strings[0x1d9] = data.strings[0x1db] = "%s %u";
    const ScopedGameData installed(std::move(data));
    AnnounceSkillIncrease(detached, 0, W8_SKILL_COMMUNICATION, true);
    REQUIRE(detached.npc_dialogue.has_queued_actions());
    REQUIRE_FALSE(active.game.npc_dialogue.has_queued_actions());
    const W8SkillNoticePayload notices{{{0, W8_SKILL_COMMUNICATION}, {1, W8_SKILL_COMMUNICATION}}};
    PostSkillIncreaseNotices(detached, notices);
    REQUIRE(detached.events.pending().size() == 2);
    REQUIRE(active.game.events.pending().empty());
}
