#pragma once
/* Native CPU surface contracts used by the recovered SGP/Video2 code.
   These are host descriptions, not serialized DirectDraw/COM structures.
   Flag values come from the Microsoft DirectDraw SDK. */
#include "Types.h"
#include "compat/kernel32.h"

struct IDirectDraw;
struct IDirectDraw2;
struct IDirectDrawSurface2;
typedef IDirectDrawSurface2 IDirectDrawSurface;
struct IDirectDrawPalette;
struct IDirectDrawClipper;
typedef IDirectDraw* LPDIRECTDRAW;
typedef IDirectDraw2* LPDIRECTDRAW2;
typedef IDirectDrawSurface* LPDIRECTDRAWSURFACE;
typedef IDirectDrawSurface2* LPDIRECTDRAWSURFACE2;
typedef IDirectDrawPalette* LPDIRECTDRAWPALETTE;
typedef IDirectDrawClipper* LPDIRECTDRAWCLIPPER;
struct DDPIXELFORMAT
{
    DWORD dwSize, dwFlags, dwRGBBitCount, dwRBitMask, dwGBitMask, dwBBitMask, dwRGBAlphaBitMask;
};
struct DDSCAPS
{
    DWORD dwCaps;
};
struct DDSURFACEDESC
{
    DWORD dwSize, dwFlags, dwHeight, dwWidth;
    LONG lPitch;
    void* lpSurface;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS ddsCaps;
};
typedef DDSURFACEDESC* LPDDSURFACEDESC;
struct DDBLTFX
{
    DWORD dwSize, dwFillColor;
};
typedef DDBLTFX* LPDDBLTFX;
struct DDCOLORKEY
{
    DWORD dwColorSpaceLowValue, dwColorSpaceHighValue;
};
typedef DDCOLORKEY* LPDDCOLORKEY;
struct PALETTEENTRY
{
    BYTE peRed, peGreen, peBlue, peFlags;
};
typedef PALETTEENTRY* LPPALETTEENTRY;
struct RGNDATAHEADER
{
    DWORD dwSize, iType, nCount, nRgnSize;
    RECT rcBound;
};
struct RGNDATA
{
    RGNDATAHEADER rdh;
    char Buffer[1];
};
typedef RGNDATA* LPRGNDATA;
#define DD_OK 0
#define DDSD_CAPS 0x1u
#define DDSD_HEIGHT 0x2u
#define DDSD_WIDTH 0x4u
#define DDSD_PIXELFORMAT 0x1000u
#define DDSCAPS_OFFSCREENPLAIN 0x40u
#define DDSCAPS_SYSTEMMEMORY 0x800u
#define DDSCAPS_VIDEOMEMORY 0x4000u
#define DDPF_PALETTEINDEXED8 0x20u
#define DDPF_RGB 0x40u
#define DDPCAPS_8BIT 0x4u
#define DDPCAPS_ALLOW256 0x40u
#define DDCKEY_COLORSPACE 0x1u
#define DDCKEY_DESTBLT 0x2u
#define DDCKEY_SRCBLT 0x8u
#define DDBLT_COLORFILL 0x400u
#define DDBLT_KEYDEST 0x2000u
#define DDBLT_KEYSRC 0x8000u
#define DDBLT_WAIT 0x1000000u
#define DDBLTFAST_NOCOLORKEY 0u
#define DDBLTFAST_SRCCOLORKEY 1u
#define DDBLTFAST_DESTCOLORKEY 2u
#define RDH_RECTANGLES 1u
#define CLR_INVALID 0xffffffffu
extern "C"
{
    void DDCreateSurface(LPDIRECTDRAW2, DDSURFACEDESC*, LPDIRECTDRAWSURFACE*,
                         LPDIRECTDRAWSURFACE2*);
    void DDReleaseSurface(LPDIRECTDRAWSURFACE*, LPDIRECTDRAWSURFACE2*);
    void DDGetSurfaceDescription(LPDIRECTDRAWSURFACE2, DDSURFACEDESC*);
    void DDLockSurface(LPDIRECTDRAWSURFACE2, LPRECT, LPDDSURFACEDESC, UINT32, HANDLE);
    void DDUnlockSurface(LPDIRECTDRAWSURFACE2, PTR);
    void DDRestoreSurface(LPDIRECTDRAWSURFACE2);
    void DDBltFastSurface(LPDIRECTDRAWSURFACE2, UINT32, UINT32, LPDIRECTDRAWSURFACE2, LPRECT,
                          UINT32);
    void DDBltSurface(LPDIRECTDRAWSURFACE2, LPRECT, LPDIRECTDRAWSURFACE2, LPRECT, UINT32,
                      LPDDBLTFX);
    void DDSetSurfaceColorKey(LPDIRECTDRAWSURFACE2, UINT32, LPDDCOLORKEY);
    void DDCreatePalette(LPDIRECTDRAW2, UINT32, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, void*);
    void DDSetPaletteEntries(LPDIRECTDRAWPALETTE, UINT32, UINT32, UINT32, LPPALETTEENTRY);
    void DDGetPaletteEntries(LPDIRECTDRAWPALETTE, UINT32, UINT32, UINT32, LPPALETTEENTRY);
    void DDReleasePalette(LPDIRECTDRAWPALETTE);
    void DDSetSurfacePalette(LPDIRECTDRAWSURFACE2, LPDIRECTDRAWPALETTE);
    HRESULT W8SurfaceGetPalette(LPDIRECTDRAWSURFACE2, LPDIRECTDRAWPALETTE*);
    void DDCreateClipper(LPDIRECTDRAW2, UINT32, LPDIRECTDRAWCLIPPER*);
    void DDReleaseClipper(LPDIRECTDRAWCLIPPER);
    void DDSetClipperList(LPDIRECTDRAWCLIPPER, LPRGNDATA, UINT32);
    void DDSetClipper(LPDIRECTDRAWSURFACE2, LPDIRECTDRAWCLIPPER);
}
#define IDirectDrawSurface2_GetPalette W8SurfaceGetPalette
