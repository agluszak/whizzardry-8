#include <algorithm>
#include <atomic>
#include <utility>

#include "surrender/srTypeRegistry.h"
#include "surrender/srDebug.h"

#include <ostream>

#define SRRUNTIMECLASS_CPP "D:\\srsdk1x\\sources\\corelib\\srRuntimeClass.cpp"
#define SRCLASS_CPP "D:\\srsdk1x\\sources\\corelib\\srClass.cpp"

namespace {
std::atomic<w8_ulong> next_instance_id{1};
} // namespace

// GLOBAL: SURRENDER 0x100A45AC
w8_ulong srClass::_timestampCtr;

// GLOBAL: SURRENDER 0x100A45B0
srClass::Update* srClass::_firstUpdate;

// GLOBAL: SURRENDER 0x100A45B8
double srClass::_lastUpdateTime;

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
    srCore.getRegistry()->renameInstance(getClassNode(), this, std::move(name));
}

// FUNCTION: SURRENDER 0x100119D0
srRuntimeClass::srRuntimeClass()
{
    srRegistry* registry = srCore.getRegistry();
    id = registry->allocateID();
    registry->registerInstance(sGetClassNode(), this);
}

// FUNCTION: SURRENDER 0x10011A40
srRuntimeClass::~srRuntimeClass()
{
    srCore.getRegistry()->unregisterInstance(sGetClassNode(), this);
}

// FUNCTION: SURRENDER 0x10011AB0
void srRuntimeClass::dump(std::ostream& stream)
{
    std::ios::fmtflags flags = stream.flags();
    stream.setf(std::ios::left, std::ios::adjustfield);
    stream.width(0x20);
    stream << "Class Id: 0x" << std::hex << getClassID() << std::dec << '\n';
    stream.width(0x20);
    stream << "Class name: " << getClassName() << '\n';
    stream.width(0x20);
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
        node = registry->registerClass("srRuntimeClass", registry->getRootNode(), 1, 0);
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
srClass::srClass() : reference_count(1), update(0)
{
    srCore.getRegistry()->registerInstance(sGetClassNode(), this);
    touch();
}

// FUNCTION: SURRENDER 0x1000E1A0
srClass::~srClass()
{
    srCore.getRegistry()->unregisterInstance(sGetClassNode(), this);
}

// FUNCTION: SURRENDER 0x1000E200
srRegistry::ClassNode* srClass::sGetClassNode()
{
    srRegistry* registry = srCore.getRegistry();
    srRegistry::ClassNode* node = registry->getClassNode(0x100);
    if (node == 0) {
        node = registry->registerClass(sGetClassName(), srRuntimeClass::sGetClassNode(), 0x100, 0);
    }
    return node;
}

// FUNCTION: SURRENDER 0x1000E2F0
srClass::UpdateCallBack srClass::getUpdateCallBack()
{
    return update == 0 ? 0 : update->callback;
}

// FUNCTION: SURRENDER 0x1000E360
double srClass::getUpdateInterval()
{
    return update == 0 ? 0.0 : update->interval;
}

// FUNCTION: SURRENDER 0x1000E380
void srClass::setUpdate(UpdateCallBack callback, double interval)
{
    if (update != 0 || callback != 0) {
        if (update != 0) {
            if (callback != 0) {
                update->callback = callback;
                update->interval = interval;
                return;
            }

            if (update != 0) {
                if (update->previous != 0) {
                    update->previous->next = update->next;
                }
                if (update->next != 0) {
                    update->next->previous = update->previous;
                }
                if (update == _firstUpdate) {
                    _firstUpdate = update->next;
                }
                delete update;
                update = 0;
            }
        }

        if (callback != 0) {
            update = new Update;
            update->instance = this;
            update->callback = callback;
            update->interval = interval;
            update->last_update_time = _lastUpdateTime;
            update->next = _firstUpdate;
            update->previous = 0;
            if (update->next != 0) {
                update->next->previous = update;
            }
            _firstUpdate = update;
        }
    }
}

// FUNCTION: SURRENDER 0x1000E490
void srClass::setUpdatesTime(double time)
{
    _lastUpdateTime = time;
    for (Update* update = _firstUpdate; update != 0; update = update->next) {
        update->last_update_time = time;
    }
}

// FUNCTION: SURRENDER 0x1000E4D0
void srClass::performUpdates(double time)
{
    if (time > _lastUpdateTime) {
        Update* update = _firstUpdate;
        if (_lastUpdateTime == 0.0) {
            _lastUpdateTime = time;
        }
        while (update != 0) {
            Update* next = update->next;
            if (update->interval <= 0.0) {
                update->callback(update->instance, time, time - _lastUpdateTime);
                update->last_update_time = time;
            } else {
                for (double update_time = update->last_update_time + update->interval;
                     update_time <= time; update_time += update->interval) {
                    update->callback(update->instance, update_time,
                                     update_time - update->last_update_time);
                    update->last_update_time = update_time;
                }
            }
            update = next;
        }
        _lastUpdateTime = time;
    }
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
    stream.width(0x20);
    if (update != 0) {
        if (update->interval == 0.0) {
            stream << "  Update interval: " << "every frame" << '\n';
        } else {
            stream << "  Update interval: " << update->interval << "secs" << '\n';
        }
        stream.width(0x20);
        // c-style-cast-ok: the callback prints as a pointer.
        stream << "    Update callback: " << (void*)update->callback << '\n';
        if (update->instance != 0) {
            stream.width(0x20);
            stream << "    Update owner: ";
            update->instance->getUniqueName(stream);
            stream << '\n';
        }
        if (update->previous != 0) {
            stream.width(0x20);
            stream << "    Update previous: " << update->previous << '\n';
        }
        if (update->next != 0) {
            stream.width(0x20);
            stream << "    Update next: " << update->next << '\n';
        }
    } else {
        stream << "  Update interval: " << "disabled" << '\n';
    }
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
srRegistry::srRegistry()
{
    root = addToTree(nullptr, "root", 0);
}

// FUNCTION: SURRENDER 0x1000EA40
w8_ulong srRegistry::getClassID(ClassNode* node)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->getClassID();
}

// FUNCTION: SURRENDER 0x1000EAA0
const char* srRegistry::getClassName(ClassNode* node)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->class_name.c_str();
}

