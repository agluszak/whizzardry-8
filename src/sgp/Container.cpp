/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "Container.h"
#include "DEBUG.H"

#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace
{
enum class Kind { stack, queue, list };

struct Container
{
    Kind kind;
    UINT32 elementSize;
    std::deque<std::vector<std::byte>> elements;
    std::mutex mutex;
};

Container* GetContainer(HCONTAINER handle, Kind kind)
{
    auto* container = static_cast<Container*>(handle);
    return container && container->kind == kind ? container : nullptr;
}

HCONTAINER CreateContainer(UINT32 estimatedItems, UINT32 elementSize, Kind kind)
{
    if (!estimatedItems || !elementSize ||
        estimatedItems > std::numeric_limits<UINT32>::max() / elementSize)
        return nullptr;
    try {
        auto container = std::make_unique<Container>();
        container->kind = kind;
        container->elementSize = elementSize;
        return container.release();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

HCONTAINER Insert(HCONTAINER handle, void* data, Kind kind, UINT32 position, bool append)
{
    auto* container = GetContainer(handle, kind);
    if (!container || !data)
        return nullptr;
    const std::lock_guard lock(container->mutex);
    auto& elements = container->elements;
    if (append)
        position = static_cast<UINT32>(elements.size());
    if (position > elements.size() || elements.size() == std::numeric_limits<UINT32>::max())
        return nullptr;
    try {
        std::vector<std::byte> element(container->elementSize);
        std::memcpy(element.data(), data, element.size());
        // Moving a byte vector is noexcept: failed deque growth leaves existing entries intact.
        elements.insert(elements.begin() + position, std::move(element));
        return handle;
    } catch (const std::bad_alloc&) {
        return nullptr;
    } catch (const std::length_error&) {
        return nullptr;
    }
}

BOOLEAN Read(HCONTAINER handle, void* data, Kind kind, UINT32 position, bool last, bool remove)
{
    auto* container = GetContainer(handle, kind);
    if (!container || !data)
        return FALSE;
    const std::lock_guard lock(container->mutex);
    auto& elements = container->elements;
    if (elements.empty())
        return FALSE;
    if (last)
        position = static_cast<UINT32>(elements.size() - 1);
    if (position >= elements.size())
        return FALSE;
    const auto& element = elements[position];
    std::memcpy(data, element.data(), element.size());
    if (remove)
        elements.erase(elements.begin() + position);
    return TRUE;
}

UINT32 Size(HCONTAINER handle, Kind kind)
{
    auto* container = GetContainer(handle, kind);
    if (!container)
        return 0;
    const std::lock_guard lock(container->mutex);
    return static_cast<UINT32>(container->elements.size());
}

BOOLEAN Delete(HCONTAINER handle, Kind kind)
{
    auto* container = GetContainer(handle, kind);
    if (!container)
        return FALSE;
    delete container;
    return TRUE;
}
} // namespace

void InitializeContainers(void)
{
    RegisterDebugTopic(TOPIC_STACK_CONTAINERS, "Stack Container");
    RegisterDebugTopic(TOPIC_LIST_CONTAINERS, "List Container");
    RegisterDebugTopic(TOPIC_QUEUE_CONTAINERS, "Queue Container");
    RegisterDebugTopic(TOPIC_ORDLIST_CONTAINERS, "Ordered List Container");
}

void ShutdownContainers(void)
{
    UnRegisterDebugTopic(TOPIC_STACK_CONTAINERS, "Stack Container");
    UnRegisterDebugTopic(TOPIC_LIST_CONTAINERS, "List Container");
    UnRegisterDebugTopic(TOPIC_QUEUE_CONTAINERS, "Queue Container");
    UnRegisterDebugTopic(TOPIC_ORDLIST_CONTAINERS, "Ordered List Container");
}

HSTACK CreateStack(UINT32 estimatedItems, UINT32 elementSize)
{
    return CreateContainer(estimatedItems, elementSize, Kind::stack);
}

HSTACK Push(HSTACK stack, void* data)
{
    return Insert(stack, data, Kind::stack, 0, true);
}

BOOLEAN Pop(HSTACK stack, void* data)
{
    return Read(stack, data, Kind::stack, 0, true, true);
}

BOOLEAN PeekStack(HSTACK stack, void* data)
{
    return Read(stack, data, Kind::stack, 0, true, false);
}

UINT32 StackSize(HSTACK stack)
{
    return Size(stack, Kind::stack);
}

BOOLEAN DeleteStack(HSTACK stack)
{
    return Delete(stack, Kind::stack);
}

HQUEUE CreateQueue(UINT32 estimatedItems, UINT32 elementSize)
{
    return CreateContainer(estimatedItems, elementSize, Kind::queue);
}

HQUEUE AddtoQueue(HQUEUE queue, void* data)
{
    return Insert(queue, data, Kind::queue, 0, true);
}

BOOLEAN RemfromQueue(HQUEUE queue, void* data)
{
    return Read(queue, data, Kind::queue, 0, false, true);
}

BOOLEAN PeekQueue(HQUEUE queue, void* data)
{
    return Read(queue, data, Kind::queue, 0, false, false);
}

UINT32 QueueSize(HQUEUE queue)
{
    return Size(queue, Kind::queue);
}

BOOLEAN DeleteQueue(HQUEUE queue)
{
    return Delete(queue, Kind::queue);
}

HLIST CreateList(UINT32 estimatedItems, UINT32 elementSize)
{
    return CreateContainer(estimatedItems, elementSize, Kind::list);
}

HLIST AddtoList(HLIST list, void* data, UINT32 position)
{
    return Insert(list, data, Kind::list, position, false);
}

BOOLEAN RemfromList(HLIST list, void* data, UINT32 position)
{
    return Read(list, data, Kind::list, position, false, true);
}

BOOLEAN PeekList(HLIST list, void* data, UINT32 position)
{
    return Read(list, data, Kind::list, position, false, false);
}

BOOLEAN StoreListNode(HLIST list, void* data, UINT32 position)
{
    auto* container = GetContainer(list, Kind::list);
    if (!container || !data)
        return FALSE;
    const std::lock_guard lock(container->mutex);
    if (position >= container->elements.size())
        return FALSE;
    auto& element = container->elements[position];
    std::memcpy(element.data(), data, element.size());
    return TRUE;
}

UINT32 ListSize(HLIST list)
{
    return Size(list, Kind::list);
}

BOOLEAN DeleteList(HLIST list)
{
    return Delete(list, Kind::list);
}
