#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "catch2/catch_message.hpp"
#include "compat/compiler.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "surrender/mesh_resources.h"
#include "surrender/srFog.h"
#include "surrender/srGERD.h"
#include "surrender/srLight.h"
#include "surrender/srMaterial.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srVertexProcessor.h"

namespace {
class TestNode : public srNode {
public:
    explicit TestNode(srNode* parent = nullptr) : srNode(parent) {}
    ~TestNode() override { ++destroyed; }
    TestNode& operator=(const TestNode&) = default;
    static inline int destroyed = 0;
};

bool sceneRelationships()
{
    TestNode first;
    TestNode second;
    first.setLocation(10, 0, 0);
    second.setLocation(-10, 0, 0);
    auto child = std::make_unique<TestNode>(&first);
    child->setLocation(3, 0, 0);
    const auto world = child->getWorldSpaceLocation();
    REQUIRE(child->setParent(&second, 1));
    REQUIRE(child->getParent() == &second);
    REQUIRE(std::abs(child->getWorldSpaceLocation().x - world.x) < 1e-6);
    REQUIRE(!second.setParent(child.get(), 0));
    REQUIRE(!child->setParent(child.get(), 0));
    REQUIRE(child->getParent() == &second);
    REQUIRE(child->setParent(nullptr, 1));
    REQUIRE(child->getParent() == nullptr);
    REQUIRE(std::abs(child->getWorldSpaceLocation().x - world.x) < 1e-6);
    *child = first;
    REQUIRE(child->getParent() == nullptr);
    REQUIRE(child->setParent(&first, 0));

    const int before = TestNode::destroyed;
    {
        TestNode parent;
        auto* branch = parent.adoptChild(std::make_unique<TestNode>());
        branch->adoptChild(std::make_unique<TestNode>());
    }
    REQUIRE(TestNode::destroyed == before + 3);
    return true;
}

class TestMaterial : public srMaterial {
public:
    srVertexProcessor::MaterialInfo getMaterialInfo() override
    {
        srVertexProcessor::MaterialInfo info{};
        info.diffuse = info.ambient = info.specular = info.emissive = glm::vec4(0.0f);
        info.translucency = info.shininess = info.value_38 = info.fog_scale = 0.0f;
        info.disabled_channels = 0xffffffff;
        return info;
    }
    void preProcess(srVertexPipe&) override {}
    void postProcess(srVertexPipe&) override {}
};

class Counter : public srVertexProcessor {
public:
    bool enabled = true;
    int selections = 0;
    int calls = 0;
    int isActive(srVertexPipe&) override
    {
        ++selections;
        return enabled;
    }
    void process(srVertexPipe&) override { ++calls; }
};

class ScratchProbe : public Counter {
public:
    bool valid = true;
    float z_bias = 0;
    float* first_alpha = nullptr;
    void process(srVertexPipe& pipe) override
    {
        ++calls;
        const auto indices = pipe.getAVT();
        const auto normals = pipe.getEyeSpaceNormal();
        const auto directions = pipe.getEyeSpaceDir();
        const auto distances = pipe.getEyeSpaceDist();
        const auto z_distances = pipe.getEyeSpaceZDist();
        const auto alpha = pipe.getAlpha();
        const auto fog = pipe.getFog();
        valid &= directions.size() == pipe.getVertexCount() &&
                 distances.size() == directions.size() && z_distances.size() == directions.size() &&
                 normals.size() == directions.size() && alpha.size() == directions.size() &&
                 fog.size() == directions.size() && indices.size() == directions.size();
        if (indices[0] == 0) {
            first_alpha = alpha.data();
            valid &= reinterpret_cast<std::uintptr_t>(alpha.data()) % 16 == 0;
            valid &= reinterpret_cast<std::uintptr_t>(normals.data()) % 16 == 0;
        }
        // First request is deliberately in a later material sub-batch. The
        // cache must fill the whole batch, not overread from that offset.
        const auto depth = indices[0] == 0 ? std::span<const float>{} : pipe.getDepthCue();
        for (w8_ulong i = 0; i < pipe.getVertexCount(); ++i) {
            const float z = static_cast<float>(indices[i] + 1) + z_bias;
            valid &= normals[i].x == 0 && normals[i].y == 0 && normals[i].z == -1;
            valid &= std::abs(directions[i].z - 1) < 1e-5f;
            valid &= std::abs(distances[i] - z) < 1e-5f;
            valid &= z_distances[i] == z;
            valid &= alpha[i] == 1 && fog[i] == 0;
            if (!depth.empty()) {
                valid &= std::abs(depth[i] - (1 - z / 100)) < 1e-5f;
            }
            alpha[i] = 0.25f;
            fog[i] = 0.75f;
        }
    }
};

bool processorOwnership()
{
    auto first_material = std::make_shared<TestMaterial>();
    auto second_material = std::make_shared<TestMaterial>();
    ScratchProbe probe;
    Counter active;
    Counter inactive;
    inactive.enabled = false;
    std::array<glm::vec3, 65> positions;
    std::array<w8_ulong, 65> indices;
    MeshResources<srMaterial> materials;
    materials.resize(65);
    std::array<glm::vec4, 65> eye_locations;
    std::array<unsigned char, 65> attributes{};
    for (w8_ulong i = 0; i < positions.size(); ++i) {
        positions[i] = glm::vec3(0, 0, static_cast<float>(i + 1));
        indices[i] = i;
        materials.set(i, i == 0 ? first_material : second_material);
    }
    glm::mat4 identity;
    identity = glm::mat4(1);
    srVertexArray arrays{};
    arrays.eye_locations = eye_locations.data();
    arrays.attributes = attributes.data();
    srVertexPipe::Record record{};
    record.flags = srVertexPipe::Record::HAS_VERTEX_MATERIALS;
    record.channels = 0xffffffff;
    record.materials = &materials;
    std::array<srVertexProcessor*, 3> processors{&probe, &active, &inactive};
    srVertexPipe::Input input(identity, identity);
    input.active_vertices = indices;
    input.direct_vertex_indices = 1;
    input.positions = positions;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};
    input.processors = processors;
    input.environment_maximum = 100;
    input.environment_inverse_scale = 1;
    {
        srVertexPipe pipe;
        srVertexPipe other;
        pipe.process(input);
        REQUIRE((probe.valid && probe.calls == 3 && active.calls == 3 && inactive.calls == 0));
        const float* first_scratch = probe.first_alpha;
        other.process(input);
        REQUIRE((probe.valid && probe.first_alpha != first_scratch));
        for (auto& position : positions) {
            position.z += 2;
        }
        probe.z_bias = 2;
        pipe.process(input); // all lazy batch flags and alpha/fog must reset
        REQUIRE((probe.valid && probe.calls == 9));

        std::vector<srVertexProcessor*> many(129, &active);
        input.processors = many;
        pipe.process(input);
        REQUIRE(active.calls == 9 + 129 * 3);
        active.enabled = false;
        pipe.process(input);
        REQUIRE(active.calls == 9 + 129 * 3);
        input.processors = {};
        pipe.process(input); // no retained active pointers after shrinking to zero
        REQUIRE(active.calls == 9 + 129 * 3);
        input.vertex_arrays = {};
        REQUIRE_THROWS_AS(pipe.process(input), std::invalid_argument);
        input.active_vertices = {};
        REQUIRE_NOTHROW(pipe.process(input));
    }
    // The pipe owns the pointer list, never the processors.
    active.enabled = true;
    srVertexPipe empty;
    REQUIRE(active.isActive(empty));
    return true;
}

