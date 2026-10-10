#pragma once

#include <iosfwd>
#include <new>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "srCore.h"

class srRuntimeClass;
class srNode;
class srColorSurfaceIFace;

// Class IDs and names are game-format identities, independent of C++ RTTI.
// This facade keeps the existing format-facing queries; objects are indexed once.
class srRegistry {
public:
    struct ClassNode {
        ClassNode* parent;
        w8_ulong class_id;
        std::string class_name;
        std::vector<ClassNode*> children;
    };

    srRegistry();
    ~srRegistry() = default;
    srRegistry(const srRegistry&) = delete;
    srRegistry& operator=(const srRegistry&) = delete;

    w8_ulong allocateID();
    void dumpClassHierarchy(std::ostream& stream);
    void dumpInstanceNames(ClassNode* node, std::ostream& stream, int indent);
    ClassNode* getClassNode(w8_ulong class_id);
    w8_ulong getClassID(ClassNode* node);
    const char* getClassName(ClassNode* node);
    w8_long getNumberOfInstances(ClassNode* node, int exact);
    ClassNode* getRootNode();
    int isDerivedOrSame(ClassNode* base, ClassNode* derived);
    ClassNode* registerClass(const char* class_name, ClassNode* parent, w8_ulong class_id);
    srRuntimeClass* find(ClassNode* node, std::string_view name, const srRuntimeClass* relative_to);
    srRuntimeClass* find(ClassNode* node, const srRuntimeClass* relative_to);
    srRuntimeClass* find(ClassNode* node, w8_ulong id);
    srRuntimeClass* findExact(ClassNode* node, std::string_view name, const srRuntimeClass* relative_to);
    srRuntimeClass* findExact(ClassNode* node, const srRuntimeClass* relative_to);
    srRuntimeClass* findExact(ClassNode* node, w8_ulong id);

private:
    friend class srRuntimeClass;
    struct Instance {
        srRuntimeClass* object;
        std::string name;
    };

    void registerInstance(srRuntimeClass* instance);
    void unregisterInstance(srRuntimeClass* instance);
    void renameInstance(srRuntimeClass* instance, std::string name);
    bool matches(ClassNode* node, srRuntimeClass* instance, bool exact);
    srRuntimeClass* findName(ClassNode* node, std::string_view name,
                             const srRuntimeClass* relative_to, bool exact);
    srRuntimeClass* findNext(ClassNode* node, const srRuntimeClass* relative_to, bool exact);
    srRuntimeClass* findID(ClassNode* node, w8_ulong id, bool exact);

    ClassNode root{nullptr, 0, "root", {}};
    std::unordered_map<w8_ulong, std::unique_ptr<ClassNode>> classes;
    // Newest first, as in the original instance lists. Names may be shared.
    std::map<w8_ulong, Instance, std::greater<w8_ulong>> instances;
    std::unordered_map<std::string, std::list<srRuntimeClass*>> names;
    std::recursive_mutex mutex;
};

// VTABLE: SURRENDER 0x100754E4 srRuntimeClass
// class srRuntimeClass
class srRuntimeClass {
public:
    enum e_verify { VERIFY_DEFAULT = 0 };

    virtual SR_DLL_IMPORT const char* getClassName() const;
    virtual SR_DLL_IMPORT w8_ulong getClassID() const;
    virtual SR_DLL_IMPORT srRegistry::ClassNode* getClassNode() const;
    virtual SR_DLL_IMPORT void dump(std::ostream& stream);
    virtual SR_DLL_IMPORT void verify(e_verify mode);

    static SR_DLL_IMPORT srRegistry::ClassNode* sGetClassNode();
    static SR_DLL_IMPORT w8_long getTotalInstances(int exact);
    static SR_DLL_IMPORT void dumpNames(std::ostream& stream, int indent);

    SR_DLL_IMPORT void setName(std::string name);
    SR_DLL_IMPORT const std::string& getName() const;
    SR_DLL_IMPORT w8_ulong getID() const;
    SR_DLL_IMPORT void getUniqueName(std::ostream& stream) const;
    SR_DLL_IMPORT int isNamed() const;
    SR_DLL_IMPORT int matchClassID(w8_ulong class_id) const;

protected:
    SR_DLL_IMPORT srRuntimeClass();
    virtual SR_DLL_IMPORT ~srRuntimeClass();

private:
    static SR_DLL_IMPORT w8_ulong sGetClassID();

    friend class srRegistry;
    std::string name;
    w8_ulong id;
};


class srClass : public srRuntimeClass {
public:
    typedef srClass RegistryClass;

