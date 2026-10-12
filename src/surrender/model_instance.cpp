#include <cstdint>
#include "surrender/model_view_scope.h"
#include <ostream>
#include <memory>
#include <string>
#include <vector>

#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/srModelInstance.h"
#include "surrender/stream_scope.h"
#include "surrender/srCore.h"
#include "surrender/srDebug.h"
#include "surrender/srGERD.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srFlags.h"
#include "surrender/srMath.h"
#include "surrender/srModel.h"
#include "surrender/srNode.h"
#include "surrender/srStatisticsManager.h"
#include "surrender/srTypeRegistry.h"

// FUNCTION: SURRENDER 0x1004F920
srModelInstance::srModelInstance(srNode* parent, Ownership ownership)
    : srClassSupport<srModelInstance, srNode, 0x1100>(static_cast<srNode*>(nullptr), ownership)
{
    alignment_flags.value = 0;
    align_angle = 0.0f;
    align_axis = glm::vec3(0, 0, 1);
    exclusion_mask = 0;
    if (parent != 0) {
        setParent(parent, 0);
    }
}

// FUNCTION: SURRENDER 0x1004F890
srModelInstance& srModelInstance::operator=(const srModelInstance& other)
{
    if (this != &other) {
        srNode::operator=(other);
        alignment_flags = other.alignment_flags;
        model_ = other.model_;
        align_angle = other.align_angle;
        align_axis = other.align_axis;
        exclusion_mask = other.exclusion_mask;
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1004FA40
srModelInstance::~srModelInstance() = default;

// FUNCTION: SURRENDER 0x1004F3A0
void srModelInstance::traverse(TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }
    if (testFlag(FLAG_DISABLE) == 0 && getModel() != 0) {
        info.entries.push_back({this, 0});
    }
    if (testFlag(FLAG_TERMINATE) == 0 && first_child_ != 0) {
        first_child_->traverse(info);
    }
}

// FUNCTION: SURRENDER 0x1004F350
srNode::BoundInfo srModelInstance::getLocalBounds()
{
    srModel* model = getModel();
    if (model == nullptr) {
        return {glm::vec3(0), glm::vec3(0), glm::vec3(0), 0.0f, 0};
    }
    const auto bounds = model->getBounds();
    return {bounds.box.minimum, bounds.box.maximum, bounds.sphere.center, bounds.sphere.radius, 1};
}

srModelViewScope srModelInstance::scopedRenderTransform(srGERD& renderer) const
{
    if ((alignment_flags.value & 1) == 0) {
        return scopedWorldSpaceMatrix(renderer);
    } else {
        srModelViewScope transform(renderer);
        const auto matrix = renderer.getMatrix(srGERD::MATRIX_MODELVIEW);
        const glm::vec4 position = matrix * glm::vec4(glm::vec3(getWorldSpaceLocation()), 1);
        const auto world_scale = glm::vec3(getWorldSpaceScale());
        glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
                        glm::length(glm::vec3(matrix[2])));
        if (glm::determinant(glm::mat3(matrix)) > 0)
            scale = -scale;
        renderer.loadIdentity();
        renderer.translate(glm::vec3(position));
        if (align_angle != 0.0f) {
            renderer.rotate(align_angle, align_axis);
        }
        scale *= world_scale;
        renderer.scale(scale.x, scale.y, -scale.z);
        return transform;
    }
}

srModelInstance::RenderScope::RenderScope(const srModelInstance& instance, srGERD& renderer,
                                          bool apply_exclusion)
    : renderer(renderer), transform(instance.scopedRenderTransform(renderer))
{
    restore_mask = apply_exclusion && instance.exclusion_mask != 0;
    if (restore_mask) {
        previous_mask = renderer.getExclusionMask();
        renderer.setExclusionMask(previous_mask | instance.exclusion_mask);
    }
}

srModelInstance::RenderScope::~RenderScope()
{
    if (restore_mask) renderer.setExclusionMask(previous_mask);
}

srModelInstance::RenderScope srModelInstance::scopedRender(srGERD& renderer, bool apply_exclusion) const
{
    return RenderScope(*this, renderer, apply_exclusion);
}

// FUNCTION: SURRENDER 0x1004F4B0
void srModelInstance::process(const ProcessInfo& info, e_processType type)
{
    ++srCore.getStatisticsManager()->statistics.meshes_traversed;
    const auto scope = scopedRender(*info.renderer);
    getModel()->render(*info.renderer);
}

// FUNCTION: SURRENDER 0x1004FB20
void srModelInstance::dump(std::ostream& stream)
{
    srNode::dump(stream);
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    stream << "  Is aligned: " << srBoolToString(alignment_flags.value & 1) << '\n';
    if ((alignment_flags.value & 1) != 0) {
        stream.width(0x20);
        stream << "    Align axis: ";
        stream << '{' << align_axis.x << ',' << align_axis.y << ',' << align_axis.z << '}' << '\n';
        stream.width(0x20);
        stream << "    Align angle: " << (double)align_angle << '\n';
    }
    stream.width(0x20);
    stream << "  Model: ";
    stream << (getModel() != 0 ? getModel()->getName() : "none") << '\n';
    stream.width(0x20);
    stream << "  Exclusion mask: " << exclusion_mask << '\n';
}

// FUNCTION: SURRENDER 0x1004FF40
double srModelInstance::getAlignAngle() const
{
    return (double)align_angle;
}

// FUNCTION: SURRENDER 0x1004FF50
glm::vec3 srModelInstance::getAlignAxis() const
{
    return align_axis;
}

// FUNCTION: SURRENDER 0x1004FE90
std::int32_t srModelInstance::isAligned() const
{
    return (std::int32_t)(alignment_flags.value & 1);
}

// FUNCTION: SURRENDER 0x10050000
std::uint32_t srModelInstance::getExclusionMask() const
{
    return exclusion_mask;
}

// FUNCTION: SURRENDER 0x1004FE60
void srModelInstance::setAlignment(std::int32_t enabled)
{
    if (enabled != 0) {
        alignment_flags.value |= 1;
        return;
    }
    alignment_flags.value &= ~1u;
}

// FUNCTION: SURRENDER 0x1004FEA0
void srModelInstance::setAlignAngle(double angle)
{
    align_angle = (float)angle;
    alignment_flags.value |= 1;
}

// FUNCTION: SURRENDER 0x1004FEC0
void srModelInstance::setAlignAxis(const glm::vec3& axis)
{
    align_axis = normalizedOrZero(axis);
    alignment_flags.value |= 1;
}

// FUNCTION: SURRENDER 0x1004FFF0
void srModelInstance::setExclusionMask(std::uint32_t mask)
{
    exclusion_mask = mask;
}
