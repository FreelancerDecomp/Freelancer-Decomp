// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Port-only widescreen support (FREELANCER_WIDESCREEN_SUPPORT). Retail ships 4:3 only: the
// options page lists 4:3 modes (FUN_004af120 tests Width * 3 == Height * 4) and every camera
// keeps its horizontal half-angle fixed while the viewport aspect changes, so a wider surface
// loses picture at the top and bottom ("Vert-"). With the define the cameras go "Hor+": the
// stored horizontal half-angle (+0x98 on the space camera, +0x14 on the room camera) keeps
// meaning the angle on a 4:3 surface of the same height, and a camera whose viewport spans the
// full surface width widens its frustum by the factor below. The vertical angle, and with it
// the whole Win3D interface, stays what a 4:3 screen of the same height shows; the extra width
// is added at the sides; the HUD's corner windows are drawn out at the edges
// (hud_widescreen.h). See docs/re/deviations.md (WIDESCREEN).

#ifdef FREELANCER_WIDESCREEN_SUPPORT

#include "globals.h"

#include <windows.h>
#include <string.h>

// The Hor+ factor for a viewport ViewportWidth pixels wide: the surface aspect over 4:3 when the
// viewport spans the full surface width and the surface is wider than 4:3, otherwise 1. A
// letterboxed viewport keeps the full width, so the bars crop height and never change the
// horizontal angle; a sub-viewport (HUD target view, comm window, nav map) keeps retail's maths.
inline double WidescreenHorPlusScale(long long ViewportWidth)
{
    long long const SurfaceWidth = static_cast<long long>(G_RenderSurfaceWidth);
    long long const SurfaceHeight = static_cast<long long>(G_RenderSurfaceHeight);
    if (SurfaceHeight <= 0 || ViewportWidth != SurfaceWidth)
        return 1.0;
    if (SurfaceWidth * 3 <= SurfaceHeight * 4)
        return 1.0;
    return (static_cast<double>(SurfaceWidth) * 3.0) / (static_cast<double>(SurfaceHeight) * 4.0);
}

// The surface width a 4:3 screen of the same height would have: the full surface width divided
// by its Hor+ factor. For code that relates a 4:3 angle to a pixel width.
inline double WidescreenEquivalentSurfaceWidth()
{
    double const SurfaceWidth = static_cast<double>(G_RenderSurfaceWidth);
    return SurfaceWidth / WidescreenHorPlusScale(static_cast<long long>(G_RenderSurfaceWidth));
}

// The desktop mode as the registry holds it (not the mode the game may have switched to).
inline bool WidescreenGetDesktopSize(int* OutWidth, int* OutHeight)
{
    DEVMODEA Desktop;
    memset(&Desktop, 0, sizeof(Desktop));
    Desktop.dmSize = sizeof(Desktop);
    if (!EnumDisplaySettingsA(nullptr, ENUM_REGISTRY_SETTINGS, &Desktop))
        return false;
    if (Desktop.dmPelsWidth == 0 || Desktop.dmPelsHeight == 0)
        return false;
    *OutWidth = static_cast<int>(Desktop.dmPelsWidth);
    *OutHeight = static_cast<int>(Desktop.dmPelsHeight);
    return true;
}

#endif
