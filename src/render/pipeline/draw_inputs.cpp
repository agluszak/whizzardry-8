#include "wiz8/render/draw_inputs.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "glm/common.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "wiz8/render/plane_math.h"

DrawTransform DrawTransform::withModel(const glm::mat4& model) const
{
    return withModelView(model_view * model);
}

DrawTransform DrawTransform::withModelView(const glm::mat4& value) const
{
    return {value, projection};
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

glm::mat4 DrawTransform::projectClipNear() const
{
    auto result = projection;
    const auto& p = projection;
    if ((p[0][3] != 0.0f || p[1][3] != 0.0f || p[2][3] != 0.0f) && p[3][3] + p[3][2] != 0.0f &&
        std::fabs((p[2][3] + p[2][2]) / (p[3][3] + p[3][2])) != 1.0)
        result *= std::fabs((p[2][3] + p[2][2]) / (p[3][3] + p[3][2]));
    return result;
}

glm::vec4 eyeSpaceClipPlane(const glm::mat4& model_view, const glm::vec4& plane)
{
    return glm::transpose(glm::inverse(model_view)) * planeMath::normalized(plane);
}
