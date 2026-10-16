#include "wiz8/engine_code/Quality.h"

#include <array>
#include <exception>

#include "surrender/srGERD.h"
#include "surrender/srTextureIFace.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/filesystem.h"

namespace {
std::array<unsigned char, W8_RENDER_OPTION_COUNT> option_states{};
}

void InitializeRenderQuality(void)
{
    option_states.fill(0);
}

// GLOBAL: WIZ8 0x0060A210
float g_render_brightness = 1.0f;
// GLOBAL: WIZ8 0x0060E610
float g_render_fog_distance = 0.5f;
// GLOBAL: WIZ8 0x0060A20C
bool g_render_missile_lights = true;
// GLOBAL: WIZ8 0x00603C6C
bool g_render_mesh_sky = true;

// FUNCTION: WIZ8 0x0047b590
void EnableRenderOption(W8RenderOption option)
{
    if (option < W8_RENDER_OPTION_COUNT) {
        SetRenderOption(option, 1);
    }
}

// FUNCTION: WIZ8 0x0047b630
void SetRenderOption(W8RenderOption option, int enabled)
{
    /* The remaining numeric cases have not been assigned reliable option names. */
    switch (static_cast<int>(option)) {
    case W8_RENDER_OPTION_BRIGHTNESS:
        g_render_brightness = enabled ? 1.0f : 0.8f;
        break;
    case 7:
        if (!enabled && g_render_fog_distance < 0.7f) g_render_fog_distance = 0.7f;
        if (enabled && g_render_fog_distance > 0.3f) g_render_fog_distance = 0.3f;
        break;
    case 8:
        if (!enabled && g_render_fog_distance < 0.9f) g_render_fog_distance = 0.9f;
        if (enabled && g_render_fog_distance > 0.1f) g_render_fog_distance = 0.1f;
        break;
    case W8_RENDER_OPTION_MISSILE_LIGHTS:
        g_render_missile_lights = enabled != 0;
        break;
    case W8_RENDER_OPTION_DITHER:
        if ((g_gerd->isEnabled(srGERD::ENABLE_POSITIONAL_0) != 0) != (enabled != 0)) {
            g_gerd->toggle(srGERD::ENABLE_POSITIONAL_0);
        }
        break;
    case W8_RENDER_OPTION_TEXTURE_MAG_FILTER:
        g_gerd->setTextureDefaultMagFilter(enabled ? srTextureIFace::FILTER_BEST
                                                   : srTextureIFace::FILTER_NONE);
        break;
    case W8_RENDER_OPTION_TEXTURE_MIN_FILTER:
        g_gerd->setTextureDefaultMinFilter(enabled ? srTextureIFace::FILTER_BEST
                                                   : srTextureIFace::FILTER_NONE);
        break;
    case W8_RENDER_OPTION_MIP_MAPPING:
        g_gerd->setTextureDefaultMipmap(enabled ? srTextureIFace::MIPMAP_BEST
                                                : srTextureIFace::MIPMAP_NONE);
        break;
    case W8_RENDER_OPTION_MESH_SKY:
        g_render_mesh_sky = enabled != 0;
        break;
    case W8_RENDER_OPTION_HIGH_TEXTURE_DETAIL:
        SetResidentTexturePolicy(enabled ? 0 : 1);
        break;
    case W8_RENDER_OPTION_HIGH_TEXTURE_CACHE:
        SetTextureCacheSize(enabled ? 0x2000000 : 0x1000000);
        break;
    case W8_RENDER_OPTION_VIDEO_SYNC:
        SetSwapInterval(enabled != 0);
        break;
    case W8_RENDER_OPTION_CORRECT_BLURRED_TEXT:
        SetSurfaceScale(enabled ? 0.5f : 0.0f);
        break;
    }
    if (option < W8_RENDER_OPTION_COUNT) {
        option_states[option] = enabled != 0;
    }
}

// FUNCTION: WIZ8 0x0047b5b0
void DisableRenderOption(W8RenderOption option)
{
    if (option < W8_RENDER_OPTION_COUNT) {
        SetRenderOption(option, 0);
    }
}

// FUNCTION: WIZ8 0x0047b5d0
void DisableAllRenderOptions(void)
{
    int option = 0;

    do {
        SetRenderOption(static_cast<W8RenderOption>(option), 0);
        ++option;
    } while (option < W8_RENDER_OPTION_COUNT);
}

// FUNCTION: WIZ8 0x0047b5f0
void EnableAllRenderOptions(void)
{
    for (int option = 0; option < W8_RENDER_OPTION_COUNT; ++option) {
        SetRenderOption(static_cast<W8RenderOption>(option), 1);
    }
}

/* Out-of-range reads report zero rather than indexing past the block. */
// FUNCTION: WIZ8 0x0047b610
unsigned char GetRenderOptionState(W8RenderOption option)
{
    if (option >= W8_RENDER_OPTION_COUNT) {
        return 0;
    }
    return option_states[option];
}

// FUNCTION: WIZ8 0x0047b890
unsigned char LoadRenderOptions(wiz8::File& handle)
try {
    int version;
    unsigned char options[W8_RENDER_OPTION_COUNT];

    handle.read_exact(&version, sizeof(version));
    if (version != 1) {
        return 0;
    }
    handle.read_exact(options, sizeof(options));
    for (int option = 0; option < W8_RENDER_OPTION_COUNT; ++option) {
        SetRenderOption(static_cast<W8RenderOption>(option), options[option] != 0);
    }
    return 1;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x0047b920
bool SaveRenderOptions(wiz8::File& handle)
{
    int version = 1;

    handle.write(&version, sizeof(version));
    handle.write(option_states.data(), option_states.size());
    return true;
}
