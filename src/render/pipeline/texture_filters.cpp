#include "wiz8/render/gpu_renderer.h"
#include "wiz8/render/texture.h"

void GpuRenderer::setTextureDefaultMagFilter(Texture::e_filter value)
{
    texture_state.default_mag_filter =
        value == Texture::FILTER_DEFAULT ? Texture::FILTER_GOOD : value;
    resetTexture();
}

void GpuRenderer::setTextureDefaultMinFilter(Texture::e_filter value)
{
    texture_state.default_min_filter =
        value == Texture::FILTER_DEFAULT ? Texture::FILTER_GOOD : value;
    resetTexture();
}

void GpuRenderer::setTextureDefaultMipmap(Texture::e_mipmap value)
{
    texture_state.default_mipmap =
        value == Texture::MIPMAP_DEFAULT ? Texture::MIPMAP_FASTEST : value;
    resetTexture();
}
