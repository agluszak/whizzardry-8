#include <memory>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/model_view_scope.h"
#include "surrender/srGERD.h"
#include "surrender/srMath.h"
#include "surrender/srModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"

// FUNCTION: SURRENDER 0x1004F920
srModelInstance::srModelInstance(srNode* parent) : srNode(static_cast<srNode*>(nullptr))
{
    alignment_flags = 0;
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
    if ((alignment_flags & 1) == 0) {
        return scopedWorldSpaceMatrix(renderer);
    } else {
        srModelViewScope transform(renderer);
        const auto matrix = renderer.getMatrix(srGERD::MATRIX_MODELVIEW);
        const glm::vec4 position = matrix * glm::vec4(glm::vec3(getWorldSpaceLocation()), 1);
        const auto world_scale = glm::vec3(getWorldSpaceScale());
        glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
                        glm::length(glm::vec3(matrix[2])));
        if (glm::determinant(glm::mat3(matrix)) > 0) scale = -scale;
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

srModelInstance::RenderScope srModelInstance::scopedRender(srGERD& renderer,
                                                           bool apply_exclusion) const
{
    return RenderScope(*this, renderer, apply_exclusion);
}

// FUNCTION: SURRENDER 0x1004F4B0
void srModelInstance::process(const ProcessInfo& info, e_processType type)
{
    const auto scope = scopedRender(*info.renderer);
    getModel()->render(*info.renderer);
}

// FUNCTION: SURRENDER 0x1004FF50
glm::vec3 srModelInstance::getAlignAxis() const
{
    return align_axis;
}

// FUNCTION: SURRENDER 0x1004FE90
int srModelInstance::isAligned() const
{
    return (int)(alignment_flags & 1);
}

// FUNCTION: SURRENDER 0x10050000
w8_ulong srModelInstance::getExclusionMask() const
{
    return exclusion_mask;
}

// FUNCTION: SURRENDER 0x1004FE60
void srModelInstance::setAlignment(int enabled)
{
    if (enabled != 0) {
        alignment_flags |= 1;
        return;
    }
    alignment_flags &= ~1u;
}

// FUNCTION: SURRENDER 0x1004FEA0
void srModelInstance::setAlignAngle(double angle)
{
    align_angle = (float)angle;
    alignment_flags |= 1;
}

// FUNCTION: SURRENDER 0x1004FEC0
void srModelInstance::setAlignAxis(const glm::vec3& axis)
{
    align_axis = normalizedOrZero(axis);
    alignment_flags |= 1;
}

// FUNCTION: SURRENDER 0x1004FFF0
void srModelInstance::setExclusionMask(w8_ulong mask)
{
    exclusion_mask = mask;
}
