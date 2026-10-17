// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail first-person (cockpit) camera (in-flight controller + 0x00c, vtable .rdata 0x005db944).
//
// Ported: the constructor (0x00517180), the name slot (0x00517330) and the ini loader
// (0x005173c0) -- the [Cockpit] walk, the head_turn globals, the obsolete-[Camera] warning, the
// cockpit mesh load on the holder at +0x190 (FUN_00420dd0 / FUN_00420fd0 / FUN_00421070), the
// camera-part search (CompoundInstanceAnalyze with FUN_00517130, then IEngine slots 0x98 and
// 0x90 into +0x13c and +0x130) and the closing aspect check. This replaced the cockpit_load.cpp
// twin of FUN_00420dd0 (2026-09-25). Later the same day: the attach (LAB_005178e0), the
// Event::Handler face (FUN_005183e0), the mesh release (FUN_00518410) and the per-frame update
// (LAB_00517ba0, slot 0x6c).

#include "space_camera_first.h"

#include "DALib.h"
#include "common_cobject.h"   // CompoundInstanceAnalyze
#include "common_csys.h"      // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"     // FindHardpoint_PS, HardpointInfo
#include "common_iface.h"     // IObjRW / IObjInspect (thiscall getters)
#include "common_ini.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "client_gameobj.h"   // FClientShipGameobj (+0x158 / +0x188, slot 0x18c)
#include "engine_light.h"     // EngineLightClearRegisteredList / EngineLightSetAmbientRgbBytes
#include "game_options.h"     // GetGameOption (FUN_005792b0)
#include "globals.h"          // CopyBoundedString (FUN_00417c50)
#include "jump_effect.h"      // FJumpShipEffect::Ambient (FUN_00504870)
#include "player_behavior.h"  // GetPlayerObjRW (FUN_0054baf0)
#include "hud_target.h"       // HudIsMousePickBlocked (FUN_0045a740)
#include "perf_options.h"     // FindPerfOption (FUN_005b36b0)
#include "space_scene_obj_draw.h" // SpaceObjDrawPass (FUN_0051c200), FUN_0051c970 / FUN_0051c9e0
#include "win3d_render.h"     // ClearWin3DBuffers (FUN_004215e0)
#include "x86math.h"

#include <cstddef>

#include <math.h>
#include <string.h>

// .data ships 30.0f / 25.0f (0x41f00000 / 0x41c80000). The [Cockpit] head_turn loader
// (0x00517545 / 0x0051755a) is the only code that touches either; nothing in the PE reads them.
float G_CockpitHeadTurnX = 30.0f;   // DAT_006135a0
float G_CockpitHeadTurnY = 25.0f;   // DAT_006135a4

// DAT_00675220: this file's own lazily-filled math engine cache (FUN_005b73e0 at 0x00517a26,
// 0x00517aa4, 0x00517aec and in the per-frame slot).
I3DMathEngine* G_FirstPersonMathEngine = nullptr;

namespace
{
    const float  kDegreesToRadians = 0.01745329238474369f;   // .rdata 0x005c77f4
    const double kExpectedAspect = 1.333;
    const double kAspectTolerance = 0.05;

    // FUN_00417c50's capacity at both call sites in this loader (0x005174d1, 0x00517515).
    const unsigned kIniStringCapacity = 0x104;

    const char kCockpitCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Cockpit.cpp";


    // LAB_00517ba0's head-motion state and tuning. The two state floats live in .bss; the four
    // tuning values are .data, each read only by that update.
    float G_CockpitLastForwardSpeed = 0.0f;   // DAT_00675228
    float G_CockpitHeadLag = 0.0f;            // DAT_0067522c
    float G_CockpitLagScale = 0.3f;           // DAT_006135a8 (0x3e99999a)
    float G_CockpitHeadPivot = 1.5f;          // DAT_006135ac
    float G_CockpitRollPerSpin = -4.0f;       // DAT_006135b8
    float G_CockpitRollLimit = 4.0f;          // DAT_006135bc

    // The face's slot 0 is called __thiscall (face in ECX, id on the stack); __fastcall with an
    // unused EDX has the same shape.


    // .rdata 0x005db93c: one slot, FUN_005183e0.

    // .rdata 0x005db994: FirstPerson's slot 0x50 is IDPMsgHandler::OnDisconnect (0x00401e90,
    // a bare RET), not GameCamera's FUN_0051c8b0 -- its scene slot runs the two list walks
    // itself around the cockpit draw.