class SeedMaterialChannels : public srVertexProcessor {
public:
    int isActive(srVertexPipe&) override { return 1; }
    void process(srVertexPipe& pipe) override
    {
        const auto diffuse = pipe.getDiffuse();
        const auto specular = pipe.getSpecular();
        const auto alpha = pipe.getAlpha();
        for (w8_ulong i = 0; i < pipe.getVertexCount(); ++i) {
            diffuse[i] = glm::vec4(0.25f);
            specular[i] = glm::vec4(0.5f);
            alpha[i] = 0.8f;
        }
    }
};

class CoverageMaterial : public TestMaterial {
public:
    float scale = 1;
    srVertexProcessor::MaterialInfo getMaterialInfo() override
    {
        auto info = TestMaterial::getMaterialInfo();
        info.diffuse.w = info.fog_scale = scale;
        return info;
    }
};

class SeedCoverage : public srVertexProcessor {
public:
    int isActive(srVertexPipe&) override { return 1; }
    void process(srVertexPipe& pipe) override
    {
        const auto diffuse = pipe.getDiffuse();
        const auto specular = pipe.getSpecular();
        const auto alpha = pipe.getAlpha();
        const auto fog = pipe.getFog();
        const auto indices = pipe.getAVT();
        const float values[] = {-2.f, 0.f, .25f, 1.f, 2.f};
        for (w8_ulong index = 0; index < pipe.getVertexCount(); ++index) {
            diffuse[index] = specular[index] = glm::vec4(.5f);
            alpha[index] = fog[index] = values[indices[index] % 5];
        }
    }
};

bool coverageFinalization()
{
    std::array<glm::vec3, 65> positions;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> eye, diffuse, specular;
    std::array<unsigned char, 65> attributes{};
    for (unsigned index = 0; index < positions.size(); ++index) {
        positions[index] = glm::vec3(0, 0, float(index + 1));
        indices[index] = index;
    }
    srVertexArray arrays{};
    arrays.eye_locations = eye.data();
    arrays.diffuse = diffuse.data();
    arrays.specular = specular.data();
    arrays.attributes = attributes.data();
    glm::mat4 identity(1);
    SeedCoverage seed;
    srVertexProcessor* processor = &seed;
    CoverageMaterial material;
    srVertexPipe::Record record{};
    record.material = &material;
    srVertexPipe::Input input(identity, identity);
    input.active_vertices = indices;
    input.direct_vertex_indices = 1;
    input.positions = positions;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};
    input.processors = {&processor, 1};
    input.environment_maximum = input.environment_inverse_scale = 1;
    const float values[] = {-2.f, 0.f, .25f, 1.f, 2.f};
    for (unsigned channels = 1; channels < 16; ++channels) {
        unsigned enabled = 0;
        const srVertexProcessor::e_channel kinds[] = {
            srVertexProcessor::CHANNEL_DIFFUSE, srVertexProcessor::CHANNEL_ALPHA,
            srVertexProcessor::CHANNEL_SPECULAR, srVertexProcessor::CHANNEL_FOG};
        for (unsigned bit = 0; bit < 4; ++bit)
            if (channels & (1u << bit)) enabled |= 1u << kinds[bit];
        record.channels = ~enabled;
        for (float scale : {-2.f, 0.f, .5f, 1.f, 2.f}) {
            material.scale = scale;
            srVertexPipe pipe;
            pipe.process(input);
            for (unsigned index = 0; index < positions.size(); ++index) {
                const float source = values[index % 5];
                // With colour enabled, clamp the final scaled component; without
                // it, clamp coverage first and retain the scaled alpha as-is.
                const float clamped = source < 0 ? 0 : source > 1 ? 1 : source;
                auto clamp = [](float value) { return value < 0 ? 0 : value > 1 ? 1 : value; };
                const float expected_alpha =
                    channels & 2 ? channels & 1 ? clamp(scale * source) : scale * clamped : .5f;
                const float expected_fog =
                    channels & 8 ? channels & 4 ? clamp(scale * source) : scale * clamped : .5f;
                CAPTURE(channels, scale, index);
                REQUIRE(diffuse[index].w == expected_alpha);
                REQUIRE(specular[index].w == expected_fog);
            }
        }
    }
    return true;
}

