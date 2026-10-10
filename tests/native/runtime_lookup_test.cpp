#include <catch2/catch_test_macros.hpp>
#include <memory>
#include "surrender/srClipPlane.h"
#include "surrender/srFog.h"
#include "surrender/srLight.h"
#include "surrender/srMaterial.h"
#include "surrender/srPalette.h"
#include "surrender/srScene.h"

#include <cstdio>
#include <cstring>
#include <string>


static bool lookup()
{
    REQUIRE(srInit() == 1);
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
        REQUIRE(registry->getNumberOfInstances(runtime, 0) == baseline + 4);
        REQUIRE(registry->getNumberOfInstances(clips, 1) == 2);
        REQUIRE(first.getClassID() == 0x1500);
        REQUIRE(std::strcmp(first.getClassName(), "srClipPlane") == 0);
        REQUIRE(fog.getClassID() == 0x1210);
        REQUIRE(first.matchClassID(0x1000));
        REQUIRE(first.matchClassID(0x100));
        REQUIRE(!first.matchClassID(0x1210));
        REQUIRE(!first.matchClassID(0xffffffff));
        REQUIRE(registry->getClassID(clips) == 0x1500);
        REQUIRE(std::strcmp(registry->getClassName(clips), "srClipPlane") == 0);

        first.setName("AutomapLayer");
        second.setName("AutomapLayer");
        scene.setName("AutomapLayer");
        REQUIRE(registry->find(clips, "AutomapLayer", nullptr) == &second);
        REQUIRE(registry->find(clips, "AutomapLayer", &second) == &first);
        REQUIRE(registry->find(clips, "AutomapLayer", &first) == nullptr);
        REQUIRE(registry->find(nodes, "AutomapLayer", nullptr) == &scene);
        REQUIRE(registry->findExact(nodes, "AutomapLayer", nullptr) == nullptr);
        REQUIRE(registry->findExact(clips, "AutomapLayer", nullptr) == &second);
        REQUIRE(registry->find(clips, "automaplayer", nullptr) == nullptr);
        REQUIRE(registry->find(clips, std::string_view{}, nullptr) == nullptr);
        REQUIRE(registry->find(nullptr, "AutomapLayer", nullptr) == nullptr);

        second.setName("RenamedLayer");
        REQUIRE(registry->find(clips, "AutomapLayer", nullptr) == &first);
        REQUIRE(registry->find(clips, "RenamedLayer", nullptr) == &second);
        second.setName(second.getName());
        REQUIRE(registry->find(clips, "RenamedLayer", nullptr) == &second);
        second.setName("");
        REQUIRE(!second.isNamed());
        REQUIRE(registry->find(clips, "RenamedLayer", nullptr) == nullptr);
        first.setName({});
        REQUIRE(registry->find(clips, "AutomapLayer", nullptr) == nullptr);
        REQUIRE(registry->find(nodes, "AutomapLayer", nullptr) == &scene);
        const std::string long_name(2048, 'x');
        first.setName(long_name.c_str());
        REQUIRE(registry->find(clips, long_name.c_str(), nullptr) == &first);

        deleted_id = first.getID();
        REQUIRE(deleted_id != second.getID());
        REQUIRE(registry->find(nodes, deleted_id) == &first);
        REQUIRE(registry->findExact(nodes, deleted_id) == nullptr);
        REQUIRE(registry->findExact(clips, deleted_id) == &first);
        REQUIRE(registry->find(clips, w8_ulong(0)) == nullptr);
        REQUIRE(registry->find(clips, static_cast<const srRuntimeClass*>(nullptr)) == &second);
        REQUIRE(registry->find(clips, &second) == &first);
        REQUIRE(registry->find(clips, &first) == nullptr);
        // Level cleanup saves the next object before releasing the current object.
        auto* transient = new srClipPlane;
        auto* next = registry->find(clips, transient);
        const auto transient_id = transient->getID();
        transient->setName("TransientLayer");
        delete transient;
        REQUIRE(next == &second);
        REQUIRE(registry->find(clips, transient_id) == nullptr);
        REQUIRE(registry->find(clips, "TransientLayer", nullptr) == nullptr);
        auto clone = std::make_unique<srClipPlane>(first);
        REQUIRE(clone->getID() != first.getID());
        REQUIRE(clone->getClassID() == first.getClassID());
        REQUIRE(registry->find(clips, long_name.c_str(), nullptr) == clone.get());
        clone.reset();
        REQUIRE(registry->find(clips, long_name.c_str(), nullptr) == &first);

        {
            srLight source;
            source.setName("CopiedLight");
            srLight copy(source);
            REQUIRE(copy.getID() != source.getID());
            REQUIRE(copy.getClassID() == 0x1220);
            REQUIRE(copy.getName() == source.getName());
            REQUIRE(registry->find(srLight::sGetClassNode(), "CopiedLight", nullptr) == &copy);
            REQUIRE(registry->getNumberOfInstances(runtime, 0) == baseline + 6);
        }

    }
    REQUIRE(registry->getNumberOfInstances(runtime, 0) == baseline);
    REQUIRE(registry->find(nodes, deleted_id) == nullptr);
    REQUIRE(registry->getNumberOfInstances(clips, 1) == 0);
    REQUIRE(registry->find(nodes, "AutomapLayer", nullptr) == nullptr);
    REQUIRE(srExit() == 1);
    return true;
}

TEST_CASE("runtime_lookup_test")
{
    REQUIRE(lookup());
    REQUIRE(lookup());
}
