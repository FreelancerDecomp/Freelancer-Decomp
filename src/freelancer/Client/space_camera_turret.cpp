// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail turret camera (in-flight controller + 0x3c8, vtable .rdata 0x005dbcc4).
//
// Ported in full: the constructor (0x0051a210), the name slot (0x0051a2a0), the ini loader
// (0x0051a2b0), the Event::Handler face (0x0051ac20), and its own slots 0x58 (0x0051a6a0),
// 0x64 (0x0051a640) and 0x6c (0x0051a730) with the view solver they share (FUN_0051a7d0).

#include "space_camera_turret.h"
#include "space_scene_obj_draw.h"

#include "space_camera_first.h"   // G_FirstPersonMathEngine (DAT_00675220)

#include "common_cobject.h"
#include "common_csys.h"          // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"         // IObjRW
#include "common_ini.h"
#include "dacom.h"
#include "globals.h"              // GetClientFrameTimeCapped (FUN_0042d680), G_RenderSurface*
#include "hud_command_dispatch.h" // HudCommandRecord (FUN_00576240)
#include "hud_target.h"           // HudIsMousePickBlocked (FUN_0045a740)
#include "input_manager.h"        // GetClientMouseButtons, GetClientSoftwareCursor
#include "player_behavior.h"      // IsMouseFlightLocked (FUN_004c7c80)
#include "x86math.h"

#include <cstddef>
#include <math.h>
#include <string.h>

namespace
{
    const char kCockpitCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Cockpit.cpp";


    // The face's slot 0 is called __thiscall (face in ECX, id on the stack); __fastcall with an
    // unused EDX has the same shape.


    // .rdata 0x005dbcb8 (installed at 0x0051a283): one slot, FUN_0051ac20.

    // FUN_0051a7d0's constants.
    const float kTurretYawWrap = 6.2831854820251465f;        // .rdata 0x005da774 (2*pi)
    const float kTurretPitchLimit = 1.0471975803375244f;     // .rdata 0x005dbe10 (pi/3)
    const float kTurretPitchLimitNeg = -1.0471975803375244f; // .rdata 0x005dbe0c
    const float kTurretMouseAimDelay = 0.25f;                // .rdata 0x005c94f8

    // The identity 3x4 both callers of FUN_0051a7d0 build on the stack first.
    void MakeIdentityView(float* View12)
    {
        for (int Index = 0; Index < 12; ++Index)
            View12[Index] = 0.0f;
        View12[0] = 1.0f;
        View12[4] = 1.0f;
        View12[8] = 1.0f;
    }

    // {orientation, position} -- twelve dwords -- into the camera's world transform (+0x04).
    void StoreWorldView(FTurretCamera* Cam, const float* View12)
    {
        FSpaceCamera* const Space = SpaceCameraOf((FHudCamera*)Cam);
        memcpy(Space->World, View12, 9 * sizeof(float));
        Space->WorldPos[0] = View12[9];
        Space->WorldPos[1] = View12[10];
        Space->WorldPos[2] = View12[11];
    }


}

void TurretCameraConstruct(FTurretCamera* Cam)
{
    // FUN_0051a210, in retail's own order: Z of the tether goes in first, then X and Y.
    HudCameraConstruct((FHudCamera*)Cam);

    Cam->Tether[2] = 25.0f;      // 0x41c80000
    Cam->Tether[0] = 0.0f;
    Cam->Tether[1] = 5.0f;       // 0x40a00000

    Cam->Field10C = 0.0f;
    Cam->Field110 = 0.0f;
    Cam->Field114 = 0.0f;
    Cam->Field118 = 0.0f;
    Cam->Field11C = 0.0f;
    Cam->Field120 = 0.0f;

    Cam->Field124 = 0.75f;       // 0x3f400000 -- nothing in the ini can move this
    Cam->YawRotateSpeed = 2.0f;  // 0x40000000
    Cam->PitchRotateSpeed = 1.5f;// 0x3fc00000
    Cam->AccelSpeed = 5.0f;      // 0x0051a277: ECX, still 0x40a00000 from 0x0051a22a

    // +0x134 (MouseAimTime) is not written by the retail ctor: the controller lives in zeroed
    // static storage (G_SpaceApp), which is where the port's camera lives too.

}

