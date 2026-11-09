// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail third-person camera (in-flight controller + 0x1b0, vtable .rdata 0x005dbb14).
//
// Ported here: the constructor (0x00518600), the class identity (slot 0x54), the cockpit-ini
// loader FUN_005187d0 (almost entirely validation -- the [ThirdPerson] section is OBSOLETE in
// the shipped game and no key is read from it), the Event::Handler face (FUN_0051a190), and
// the class's own behavioural slots: activate 0x64 (FUN_00518ad0), attach 0x58 (0x00518af0) and
// update 0x6c (0x00518e40), with the chase solver both of the last two call, FUN_00518f20.

#include "space_camera_third.h"
#include "space_scene_obj_draw.h"

#include "client_gameobj.h"          // FClientShipGameobj::Field_0x158
#include "hud_command_dispatch.h"    // HudRegisterCommandHandler (FUN_005760d0)
#include "hud_target.h"              // HudIsMousePickBlocked (FUN_0045a740)
#include "ship_camera_params.h"      // FShipCameraParams (ship companion +0x68)
#include "space_camera_first.h"      // G_FirstPersonMathEngine (DAT_00675220)
#include "win3d.h"                   // Win3DTransformByMatrix (FUN_00423b90)

#include "common_archetype.h"        // Archetype::Root::Companion (+0x04)
#include "common_cobject.h"          // CObject::get_archetype
#include "common_csys.h"             // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"            // FindHardpoint_PS, HardpointInfo
#include "common_iface.h"            // GetBehaviorManager, IBehaviorManager
#include "common_ini.h"
#include "dacom.h"
#include "x86math.h"

#include <math.h>
#include <string.h>

namespace
{
    // .rdata 0x005c77f4, the float the loader multiplies both angles by.
    const float kDegreesToRadians = 0.01745329238474369f;
    // .rdata 0x005dba18 and 0x005d5b58, both doubles.
    const double kExpectedAspect = 1.333;
    const double kAspectTolerance = 0.05;

    const char kCockpitCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Cockpit.cpp";

    // A BaseWatcher holds the Watchable eight bytes into the client object.
    const int kWatchableOffset = 8;

    // FUN_00518f20's constants, every one read out of the exe.
    const float  kMinDeltaTime = 9.999999747378752e-06f;   // .rdata 0x005d7e84
    const float  kSlackAccelScale = 0.019999999552965164f; // .rdata 0x005d1fe4
    const double kSlackThreshold = 0.01;                   // .rdata 0x005d5138 (double)
    const float  kSlackCapScale = 0.03999999910593033f;    // .rdata 0x005d845c
    const float  kLevelTurnMinDot = -0.9900000095367432f;  // .rdata 0x005dbcb0 (float)
    const double kLevelTurnMaxDot = 0.9998999834060669;    // .rdata 0x005dbca8 (double)
    const double kLevelUpAgreeDot = 0.995;                 // .rdata 0x005dbca0 (double)
    const double kLevelAxisMaxDot = 0.9999;                // .rdata 0x005dafa8 (double)
    const double kMinTurnAngle = 0.0001;                   // .rdata 0x005dbc98 (double)
    const float  kDownTurnYawScale = 0.800000011920929f;   // .data  0x006135c4 (read only)


    // The face's slot 0 is called __thiscall (face in ECX, id on the stack); __fastcall with an
    // unused EDX has the same shape.


    // .rdata 0x005dbb0c (installed at 0x00518612): one slot, FUN_0051a190.

    // DAT_00675220, acquired through FUN_005b73e0 on first use -- retail repeats the test before
    // every engine call in FUN_00518f20.
    I3DMathEngine* CockpitMathEngine()
    {
        if (G_FirstPersonMathEngine == nullptr)
            G_FirstPersonMathEngine = Acquire3DMathEngine();
        return G_FirstPersonMathEngine;
    }

    FMatrix const& AsMatrix(const float* M9)
    {
        return *reinterpret_cast<FMatrix const*>(M9);
    }

    FMatrix& AsMatrix(float* M9)
    {
        return *reinterpret_cast<FMatrix*>(M9);
    }

    FVector const& AsVector(const float* V3)
    {
        return *reinterpret_cast<FVector const*>(V3);
    }

    // FUN_00423b30 (cdecl Out, A, B): Out = A + B, z summed first, each sum stored as a float.
    void Vec3AddStored(float* Out, const float* A, const float* B)
    {
        const float Z = static_cast<float>(static_cast<double>(A[2]) + B[2]);
        const float Y = static_cast<float>(static_cast<double>(A[1]) + B[1]);
        const float X = static_cast<float>(static_cast<double>(A[0]) + B[0]);
        Out[0] = X;
        Out[1] = Y;
        Out[2] = Z;
    }

    // FUN_0051a0c0 (cdecl Out, A, B): Out = A x B, the three terms on the FPU, stored as floats.
    void Vec3CrossStored(float* Out, const float* A, const float* B)
    {
        const double Z = static_cast<double>(A[0]) * B[1] - static_cast<double>(A[1]) * B[0];
        const double Y = static_cast<double>(A[2]) * B[0] - static_cast<double>(B[2]) * A[0];
        const double X = static_cast<double>(A[1]) * B[2] - static_cast<double>(A[2]) * B[1];
        Out[0] = static_cast<float>(X);
        Out[1] = static_cast<float>(Y);
        Out[2] = static_cast<float>(Z);
    }

    // FUN_00410190 (thiscall): sqrt((x*x + y*y) + z*z), unrounded.
    double Vec3Length(const float* V)
    {
        double SumSq = static_cast<double>(V[0]) * V[0] + static_cast<double>(V[1]) * V[1];
        SumSq = SumSq + static_cast<double>(V[2]) * V[2];
        return sqrt(SumSq);
    }

    // FUN_0041a390 (thiscall): scale by 1.0f / length (FDIVR, unrounded), each product stored.
    void Vec3NormaliseInPlace(float* V)
    {
        const double Inv = 1.0f / Vec3Length(V);
        V[0] = static_cast<float>(Inv * V[0]);
        V[1] = static_cast<float>(Inv * V[1]);
        V[2] = static_cast<float>(Inv * V[2]);
    }

