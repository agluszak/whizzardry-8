
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srCamera.h"
#include "surrender/srGERD.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"

// FUNCTION: SURRENDER 0x100565D0
srScene::srScene(srNode* parent) : srNode(static_cast<srNode*>(0))
{
    ambient_light = glm::vec3(0.2f);
    fog_color = glm::vec3(0.1f, 0.2f, 0.4f);
    enabled = 0;
    if (parent != 0) {
        setParent(parent, 0);
    }
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
        if ((enabled & 1) != 0) {
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
}

// FUNCTION: SURRENDER 0x100564A0
void srScene::render(srGERD& renderer, srCamera* camera)
{
    if (testFlag(FLAG_DISABLE) || testFlag(FLAG_TERMINATE) || first_child_ == 0) {
        return;
    }
    ProcessInfo process_info;
    process_info.renderer = &renderer;
    if (camera != 0) {
        camera->process(process_info, PROCESS_PUSH);
    }
    process(process_info, PROCESS_RENDER);
    if (camera != 0) {
        camera->process(process_info, PROCESS_POP);
    }
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
