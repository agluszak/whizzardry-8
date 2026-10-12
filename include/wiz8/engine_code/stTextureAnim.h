#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "surrender/srTexture.h"
#include "wiz8/texture_animation.h"
#include "compat/compiler.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srTypeRegistry.h"

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

    virtual std::uint32_t getTextureFrameHandle() override;               /* 0x004856F0 */
    virtual float getPriority() override;                                 /* 0x00484D60 */
    Dimensions getDimensions() override;          /* 0x00484D80 */
    virtual void getMipmapData(const MultiRequest& request) override;           /* 0x00484DB0 */
    virtual void getMipmapLevelPartial(PartialRequest& request) override; /* 0x00484DE0 */
    Parameters getParameters() override;        /* 0x00484E10 */
    virtual const char* getTextureName() override;                        /* 0x00484E40 */
    virtual void invalidate() override;                                   /* 0x004023A0 */

    void SetFrame(std::int32_t frame);
    void AddTexture(std::shared_ptr<srTextureIFace> texture);
    std::int32_t IsFinished() const;
    std::uint8_t Prepare();
    virtual void setupDefaultValues() override; /* 0x00485760 */

public:
    stTextureAnim& operator=(const stTextureAnim&) = default;
    void UpdateFrame();

    std::vector<std::shared_ptr<srTextureIFace>> textures;
    std::int32_t frame = 0;
    std::int32_t direction = 1;
    W8TextureAnimationMode animation_mode = W8_TEXTURE_ANIM_LOOP;
    std::int32_t initial_frame = 0;
    float frame_rate = 15.0f;
    std::uint32_t frame_tick;
    W8TextureTriggerMode trigger_mode = W8_TEXTURE_TRIGGER_CONTINUOUS;
    float probability = -1.0f;
    bool running = false;

private:
    srTextureIFace& current()
    {
        return *textures[frame];
    }
};
