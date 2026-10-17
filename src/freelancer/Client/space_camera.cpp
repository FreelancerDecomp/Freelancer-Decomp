// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail in-flight camera base class (FUN_0040f150, vtable .rdata 0x005c8944) and the camera
// mode dispatcher FUN_00549e70.
//
// What is here: the object layout, all twenty interface slots, the mode dispatch and the camera
// install (FUN_00549bc0). The eight derived cameras retail embeds in the in-flight controller at
// +0x00c / +0x1b0 / +0x3c8 / +0x500 / +0x60c / +0x760 / +0x9a4 / +0xa9c live in their own files
// (space_camera_first/third/turret/rear/death/script/static/docking) and are members of the
// controller SpaceApp (space_app.h), built by its ctor FUN_00549300.

#include "space_camera.h"
#include "space_camera_third.h"
#include "space_camera_first.h"
#include "space_camera_turret.h"
#include "space_camera_rear.h"
#include "space_camera_script.h"
#include "space_camera_static.h"
#include "space_camera_docking.h"
#include "space_camera_death.h"
#include "space_app.h"       // G_SpaceApp (DAT_00678b98)
#include "player_spawn.h"
#include "client_gameobj.h" // CloseRearAndTurretViewsForDocking (FUN_00549e30)
#include "server_connect.h"
#include "menu_sound_arch.h"
#include "input_manager.h"
#include "globals.h"         // g_client.window (DAT_0067eca0), G_RenderSurfaceWidth/Height
#include "player_behavior.h" // GetPlayerObjRW (the DAT_00679744 - 8 read, FUN_0054baf0)
#include "star_sys.h"        // StarSysGetFogColor (FUN_005552e0)
#include "widescreen.h"      // port-only Hor+ (FREELANCER_WIDESCREEN_SUPPORT)

#include "dacom.h"
#include "common_csys.h"     // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"    // IObjRW (NotifyCinematicView)
#include "common_server.h"   // IServer::SPRequestInvincibility (DAT_0067ecd0 slot 57)
#include "x86math.h"         // I3DMathEngine::InverseTransform (slot 0x3c)

#include <math.h>
#include <stdlib.h>
#include <string.h>

// DAT_00614028 -- set to 1 by every FUN_00549e70 call, including the ones that select nothing.
// .data ships 1; nothing in the PE reads it.
unsigned char G_SpaceCameraSwitched = 1;

namespace
{
    // The base ctor's literals, resolved out of the exe rather than guessed:
    //   DAT_005c8938 = 0.6108652334660292  (35 degrees in radians)
    //   DAT_005c8930 = 0.75                (1 / (4:3))
    //   DAT_005c8928 = 57.29577791868205   (180 / PI)
    //   DAT_005c75dc = 1.0
    //   DAT_005c7474 = 0.0
    //   DAT_00610530 = 3.0                 (znear)
    //   DAT_00610534 = 250000.0            (zfar)
    const double kHalfFovXRadians = 0.6108652334660292;
    const float  kInverseAspect = 0.75f;
    const double kRadiansToDegrees = 57.29577791868205;
    const float  kHalfFovXDegrees = 35.0f;
    const float  kDefaultAspect = 1.3333334f;   // 0x3faaaaab
    const float  kDefaultZNear = 3.0f;
    const float  kDefaultZFar = 250000.0f;
    const float  kDefaultHalfWidth = 320.0f;    // 0x43a00000
    const float  kDefaultHalfHeight = 240.0f;   // 0x43700000

    // FUN_0040f5f0's "fovx" branch: DAT_005c75e8 = 0.5 (the INI carries the FULL angle) and
    // DAT_005c77f4 = 0.01745329238474369f (degrees to radians).
    const double kFullFovToHalf = 0.5;
    const float  kDegreesToRadians = 0.01745329238474369f;

    // A frustum plane normalised as every projection rebuild does it (FUN_00410190, or its body
    // inlined): the three stored floats squared and summed on the FPU as (V0*V0 + V1*V1) +
    // V2*V2, FSQRT, FDIVR 1.0f, then each component times that inverse, FSTP float. No zero
    // guard -- one component is always exactly 1.0f.
    void NormalizePlane(float* V)
    {
        const double Len = sqrt(static_cast<double>(V[0]) * V[0] + static_cast<double>(V[1]) * V[1] +
                                static_cast<double>(V[2]) * V[2]);
        const double Inv = 1.0 / Len;
        V[0] = static_cast<float>(Inv * V[0]);
        V[1] = static_cast<float>(Inv * V[1]);
        V[2] = static_cast<float>(Inv * V[2]);
    }

    // The X frustum plane and XScale, as all six rebuilds inline them: +0x80 = 1.0f, +0x84 = 0,
    // +0x88 = TanX stored as float, normalised; then FCOM TanX against 0.0f (DAT_005c7474) /
    // TEST AH,0x41 / JNZ -- skipped for less, equal and unordered -- else XScale = HalfWidth /
    // TanX, rounded once. TanX is the value the caller has on the FPU (the unrounded tangent in
    // five of them, the float the "fovy" property stored in the sixth).
    void RebuildPlaneX(FSpaceCamera* Cam, double TanX)
    {
        Cam->PlaneX[0] = 1.0f;
        Cam->PlaneX[1] = 0.0f;
        Cam->PlaneX[2] = static_cast<float>(TanX);
        NormalizePlane(Cam->PlaneX);
        if (TanX > 0.0)
            Cam->XScale = static_cast<float>(static_cast<double>(Cam->HalfWidth) / TanX);
    }

    // The Y plane and YScale: +0x8c = 0, +0x90 = 1.0f, +0x94 = TanY stored as float, normalised;
    // a strictly positive TanY (same compare) gives YScale = -(HalfHeight / TanY), FCHS, rounded
    // once. FUN_0040fa10 divides by the unrounded tangent; every other rebuild by the float it
    // stored.
    void RebuildPlaneY(FSpaceCamera* Cam, double TanY)
    {
        Cam->PlaneY[0] = 0.0f;
        Cam->PlaneY[1] = 1.0f;
        Cam->PlaneY[2] = static_cast<float>(TanY);
        NormalizePlane(Cam->PlaneY);
        if (TanY > 0.0)
            Cam->YScale = static_cast<float>(-(static_cast<double>(Cam->HalfHeight) / TanY));
    }

