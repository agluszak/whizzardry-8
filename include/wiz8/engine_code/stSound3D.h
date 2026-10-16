#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "soundman.h"
#include "surrender/srNode.h"
#include "wiz8/audio_resources.h"
#include "wiz8/geometry.h"

class stSound3D;
extern std::vector<stSound3D*> g_sound3d_instances;

/* General positional-audio node, recovered in src/wiz8/engine_code/Spells.cpp.
   That placement is provisional: the original-TU evidence has a gap over its
   0x004AE6D0..0x004AEFD0 span, bounded by Spells.cpp (up to 0x004ADB20) below
   and OctBuildPreTree.cpp (from 0x004B19F0) above, so a dedicated audio
   translation unit is at least as likely; no body carries a source-path
   string to settle it. Move the bodies when the gap is attributed. */
class stSound3D : public srNode {
public:
    stSound3D(const char* sound_name, srNode* parent); /* 0x004AE6D0 */
    stSound3D(const stSound3D& other) : stSound3D(other.wave_name.c_str(), nullptr)
    {
        *this = other;
    }
    virtual ~stSound3D() override;
    bool IsPlaying(); /* 0x004AEC70 */
    void Stop();      /* 0x004AEC90 */
    /* loop repeats the playback until stopped; auto_release
       makes the update pass release the node once playback ends. */
    unsigned char Play(bool loop, bool auto_release); /* 0x004AEBF0 */
    wiz8::sound::PlayOptions BuildSoundOptions(const glm::vec3& listener); /* 0x004AECC0 */

    int unknown_138; /* 0x138: ctor zeroes it; no retail reader found */
    wiz8::sound::Playback sound_handle; // Current playback; empty when silent.
    int volume; /* 0x140: base volume before distance/effects scale */
    float falloff; /* 0x144: audible range in world units */
    std::string wave_name;
    bool auto_release; /* 0x14c: release the node when playback ends */

    /* Copy the wave filename, but leave a live playback handle unshared. */
    stSound3D& operator=(const stSound3D& other)
    {
        if (this == &other) return *this;
        Stop();
        srNode::operator=(other);
        wave_name = other.wave_name;
        falloff = other.falloff;
        volume = other.volume;
        auto_release = other.auto_release;
        return *this;
    }
};
