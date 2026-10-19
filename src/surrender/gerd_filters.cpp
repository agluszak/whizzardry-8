#include "surrender/srGERD.h"
#include "surrender/srTexture.h"

void srGERD::setTextureDefaultMagFilter(srTexture::e_filter value)
{
    texture_state.default_mag_filter =
        value == srTexture::FILTER_DEFAULT ? srTexture::FILTER_GOOD : value;
    resetTexture();
}

void srGERD::setTextureDefaultMinFilter(srTexture::e_filter value)
{
    texture_state.default_min_filter =
        value == srTexture::FILTER_DEFAULT ? srTexture::FILTER_GOOD : value;
    resetTexture();
}

void srGERD::setTextureDefaultMipmap(srTexture::e_mipmap value)
{
    texture_state.default_mipmap =
        value == srTexture::MIPMAP_DEFAULT ? srTexture::MIPMAP_FASTEST : value;
    resetTexture();
}
