#pragma once
#include <cstdint>
#include <memory>
#include <vector>
struct IDirectDrawSurface2;
struct W8NativeVideo
{
    enum Result
    {
        Waiting,
        FrameReady,
        Done
    };
    struct Frame
    {
        int width, height;
        double time, duration;
        std::vector<uint16_t> pixels;
    };
    W8NativeVideo();
    ~W8NativeVideo();
    void open(const char* path);
    Result update(double elapsed);
    Result update_now();
    void present(IDirectDrawSurface2* target);
    const Frame& frame() const;
    unsigned decoded_frames() const;
    uint64_t decoded_audio_frames() const;

  private:
    struct State;
    std::unique_ptr<State> state;
};