    // FPATAN(x, 1.0) times DAT_005c8928 (180 / PI), FSTP float.
    float TangentToDegrees(double Tangent)
    {
        return static_cast<float>(atan2(Tangent, 1.0) * kRadiansToDegrees);
    }

    // ---------------------------------------------------------------- vtable slots
    // Every slot is __stdcall with `this` as an explicit first stack argument, matching the
    // retail leaf functions (MOV EAX,[ESP+4] / RET 4).

















    // LAB_0040f4d0 / LAB_0040f4e0: FLD float, FDIV float, RET 4 -- the quotient is returned in
    // ST0 unrounded (ICamera slots 11 / 12 return double).




    // DAT_006164d8 -- the camera code's lazily acquired 3DMathEngine (FUN_005b73e0 on first use).
    I3DMathEngine* G_CameraMathEngine = nullptr;














    // FUN_0040f5f0 body (listing 0x0040f5f0..0x0040f8b3). No null checks: 0x0040f5f9 hands the
    // key straight to stricmp.
    int SetPropertyImpl(FSpaceCamera* Cam, const char* Key, const char* Value)
    {
        if (_stricmp(Key, "fovx") == 0)
        {
            // 0x0040f612..0x0040f76c. atof * 0.5 (DAT_005c75e8, the INI carries the FULL angle)
            // stays on the FPU: FST +0x98 does not pop, so the tangent is taken of the unrounded
            // half-angle times DAT_005c77f4. Its quotient by the live aspect (+0xa0) is stored
            // as a float local and the unrounded one feeds FPATAN for +0x9c; the X plane and
            // XScale use the unrounded tangent, the Y plane and YScale the stored float.
            const double HalfFovX = atof(Value) * kFullFovToHalf;
            Cam->HalfFovXDeg = static_cast<float>(HalfFovX);
            double TanX = tan(HalfFovX * static_cast<double>(kDegreesToRadians));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            // Port-only Hor+: +0x98 keeps the 4:3 angle, the frustum takes the widened one.
            TanX *= SpaceCameraHorPlusScale(Cam);
#endif
            const double TanY = TanX / static_cast<double>(Cam->Aspect);
            const float TanYStored = static_cast<float>(TanY);
            Cam->FovYDeg = TangentToDegrees(TanY);
            RebuildPlaneX(Cam, TanX);
            RebuildPlaneY(Cam, TanYStored);
            // Retail does NOT raise +0x34 here: changing the projection does not invalidate
            // the cached VIEW matrix, and 0x0040f76f / 0x0040f8a5 return with AL = 1 having
            // touched nothing else.
            return 1;
        }

        if (_stricmp(Key, "fovy") == 0)
        {
            // 0x0040f787..0x0040f852. The mirror image of "fovx": atof * 0.5 FST +0x9c (no
            // pop), the tangent of the unrounded value stored as a float local (FST [ESP+0x14]),
            // times the aspect stored as another (FST [ESP+0x10]); the unrounded product feeds
            // FPATAN for +0x98 and the X plane's +0x88. This branch alone compares and divides
            // with the STORED floats: XScale by the float TanX (0x0040f800..0x0040f818), YScale
            // by the float TanY (0x0040f74b..0x0040f76c). The planes go through FUN_00410190.
            const double FovY = atof(Value) * kFullFovToHalf;
            Cam->FovYDeg = static_cast<float>(FovY);
            const double TanY = tan(FovY * static_cast<double>(kDegreesToRadians));
            const float TanYStored = static_cast<float>(TanY);
            const double TanX = TanY * static_cast<double>(Cam->Aspect);
            const float TanXStored = static_cast<float>(TanX);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
            // Port-only Hor+: +0x98 is the 4:3 angle the widened tangent came from.
            Cam->HalfFovXDeg = TangentToDegrees(TanX / SpaceCameraHorPlusScale(Cam));
#else
            Cam->HalfFovXDeg = TangentToDegrees(TanX);
#endif
            RebuildPlaneX(Cam, TanXStored);
            RebuildPlaneY(Cam, TanYStored);
            return 1;
        }

        // 0x0040f866 and 0x0040f891: both are a bare atof into the field, with no rebuild --
        // neither near nor far takes part in the plane maths.
        if (_stricmp(Key, "znear") == 0)
        {
            Cam->ZNear = static_cast<float>(atof(Value));
            return 1;
        }
        if (_stricmp(Key, "zfar") == 0)
        {
            Cam->ZFar = static_cast<float>(atof(Value));
            return 1;
        }

        // 0x0040f8ad: anything else returns false.
        return 0;
    }

    // Slot 0x4c as retail calls it: __thiscall (this in ECX, Key and Value on the stack, RET 8),
    // the ICamera::SetProperty contract. __fastcall with an unused EDX is the same callee ABI.



    // ---------------------------------------------------------------- mode dispatch state
    // Retail carries all of it on the in-flight controller SpaceApp (DAT_00678b98, the `this`
    // of FUN_00549e70 / FUN_00549bc0): the eight cameras at +0x00c .. +0xa9c, the active camera
    // at +0xba0, the preference at +0xba4, the bytes at +0x08 / +0x0b / +0xba8. The port's
    // object is G_SpaceApp (space_app.h), built at static init as retail's is.
    bool         G_LastModeWasScript = false;  // FUN_00549e70's local_28

    // The camera for one of FUN_00549e70's names.
    FSpaceCamera* CameraForSlot(int Slot)
    {
        switch (Slot)
        {
        case kSpaceCameraFirstPerson: return SpaceCameraOf((FHudCamera*)&G_SpaceApp.FirstPerson);
        case kSpaceCameraThirdPerson: return SpaceCameraOf((FHudCamera*)&G_SpaceApp.ThirdPerson);
        case kSpaceCameraTurret:      return SpaceCameraOf((FHudCamera*)&G_SpaceApp.Turret);
        case kSpaceCameraRearview:    return SpaceCameraOf((FHudCamera*)&G_SpaceApp.RearView);
        case kSpaceCameraScript:      return (FSpaceCamera*)&G_SpaceApp.Script;
        case kSpaceCameraStatic:      return (FSpaceCamera*)&G_SpaceApp.Static;
        case kSpaceCameraDocking:     return (FSpaceCamera*)&G_SpaceApp.Docking;
        case kSpaceCameraDeath:       return (FSpaceCamera*)&G_SpaceApp.Death;
        default:                      return nullptr;
        }
    }
}

