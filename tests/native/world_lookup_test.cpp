#include "surrender/srClipPlane.h"
#include "surrender/srCore.h"
#include "surrender/srScene.h"
#include "temporary_directory.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/game_timer.h"
#include "surrender/srClock.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return false; } } while (0)

namespace {
struct TestWorld {
    W8World world{};
    std::vector<Trigger*> triggers;
    std::vector<stParticle*> particles;

    TestWorld()
    {
        world.static_scene = new srScene(static_cast<srNode*>(0));
        world.dynamic_scene = new srNode(world.static_scene);
        world.triggers = &triggers;
        world.particles = &particles;
    }
    ~TestWorld()
    {
        for (Trigger* trigger : triggers) delete trigger;
        for (stParticle* particle : particles) delete particle;
        delete world.static_scene;
    }
};

Trigger* AddTrigger(TestWorld& world, const char* name)
{
    auto* trigger = new Trigger;
    std::strcpy(trigger->name, name);
    world.triggers.push_back(trigger);
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
    CHECK(FindTriggerByName("door") == newest);
    CHECK(FindTriggerByName("DOOR") == newest);
    CHECK(FindTriggerByName("SkyDoor") == in_sky);
    CHECK(FindTriggerByName("") == nullptr);
    CHECK(FindTriggerByName("MISSING") == nullptr);
    level.triggers.erase(level.triggers.begin() + 1);
    delete newest;
    CHECK(FindTriggerByName("DOOR") == first);
    SetCurrentWorld(0);
    SetSecondaryWorld(0);
    return true;
}

bool nodes(const std::filesystem::path& directory)
{
    TestWorld level, sky;
    SetCurrentWorld(&level.world);
    SetSecondaryWorld(&sky.world);

    auto* lamp = new stLight(level.world.dynamic_scene);
    lamp->setName("LAMP");
    auto* torch = new stLight(level.world.static_scene);
    torch->setName("TORCH");
    auto* sky_light = new stLight(sky.world.static_scene);
    sky_light->setName("SUN");
    auto* layer = new srClipPlane(level.world.static_scene);
    layer->setName("LAYER 1");

    CHECK(FindWorldNode<stLight>("LAMP") == lamp);
    CHECK(FindWorldNode<stLight>("lamp") == nullptr);
    CHECK(FindWorldNode<srNode>("SUN") == sky_light);
    CHECK(FindWorldNode<srClipPlane>("LAYER 1") == layer);
    CHECK(FindWorldNode<stLight>("LAYER 1") == nullptr);
    CHECK(FindLightByName("TORCH") == torch);

    int lights = 0;
    ForEachWorldNode<stLight>([&](stLight*) { ++lights; });
    CHECK(lights == 3);

    // Only trigger-toggled lights are saved; loading restores them by name.
    lamp->m_save_marked = true;
    lamp->setFlag(srNode::FLAG_DISABLE);
    const auto path = directory / "lights.bin";
    {
        auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        SaveLightStates(file.get());
    }
    lamp->clearFlag(srNode::FLAG_DISABLE);
    torch->setFlag(srNode::FLAG_DISABLE);
    {
        auto file = wiz8::open_host_file(path);
        LoadLightStates(file.get());
    }
    CHECK(lamp->testFlag(srNode::FLAG_DISABLE));
    CHECK(torch->testFlag(srNode::FLAG_DISABLE));

    srClock clock;
    g_shared_timer_base = &clock;
    auto* gas = new stParticle(level.world.dynamic_scene, 1);
    gas->setName("GAS");
    level.particles.push_back(gas);
    auto* idle = new stParticle(level.world.dynamic_scene, 1);
    idle->setName("IDLE");
    level.particles.push_back(idle);
    gas->persisted = true;
    gas->SetActive(0);
    {
        auto file = wiz8::open_host_file(path, wiz8::OpenMode::replace);
        SaveParticleStates(file.get());
    }
    gas->emitting = 1;
    {
        auto file = wiz8::open_host_file(path);
        LoadParticleStates(file.get());
    }
    CHECK(!gas->emitting && idle->emitting);
    // Particles are owned by the world list, not the scene.
    level.particles.clear();
    delete gas;
    delete idle;
    g_shared_timer_base = 0;

    SetCurrentWorld(0);
    SetSecondaryWorld(0);
    return true;
}
} // namespace

int main()
{
    if (!srInit()) return 1;
    const std::filesystem::path directory = make_temporary_directory("wiz8-world-lookup");
    const bool passed = triggers() && nodes(directory);
    std::filesystem::remove_all(directory);
    srExit();
    if (!passed) return 1;
    std::puts("ok: world trigger, light, clip plane and particle lookups");
}