    // FUN_004cbf40 (cdecl A, B): (a.z*b.z + a.y*b.y) + a.x*b.x, left on the FPU.
    double Vec3DotZyx(const float* A, const float* B)
    {
        double Sum = static_cast<double>(A[2]) * B[2] + static_cast<double>(A[1]) * B[1];
        return Sum + static_cast<double>(A[0]) * B[0];
    }

    // Column C of a row-major 3x3 (elements C, C + 3, C + 6).
    void GetColumn(float* Out, const float* M9, int Column)
    {
        Out[0] = M9[Column];
        Out[1] = M9[Column + 3];
        Out[2] = M9[Column + 6];
    }

    // FUN_0051a100 / FUN_0051a120 / FUN_00411f70 (thiscall on the matrix): write column 0 / 1 / 2.
    void SetColumn(float* M9, int Column, const float* V)
    {
        M9[Column] = V[0];
        M9[Column + 3] = V[1];
        M9[Column + 6] = V[2];
    }

    // The two rotation loops FUN_00518f20 runs over a 3x3 in place: cos is stored as a float
    // first, sin stays on the FPU. Pitch works columns 1 and 2, yaw columns 0 and 2.
    void RotatePitchRows(float* M9, double Angle)
    {
        const float CosA = static_cast<float>(cos(Angle));
        const double SinA = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double B = M9[Row * 3 + 1];
            const double C = M9[Row * 3 + 2];
            M9[Row * 3 + 1] = static_cast<float>(SinA * C + B * CosA);
            M9[Row * 3 + 2] = static_cast<float>(static_cast<double>(CosA) * C - B * SinA);
        }
    }

    void RotateYawRows(float* M9, double Angle)
    {
        const float CosA = static_cast<float>(cos(Angle));
        const double SinA = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double A = M9[Row * 3 + 0];
            const double C = M9[Row * 3 + 2];
            M9[Row * 3 + 0] = static_cast<float>(static_cast<double>(CosA) * A - SinA * C);
            M9[Row * 3 + 2] = static_cast<float>(static_cast<double>(CosA) * C + A * SinA);
        }
    }

    void MakeIdentityView(float* View12)
    {
        for (int Index = 0; Index < 12; ++Index)
            View12[Index] = 0.0f;
        View12[0] = 1.0f;
        View12[4] = 1.0f;
        View12[8] = 1.0f;
    }

    // The watcher, and the object behind it (watcher - 8, null when there is none) -- retail
    // re-reads +0xc0 each time it needs the object.
    IObjRW* WatchedObject(FThirdPersonCamera* Cam)
    {
        return static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Cam));
    }

    // The watcher chain every solver entry tests: watcher, watcher - 8, its +0xc'd pointer's
    // +0x4 (ObjRW + 0x10) and the ship class mask 0x503 at CObject + 0x4c.
    CEqObj* WatchedShip(FThirdPersonCamera* Cam)
    {
        const unsigned char* const Held =
            reinterpret_cast<const unsigned char*>(Cam->Watched.target());
        if (Held == nullptr || Held - kWatchableOffset == nullptr)
            return nullptr;
        const unsigned char* const Plus0c = Held - kWatchableOffset + 0xc;
        if (Plus0c == nullptr)
            return nullptr;
        CEqObj* const EqObj = *reinterpret_cast<CEqObj* const*>(Plus0c + 4);
        if (EqObj == nullptr ||
            (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) != 0x503u)
            return nullptr;
        return EqObj;
    }
}

float* CockpitMulMatrix(float* Out9, const float* A9, const float* B9)
{
    // FUN_0051a140 (cdecl): DAT_00675220 (FUN_005b73e0 on first use), MulMatrix (vt+0x20) into
    // a stack local, the nine dwords copied to Out; EAX = Out.
    float Product[9];
    CockpitMathEngine()->MulMatrix(AsMatrix(Product), AsMatrix(A9), AsMatrix(B9));
    memcpy(Out9, Product, sizeof(Product));
    return Out9;
}

bool ThirdPersonCameraOnEvent(FThirdPersonCamera* Cam, unsigned MsgId)
{
    // FUN_0051a190 (thiscall on the face, RET 4). Blocked mouse pick (FUN_0045a740) -> false;
    // HUDCamera's FUN_0051d7f0 taking it -> true.
    if (HudIsMousePickBlocked())
        return false;
    if (HudCameraOnEvent((FHudCamera*)Cam, MsgId))
        return true;

    // 0x0051a1bb: only 0x3f is this class's own.
    if (MsgId != 0x3f)
        return false;

    // 0x0051a1cd..0x0051a1d5: flip +0x13c (face + 0x44).
    Cam->bCameraLevelEnabled = (Cam->bCameraLevelEnabled == 0) ? 1 : 0;

    // 0x0051a1d8..0x0051a1fc: the watched object (face - 0x38 = +0xc0, minus eight, null when
    // there is none) -> GetBehaviorManager -> update_level_camera(flag) when there is a manager.
    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    IObjRW* const Target =
        (Held != nullptr)
            ? reinterpret_cast<IObjRW*>(const_cast<unsigned char*>(Held - kWatchableOffset))
            : nullptr;
    IBehaviorManager* const Manager = GetBehaviorManager(Target);
    if (Manager != nullptr)
        Manager->update_level_camera(Cam->bCameraLevelEnabled != 0);
    return true;
}

const char* ThirdPersonCameraGetName()
{
    // 0x005187c0: MOV EAX, .rdata "ThirdPerson" / RET. No `this`, no arguments.
    return "ThirdPerson";
}