class SurfaceLightProbe : public Counter {
public:
    srLight light;
    unsigned channels = 0, ready = 0;
    unsigned attenuation_kind = 0;
    bool directional = false;
    void process(srVertexPipe& pipe) override
    {
        ++calls;
        const unsigned count = pipe.getVertexCount();
        if (ready & 1) {
            const auto colors = pipe.getDiffuse();
            for (unsigned i = 0; i < count; ++i) colors[i] = glm::vec4(.25f);
        }
        if (ready & 2) {
            const auto colors = pipe.getSpecular();
            for (unsigned i = 0; i < count; ++i) colors[i] = glm::vec4(.5f);
        }

        light.process(pipe);
        const float attenuation = directional || attenuation_kind == 0 ? 1.f
                                  : attenuation_kind == 4              ? .25f
                                                                       : .5f;
        if (channels & 1) {
            const auto colors = pipe.getDiffuse();
            const float expected = (ready & 1 ? .25f : .125f) + .25f * attenuation;
            for (unsigned i = 0; i < count; ++i)
                for (unsigned c = 0; c < 4; ++c) REQUIRE(colors[i][c] == expected);
        }
        if (channels & 2) {
            const auto colors = pipe.getSpecular();
            const float expected = (ready & 2 ? .5f : 0.f) + .0625f * attenuation;
            for (unsigned i = 0; i < count; ++i)
                for (unsigned c = 0; c < 4; ++c) REQUIRE(colors[i][c] == expected);
        }
    }
};

class LightingMaterial : public TestMaterial {
public:
    srVertexProcessor::MaterialInfo getMaterialInfo() override
    {
        auto info = TestMaterial::getMaterialInfo();
        info.disabled_channels = 0;
        info.diffuse = glm::vec4(.5f);
        info.specular = glm::vec4(.25f);
        info.emissive = glm::vec4(.125f);
        info.shininess = 1;
        return info;
    }
};

bool surfaceLighting()
{
    std::array<glm::vec3, 65> positions, normals;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> diffuse, specular, eye;
    std::array<unsigned char, 65> attributes{};
    for (unsigned i = 0; i < 65; ++i) {
        positions[i] = glm::vec3(0, 0, -10);
        normals[i] = glm::vec3(0, 0, 1);
        indices[i] = i;
    }
    srVertexArray arrays{};
    arrays.diffuse = diffuse.data();
    arrays.specular = specular.data();
    arrays.eye_locations = eye.data();
    arrays.attributes = attributes.data();
    LightingMaterial material;
    srVertexPipe::Record record{};
    record.material = &material;
    glm::mat4 identity(1);
    SurfaceLightProbe probe;
    srVertexProcessor* processor = &probe;
    srVertexPipe::Input input(identity, identity);
    input.positions = positions;
    input.normals = normals;
    input.active_vertices = indices;
    input.direct_vertex_indices = 1;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};

    input.processors = {&processor, 1};
    input.ambient_light = glm::vec4(0);
    for (unsigned channels = 1; channels < 4; ++channels)
        for (unsigned ready = 0; ready < 4; ++ready)
            for (bool directional : {false, true})
                for (unsigned attenuation_kind : {0u, 1u, 2u, 3u, 4u})
                    for (unsigned count : {0u, 1u, 65u}) {
                        CAPTURE(channels, ready, directional, attenuation_kind, count);
                        probe.channels = channels;
                        probe.ready = ready;
                        probe.directional = directional;
                        probe.attenuation_kind = attenuation_kind;
                        auto& light = probe.light;
                        light.channel_mask =
                            (channels & 1 ? 1u << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE : 0) |
                            (channels & 2 ? 1u << srVertexProcessor::CHANNEL_SPECULAR : 0);
                        light.derived_flags = directional ? srLight::DERIVED_DIRECTIONAL
                                              : attenuation_kind == 1
                                                  ? srLight::DERIVED_OPENGL_ATTENUATION |
                                                        srLight::DERIVED_CONSTANT_ATTENUATION
                                                  : 0;
                        light.eye_location = directional ? glm::vec3(0, 0, 1) : glm::vec3(0);
                        light.attenuation_model = attenuation_kind == 0 ? srLight::ATTENUATION_NONE
                                                  : attenuation_kind == 1
                                                      ? srLight::ATTENUATION_OPENGL
                                                      : srLight::ATTENUATION_3DSTUDIO_MAX;
                        light.enable_flags = (attenuation_kind == 2 || attenuation_kind == 4
                                                  ? 1u << srLight::ENABLE_RANGE_NEAR
                                                  : 0) |
                                             (attenuation_kind == 3 || attenuation_kind == 4
                                                  ? 1u << srLight::ENABLE_RANGE_FAR
                                                  : 0);
                        light.scaled_near_start = 0;
                        light.scaled_far_end = 20;
                        light.near_attenuation = light.far_attenuation = .05f;
                        light.opengl_attenuation = glm::vec3(2, 0, 0);
                        light.scaled_diffuse = glm::vec4(.5f);
                        light.scaled_specular = glm::vec4(.25f);
                        input.active_vertices = std::span{indices}.first(count);
                        record.channels =
                            ~(light.channel_mask | (1u << srVertexProcessor::CHANNEL_DIFFUSE));
                        const int before = probe.calls;
                        srVertexPipe pipe;
                        pipe.process(input);
                        REQUIRE(probe.calls - before == (count == 0 ? 0 : count == 65 ? 2 : 1));
                    }
    return true;
}

