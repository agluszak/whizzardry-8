#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srScene.h"
#include "surrender/srClock.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_script_file.h"
#include "wiz8/asset_paths.h"
#include "wiz8/engine_code/AnimObj.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/game_timer.h"
#include "temporary_directory.h"
#include "local_code/NPCScripting.h"
#include "surrender/srPixelConvert.h"

extern std::shared_ptr<srColorSurface> g_mouse_surface;
extern std::unique_ptr<srColorSurface> g_automap_surface;


static bool shutdown_survivors()
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
    return true;
}

static bool npc_script_teardown()
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

    return true;
}

static bool queued_state_teardown()
{
    {
        W8CharacterEventQueue queue;
        queue.pending_events.push_back(new W8CharacterEvent(nullptr, 0, 0, 0, 0));
        queue.npc_deferred_events.push_back(new W8CharacterEvent(nullptr, 0, 0, 0, 0));
        queue.vector0.push_back(new W8CharacterEvent(nullptr, 0, 0, 0, 0));
        queue.vector1.push_back(new W8CharacterEvent(nullptr, 0, 0, 0, 0));
    } // Every pending vector is owning, even when no frame dispatched it.
    AddDialogueTranscriptKeyword("Monastery", 0);
    AddDialogueTranscriptKeyword("Arnika", 0);
    REQUIRE((static_cast<int>(g_npc_interaction_state->dialogue_transcript.size()) == 2));
    ClearNpcDialogueTranscript();
    ClearNpcDialogueTranscript();
    REQUIRE((g_npc_interaction_state->dialogue_transcript.size() == 0));
    for (auto kind : {W8_NPC_MSG_PORTRAIT_MESSAGE, W8_NPC_MSG_PORTRAIT_EXTRA,
                      W8_NPC_MSG_SKILL_NOTICES, W8_NPC_MSG_LEVEL_UP}) {
        W8MessageBoxPayload extra{};
        if (kind == W8_NPC_MSG_PORTRAIT_EXTRA) extra.experience = new W8ExperienceNoticePayload{};
        if (kind == W8_NPC_MSG_SKILL_NOTICES) extra.skill_notices = new W8SkillNoticePayload{};
        if (kind == W8_NPC_MSG_LEVEL_UP) extra.level_up_slot = new int{};
        AddTextMessageBoxLine(kind, std::string(80, 't'), extra);
    }
    ClearNpcMessageQueue();
    ClearNpcMessageQueue();
    REQUIRE((IsMessageBoxLineQueueEmpty()));
    return true;
}

TEST_CASE("resource_lifecycle_test")
{
    REQUIRE(shutdown_survivors()); REQUIRE(shutdown_survivors()); REQUIRE(npc_script_teardown()); REQUIRE(queued_state_teardown());
    std::puts("ok: detached resource destruction, interrupted scenes, and reinitialization");
}

struct SpellVisualFixture
{
    ScopedTemporaryDirectory directory{"wiz8-spell-visuals"};
    w8_native::Roots previous_roots = w8_native::path_roots();
    W8World* previous_world = GetWorld();
    srClock* previous_clock = g_shared_timer_base;
    srClock clock;
    W8Octree* previous_octree = g_octree;
    std::unique_ptr<W8Octree> octree;
    W8World world{};
    std::vector<W8SpellVisual*> visuals;

    SpellVisualFixture()
    {
        REQUIRE(srInit());
        g_shared_timer_base = &clock;
        octree = std::make_unique<W8Octree>(nullptr, nullptr);
        g_octree = octree.get();
        std::filesystem::create_directory(directory.path / "assets");
        std::filesystem::create_directory(directory.path / "user");
        w8_native::configure_paths({(directory.path / "assets").string(),
                                   (directory.path / "user").string(), {"", "", ""}});
        world.spell_visuals = &visuals;
        SetCurrentWorld(&world);
    }

    ~SpellVisualFixture()
    {
        DestroyAllSpellVisuals(&world);
        octree.reset();
        g_octree = previous_octree;
        g_shared_timer_base = previous_clock;
        SetCurrentWorld(previous_world);
        w8_native::configure_paths(previous_roots);
        srExit();
    }
};

TEST_CASE_METHOD(SpellVisualFixture, "Spell visual fallback and registration", "[resources][spell]")
{
    W8SpellVisual named, generic;
    named.mode = generic.mode = W8_SPELL_VISUAL_EXPLOSION;
    const int first_cycle = W8_SPELL_VISUAL_EXPLOSION * 7;
    generic.host->emitters[first_cycle + 1] = new W8AnimObj;
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
    named.host->emitters[first_cycle + 2] = new W8AnimObj;
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
    const int cycle = static_cast<int>(std::size(source->emitters)) - 1;
    source->emitters[cycle] = new W8AnimObj;
    source->emitters[cycle]->playback_scale = 3.5f;
    source->emitter_playback_scales[cycle] = 7.25f;
    source->light_lists[cycle].push_back(nullptr);
    source->light_lists[cycle].push_back(new std::vector<stLight*>);
    auto copy = std::make_unique<Host>(*source);
    REQUIRE(copy->emitters[cycle] != source->emitters[cycle]);
    REQUIRE(copy->light_lists[cycle][1] != source->light_lists[cycle][1]);
    source.reset();
    REQUIRE(copy->emitters[cycle]->playback_scale == 3.5f);
    REQUIRE(copy->emitter_playback_scales[cycle] == 7.25f);
    REQUIRE(copy->value_0ac == 11);
    REQUIRE(copy->value_0b0 == 22);
    REQUIRE(copy->light_lists[cycle][0] == nullptr);
    REQUIRE(copy->light_lists[cycle][1]->empty());
}

TEST_CASE_METHOD(SpellVisualFixture, "Emitter clones outlive their source", "[resources][spell]")
{
    CheckEmitterCloneLifetime<W8MissileRep>();
    CheckEmitterCloneLifetime<W8SpellEmitterHost>();
}