void ThirdPersonCameraConstruct(FThirdPersonCamera* Cam)
{
    // FUN_00518600: HUDCamera's ctor (FUN_0051ca80), then the two vptrs.
    HudCameraConstruct((FHudCamera*)Cam);

    // 0x0051861c..0x0051864b: the hardpoint position, three zero dwords through stack temps.
    Cam->HardpointPos[0] = 0.0f;
    Cam->HardpointPos[1] = 0.0f;
    Cam->HardpointPos[2] = 0.0f;

    // 0x0051864e..0x00518685: the hardpoint orientation, identity.
    Cam->HardpointOrient[7] = 0.0f;   // +0x128
    Cam->HardpointOrient[6] = 0.0f;   // +0x124
    Cam->HardpointOrient[5] = 0.0f;   // +0x120
    Cam->HardpointOrient[3] = 0.0f;   // +0x118
    Cam->HardpointOrient[2] = 0.0f;   // +0x114
    Cam->HardpointOrient[1] = 0.0f;   // +0x110
    Cam->HardpointOrient[8] = 1.0f;   // +0x12c
    Cam->HardpointOrient[4] = 1.0f;   // +0x11c
    Cam->HardpointOrient[0] = 1.0f;   // +0x10c

    // 0x0051868b..0x005186b0: the look offset {0, 7.0f (0x40e00000), 60.0f (0x42700000)}.
    Cam->Field140 = 0.0f;
    Cam->OffsetX = 7.0f;
    Cam->OffsetY = 60.0f;

    // 0x005186b3..0x005186bf: the second offset vector.
    Cam->Field14C = 0.0f;
    Cam->Field150 = 0.0f;
    Cam->Field154 = 0.0f;

    // 0x005186c5..0x005186cb: HUDCamera's bytes +0xfc and +0xfd.
    Cam->bFlagFC = 0;
    Cam->bFlagFD = 0;

    // 0x005186d1..0x005186f4: the speed tracker, auto-level on.
    Cam->SpeedSlack = 0.0f;
    Cam->SmoothedSpeed = 0.0f;
    Cam->PrevSpeed = 0.0f;
    Cam->bCameraLevelEnabled = 1;
    Cam->LookAheadMultiplier = 0.05f;      // 0x3d4ccccd
    Cam->AccelerationLimit = 40.0f;        // 0x42200000

    // 0x005186fe..0x0051872e: the orientation history, identity.
    Cam->ResetOrient[8] = 1.0f;   // +0x178
    Cam->ResetOrient[4] = 1.0f;   // +0x168
    Cam->ResetOrient[0] = 1.0f;   // +0x158
    Cam->ResetOrient[7] = 0.0f;   // +0x174
    Cam->ResetOrient[6] = 0.0f;   // +0x170
    Cam->ResetOrient[5] = 0.0f;   // +0x16c
    Cam->ResetOrient[3] = 0.0f;   // +0x164
    Cam->ResetOrient[2] = 0.0f;   // +0x160
    Cam->ResetOrient[1] = 0.0f;   // +0x15c

    // 0x00518734..0x00518753: the turn angles (radians) and the look-ahead slerp.
    Cam->VerticalTurnUpAngle = 0.1f;          // 0x3dcccccd
    Cam->TurnLookAheadSlerpAmount = 0.5f;     // 0x3f000000
    Cam->HorizontalTurnAngle = 0.2f;          // 0x3e4ccccd
    Cam->VerticalTurnDownAngle = 0.2f;        // 0x3e4ccccd

    // 0x00518759..0x0051876b: up (0, 1, 0) and +0x210.
    Cam->UpAxis[1] = 1.0f;
    Cam->UpAxis[0] = 0.0f;
    Cam->UpAxis[2] = 0.0f;
    Cam->Field210 = 0.0f;

    // 0x00518771..0x00518798.
    Cam->TurnRateScale = 2.0f;                // 0x40000000
    Cam->LevelAxis[0] = 0.0f;
    Cam->LevelAxis[1] = 0.0f;
    Cam->LevelAxis[2] = -1.0f;                // 0xbf800000
    Cam->PrevAngularRate = 0.0f;
    Cam->bInitialised = 0;

    // 0x005187a3..0x005187a9: the angular limits.
    Cam->AngularSlerpMultiplier = 2.0f;       // 0x40000000
    Cam->AngularAcceleration = 0.049087385f;  // 0x3d490fdb (pi / 64)

    // Not written by the retail ctor (the controller is zeroed static storage):
    // TranslationCache (+0x130), TargetOrientB (+0x17c), TargetOrientA (+0x1b0) and the pad bytes.
}

bool ThirdPersonCameraActivate(FThirdPersonCamera* Cam, const int* ViewRect4)
{
    // FUN_00518ad0 (thiscall, RET 4): event 0x3f through FUN_005760d0 on the face at +0xf8,
    // then HUDCamera's FUN_0051cad0 -- its answer.
    FHudCmdHandler* const Face = Cam->AsHudCmdHandler();
    HudRegisterCommandHandler(Face, 0x3f);
    return HudCameraActivate((FHudCamera*)Cam, ViewRect4);
}

