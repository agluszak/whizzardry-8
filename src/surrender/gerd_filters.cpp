#include <cstddef>
#include <mutex>

#include "compat/compiler.h"
#include "surrender/srGERD.h"
#include "surrender/srTextureIFace.h"

// Resolves a requested DEFAULT to its concrete choice, then mirrors that choice's entry into the
// DEFAULT slot of the lookup table.
template<class Enum, std::size_t N>
static void resolveDefault(Enum& stored, w8_ulong (&map)[N], Enum requested, Enum fallback,
                           Enum default_value)
{
    if (requested == default_value) {
        requested = fallback;
    }
    stored = requested;
    map[static_cast<std::size_t>(default_value)] = map[static_cast<std::size_t>(requested)];
}

// FUNCTION: SURRENDER 0x10018550
void srGERD::setTextureDefaultCorrection(srTextureIFace::e_correction correction)
{
    std::lock_guard access(state_section);
    resolveDefault(texture_state.default_correction, texture_state.correction_map, correction,
                   srTextureIFace::CORRECTION_GOOD, srTextureIFace::CORRECTION_DEFAULT);
    resetTexture();
}

// FUNCTION: SURRENDER 0x100185D0
void srGERD::setTextureDefaultCompression(srTextureIFace::e_compression compression)
{
    std::lock_guard access(state_section);
    /* COMPRESSION_DEFAULT (4) collapses to entry 0 through the mask. */
    compression = static_cast<srTextureIFace::e_compression>(
        compression & ((compression == srTextureIFace::COMPRESSION_DEFAULT) - 1));
    texture_state.default_compression = compression;
    texture_state.default_texture_params[4] = texture_state.default_texture_params[compression];
    resetTexture();
}

// FUNCTION: SURRENDER 0x100187D0
srTextureIFace::e_compression srGERD::getTextureDefaultCompression() const
{
    std::lock_guard access(state_section);
    return texture_state.default_compression;
}

// FUNCTION: SURRENDER 0x100187F0
srTextureIFace::e_correction srGERD::getTextureDefaultCorrection() const
{
    std::lock_guard access(state_section);
    return texture_state.default_correction;
}

// FUNCTION: SURRENDER 0x10018810
srTextureIFace::e_filter srGERD::getTextureDefaultMagFilter() const
{
    std::lock_guard access(state_section);
    return texture_state.default_mag_filter;
}

// FUNCTION: SURRENDER 0x10018830
srTextureIFace::e_filter srGERD::getTextureDefaultMinFilter() const
{
    std::lock_guard access(state_section);
    return texture_state.default_min_filter;
}

// FUNCTION: SURRENDER 0x10018850
srTextureIFace::e_mipmap srGERD::getTextureDefaultMipmap() const
{
    std::lock_guard access(state_section);
    return texture_state.default_mipmap;
}

// FUNCTION: SURRENDER 0x10018650
void srGERD::setTextureDefaultMagFilter(srTextureIFace::e_filter filter)
{
    std::lock_guard access(state_section);
    resolveDefault(texture_state.default_mag_filter, texture_state.mag_filter_map, filter,
                   srTextureIFace::FILTER_GOOD, srTextureIFace::FILTER_DEFAULT);
    resetTexture();
}

// FUNCTION: SURRENDER 0x100186D0
void srGERD::setTextureDefaultMinFilter(srTextureIFace::e_filter filter)
{
    std::lock_guard access(state_section);
    resolveDefault(texture_state.default_min_filter, texture_state.min_filter_map, filter,
                   srTextureIFace::FILTER_GOOD, srTextureIFace::FILTER_DEFAULT);
    resetTexture();
}

// FUNCTION: SURRENDER 0x10018750
void srGERD::setTextureDefaultMipmap(srTextureIFace::e_mipmap mipmap)
{
    std::lock_guard access(state_section);
    resolveDefault(texture_state.default_mipmap, texture_state.mipmap_map, mipmap,
                   srTextureIFace::MIPMAP_FASTEST, srTextureIFace::MIPMAP_DEFAULT);
    resetTexture();
}
