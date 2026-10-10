#include "wiz8/containers.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/engine_code/GrCycle.h"

#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/stTextureFile.h"

#include <stdlib.h>

// VTABLE: WIZ8 0x005EC9C0
// class stTextureAnim

// VTABLE: WIZ8 0x005ECA04
// class srClassSupport<stTextureAnim,srTexture,65536>

// FUNCTION: WIZ8 0x00484BE0
stTextureAnim::stTextureAnim()
{
    textures = 0;
    frame = 0;
    direction = 1;
    animation_mode = W8_TEXTURE_ANIM_LOOP;
    initial_frame = 0;
    frame_rate = 15.0f;
    frame_tick = w8_get_ticks();
    trigger_mode = W8_TEXTURE_TRIGGER_CONTINUOUS;
    probability = -1.0f;
    running = false;
    textures = new std::vector<srTextureIFace*>;
}

// FUNCTION: WIZ8 0x00484E60
srClass* stTextureAnim::vInstance()
{
    return new stTextureAnim;
}

// FUNCTION: WIZ8 0x00485070
stTextureAnim::stTextureAnim(const stTextureAnim& other)
{
    int i;

    textures = 0;
    frame = 0;
    direction = 1;
    animation_mode = other.animation_mode;
    initial_frame = other.initial_frame;
    frame_rate = other.frame_rate;
    frame_tick = w8_get_ticks();
    trigger_mode = other.trigger_mode;
    probability = other.probability;
    running = other.running;
    textures = new std::vector<srTextureIFace*>;

    for (i = 0; i < static_cast<int>(other.textures->size()); ++i) {
        srTextureIFace* texture = (*other.textures)[i];
        textures->push_back(texture);
        texture->addReference();
    }
}

// FUNCTION: WIZ8 0x00485290
stTextureAnim::~stTextureAnim()
{
    if (IsTextureInReadMeshScratch(this)) {
        ReleaseReadMeshScratch();
    }

    wiz8::clear_and_dispose(*textures, [](auto* value) { value->release(); });
    delete textures;
}

// FUNCTION: WIZ8 0x00485400
void stTextureAnim::SetFrame(int frame)
{
    this->frame = frame;
    frame_tick = w8_get_ticks();
}

// FUNCTION: WIZ8 0x00485420
void stTextureAnim::AddTexture(srTextureIFace* texture)
{
    textures->push_back(texture);
    texture->addReference();
}

// FUNCTION: WIZ8 0x004854B0
void stTextureAnim::UpdateFrame()
{
    int elapsed_frames;

    if (animation_mode == W8_TEXTURE_ANIM_MANUAL) {
        return;
    }

    if (trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_FRAME) {
        if (rand() / static_cast<float>(RAND_MAX) < probability) {
            this->frame = static_cast<int>(rand() / static_cast<float>(RAND_MAX) *
                                           static_cast<int>(textures->size()));
        }
        return;
    }

    if (trigger_mode == W8_TEXTURE_TRIGGER_RANDOM_START) {
        if (!running && rand() / static_cast<float>(RAND_MAX) < probability) {
            running = true;
            direction = 0;
            this->frame = 0;
            frame_tick = w8_get_ticks();
        }
        if (!running || textures->empty()) {
            return;
        }
    } else if (textures->empty()) {
        return;
    }

    elapsed_frames =
        static_cast<int>((w8_get_ticks() - frame_tick) * frame_rate * g_float_one_thousandth);
    if (animation_mode == W8_TEXTURE_ANIM_LOOP) {
        int frame = (direction * elapsed_frames) % static_cast<int>(textures->size());
        if (frame < this->frame) {
            this->frame = 0;
            running = false;
            return;
        }
        this->frame = frame;
    } else if (animation_mode == W8_TEXTURE_ANIM_PING_PONG) {
        if ((elapsed_frames / static_cast<int>(textures->size()) & 1) != 0) {
            direction = -1;
            this->frame = static_cast<int>(textures->size()) -
                          elapsed_frames % static_cast<int>(textures->size()) - 1;
        } else {
            if (direction == -1) {
                running = false;
                return;
            }
            direction = 1;
            this->frame = elapsed_frames % static_cast<int>(textures->size());
        }
    } else if (animation_mode == W8_TEXTURE_ANIM_PLAY_ONCE) {
        if (elapsed_frames >= static_cast<int>(textures->size())) {
            running = false;
            this->frame = static_cast<int>(textures->size()) - 1 < 0
                              ? 0
                              : static_cast<int>(textures->size()) - 1;
        } else {
            this->frame = elapsed_frames;
        }
    }
}

// FUNCTION: WIZ8 0x00485730
int stTextureAnim::IsFinished() const
{
    if (animation_mode != W8_TEXTURE_ANIM_PLAY_ONCE) {
        return 0;
    }

    int final_frame = static_cast<int>(textures->size()) - 1;
    final_frame = final_frame < 0 ? 0 : final_frame;
    if (frame != final_frame) {
        return 0;
    }
    return 1;
}

// FUNCTION: WIZ8 0x004856F0
w8_ulong stTextureAnim::getTextureFrameHandle()
{
    UpdateFrame();
    if ((texture_flags_ & (1UL << FLAG_DIRTY_DEFAULTS)) != 0) {
        setupDefaultValues();
    }
    return ((*textures)[frame])->getTextureFrameHandle();
}

// FUNCTION: WIZ8 0x00484D60
float stTextureAnim::getPriority() { return ((*textures)[frame])->getPriority(); }

// FUNCTION: WIZ8 0x00484D80
void stTextureAnim::getDimensions(Dimensions& dimensions)
{
    ((*textures)[frame])->getDimensions(dimensions);
}

// FUNCTION: WIZ8 0x00484DB0
void stTextureAnim::getMipmapData(MultiRequest& request)
{
    ((*textures)[frame])->getMipmapData(request);
}

// FUNCTION: WIZ8 0x00484DE0
void stTextureAnim::getMipmapLevelPartial(PartialRequest& request)
{
    ((*textures)[frame])->getMipmapLevelPartial(request);
}

// FUNCTION: WIZ8 0x00484E10
void stTextureAnim::getTextureParms(Parameters& parameters)
{
    ((*textures)[frame])->getTextureParms(parameters);
}

// FUNCTION: WIZ8 0x00484E40
const char* stTextureAnim::getTextureName() { return ((*textures)[frame])->getTextureName(); }

void stTextureAnim::invalidate() {}

// FUNCTION: WIZ8 0x00485760
void stTextureAnim::setupDefaultValues()
{
    srTextureIFace* texture = (*textures)[0];

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
    srTextureIFace* texture = (*textures)[0];

    if (texture != 0 && texture->getClassID() == stTextureFile::CLASS_ID) {
        texture->getTextureFrameHandle();
        return static_cast<stTextureFile*>(texture)->hasAlpha();
    }
    return texture_dimensions_.format.alpha_bits != 0;
}