    I3DMathEngine* FirstPersonMathEngine()
    {
        if (G_FirstPersonMathEngine == nullptr)
            G_FirstPersonMathEngine = Acquire3DMathEngine();
        return G_FirstPersonMathEngine;
    }

    FMatrix const& AsMatrix(const float* Nine)
    {
        return *reinterpret_cast<FMatrix const*>(Nine);
    }

    FVector const& AsVector(const float* Three)
    {
        return *reinterpret_cast<FVector const*>(Three);
    }

    // FUN_00517130 -- the CompoundInstanceAnalyze callback. Asks the engine for the part's name
    // (IEngine slot 0x68, GetInstanceName, into a stack FEngineName), treats a null name as ""
    // (.rdata 0x005c865c), and records the part in the context when the name contains "cam"
    // (.rdata 0x005db934). No early out: a later match overwrites an earlier one.
    void __cdecl FindCockpitCameraPart(long Instance, void* Context)
    {
        FEngineName Name;
        const char* Text =
            DALib::Engine->GetInstanceName(&Name, reinterpret_cast<void*>(Instance))->Text;
        if (Text == nullptr)
            Text = "";
        if (strstr(Text, "cam") != nullptr)
            *static_cast<long*>(Context) = Instance;
    }

    void SetIdentity(float* M)
    {
        for (int i = 0; i < 9; ++i)
            M[i] = 0.0f;
        M[0] = 1.0f;
        M[4] = 1.0f;
        M[8] = 1.0f;
    }
}

void FirstPersonCameraConstruct(FFirstPersonCamera* Cam)
{
    // FUN_00517180.
    HudCameraConstruct((FHudCamera*)Cam);

    // 0x005171a7: FUN_00420d40 constructs the cockpit mesh holder in place at +0x190.
    MeshHolderConstruct(&Cam->Mesh);

    // 0x005171bc: the face table 0x005db93c, one slot FUN_005183e0.

    // 0x005171c6: three transforms, each a zero position followed by an identity matrix. Retail
    // writes them out of order -- the matrices' off-diagonals first, then the three 1.0f -- but
    // the result is exactly this.
    Cam->PositionA[0] = 0.0f;
    Cam->PositionA[1] = 0.0f;
    Cam->PositionA[2] = 0.0f;
    SetIdentity(Cam->OrientationA);

    Cam->HeadPosition[0] = 0.0f;
    Cam->HeadPosition[1] = 0.0f;
    Cam->HeadPosition[2] = 0.0f;
    SetIdentity(Cam->HeadOrientation);

    Cam->PositionC[0] = 0.0f;
    Cam->PositionC[1] = 0.0f;
    Cam->PositionC[2] = 0.0f;
    SetIdentity(Cam->OrientationC);

    // 0x0051729f: this class clears BOTH of the HUDCamera bytes at +0xfc and +0xfd -- the only
    // evidence that +0xfd is a field at all rather than padding. It leaves +0xfe at the 1 the
    // HUDCamera ctor put there.
    Cam->bFlagFC = 0;
    Cam->bFlagFD = 0;

    Cam->bFlag1A0 = 1;          // 0x005172b7
    Cam->Pad1A1[0] = 0;
    Cam->Pad1A1[1] = 0;
    Cam->Pad1A1[2] = 0;
}

const char* FirstPersonCameraGetName()
{
    return "FirstPerson";       // 0x00517330 -> .rdata
}

bool FirstPersonCameraOnEvent(FFirstPersonCamera* Cam, unsigned MsgId)
{
    // FUN_005183e0 (thiscall on the face, RET 4): FUN_0045a740 -> false; otherwise HUDCamera's
    // FUN_0051d7f0 on the same face, its answer normalised (SETNZ).
    if (HudIsMousePickBlocked())
        return false;
    return HudCameraOnEvent((FHudCamera*)Cam, MsgId) != false;
}

void FirstPersonCameraReleaseMesh(FFirstPersonCamera* Cam)
{
    // FUN_00518410: ADD ECX,0x190 / JMP FUN_00420f70.
    ClientMeshHolderRelease(&Cam->Mesh);
}

