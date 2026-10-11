#include <catch2/catch_test_macros.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>
#include <cstddef>
#include <span>

#include "catch2/catch_message.hpp"
#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srGERD.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srFog.h"
#include "surrender/srLight.h"
#include "surrender/srStatisticsManager.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srMaterialIFace.h"
#include "surrender/srMaterial.h"
#include "surrender/srNode.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srMath.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srTriMeshPipeline.h"
#include "surrender/srVertexProcessor.h"


namespace {
class TestNode : public srNode {
public:
    explicit TestNode(srNode* parent = nullptr) : srNode(parent) {}
    ~TestNode() override { ++destroyed; }
    TestNode& operator=(const TestNode&) = default;
    static inline int destroyed = 0;
};

bool recursiveSceneGraph()
{
    TestNode first;
    TestNode second;
    first.setLocation(10, 0, 0);
    second.setLocation(-10, 0, 0);
    auto* child = new TestNode(&first);
    child->setLocation(3, 0, 0);
    const auto world = child->getWorldSpaceLocation();

    srNode::lockSceneGraph();
    srNode::lockSceneGraph();
    REQUIRE(srNode::isSceneGraphLocked());
    REQUIRE(child->setParent(&second, 1));
    REQUIRE(child->getParent() == &second);
    REQUIRE(std::abs(child->getWorldSpaceLocation().x - world.x) < 1e-6);
    REQUIRE(!second.setParent(child, 0));
    REQUIRE(!child->setParent(child, 0));
    *child = first; // assignment must also recurse through the explicit lock
    srNode::unlockSceneGraph();
    REQUIRE(srNode::isSceneGraphLocked());

    std::promise<void> started;
    std::promise<void> finished;
    auto done = finished.get_future();
    std::jthread worker([&] {
        started.set_value();
        child->setParent(&first, 0);
        finished.set_value();
    });
    started.get_future().wait();
    const bool blocked = done.wait_for(std::chrono::milliseconds(20)) ==
                         std::future_status::timeout;
    srNode::unlockSceneGraph();
    worker.join();
    REQUIRE((blocked && !srNode::isSceneGraphLocked()));
    REQUIRE(child->getParent() == &first);

    // Parent destruction recurses into child destruction under the same mutex.
    const int before = TestNode::destroyed;
    {
        TestNode parent;
        new TestNode(new TestNode(&parent));
    }
    REQUIRE(TestNode::destroyed == before + 3);
    return true;
}

class TestMaterial : public srMaterialIFace {
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
    int isActive(srVertexPipe&) override { ++selections; return enabled; }
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
        const auto* indices = pipe.getAVT();
        const auto* normals = pipe.getEyeSpaceNormal();
        const auto* directions = pipe.getEyeSpaceDir();
        const auto* distances = pipe.getEyeSpaceDist();
        const auto* z_distances = pipe.getEyeSpaceZDist();
        float* alpha = pipe.getAlpha();
        float* fog = pipe.getFog();
        if (indices[0] == 0) {
            first_alpha = alpha;
            valid &= reinterpret_cast<std::uintptr_t>(alpha) % 16 == 0;
            valid &= reinterpret_cast<std::uintptr_t>(normals) % 16 == 0;
        }
        // First request is deliberately in a later material sub-batch. The
        // cache must fill the whole batch, not overread from that offset.
        const float* depth = indices[0] == 0 ? nullptr : pipe.getDepthCue();
        for (w8_ulong i = 0; i < pipe.getVertexCount(); ++i) {
            const float z = static_cast<float>(indices[i] + 1) + z_bias;
            valid &= normals[i].x == 0 && normals[i].y == 0 && normals[i].z == -1;
            valid &= std::abs(directions[i].z - 1) < 1e-5f;
            valid &= std::abs(distances[i] - z) < 1e-5f;
            valid &= z_distances[i] == z;
            valid &= alpha[i] == 1 && fog[i] == 0;
            if (depth) {
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
    std::array<srVector3, 65> positions;
    std::array<w8_ulong, 65> indices;
    std::array<std::shared_ptr<srMaterialIFace>, 65> materials;
    std::array<glm::vec4, 65> eye_locations;
    std::array<unsigned char, 65> attributes{};
    for (w8_ulong i = 0; i < positions.size(); ++i) {
        positions[i].Set(0, 0, static_cast<float>(i + 1));
        indices[i] = i;
        materials[i] = i == 0 ? first_material : second_material;
    }
    glm::mat4 identity;
    identity = glm::mat4(1);
    srVertexArray arrays{};
    arrays.eye_locations = eye_locations.data();
    arrays.attributes = attributes.data();
    srVertexPipe::Record record{};
    record.flags = srVertexPipe::Record::HAS_VERTEX_MATERIALS;
    record.channels = 0xffffffff;
    record.materials = materials.data();
    std::array<srVertexProcessor*, 3> processors{&probe, &active, &inactive};
    srVertexPipe::Input input{};
    input.vertex_count = positions.size();
    input.active_vertices = indices.data();
    input.direct_vertex_indices = 1;
    input.positions = positions.data();
    input.model_view = input.normal_matrix = &identity;
    input.vertex_arrays = &arrays;
    input.records = {&record, 1};
    input.processors = processors.data();
    input.processor_count = processors.size();
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
        for (auto& position : positions) { position.z += 2; }
        probe.z_bias = 2;
        pipe.process(input); // all lazy batch flags and alpha/fog must reset
        REQUIRE((probe.valid && probe.calls == 9));

        std::vector<srVertexProcessor*> many(129, &active);
        input.processors = many.data();
        input.processor_count = many.size();
        pipe.process(input);
        REQUIRE(active.calls == 9 + 129 * 3);
        active.enabled = false;
        pipe.process(input);
        REQUIRE(active.calls == 9 + 129 * 3);
        input.processors = nullptr;
        input.processor_count = 0;
        pipe.process(input); // no retained active pointers after shrinking to zero
        REQUIRE(active.calls == 9 + 129 * 3);
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
        auto* diffuse = pipe.getDiffuse();
        auto* specular = pipe.getSpecular();
        auto* alpha = pipe.getAlpha();
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
        auto* diffuse = pipe.getDiffuse();
        auto* specular = pipe.getSpecular();
        auto* alpha = pipe.getAlpha();
        auto* fog = pipe.getFog();
        const auto* indices = pipe.getAVT();
        const float values[] = {-2.f, 0.f, .25f, 1.f, 2.f};
        for (w8_ulong index = 0; index < pipe.getVertexCount(); ++index) {
            diffuse[index] = specular[index] = glm::vec4(.5f);
            alpha[index] = fog[index] = values[indices[index] % 5];
        }
    }
};

bool coverageFinalization()
{
    std::array<srVector3, 65> positions;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> eye, diffuse, specular;
    std::array<unsigned char, 65> attributes{};
    for (unsigned index = 0; index < positions.size(); ++index) {
        positions[index].Set(0, 0, float(index + 1));
        indices[index] = index;
    }
    srVertexArray arrays{};
    arrays.eye_locations = eye.data(); arrays.diffuse = diffuse.data();
    arrays.specular = specular.data(); arrays.attributes = attributes.data();
    glm::mat4 identity(1);
    SeedCoverage seed;
    srVertexProcessor* processor = &seed;
    CoverageMaterial material;
    srVertexPipe::Record record{};
    record.material = &material;
    srVertexPipe::Input input{};
    input.vertex_count = positions.size(); input.active_vertices = indices.data();
    input.direct_vertex_indices = 1; input.positions = positions.data();
    input.model_view = input.normal_matrix = &identity;
    input.vertex_arrays = &arrays; input.records = {&record, 1};
    input.processors = &processor; input.processor_count = 1;
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
                const float expected_alpha = channels & 2
                    ? channels & 1 ? clamp(scale * source) : scale * clamped : .5f;
                const float expected_fog = channels & 8
                    ? channels & 4 ? clamp(scale * source) : scale * clamped : .5f;
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
    bool attenuated = false, directional = false;
    void process(srVertexPipe& pipe) override
    {
        ++calls;
        const unsigned count = pipe.getVertexCount();
        if (ready & 1) {
            auto* colors = pipe.getDiffuse();
            for (unsigned i = 0; i < count; ++i) colors[i] = glm::vec4(.25f);
        }
        if (ready & 2) {
            auto* colors = pipe.getSpecular();
            for (unsigned i = 0; i < count; ++i) colors[i] = glm::vec4(.5f);
        }
        auto& statistics = srCore.getStatisticsManager()->statistics;
        const auto before_diffuse = statistics.diffuse_operations;
        const auto before_specular = statistics.specular_operations;
        light.process(pipe);
        REQUIRE(statistics.diffuse_operations - before_diffuse == (channels & 1 ? count : 0));
        REQUIRE(statistics.specular_operations - before_specular == (channels & 2 ? count : 0));
        const float attenuation = attenuated && !directional ? .5f : 1.f;
        if (channels & 1) {
            const auto* colors = pipe.getDiffuse();
            const float expected = (ready & 1 ? .25f : .125f) + .25f * attenuation;
            for (unsigned i = 0; i < count; ++i)
                for (unsigned c = 0; c < 4; ++c) REQUIRE(colors[i][c] == expected);
        }
        if (channels & 2) {
            const auto* colors = pipe.getSpecular();
            const float expected = (ready & 2 ? .5f : 0.f) + .0625f * attenuation;
            for (unsigned i = 0; i < count; ++i)
                for (unsigned c = 0; c < 4; ++c) REQUIRE(colors[i][c] == expected);
        }
    }
};

class LightingMaterial : public TestMaterial {
public:
    void getMaterialInfo(srVertexProcessor::MaterialInfo& info) override
    {
        TestMaterial::getMaterialInfo(info);
        info.disabled_channels = 0;
        info.diffuse = glm::vec4(.5f); info.specular = glm::vec4(.25f);
        info.emissive = glm::vec4(.125f); info.shininess = 1;
    }
};

bool surfaceLighting()
{
    std::array<srVector3, 65> positions, normals;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> diffuse, specular, eye;
    std::array<unsigned char, 65> attributes{};
    for (unsigned i = 0; i < 65; ++i) {
        positions[i].Set(0, 0, -10); normals[i].Set(0, 0, 1); indices[i] = i;
    }
    srVertexArray arrays{};
    arrays.diffuse = diffuse.data(); arrays.specular = specular.data();
    arrays.eye_locations = eye.data(); arrays.attributes = attributes.data();
    LightingMaterial material;
    srVertexPipe::Record record{}; record.material = &material;
    glm::mat4 identity(1);
    SurfaceLightProbe probe;
    srVertexProcessor* processor = &probe;
    srVertexPipe::Input input{};
    input.positions = positions.data(); input.normals = normals.data();
    input.active_vertices = indices.data(); input.direct_vertex_indices = 1;
    input.vertex_arrays = &arrays; input.records = {&record, 1};
    input.model_view = input.normal_matrix = &identity;
    input.processors = &processor; input.processor_count = 1;
    input.ambient_light = glm::vec4(0);
    for (unsigned channels = 1; channels < 4; ++channels)
    for (unsigned ready = 0; ready < 4; ++ready)
    for (bool directional : {false, true})
    for (bool attenuated : {false, true})
    for (unsigned count : {0u, 1u, 65u}) {
        CAPTURE(channels, ready, directional, attenuated, count);
        probe.channels = channels; probe.ready = ready;
        probe.directional = directional; probe.attenuated = attenuated;
        auto& light = probe.light;
        light.channel_mask = (channels & 1 ? 1u << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE : 0) |
                             (channels & 2 ? 1u << srVertexProcessor::CHANNEL_SPECULAR : 0);
        light.derived_flags = directional ? srLight::DERIVED_DIRECTIONAL :
            attenuated ? srLight::DERIVED_OPENGL_ATTENUATION |
                         srLight::DERIVED_CONSTANT_ATTENUATION : 0;
        light.eye_location = directional ? glm::vec3(0, 0, 1) : glm::vec3(0);
        light.attenuation_model = attenuated ? srLight::ATTENUATION_OPENGL :
                                             srLight::ATTENUATION_NONE;
        light.opengl_attenuation = glm::vec3(2, 0, 0);
        light.scaled_diffuse = glm::vec4(.5f); light.scaled_specular = glm::vec4(.25f);
        input.vertex_count = count;
        record.channels = ~(light.channel_mask | (1u << srVertexProcessor::CHANNEL_DIFFUSE));
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
        const auto* indices = pipe.getAVT();
        const auto* cue = pipe.getDepthCue();
        for (unsigned index = 0; index < pipe.getVertexCount(); ++index) {
            const float distance = float(indices[index] + 1);
            float expected;
            if (minimum == maximum) expected = distance < minimum ? 1 - low : 1 - high;
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
    std::array<srVector3, 65> positions;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> eye, specular;
    std::array<unsigned char, 65> attributes{};
    for (unsigned index = 0; index < positions.size(); ++index) {
        positions[index].Set(0, 0, float(index + 1));
        indices[index] = index;
    }
    srVertexArray arrays{};
    arrays.eye_locations = eye.data(); arrays.specular = specular.data();
    arrays.attributes = attributes.data();
    glm::mat4 identity(1);
    ProfileMaterial material;
    srVertexPipe::Record record{};
    record.material = &material;
    record.channels = ~(1u << srVertexProcessor::CHANNEL_FOG);
    srVertexPipe::Input input{};
    input.vertex_count = positions.size(); input.active_vertices = indices.data();
    input.direct_vertex_indices = 1; input.positions = positions.data();
    input.model_view = input.normal_matrix = &identity;
    input.vertex_arrays = &arrays; input.records = {&record, 1};
    input.eye_center = glm::vec3(0, 0, 33); input.eye_radius = 33;
    input.environment_scale = .2f; input.environment_inverse_scale = .8f;
    DistanceProfileProbe probe;
    srVertexProcessor* processor = &probe;
    input.processors = &processor; input.processor_count = 1;
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
    std::array<srVector3, 65> positions;
    std::array<w8_ulong, 65> indices;
    std::array<glm::vec4, 65> eye, diffuse, specular;
    std::array<unsigned char, 65> attributes{};
    for (unsigned i = 0; i < positions.size(); ++i) {
        positions[i].Set(0, 0, float(i + 1));
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
    record.channels = ~((1u << srVertexProcessor::CHANNEL_DIFFUSE) |
                        (1u << srVertexProcessor::CHANNEL_SPECULAR) |
                        (1u << srVertexProcessor::CHANNEL_ALPHA) |
                        (1u << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE) |
                        (1u << srVertexProcessor::CHANNEL_LIGHT_AMBIENT));
    srVertexPipe::Input input{};
    input.vertex_count = positions.size();
    input.active_vertices = indices.data();
    input.direct_vertex_indices = 1;
    input.positions = positions.data();
    input.model_view = input.normal_matrix = &identity;
    input.vertex_arrays = &arrays;
    input.records = {&record, 1};
    input.processors = &processor;
    input.processor_count = 1;
    input.environment_maximum = 100;
    input.environment_inverse_scale = 1;
    srVertexPipe pipe;
    for (unsigned operations = 0; operations < 32; ++operations) {
        for (unsigned bit = 0; bit < 5; ++bit) {
            auto operation = static_cast<srMaterial::e_oper>(bit);
            if (operations & (1u << bit)) material.enable(operation);
            else material.disable(operation);
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
    {
        srGERD growing(srCreateSDLGPUDevice(), "renderer entry growth");
        std::array<srGERD::Renderer*, 32> borrowed{};
        for (std::size_t index = 0; index < borrowed.size(); ++index) {
            borrowed[index] = growing.lockRenderer();
            for (std::size_t previous = 0; previous < index; ++previous) {
                REQUIRE(borrowed[index] != borrowed[previous]);
            }
        }
        for (auto* renderer : borrowed) growing.unlockRenderer(renderer, 0);
        REQUIRE(growing.lockRenderer() == borrowed.back());
        growing.unlockRenderer(borrowed.back(), 0);
    }
    // No context or window is opened: these checks only exercise CPU ownership
    // and the renderer's owner-thread submission bookkeeping.
    srGERD gerd(srCreateSDLGPUDevice(), "ownership fixture");
    auto* renderer = gerd.lockRenderer(); // nested flush/create locks
    gerd.unlockRenderer(renderer, 1);
    auto statistics = renderer->getStatistics();
    REQUIRE(statistics.device_calls == 1);

    for (int mode = 0; mode < 4; ++mode) {
        std::promise<srGERD::Renderer*> acquired;
        std::promise<void> release;
        auto permission = release.get_future();
        std::jthread worker([&] {
            auto* borrowed = gerd.lockRenderer();
            acquired.set_value(borrowed);
            permission.wait();
            // Give the owner time to enter the waiting path while still busy.
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            gerd.unlockRenderer(borrowed, 1);
            gerd.flushImmediateRenderers(); // workers must not submit to the DD
            gerd.flushRenderers();
        });
        auto* borrowed = acquired.get_future().get();
        release.set_value();
        if (mode == 0) { gerd.flushImmediateRenderers(); }
        else if (mode == 2) { gerd.resetStatistics(); }
        else { gerd.flushRenderers(); }
        worker.join();
        REQUIRE(borrowed == renderer);
        statistics = renderer->getStatistics();
        REQUIRE(statistics.device_calls == (mode == 0 ? 2u : mode == 1 ? 3u : mode == 2 ? 0u : 1u));
        if (mode == 2) {
            // Reuse the CPU renderer as a sorted renderer without opening a
            // window, to exercise flushSort's worker-release wait as well.
            renderer->sorted = 1;
            gerd.toggle(srGERD::ENABLE_SORTED_RENDERING);
        }
    }

    // Sorted acquisition must wait without monopolizing the mutex or handing out a busy batch.
    REQUIRE(gerd.lockRenderer() == renderer);
    std::promise<srGERD::Renderer*> acquired;
    auto ready = acquired.get_future();
    std::jthread contender([&] {
        auto* borrowed = gerd.lockRenderer();
        acquired.set_value(borrowed);
        gerd.unlockRenderer(borrowed, 0);
    });
    const bool waited = ready.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
    gerd.unlockRenderer(renderer, 0);
    auto* resumed = ready.get();
    contender.join();
    REQUIRE((waited && resumed == renderer));

    srGERD::Renderer::IndexWrite write;
    renderer->indices.alloc(write, 2);
    srVertexArray arrays{};
    renderer->vertices.alloc(arrays, 3);
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
    srGERD gerd(srCreateSDLGPUDevice(), "texture stage fixture");
    for (unsigned stages : {1u, 2u}) {
        srGERD::Renderer renderer({&gerd, 0, 256, stages});
        srVertexArray arrays{};
        renderer.allocVertexArray(arrays, 3);
        for (unsigned stage = 0; stage < 2; ++stage)
            for (unsigned i = 0; i < 3; ++i) {
                renderer.vertices.st[stage][i] = glm::vec2(10 * stage + i, 20 + i);
                renderer.vertices.q[stage][i] = 0.25f * (stage + i + 1);
            }
        for (unsigned attributes = 0; attributes < 128; ++attributes) {
            for (auto& stream : renderer.stq) stream.clear();
            renderer.vertices.attributes[0] = attributes;
            renderer.programVertexArrays(&arrays, 3);
            const auto mask = gerd.getVertexArrayMask().value;
            for (unsigned stage = 0; stage < 2; ++stage) {
                const bool enabled = stage < stages &&
                    (attributes & (srVertexArray::ATTRIBUTE_ST0 << stage));
                REQUIRE(bool(mask & (1u << (srRendererDefs::VERTEX_ARRAY_TEXCOORD0 + stage))) == enabled);
                const bool combined = enabled &&
                    (attributes & (srVertexArray::ATTRIBUTE_Q0 << stage));
                REQUIRE(renderer.stq[stage].size() == (combined ? 3u : 0u));
                if (combined)
                    for (unsigned i = 0; i < 3; ++i) {
                        REQUIRE(renderer.stq[stage][i].st == renderer.vertices.st[stage][i]);
                        REQUIRE(renderer.stq[stage][i].q == renderer.vertices.q[stage][i]);
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
            srVertexArray arrays{};
            renderer.vertices.alloc(arrays, 5);
            renderer.allocVertexArray(arrays, 6);
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
            std::array<srVector3i, 1> triangles{{{0, 1, 2}}};
            std::array<int, 3> uv;
            for (unsigned corner = 0; corner < 3; ++corner)
                uv[corner] = corner + ((changed & (1u << corner)) ? 3 : 0);
            std::array<srVector3i, 1> poly_uv{{{uv[0], uv[1], uv[2]}}};
            std::array<glm::vec2, 6> texcoords;
            for (unsigned i = 0; i < texcoords.size(); ++i) texcoords[i] = glm::vec2(i, i + 10);
            std::array<srTriMeshPipeline::Pass, 2> passes{};
            passes[0].poly_uv = poly_uv.data();
            passes[0].texcoords = texcoords.data();
            srGERD::Renderer::TriInput input{};
            input.triangle_count = 1;
            input.record_count = 2;
            input.vertex_count = 3;
            input.indices = indices.data();
            input.vertices = vertices.data();
            input.triangles = triangles.data();
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
    srGERD renderer(srCreateSDLGPUDevice(), "record handoff");
    auto* pipeline = srTriMeshPipeline::Get(&renderer);
    std::array<srVector3, 3> positions{{{0, 0, 1}, {1, 0, 1}, {0, 1, 1}}};
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
        slot.channels = ~((1u << srVertexProcessor::CHANNEL_ST0) |
                          (1u << srVertexProcessor::CHANNEL_ST1));
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
    srVertexPipe::Input input{};
    input.vertex_count = positions.size();
    input.active_vertices = indices.data();
    input.positions = positions.data();
    input.model_view = input.normal_matrix = &identity;
    input.vertex_arrays = arrays.data();
    input.records = std::span{pipeline->records}.first(pipeline->slot_count);
    pipeline->vertex_pipe->process(input);
    for (unsigned channel = 0; channel < output.size(); ++channel)
        for (unsigned vertex = 0; vertex < positions.size(); ++vertex) {
            REQUIRE(output[channel][vertex].x == source[channel][indices[vertex]].x);
            REQUIRE(output[channel][vertex].y == source[channel][indices[vertex]].y);
            REQUIRE(q[channel][vertex] == 1.0f);
        }
    pipeline->Reset(&renderer);
    return true;
}

} // namespace

TEST_CASE("renderer_ownership_test")
{
    for (int cycle = 0; cycle < 2; ++cycle) {
        REQUIRE(srInit());
        REQUIRE(recursiveSceneGraph());
        REQUIRE(processorOwnership());
        REQUIRE(rendererOwnership());
        REQUIRE(materialChannelOperations());
        REQUIRE(coverageFinalization());
        REQUIRE(distanceProfiles());
        REQUIRE(surfaceLighting());
        REQUIRE(pipelineRecordTexcoords());
        REQUIRE(rendererTextureStages());
        REQUIRE(rendererCornerRemapping());
        REQUIRE(srExit());
        REQUIRE(srGERD::getFirst() == nullptr);
    }
}
