#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "asset_paths.h"
#include "filesystem.h"
#include "save_payload.h"
#include "scoped_asset_roots.h"
#include "scoped_game.h"
#include "scoped_game_data.h"
#include "temporary_directory.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/chunk.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/game_data.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/locks.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/level_population.h"
#include "wiz8/level_state.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/light.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/world/automap.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

namespace {

Trigger* AddTrigger(World& world, const char* name)
{
    auto owner = std::make_unique<Trigger>();
    auto* trigger = owner.get();
    std::strcpy(trigger->name, name);
    trigger->m_pWorld = &world;
    world.triggers.push_back(std::move(owner));
    return trigger;
}

bool triggers()
{
    CurrentGame().level.replacePrimary(std::make_unique<World>());
    CurrentGame().level.replaceSky(std::make_unique<World>());
    auto& level = *GetWorld();
    auto& sky = *GetSecondaryWorld();
    Trigger* first = AddTrigger(level, "DOOR");
    Trigger* newest = AddTrigger(level, "DOOR");
    Trigger* in_sky = AddTrigger(sky, "SKYDOOR");
    AddTrigger(level, "");
    REQUIRE((FindTriggerByName("door") == newest));
    REQUIRE((FindTriggerByName("DOOR") == newest));
    REQUIRE((FindTriggerByName("SkyDoor") == in_sky));
    REQUIRE((FindTriggerByName("") == nullptr));
    REQUIRE((FindTriggerByName("MISSING") == nullptr));
    level.triggers.erase(level.triggers.begin() + 1);
    REQUIRE((FindTriggerByName("DOOR") == first));
    first->initial_action = 0x34;
    first->position = {0, 0, 0};
    first->range_minimum = 0;
    first->range_maximum = 1000;
    first->flags = 0;
    REQUIRE(InsideDestinationTrigger(level, 0, 0, 0));
    REQUIRE_FALSE(InsideDestinationTrigger(sky, 0, 0, 0));
    // Removing an owner also removes its borrowed lookup entry.
    level.triggers.clear();
    REQUIRE(level.triggers.empty());
    REQUIRE(FindTriggerByName("DOOR") == nullptr);
    REQUIRE(FindTriggerByName("SKYDOOR") == in_sky);
    Trigger* replacement = AddTrigger(level, "DOOR");
    REQUIRE(FindTriggerByName("DOOR") == replacement);
    return true;
}

bool nodes(const std::filesystem::path& directory)
{
    CurrentGame().level.replacePrimary(std::make_unique<World>());
    CurrentGame().level.replaceSky(std::make_unique<World>());
    auto& level = *GetWorld();
    auto& sky = *GetSecondaryWorld();

    auto* lamp = CreateWorldLight(&level, "LAMP");
    lamp->setName("LAMP");
    auto* torch = CreateWorldLight(&level, "TORCH");
    torch->setName("TORCH");
    auto* sky_light = CreateWorldLight(&sky, "SUN");
    sky_light->setName("SUN");
    level.scene->clip_planes.push_back(std::make_unique<ClipPlane>());
    auto* layer = level.scene->clip_planes.back().get();
    layer->setName("LAYER 1");

    // Engine-light setup must keep its exact-type contract: Light has its
    // own group configuration even though it derives from Light.
    auto* engine_light = CreateWorldLight(&level, "TORCH");
    engine_light->setName("TORCH");
    engine_light->setGroupMask(7);
    torch->setGroupMask(9);
    FinalizeStaticScene(level);
    REQUIRE(engine_light->getGroupMask() == 1);
    REQUIRE(torch->getGroupMask() == 1);

    REQUIRE((FindWorldLight("LAMP") == lamp));
    REQUIRE((FindWorldLight("lamp") == nullptr));
    REQUIRE((FindWorldLight("SUN") == sky_light));
    REQUIRE((FindWorldClipPlane("LAYER 1") == layer));
    REQUIRE((FindWorldLight("LAYER 1") == nullptr));
    REQUIRE((FindLightByName("TORCH") == engine_light));

    int lights = 0;
    ForEachWorldLight([&](Light*) { ++lights; });
    REQUIRE((lights == 4));

    // Ancestry-based traversal includes both engine and game lights.
    int all_lights = 0;
    ForEachWorldLight([&](Light*) { ++all_lights; });
    REQUIRE(all_lights == 4);

    // Saved light states restore the lights by name.
    lamp->m_save_marked = true;
    lamp->visible = false;
    const auto path = directory / "lights.bin";
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        std::vector<W8LightState> states;
        ForEachWorldLight([&](Light* light) {
            if (light->m_save_marked) states.push_back({light->getName(), light->visible});
        });
        WriteLightStates(*file, states);
    }
    lamp->visible = true;
    torch->visible = false;
    {
        auto file = wiz8::open_file(path);
        ReadSavePayload(
            *file, [&level](W8Chunk& chunk) { ApplyLightStates(level, ReadLightStates(chunk)); });
    }
    REQUIRE(((!lamp->visible)));
    REQUIRE(((!torch->visible)));

