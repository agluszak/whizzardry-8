#pragma once

namespace wiz8 {
class File;
}
/* Engine option ids. The UI mapping plus recovered consumers establish
   the live user-visible entries: mip mapping calls the SR mipmap setter,
   missile lights gate spell/missile lights, mesh sky gates the separate sky
   world, video sync sets swap interval,
   additional animations selects the richer monster-cycle values, and blurred
   text applies the half-pixel 2D correction. */
enum W8RenderOption {
    W8_RENDER_OPTION_TEXTURE_MAG_FILTER = 2,
    W8_RENDER_OPTION_TEXTURE_MIN_FILTER = 3,
    W8_RENDER_OPTION_MIP_MAPPING = 4,
    // Reserved QLTY byte; the old dither switch had no rendering effect.
    W8_RENDER_OPTION_RESERVED_5 = 5,
    W8_RENDER_OPTION_BRIGHTNESS = 6,
    W8_RENDER_OPTION_MISSILE_LIGHTS = 9,
    W8_RENDER_OPTION_MESH_SKY = 10,
    W8_RENDER_OPTION_HIGH_TEXTURE_DETAIL = 11,
    W8_RENDER_OPTION_RESERVED_12 = 12,
    W8_RENDER_OPTION_VIDEO_SYNC = 13,
    W8_RENDER_OPTION_ADDITIONAL_ANIMATIONS = 14,
    /* PlayFootstep (0x0047a440) and the audio panel mute button agree on 15. */
    W8_RENDER_OPTION_FOOTSTEP_SOUND = 15,
    W8_RENDER_OPTION_CORRECT_BLURRED_TEXT = 16,
    W8_RENDER_OPTION_COUNT = 17
};

void InitializeRenderQuality(void);
void SetRenderOption(W8RenderOption option, int enabled);
void EnableRenderOption(W8RenderOption option);
void DisableRenderOption(W8RenderOption option);
void DisableAllRenderOptions(void);
void EnableAllRenderOptions(void);
unsigned char GetRenderOptionState(W8RenderOption option);
unsigned char LoadRenderOptions(wiz8::File& handle);
bool SaveRenderOptions(wiz8::File& handle);

extern float g_render_brightness;
extern float g_render_fog_distance;
extern bool g_render_missile_lights;
extern bool g_render_mesh_sky;
