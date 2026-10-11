#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_surface.h>
#include <cfenv>
#include <algorithm>
#include <array>
#include <limits>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "surrender/srIO.h"
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
#include "surrender/srModelInstance.h"
#include "surrender/srModeler.h"
#include "surrender/srQuadWord.h"
#include "surrender/srScene.h"
#include "surrender/srTextureFile.h"
#include "surrender/srTextureMap.h"
#include "wiz8/sr_api.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srARGB.h"
#include "surrender/srColorSurface.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srNode.h"
#include "surrender/srPalette.h"
#include "surrender/srPixelConvert.h"
#include "surrender/srTextureIFace.h"

class srModel;

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
    ClientTextureFile() : srTextureFile({}, 0)
    {
    }
    ~ClientTextureFile() override
    {
    }
};
// Compares a 3x2 BGRA32 surface with the 20-byte-pitch fixture rows.
static bool samePixels(srColorSurface& surface, const unsigned char* pixels)
{
    const auto* data = static_cast<const unsigned char*>(surface.getDataPtr());
    for (int row = 0; row < 2; ++row) {
        if (std::memcmp(data + row * surface.getPitch(), pixels + row * 20, 12) != 0) {
            return false;
        }
    }
    return true;
}

static int surface_storage()
{
    std::array<unsigned char, 40> pixels;
    for (std::size_t index = 0; index < pixels.size(); ++index)
        pixels[index] = static_cast<unsigned char>(index * 7);
    const auto original = pixels;
    auto palette = std::make_shared<srPalette>();
    const auto references = palette.use_count();
    {
        srColorSurface borrowed(srPixelConvert::SURFACE_BGRA32, pixels.data(), 3, 2, 20);
        borrowed.setPalette(palette);
        REQUIRE((borrowed.getDataPtr() == pixels.data() && borrowed.getDataSize() == 40));
        REQUIRE((!borrowed.resize(4, 3) && !borrowed.rescale(4, 3)));
        srPixelConvert::PixelFormat format;
        srPixelConvert::mapPixelFormat(srPixelConvert::SURFACE_RGB24, format);
        REQUIRE(!borrowed.changePixelFormat(format, 1));

        srColorSurface copied(borrowed);
        REQUIRE(copied.getDataPtr() != pixels.data());
        REQUIRE(samePixels(copied, pixels.data()));
        REQUIRE(palette.use_count() == references + 2);
        copied.setPixelRaw(0, 0, 0x12345678);
        REQUIRE((pixels == original && copied.getPixelRaw(0, 0) == 0x12345678));
        copied = static_cast<const srColorSurface&>(copied);
        REQUIRE(copied.getPixelRaw(0, 0) == 0x12345678);

        srColorSurface assigned(srPixelConvert::SURFACE_BGRA32, 1, 1);
        assigned = borrowed;
        REQUIRE(assigned.getDataPtr() != pixels.data());
        REQUIRE(samePixels(assigned, pixels.data()));
        REQUIRE(palette.use_count() == references + 3);
        srColorSurface view(srPixelConvert::SURFACE_BGRA32, assigned.getDataPtr(), 3, 2,
                            assigned.getPitch());
        assigned = view;
        REQUIRE(samePixels(assigned, pixels.data()));
        REQUIRE(palette.use_count() == references + 2);
        REQUIRE(assigned.resize(7, 5));
        auto* begin = static_cast<unsigned char*>(assigned.getDataPtr());
        REQUIRE(std::all_of(begin, begin + assigned.getDataSize(), [](auto value) { return value == 0; }));
        assigned.fill(0xff336699);
        REQUIRE(assigned.rescale(3, 2));
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
                REQUIRE(assigned.getPixelRaw(x, y) == 0xff336699);
        REQUIRE(assigned.changePixelFormat(format, 1));
        REQUIRE((assigned.getPitch() >= 9 && assigned.getPixelRaw(0, 0) == 0x996633));
        REQUIRE(assigned.getPixel(0, 0) == 0xff336699);

        std::array<unsigned char, 4> other_pixels{1, 2, 3, 4};
        srColorSurface other_borrowed(srPixelConvert::SURFACE_BGRA32, other_pixels.data(), 1, 1, 4);
        other_borrowed = borrowed;
        REQUIRE(other_pixels == (std::array<unsigned char, 4>{1, 2, 3, 4}));
        REQUIRE(other_borrowed.getDataPtr() != pixels.data());
        REQUIRE(other_borrowed.resize(2, 2));
    }
    REQUIRE((pixels == original && palette.use_count() == references));

    // A view of an SDL surface keeps its pixels after the original owner releases them.
    {
        SDL_Surface* owner = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_ARGB1555);
        REQUIRE(owner);
        srColorSurface shared(srPixelConvert::SURFACE_ARGB1555, *owner);
        REQUIRE((shared.getDataPtr() == owner->pixels && !shared.resize(4, 4)));
        SDL_DestroySurface(owner);
        shared.setPixelRaw(1, 1, 0x7fff);
        REQUIRE(shared.getPixelRaw(1, 1) == 0x7fff);
    }
    return 0;
}

