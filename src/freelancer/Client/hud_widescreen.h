// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Port-only (FREELANCER_WIDESCREEN_SUPPORT): the in-flight HUD's corner windows pushed out to
// the edges of a widescreen surface, the job retail-era HudShift did by patching the exe.
//
// Under Hor+ (widescreen.h) the Win3D interface keeps a 4:3 layout in the middle of the
// surface. The windows stay where retail puts them in the world; what moves is the picture:
// while a corner window is drawn, updated or offered the mouse, the Win3D camera
// (DAT_0067dbf8) is sheared so that every point, whatever its depth, lands ShiftPx pixels to
// the side -- the view transform gets x' = x + s * depth (s = ShiftPx / XScale) and the
// screen centre moves by ShiftPx, so meshes (whose view the material batcher captures at
// submission), Project / Unproject (text, screen quads, sub-viewports) and mouse rays all
// agree. Nothing in a window's own placement code changes.
//
// The windows: HUD_Status and the NeuroNet buttons (NN_MainButton; a button flying into a
// centred NeuroNet screen loses its shift with the move, and the panel under it has none) to
// the right; HUD_Target, HUD_ContactList and the comm window (HUD_Receiver) to the
// left; the main menu column (TypeSelect) to the left with its "Version" caption to the right.
// Children that belong to the centre (HUD_Status's special-mode captions, NN_MainButton's
// "ScaledBackground") stay in the middle. The Win3D walks and the Draw of the common controls
// (WinControl, NnButtonControl, NnStaticControl, NnTextControl) each set the shift of the object
// they handle, so owners that walk their children themselves still get it right.

#ifdef FREELANCER_WIDESCREEN_SUPPORT

struct Win3D;

// The pixel shift for Object: that of its root window, 0 for anything else, for an excluded
// child, or on a surface not wider than 4:3.
float WidescreenHudShiftPixelsFor(Win3D const* Object);

// The shift currently applied to the Win3D camera (0 outside any scope).
float WidescreenHudCurrentShift();

// While a shift is applied: Out12 = In12 with the shift's shear in front (row 0 minus s times
// row 2, the same as the Win3D view gets), for modelviews bound without a camera; false (Out12
// untouched) when no shift is applied. A flat matrix (z rows zero) comes out unchanged.
bool WidescreenHudShearModelview(float const* In12, float* Out12);

// The mouse ray for the cursor through the Win3D camera as it is now (shifted or not).
void WidescreenHudCursorRay(float* OutRay3);

// Applies ShiftPx to the Win3D camera for the scope's lifetime and puts the previous shift
// back on exit. bDraw: also hand the camera to the pipeline on each change and flush the
// world-space immediate batches drawn under the outgoing state first.
class FWidescreenHudShiftScope
{
public:
    FWidescreenHudShiftScope(float ShiftPx, bool bDraw);
    ~FWidescreenHudShiftScope();

    FWidescreenHudShiftScope(FWidescreenHudShiftScope const&) = delete;
    FWidescreenHudShiftScope& operator=(FWidescreenHudShiftScope const&) = delete;

private:
    float PreviousShift;
    bool bDraw;
    bool bChanged;
};

#endif
