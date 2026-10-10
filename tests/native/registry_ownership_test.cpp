#include <catch2/catch_test_macros.hpp>
#include "surrender/srTypeRegistry.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
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

struct Inherited : srClassSupport<Inherited, Registered, 0x7ff002> {
    static const char* sGetClassName() { return "Inherited registry fixture"; }
};

struct IndexedDerived : srClassSupport<IndexedDerived, Registered, 0x7ff003> {
    static const char* sGetClassName() { return "Indexed derived registry fixture"; }
};

struct Branch : srClassSupport<Branch, srClass, 0x7ff004> {
    static const char* sGetClassName() { return "Sibling registry fixture"; }
};

void growth()
{
    srRegistry* registry = srCore.getRegistry();
    auto* node = Registered::sGetClassNode();
    const auto before = registry->getNumberOfInstances(node, 0);
    std::vector<std::unique_ptr<Registered>> objects;
    std::vector<w8_ulong> ids;
    for (int index = 0; index < 2048; ++index) {
        auto object = std::make_unique<Registered>();
        object->setName(index % 2 == 0 ? "duplicate" : "unique-" + std::to_string(index));
        ids.push_back(object->getID());
        objects.push_back(std::move(object));
        // Index growth must not move ClassNodes or reverse existing duplicate order.
        REQUIRE(registry->getClassNode(Registered::CLASS_ID) == node);
        REQUIRE(registry->find(node, ids.front()) == objects.front().get());
        REQUIRE(registry->find(node, "duplicate", nullptr) == objects[index - index % 2].get());
    }
    REQUIRE(registry->getNumberOfInstances(node, 0) == before + 2048);
    REQUIRE(registry->getNumberOfInstances(node, 1) == before + 2048);
    const srRuntimeClass* relative = nullptr;
    for (int index = 2047; index >= 0; --index) {
        auto* found = registry->find(node, relative);
        REQUIRE(found == objects[index].get());
        REQUIRE(found->getID() == ids[index]);
        REQUIRE(registry->findExact(node, ids[index]) == found);
        relative = found;
    }
    REQUIRE(registry->find(node, relative) == nullptr);
    relative = nullptr;
    for (int index = 2046; index >= 0; index -= 2) {
        auto* found = registry->find(node, "duplicate", relative);
        REQUIRE(found == objects[index].get());
        relative = found;
    }
    REQUIRE(registry->find(node, "duplicate", relative) == nullptr);

    // Erasing interior/head/tail entries must not invalidate links to surviving instances.
    for (int index = 0; index < 2048; index += 3) {
        objects[index].reset();
        REQUIRE(registry->find(node, ids[index]) == nullptr);
    }
    relative = nullptr;
    for (int index = 2047; index >= 0; --index) {
        if (!objects[index]) continue;
        auto* found = registry->find(node, relative);
        REQUIRE(found == objects[index].get());
        relative = found;
    }
    REQUIRE(registry->find(node, relative) == nullptr);
    objects.clear();
    REQUIRE(registry->getNumberOfInstances(node, 0) == before);
    REQUIRE(registry->find(node, static_cast<const srRuntimeClass*>(nullptr)) == nullptr);
    REQUIRE(registry->find(node, "duplicate", nullptr) == nullptr);

    // Repopulate after the last removal: no stale pool head or recycled pointer may remain.
    Registered fresh;
    fresh.setName("duplicate");
    REQUIRE(fresh.getID() > ids.back());
    REQUIRE(registry->find(node, fresh.getID()) == &fresh);
    REQUIRE(registry->find(node, "duplicate", nullptr) == &fresh);
    REQUIRE(registry->find(node, &fresh) == nullptr);
}

