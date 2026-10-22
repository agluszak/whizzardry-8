#pragma once
#include <cstdint>
#include <memory>
#include <vector>
struct SDL_Surface;
/* FFmpeg movie playback in timed RGB555 frames. */
struct W8NativeVideo {
    enum Result { Waiting, FrameReady, Done };
    struct Frame {
        int width, height;
        double time, duration;
        std::vector<uint16_t> pixels;
    };
    W8NativeVideo();
    ~W8NativeVideo();
    /* Decode `path`, replacing any movie already open. Throws when it cannot be read. */
    void open(const char* path);
    Result update(double elapsed);
    Result update_now();
    const Frame& frame() const;
    /* Open `path` to play onto `target`, an RGB555 surface at least as large as the
       movie. The previous movie stops first. False, after logging, when it cannot play. */
    bool play(const char* path, SDL_Surface& target);
    /* Advance on the monotonic clock and show a frame that is due. True once the
       movie has ended or failed. */
    bool advance();
    unsigned decoded_frames() const;
    uint64_t decoded_audio_frames() const;

private:
    void show();

    struct State;
    std::unique_ptr<State> state;
    SDL_Surface* target = nullptr;
};
