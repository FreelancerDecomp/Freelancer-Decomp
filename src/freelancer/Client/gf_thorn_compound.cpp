// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_thorn_compound.h"

#include "DALib.h"
#include "gf_script_object.h"
#include "room_control.h"
#include "x86math.h"

#include <cstdint>
#include <cstring>

namespace
{
    char const* const GScriptCompoundFile =
        "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptCompound.cpp";

    void* const kCompoundNoAnimScript = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    unsigned long const kCompoundNoPlayback = 0xFFFFFFFFu;

    // DAT_0066d11c -- this file's 3DMathEngine, acquired (FUN_005b73e0) on first use by each of
    // the eight bodies that read it (0x00451515 .. 0x00451d17; nothing else references it).
    I3DMathEngine* G_ThornCompoundMath = nullptr;

    I3DMathEngine* ThornCompoundMath()
    {
        if (G_ThornCompoundMath == nullptr)
            G_ThornCompoundMath = Acquire3DMathEngine();
        return G_ThornCompoundMath;
    }

    void* InstancePointer(long Instance)
    {
        return reinterpret_cast<void*>(static_cast<intptr_t>(Instance));
    }

    FVector const& AsMathVector(Vector const& Value)
    {
        return *reinterpret_cast<FVector const*>(&Value);
    }

    FMatrix const& AsMathMatrix(Matrix const& Value)
    {
        return *reinterpret_cast<FMatrix const*>(&Value);
    }

    Matrix const& AsMatrix(FMatrix const& Value)
    {
        return *reinterpret_cast<Matrix const*>(&Value);
    }

    // The transposed copy LAB_004517b0 / LAB_00451930 / LAB_00451a00 build with nine FLD / FSTP
    // pairs: row r of the result is column r of Source.
    Matrix Transposed(Matrix const& Source)
    {
        Matrix Out;
        int Row = 0;
        for (; Row < 3; ++Row)
        {
            int Col = 0;
            for (; Col < 3; ++Col)
                Out.E[Row][Col] = Source.E[Col][Row];
        }
        return Out;
    }

    // The script-frame view of the instance, shared by LAB_004517b0 / LAB_00451930 /
    // LAB_00451a00: R = to_frame(instance orientation) * Offset.Orient^T, and the position
    // to_frame(instance position) - R * Offset.Pos.
    void LocalFromInstance(FThornCompound const& Self, Vector& OutPos, Matrix& OutOrient)
    {
        void* const Instance = InstancePointer(Self.Instance);
        Vector const LocalPos = Self.Frame.trans_to_frame(
            *reinterpret_cast<Vector const*>(DALib::Engine->GetInstancePosition(Instance)));
        Matrix const LocalOrient = Self.Frame.trans_to_frame(
            *reinterpret_cast<Matrix const*>(DALib::Engine->GetInstanceOrientation(Instance)));
        Matrix const OffsetTransposed = Transposed(Self.Offset.Orient);

        FMatrix Product;
        ThornCompoundMath()->MulMatrix(Product, AsMathMatrix(LocalOrient),
                                       AsMathMatrix(OffsetTransposed));
        FVector Rotated;
        ThornCompoundMath()->TransformMatrix(Rotated, Product, AsMathVector(Self.Offset.Pos));

        OutPos.x = LocalPos.x - Rotated.X;
        OutPos.y = LocalPos.y - Rotated.Y;
        OutPos.z = LocalPos.z - Rotated.Z;
        OutOrient = AsMatrix(Product);
    }

    // Local.Orient * Offset.Pos + Local.Pos (LAB_004514f0 / LAB_00451660).
    Vector InstancePosFromLocal(FThornCompound const& Self)
    {
        FVector Rotated;
        ThornCompoundMath()->TransformMatrix(Rotated, AsMathMatrix(Self.Local.Orient),
                                             AsMathVector(Self.Offset.Pos));
        Vector World;
        World.x = Rotated.X + Self.Local.Pos.x;
        World.y = Rotated.Y + Self.Local.Pos.y;
        World.z = Rotated.Z + Self.Local.Pos.z;
        return World;
    }

