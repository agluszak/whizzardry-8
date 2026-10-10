#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/GrCycle.h"

#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/stTextureFile.h"

#include <algorithm>
#include <stdlib.h>

// VTABLE: WIZ8 0x005EC9C0
// class stTextureAnim

// VTABLE: WIZ8 0x005ECA04
// class srClassSupport<stTextureAnim,srTexture,65536>

// FUNCTION: WIZ8 0x00484BE0
stTextureAnim::stTextureAnim() : frame_tick(w8_get_ticks()) {}

/* Copies share the frame textures but restart playback. */
// FUNCTION: WIZ8 0x00485070
stTextureAnim::stTextureAnim(const stTextureAnim& other)
    : textures(other.textures), animation_mode(other.animation_mode),
      initial_frame(other.initial_frame), frame_rate(other.frame_rate),
      frame_tick(w8_get_ticks()), trigger_mode(other.trigger_mode),
      probability(other.probability), running(other.running)
{
}

// FUNCTION: WIZ8 0x00485400
void stTextureAnim::SetFrame(int frame)
{
    this->frame = frame;
    frame_tick = w8_get_ticks();
}

// FUNCTION: WIZ8 0x00485420
void stTextureAnim::AddTexture(std::shared_ptr<srTextureIFace> texture)
{
    textures.push_back(std::move(texture));
}

// FUNCTION: WIZ8 0x004854B0
void stTextureAnim::UpdateFrame()
{
    const int count = static_cast<int>(textures.size());

    if (animation_mode == W8_TEXTURE_ANIM_MANUAL) {
        return;
    }

    if (trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_FRAME) {
        if (rand() / static_cast<float>(RAND_MAX) < probability) {
            frame = static_cast<int>(rand() / static_cast<float>(RAND_MAX) * count);
        }
        return;
    }

    if (trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_START) {
        if (!running && rand() / static_cast<float>(RAND_MAX) < probability) {
            running = true;
            direction = 0;
            frame = 0;
            frame_tick = w8_get_ticks();
        }
        if (!running) {
            return;
        }
    }
    if (count == 0) {
        return;
    }

    const int elapsed_frames =
        static_cast<int>((w8_get_ticks() - frame_tick) * frame_rate * g_float_one_thousandth);
    if (animation_mode == W8_TEXTURE_ANIM_LOOP) {
        int next = (direction * elapsed_frames) % count;
        if (next < frame) {
            frame = 0;
            running = false;
            return;
        }
        frame = next;
    } else if (animation_mode == W8_TEXTURE_ANIM_PING_PONG) {
        if ((elapsed_frames / count & 1) != 0) {
            direction = -1;
            frame = count - elapsed_frames % count - 1;
        } else {
            if (direction == -1) {
                running = false;
                return;
            }
            direction = 1;
            frame = elapsed_frames % count;
        }
    } else if (animation_mode == W8_TEXTURE_ANIM_PLAY_ONCE) {
        if (elapsed_frames >= count) {
            running = false;
            frame = count - 1;
        } else {
            frame = elapsed_frames;
        }
    }
}

// FUNCTION: WIZ8 0x00485730
int stTextureAnim::IsFinished() const
{
    return animation_mode == W8_TEXTURE_ANIM_PLAY_ONCE &&
           frame == std::max(static_cast<int>(textures.size()) - 1, 0);
}

// FUNCTION: WIZ8 0x004856F0
w8_ulong stTextureAnim::getTextureFrameHandle()
{
    UpdateFrame();
    if ((texture_flags_ & (1UL << FLAG_DIRTY_DEFAULTS)) != 0) {
        setupDefaultValues();
    }
    return current().getTextureFrameHandle();
}

// FUNCTION: WIZ8 0x00484D60
float stTextureAnim::getPriority()
{
    return current().getPriority();
}

// FUNCTION: WIZ8 0x00484D80
void stTextureAnim::getDimensions(Dimensions& dimensions)
{
    current().getDimensions(dimensions);
}

// FUNCTION: WIZ8 0x00484DB0
void stTextureAnim::getMipmapData(MultiRequest& request)
{
    current().getMipmapData(request);
}

// FUNCTION: WIZ8 0x00484DE0
void stTextureAnim::getMipmapLevelPartial(PartialRequest& request)
{
    current().getMipmapLevelPartial(request);
}

// FUNCTION: WIZ8 0x00484E10
void stTextureAnim::getTextureParms(Parameters& parameters)
{
    current().getTextureParms(parameters);
}

// FUNCTION: WIZ8 0x00484E40
const char* stTextureAnim::getTextureName()
{
    return current().getTextureName();
}

void stTextureAnim::invalidate() {}

// FUNCTION: WIZ8 0x00485760
void stTextureAnim::setupDefaultValues()
{
    srTextureIFace* texture = textures.empty() ? nullptr : textures[0].get();

    if (texture != 0) {
        texture->getDimensions(texture_dimensions_);
        texture_flags_ &= ~2U; /* FLAG_DIRTY_DEFAULTS */
    } else {
        texture_dimensions_.width = 1;
        texture_dimensions_.height = 1;
        texture_dimensions_.palette = 0;
    }
}

// FUNCTION: WIZ8 0x004857B0
unsigned char stTextureAnim::Prepare()
{
    auto* file = textures.empty() ? nullptr : sr_cast<stTextureFile>(textures[0]);
    if (file != 0) {
        file->getTextureFrameHandle();
        return file->hasAlpha();
    }
    return texture_dimensions_.format.alpha_bits != 0;
}
