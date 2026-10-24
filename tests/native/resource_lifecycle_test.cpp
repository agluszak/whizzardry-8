#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "local_code/NPCScripting.h"
#include "scoped_asset_roots.h"
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "temporary_directory.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/npc_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"
#include "wiz8/entities/emitter_cycles.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/script.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/npc_dialogue.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/message_box.h"
#include "wiz8/monster_actions.h"
#include "wiz8/monster_cycles.h"
#include "wiz8/navigation_flags.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/party_quote_input.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/random.h"
#include "wiz8/save_game.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/story.h"
#include "wiz8/surface_storage.h"
#include "wiz8/ui_input.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/camera_pose.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

extern std::shared_ptr<SDL_Surface> g_mouse_surface;

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

    REQUIRE_THROWS_AS(
        [&] {
            WorldScene scene;
            auto* instance = scene.addStaticMesh(std::make_unique<MeshInstance>(nullptr));
            instance->setModel(model);
            throw std::runtime_error("Interrupted scene initialization");
        }(),
        std::runtime_error);

    // This is the actual global owner used by mouse rendering.
    REQUIRE(SDL_FillSurfaceRect(g_mouse_surface.get(), nullptr, 17));
    REQUIRE(*static_cast<unsigned char*>(g_mouse_surface->pixels) == 17);
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

TEST_CASE("A dead character's events leave every queue of their own game", "[resources][lifecycle]")
{
    const ScopedGame current;
    // The queue's game is not the current one: completion must act on its owner.
    Game owner{current.game.data};
    auto& party = owner.party.characters;
    party[0].fInParty = party[1].fInParty = true;
    auto& queue = owner.speech;
    const auto queues = {&queue.pending_events, &queue.npc_deferred_events, &queue.active_events};
    for (auto* events : queues) {
        for (auto* character : {&party[0], &party[1]})
            events->push_back(std::make_unique<W8CharacterEvent>(queue, character, 51, 0, 0));
    }
    owner.monster_manager_entries[0].active_character_event = queue.active_events.front().get();

    owner.monster_manager_entries[0].portrait_event_active = true;
    unsigned int presented = 0;
    owner.events.observe([&](const Event& event) {
        REQUIRE(std::holds_alternative<PortraitSpeechChanged>(event));
        for (const auto* events : queues)
            REQUIRE((events->size() == 1 && events->front()->character == &party[1]));
        ++presented;
    });
    std::vector<Event> outcomes;
    queue.RemoveCharacterEvents(&party[0], outcomes);
    REQUIRE(presented == 0);
    owner.events.publish(std::move(outcomes));
    REQUIRE(presented == 1);
    owner.events.observe({});

    for (const auto* events : queues)
        REQUIRE((events->size() == 1 && events->front()->character == &party[1]));
    // Completion releases the portrait's borrowed pointer and runs its consequences in
    // the owning game only.
    REQUIRE(owner.monster_manager_entries[0].active_character_event == nullptr);
    REQUIRE(owner.npc_dialogue.has_queued_actions());
    REQUIRE_FALSE(CurrentGame().npc_dialogue.has_queued_actions());
    // Destroying the owner drops the remaining events without completing them.
}

