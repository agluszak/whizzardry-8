#include "Container.h"
#include "allocation_failures.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); std::exit(1); } } while (0)

static void stack_and_queue()
{
    auto stack = CreateStack(1, sizeof(unsigned));
    auto queue = CreateQueue(1, sizeof(unsigned));
    CHECK(stack && queue);
    unsigned output = 99;
    CHECK(!Pop(stack, &output) && !PeekStack(stack, &output));
    CHECK(!RemfromQueue(queue, &output) && !PeekQueue(queue, &output));
    CHECK(output == 99);
    for (unsigned i = 0; i < 2048; ++i) {
        CHECK(Push(stack, &i) == stack);
        CHECK(AddtoQueue(queue, &i) == queue);
    }
    CHECK(StackSize(stack) == 2048 && QueueSize(queue) == 2048);
    CHECK(PeekStack(stack, &output) && output == 2047);
    CHECK(PeekQueue(queue, &output) && output == 0);
    for (unsigned i = 0; i < 1024; ++i) {
        CHECK(Pop(stack, &output) && output == 2047 - i);
        CHECK(RemfromQueue(queue, &output) && output == i);
    }
    for (unsigned i = 2048; i < 4096; ++i) {
        CHECK(Push(stack, &i) == stack);
        CHECK(AddtoQueue(queue, &i) == queue);
    }
    for (unsigned i = 0; i < 2048; ++i)
        CHECK(Pop(stack, &output) && output == 4095 - i);
    for (unsigned i = 0; i < 1024; ++i)
        CHECK(Pop(stack, &output) && output == 1023 - i);
    for (unsigned i = 1024; i < 4096; ++i)
        CHECK(RemfromQueue(queue, &output) && output == i);
    CHECK(StackSize(stack) == 0 && QueueSize(queue) == 0);
    CHECK(!Pop(stack, &output) && !RemfromQueue(queue, &output));
    CHECK(DeleteStack(stack) && DeleteQueue(queue));
}

static void list_model()
{
    auto list = CreateList(1, sizeof(unsigned));
    CHECK(list);
    std::vector<unsigned> expected;
    std::mt19937 random(817);
    for (unsigned step = 0; step < 4000; ++step) {
        const auto position = static_cast<UINT32>(random() % (expected.size() + 1));
        unsigned value = random();
        if (step < 1024 || random() % 3 == 0 || position == expected.size()) {
            CHECK(AddtoList(list, &value, position) == list);
            expected.insert(expected.begin() + position, value);
        } else if (random() % 2 == 0) {
            unsigned output = 0;
            CHECK(RemfromList(list, &output, position));
            CHECK(output == expected[position]);
            expected.erase(expected.begin() + position);
        } else {
            CHECK(StoreListNode(list, &value, position));
            expected[position] = value;
        }
        CHECK(ListSize(list) == expected.size());
        for (UINT32 i = 0; i < expected.size(); ++i) {
            unsigned output = 0;
            CHECK(PeekList(list, &output, i) && output == expected[i]);
        }
    }
    while (!expected.empty()) {
        const auto position = static_cast<UINT32>(expected.size() / 2);
        unsigned output = 0;
        CHECK(RemfromList(list, &output, position) && output == expected[position]);
        expected.erase(expected.begin() + position);
    }
    unsigned value = 13;
    CHECK(!RemfromList(list, &value, 0) && !StoreListNode(list, &value, 0));
    CHECK(AddtoList(list, &value, 0) == list);
    CHECK(PeekList(list, &value, 0) && value == 13);
    CHECK(DeleteList(list));
}