void ThirdPersonCameraAttach(FThirdPersonCamera* Cam, void* Object)
{
    // LAB_00518af0 (slot 0x58, thiscall RET 4). 0x00518afe: HUDCamera's attach (FUN_0051cbb0).
    HudCameraAttach((FHudCamera*)Cam, Object);

    // 0x00518b03..0x00518b11: the second offset vector, on every attach.
    Cam->Field14C = 0.0f;
    Cam->Field150 = 0.0f;
    Cam->Field154 = 0.0f;

    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    if (Held != nullptr && Held - kWatchableOffset != nullptr)
    {
        // 0x00518b30..0x00518bc3: with a client ship, the camera block of its archetype's
        // companion (CObject::get_archetype, Root + 0x04, the block at + 0x68) -- retail reads
        // it at fixed offsets; FShipCameraParams records them.
        CEqObj* const Ship = WatchedShip(Cam);
        if (Ship != nullptr)
        {
            const unsigned char* const Companion = reinterpret_cast<const unsigned char*>(
                Ship->get_archetype()->Companion);
            if (Companion != nullptr)
            {
                const FShipCameraParams* const Params =
                    reinterpret_cast<const FShipCameraParams*>(Companion + 0x68);
                Cam->AngularSlerpMultiplier = Params->AngularSlerpMultiplier;      // +0x74
                Cam->AngularAcceleration = Params->AngularAcceleration;            // +0x70
                Cam->HorizontalTurnAngle = Params->HorizontalTurnAngle;            // +0x78
                Cam->VerticalTurnUpAngle = Params->VerticalTurnUpAngle;            // +0x7c
                Cam->VerticalTurnDownAngle = Params->VerticalTurnDownAngle;        // +0x80
                Cam->TurnLookAheadSlerpAmount = Params->TurnLookAheadSlerpAmount;  // +0x84
                Cam->Field140 = 0.0f;
                Cam->OffsetX = Params->Offset[0];                                  // +0x68
                Cam->OffsetY = Params->Offset[1];                                  // +0x6c
            }
        }

        // 0x00518bc9..0x00518c41: two copies of the object's orientation (slot 0xc), and the
        // up axis out of the first one's column 1.
        IObjRW* const Target = WatchedObject(Cam);
        memcpy(Cam->TargetOrientA, &Target->get_transform().Rotation.E[0][0], 9 * sizeof(float));
        memcpy(Cam->TargetOrientB, &WatchedObject(Cam)->get_transform().Rotation.E[0][0],
               9 * sizeof(float));
        Cam->UpAxis[0] = Cam->TargetOrientA[1];
        Cam->UpAxis[1] = Cam->TargetOrientA[4];
        Cam->UpAxis[2] = Cam->TargetOrientA[7];

        // 0x00518c44..0x00518c6a: slot 0x10's transform -- twelve dwords -- to the world
        // transform, view dirty first.
        FSpaceCamera* const Space = SpaceCameraOf((FHudCamera*)Cam);
        const float* const Xform =
            reinterpret_cast<const float*>(&WatchedObject(Cam)->get_orientation());
        Space->bViewDirty = 1;
        memcpy(Space->World, Xform, 9 * sizeof(float));
        Space->WorldPos[0] = Xform[9];
        Space->WorldPos[1] = Xform[10];
        Space->WorldPos[2] = Xform[11];

        // 0x00518c6c..0x00518d33: the "HpCockpit" hardpoint of the object's instance (slot 0x1c)
        // in parent space; the fallback is (0, 0, -4) and identity.
        long HardpointHandle = 0;
        HardpointInfo Info;
        if (FindHardpoint_PS(WatchedObject(Cam)->get_index(), "HpCockpit", HardpointHandle, Info))
        {
            Cam->HardpointPos[0] = Info.Position.x;
            Cam->HardpointPos[1] = Info.Position.y;
            Cam->HardpointPos[2] = Info.Position.z;
            memcpy(Cam->HardpointOrient, &Info.Orient, 9 * sizeof(float));
        }
        else
        {
            Cam->HardpointPos[0] = 0.0f;
            Cam->HardpointPos[1] = 0.0f;
            Cam->HardpointPos[2] = -4.0f;   // 0xc0800000
            Cam->HardpointOrient[8] = 1.0f;
            Cam->HardpointOrient[4] = 1.0f;
            Cam->HardpointOrient[0] = 1.0f;
            Cam->HardpointOrient[7] = 0.0f;
            Cam->HardpointOrient[6] = 0.0f;
            Cam->HardpointOrient[5] = 0.0f;
            Cam->HardpointOrient[3] = 0.0f;
            Cam->HardpointOrient[2] = 0.0f;
            Cam->HardpointOrient[1] = 0.0f;
        }
    }

    // 0x00518d3b..0x00518d4e: the watcher tested again.
    const unsigned char* const HeldAgain =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    if (HeldAgain == nullptr || HeldAgain - kWatchableOffset == nullptr)
    {
        // 0x00518e00..0x00518e20: Cockpit.cpp line 0x3c9, through the import, unguarded.
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kCockpitCpp, 0x3c9,
              "no object linked to third person camera");
        Cam->bInitialised = 1;
        return;
    }

    // 0x00518d54..0x00518dfd: a reset solve (dt 0) from an identity view, whatever it answers;
    // the view to the world transform, its translation to +0x130, view dirty, initialised.
    float View[12];
    MakeIdentityView(View);
    ThirdPersonCameraSolve(Cam, View, 0.0f, true);
    FSpaceCamera* const Space = SpaceCameraOf((FHudCamera*)Cam);
    memcpy(Space->World, View, 9 * sizeof(float));
    Space->WorldPos[0] = View[9];
    Space->WorldPos[1] = View[10];
    Space->WorldPos[2] = View[11];
    Cam->TranslationCache[0] = View[9];
    Cam->TranslationCache[1] = View[10];
    Cam->TranslationCache[2] = View[11];
    Space->bViewDirty = 1;
    Cam->bInitialised = 1;
}

void ThirdPersonCameraUpdate(FThirdPersonCamera* Cam, float DeltaSeconds)
{
    // LAB_00518e40 (slot 0x6c, thiscall RET 4). The identity view is built on every call.
    float View[12];
    MakeIdentityView(View);

    // 0x00518eb3: skipped for the frame right after an attach.
    if (Cam->bInitialised == 0)
    {
        if (ThirdPersonCameraSolve(Cam, View, DeltaSeconds, false))
        {
            // 0x00518ec6..0x00518ed7: twelve dwords to +0x04, then view dirty.
            FSpaceCamera* const Space = SpaceCameraOf((FHudCamera*)Cam);
            memcpy(Space->World, View, 9 * sizeof(float));
            Space->WorldPos[0] = View[9];
            Space->WorldPos[1] = View[10];
            Space->WorldPos[2] = View[11];
            Space->bViewDirty = 1;
        }
        else
        {
            // 0x00518ee0..0x00518f00: Cockpit.cpp line 0x3e9, through the import, unguarded.
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCockpitCpp, 0x3e9,
                  "third person camera unable to calculate viewer transform, missin linked Ship*");
        }
    }

    // 0x00518f05..0x00518f0e: HUDCamera's update, then the attach latch cleared.
    HudCameraUpdate((FHudCamera*)Cam, DeltaSeconds);
    Cam->bInitialised = 0;
}