TEST_CASE("A new game releases the previous game's queued events", "[resources][lifecycle]")
{
    W8Character character{};
    ScopedGame game;
    CurrentGame().speech.pending_events.push_back(
        std::make_unique<W8CharacterEvent>(CurrentGame().speech, &character, 0, 0, 0));
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

TEST_CASE("Pending outcomes do not survive into a replacement game", "[lifetime][events]")
{
    ScopedGame scope;
    scope.game.events.push(PartySlotChanged{2});
    REQUIRE(scope.game.events.pending().size() == 1);

    auto& next = scope.session.begin();

    REQUIRE(next.events.pending().empty());
    REQUIRE(scope.session.game() == &next);
    std::vector<Game*> sources;
    scope.session.observe([&](Game& source, const Event&) { sources.push_back(&source); });
    next.events.push(PartySlotChanged{3});
    REQUIRE(sources == std::vector<Game*>{&next});
    auto& replacement = scope.session.begin();
    replacement.events.push(PartySlotChanged{4});
    REQUIRE(sources.size() == 2);
    REQUIRE(sources.back() == &replacement);
    scope.session.observe({});
}

TEST_CASE("Level departure belongs to the game, not screen teardown", "[lifetime][level]")
{
    ScopedGame scope;
    auto replacement = std::make_unique<World>();
    World* world = replacement.get();
    scope.game.level.replacePrimary(std::move(replacement));
    REQUIRE(GetWorld() == world);

    scope.game.leaveLevel();

    REQUIRE(GetWorld() == nullptr);
    REQUIRE(scope.session.game() == &scope.game);
}

TEST_CASE("A rejected destination leaves the loaded world current", "[lifetime][level]")
{
    ScopedGame scope;
    auto replacement = std::make_unique<World>();
    World* world = replacement.get();
    scope.game.level.replacePrimary(std::move(replacement));

    REQUIRE_FALSE(scope.game.travelToLevel(999, -1));

    REQUIRE(scope.game.level.primary.get() == world);
    REQUIRE(GetWorld() == world);
    REQUIRE_FALSE(scope.game.level.sky);
}

static World& PublishEmptyWorld(Game& game)
{
    game.level.replacePrimary(
        std::make_unique<World>(-1, "", game.timerClocks(), &game.camera_shakes));
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
    game.game.timerClocks().pause();
    REQUIRE(SpawnSpellEffect(&position, "Missing", 1, 0, 0) == nullptr);
    REQUIRE(SpawnCameraSpellEffect("Missing", 1, 0, 0) == nullptr);
    REQUIRE(CreateAttachedSpellEffect("Missing", 1, nullptr, 0, 0) == nullptr);
    REQUIRE(CreateAimedSpellEffect("Missing", 1, &position, &rotation, 0, 0) == nullptr);
    REQUIRE(visuals.empty());
    REQUIRE(game.game.timerClocks().held()); // Resource decoding cannot release a caller's hold.
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
        auto particle = std::make_unique<ParticleSystem>(&world, 1);
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
    auto particle = std::make_unique<ParticleSystem>(retiring, 1);
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
        world.setNavigatorsPaused(true);
        REQUIRE(published.halted);
        REQUIRE(world.party.navigator.halted);
        REQUIRE_FALSE(prepared.halted);
        REQUIRE_FALSE(replacement.party.navigator.halted);
        world.navigation_changed = replacement.navigation_changed = false;
        const glm::vec3 moved{100, 100, 100};
        prepared.SetPositionInternal(&moved);
        REQUIRE(replacement.navigation_changed);
        REQUIRE_FALSE(world.navigation_changed);
        prepared.halted = true;
        world.setNavigatorsPaused(false);
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
    W8MonsterRecord species{};
    Monster prepared(replacement, &species);
    prepared.location_id = 7;
    REQUIRE(prepared.IsRenderable(false));
    REQUIRE_FALSE(prepared.IsRenderable(true));
    REQUIRE_FALSE(world.population->findMonster(7)->within_viewing_distance);

    world.camera = &world.scene->camera;
    replacement.camera = &replacement.scene->camera;
    for (auto* camera : {world.camera, replacement.camera}) {
        camera->setClipRange(1, 100);
        camera->setViewPlane(Camera3D::Rect{-1, -1, 1, 1}, 1);
    }
    world.camera->transform.setLocation(100, 0, 0);
    replacement.camera->transform.setLocation(0, 0, 0);
    REQUIRE(replacement.projectsToCamera({0, 0, 10}));
    REQUIRE_FALSE(world.projectsToCamera({0, 0, 10}));

    for (auto* owner : {&world, &replacement}) {
        auto observer = std::make_unique<W8MonsterInfo>();
        observer->location_id = 8;
        observer->fActive = true;
        observer->monster_visibility = std::make_unique<W8MonsterVisibility>();
        (*observer->monster_visibility)[7] = {};
        owner->population->monster_roster.add(std::move(observer), false);
    }
    replacement.population->releaseMonsterVisibility(*replacement.population->findMonster(7));
    REQUIRE(replacement.population->findMonster(8)->monster_visibility->empty());
    REQUIRE(world.population->findMonster(8)->monster_visibility->contains(7));
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
    scope.game.beginNpcDialogue(npc.get(), false);
    dialogue.QueueQuote(0, false, false, false);
    npc->script_file = std::make_shared<W8NpcScriptFile>();
    npc->is_grouped = true;
    scope.game.beginNpcDialogue(npc.get(), false);
    dialogue.Advance(MakeScriptContext(scope.game));
    REQUIRE(dialogue.binding().script == script);
    REQUIRE(dialogue.binding().voice_stem == "NPC_Test");
    REQUIRE(dialogue.binding().npc_voice);
    REQUIRE_FALSE(dialogue.has_queued_actions());
}

TEST_CASE("Skill notices stay with the supplied game", "[resources][notices]")
{
    const ScopedGame active;
    GameData data;
    data.strings.resize(1024);
    data.strings[0x1d9] = data.strings[0x1db] = "%s %u";
    Game detached(data);
    AnnounceSkillIncrease(detached, 0, W8_SKILL_COMMUNICATION, true);
    REQUIRE(detached.npc_dialogue.has_queued_actions());
    REQUIRE_FALSE(active.game.npc_dialogue.has_queued_actions());
    const W8SkillNoticePayload notices{{{0, W8_SKILL_COMMUNICATION}, {1, W8_SKILL_COMMUNICATION}}};
    PostSkillIncreaseNotices(detached, notices);
    REQUIRE(detached.events.pending().size() == 2);
    REQUIRE(active.game.events.pending().empty());
}

TEST_CASE(
    "Session replacement releases outgoing presentation before construction and clears failure",
    "[lifetime][save]")
{
    ScopedGame scope;
    auto* outgoing = scope.session.game();
    scope.session.seed(1);
    REQUIRE(outgoing->rng.next() == 41);
    SavedGame saved;
    saved.state.status.resume_level = 100; // No such level: fail without requiring retail assets.
    bool released = false;
    REQUIRE_THROWS_WITH(scope.session.load(std::move(saved),
                                           [&] {
                                               REQUIRE(scope.session.game() == outgoing);
                                               released = true;
                                           }),
                        "cannot construct saved level");
    REQUIRE(released);
    REQUIRE(scope.session.game() == nullptr);
    REQUIRE_FALSE(GameInProgress());
    REQUIRE(scope.session.begin().rng.next() == 18467);
}

TEST_CASE("A monster record owns its loaded body independently of activation",
          "[resources][monsters]")
{
    World world;
    auto record = std::make_unique<W8MonsterInfo>();
    record->location_id = 7;
    record->entity = std::make_unique<Monster>(world);
    auto* body = record->entity.get();
    body->inactive = true;
    world.active_monsters.push_back(body);
    Navigator follower(world);
    follower.target_navigator = &body->nav;
    follower.linked_navigator = &body->nav;
    auto* info = world.population->monster_roster.add(std::move(record), true);

    REQUIRE_FALSE(info->fActive);
    REQUIRE(world.population->monster_roster.promote(info));
    REQUIRE(world.population->findEntity(7) == body);
    REQUIRE(body->inactive);

    // Motion and authored-cycle invariants need no installed game or party fight.
    Animation animation;
    AnimationMesh mesh;
    mesh.radius = 3.0f;
    animation.parts[2].push_back({std::move(mesh), std::nullopt});
    body->visual.active_animation = &animation;
    body->visual.m_bLOD = 2;
    body->visual.animation_playing = true;
    body->visual.pending_cycle = W8_MONSTER_CYCLE_GET_HIT;
    info->setMotionless(true);
    REQUIRE(info->fMotionless);
    REQUIRE(body->visual.animation_playing); // Do not interrupt the hit reaction.
    info->setMotionless(false);
    body->visual.pending_cycle = W8_MONSTER_CYCLE_IDLE;
    info->setMotionless(true);
    REQUIRE_FALSE(body->visual.animation_playing);
    REQUIRE(body->visual.pending_cycle == W8_MONSTER_CYCLE_NONE);
    info->setMotionless(false);
    REQUIRE(body->visual.animation_playing);
    REQUIRE(body->visual.pending_cycle == W8_MONSTER_CYCLE_IDLE);
    REQUIRE(body->visual.pending_behaviour == W8_ANIMATION_NEVER_STOP);
    REQUIRE(body->visual.animation_radius == 3.0f);

    // Unloading the runtime leaves the logical record, but no dangling update,
    // targeting or formation references to its former body.
    info->entity.reset();
    REQUIRE(world.population->findMonster(7) == info);
    REQUIRE(world.active_monsters.empty());
    REQUIRE(follower.target_navigator == nullptr);
    REQUIRE(follower.linked_navigator == nullptr);
    REQUIRE(world.navigators.size() == 2); // Party and follower only.

    // A later body is released by population teardown, while its world's
    // navigation and scene resources are still alive.
    info->entity = std::make_unique<Monster>(world);
    world.active_monsters.push_back(info->entity.get());
}

// Minimal authored cycles exercise the real update -> script -> trigger path without assets.
TEST_CASE("Scripted removal retires relationships before reclaiming an updating monster",
          "[resources][monsters]")
{
    ScopedGame scope;
    auto& world = PublishEmptyWorld(scope.game);
    world.spatial = std::make_unique<LevelSpatialIndex>("", &world);
    world.compiledIndex()->registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    std::vector<int> calls;
    const auto add = [&](int id, std::function<void()> action) {
        auto record = std::make_unique<W8MonsterInfo>();
        record->location_id = id;
        record->bound_npc_index = -1;
        record->entity = std::make_unique<Monster>(world);
        auto& body = *record->entity;
        body.location_id = id;
        body.disabled = true;
        body.nav.flags = 0x200000;
        body.nav.halted = true;
        body.nav.position_dirty = false;
        body.nav.movement.position_adjusted = false;
        auto& cycle = body.cycles[W8_MONSTER_CYCLE_IDLE].emplace_back(
            EmitterLights{}, std::make_unique<Animation>(), 1.0f);
        AnimationMesh mesh;
        mesh.frame_count = 1;
        mesh.frames = {{std::make_shared<Mesh>(), {}}};
        cycle.animation->parts[2].push_back({std::move(mesh), std::nullopt});
        body.visual.active_animation = cycle.animation.get();
        body.visual.current_cycle = W8_MONSTER_CYCLE_IDLE;
        body.visual.current_subcycle = 0;
        body.visual.animation_behaviour = W8_ANIMATION_NEVER_STOP;
        body.visual.animation_playing = 0;
        body.visual.rep_active = false;
        body.script = std::make_shared<Script>();
        body.script_line = 0;
        const auto name = "Step" + std::to_string(id);
        body.script->lines.push_back({"TRIGGER " + name, 1});
        auto trigger = std::make_unique<Trigger>(&world);
        std::snprintf(trigger->name, sizeof(trigger->name), "%s", name.c_str());
        trigger->activation_callback = [&, id, action = std::move(action)](Trigger*) {
            calls.push_back(id);
            action();
            return false;
        };
        world.triggers.push_back(std::move(trigger));
        world.active_monsters.push_back(&body);
        return world.population->monster_roster.add(std::move(record), false);
    };

    SECTION("self and later removal do not skip the surviving actor")
    {
        W8MonsterInfo* first = nullptr;
        W8MonsterInfo* second = nullptr;
        std::weak_ptr<Script> first_script, second_script;
        Navigator follower(world);
        first = add(1, [&] {
            REQUIRE(RemoveMonster(first, W8MonsterRemoval::Despawn));
            REQUIRE(RemoveMonster(second, W8MonsterRemoval::Despawn));
            REQUIRE_FALSE(world.population->findMonster(1));
            REQUIRE_FALSE(world.population->findMonster(2));
            REQUIRE_FALSE(follower.target_navigator);
            REQUIRE_FALSE(follower.linked_navigator);
            REQUIRE_FALSE(first_script.expired());
            REQUIRE_FALSE(second_script.expired());
            REQUIRE(world.isRetiring(*first->entity));
            REQUIRE_FALSE(std::ranges::contains(world.navigators, &first->entity->nav));
            REQUIRE(world.compiledIndex()->registry.cells->by_object.empty());
            // The old navigator's deferred destructor must not erase a reused cell ID.
            follower.movement.location_id = 7;
            world.compiledIndex()->registry.cells->RegisterObjectCell(W8_OCTREE_KIND_NAVIGATOR, 8,
                                                                      {0, 0, 0});
        });
        second = add(2, [] {});
        auto* third = add(3, [] {});
        first->entity->nav.movement.location_id = 7;
        world.compiledIndex()->registry.cells->RegisterObjectCell(W8_OCTREE_KIND_NAVIGATOR, 8,
                                                                  {0, 0, 0});
        first_script = first->entity->script;
        second_script = second->entity->script;
        follower.target_navigator = &first->entity->nav;
        follower.linked_navigator = &second->entity->nav;
        std::vector<Rng::Draw> draws;
        scope.game.rng.trace(&draws);
        world.updateMonsters();
        scope.game.rng.trace(nullptr);
        REQUIRE(draws.empty());
        REQUIRE(calls == std::vector<int>{1, 3});
        REQUIRE(first_script.expired());
        REQUIRE(second_script.expired());
        REQUIRE(world.compiledIndex()->registry.cells->by_object.size() == 1);
        REQUIRE(world.active_monsters == std::vector<Monster*>{third->entity.get()});
        // A loaded inactive body is not a removed body.
        REQUIRE_FALSE(third->fActive);
        REQUIRE(world.population->findMonster(3) == third);
    }
    SECTION("exceptional callback exit reclaims retired storage and ends the boundary")
    {
        W8MonsterInfo* first = nullptr;
        std::weak_ptr<Script> script;
        first = add(1, [&] {
            REQUIRE(RemoveMonster(first, W8MonsterRemoval::Despawn));
            REQUIRE_FALSE(script.expired());
            throw std::runtime_error("script failed");
        });
        script = first->entity->script;
        REQUIRE_THROWS_WITH(world.updateMonsters(), "script failed");
        REQUIRE(script.expired());
        auto* next = add(2, [] {});
        script = next->entity->script;
        REQUIRE(RemoveMonster(next, W8MonsterRemoval::Despawn));
        REQUIRE(script.expired()); // Outside an update, destruction is immediate again.
        REQUIRE_NOTHROW(world.updateMonsters());
    }
}

TEST_CASE("A missile reports and detaches only from its original world's casts",
          "[resources][spell]")
{
    ScopedGame scope;
    auto outgoing = std::make_unique<World>();
    auto incoming = std::make_unique<World>();
    auto old_cast = std::make_unique<W8SpellEffectEntry>();
    auto new_cast = std::make_unique<W8SpellEffectEntry>();
    auto old_missile = std::make_unique<Missile>(*outgoing);
    auto new_missile = std::make_unique<Missile>(*incoming);
    old_cast->missiles.push_back(old_missile.get());
    new_cast->missiles.push_back(new_missile.get());
    auto* old_result = old_cast.get();
    auto* new_result = new_cast.get();
    outgoing->spell_effects.push_back(std::move(old_cast));
    incoming->spell_effects.push_back(std::move(new_cast));
    incoming->missiles.push_back(std::move(new_missile));
    scope.game.level.replacePrimary(std::move(incoming));

    old_missile->result.amount = 12;
    old_missile->result.count = 1;
    old_missile->result.reports.push_back(std::make_unique<W8SpellDamageReport>());
    outgoing->absorbMissileDamage(*old_missile);
    REQUIRE(old_result->result.amount == 12);
    REQUIRE(old_result->result.reports.size() == 1);
    REQUIRE(old_missile->result.reports.empty());
    REQUIRE(new_result->result.amount == 0);
    old_missile.reset();
    REQUIRE(old_result->missiles.empty());
    REQUIRE(new_result->missiles.size() == 1);

    // The entire old world can also be reclaimed after its session has ended.
    auto remaining = std::make_unique<Missile>(*outgoing);
    old_result->missiles.push_back(remaining.get());
    outgoing->missiles.push_back(std::move(remaining));
    scope.session.end();
    REQUIRE_FALSE(GameInProgress());
    outgoing.reset();
}

TEST_CASE("Only the first loaded actor of a species inherits its default script",
          "[resources][monsters]")
{
    Rng rng(17);
    std::vector<Rng::Draw> draws;
    rng.trace(&draws);
    World world;
    W8MonsterRecord species{};
    species.cycle_name = "Test";
    auto definition = std::make_unique<MonsterDefinition>();
    definition->asset_name = "Test";
    const auto script = definition->script = std::make_shared<Script>();
    world.monster_definitions.push_back(std::move(definition));

    auto first = world.loadMonsterBody(species, 1, rng);
    REQUIRE(first->script == script);
    auto second = world.loadMonsterBody(species, 2, rng);
    REQUIRE_FALSE(second->script);
    first.reset();
    auto third = world.loadMonsterBody(species, 3, rng);
    REQUIRE_FALSE(third->script);
    second.reset();
    third.reset();
    auto last = world.loadMonsterBody(species, 4, rng);
    REQUIRE(last->script == script);
    REQUIRE(draws.size() == 16); // Handedness, hover, amplitude and phase per actor.
    Rng expected(17);
    for (std::size_t index = 0; index < draws.size(); ++index) {
        REQUIRE(draws[index].range == (index % 4 == 0 ? 100u : 1000u));
        REQUIRE(draws[index].value == expected.below(draws[index].range));
    }
}

TEST_CASE("An item's runtime ownership settles before removal is presented", "[resources][items]")
{
    ScopedGame scope;
    scope.game.level.replacePrimary(std::make_unique<World>());
    auto& world = scope.game.level.loaded();
    auto item = std::make_unique<W8WorldItem>();
    item->runtime_id = 7;
    item->entity = std::make_unique<WorldItemEntity>(world);
    item->entity->m_psrMesh = std::make_unique<MeshInstance>(nullptr);
    item->entity->flags |= W8_ITEM_ENTITY_MESH_LOADED;
    item->entity->AttachMesh();
    auto* placed = item.get();
    world.population->world_items.push_back(std::move(item));
    REQUIRE(world.scene->dynamic_meshes.size() == 1);
    const glm::vec3 settled_position{1, 2, 3};
    placed->entity->SetLocation(&settled_position);
    AnimateWorldItems(world);
    REQUIRE(placed->entity->m_psrMesh->transform.getLocation() == glm::dvec3(1, 2, 3));
    REQUIRE(world.scene->dynamic_meshes.front() == placed->entity->m_psrMesh.get());
    const auto saved = CaptureWorldItem(*placed);
    REQUIRE(saved.front().fActive);
    REQUIRE(saved.front().p3D != 0);
    REQUIRE_FALSE(BuildWorldItem(saved)->entity); // A save never owns a process address.

    bool presented = false;
    scope.game.events.observe([&](const Event& event) {
        if (!std::holds_alternative<WorldItemRemoved>(event)) return;
        presented = true;
        REQUIRE_FALSE(placed->entity);
        REQUIRE(world.scene->dynamic_meshes.empty());
    });
    placed->deactivate(scope.game.events);
    REQUIRE(presented);
    REQUIRE_FALSE(CaptureWorldItem(*placed).front().fActive);
}

TEST_CASE("Dialogue owns voice completion, strict holds and stale completion rejection",
          "[dialogue][lifetime]")
{
    W8NpcDialogue dialogue;
    const auto first = dialogue.BeginVoiceQuote();
    dialogue.VoiceStarted(first, 1000, true, 2000);
    REQUIRE(dialogue.voice_pending());
    REQUIRE_FALSE(dialogue.CanSkipUtterance(1500));
    REQUIRE(dialogue.CanSkipUtterance(1501));
    REQUIRE_FALSE(dialogue.UtteranceHoldOver(first, 10000)); // EOS must be reported, not polled.
    dialogue.VoiceFinished(first, 2000);
    REQUIRE_FALSE(dialogue.voice_pending());
    REQUIRE(dialogue.has_active_utterance());
    REQUIRE_FALSE(dialogue.UtteranceHoldOver(first, 2700));
    REQUIRE(dialogue.UtteranceHoldOver(first, 2701));
    dialogue.VoiceFinished(first, 3000); // Duplicate EOS cannot restart the hold.
    REQUIRE(dialogue.UtteranceHoldOver(first, 3000));

    const auto replacement = dialogue.BeginHeldQuote(4000, 2000);
    dialogue.VoiceFinished(first, 5000);
    dialogue.AbandonUtterance(first);
    int completions = 0;
    const auto release = [&](const auto&) {
        REQUIRE_FALSE(dialogue.has_active_utterance());
        ++completions;
    };
    dialogue.FinishUtterance(first, release);
    REQUIRE(completions == 0);
    REQUIRE(dialogue.utterance(replacement));
    REQUIRE_FALSE(dialogue.UtteranceHoldOver(replacement, 6000));
    REQUIRE(dialogue.UtteranceHoldOver(replacement, 6001));
    dialogue.FinishUtterance(replacement, release);
    dialogue.FinishUtterance(replacement, release);
    REQUIRE(completions == 1);

    const auto portrait = dialogue.BeginPortraitMessage(7000, 1000);
    dialogue.AbandonUtterance(portrait); // Discard performs no completion/presentation.
    REQUIRE_FALSE(dialogue.has_active_utterance());
    REQUIRE(completions == 1);
}

TEST_CASE("A completed NPC subquote waits for its hold before continuing", "[dialogue][lifetime]")
{
    ScopedGame scope;
    W8NpcDatabaseRecord record{};
    record.voice_script = 1;
    auto npc = std::make_shared<W8NpcState>();
    npc->record = &record;
    npc->has_monster = true;
    auto script = std::make_shared<W8NpcScriptFile>();
    script->quotes.resize(1);
    script->quotes[0].subquotes = {"", ""};
    npc->script_file = script;
    auto& dialogue = scope.game.npc_dialogue;
    scope.game.beginNpcDialogue(npc.get(), false);
    dialogue.QueueQuote(0, false, false, false);
    dialogue.Advance(MakeScriptContext(scope.game));
    REQUIRE(dialogue.has_queued_actions());
    const auto utterance = dialogue.BeginVoiceQuote();
    dialogue.VoiceStarted(utterance, 1000, true, 2000);
    dialogue.VoiceFinished(utterance, 2000);
    dialogue.Advance(MakeScriptContext(scope.game));
    REQUIRE(dialogue.has_queued_actions());
    REQUIRE(dialogue.UtteranceHoldOver(utterance, 2701));
    dialogue.FinishUtterance(utterance, [](const auto&) {});
    dialogue.Advance(MakeScriptContext(scope.game));
    REQUIRE_FALSE(dialogue.has_queued_actions()); // Continue, rather than replaying subquote zero.
}

TEST_CASE("Utterance completion follows its suspended or reply session", "[dialogue][lifetime]")
{
    ScopedGame scope;
    W8NpcDatabaseRecord record{};
    record.voice_script = 1;
    auto script = std::make_shared<W8NpcScriptFile>();
    script->quotes.resize(1);
    auto& quote = script->quotes[0];
    quote.subquotes = {"", ""};
    // Only the second subquote draws its response. This observes the resumed cursor
    // without exposing implementation-only getters or running a rendered conversation.
    quote.entries.push_back(
        {.kind = W8_NPC_ENTRY_NEXT_QUOTE, .operand0 = 100, .operand1 = 2, .operand2 = 101});
    auto first = std::make_shared<W8NpcState>();
    first->record = &record;
    first->has_monster = true;
    first->script_file = script;
    auto second = std::make_shared<W8NpcState>();
    second->record = &record;
    second->has_monster = true;
    second->script_file = script;
    auto& dialogue = scope.game.npc_dialogue;
    const auto context = MakeScriptContext(scope.game);
    scope.game.beginNpcDialogue(first.get(), false);
    dialogue.Speak(context, 0, false);
    const auto identity = dialogue.BeginVoiceQuote();
    dialogue.VoiceStarted(identity, 1000, true, 2000);

    bool reply = false;
    SECTION("suspended conversation")
    {
        scope.game.beginNpcDialogue(second.get(), true);
    }
    SECTION("reply session")
    {
        dialogue.ReplyDialogOpened(std::nullopt);
        scope.game.beginNpcDialogue(second.get(), false);
        reply = true;
    }
    dialogue.Speak(context, 1, false); // No such quote; establish a different current cursor.
    dialogue.VoiceFinished(identity, 2000);
    dialogue.FinishUtterance(identity, [&](const auto& finished) {
        REQUIRE(finished.npc == first);
        REQUIRE(finished.quote == 0);
        REQUIRE(dialogue.speaker() == second.get());
    });
    if (reply) {
        dialogue.Answer(context, "");
    } else {
        // The queued continuation and its out-of-range response drain before resume.
        for (int step = 0; step < 3 && dialogue.speaker() != first.get(); ++step)
            dialogue.Advance(context);
    }
    REQUIRE(dialogue.speaker() == first.get());
    std::vector<Rng::Draw> draws;
    scope.game.rng.trace(&draws);
    dialogue.Speak(context, 0, false);
    scope.game.rng.trace(nullptr);
    REQUIRE(draws.size() == 1); // Finishing A did not mark whichever quote B was running.
    REQUIRE(draws.front().range == 2);
}

TEST_CASE("Prepared monster bodies use their definition and RNG without an installed game",
          "[resources][monsters][lifetime]")
{
    REQUIRE_FALSE(GameInProgress());
    W8MonsterRecord species{};
    species.cycle_name = "Candidate";
    species.can_open_doors = true;
    species.camouflage0 = 1;
    species.effective_level = 4;
    species.hit_points = {.base = 10, .count = 1, .sides = 10};
    std::ranges::fill(species.attribute_values, 55);
    World world(7);
    world.spatial = std::make_unique<LevelSpatialIndex>("", &world);
    world.spatial->compiled_available = true;
    world.spatial->registry.cells = std::make_unique<W8OctreeObjectRegistry>();
    auto& spatial = world.spatial->m_spatial;
    spatial.m_bounds.minimum = glm::vec3(-5000);
    spatial.m_clipped_bounds.maximum = glm::vec3(5000);
    spatial.m_node_extent = 1000;
    spatial.m_region_grid_cell = 1000;
    spatial.m_region_cells_per_axis = 0; // Valid empty geometry, with no assigned sector.
    auto definition = std::make_unique<MonsterDefinition>();
    definition->asset_name = species.cycle_name;
    definition->minimum_scale = 1;
    definition->maximum_scale = 2;
    for (int index : {W8_MONSTER_CYCLE_BIRTH, W8_MONSTER_CYCLE_IDLE}) {
        auto& cycle = definition->cycles[index].emplace_back(EmitterLights{},
                                                             std::make_unique<Animation>(), 1.0f);
        AnimationMesh mesh;
        mesh.frame_count = 1;
        mesh.frames = {{std::make_shared<Mesh>(), {}}};
        cycle.animation->parts[2].push_back({std::move(mesh), std::nullopt});
    }
    world.monster_definitions.push_back(std::move(definition));
    Rng rng(17);
    std::vector<Rng::Draw> draws;
    rng.trace(&draws);
    const auto prepare = [&](int id, bool unborn, bool visited) -> W8MonsterInfo& {
        auto info = std::make_unique<W8MonsterInfo>();
        info->location_id = id;
        info->monster_species = 123; // Deliberately no installed database with this species.
        info->uiHPMax = info->hp_current = 15;
        info->stamina_max = info->stamina = 10;
        info->scale = 1.75f;
        info->saved_mirror_x = true;
        info->entity = world.loadMonsterBody(species, id, rng);
        auto& adopted = *world.population->monster_roster.add(std::move(info), unborn);
        adopted.configureBody(rng, visited);
        world.prepareMonsterBody(adopted, rng, unborn);
        return adopted;
    };
    auto& live = prepare(1, false, true);
    REQUIRE(live.scale == 1.75f);
    REQUIRE(live.entity->visual.mirror_x);
    REQUIRE(live.fActive);
    REQUIRE_FALSE(live.entity->pending_finalize);
    REQUIRE(std::ranges::all_of(live.attributes, [](auto value) { return value == 55; }));
    REQUIRE((live.entity->nav.movement.navigation_filter & W8_NAV_THROUGH_DOORS) != 0);
    REQUIRE(draws.size() == 6); // Five appearance draws, then the live idle-frame choice.
    REQUIRE(draws.back().range == 1);
    REQUIRE_FALSE(live.entity->IsRenderable(false));
    live.party_threat.party_detected = true;
    REQUIRE(live.entity->IsRenderable(false));
    world.prepareMonsterBody(live, rng, false);
    REQUIRE(draws.size() == 6); // Already active: no second preparation or idle draw.

    const auto before_unborn = draws.size();
    auto& unborn = prepare(2, true, false);
    REQUIRE(unborn.fActive); // Logical preparation is distinct from active navigation/animation.
    REQUIRE(unborn.entity->inactive);
    REQUIRE_FALSE(unborn.entity->nav.active);
    REQUIRE_FALSE(unborn.entity->visual.animation_playing);
    REQUIRE(draws.size() == before_unborn + 6); // Appearance and fresh scale, but no idle draw.
    REQUIRE(world.active_monsters.size() == 2);
}

TEST_CASE("NPC preparation keeps story identity and publishes only into its supplied game",
          "[resources][npc][lifetime]")
{
    ScopedTemporaryDirectory directory{"wiz8-npc-candidate"};
    ScopedAssetRoots roots;
    std::filesystem::create_directory(directory.path / "assets");
    std::filesystem::create_directory(directory.path / "user");
    const auto npc_scripts = directory.path / "assets/data/npc scripts";
    const auto monster_scripts = directory.path / "assets/data/monsters/scripts";
    std::filesystem::create_directories(npc_scripts);
    std::filesystem::create_directories(monster_scripts);
    for (const auto* name : {"npc_singleton.nsf", "npc_rfs81b.nsf"}) {
        auto file = wiz8::open_file(npc_scripts / name, wiz8::OpenMode::replace);
        const wiz8::retail::nsf::Header header{};
        file->write(&header, sizeof(header));
    }
    {
        auto file = wiz8::open_file(monster_scripts / "npc_singleton.msf", wiz8::OpenMode::replace);
        constexpr char source[] = "* candidate monster script\n";
        file->write(source, sizeof(source) - 1);
    }
    roots.configure({(directory.path / "assets").string(), (directory.path / "user").string()});
    REQUIRE(
        std::filesystem::is_regular_file(wiz8::asset_path("Data\\NPC Scripts\\NPC_Singleton.nsf")));
    REQUIRE(std::filesystem::is_regular_file(
        wiz8::asset_path("Data\\Monsters\\Scripts\\NPC_Singleton.msf")));
    GameData active_data;
    active_data.npcs.resize(1);
    active_data.npcs[0].kind = 0;
    ScopedGame active(active_data);
    auto active_npc = std::make_shared<W8NpcState>();
    active_npc->record = &active_data.npcs[0];
    active_npc->gold = 900;
    active.game.story.npcs.push_back(active_npc);
    active.game.status.world_clock = 9001;
    active.game.party.members[0].emplace();
    active.game.party.characters[0].uiExpLevel = 50;

    GameData data;
    data.npcs.resize(W8_NPC_RFS81_A + 1);
    data.npcs[0].kind = 0;
    data.npcs[0].display_name = "Singleton";
    data.npcs[1].kind = 1;
    data.npcs[1].display_name = "Bound";
    data.npcs[1].monster_bound = true;
    data.npcs[1].owns_stock = true;
    data.npcs[1].item_stock_rules = {{0, 2, 1}, {1, 1, 0}};
    data.npcs[W8_NPC_RFS81_A].display_name = "RFS81A";
    data.items.resize(2);
    data.items[1].identify_difficulty = 8;
    data.monsters.resize(4);
    for (int index : {0, 1, 3}) data.monsters[index].flags = W8_MONSTER_FLAG_NPC;
    data.monsters[1].npc_kind = 1;
    data.monsters[3].npc_kind = 255; // Invalid late binding, for failure-before-publication.
    Game candidate(data);
    candidate.status.world_clock = 77;
    candidate.party.members[2].emplace();
    candidate.party.characters[2].uiExpLevel = 10;
    World world(7, "", candidate.timerClocks());
    auto singleton = std::make_shared<W8NpcState>();
    singleton->record = &data.npcs[0];
    singleton->location_id = 101;
    singleton->gold = 777;
    singleton->character.emplace().name = "Retained snapshot";
    singleton->items.push_back({.available_at = 88, .item = {.iItemNo = 0}, .quantity = 9});
    auto old_script = std::make_shared<W8NpcScriptFile>();
    singleton->script_file = old_script;
    auto retired = std::make_shared<W8NpcState>();
    retired->record = &data.npcs[1];
    retired->partner_index = 1;
    retired->binding_unavailable = true;
    retired->gold = 999;
    candidate.story.npcs = {singleton, retired};
    const auto add = [&](int id, unsigned int species, bool unborn = false) -> W8MonsterInfo& {
        auto info = std::make_unique<W8MonsterInfo>();
        info->location_id = id;
        info->monster_species = species;
        info->bound_npc_index = -1;
        info->entity = std::make_unique<Monster>(world, &data.monsters[species]);
        return *world.population->monster_roster.add(std::move(info), unborn);
    };
    add(1, 0);
    auto& first = add(2, 1);
    auto& second = add(3, 1);
    add(4, 0); // One singleton slot: the last binding wins, not a copied NPC.
    auto& unborn = add(5, 1, true);
    auto& ordinary = add(6, 2);
    std::vector<Rng::Draw> draws, active_draws;
    candidate.rng.seed(17);
    candidate.rng.trace(&draws);
    active.game.rng.trace(&active_draws);
    const auto unchanged = [&] {
        REQUIRE(candidate.story.npcs.size() == 2);
        REQUIRE(candidate.story.npcs[0] == singleton);
        REQUIRE(candidate.story.npcs[1] == retired);
        REQUIRE(singleton->location_id == 101);
        REQUIRE(singleton->gold == 777);
        REQUIRE(singleton->script_file == old_script);
        REQUIRE(singleton->character->name == "Retained snapshot");
        REQUIRE(singleton->items.front().quantity == 9);
        REQUIRE(active_npc->gold == 900);
        REQUIRE(active_draws.empty());
    };
    SECTION("A later invalid NPC leaves existing story values untouched")
    {
        add(7, 3);
        REQUIRE_THROWS(candidate.prepareNpcBindings(world));
        unchanged();
        REQUIRE(draws.size() == 6); // Live entries were prepared; unborn entries were not reached.
    }
    SECTION("A reused slot rejects publication before any earlier binding changes")
    {
        auto pending = candidate.prepareNpcBindings(world);
        unchanged();
        auto intervening = std::make_shared<W8NpcState>();
        intervening->record = &data.npcs[1];
        intervening->gold = 42;
        candidate.story.npcs[1] = intervening;
        REQUIRE_THROWS(candidate.story.installNpcBindings(std::move(pending)));
        REQUIRE(candidate.story.npcs.size() == 2);
        REQUIRE(candidate.story.npcs[1] == intervening);
        REQUIRE(intervening->gold == 42);
        REQUIRE(singleton->location_id == 101);
        REQUIRE(singleton->script_file == old_script);
        REQUIRE(retired->gold == 999);
    }
    SECTION("Live binding shares candidate identity and slot rules")
    {
        candidate.bindMonsterNpc(world, first);
        REQUIRE(first.bound_npc_index == 1);
        REQUIRE(draws.size() == 3);
        auto identity = candidate.story.npcs[1];
        identity->gold = 42;
        candidate.bindMonsterNpc(world, first);
        REQUIRE(candidate.story.npcs[1] == identity);
        REQUIRE(identity->gold == 42);
        REQUIRE(identity->bound_level == 7);
        REQUIRE(identity->maintenance_clock == 77);
        REQUIRE(draws.size() == 3); // Matching instance: no stock recreation.
        second.bound_npc_index = first.bound_npc_index;
        candidate.bindMonsterNpc(world, second);
        REQUIRE(second.bound_npc_index == 2); // Location mismatch is a new instance.
        REQUIRE(candidate.story.npcs[2] != identity);
        REQUIRE(draws.size() == 6);
        candidate.bindMonsterNpc(world, ordinary);
        REQUIRE(ordinary.bound_npc_index == -1);
        REQUIRE(draws.size() == 6);
        REQUIRE(retired->gold == 999);
        REQUIRE(active.game.story.npcs.front() == active_npc);
        REQUIRE(active_npc->gold == 900);
        REQUIRE(active_draws.empty());
    }
    SECTION("Publication preserves old identities and separates monster-bound instances")
    {
        auto pending = candidate.prepareNpcBindings(world);
        unchanged();
        REQUIRE(pending.records.size() == 4);
        REQUIRE(world.population->findMonster(1)->entity->script);
        REQUIRE(world.population->findMonster(4)->entity->script);
        REQUIRE(ordinary.bound_npc_index == -1);
        REQUIRE(draws.size() == 9);
        Rng expected(17);
        for (const auto& draw : draws) {
            REQUIRE(draw.range == 100);
            REQUIRE(draw.value == expected.below(100));
        }
        auto& program = *world.population->findMonster(1)->entity;
        program.script_line = 17;
        program.script_wait = MONSCR_DELAY;
        program.orders_finished = true;
        candidate.story.installNpcBindings(std::move(pending));
        REQUIRE(program.script_line == 17);
        REQUIRE(program.script_wait == MONSCR_DELAY);
        REQUIRE(program.orders_finished);
        REQUIRE(draws.size() == 9);
        REQUIRE(candidate.story.npcs.size() == 4);
        REQUIRE(candidate.story.npcs[0] == singleton);
        REQUIRE(singleton->location_id == 4);
        REQUIRE(singleton->gold == 777);
        REQUIRE(singleton->character->name == "Retained snapshot");
        REQUIRE(singleton->items.front().quantity == 9);
        REQUIRE(singleton->script_file);
        REQUIRE(singleton->script_file != old_script);
        REQUIRE(candidate.story.npcs[1] != retired);
        REQUIRE(retired->binding_unavailable);
        REQUIRE(retired->gold == 999); // Outstanding work still owns the retired object.
        REQUIRE(first.bound_npc_index == 1);
        REQUIRE(second.bound_npc_index == 2);
        REQUIRE(unborn.bound_npc_index == 3);
        for (int slot = 1; slot <= 3; ++slot) {
            const auto& npc = candidate.story.npcs[slot];
            REQUIRE(npc->record == &data.npcs[1]);
            REQUIRE(npc->bound_level == 7);
            REQUIRE(npc->maintenance_clock == 77);
            REQUIRE(npc->restock_clock == 77);
            REQUIRE(npc->items.front().item.iItemNo == 0);
            REQUIRE(npc->items.front().quantity == 2);
            REQUIRE(npc->items.size() == (draws[slot * 3 - 1].value < 75 ? 2 : 1));
        }
        REQUIRE(candidate.story.npcs[1] != candidate.story.npcs[2]);
        REQUIRE(candidate.story.npcs[2] != candidate.story.npcs[3]);
        REQUIRE(active.game.story.npcs.size() == 1);
        REQUIRE(active.game.story.npcs.front() == active_npc);
        const auto instance = candidate.story.npcs[1];
        const auto retained_script = singleton->script_file;
        const auto retained_gold = instance->gold;
        instance->releaseWorldBinding();
        singleton->releaseWorldBinding();
        REQUIRE_FALSE(instance->has_monster);
        REQUIRE_FALSE(instance->script_file);
        REQUIRE(instance->binding_unavailable);
        REQUIRE(candidate.story.npc(1) == nullptr);
        REQUIRE(retained_script); // Outstanding continuations keep their script alive.
        REQUIRE(instance->gold == retained_gold);
        REQUIRE_FALSE(singleton->has_monster);
        REQUIRE_FALSE(singleton->script_file);
        REQUIRE_FALSE(singleton->binding_unavailable);
        REQUIRE(candidate.story.npc(0) == singleton.get());
        REQUIRE(singleton->gold == 777);
        REQUIRE(singleton->character->name == "Retained snapshot");
        REQUIRE(singleton->items.front().quantity == 9);
        REQUIRE(draws.size() == 9);
    }
    W8NpcState rfs{};
    rfs.name_style = W8_NPC_RFS81_A;
    rfs.record = &data.npcs[W8_NPC_RFS81_A];
    candidate.story.facts[W8_FACT_RFS81_HAS_BEEN_FIXED] = 1;
    REQUIRE(FormatNpcVoiceSoundPath(candidate.story, &rfs) == "NPC_RFS81B");
    REQUIRE(FormatNpcVoiceSoundPath(active.game.story, &rfs) == "NPC_RFS81A");
    REQUIRE(LoadNpcScriptResources(candidate.story, rfs, nullptr));
    REQUIRE_FALSE(LoadNpcScriptResources(active.game.story, rfs, nullptr));
}

TEST_CASE("Recruitable NPC construction uses standalone spell and character rules",
          "[resources][npc][character]")
{
    GameData data;
    data.npcs.resize(1);
    auto& record = data.npcs.front();
    record.has_group = true;
    auto& source = record.character;
    source.name = "Candidate mage";
    source.profession = W8_PROFESSION_MAGE;
    source.race = W8_RACE_HUMAN;
    source.level = 10;
    std::ranges::fill(source.attributes, 60);
    std::ranges::fill(source.skills, 100);
    source.spells[W8_SPELL_LIGHT - 1] = 1;
    data.spells.resize(W8_SPELL_COUNT);
    data.spells[W8_SPELL_LIGHT].wizardry_spell = true;
    data.spells[W8_SPELL_LIGHT].realm = W8_SPELL_REALM_FIRE;
    data.spells[W8_SPELL_LIGHT].spell_level = 1;
    Game candidate(data);
    candidate.party.modifiers.damage_bonus = 123;
    auto* npc = candidate.createNpcRuntimeNode(0);
    REQUIRE(npc->character);
    const auto& character = *npc->character;
    REQUIRE(character.name == "Candidate mage");
    REQUIRE(character.spell_learned[W8_SPELL_LIGHT] == 1);
    REQUIRE(character.party_weight_share == 0);
    REQUIRE(character.bonus.damage_bonus == 0);
    REQUIRE(std::cmp_equal(character.hp_current, character.uiHPMax));
    REQUIRE(std::ranges::all_of(character.EquippedItem,
                                [](const auto& item) { return item.iItemNo == -1; }));
}

TEST_CASE("Level variants and bands use the supplied game's inventory and facts",
          "[resources][levels]")
{
    REQUIRE_FALSE(GameInProgress());
    GameData data;
    Game game(data);
    REQUIRE(game.normalizeLevel(0x10) == 0x26);
    REQUIRE(game.levelBand(0x10) == 0);
    REQUIRE(game.levelBand(0x26) == 0);
    game.story.facts[W8_FACT_PARTY_AT_RAC] = 1;
    REQUIRE(game.normalizeLevel(0x26) == 0x10);
    REQUIRE(game.levelBand(0x10) == 13);
    REQUIRE(game.levelBand(0x26) == 13);
    game.story.facts[W8_FACT_PARTY_AT_RAC] = 0;
    game.inventory.restore({W8ItemInstance{.iItemNo = 0x242}}, std::nullopt);
    REQUIRE(game.levelBand(0x10) == 0);
    game.inventory.restore({W8ItemInstance{.iItemNo = 0x242}, W8ItemInstance{.iItemNo = 0x244}},
                           std::nullopt);
    REQUIRE(game.normalizeLevel(0x26) == 0x10);
    REQUIRE(game.levelBand(0x26) == 13);
    REQUIRE(game.levelBand(-1) == 0);
    REQUIRE(game.levelBand(W8_LEVEL_COUNT) == 0);
}

TEST_CASE("Saved facts restore their own story without changing the outgoing game",
          "[lifetime][save][story]")
{
    ScopedGame outgoing;
    outgoing.game.story.facts[W8_FACT_IMPORT_NOALIGN] = 7;
    Game candidate(outgoing.game.data);
    SavedGame saved;
    saved.facts.emplace();
    std::move(saved).install(candidate);
    REQUIRE(candidate.story.facts[W8_FACT_IMPORT_NOALIGN] == 1);
    REQUIRE(candidate.story.journal.size() == 1);
    REQUIRE(candidate.story.journal.front().level == -1);
    REQUIRE(candidate.story.journal.front().fact == W8_FACT_IMPORT_NOALIGN);
    REQUIRE(candidate.story.journal.front().alternate_text);
    REQUIRE(outgoing.game.story.facts[W8_FACT_IMPORT_NOALIGN] == 7);
    REQUIRE(outgoing.game.story.journal.empty());

    SavedGame with_journal;
    with_journal.facts.emplace();
    with_journal.journal.emplace();
    std::move(with_journal).install(candidate);
    REQUIRE(candidate.story.facts[W8_FACT_IMPORT_NOALIGN] == 1);
    REQUIRE(candidate.story.journal.empty());

    auto aligned = candidate.story.facts;
    aligned[W8_FACT_IMPORT_NOALIGN] = 0;
    aligned[W8_FACT_IMPORT_TRANG] = 1;
    candidate.story.restoreFacts(aligned, 3);
    REQUIRE(candidate.story.facts[W8_FACT_IMPORT_NOALIGN] == 0);
    REQUIRE(candidate.story.journal.empty());
}

TEST_CASE("Condition and death reactions stay with their originating party and clock",
          "[lifetime][speech]")
{
    ScopedGame outgoing;
    Game candidate(outgoing.game.data);
    candidate.time = std::chrono::milliseconds{1234};
    candidate.rng.seed(17);
    for (unsigned int slot : {2u, 3u}) {
        candidate.party.members[slot].emplace();
        auto& character = candidate.party.characters[slot];
        character.fInParty = true;
        character.hp_current = 10;
        character.uiHPMax = 10;
    }
    // This selection includes living incapacitated members; the awake selector does not.
    candidate.party.characters[3].highest_condition = W8_CONDITION_ASLEEP;
    std::vector<Rng::Draw> draws;
    candidate.rng.trace(&draws);
    candidate.speech.QueuePartyDeathReaction(0);
    REQUIRE(draws.size() == 1);
    REQUIRE(draws.front().range == 2);
    REQUIRE(candidate.speech.pending_events.size() == 1);
    const auto& reaction = *candidate.speech.pending_events.front();
    REQUIRE(reaction.character == &candidate.party.characters[2 + draws.front().value]);
    REQUIRE(reaction.queued_at == candidate.time);
    REQUIRE(reaction.dispatch_delay == std::chrono::milliseconds{3000});
    REQUIRE(outgoing.game.speech.pending_events.empty());

    // A sole eligible member needs no random selection; an empty set also draws nothing.
    candidate.party.members[3].reset();
    REQUIRE(candidate.party.randomLivingSlot(candidate.rng) == 2);
    REQUIRE_FALSE(candidate.party.randomLivingSlot(candidate.rng, 2));
    REQUIRE(draws.size() == 1);
    candidate.rng.trace(nullptr);

    outgoing.game.status.skip_next_condition_reaction = true;
    candidate.status.skip_next_condition_reaction = true;
    auto& character = candidate.party.characters[2];
    candidate.speech.QueueConditionClearedReaction(&character, W8_CONDITION_POISONED);
    REQUIRE_FALSE(candidate.status.skip_next_condition_reaction);
    REQUIRE(outgoing.game.status.skip_next_condition_reaction);
    REQUIRE(candidate.speech.pending_events.size() == 1);
    candidate.speech.QueueConditionClearedReaction(&character, W8_CONDITION_POISONED);
    REQUIRE(candidate.speech.pending_events.size() == 2);
    REQUIRE(candidate.speech.pending_events.back()->event_type == 0x55);
    REQUIRE(outgoing.game.speech.pending_events.empty());
    // The generic random reaction retains its original one selection plus fifty retries.
    draws.clear();
    std::vector<Rng::Draw> outgoing_draws;
    candidate.rng.trace(&draws);
    outgoing.game.rng.trace(&outgoing_draws);
    candidate.speech.SetEventCharacterMask(13, 2, true);
    REQUIRE(candidate.speech.QueueRandom(13, -1, 0) == nullptr);
    REQUIRE(draws.size() == 51);
    for (const auto& draw : draws) REQUIRE(draw.range == 8);
    candidate.speech.SetEventCharacterMask(13, 2, false);
    const auto* selected = candidate.speech.QueueRandom(13, -1, 0);
    REQUIRE(selected);
    REQUIRE(selected->character == &candidate.party.characters[2]);
    REQUIRE(selected->queued_at == candidate.time);
    REQUIRE(draws.size() == 52);
    REQUIRE(outgoing_draws.empty());
    candidate.rng.trace(nullptr);
    outgoing.game.rng.trace(nullptr);
}

TEST_CASE("Speech completion notices use the originating game's content", "[lifetime][speech]")
{
    ScopedGame outgoing;
    GameData data;
    data.strings.resize(0x773);
    data.strings[0x771] = "candidate completion";
    Game candidate(data);
    candidate.party.members[0].emplace();
    auto& character = candidate.party.characters[0];
    character.fInParty = true;
    auto event = std::make_unique<W8CharacterEvent>(candidate.speech, &character, 23, 0, 127);
    auto* active = event.get();
    candidate.speech.active_events.push_back(std::move(event));
    candidate.speech.CompleteActiveEvent(active);
    REQUIRE(candidate.speech.active_events.empty());
    REQUIRE(candidate.events.pending().size() == 1);
    REQUIRE(std::get<CharacterNotice>(candidate.events.pending().front()).text ==
            "candidate completion");
    REQUIRE(outgoing.game.events.pending().empty());
}

TEST_CASE("Quote input belongs to screens rather than any speech queue's destruction",
          "[lifetime][speech][ui]")
{
    GameData data;
    Game shown(data);
    const auto targets = LiveUiTargetCount();
    auto main_view = AcquirePartyQuoteInput(shown);
    REQUIRE(LiveUiTargetCount() == targets + 8);
    auto camp_view = AcquirePartyQuoteInput(shown);
    REQUIRE(camp_view == main_view);
    {
        Game discarded_candidate(data);
    }
    REQUIRE(LiveUiTargetCount() == targets + 8);
    main_view.reset();
    REQUIRE(LiveUiTargetCount() == targets + 8);
    camp_view.reset();
    REQUIRE(LiveUiTargetCount() == targets);
    auto rebuilt = AcquirePartyQuoteInput(shown);
    REQUIRE(LiveUiTargetCount() == targets + 8);
    rebuilt.reset();
    REQUIRE(LiveUiTargetCount() == targets);
}

TEST_CASE("Script fact tests and diagnostics use their own party, story and content",
          "[lifetime][facts]")
{
    ScopedGame outgoing;
    outgoing.game.story.facts[W8_FACT_ALIGNMENT_TRANG] = 1;
    GameData data;
    data.facts.resize(W8_FACT_SLOT_COUNT);
    data.facts[W8_FACT_ALIGNMENT_TRANG].symbolic_name = "Trang";
    data.facts[W8_FACT_ALIGNMENT_UMPANI].symbolic_name = "Umpani";
    data.facts[W8_FACT_ALIGNMENT_NONE].symbolic_name = "None";
    Game candidate(data);
    candidate.status.log_fact_checks = true;
    REQUIRE(candidate.testFact(W8_FACT_ALIGNMENT_NONE) == 1);
    const auto& notices = candidate.events.pending();
    REQUIRE(notices.size() == 3);
    REQUIRE(std::get<TextNotice>(notices[0]).text == "Checking fact Trang which is FALSE");
    REQUIRE(std::get<TextNotice>(notices[1]).text == "Checking fact Umpani which is FALSE");
    REQUIRE(std::get<TextNotice>(notices[2]).text == "Checking fact None which is TRUE");
    REQUIRE(outgoing.game.events.pending().empty());
    candidate.status.log_fact_checks = false;

    candidate.story.factions[W8_FACTION_HIGARDI_COMMON].disposition_score = 75;
    outgoing.game.story.factions[W8_FACTION_HIGARDI_COMMON].disposition_score = 0;
    REQUIRE(candidate.testFact(W8_FACT_FACTION_HIGARDI_COMMON_FRIENDLY) == 1);
    REQUIRE(candidate.story.factionDisposition(-1) == W8_FACTION_NEUTRAL);

    candidate.party.members[0].emplace();
    candidate.party.members[0]->npc_index = 0;
    auto npc = std::make_shared<W8NpcState>();
    npc->name_style = W8_NPC_VI_DOMINA;
    candidate.story.npcs.push_back(npc);
    REQUIRE(candidate.testFact(W8_FACT_VI_IN_PARTY) == 1);
    npc->binding_unavailable = true;
    REQUIRE(candidate.testFact(W8_FACT_VI_IN_PARTY) == 0);
    REQUIRE(outgoing.game.story.npcs.empty());

    candidate.events = {};
    candidate.status.log_fact_checks = true;
    std::vector<std::string> presented;
    candidate.events.observe([&](const Event& event) {
        const auto& notice = std::get<TextNotice>(event);
        if (presented.empty()) candidate.story.facts[W8_FACT_ALIGNMENT_UMPANI] = 1;
        presented.push_back(notice.text);
    });
    REQUIRE(candidate.testFact(W8_FACT_ALIGNMENT_NONE) == 1);
    REQUIRE(presented.size() == 3);
    REQUIRE(presented[1] == "Checking fact Umpani which is FALSE");
    REQUIRE(candidate.story.facts[W8_FACT_ALIGNMENT_UMPANI] == 1);
    candidate.events.observe({});
}

TEST_CASE("Camera sway and its gravity belong to the world's party", "[lifetime][world][motion]")
{
    ScopedGame outgoing;
    outgoing.game.level.replacePrimary(std::make_unique<World>());
    auto& first = outgoing.game.level.loaded();
    first.party.camera.applySway(first.scene->camera, CameraSwayMode::Start);
    REQUIRE(outgoing.game.cameraSwayActive());
    REQUIRE(first.party.camera.gravity() == 93.75f);
    REQUIRE(first.party.camera.environment.value.gravity_y == -93.75f);
    // Preparation captures the source world's normalization reference, not its live mode.
    World prepared{-1, "", {}, nullptr, first.party.camera.gravity()};
    REQUIRE(prepared.party.camera.environment.value.gravity_y == -93.75f);
    REQUIRE_FALSE(prepared.party.camera.swayActive());
    REQUIRE(prepared.party.camera.gravity() == 187.5f);
    LevelSpatialIndex prepared_geometry{"", &prepared, first.party.camera.gravity()};
    REQUIRE(prepared_geometry.environments.front().gravity_y == -93.75f);

    Game candidate{outgoing.game.data};
    candidate.level.replacePrimary(std::make_unique<World>());
    auto& second = candidate.level.loaded();
    REQUIRE_FALSE(candidate.cameraSwayActive());
    REQUIRE(second.party.camera.gravity() == 187.5f);
    second.party.camera.applySway(second.scene->camera, CameraSwayMode::Start);
    second.party.camera.applySway(second.scene->camera, CameraSwayMode::Stop);
    REQUIRE_FALSE(candidate.cameraSwayActive());
    REQUIRE(second.party.camera.environment.value.gravity_y == -187.5f);
    REQUIRE(outgoing.game.cameraSwayActive());
    REQUIRE(first.party.camera.gravity() == 93.75f);
    candidate.level.replacePrimary(nullptr);
    REQUIRE(outgoing.game.cameraSwayActive());
}

TEST_CASE("Party effect expiry settles and lights only its originating world",
          "[lifetime][effects]")
{
    ScopedGame outgoing;
    outgoing.game.level.replacePrimary(std::make_unique<World>());
    Light old_light{nullptr};
    outgoing.game.level.loaded().camera_light = &old_light;
    old_light.intensity = 42.0f;
    old_light.visible = true;

    Game candidate{outgoing.game.data};
    candidate.level.replacePrimary(std::make_unique<World>());
    Light light{nullptr};
    candidate.level.loaded().camera_light = &light;
    auto& effect = candidate.status.effect_slots[0];
    effect.percent = 50;
    effect.Activate(8, 4, 3);
    candidate.refreshPartyEffects();
    REQUIRE(candidate.party.modifiers.light == 12);
    REQUIRE(light.visible);
    REQUIRE(light.intensity == 14.0f);
    const auto intensity = light.intensity;
    candidate.resetPartyEffect(effect);
    REQUIRE_FALSE(effect.active);
    REQUIRE(effect.percent == 50);
    REQUIRE(candidate.party.modifiers.light == 0);
    REQUIRE_FALSE(light.visible);
    REQUIRE(light.intensity == intensity);
    REQUIRE(old_light.visible);
    REQUIRE(old_light.intensity == 42.0f);
    REQUIRE(outgoing.game.events.pending().empty());
    candidate.level.loaded().camera_light = nullptr;
    outgoing.game.level.loaded().camera_light = nullptr;
}

TEST_CASE("A group leaves only its own game fight and releases its engagement effects",
          "[lifetime][combat]")
{
    ScopedGame outgoing;
    outgoing.game.combat.emplace().schedule(MonsterActor{7});
    GameData data;
    data.monsters.resize(1);
    Game candidate{data};
    candidate.combat.emplace().schedule(MonsterActor{7});
    candidate.level.replacePrimary(std::make_unique<World>());
    auto& world = candidate.level.loaded();
    auto monster = std::make_unique<W8MonsterInfo>();
    monster->location_id = 7;
    monster->monster_group_id = 1;
    monster->hp_current = monster->uiHPMax = 1;
    monster->fActive = true;
    monster->ubDisposition = W8_DISPOSITION_HOSTILE;
    monster->entity = std::make_unique<Monster>(world, &data.monsters[0]);
    monster->pCombat = std::make_unique<W8MonsterCombatState>();
    monster->pCombat->combat_effects[0].Activate(2, 1, 10);
    monster->pCombat->combat_effects_2[0].Activate(2, 1, 10);
    monster->modifiers.hit_bonus = monster->modifiers.armor_flat = 2;
    auto& member = *world.population->monster_roster.add(std::move(monster), false);
    auto& group =
        *world.population->monster_groups.emplace_back(std::make_unique<W8MonsterGroup>());
    group.group_id = 1;
    group.fInCombat = true;
    group.leader_location_id = 7;
    group.monsters = {7};
    candidate.refreshCombatEngagement();
    REQUIRE(candidate.combat->enemies_engaged);
    candidate.events = {};
    unsigned int outcomes = 0;
    candidate.events.observe([&](const Event&) {
        ++outcomes;
        REQUIRE_FALSE(group.fInCombat);
        REQUIRE_FALSE(member.pCombat);
        REQUIRE_FALSE(candidate.combat->holdsTurn(MonsterActor{7}));
        REQUIRE((member.ai_mode & W8_MONSTER_AI_REAPPLY_MODE) != 0);
    });
    candidate.endGroupCombat(group);
    REQUIRE(outcomes == 2);
    REQUIRE_FALSE(group.fInCombat);
    REQUIRE_FALSE(member.pCombat);
    REQUIRE_FALSE(candidate.combat->holdsTurn(MonsterActor{7}));
    REQUIRE(candidate.combat->enemies_engaged); // Encounter history is sticky, not a live count.
    REQUIRE((member.ai_mode & W8_MONSTER_AI_REAPPLY_MODE) != 0);
    REQUIRE(member.modifiers.hit_bonus == 0);
    REQUIRE(member.modifiers.armor_flat == 0);
    REQUIRE(outgoing.game.combat->holdsTurn(MonsterActor{7}));
    REQUIRE_FALSE(outgoing.game.combat->enemies_engaged);
    REQUIRE(outgoing.game.events.pending().empty());

    candidate.combat.reset();
    auto& nav = member.entity->nav;
    nav.movement.location_id = 7;
    nav.patrol_home = {4, 5, 6};
    auto follower = std::make_unique<W8MonsterInfo>();
    follower->location_id = 8;
    follower->monster_group_id = 1;
    follower->hp_current = 1;
    follower->entity = std::make_unique<Monster>(world, &data.monsters[0]);
    auto& following = follower->entity->nav;
    following.movement.location_id = 8;
    following.linked_navigator = &nav;
    following.movement_stopped = true;
    world.population->monster_roster.add(std::move(follower), false);
    group.monsters.push_back(8);
    nav.movement_stopped = false;
    nav.flags = 0x205;
    nav.target_navigator = &world.party.navigator;
    nav.movement_target = {1, 2, 3};
    nav.collision_margin = 7;
    nav.movement.attachment->flags |= W8_NAV_ATTACHMENT_FOLLOW_PATH;
    member.ai_mode = W8_RT_AI_APPROACH_PARTY;
    world.resumeMonstersAfterCombat();
    REQUIRE(nav.flags == 4);
    REQUIRE(nav.target_navigator == nullptr);
    REQUIRE(nav.movement_target == glm::vec3(0));
    REQUIRE(nav.collision_margin == 0);
    REQUIRE((nav.movement.attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0);
    REQUIRE(member.ai_mode == W8_RT_AI_IDLE);
    REQUIRE_FALSE(following.movement_stopped);
    REQUIRE(following.patrol_home == nav.patrol_home);
    REQUIRE(following.flags == 0x201);
    REQUIRE((following.movement.attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0);
    REQUIRE(outgoing.game.combat->holdsTurn(MonsterActor{7}));
}

TEST_CASE("Monster names query their owning game without maintaining a display cache",
          "[lifetime][names]")
{
    ScopedGame outgoing;
    GameData data;
    data.monsters.resize(3);
    data.monsters[0].name0 = "Hidden rat";
    data.monsters[0].name2 = "Rat";
    data.monsters[1].name0 = "Hidden wolf";
    data.monsters[1].name1 = "Hidden wolves";
    data.monsters[1].name2 = "Wolf";
    data.monsters[1].name3 = "Wolves";
    data.monsters[2].record_id = W8_MONSTER_RECORD_ALTERNATE_NAME;
    Game candidate{data};
    candidate.level.replacePrimary(std::make_unique<World>());
    candidate.status.retail.monster_name_cache.fill(std::byte{0xa5});
    const auto cache = candidate.status.retail.monster_name_cache;
    auto& group = *candidate.level.loaded().population->monster_groups.emplace_back(
        std::make_unique<W8MonsterGroup>());
    group.group_id = group.monster_id = 1;
    group.monsters = {7};
    W8MonsterInfo monster{};
    monster.monster_group_id = 1;
    const Game& query = candidate;
    REQUIRE(query.monsterName(monster) == "Rat");
    REQUIRE(query.monsterGroupName(group) == "Wolf");
    group.monsters.push_back(8);
    REQUIRE(query.monsterGroupName(group) == "Wolves");
    group.alternate_name = true;
    REQUIRE(query.monsterName(monster) == "Hidden rat");
    REQUIRE(query.monsterGroupName(group) == "Hidden wolves");
    group.monsters.clear();
    REQUIRE(query.monsterGroupName(group) == "Hidden wolves"); // Only one is singular.
    monster.monster_species = 2;
    monster.monster_group_id = 999; // Al-name selection never needs a group lookup.
    candidate.status.sedexus_party_slot = 1;
    candidate.party.characters[1].name = "Ada";
    REQUIRE(query.monsterName(monster) == "Al-Ada");
    candidate.party.characters[1].name = "Bea";
    group.monster_id = 2;
    REQUIRE(query.monsterName(monster) == "Al-Bea");
    REQUIRE(query.monsterGroupName(group) == "Al-Bea");
    REQUIRE(candidate.status.retail.monster_name_cache == cache);
    REQUIRE(candidate.events.pending().empty());
    REQUIRE(outgoing.game.events.pending().empty());
}
