#ifndef WIZ8_BINK_VIDEO_H
#define WIZ8_BINK_VIDEO_H

#include <cstdint>
#include "compat/compiler.h"

struct W8NativeVideo;
struct SDL_Surface;

/* Movie playback backed by FFmpeg. */
class W8BinkVideo {
public:
    W8BinkVideo();
    ~W8BinkVideo();

    std::uint8_t Open(const char* path, std::int32_t flags);
    std::uint8_t UpdateFrame();
    std::uint8_t CopyFrameToPrimarySurface();
    std::uint8_t CopyFrameToTargetSurface();
    void SetTarget(SDL_Surface* target);

private:
    W8NativeVideo* m_handle;
    SDL_Surface* m_target; /* 0x08 */
};

#endif
