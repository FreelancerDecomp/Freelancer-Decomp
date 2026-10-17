// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The cinematic letterbox: FUN_0041a0f0 (set target), FUN_0041a3d0 (forget camera),
// FUN_0041a3e0 / FUN_0041a400 / FUN_0041a410 (queries), FUN_0041a420 (per-frame update for a
// space camera) and FUN_0041a140 (its twin for the room camera).

#include "space_letterbox.h"

#include "fade_to_black.h"
#include "globals.h"
#include "room_camera.h"
#include "space_camera.h"
#include "widescreen.h"   // port-only Hor+ (FREELANCER_WIDESCREEN_SUPPORT)

#include "common_text.h"

#include <math.h>

namespace
{
    // 0x006166b0 / 0x006166ac / 0x006166b4 / 0x006166b8 are zero in the image -- the client
    // starts with no bars and nothing animating. 0x00610690 (the speed) is NOT: the image
    // holds 3.0f there (raw PE dword 0x40400000), the same speed every retail caller passes.
    float       G_LetterboxAmount = 0.0f;
    float       G_LetterboxTarget = 0.0f;
    float       G_LetterboxSpeed = 3.0f;
    bool        G_bLetterboxAnimating = false;
    // DAT_006166b8 -- ONE slot shared by both updates: the space camera (FUN_0041a420) and the
    // room camera (FUN_0041a140) store and compare the same dword, so switching between the two
    // kinds of camera also counts as a camera change.
    void const* G_LetterboxLastCamera = nullptr;

    // .rdata 0x005c9020 = 0x3feccccd. The cinema aspect the bars converge on.
    const float kCinemaAspect = 1.85f;

    // .rdata 0x005c901c (float) and 0x005c8928 (double): pi/360 and 180/pi, the two constants
    // FUN_0041a140 turns the half-angle into a tangent and back with.
    const float kPiOver360 = 0.008726646192371845f;
    const double kRadToDeg = 57.29577791868205;

    // DAT_00610850 / DAT_00610854, read with FILD dword (signed) -- the surface size
    // ApplyGameWindowLayout (FUN_00424870) records, not the requested display mode.
    void GetSurfaceSize(int* OutWidth, int* OutHeight)
    {
        *OutWidth = static_cast<int>(G_RenderSurfaceWidth);
        *OutHeight = static_cast<int>(G_RenderSurfaceHeight);
    }

    // The head both updates share, inlined identically in FUN_0041a420 (0x0041a420..0x0041a4bc)
    // and FUN_0041a140 (0x0041a140..0x0041a1d7). Returns retail's CL: true whenever the amount
    // was animating on entry, whether or not it has now arrived. Arriving is NOT a return --
    // every path falls through to the camera part, which is what re-applies the viewport on
    // every tick of the slide.
    //
    // x87: the sum/difference is kept on the FPU stack after the FST, so the arrival test
    // compares the UNROUNDED value with the target; the port keeps it in a double.
    bool StepLetterboxAmount(float DeltaSeconds)
    {
        if (!G_bLetterboxAnimating)
            return false;

        if (G_LetterboxAmount < G_LetterboxTarget)
        {
            // TEST AH,0x05 / JP not taken: strictly less (ordered).
            const double Sum = static_cast<double>(G_LetterboxSpeed) * DeltaSeconds +
                               static_cast<double>(G_LetterboxAmount);
            G_LetterboxAmount = static_cast<float>(Sum);
            // TEST CL(1),AH / JNZ: C0 set (still less, or unordered) keeps the animation going.
            if (!(Sum >= static_cast<double>(G_LetterboxTarget)))
                return true;
            G_LetterboxAmount = G_LetterboxTarget;
        }
        else if (G_LetterboxAmount > G_LetterboxTarget)
        {
            // TEST AH,0x41 / JNZ not taken: strictly greater (ordered).
            const double Difference = static_cast<double>(G_LetterboxAmount) -
                                      static_cast<double>(G_LetterboxSpeed) * DeltaSeconds;
            G_LetterboxAmount = static_cast<float>(Difference);
            // TEST AH,0x41 / JP: neither C0 nor C3 (still greater), or both (unordered), keeps
            // the animation going; exactly one of them (less or equal) lands it.
            if (!(Difference <= static_cast<double>(G_LetterboxTarget)))
                return true;
            G_LetterboxAmount = G_LetterboxTarget;
        }
        // else: equal, or unordered -- the flag is simply dropped (0x0041a48b / 0x0041a1a6).

        G_bLetterboxAnimating = false;
        return true;
    }
}