bool TurretCameraActivate(FTurretCamera* Cam, const int* ViewRect4)
{
    // FUN_0051a640 (thiscall, RET 4): the five turret ids {0x32..0x36} from a stack table, each
    // through FUN_005760d0 on the face at +0xf8, then HUDCamera's FUN_0051cad0 -- its answer.
    static const unsigned kTurretEvents[5] = {0x32, 0x33, 0x34, 0x35, 0x36};
    FHudCmdHandler* const Face = Cam->AsHudCmdHandler();
    for (unsigned Index = 0; Index < 5; ++Index)
        HudRegisterCommandHandler(Face, kTurretEvents[Index]);
    return HudCameraActivate((FHudCamera*)Cam, ViewRect4);
}

bool TurretCameraComputeView(FTurretCamera* Cam, float* Out12, float DeltaSeconds, bool)
{
    // FUN_0051a7d0 (thiscall, RET 0xc). The third argument (1 from the attach, 0 from the
    // update) is never read.
    FHudCamera* const Hud = (FHudCamera*)Cam;

    // 0x0051a7d6..0x0051a82b: the watcher at +0xc0 and the object (watcher - 8), read twice,
    // then its +0xc'd pointer's +0x4 and the ship mask 0x503 -- any miss answers true.
    {
        const unsigned char* const Held =
            reinterpret_cast<const unsigned char*>(Hud->Watched.target());
        if (Held == nullptr || Held - 8 == nullptr)
            return true;
        const unsigned char* const Plus0c = Held - 8 + 0xc;
        if (Plus0c == nullptr)
            return true;
        CEqObj* const EqObj = *reinterpret_cast<CEqObj* const*>(Plus0c + 4);
        if (EqObj == nullptr ||
            (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) != 0x503u)
            return true;
    }

    // 0x0051a831..0x0051a84a: the ship's transform (slot 0xc); its rotation is read through this
    // pointer below.
    IObjRW* const Object = static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Hud));
    const float* const ShipRot = &Object->get_transform().Rotation.E[0][0];

    // 0x0051a84c..0x0051a943: this frame's yaw (stack float) and pitch (kept on the FPU).
    float Yaw = 0.0f;
    double Pitch = 0.0;
    bool bSteer = true;
    if ((GetClientMouseButtons() & 1u) != 1u && !IsMouseFlightLocked())
    {
        // Keys: the clock resets; each non-zero key rate (TEST AH,0x44 / JNP skips only an
        // ordered zero) times its speed and dt.
        Cam->MouseAimTime = 0.0f;
        if (Cam->Field11C != 0.0f)
            Yaw = static_cast<float>(static_cast<double>(Cam->YawRotateSpeed) * Cam->Field11C *
                                     DeltaSeconds);
        if (Cam->Field120 != 0.0f)
            Pitch = static_cast<double>(Cam->PitchRotateSpeed) * Cam->Field120 * DeltaSeconds;
    }
    else
    {
        // 0x0051a8d1..0x0051a8ed: left button held or mouse flight locked -- the clock runs
        // (FUN_0042d680 + +0x134, stored as a float, compared unrounded); under 0.25s nothing
        // steers yet (TEST AH,1 / JNZ: below or unordered skips to the easing at 0x0051a9fa).
        const double Clock = GetClientFrameTimeCapped() + Cam->MouseAimTime;
        Cam->MouseAimTime = static_cast<float>(Clock);
        if (!(Clock >= kTurretMouseAimDelay))
        {
            bSteer = false;
        }
        else
        {
            // 0x0051a8f3..0x0051a93f: the cursor's offset from the surface centre
            // (DAT_00610850 / DAT_00610854 halves), normalised, times speed and dt.
            const double HalfW = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) * 0.5f;
            const double HalfH = static_cast<double>(static_cast<int>(G_RenderSurfaceHeight)) * 0.5f;
            int CursorX = 0;
            int CursorY = 0;
            int CursorZ = 0;
            GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
            Yaw = static_cast<float>((static_cast<double>(CursorX) - HalfW) / HalfW *
                                     Cam->YawRotateSpeed * DeltaSeconds);
            Pitch = (static_cast<double>(CursorY) - HalfH) / HalfH * Cam->PitchRotateSpeed *
                    DeltaSeconds;
        }
    }

    // 0x0051a947..0x0051a9f8: unless both are an ordered zero, move the targets.
    if (bSteer && (Yaw != 0.0f || Pitch != 0.0))
    {
        // Yaw target (+0x110), wrapped into [0, 2*pi] by moving the current yaw (+0x10c) along.
        const double YawTarget = static_cast<double>(Yaw) + Cam->Field110;
        Cam->Field110 = static_cast<float>(YawTarget);
        if (YawTarget > kTurretYawWrap)
        {
            Cam->Field110 = static_cast<float>(YawTarget - kTurretYawWrap);
            Cam->Field10C = static_cast<float>(static_cast<double>(Cam->Field10C) - kTurretYawWrap);
        }
        else if (YawTarget < 0.0)
        {
            Cam->Field110 = static_cast<float>(YawTarget + kTurretYawWrap);
            Cam->Field10C = static_cast<float>(static_cast<double>(Cam->Field10C) + kTurretYawWrap);
        }

        // Pitch target (+0x118), clamped to +/- pi/3 (0x3f860a92 / 0xbf860a92).
        const double PitchTarget = Pitch + Cam->Field118;
        Cam->Field118 = static_cast<float>(PitchTarget);
        if (PitchTarget > kTurretPitchLimit)
            Cam->Field118 = kTurretPitchLimit;
        else if (PitchTarget < kTurretPitchLimitNeg)
            Cam->Field118 = kTurretPitchLimitNeg;
    }

    // 0x0051a9fa..0x0051aa7b: ease the current angles towards the targets by dt * accel_speed;
    // over 1.0f they snap.
    {
        const double Ease = static_cast<double>(DeltaSeconds) * Cam->AccelSpeed;
        if (Ease > 1.0f)
        {
            Cam->Field10C = Cam->Field110;
            Cam->Field114 = Cam->Field118;
        }
        else
        {
            Cam->Field10C = static_cast<float>(
                (static_cast<double>(Cam->Field110) - Cam->Field10C) * Ease + Cam->Field10C);
            Cam->Field114 = static_cast<float>(
                (static_cast<double>(Cam->Field118) - Cam->Field114) * Ease + Cam->Field114);
        }
    }

    // 0x0051aa7d..0x0051aacc: the ship's rotation turned by -yaw (columns 0 and 2; cos stored
    // as a float, sin kept on the FPU).
    float M[9];
    memcpy(M, ShipRot, sizeof(M));
    {
        const double Angle = -static_cast<double>(Cam->Field10C);
        const float CosA = static_cast<float>(cos(Angle));
        const double SinA = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double A = M[Row * 3 + 0];
            const double C = M[Row * 3 + 2];
            M[Row * 3 + 0] = static_cast<float>(static_cast<double>(CosA) * A - SinA * C);
            M[Row * 3 + 2] = static_cast<float>(A * SinA + static_cast<double>(CosA) * C);
        }
    }
    // 0x0051aace..0x0051ab19: then by -pitch (columns 1 and 2).
    {
        const double Angle = -static_cast<double>(Cam->Field114);
        const float CosB = static_cast<float>(cos(Angle));
        const double SinB = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double B = M[Row * 3 + 1];
            const double C = M[Row * 3 + 2];
            M[Row * 3 + 1] = static_cast<float>(static_cast<double>(CosB) * B + SinB * C);
            M[Row * 3 + 2] = static_cast<float>(static_cast<double>(CosB) * C - B * SinB);
        }
    }

    // 0x0051ab1b..0x0051ab44: FUN_00516fe0 into the stack block at +0x54 with the ship's up
    // column (rotation +0x04 / +0x10 / +0x1c). That block is overwritten by the orientation copy
    // at 0x0051abc1 before anything reads it, so the result is dead in retail too.
    {
        const float Up[3] = {ShipRot[1], ShipRot[4], ShipRot[7]};
        float Discarded[9];
        CameraOrientFromForwardAndUp(Discarded, M, Up);
    }

    // 0x0051ab47..0x0051ab4e: FUN_0051dbd0 shakes it.
    HudCameraApplyShake(Hud, M);

    // 0x0051ab53..0x0051ab7a: DAT_00675220 (FUN_005b73e0 on first use), vt+0x2c -- the tether
    // (+0x100) through the orientation.
    if (G_FirstPersonMathEngine == nullptr)
        G_FirstPersonMathEngine = Acquire3DMathEngine();
    FVector Offset;
    G_FirstPersonMathEngine->TransformMatrix(Offset, *reinterpret_cast<FMatrix const*>(M),
                                             *reinterpret_cast<FVector const*>(Cam->Tether));

    // 0x0051ab7d..0x0051abd5: the watcher re-read, the object's position (slot 0) added.
    IObjRW* const Again = static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Hud));
    Vector const& ShipPos = Again->get_position();

    // 0x0051abd9..0x0051ac04: {orientation, position} out; 0x0051ac09 answers false.
    memcpy(Out12, M, 9 * sizeof(float));
    Out12[9] = Offset.X + ShipPos.x;
    Out12[10] = Offset.Y + ShipPos.y;
    Out12[11] = Offset.Z + ShipPos.z;
    return false;
}

