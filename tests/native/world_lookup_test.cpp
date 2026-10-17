#include <array>
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
#include "layouts/world.h"
#include "scoped_asset_roots.h"
#include "surrender/srClipPlane.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "temporary_directory.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/music_playlist.h"
#include "wiz8/npc_interaction.h"
#include "world_fixture.h"

namespace {
std::unique_ptr<W8World> TestWorld()
{
    auto world = std::make_unique<W8World>();
    world->static_scene = std::make_unique<srScene>();
    world->dynamic_scene = world->static_scene->adoptChild(std::make_unique<srNode>());
    return world;
}

Trigger* AddTrigger(W8World& world, const char* name)
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
    ExchangeCurrentWorld(TestWorld());
    ExchangeSkyWorld(TestWorld());
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
    // Removing an owner also removes its borrowed lookup entry.
    DestroyAllWorldTriggers(&level);
    REQUIRE(level.triggers.empty());
    REQUIRE(FindTriggerByName("DOOR") == nullptr);
    REQUIRE(FindTriggerByName("SKYDOOR") == in_sky);
    Trigger* replacement = AddTrigger(level, "DOOR");
    REQUIRE(FindTriggerByName("DOOR") == replacement);
    ExchangeCurrentWorld(nullptr);
    ExchangeSkyWorld(nullptr);
    return true;
}

bool nodes(const std::filesystem::path& directory)
{
    ExchangeCurrentWorld(TestWorld());
    ExchangeSkyWorld(TestWorld());
    auto& level = *GetWorld();
    auto& sky = *GetSecondaryWorld();

    auto* lamp = level.dynamic_scene->adoptChild(std::make_unique<stLight>());
    lamp->setName("LAMP");
    auto* torch = level.static_scene->adoptChild(std::make_unique<stLight>());
    torch->setName("TORCH");
    auto* sky_light = sky.static_scene->adoptChild(std::make_unique<stLight>());
    sky_light->setName("SUN");
    auto* layer = level.static_scene->adoptChild(std::make_unique<srClipPlane>());
    layer->setName("LAYER 1");

    // Engine-light setup must keep its exact-type contract: stLight has its
    // own group configuration even though it derives from srLight.
    auto* engine_light = level.static_scene->adoptChild(std::make_unique<srLight>());
    engine_light->setName("TORCH");
    engine_light->setGroupMask(7);
    torch->setGroupMask(9);
    FinalizeStaticScene(level);
    REQUIRE(engine_light->getGroupMask() == 1);
    REQUIRE(torch->getGroupMask() == 9);

    REQUIRE((FindWorldNode<stLight>("LAMP") == lamp));
    REQUIRE((FindWorldNode<stLight>("lamp") == nullptr));
    REQUIRE((FindWorldNode<srNode>("SUN") == sky_light));
    REQUIRE((FindWorldNode<srClipPlane>("LAYER 1") == layer));
    REQUIRE((FindWorldNode<stLight>("LAYER 1") == nullptr));
    REQUIRE((FindLightByName("TORCH") == torch));

    int lights = 0;
    ForEachWorldNode<stLight>([&](stLight*) { ++lights; });
    REQUIRE((lights == 3));

    // Ancestry-based traversal includes both engine and game lights.
    int all_lights = 0;
    ForEachWorldNode<srLight>([&](srLight*) { ++all_lights; });
    REQUIRE(all_lights == 4);

    // Only trigger-toggled lights are saved; loading restores them by name.
    lamp->m_save_marked = true;
    lamp->setFlag(srNode::FLAG_DISABLE);
    const auto path = directory / "lights.bin";
    {
        auto file = wiz8::open_file(path, wiz8::OpenMode::replace);
        SaveLightStates(*file);
    }
    lamp->clearFlag(srNode::FLAG_DISABLE);
    torch->setFlag(srNode::FLAG_DISABLE);
    {
        auto file = wiz8::open_file(path);
        LoadLightStates(*file);
    }
    REQUIRE((lamp->testFlag(srNode::FLAG_DISABLE)));
    REQUIRE((torch->testFlag(srNode::FLAG_DISABLE)));

    auto* gas =
        level.particles.emplace_back(std::make_unique<stParticle>(level.dynamic_scene, 1)).get();
    gas->setName("GAS");
    auto* idle =
        level.particles.emplace_back(std::make_unique<stParticle>(level.dynamic_scene, 1)).get();
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
        LoadParticleStates(*file);
    }
    REQUIRE((!gas->emitting && idle->emitting));
    // Particles are owned by the world list, not the scene.
    level.particles.clear();

    ExchangeCurrentWorld(nullptr);
    ExchangeSkyWorld(nullptr);
    return true;
}
} // namespace

