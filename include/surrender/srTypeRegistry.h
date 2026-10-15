#pragma once

#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "srCore.h"

class srRuntimeClass;
class srNode;
class srColorSurfaceIFace;

// Class IDs and names are game-format identities, independent of C++ RTTI.
// Objects are not tracked; whoever needs to find an object owns or indexes it.
class srRegistry {
public:
    struct ClassNode {
        ClassNode* parent;
        w8_ulong class_id;
        std::string class_name;
        std::vector<ClassNode*> children;
    };

    srRegistry() = default;
    srRegistry(const srRegistry&) = delete;
    srRegistry& operator=(const srRegistry&) = delete;

    ClassNode* getClassNode(w8_ulong class_id);
    w8_ulong getClassID(ClassNode* node);
    const char* getClassName(ClassNode* node);
    ClassNode* getRootNode();
    int isDerivedOrSame(ClassNode* base, ClassNode* derived);
    ClassNode* registerClass(const char* class_name, ClassNode* parent, w8_ulong class_id);

private:
    ClassNode root{nullptr, 0, "root", {}};
    std::unordered_map<w8_ulong, std::unique_ptr<ClassNode>> classes;
    std::recursive_mutex mutex;
};

// VTABLE: SURRENDER 0x100754E4 srRuntimeClass
// class srRuntimeClass
class srRuntimeClass {
public:
    enum e_verify { VERIFY_DEFAULT = 0 };

    virtual const char* getClassName() const;
    virtual w8_ulong getClassID() const;
    virtual srRegistry::ClassNode* getClassNode() const;
    virtual void dump(std::ostream& stream);
    virtual void verify(e_verify mode);

    static srRegistry::ClassNode* sGetClassNode();

    void setName(std::string name);
    const std::string& getName() const;
    int isNamed() const;
    int matchClassID(w8_ulong class_id) const;

    virtual ~srRuntimeClass() = default;

protected:
    srRuntimeClass() = default;
    /* Copies start unnamed; srClass assignment copies the name explicitly. */
    srRuntimeClass(const srRuntimeClass&) {}
    srRuntimeClass& operator=(const srRuntimeClass&) { return *this; }

private:
    static w8_ulong sGetClassID();

    std::string name;
};

class srClass : public srRuntimeClass {
public:
    typedef srClass RegistryClass;

    static const char* sGetClassName();
    static srRegistry::ClassNode* sGetClassNode();

    /* Assignment copies only the instance name. */
    srClass& operator=(const srClass& other);

    virtual srRegistry::ClassNode* getClassNode() const override;
    virtual void dump(std::ostream& stream) override;
    virtual void verify(srRuntimeClass::e_verify mode) override;

protected:
    srClass();
    srClass(const srClass& other) : srRuntimeClass(other) { *this = other; }
};

/* Supplies format identity and cloning for a concrete scene/data type. */
template<class Derived, class Base, w8_ulong ClassID>
class srClassSupport : public Base {
public:
    typedef Derived RegistryClass;
    enum { CLASS_ID = ClassID };

    static srRegistry::ClassNode* sGetClassNode()
    {
        srRegistry* registry = srCore.getRegistry();
        srRegistry::ClassNode* node = registry->getClassNode(ClassID);

        if (node == 0) {
            node =
                registry->registerClass(Derived::sGetClassName(), Base::sGetClassNode(), ClassID);
        }
        return node;
    }

    virtual const char* getClassName() const override { return Derived::sGetClassName(); }

    virtual w8_ulong getClassID() const override { return ClassID; }

    virtual srRegistry::ClassNode* getClassNode() const override { return sGetClassNode(); }

public:
    srClassSupport() { sGetClassNode(); }

public:
    /* Forwarding constructors. They are templates so that exporting an instantiation whose Base
       lacks a given constructor does not instantiate a forwarding body for it. */
    template<class A0>
        requires(!std::is_base_of_v<srClassSupport, A0>)
    explicit srClassSupport(A0 first_argument) : Base(first_argument)
    {
        sGetClassNode();
    }

    template<class A0, class A1>
    srClassSupport(A0 first_argument, A1 second_argument) : Base(first_argument, second_argument)
    {
        sGetClassNode();
    }

    template<class A0, class A1, class A2>
    srClassSupport(A0 first_argument, A1 second_argument, A2 third_argument)
        : Base(first_argument, second_argument, third_argument)
    {
        sGetClassNode();
    }

    template<class A0, class A1, class A2, class A3, class A4>
    srClassSupport(A0 first_argument, A1 second_argument, A2 third_argument, A3 fourth_argument,
                   A4 fifth_argument)
        : Base(first_argument, second_argument, third_argument, fourth_argument, fifth_argument)
    {
        sGetClassNode();
    }

    srClassSupport(const srClassSupport&) = default;
    srClassSupport& operator=(const srClassSupport&) = default;
};

/* Exact-class downcast by format class ID; the engine builds without RTTI. */
template<class T, class U> T* sr_cast(U* object)
{
    return object != nullptr && object->getClassID() == T::CLASS_ID ? static_cast<T*>(object)
                                                                    : nullptr;
}

template<class T, class U> T* sr_cast(const std::shared_ptr<U>& object)
{
    return sr_cast<T>(object.get());
}
