#include <catch2/catch_test_macros.hpp>
#include "surrender/srTypeRegistry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

// The focused fixture can link just type_registry.cpp while the rest of SurRender is changing.
// Without this define, it uses the normal library initialization and shutdown.
#ifdef WIZ8_REGISTRY_STANDALONE_TEST
#include "surrender/srDebug.h"

class srCore srCore;
srCore::srCore() = default;
srCore::~srCore() = default;
int __cdecl srInit()
{
    srCore.registry_ = std::make_unique<srRegistry>();
    return 1;
}
int __cdecl srExit()
{
    srCore.registry_.reset();
    return 1;
}
w8_long __cdecl srDebugPrintf(w8_ulong, const char*, ...) { return 0; }
void __cdecl srAssertFail(const char* expression, const char* path, w8_long line, const char*, ...)
{
    std::fprintf(stderr, "%s:%d: %s\n", path, line, expression);
    std::abort();
}
#endif


namespace {
struct Registered : srClassSupport<Registered, srClass, 0x7ff001> {
    static const char* sGetClassName() { return "Registry ownership fixture"; }
};

void names()
{
    Registered first, second, unnamed;
    REQUIRE((!unnamed.isNamed() && unnamed.getName() == "anonymous"));
    std::string source(256, 'a');
    first.setName(source);
    source[0] = 'b';
    REQUIRE((first.getName() == std::string(256, 'a')));
    first.setName(first.getName()); // Setter owns its argument before replacing the name.
    REQUIRE((first.getName() == std::string(256, 'a')));
    first.setName({});
    REQUIRE((!first.isNamed() && first.getName() == "anonymous"));

    // Assignment copies the name, not the fallback display name.
    second.setName("same");
    first.srClass::operator=(second);
    REQUIRE((first.isNamed() && first.getName() == "same"));
    first.srClass::operator=(unnamed);
    REQUIRE((!first.isNamed()));
    Registered copy(second);
    REQUIRE((copy.getName() == "same" && copy.getClassID() == Registered::CLASS_ID));
}

void class_tree()
{
    srRegistry registry;
    REQUIRE((registry.getRootNode()->children.empty()));
    auto* root = registry.getRootNode();
    std::vector<srRegistry::ClassNode*> nodes;
    for (w8_ulong id = 1; id <= 1024; ++id) {
        std::string name = "owned-class-" + std::to_string(id);
        nodes.push_back(registry.registerClass(name.c_str(), root, id));
        name.assign("changed producer buffer");
        REQUIRE((std::string(registry.getClassName(nodes.back())) == "owned-class-" + std::to_string(id)));
        REQUIRE((registry.getClassNode(1) == nodes.front()));
        REQUIRE((registry.registerClass("ignored duplicate ID", root, id) == nodes.back()));
        REQUIRE((root->children.front() == nodes.back()));
    }
    REQUIRE((root->children.size() == nodes.size()));
    REQUIRE((std::equal(root->children.begin(), root->children.end(), nodes.rbegin())));
    REQUIRE((registry.getClassNode(0) == nullptr));
    REQUIRE((nodes.front()->children.empty()));
    // Ownership is flat, so teardown does not recursively delete a potentially deep class tree.
    auto* parent = nodes.front();
    for (w8_ulong id = 2048; id < 4096; ++id) {
        parent = registry.registerClass("deep descendant", parent, id);
    }
    REQUIRE((registry.isDerivedOrSame(nodes.front(), parent)));
}

} // namespace

TEST_CASE("registry_ownership_test")
{
    class_tree();
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE((srInit()));
        REQUIRE((srCore.getRegistry()->getClassNode(Registered::CLASS_ID) == nullptr));
        names();
        REQUIRE((srCore.getRegistry()->getClassNode(Registered::CLASS_ID) == Registered::sGetClassNode()));
        REQUIRE((srExit()));
    }
    std::puts("ok: registry class tree, owned names, teardown");
}
