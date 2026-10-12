
#include <cstdint>
#include "surrender/srIlluminator.h"
#include "surrender/srGERD.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srNode.h"

// FUNCTION: SURRENDER 0x1004C7D0
srIlluminator::srIlluminator(srNode* parent)
{
    if (parent != 0) {
        setParent(parent, 0);
    }
    setFlag(FLAG_GLOBAL);
    group_mask = 0x80000000;
}

// FUNCTION: SURRENDER 0x1004C8D0
srIlluminator& srIlluminator::operator=(const srIlluminator& other)
{
    if (this != &other) {
        srNode::operator=(other);
        group_mask = other.group_mask;
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1004C900
void srIlluminator::process(const ProcessInfo& info, e_processType type)
{
    srGERD* renderer = info.renderer;
    if (type == PROCESS_PUSH || type == PROCESS_PUSH_GLOBAL) {
        {
            const auto transform = scopedWorldSpaceMatrix(*renderer);
            const glm::vec4 location = renderer->getEyeSpaceLocation(glm::vec3(0));
            eye_location = glm::vec3(location);
        }
        renderer->pushVertexProcessor(*this);
        return;
    }
    if (type == PROCESS_POP || type == PROCESS_POP_GLOBAL) {
        renderer->popVertexProcessor();
    }
}

// FUNCTION: SURRENDER 0x1004C9E0
void srIlluminator::traverse(TraverseInfo& info)
{
    traverseEffect(info);
}

// FUNCTION: SURRENDER 0x1004CAE0
const char* srIlluminator::sGetClassName()
{
    return "srIlluminator";
}

// FUNCTION: SURRENDER 0x1004CAF0
void srIlluminator::setGroupMask(std::uint32_t mask)
{
    group_mask = mask;
}

// FUNCTION: SURRENDER 0x1004CB00
std::uint32_t srIlluminator::getGroupMask() const
{
    return group_mask;
}
