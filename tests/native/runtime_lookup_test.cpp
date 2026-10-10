#include "surrender/srClipPlane.h"
#include "surrender/srFog.h"
#include "surrender/srLight.h"
#include "surrender/srMaterial.h"
#include "surrender/srPalette.h"
#include "surrender/srScene.h"

#include <cstdio>
#include <cstring>
#include <string>

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
        return false; \
    } \
} while (0)

static bool lookup()
{
    CHECK(srInit() == 1);
    auto* registry = srCore.getRegistry();
    auto* runtime = srRuntimeClass::sGetClassNode();
    auto* nodes = srNode::sGetClassNode();
    auto* clips = srClipPlane::sGetClassNode();
    const auto baseline = registry->getNumberOfInstances(runtime, 0);
    w8_ulong deleted_id = 0;
    {
        srClipPlane first;
        srClipPlane second;
        srScene scene;
        srFog fog;
        CHECK(registry->getNumberOfInstances(runtime, 0) == baseline + 4);
        CHECK(registry->getNumberOfInstances(clips, 1) == 2);
        CHECK(first.getClassID() == 0x1500);
        CHECK(std::strcmp(first.getClassName(), "srClipPlane") == 0);
        CHECK(fog.getClassID() == 0x1210);
        CHECK(first.matchClassID(0x1000));
        CHECK(first.matchClassID(0x100));
        CHECK(!first.matchClassID(0x1210));
        CHECK(!first.matchClassID(0xffffffff));
        CHECK(registry->getClassID(clips) == 0x1500);
        CHECK(std::strcmp(registry->getClassName(clips), "srClipPlane") == 0);

        first.setName("AutomapLayer");
        second.setName("AutomapLayer");
        scene.setName("AutomapLayer");
        CHECK(registry->find(clips, "AutomapLayer", nullptr) == &second);
        CHECK(registry->find(clips, "AutomapLayer", &second) == &first);
        CHECK(registry->find(clips, "AutomapLayer", &first) == nullptr);
        CHECK(registry->find(nodes, "AutomapLayer", nullptr) == &scene);
        CHECK(registry->findExact(nodes, "AutomapLayer", nullptr) == nullptr);
        CHECK(registry->findExact(clips, "AutomapLayer", nullptr) == &second);
        CHECK(registry->find(clips, "automaplayer", nullptr) == nullptr);
        CHECK(registry->find(clips, std::string_view{}, nullptr) == nullptr);
        CHECK(registry->find(nullptr, "AutomapLayer", nullptr) == nullptr);

        second.setName("RenamedLayer");
        CHECK(registry->find(clips, "AutomapLayer", nullptr) == &first);
        CHECK(registry->find(clips, "RenamedLayer", nullptr) == &second);
        second.setName(second.getName());
        CHECK(registry->find(clips, "RenamedLayer", nullptr) == &second);
        second.setName("");
        CHECK(!second.isNamed());
        CHECK(registry->find(clips, "RenamedLayer", nullptr) == nullptr);
        first.setName({});
        CHECK(registry->find(clips, "AutomapLayer", nullptr) == nullptr);
        CHECK(registry->find(nodes, "AutomapLayer", nullptr) == &scene);
        const std::string long_name(2048, 'x');
        first.setName(long_name.c_str());
        CHECK(registry->find(clips, long_name.c_str(), nullptr) == &first);

        deleted_id = first.getID();
        CHECK(deleted_id != second.getID());
        CHECK(registry->find(nodes, deleted_id) == &first);
        CHECK(registry->findExact(nodes, deleted_id) == nullptr);
        CHECK(registry->findExact(clips, deleted_id) == &first);
        CHECK(registry->find(clips, w8_ulong(0)) == nullptr);
        CHECK(registry->find(clips, static_cast<const srRuntimeClass*>(nullptr)) == &second);
        CHECK(registry->find(clips, &second) == &first);
        CHECK(registry->find(clips, &first) == nullptr);
        // Level cleanup saves the next object before releasing the current object.
        auto* transient = new srClipPlane;
        auto* next = registry->find(clips, transient);
        const auto transient_id = transient->getID();
        transient->setName("TransientLayer");
        CHECK(transient->release());
        CHECK(next == &second);
        CHECK(registry->find(clips, transient_id) == nullptr);
        CHECK(registry->find(clips, "TransientLayer", nullptr) == nullptr);
        auto* clone = first.clone();
        CHECK(clone->getID() != first.getID());
        CHECK(clone->getClassID() == first.getClassID());
        CHECK(registry->find(clips, long_name.c_str(), nullptr) == clone);
        CHECK(clone->release());
        CHECK(registry->find(clips, long_name.c_str(), nullptr) == &first);

        {
            srLight source;
            source.setName("CopiedLight");
            srLight copy(source);
            CHECK(copy.getID() != source.getID());
            CHECK(copy.getClassID() == 0x1220);
            CHECK(copy.getName() == source.getName());
            CHECK(registry->find(srLight::sGetClassNode(), "CopiedLight", nullptr) == &copy);
            CHECK(registry->getNumberOfInstances(runtime, 0) == baseline + 6);
        }

        // Palette reuse uses exact-type iteration rather than a name lookup.
        auto* palette = new srPalette(nullptr, 2);
        srARGB colors[2];
        std::memset(colors, 0, sizeof(colors));
        palette->setColor(0, colors[0]);
        palette->setColor(1, colors[1]);
        CHECK(srPalette::findMatchingPalette(colors, 2) == palette);
        CHECK(palette->release());
    }
    CHECK(registry->getNumberOfInstances(runtime, 0) == baseline);
    CHECK(registry->find(nodes, deleted_id) == nullptr);
    CHECK(registry->getNumberOfInstances(clips, 1) == 0);
    CHECK(registry->find(nodes, "AutomapLayer", nullptr) == nullptr);
    CHECK(srExit() == 1);
    return true;
}

int main()
{
    // Metadata is owned by each runtime lifecycle, with no stale static cache.
    if (!lookup() || !lookup()) return 1;
    std::puts("ok: runtime lookup, format identity, rename, clone and cleanup");
}