void SetLetterboxTarget(float Target, float Speed)
{
    // FUN_0041a0f0. The fade test comes FIRST and assigns the TARGET to the amount, not the
    // other way round -- so a covered screen gets the finished bars immediately.
    if (IsFadeOpaque())
        G_LetterboxAmount = Target;

    G_LetterboxTarget = Target;
    G_LetterboxSpeed = Speed;
    G_bLetterboxAnimating = true;

    // 0x0041a10a: and the camera is forgotten, so the very next update reapplies the viewport
    // even when the amount landed on its target above and nothing is left to animate.
    G_LetterboxLastCamera = nullptr;
}

void ForgetLetterboxCamera()
{
    G_LetterboxLastCamera = nullptr;                // FUN_0041a3d0
}

bool IsLetterboxVisible()
{
    // FUN_0041a3e0: `if (0.0 < Amount) return 1;` -- strictly positive, so an amount of exactly
    // zero reports no letterbox.
    return G_LetterboxAmount > 0.0f;
}

float GetLetterboxAmount()
{
    return G_LetterboxAmount;                       // FUN_0041a400
}

float GetLetterboxTarget()
{
    return G_LetterboxTarget;                       // FUN_0041a410
}

int GetLetterboxBarHeight(float Amount)
{
    int Width = 0;
    int Height = 0;
    GetSurfaceSize(&Width, &Height);

    // 0x0041a4e9 .. 0x0041a51c (and 0x0041a207 .. 0x0041a23c): every intermediate stays on the
    // x87 stack -- FILD, FDIV, FSUB 1.85f, FMUL Amount, FADD, FDIVP, FSUBR, FMUL/FADD 0.5
    // (double 0x005c75e8) -- so nothing is rounded to float on the way; doubles here.
    const double SurfaceAspect = static_cast<double>(Width) / static_cast<double>(Height);
    const double Divisor =
        (static_cast<double>(kCinemaAspect) - SurfaceAspect) * Amount + SurfaceAspect;
    const double Bars =
        (static_cast<double>(Height) - static_cast<double>(Width) / Divisor) * 0.5 + 0.5;

    // 0x0041a525 / 0x0041a52b: floor then ftol, in that order, so the +0.5 above is the rounding
    // and the floor is what makes it a round-half-up rather than a truncation.
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only: retail never sees a surface wider than 1.85, where the formula goes negative
    // and the "bars" would grow the viewport past the surface. Such a surface gets none.
    const int BarHeight = static_cast<int>(floor(Bars));
    return BarHeight > 0 ? BarHeight : 0;
#else
    return static_cast<int>(floor(Bars));
#endif
}

void UpdateLetterbox(FSpaceCamera* Cam, float DeltaSeconds)
{
    // FUN_0041a420. 0x0041a43d: CL is set to 1 before either branch, so merely BEING animated
    // counts as a change -- and the port used to `return` while the bars were still sliding
    // (0x0041a46c / 0x0041a4ae jump to 0x0041a4c2, the camera part, not to the epilogue), which
    // applied the bars only once they had arrived instead of on every tick of the slide.
    const bool bStepped = StepLetterboxAmount(DeltaSeconds);

    // 0x0041a4c2: a null camera is not an error -- the amount has been advanced and that is all
    // this call can do.
    if (Cam == nullptr)
        return;

    // 0x0041a4ce .. 0x0041a4e3: the camera is compared and stored before the CL test.
    const bool bCameraChanged = (G_LetterboxLastCamera != Cam);
    G_LetterboxLastCamera = Cam;
    if (!bStepped && !bCameraChanged)
        return;

    int Width = 0;
    int Height = 0;
    GetSurfaceSize(&Width, &Height);

    const int Bars = GetLetterboxBarHeight(G_LetterboxAmount);

    // 0x0041a539 .. 0x0041a556: the rectangle is built as extents -- left 0, top `bars`, the
    // full width, and the height less BOTH bars -- and converted to inclusive edges before it
    // reaches the camera.
    Rect Bounds;
    Bounds.Left = 0;
    Bounds.Top = Bars;
    Bounds.Width = Width;
    Bounds.Height = Height - Bars * 2;

    const ViewRect View = Rect_to_ViewRect(Bounds);

    // 0x0041a586: the two-argument viewport setter, with the dead flag zero.
    SpaceCameraSetViewportEx(Cam, &View.Left, 0);
}

