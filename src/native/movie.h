#pragma once
#include <cstdint>
#include <memory>
#include <vector>
struct SDL_Surface;
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
        std::int32_t width, height;
        double time, duration;
        std::vector<std::uint16_t> pixels;
    };
    W8NativeVideo();
    ~W8NativeVideo();
    void open(const char* path);
    Result update(double elapsed);
    Result update_now();
    void present(SDL_Surface* target);
    const Frame& frame() const;
    std::uint32_t decoded_frames() const;
    std::uint64_t decoded_audio_frames() const;

  private:
    struct State;
    std::unique_ptr<State> state;
};
