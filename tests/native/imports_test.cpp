/* Native consumer-side linking and lifetime for the renderer contracts used
   by the game, including compiler-generated members imported on Windows. */
#include "surrender/srCamera.h"
#include "surrender/srClipPlane.h"
#include "surrender/srFog.h"
#include "surrender/srHuffman.h"
#include "surrender/srLight.h"
#include "surrender/srMaterial.h"
#include "surrender/srMemoryAllocator.h"
#include "surrender/srQuadWord.h"
#include "wiz8/sr_api.h"
#include <cstdio>
#include <cstring>

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                               \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static int assertions = 0;
static void assertion(const char* expression, const char* path, w8_long line, const char* message)
{
    if (strcmp(expression, "native probe") || strcmp(path, "imports_test.cpp") || line != 42 ||
        strcmp(message, "value 7"))
    {
        fputs("assertion arguments\n", stderr);
        exit(1);
    }
    ++assertions;
}
struct ClientNode : srNode
{
    ~ClientNode() override {}
};
struct ClientMaterial : srMaterial
{
    ~ClientMaterial() override {}
};
int main()
{
    struct alignas(8) WordPair
    {
        unsigned int padding;
        srQuadWord value;
    } words{0, {7, 1}};
    static_assert(offsetof(WordPair, value) == 4, "exercise four-byte aligned word pair");
    CHECK(double(words.value) == 4294967303.0);
    srAssertSetFunc(assertion);
    CHECK(srInit());
    {
        srMemoryAllocator allocator;
        const unsigned sizes[] = {1u, 17u, 257u, 1025u};
        for (unsigned size : sizes)
        {
            void* first = allocator.allocate(3, size, "first allocation");
            void* second = allocator.allocate(size + 1, "second allocation");
            CHECK(reinterpret_cast<w8_ulong_ptr>(first) % 32 == 0);
            CHECK(reinterpret_cast<w8_ulong_ptr>(second) % 32 == 0);
            CHECK(allocator.getSize(first) == 3 * size && allocator.getSize(second) == size + 1);
            memset(first, 0xa5, 3 * size);
            memset(second, 0x5a, size + 1);
            CHECK(strcmp(allocator.getName(first), "first allocation") == 0);
            CHECK(strcmp(allocator.getName(second), "second allocation") == 0);
            allocator.free(first); // Unlink a non-head block, then the head.
            allocator.free(second);
        }
        srCamera source, copy;
        source.setViewPlane(2, 3);
        copy = source;
        srCamera::Rect before, after;
        double distance;
        source.getViewPlane(before, distance);
        copy.getViewPlane(after, distance);
        CHECK(before.left == after.left && before.top == after.top);
        srClipPlane plane, plane_copy;
        srVector4T<float> plane_value;
        plane_value.x = 1;
        plane_value.y = 2;
        plane_value.z = 3;
        plane_value.w = 4;
        plane.setClipPlane(plane_value);
        plane_copy = plane;
        CHECK(plane_copy.getClipPlane().w == 4);
        srFog fog, fog_copy;
        fog.setDensity(0.25f);
        fog_copy = fog;
        CHECK(fog_copy.getDensity() == 0.25f);
        ClientMaterial material, material_copy;
        srVector4T<float> emissive;
        emissive.x = 0.25f;
        emissive.y = 0.5f;
        emissive.z = 0.75f;
        emissive.w = 1;
        material.setEmissive(emissive);
        material_copy.srMaterial::operator=(material);
        CHECK(material_copy.getEmissive().z == 0.75f);
        // Copied lights must own independent registry entries and scene links.
        auto parent = new ClientNode;
        auto original = new srLight(parent);
        auto sibling = new srLight(parent);
        auto clone = new srLight(*original);
        CHECK(clone->getParent() != parent);
        clone->setParent(parent, 0);
        delete original;
        CHECK(parent->getChildCount() == 2 && clone->getParent() == parent &&
              sibling->getParent() == parent);
        delete parent; // Recursively releases the sibling and copied light.
        srHuffman::Sampler sampler;
        sampler.insert(10);
        sampler.insert(10);
        sampler.insert(11);
        CHECK(sampler.getNumSymbols() == 2 && sampler.getSymbolFrequency(0) == 2);
        // Exercise a branching symbol tree, as used by octree alpha bits.
        const w8_ulong values[] = {10, 10, 11, 12, 13, 10, 13, 14, 0xffffffff};
        srHuffman::Sampler tree_sampler;
        for (auto value : values) tree_sampler.insert(value);
        srHuffman::Compressor compressor(tree_sampler);
        srBinOMStream encoded;
        {
            srHuffman::BitOStream bits(encoded);
            bits.put(compressor.num_symbols, 32);
            bits.put(compressor.code_width, 6);
            bits.put(sizeof(values) / sizeof(values[0]), 32);
            compressor.storeSymbolTable(bits);
            for (auto value : values) compressor.compressSymbol(bits, value);
        }
        srBinIMStream input(encoded.getPtr(), encoded.getSize());
        srHuffman::BitIStream bits(input);
        srHuffman::Decompressor decoder(bits);
        CHECK(decoder.getDataCount() == sizeof(values) / sizeof(values[0]));
        for (auto value : values) CHECK(decoder.decompressSymbol() == value);

    }
    srAssertFail("native probe", "imports_test.cpp", 42, "value %d", 7);
    CHECK(assertions == 1);
    srExit();
    srAssertSetFunc(nullptr);
    puts("ok: native renderer consumer imports, generated special members and variadic assertion");
}