class ProfileMaterial : public TestMaterial {
public:
    srVertexProcessor::MaterialInfo getMaterialInfo() override
    {
        auto info = TestMaterial::getMaterialInfo();
        info.disabled_channels = 0;
        info.fog_scale = 1;
        return info;
    }
};

class DistanceProfileProbe : public Counter {
public:
    srFog* fog = nullptr;
    float minimum = 0, maximum = 100, low = .2f, high = .8f;
    bool valid = true;
    void process(srVertexPipe& pipe) override
    {
        ++calls;
        const auto indices = pipe.getAVT();
        const auto cue = pipe.getDepthCue();
        for (unsigned index = 0; index < pipe.getVertexCount(); ++index) {
            const float distance = float(indices[index] + 1);
            float expected;
            if (minimum == maximum)
                expected = distance < minimum ? 1 - low : 1 - high;
            else {
                float normalized = (maximum - distance) / (maximum - minimum);
                normalized = normalized < 0 ? 0 : normalized > 1 ? 1 : normalized;
                expected = low + (high - low) * normalized;
            }
            valid &= std::abs(cue[index] - expected) < 1e-6f;
        }
        fog->process(pipe);
    }
};

bool distanceProfiles()
{
    std::array<glm::vec3, 65> positions;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> eye, specular;
    std::array<unsigned char, 65> attributes{};
    for (unsigned index = 0; index < positions.size(); ++index) {
        positions[index] = glm::vec3(0, 0, float(index + 1));
        indices[index] = index;
    }
    srVertexArray arrays{};
    arrays.eye_locations = eye.data();
    arrays.specular = specular.data();
    arrays.attributes = attributes.data();
    glm::mat4 identity(1);
    ProfileMaterial material;
    srVertexPipe::Record record{};
    record.material = &material;
    record.channels = ~(1u << srVertexProcessor::CHANNEL_FOG);
    srVertexPipe::Input input(identity, identity);
    input.active_vertices = indices;
    input.direct_vertex_indices = 1;
    input.positions = positions;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};
    input.eye_bounds = {glm::vec3(0, 0, 33), 33};
    input.environment_scale = .2f;
    input.environment_inverse_scale = .8f;
    DistanceProfileProbe probe;
    srVertexProcessor* processor = &probe;
    input.processors = {&processor, 1};
    srFog fog;
    probe.fog = &fog;
    for (auto range : {std::pair{0.f, 100.f}, std::pair{17.f, 43.f}, std::pair{32.f, 32.f}}) {
        input.environment_minimum = probe.minimum = range.first;
        input.environment_maximum = probe.maximum = range.second;
        fog.setRange(range.first, range.second);
        for (float density : {0.f, .5f, 1.f}) {
            fog.setDensity(density);
            srVertexPipe pipe;
            pipe.process(input);
            REQUIRE(probe.valid);
            for (unsigned index = 0; index < positions.size(); ++index) {
                const float distance = float(index + 1);
                float normalized = range.first == range.second
                                       ? distance <= range.first ? 0 : 1
                                       : (distance - range.first) / (range.second - range.first);
                normalized = normalized < 0 ? 0 : normalized > 1 ? 1 : normalized;
                REQUIRE(std::abs(specular[index].w - density * normalized) < 1e-6f);
            }
        }
    }
    REQUIRE(probe.calls == 18); // Two batches for each range/density combination.
    return true;
}

bool materialChannelOperations()
{
    // Cross the 64-vertex batch boundary for every combination of material operations.
    std::array<glm::vec3, 65> positions;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> eye, diffuse, specular;
    std::array<unsigned char, 65> attributes{};
    for (unsigned i = 0; i < positions.size(); ++i) {
        positions[i] = glm::vec3(0, 0, float(i + 1));
        indices[i] = i;
    }
    srVertexArray arrays{};
    arrays.eye_locations = eye.data();
    arrays.diffuse = diffuse.data();
    arrays.specular = specular.data();
    arrays.attributes = attributes.data();
    glm::mat4 identity(1);
    SeedMaterialChannels seed;
    srVertexProcessor* processor = &seed;
    srMaterial material;
    material.setSpecular(glm::vec4(1));
    srVertexPipe::Record record{};
    record.material = &material;
    record.channels =
        ~((1u << srVertexProcessor::CHANNEL_DIFFUSE) | (1u << srVertexProcessor::CHANNEL_SPECULAR) |
          (1u << srVertexProcessor::CHANNEL_ALPHA) |
          (1u << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE) |
          (1u << srVertexProcessor::CHANNEL_LIGHT_AMBIENT));
    srVertexPipe::Input input(identity, identity);
    input.active_vertices = indices;
    input.direct_vertex_indices = 1;
    input.positions = positions;
    input.vertex_arrays = {&arrays, 1};
    input.records = {&record, 1};
    input.processors = {&processor, 1};
    input.environment_maximum = 100;
    input.environment_inverse_scale = 1;
    srVertexPipe pipe;
    for (unsigned operations = 0; operations < 32; ++operations) {
        for (unsigned bit = 0; bit < 5; ++bit) {
            auto operation = static_cast<srMaterial::e_oper>(bit);
            if (operations & (1u << bit))
                material.enable(operation);
            else
                material.disable(operation);
        }
        pipe.process(input);
        float diffuse_value = 0.25f, specular_value = 0.5f;
        if (operations & (1u << srMaterial::OPER_DIFFUSE_SPECULAR)) {
            std::swap(diffuse_value, specular_value);
        } else if (operations & (1u << srMaterial::OPER_DIFFUSE)) {
            specular_value = diffuse_value;
        } else if (operations & (1u << srMaterial::OPER_SPECULAR)) {
            diffuse_value = specular_value;
        }
        for (unsigned i = 0; i < positions.size(); ++i) {
            CAPTURE(operations, i);
            const float depth = 1 - float(i + 1) / 100;
            const float color_scale = operations & (1u << srMaterial::OPER_DEPTH_CUE) ? depth : 1;
            const float alpha_scale = operations & (1u << srMaterial::OPER_ALPHA) ? depth : 1;
            REQUIRE(std::abs(diffuse[i].x - diffuse_value * color_scale) < 1e-6f);
            REQUIRE(std::abs(specular[i].x - specular_value * color_scale) < 1e-6f);
            REQUIRE(std::abs(diffuse[i].w - 0.8f * alpha_scale) < 1e-6f);
        }
    }
    return true;
}