void renaming()
{
    srRegistry* registry = srCore.getRegistry();
    auto* node = Registered::sGetClassNode();
    Registered first, second, unnamed;
    REQUIRE((!unnamed.isNamed() && unnamed.getName() == "anonymous"));
    REQUIRE(registry->find(node, "anonymous", nullptr) == nullptr);
    std::string source(256, 'a');
    first.setName(source);
    source[0] = 'b';
    REQUIRE(first.getName()[0] == 'a');
    first.setName(first.getName()); // Setter owns its argument before changing the index/name.
    REQUIRE(first.getName() == std::string(256, 'a'));
    REQUIRE(registry->find(node, first.getName(), nullptr) == &first);
    first.setName(first.getName().c_str() + 128);
    REQUIRE(first.getName() == std::string(128, 'a'));
    REQUIRE(registry->find(node, std::string(256, 'a'), nullptr) == nullptr);

    first.setName("same");
    second.setName("same");
    REQUIRE(registry->find(node, "same", nullptr) == &second);
    REQUIRE(registry->find(node, "same", &second) == &first);
    REQUIRE(registry->find(node, "same", &first) == nullptr);
    REQUIRE(registry->find(node, "Same", nullptr) == nullptr);
    // Lookups borrow a bounded view, not a nullable or necessarily terminated C string.
    const std::string surrounded = "xsamez";
    REQUIRE(registry->find(node, std::string_view(surrounded).substr(1, 4), nullptr) == &second);
    REQUIRE(registry->find(node, "same", &unnamed) == &second);
    first.setName(first.getName());
    REQUIRE(registry->find(node, "same", nullptr) == &first);
    REQUIRE(registry->find(node, "same", &first) == &second);
    first.setName("different");
    REQUIRE(registry->find(node, "same", nullptr) == &second);
    REQUIRE(registry->find(node, "same", &first) == nullptr);
    first.setName({});
    REQUIRE((!first.isNamed() && first.getName() == "anonymous"));
    REQUIRE(registry->find(node, "different", nullptr) == nullptr);
    REQUIRE(registry->find(node, first.getID()) == &first);

    // Assignment copies the name, not the fallback display name or the instance ID.
    const auto first_id = first.getID();
    first.srClass::operator=(second);
    REQUIRE((first.isNamed() && first.getName() == "same" && first.getID() == first_id));
    first.srClass::operator=(unnamed);
    REQUIRE((!first.isNamed() && first.getID() == first_id));
    std::ostringstream stream;
    first.getUniqueName(stream);
    REQUIRE(stream.str() == "anonymous[" + std::to_string(first_id) + "]");
}