bool ThirdPersonCameraSolve(FThirdPersonCamera* Cam, float* Out12, float DeltaSeconds, bool bReset)
{
    // FUN_00518f20 (thiscall RET 0xc). 0x00518f29..0x00518f7e: no client ship -> false.
    if (WatchedShip(Cam) == nullptr)
        return false;

    // 0x00518f84..0x00518fb7: the ship's rotation (slot 0xc) -- the pointer is kept and read
    // again further down -- and a working copy M.
    const float* const ShipRot = &WatchedObject(Cam)->get_transform().Rotation.E[0][0];
    float M[9];
    memcpy(M, ShipRot, sizeof(M));

    // 0x00518fc2..0x00518fde: its position (slot 0).
    const Vector ShipPosRef = WatchedObject(Cam)->get_position();
    float Pos[3] = {ShipPosRef.x, ShipPosRef.y, ShipPosRef.z};

    // 0x00518feb..0x00519024: the speed, |velocity| (slot 4) with (x*x + y*y) + z*z, stored.
    const Vector Velocity = WatchedObject(Cam)->get_velocity();
    float Heading[3] = {Velocity.x, Velocity.y, Velocity.z};
    const double SpeedExact = Vec3Length(Heading);
    const float Speed = static_cast<float>(SpeedExact);

    // The scratch vectors of retail's frame (0x14, 0x24, 0x3c). V14's first value, from the
    // block below, is overwritten on every later path before it is read, as is Heading.
    float V14[3];
    float V24[3];

    // 0x0051902a..0x005190ab: over 1.0f (TEST AH,0x41: not for a NaN), the velocity again
    // (slot 4), normalised (FUN_00410190, FDIVR 1.0f), and the heading replaced by minus the
    // rotation's column 2 (FUN_00422b80 on FCHS'd floats).
    if (SpeedExact > 1.0f)
    {
        const Vector Again = WatchedObject(Cam)->get_velocity();
        V14[0] = Again.x;
        V14[1] = Again.y;
        V14[2] = Again.z;
        const double Inv = 1.0f / Vec3Length(V14);
        V14[0] = static_cast<float>(V14[0] * Inv);
        V14[1] = static_cast<float>(V14[1] * Inv);
        V14[2] = static_cast<float>(V14[2] * Inv);
        const float* const Rot = &WatchedObject(Cam)->get_transform().Rotation.E[0][0];
        Heading[0] = -Rot[2];
        Heading[1] = -Rot[5];
        Heading[2] = -Rot[8];
    }

    // 0x005190b0..0x005190c8: the behaviour manager (null allowed).
    IBehaviorManager* const Manager = GetBehaviorManager(WatchedObject(Cam));

    // 0x005190cc..0x00519190: the reset -- tracker seeded with this speed, level axis (0,0,-1),
    // the orientation history from the object's rotation, up = its column 1, and the manager put
    // in level mode with that up.
    if (bReset)
    {
        Cam->SmoothedSpeed = Speed;
        Cam->PrevSpeed = Speed;
        Cam->SpeedSlack = 0.0f;
        Cam->LevelAxis[0] = 0.0f;
        Cam->LevelAxis[1] = 0.0f;
        Cam->LevelAxis[2] = -1.0f;
        memcpy(Cam->ResetOrient, &WatchedObject(Cam)->get_transform().Rotation.E[0][0],
               9 * sizeof(float));
        GetColumn(V14, Cam->ResetOrient, 1);
        Cam->UpAxis[0] = V14[0];
        Cam->UpAxis[1] = V14[1];
        Cam->UpAxis[2] = V14[2];
        if (Manager != nullptr)
        {
            Manager->set_camera_mode(IBehaviorManager::CameraModeLevel);
            Vector Up;
            Up.x = Cam->ResetOrient[1];
            Up.y = Cam->ResetOrient[4];
            Up.z = Cam->ResetOrient[7];
            Manager->submit_camera_up(Up);
        }
    }

    // 0x00519196..0x00519299: the speed tracker. Speed * look-ahead stays on the FPU until the
    // (updated) slack is added at the end.
    const double LookAheadBase = static_cast<double>(Speed) * Cam->LookAheadMultiplier;
    // 0x005191a0: TEST AH,0x41 -- "not above 1e-5" (and a NaN dt) gives the double 0.0.
    double Accel = 0.0;
    if (DeltaSeconds > kMinDeltaTime)
        Accel = (static_cast<double>(Speed) - Cam->PrevSpeed) / DeltaSeconds;
    // 0x005191cd..0x005191f7: the slack, stored with FST, zeroed when the unrounded sum is below
    // zero (TEST AH,5 / JP).
    const double SlackSum =
        (static_cast<double>(Speed) - Cam->SmoothedSpeed) * DeltaSeconds + Cam->SpeedSlack;
    Cam->SpeedSlack = static_cast<float>(SlackSum);
    if (SlackSum < 0.0f)
        Cam->SpeedSlack = 0.0f;
    // 0x00519201..0x0051921d: the catch-up limit, stored.
    const double Slack = Cam->SpeedSlack;
    const float Limit = static_cast<float>(
        (Slack * Slack * kSlackAccelScale + 1.0f) * Cam->AccelerationLimit);
    // 0x00519223..0x0051923f: ramp when the slack is over 0.01 (double) or the acceleration is
    // above the limit (both TEST AH,0x41 / JZ: strictly greater); otherwise snap.
    bool bRamp = true;
    if (!(static_cast<double>(Cam->SpeedSlack) > kSlackThreshold))
        bRamp = Accel > Limit;
    if (bRamp)
    {
        // 0x0051924f..0x00519293: limit * dt onto the smoothed speed (FST), capped by
        // k*k*speed + speed with k = slack * 0.04f -- the cap stored, the ramp unrounded.
        const double Ramped = static_cast<double>(Limit) * DeltaSeconds + Cam->SmoothedSpeed;
        Cam->SmoothedSpeed = static_cast<float>(Ramped);
        const double K = static_cast<double>(Cam->SpeedSlack) * kSlackCapScale;
        const float Cap = static_cast<float>(K * K * Speed + Speed);
        if (Ramped > Cap)
            Cam->SmoothedSpeed = Cap;
    }
    else
    {
        Cam->SmoothedSpeed = Speed;   // 0x00519241
    }
    // 0x00519299..0x005192d1: the look-ahead, stored.
    const float LookAhead = static_cast<float>(LookAheadBase + Cam->SpeedSlack);

    // 0x0051929f..0x0051933f: the camera offset V78 = hardpoint + ((+0x140 + +0x14c) +
    // (0, 0, look-ahead)), each FUN_00423b30 sum stored, and the last one too.
    float V78[3] = {Cam->HardpointPos[0], Cam->HardpointPos[1], Cam->HardpointPos[2]};
    V24[0] = 0.0f;
    V24[1] = 0.0f;
    V24[2] = LookAhead;
    float Vdc[3];
    Vec3AddStored(Vdc, &Cam->Field140, &Cam->Field14C);
    float Va8[3];
    Vec3AddStored(Va8, Vdc, V24);
    V78[0] = static_cast<float>(static_cast<double>(Va8[0]) + V78[0]);
    V78[1] = static_cast<float>(static_cast<double>(V78[1]) + Va8[1]);
    V78[2] = static_cast<float>(static_cast<double>(V78[2]) + Va8[2]);

    float Mb8[9];
    float Mec[9];
    float M110[9];
    FQuaternion Q48;
    FQuaternion Q64;

    // 0x00519313..0x00519362: auto-level only with +0x13c set, a manager, and the manager's
    // camera-level status.
    if (Cam->bCameraLevelEnabled != 0 && Manager != nullptr && Manager->get_camera_level_status())
    {
        if (Manager->get_user_turning_input_state())
        {
            // 0x0051937e..0x005193a1: the player is steering -- tell the manager the reset
            // orientation's up (column 1).
            Vector Up;
            Up.x = Cam->ResetOrient[1];
            Up.y = Cam->ResetOrient[4];
            Up.z = Cam->ResetOrient[7];
            Manager->submit_camera_up(Up);

            // 0x005193a7..0x005193e7: q(M levelled to +0x204).
            CameraOrientFromForwardAndUp(M110, M, Cam->UpAxis);
            FQuaternion Qa8;
            CockpitMathEngine()->MatrixToQuaternion(Qa8, AsMatrix(M110));

            // 0x005193ea..0x00519446: q(M levelled to the reset up).
            GetColumn(V24, Cam->ResetOrient, 1);
            CameraOrientFromForwardAndUp(M110, M, V24);
            FQuaternion Qdc;
            CockpitMathEngine()->MatrixToQuaternion(Qdc, AsMatrix(M110));

            // 0x00519449..0x0051948a: slerp by dt * +0x200 (stored).
            const float T = static_cast<float>(static_cast<double>(DeltaSeconds) * Cam->TurnRateScale);
            CockpitMathEngine()->QuatNlerp(Q64, Qa8, Qdc, T);

            // 0x0051948d..0x0051951d: back to a matrix; up = its column 1; M = it.
            Q48 = Q64;
            CockpitMathEngine()->QuaternionToMatrix(AsMatrix(Mec), Q48);
            GetColumn(V14, Mec, 1);
            memcpy(M, Mec, sizeof(M));
            Cam->UpAxis[0] = V14[0];
            Cam->UpAxis[1] = V14[1];
            Cam->UpAxis[2] = V14[2];
        }
        else
        {
            // 0x00519522..0x00519527: the ship's up direction.
            const Vector ShipUp = Manager->get_ship_up_direction();
            V14[0] = ShipUp.x;
            V14[1] = ShipUp.y;
            V14[2] = ShipUp.z;

            // 0x0051952d..0x005195c0: in the Ship Gameobj's +0x158 states 2 or 3 (the object is
            // tested but not its class), an up other than exactly (0, 1, 0) -- a NaN counts as
            // other -- is replaced by it and pushed to the manager.
            const unsigned char* const Held =
                reinterpret_cast<const unsigned char*>(Cam->Watched.target());
            if (Held != nullptr && Held - kWatchableOffset != nullptr)
            {
                FClientShipGameobj* const ShipGo = static_cast<FClientShipGameobj*>(
                    dynamic_cast<FClientGameobj*>(reinterpret_cast<IObjInspect*>(
                        const_cast<unsigned char*>(Held - kWatchableOffset))));
                const int State = ShipGo->Field_0x158;
                if ((State == 2 || State == 3) &&
                    (V14[0] != 0.0f || V14[1] != 1.0f || V14[2] != 0.0f))
                {
                    V14[0] = 0.0f;
                    V14[1] = 1.0f;
                    V14[2] = 0.0f;
                    Vector Up;
                    Up.x = 0.0f;
                    Up.y = 1.0f;
                    Up.z = 0.0f;
                    Manager->set_ship_up_direction(Up);
                }
            }

            // 0x005195c6..0x0051963b: M levelled to that up; how far it is from +0x204 (stored),
            // and how far the levelled up is from the ship's own (stored).
            CameraOrientFromForwardAndUp(Mb8, M, V14);
            float Dot = static_cast<float>(
                (static_cast<double>(V14[2]) * Cam->UpAxis[2] +
                 static_cast<double>(V14[1]) * Cam->UpAxis[1]) +
                static_cast<double>(V14[0]) * Cam->UpAxis[0]);
            const float UpAgree = static_cast<float>(
                (static_cast<double>(Mb8[7]) * ShipRot[7] +
                 static_cast<double>(Mb8[4]) * ShipRot[4]) +
                static_cast<double>(Mb8[1]) * ShipRot[1]);

            if (Dot > kLevelTurnMinDot)
            {
                // 0x0051965a..0x005197d3: unless the two ups already agree (|dot| not under
                // 0.9999f), turn from M levelled to +0x204 towards Mb8 at
                // (max(dot, 0) + 1) * +0x200 * dt * 0.5f.
                if (fabs(static_cast<double>(Dot)) < kLevelTurnMaxDot)
                {
                    CameraOrientFromForwardAndUp(M110, M, Cam->UpAxis);
                    FQuaternion Qdc;
                    CockpitMathEngine()->MatrixToQuaternion(Qdc, AsMatrix(Mb8));
                    FQuaternion Q24;
                    CockpitMathEngine()->MatrixToQuaternion(Q24, AsMatrix(M110));
                    if (Dot < 0.0f)
                        Dot = 0.0f;
                    const float T = static_cast<float>(
                        (static_cast<double>(Dot) + 1.0f) * Cam->TurnRateScale * DeltaSeconds * 0.5f);
                    CockpitMathEngine()->QuatNlerp(Q64, Q24, Qdc, T);
                    Q48 = Q64;
                    CockpitMathEngine()->QuaternionToMatrix(AsMatrix(Mec), Q48);
                    float V3c[3];
                    GetColumn(V3c, Mec, 1);
                    memcpy(Mb8, Mec, sizeof(Mb8));
                    V14[0] = V3c[0];
                    V14[1] = V3c[1];
                    V14[2] = V3c[2];
                }

                // 0x005197d7..0x0051982a: +0x204 = V14, M = Mb8, and Mb8's up to the manager.
                Cam->UpAxis[0] = V14[0];
                Cam->UpAxis[1] = V14[1];
                Cam->UpAxis[2] = V14[2];
                Vector Up;
                Up.x = Mb8[1];
                Up.y = Mb8[4];
                Up.z = Mb8[7];
                memcpy(M, Mb8, sizeof(M));
                Manager->submit_camera_up(Up);
            }
            else if (static_cast<double>(UpAgree) > kLevelUpAgreeDot)
            {
                // 0x00519832..0x00519854: almost opposite, but the ship's own up agrees -- adopt
                // it without turning.
                Cam->UpAxis[0] = V14[0];
                Cam->UpAxis[1] = V14[1];
                Cam->UpAxis[2] = V14[2];
            }
        }
    }

    if (bReset)
    {
        // 0x00519862..0x005198c5: the history = M; the offset through M onto the position.
        memcpy(Cam->ResetOrient, M, sizeof(M));
        FVector Offset;
        CockpitMathEngine()->TransformMatrix(Offset, AsMatrix(M), AsVector(V78));
        Pos[0] = static_cast<float>(static_cast<double>(Offset.X) + Pos[0]);
        Pos[1] = static_cast<float>(static_cast<double>(Offset.Y) + Pos[1]);
        Pos[2] = static_cast<float>(static_cast<double>(Offset.Z) + Pos[2]);
    }
    else
    {
        // 0x005198ca..0x005198f0: q(M) before the turn.
        FQuaternion Q154;
        CockpitMathEngine()->MatrixToQuaternion(Q154, AsMatrix(M));

        // 0x005198f3..0x0051993c: the object's axis throttle (IObjInspect slot 0x50, its answer
        // ignored), into M's frame (TransposeTransform). Retail's out buffer is the stack slot
        // that still holds Va8, so a getter that writes nothing leaves Va8 there.
        Vector Throttle;
        Throttle.x = Va8[0];
        Throttle.y = Va8[1];
        Throttle.z = Va8[2];
        WatchedObject(Cam)->get_axis_throttle(Throttle);
        FVector Local;
        CockpitMathEngine()->TransposeTransform(Local, AsMatrix(M),
                                                *reinterpret_cast<FVector const*>(&Throttle));
        V14[0] = Local.X;
        V14[1] = Local.Y;
        V14[2] = Local.Z;

        // 0x00519973..0x005199cb: Mb8 = identity, turned by the throttle.
        for (int Index = 0; Index < 9; ++Index)
            Mb8[Index] = 0.0f;
        Mb8[0] = 1.0f;
        Mb8[4] = 1.0f;
        Mb8[8] = 1.0f;
        if (V14[0] > 0.0f)
        {
            // 0x005199e8..0x00519a77: pitch by -(x * +0x1e0), yaw by -(y * +0x1dc).
            RotatePitchRows(Mb8, -(static_cast<double>(V14[0]) * Cam->VerticalTurnUpAngle));
            RotateYawRows(Mb8, -(static_cast<double>(V14[1]) * Cam->HorizontalTurnAngle));
        }
        else
        {
            // 0x00519a7e..0x00519b18 (not above zero, or NaN): pitch by -(x * +0x1e4); yaw by
            // -(y * +0x1dc) scaled by |x| * 0.8f + 1.0.
            RotatePitchRows(Mb8, -(static_cast<double>(V14[0]) * Cam->VerticalTurnDownAngle));
            const double Scale = fabs(static_cast<double>(V14[0])) * kDownTurnYawScale + 1.0;
            RotateYawRows(Mb8, Scale * -(static_cast<double>(V14[1]) * Cam->HorizontalTurnAngle));
        }

        // 0x00519b1a..0x00519b56: M110 = M * Mb8 (FUN_0051a140), then M = M110.
        CockpitMulMatrix(M110, M, Mb8);
        memcpy(M, M110, sizeof(M));

        // 0x00519b64..0x00519bd3: the level axis through the ship's live rotation, against M's
        // column 1.
        FVector LevelWorld;
        CockpitMathEngine()->TransformMatrix(LevelWorld, AsMatrix(ShipRot), AsVector(Cam->LevelAxis));
        GetColumn(V24, M, 1);
        V14[0] = LevelWorld.X;
        V14[1] = LevelWorld.Y;
        V14[2] = LevelWorld.Z;
        const double AxisDot = Vec3DotZyx(V24, V14);

        // 0x00519bd9..0x00519c8c: unless the two are (anti)parallel, the target frame is
        // {normalise(V14 x V24), normalise(that x V14), -V14} by columns; otherwise M itself.
        if (fabs(AxisDot) < kLevelAxisMaxDot)
        {
            float Right[3];
            Vec3CrossStored(Right, V14, V24);
            Vec3NormaliseInPlace(Right);
            float Up[3];
            Vec3CrossStored(Up, Right, V14);
            Vec3NormaliseInPlace(Up);
            SetColumn(Mec, 0, Right);
            SetColumn(Mec, 1, Up);
            float Back[3] = {-V14[0], -V14[1], -V14[2]};   // FUN_00411350
            SetColumn(Mec, 2, Back);
        }
        else
        {
            memcpy(Mec, M, sizeof(Mec));
        }

        // 0x00519c8e..0x00519ce0: q(target), q(M).
        FQuaternion Q164;
        CockpitMathEngine()->MatrixToQuaternion(Q164, AsMatrix(Mec));
        FQuaternion Q134;
        CockpitMathEngine()->MatrixToQuaternion(Q134, AsMatrix(M));

        // 0x00519ce0..0x00519d7b: the angle between their column 2s -- the cosine stored (FST),
        // clamped to [0, 1] (the low test on the unrounded value, the high on the stored one),
        // acos stored.
        float Back3c[3];
        GetColumn(Back3c, M, 2);
        float Back24[3];
        GetColumn(Back24, Mec, 2);
        const double CosSum =
            (static_cast<double>(Back3c[2]) * Back24[2] + static_cast<double>(Back3c[1]) * Back24[1]) +
            static_cast<double>(Back24[0]) * Back3c[0];
        float Cos = static_cast<float>(CosSum);
        if (CosSum < 0.0)
            Cos = 0.0f;
        else if (static_cast<double>(Cos) > 1.0)
            Cos = 1.0f;
        const float Angle = static_cast<float>(acos(static_cast<double>(Cos)));

        // 0x00519d7f..0x00519e0a: the turn rate W = angle / dt, its growth capped by +0x1d4; the
        // slerp fraction dt / angle * W (1.0f for an angle not above 1e-4), capped by +0x1d8 --
        // which then also recomputes W from the stored 1/dt. W is stored in +0x1ac.
        const double InvDt = 1.0f / static_cast<double>(DeltaSeconds);
        const float InvDtStored = static_cast<float>(InvDt);
        double Rate = InvDt * Angle;
        if (Rate - Cam->PrevAngularRate > Cam->AngularAcceleration)
            Rate = static_cast<double>(Cam->AngularAcceleration) + Cam->PrevAngularRate;
        float Fraction = 1.0f;
        if (static_cast<double>(Angle) > kMinTurnAngle)
            Fraction = static_cast<float>(static_cast<double>(DeltaSeconds) / Angle * Rate);
        if (Fraction > Cam->AngularSlerpMultiplier)
        {
            Fraction = Cam->AngularSlerpMultiplier;
            Rate = static_cast<double>(InvDtStored) * Fraction * Angle;
        }
        Cam->PrevAngularRate = static_cast<float>(Rate);

        // 0x00519e10..0x00519ea0: M = slerp(q(target), q(M), fraction).
        CockpitMathEngine()->QuatNlerp(Q64, Q164, Q134, Fraction);
        Q48 = Q64;
        CockpitMathEngine()->QuaternionToMatrix(AsMatrix(M110), Q48);
        memcpy(M, M110, sizeof(M));

        // 0x00519ea2..0x00519edf: the offset through M (FUN_00423b90, the Win3D engine cache
        // DAT_006679d8) onto the position.
        FVector Offset;
        Win3DTransformByMatrix(Offset, AsMatrix(M), AsVector(V78));
        Pos[0] = static_cast<float>(static_cast<double>(Offset.X) + Pos[0]);
        Pos[1] = static_cast<float>(static_cast<double>(Pos[1]) + Offset.Y);
        Pos[2] = static_cast<float>(static_cast<double>(Pos[2]) + Offset.Z);

        // 0x00519ee3..0x00519f5c: the new level axis = -(ship rotation^T * M's column 2).
        GetColumn(V24, M, 2);
        FVector BackLocal;
        CockpitMathEngine()->TransposeTransform(BackLocal, AsMatrix(ShipRot), AsVector(V24));
        Cam->LevelAxis[0] = -BackLocal.X;
        Cam->LevelAxis[1] = -BackLocal.Y;
        Cam->LevelAxis[2] = -BackLocal.Z;

        // 0x00519f5f..0x0051a035: the history = M, then M = slerp(q(M), q(before), +0x1e8).
        memcpy(Cam->ResetOrient, M, sizeof(M));
        FQuaternion Q144;
        CockpitMathEngine()->MatrixToQuaternion(Q144, AsMatrix(M));
        const float LookAheadNlerp = Cam->TurnLookAheadSlerpAmount;
        CockpitMathEngine()->QuatNlerp(Q64, Q144, Q154, LookAheadNlerp);
        Q48 = Q64;
        CockpitMathEngine()->QuaternionToMatrix(AsMatrix(M110), Q48);
        memcpy(M, M110, sizeof(M));
    }

    // 0x0051a037..0x0051a099: +0x130 = position, +0x1ec = this speed, the shake (FUN_0051dbd0),
    // and {M, +0x130} out.
    Cam->TranslationCache[0] = Pos[0];
    Cam->TranslationCache[1] = Pos[1];
    Cam->TranslationCache[2] = Pos[2];
    Cam->PrevSpeed = Speed;
    HudCameraApplyShake((FHudCamera*)Cam, M);
    memcpy(Out12, M, 9 * sizeof(float));
    Out12[9] = Cam->TranslationCache[0];
    Out12[10] = Cam->TranslationCache[1];
    Out12[11] = Cam->TranslationCache[2];
    return true;
}