bool rendererOwnership()
{
    // These checks exercise CPU ownership without a window or GPU context.
    srGERD gerd;
    struct TransformInstance : srModelInstance {
        using srModelInstance::scopedRenderTransform;
    };
    TransformInstance instance;
    instance.setLocation(3, 4, 5);
    instance.setScale(glm::dvec3(2, 3, 4));
    for (float handedness : {-1.0f, 1.0f}) {
        const auto view = glm::scale(glm::translate(glm::mat4(1), glm::vec3(7, 8, 9)),
                                     glm::vec3(handedness * 2, 3, 4));
        for (bool aligned : {false, true}) {
            instance.setAlignAngle(0.5);
            instance.setAlignAxis(glm::vec3(0, 0, 1));
            instance.setAlignment(aligned);
            gerd.matrixMode(srGERD::MATRIX_MODELVIEW);
            gerd.loadMatrix(view);
            {
                const auto transform = instance.scopedRenderTransform(gerd);
                glm::mat4 expected;
                if (aligned) {
                    expected =
                        glm::translate(glm::mat4(1), glm::vec3(view * glm::vec4(3, 4, 5, 1)));
                    expected = glm::rotate(expected, 0.5f, glm::vec3(0, 0, 1));
                    const float sign = handedness > 0 ? -1 : 1;
                    expected = glm::scale(expected, sign * glm::vec3(4, 9, -16));
                } else {
                    expected = view * instance.getWorldSpaceMatrix<glm::mat4>();
                }
                const auto actual = gerd.getMatrix(srGERD::MATRIX_MODELVIEW);
                for (int column = 0; column < 4; ++column)
                    REQUIRE(glm::length(actual[column] - expected[column]) < 1e-5f);
            }
            REQUIRE(gerd.getMatrix(srGERD::MATRIX_MODELVIEW) == view);
        }
    }
    auto* immediate = &gerd.batchRenderer();
    immediate->submit();
    REQUIRE(immediate->getStatistics().device_calls == 1);
    gerd.toggle(srGERD::ENABLE_SORTED_RENDERING);
    auto* renderer = &gerd.batchRenderer();
    REQUIRE(renderer != immediate);
    renderer->submit();
    REQUIRE(renderer->getStatistics().device_calls == 1);
    gerd.resetStatistics();
    REQUIRE(immediate->getStatistics().device_calls == 0);
    REQUIRE(renderer->getStatistics().device_calls == 0);
    gerd.toggle(srGERD::ENABLE_SORTED_RENDERING);
    REQUIRE(&gerd.batchRenderer() == immediate);

    renderer->indices.alloc(2);
    renderer->vertices.alloc(3);
    const auto index_capacity = renderer->indices.triangles.capacity();
    const auto vertex_capacity = renderer->vertices.positions.capacity();
    renderer->reset(0);
    REQUIRE((renderer->indices.count == 0 && renderer->vertices.count == 0));
    REQUIRE(renderer->indices.triangles.capacity() == index_capacity);
    REQUIRE(renderer->vertices.positions.capacity() == vertex_capacity);
    renderer->reset(1); // real eviction remains distinct from ordinary reset
    REQUIRE(renderer->indices.triangles.capacity() == 0);
    REQUIRE(renderer->vertices.positions.capacity() == 0);
    return true;
}

bool rendererTextureStages()
{
    srGERD gerd;
    for (unsigned stages : {1u, 2u}) {
        srGERD::Renderer renderer({&gerd, 0, 256, stages});
        auto arrays = renderer.allocVertexArray(8);
        for (unsigned stage = 0; stage < 2; ++stage)
            for (unsigned i = 0; i < 8; ++i) {
                renderer.vertices.st[stage][i] = glm::vec2(10 * stage + i, 20 + i);
                renderer.vertices.q[stage][i] = 0.25f * (stage + i + 1);
            }
        for (unsigned count : {1u, 3u, 4u, 5u, 8u}) {
            for (unsigned lane = 0; lane < count; ++lane) {
                for (unsigned attributes = 0; attributes < 128; ++attributes) {
                    std::fill(renderer.vertices.attributes.begin(),
                              renderer.vertices.attributes.end(), 0);
                    renderer.vertices.attributes[lane] = attributes;
                    renderer.programVertexArrays(&arrays, count);
                    const auto mask = gerd.getVertexArrayMask();
                    for (unsigned stage = 0; stage < 2; ++stage) {
                        const bool enabled = stage < stages &&
                                             (attributes & (srVertexArray::ATTRIBUTE_ST0 << stage));
                        REQUIRE(bool(mask & (1u << (srRendererDefs::VERTEX_ARRAY_TEXCOORD0 +
                                                    stage))) == enabled);
                    }
                }
            }
        }
    }
    return true;
}