// FUNCTION: SURRENDER 0x1000EAD0
srRegistry::~srRegistry() = default;

// FUNCTION: SURRENDER 0x1000EBD0
srRegistry::ClassNode* srRegistry::getClassNode(w8_ulong class_id)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    if (class_id == 0) {
        return 0;
    }
    const auto found = class_index.find(class_id);
    return found == class_index.end() ? nullptr : found->second.get();
}

// FUNCTION: SURRENDER 0x1000EC60
srRegistry::ClassNode* srRegistry::registerClass(const char* class_name, ClassNode* parent,
                                                 w8_ulong class_id, int register_instances)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    const auto found = class_index.find(class_id);
    ClassNode* node = found == class_index.end() ? nullptr : found->second.get();
    if (node == 0) {
        srDebugPrintf(0xfe, "srRegistry::registerClass() - registering %s (ID 0x%x)\n", class_name,
                      class_id);
        node = addToTree(parent, class_name, class_id);
        if (register_instances != 0) {
            node->enableInstanceLookup();
        }
    }
    return node;
}

// FUNCTION: SURRENDER 0x1000ED40
void srRegistry::dumpClassHierarchy(std::ostream& stream)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    for (ClassNode* child : root->children) {
        child->dump(stream, 0);
    }
}

// FUNCTION: SURRENDER 0x1000EDB0
srRegistry::ClassNode* srRegistry::addToTree(ClassNode* parent, const char* class_name,
                                             w8_ulong class_id)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    auto node = std::unique_ptr<ClassNode>(new ClassNode(parent, class_name, class_id));
    ClassNode* result = node.get();
    class_index.emplace(class_id, std::move(node));
    if (parent != nullptr) {
        try {
            parent->children.insert(parent->children.begin(), result);
        } catch (...) {
            class_index.erase(class_id);
            throw;
        }
    }
    return result;
}

// FUNCTION: SURRENDER 0x1000EEA0
void srRegistry::dumpInstanceNames(ClassNode* node, std::ostream& stream, int indent)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    // c-style-cast-ok: selects the instance overload.
    for (srRuntimeClass* instance = find(node, (srRuntimeClass*)0); instance != 0;
         instance = find(node, instance)) {
        if (indent != 0 || instance->isNamed()) {
            stream << "Name: " << instance->getName();
            stream << " (class: " << instance->getClassName() << ", address: " << instance
                   << ", instanceId: " << instance->getID() << ")\n";
        }
    }
}