bool ThirdPersonCameraLoad(FThirdPersonCamera* Cam, const char* CockpitIniPath)
{
    // FUN_005187d0, in retail's order. Each FDUMP goes through the import with the raw path;
    // retail tests neither.
    bool bOk = true;

    INI_Reader Reader;
    if (!Reader.open(CockpitIniPath, false))
    {
        // Client\space\Cockpit.cpp:0x347
        FDUMP(0x100002, "%s(%d) : *** WARNING: third person cockpit file not found '%s'\n",
              kCockpitCpp, 0x347, CockpitIniPath);
        bOk = false;
    }

    // Retail walks every header purely to complain about one of them. No key is ever read
    // from [ThirdPerson] -- the section is dead weight kept only so old cockpit files still
    // parse, and the camera's real parameters come from the ship archetype instead (slot
    // 0x58). Reproduced because the warning is the whole point of the loop.
    while (Reader.read_header())
    {
        if (Reader.is_header("ThirdPerson"))
        {
            // Client\space\Cockpit.cpp:0x34e
            FDUMP(0x100002, "%s(%d) : *** WARNING: [ThirdPerson] is obsolete in file %s\n",
                  kCockpitCpp, 0x34e, CockpitIniPath);
        }
    }

    // The aspect sanity check: the tangents of the two half-angles must be in 4:3.
    // Retail reads them through the vtable (slots 0x18 and 0x1c) rather than the fields, so a
    // derived class that overrode either getter would be checked on its own numbers.
    ICamera* const Space = AsCamera(SpaceCameraOf((FHudCamera*)Cam));

    const double TanX = tan(static_cast<double>(Space->GetHalfFovXDeg() * kDegreesToRadians));
    const double TanY = tan(static_cast<double>(Space->GetFovYDeg() * kDegreesToRadians));
    const double Aspect = TanX / TanY;

    if (fabs(Aspect - kExpectedAspect) >= kAspectTolerance)
    {
        // Client\space\Cockpit.cpp:0x35e
        FDUMP(0x100002, "%s(%d) : *** WARNING: cockpit aspect (%f) is not PC-compatible?\n",
              kCockpitCpp, 0x35e, Aspect);
        bOk = false;
    }

    return bOk;
}

char const* FThirdPersonCamera::GetName()
{
    return ThirdPersonCameraGetName();
}

void FThirdPersonCamera::Attach(void* Object)
{
    ThirdPersonCameraAttach(this, Object);
}

bool FThirdPersonCamera::Activate(int const* ViewRect4)
{
    return ThirdPersonCameraActivate(this, ViewRect4);
}

void FThirdPersonCamera::Update(float DeltaSeconds)
{
    ThirdPersonCameraUpdate(this, DeltaSeconds);
}

bool FThirdPersonCamera::OnEvent(unsigned MsgId)
{
    return ThirdPersonCameraOnEvent(this, MsgId);
}

void FThirdPersonCamera::Deactivate()
{
    HudCameraDerivedDeactivate(this);
}

void FThirdPersonCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    SpaceCameraRenderSlot78(this, LayerFlags, Param2);
}