// Instances, including copies, share ownership of their model.
static int shared_model_lifetimes()
{
    auto model = std::make_shared<srMeshModel>(0, 0);
    std::weak_ptr<srModel> lifetime = model;
    auto* first = new srModelInstance(nullptr);
    first->setModel(std::move(model));
    auto* copy = new srModelInstance(nullptr);
    *copy = *first;
    REQUIRE(copy->getModel() == first->getModel());
    delete first;
    REQUIRE(!lifetime.expired());
    delete copy;
    REQUIRE(lifetime.expired());
    return 0;
}

// Maps share their source surface; copies of dimensions also keep the palette alive.
static int shared_surface_lifetimes()
{
    struct Map : srTextureMap {
        using srTextureMap::srTextureMap;
        Map& operator=(const Map&) = default;
        ~Map() override = default;
    };
    std::weak_ptr<srColorSurfaceIFace> surface_lifetime;
    std::weak_ptr<srPalette> palette_lifetime;
    srTextureIFace::Dimensions dimensions{};
    {
        auto palette = std::make_shared<srPalette>(nullptr, 7);
        srARGB color;
        color.alpha = 0xff; color.red = 0x12; color.green = 0x34; color.blue = 0x56;
        palette->setColor(0, color);
        palette_lifetime = palette;
        srPalette borrowed;
        borrowed = *palette;
        auto surface = std::make_shared<srColorSurface>(srPixelConvert::SURFACE_P8, 2, 2);
        surface->setPalette(palette);
        surface_lifetime = surface;
        Map first(surface), second;
        second = first;
        first.getDimensions(dimensions);
        surface.reset();
        palette.reset();
        first.setSurfacePtr(nullptr);
        REQUIRE(!surface_lifetime.expired());
        REQUIRE(second.getSurfacePtr()->getWidth() == 2);
        second.setSurfacePtr(nullptr);
        REQUIRE(surface_lifetime.expired());
        REQUIRE(!palette_lifetime.expired());
    }
    dimensions.palette.reset();
    REQUIRE(palette_lifetime.expired());
    return 0;
}

