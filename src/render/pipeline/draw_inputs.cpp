#include "wiz8/render/draw_inputs.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "glm/common.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "wiz8/render/plane_math.h"

DrawTransform DrawTransform::withModel(const glm::mat4& model) const
{
    return withModelView(model_view * model);
}

DrawTransform DrawTransform::withModelView(const glm::mat4& value) const
{
    return {value, projection, user_clip_planes};
}

glm::mat4 DrawTransform::inverseModelView() const
{
    return glm::inverse(model_view);
}

float DrawTransform::maxModelViewScale() const
{
    const glm::mat3 basis(model_view);
    return glm::max(glm::length(basis[0]), glm::max(glm::length(basis[1]), glm::length(basis[2])));
}

glm::mat4 DrawTransform::normalMatrix() const
{
    glm::mat3 basis(model_view);
    const glm::vec3 lengths(glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2]));
    for (int column = 0; column < 3; ++column) basis[column] /= lengths[column];
    return glm::mat4(glm::transpose(glm::inverse(basis)));
}

glm::mat4 DrawTransform::projectClipNear() const
{
    auto result = projection;
    const auto& p = projection;
    if ((p[0][3] != 0.0f || p[1][3] != 0.0f || p[2][3] != 0.0f) && p[3][3] + p[3][2] != 0.0f &&
        std::fabs((p[2][3] + p[2][2]) / (p[3][3] + p[3][2])) != 1.0)
        result *= std::fabs((p[2][3] + p[2][2]) / (p[3][3] + p[3][2]));
    return result;
}

ClipPlanes DrawTransform::clipPlanes() const
{
    ClipPlanes result;
    const auto rows = glm::transpose(projection);
    result.planes[0] = rows[3] + rows[0];
    result.planes[1] = rows[3] - rows[0];
    result.planes[2] = rows[3] + rows[1];
    result.planes[3] = rows[3] - rows[1];
    result.planes[4] = rows[3] + rows[2];
    result.planes[5] = rows[3] - rows[2];
    for (int i = 0; i < 6; ++i) result.planes[i] = planeMath::normalized(result.planes[i]);
    result.mask = 0x3fu;
    const auto count = std::min(user_clip_planes.size(), max_user_clip_planes);
    for (std::size_t i = 0; i < count; ++i) {
        result.planes[6 + i] = user_clip_planes[i];
        result.mask |= 1u << (6 + i);
    }
    return result;
}

bool DrawTransform::sphereVisible(const glm::vec3& center, float radius) const
{
    const glm::vec4 eye = model_view * glm::vec4(center, 1);
    const float negative_radius = -radius * maxModelViewScale();
    const auto planes = clipPlanes();
    for (unsigned i = 0; i < planes.planes.size(); ++i)
        if ((planes.mask & (1u << i)) && glm::dot(eye, planes.planes[i]) <= negative_radius)
            return false;
    return true;
}

glm::vec4 eyeSpaceClipPlane(const glm::mat4& model_view, const glm::vec4& plane)
{
    return glm::transpose(glm::inverse(model_view)) * planeMath::normalized(plane);
}