void FirstPersonCameraAttach(FFirstPersonCamera* Cam, void* Object)
{
    // LAB_005178e0 (slot 0x58, __thiscall, RET 4).
    // 0x005178f1: the HUDCamera attach first (watcher swap + HUD retarget).
    HudCameraAttach((FHudCamera*)Cam, Object);

    FSpaceCamera* const Space = SpaceCameraOf((FHudCamera*)Cam);

    IObjRW* const Watched = static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Cam));
    if (Watched != nullptr)
    {
        IObjInspect* const Inspect = Watched;

        // 0x00517924: slot 4 (get_orientation), whose body is folded with slot 3 and hands back
        // the whole transform -- twelve dwords go into the camera's world rotation + position.
        Transform const& ObjectXform = Inspect->get_transform();
        Space->bViewDirty = 1;                                             // 0x00517929
        memcpy(Space->World, &ObjectXform, 12 * sizeof(float));

        // 0x00517954: FindHardpoint_PS(get_index(), "HpCockpit", handle, info).
        long Handle = 0;
        HardpointInfo Info;
        if (FindHardpoint_PS(Inspect->get_index(), "HpCockpit", Handle, Info))
        {
            Cam->PositionA[0] = Info.Position.x;                           // +0x100
            Cam->PositionA[1] = Info.Position.y;
            Cam->PositionA[2] = Info.Position.z;
            memcpy(Cam->OrientationA, &Info.Orient, 9 * sizeof(float));   // +0x10c
        }
        else
        {
            // 0x00517997: no cockpit hardpoint -- four units behind the origin, identity.
            Cam->PositionA[0] = 0.0f;
            Cam->PositionA[1] = 0.0f;
            Cam->PositionA[2] = -4.0f;                                     // 0xc0800000
            Cam->OrientationA[8] = 1.0f;
            Cam->OrientationA[4] = 1.0f;
            Cam->OrientationA[0] = 1.0f;
            Cam->OrientationA[7] = 0.0f;
            Cam->OrientationA[6] = 0.0f;
            Cam->OrientationA[5] = 0.0f;
            Cam->OrientationA[3] = 0.0f;
            Cam->OrientationA[2] = 0.0f;
            Cam->OrientationA[1] = 0.0f;
        }
    }

    // 0x00517a04: the watcher is tested again.
    IObjRW* const Again = static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Cam));
    if (Again != nullptr)
    {
        I3DMathEngine* const Math = FirstPersonMathEngine();

        // 0x00517a43: MulMatrix(stack, world rotation, hardpoint orientation) -- the head's
        // base orientation in world space; kept in a second stack copy.
        FMatrix Product;
        Math->MulMatrix(Product, AsMatrix(Space->World), AsMatrix(Cam->OrientationA));
        FMatrix const Head = Product;

        // 0x00517a6b: slot 0, get_position.
        Vector const& ObjectPosition = static_cast<IObjInspect*>(Again)->get_position();
        float Position[3] = {ObjectPosition.x, ObjectPosition.y, ObjectPosition.z};

        // 0x00517abc: + world rotation × hardpoint position (TransformMatrix, vt+0x2c).
        FVector Offset;
        FirstPersonMathEngine()->TransformMatrix(Offset, AsMatrix(Space->World),
                                                 AsVector(Cam->PositionA));
        Position[0] = Offset.X + Position[0];
        Position[1] = Offset.Y + Position[1];
        Position[2] = Offset.Z + Position[2];

        // 0x00517b0d: + head orientation × the cockpit mesh's camera-part position (+0x130).
        FirstPersonMathEngine()->TransformMatrix(Offset, Head, AsVector(Cam->HeadPosition));
        Position[0] = Offset.X + Position[0];
        Position[1] = Offset.Y + Position[1];
        Position[2] = Offset.Z + Position[2];

        // 0x00517b28: +0x160 keeps the eye point; the view is marked dirty; the camera's world
        // transform becomes {head orientation, eye point}.
        Cam->PositionC[0] = Position[0];
        Cam->PositionC[1] = Position[1];
        Cam->PositionC[2] = Position[2];
        Space->bViewDirty = 1;
        memcpy(Space->World, &Head, 9 * sizeof(float));
        Space->WorldPos[0] = Position[0];
        Space->WorldPos[1] = Position[1];
        Space->WorldPos[2] = Position[2];
    }

    Cam->bFlag1A0 = 1;                                                     // 0x00517b85
}