TEST_CASE("imports_test")
{
    {
        srModeler modeler;
        srModeler::Triangle triangle;
        for (int row = 0; row < 2; ++row)
        {
            for (int vertex = 0; vertex < 3; ++vertex)
                triangle.vertices[vertex].position =
                    glm::vec3(row * 10 + vertex, row * 20 + vertex, -vertex);
            modeler.addTriangle(triangle);
        }
        REQUIRE((modeler.getAxialBounds(srModeler::AXIS_X) == std::pair{0.f, 12.f}));
        REQUIRE((modeler.getAxialBounds(srModeler::AXIS_Y) == std::pair{0.f, 22.f}));
        REQUIRE((modeler.getAxialBounds(srModeler::AXIS_Z) == std::pair{-2.f, 0.f}));
        REQUIRE_FALSE(modeler.getAxialBounds(static_cast<srModeler::e_axis>(-1)));
    }
    struct alignas(8) WordPair
    {
        unsigned int padding;
        srQuadWord value;
    } words{0, {7, 1}};
    static_assert(offsetof(WordPair, value) == 4, "exercise four-byte aligned word pair");
    REQUIRE(double(words.value) == 4294967303.0);
    for (int mode : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
    {
        REQUIRE(fesetround(mode) == 0);
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
            REQUIRE((actual == retail && invalid == bool(status & 1)));
#else
            if (!std::isfinite(input) || input >= 2147483648.0 || input < -2147483648.0)
            {
                REQUIRE((actual == (-2147483647 - 1) && invalid));
            }
#endif
        }
    }
    REQUIRE(fesetround(FE_TONEAREST) == 0);
    REQUIRE(srFloatToInt(std::numeric_limits<float>::quiet_NaN()) == (-2147483647 - 1));
    srAssertSetFunc(assertion);
    REQUIRE(srInit());
    REQUIRE(surface_storage() == 0);
    REQUIRE(shared_surface_lifetimes() == 0);
    REQUIRE(shared_model_lifetimes() == 0);
    {
        const int before_destruction = destroyed_nodes;
        srScene* scene = new srScene;
        auto child = new ClientNode;
        child->setParent(scene, 0);
        REQUIRE(scene->getChildCount() == 1);
        delete scene;
        REQUIRE(destroyed_nodes == before_destruction + 1);

        auto parent_node = new ClientNode;
        auto transformed_child = new ClientNode;
        transformed_child->setParent(parent_node, 0);
        REQUIRE(transformed_child->getWorldSpaceLocation().x == 0);
        REQUIRE((!parent_node->transformDirty() && !transformed_child->transformDirty()));
        parent_node->setLocation(10, 0, 0);
        REQUIRE((parent_node->transformDirty() && transformed_child->transformDirty()));
        REQUIRE(transformed_child->getWorldSpaceLocation().x == 10);
        REQUIRE((!parent_node->transformDirty() && !transformed_child->transformDirty()));
        transformed_child->setLocation(3, 0, 0);
        REQUIRE(transformed_child->getWorldSpaceLocation().x == 13);
        auto other_parent = new ClientNode;
        other_parent->setLocation(20, 0, 0);
        transformed_child->setParent(other_parent, 1);
        REQUIRE(transformed_child->getWorldSpaceLocation().x == 13);
        other_parent->setLocation(30, 0, 0);
        REQUIRE(transformed_child->transformDirty());
        REQUIRE(transformed_child->getWorldSpaceLocation().x == 23);
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
        REQUIRE(reinterpret_cast<uintptr_t>(&vectors.values[0]) % alignof(float) != 0);
        {
            assert(std::size(vectors.values) != 0);
            auto lower = vectors.values[0].glmValue();
            auto upper = lower;
            for (std::size_t point_index = 1; point_index < std::size(vectors.values);
                 ++point_index) {
                const auto& point = vectors.values[point_index];
                const auto v = point.glmValue();
                // Keep the comparison order for NaNs in recovered bounds.
                for (int c = 0; c < 3; ++c) {
                    if (v[c] >= lower[c]) {
                        if (v[c] > upper[c])
                            upper[c] = v[c];
                    } else
                        lower[c] = v[c];
                }
            }
            (vectors.minimum) = lower;
            (vectors.maximum) = upper;
        }
        REQUIRE((vectors.minimum.x == -3 && vectors.minimum.y == -2 && vectors.minimum.z == 1));
        REQUIRE((vectors.maximum.x == 5 && vectors.maximum.y == 9 && vectors.maximum.z == 7));
        // File names own their text, including an aliased setter argument.
        ClientTextureFile texture;
        texture.setFileName("first.bmp");
        texture.setFileName("second.bmp");
        REQUIRE(texture.getFileName() == "second.bmp");
        texture.setFileName(texture.getFileName());
        REQUIRE(texture.getFileName() == "second.bmp");
        texture.setFileName({});
        REQUIRE(texture.getFileName().empty());
        srCamera source, copy;
        source.setViewPlane(2, 3);
        copy = source;
        const auto before = source.getViewPlane();
        const auto after = copy.getViewPlane();
        REQUIRE((before.rectangle.left == after.rectangle.left &&
                 before.rectangle.top == after.rectangle.top &&
                 before.distance == after.distance));
        const auto normalized = copy.getNormalizedViewPlane();
        REQUIRE((normalized.left == after.rectangle.left / after.distance &&
                 normalized.bottom == after.rectangle.bottom / after.distance &&
                 normalized.right == after.rectangle.right / after.distance &&
                 normalized.top == after.rectangle.top / after.distance));
        copy.setClipRange(1000, 10);
        const auto [near_clip, far_clip] = copy.getClipRange();
        REQUIRE((near_clip == 10 && far_clip == 1000));
        copy.setEnvironmentRange(3, 9);
        const auto [near_range, far_range] = copy.getEnvironmentRange();
        REQUIRE((near_range == 3 && far_range == 9));
        copy.setEnvironmentScale(0.5f, 2);
        const auto [near_scale, far_scale] = copy.getEnvironmentScale();
        REQUIRE((near_scale == 0.5f && far_scale == 2));
        srClipPlane plane, plane_copy;
        glm::vec4 plane_value;
        plane_value.x = 1;
        plane_value.y = 2;
        plane_value.z = 3;
        plane_value.w = 4;
        plane.setClipPlane(plane_value);
        plane_copy = plane;
        REQUIRE(plane_copy.getClipPlane().w == 4);
        srFog fog, fog_copy;
        fog.setDensity(0.25f);
        fog_copy = fog;
        REQUIRE(fog_copy.getDensity() == 0.25f);
        ClientMaterial material, material_copy;
        glm::vec4 emissive;
        emissive.x = 0.25f;
        emissive.y = 0.5f;
        emissive.z = 0.75f;
        emissive.w = 1;
        material.setEmissive(emissive);
        material_copy.srMaterial::operator=(material);
        REQUIRE(material_copy.getEmissive().z == 0.75f);
        // Copied lights must own independent registry entries and scene links.
        auto parent = new ClientNode;
        auto original = new srLight(parent);
        auto sibling = new srLight(parent);
        auto clone = new srLight(*original);
        REQUIRE(clone->getParent() != parent);
        clone->setParent(parent, 0);
        delete original;
        REQUIRE((parent->getChildCount() == 2 && clone->getParent() == parent &&
              sibling->getParent() == parent));
        delete parent; // Recursively releases the sibling and copied light.
        srHuffman::Sampler sampler;
        sampler.insert(10);
        sampler.insert(10);
        sampler.insert(11);
        REQUIRE((sampler.getNumSymbols() == 2 && sampler.getSymbolFrequency(0) == 2));
        // Exercise a branching symbol tree, as used by octree alpha bits.
        const w8_ulong values[] = {10, 10, 11, 12, 13, 10, 13, 14, 0xffffffff};
        srHuffman::Sampler tree_sampler;
        for (auto value : values)
            tree_sampler.insert(value);
        srHuffman::Compressor compressor(tree_sampler);
        auto encoded = srIO::own(SDL_IOFromDynamicMem());
        {
            srHuffman::BitOStream bits(encoded.get());
            bits.put(compressor.num_symbols, 32);
            bits.put(compressor.code_width, 6);
            bits.put(sizeof(values) / sizeof(values[0]), 32);
            compressor.storeSymbolTable(bits);
            for (auto value : values)
                compressor.compressSymbol(bits, value);
            bits.flush();
        }
        // Captured from main's pre-consolidation stream/Huffman implementation.
        const unsigned char golden[] = {
            0x06,0x00,0x00,0x00,0x60,0x02,0x00,0x00,0x00,0x2e,0x00,0x00,0x00,
            0x64,0x00,0x00,0x00,0xd0,0x01,0x00,0x00,0xe0,0xff,0xff,0xff,0xbf,
            0x0d,0x00,0x00,0x00,0x15,0x00,0x00,0x00,0x1e,0xec,0x64
        };
        REQUIRE(SDL_GetIOSize(encoded.get()) == sizeof(golden));
        REQUIRE(memcmp(SDL_GetPointerProperty(SDL_GetIOProperties(encoded.get()),
                     SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr), golden, sizeof(golden)) == 0);
        auto input = srIO::own(SDL_IOFromConstMem(SDL_GetPointerProperty(SDL_GetIOProperties(encoded.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr), SDL_GetIOSize(encoded.get())));
        srHuffman::BitIStream bits(input.get());
        srHuffman::Decompressor decoder(bits);
        REQUIRE(decoder.getDataCount() == sizeof(values) / sizeof(values[0]));
        for (auto value : values)
            REQUIRE(decoder.decompressSymbol() == value);
    }
    srAssertFail("native probe", "imports_test.cpp", 42, "value %d", 7);
    REQUIRE(assertions == 1);
    srExit();
    srAssertSetFunc(nullptr);
    puts("ok: native renderer consumer imports, generated special members and variadic assertion");

}