int SpaceCameraSetProperty(FSpaceCamera* Cam, const char* Key, const char* Value)
{
    return SetPropertyImpl(Cam, Key, Value);
}

#ifdef FREELANCER_WIDESCREEN_SUPPORT
double SpaceCameraHorPlusScale(const FSpaceCamera* Cam)
{
    // Inclusive edges, as FUN_0040fd60 counts them.
    return WidescreenHorPlusScale(static_cast<long long>(Cam->Viewport[2]) - Cam->Viewport[0] + 1);
}
#endif

void SpaceCameraSetHalfFovX(FSpaceCamera* Cam, float HalfFovXDeg)
{
    // FUN_0040f8c0. A NON-virtual method (`this` in ECX, one stack float, RET 4) that owns the
    // whole projection derivation; the ctor and the "fovx" property branch are inlined copies
    // of it, and the GameCamera ctor calls it directly with 35.0f.
    // 0x0040f8c0..0x0040fa0a: +0x98 = the argument; FPTAN of it times DAT_005c77f4; the tangent
    // over the live aspect (+0xa0) stored back into the argument slot as a float, the unrounded
    // quotient into FPATAN for +0x9c; X plane / XScale with the unrounded tangent, Y plane /
    // YScale with the stored float.
    Cam->HalfFovXDeg = HalfFovXDeg;

    double TanX = tan(static_cast<double>(HalfFovXDeg) * static_cast<double>(kDegreesToRadians));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: +0x98 keeps the 4:3 angle, the frustum takes the widened one.
    TanX *= SpaceCameraHorPlusScale(Cam);
#endif
    const double TanY = TanX / static_cast<double>(Cam->Aspect);
    const float TanYStored = static_cast<float>(TanY);
    Cam->FovYDeg = TangentToDegrees(TanY);

    RebuildPlaneX(Cam, TanX);
    RebuildPlaneY(Cam, TanYStored);
}

void SpaceCameraSetFovY(FSpaceCamera* Cam, float FovYDeg)
{
    // FUN_0040fa10: +0x9c first, then FPTAN of FovY * DAT_005c77f4, times the aspect (+0xa0),
    // FPATAN(x, 1) * 57.29577791868205 into +0x98, and the two plane rebuilds with the
    // strictly-positive guards of 0x0040fab2 / 0x0040fb37 -- both against the UNROUNDED
    // tangents still on the FPU (FST +0x88 / +0x94 do not pop).
    Cam->FovYDeg = FovYDeg;
    const double TanY = tan(static_cast<double>(FovYDeg) * static_cast<double>(kDegreesToRadians));
    const double TanX = TanY * static_cast<double>(Cam->Aspect);
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: +0x98 is the 4:3 angle the widened tangent came from.
    Cam->HalfFovXDeg = TangentToDegrees(TanX / SpaceCameraHorPlusScale(Cam));
#else
    Cam->HalfFovXDeg = TangentToDegrees(TanX);
#endif
    RebuildPlaneX(Cam, TanX);
    RebuildPlaneY(Cam, TanY);
}

bool SpaceCameraSetViewport(FSpaceCamera* Cam, const int* ViewRect4)
{
    // FUN_0040fd60. `this` in ECX, one stack pointer, RET 4.
    Cam->Viewport[0] = ViewRect4[0];
    Cam->Viewport[1] = ViewRect4[1];
    Cam->Viewport[2] = ViewRect4[2];
    Cam->Viewport[3] = ViewRect4[3];

    // 0x0040fd87..0x0040fdb1: both extents are the difference PLUS ONE (inclusive edges), FILD
    // as ints.
    const int Width = Cam->Viewport[2] - Cam->Viewport[0] + 1;
    const int Height = Cam->Viewport[3] - Cam->Viewport[1] + 1;

    // 0x0040fdb9..0x0040fdf7, all on the FPU: aspect = W / H (FST as a float local and into
    // +0xa0); W * 0.5 (double DAT_005c75e8) FST as a float local and into +0x70; H * 0.5 FST
    // into +0x74 and kept; +0x68 = left + the float half-width, +0x6c = top + the unrounded
    // half-height, each rounded once.
    const float AspectStored = static_cast<float>(static_cast<double>(Width) / static_cast<double>(Height));
    Cam->Aspect = AspectStored;
    const float HalfWidthStored = static_cast<float>(static_cast<double>(Width) * 0.5);
    Cam->HalfWidth = HalfWidthStored;
    const double HalfHeight = static_cast<double>(Height) * 0.5;
    Cam->HalfHeight = static_cast<float>(HalfHeight);
    Cam->ScreenCenterX = static_cast<float>(static_cast<double>(Cam->Viewport[0]) + HalfWidthStored);
    Cam->ScreenCenterY = static_cast<float>(static_cast<double>(Cam->Viewport[1]) + HalfHeight);

    // 0x0040fdfc..0x0040fe26: +0x98 loaded and stored straight back (a no-op, not reproduced);
    // FPTAN of it times DAT_005c77f4; FDIVR by the stored float aspect -> the tangent of the
    // vertical half-angle, stored as a float local, the unrounded one into FPATAN for +0x9c.
    // X plane / XScale with the unrounded tangent, Y plane / YScale with the stored float.
    double TanX = tan(static_cast<double>(Cam->HalfFovXDeg) * static_cast<double>(kDegreesToRadians));
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: the 4:3 angle at +0x98 widened for the viewport just stored.
    TanX *= SpaceCameraHorPlusScale(Cam);
#endif
    const double TanY = TanX / static_cast<double>(AspectStored);
    const float TanYStored = static_cast<float>(TanY);
    Cam->FovYDeg = TangentToDegrees(TanY);

    RebuildPlaneX(Cam, TanX);
    RebuildPlaneY(Cam, TanYStored);
    // 0x0040ff29: AL = 1.
    return true;
}

void SpaceCameraSetViewportEx(FSpaceCamera* Cam, const int* ViewRect4, int UnusedFlag)
{
    // FUN_0040fb80. See the header: the flag really is dead in the shipped build.
    (void)UnusedFlag;
    SpaceCameraSetViewport(Cam, ViewRect4);
}