bool rendererCornerRemapping()
{
    // All combinations of reused/new corners, mixed UV and flat passes, and
    // a nonzero batch base exercise allocation order and copied vertex streams.
    for (unsigned changed = 0; changed < 8; ++changed) {
        for (int sorted : {0, 1}) {
            srGERD::Renderer renderer({nullptr, sorted, 256, 2});
            renderer.vertices.alloc(5);
            renderer.allocVertexArray(6);
            for (unsigned i = 5; i < 11; ++i) {
                renderer.vertices.positions[i] = glm::vec4(float(i), 2, 3, 1);
                renderer.vertices.diffuse[i] = glm::vec4(float(i) / 20);
                renderer.vertices.specular[i] = glm::vec4(float(i) / 30);
                renderer.vertices.st[1][i] = glm::vec2(float(i), 4);
                renderer.vertices.q[1][i] = float(i) / 10;
                renderer.vertices.attributes[i] = i;
            }
            std::array<w8_ulong, 1> indices{0};
            std::array<w8_ulong, 3> vertices{2, 0, 1};
            std::array<glm::ivec3, 1> triangles{{{0, 1, 2}}};
            std::array<int, 3> uv;
            for (unsigned corner = 0; corner < 3; ++corner)
                uv[corner] = corner + ((changed & (1u << corner)) ? 3 : 0);
            std::array<glm::ivec3, 1> poly_uv{{{uv[0], uv[1], uv[2]}}};
            std::array<glm::vec2, 6> texcoords;
            for (unsigned i = 0; i < texcoords.size(); ++i) texcoords[i] = glm::vec2(i, i + 10);
            std::array<srTriMeshPipeline::Pass, 2> passes{};
            passes[0].poly_uv = poly_uv.data();
            passes[0].texcoords = texcoords.data();
            srGERD::Renderer::TriInput input{};
            input.record_count = 2;
            input.vertex_count = 3;
            input.indices = indices;
            input.vertices = vertices;
            input.triangles = triangles;
            input.passes = passes.data();
            renderer.expandTriangles(input, sorted);
            const auto& result = renderer.indices.triangles[0];
            const std::array corners{result.x, result.y, result.z};
            int next = 11;
            for (unsigned corner = 0; corner < 3; ++corner) {
                const int source = 5 + static_cast<int>(vertices[corner]);
                if ((changed & (1u << corner)) == 0) {
                    REQUIRE(corners[corner] == source);
                    continue;
                }
                REQUIRE(corners[corner] == next);
                REQUIRE(renderer.vertices.positions[next] == renderer.vertices.positions[source]);
                REQUIRE(renderer.vertices.diffuse[next] == renderer.vertices.diffuse[source]);
                REQUIRE(renderer.vertices.specular[next] == renderer.vertices.specular[source]);
                REQUIRE(renderer.vertices.st[1][next] == renderer.vertices.st[1][source]);
                REQUIRE(renderer.vertices.q[1][next] == renderer.vertices.q[1][source]);
                REQUIRE(renderer.vertices.attributes[next] == renderer.vertices.attributes[source]);
                REQUIRE(renderer.vertices.st[0][next] == texcoords[uv[corner]]);
                REQUIRE(renderer.vertices.q[0][next] == 1.0f);
                ++next;
            }
            REQUIRE(renderer.vertices.count == next);
            const auto& flat = renderer.indices.triangles[1];
            REQUIRE((flat.x == 10 && flat.y == 8 && flat.z == 9));
            if (sorted) REQUIRE(renderer.indices.sort_key[0] == renderer.indices.sort_key[1]);
        }
    }
    return true;
}

bool pipelineRecordTexcoords()
{
    // Consume the producer's actual records, including a second record and
    // both UV channels. A layout/stride mismatch used to crash menu rendering.
    srGERD renderer;
    auto* pipeline = &renderer.meshPipeline();
    std::array<glm::vec3, 3> positions{{{0, 0, 1}, {1, 0, 1}, {0, 1, 1}}};
    std::array<w8_ulong, 3> indices{2, 0, 1};
    glm::mat4 identity;
    identity = glm::mat4(1);
    std::array<std::array<glm::vec2, 3>, 4> source;
    std::array<std::array<glm::vec2, 3>, 4> output;
    std::array<std::array<float, 3>, 4> q;
    std::array<std::array<glm::vec4, 3>, 2> eyes;
    std::array<std::array<unsigned char, 3>, 2> attributes;
    std::array<srVertexArray, 2> arrays{};
    for (unsigned record = 0; record < arrays.size(); ++record) {
        auto& slot = *pipeline->current_record;
        slot.flags = srVertexPipe::Record::HAS_TEXCOORD0 | srVertexPipe::Record::HAS_TEXCOORD1;
        slot.channels =
            ~((1u << srVertexProcessor::CHANNEL_ST0) | (1u << srVertexProcessor::CHANNEL_ST1));
        for (unsigned layer = 0; layer < 2; ++layer) {
            const unsigned channel = record * 2 + layer;
            for (unsigned vertex = 0; vertex < positions.size(); ++vertex)
                source[channel][vertex] =
                    glm::vec2(float(10 * channel + vertex), float(20 + vertex));
            slot.st_source[layer] = source[channel].data();
        }
        arrays[record].eye_locations = eyes[record].data();
        arrays[record].attributes = attributes[record].data();
        arrays[record].st0 = output[record * 2].data();
        arrays[record].st1 = output[record * 2 + 1].data();
        arrays[record].q0 = q[record * 2].data();
        arrays[record].q1 = q[record * 2 + 1].data();
        ++pipeline->slot_count;
        pipeline->PrepareSlot();
    }
    srVertexPipe::Input input(identity, identity);
    input.active_vertices = indices;
    input.positions = positions;
    input.vertex_arrays = arrays;
    input.records = std::span{pipeline->records}.first(pipeline->slot_count);
    pipeline->vertex_pipe.process(input);
    for (unsigned channel = 0; channel < output.size(); ++channel)
        for (unsigned vertex = 0; vertex < positions.size(); ++vertex) {
            REQUIRE(output[channel][vertex].x == source[channel][indices[vertex]].x);
            REQUIRE(output[channel][vertex].y == source[channel][indices[vertex]].y);
            REQUIRE(q[channel][vertex] == 1.0f);
        }
    pipeline->Reset(&renderer);
    return true;
}

