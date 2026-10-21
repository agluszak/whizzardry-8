#include <array>
#include <cstddef>
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
#include "wiz8/chunk.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/game_data.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/clip_plane.h"
#include "wiz8/render/light.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/targeting.h"
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

    // Only trigger-toggled lights are saved; loading restores them by name.
    lamp->m_save_marked = true;
    lamp->visible = false;
    const auto path = directory / "lights.bin";
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        SaveLightStates(*file);
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

    auto* gas =
        level.particles.emplace_back(std::make_unique<ParticleSystem>((level.scene.get()), 1))
            .get();
    gas->setName("GAS");
    auto* idle =
        level.particles.emplace_back(std::make_unique<ParticleSystem>((level.scene.get()), 1))
            .get();
    idle->setName("IDLE");
    gas->persisted = true;
    gas->SetActive(0);
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        SaveParticleStates(*file);
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
        REQUIRE_THROWS_WITH(LoadSkyWorld("MissingSky.LVL"),
                            Catch::Matchers::ContainsSubstring("Couldn't open"));
        REQUIRE(GetSecondaryWorld() == sky);
        REQUIRE(GetWorld() == primary);
        // Traversal must derive from the live owners after the failed load.
        DetachAllWorldItems();
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
        REQUIRE_THROWS_WITH(LoadLevel(47, -1, false),
                            Catch::Matchers::ContainsSubstring("cannot read level"));
        REQUIRE(GetWorld() == primary);
        REQUIRE(GetSecondaryWorld() == sky);
        REQUIRE(GetLoadedLevelID() == 8);
        REQUIRE(CurrentGame().status.resume_level == 8);
        REQUIRE(GetWorld()->scene != nullptr);
        DetachAllWorldItems();
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
    auto pending = std::make_unique<World>(3);
    REQUIRE_THROWS(ReadLevelStatus(*pending, path, 3, true));
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
        trigger->m_pEvent = std::make_unique<TriggerEvent>();
        trigger->m_pEvent->trigger = trigger.get();
        trigger->m_pEvent->completed = true;
        auto* owner = trigger.get();
        world.timed_events.push_back(trigger->m_pEvent.get());
        world.triggers.push_back(std::move(trigger));
        return owner;
    };
    auto* active_trigger = schedule(*GetWorld());
    auto* pending_trigger = schedule(*pending);
    UpdateTimedTriggerEvents();
    REQUIRE_FALSE(active_trigger->m_pEvent);
    REQUIRE(pending_trigger->m_pEvent);
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
    {
        TriggerEvent event;
        event.shake.emplace();
        event.Update();
        REQUIRE(event.shake->effect);
        REQUIRE(CameraShakes().IsActive(*event.shake->effect));
        REQUIRE(CameraShakes().Update({0, 0, 0}) == 1.0f);
        REQUIRE_FALSE(event.completed);
    }
    REQUIRE(CameraShakes().Update({0, 0, 0}) == 0.0f);
}

TEST_CASE("Trigger names retain empty tokens, duplicates and authored spacing",
          "[levels][triggers]")
{
    REQUIRE(ParseTriggerNames("").empty());
    const std::vector<std::string> expected{"A", "", "A", " b", ""};
    REQUIRE(ParseTriggerNames("A,,A, b,") == expected);
}