// FUNCTION: SURRENDER 0x1000EFB0
void srRegistry::registerInstance(ClassNode* node, srRuntimeClass* instance)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    node->registerInstance(instance);
}

// FUNCTION: SURRENDER 0x1000F010
// FUNCTION: SURRENDER 0x1000FAD0
void srRegistry::renameInstance(ClassNode* node, srRuntimeClass* instance, std::string name)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    // Remove the old key before changing the owned name. Both operations share the registry lock.
    for (ClassNode* ancestor = node; ancestor != nullptr; ancestor = ancestor->parent) {
        ancestor->removeName(instance);
    }
    instance->name = std::move(name);
    for (ClassNode* ancestor = node; ancestor != nullptr; ancestor = ancestor->parent) {
        ancestor->addName(instance);
    }
}

// FUNCTION: SURRENDER 0x1000F070
srRuntimeClass* srRegistry::find(ClassNode* node, std::string_view name,
                                 const srRuntimeClass* relative_to)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->findByName(node, name, 0, relative_to);
}

// FUNCTION: SURRENDER 0x1000F0E0
srRuntimeClass* srRegistry::findExact(ClassNode* node, std::string_view name,
                                      const srRuntimeClass* relative_to)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->findByName(node, name, 1, relative_to);
}

// FUNCTION: SURRENDER 0x1000F150
srRuntimeClass* srRegistry::find(ClassNode* node, w8_ulong id)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->findByID(node, id, 0);
}

// FUNCTION: SURRENDER 0x1000F1B0
srRuntimeClass* srRegistry::findExact(ClassNode* node, w8_ulong id)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->findByID(node, id, 1);
}

// FUNCTION: SURRENDER 0x1000F210
void srRegistry::unregisterInstance(ClassNode* node, srRuntimeClass* instance)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    node->unregisterInstance(instance);
}

// FUNCTION: SURRENDER 0x1000F270
int srRegistry::isDerivedOrSame(ClassNode* base, ClassNode* derived)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    if (base != 0 && derived != 0) {
        return base->isDerivedOrSame(derived);
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1000F2F0
srRuntimeClass* srRegistry::findExact(ClassNode* node, const srRuntimeClass* relative_to)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->findRelative(node, 1, relative_to);
}

// FUNCTION: SURRENDER 0x1000F350
srRuntimeClass* srRegistry::find(ClassNode* node, const srRuntimeClass* relative_to)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->findRelative(node, 0, relative_to);
}

// FUNCTION: SURRENDER 0x1000F3B0
srRegistry::ClassNode* srRegistry::getRootClass()
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return root->children.empty() ? nullptr : root->children.front();
}

// FUNCTION: SURRENDER 0x1000F3E0
srRegistry::ClassNode* srRegistry::getChildClass(ClassNode* parent, ClassNode* child)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    auto next = parent->children.begin();
    if (child != nullptr) {
        next = std::find(next, parent->children.end(), child);
        if (next == parent->children.end()) {
            return nullptr;
        }
        ++next;
    }
    return next == parent->children.end() ? nullptr : *next;
}

// FUNCTION: SURRENDER 0x1000F450
int srRegistry::checkValidity()
{
    return 1;
}

// FUNCTION: SURRENDER 0x1000F460
w8_long srRegistry::getNumberOfInstances(ClassNode* node, int exact)
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return node->getNumberOfInstances(exact);
}

// FUNCTION: SURRENDER 0x1000F4C0
w8_ulong srRegistry::allocateID()
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return next_instance_id++;
}

// FUNCTION: SURRENDER 0x100105C0
srRegistry::ClassNode* srRegistry::getRootNode()
{
    std::lock_guard<std::recursive_mutex> access(critical_section);
    return root;
}

// FUNCTION: SURRENDER 0x1000F580
// FUNCTION: SURRENDER 0x1000F5F0
srRegistry::ClassNode::ClassNode(ClassNode* parent, const char* class_name, w8_ulong class_id)
    : parent(parent), class_id(class_id), class_name(class_name)
{
}

