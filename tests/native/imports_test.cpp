#include "surrender/srMath.h"
/* Native consumer-side linking and lifetime for the renderer contracts used
   by the game, including compiler-generated members imported on Windows. */
#include "surrender/srCamera.h"
#include "surrender/srClipPlane.h"
#include "surrender/srFog.h"
#include "surrender/srHuffman.h"
#include "surrender/srLight.h"
#include "surrender/srMaterial.h"
#include "surrender/srMeshModel.h"
#include "surrender/srMemoryAllocator.h"
#include "surrender/srModeler.h"
#include "surrender/srQuadWord.h"
#include "surrender/srScene.h"
#include "surrender/srTextureFile.h"
#include "surrender/srVP_generic.h"
#include <cfenv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
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
static int destroyed_nodes = 0;
struct ClientNode : srNode
{
    bool transformDirty() const { return testNotify(NOTIFY_TRANSFORM_DIRTY) != 0; }
    ~ClientNode() override
    {
        ++destroyed_nodes;
    }
};
struct ClientMaterial : srMaterial
{
    ~ClientMaterial() override
    {
    }
};
struct ClientTextureFile : srTextureFile
{
    ClientTextureFile() : srTextureFile(nullptr, 0)
    {
    }
    ~ClientTextureFile() override
    {
    }
};
struct TrackedElement
{
    inline static int live = 0;
    inline static int constructors_before_throw = -1;
    int value = 0;
    TrackedElement()
    {
        if (constructors_before_throw == 0) throw std::runtime_error("element construction");
        if (constructors_before_throw > 0) --constructors_before_throw;
        ++live;
    }
    ~TrackedElement() { --live; }
};

