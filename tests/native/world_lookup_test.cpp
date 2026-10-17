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
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/music_playlist.h"

namespace {
struct TestWorld {
    W8World world{};
    std::vector<std::unique_ptr<Trigger>>& triggers = world.triggers;
    std::vector<std::unique_ptr<stParticle>>& particles = world.particles;

    TestWorld()
    {
        world.static_scene = std::make_unique<srScene>();
        world.dynamic_scene = world.static_scene->adoptChild(std::make_unique<srNode>());
    }
    ~TestWorld()
    {
        DestroyAllWorldTriggers(&world);
        particles.clear();
        world.static_scene.reset();
    }
};

Trigger* AddTrigger(TestWorld& world, const char* name)
{
    auto owner = std::make_unique<Trigger>();
    auto* trigger = owner.get();
    std::strcpy(trigger->name, name);
    trigger->m_pWorld = &world.world;
    world.triggers.push_back(std::move(owner));
    return trigger;
}

bool triggers()
{
    TestWorld level, sky;
    SetCurrentWorld(&level.world);
    SetSecondaryWorld(&sky.world);
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
    DestroyAllWorldTriggers(&level.world);
    REQUIRE(level.world.triggers.empty());
    REQUIRE(FindTriggerByName("DOOR") == nullptr);
    REQUIRE(FindTriggerByName("SKYDOOR") == in_sky);
    Trigger* replacement = AddTrigger(level, "DOOR");
    REQUIRE(FindTriggerByName("DOOR") == replacement);
    SetCurrentWorld(0);
    SetSecondaryWorld(0);
    return true;
}

bool nodes(const std::filesystem::path& directory)
{
    TestWorld level, sky;
    SetCurrentWorld(&level.world);
    SetSecondaryWorld(&sky.world);

    auto* lamp = level.world.dynamic_scene->adoptChild(std::make_unique<stLight>());
    lamp->setName("LAMP");
    auto* torch = level.world.static_scene->adoptChild(std::make_unique<stLight>());
    torch->setName("TORCH");
    auto* sky_light = sky.world.static_scene->adoptChild(std::make_unique<stLight>());
    sky_light->setName("SUN");
    auto* layer = level.world.static_scene->adoptChild(std::make_unique<srClipPlane>());
    layer->setName("LAYER 1");

    // Engine-light setup must keep its exact-type contract: stLight has its
    // own group configuration even though it derives from srLight.
    auto* engine_light = level.world.static_scene->adoptChild(std::make_unique<srLight>());
    engine_light->setName("TORCH");
    engine_light->setGroupMask(7);
    torch->setGroupMask(9);
    FinalizeStaticScene(level.world.static_scene.get());
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
        level.particles.emplace_back(std::make_unique<stParticle>(level.world.dynamic_scene, 1))
            .get();
    gas->setName("GAS");
    auto* idle =
        level.particles.emplace_back(std::make_unique<stParticle>(level.world.dynamic_scene, 1))
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
        LoadParticleStates(*file);
    }
    REQUIRE((!gas->emitting && idle->emitting));
    // Particles are owned by the world list, not the scene.
    level.particles.clear();

    SetCurrentWorld(0);
    SetSecondaryWorld(0);
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
        auto* world = CreateWorld();
        REQUIRE(world->level != nullptr);
        REQUIRE(world->dynamic_scene->getParent() == world->static_scene.get());
        REQUIRE(GetWorldProp(world, 0) == nullptr);
        REQUIRE(GetWorldCameraPath(world, -1) == nullptr);
        REQUIRE(GetWorldAmbientSound(world, 0) == nullptr);
        DestroyWorld(world);
    }
    std::filesystem::remove_all(directory);
    REQUIRE(passed);
    std::puts("ok: world trigger, light, clip plane and particle lookups");
}

TEST_CASE("Failed world asset loads release published worlds and allow retry", "[levels][lifetime]")
{
    ScopedTemporaryDirectory directory("wiz8-failed-world-load");
    const auto assets = directory.path / "assets";
    const auto user = directory.path / "user";
    std::filesystem::create_directory(assets);
    std::filesystem::create_directory(user);
    ScopedAssetRoots roots({assets.generic_string(), user.generic_string()});

    // A failed sky load must unregister the partial sky without touching the primary world.
    auto primary = std::unique_ptr<W8World>(CreateWorld());
    SetCurrentWorld(primary.get());
    W8LevelInfo missing_sky{"", "MissingSky.LVL"};
    for (int attempt = 0; attempt < 2; ++attempt) {
        REQUIRE_THROWS_WITH(LoadSkyWorld(0, &missing_sky),
                            Catch::Matchers::ContainsSubstring("Could not open level file."));
        REQUIRE(GetSecondaryWorld() == nullptr);
        REQUIRE(GetWorld() == primary.get());
        // The update registry must not retain the destroyed sky.
        DetachAllWorldItems();
    }
    primary.reset();
    REQUIRE(GetWorld() == nullptr);

    REQUIRE(InitializeMusicPlaylist());
    std::filesystem::create_directories(assets / "Levels" / "Test");
    {
        std::ofstream truncated(assets / "Levels" / "Test" / "level1.LVL");
        truncated.put('\0');
        REQUIRE(truncated.good());
    }
    // The path exists, so loading reaches ReadLevel after publishing the replacement.
    // A failed replacement cannot restore an old level ID whose world is gone.
    for (int attempt = 0; attempt < 2; ++attempt) {
        g_status.current_level = 8;
        REQUIRE_THROWS_WITH(LoadLevel(47, -1, false),
                            Catch::Matchers::ContainsSubstring("Problem loading level"));
        REQUIRE(GetWorld() == nullptr);
        REQUIRE(GetSecondaryWorld() == nullptr);
        REQUIRE(g_status.current_level == -1);
        DetachAllWorldItems();
    }
}
