#include "surrender/srTypeRegistry.h"
#include "surrender/srDebug.h"

#include <algorithm>
#include <atomic>
#include <utility>
#include <ostream>
#include <string.h>

#define SRRUNTIMECLASS_CPP "srRuntimeClass.cpp"
#define SRCLASS_CPP "srClass.cpp"

namespace {
std::atomic<w8_ulong> next_instance_id{1};
}

// GLOBAL: SURRENDER 0x100A45AC
w8_ulong srClass::_timestampCtr;

// FUNCTION: SURRENDER 0x10011790
const std::string& srRuntimeClass::getName() const
{
    static const std::string anonymous = "anonymous";
    return name.empty() ? anonymous : name;
}

// FUNCTION: SURRENDER 0x100117A0
void srRuntimeClass::verify(e_verify)
{
    if (getID() == 0) {
        srAssertFail("getID()", SRRUNTIMECLASS_CPP, 0xb1, 0);
    }
    if (getClassName() == 0) {
        srAssertFail("getClassName()", SRRUNTIMECLASS_CPP, 0xb2, 0);
    }
    if (getClassID() == 0) {
        srAssertFail("getClassID()", SRRUNTIMECLASS_CPP, 0xb3, 0);
    }
    if (getClassNode() == 0) {
        srAssertFail("getClassNode()", SRRUNTIMECLASS_CPP, 0xb4, 0);
    }
    if (matchClassID(getClassID()) == 0) {
        srAssertFail("matchClassID(getClassID())", SRRUNTIMECLASS_CPP, 0xb5, 0);
    }
}

// FUNCTION: SURRENDER 0x100118D0
int srRuntimeClass::matchClassID(w8_ulong class_id) const
{
    srRegistry* registry = srCore.getRegistry();
    srRegistry::ClassNode* instance_class = getClassNode();
    srRegistry::ClassNode* requested_class = registry->getClassNode(class_id);
    return registry->isDerivedOrSame(requested_class, instance_class);
}

// FUNCTION: SURRENDER 0x10011900
int srRuntimeClass::isNamed() const
{
    return !name.empty();
}

// FUNCTION: SURRENDER 0x10011910
w8_long srRuntimeClass::getTotalInstances(int exact)
{
    return srCore.getRegistry()->getNumberOfInstances(sGetClassNode(), exact);
}

// FUNCTION: SURRENDER 0x10011930
void srRuntimeClass::dumpNames(std::ostream& stream, int indent)
{
    srCore.getRegistry()->dumpInstanceNames(sGetClassNode(), stream, indent);
}

// FUNCTION: SURRENDER 0x10011880
void srRuntimeClass::getUniqueName(std::ostream& stream) const
{
    stream << getName() << '[' << getID() << ']';
}

// FUNCTION: SURRENDER 0x10011950
void srRuntimeClass::setName(std::string name)
{
    srCore.getRegistry()->renameInstance(this, std::move(name));
}

// FUNCTION: SURRENDER 0x100119D0
srRuntimeClass::srRuntimeClass()
{
    srRegistry* registry = srCore.getRegistry();
    id = registry->allocateID();
    registry->registerInstance(this);
}

// FUNCTION: SURRENDER 0x10011A40
srRuntimeClass::~srRuntimeClass()
{
    srCore.getRegistry()->unregisterInstance(this);
}

// FUNCTION: SURRENDER 0x10011AB0
void srRuntimeClass::dump(std::ostream& stream)
{
    std::ios::fmtflags flags = stream.flags();
    stream.setf(std::ios::left, std::ios::adjustfield);
    stream.width(0x20);
    // c-style-cast-ok: the id prints as a pointer.
    stream << "Class Id: 0x" << std::hex << getClassID() << std::dec << '\n';
    stream.width(0x20);
    stream << "Class name: " << getClassName() << '\n';
    stream.width(0x20);
    // c-style-cast-ok: the id prints as a pointer.
    stream << "Instance Id code: 0x" << std::hex << getID() << std::dec << '\n';
    stream.width(0x20);
    stream << "Instance name: " << getName() << '\n';
    stream.width(0x20);
    stream << "Memory address: " << this << '\n';
    stream.flags(flags);
}

