#pragma once

#include <SDL3/SDL_surface.h>
#include <string>
#include <string_view>
#include "Font.h"

#include "wiz8/local_code/ControlsRect.h"

/* Shared declaration owner; implementation remains in Local Code\Controls.cpp. */

extern const unsigned int g_W8TextBufferAlignLeft;
extern const unsigned int g_W8TextBufferAlignCenter;
extern const unsigned int g_W8TextBufferAlignRight;
extern const unsigned int g_W8TextBufferAlignMiddle;
extern const unsigned int g_W8TextBufferAlignTop;
extern const unsigned int g_W8TextBufferAlignBottom;
extern const unsigned int g_W8TextBufferNoWrap;

// VTABLE: WIZ8 0x005ed5b8
class W8TextBuffer {
public:
    friend class W8DialogTextArea;
    W8TextBuffer();
    W8TextBuffer(const W8ControlsRect* bounds, std::string_view text, Font& font,
                 unsigned int layout_mode, int render_mode);
    std::string_view GetText() const
    {
        return m_text;
    }
    unsigned int GetLineHeight();
    int GetHorizontalPosition(int width);
    int GetVerticalPosition();
    void SetLineHeight(unsigned int height);
    void FillBounds(int colour);
    void RenderText(SDL_Surface& target, int x_offset, int y_offset, bool force);
    void RenderToTarget(int offset, bool force, SDL_Surface& target);
    void UpdateLayout();
    void SetLayoutMode(unsigned int layout_mode);
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

    void SetLayoutBounds(int left, int top, int right, int bottom)
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
    void SetRenderMode(int mode)
    {
        m_renderMode = mode;
    }
    void SetFontStateIndex(int index)
    {
        m_fontStateIndex = index;
    }
    void MarkGeometryDirty(unsigned int mode)
    {
        m_geometryDirty = true;
        m_layoutMode = mode;
    }

    virtual ~W8TextBuffer();

protected:
    W8ControlsRect m_layoutBounds;  /* current absolute layout bounds */
    W8ControlsRect m_pendingBounds; /* mirrored pending bounds */
    int m_field_24;                 /* the constructor steps over this one */
    Font* m_font;                   /* font used for uncached line height */
public:
    /* The tooltip builder in Video2.cpp reads the finished layout directly. */
    int m_lineCount;

protected:
    unsigned int m_lineHeight; /* cached height, zero means query font */
    std::string m_text;        // Original text; wrapping never modifies it.
    std::string m_buffer;      // Rebuilt layout text, owned by this control.
    unsigned int m_layoutMode; /* 10 initially */
public:
    unsigned int m_maxLineWidth;

public:
    /* The party-selection controller raises the geometry-dirty byte directly
       when it changes modes; retain that observed public storage access. */
    bool m_geometryDirty;
    unsigned char m_alternateRenderer;
    unsigned char pad_42[2];
    int m_renderMode;     /* 4 initially */
    int m_fontStateIndex; /* -1 skips the state-table override */
    bool m_highlighted;
};

inline void DestroyDialogTextBuffers(W8TextBuffer** buffers, int count)
{
    for (int index = 0; index < count; ++index) {
        if (buffers[index] != 0) {
            delete buffers[index];
            buffers[index] = 0;
        }
    }
}
