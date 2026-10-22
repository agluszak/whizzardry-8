#include "wiz8/render/quality.h"

#include <array>
#include <exception>

#include "wiz8/chunk.h"
#include "wiz8/filesystem.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/texture.h"

namespace {
std::array<unsigned char, W8_RENDER_OPTION_COUNT> option_states{};
}

void InitializeRenderQuality(void)
{
    option_states.fill(0);
}

float g_render_brightness = 1.0f;
float g_render_fog_distance = 0.5f;
bool g_render_missile_lights = true;
bool g_render_mesh_sky = true;

void EnableRenderOption(W8RenderOption option)
{
    if (option < W8_RENDER_OPTION_COUNT) {
        SetRenderOption(option, 1);
    }
}

void SetRenderOption(W8RenderOption option, int enabled)
{
    if (option == W8_RENDER_OPTION_RESERVED_5 || option == W8_RENDER_OPTION_RESERVED_12 ||
        option == W8_RENDER_OPTION_RESERVED_16)
        return;
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
    case W8_RENDER_OPTION_TEXTURE_MAG_FILTER:
        Texture::defaults.mag = enabled ? Texture::FILTER_BEST : Texture::FILTER_NONE;
        break;
    case W8_RENDER_OPTION_TEXTURE_MIN_FILTER:
        Texture::defaults.min = enabled ? Texture::FILTER_BEST : Texture::FILTER_NONE;
        break;
    case W8_RENDER_OPTION_MIP_MAPPING:
        Texture::defaults.mipmap = enabled ? Texture::MIPMAP_BEST : Texture::MIPMAP_NONE;
        break;
    case W8_RENDER_OPTION_MESH_SKY:
        g_render_mesh_sky = enabled != 0;
        break;
    case W8_RENDER_OPTION_HIGH_TEXTURE_DETAIL:
        SetResidentTexturePolicy(enabled ? 0 : 1);
        break;
    case W8_RENDER_OPTION_VIDEO_SYNC:
        SetSwapInterval(enabled != 0);
        break;
    }
    if (option < W8_RENDER_OPTION_COUNT) {
        option_states[option] = enabled != 0;
    }
}

void DisableRenderOption(W8RenderOption option)
{
    if (option < W8_RENDER_OPTION_COUNT) {
        SetRenderOption(option, 0);
    }
}

void DisableAllRenderOptions(void)
{
    int option = 0;

    do {
        SetRenderOption(static_cast<W8RenderOption>(option), 0);
        ++option;
    } while (option < W8_RENDER_OPTION_COUNT);
}

void EnableAllRenderOptions(void)
{
    for (int option = 0; option < W8_RENDER_OPTION_COUNT; ++option) {
        SetRenderOption(static_cast<W8RenderOption>(option), 1);
    }
}

/* Out-of-range reads report zero rather than indexing past the block. */
unsigned char GetRenderOptionState(W8RenderOption option)
{
    if (option >= W8_RENDER_OPTION_COUNT) {
        return 0;
    }
    return option_states[option];
}

unsigned char LoadRenderOptions(W8Chunk& handle)
try {
    int version;
    unsigned char options[W8_RENDER_OPTION_COUNT];

    handle.ReadBytes(&version, sizeof(version));
    if (version != 1) {
        return 0;
    }
    handle.ReadBytes(options, sizeof(options));
    for (int option = 0; option < W8_RENDER_OPTION_COUNT; ++option) {
        SetRenderOption(static_cast<W8RenderOption>(option), options[option] != 0);
    }
    return 1;
} catch (const std::exception&) {
    return false;
}

bool SaveRenderOptions(wiz8::File& handle)
{
    int version = 1;

    handle.write(&version, sizeof(version));
    handle.write(option_states.data(), option_states.size());
    return true;
}