// FUNCTION: SURRENDER 0x10011CB0
w8_ulong srRuntimeClass::getID() const
{
    return id;
}

// FUNCTION: SURRENDER 0x10011C50
srRegistry::ClassNode* srRuntimeClass::sGetClassNode()
{
    srRegistry* registry = srCore.getRegistry();
    srRegistry::ClassNode* node = registry->getClassNode(1);
    if (node == 0) {
        node = registry->registerClass("srRuntimeClass", registry->getRootNode(), 1);
    }
    return node;
}

// FUNCTION: SURRENDER 0x10011C90
w8_ulong srRuntimeClass::sGetClassID()
{
    srRegistry* registry = srCore.getRegistry();
    return registry->getClassID(sGetClassNode());
}

// FUNCTION: SURRENDER 0x10011CC0
const char* srRuntimeClass::getClassName() const
{
    return "srRuntimeClass";
}

// FUNCTION: SURRENDER 0x10011CD0
w8_ulong srRuntimeClass::getClassID() const
{
    return sGetClassID();
}

// FUNCTION: SURRENDER 0x10011CE0
srRegistry::ClassNode* srRuntimeClass::getClassNode() const
{
    return sGetClassNode();
}

// FUNCTION: SURRENDER 0x1000E050
void srClass::verify(srRuntimeClass::e_verify mode)
{
    srRuntimeClass::verify(mode);
    if (reference_count < 0) {
        srAssertFail("_refCount >= 0", SRCLASS_CPP, 0x3f, 0);
    }
}

// FUNCTION: SURRENDER 0x1000E080
srClass* srClass::find(std::string_view name, const srClass* relative_to)
{
    return static_cast<srClass*>(srCore.getRegistry()->find(sGetClassNode(), name, relative_to));
}

// FUNCTION: SURRENDER 0x1000E0A0
srClass* srClass::find(std::string_view name, w8_ulong class_id, const srRuntimeClass* relative_to)
{
    srRegistry* registry = srCore.getRegistry();
    return static_cast<srClass*>(
        registry->find(registry->getClassNode(class_id), name, relative_to));
}

// FUNCTION: SURRENDER 0x1000E0D0
srClass* srClass::find(w8_ulong id)
{
    return static_cast<srClass*>(srCore.getRegistry()->find(sGetClassNode(), id));
}

// FUNCTION: SURRENDER 0x1000E0F0
srClass* srClass::find(const srClass* relative_to)
{
    return static_cast<srClass*>(srCore.getRegistry()->find(sGetClassNode(), relative_to));
}

