#pragma once

#include <cstdint>
#include <SDL3/SDL_surface.h>
#include <string>
#include <string_view>
#include "Font.h"

#include "wiz8/local_code/ControlsRect.h"

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

extern const std::uint32_t g_W8TextBufferAlignLeft;
extern const std::uint32_t g_W8TextBufferAlignCenter;
extern const std::uint32_t g_W8TextBufferAlignRight;
extern const std::uint32_t g_W8TextBufferAlignMiddle;
extern const std::uint32_t g_W8TextBufferAlignTop;
extern const std::uint32_t g_W8TextBufferAlignBottom;
extern const std::uint32_t g_W8TextBufferNoWrap;

// VTABLE: WIZ8 0x005ed5b8
class W8TextBuffer {
public:
    friend class W8DialogTextArea;
    W8TextBuffer();
    W8TextBuffer(const W8ControlsRect* bounds, std::string_view text, Font& font,
                 std::uint32_t layout_mode, std::int32_t render_mode);
    std::string_view GetText() const { return m_text; }
    std::uint32_t GetLineHeight();
    std::int32_t GetHorizontalPosition(std::int32_t width);
    std::int32_t GetVerticalPosition();
    void SetLineHeight(std::uint32_t height);
    void FillBounds(std::int32_t colour);
    void RenderText(SDL_Surface& target, std::int32_t x_offset, std::int32_t y_offset, bool force);
    void RenderToTarget(std::int32_t offset, bool force, SDL_Surface& target);
    void UpdateLayout();
    void SetLayoutMode(std::uint32_t layout_mode);
    void SetText(std::string_view text, Font& font);
    void SetLayoutBounds(const W8ControlsRect* bounds, bool copy_pending, bool update_layout);

    void RenderString(std::string_view text, Font& font, bool force, SDL_Surface& target)
    {
        SetText(text, font);
        RenderToTarget(0, force, target);
    }

    void RenderString(const W8ControlsRect* bounds, std::string_view text, Font& font, bool force,
                      SDL_Surface& target)
    {
        SetLayoutBounds(bounds, true, true);
        RenderString(text, font, force, target);
    }

    void SetLayoutBounds(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
    {
        m_layoutBounds.left = left;
        m_layoutBounds.top = top;
        m_layoutBounds.right = right;
        m_layoutBounds.bottom = bottom;
        m_pendingBounds = m_layoutBounds;
    }

    bool HasBuffer() const
    {
        return !m_buffer.empty();
    }
    void SetGeometryDirty()
    {
        m_geometryDirty = true;
    }
    void SetRenderMode(std::int32_t mode)
    {
        m_renderMode = mode;
    }
    void SetFontStateIndex(std::int32_t index)
    {
        m_fontStateIndex = index;
    }
    void MarkGeometryDirty(std::uint32_t mode)
    {
        m_geometryDirty = true;
        m_layoutMode = mode;
    }

    virtual ~W8TextBuffer();

protected:
    W8ControlsRect m_layoutBounds;  /* current absolute layout bounds */
    W8ControlsRect m_pendingBounds; /* mirrored pending bounds */
    std::int32_t m_field_24;                 /* the constructor steps over this one */
    Font* m_font;                     /* font used for uncached line height */
public:
    /* The tooltip builder in Video2.cpp reads the finished layout directly. */
    std::int32_t m_lineCount;

protected:
    std::uint32_t m_lineHeight; /* cached height, zero means query font */
    std::string m_text; // Original text; wrapping never modifies it.
    std::string m_buffer; // Rebuilt layout text, owned by this control.
    std::uint32_t m_layoutMode; /* 10 initially */
public:
    std::uint32_t m_maxLineWidth;

public:
    /* The party-selection controller raises the geometry-dirty byte directly
       when it changes modes; retain that observed public storage access. */
    bool m_geometryDirty;
    std::uint8_t m_alternateRenderer;
    std::uint8_t pad_42[2];
    std::int32_t m_renderMode;     /* 4 initially */
    std::int32_t m_fontStateIndex; /* -1 skips the state-table override */
    bool m_highlighted;
};

inline void DestroyDialogTextBuffers(W8TextBuffer** buffers, std::int32_t count)
{
    for (std::int32_t index = 0; index < count; ++index) {
        if (buffers[index] != 0) {
            delete buffers[index];
            buffers[index] = 0;
        }
    }
}
