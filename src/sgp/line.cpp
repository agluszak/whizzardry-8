/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "line.h"

#include <SDL3/SDL_rect.h>

#include "Types.h"
//				Example Usage

//	SEND THE PITCH IN BYTES
//	SetClippingRegionAndImageWidth( uiPitch, 15, 15, 30, 30 );
//	LineDraw( TRUE, 10, 10, 200, 200, colour, pImageData);
//    OR
//	RectangleDraw( TRUE, 10, 10, 200, 200, colour, pImageData);
//				Line Drawing Functions

// GLOBAL: WIZ8 0x00650f9c
int giImageWidth = 0;
// The clipping rectangle uses SDL's half-open bounds. The original
// min/max coordinates were inclusive, so the stored width/height match
// the caller's dimensions without subtracting one.
static SDL_Rect g_line_clip{};

static void DrawPixelRun(char** screen, int length, int color, int pixel_step, int next_run_step);

// FUNCTION: WIZ8 0x00413fd0
void SetClippingRegionAndImageWidth(int iImageWidth, int iClipStartX, int iClipStartY,
                                    int iClipWidth, int iClipHeight)
{
    giImageWidth = iImageWidth;
    g_line_clip = {iClipStartX, iClipStartY, iClipWidth, iClipHeight};
}

/* Draws a line between the specified endpoints in color Color. */
// FUNCTION: WIZ8 0x00414390
void LineDraw(bool fClip, int XStart, int YStart, int XEnd, int YEnd, short Color, char* ScreenPtr)
{
    int Temp, AdjUp, AdjDown, ErrorTerm, XAdvance, XDelta, YDelta;
    int WholeStep, InitialPixelCount, FinalPixelCount, i, RunLength;
    char col2 = Color >> 8;
    char col1 = Color & 0x00FF;

    if (fClip && !SDL_GetRectAndLineIntersection(
                     &g_line_clip, &XStart, &YStart, &XEnd, &YEnd))
        return;

    /*	We'll always draw top to bottom, to reduce the number of cases we have to
		handle, and to make lines between the same endpoints draw the same pixels */
    if (YStart > YEnd) {
        Temp = YStart;
        YStart = YEnd;
        YEnd = Temp;
        Temp = XStart;
        XStart = XEnd;
        XEnd = Temp;
    }

    // point to the bitmap address first pixel to draw
    ScreenPtr = ScreenPtr + YStart * giImageWidth + XStart * 2;

    /*	Figure out whether we're going left or right, and how far we're
		going horizontally */
    if ((XDelta = XEnd - XStart) < 0) {
        XAdvance = -1;
        XDelta = -XDelta;
    } else {
        XAdvance = 1;
    }
    /* Figure out how far we're going vertically */
    YDelta = YEnd - YStart;

    /* Special-case horizontal, vertical, and diagonal lines, for speed
	and to avoid nasty boundary conditions and division by 0 */
    if (XDelta == 0) {
        /* Vertical line */
        for (i = 0; i <= YDelta; i++) {
            ScreenPtr[0] = col1;
            ScreenPtr[1] = col2;
            ScreenPtr += giImageWidth;
        }
        return;
    }
    if (YDelta == 0) {
        /* Horizontal line */
        for (i = 0; i <= XDelta; i++) {
            ScreenPtr[0] = col1;
            ScreenPtr[1] = col2;
            ScreenPtr += XAdvance * 2;
        }
        return;
    }
    if (XDelta == YDelta) {
        /* Diagonal line */
        for (i = 0; i <= XDelta; i++) {
            ScreenPtr[0] = col1;
            ScreenPtr[1] = col2;
            ScreenPtr += (XAdvance * 2) + giImageWidth;
        }
        return;
    }

    const bool x_major = XDelta >= YDelta;
    const int major_delta = x_major ? XDelta : YDelta;
    const int minor_delta = x_major ? YDelta : XDelta;
    const int pixel_step = x_major ? XAdvance * 2 : giImageWidth;
    const int next_run_step = x_major ? giImageWidth : XAdvance * 2;
    const auto draw_run = [&](int length) {
        DrawPixelRun(&ScreenPtr, length, Color, pixel_step, next_run_step);
    };

    WholeStep = major_delta / minor_delta;
    AdjUp = (major_delta % minor_delta) * 2;
    AdjDown = minor_delta * 2;
    ErrorTerm = (major_delta % minor_delta) - AdjDown;
    // Split a full run plus the initial pixel between the two partial endpoint runs.
    InitialPixelCount = (WholeStep / 2) + 1;
    FinalPixelCount = InitialPixelCount;
    // Allocate an even-length tie to the final run; carry an odd half-pixel into the error.
    if (AdjUp == 0 && (WholeStep & 1) == 0) {
        --InitialPixelCount;
    }
    if ((WholeStep & 1) != 0) {
        ErrorTerm += minor_delta;
    }
    draw_run(InitialPixelCount);
    for (i = 0; i < minor_delta - 1; ++i) {
        RunLength = WholeStep;
        if ((ErrorTerm += AdjUp) > 0) {
            ++RunLength;
            ErrorTerm -= AdjDown;
        }
        draw_run(RunLength);
    }
    draw_run(FinalPixelCount);
}

static void DrawPixelRun(char** screen, int length, int color, int pixel_step, int next_run_step)
{
    char* pixel = *screen;
    const char high = color >> 8;
    const char low = color & 0x00ff;
    for (int i = 0; i < length; ++i) {
        pixel[0] = low;
        pixel[1] = high;
        pixel += pixel_step;
    }
    *screen = pixel + next_run_step;
}

/* Draws a rectangle between the specified endpoints in color Color. */
// FUNCTION: WIZ8 0x00414660
void RectangleDraw(bool fClip, int XStart, int YStart, int XEnd, int YEnd, short Color,
                   char* ScreenPtr)
{
    LineDraw(fClip, XStart, YStart, XEnd, YStart, Color, ScreenPtr);
    LineDraw(fClip, XStart, YEnd, XEnd, YEnd, Color, ScreenPtr);
    LineDraw(fClip, XStart, YStart, XStart, YEnd, Color, ScreenPtr);
    LineDraw(fClip, XEnd, YStart, XEnd, YEnd, Color, ScreenPtr);
}

/***********************************************************************************
* 8-Bit Versions
*
*
*
* Added by Derek Beland
***********************************************************************************/
