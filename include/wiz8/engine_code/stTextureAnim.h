#pragma once

#include <memory>
#include <vector>

#include "compat/compiler.h"
#include "surrender/srTexture.h"
#include "wiz8/texture_animation.h"

enum W8TextureTriggerMode {
    W8_TEXTURE_TRIGGER_CONTINUOUS = 0,
    W8_TEXTURE_TRIGGER_RANDOM_FRAME = 1,
    W8_TEXTURE_TRIGGER_RANDOM_START = 2
};

class stTextureAnim : public srTexture {
public:
    stTextureAnim();                           /* 0x00484BE0 */
    stTextureAnim(const stTextureAnim& other); /* 0x00485070 */

    srTexture* resolve() override;               /* 0x004856F0 */
    virtual void invalidate() override;                                   /* 0x004023A0 */

    void SetFrame(int frame);
    void AddTexture(std::shared_ptr<srTexture> texture);
    int IsFinished() const;
    unsigned char Prepare();
    virtual void setupDefaultValues() override; /* 0x00485760 */

public:
    stTextureAnim& operator=(const stTextureAnim&) = default;
    void UpdateFrame();

    std::vector<std::shared_ptr<srTexture>> textures;
    int frame = 0;
    int direction = 1;
    W8TextureAnimationMode animation_mode = W8_TEXTURE_ANIM_LOOP;
    int initial_frame = 0;
    float frame_rate = 15.0f;
    w8_ulong frame_tick;
    W8TextureTriggerMode trigger_mode = W8_TEXTURE_TRIGGER_CONTINUOUS;
    float probability = -1.0f;
    bool running = false;

private:
    srTexture& current() { return *textures[frame]; }
};