    auto* gas = level.particles.emplace_back(std::make_unique<ParticleSystem>(&level, 1)).get();
    gas->setName("GAS");
    auto* idle = level.particles.emplace_back(std::make_unique<ParticleSystem>(&level, 1)).get();
    idle->setName("IDLE");
    gas->persisted = true;
    gas->SetActive(0);
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        WriteParticleStates(*file, std::vector<W8ParticleState>{{gas->getName(), gas->emitting}});
    }
    gas->emitting = 1;
    {
        auto file = wiz8::open_file(path);
        ReadSavePayload(*file, [&level](W8Chunk& chunk) {
            ApplyParticleStates(level, ReadParticleStates(chunk));
        });
    }
    REQUIRE((!gas->emitting && idle->emitting));
    // Particles are owned by the world list, not the scene.
    level.particles.clear();
    return true;
}
} // namespace

TEST_CASE("world_lookup_test")
{
    const ScopedGame game;
    const std::filesystem::path directory = make_temporary_directory("wiz8-world-lookup");
    const bool passed = triggers() && nodes(directory);
    // Empty worlds need no collection initialization and can be torn down
    // without ever loading a level or publishing them as the active world.
    for (int iteration = 0; iteration < 3; ++iteration) {
        const auto world = std::make_unique<World>();
        REQUIRE(world->scene != nullptr);
        REQUIRE(GetWorldProp(world.get(), 0) == nullptr);
        REQUIRE(GetWorldCameraPath(world.get(), -1) == nullptr);
        REQUIRE(GetWorldAmbientSound(world.get(), 0) == nullptr);
    }
    std::filesystem::remove_all(directory);
    REQUIRE(passed);
    std::puts("ok: world trigger, light, clip plane and particle lookups");
}

TEST_CASE("Failed resource construction preserves the active worlds and allows retry",
          "[levels][lifetime]")
{
    const ScopedGame game;
    ScopedTemporaryDirectory directory("wiz8-failed-world-load");
    const auto assets = directory.path / "assets";
    const auto user = directory.path / "user";
    std::filesystem::create_directory(assets);
    std::filesystem::create_directory(user);
    ScopedAssetRoots roots({assets.generic_string(), user.generic_string()});

    // Both owners remain usable while replacements are constructed locally.
    game.game.level.replacePrimary(std::make_unique<World>(8));
    auto* primary = GetWorld();
    game.game.level.replaceSky(std::make_unique<World>(-1, "ExistingSky.LVL"));
    auto* sky = GetSecondaryWorld();
    for (int attempt = 0; attempt < 2; ++attempt) {
        REQUIRE_THROWS_WITH(game.game.loadWorld(-1, "MissingSky.LVL"),
                            Catch::Matchers::ContainsSubstring("Couldn't open"));
        REQUIRE(GetSecondaryWorld() == sky);
        REQUIRE(GetWorld() == primary);
        // Traversal must derive from the live owners after the failed load.
        AnimateWorldItems(*primary);
        AnimateWorldItems(*sky);
    }

    const auto level_path = wiz8::asset_path("Levels/Test/level1.LVL");
    std::filesystem::create_directories(level_path.parent_path());
    {
        std::ofstream truncated(level_path);
        truncated.put('\0');
        REQUIRE(truncated.good());
    }
    W8LevelInfo info;
    REQUIRE(LevelBuildInfoByID(47, &info));
    // A malformed level passes path preflight, then fails before publication.
    CurrentGame().status.resume_level = 8; // A pending saved destination is not the active world.
    for (int attempt = 0; attempt < 2; ++attempt) {
        REQUIRE_THROWS_WITH(CurrentGame().travelToLevel(47, -1),
                            Catch::Matchers::ContainsSubstring("cannot read level"));
        REQUIRE(GetWorld() == primary);
        REQUIRE(GetSecondaryWorld() == sky);
        REQUIRE(GetLoadedLevelID() == 8);
        REQUIRE(CurrentGame().status.resume_level == 8);
        REQUIRE(GetWorld()->scene != nullptr);
        AnimateWorldItems(*primary);
        AnimateWorldItems(*sky);
    }
}