    static SR_DLL_IMPORT const char* sGetClassName();
    static SR_DLL_IMPORT srRegistry::ClassNode* sGetClassNode();
    static SR_DLL_IMPORT srClass* find(w8_ulong id);
    static SR_DLL_IMPORT srClass* find(std::string_view name, w8_ulong class_id,
                                       const srRuntimeClass* relative_to);
    static SR_DLL_IMPORT srClass* find(std::string_view name, const srClass* relative_to);
    static SR_DLL_IMPORT srClass* find(const srClass* relative_to);

    /* Assignment copies only the instance name. */
    SR_DLL_IMPORT srClass& operator=(const srClass& other);

    virtual SR_DLL_IMPORT srRegistry::ClassNode* getClassNode() const override;
    virtual SR_DLL_IMPORT void dump(std::ostream& stream) override;
    virtual SR_DLL_IMPORT void verify(srRuntimeClass::e_verify mode) override;

protected:
    virtual SR_DLL_IMPORT ~srClass() override;

public:
    virtual srClass* vInstance() = 0;

    /* Slot 7; clone is the nonvirtual forwarder onto it, as instance is onto vInstance. */
    virtual srClass* vClone() = 0;

    // FUNCTION: SURRENDER 0x1000E860 SYMBOL
    // RECOMP: ?clone@srClass@@QAEPAV1@XZ
    srClass* clone()
    {
        return vClone();
    }
    SR_DLL_IMPORT srClass* instance();
    SR_DLL_IMPORT int release() const;
    SR_DLL_IMPORT void addReference() const;
    SR_DLL_IMPORT w8_long getReferenceCount() const;
    SR_DLL_IMPORT void autoRelease();
    SR_DLL_IMPORT void touch();
    SR_DLL_IMPORT w8_ulong getTimestamp() const;

protected:
    SR_DLL_IMPORT srClass();
    SR_DLL_IMPORT w8_ulong allocateTimeStamps(w8_ulong count) const;

private:
    static SR_DLL_IMPORT w8_ulong _timestampCtr;

    mutable w8_long reference_count;
    w8_ulong timestamp;
};


/* Supplies format identity and cloning for a concrete scene/data type. */
template <class Derived, class Base, w8_ulong ClassID>
class srClassSupport : public Base {
public:
    typedef Derived RegistryClass;
    enum { CLASS_ID = ClassID };

    static srRegistry::ClassNode* sGetClassNode()
    {
        srRegistry* registry = srCore.getRegistry();
        srRegistry::ClassNode* node = registry->getClassNode(ClassID);

        if (node == 0) {
            node = registry->registerClass(Derived::sGetClassName(), Base::sGetClassNode(), ClassID);
        }
        return node;
    }

    virtual const char* getClassName() const override
    {
        return Derived::sGetClassName();
    }

    virtual w8_ulong getClassID() const override
    {
        return ClassID;
    }

    virtual srRegistry::ClassNode* getClassNode() const override
    {
        return sGetClassNode();
    }

public:
    srClassSupport()
    {
        sGetClassNode();
    }

    /* Give the copy its own identity and scene links. Derived members are copied by
       the concrete constructor after this base has been initialized. */
    srClassSupport(const Derived& other) : Base()
    {
        sGetClassNode();
        Base::operator=(other);
    }

public:
    /* Forwarding constructors. They are templates so that exporting an instantiation whose Base
       lacks a given constructor does not instantiate a forwarding body for it. */
    template <class A0> explicit srClassSupport(A0 first_argument) : Base(first_argument)
    {
        sGetClassNode();
    }

    template <class A0, class A1>
    srClassSupport(A0 first_argument, A1 second_argument) : Base(first_argument, second_argument)
    {
        sGetClassNode();
    }

    template <class A0, class A1, class A2>
    srClassSupport(A0 first_argument, A1 second_argument, A2 third_argument)
        : Base(first_argument, second_argument, third_argument)
    {
        sGetClassNode();
    }

    template <class A0, class A1, class A2, class A3, class A4>
    srClassSupport(A0 first_argument, A1 second_argument, A2 third_argument, A3 fourth_argument,
                   A4 fifth_argument)
        : Base(first_argument, second_argument, third_argument, fourth_argument, fifth_argument)
    {
        sGetClassNode();
    }

    srClassSupport& operator=(const srClassSupport&) = default;

protected:
    ~srClassSupport() override = default;

public:
    /* Slot 7 of every registry class; returns srClass* at every level. */
    virtual srClass* vClone() override
    {
        Derived* copy = static_cast<Derived*>(this->vInstance());
        *copy = *static_cast<const Derived*>(this);
        return copy;
    }
};
