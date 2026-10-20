#pragma once

// Command line and output helpers shared by the library prototypes.
//
//   --frames N        render N frames, then exit (default: run until closed)
//   --time SECONDS    freeze the scene at this time instead of animating
//   --screenshot PNG  save the last frame
//   --verify          compare the GPU vertex stage against shadeVertices
//   --size WxH        window size (default 960x720)
//   --hidden          do not show the window (CI and headless runs)

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace render_poc {

struct Options {
    int width = 960;
    int height = 720;
    std::optional<int> frames;
    std::optional<double> time;
    std::filesystem::path screenshot;
    bool verify = false;
    bool hidden = false;
};

Options parseOptions(int argc, char** argv);

// Writes RGBA8 pixels. `bottom_up` flips rows read back from OpenGL.
void savePng(const std::filesystem::path& path, std::span<const std::uint8_t> rgba, int width,
             int height, bool bottom_up);

// Frame pacing and a summary of CPU time spent planning and submitting.
class FrameClock {
public:
    double sceneTime(const Options& options) const;
    void frameDone();
    std::string summary() const;

private:
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last = start;
    int frames = 0;
    double total_seconds = 0;
};

} // namespace render_poc