TEST_CASE("Malformed level status cannot change the active level or session counters",
          "[levels][records]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>(8));
    ScopedTemporaryDirectory directory("wiz8-unpublished-status");
    const auto path = directory.path / "level.sts";
    std::array<std::byte, 0x314> header{};
    const float version = 2.0f;
    const int next_group = 123;
    std::memcpy(header.data(), &version, sizeof(version));
    std::memcpy(header.data() + 4, &next_group, sizeof(next_group));
    W8Chunk output;
    REQUIRE(output.OpenWrite(path));
    output.WriteGroup(W8ChunkId("LVLS"), [&] {
        output.WriteValue(3);
        output.WriteChunk(W8ChunkId("STAT"),
                          [&] { output.WriteBytes(header.data(), header.size()); });
        output.WriteChunk(W8ChunkId("ITEM"), [&] { output.WriteValue(1u); });
    });
    output.Close();
    CurrentGame().status.next_group_id = 42;
    REQUIRE_THROWS(LevelState::decode(path, 3, true));
    REQUIRE(CurrentGame().status.next_group_id == 42);
    REQUIRE(GetLoadedLevelID() == 8);
}

TEST_CASE("Only published worlds schedule their trigger events", "[levels][events]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>(8));
    auto pending = std::make_unique<World>(3);
    const auto schedule = [](World& world) {
        auto trigger = std::make_unique<Trigger>(&world);
        auto& action = world.addTimedAction(trigger.get());
        action.completed = true;
        auto* owner = trigger.get();
        world.triggers.push_back(std::move(trigger));
        return owner;
    };
    auto* active_trigger = schedule(*GetWorld());
    auto* pending_trigger = schedule(*pending);
    UpdateTimedTriggerEvents();
    REQUIRE_FALSE(GetWorld()->findTimedAction(active_trigger));
    REQUIRE(pending->timed_events.front()->trigger == pending_trigger);
    REQUIRE(pending->timed_events.size() == 1);
    pending.reset();
    REQUIRE(GetLoadedLevelID() == 8);
    REQUIRE(GetWorld()->timed_events.empty());
}

TEST_CASE("Reward accounting follows the owned world, not a pending save destination",
          "[levels][rewards]")
{
    const ScopedGame game;
    GameData data{.strings = std::vector<std::string>(0x233)};
    data.strings[0x232] = "%d";
    const ScopedGameData installed(std::move(data));
    auto& characters = CurrentGame().party.characters;
    CurrentGame().party.members[0].emplace();
    characters[0].hp_current = 1;
    CurrentGame().status.resume_level = 8;
    game.game.level.replacePrimary(std::make_unique<World>(3));
    AddPartyGold(CurrentGame(), 1, false);
    AwardPartyExperience(1, 0);
    REQUIRE(CurrentGame().status.level_progress[3].gold_collected == 1);
    REQUIRE(CurrentGame().status.level_progress[3].experience_gained == 1);
    REQUIRE(CurrentGame().status.level_progress[8].gold_collected == 0);
    REQUIRE(CurrentGame().status.level_progress[8].experience_gained == 0);
    game.game.level.replacePrimary(nullptr);
    const auto suffix = CurrentGame().status.retail.status_header_suffix;
    AddPartyGold(CurrentGame(), 1, false);
    AwardPartyExperience(1, 0);
    REQUIRE(CurrentGame().party.gold == 2);
    REQUIRE(characters[0].experience == 2);
    REQUIRE(CurrentGame().status.level_progress[3].gold_collected == 1);
    REQUIRE(CurrentGame().status.level_progress[3].experience_gained == 1);
    REQUIRE(CurrentGame().status.retail.status_header_suffix == suffix);
}