void SpaceCameraConstruct(FSpaceCamera* Cam)
{
    // FUN_0040f150. No memset: retail writes exactly the fields below and leaves ScreenCenterX/Y
    // (+0x68/+0x6c), the viewport (+0xac..+0xbb) and the pad bytes (+0x35..+0x37,
    // +0xbd..+0xbf) as the storage held them -- SetViewport (FUN_0040fd60) fills those in.
    // 0x0040f153..0x0040f1c8: World (+0x04) and View (+0x38) identity, WorldPos (+0x28) and
    // ViewPos (+0x5c) zero (the inlined base ctor writes World/WorldPos a second time with the
    // same values), the vtable at +0x00 in between.
    for (int Index = 0; Index < 9; ++Index)
    {
        Cam->World[Index] = 0.0f;
        Cam->View[Index] = 0.0f;
    }
    Cam->World[0] = 1.0f;
    Cam->World[4] = 1.0f;
    Cam->World[8] = 1.0f;
    Cam->View[0] = 1.0f;
    Cam->View[4] = 1.0f;
    Cam->View[8] = 1.0f;
    for (int Index = 0; Index < 3; ++Index)
    {
        Cam->WorldPos[Index] = 0.0f;
        Cam->ViewPos[Index] = 0.0f;
    }

    Cam->bViewDirty = 1;         // 0x0040f1cb: byte +0x34
    Cam->FlagBC = 0;             // 0x0040f1cf: byte +0xbc

    Cam->Aspect = kDefaultAspect;         // +0xa0 = 0x3faaaaab
    Cam->HalfWidth = kDefaultHalfWidth;   // +0x70 = 0x43a00000
    Cam->ZNear = kDefaultZNear;           // +0xa4 = DAT_00610530
    Cam->HalfHeight = kDefaultHalfHeight; // +0x74 = 0x43700000
    Cam->ZFar = kDefaultZFar;             // +0xa8 = DAT_00610534
    Cam->HalfFovXDeg = kHalfFovXDegrees;  // +0x98 = 0x420c0000

    // 0x0040f20f..0x0040f22f: FPTAN of DAT_005c8938 (double); FLD 0.75f (DAT_005c8930), FMUL ST1
    // -> the skew, FST float into local_4 and the UNROUNDED skew into FPATAN for +0x9c.
    // 0x0040f235..0x0040f2b1: X plane and XScale against the unrounded tangent still in ST0.
    // 0x0040f2b4..0x0040f341: +0x94 = local_4 (the float skew); YScale compares and divides by
    // local_4 again.
    const double TanX = tan(kHalfFovXRadians);
    const double Skew = static_cast<double>(kInverseAspect) * TanX;
    const float SkewStored = static_cast<float>(Skew);
    Cam->FovYDeg = TangentToDegrees(Skew);
    RebuildPlaneX(Cam, TanX);
    RebuildPlaneY(Cam, SkewStored);
}

const float kSpaceCameraCutCosine = 0.95f;      // .data 0x0061402c

bool SpaceCameraIsHardCut(const float* OldOrientation, const float* NewOrientation,
                          bool bHadNoPreviousCamera)
{
    // 0x00549da8: the dot of the two column-2 axes. 0x00549ddc..0x00549de9 multiply and add on
    // the FPU as (new8*old8 + new5*old5) + new2*old2, and the port keeps that association.
    const double Dot =
        (static_cast<double>(NewOrientation[8]) * OldOrientation[8] +
         static_cast<double>(NewOrientation[5]) * OldOrientation[5]) +
        static_cast<double>(NewOrientation[2]) * OldOrientation[2];

    // 0x00549deb: FCOMP against 0.95, then TEST AH,5 + JNP. That combination jumps only when
    // C0 is set and C2 is not -- a strict, ORDERED "less than". Equal does not cut, and an
    // unordered compare (a NaN in either matrix) does not cut either.
    if (Dot < static_cast<double>(kSpaceCameraCutCosine))
        return true;

    // 0x00549dff: and having had no camera at all before this one always counts as a cut,
    // whatever the dot said.
    return bHadNoPreviousCamera;
}

void InstallSpaceCamera(FSpaceCamera* NewCam, bool bForce)
{
    // FUN_00549bc0 (thiscall on the in-flight controller G_SpaceApp, RET 8).

    // 0x00549bc9..0x00549be4: installing the camera that is already installed does nothing
    // unless the caller insists.
    if (G_SpaceApp.ActiveCamera == NewCam && !bForce)
        return;

    // 0x00549bea..0x00549c28: the outgoing camera's transform, read through slot 0x10 BEFORE
    // anything is torn down, is copied to a local (0x30 bytes, REP MOVSD). With no outgoing
    // camera it is an identity (FUN_004215b0) with a zero position. Only the 3x3 part is read
    // again, by the cut test.
    float Saved[12];
    if (G_SpaceApp.ActiveCamera != nullptr)
    {
        const float* const Src = AsCamera(G_SpaceApp.ActiveCamera)->GetOrientation();
        for (int I = 0; I < 12; ++I)
            Saved[I] = Src[I];
    }
    else
    {
        for (int I = 0; I < 12; ++I)
            Saved[I] = 0.0f;
        Saved[0] = 1.0f;
        Saved[4] = 1.0f;
        Saved[8] = 1.0f;
    }

    // 0x00549c2a..0x00549c51: an outgoing camera gets slot 0x68 (deactivate), then the active
    // pointer is cleared and FUN_0040f140(0) drops the sound listener; with none, local_b5 = 1.
    bool bWasNone = false;
    if (G_SpaceApp.ActiveCamera != nullptr)
    {
        ((FGameCamera*)G_SpaceApp.ActiveCamera)->Deactivate();
        G_SpaceApp.ActiveCamera = nullptr;
        SetClientSoundListenerObject(nullptr);
    }
    else
    {
        bWasNone = true;
    }

    if (NewCam != nullptr)
    {
        // 0x00549c5e..0x00549c8d: the game window's client rect (DAT_0067eca0) becomes the
        // inclusive view rect {0, 0, right - 1, bottom - 1}.
        RECT Client;
        GetClientRect(g_client.window, &Client);
        int View[4];
        View[0] = 0;
        View[1] = 0;
        View[2] = Client.right - 1;
        View[3] = Client.bottom - 1;

        // 0x00549c7e..0x00549ca0: slot 0x58 with the player's IObjRW (DAT_00679744 minus eight,
        // or null); the result is not read.
        ((FGameCamera*)NewCam)->Attach(GetPlayerObjRW());

        // 0x00549ca3..0x00549cdd: slot 0x64 with that rect when both extents are positive
        // (signed JLE), else with {0, 0, surface width - 1, surface height - 1} from
        // DAT_00610850 / DAT_00610854. The result is not read.
        if (View[2] <= 0 || View[3] <= 0)
        {
            View[2] = static_cast<int>(G_RenderSurfaceWidth) - 1;
            View[3] = static_cast<int>(G_RenderSurfaceHeight) - 1;
        }
        ((FGameCamera*)NewCam)->Activate(View);

        // 0x00549ce0..0x00549d23: the fog colour at the camera's position (slot 0x0c,
        // FUN_005552e0) becomes the clear colour at +0xe9..+0xeb, +0xe8 |= 3 (colour and depth
        // clear), then FUN_0051b500 clears now. Every camera the controller installs is a
        // GameCamera.
        unsigned char Fog[4];
        StarSysGetFogColor(reinterpret_cast<Vector const*>(AsCamera(NewCam)->GetPosition()), Fog);
        FGameCamera* const Game = reinterpret_cast<FGameCamera*>(NewCam);
        Game->ClearColor[0] = Fog[0];
        Game->ClearColor[1] = Fog[1];
        Game->ClearColor[2] = Fog[2];
        Game->Flags = static_cast<unsigned char>(Game->Flags | 3);
        GameCameraClear(Game);
    }

    // 0x00549d28..0x00549d34: the new camera (or null) becomes the active one and the sound
    // listener (FUN_0040f140).
    G_SpaceApp.ActiveCamera = NewCam;
    SetClientSoundListenerObject(NewCam);

    // 0x00549d37..0x00549dc5: the incoming transform through slot 0x10, or an inline identity
    // with a zero position.
    float Current[12];
    if (NewCam != nullptr)
    {
        const float* const Src = AsCamera(NewCam)->GetOrientation();
        for (int I = 0; I < 12; ++I)
            Current[I] = Src[I];
    }
    else
    {
        for (int I = 0; I < 12; ++I)
            Current[I] = 0.0f;
        Current[0] = 1.0f;
        Current[4] = 1.0f;
        Current[8] = 1.0f;
    }

    // 0x00549da8..0x00549e11: a cut (or no previous camera) raises +0xbc on the active camera,
    // re-read from the controller -- null after a switch to nothing, so no flag then.
    if (SpaceCameraIsHardCut(Saved, Current, bWasNone) && G_SpaceApp.ActiveCamera != nullptr)
        G_SpaceApp.ActiveCamera->FlagBC = 1;
}