void FirstPersonCameraUpdate(FFirstPersonCamera* Cam, float DeltaSeconds)
{
    // LAB_00517ba0 (slot 0x6c, thiscall RET 4).
    FHudCamera* const Hud = (FHudCamera*)Cam;
    FSpaceCamera* const Space = SpaceCameraOf(Hud);

    // 0x00517ba9..0x00517be5: only with a watched client ship -- watcher - 8, its +0xc'd
    // pointer's +0x4, CObject mask 0x503.
    bool bShip = false;
    {
        unsigned char* const Held = const_cast<unsigned char*>(
            reinterpret_cast<const unsigned char*>(Hud->Watched.target()));
        if (Held != nullptr)
        {
            unsigned char* const Object = Held - 8;
            if (Object != nullptr)
            {
                unsigned char* const Plus0c = Object + 0xc;
                if (Plus0c != nullptr)
                {
                    CEqObj* const EqObj = *reinterpret_cast<CEqObj**>(Plus0c + 4);
                    if (EqObj != nullptr &&
                        (static_cast<unsigned>(EqObj->ObjectClass) & 0x503u) == 0x503u)
                        bShip = true;
                }
            }
        }
    }

    if (bShip)
    {
        IObjRW* const Object = static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Hud));

        // 0x00517c01: the ship's transform (slot 0xc); its rotation is read through this pointer
        // for the rest of the function.
        Transform const& ShipXform = Object->get_transform();
        FMatrix const& ShipRot = AsMatrix(&ShipXform.Rotation.E[0][0]);

        // 0x00517c30: ship rotation x cockpit hardpoint orientation (+0x10c), copied to the
        // frame's mesh orientation.
        FMatrix CockpitRot;
        FirstPersonMathEngine()->MulMatrix(CockpitRot, ShipRot, AsMatrix(Cam->OrientationA));
        FMatrix MeshRot = CockpitRot;

        // 0x00517c59: slot 0, the ship's position.
        Vector const& ShipPos = Object->get_position();
        float Eye[3] = {ShipPos.x, ShipPos.y, ShipPos.z};

        // 0x00517c83..0x00517c9e: on the first update after an attach (+0x1a0), the ship's
        // behavior manager goes back to the normal camera mode.
        IBehaviorManager* const Manager = GetBehaviorManager(Object);
        if (Cam->bFlag1A0 != 0 && Manager != nullptr)
            Manager->set_camera_mode(IBehaviorManager::CameraModeNormal);

        // 0x00517cc7..0x00517d10: + ship rotation x cockpit hardpoint position (+0x100); the
        // point is kept in +0x160.
        FVector Offset;
        FirstPersonMathEngine()->TransformMatrix(Offset, ShipRot, AsVector(Cam->PositionA));
        Eye[0] = Offset.X + Eye[0];
        Cam->PositionC[0] = Eye[0];
        Eye[1] = Offset.Y + Eye[1];
        Cam->PositionC[1] = Eye[1];
        Eye[2] = Offset.Z + Eye[2];
        Cam->PositionC[2] = Eye[2];

        // 0x00517d15..0x00517d2b: the cockpit mesh follows (FUN_00420fd0 / FUN_00421010 /
        // FUN_00421070).
        ClientMeshHolderSetPosition(&Cam->Mesh, Eye);
        ClientMeshHolderSetOrientation(&Cam->Mesh, &MeshRot.D[0][0]);
        ClientMeshHolderUpdate(&Cam->Mesh);

        // 0x00517d41..0x00517d71: the velocity (slot 1) into ship space (vt+0x38).
        const Vector Velocity = Object->get_velocity();
        FVector LocalVelocity;
        FirstPersonMathEngine()->TransposeTransform(LocalVelocity, ShipRot,
                                                    AsVector(&Velocity.x));

        // 0x00517d74..0x00517da1: forward acceleration, 0 unless dt > 0.
        float Accel = 0.0f;
        if (DeltaSeconds > 0.0)
        {
            Accel = static_cast<float>(
                (static_cast<double>(LocalVelocity.Z) - G_CockpitLastForwardSpeed) / DeltaSeconds);
        }

        // 0x00517da5..0x00517de5: the lag the head wants, (1 - 1/(|a| + 1)) * 0.3f with a's
        // sign, less the current lag, times 0.01; the forward speed is remembered.
        double Step = (1.0 - 1.0 / (fabs(static_cast<double>(Accel)) + 1.0)) * G_CockpitLagScale;
        if (Accel < 0.0f)
            Step = -Step;
        Step = Step - G_CockpitHeadLag;
        G_CockpitLastForwardSpeed = LocalVelocity.Z;
        Step = Step * 0.01;

        // 0x00517deb..0x00517e32: at most 3 * dt either way, then added to the lag.
        const float Limit = static_cast<float>(DeltaSeconds * 3.0);
        const float NegLimit = -Limit;
        if (Step < NegLimit)
            Step = NegLimit;
        else if (Step > Limit)
            Step = Limit;
        G_CockpitHeadLag = static_cast<float>(Step + G_CockpitHeadLag);
        const float Lag = G_CockpitHeadLag;

        // 0x00517e38..0x00517e52: the mesh's orientation back from the engine.
        memcpy(&MeshRot, ClientMeshHolderGetOrientation(&Cam->Mesh), sizeof(MeshRot));

        // 0x00517e77..0x00517ec7: the rigid eye -- mesh position + mesh rotation x the camera
        // part's position (+0x130), FUN_00423b30.
        FVector HeadOffset;
        FirstPersonMathEngine()->TransformMatrix(HeadOffset, MeshRot, AsVector(Cam->HeadPosition));
        {
            float const* const MeshPos = ClientMeshHolderGetPosition(&Cam->Mesh);
            Eye[0] = MeshPos[0] + HeadOffset.X;
            Eye[1] = MeshPos[1] + HeadOffset.Y;
            Eye[2] = MeshPos[2] + HeadOffset.Z;
        }
        // 0x00517ecb..0x00517f11: an identity written to the stack matrix the roll overwrites
        // before any read -- dead in retail too.

        // 0x00517eb9..0x00517f88: a function-local static (guard DAT_00675210 bit 0, empty atexit
        // destructor) takes the angular velocity (slot 2) and then its ship-space version.
        static Vector LocalSpin = {0.0f, 0.0f, 0.0f};   // DAT_00675204..DAT_0067520c
        LocalSpin = Object->get_angular_velocity();
        FVector Spin;
        FirstPersonMathEngine()->TransposeTransform(Spin, ShipRot, AsVector(&LocalSpin.x));
        LocalSpin.x = Spin.X;
        LocalSpin.y = Spin.Y;
        LocalSpin.z = Spin.Z;

        // 0x00517fb0..0x00518032: roll = spin.y * DAT_006135b8, clamped to +/-DAT_006135bc,
        // turned into a matrix by RotateMatrix((0, 0, roll)).
        double Roll = static_cast<double>(LocalSpin.y) * G_CockpitRollPerSpin;
        const float NegRollLimit = -G_CockpitRollLimit;
        if (Roll < NegRollLimit)
            Roll = NegRollLimit;
        else if (Roll > G_CockpitRollLimit)
            Roll = G_CockpitRollLimit;
        Vector RollAngles;
        RollAngles.z = static_cast<float>(Roll);
        RollAngles.x = 0.0f;
        RollAngles.y = 0.0f;
        const Matrix RollMatrix = RotateMatrix(RollAngles);

        // 0x00518032..0x005180b6: the head pivot (0, DAT_006135ac, 0) through the camera part's
        // orientation (+0x13c), taken off the part's position (+0x130).
        Vector Pivot;
        Pivot.x = 0.0f;
        Pivot.y = G_CockpitHeadPivot;
        Pivot.z = 0.0f;
        FVector PivotOffset;
        FirstPersonMathEngine()->TransformMatrix(PivotOffset, AsMatrix(Cam->HeadOrientation),
                                                 AsVector(&Pivot.x));
        float Base[3];
        Base[0] = Cam->HeadPosition[0] - PivotOffset.X;
        Base[1] = Cam->HeadPosition[1] - PivotOffset.Y;
        Base[2] = Cam->HeadPosition[2] - PivotOffset.Z;

        // 0x005180bb..0x00518135: the pivot pushed back by the lag, rolled, put back on the base.
        Vector Swing;
        Swing.x = Pivot.x;
        Swing.y = Pivot.y;
        Swing.z = static_cast<float>(static_cast<double>(Pivot.z) + -static_cast<double>(Lag));
        FVector Swung;
        FirstPersonMathEngine()->TransformMatrix(Swung, AsMatrix(&RollMatrix.E[0][0]),
                                                 AsVector(&Swing.x));
        float Local[3];
        Local[0] = Swung.X + Base[0];
        Local[1] = Swung.Y + Base[1];
        Local[2] = Swung.Z + Base[2];

        // 0x0051814d..0x005181d0: into the mesh's frame, onto the mesh position -> +0x160.
        FVector Moved;
        FirstPersonMathEngine()->TransformMatrix(Moved, MeshRot, AsVector(Local));
        {
            float const* const MeshPos = ClientMeshHolderGetPosition(&Cam->Mesh);
            Cam->PositionC[0] = Moved.X + MeshPos[0];
            Cam->PositionC[1] = Moved.Y + MeshPos[1];
            Cam->PositionC[2] = Moved.Z + MeshPos[2];
        }

        // 0x005181cb..0x005181e9: the "cockpit" perf option (FUN_005b36b0, applied value +0x14,
        // no null test) under 0.999 keeps the head rigid; otherwise the lag and roll stay.
        FMatrix Final;
        if (FindPerfOption("cockpit")->Applied < 0.999)
        {
            // 0x005181ef..0x00518274: the eye back to the rigid point; mesh x part (+0x13c) x
            // +0x16c.
            Cam->PositionC[0] = Eye[0];
            Cam->PositionC[1] = Eye[1];
            Cam->PositionC[2] = Eye[2];
            FMatrix Head;
            FirstPersonMathEngine()->MulMatrix(Head, MeshRot, AsMatrix(Cam->HeadOrientation));
            const FMatrix HeadCopy = Head;
            FirstPersonMathEngine()->MulMatrix(Final, HeadCopy, AsMatrix(Cam->OrientationC));
        }
        else
        {
            // 0x00518280..0x00518332: the same product, then x the roll.
            FMatrix Head;
            FirstPersonMathEngine()->MulMatrix(Head, MeshRot, AsMatrix(Cam->HeadOrientation));
            const FMatrix HeadCopy = Head;
            FMatrix Offsetted;
            FirstPersonMathEngine()->MulMatrix(Offsetted, HeadCopy, AsMatrix(Cam->OrientationC));
            const FMatrix OffsettedCopy = Offsetted;
            FirstPersonMathEngine()->MulMatrix(Final, OffsettedCopy, AsMatrix(&RollMatrix.E[0][0]));
        }

        // 0x00518339..0x00518351: FUN_0051dbd0 shakes the orientation.
        HudCameraApplyShake(Hud, &Final.D[0][0]);

        // 0x00518356..0x0051839c: world = {orientation, +0x160}, view dirty.
        memcpy(Space->World, &Final, 9 * sizeof(float));
        Space->WorldPos[0] = Cam->PositionC[0];
        Space->WorldPos[1] = Cam->PositionC[1];
        Space->WorldPos[2] = Cam->PositionC[2];
        Space->bViewDirty = 1;
    }

    // 0x005183a1..0x005183b0: HUDCamera's update, then the attach flag drops.
    HudCameraUpdate(Hud, DeltaSeconds);
    Cam->bFlag1A0 = 0;
}

