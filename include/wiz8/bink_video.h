#ifndef WIZ8_BINK_VIDEO_H
#define WIZ8_BINK_VIDEO_H

struct W8NativeVideo;
struct IDirectDrawSurface2;

/* Movie playback backed by FFmpeg. */
class W8BinkVideo {
public:
    W8BinkVideo();
    ~W8BinkVideo();

    unsigned char Open(const char* path, int flags);
    unsigned char UpdateFrame();
    unsigned char CopyFrameToPrimarySurface();
    unsigned char CopyFrameToTargetSurface();
    void SetTarget(IDirectDrawSurface2* target);

private:
    W8NativeVideo* m_handle;
    unsigned char unknown_04[4];   /* 0x04: constructor clears; scalar type unresolved */
    IDirectDrawSurface2* m_target; /* 0x08 */
};

W8_ABI_ASSERT(sizeof(W8BinkVideo) == 0x0c, "W8BinkVideo_must_be_0x0c");

#endif
