#include <cstddef>

#include "compat/compiler.h"
#include "surrender/srGERD.h"
#include "surrender/srTextureIFace.h"

// Resolves a requested DEFAULT to its concrete choice, then mirrors that choice's entry into the
// DEFAULT slot of the lookup table.
template<class Enum, std::size_t N>
static void resolveDefault(w8_ulong (&map)[N], Enum requested, Enum fallback, Enum default_value)
{
    if (requested == default_value) requested = fallback;
    map[static_cast<std::size_t>(default_value)] = map[static_cast<std::size_t>(requested)];
}

// FUNCTION: SURRENDER 0x10018650
void srGERD::setTextureDefaultMagFilter(srTextureIFace::e_filter filter)
{
    resolveDefault(texture_state.mag_filter_map, filter, srTextureIFace::FILTER_GOOD,
                   srTextureIFace::FILTER_DEFAULT);
    resetTexture();
}

// FUNCTION: SURRENDER 0x100186D0
void srGERD::setTextureDefaultMinFilter(srTextureIFace::e_filter filter)
{
    resolveDefault(texture_state.min_filter_map, filter, srTextureIFace::FILTER_GOOD,
                   srTextureIFace::FILTER_DEFAULT);
    resetTexture();
}

// FUNCTION: SURRENDER 0x10018750
void srGERD::setTextureDefaultMipmap(srTextureIFace::e_mipmap mipmap)
{
    resolveDefault(texture_state.mipmap_map, mipmap, srTextureIFace::MIPMAP_FASTEST,
                   srTextureIFace::MIPMAP_DEFAULT);
    resetTexture();
}
