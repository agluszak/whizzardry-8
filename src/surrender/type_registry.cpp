#include <ios>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "compat/compiler.h"
#include "surrender/srCore.h"
#include "surrender/srDebug.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/stream_scope.h"

#define SRRUNTIMECLASS_CPP "srRuntimeClass.cpp"

// FUNCTION: SURRENDER 0x10011790
const std::string& srRuntimeClass::getName() const
{
    static const std::string anonymous = "anonymous";
    return name.empty() ? anonymous : name;
}

// FUNCTION: SURRENDER 0x100117A0
void srRuntimeClass::verify(e_verify)
{
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

// FUNCTION: SURRENDER 0x10011950
void srRuntimeClass::setName(std::string name)
{
    this->name = std::move(name);
}

// FUNCTION: SURRENDER 0x10011AB0
void srRuntimeClass::dump(std::ostream& stream)
{
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    // c-style-cast-ok: the id prints as a pointer.
    stream << "Class Id: 0x" << std::hex << getClassID() << std::dec << '\n';
    stream.width(0x20);
    stream << "Class name: " << getClassName() << '\n';
    stream.width(0x20);
    stream << "Instance name: " << getName() << '\n';
    stream.width(0x20);
    stream << "Memory address: " << this << '\n';
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
}

/* Assigns only the instance name. */
// FUNCTION: SURRENDER 0x1000E110
srClass& srClass::operator=(const srClass& other)
{
    if (this != &other) {
        setName(other.isNamed() ? other.getName() : std::string{});
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1000E130
srClass::srClass()
{
    sGetClassNode();
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

// FUNCTION: SURRENDER 0x1000E620
void srClass::dump(std::ostream& stream)
{
    srRuntimeClass::dump(stream);
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
    if (!base) {
        return 0;
    }
    for (; derived; derived = derived->parent) {
        if (derived == base) {
            return 1;
        }
    }
    return 0;
}
