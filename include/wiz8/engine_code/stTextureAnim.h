#pragma once

#include "surrender/srTexture.h"
#include <memory>
#include <vector>
#include "wiz8/texture_animation.h"

enum W8TextureTriggerMode {
    W8_TEXTURE_TRIGGER_CONTINUOUS = 0,
    W8_TEXTURE_TRIGGER_RANDOM_FRAME = 1,
    W8_TEXTURE_TRIGGER_RANDOM_START = 2
};

class stTextureAnim : public srClassSupport<stTextureAnim, srTexture, 0x10000> {
public:
    static const char* sGetClassName()
    {
        return "stTextureAnim";
    }

    stTextureAnim();                           /* 0x00484BE0 */
    stTextureAnim(const stTextureAnim& other); /* 0x00485070 */

    virtual w8_ulong getTextureFrameHandle() override;               /* 0x004856F0 */
    virtual float getPriority() override;                                 /* 0x00484D60 */
    virtual void getDimensions(Dimensions& dimensions) override;          /* 0x00484D80 */
    virtual void getMipmapData(MultiRequest& request) override;           /* 0x00484DB0 */
    virtual void getMipmapLevelPartial(PartialRequest& request) override; /* 0x00484DE0 */
    virtual void getTextureParms(Parameters& parameters) override;        /* 0x00484E10 */
    virtual const char* getTextureName() override;                        /* 0x00484E40 */
    virtual void invalidate() override;                                   /* 0x004023A0 */

    void SetFrame(int frame);
    void AddTexture(std::shared_ptr<srTextureIFace> texture);
    int IsFinished() const;
    unsigned char Prepare();
    virtual void setupDefaultValues() override; /* 0x00485760 */

public:
    stTextureAnim& operator=(const stTextureAnim&) = default;
    void UpdateFrame();

    std::vector<std::shared_ptr<srTextureIFace>> textures;
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
    srTextureIFace& current()
    {
        return *textures[frame];
    }
};
