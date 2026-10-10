#include "surrender/srCore.h"
#include "surrender/srDD_SDLGPU.h"
#include "surrender/srDebugVP.h"
#include "surrender/srGERD.h"
#include "surrender/srMaterialIFace.h"
#include "surrender/srNode.h"
#include "surrender/srVP_generic.h"
#include "surrender/srVertexPipe.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#define CHECK(expression)                                                      \
    do {                                                                       \
        if (!(expression)) {                                                   \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #expression);        \
            return false;                                                      \
        }                                                                      \
    } while (0)

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
    CHECK(srNode::isSceneGraphLocked());
    CHECK(child->setParent(&second, 1));
    CHECK(child->getParent() == &second);
    CHECK(std::abs(child->getWorldSpaceLocation().x - world.x) < 1e-6);
    CHECK(!second.setParent(child, 0));
    CHECK(!child->setParent(child, 0));
    *child = first; // assignment must also recurse through the explicit lock
    srNode::unlockSceneGraph();
    CHECK(srNode::isSceneGraphLocked());

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
    CHECK(blocked && !srNode::isSceneGraphLocked());
    CHECK(child->getParent() == &first);

    // Parent destruction recurses into child destruction under the same mutex.
    const int before = TestNode::destroyed;
    {
        TestNode parent;
        new TestNode(new TestNode(&parent));
    }
    CHECK(TestNode::destroyed == before + 3);
    return true;
}

class TestMaterial : public srMaterialIFace {
public:
    srClass* vInstance() override { return new TestMaterial; }
    void getMaterialInfo(srVertexProcessor::MaterialInfo& info) override
    {
        info.diffuse = info.ambient = info.specular = info.emissive = 0.0f;
        info.translucency = info.shininess = info.value_38 = info.fog_scale = 0.0f;
        info.disabled_channels = 0xffffffff;
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
    TestMaterial first_material;
    TestMaterial second_material;
    ScratchProbe probe;
    Counter active;
    Counter inactive;
    inactive.enabled = false;
    std::array<srVector3, 65> positions;
    std::array<w8_ulong, 65> indices;
    std::array<srMaterialIFace*, 65> materials;
    std::array<srVector4, 65> eye_locations;
    std::array<unsigned char, 65> attributes{};
    for (w8_ulong i = 0; i < positions.size(); ++i) {
        positions[i].Set(0, 0, static_cast<float>(i + 1));
        indices[i] = i;
        materials[i] = i == 0 ? &first_material : &second_material;
    }
    srMatrix4 identity;
    identity.SetIdentity();
    srVertexArray arrays{};
    arrays.eye_locations = eye_locations.data();
    arrays.attributes = attributes.data();
    srVertexPipe::Record record{};
    record.flags = srVertexPipe::Record::HAS_VERTEX_MATERIALS;
    record.channels = 0xffffffff;
    record.materials = materials.data();
    std::array<srVertexProcessor*, 3> processors{&probe, &active, &inactive};
    srVertexPipe::Input input{};
    input.record_count = 1;
    input.vertex_count = positions.size();
    input.active_vertices = indices.data();
    input.direct_vertex_indices = 1;
    input.positions = positions.data();
    input.model_view = input.normal_matrix = &identity;
    input.vertex_arrays = &arrays;
    input.records = &record;
    input.processors = processors.data();
    input.processor_count = processors.size();
    input.environment_maximum = 100;
    input.environment_inverse_scale = 1;
    {
        srVertexPipe pipe;
        srVertexPipe other;
        pipe.process(input);
        CHECK(probe.valid && probe.calls == 3 && active.calls == 3 && inactive.calls == 0);
        const float* first_scratch = probe.first_alpha;
        other.process(input);
        CHECK(probe.valid && probe.first_alpha != first_scratch);
        for (auto& position : positions) { position.z += 2; }
        probe.z_bias = 2;
        pipe.process(input); // all lazy batch flags and alpha/fog must reset
        CHECK(probe.valid && probe.calls == 9);

        std::vector<srVertexProcessor*> many(129, &active);
        input.processors = many.data();
        input.processor_count = many.size();
        pipe.process(input);
        CHECK(active.calls == 9 + 129 * 3);
        active.enabled = false;
        pipe.process(input);
        CHECK(active.calls == 9 + 129 * 3);
        input.processors = nullptr;
        input.processor_count = 0;
        pipe.process(input); // no retained active pointers after shrinking to zero
        CHECK(active.calls == 9 + 129 * 3);
    }
    // The pipe owns the pointer list, never the processors.
    active.enabled = true;
    srVertexPipe empty;
    CHECK(active.isActive(empty));
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
                CHECK(borrowed[index] != borrowed[previous]);
            }
        }
        for (auto* renderer : borrowed) growing.unlockRenderer(renderer, 0);
        CHECK(growing.lockRenderer() == borrowed.back());
        growing.unlockRenderer(borrowed.back(), 0);
    }
    // No context or window is opened: these checks only exercise CPU ownership
    // and the renderer's owner-thread submission bookkeeping.
    srGERD gerd(srCreateSDLGPUDevice(), "ownership fixture");
    auto* renderer = gerd.lockRenderer(); // nested flush/create locks
    gerd.unlockRenderer(renderer, 1);
    w8_ulong statistics[7];
    renderer->getStatistics(statistics);
    CHECK(statistics[4] == 1);

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
        CHECK(borrowed == renderer);
        renderer->getStatistics(statistics);
        CHECK(statistics[4] == (mode == 0 ? 2u : mode == 1 ? 3u : mode == 2 ? 0u : 1u));
        if (mode == 2) {
            // Reuse the CPU renderer as a sorted renderer without opening a
            // window, to exercise flushSort's worker-release wait as well.
            renderer->sorted = 1;
            gerd.toggle(srGERD::ENABLE_SORTED_RENDERING);
        }
    }

    // Sorted acquisition must wait without monopolizing the mutex or handing out a busy batch.
    CHECK(gerd.lockRenderer() == renderer);
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
    CHECK(waited && resumed == renderer);

    srGERD::Renderer::IndexWrite write;
    renderer->indices.alloc(write, 2);
    srVertexArray arrays{};
    renderer->vertices.alloc(arrays, 3);
    const auto index_capacity = renderer->indices.triangles.capacity();
    const auto vertex_capacity = renderer->vertices.positions.capacity();
    renderer->reset(0);
    CHECK(renderer->indices.count == 0 && renderer->vertices.count == 0);
    CHECK(renderer->indices.triangles.capacity() == index_capacity);
    CHECK(renderer->vertices.positions.capacity() == vertex_capacity);
    renderer->reset(1); // real eviction remains distinct from ordinary reset
    CHECK(renderer->indices.triangles.capacity() == 0);
    CHECK(renderer->vertices.positions.capacity() == 0);
    return true;
}

bool debugInitialization()
{
    srVP_generic base;
    srDebugVP debug(&base);
    const float input[] = {4, 9};
    float output[2];
    debug._sqrt(output, input, 2);
    CHECK(output[0] == 2 && output[1] == 3);
    return true;
}
} // namespace

int main()
{
    for (int cycle = 0; cycle < 2; ++cycle) {
        if (!srInit()) { return 1; }
        const bool passed = recursiveSceneGraph() && processorOwnership() &&
                            rendererOwnership() && debugInitialization();
        const bool exited = srExit();
        if (!passed || !exited || srGERD::getFirst() != nullptr) { return 1; }
    }
    std::puts("ok: recursive scene/renderer locks, processor scratch and reset ownership");
}
