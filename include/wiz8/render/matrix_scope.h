#pragma once
#include "wiz8/render/gpu_renderer.h"

// A caller-owned matrix snapshot. No renderer-managed stack or depth limit.
class MatrixScope {
public:
    explicit MatrixScope(GpuRenderer& renderer,
                         GpuRenderer::e_matrixMode target = GpuRenderer::MATRIX_MODELVIEW);
    ~MatrixScope();
    MatrixScope(MatrixScope&& other) noexcept;
    MatrixScope(const MatrixScope&) = delete;
    MatrixScope& operator=(const MatrixScope&) = delete;

private:
    GpuRenderer* renderer;
    glm::mat4 previous;
    GpuRenderer::e_matrixMode previous_mode;
    GpuRenderer::e_matrixMode target;
};