/* Assigns only the instance name; the reference count, timestamp and update link are left alone. */
// FUNCTION: SURRENDER 0x1000E110
srClass& srClass::operator=(const srClass& other)
{
    if (this != &other) {
        setName(other.isNamed() ? other.getName() : std::string{});
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1000E130
srClass::srClass() : reference_count(1)
{
    sGetClassNode();
    touch();
}

// FUNCTION: SURRENDER 0x1000E1A0
srClass::~srClass()
{
}

// FUNCTION: SURRENDER 0x1000E200
srRegistry::ClassNode* srClass::sGetClassNode()
{
    srRegistry* registry = srCore.getRegistry();
    srRegistry::ClassNode* node = registry->getClassNode(0x100);
    if (node == 0) {
        node = registry->registerClass(sGetClassName(), srRuntimeClass::sGetClassNode(), 0x100);
    }
    return node;
}

// FUNCTION: SURRENDER 0x1000E5E0
void srClass::touch()
{
    timestamp = ++_timestampCtr;
}

// FUNCTION: SURRENDER 0x1000E5F0
w8_ulong srClass::getTimestamp() const
{
    return timestamp;
}

// FUNCTION: SURRENDER 0x1000E600
w8_ulong srClass::allocateTimeStamps(w8_ulong count) const
{
    w8_ulong first = _timestampCtr;
    _timestampCtr += count;
    return first + 1;
}

// FUNCTION: SURRENDER 0x1000E620
void srClass::dump(std::ostream& stream)
{
    srRuntimeClass::dump(stream);
    std::ios::fmtflags flags = stream.flags();
    stream.setf(std::ios::left, std::ios::adjustfield);
    stream.width(0x20);
    stream << "  Timestamp: " << getTimestamp() << '\n';
    stream.width(0x20);
    stream << "  Reference count: " << getReferenceCount() << '\n';
    stream.flags(flags);
}

// FUNCTION: SURRENDER 0x1000E850
srClass* srClass::instance()
{
    return vInstance();
}

// FUNCTION: SURRENDER 0x1000E870
const char* srClass::sGetClassName()
{
    return "srClass";
}

// FUNCTION: SURRENDER 0x1000E880
srRegistry::ClassNode* srClass::getClassNode() const
{
    return sGetClassNode();
}

// FUNCTION: SURRENDER 0x1000E910
srRegistry::srRegistry() = default;

w8_ulong srRegistry::allocateID()
{
    std::lock_guard lock(mutex);
    return next_instance_id++;
}

srRegistry::ClassNode* srRegistry::getRootNode()
{
    return &root;
}

srRegistry::ClassNode* srRegistry::getClassNode(w8_ulong class_id)
{
    std::lock_guard lock(mutex);
    auto found = classes.find(class_id);
    return found == classes.end() ? nullptr : found->second.get();
}

srRegistry::ClassNode* srRegistry::registerClass(const char* class_name, ClassNode* parent,
                                               w8_ulong class_id)
{
    std::lock_guard lock(mutex);
    if (auto* existing = getClassNode(class_id)) {
        return existing;
    }
    auto metadata = std::make_unique<ClassNode>(ClassNode{parent, class_id, class_name, {}});
    auto* node = metadata.get();
    classes.emplace(class_id, std::move(metadata));
    if (parent) {
        parent->children.insert(parent->children.begin(), node);
    }
    return node;
}

w8_ulong srRegistry::getClassID(ClassNode* node)
{
    return node ? node->class_id : 0;
}

const char* srRegistry::getClassName(ClassNode* node)
{
    return node ? node->class_name.c_str() : nullptr;
}

int srRegistry::isDerivedOrSame(ClassNode* base, ClassNode* derived)
{
    if (!base) return 0;
    for (; derived; derived = derived->parent) {
        if (derived == base) return 1;
    }
    return 0;
}

void srRegistry::registerInstance(srRuntimeClass* instance)
{
    std::lock_guard lock(mutex);
    instances.emplace(instance->getID(), Instance{instance, {}});
}

void srRegistry::renameInstance(srRuntimeClass* instance, std::string name)
{
    std::lock_guard lock(mutex);
    auto found = instances.find(instance->getID());
    if (found == instances.end()) return;
    auto& previous = found->second.name;
    if (!previous.empty()) {
        auto named = names.find(previous);
        named->second.remove(instance);
        if (named->second.empty()) names.erase(named);
    }
    instance->name = std::move(name);
    previous = instance->name;
    if (!previous.empty()) names[previous].push_front(instance);
}

void srRegistry::unregisterInstance(srRuntimeClass* instance)
{
    std::lock_guard lock(mutex);
    auto found = instances.find(instance->getID());
    if (found == instances.end()) return;
    if (!found->second.name.empty()) {
        auto named = names.find(found->second.name);
        named->second.remove(instance);
        if (named->second.empty()) names.erase(named);
    }
    instances.erase(found);
}

bool srRegistry::matches(ClassNode* node, srRuntimeClass* instance, bool exact)
{
    return node && (exact ? node == instance->getClassNode()
                         : isDerivedOrSame(node, instance->getClassNode()));
}

srRuntimeClass* srRegistry::findName(ClassNode* node, std::string_view name,
                                    const srRuntimeClass* relative_to, bool exact)
{
    std::lock_guard lock(mutex);
    if (!node || name.empty()) return nullptr;
    auto named = names.find(std::string(name));
    if (named == names.end()) return nullptr;
    auto cursor = named->second.begin();
    if (relative_to && relative_to->isNamed()) {
        cursor = std::find(cursor, named->second.end(), relative_to);
        if (cursor == named->second.end()) return nullptr;
        ++cursor;
    }
    for (; cursor != named->second.end(); ++cursor) {
        if (matches(node, *cursor, exact)) return *cursor;
    }
    return nullptr;
}

srRuntimeClass* srRegistry::findNext(ClassNode* node, const srRuntimeClass* relative_to,
                                    bool exact)
{
    std::lock_guard lock(mutex);
    auto cursor = instances.begin();
    if (relative_to) cursor = instances.upper_bound(relative_to->getID());
    for (; cursor != instances.end(); ++cursor) {
        if (matches(node, cursor->second.object, exact)) return cursor->second.object;
    }
    return nullptr;
}

srRuntimeClass* srRegistry::findID(ClassNode* node, w8_ulong id, bool exact)
{
    std::lock_guard lock(mutex);
    auto found = instances.find(id);
    return found != instances.end() && matches(node, found->second.object, exact)
               ? found->second.object : nullptr;
}

srRuntimeClass* srRegistry::find(ClassNode* node, std::string_view name,
                                const srRuntimeClass* relative_to)
{
    return findName(node, name, relative_to, false);
}

srRuntimeClass* srRegistry::findExact(ClassNode* node, std::string_view name,
                                     const srRuntimeClass* relative_to)
{
    return findName(node, name, relative_to, true);
}

srRuntimeClass* srRegistry::find(ClassNode* node, const srRuntimeClass* relative_to)
{
    return findNext(node, relative_to, false);
}

srRuntimeClass* srRegistry::findExact(ClassNode* node, const srRuntimeClass* relative_to)
{
    return findNext(node, relative_to, true);
}

srRuntimeClass* srRegistry::find(ClassNode* node, w8_ulong id)
{
    return findID(node, id, false);
}

srRuntimeClass* srRegistry::findExact(ClassNode* node, w8_ulong id)
{
    return findID(node, id, true);
}

w8_long srRegistry::getNumberOfInstances(ClassNode* node, int exact)
{
    std::lock_guard lock(mutex);
    return static_cast<w8_long>(std::count_if(instances.begin(), instances.end(),
        [&](const auto& entry) { return matches(node, entry.second.object, exact != 0); }));
}

void srRegistry::dumpInstanceNames(ClassNode* node, std::ostream& stream, int indent)
{
    std::lock_guard lock(mutex);
    for (const auto& [id, instance] : instances) {
        if (matches(node, instance.object, false) && (indent || instance.object->isNamed())) {
            stream << "Name: " << instance.object->getName()
                   << " (class: " << instance.object->getClassName()
                   << ", address: " << instance.object << ", instanceId: " << id << ")\n";
        }
    }
}

void srRegistry::dumpClassHierarchy(std::ostream& stream)
{
    std::lock_guard lock(mutex);
    auto dump = [&](auto&& self, ClassNode* node, int indent) -> void {
        stream << std::string(indent, ' ') << "Class name: " << node->class_name
               << " (ID: " << node->class_id << ")\n";
        for (auto* child : node->children) self(self, child, indent + 2);
    };
    for (auto* child : root.children) dump(dump, child, 0);
}

// FUNCTION: SURRENDER 0x1000E240
void srClass::addReference() const
{
    ++reference_count;
}

// FUNCTION: SURRENDER 0x1000E250
void srClass::autoRelease()
{
    if (reference_count == 1) {
        reference_count = 0;
    }
}

// FUNCTION: SURRENDER 0x1000E260
int srClass::release() const
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
    if (this == 0) {
#pragma clang diagnostic pop
        return 1;
    }

    if (--reference_count <= 0) {
        delete this;
        return 1;
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1000E840
w8_long srClass::getReferenceCount() const
{
    return reference_count;
}
