#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srScene.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"

#include <cstdio>
#include <cstdlib>
#include "wiz8/npc_script_file.h"
#include <memory>
#include <stdexcept>

extern std::shared_ptr<srColorSurface> g_mouse_surface;
extern std::unique_ptr<srColorSurface> g_automap_surface;

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
        return false; \
    } \
} while (0)

static bool shutdown_survivors()
{
    CHECK(srInit());
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
    auto* registry = srCore.getRegistry();
    const auto before = registry->getNumberOfInstances(srRuntimeClass::sGetClassNode(), 0);
    try {
        auto scene = std::make_unique<InterruptedScene>(model);
        CHECK(false);
    } catch (const std::runtime_error&) {}
    CHECK(registry->getNumberOfInstances(srRuntimeClass::sGetClassNode(), 0) == before);

    // These are the actual global owners used by mouse and automap rendering.
    CHECK(srExit());
    CHECK(srCore.getRegistry() == nullptr);
    g_mouse_surface->setName("Detached mouse surface");
    CHECK(g_mouse_surface->getName() == "Detached mouse surface");
    g_automap_surface.reset(); // Destruction without a live registry.
    CHECK(srInit());
    registry = srCore.getRegistry();
    const auto baseline = registry->getNumberOfInstances(srRuntimeClass::sGetClassNode(), 0);
    model.reset();
    survivor.reset(); // Old resources must not unregister from this registry.
    g_mouse_surface.reset();
    palette.reset();
    CHECK(g_mesh_models.empty() && g_decompressed_mesh_bytes == 0);
    CHECK(registry->getNumberOfInstances(srRuntimeClass::sGetClassNode(), 0) == baseline);
    CHECK(srExit());
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
    CHECK(static_cast<int>(g_npc_interaction_state->dialogue_transcript.size()) == 2);
    ClearNpcDialogueTranscript();
    ClearNpcDialogueTranscript();
    CHECK(g_npc_interaction_state->dialogue_transcript.size() == 0);
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
    CHECK(IsMessageBoxLineQueueEmpty());
    return true;
}

int main()
{
    if (!shutdown_survivors() || !shutdown_survivors() || !npc_script_teardown() || !queued_state_teardown()) return 1;
    std::puts("ok: detached resource destruction, interrupted scenes, and reinitialization");
}
