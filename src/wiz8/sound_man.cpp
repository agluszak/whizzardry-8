#include "wiz8/sound_man.h"
#include "soundman.h"

/* Original translation unit is not established by the surrounding source anchors. */

// FUNCTION: WIZ8 0x00479010
void ConfigureSoundCache(void)
{
    SoundSetCacheThreshhold(0xc8000);
}