bool SpaceCameraLoadCockpit(const char* CockpitIniPath)
{
    // 0x0054ad25 .. 0x0054ad5d: the in-flight init hands the SAME path to all four cockpit
    // cameras, in this order, each as a __thiscall on its own subobject. Retail ignores every
    // return value; the port folds them so a caller can tell whether anything complained.

    bool bOk = true;
    FirstPersonCameraLoad(&G_SpaceApp.FirstPerson, CockpitIniPath);
    bOk = ThirdPersonCameraLoad(&G_SpaceApp.ThirdPerson, CockpitIniPath) && bOk;
    bOk = TurretCameraLoad(&G_SpaceApp.Turret, CockpitIniPath) && bOk;
    bOk = RearViewCameraLoad(&G_SpaceApp.RearView, CockpitIniPath) && bOk;
    return bOk;
}

FSpaceCamera* SwitchSpaceCameraMode(const char* ModeName, void* AttachTarget)
{
    // FUN_00549e70. Retail copies the name into a 0x20-byte stack buffer via FUN_00417c50
    // first, so a name longer than 31 characters is silently truncated before the comparisons;
    // that truncation is reproduced because it decides which mode a long string lands in.
    char Name[0x20];
    Name[0] = 0;
    if (ModeName != nullptr)
    {
        unsigned i = 0;
        while (i + 1 < sizeof(Name) && ModeName[i] != 0)
        {
            Name[i] = ModeName[i];
            ++i;
        }
        Name[i] = 0;
    }

    // DAT_00614028 = 1 unconditionally, before any name test.
    G_SpaceCameraSwitched = 1;

    G_LastModeWasScript = false;

    int Selected = -1;
    bool bCrosshair = false;

    if (strcmp(Name, "player") == 0)
    {
        // 0x00549ecb..0x00549eff: the rear view (+0x5ba == 1 -> FUN_005763b0(0x39, 0)) and the
        // turret view (+0x5b9 == 1 -> FUN_005763b0(0xbf, 0)) of DAT_00674aa0 are closed. The
        // bytes are the body of FUN_00549e30 inlined, instruction for instruction.
        CloseRearAndTurretViewsForDocking();
        if (G_SpaceApp.PlayerPreference == 0)
            Selected = kSpaceCameraFirstPerson;
        else if (G_SpaceApp.PlayerPreference == 1)
            Selected = kSpaceCameraThirdPerson;
        // Any other stored value selects nothing at all -- retail leaves EBP null and falls
        // through to the crosshair branch regardless.
        bCrosshair = true;
    }
    else if (strcmp(Name, "first_person") == 0)
    {
        CloseRearAndTurretViewsForDocking();   // 0x00549f4a..0x00549f7e, same inlined body
        G_SpaceApp.PlayerPreference = 0;
        Selected = kSpaceCameraFirstPerson;
        bCrosshair = true;
    }
    else if (strcmp(Name, "third_person") == 0)
    {
        CloseRearAndTurretViewsForDocking();   // 0x00549fa1..0x00549fd5, same inlined body
        G_SpaceApp.PlayerPreference = 1;
        Selected = kSpaceCameraThirdPerson;
        bCrosshair = true;
    }
    else if (strcmp(Name, "turret") == 0)
    {
        Selected = kSpaceCameraTurret;
        bCrosshair = true;
    }
    else if (strcmp(Name, "rearview") == 0)
    {
        Selected = kSpaceCameraRearview;
        bCrosshair = true;
    }
    else if (strcmp(Name, "death") == 0)
    {
        Selected = kSpaceCameraDeath;
    }
    else if (strcmp(Name, "script") == 0)
    {
        G_LastModeWasScript = true;
        Selected = kSpaceCameraScript;
    }
    else if (strcmp(Name, "static") == 0)
    {
        Selected = kSpaceCameraStatic;
    }
    else if (strcmp(Name, "docking") == 0)
    {
        Selected = kSpaceCameraDocking;
    }
    else
    {
        // Unknown name: retail returns immediately without even touching the cursor.
        return nullptr;
    }

    if (bCrosshair)
    {
        // 0x00549f1b: the five piloting modes select the "Cross" cursor as the new default
        // (FUN_0041dde0("Cross", 1)) and turn the software-cursor draw latch on
        // (FUN_0041dda0(1)). Reached even when "player" selected nothing.
        MouseSetCursor("Cross", true);
        SetClientMouseCursorDrawFlag(1);
    }
    else
    {
        // 0x0054a09e: the four cinematic modes only turn the latch off (FUN_0041dda0(0)).
        SetClientMouseCursorDrawFlag(0);
    }

    // 0x0054a0a8: TEST EBP,EBP -- "player" with a stored preference other than 0/1 installs
    // nothing and returns null.
    if (Selected < 0)
        return nullptr;

    FSpaceCamera* Cam = CameraForSlot(Selected);

    // 0x0054a0b4: FUN_00549bc0(this, Cam, local_28) -- local_28 is the "script" flag, passed as
    // the force argument.
    InstallSpaceCamera(Cam, G_LastModeWasScript);

    if (AttachTarget != nullptr)
    {
        // 0x0054a0c1: the SELECTED camera's slot 0x58 (attach) with the target. Every camera
        // CameraForSlot hands out is a GameCamera, whose 32-slot table carries slot 0x58; the
        // port's slot leaves take `this` on the stack. Retail discards the result (the next
        // instruction is the epilogue), so it is called as returning nothing.
        ((FGameCamera*)Cam)->Attach(AttachTarget);
    }

    return Cam;
}