void UpdateRoomLetterbox(FRoomCamera* Cam, float DeltaSeconds)
{
    // FUN_0041a140 (cdecl: camera, dt). The same head and the same bar geometry as
    // FUN_0041a420; only the camera update differs, because the room camera has no two-argument
    // viewport setter -- the viewport and the projection fields are rebuilt inline.
    const bool bStepped = StepLetterboxAmount(DeltaSeconds);

    // 0x0041a1df .. 0x0041a201.
    if (Cam == nullptr)
        return;

    const bool bCameraChanged = (G_LetterboxLastCamera != Cam);
    G_LetterboxLastCamera = Cam;
    if (!bStepped && !bCameraChanged)
        return;

    int Width = 0;
    int Height = 0;
    GetSurfaceSize(&Width, &Height);

    const int Bars = GetLetterboxBarHeight(G_LetterboxAmount);

    // 0x0041a250 .. 0x0041a27e: the same extents rectangle, converted by Rect_to_ViewRect.
    Rect Bounds;
    Bounds.Left = 0;
    Bounds.Top = Bars;
    Bounds.Width = Width;
    Bounds.Height = Height - Bars * 2;

    const ViewRect View = Rect_to_ViewRect(Bounds);

    // 0x0041a299: ICamera slot 6 (+0x18, GetHalfFovXDeg) -- read BEFORE the viewport is stored.
    const float HalfFovXDeg = Cam->GetHalfFovXDeg();

    // 0x0041a2ad .. 0x0041a2bd: the four edges into +0x04.
    Cam->Viewport[0] = View.Left;
    Cam->Viewport[1] = View.Top;
    Cam->Viewport[2] = View.Right;
    Cam->Viewport[3] = View.Bottom;

    // 0x0041a2c0 .. 0x0041a2ee: half extents from the edges, NO +1 (right - left, bottom - top),
    // times 0.5f (.rdata 0x005c75e0).
    Cam->HalfWidth = static_cast<float>(Cam->Viewport[2] - Cam->Viewport[0]) * 0.5f;
    Cam->HalfHeight = static_cast<float>(Cam->Viewport[3] - Cam->Viewport[1]) * 0.5f;

    // 0x0041a2f3 .. 0x0041a2fe: both scales against the tangents the camera had BEFORE this
    // call. Both are overwritten again below with the new tangents and nothing reads them in
    // between; retail still makes the stores, so the port does too.
    Cam->XScale = Cam->HalfWidth / Cam->TanHalfFovX;
    Cam->YScale = -(Cam->HalfHeight / Cam->TanHalfFovY);

    // 0x0041a301 .. 0x0041a332: FADD ST0,ST0 / FMUL pi/360 / FPTAN -- the tangent of the half
    // angle in radians -- then FPATAN(tan, 1) * 180/pi back into +0x14, and the X side plane
    // {1, 0, tan} normalised through FUN_0041a390.
    const double TanX =
        tan((static_cast<double>(HalfFovXDeg) + static_cast<double>(HalfFovXDeg)) * kPiOver360);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+ (room_camera.cpp): +0x14 keeps the 4:3 angle; the full-width rectangle
    // widens +0x24 and the X plane, so the bars only crop height.
    const double TanXWide = TanX * WidescreenHorPlusScale(Bounds.Width);
#else
    const double TanXWide = TanX;
#endif
    Cam->TanHalfFovX = static_cast<float>(TanXWide);
    Cam->HalfFovXDeg = static_cast<float>(atan2(TanX, 1.0) * kRadToDeg);
    Cam->PlaneX[0] = 1.0f;
    Cam->PlaneX[1] = 0.0f;
    Cam->PlaneX[2] = static_cast<float>(TanXWide);
    RoomVec3Normalize(Cam->PlaneX);

    // 0x0041a337 .. 0x0041a340.
    Cam->XScale = Cam->HalfWidth / Cam->TanHalfFovX;

    // 0x0041a343 .. 0x0041a34b: the aspect is the RECTANGLE's (width / height-less-bars, FILD /
    // FIDIV), not the edges'; it stays unrounded on the stack for the division below.
    const double Aspect = static_cast<double>(Bounds.Width) / static_cast<double>(Bounds.Height);
    Cam->Aspect = static_cast<float>(Aspect);

    // 0x0041a34e .. 0x0041a373: tanY = stored tanX / aspect, FPATAN back into +0x18, the Y side
    // plane {0, 1, tanY} normalised.
    const double TanY = static_cast<double>(Cam->TanHalfFovX) / Aspect;
    Cam->TanHalfFovY = static_cast<float>(TanY);
    Cam->HalfFovYDeg = static_cast<float>(atan2(TanY, 1.0) * kRadToDeg);
    Cam->PlaneY[0] = 0.0f;
    Cam->PlaneY[1] = 1.0f;
    Cam->PlaneY[2] = static_cast<float>(TanY);
    RoomVec3Normalize(Cam->PlaneY);

    // 0x0041a378 .. 0x0041a382.
    Cam->YScale = -(Cam->HalfHeight / Cam->TanHalfFovY);
}