    // Local.Orient * Offset.Orient (LAB_004515b0 / LAB_00451660).
    Matrix InstanceOrientFromLocal(FThornCompound const& Self)
    {
        FMatrix Product;
        ThornCompoundMath()->MulMatrix(Product, AsMathMatrix(Self.Local.Orient),
                                       AsMathMatrix(Self.Offset.Orient));
        return AsMatrix(Product);
    }
}

// FUN_004510f0. Base ctor, the summary's ctor, then +0x6c 0, +0x70 / +0x74 / +0x78 -1, 0xc dwords
// of Offset, Local identity (0x004511a3..0x004511c8), the sixteen slots {-1, -1, 0}
// (0x004511e4 loop), and HardpointSummary::clear. +0x7c is not written. (0x004511d0..0x004511d8
// also zero a stack vector nothing reads.)
FThornCompound::FThornCompound(Csys const& ScriptFrame, Csys const& InOffset, FGfScript* InOwner,
                               FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Object(nullptr),
      Instance(-1),
      AnimScript(kCompoundNoAnimScript),
      Category(-1),
      Offset(InOffset)
{
    Local.Pos.x = 0.0f;
    Local.Pos.y = 0.0f;
    Local.Pos.z = 0.0f;
    MatrixSetIdentity(Local.Orient);
    int Slot = 0;
    for (; Slot < kMotionSlots; ++Slot)
    {
        Playback[Slot] = kCompoundNoPlayback;
        EventId[Slot] = -1;
        bPlaying[Slot] = 0;
    }
    Hardpoints.clear();
}

// FUN_00451240: ~HardpointSummary, then FUN_0044fd80.
FThornCompound::~FThornCompound()
{
}

// FUN_004513b0 (stdcall facet, RET 8). No AnimScript -> 0. The first idle slot takes the
// descriptor's unique id and is marked playing; IAnimation2 slot 9 plays the animation on the
// instance and slot 11 evaluates it (event flags, start_time, time_scale, trans_time, weight,
// trans_scale, 0) unless the playback id is -1. Always 0.
//
// RETAIL DEFECT KEPT: with all sixteen slots busy the slot index stays -1 (0x004513c8 OR EBX,-1)
// and the three stores go to [-1] -- EventId[-1] is Playback[15] (+0x11c), bPlaying[-1] the top
// byte of EventId[15] (+0x15f), Playback[-1] Local.Orient's last element (+0xdc).
GENRESULT DACOM_API FThornCompound::StartMotion(void* DescArg)
{
    FThornMotionEventDesc const* Desc = static_cast<FThornMotionEventDesc const*>(DescArg);
    if (AnimScript == kCompoundNoAnimScript)
        return GR_OK;

    int Slot = -1;
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        if (bPlaying[Index] == 0)
        {
            Slot = Index;
            break;
        }
    }

    EventId[Slot] = Desc->Common.UniqueId;
    bPlaying[Slot] = 1;
    unsigned long const PlaybackId = DALib::Anim->PlayScript(
        AnimScript, InstancePointer(Instance), Desc->Animation, nullptr, nullptr);
    Playback[Slot] = PlaybackId;
    if (PlaybackId != kCompoundNoPlayback)
    {
        DALib::Anim->EvalPlayback(PlaybackId, static_cast<unsigned long>(Desc->Common.EventFlags),
                                  Desc->StartTime, Desc->TimeScale,
                                  AsOpaqueDword(Desc->TransTime), AsOpaqueDword(Desc->Weight),
                                  AsOpaqueDword(Desc->TransScale), nullptr);
    }
    return GR_OK;
}

