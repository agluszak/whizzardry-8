#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srScene.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/character_event_queue.h"
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
    model->setReusable(true);
    model->InitializeVertexFrames(1);
    model->GetVertexLocations(0, true, 0.0f);
    srModel::Client client;
    client.setModel(model);
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
    client.setModel(nullptr); // Old clients must not unregister from this registry.
    g_mouse_surface.reset();
    palette.reset();
    CHECK(g_mesh_models.empty() && g_decompressed_mesh_bytes == 0);
    CHECK(registry->getNumberOfInstances(srRuntimeClass::sGetClassNode(), 0) == baseline);
    CHECK(srExit());
    return true;
}

static bool reusable_factory_boundary()
{
    CHECK(srInit());
    {
        auto unique = std::make_unique<stMeshModel>(0, 0);
        CHECK(!unique->canReuse());
        bool rejected = false;
        try { unique->setReusable(true); }
        catch (const std::logic_error&) { rejected = true; }
        CHECK(rejected && !unique->canReuse());
        auto shared = std::make_shared<stMeshModel>(0, 0);
        shared->setReusable(true);
        CHECK(shared->canReuse());
        srClass& erased = *shared;
        for (int operation = 0; operation != 3; ++operation) {
            bool rejected = false;
            try {
                if (operation == 0) erased.addReference();
                else if (operation == 1) erased.autoRelease();
                else erased.release();
            } catch (const std::logic_error&) { rejected = true; }
            CHECK(rejected && shared->canReuse());
        }
        std::unique_ptr<stMeshModel> clone(static_cast<stMeshModel*>(shared->clone()));
        CHECK(!clone->canReuse());
    }
    CHECK(srExit());
    return true;
}

static bool npc_script_teardown()
{
    auto* file = static_cast<W8NpcScriptFile*>(std::calloc(1, sizeof(W8NpcScriptFile)));
    std::unique_ptr<W8NpcScriptFile, decltype(&ReleaseNpcScriptFile)> owner(file, &ReleaseNpcScriptFile);
    CHECK(file);
    file->name = static_cast<char*>(std::calloc(8, 1));
    file->quote_count = 2;
    file->quotes = static_cast<W8NpcScriptQuote*>(std::calloc(2, sizeof(W8NpcScriptQuote)));
    CHECK(file->quotes);
    for (int index = 0; index != 2; ++index) {
        auto& quote = file->quotes[index];
        quote.subquote_count = 2;
        quote.subquotes = static_cast<char**>(std::calloc(2, sizeof(char*)));
        CHECK(quote.subquotes);
        for (int text = 0; text != 2; ++text)
            quote.subquotes[text] = static_cast<char*>(std::calloc(8, 1));
        quote.entry_count = 2;
        quote.entries = static_cast<W8NpcQuoteEntry*>(std::calloc(2, sizeof(W8NpcQuoteEntry)));
        CHECK(quote.entries);
        for (int entry = 0; entry != 2; ++entry) {
            auto& record = quote.entries[entry];
            record.sub_entry_count = 2;
            record.sub_entries = static_cast<W8NpcQuoteSubEntry*>(std::calloc(2, sizeof(W8NpcQuoteSubEntry)));
            CHECK(record.sub_entries);
            for (int sub = 0; sub != 2; ++sub)
                record.sub_entries[sub].text = static_cast<char*>(std::calloc(8, 1));
        }
    }
    owner.reset();
    file = static_cast<W8NpcScriptFile*>(std::calloc(1, sizeof(W8NpcScriptFile)));
    owner.reset(file);
    CHECK(file);
    file->name = static_cast<char*>(std::calloc(8, 1));
    // Zero-quote scripts still own their name and header.
    return true;
}

static bool queued_state_teardown()
{
    {
        W8CharacterEventQueue queue;
        queue.pending_events.Add(new W8CharacterEvent(nullptr, 0, 0, 0, 0));
        queue.npc_deferred_events.Add(new W8CharacterEvent(nullptr, 0, 0, 0, 0));
        queue.vector0.Add(new W8CharacterEvent(nullptr, 0, 0, 0, 0));
        queue.vector1.Add(new W8CharacterEvent(nullptr, 0, 0, 0, 0));
    } // Every pending vector is owning, even when no frame dispatched it.
    AddDialogueTranscriptKeyword(L"Monastery", 0);
    AddDialogueTranscriptKeyword(L"Arnika", 0);
    CHECK(g_npc_interaction_state->dialogue_transcript.GetCount() == 2);
    ClearNpcDialogueTranscript();
    ClearNpcDialogueTranscript();
    CHECK(g_npc_interaction_state->dialogue_transcript.GetCount() == 0);
    return true;
}

int main()
{
    if (!shutdown_survivors() || !shutdown_survivors() || !reusable_factory_boundary() || !npc_script_teardown() || !queued_state_teardown()) return 1;
    std::puts("ok: detached resource destruction, interrupted scenes, reinitialization and reuse ownership");
}
