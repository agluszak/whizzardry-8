#include <memory>
#include "surrender/srClipPlane.h"
#include "surrender/srFog.h"
#include "surrender/srLight.h"

#include <cstdio>
#include <cstring>
#include <string>

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
        return false; \
    } \
} while (0)

static bool identity()
{
    CHECK(srInit() == 1);
    auto* registry = srCore.getRegistry();
    auto* clips = srClipPlane::sGetClassNode();
    {
        srClipPlane first;
        srFog fog;
        CHECK(first.getClassID() == 0x1500);
        CHECK(std::strcmp(first.getClassName(), "srClipPlane") == 0);
        CHECK(fog.getClassID() == 0x1210);
        CHECK(first.matchClassID(0x1000));
        CHECK(first.matchClassID(0x100));
        CHECK(!first.matchClassID(0x1210));
        CHECK(!first.matchClassID(0xffffffff));
        CHECK(registry->getClassID(clips) == 0x1500);
        CHECK(std::strcmp(registry->getClassName(clips), "srClipPlane") == 0);
        CHECK(sr_cast<srClipPlane>(static_cast<srNode*>(&first)) == &first);
        CHECK(sr_cast<srClipPlane>(static_cast<srNode*>(nullptr)) == nullptr);

        const std::string long_name(2048, 'x');
        first.setName(long_name);
        auto clone = std::make_unique<srClipPlane>(first);
        CHECK(clone->getClassID() == first.getClassID());
        CHECK(clone->getName() == long_name);

        srLight source;
        source.setName("CopiedLight");
        srLight copy(source);
        CHECK(copy.getClassID() == 0x1220);
        CHECK(copy.getName() == source.getName());
    }
    CHECK(srExit() == 1);
    return true;
}

int main()
{
    // Metadata is owned by each runtime lifecycle, with no stale static cache.
    if (!identity() || !identity()) return 1;
    std::puts("ok: runtime format identity, names and clones");
}
