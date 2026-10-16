#include "wiz8/surface2d.h"

#include <memory>
#include <stdexcept>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srGERD.h"
#include "surrender/srNode.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"
#include "surrender/srTextureIFace.h"
#include "wiz8/float_constants.h"

stTexture2D::stTexture2D()
    : srTexture(), left(0), top(0), right(128), bottom(128), frame_handle(getNewFrameHandle()),
      surface(0)
{
    setMipmap(MIPMAP_NONE);
    enableHint(HINT_NO_MIPMAPS);
    enableHint(HINT_POSITIONAL_6);
    enableHint(HINT_NO_ALPHA);
    texture_dimensions_.width = 128;
    texture_dimensions_.height = 128;
}

// FUNCTION: WIZ8 0x0047E600
stTexture2D::~stTexture2D()
{
    invalidateFrameHandle(frame_handle);
}

// FUNCTION: WIZ8 0x0047DE50
w8_ulong stTexture2D::getTextureFrameHandle()
{
    return frame_handle;
}

// FUNCTION: WIZ8 0x0047E710
void stTexture2D::getMipmapData(const MultiRequest& request)
{
    if (request.destinations.empty() || !request.destinations.front()) return;
    SDL_Rect source{left, top, right - left, bottom - top};
    if (!SDL_BlitSurface(surface.get(), &source, request.destinations.front(), nullptr))
        throw std::runtime_error(SDL_GetError());
}

// FUNCTION: WIZ8 0x0047E740
void stTexture2D::getMipmapLevelPartial(PartialRequest& request)
{
    SDL_Rect source{left + request.destination_x, top + request.destination_y,
                    request.source_right - request.destination_x,
                    request.source_bottom - request.destination_y};
    SDL_Rect destination{request.destination_x, request.destination_y, source.w, source.h};
    if (!SDL_BlitSurface(surface.get(), &source, request.destination, &destination))
        throw std::runtime_error(SDL_GetError());
}

// FUNCTION: WIZ8 0x0047DE40
void stTexture2D::invalidate()
{
    invalidateFrameHandle(frame_handle);
}

// FUNCTION: WIZ8 0x0047E790
void stTexture2D::setupDefaultValues()
{
    if (surface) {
        texture_dimensions_.format = surface->format;
    }
    texture_flags_ &= ~(1UL << FLAG_DIRTY_DEFAULTS);
}

// FUNCTION: WIZ8 0x0047DAE0
stSurface2D::stSurface2D(std::shared_ptr<SDL_Surface> source, int source_width, int source_height,
                         srNode* parent, int tile_extent)
    : srNode(static_cast<srNode*>(0)),
      vertex_array_mask(1 << srRendererDefs::VERTEX_ARRAY_TEXCOORD0), shader_bits(0x100a017),
      tile_size(tile_extent), columns((source_width + tile_extent - 1) / tile_extent),
      rows((source_height + tile_extent - 1) / tile_extent), width(source_width),
      height(source_height), tile_u(tile_extent / static_cast<float>(source_width)),
      tile_v(tile_extent / static_cast<float>(source_height)), field_168(0), field_16c(1.0f),
      scale(0.0f), texture_update_flags(0)
{
    setParent(parent, 1);
    tiles.reserve(columns * rows);
    for (int row = 0; row != rows; ++row) {
        for (int column = 0; column != columns; ++column) {
            auto& texture = tiles.emplace_back(std::make_unique<stTexture2D>());
            texture->surface = source;
            texture->texture_dimensions_.width = tile_extent;
            texture->texture_dimensions_.height = tile_extent;
            texture->left = column * tile_extent;
            texture->top = row * tile_extent;
            texture->right = texture->left + tile_extent;
            texture->bottom = texture->top + tile_extent;
            texture->setWrapS(srTextureIFace::WRAP_CLAMP);
            texture->setWrapT(srTextureIFace::WRAP_CLAMP);
        }
    }
    texture_coordinates[0][0] = 0.0f;
    texture_coordinates[0][1] = 0.0f;
    texture_coordinates[1][0] = 1.0f;
    texture_coordinates[1][1] = 0.0f;
    texture_coordinates[2][0] = 0.0f;
    texture_coordinates[2][1] = 1.0f;
    texture_coordinates[3][0] = 1.0f;
    texture_coordinates[3][1] = 1.0f;
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
    DrawTiles(info.renderer);
}

/* The tile render pass, entered from process(): ortho projection, vertex
   array state, then one triangle strip per tile in row-major order. */