void ReleaseSpaceCameraControllerParts()
{
    // 0x0054aef4: FUN_00518410 on controller + 0x00c.
    FirstPersonCameraReleaseMesh(&G_SpaceApp.FirstPerson);
    // 0x0054aefc / 0x0054af07 / 0x0054af12: IDPMsgHandler::OnDisconnect (0x00401e90, RET) on
    // + 0x1b0, + 0x3c8 and + 0x500 -- empty bodies, nothing to do.
}

FSpaceCamera* GetActiveSpaceCamera()
{
    return G_SpaceApp.ActiveCamera;
}

void RaiseActiveSpaceCameraCut()
{
    // FUN_0054bd50: MOV EAX,[DAT_00679738]; TEST; JZ; MOV byte [EAX+0xbc],1.
    if (G_SpaceApp.ActiveCamera != nullptr)
        G_SpaceApp.ActiveCamera->FlagBC = 1;
}

bool IsSpaceCameraControllerReady()
{
    // DAT_00678ba0 = SpaceApp + 0x08 (read at 0x0054bcb0); written only by the space init
    // (0x0054ab8e) and teardown (0x0054af8d), both in space_state.cpp.
    return G_SpaceApp.bCameraReady != 0;
}

bool IsSpaceInitInProgress()
{
    // FUN_0054bd40: MOV AL,[DAT_00678ba3] -- SpaceApp + 0x0b, written only by the space init.
    return G_SpaceApp.bInitInProgress != 0;
}

FSpaceCamera* GetFirstPersonSpaceCamera()
{
    // FUN_0054bc30: MOV EAX,0x00678ba4 / RET.
    return CameraForSlot(kSpaceCameraFirstPerson);
}

FSpaceCamera* GetThirdPersonSpaceCamera()
{
    // FUN_0054bc40: MOV EAX,0x00678d48 / RET.
    return CameraForSlot(kSpaceCameraThirdPerson);
}

FSpaceCamera* GetTurretSpaceCamera()
{
    // FUN_0054bc50: MOV EAX,0x00678f60 / RET.
    return CameraForSlot(kSpaceCameraTurret);
}

bool IsFirstPersonCameraCurrent()
{
    // FUN_0054bc80: CALL FUN_0040f130 (the camera at DAT_006164dc) / SUB EAX,0x00678ba4 /
    // NEG / SBB / INC -- true exactly when that camera is the first-person camera, which lives
    // at controller 0x00678b98 + 0x0c. The port's FUN_0040f130 is
    // GetClientSoundListenerObject(); the first-person camera is the kSpaceCameraFirstPerson
    // slot. No null test: a null listener simply compares unequal, as in retail.
    return GetClientSoundListenerObject() ==
           static_cast<void*>(CameraForSlot(kSpaceCameraFirstPerson));
}

bool IsDockingCameraCurrent()
{
    // FUN_0054bca0: CALL FUN_0040f130 / SUB EAX,0x00679634 / NEG / SBB / INC -- true exactly
    // when that camera is the docking camera (.data 0x00679634, the object FUN_0054bbd0 tests
    // and arms). No null test, as in retail.
    return GetClientSoundListenerObject() == static_cast<void*>((FSpaceCamera*)&G_SpaceApp.Docking);
}

void NotifyCinematicView(bool bActive)
{
    // 0x0051e615 / 0x0051e6f1 (script camera) and 0x0051e93f / 0x0051e985 (static camera).
    //
    // FUN_0054baf0: the player's IObjRW (the watcher's pointer minus eight); null ends it.
    IObjRW* const Ship = static_cast<IObjRW*>(GetPlayerObjRW());
    if (Ship == nullptr)
        return;

    // 0x0051e61e..0x0051e63f: the four arguments of IServer slot 57 (+0xe4, RET 0x10) are pushed
    // right to left around the ship's call -- DAT_00673344, 5, bActive -- and the ship's
    // IObjInspect slot 8 (+0x20) get_id takes none of them; its answer is pushed last as the
    // first argument. DAT_0067ecd0 is used with no null test.
    const unsigned Connection = GetPlayerConnectionId();
    const unsigned ShipId = Ship->get_id();
    static_cast<IServer*>(GetLocalServerInterface())
        ->SPRequestInvincibility(ShipId, bActive, INVINCIBILITY_REASON_5, Connection);
}

void SetDockingCameraTransform(const float* Csys12)
{
    // FUN_0054bb90 (cdecl, one argument). 0x0054bb96: twelve dwords into the docking camera's
    // own Csys (.data 0x00679638 = controller 0x00678b98 + 0xa9c + 0x04), then its view-dirty
    // byte (0x00679668 = + 0x34) is raised -- both BEFORE the guard, so they land even when no
    // controller exists.
    FSpaceCamera* const Docking = (FSpaceCamera*)&G_SpaceApp.Docking;
    memcpy(Docking->World, Csys12, 9 * sizeof(float));
    memcpy(Docking->WorldPos, Csys12 + 9, 3 * sizeof(float));
    Docking->bViewDirty = 1;

    // 0x0054bba9: DAT_00678ba0, the "controller exists" byte (see RequestSpaceCameraMode), then
    // FUN_00549e70("docking", 0).
    if (G_SpaceApp.bCameraReady)
        SwitchSpaceCameraMode("docking", nullptr);
}