// FUN_00451450 (stdcall facet, RET 8). No AnimScript -> 0. The playing slot with the event's
// unique id (index -1 when none: retail then tests bPlaying[-1], the top byte of EventId[15]) is
// stopped (IAnimation2 slot 14), its playback destroyed (slot 10) and reset. Always 0.
GENRESULT DACOM_API FThornCompound::StopMotion(void* DescArg)
{
    FThornMotionEventDesc const* Desc = static_cast<FThornMotionEventDesc const*>(DescArg);
    if (AnimScript == kCompoundNoAnimScript)
        return GR_OK;

    int Slot = -1;
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        if (bPlaying[Index] != 0 && EventId[Index] == Desc->Common.UniqueId)
        {
            Slot = Index;
            break;
        }
    }
    if (bPlaying[Slot] == 0)
        return GR_OK;

    DALib::Anim->StopPlayback(Playback[Slot]);
    DALib::Anim->DestroyScript(reinterpret_cast<void*>(static_cast<uintptr_t>(Playback[Slot])));
    Playback[Slot] = kCompoundNoPlayback;
    EventId[Slot] = -1;
    bPlaying[Slot] = 0;
    return GR_OK;
}

// FUN_00451e50 (stdcall facet, RET 8): FUN_0044fdd0(+0x1c, +0x2c, +0x20, +0x30), then the
// Owner's FUN_0044f9a0(+0x1c, 1). Always 0.
GENRESULT DACOM_API FThornCompound::ConnectHardpoints(void* DescArg)
{
    FThornConnectEventDesc const* Desc = static_cast<FThornConnectEventDesc const*>(DescArg);
    ConnectEntityHardpoints(Desc->Common.PrimaryIndex, Desc->Hardpoint,
                            Desc->Common.SecondaryIndex, Desc->ParentHardpoint);
    Owner->SetEntityPropDetached(Desc->Common.PrimaryIndex, 1);
    return GR_OK;
}

// FUN_00451e90: the disconnect twin, FUN_0044ff30 and 0.
GENRESULT DACOM_API FThornCompound::DisconnectHardpoints(void* DescArg)
{
    FThornConnectEventDesc const* Desc = static_cast<FThornConnectEventDesc const*>(DescArg);
    DisconnectEntityHardpoints(Desc->Common.PrimaryIndex, Desc->Hardpoint,
                               Desc->Common.SecondaryIndex, Desc->ParentHardpoint);
    Owner->SetEntityPropDetached(Desc->Common.PrimaryIndex, 0);
    return GR_OK;
}

