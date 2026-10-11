
#include <ostream>
#include <vector>

#include "surrender/srScene.h"
#include "surrender/stream_scope.h"
#include "surrender/srCamera.h"
#include "surrender/srCore.h"
#include "surrender/srGERD.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srClock.h"
#include "surrender/srFlags.h"
#include "surrender/srNode.h"
#include "surrender/srTypeRegistry.h"

// FUNCTION: SURRENDER 0x100565D0
srScene::srScene(srNode* parent)
    : srClassSupport<srScene, srNode, 0x1010>(static_cast<srNode*>(0))
{
    ambient_light = glm::vec3(0.2f);
    fog_color = glm::vec3(0.1f, 0.2f, 0.4f);
    traversal.renderer = 0;
    enabled.value = 0;
    if (parent != 0) {
        setParent(parent, 0);
    }
    resetStatistics();
}

// FUNCTION: SURRENDER 0x100566F0
srScene& srScene::operator=(const srScene& other)
{
    if (this != &other) {
        srNode::operator=(other);
        ambient_light = other.ambient_light;
        fog_color = other.fog_color;
        enabled = other.enabled;
    }
    return *this;
}

/* The scene walks its own traversal arrays: nodes carry the global pre/post passes (types 3 and 4)
   and the typed entries carry their per-node process type in the entry value. */
// FUNCTION: SURRENDER 0x100561F0
void srScene::traverse(TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }
    if (!testFlag(FLAG_DISABLE) && !testFlag(FLAG_TERMINATE) && first_child_ != 0) {
        info.entries.push_back({this, 0});
    }
}

// FUNCTION: SURRENDER 0x100562E0
void srScene::process(const ProcessInfo& info, e_processType type)
{
    if (first_child_ == 0) {
        return;
    }
    traversal.entries.clear();
    traversal.nodes.clear();
    traversal.renderer = info.renderer;
    first_child_->traverse(traversal);
    const auto node_count = static_cast<w8_long>(traversal.nodes.size());
    const auto entry_count = static_cast<w8_long>(traversal.entries.size());
    srGERD* renderer = info.renderer;
    w8_ulong_ptr pick_key = renderer->getPickKey();
    const auto fog_color = renderer->getFogColor();
    const auto ambient_light = renderer->getAmbientLight();
    renderer->setFogColor(glm::vec4(this->fog_color, 0));
    renderer->setAmbientLight(glm::vec4(this->ambient_light, 0));
    ProcessInfo process_info = info;
    for (w8_long index = 0; index < node_count; ++index) {
        traversal.nodes[index]->process(process_info, PROCESS_PUSH_GLOBAL);
    }
    for (w8_long index = 0; index < entry_count; ++index) {
        const auto entry = traversal.entries[index];
        if ((enabled.value & 1) != 0) {
            // reinterpret-ok: the pick key is the node pointer itself.
            renderer->setPickKey(reinterpret_cast<w8_ulong_ptr>(entry.node));
        }
        entry.node->process(process_info, static_cast<e_processType>(entry.value));
    }
    for (w8_long index = node_count; index > 0; --index) {
        traversal.nodes[index - 1]->process(process_info, PROCESS_POP_GLOBAL);
    }
    renderer->setFogColor(fog_color);
    renderer->setAmbientLight(ambient_light);
    renderer->setPickKey(pick_key);
    statistics.node_calls += node_count;
    ++statistics.render_calls;
    statistics.process_calls += entry_count;
}

// FUNCTION: SURRENDER 0x100564A0
void srScene::render(srGERD& renderer, srCamera* camera)
{
    srGERD* renderer_pointer = &renderer;
    if (renderer_pointer == 0) {
        return;
    }
    if (testFlag(FLAG_DISABLE) || testFlag(FLAG_TERMINATE) || first_child_ == 0) {
        return;
    }
    srNode::lockSceneGraph();
    ProcessInfo process_info;
    process_info.renderer = &renderer;
    if (camera != 0) {
        camera->process(process_info, PROCESS_PUSH);
    }
    process(process_info, PROCESS_RENDER);
    if (camera != 0) {
        camera->process(process_info, PROCESS_POP);
    }
    srNode::unlockSceneGraph();
}

// FUNCTION: SURRENDER 0x10056520
srScene::Statistics srScene::getStatistics() const
{
    Statistics result = statistics;
    result.elapsed = srCore.getTimer()->seconds() - result.elapsed;
    return result;
}

// FUNCTION: SURRENDER 0x10056550
void srScene::resetStatistics()
{
    statistics = {};
    statistics.elapsed = srCore.getTimer()->seconds();
}

// FUNCTION: SURRENDER 0x10056750
void srScene::dump(std::ostream& stream)
{
    srNode::dump(stream);
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    stream << "  Ambient light: ";
    stream << '{' << ambient_light.x << ',' << ambient_light.y << ',' << ambient_light.z << '}'
           << '\n';
    stream.width(0x20);
    stream << "  Fog color: ";
    stream << '{' << fog_color.x << ',' << fog_color.y << ',' << fog_color.z << '}' << '\n';
    const auto statistics = getStatistics();
    stream.width(0x20);
    stream << "  Time since stat reset: " << statistics.elapsed << '\n';
    stream.width(0x20);
    stream << "  Render calls/sec: " << statistics.render_calls / statistics.elapsed << '\n';
    stream.width(0x20);
    stream << "  Global calls/sec: " << statistics.node_calls / statistics.elapsed << '\n';
    stream.width(0x20);
    stream << "  Process calls/sec: " << statistics.process_calls / statistics.elapsed << '\n';
}

// FUNCTION: SURRENDER 0x10056B50
void srScene::disable(e_enable option)
{
    enabled.value &= ~(1ul << option);
}

// FUNCTION: SURRENDER 0x10056B70
void srScene::enable(e_enable option)
{
    enabled.value |= 1ul << option;
}

// FUNCTION: SURRENDER 0x10056B90
int srScene::isEnabled(e_enable option) const
{
    return (enabled.value & (1ul << option)) != 0;
}

// FUNCTION: SURRENDER 0x10056C40
glm::vec3 srScene::getAmbientLight() const
{
    return ambient_light;
}

// FUNCTION: SURRENDER 0x10056C90
glm::vec3 srScene::getFogColor() const
{
    return fog_color;
}

// FUNCTION: SURRENDER 0x10056CC0
void srScene::setAmbientLight(float red, float green, float blue)
{
    ambient_light = glm::vec3(red, green, blue);
}

// FUNCTION: SURRENDER 0x10056CF0
void srScene::setAmbientLight(const glm::vec3& color)
{
    ambient_light = color;
}

// FUNCTION: SURRENDER 0x10056D10
void srScene::setFogColor(float red, float green, float blue)
{
    fog_color = glm::vec3(red, green, blue);
}
