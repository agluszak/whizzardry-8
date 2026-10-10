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

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                            \
            std::abort();                                                                          \
        }                                                                                          \
    } while (0)

namespace {
struct Registered : srClassSupport<Registered, srClass, 0x7ff001> {
    static const char* sGetClassName() { return "Registry ownership fixture"; }
};

void names()
{
    Registered first, second, unnamed;
    CHECK(!unnamed.isNamed() && unnamed.getName() == "anonymous");
    std::string source(256, 'a');
    first.setName(source);
    source[0] = 'b';
    CHECK(first.getName() == std::string(256, 'a'));
    first.setName(first.getName()); // Setter owns its argument before replacing the name.
    CHECK(first.getName() == std::string(256, 'a'));
    first.setName({});
    CHECK(!first.isNamed() && first.getName() == "anonymous");

    // Assignment copies the name, not the fallback display name.
    second.setName("same");
    first.srClass::operator=(second);
    CHECK(first.isNamed() && first.getName() == "same");
    first.srClass::operator=(unnamed);
    CHECK(!first.isNamed());
    Registered copy(second);
    CHECK(copy.getName() == "same" && copy.getClassID() == Registered::CLASS_ID);
}

void class_tree()
{
    srRegistry registry;
    CHECK(registry.getRootNode()->children.empty());
    auto* root = registry.getRootNode();
    std::vector<srRegistry::ClassNode*> nodes;
    for (w8_ulong id = 1; id <= 1024; ++id) {
        std::string name = "owned-class-" + std::to_string(id);
        nodes.push_back(registry.registerClass(name.c_str(), root, id));
        name.assign("changed producer buffer");
        CHECK(std::string(registry.getClassName(nodes.back())) == "owned-class-" + std::to_string(id));
        CHECK(registry.getClassNode(1) == nodes.front());
        CHECK(registry.registerClass("ignored duplicate ID", root, id) == nodes.back());
        CHECK(root->children.front() == nodes.back());
    }
    CHECK(root->children.size() == nodes.size());
    CHECK(std::equal(root->children.begin(), root->children.end(), nodes.rbegin()));
    CHECK(registry.getClassNode(0) == nullptr);
    CHECK(nodes.front()->children.empty());
    // Ownership is flat, so teardown does not recursively delete a potentially deep class tree.
    auto* parent = nodes.front();
    for (w8_ulong id = 2048; id < 4096; ++id) {
        parent = registry.registerClass("deep descendant", parent, id);
    }
    CHECK(registry.isDerivedOrSame(nodes.front(), parent));
}

} // namespace

int main()
{
    class_tree();
    for (int cycle = 0; cycle < 3; ++cycle) {
        CHECK(srInit());
        CHECK(srCore.getRegistry()->getClassNode(Registered::CLASS_ID) == nullptr);
        names();
        CHECK(srCore.getRegistry()->getClassNode(Registered::CLASS_ID) == Registered::sGetClassNode());
        CHECK(srExit());
    }
    std::puts("ok: registry class tree, owned names, teardown");
}