void TurretCameraAttach(FTurretCamera* Cam, void* Object)
{
    // FUN_0051a6a0 (slot 0x58, thiscall RET 4): the four angles zeroed (+0x10c, +0x114, +0x110,
    // +0x118, in that order), HUDCamera's attach (FUN_0051cbb0), FUN_0051a7d0(&view, 0, 1) on an
    // identity 3x4 whatever it answers, view dirty, view copied to the world transform.
    Cam->Field10C = 0.0f;
    Cam->Field114 = 0.0f;
    Cam->Field110 = 0.0f;
    Cam->Field118 = 0.0f;
    HudCameraAttach((FHudCamera*)Cam, Object);

    float View[12];
    MakeIdentityView(View);
    TurretCameraComputeView(Cam, View, 0.0f, true);
    SpaceCameraOf((FHudCamera*)Cam)->bViewDirty = 1;   // 0x0051a716
    StoreWorldView(Cam, View);
}

void TurretCameraUpdate(FTurretCamera* Cam, float DeltaSeconds)
{
    // FUN_0051a730 (slot 0x6c, thiscall RET 4): FUN_0051a7d0(&view, dt, 0) on an identity 3x4,
    // copied to the world transform, view dirty, then HUDCamera's update (FUN_0051d070).
    float View[12];
    MakeIdentityView(View);
    TurretCameraComputeView(Cam, View, DeltaSeconds, false);
    StoreWorldView(Cam, View);
    SpaceCameraOf((FHudCamera*)Cam)->bViewDirty = 1;   // 0x0051a7bb
    HudCameraUpdate((FHudCamera*)Cam, DeltaSeconds);
}