// FUNCTION: WIZ8 0x0047E100
void stSurface2D::DrawTiles(srGERD* renderer)
{
    glm::vec3 location;
    int row;
    int column;
    int index = 0;

    location = glm::vec3(getLocation());
    renderer->matrixMode(srGERD::MATRIX_PROJECTION);
    renderer->pushMatrix();
    renderer->loadIdentity();
    renderer->ortho(0.0, 1.0, 1.0, 0.0, 0.0, 1.0);
    renderer->setVertexArrayMask(vertex_array_mask);
    renderer->setClipState(0x3f); /* CLIP_LEFT..CLIP_FAR */
    renderer->setCullMode(srGERD::CULL_NONE);
    srShader shader;
    shader.value = shader_bits;
    renderer->setShader(shader);
    renderer->setTexCoordPointer(2, srRendererDefs::TYPE_FLOAT, 8, texture_coordinates[0], 0);
    renderer->setAntiAlias(srGERD::ANTIALIAS_NONE);

    for (row = 0; row != rows; ++row) {
        for (column = 0; column != columns; ++column) {
            float vertices[12];
            float left = column * tile_u;
            float top = row * tile_v;
            float right = (column + 1) * tile_u;
            float bottom = (row + 1) * tile_v;

            vertices[0] = left;
            vertices[1] = top;
            vertices[2] = -0.1f;
            vertices[3] = right;
            vertices[4] = top;
            vertices[5] = -0.1f;
            vertices[6] = left;
            vertices[7] = bottom;
            vertices[8] = -0.1f;
            vertices[9] = right;
            vertices[10] = bottom;
            vertices[11] = -0.1f;

            renderer->setTexture(tiles[index++].get(), 0);
            renderer->setVertexPointer(3, srRendererDefs::TYPE_FLOAT, 0xc, vertices, 4);
            renderer->drawArrays(srRendererDefs::PRIMITIVE_TRIANGLE_STRIP, 0, 4);
        }
    }
    renderer->setTexture(0, 0);
    renderer->matrixMode(srGERD::MATRIX_PROJECTION);
    renderer->popMatrix();
}

// FUNCTION: WIZ8 0x0047E560
void stSurface2D::setScale(float new_scale)
{
    float factor = g_float_one / tile_size;
    float delta = (new_scale - scale) * factor;

    for (int corner = 0; corner < 4; ++corner) {
        for (int axis = 0; axis < 2; ++axis) {
            texture_coordinates[corner][axis] += delta;
        }
    }
    scale = new_scale;
}

void stSurface2D::invalidateTiles()
{
    for (auto& tile : tiles) {
        tile->invalidate();
    }
}

// FUNCTION: WIZ8 0x0047E370
void stSurface2D::setAlphaTestEnabled(bool enabled)
{
    if (!enabled) {
        shader_bits &= ~srShader::MASK_ALPHATEST;
        for (auto& tile : tiles) {
            tile->disableHint(srTextureIFace::HINT_ONE_BIT_ALPHA);
            tile->enableHint(srTextureIFace::HINT_NO_ALPHA);
            tile->invalidate();
        }
    } else {
        shader_bits |= srShader::MASK_ALPHATEST;
        for (auto& tile : tiles) {
            tile->disableHint(srTextureIFace::HINT_NO_ALPHA);
            tile->enableHint(srTextureIFace::HINT_ONE_BIT_ALPHA);
            tile->invalidate();
        }
    }
}

/* Retail 0x0047E450. The locked surface and pitch are deliberately retained in
   the ABI even though SurRender obtains the pixels through each stTexture2D's
   source surface. Keeping the source locked brackets the immediate partial
   texture uploads exactly as the caller does. */
// FUNCTION: WIZ8 0x0047E450
void stSurface2D::updateRectangle(srGERD* renderer, void*, w8_long, int left, int top, int right,
                                  int bottom)
{
    int x = left;
    int y = top;

    while (y < bottom) {
        stTexture2D* texture = 0;
        for (auto& tile : tiles) {
            stTexture2D* candidate = tile.get();
            if (candidate->left <= x && x < candidate->right && candidate->top <= y &&
                y < candidate->bottom) {
                texture = candidate;
                break;
            }
        }
        if (!texture) {
            return;
        }

        int destination_x = x - texture->left;
        int destination_y = y - texture->top;
        int update_width = right - x;
        int update_height = bottom - y;
        if (texture->right - x < update_width) {
            update_width = texture->right - x;
        }
        if (texture->bottom - y < update_height) {
            update_height = texture->bottom - y;
        }
        if (texture_update_flags & UPDATE_FULL_TILE) {
            destination_x = 0;
            destination_y = 0;
            update_width = tile_size;
            update_height = tile_size;
        }
        renderer->setTextureSubImage(texture, 0, destination_x, destination_y, update_width,
                                     update_height);

        x += tile_size - x % tile_size;
        if (right <= x) {
            y += tile_size - y % tile_size;
            x = left;
        }
    }
}

// FUNCTION: WIZ8 0x0047e5b0
void stSurface2D::enableTextureUpdateFlags(unsigned int flag)
{
    texture_update_flags |= flag;
}
