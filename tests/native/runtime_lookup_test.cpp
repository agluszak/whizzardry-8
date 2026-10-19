#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "wiz8/render/clip_plane.h"
#include "wiz8/render/light.h"

TEST_CASE("Copied scene resources preserve owned names and unnamed state")
{
    ClipPlane first;
    const std::string long_name(2048, 'x');
    first.setName(long_name);
    auto clone = std::make_unique<ClipPlane>(first);
    first.setName("changed");
    REQUIRE(clone->getName() == long_name);

    Light source;
    source.setName("CopiedLight");
    Light copy(source);
    REQUIRE(copy.getName() == source.getName());

    ClipPlane unnamed;
    first = unnamed;
    REQUIRE(!first.isNamed());
    REQUIRE(first.getName() == "anonymous");
    ClipPlane unnamed_copy(first);
    REQUIRE(!unnamed_copy.isNamed());
}