TEST_CASE("An unloaded monster target has no position", "[levels][targeting]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>(8));
    W8CombatSlot target{};
    target.iType = W8_TARGET_KIND_MONSTER;
    target.iMonsterID = 42;
    target.point = {1, 2, 3};
    REQUIRE_FALSE(ResolveTargetPoint(&target, false));
    REQUIRE_FALSE(ResolveTargetPoint(&target, true));
    REQUIRE(target.point == glm::vec3(1, 2, 3));
}

TEST_CASE("An untimed camera trigger owns its shake until the event is removed",
          "[levels][events][shake]")
{
    ShakeSystem shakes;
    World world(-1, "", {}, &shakes);
    {
        TriggerEvent event(world);
        event.shake.emplace();
        event.Update();
        REQUIRE(event.shake->effect);
        REQUIRE(shakes.IsActive(*event.shake->effect));
        REQUIRE(shakes.Update({0, 0, 0}) == 1.0f);
        REQUIRE_FALSE(event.completed);
    }
    REQUIRE(shakes.Update({0, 0, 0}) == 0.0f);
}

TEST_CASE("Trigger names retain empty tokens, duplicates and authored spacing",
          "[levels][triggers]")
{
    REQUIRE(ParseTriggerNames("").empty());
    const std::vector<std::string> expected{"A", "", "A", " b", ""};
    REQUIRE(ParseTriggerNames("A,,A, b,") == expected);
}

TEST_CASE("Replacing a timed action keeps the executing action alive until the step ends",
          "[levels][events]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>(8));
    auto& world = *GetWorld();
    auto* trigger = AddTrigger(world, "TIMER");
    auto& previous = world.addTimedAction(trigger);
    auto& replacement = world.addTimedAction(trigger);
    REQUIRE(previous.completed);
    REQUIRE(world.findTimedAction(trigger) == &replacement);
    REQUIRE(world.timed_events.size() == 2);
    replacement.completed = true;
    UpdateTimedTriggerEvents();
    REQUIRE(world.timed_events.empty());
}

