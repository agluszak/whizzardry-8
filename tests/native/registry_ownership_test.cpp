#include "surrender/srTypeRegistry.h"

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
#include "surrender/srIStreamOpener.h"
#include "surrender/srImporter.h"
#include "surrender/srVideoManager.h"

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
struct Registered : srClassSupport<Registered, srClass, true, 0x7ff001> {
    static const char* sGetClassName() { return "Registry ownership fixture"; }
    srClass* vInstance() override { return new Registered; }
};

struct Inherited : srClassSupport<Inherited, Registered, false, 0x7ff002> {
    static const char* sGetClassName() { return "Inherited registry fixture"; }
    srClass* vInstance() override { return new Inherited; }
};

struct IndexedDerived : srClassSupport<IndexedDerived, Registered, true, 0x7ff003> {
    static const char* sGetClassName() { return "Indexed derived registry fixture"; }
    srClass* vInstance() override { return new IndexedDerived; }
};

struct Branch : srClassSupport<Branch, srClass, true, 0x7ff004> {
    static const char* sGetClassName() { return "Sibling registry fixture"; }
    srClass* vInstance() override { return new Branch; }
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
        CHECK(registry->getClassNode(Registered::CLASS_ID) == node);
        CHECK(registry->find(node, ids.front()) == objects.front().get());
        CHECK(registry->find(node, "duplicate", nullptr) == objects[index - index % 2].get());
    }
    CHECK(registry->getNumberOfInstances(node, 0) == before + 2048);
    CHECK(registry->getNumberOfInstances(node, 1) == before + 2048);
    const srRuntimeClass* relative = nullptr;
    for (int index = 2047; index >= 0; --index) {
        auto* found = registry->find(node, relative);
        CHECK(found == objects[index].get());
        CHECK(found->getID() == ids[index]);
        CHECK(registry->findExact(node, ids[index]) == found);
        relative = found;
    }
    CHECK(registry->find(node, relative) == nullptr);
    relative = nullptr;
    for (int index = 2046; index >= 0; index -= 2) {
        auto* found = registry->find(node, "duplicate", relative);
        CHECK(found == objects[index].get());
        relative = found;
    }
    CHECK(registry->find(node, "duplicate", relative) == nullptr);

    // Erasing interior/head/tail entries must not invalidate links to surviving instances.
    for (int index = 0; index < 2048; index += 3) {
        objects[index].reset();
        CHECK(registry->find(node, ids[index]) == nullptr);
    }
    relative = nullptr;
    for (int index = 2047; index >= 0; --index) {
        if (!objects[index]) continue;
        auto* found = registry->find(node, relative);
        CHECK(found == objects[index].get());
        relative = found;
    }
    CHECK(registry->find(node, relative) == nullptr);
    objects.clear();
    CHECK(registry->getNumberOfInstances(node, 0) == before);
    CHECK(registry->find(node, static_cast<const srRuntimeClass*>(nullptr)) == nullptr);
    CHECK(registry->find(node, "duplicate", nullptr) == nullptr);

    // Repopulate after the last removal: no stale pool head or recycled pointer may remain.
    Registered fresh;
    fresh.setName("duplicate");
    CHECK(fresh.getID() > ids.back());
    CHECK(registry->find(node, fresh.getID()) == &fresh);
    CHECK(registry->find(node, "duplicate", nullptr) == &fresh);
    CHECK(registry->find(node, &fresh) == nullptr);
}

