#include "wiz8/surface2d.h"

#include <memory>
#include <utility>

#include "surrender/srGERD.h"
#include "surrender/srNode.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"

stSurface2D::stSurface2D(std::shared_ptr<SDL_Surface> source, srNode* parent)
    : texture_(std::move(source))
{
    setParent(parent, 1);
    auto dimensions = texture_.getDimensions();
    dimensions.native_size = true;
    texture_.setDimensions(dimensions);
    texture_.setMipmap(srTexture::MIPMAP_NONE);
    texture_.setWrapS(srTexture::WRAP_CLAMP);
    texture_.setWrapT(srTexture::WRAP_CLAMP);
}

// FUNCTION: WIZ8 0x004D6540
void stSurface2D::traverse(TraverseInfo& info)
{
    if (next_sibling_ != 0) {
        next_sibling_->traverse(info);
    }

    if (!testFlag(FLAG_DISABLE)) {
        info.entries.push_back({this, 0});
    }

    if (!testFlag(FLAG_TERMINATE) && first_child_ != 0) {
        first_child_->traverse(info);
    }
}

// FUNCTION: WIZ8 0x0047E0F0
void stSurface2D::process(const ProcessInfo& info, e_processType)
{
    draw(info.renderer);
}

void stSurface2D::draw(srGERD* renderer)
{
    const auto dimensions = texture_.getDimensions();
    const float u = pixel_offset_ / dimensions.width;
    const float v = pixel_offset_ / dimensions.height;
    const float coordinates[4][2]{{u, v}, {1 + u, v}, {u, 1 + v}, {1 + u, 1 + v}};
    const float vertices[4][3]{{0, 0, -0.1f}, {1, 0, -0.1f}, {0, 1, -0.1f}, {1, 1, -0.1f}};
    renderer->matrixMode(srGERD::MATRIX_PROJECTION);
    renderer->pushMatrix();
    renderer->loadIdentity();
    renderer->ortho(0.0, 1.0, 1.0, 0.0, 0.0, 1.0);
    renderer->setVertexArrayMask(1 << srRendererDefs::VERTEX_ARRAY_TEXCOORD0);
    renderer->setClipState(0x3f); /* CLIP_LEFT..CLIP_FAR */
    renderer->setCullMode(srGERD::CULL_NONE);
    srShader shader;
    shader.value = shader_bits_;
    renderer->setShader(shader);
    renderer->setTexCoordPointer(2, srRendererDefs::TYPE_FLOAT, sizeof(coordinates[0]),
                                 coordinates[0], 0);
    renderer->setTexture(&texture_, 0);
    renderer->setVertexPointer(3, srRendererDefs::TYPE_FLOAT, sizeof(vertices[0]), vertices, 4);
    renderer->drawArrays(srRendererDefs::PRIMITIVE_TRIANGLE_STRIP, 0, 4);
    renderer->setTexture(nullptr, 0);
    renderer->matrixMode(srGERD::MATRIX_PROJECTION);
    renderer->popMatrix();
}

// FUNCTION: WIZ8 0x0047E370
void stSurface2D::setAlphaTestEnabled(bool enabled)
{
    if (enabled)
        shader_bits_ |= srShader::MASK_ALPHATEST;
    else
        shader_bits_ &= ~srShader::MASK_ALPHATEST;
}

void stSurface2D::updateRectangle(srGERD* renderer, int left, int top, int right, int bottom)
{
    renderer->setTextureSubImage(&texture_, 0, left, top, right - left, bottom - top);
}