void FirstPersonCameraSceneDraw(FFirstPersonCamera* Cam, unsigned LayerFlags, unsigned Param2)
{
    // LAB_00518420 (slot 0x78, thiscall RET 8).
    FHudCamera* const Hud = (FHudCamera*)Cam;
    FSpaceCamera* const Space = SpaceCameraOf(Hud);

    // 0x00518426..0x00518439: the watcher at +0xc0 set, and the object (watcher - 8) non-null.
    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Hud->Watched.target());
    if (Held == nullptr)
        return;
    if (Held - 8 == nullptr)
        return;

    // 0x0051843f..0x0051844a: FUN_0051c200(LayerFlags, Param2).
    SpaceObjDrawPass(Space, LayerFlags, Param2);

    // 0x0051844f..0x00518463: the "cockpit" perf option's applied value (+0x14, no null test),
    // kept in the first argument's stack slot for the second compare below.
    const float Applied = FindPerfOption("cockpit")->Applied;

    // 0x00518463..0x00518488: FCOMP 0.999 / TEST AH,0x41 / JZ -- strictly greater (ordered)
    // clears depth outright; otherwise depth is cleared only with the HUD option (0xa) on.
    if (static_cast<double>(Applied) > 0.999 || GetGameOption(kGameOptHud) != 0)
        ClearWin3DBuffers(kWin3DClearDepth);

    // 0x0051848b: FUN_0051c970.
    SpaceEnvFxBeginCockpitPass(Space);

    // 0x00518492..0x005184a1: the same compare, JNZ past the cockpit when not strictly greater.
    if (static_cast<double>(Applied) > 0.999)
    {
        // 0x005184a7..0x005184d6 -- the cockpit mesh pass. The camera's near / far (slots 0x20
        // / 0x24) are saved, FUN_0040fb60(0.05f, 500.0f) (.rdata 0x005c8914 / 0x005c8918), and
        // FUN_00422950(this).
        ICamera* const Camera = AsCamera(Space);
        const float SavedNear = Camera->GetZNear();
        const float SavedFar = Camera->GetZFar();
        SpaceCameraSetNearFar(Space, 0.05f, 500.0f);
        Win3DApplyCamera(Camera);

        // 0x005184d9..0x005184f2: the player (FUN_0054baf0) in the jump arm (+0x158 == 2 or 3,
        // read without a class test) -- FUN_004101e0, then the jump effect's ambient
        // (FUN_00504870 = its +0x1c) through FUN_00410240 when the effect and the ambient are
        // both set.
        FClientShipGameobj* const Ship =
            static_cast<FClientShipGameobj*>(static_cast<IObjRW*>(GetPlayerObjRW()));
        if (Ship != nullptr && (Ship->Field_0x158 == 2 || Ship->Field_0x158 == 3))
        {
            EngineLightClearRegisteredList();
            FJumpShipEffect* const JumpFx = static_cast<FJumpShipEffect*>(Ship->Field_0x188);
            if (JumpFx != nullptr && JumpFx->Ambient != nullptr)
                EngineLightSetAmbientRgbBytes(JumpFx->Ambient);
        }
        else
        {
            // 0x00518517..0x00518579: the cockpit mesh's world bounding sphere (FUN_00421090),
            // the flag 1 unless FUN_004f70c0 is below 0.999 (FCOMP double DAT_005c8b68,
            // TEST AH,1: C0 -> 0), the ship's slot 0x18c draw state (OutB zeroed first), then
            // FUN_005554d0(centre, radius, this, A, B, flag). A missing player reaches the slot
            // call through null, as in retail.
            float Center[3];
            const float Radius = ClientMeshHolderGetBoundingSphere(&Cam->Mesh, Center);
            unsigned char Flag = 1;
            if (SpaceEffectPerfOneMinus() < 0.999)
                Flag = 0;
            unsigned DrawStateB = 0;
            const unsigned DrawStateA = Ship->Slot18c(&DrawStateB);
            SpaceApplyObjectLighting(Center, Radius, Space, DrawStateA, DrawStateB, Flag);
        }

        // 0x0051857c..0x005185d2: depth range [0.75, 1] (Pipeline slot 23), the cockpit mesh
        // (FUN_00421120 with flags 0x100) when the holder has an instance (FUN_00420d60),
        // depth range [0, 0.75], and the saved near / far back through FUN_0040fb60.
        DALib::Pipeline->SetDepthRange(0.75f, 1.0f);
        if (ClientMeshHolderHasInstance(&Cam->Mesh))
            ClientMeshHolderRender(&Cam->Mesh, Space, 0x100);
        DALib::Pipeline->SetDepthRange(0.0f, 0.75f);
        SpaceCameraSetNearFar(Space, SavedNear, SavedFar);
    }

    // 0x005185d8..0x005185e0: HUDCamera's slot 0x78 body, called directly.
    HudCameraDraw((FGameCamera*)Hud);

    // 0x005185e5: FUN_0051c9e0.
    SpaceEnvFxEndCockpitPass(Space);
}

