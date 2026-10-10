#include "wiz8/index_algorithms.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "surrender/srGERD.h"
#include "surrender/srHuffman.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression); return false; \
} } while (0)

static bool candidateOrdering()
{
    std::unordered_multimap<unsigned int, int> index;
    InsertIndexValue(index, 7, 1);
    InsertIndexValue(index, 7, 2);
    InsertIndexValue(index, 7, 3);
    InsertUniqueIndexValue(index, 7, 2);
    // Growth must not reverse the candidates of an existing key.
    for (unsigned int key = 100; key < 10000; ++key) InsertIndexValue(index, key, 4);
    index.rehash(index.bucket_count() * 2);
    auto [first, last] = index.equal_range(7);
    std::vector<int> values;
    for (auto item = first; item != last; ++item) values.push_back(item->second);
    CHECK((values == std::vector<int>{3, 2, 1}));
    RemoveIndexValue(index, 7, 2);
    CHECK(LookupIndexValue(index, 7) == 3);
    RemoveIndexValue(index, 7);
    CHECK(LookupIndexValue(index, 7) == 1);
    RemoveIndexValue(index, 7);
    CHECK(LookupIndexValue(index, 7) == 0);

    // Index destruction and rehashing never destroy or relocate the pointee.
    int borrowed = 42;
    {
        std::unordered_multimap<unsigned int, int*> pointers;
        InsertIndexValue(pointers, 1, &borrowed);
        const auto* stored = &pointers.begin()->second;
        pointers.rehash(2048);
        CHECK(*stored == &borrowed && **stored == 42);
        pointers.clear();
    }
    CHECK(borrowed == 42);
    return true;
}

static bool mirroredOctreeIndexes()
{
    W8OctreeObjectRegistry registry;
    const srVector3T<int> first(1, 2, 3), second(4, 5, 6);
    CHECK(registry.MoveObjectToCell(W8_OCTREE_KIND_PROP, 5, &first));
    CHECK(registry.MoveObjectToCell(W8_OCTREE_KIND_PROP, 5, &first));
    const auto object = PackOctreeObjectKey(W8_OCTREE_KIND_PROP, 5);
    CHECK(registry.by_object->count(object) == 1);
    CHECK(registry.by_cell->size() == 1);
    CHECK(registry.MoveObjectToCell(W8_OCTREE_KIND_PROP, 5, &second));
    CHECK(registry.by_object->count(object) == 2);
    CHECK(registry.UnregisterObject(W8_OCTREE_KIND_PROP, 5));
    CHECK(registry.by_object->count(object) == 1);
    CHECK(registry.by_cell->count(PackOctreeCellKey(4, 5, 6)) == 0);
    CHECK(registry.UnregisterObject(W8_OCTREE_KIND_PROP, 5));
    CHECK(registry.by_object->empty() && registry.by_cell->empty());
    CHECK(!registry.UnregisterObject(W8_OCTREE_KIND_PROP, 5));
    return true;
}

static bool pathPriorityOrder()
{
    W8PathOpenSet queue;
    queue.push({10, 4});
    queue.push({11, 1});
    queue.push({12, 3});
    queue.push({13, 1});
    queue.push({14, 2});
    for (const auto expected : {1u, 1u, 2u, 3u, 4u}) {
        CHECK(!queue.empty());
        CHECK(queue.top().priority == expected);
        queue.pop();
    }
    CHECK(queue.empty());
    return true;
}

static bool renderingAndSymbolIndexes()
{
    srGERD::Renderer::TextureSetCache cache;
    const srGERD::Renderer::TextureSetKey first{nullptr, nullptr, srShader{}};
    auto second = first;
    second.shader.value = 1;
    CHECK(cache.intern(first) == 0); // zero is a real cached index
    CHECK(cache.intern(second) == 1);
    CHECK(cache.intern(first) == 0);
    CHECK(cache.sets.size() == 2);
    cache.clear();
    CHECK(cache.intern(second) == 0);

    srHuffman::Sampler sampler;
    sampler.insert(0);
    sampler.insert(500);
    for (unsigned int symbol = 1; symbol < 500; ++symbol) sampler.insert(symbol);
    sampler.insert(0);
    sampler.insert(500);
    CHECK(sampler.getNumSymbols() == 501);
    CHECK(sampler.getSymbolValue(0) == 0 && sampler.getSymbolFrequency(0) == 2);
    CHECK(sampler.getSymbolValue(1) == 500 && sampler.getSymbolFrequency(1) == 2);
    srHuffman::Compressor compressor(sampler);
    CHECK(compressor.table.size() == sampler.getNumSymbols());
    for (const auto& [symbol, node] : compressor.table) {
        CHECK(node != nullptr && node->symbol == symbol);
        CHECK(node >= compressor.nodes.data());
        CHECK(node < compressor.nodes.data() + compressor.nodes.size());
    }
    return true;
}

int main()
{
    return candidateOrdering() && mirroredOctreeIndexes() && pathPriorityOrder() &&
           renderingAndSymbolIndexes() ? 0 : 1;
}
