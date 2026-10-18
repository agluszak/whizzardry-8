#include "wiz8/surface2d.h"

#include <memory>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "glm/ext/vector_float3.hpp"
#include "surrender/srGERD.h"
#include "surrender/srNode.h"
#include "surrender/srRendererDefs.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h"

stSurface2D::stSurface2D(std::shared_ptr<SDL_Surface> source, int source_width, int source_height,
                         srNode* parent, int tile_extent)
    : srNode(static_cast<srNode*>(0)),
      vertex_array_mask(1 << srRendererDefs::VERTEX_ARRAY_TEXCOORD0), shader_bits(0x100a017),
      tile_size(tile_extent), columns((source_width + tile_extent - 1) / tile_extent),
      rows((source_height + tile_extent - 1) / tile_extent), width(source_width),
      height(source_height), tile_u(tile_extent / static_cast<float>(source_width)),
      tile_v(tile_extent / static_cast<float>(source_height)), field_168(0), field_16c(1.0f),
      scale(0.0f)
{
    setParent(parent, 1);
    tiles.reserve(columns * rows);
    for (int row = 0; row != rows; ++row) {
        for (int column = 0; column != columns; ++column) {
            auto& texture = tiles.emplace_back(std::make_unique<srTexture>(source));
            texture->source_region = {column * tile_extent, row * tile_extent, tile_extent,
                                      tile_extent};
            auto dimensions = texture->getDimensions();
            dimensions.width = tile_extent;
            dimensions.height = tile_extent;
            texture->setDimensions(dimensions);
            texture->setMipmap(srTexture::MIPMAP_NONE);

            texture->setWrapS(srTexture::WRAP_CLAMP);
            texture->setWrapT(srTexture::WRAP_CLAMP);
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

void stSurface2D::process(const ProcessInfo& info, e_processType)
{
    DrawTiles(info.renderer);
}

/* The tile render pass, entered from process(): ortho projection, vertex
   array state, then one triangle strip per tile in row-major order. */
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

void stSurface2D::setScale(float new_scale)
{
    float factor = 1.0f / tile_size;
    float delta = (new_scale - scale) * factor;

    for (auto& texture_coordinate : texture_coordinates) {
        for (float& component : texture_coordinate) {
            component += delta;
        }
    }
    scale = new_scale;
}

void stSurface2D::setAlphaTestEnabled(bool enabled)
{
    if (enabled)
        shader_bits |= srShader::MASK_ALPHATEST;
    else
        shader_bits &= ~srShader::MASK_ALPHATEST;
}

/* Dirty rectangles update only the intersecting GPU tiles. */
void stSurface2D::updateRectangle(srGERD* renderer, int left, int top, int right, int bottom)
{
    int x = left;
    int y = top;

    while (y < bottom) {
        srTexture* texture = 0;
        for (auto& tile : tiles) {
            srTexture* candidate = tile.get();
            if (candidate->source_region.x <= x &&
                x < (candidate->source_region.x + candidate->source_region.w) &&
                candidate->source_region.y <= y &&
                y < (candidate->source_region.y + candidate->source_region.h)) {
                texture = candidate;
                break;
            }
        }
        if (!texture) {
            return;
        }

        int destination_x = x - texture->source_region.x;
        int destination_y = y - texture->source_region.y;
        int update_width = right - x;
        int update_height = bottom - y;
        if ((texture->source_region.x + texture->source_region.w) - x < update_width) {
            update_width = (texture->source_region.x + texture->source_region.w) - x;
        }
        if ((texture->source_region.y + texture->source_region.h) - y < update_height) {
            update_height = (texture->source_region.y + texture->source_region.h) - y;
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
