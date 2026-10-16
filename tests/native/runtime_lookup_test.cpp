#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "surrender/srClipPlane.h"
#include "surrender/srLight.h"

TEST_CASE("Copied scene resources preserve owned names and unnamed state")
{
    srClipPlane first;
    const std::string long_name(2048, 'x');
    first.setName(long_name);
    auto clone = std::make_unique<srClipPlane>(first);
    first.setName("changed");
    REQUIRE(clone->getName() == long_name);

    srLight source;
    source.setName("CopiedLight");
    srLight copy(source);
    REQUIRE(copy.getName() == source.getName());

    srClipPlane unnamed;
    first = unnamed;
    REQUIRE(!first.isNamed());
    REQUIRE(first.getName() == "anonymous");
    srClipPlane unnamed_copy(first);
    REQUIRE(!unnamed_copy.isNamed());
}