// LAB_00451b90 (stdcall, RET 0x10). World-space answer (no script-frame conversion):
//   0 ROOT, or 2 PART named "root" / "" -> the instance's position / orientation, -1 without one;
//   2 with any other name -> "sub_part ... not written" ERROR, 0 (the Petal-bug WARNING branch
//     needs an empty name, which the second stricmp has already taken);
//   1 HARDPOINT -> the named hardpoint of Hardpoints (-1 when unknown): its instance's frame
//     applied to the hardpoint's local position and orientation;
//   anything else -> the same ERROR, 0.
GENRESULT DACOM_API FThornCompound::GetHardpointXform(int TargetType, char const* Name,
                                                      float* OutXform12)
{
    Transform* Out = reinterpret_cast<Transform*>(OutXform12);
    if (TargetType != 0)
    {
        if (TargetType == 2)
        {
            if (_stricmp(Name, "root") != 0 && _stricmp(Name, "") != 0)
            {
                if (Name == nullptr || Name[0] != '\0')
                {
                    FDUMP(0x100001, "%s(%d) : *** ERROR: %s", GScriptCompoundFile, 0x17d,
                          "Thorn requested a sub_part for which the implementation is not written");
                    return GR_OK;
                }
                FDUMP(0x100002, "%s(%d) : *** WARNING: %s", GScriptCompoundFile, 0x179,
                      "Petal bug:  Script wants target_type = PART with target_name = <null>\n");
                return GR_OK;
            }
        }
        else if (TargetType == 1)
        {
            int const Index = Hardpoints.find_name(Name);
            if (Index == -1)
                return GR_GENERIC;
            long const HpInstance = Hardpoints.get_instance(Index);
            HardpointInfo const Info = Hardpoints.get_info(Index);
            Vector const InstPos = *reinterpret_cast<Vector const*>(
                DALib::Engine->GetInstancePosition(InstancePointer(HpInstance)));
            Matrix const InstOrient = *reinterpret_cast<Matrix const*>(
                DALib::Engine->GetInstanceOrientation(InstancePointer(HpInstance)));

            FVector Rotated;
            ThornCompoundMath()->TransformMatrix(Rotated, AsMathMatrix(InstOrient),
                                                 AsMathVector(Info.Position));
            Vector World;
            World.x = Rotated.X + InstPos.x;
            World.y = Rotated.Y + InstPos.y;
            World.z = Rotated.Z + InstPos.z;
            FMatrix Product;
            ThornCompoundMath()->MulMatrix(Product, AsMathMatrix(InstOrient),
                                           AsMathMatrix(Info.Orient));
            Out->Translation = World;
            Out->Rotation = AsMatrix(Product);
            return GR_OK;
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", GScriptCompoundFile, 0x17d,
                  "Thorn requested a sub_part for which the implementation is not written");
            return GR_OK;
        }
    }

    if (Instance == -1)
        return GR_GENERIC;
    Out->Translation = *reinterpret_cast<Vector const*>(
        DALib::Engine->GetInstancePosition(InstancePointer(Instance)));
    Out->Rotation = *reinterpret_cast<Matrix const*>(
        DALib::Engine->GetInstanceOrientation(InstancePointer(Instance)));
    return GR_OK;
}

// LAB_00451660 (stdcall, RET 8). Local takes the transform; the instance goes to
// from_frame(Local.Orient * Offset.Pos + Local.Pos) and from_frame(Local.Orient *
// Offset.Orient), position first, then Engine slot 27 (0, 0). Always 0.
GENRESULT DACOM_API FThornCompound::SetTransform(float const* Transform12)
{
    Transform const* In = reinterpret_cast<Transform const*>(Transform12);
    Local.Pos = In->Translation;
    Local.Orient = In->Rotation;
    Vector const World = InstancePosFromLocal(*this);
    Matrix const WorldOrient = InstanceOrientFromLocal(*this);

    void* const Inst = InstancePointer(Instance);
    Vector EnginePos = Frame.trans_from_frame(World);
    DALib::Engine->SetInstancePosition(Inst, &EnginePos);
    Matrix EngineOrient = Frame.trans_from_frame(WorldOrient);
    DALib::Engine->SetInstanceOrientation(Inst, &EngineOrient);
    DALib::Engine->UpdateInstance(Inst, 0, nullptr);
    return GR_OK;
}

// LAB_00451a00 (stdcall, RET 8): the script-frame transform LocalFromInstance recovers.
GENRESULT DACOM_API FThornCompound::GetTransform(float* OutTransform12)
{
    Transform* Out = reinterpret_cast<Transform*>(OutTransform12);
    Vector Pos;
    Matrix Orient;
    LocalFromInstance(*this, Pos, Orient);
    Out->Translation = Pos;
    Out->Rotation = Orient;
    return GR_OK;
}

// LAB_004514f0 (geo facet, RET 8): Local.Pos only, then the SetTransform position path.
GENRESULT DACOM_API FThornCompound::SetPosition(float* Position)
{
    Local.Pos = *reinterpret_cast<Vector const*>(Position);
    Vector const World = InstancePosFromLocal(*this);
    void* const Inst = InstancePointer(Instance);
    Vector EnginePos = Frame.trans_from_frame(World);
    DALib::Engine->SetInstancePosition(Inst, &EnginePos);
    DALib::Engine->UpdateInstance(Inst, 0, nullptr);
    return GR_OK;
}