TEST_CASE("world_lookup_test")
{
    const std::filesystem::path directory = make_temporary_directory("wiz8-world-lookup");
    const bool passed = triggers() && nodes(directory);
    // Empty worlds need no collection initialization and can be torn down
    // without ever loading a level or publishing them as the active world.
    for (int iteration = 0; iteration < 3; ++iteration) {
        auto world = CreateWorld();
        REQUIRE(world->level != nullptr);
        REQUIRE(world->dynamic_scene->getParent() == world->static_scene.get());
        REQUIRE(GetWorldProp(world.get(), 0) == nullptr);
        REQUIRE(GetWorldCameraPath(world.get(), -1) == nullptr);
        REQUIRE(GetWorldAmbientSound(world.get(), 0) == nullptr);
        world.reset();
    }
    std::filesystem::remove_all(directory);
    REQUIRE(passed);
    std::puts("ok: world trigger, light, clip plane and particle lookups");
}

TEST_CASE("Failed resource construction preserves the active worlds and allows retry",
          "[levels][lifetime]")
{
    ScopedTemporaryDirectory directory("wiz8-failed-world-load");
    const auto assets = directory.path / "assets";
    const auto user = directory.path / "user";
    std::filesystem::create_directory(assets);
    std::filesystem::create_directory(user);
    ScopedAssetRoots roots({assets.generic_string(), user.generic_string()});

    // Both owners remain usable while replacements are constructed locally.
    ExchangeCurrentWorld(CreateWorld(8));
    auto* primary = GetWorld();
    ExchangeSkyWorld(CreateWorld(-1, "ExistingSky.LVL"));
    auto* sky = GetSecondaryWorld();
    for (int attempt = 0; attempt < 2; ++attempt) {
        REQUIRE_THROWS_WITH(LoadSkyWorld("MissingSky.LVL"),
                            Catch::Matchers::ContainsSubstring("Could not open level file."));
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
    g_status.resume_level = 8; // A pending saved destination is not the active world.
    for (int attempt = 0; attempt < 2; ++attempt) {
        REQUIRE_THROWS_WITH(LoadLevel(47, -1, false),
                            Catch::Matchers::ContainsSubstring("Problem loading level"));
        REQUIRE(GetWorld() == primary);
        REQUIRE(GetSecondaryWorld() == sky);
        REQUIRE(GetLoadedLevelID() == 8);
        REQUIRE(g_status.resume_level == 8);
        REQUIRE(GetWorld()->static_scene != nullptr);
        DetachAllWorldItems();
    }
    ExchangeCurrentWorld(nullptr);
    ExchangeSkyWorld(nullptr);
}

TEST_CASE("Reward accounting follows the owned world, not a pending save destination",
          "[levels][rewards]")
{
    struct Restore {
        W8GlobalStatus status = g_status;
        std::vector<std::string> strings = std::move(g_localized_strings);
        ~Restore()
        {
            ExchangeCurrentWorld(nullptr);
            ClearNpcMessageQueue();
            g_status = std::move(status);
            g_localized_strings = std::move(strings);
        }
    } restore;
    std::array<W8Character, 8> characters{};
    std::array<W8PartySlotRow, 8> rows{};
    g_status = W8GlobalStatus{};
    g_status.buffers.Char = characters.data();
    g_status.buffers.XChar = rows.data();
    rows[0].fOccupied = true;
    characters[0].hp_current = 1;
    g_localized_strings.resize(0x233);
    g_localized_strings[0x232] = "%d";
    g_status.resume_level = 8;
    ExchangeCurrentWorld(CreateWorld(3));
    AddPartyGold(1, false);
    AwardPartyExperience(1, 0);
    REQUIRE(g_status.level_progress[3].gold_collected == 1);
    REQUIRE(g_status.level_progress[3].experience_gained == 1);
    REQUIRE(g_status.level_progress[8].gold_collected == 0);
    REQUIRE(g_status.level_progress[8].experience_gained == 0);
    ExchangeCurrentWorld(nullptr);
    const auto suffix = std::to_array(g_status.status_header_suffix);
    AddPartyGold(1, false);
    AwardPartyExperience(1, 0);
    REQUIRE(g_status.party_gold == 2);
    REQUIRE(characters[0].experience == 2);
    REQUIRE(g_status.level_progress[3].gold_collected == 1);
    REQUIRE(g_status.level_progress[3].experience_gained == 1);
    REQUIRE(std::to_array(g_status.status_header_suffix) == suffix);
}
