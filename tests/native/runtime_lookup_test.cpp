#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "surrender/srClipPlane.h"
#include "surrender/srCore.h"
#include "surrender/srFog.h"
#include "surrender/srLight.h"
#include "surrender/srTypeRegistry.h"

class srNode;

static bool identity()
{
    REQUIRE((srInit() == 1));
    auto* registry = srCore.getRegistry();
    auto* clips = srClipPlane::sGetClassNode();
    {
        srClipPlane first;
        srFog fog;
        REQUIRE((first.getClassID() == 0x1500));
        REQUIRE((std::strcmp(first.getClassName(), "srClipPlane") == 0));
        REQUIRE((fog.getClassID() == 0x1210));
        REQUIRE((first.matchClassID(0x1000)));
        REQUIRE((first.matchClassID(0x100)));
        REQUIRE((!first.matchClassID(0x1210)));
        REQUIRE((!first.matchClassID(0xffffffff)));
        REQUIRE((registry->getClassID(clips) == 0x1500));
        REQUIRE((std::strcmp(registry->getClassName(clips), "srClipPlane") == 0));
        REQUIRE((sr_cast<srClipPlane>(static_cast<srNode*>(&first)) == &first));
        REQUIRE((sr_cast<srClipPlane>(static_cast<srNode*>(nullptr)) == nullptr));

        const std::string long_name(2048, 'x');
        first.setName(long_name);
        auto clone = std::make_unique<srClipPlane>(first);
        REQUIRE((clone->getClassID() == first.getClassID()));
        REQUIRE((clone->getName() == long_name));

        srLight source;
        source.setName("CopiedLight");
        srLight copy(source);
        REQUIRE((copy.getClassID() == 0x1220));
        REQUIRE((copy.getName() == source.getName()));
    }
    REQUIRE((srExit() == 1));
    return true;
}

TEST_CASE("runtime_lookup_test")
{
    // Metadata is owned by each runtime lifecycle, with no stale static cache.
    REQUIRE(identity());
    REQUIRE(identity());
    std::puts("ok: runtime format identity, names and clones");
}
