/* Native consumer-side linking and lifetime for the renderer contracts used
   by the game, including compiler-generated members imported on Windows. */
#include "surrender/srCamera.h"
#include "surrender/srClipPlane.h"
#include "surrender/srFog.h"
#include "surrender/srHuffman.h"
#include "surrender/srMaterial.h"
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
        srHuffman::Sampler sampler;
        sampler.insert(10);
        sampler.insert(10);
        sampler.insert(11);
        CHECK(sampler.getNumSymbols() == 2 && sampler.getSymbolFrequency(0) == 2);
    }
    srAssertFail("native probe", "imports_test.cpp", 42, "value %d", 7);
    CHECK(assertions == 1);
    srExit();
    srAssertSetFunc(nullptr);
    puts("ok: native renderer consumer imports, generated special members and variadic assertion");
}