int main()
{
    {
        srMeshModel::MeshTable<TrackedElement> table;
        table.Resize(3, 0);
        CHECK(TrackedElement::live == 3 && table.count == 3);
        table.data[0].value = 42;
        table.Resize(5, 1);
        CHECK(TrackedElement::live == 5 && table.data[0].value == 42);
        auto copy = table;
        CHECK(TrackedElement::live == 10 && copy.data[0].value == 42);
        TrackedElement::constructors_before_throw = 1;
        bool threw = false;
        try { table.Resize(8, 1); }
        catch (const std::runtime_error&) { threw = true; }
        TrackedElement::constructors_before_throw = -1;
        CHECK(threw && TrackedElement::live == 10 && table.count == 5);
        CHECK(table.data[0].value == 42);
        table.Release();
        CHECK(TrackedElement::live == 5 && !table.data && !table.count);
    }
    CHECK(TrackedElement::live == 0);
    {
        srModeler modeler;
        srModeler::Triangle triangle;
        for (int row = 0; row < 2; ++row)
        {
            for (int vertex = 0; vertex < 3; ++vertex)
                triangle.vertices[vertex].position.Set(row * 10 + vertex, row * 20 + vertex, -vertex);
            modeler.addTriangle(triangle);
        }
        float minimum, maximum;
        modeler.getAxialBounds(srModeler::AXIS_X, minimum, maximum);
        CHECK(minimum == 0 && maximum == 12);
        modeler.getAxialBounds(srModeler::AXIS_Y, minimum, maximum);
        CHECK(minimum == 0 && maximum == 22);
        modeler.getAxialBounds(srModeler::AXIS_Z, minimum, maximum);
        CHECK(minimum == -2 && maximum == 0);
    }
    struct alignas(8) WordPair
    {
        unsigned int padding;
        srQuadWord value;
    } words{0, {7, 1}};
    static_assert(offsetof(WordPair, value) == 4, "exercise four-byte aligned word pair");
    CHECK(double(words.value) == 4294967303.0);
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
    {
        CHECK(fesetround(mode) == 0);
        const double inputs[] = {0.0,
                                 1.5,
                                 -1.5,
                                 2147483647.0,
                                 -2147483648.0,
                                 2147483648.0,
                                 -2147483649.0,
                                 4294967296.0,
                                 std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()};
        for (volatile double input : inputs)
        {
            feclearexcept(FE_ALL_EXCEPT);
            const w8_long actual = srFloatToInt(input);
            const bool invalid = fetestexcept(FE_INVALID);
#if defined(__i386__) || defined(__x86_64__)
            w8_long retail;
            unsigned short saved_control, status;
            asm volatile("fnstcw %0" : "=m"(saved_control));
            const unsigned rounding = mode == FE_DOWNWARD ? 1 : mode == FE_UPWARD ? 2
                                       : mode == FE_TOWARDZERO ? 3 : 0;
            const unsigned short control = (saved_control & ~0x0c00u) | (rounding << 10);
            // The Windows x64 CRT controls SSE, not the oracle's x87 state.
            asm volatile("fldcw %0; fnclex" : : "m"(control));
            asm volatile("fldl %2; fistpl %0; fnstsw %1"
                         : "=m"(retail), "=m"(status) : "m"(input) : "st");
            asm volatile("fnclex; fldcw %0" : : "m"(saved_control));
            CHECK(actual == retail && invalid == bool(status & 1));
#else
            if (!std::isfinite(input) || input >= 2147483648.0 || input < -2147483648.0)
            {
                CHECK(actual == (-2147483647 - 1) && invalid);
            }
#endif
        }
    }
    CHECK(fesetround(FE_TONEAREST) == 0);
    CHECK(srFloatToInt(std::numeric_limits<float>::quiet_NaN()) == (-2147483647 - 1));
    srAssertSetFunc(assertion);
    CHECK(srInit());
    {
        const int before_destruction = destroyed_nodes;
        srScene* scene = new srClientSupport<srScene, 0x1010>;
        auto child = new ClientNode;
        child->setParent(scene, 0);
        CHECK(scene->getChildCount() == 1);
        delete scene;
        CHECK(destroyed_nodes == before_destruction + 1);

        auto parent_node = new ClientNode;
        auto transformed_child = new ClientNode;
        transformed_child->setParent(parent_node, 0);
        CHECK(transformed_child->getWorldSpaceLocation().x == 0);
        CHECK(!parent_node->transformDirty() && !transformed_child->transformDirty());
        parent_node->setLocation(10, 0, 0);
        CHECK(parent_node->transformDirty() && transformed_child->transformDirty());
        CHECK(transformed_child->getWorldSpaceLocation().x == 10);
        CHECK(!parent_node->transformDirty() && !transformed_child->transformDirty());
        transformed_child->setLocation(3, 0, 0);
        CHECK(transformed_child->getWorldSpaceLocation().x == 13);
        auto other_parent = new ClientNode;
        other_parent->setLocation(20, 0, 0);
        transformed_child->setParent(other_parent, 1);
        CHECK(transformed_child->getWorldSpaceLocation().x == 13);
        other_parent->setLocation(30, 0, 0);
        CHECK(transformed_child->transformDirty());
        CHECK(transformed_child->getWorldSpaceLocation().x == 23);
        delete parent_node;
        delete other_parent;

        struct alignas(16) PackedVectors
        {
            char padding;
            srVector3 values[2];
            srVector3 minimum;
            srVector3 maximum;
        } vectors;
        vectors.values[0].Set(5, -2, 7);
        vectors.values[1].Set(-3, 9, 1);
        CHECK(reinterpret_cast<uintptr_t>(&vectors.values[0]) % alignof(float) != 0);
        srVP_generic processor;
        processor._minMax(vectors.values, vectors.minimum, vectors.maximum, 2);
        CHECK(vectors.minimum.x == -3 && vectors.minimum.y == -2 && vectors.minimum.z == 1);
        CHECK(vectors.maximum.x == 5 && vectors.maximum.y == 9 && vectors.maximum.z == 7);
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
        // A raw operator-new name buffer must use the same release family.
        ClientTextureFile texture;
        texture.setFileName("first.bmp");
        texture.setFileName("second.bmp");
        CHECK(strcmp(texture.getFileName(), "second.bmp") == 0);
        texture.setFileName(nullptr);
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
        for (auto value : values)
            tree_sampler.insert(value);
        srHuffman::Compressor compressor(tree_sampler);
        srBinOMStream encoded;
        {
            srHuffman::BitOStream bits(encoded);
            bits.put(compressor.num_symbols, 32);
            bits.put(compressor.code_width, 6);
            bits.put(sizeof(values) / sizeof(values[0]), 32);
            compressor.storeSymbolTable(bits);
            for (auto value : values)
                compressor.compressSymbol(bits, value);
        }
        srBinIMStream input(encoded.getPtr(), encoded.getSize());
        srHuffman::BitIStream bits(input);
        srHuffman::Decompressor decoder(bits);
        CHECK(decoder.getDataCount() == sizeof(values) / sizeof(values[0]));
        for (auto value : values)
            CHECK(decoder.decompressSymbol() == value);
    }
    srAssertFail("native probe", "imports_test.cpp", 42, "value %d", 7);
    CHECK(assertions == 1);
    srExit();
    srAssertSetFunc(nullptr);
    puts("ok: native renderer consumer imports, generated special members and variadic assertion");
}