void BeginDockingCameraSequence(float Seconds)
{
    // FUN_0054bbd0 (cdecl, one float). 0x0054bbd0: the active camera (controller + 0xba0) must
    // be the docking camera at controller + 0xa9c, or retail complains -- and arms it anyway.
    if (G_SpaceApp.ActiveCamera != (FSpaceCamera*)&G_SpaceApp.Docking)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Client\\space\\space.cpp", 0x8ad,
              "static docking camera must be the active camera before setting the fade out "
              "timer");
    }
    // 0x0054bc0a: FUN_0051ebd0 on the docking camera.
    DockingCameraBeginSequence(&G_SpaceApp.Docking, Seconds);
}

void SetupSpaceScriptCamera(void* Object, const float* Transform12, const char* ScriptName)
{
    // FUN_0054bb10 (cdecl, three arguments). 0x0054bb10..0x0054bb24: FUN_0051e3b0 on the script
    // camera (.data 0x006792f8 = controller 0x00678b98 + 0x760) with the arguments reordered to
    // (ScriptName, Object, Transform12) -- before the guard, like FUN_0054bb90.
    ScriptCameraSetup(&G_SpaceApp.Script, ScriptName, Object, Transform12);

    // 0x0054bb29..0x0054bb3e: DAT_00678ba0, then FUN_00549e70("script", 0).
    if (G_SpaceApp.bCameraReady)
        SwitchSpaceCameraMode("script", nullptr);

    // 0x0054bb43: the latch FUN_0054bc90 reports, raised on both paths. DAT_00679740 is SpaceApp
    // + 0xba8 (0x00678b98 + 0xba8), the byte the frame drops at 0x0054b1fa and the teardown at
    // 0x0054af2e.
    G_SpaceApp.bScriptCameraLatch = 1;
}

bool GetSpaceScriptCameraLatch()
{
    // FUN_0054bc90: MOV AL,[DAT_00679740]; RET.
    return G_SpaceApp.bScriptCameraLatch != 0;
}

void RequestSpaceCameraMode(const char* ModeName)
{
    // FUN_0054bc10. 0x0054bc10: DAT_00678ba0 (controller + 0x08), raised by the space init and
    // dropped by the space teardown.
    if (!G_SpaceApp.bCameraReady)
        return;
    SwitchSpaceCameraMode(ModeName, nullptr);
}

int GetSpaceCameraPlayerPreference()
{
    return G_SpaceApp.PlayerPreference;
}

void SetSpaceCameraPlayerPreference(int Preference)
{
    // 0x0054ae06: MOV [ESI + 0xba4],EBX (EBX = 0 there).
    G_SpaceApp.PlayerPreference = Preference;
}

bool WasLastSpaceCameraModeScript()
{
    // FUN_00549e70's local_28, which it hands to FUN_00549bc0 as the second argument.
    return G_LastModeWasScript;
}

GENRESULT DACOM_API FSpaceCamera::QueryInterface(char const* Iid, void** Out)
{
    // LAB_0040f350: MOV EAX,[ESP+0xc]; MOV [EAX],0 (no null test); OR EAX,-1; RET 0xc.
    *Out = nullptr;
    return -1;
}

unsigned long DACOM_API FSpaceCamera::AddRef()
{
    // LAB_0040f360: MOV EAX,1 / RET 4. Occupies both slot 0x04 and slot 0x08.
    return 1;
}

unsigned long DACOM_API FSpaceCamera::Release()
{
    return 1;
}

float const* DACOM_API FSpaceCamera::GetPosition()
{
    return this->WorldPos;                       // LAB_0040f370: this + 0x28
}

float const* DACOM_API FSpaceCamera::GetOrientation()
{
    return this->World;                          // LAB_0040f380: this + 0x04
}

int const* DACOM_API FSpaceCamera::GetViewport()
{
    return this->Viewport;                       // LAB_0040f470: this + 0xac
}

float DACOM_API FSpaceCamera::GetHalfFovXDeg()
{ return this->HalfFovXDeg; }

float DACOM_API FSpaceCamera::GetFovYDeg()
{ return this->FovYDeg; }

float DACOM_API FSpaceCamera::GetZNear()
{ return this->ZNear; }

float DACOM_API FSpaceCamera::GetZFar()
{ return this->ZFar; }

float DACOM_API FSpaceCamera::GetAspect()
{ return this->Aspect; }

double DACOM_API FSpaceCamera::GetXScaleOverNear()
{
    return static_cast<double>(this->XScale) / this->ZNear; // LAB_0040f4d0
}

double DACOM_API FSpaceCamera::GetYScaleOverNear()
{
    return static_cast<double>(this->YScale) / this->ZNear; // LAB_0040f4e0
}

int DACOM_API FSpaceCamera::Project(float* OutX, float* OutY, float* OutZ, float const* World)
{
    // FUN_0040ff50 (explicit this, RET 0x14). 0x0040ff50..0x0040ff7c: the world point into
    // camera space through math engine slot 0x3c (InverseTransform) against the camera's
    // world transform at +0x04 (orientation + position, one FTransform).
    if (G_CameraMathEngine == nullptr)
        G_CameraMathEngine = Acquire3DMathEngine();
    FVector Local;
    G_CameraMathEngine->InverseTransform(Local, *reinterpret_cast<FTransform const*>(this->World),
                                         *reinterpret_cast<FVector const*>(World));

    // 0x0040ff7f: FLD ZNear, FCHS, FCOMP z, TEST AH,0x1 / JNZ -- C0 (below or unordered)
    // returns 0, so only -ZNear >= z (ordered) projects; a point exactly on the near plane
    // does.
    if (!(-this->ZNear >= Local.Z))
        return 0;

    // 0x0040ff92..0x0040ffcf: InvZ = -1.0f (.rdata 0x005c7500) / z stays on the FPU; each
    // screen coordinate is local * scale * InvZ + centre, rounded once at the store; the
    // depth is -z.
    const double InvZ = -1.0 / static_cast<double>(Local.Z);
    *OutX = static_cast<float>(static_cast<double>(Local.X) * this->XScale * InvZ +
                               this->ScreenCenterX);
    *OutY = static_cast<float>(static_cast<double>(Local.Y) * this->YScale * InvZ +
                               this->ScreenCenterY);
    *OutZ = -Local.Z;
    return 1;
}