void inherited_and_relative()
{
    srRegistry* registry = srCore.getRegistry();
    auto* base = Registered::sGetClassNode();
    auto* inherited = Inherited::sGetClassNode();
    auto* indexed = IndexedDerived::sGetClassNode();
    const auto total = registry->getNumberOfInstances(base, 0);
    {
        Registered exact;
        Inherited derived;
        IndexedDerived own_index;
        exact.setName("shared");
        derived.setName("shared");
        own_index.setName("shared");
        REQUIRE(registry->getNumberOfInstances(base, 0) == total + 3);
        REQUIRE(registry->getNumberOfInstances(base, 1) == total + 1);
        REQUIRE(registry->getNumberOfInstances(inherited, 1) == 1);
        REQUIRE(registry->find(base, "shared", nullptr) == &own_index);
        REQUIRE(registry->find(base, "shared", &own_index) == &derived);
        REQUIRE(registry->find(base, "shared", &derived) == &exact);
        REQUIRE(registry->findExact(base, "shared", nullptr) == &exact);
        REQUIRE(registry->find(inherited, "shared", nullptr) == &derived);
        REQUIRE(registry->findExact(inherited, "shared", nullptr) == &derived);
        REQUIRE(registry->find(inherited, own_index.getID()) == nullptr);
        REQUIRE(registry->findExact(base, own_index.getID()) == nullptr);
        REQUIRE(registry->find(indexed, own_index.getID()) == &own_index);
        REQUIRE(registry->find(base, static_cast<const srRuntimeClass*>(nullptr)) == &own_index);
        REQUIRE(registry->find(base, &own_index) == &derived);
        REQUIRE(registry->findExact(base, static_cast<const srRuntimeClass*>(nullptr)) == &exact);
        REQUIRE(registry->findExact(inherited, static_cast<const srRuntimeClass*>(nullptr)) == &derived);

        own_index.setName("renamed");
        REQUIRE(registry->find(base, "shared", nullptr) == &derived);
        REQUIRE(registry->find(indexed, "shared", nullptr) == nullptr);
        REQUIRE(registry->find(base, "renamed", nullptr) == &own_index);
        REQUIRE(registry->find(indexed, "renamed", nullptr) == &own_index);
        own_index.setName({});
        REQUIRE(registry->find(base, "renamed", nullptr) == nullptr);
        REQUIRE(registry->find(indexed, "renamed", nullptr) == nullptr);
        REQUIRE(registry->find(indexed, own_index.getID()) == &own_index);
    }
    REQUIRE(registry->getNumberOfInstances(base, 0) == total);
    REQUIRE(registry->getNumberOfInstances(inherited, 0) == 0);
    REQUIRE(registry->getNumberOfInstances(indexed, 0) == 0);
    REQUIRE(registry->find(base, "shared", nullptr) == nullptr);

    // Iteration visits newest objects first and continues across sibling classes.
    auto* parent = srClass::sGetClassNode();
    std::vector<srRuntimeClass*> existing;
    for (auto* object = registry->find(parent, static_cast<const srRuntimeClass*>(nullptr));
         object != nullptr; object = registry->find(parent, object)) {
        REQUIRE(existing.size() < static_cast<std::size_t>(registry->getNumberOfInstances(parent, 0)));
        existing.push_back(object);
    }
    Registered first, second;
    Branch sibling;
    first.setName("tree");
    second.setName("tree");
    sibling.setName("tree");
    REQUIRE(registry->find(parent, "tree", nullptr) == &sibling);
    REQUIRE(registry->find(parent, "tree", &sibling) == &second);
    REQUIRE(registry->find(parent, "tree", &second) == &first);
    REQUIRE(registry->find(parent, "tree", &first) == nullptr);
    REQUIRE(registry->find(parent, &sibling) == &second);
    REQUIRE(registry->find(parent, &second) == &first);
    // srInit also registers the default material, palette, surface, texture and root node.
    auto* next = registry->find(parent, &first);
    for (auto* expected : existing) {
        REQUIRE(next == expected);
        next = registry->find(parent, next);
    }
    REQUIRE(next == nullptr);
    REQUIRE(registry->findExact(parent, "tree", nullptr) == nullptr);
    REQUIRE(srClass::find("tree", static_cast<const srClass*>(nullptr)) == &sibling);
    REQUIRE(srClass::find("tree", Registered::CLASS_ID, nullptr) == &second);
}

void class_tree()
{
    srRegistry registry;
    REQUIRE(registry.getRootNode()->children.empty());
    auto* root = registry.getRootNode();
    std::vector<srRegistry::ClassNode*> nodes;
    for (w8_ulong id = 1; id <= 1024; ++id) {
        std::string name = "owned-class-" + std::to_string(id);
        nodes.push_back(registry.registerClass(name.c_str(), root, id));
        name.assign("changed producer buffer");
        REQUIRE(std::string(registry.getClassName(nodes.back())) == "owned-class-" + std::to_string(id));
        REQUIRE(registry.getClassNode(1) == nodes.front());
        REQUIRE(registry.registerClass("ignored duplicate ID", root, id) == nodes.back());
        REQUIRE(root->children.front() == nodes.back());
    }
    REQUIRE(root->children.size() == nodes.size());
    REQUIRE(std::equal(root->children.begin(), root->children.end(), nodes.rbegin()));
    REQUIRE(registry.getClassNode(0) == nullptr);
    REQUIRE(nodes.front()->children.empty());
    // Ownership is flat, so teardown does not recursively delete a potentially deep class tree.
    auto* parent = nodes.front();
    for (w8_ulong id = 2048; id < 4096; ++id) {
        parent = registry.registerClass("deep descendant", parent, id);
    }
    REQUIRE(registry.isDerivedOrSame(nodes.front(), parent));
}

} // namespace

TEST_CASE("registry_ownership_test")
{
    class_tree();
    w8_ulong previous_id = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(srInit());
        REQUIRE(srCore.getRegistry()->allocateID() > previous_id);
        growth();
        renaming();
        inherited_and_relative();
        previous_id = srCore.getRegistry()->allocateID();
        REQUIRE(srExit());
    }
    std::puts("ok: registry growth, owned/aliased names, duplicate and relative lookup, teardown");

}
