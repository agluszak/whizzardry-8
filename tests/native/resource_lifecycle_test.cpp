#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srScene.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"

#include <cstdio>
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

int main()
{
    if (!shutdown_survivors() || !shutdown_survivors() || !reusable_factory_boundary()) return 1;
    std::puts("ok: detached resource destruction, interrupted scenes, reinitialization and reuse ownership");
}
