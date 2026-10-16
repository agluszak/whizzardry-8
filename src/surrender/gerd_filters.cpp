#include "surrender/srGERD.h"
#include "surrender/srTexture.h"

// FUNCTION: SURRENDER 0x10018650
void srGERD::setTextureDefaultMagFilter(srTexture::e_filter value)
{
    texture_state.default_mag_filter =
        value == srTexture::FILTER_DEFAULT ? srTexture::FILTER_GOOD : value;
    resetTexture();
}

// FUNCTION: SURRENDER 0x100186D0
void srGERD::setTextureDefaultMinFilter(srTexture::e_filter value)
{
    texture_state.default_min_filter =
        value == srTexture::FILTER_DEFAULT ? srTexture::FILTER_GOOD : value;
    resetTexture();
}

// FUNCTION: SURRENDER 0x10018750
void srGERD::setTextureDefaultMipmap(srTexture::e_mipmap value)
{
    texture_state.default_mipmap =
        value == srTexture::MIPMAP_DEFAULT ? srTexture::MIPMAP_FASTEST : value;
    resetTexture();
}