bool TurretCameraOnEvent(FTurretCamera* Cam, unsigned MsgId)
{
    // FUN_0051ac20 (thiscall on the face, RET 4). Blocked mouse pick (FUN_0045a740) -> false;
    // HUDCamera's FUN_0051d7f0 taking it -> true.
    if (HudIsMousePickBlocked())
        return false;
    if (HudCameraOnEvent((FHudCamera*)Cam, MsgId))
        return true;

    // 0x0051ac4b..0x0051ac5a: ids 0x32..0x36 through a five-entry table; anything else -> false.
    // The key-down byte of the record decides between +/-Field124 (+0x124) and 0.
    const bool bDown = HudCommandRecord()->Byte18 != 0;
    switch (MsgId)
    {
    case 0x32:
        Cam->Field11C = bDown ? -Cam->Field124 : 0.0f;   // 0x0051ac61, face + 0x24
        return true;
    case 0x33:
        Cam->Field11C = bDown ? Cam->Field124 : 0.0f;    // 0x0051ac81
        return true;
    case 0x34:
        Cam->Field120 = bDown ? -Cam->Field124 : 0.0f;   // 0x0051ac9f, face + 0x28
        return true;
    case 0x35:
        Cam->Field120 = bDown ? Cam->Field124 : 0.0f;    // 0x0051acbf
        return true;
    case 0x36:
        // 0x0051acdd: face + 0x14, + 0x1c, + 0x18, + 0x20.
        Cam->Field10C = 0.0f;
        Cam->Field114 = 0.0f;
        Cam->Field110 = 0.0f;
        Cam->Field118 = 0.0f;
        return true;
    default:
        return false;
    }
}

