#include "wiz8/bink_video.h"

#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "movie.h"
#include "wiz8/render/frame.h"
#include "wiz8/runtime_test_hooks.h"

namespace {
bool copy(const W8NativeVideo::Frame& frame, SDL_Surface* target)
{
    if (!target || frame.pixels.empty()) return false;
    const SDL_PixelFormatDetails* format = SDL_GetPixelFormatDetails(target->format);
    if (!format || target->w < frame.width || target->h < frame.height ||
        SDL_BYTESPERPIXEL(target->format) != 2 || format->Rmask != 0x7c00 ||
        format->Gmask != 0x3e0 || format->Bmask != 0x1f)
        return false;
    if (!target->pixels) return false;
    return SDL_ConvertPixels(frame.width, frame.height, SDL_PIXELFORMAT_XRGB1555,
                             frame.pixels.data(), frame.width * 2, target->format, target->pixels,
                             target->pitch);
}

} // namespace
W8BinkVideo::W8BinkVideo() : m_handle(nullptr), m_target(nullptr) {}
W8BinkVideo::~W8BinkVideo()
{
    delete m_handle;
}
unsigned char W8BinkVideo::Open(const char* path, int flags)
{
    // Retail can open the next intro on the same owner. Stop the old audio first.
    delete m_handle;
    m_handle = nullptr;
    WIZ8_TEST_HOOK(if (g_runtime_test_hooks.skip_movies) return 0;)
    try {
        if (!path || flags) throw std::runtime_error("Unsupported movie open parameters");
        auto video = std::make_unique<W8NativeVideo>();
        video->open(path);
        m_handle = video.release();
        return 1;
    } catch (const std::exception& failure) {
        fprintf(stderr, "Wizardry movie: %s\n", failure.what());
        return 0;
    }
}
unsigned char W8BinkVideo::UpdateFrame()
{
    if (!m_handle) return 0;
    try {
        auto result = m_handle->update_now();
        if (result == W8NativeVideo::Done) return 1;
        if (result == W8NativeVideo::FrameReady) {
            if (!(m_target ? CopyFrameToTargetSurface() : CopyFrameToPrimarySurface()))
                throw std::runtime_error("Movie surface does not support RGB555 output");
            m_handle->present(m_target ? m_target : &GetFrameBufferSurface());
            WIZ8_TEST_HOOK(++g_runtime_test_hooks.movie_frames_presented;)
        }
        return 0;
    } catch (const std::exception& failure) {
        fprintf(stderr, "Wizardry movie: %s\n", failure.what());
        return 1;
    }
}
unsigned char W8BinkVideo::CopyFrameToPrimarySurface()
{
    if (!m_handle || !copy(m_handle->frame(), &GetFrameBufferSurface())) return 0;
    InvalidateRegion(0, 0, m_handle->frame().width, m_handle->frame().height);
    return 1;
}
unsigned char W8BinkVideo::CopyFrameToTargetSurface()
{
    return m_handle && copy(m_handle->frame(), m_target);
}
void W8BinkVideo::SetTarget(SDL_Surface* target)
{
    if (!target) return;
    m_target = target;
    SDL_FillSurfaceRect(target, nullptr, 0);
}