void FirstPersonCameraLoad(FFirstPersonCamera* Cam, const char* CockpitIniPath)
{
    // FUN_005173c0 (thiscall, the path), in retail's order. Returns nothing.

    // [ESP+0x28]/[ESP+0x2c] and [ESP+0x130]/[ESP+0x134]: two length-prefixed 0x104-byte
    // strings, both {0, ""} before the reader opens (0x00517419..0x00517425).
    unsigned MeshLength = 0;
    char MeshName[kIniStringCapacity];
    MeshName[0] = 0;
    unsigned MaterialLibraryLength = 0;
    char MaterialLibrary[kIniStringCapacity];
    MaterialLibrary[0] = 0;

    INI_Reader Reader;
    if (!Reader.open(CockpitIniPath, false))
    {
        // Client\space\Cockpit.cpp:0xe4 -- a warning only; the rest of the loader still runs.
        FDUMP(0x100002, "%s(%d) : *** WARNING: cockpit file not found '%s'\n",
              kCockpitCpp, 0xe4, CockpitIniPath);
    }

    while (Reader.read_header())
    {
        if (Reader.is_header("Cockpit"))
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("mesh"))
                {
                    // 0x005174b9: a null value clears the string; otherwise FUN_00417c50 copies
                    // at most 0x103 characters and its return (the length) is stored.
                    const char* const Value = Reader.get_value_string();
                    if (Value == nullptr)
                    {
                        MeshLength = 0;
                        MeshName[0] = 0;
                    }
                    else
                    {
                        MeshLength = CopyBoundedString(MeshName, kIniStringCapacity, Value);
                    }
                }
                else if (Reader.is_value("material_library"))
                {
                    // 0x005174f7: same shape, into the second 0x104-byte string.
                    const char* const Value = Reader.get_value_string();
                    if (Value == nullptr)
                    {
                        MaterialLibraryLength = 0;
                        MaterialLibrary[0] = 0;
                    }
                    else
                    {
                        MaterialLibraryLength =
                            CopyBoundedString(MaterialLibrary, kIniStringCapacity, Value);
                    }
                }
                else if (Reader.is_value("head_turn"))
                {
                    // 0x0051752c: the only two values in the whole cockpit file that land in
                    // globals rather than on an object.
                    G_CockpitHeadTurnX = Reader.get_value_float(0);
                    G_CockpitHeadTurnY = Reader.get_value_float(1);
                }
            }
            continue;
        }

        if (Reader.is_header("Camera"))
        {
            // Client\space\Cockpit.cpp:0xf9
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: header [Camera] is no longer supported, found in file (%s)\n",
                  kCockpitCpp, 0xf9, CockpitIniPath);
        }
    }
    (void)MaterialLibraryLength;

    // 0x005175c7: CMP [ESP+0x28],0 -- the mesh is only loaded when its stored LENGTH is
    // non-zero (an empty `mesh =` loads nothing), and the material-library buffer is passed
    // alongside it whether or not a value was given -- an empty string, never null.
    if (MeshLength != 0)
        ClientMeshHolderLoad(&Cam->Mesh, MeshName, MaterialLibrary);

    // 0x005175e5: the instance is placed at the origin and updated, unconditionally.
    const float Origin[3] = {0.0f, 0.0f, 0.0f};
    ClientMeshHolderSetPosition(&Cam->Mesh, Origin);    // FUN_00420fd0
    ClientMeshHolderUpdate(&Cam->Mesh);                 // FUN_00421070

    // 0x0051762f: the compound is walked with FUN_00517130 to find the part whose name
    // contains "cam"; only when one was found does the head transform come out of the engine
    // -- nine floats from IEngine slot 0x98 into +0x13c and three from slot 0x90 into +0x130.
    long CameraPart = -1;
    CompoundInstanceAnalyze(reinterpret_cast<long>(Cam->Mesh.EngineInstance),
                            &FindCockpitCameraPart, &CameraPart);
    if (CameraPart != -1)
    {
        void* const Part = reinterpret_cast<void*>(CameraPart);
        memcpy(Cam->HeadOrientation, DALib::Engine->GetInstanceOrientation(Part),
               9 * sizeof(float));
        const float* const Position = DALib::Engine->GetInstancePosition(Part);
        Cam->HeadPosition[0] = Position[0];
        Cam->HeadPosition[1] = Position[1];
        Cam->HeadPosition[2] = Position[2];
    }

    // 0x00517689: the closing 4:3 check, read through the vtable exactly as the third-person
    // loader reads it.
    ICamera* const Space = AsCamera(SpaceCameraOf((FHudCamera*)Cam));

    // Each slot's return is multiplied by the float 0.0174532924f (DAT_005c77f4) on the x87
    // stack, FPTAN; the first tangent is parked as a double, the second divides it (FDIVR).
    const double TanX = tan(static_cast<double>(Space->GetHalfFovXDeg()) *
                            static_cast<double>(kDegreesToRadians));
    const double TanY = tan(static_cast<double>(Space->GetFovYDeg()) *
                            static_cast<double>(kDegreesToRadians));
    const double Aspect = TanX / TanY;

    // 0x005176bb..0x005176cc: |Aspect - 1.333| FCOMP 0.05, TEST AH,1 / JNZ -> skip. C0 is set
    // for "less" AND for unordered, so the warning is given only for an ordered >= (a NaN
    // ratio stays silent).
    if (fabs(Aspect - kExpectedAspect) >= kAspectTolerance)
    {
        // Client\space\Cockpit.cpp:0x113
        FDUMP(0x100002, "%s(%d) : *** WARNING: cockpit aspect (%f) is not PC-compatible?\n",
              kCockpitCpp, 0x113, Aspect);
    }
}

char const* FFirstPersonCamera::GetName()
{
    return FirstPersonCameraGetName();
}

void FFirstPersonCamera::ScaleEnvironmentEffects()
{
}

void FFirstPersonCamera::Attach(void* Object)
{
    FirstPersonCameraAttach(this, Object);
}

void FFirstPersonCamera::Update(float DeltaSeconds)
{
    FirstPersonCameraUpdate(this, DeltaSeconds);
}

void FFirstPersonCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    FirstPersonCameraSceneDraw(this, LayerFlags, Param2);
}

bool FFirstPersonCamera::OnEvent(unsigned MsgId)
{
    return FirstPersonCameraOnEvent(this, MsgId);
}

void FFirstPersonCamera::Deactivate()
{
    HudCameraDerivedDeactivate(this);
}