static void invalid_and_failed_growth()
{
    unsigned value = 19;
    CHECK(!CreateStack(0, 4) && !CreateQueue(1, 0) && !CreateList(0, 0));
    CHECK(!CreateList(std::numeric_limits<UINT32>::max(), 2));
    CHECK(!CreateQueue(2, std::numeric_limits<UINT32>::max()));
    CHECK(!CreateStack(std::numeric_limits<UINT32>::max(), 2));
    CHECK(!Push(nullptr, &value) && !AddtoQueue(nullptr, &value) && !AddtoList(nullptr, &value, 0));
    CHECK(!Pop(nullptr, &value) && !RemfromQueue(nullptr, &value) && !RemfromList(nullptr, &value, 0));
    CHECK(!PeekStack(nullptr, &value) && !PeekQueue(nullptr, &value) && !PeekList(nullptr, &value, 0));
    CHECK(!StoreListNode(nullptr, &value, 0));
    CHECK(!DeleteStack(nullptr) && !DeleteQueue(nullptr) && !DeleteList(nullptr));
    CHECK(StackSize(nullptr) == 0 && QueueSize(nullptr) == 0 && ListSize(nullptr) == 0);
    for (int failure = 0; failure < 4; ++failure) {
        allocation_failures::after = failure;
        auto stack = CreateStack(1, sizeof(value));
        allocation_failures::after = -1;
        if (stack)
            CHECK(DeleteStack(stack));
        else
            CHECK(failure < 3);
    }
    auto list = CreateList(1, sizeof(value));
    auto stack = CreateStack(1, sizeof(value));
    auto queue = CreateQueue(1, sizeof(value));
    CHECK(list && stack && queue);
    CHECK(!Push(list, &value) && !AddtoQueue(stack, &value) && !AddtoList(queue, &value, 0));
    CHECK(!DeleteStack(list) && !DeleteQueue(stack) && !DeleteList(queue));
    CHECK(!Push(stack, nullptr) && !AddtoQueue(queue, nullptr) && !AddtoList(list, nullptr, 0));
    CHECK(!Pop(stack, nullptr) && !RemfromQueue(queue, nullptr));
    CHECK(!PeekList(list, nullptr, 0) && !StoreListNode(list, nullptr, 0));
    CHECK(!AddtoList(list, &value, 1));
    CHECK(!AddtoList(list, &value, std::numeric_limits<UINT32>::max()));
    for (unsigned i = 0; i < 1024; ++i) {
        for (auto handle : {list, stack, queue}) {
            const auto position = ListSize(list) / 2;
            allocation_failures::after = static_cast<int>(i % 2);
            void* inserted = handle == list ? AddtoList(list, &i, position) :
                             handle == stack ? Push(stack, &i) : AddtoQueue(queue, &i);
            allocation_failures::after = -1;
            if (inserted) {
                CHECK(inserted == handle);
                unsigned removed = 0;
                if (handle == list)
                    CHECK(RemfromList(list, &removed, position) && removed == i);
                else if (handle == stack)
                    CHECK(Pop(stack, &removed) && removed == i);
                else {
                    CHECK(QueueSize(queue) == i + 1);
                }
            } else {
                if (handle == list)
                    CHECK(ListSize(list) == i);
                else if (handle == stack)
                    CHECK(StackSize(stack) == i);
                else
                    CHECK(QueueSize(queue) == i);
            }
            if (handle == list)
                CHECK(AddtoList(list, &i, ListSize(list)) == list);
            else if (handle == stack)
                CHECK(Push(stack, &i) == stack);
            else if (!inserted)
                CHECK(AddtoQueue(queue, &i) == queue);
        }
        for (unsigned j = 0; j <= i; ++j) {
            unsigned output = 0;
            CHECK(PeekList(list, &output, j) && output == j);
        }
    }
    CHECK(!PeekList(list, &value, 1024) && !RemfromList(list, &value, 1024));
    CHECK(!StoreListNode(list, &value, 1024));
    for (unsigned i = 0; i < 1024; ++i) {
        CHECK(RemfromQueue(queue, &value) && value == i);
        CHECK(Pop(stack, &value) && value == 1023 - i);
    }
    CHECK(DeleteList(list) && DeleteStack(stack) && DeleteQueue(queue));
}

static void borrowed_data_and_shutdown()
{
    unsigned destructions = 0;
    struct Borrowed
    {
        unsigned& destructions;
        explicit Borrowed(unsigned& destructions) : destructions(destructions) {}
        ~Borrowed() { ++destructions; }
    };
    auto owned = std::make_unique<Borrowed>(destructions);
    destructions = 0;
    struct Entry { Borrowed* pointer; unsigned value; } entry{owned.get(), 42};
    auto list = CreateList(1, sizeof(Entry));
    CHECK(list);
    CHECK(AddtoList(list, &entry, 0) == list);
    entry.value = 0;
    ShutdownContainers();
    InitializeContainers();
    Entry output{};
    CHECK(PeekList(list, &output, 0) && output.pointer == owned.get() && output.value == 42);
    CHECK(DeleteList(list));
    CHECK(destructions == 0);
    owned.reset();
    CHECK(destructions == 1);
    for (unsigned cycle = 0; cycle < 10; ++cycle) {
        InitializeContainers();
        auto stack = CreateStack(1, 1);
        auto queue = CreateQueue(1, 1);
        CHECK(stack && queue);
        unsigned char byte = 0xab;
        CHECK(Push(stack, &byte) == stack && AddtoQueue(queue, &byte) == queue);
        ShutdownContainers();
        byte = 0;
        CHECK(Pop(stack, &byte) && byte == 0xab);
        CHECK(RemfromQueue(queue, &byte) && byte == 0xab);
        CHECK(DeleteStack(stack) && DeleteQueue(queue));
    }
}

static void concurrent_queue()
{
    auto queue = CreateQueue(1, sizeof(unsigned));
    CHECK(queue);
    std::vector<std::thread> producers;
    for (unsigned thread = 0; thread < 4; ++thread) {
        producers.emplace_back([queue, thread] {
            for (unsigned i = 0; i < 500; ++i) {
                unsigned value = thread * 500 + i;
                CHECK(AddtoQueue(queue, &value) == queue);
            }
        });
    }
    for (auto& producer : producers)
        producer.join();
    CHECK(QueueSize(queue) == 2000);
    std::vector<bool> seen(2000);
    unsigned next[4]{};
    for (unsigned i = 0; i < 2000; ++i) {
        unsigned value = 0;
        CHECK(RemfromQueue(queue, &value));
        CHECK(value < 2000 && !seen[value]);
        CHECK(value % 500 == next[value / 500]++);
        seen[value] = true;
    }
    CHECK(QueueSize(queue) == 0);
    CHECK(DeleteQueue(queue));
}

int main()
{
    InitializeContainers();
    stack_and_queue();
    list_model();
    invalid_and_failed_growth();
    borrowed_data_and_shutdown();
    concurrent_queue();
    ShutdownContainers();
    std::puts("containers: order, growth, indexed removal/store, borrowed data, shutdown, allocation failures and concurrency passed");
    return 0;
}
