#ifndef WIZ8_BINK_VIDEO_H
#define WIZ8_BINK_VIDEO_H

struct W8NativeVideo;
struct SDL_Surface;

/* Movie playback backed by FFmpeg. */
class W8BinkVideo {
public:
    W8BinkVideo();
    ~W8BinkVideo();
    W8BinkVideo(const W8BinkVideo&) = delete;
    W8BinkVideo& operator=(const W8BinkVideo&) = delete;

    unsigned char Open(const char* path, int flags);
    unsigned char UpdateFrame();
    unsigned char CopyFrameToPrimarySurface();
    unsigned char CopyFrameToTargetSurface();
    void SetTarget(SDL_Surface* target);

private:
    W8NativeVideo* m_handle;
    SDL_Surface* m_target; /* 0x08 */
};

#endif
