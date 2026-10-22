#pragma once

#include <string>
#include <string_view>

#include <SDL3/SDL_surface.h>

#include "wiz8/canvas/Font.h"
#include "wiz8/local_code/ControlsRect.h"

extern char g_W8TextSeparator[];

extern const unsigned int g_W8TextBufferAlignLeft;
extern const unsigned int g_W8TextBufferAlignCenter;
extern const unsigned int g_W8TextBufferAlignRight;
extern const unsigned int g_W8TextBufferAlignMiddle;
extern const unsigned int g_W8TextBufferAlignTop;
extern const unsigned int g_W8TextBufferAlignBottom;
extern const unsigned int g_W8TextBufferNoWrap;

class W8TextBuffer {
public:
    friend class W8DialogTextArea;
    W8TextBuffer() = default;
    W8TextBuffer(const W8ControlsRect* bounds, std::string_view text, Font& font,
                 unsigned int layout_mode, int render_mode);
    std::string_view GetText() const { return m_text; }
    unsigned int GetLineHeight();
    int GetHorizontalPosition(int width);
    int GetVerticalPosition();
    void SetLineHeight(unsigned int height);
    void FillBounds(SDL_Surface& target, int colour);
    void RenderText(SDL_Surface& target, int x_offset, int y_offset, bool force);
    void RenderToTarget(int offset, bool force, SDL_Surface& target);
    void Paint(SDL_Surface& target, int offset = 0);
    void UpdateLayout();
    void SetLayoutMode(unsigned int layout_mode);
    void SetClipBounds(const W8ControlsRect& bounds) { m_clipBounds = bounds; }
    void SetText(std::string_view text, Font& font);
    void SetLayoutBounds(const W8ControlsRect* bounds, bool update_layout = true);

    void RenderString(std::string_view text, Font& font, bool force, SDL_Surface& target)
    {
        SetText(text, font);
        RenderToTarget(0, force, target);
    }

    void RenderString(const W8ControlsRect* bounds, std::string_view text, Font& font, bool force,
                      SDL_Surface& target)
    {
        SetClipBounds(*bounds);
        SetLayoutBounds(bounds, true);
        RenderString(text, font, force, target);
    }

    void SetLayoutBounds(int left, int top, int right, int bottom)
    {
        m_layoutBounds.left = left;
        m_layoutBounds.top = top;
        m_layoutBounds.right = right;
        m_layoutBounds.bottom = bottom;
        m_clipBounds = m_layoutBounds;
    }

    void SetGeometryDirty() { m_geometryDirty = true; }
    void SetRenderMode(int mode) { m_renderMode = mode; }
    void SetFontStateIndex(int index) { m_fontStateIndex = index; }
    void MarkGeometryDirty(unsigned int mode)
    {
        m_geometryDirty = true;
        m_layoutMode = mode;
    }

    W8TextBuffer(const W8TextBuffer&) = default;
    W8TextBuffer& operator=(const W8TextBuffer&) = default;
    virtual ~W8TextBuffer() = default;

private:
    void PaintLines(SDL_Surface& target, SDL_Point offset, bool highlighted, bool mono);

protected:
    W8ControlsRect m_layoutBounds{};  /* current absolute layout bounds */
    W8ControlsRect m_clipBounds{}; /* clip remains fixed while layout scrolls */
    Font* m_font = nullptr;                     /* font used for uncached line height */
public:
    /* The tooltip builder in Video2.cpp reads the finished layout directly. */
    int m_lineCount = 0;

protected:
    unsigned int m_lineHeight = 0; /* cached height, zero means query font */
    std::string m_text; // Original text; wrapping never modifies it.
    std::string m_buffer; // Rebuilt layout text, owned by this control.
    unsigned int m_layoutMode = 10; /* 10 initially */
public:
    unsigned int m_maxLineWidth = 0;

public:
    /* The party-selection controller raises the geometry-dirty byte directly
       when it changes modes; retain that observed public storage access. */
    bool m_geometryDirty = false;
    unsigned char m_alternateRenderer = 0;
    int m_renderMode = 4;     /* 4 initially */
    int m_fontStateIndex = -1; /* -1 skips the state-table override */
    bool m_highlighted = false;
};
