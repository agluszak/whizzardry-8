/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "line.h"

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
// GLOBAL: WIZ8 0x00650fa0
int giClipXMin = 0;
// GLOBAL: WIZ8 0x00650fa4
int giClipXMax = 0;
// GLOBAL: WIZ8 0x00650fa8
int giClipYMin = 0;
// GLOBAL: WIZ8 0x00650fac
int giClipYMax = 0;

static void DrawPixelRun(char** screen, int length, int color, int pixel_step, int next_run_step);

// FUNCTION: WIZ8 0x00413fd0
void SetClippingRegionAndImageWidth(int iImageWidth, int iClipStartX, int iClipStartY,
                                    int iClipWidth, int iClipHeight)
{
    giImageWidth = iImageWidth;
    giClipXMin = iClipStartX;
    giClipXMax = iClipStartX + iClipWidth - 1;
    giClipYMin = iClipStartY;
    giClipYMax = iClipStartY + iClipHeight - 1;
}

bool Clipt(FLOAT denom, FLOAT num, FLOAT* tE, FLOAT* tL)
{
    FLOAT t;
    bool accept;

    accept = TRUE;

    if (denom > 0.0f) {
        t = num / denom;
        if (t > *tL)
            accept = FALSE;
        else if (t > *tE)
            *tE = t;
    } else if (denom < 0.0f) {
        t = num / denom;
        if (t < *tE)
            accept = FALSE;
        else if (t < *tL)
            *tL = t;
    } else if (num > 0)
        accept = FALSE;

    return (accept);
}

bool ClipPoint(int x, int y)
{
    return (x <= giClipXMax && x >= giClipXMin && y <= giClipYMax && y >= giClipYMin);
}

// FUNCTION: WIZ8 0x00414010
bool Clip2D(int* ix0, int* iy0, int* ix1, int* iy1)
{
    bool visible;
    FLOAT te, tl;
    FLOAT dx, dy;
    FLOAT x0, y0, x1, y1;

    x0 = (FLOAT)*ix0;
    x1 = (FLOAT)*ix1;
    y0 = (FLOAT)*iy0;
    y1 = (FLOAT)*iy1;

    dx = x1 - x0;
    dy = y1 - y0;
    visible = FALSE;

    if (dx == 0.0 && dy == 0.0 && ClipPoint(*ix0, *iy0))
        visible = TRUE;
    else {
        te = 0.0f;
        tl = 1.0f;
        if (Clipt(dx, (FLOAT)giClipXMin - x0, &te, &tl)) {
            if (Clipt(-dx, x0 - (FLOAT)giClipXMax, &te, &tl)) {
                if (Clipt(dy, (FLOAT)giClipYMin - y0, &te, &tl)) {
                    if (Clipt(-dy, y0 - (FLOAT)giClipYMax, &te, &tl)) {
                        visible = TRUE;
                        if (tl < 1.0f) {
                            x1 = x0 + tl * dx;
                            y1 = y0 + tl * dy;
                        }
                        if (te > 0) {
                            x0 = x0 + te * dx;
                            y0 = y0 + te * dy;
                        }
                    }
                }
            }
        }
    }

    *ix0 = (int)x0;
    *ix1 = (int)x1;
    *iy0 = (int)y0;
    *iy1 = (int)y1;

    return (visible);
}

/* Draws a line between the specified endpoints in color Color. */
// FUNCTION: WIZ8 0x00414390
void LineDraw(bool fClip, int XStart, int YStart, int XEnd, int YEnd, short Color, char* ScreenPtr)
{
    int Temp, AdjUp, AdjDown, ErrorTerm, XAdvance, XDelta, YDelta;
    int WholeStep, InitialPixelCount, FinalPixelCount, i, RunLength;
    char col2 = Color >> 8;
    char col1 = Color & 0x00FF;

    if (fClip) {
        if (!Clip2D(&XStart, &YStart, &XEnd, &YEnd))
            return;
    }

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