// FUNCTION: SURRENDER 0x1000F7E0
void srRegistry::ClassNode::enableInstanceLookup()
{
    instance_lookup_enabled = true;
}

// FUNCTION: SURRENDER 0x1000FEE0
void srRegistry::ClassNode::dump(std::ostream& stream, int indent)
{
    int i;
    for (i = indent; i != 0; i--) {
        stream << ' ';
    }
    stream << "Class name: " << class_name << '\n';
    for (i = indent; i != 0; i--) {
        stream << ' ';
    }
    const auto flags = stream.flags();
    stream << "Class Id: 0x" << std::hex << class_id << '\n';
    stream.flags(flags);
    for (i = indent; i != 0; i--) {
        stream << ' ';
    }
    stream << "Num children: " << children.size() << '\n';
    for (i = indent; i != 0; i--) {
        stream << ' ';
    }
    stream << "Hash: " << (instance_lookup_enabled ? &named_instances : nullptr) << '\n';
    for (i = indent; i != 0; i--) {
        stream << ' ';
    }
    ClassNode* inherited = parent == nullptr ? nullptr : parent->getLookupNode();
    stream << "Nearest parent hash: " << (inherited == nullptr ? nullptr : &inherited->named_instances)
           << '\n';
    for (ClassNode* child : children) {
        child->dump(stream, indent + 2);
    }
}

// FUNCTION: SURRENDER 0x1000FED0
w8_ulong srRegistry::ClassNode::getClassID() const
{
    return class_id;
}

// FUNCTION: SURRENDER 0x10010060
srRegistry::ClassNode* srRegistry::ClassNode::getParent() const
{
    return parent;
}

// FUNCTION: SURRENDER 0x10010070
// FUNCTION: SURRENDER 0x10010080
srRegistry::ClassNode* srRegistry::ClassNode::getLookupNode()
{
    for (ClassNode* node = this; node != nullptr; node = node->parent) {
        if (node->instance_lookup_enabled) {
            return node;
        }
    }
    return nullptr;
}

void srRegistry::ClassNode::addName(srRuntimeClass* instance)
{
    if (instance_lookup_enabled && instance->isNamed()) {
        auto& duplicates = named_instances[instance->getName()];
        duplicates.insert(duplicates.begin(), instance);
    }
}

void srRegistry::ClassNode::removeName(srRuntimeClass* instance)
{
    if (!instance_lookup_enabled || !instance->isNamed()) {
        return;
    }
    const auto found = named_instances.find(instance->getName());
    if (found != named_instances.end()) {
        std::erase(found->second, instance);
        if (found->second.empty()) {
            named_instances.erase(found);
        }
    }
}

// FUNCTION: SURRENDER 0x1000F930
void srRegistry::ClassNode::registerInstance(srRuntimeClass* instance)
{
    addName(instance);
    if (instance_lookup_enabled) {
        // FUNCTION: SURRENDER 0x100107E0
        instances_by_id.emplace(instance->getID(), instance);
    }
    ++instance_count;
}

// FUNCTION: SURRENDER 0x1000FCD0
void srRegistry::ClassNode::unregisterInstance(srRuntimeClass* instance)
{
    removeName(instance);
    instances_by_id.erase(instance->getID());
    --instance_count;
}

// FUNCTION: SURRENDER 0x100100D0
srRuntimeClass* srRegistry::ClassNode::findByName(ClassNode* requested_class, std::string_view name,
                                                  int exact, const srRuntimeClass* relative_to)
{
    ClassNode* index = getLookupNode();
    if (index == nullptr) {
        if (exact != 0) {
            return nullptr;
        }
        auto child = children.begin();
        if (relative_to != nullptr) {
            for (; child != children.end(); ++child) {
                srRuntimeClass* hit = (*child)->findByName(requested_class, name, 0, nullptr);
                while (hit != nullptr && hit != relative_to) {
                    hit = (*child)->findByName(requested_class, name, 0, hit);
                }
                if (hit == relative_to) {
                    if (auto* found = (*child)->findByName(requested_class, name, 0, relative_to)) {
                        return found;
                    }
                    ++child;
                    break;
                }
            }
        }
        for (; child != children.end(); ++child) {
            if (auto* found = (*child)->findByName(requested_class, name, 0, nullptr)) {
                return found;
            }
        }
        return nullptr;
    }

    const auto named = index->named_instances.find(name);
    if (named == index->named_instances.end()) {
        return nullptr;
    }
    const auto& duplicates = named->second;
    auto next = duplicates.begin();
    if (relative_to != nullptr) {
        next = std::find(next, duplicates.end(), relative_to);
        if (next != duplicates.end()) {
            ++next;
        } else {
            // An unindexed relative starts at the first match; an indexed different name does not.
            const auto relative_name = index->named_instances.find(relative_to->getName());
            if (relative_name != index->named_instances.end() &&
                std::find(relative_name->second.begin(), relative_name->second.end(), relative_to) !=
                    relative_name->second.end()) {
                return nullptr;
            }
            next = duplicates.begin();
        }
    }
    for (; next != duplicates.end(); ++next) {
        srRuntimeClass* found = *next;
        if (exact != 0 ? requested_class->isSame(found->getClassNode())
                       : requested_class->isDerivedOrSame(found->getClassNode())) {
            return found;
        }
    }
    return nullptr;
}

