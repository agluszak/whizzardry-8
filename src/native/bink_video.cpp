#include "wiz8/bink_video.h"
#include "compat/surfaces.h"
#include "movie.h"
#include "wiz8/engine_code/Video2.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace
{
bool copy(const W8NativeVideo::Frame& frame, IDirectDrawSurface2* target)
{
    if (!target || frame.pixels.empty())
        return false;
    DDSURFACEDESC description{};
    DDGetSurfaceDescription(target, &description);
    if (description.dwWidth < unsigned(frame.width) ||
        description.dwHeight < unsigned(frame.height) ||
        description.ddpfPixelFormat.dwRGBBitCount != 16 ||
        description.ddpfPixelFormat.dwRBitMask != 0x7c00 ||
        description.ddpfPixelFormat.dwGBitMask != 0x3e0 ||
        description.ddpfPixelFormat.dwBBitMask != 0x1f)
        return false;
    DDLockSurface(target, nullptr, &description, 0, nullptr);
    if (!description.lpSurface)
        return false;
    for (int y = 0; y < frame.height; ++y)
        memcpy(static_cast<unsigned char*>(description.lpSurface) + y * description.lPitch,
               frame.pixels.data() + y * frame.width, frame.width * 2);
    DDUnlockSurface(target, nullptr);
    return true;
}

} // namespace
W8BinkVideo::W8BinkVideo() : m_handle(nullptr), unknown_04{}, m_target(nullptr)
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
void W8BinkVideo::SetTarget(IDirectDrawSurface2* target)
{
    if (!target)
        return;
    m_target = target;
    DDBLTFX effects{};
    effects.dwSize = sizeof(effects);
    DDBltSurface(target, nullptr, nullptr, nullptr, DDBLT_COLORFILL, &effects);
}
