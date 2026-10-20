#include "poc_app.h"

#include <cstdio>
#include <format>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

namespace render_poc {

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        const auto value = [&]() -> std::string_view {
            if (++index >= argc)
                throw std::invalid_argument(std::format("{} needs a value", argument));
            return argv[index];
        };
        if (argument == "--frames") {
            options.frames = std::stoi(std::string(value()));
        } else if (argument == "--time") {
            options.time = std::stod(std::string(value()));
        } else if (argument == "--screenshot") {
            options.screenshot = value();
        } else if (argument == "--verify") {
            options.verify = true;
        } else if (argument == "--herd") {
            options.herd = std::stoi(std::string(value()));
            if (options.herd < 1) throw std::invalid_argument("--herd needs at least 1");
        } else if (argument == "--hidden") {
            options.hidden = true;
        } else if (argument == "--size") {
            const auto size = std::string(value());
            if (std::sscanf(size.c_str(), "%dx%d", &options.width, &options.height) != 2)
                throw std::invalid_argument("--size expects WxH");
        } else {
            throw std::invalid_argument(std::format("Unknown option {}", argument));
        }
    }
    return options;
}

void savePng(const std::filesystem::path& path, std::span<const std::uint8_t> rgba, int width,
             int height, bool bottom_up)
{
    std::vector<std::uint8_t> rows(rgba.begin(), rgba.end());
    if (bottom_up) {
        const auto pitch = static_cast<std::size_t>(width) * 4;
        for (int y = 0; y < height; ++y)
            std::copy_n(rgba.data() + (height - 1 - y) * pitch, pitch, rows.data() + y * pitch);
    }
    auto* surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, rows.data(), width * 4);
    if (!surface) throw std::runtime_error(SDL_GetError());
    const bool saved = IMG_SavePNG(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    if (!saved) throw std::runtime_error(SDL_GetError());
}

double FrameClock::sceneTime(const Options& options) const
{
    if (options.time) return *options.time;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void FrameClock::frameDone()
{
    const auto now = std::chrono::steady_clock::now();
    total_seconds += std::chrono::duration<double>(now - last).count();
    last = now;
    ++frames;
}

std::string FrameClock::summary() const
{
    if (frames == 0) return "no frames";
    return std::format("{} frames, {:.2f} ms per frame", frames, total_seconds * 1000 / frames);
}

} // namespace render_poc
