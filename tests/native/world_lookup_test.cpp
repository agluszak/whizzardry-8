#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "chunk_payload.h"
#include "filesystem.h"
#include "layouts/world.h"
#include "surrender/srClipPlane.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "temporary_directory.h"
#include "wiz8/chunk.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"

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
        LoadLightStates(ChunkPayload(*file).get());
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
        LoadParticleStates(ChunkPayload(*file).get());
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

TEST_CASE("Prop save records are decoded within their chunk before application", "[save]")
{
    ScopedTemporaryDirectory directory{"prop-records"};
    const auto path = directory.path / "props.riff";
    W8World world{};
    auto owned_prop = std::make_unique<W8Prop>();
    auto& prop = *owned_prop;
    prop.id = 42;
    prop.name = "DOOR";
    world.plsProps.push_back(std::move(owned_prop));
    for (bool named : {false, true}) {
        W8Chunk writer;
        REQUIRE(writer.OpenWrite(path));
        writer.WriteChunk(W8ChunkId("APST"), [&] {
            if (named) {
                writer.WriteValue(0xDEADD00Du);
                writer.WriteValue(1u);
            }
            writer.WriteValue(2u);
            for (bool missing : {true, false}) {
                if (named) {
                    char name[64]{};
                    std::strcpy(name, missing ? "MISSING" : "DOOR");
                    writer.WriteBytes(name, sizeof(name));
                } else
                    writer.WriteValue(missing ? 99 : 42);
                const unsigned char state[6]{7, 2, 9, 1, 1, 0xff};
                writer.WriteBytes(state, sizeof(state));
            }
        });
        writer.Close();
        W8Chunk reader;
        REQUIRE(reader.OpenRead(path));
        REQUIRE(reader.OpenChunk(0, 0));
        prop.Rep()->subcycle = 0;
        LoadWorldProps(&world, reader);
        REQUIRE(prop.Rep()->subcycle == 7);
        REQUIRE(prop.Rep()->first_frame == 2);
        REQUIRE(prop.Rep()->last_frame == 9);
        REQUIRE(reader.RemainingBytes() == 0);
    }
    W8Chunk writer;
    REQUIRE(writer.OpenWrite(path));
    writer.WriteChunk(W8ChunkId("APST"), [&] {
        writer.WriteValue(1u);
        writer.WriteValue(42);
        const unsigned char partial[5]{3, 0, 4, 0, 0};
        writer.WriteBytes(partial, sizeof(partial));
    });
    writer.WriteChunk(W8ChunkId("NEXT"), [&] { writer.WriteValue(0x12345678u); });
    writer.Close();
    W8Chunk reader;
    REQUIRE(reader.OpenRead(path));
    REQUIRE(reader.OpenChunk(0, 0));
    REQUIRE_THROWS(LoadWorldProps(&world, reader));
    REQUIRE(prop.Rep()->subcycle == 7);
    REQUIRE(prop.Rep()->first_frame == 2);
    reader.SkipCurrentChunk();
    reader.ReleaseCurrentChunk();
    REQUIRE(reader.OpenChunk(0, 0));
    REQUIRE(reader.CurrentChunkId() == W8ChunkId("NEXT"));
    REQUIRE(reader.ReadValue<unsigned>() == 0x12345678u);
    world.plsProps.clear();
}