const char* TurretCameraGetName()
{
    return "Turret";             // 0x0051a2a0 -> .rdata 0x005dbd44
}

bool TurretCameraLoad(FTurretCamera* Cam, const char* CockpitIniPath)
{
    // FUN_0051a2b0. Same shape as the rear-view loader, four keys instead of one.
    bool bOk = true;

    INI_Reader Reader;
    if (!Reader.open(CockpitIniPath, false))
    {
        // 0x0051a301..0x0051a320: FDUMP through the import with the raw path, no tests.
        FDUMP(0x100002, "%s(%d) : *** WARNING: turret camera file not found '%s'\n",
              kCockpitCpp, 0x5a0, CockpitIniPath);
        bOk = false;
    }

    while (Reader.read_header())
    {
        // [Turret] is obsolete in exactly the same way [ThirdPerson] and [RearView] are: the
        // loader recognises it only to warn, and reads nothing out of it. All three warnings
        // come from the same source file, Client\space\Cockpit.cpp.
        if (Reader.is_header("Turret"))
        {
            // 0x0051a353..: the same unguarded call.
            FDUMP(0x100002, "%s(%d) : *** WARNING: [Turret] is obsolete in file %s\n",
                  kCockpitCpp, 0x5a7, CockpitIniPath);
            continue;
        }

        if (!Reader.is_header("TurretCamera"))
            continue;

        while (Reader.read_value())
        {
            // 0x0051a390: the four keys are tested in this order, and the chain stops at the
            // first match -- so a line that somehow matched two would take the first.
            if (Reader.is_value("tether"))
            {
                // Retail pulls the components 1, 0, 2 (FPU stack juggling) and stores 0, 1, 2.
                const float X = Reader.get_value_float(0);
                const float Y = Reader.get_value_float(1);
                const float Z = Reader.get_value_float(2);
                Cam->Tether[0] = X;
                Cam->Tether[1] = Y;
                Cam->Tether[2] = Z;
            }
            else if (Reader.is_value("yaw_rotate_speed"))
            {
                Cam->YawRotateSpeed = Reader.get_value_float(0);
            }
            else if (Reader.is_value("pitch_rotate_speed"))
            {
                Cam->PitchRotateSpeed = Reader.get_value_float(0);
            }
            else if (Reader.is_value("accel_speed"))
            {
                Cam->AccelSpeed = Reader.get_value_float(0);
            }
        }
    }

    return bOk;
}

char const* FTurretCamera::GetName()
{
    return TurretCameraGetName();
}

void FTurretCamera::Attach(void* Object)
{
    TurretCameraAttach(this, Object);
}

bool FTurretCamera::Activate(int const* ViewRect4)
{
    return TurretCameraActivate(this, ViewRect4);
}

void FTurretCamera::Update(float DeltaSeconds)
{
    TurretCameraUpdate(this, DeltaSeconds);
}

bool FTurretCamera::OnEvent(unsigned MsgId)
{
    return TurretCameraOnEvent(this, MsgId);
}

void FTurretCamera::Deactivate()
{
    HudCameraDerivedDeactivate(this);
}

void FTurretCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    SpaceCameraRenderSlot78(this, LayerFlags, Param2);
}
