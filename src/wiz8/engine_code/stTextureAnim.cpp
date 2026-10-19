#include "wiz8/engine_code/stTextureAnim.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <SDL3/SDL_pixels.h>

#include "compat/native.h"
#include "surrender/srTexture.h"
#include "texture_animation.h"
#include "wiz8/engine_code/stTextureFile.h"
#include "wiz8/platform/random.h"

stTextureAnim::stTextureAnim() : frame_tick(w8_get_ticks()) {}

/* Copies share the frame textures but restart playback. */
stTextureAnim::stTextureAnim(const stTextureAnim& other)
    : textures(other.textures), animation_mode(other.animation_mode),
      initial_frame(other.initial_frame), frame_rate(other.frame_rate), frame_tick(w8_get_ticks()),
      trigger_mode(other.trigger_mode), probability(other.probability), running(other.running)
{
}

void stTextureAnim::SetFrame(int frame)
{
    this->frame = frame;
    frame_tick = w8_get_ticks();
}

void stTextureAnim::AddTexture(std::shared_ptr<srTexture> texture)
{
    textures.push_back(std::move(texture));
}

void stTextureAnim::UpdateFrame()
{
    const int count = static_cast<int>(textures.size());

    if (animation_mode == W8_TEXTURE_ANIM_MANUAL) {
        return;
    }

    if (trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_FRAME) {
        if (Random15() / static_cast<float>(0x7fff) < probability) {
            frame = static_cast<int>(Random15() / static_cast<float>(0x7fff) * count);
        }
        return;
    }

    if (trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_START) {
        if (!running && Random15() / static_cast<float>(0x7fff) < probability) {
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
        static_cast<int>((w8_get_ticks() - frame_tick) * frame_rate * 0.001f);
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

int stTextureAnim::IsFinished() const
{
    return animation_mode == W8_TEXTURE_ANIM_PLAY_ONCE &&
           frame == std::max(static_cast<int>(textures.size()) - 1, 0);
}

srTexture* stTextureAnim::resolve()
{
    UpdateFrame();
    return textures.empty() ? nullptr : current().resolve();
}

void stTextureAnim::invalidate() {}

void stTextureAnim::setupDefaultValues()
{
    srTexture* texture = textures.empty() ? nullptr : textures[0].get();

    if (texture != 0) {
        texture_dimensions_ = texture->getDimensions();
        texture_flags_ &= ~2U; /* FLAG_DIRTY_DEFAULTS */
    } else {
        texture_dimensions_.width = 1;
        texture_dimensions_.height = 1;
    }
}

unsigned char stTextureAnim::Prepare()
{
    auto* file = textures.empty() ? nullptr : dynamic_cast<stTextureFile*>(textures[0].get());
    if (file != 0) {
        file->resolve();
        return file->hasAlpha();
    }
    return SDL_ISPIXELFORMAT_ALPHA(texture_dimensions_.format);
}
