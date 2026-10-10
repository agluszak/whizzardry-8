#include "wiz8/bink_video.h"
#include "compat/surfaces.h"
#include "movie.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/runtime_test_hooks.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace
{
bool copy(const W8NativeVideo::Frame& frame, CpuSurface* target)
{
    if (!target || frame.pixels.empty())
        return false;
    SurfaceLock description{};
    description.width = target->surface->w;
    description.height = target->surface->h;
    if (description.width < frame.width || description.height < frame.height ||
        SDL_BYTESPERPIXEL(target->surface->format) != 2 ||
        target->redMask != 0x7c00 ||
        target->greenMask != 0x3e0 ||
        target->blueMask != 0x1f)
        return false;
    description = LockCpuSurface(*target);
    if (!description.pixels)
        return false;
    for (int y = 0; y < frame.height; ++y)
        memcpy(static_cast<unsigned char*>(description.pixels) + y * description.pitch,
               frame.pixels.data() + y * frame.width, frame.width * 2);
    UnlockCpuSurface(*target);
    return true;
}

} // namespace
W8BinkVideo::W8BinkVideo() : m_handle(nullptr), m_target(nullptr)
{
}
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
    try
    {
        if (!path || flags)
            throw std::runtime_error("Unsupported movie open parameters");
        auto video = std::make_unique<W8NativeVideo>();
        video->open(path);
        m_handle = video.release();
        return 1;
    }
    catch (const std::exception& failure)
    {
        fprintf(stderr, "Wizardry movie: %s\n", failure.what());
        return 0;
    }
}
unsigned char W8BinkVideo::UpdateFrame()
{
    if (!m_handle)
        return 0;
    try
    {
        auto result = m_handle->update_now();
        if (result == W8NativeVideo::Done)
            return 1;
        if (result == W8NativeVideo::FrameReady)
        {
            if (!(m_target ? CopyFrameToTargetSurface() : CopyFrameToPrimarySurface()))
                throw std::runtime_error("Movie surface does not support RGB555 output");
            m_handle->present(m_target ? m_target : g_primary_surface);
            WIZ8_TEST_HOOK(++g_runtime_test_hooks.movie_frames_presented;)
        }
        return 0;
    }
    catch (const std::exception& failure)
    {
        fprintf(stderr, "Wizardry movie: %s\n", failure.what());
        return 1;
    }
}
unsigned char W8BinkVideo::CopyFrameToPrimarySurface()
{
    if (!m_handle || !copy(m_handle->frame(), g_primary_surface))
        return 0;
    InvalidateRegion(0, 0, m_handle->frame().width, m_handle->frame().height, 0);
    return 1;
}
unsigned char W8BinkVideo::CopyFrameToTargetSurface()
{
    return m_handle && copy(m_handle->frame(), m_target);
}
void W8BinkVideo::SetTarget(CpuSurface* target)
{
    if (!target)
        return;
    m_target = target;
    FillCpuSurface(*target, 0);
}