void renaming()
{
    srRegistry* registry = srCore.getRegistry();
    auto* node = Registered::sGetClassNode();
    Registered first, second, unnamed;
    CHECK(!unnamed.isNamed() && unnamed.getName() == "anonymous");
    CHECK(registry->find(node, "anonymous", nullptr) == nullptr);
    std::string source(256, 'a');
    first.setName(source);
    source[0] = 'b';
    CHECK(first.getName()[0] == 'a');
    first.setName(first.getName()); // Setter owns its argument before changing the index/name.
    CHECK(first.getName() == std::string(256, 'a'));
    CHECK(registry->find(node, first.getName(), nullptr) == &first);
    first.setName(first.getName().c_str() + 128);
    CHECK(first.getName() == std::string(128, 'a'));
    CHECK(registry->find(node, std::string(256, 'a'), nullptr) == nullptr);

    first.setName("same");
    second.setName("same");
    CHECK(registry->find(node, "same", nullptr) == &second);
    CHECK(registry->find(node, "same", &second) == &first);
    CHECK(registry->find(node, "same", &first) == nullptr);
    CHECK(registry->find(node, "Same", nullptr) == nullptr);
    // Lookups borrow a bounded view, not a nullable or necessarily terminated C string.
    const std::string surrounded = "xsamez";
    CHECK(registry->find(node, std::string_view(surrounded).substr(1, 4), nullptr) == &second);
    CHECK(registry->find(node, "same", &unnamed) == &second);
    first.setName(first.getName());
    CHECK(registry->find(node, "same", nullptr) == &first);
    CHECK(registry->find(node, "same", &first) == &second);
    first.setName("different");
    CHECK(registry->find(node, "same", nullptr) == &second);
    CHECK(registry->find(node, "same", &first) == nullptr);
    first.setName({});
    CHECK(!first.isNamed() && first.getName() == "anonymous");
    CHECK(registry->find(node, "different", nullptr) == nullptr);
    CHECK(registry->find(node, first.getID()) == &first);

    // Assignment copies the name, not the fallback display name or the instance ID.
    const auto first_id = first.getID();
    first.srClass::operator=(second);
    CHECK(first.isNamed() && first.getName() == "same" && first.getID() == first_id);
    first.srClass::operator=(unnamed);
    CHECK(!first.isNamed() && first.getID() == first_id);
    std::ostringstream stream;
    first.getUniqueName(stream);
    CHECK(stream.str() == "anonymous[" + std::to_string(first_id) + "]");
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
        CHECK(registry->getNumberOfInstances(base, 0) == total + 3);
        CHECK(registry->getNumberOfInstances(base, 1) == total + 1);
        CHECK(registry->getNumberOfInstances(inherited, 1) == 1);
        CHECK(registry->find(base, "shared", nullptr) == &own_index);
        CHECK(registry->find(base, "shared", &own_index) == &derived);
        CHECK(registry->find(base, "shared", &derived) == &exact);
        CHECK(registry->findExact(base, "shared", nullptr) == &exact);
        CHECK(registry->find(inherited, "shared", nullptr) == &derived);
        CHECK(registry->findExact(inherited, "shared", nullptr) == &derived);
        CHECK(registry->find(inherited, own_index.getID()) == nullptr);
        CHECK(registry->findExact(base, own_index.getID()) == nullptr);
        CHECK(registry->find(indexed, own_index.getID()) == &own_index);
        CHECK(registry->find(base, static_cast<const srRuntimeClass*>(nullptr)) == &own_index);
        CHECK(registry->find(base, &own_index) == &derived);
        CHECK(registry->findExact(base, static_cast<const srRuntimeClass*>(nullptr)) == &exact);
        CHECK(registry->findExact(inherited, static_cast<const srRuntimeClass*>(nullptr)) == &derived);

        own_index.setName("renamed");
        CHECK(registry->find(base, "shared", nullptr) == &derived);
        CHECK(registry->find(indexed, "shared", nullptr) == nullptr);
        CHECK(registry->find(base, "renamed", nullptr) == &own_index);
        CHECK(registry->find(indexed, "renamed", nullptr) == &own_index);
        own_index.setName({});
        CHECK(registry->find(base, "renamed", nullptr) == nullptr);
        CHECK(registry->find(indexed, "renamed", nullptr) == nullptr);
        CHECK(registry->find(indexed, own_index.getID()) == &own_index);
    }
    CHECK(registry->getNumberOfInstances(base, 0) == total);
    CHECK(registry->getNumberOfInstances(inherited, 0) == 0);
    CHECK(registry->getNumberOfInstances(indexed, 0) == 0);
    CHECK(registry->find(base, "shared", nullptr) == nullptr);

    // A parent with no index searches child classes newest-first and continues across siblings.
    auto* parent = srClass::sGetClassNode();
    std::vector<srRuntimeClass*> existing;
    for (auto* object = registry->find(parent, static_cast<const srRuntimeClass*>(nullptr));
         object != nullptr; object = registry->find(parent, object)) {
        CHECK(existing.size() < static_cast<std::size_t>(registry->getNumberOfInstances(parent, 0)));
        existing.push_back(object);
    }
    Registered first, second;
    Branch sibling;
    first.setName("tree");
    second.setName("tree");
    sibling.setName("tree");
    CHECK(registry->find(parent, "tree", nullptr) == &sibling);
    CHECK(registry->find(parent, "tree", &sibling) == &second);
    CHECK(registry->find(parent, "tree", &second) == &first);
    CHECK(registry->find(parent, "tree", &first) == nullptr);
    CHECK(registry->find(parent, &sibling) == &second);
    CHECK(registry->find(parent, &second) == &first);
    // srInit also registers the default material, palette, surface, texture and root node.
    auto* next = registry->find(parent, &first);
    for (auto* expected : existing) {
        CHECK(next == expected);
        next = registry->find(parent, next);
    }
    CHECK(next == nullptr);
    CHECK(registry->findExact(parent, "tree", nullptr) == nullptr);
    CHECK(srClass::find("tree", static_cast<const srClass*>(nullptr)) == &sibling);
    CHECK(srClass::find("tree", Registered::CLASS_ID, nullptr) == &second);
}

