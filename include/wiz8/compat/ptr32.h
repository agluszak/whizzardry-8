#pragma once

/* A pointer member inside a record the game reads or writes as raw bytes.
   Retail stores a 32-bit pointer in the slot (garbage on disk, assigned after
   loading). This temporary adapter keeps four-byte slots through a pointer
   table until disk records and runtime objects are separated. */
#include <stdint.h>
#include <string.h>

#include <unordered_map>
#include <vector>

namespace w8_ptr32_detail {
struct Table {
    std::vector<void*> pointers;
    std::unordered_map<void*, uint32_t> handles;
    Table() : pointers(1, nullptr) {}
};

inline Table& table()
{
    static Table instance;
    return instance;
}

/* Equal pointers share a handle, so the table grows with distinct addresses only. */
inline uint32_t store(void* pointer)
{
    if (pointer == nullptr) {
        return 0;
    }
    Table& t = table();
    auto found = t.handles.find(pointer);
    if (found != t.handles.end()) {
        return found->second;
    }
    const uint32_t handle = (uint32_t)t.pointers.size();
    t.pointers.push_back(pointer);
    t.handles.emplace(pointer, handle);
    return handle;
}

/* Only runtime handles may be resolved here. Loaders must reset disk pointer
   slots or inspect their raw marker bytes before installing live pointers;
   a saved word can happen to equal a valid handle in this process. */
inline void* load(uint32_t handle)
{
    const Table& t = table();
    return handle < t.pointers.size() ? t.pointers[handle] : nullptr;
}
} // namespace w8_ptr32_detail

template <class T> class W8Ptr32 {
public:
    W8Ptr32& operator=(T* pointer)
    {
        const uint32_t value = w8_ptr32_detail::store(const_cast<void*>(static_cast<const void*>(pointer)));
        memcpy(handle_, &value, sizeof(value));
        return *this;
    }

    operator T*() const
    {
        uint32_t value;
        memcpy(&value, handle_, sizeof(value));
        return static_cast<T*>(w8_ptr32_detail::load(value));
    }

    T* operator->() const
    {
        return *this;
    }

    bool hasStoredValue() const
    {
        uint32_t value;
        memcpy(&value, handle_, sizeof(value));
        return value != 0;
    }

private:
    /* Bytes, not uint32_t: the slots sit in packed records. */
    unsigned char handle_[4];
};

#define W8_PTR32(T) W8Ptr32<T>

/* Disk pointer words select following payloads; they must never be resolved
   through the runtime handle table to decide whether those payloads exist. */
template <class T> inline bool W8SerializedPointerPresent(const W8Ptr32<T>& slot)
{
    return slot.hasStoredValue();
}