// A level left with something in every section decodes without its world and encodes
// back to the bytes it was stored as.
TEST_CASE("A left level's decoded state encodes to the bytes it was stored as", "[save]")
{
    const ScopedGame game;
    game.game.level.replacePrimary(std::make_unique<World>(3));
    auto& world = *GetWorld();
    auto& status = CurrentGame().status;
    status.next_group_id = 77;
    status.world_clock = 123456;
    auto& population = *world.population;
    for (int index = 0; index < 4; ++index) {
        auto group = std::make_unique<W8MonsterGroup>();
        group->group_id = index + 1;
        group->monster_id = 10 + index;
        group->leader_location_id = index + 1;
        group->encounter_registered = index == 2;
        group->formation = {float(index), 0.5f, 0.0f};
        group->allied_group_ids[0] = (index + 1) % 4 + 1;
        group->version = 1;
        group->forced_neutral = true;
        group->monsters = {index + 1};
        group->centre = {1.5f * index, 2.0f, 3.0f};
        (index == 3 ? population.encounter_groups : population.monster_groups)
            .push_back(std::move(group));
        auto record = std::make_unique<W8MonsterInfo>();
        record->location_id = index + 1;
        record->monster_group_id = index == 1 ? 0 : index + 1;
        record->monster_species = 10 + index;
        record->hp_current = 5 * index;
        record->position = {index * 10.0f, 1.0f, -2.0f};
        record->enchantments[1].turns = 3;
        auto& model = record->entity = std::make_unique<Monster>(world);
        model->location_id = index + 1;
        model->defining_orders = index == 0;
        model->patrol_distance = 4.5f;
        model->patrol_index = 2;
        model->vector = {{1, 2, 3}, {4, 5, 6}};
        model->direction_x = 0.25f;
        model->face_party = true;
        population.monster_roster.add(std::move(record), index == 2);
    }
    for (int index = 0; index < 2; ++index) {
        auto item = std::make_unique<W8WorldItem>();
        item->runtime_id = 40 + index;
        item->position = {1, 2, 3.0f * index};
        item->entity_flags = 0x18;
        item->unknown_3d[5] = 0x5a;
        if (index == 1) {
            auto inner = std::make_unique<W8WorldItem>();
            inner->runtime_id = 99;
            item->contents.push_back(std::move(inner));
        }
        population.world_items.push_back(std::move(item));
    }
    Trigger* door = AddTrigger(world, "DOOR01");
    door->flags = 0x1234;
    door->state_index = 2;
    door->action = 7;
    door->required_item_id = -1;
    door->m_lData1 = 5;
    W8DoorTriggerActionData data{};
    data.open = true;
    data.locked = true;
    data.secret = true;
    data.item = 12;
    door->action_payload = data;
    auto& timed = world.addTimedAction(door);
    timed.timer.SetDuration(5.0f);
    timed.timer.Restart();
    timed.timer.SetProgress(1.25f);
    door->lock_state.lock_type = LockType::pickable_lock;
    door->lock_state.device_state.completed = 3;
    door->lock_state.device_state.pins[2] = 9;
    door->lock_state.lock_countdown = 6;
    door->lock_state.last_interaction_clock = 1000;
    door->world_item_group = std::make_unique<W8WorldItem>();
    door->world_item_group->runtime_id = 55;
    door->gold = 17;
    door->uses_remaining = 2;
    door->item_group_seed = 0xdeadbeef;
    Trigger* plain = AddTrigger(world, "PLAIN");
    plain->required_item_id = 4;
    for (int index = 0; index < 2; ++index) {
        auto prop = std::make_unique<Prop>();
        prop->name = index ? "LEVER" : "GATE";
        prop->subcycle = 3 + index;
        prop->first_frame = 1;
        prop->last_frame = 9;
        prop->animation_playing = 1;
        prop->active = static_cast<unsigned char>(index);
        world.plsProps.push_back(std::move(prop));
    }
    for (int index = 0; index < 2; ++index) {
        auto node = std::make_unique<W8WorldCursorNode>();
        std::snprintf(node->name, sizeof(node->name), "Cube%03d", index);
        node->userdata = std::vector<std::byte>(3 + index, std::byte{0x41});
        world.cursor_nodes.push_back(std::move(node));
    }
    world.encounters.save_flag = 1;
    for (int index = 0; index < 2; ++index) {
        auto& generator = *world.encounters.generators.emplace_back(std::make_unique<MonGen>());
        std::snprintf(generator.name, sizeof(generator.name), "GEN%d", index);
        generator.flags = 0x40 + index;
        generator.generation_enabled = index == 0;
        generator.timer.setSeconds(4.0f);
        generator.timer.setScale(0.5f + index, false);
        generator.timer.advance(std::chrono::milliseconds{300});
    }
    for (int index = 0; index < 2; ++index) {
        auto sound = std::make_unique<W8AmbientSound>();
        sound->name = index ? "WIND" : "DRIP";
        sound->stopped = static_cast<unsigned char>(index);
        world.audio.emitters.push_back(std::move(sound));
    }
    auto* gas = world.particles.emplace_back(std::make_unique<ParticleSystem>(&world, 1)).get();
    gas->setName("GAS");
    gas->persisted = true;
    auto* lamp = CreateWorldLight(&world, "LAMP");
    lamp->setName("LAMP");
    lamp->m_save_marked = true;
    lamp->visible = false;
    {
        auto cells = wiz8::open_memory_writer();
        const float size = 2000.0f;
        const std::array<std::uint32_t, 6> count_and_keys{5, 1, 2, 3, 4, 5};
        cells->write(&size, sizeof(size));
        cells->write(count_and_keys.data(), sizeof(count_and_keys));
        REQUIRE(world.automap.readCells(*wiz8::open_memory(cells->written_memory())));
    }
    W8SavedAutomapNotes notes;
    notes.visited = {true, false, true, true, false};
    auto note = std::make_unique<W8AutomapNote>();
    note->position = {1.5f, 2.5f};
    note->layer = 2;
    note->text = "Hello Ж";
    notes.notes.push_back(std::move(note));
    world.automap.restore(std::move(notes));
    const auto encode = [](const LevelState& state) {
        const std::shared_ptr<wiz8::File> file = wiz8::open_memory_writer();
        W8Chunk image;
        REQUIRE(image.OpenWrite(file));
        state.encode(image);
        image.Close();
        return file->written_memory();
    };
    auto& left = CurrentGame().left_levels;
    const auto captured = LevelState::capture(CurrentGame().level, CurrentGame().status);
    left.store(captured);
    const auto state = left.decode(world.level_id);
    REQUIRE(state);
    CHECK(encode(*state) == encode(captured));
}