void class_tree()
{
    srRegistry registry;
    CHECK(registry.checkValidity());
    CHECK(registry.getRootClass() == nullptr);
    auto* root = registry.getRootNode();
    std::vector<srRegistry::ClassNode*> nodes;
    for (w8_ulong id = 1; id <= 1024; ++id) {
        std::string name = "owned-class-" + std::to_string(id);
        nodes.push_back(registry.registerClass(name.c_str(), root, id, id % 2));
        name.assign("changed producer buffer");
        CHECK(std::string(registry.getClassName(nodes.back())) == "owned-class-" + std::to_string(id));
        CHECK(registry.getClassNode(1) == nodes.front());
        CHECK(registry.registerClass("ignored duplicate ID", root, id, 0) == nodes.back());
        CHECK(registry.getRootClass() == nodes.back());
    }
    auto* child = registry.getChildClass(root, nullptr);
    for (auto expected = nodes.rbegin(); expected != nodes.rend(); ++expected) {
        CHECK(child == *expected);
        child = registry.getChildClass(root, child);
    }
    CHECK(child == nullptr);
    CHECK(registry.getClassNode(0) == nullptr);
    CHECK(registry.getChildClass(nodes.front(), nullptr) == nullptr);
    CHECK(registry.getChildClass(nodes.front(), nodes.back()) == nullptr);
    // Ownership is flat, so teardown does not recursively delete a potentially deep class tree.
    auto* parent = nodes.front();
    for (w8_ulong id = 2048; id < 4096; ++id) {
        parent = registry.registerClass("deep descendant", parent, id, 0);
    }
    CHECK(registry.isDerivedOrSame(nodes.front(), parent));
}

void intrusive_references()
{
    auto* registry = srCore.getRegistry();
    auto* node = Registered::sGetClassNode();
    const auto before = registry->getNumberOfInstances(node, 0);
    auto* object = new Registered;
    object->setName("reference lifetime");
    const auto id = object->getID();
    CHECK(object->getReferenceCount() == 1);
    object->addReference();
    CHECK(object->getReferenceCount() == 2);
    CHECK(!object->release());
    CHECK(registry->find(node, id) == object);
    CHECK(object->release());
    CHECK(registry->find(node, id) == nullptr);
    CHECK(registry->find(node, "reference lifetime", nullptr) == nullptr);
    CHECK(registry->getNumberOfInstances(node, 0) == before);
    object = new Registered;
    object->autoRelease();
    CHECK(object->getReferenceCount() == 0);
    CHECK(object->release());
    CHECK(registry->getNumberOfInstances(node, 0) == before);
}
} // namespace

int main()
{
    class_tree();
    w8_ulong previous_id = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        CHECK(srInit());
        CHECK(srCore.getRegistry()->allocateID() > previous_id);
        growth();
        renaming();
        inherited_and_relative();
        intrusive_references();
        previous_id = srCore.getRegistry()->allocateID();
        CHECK(srExit());
    }
    std::puts("ok: registry growth, owned/aliased names, duplicate and relative lookup, teardown");
}