bool countedGeometryTransforms()
{
    struct Probe : Counter {
        const srVertexPipe::Input* input = nullptr;
        unsigned processed = 0;
        void process(srVertexPipe& pipe) override
        {
            const auto locations = pipe.getEyeSpaceLocation();
            const auto normals = pipe.getEyeSpaceNormal();
            const auto attributes = pipe.getAVT();
            for (unsigned index = 0; index < locations.size(); ++index) {
                const auto source = input->geometryIndex(processed + index);
                REQUIRE(locations[index] ==
                        input->model_view * glm::vec4(input->positions[source], 1));
                const auto expected =
                    input->normals.empty()
                        ? glm::vec3(0, 0, -1)
                        : glm::vec3(input->normal_matrix * glm::vec4(input->normals[source], 1));
                REQUIRE(normals[index] == expected);
                REQUIRE(attributes[index] == input->active_vertices[processed + index]);
            }
            processed += locations.size();
        }
    } probe;
    std::array<glm::vec3, 65> positions, normals;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> eyes;
    std::array<unsigned char, 65> attributes{};
    for (unsigned index = 0; index < positions.size(); ++index) {
        positions[index] = glm::vec3(index, 2, 3);
        normals[index] = glm::vec3(1, index, 2);
        indices[index] = positions.size() - index - 1;
    }
    const glm::mat4 model_view = glm::translate(glm::mat4(1), glm::vec3(4, 5, 6));
    const glm::mat4 normal_matrix = glm::scale(model_view, glm::vec3(2, 3, 4));
    srVertexPipe::Input input(model_view, normal_matrix);
    input.positions = positions;
    input.normals = normals;
    input.active_vertices = indices;
    srVertexArray array{};
    array.eye_locations = eyes.data();
    array.attributes = attributes.data();
    input.vertex_arrays = {&array, 1};
    TestMaterial material;
    srVertexPipe::Record record{};
    record.material = &material;
    record.channels = 0xffffffff;
    input.records = {&record, 1};
    srVertexProcessor* processor = &probe;
    input.processors = {&processor, 1};
    probe.input = &input;
    srVertexPipe pipe;
    for (bool direct : {false, true}) {
        input.direct_vertex_indices = direct;
        probe.processed = 0;
        pipe.process(input);
        REQUIRE(probe.processed == positions.size());
    }
    input.normals = {};
    probe.processed = 0;
    pipe.process(input);
    REQUIRE(probe.processed == positions.size());
    input.positions = std::span{positions}.first(64);
    eyes.fill(glm::vec4(-99));
    REQUIRE_THROWS_AS(pipe.process(input), std::out_of_range);
    for (const auto& eye : eyes) REQUIRE(eye == glm::vec4(-99));
    input.positions = positions;
    input.normals = std::span{normals}.first(64);
    REQUIRE_THROWS_AS(pipe.process(input), std::out_of_range);
    return true;
}

} // namespace

TEST_CASE("Scene membership preserves independent node ownership")
{
    const int before = TestNode::destroyed;
    auto owned = std::make_unique<TestNode>(nullptr);
    REQUIRE(owned->getParent() == nullptr);
    {
        TestNode scene;
        REQUIRE(owned->setParent(&scene, 0));
        REQUIRE(scene.getChildCount() == 1);
    }
    REQUIRE(TestNode::destroyed == before + 1); // Only the scene was destroyed.
    REQUIRE(owned->getParent() == nullptr);
    owned.reset();
    REQUIRE(TestNode::destroyed == before + 2);
}

TEST_CASE("Scene adoption transfers node destruction ownership")
{
    const int before = TestNode::destroyed;
    {
        TestNode scene;
        auto owned = std::make_unique<TestNode>(nullptr);
        auto* observed = scene.adoptChild(std::move(owned));
        REQUIRE_FALSE(owned);
        REQUIRE(observed->getParent() == &scene);
        REQUIRE(scene.getChildCount() == 1);
        {
            TestNode render_parent;
            REQUIRE(observed->setParent(&render_parent, 0));
            REQUIRE(scene.getChildCount() == 0);
        }
        REQUIRE(observed->getParent() == nullptr);
        REQUIRE(TestNode::destroyed == before + 1);
    }
    REQUIRE(TestNode::destroyed == before + 3);
}