void DACOM_API FSpaceCamera::Unproject(float* OutDir, float ScreenX, float ScreenY)
{
    // LAB_0040f520. Screen point -> a direction in world space:
    //   local.x = (ScreenX - ScreenCenterX) * (ZNear / XScale)
    //   local.y = (ScreenY - ScreenCenterY) * (ZNear / YScale)
    //   local.z = -ZNear
    // then math-engine slot 0x2c (TransformMatrix) rotates it by the world orientation: the
    // nine floats at this+0x04 are copied to the stack (REP MOVSD 9, 0x0040f583), DAT_006164d8
    // is tested and filled (0x0040f56e..0x0040f597), and the result is copied to OutDir.
    FVector Local;
    Local.X = (ScreenX - this->ScreenCenterX) * (this->ZNear / this->XScale);
    Local.Y = (ScreenY - this->ScreenCenterY) * (this->ZNear / this->YScale);
    Local.Z = -this->ZNear;
    FMatrix Rotation;
    memcpy(&Rotation, this->World, sizeof(Rotation));
    if (G_CameraMathEngine == nullptr)
        G_CameraMathEngine = Acquire3DMathEngine();
    FVector Dir;
    G_CameraMathEngine->TransformMatrix(Dir, Rotation, Local);
    OutDir[0] = Dir.X;
    OutDir[1] = Dir.Y;
    OutDir[2] = Dir.Z;
}

int DACOM_API FSpaceCamera::ClassifySphere(float const* Center, float Radius)
{
    // FUN_0040ffe0. Classifies a sphere against the frustum and returns a CODE, not a
    // boolean: 1 = outside, 3 = straddling a plane, 4 = fully inside. The result starts at
    // 4 and is knocked down to 3 by any plane the sphere crosses; any plane it is fully
    // outside of returns 1 immediately.
    //
    // The camera looks down -Z, so the distance along the view axis is -z, and the sphere
    // spans [-z - Radius, -z + Radius].
    const float NearEdge = -Center[2] - Radius;
    const float FarEdge = Radius - Center[2];

    int Result = 4;

    // 0x00410009: entirely nearer than the near plane -> out.
    if (FarEdge < this->ZNear)
        return 1;
    // 0x00410026: crossing it -> straddling.
    if (NearEdge < this->ZNear)
        Result = 3;

    // 0x0041003c: the left/right pair, folded into one test by taking |x|. PlaneX is the
    // unit normal the ctor built as normalise(1, 0, tan(halfFovX)).
    // The distance stays on the FPU for both compares (FCOM, then FCOMP against -Radius),
    // so it is never rounded to float.
    const double DistX = static_cast<double>(fabsf(Center[0])) * this->PlaneX[0] +
                         static_cast<double>(this->PlaneX[2]) * Center[2];
    if (DistX > Radius)
        return 1;
    if (DistX > -Radius)
        Result = 3;

    // 0x00410084: the top/bottom pair, same shape with PlaneY.
    const double DistY = static_cast<double>(fabsf(Center[1])) * this->PlaneY[1] +
                         static_cast<double>(this->PlaneY[2]) * Center[2];
    if (DistY > Radius)
        return 1;
    if (DistY > -Radius)
        Result = 3;

    // 0x004100c3: entirely beyond the far plane -> out; crossing it -> straddling.
    if (NearEdge > this->ZFar)
        return 1;
    if (FarEdge > this->ZFar)
        Result = 3;

    return Result;
}

float const* DACOM_API FSpaceCamera::GetViewTransform()
{
    // LAB_0040f390. Lazily rebuilds the cached view transform at this+0x38 and returns it.
    //
    // 0x0040f3a3..0x0040f3c8: math engine DAT_006164d8 (FUN_005b73e0 on first use) slot 0x38
    // TransposeTransform(out, World matrix at +0x04, WorldPos at +0x28) = R^T * p.
    // 0x0040f3cb..0x0040f45a: a local transform {transpose(World), -out} (the matrix read
    // column by column) copied whole to +0x38..+0x67; then +0x34 = 0.
    if (this->bViewDirty != 0)
    {
        if (G_CameraMathEngine == nullptr)
            G_CameraMathEngine = Acquire3DMathEngine();
        FVector Out;
        G_CameraMathEngine->TransposeTransform(Out, *reinterpret_cast<FMatrix const*>(this->World),
                                               *reinterpret_cast<FVector const*>(this->WorldPos));

        const float* R = this->World;
        float* V = this->View;
        V[0] = R[0]; V[1] = R[3]; V[2] = R[6];
        V[3] = R[1]; V[4] = R[4]; V[5] = R[7];
        V[6] = R[2]; V[7] = R[5]; V[8] = R[8];
        this->ViewPos[0] = -Out.X;
        this->ViewPos[1] = -Out.Y;
        this->ViewPos[2] = -Out.Z;

        this->bViewDirty = 0;
    }
    return this->View;
}

int DACOM_API FSpaceCamera::ProjectVec(float* OutXYZ, float const* World)
{
    // LAB_0040f4f0: hands slot 0x34 the three components of the destination vector as three
    // separate out-pointers (it pushes arg+8, arg+4 and arg), plus the world point, through
    // the vtable (0x0040f50a CALL [EDX+0x34]), and returns that call's EAX unchanged.
    return Project(&OutXYZ[0], &OutXYZ[1], &OutXYZ[2], World);
}

void FSpaceCamera::GetVelocity(float* Out)
{
    // LAB_0040f5d0: MOV EAX,[ESP+4]; zero [EAX], [EAX+4], [EAX+8]; RET 4.
    //
    // ICamera::GetVelocity is __thiscall (this in ECX, Out on stack). The leaf never
    // reads this — only the out-vector. Same cleanup width as a stdcall one-arg.
    Out[0] = 0.0f;
    Out[1] = 0.0f;
    Out[2] = 0.0f;
}

int FSpaceCamera::SetProperty(char const* Key, char const* Value)
{
    return SetPropertyImpl(this, Key, Value);
}