// FUNCTION: SURRENDER 0x100103B0
srRuntimeClass* srRegistry::ClassNode::findRelative(ClassNode* requested_class, int exact,
                                                    const srRuntimeClass* relative_to)
{
    ClassNode* index = getLookupNode();
    if (index == nullptr) {
        if (exact != 0) {
            return nullptr;
        }
        auto child = children.begin();
        if (relative_to != nullptr) {
            for (; child != children.end(); ++child) {
                srRuntimeClass* hit = (*child)->findRelative(requested_class, 0, nullptr);
                while (hit != nullptr && hit != relative_to) {
                    hit = (*child)->findRelative(requested_class, 0, hit);
                }
                if (hit == relative_to) {
                    if (auto* found = (*child)->findRelative(requested_class, 0, relative_to)) {
                        return found;
                    }
                    ++child;
                    break;
                }
            }
        }
        for (; child != children.end(); ++child) {
            if (auto* found = (*child)->findRelative(requested_class, 0, nullptr)) {
                return found;
            }
        }
        return nullptr;
    }

    auto next = index->instances_by_id.rbegin();
    if (relative_to != nullptr) {
        const auto relative = index->instances_by_id.find(relative_to->getID());
        if (relative == index->instances_by_id.end()) {
            return nullptr;
        }
        next = std::make_reverse_iterator(relative);
    }
    for (; next != index->instances_by_id.rend(); ++next) {
        srRuntimeClass* found = next->second;
        if (exact != 0 ? requested_class->isSame(found->getClassNode())
                       : requested_class->isDerivedOrSame(found->getClassNode())) {
            return found;
        }
    }
    return nullptr;
}

// FUNCTION: SURRENDER 0x100104D0
srRuntimeClass* srRegistry::ClassNode::findByID(ClassNode* requested_class, w8_ulong id,
                                                int exact)
{
    ClassNode* index = getLookupNode();
    if (index == nullptr) {
        if (exact == 0) {
            for (ClassNode* child : children) {
                srRuntimeClass* found = child->findByID(requested_class, id, 0);
                if (found != nullptr) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    const auto entry = index->instances_by_id.find(id);
    if (entry != index->instances_by_id.end()) {
        srRuntimeClass* found = entry->second;
        if (exact == 0) {
            if (requested_class->isDerivedOrSame(found->getClassNode())) {
                return found;
            }
        } else {
            if (requested_class->isSame(found->getClassNode())) {
                return found;
            }
        }
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1000F920
int srRegistry::ClassNode::isSame(ClassNode* other) const
{
    return this == other;
}

// FUNCTION: SURRENDER 0x1000F8E0
int srRegistry::ClassNode::isDerivedOrSame(ClassNode* derived) const
{
    while (true) {
        if (this == derived) {
            return 1;
        }
        if (derived->getParent() == 0) {
            break;
        }
        derived = derived->getParent();
    }
    return 0;
}

// FUNCTION: SURRENDER 0x10010090
w8_long srRegistry::ClassNode::getNumberOfInstances(int exact) const
{
    if (exact == 0) {
        return instance_count;
    }

    w8_long child_instances = 0;
    for (ClassNode* child : children) {
        child_instances += child->getNumberOfInstances(0);
    }
    return instance_count - child_instances;
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