TEST_CASE("renderer_ownership_test")
{
    for (int cycle = 0; cycle < 2; ++cycle) {
        REQUIRE(sceneRelationships());
        REQUIRE(processorOwnership());
        REQUIRE(countedGeometryTransforms());
        REQUIRE(rendererOwnership());
        REQUIRE(materialChannelOperations());
        REQUIRE(coverageFinalization());
        REQUIRE(distanceProfiles());
        REQUIRE(surfaceLighting());
        REQUIRE(pipelineRecordTexcoords());
        REQUIRE(rendererTextureStages());
        REQUIRE(rendererCornerRemapping());
        REQUIRE(srGERD::getActive() == nullptr);
    }
}

TEST_CASE("Picking honors counted selections, direct indices, nearest depth and atomic validation")
{
    srGERD renderer;
    srModelInstance selected;
    renderer.setPickKey(reinterpret_cast<w8_ulong_ptr>(&selected));
    REQUIRE_FALSE(renderer.popPick());
    const std::array<glm::vec4, 3> positions{{{-1, -1, .4f, 1}, {1, -1, .4f, 1}, {0, 1, .4f, 1}}};
    const std::array<glm::ivec3, 2> triangles{{{0, 0, 0}, {1, 2, 0}}};
    const std::array<w8_ulong, 1> indices{1};
    const std::array<w8_ulong, 3> remap{2, 0, 1};
    srGERD::Renderer batch({&renderer, 0, 256, 1});
    const glm::mat4 projection(1);
    const srTriMeshPipeline::Pass pass{};
    srGERD::Renderer::TriInput input{};
    input.indices = indices;
    input.triangles = triangles;
    input.vertices = remap;
    input.vertex_count = positions.size();
    input.record_count = 1;
    input.passes = &pass;
    input.project_clip_near = &projection;
    const auto performPick = [&] {
        batch.reset(0);
        batch.allocVertexArray(input.vertex_count);
        std::copy_n(positions.begin(), input.vertex_count,
                    batch.vertices.positions.begin() + batch.first_vertex);
        batch.render(input);
    };
    renderer.pushPick({{0, 0, 1}, nullptr, 0});
    renderer.pushPick({{2, 0, 1}, nullptr, 0});
    performPick();
    const auto outside = renderer.popPick();
    REQUIRE(outside->selected_model == nullptr);
    REQUIRE(outside->position.z == 1);
    const auto hit = renderer.popPick();
    REQUIRE(hit->selected_model == &selected);
    REQUIRE(hit->triangle_index == 1);
    REQUIRE(std::abs(hit->position.z - .4f) < 1e-6f);

    input.vertices = {};
    input.direct_vertex_indices = true;
    renderer.pushPick({{0, 0, .3f}, nullptr, 9});
    performPick();
    const auto nearer = renderer.popPick();
    REQUIRE(nearer->position.z == .3f);
    REQUIRE(nearer->triangle_index == 9);
    renderer.pushPick({{0, 0, 1}, nullptr, 0});
    performPick();
    REQUIRE(renderer.popPick()->selected_model == &selected);

    const std::array<w8_ulong, 2> invalid_selection{1, 2};
    input.indices = invalid_selection;
    renderer.pushPick({{0, 0, 1}, nullptr, 9});
    REQUIRE_THROWS_AS(performPick(), std::invalid_argument);
    const auto unchanged = renderer.popPick();
    REQUIRE(unchanged->position.z == 1);
    REQUIRE(unchanged->triangle_index == 9);
    input.indices = indices;
    input.vertex_count = 2;
    renderer.pushPick({{0, 0, 1}, nullptr, 9});
    REQUIRE_THROWS_AS(performPick(), std::invalid_argument);
    REQUIRE(renderer.popPick()->triangle_index == 9);
    REQUIRE_FALSE(renderer.popPick());
}

TEST_CASE("Shared material overrides belong to each vertex submission", "[renderer]")
{
    class MaterialProbe : public Counter {
    public:
        std::vector<srVertexProcessor::MaterialInfo> seen;
        void process(srVertexPipe& pipe) override { seen.push_back(pipe.getMaterialInfo()); }
    } probe;
    CoverageMaterial material;
    std::array<srVertexPipe::Record, 3> records{};
    for (auto& record : records) {
        record.material = &material;
        record.channels = 0xffffffff;
    }
    records[0].material_overrides = {.diffuse_alpha_scale = .25f, .emissive = .75f};
    records[1].material_overrides = {.diffuse_alpha_scale = .5f, .emissive = .125f};
    std::array<glm::vec3, 1> positions{glm::vec3(0, 0, -1)};
    std::array<w8_ulong, 1> indices{0};
    std::array<glm::vec4, 1> eye;
    std::array<unsigned char, 1> attributes{};
    std::array<srVertexArray, 3> arrays{};
    for (auto& array : arrays) {
        array.eye_locations = eye.data();
        array.attributes = attributes.data();
    }
    glm::mat4 identity(1);
    srVertexPipe::Input input(identity, identity);
    input.positions = positions;
    input.active_vertices = indices;
    input.direct_vertex_indices = 1;
    input.vertex_arrays = arrays;
    input.records = records;
    srVertexProcessor* processor = &probe;
    input.processors = {&processor, 1};
    srVertexPipe pipe;
    pipe.process(input);
    REQUIRE(probe.seen.size() == 3);
    REQUIRE(probe.seen[0].diffuse.w == .25f);
    REQUIRE(probe.seen[0].emissive == glm::vec4(.75f, .75f, .75f, 1));
    REQUIRE(probe.seen[1].diffuse.w == .5f);
    REQUIRE(probe.seen[1].emissive == glm::vec4(.125f, .125f, .125f, 1));
    REQUIRE(probe.seen[2].diffuse.w == 1);
    REQUIRE(probe.seen[2].emissive == glm::vec4(0));
    REQUIRE(material.getMaterialInfo().diffuse.w == 1);
}