// LAB_004515b0 (geo facet, RET 8): Local.Orient only, then the orientation path.
GENRESULT DACOM_API FThornCompound::SetOrientation(float* Orient9)
{
    Local.Orient = *reinterpret_cast<Matrix const*>(Orient9);
    Matrix EngineOrient = Frame.trans_from_frame(InstanceOrientFromLocal(*this));
    void* const Inst = InstancePointer(Instance);
    DALib::Engine->SetInstanceOrientation(Inst, &EngineOrient);
    DALib::Engine->UpdateInstance(Inst, 0, nullptr);
    return GR_OK;
}

// LAB_004517b0 (geo facet, RET 8).
GENRESULT DACOM_API FThornCompound::GetPosition(float* OutPosition)
{
    Vector Pos;
    Matrix Orient;
    LocalFromInstance(*this, Pos, Orient);
    *reinterpret_cast<Vector*>(OutPosition) = Pos;
    return GR_OK;
}

// LAB_00451930 (geo facet, RET 8). Only the orientation half: no position is read.
GENRESULT DACOM_API FThornCompound::GetOrientation(float* OutOrient9)
{
    void* const Inst = InstancePointer(Instance);
    Matrix const LocalOrient = Frame.trans_to_frame(
        *reinterpret_cast<Matrix const*>(DALib::Engine->GetInstanceOrientation(Inst)));
    Matrix const OffsetTransposed = Transposed(Offset.Orient);
    FMatrix Product;
    ThornCompoundMath()->MulMatrix(Product, AsMathMatrix(LocalOrient),
                                   AsMathMatrix(OffsetTransposed));
    *reinterpret_cast<Matrix*>(OutOrient9) = AsMatrix(Product);
    return GR_OK;
}

// FUN_00451290 (slot 14). The summary destroyed, every playing slot stopped (IAnimation2 14),
// destroyed (10) and reset, all sixteen reset again (FUN_00451380), and the room object, when
// there is one, back to light group 3, layer 0, render mode 0, +0x14 0.
void FThornCompound::Stop()
{
    Hardpoints.destroy();
    int Slot = 0;
    for (; Slot < kMotionSlots; ++Slot)
    {
        if (bPlaying[Slot] == 0)
            continue;
        DALib::Anim->StopPlayback(Playback[Slot]);
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(static_cast<uintptr_t>(Playback[Slot])));
        Playback[Slot] = kCompoundNoPlayback;
        EventId[Slot] = -1;
        bPlaying[Slot] = 0;
    }
    ResetMotionSlots();
    if (Object != nullptr)
    {
        Object->LightGroup = 3;
        Object->Layer = 0;
        Object->RenderMode = 0;
        Object->Flag14 = 0;
    }
}

// LAB_00451360 -- +0x70. (ShipObject's slot 6, 0x005cbe08, is the same folded body.)
void* FThornCompound::GetEngineInstance()
{
    return InstancePointer(Instance);
}

// LAB_00451ed0 -- only with a room object and bUseScene.
void FThornCompound::SetScene(void* Scene)
{
    if (Object != nullptr && bUseScene != 0)
        Object->AmbientSource = Scene;
}

// LAB_00451ef0 -- only with a room object.
void FThornCompound::SetFlags(unsigned char bVisible, unsigned char bLit, unsigned char bFog,
                              unsigned char bFlag14)
{
    if (Object == nullptr)
        return;
    Object->bVisible = bVisible;
    Object->bLit = bLit;
    Object->bFog = bFog;
    Object->Flag14 = bFlag14;
}

// FUN_00451330.
void FThornCompound::SetInstance(long InInstance)
{
    Instance = InInstance;
    Hardpoints.destroy();
    Hardpoints.analyze_instance(InInstance);
}

// FUN_00451380.
void FThornCompound::ResetMotionSlots()
{
    int Slot = 0;
    for (; Slot < kMotionSlots; ++Slot)
    {
        Playback[Slot] = kCompoundNoPlayback;
        EventId[Slot] = -1;
        bPlaying[Slot] = 0;
    }
}
