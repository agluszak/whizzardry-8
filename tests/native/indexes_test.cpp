#include <catch2/catch_test_macros.hpp>
#include <unordered_map>
#include <vector>

#include "wiz8/index_algorithms.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "surrender/srGERD.h"
#include "surrender/srHuffman.h"
#include "surrender/srMath.h"
#include "surrender/srShader.h"


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
    REQUIRE((values == std::vector<int>{3, 2, 1}));
    RemoveIndexValue(index, 7, 2);
    REQUIRE(LookupIndexValue(index, 7) == 3);
    RemoveIndexValue(index, 7);
    REQUIRE(LookupIndexValue(index, 7) == 1);
    RemoveIndexValue(index, 7);
    REQUIRE(LookupIndexValue(index, 7) == 0);

    // Index destruction and rehashing never destroy or relocate the pointee.
    int borrowed = 42;
    {
        std::unordered_multimap<unsigned int, int*> pointers;
        InsertIndexValue(pointers, 1, &borrowed);
        const auto* stored = &pointers.begin()->second;
        pointers.rehash(2048);
        REQUIRE((*stored == &borrowed && **stored == 42));
        pointers.clear();
    }
    REQUIRE(borrowed == 42);
    return true;
}

static bool mirroredOctreeIndexes()
{
    W8OctreeObjectRegistry registry;
    const srVector3T<int> first(1, 2, 3), second(4, 5, 6);
    REQUIRE(registry.MoveObjectToCell(W8_OCTREE_KIND_PROP, 5, &first));
    REQUIRE(registry.MoveObjectToCell(W8_OCTREE_KIND_PROP, 5, &first));
    const auto object = PackOctreeObjectKey(W8_OCTREE_KIND_PROP, 5);
    REQUIRE(registry.by_object->count(object) == 1);
    REQUIRE(registry.by_cell->size() == 1);
    REQUIRE(registry.MoveObjectToCell(W8_OCTREE_KIND_PROP, 5, &second));
    REQUIRE(registry.by_object->count(object) == 2);
    REQUIRE(registry.UnregisterObject(W8_OCTREE_KIND_PROP, 5));
    REQUIRE(registry.by_object->count(object) == 1);
    REQUIRE(registry.by_cell->count(PackOctreeCellKey(4, 5, 6)) == 0);
    REQUIRE(registry.UnregisterObject(W8_OCTREE_KIND_PROP, 5));
    REQUIRE((registry.by_object->empty() && registry.by_cell->empty()));
    REQUIRE(!registry.UnregisterObject(W8_OCTREE_KIND_PROP, 5));
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
        REQUIRE(!queue.empty());
        REQUIRE(queue.top().priority == expected);
        queue.pop();
    }
    REQUIRE(queue.empty());
    return true;
}

static bool renderingAndSymbolIndexes()
{
    srGERD::Renderer::TextureSetCache cache;
    const srGERD::Renderer::TextureSetKey first{nullptr, nullptr, srShader{}};
    auto second = first;
    second.shader.value = 1;
    REQUIRE(cache.intern(first) == 0); // zero is a real cached index
    REQUIRE(cache.intern(second) == 1);
    REQUIRE(cache.intern(first) == 0);
    REQUIRE(cache.sets.size() == 2);
    cache.clear();
    REQUIRE(cache.intern(second) == 0);

    srHuffman::Sampler sampler;
    sampler.insert(0);
    sampler.insert(500);
    for (unsigned int symbol = 1; symbol < 500; ++symbol) sampler.insert(symbol);
    sampler.insert(0);
    sampler.insert(500);
    REQUIRE(sampler.getNumSymbols() == 501);
    REQUIRE((sampler.getSymbolValue(0) == 0 && sampler.getSymbolFrequency(0) == 2));
    REQUIRE((sampler.getSymbolValue(1) == 500 && sampler.getSymbolFrequency(1) == 2));
    srHuffman::Compressor compressor(sampler);
    REQUIRE(compressor.table.size() == sampler.getNumSymbols());
    for (const auto& [symbol, node] : compressor.table) {
        REQUIRE((node != nullptr && node->symbol == symbol));
        REQUIRE(node >= compressor.nodes.data());
        REQUIRE(node < compressor.nodes.data() + compressor.nodes.size());
    }
    return true;
}

TEST_CASE("indexes_test")
{
    REQUIRE(candidateOrdering());
    REQUIRE(mirroredOctreeIndexes());
    REQUIRE(pathPriorityOrder());
    REQUIRE(renderingAndSymbolIndexes());
}
