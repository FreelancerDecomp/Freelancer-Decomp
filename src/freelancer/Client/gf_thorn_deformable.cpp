// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_thorn_deformable.h"

#include "DALib.h"
#include "character_info.h"      // GetCharacterInfoDb
#include "common_goods.h"        // HardpointSummary, HardpointInfo
#include "common_id.h"           // CreateID
#include "common_misc.h"         // malloc_strfree
#include "gf_character_object.h"
#include "gf_script_object.h"

#include <cstdint>
#include <cstring>

namespace
{
    char const* const GScriptDeformableFile =
        "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptDeformable.cpp";

    void* const kDeformNoScript = reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    void* const kDeformNoInstance = reinterpret_cast<void*>(static_cast<intptr_t>(-1));

    void* InstancePointer(long Instance)
    {
        return reinterpret_cast<void*>(static_cast<intptr_t>(Instance));
    }

    Vector InstancePosition(void* Instance)
    {
        return *reinterpret_cast<Vector const*>(DALib::Engine->GetInstancePosition(Instance));
    }

    Matrix InstanceOrientation(void* Instance)
    {
        return *reinterpret_cast<Matrix const*>(DALib::Engine->GetInstanceOrientation(Instance));
    }
}

// LAB_00451f50.
FThornDeformable::FMotionSlot::FMotionSlot() : Handle(-1), ScriptId(0), EventId(-1), bActive(0)
{
}

// LAB_00451f70. (0x00451fae..0x00451fb6 also zero a stack vector nothing reads.)
FThornDeformable::FAimSlot::FAimSlot() : Handle(nullptr), Key(nullptr), bActive(0), bStarted(0)
{
    Target.Pos.x = 0.0f;
    Target.Pos.y = 0.0f;
    Target.Pos.z = 0.0f;
    MatrixSetIdentity(Target.Orient);
}

// FUN_00451fd0. Base ctor, the two slot arrays (FUN_005b7fba with LAB_00451f50 / LAB_00451f70),
// then +0x6c / +0x70 / +0x74 = 0.
FThornDeformable::FThornDeformable(Csys const& ScriptFrame, FGfScript* InOwner,
                                   FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Character(nullptr),
      ActorName(nullptr),
      FloorHeight(0.0f)
{
}

// FUN_004520c0: the two arrays' element destructors are the empty IDPMsgHandler::OnDisconnect
// fold, then FUN_0044fd80.
FThornDeformable::~FThornDeformable()
{
}

// LAB_00452240 (stdcall facet, RET 8).
//   The script id is CreateID(animation). The first idle motion slot takes the event id, the
//   script id and bActive -- or nothing, when all 32 are busy (the slot index stays -1).
//   CharacterInfo FUN_00434d10 acquires the script (-1: "does not exist" WARNING, 0).
//   IDeformable slot 7 plays it on the character with the descriptor's timing, event flags and
//   heading; the handle lands in the slot and the character counts it (FUN_004456e0).
//   A locked bone that resolves to a part is blocked through IAnimation2 slot 36 ("Failed to
//   block bone" WARNING when that answers non-zero), and that answer is returned; otherwise
//   slot 7's is.
// RETAIL DEFECT KEPT: with no idle slot, slot 7 still gets &Motions[-1].Handle -- this + 0x68, the
// reference byte and its padding.
GENRESULT DACOM_API FThornDeformable::StartMotion(void* DescArg)
{
    FThornMotionEventDesc const* Desc = static_cast<FThornMotionEventDesc const*>(DescArg);
    char const* const LockedBone = Desc->LockedBone;
    unsigned const ScriptId = CreateID(Desc->Animation);
    int const EventId = Desc->Common.UniqueId;

    int Slot = -1;
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        if (Motions[Index].bActive == 0)
        {
            Slot = Index;
            Motions[Slot].EventId = EventId;
            Motions[Slot].ScriptId = ScriptId;
            Motions[Slot].bActive = 1;
            break;
        }
    }

    void* const AnimScript = GetCharacterInfoDb()->AcquireScript(ScriptId);
    if (AnimScript == kDeformNoScript)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: Animation (%s) does not exist, skipping\n",
              GScriptDeformableFile, 0x14b, Desc->Animation);
        return GR_OK;
    }

    GENRESULT const PlayResult = DALib::Deform->Play(
        Character->Lod.Instance, &Motions[Slot].Handle, AnimScript,
        const_cast<char*>(Desc->Animation), AsOpaqueDword(Desc->StartTime),
        AsOpaqueDword(Desc->TransTime), AsOpaqueDword(Desc->TimeScale),
        AsOpaqueDword(Desc->Weight), AsOpaqueDword(Desc->Common.EventFlags),
        AsOpaqueDword(Desc->Heading), nullptr);
    Character->AddMotionHandle(Motions[Slot].Handle);

    if (LockedBone == nullptr)
        return PlayResult;
    long const Part = Character->FindPart(LockedBone);
    if (Part == -1)
        return PlayResult;
    unsigned long const BlockResult =
        DALib::Anim->RegisterHandle(static_cast<unsigned long>(Part));
    if (BlockResult != 0)
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Failed to block bone %s for animation %s, bone may already "
              "be locked\n",
              GScriptDeformableFile, 0x143, LockedBone, Desc->Animation);
    }
    return static_cast<GENRESULT>(BlockResult);
}

// LAB_004523a0 (stdcall facet, RET 8). The active slot with the event's id (-1 when none). A
// locked bone that resolves is released through IAnimation2 slot 38 (answer ignored). The
// character forgets the slot's handle (FUN_004456f0) either way; an active slot's handle is
// destroyed (IDeformable slot 10), its script released (FUN_00434e20) and the slot reset.
// Always 0.
GENRESULT DACOM_API FThornDeformable::StopMotion(void* DescArg)
{
    FThornMotionEventDesc const* Desc = static_cast<FThornMotionEventDesc const*>(DescArg);
    int Slot = -1;
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        if (Motions[Index].bActive != 0 && Motions[Index].EventId == Desc->Common.UniqueId)
        {
            Slot = Index;
            break;
        }
    }

    if (Desc->LockedBone != nullptr)
    {
        long const Part = Character->FindPart(Desc->LockedBone);
        if (Part != -1)
            DALib::Anim->HasAnyHandle(static_cast<unsigned long>(Part));
    }

    FMotionSlot& Motion = Motions[Slot];
    Character->RemoveMotionHandle(Motion.Handle);
    if (Motion.bActive != 0)
    {
        DALib::Deform->DestroyHandle(Character->Lod.Instance, Motion.Handle);
        GetCharacterInfoDb()->ReleaseScript(Motion.ScriptId);
        Motion.Handle = -1;
        Motion.ScriptId = 0;
        Motion.EventId = -1;
        Motion.bActive = 0;
    }
    return GR_OK;
}

// LAB_00452d00 (stdcall facet, RET 8). The height is kept; the character gets the world y of
// the script-frame point (0, Height, 0) through RoomObject slot 3.
GENRESULT DACOM_API FThornDeformable::SetFloorHeight(float Height)
{
    FloorHeight = Height;
    Vector Point;
    Point.x = 0.0f;
    Point.y = Height;
    Point.z = 0.0f;
    Vector const World = Frame.trans_from_frame(Point);
    Character->SetFloat1C(World.y);
    return GR_OK;
}

// LAB_00452210 (stdcall facet, RET 8): RoomObject slot 5 with DeltaMs * 0.001 (FILD, FMUL by the
// double at 0x005c75f8, pushed as a double).
GENRESULT DACOM_API FThornDeformable::AdvanceTime(int DeltaMs)
{
    Character->Update(static_cast<double>(DeltaMs) * 0.001);
    return GR_OK;
}

// FUN_00452d70: FUN_0044fdd0(+0x20, +0x30, +0x1c, +0x2c) -- the Compound's order swapped --, then
// the Owner's FUN_0044f9a0(+0x1c, 1). Always 0.
GENRESULT DACOM_API FThornDeformable::ConnectHardpoints(void* DescArg)
{
    FThornConnectEventDesc const* Desc = static_cast<FThornConnectEventDesc const*>(DescArg);
    ConnectEntityHardpoints(Desc->Common.SecondaryIndex, Desc->ParentHardpoint,
                            Desc->Common.PrimaryIndex, Desc->Hardpoint);
    Owner->SetEntityPropDetached(Desc->Common.PrimaryIndex, 1);
    return GR_OK;
}

// FUN_00452db0: the disconnect twin.
GENRESULT DACOM_API FThornDeformable::DisconnectHardpoints(void* DescArg)
{
    FThornConnectEventDesc const* Desc = static_cast<FThornConnectEventDesc const*>(DescArg);
    DisconnectEntityHardpoints(Desc->Common.SecondaryIndex, Desc->ParentHardpoint,
                               Desc->Common.PrimaryIndex, Desc->Hardpoint);
    Owner->SetEntityPropDetached(Desc->Common.PrimaryIndex, 0);
    return GR_OK;
}

// LAB_00452730 (stdcall, RET 0x10). World position (0) and orientation (identity) of:
//   0 ROOT -> the character's engine instance (-1 when it has none, returned as is);
//   2 PART -> the named part ("Invalid part name" WARNING and -1 when unknown);
//   1 HARDPOINT -> the character's named hardpoint ("Invalid hardpoint name" ERROR and -1): its
//     instance's frame applied to the hardpoint's local position and orientation;
//   anything else -> the defaults.
// The answer is converted into the script frame (trans_to_frame). 0.
GENRESULT DACOM_API FThornDeformable::GetHardpointXform(int TargetType, char const* Name,
                                                        float* OutXform12)
{
    Vector Pos;
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    Matrix Orient;
    MatrixSetIdentity(Orient);

    if (TargetType == 0)
    {
        void* const Instance = Character->GetEngineInstance();
        if (Instance == kDeformNoInstance)
            return GR_GENERIC;
        Pos = InstancePosition(Instance);
        Orient = InstanceOrientation(Instance);
    }
    else if (TargetType == 2)
    {
        long const Part = Character->FindPart(Name);
        if (Part == -1)
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: Invalid part name %s in get_sub_part_transform()\n",
                  GScriptDeformableFile, 0x255, Name);
            return GR_GENERIC;
        }
        Pos = InstancePosition(InstancePointer(Part));
        Orient = InstanceOrientation(InstancePointer(Part));
    }
    else if (TargetType == 1)
    {
        HardpointSummary& Hardpoints = Character->Hardpoints;
        int const Index = Hardpoints.find_name(Name);
        if (Index == -1)
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: Invalid hardpoint name %s in get_sub_part_transform()\n",
                  GScriptDeformableFile, 0x265, Name);
            return GR_GENERIC;
        }
        long const HpInstance = Hardpoints.get_instance(Index);
        Vector const HpPos = Hardpoints.get_info(Index).Position;
        Matrix const HpOrient = Hardpoints.get_info(Index).Orient;
        Csys InstanceFrame;
        InstanceFrame.Pos = InstancePosition(InstancePointer(HpInstance));
        InstanceFrame.Orient = InstanceOrientation(InstancePointer(HpInstance));
        Pos = InstanceFrame.trans_from_frame(HpPos);
        Orient = InstanceFrame.trans_from_frame(HpOrient);
    }

    Transform* Out = reinterpret_cast<Transform*>(OutXform12);
    Out->Translation = Frame.trans_to_frame(Pos);
    Out->Rotation = Frame.trans_to_frame(Orient);
    return GR_OK;
}

// LAB_00452d50.
GENRESULT DACOM_API FThornDeformable::GetFloorHeight(float* OutHeight)
{
    *OutHeight = FloorHeight;
    return GR_OK;
}

// LAB_00452b50 (stdcall, RET 8): IDeformable slots 19 / 20 with the transform brought out of the
// script frame, position first. 0.
GENRESULT DACOM_API FThornDeformable::SetTransform(float const* Transform12)
{
    Transform const* In = reinterpret_cast<Transform const*>(Transform12);
    Vector WorldPos = Frame.trans_from_frame(In->Translation);
    DALib::Deform->SetPosePos(Character->Lod.Instance, &WorldPos.x);
    Matrix WorldOrient = Frame.trans_from_frame(In->Rotation);
    DALib::Deform->SetPoseOri(Character->Lod.Instance, &WorldOrient);
    return GR_OK;
}

// LAB_00452c70 (stdcall, RET 8): the character's engine instance, into the script frame.
GENRESULT DACOM_API FThornDeformable::GetTransform(float* OutTransform12)
{
    void* const Instance = Character->GetEngineInstance();
    Transform* Out = reinterpret_cast<Transform*>(OutTransform12);
    Out->Translation = Frame.trans_to_frame(InstancePosition(Instance));
    Out->Rotation = Frame.trans_to_frame(InstanceOrientation(Instance));
    return GR_OK;
}

// LAB_00452460 (IK facet, RET 0x10).
//   End effector = the character's part named by the descriptor; root = its count_to_root-th
//   ancestor (FUN_0044fa60, run before either is tested). Either -1 -> -1.
//   The target comes out of the script frame; the first inactive aim slot (none -> -1) takes the
//   key, the target and bActive. The aim descriptor (engbase.h FDeformAimDesc) carries the
//   event flags, plus 0x40 for point_at (and 0x80 when count_to_root is 1 inside that), 0x20 for
//   move_to, 0x100 when the flags' low byte has its top bit set (0x004525e8 `TEST CL,CL; JNS`).
//   IDeformable slot 11 starts it (transition duration as the last argument), the character
//   counts the handle (FUN_00445700), and a zero answer marks the slot started (0); anything
//   else is a TRACE and -1 with bStarted cleared. bActive stays set either way.
GENRESULT DACOM_API FThornDeformable::StartIk(void* DescArg, float* Position, float* Orientation)
{
    FThornStartIkDesc const* Desc = static_cast<FThornStartIkDesc const*>(DescArg);
    long const EndPart = Character->FindPart(Desc->EndEffector);
    void* const Root = GfEngineInstanceAncestor(InstancePointer(EndPart), Desc->CountToRoot);
    if (EndPart == -1 || Root == kDeformNoInstance)
        return GR_GENERIC;

    Vector const TargetPos = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Position));
    Matrix const TargetOrient =
        Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Orientation));

    int Slot = -1;
    int Index = 0;
    for (; Index < kAimSlots; ++Index)
    {
        if (Aims[Index].bActive == 0)
        {
            Slot = Index;
            break;
        }
    }
    if (Slot == -1)
        return GR_GENERIC;

    FAimSlot& Aim = Aims[Slot];
    Aim.Key = Desc->Key;
    Aim.Target.Pos = TargetPos;
    Aim.bActive = 1;
    Aim.Target.Orient = TargetOrient;

    unsigned long const EventFlags = static_cast<unsigned long>(Desc->Common.EventFlags);
    FDeformAimDesc AimDesc;
    AimDesc.Name = "";
    AimDesc.RootPart = static_cast<long>(reinterpret_cast<intptr_t>(Root));
    AimDesc.EndPart = EndPart;
    AimDesc.Flags = EventFlags;
    AimDesc.TargetPos = &Aim.Target.Pos.x;
    AimDesc.TargetOrient = &Aim.Target.Orient.E[0][0];
    AimDesc.Damping = Desc->Damping;
    AimDesc.Field1C = 0;
    if (Desc->bPointAt != 0)
    {
        AimDesc.Flags = EventFlags | 0x40;
        if (Desc->CountToRoot == 1)
            AimDesc.Flags = EventFlags | 0x40 | 0x80;
    }
    if (Desc->bMoveTo != 0)
        AimDesc.Flags |= 0x20;
    if (static_cast<signed char>(EventFlags & 0xff) < 0)
        AimDesc.Flags |= 0x100;

    GENRESULT const Result = DALib::Deform->StartAim(
        Character->Lod.Instance, &Aim.Handle, reinterpret_cast<unsigned char*>(&AimDesc),
        AsOpaqueDword(Desc->TransitionDuration));
    Character->AddAimHandle(Aim.Handle);
    if (Result == 0)
    {
        Aim.bStarted = 1;
        return GR_OK;
    }
    FDUMP(0x100004, "%s(%d) : TRACE_1:General:%s", GScriptDeformableFile, 0x213,
          "Invalid Deform::start_aim() command");
    Aim.bStarted = 0;
    return GR_GENERIC;
}

// FUN_00452a30 (IK facet, RET 8). The active slot with the descriptor's key (-1 when none); the
// character forgets its handle (FUN_00445710); an active slot is stopped (IDeformable slot 12,
// only if it started) and deactivated. Answer: slot 12's, or -1 when it was not called.
GENRESULT DACOM_API FThornDeformable::StopIk(void* DescArg)
{
    FThornStartIkDesc const* Desc = static_cast<FThornStartIkDesc const*>(DescArg);
    int Slot = -1;
    int Index = 0;
    for (; Index < kAimSlots; ++Index)
    {
        if (Aims[Index].bActive != 0 && Aims[Index].Key == Desc->Key)
        {
            Slot = Index;
            break;
        }
    }

    FAimSlot& Aim = Aims[Slot];
    Character->RemoveAimHandle(Aim.Handle);
    GENRESULT Result = GR_GENERIC;
    if (Aim.bActive != 0)
    {
        if (Aim.bStarted != 0)
            Result = DALib::Deform->StopAim(Character->Lod.Instance, Aim.Handle);
        Aim.bActive = 0;
    }
    return Result;
}

// LAB_00452680 (IK facet, RET 0x10). The active slot with the key (-1 when none); not started
// -> -1; otherwise its target takes the new position and orientation out of the script frame. 0.
GENRESULT DACOM_API FThornDeformable::UpdateIk(void* DescArg, float* Position, float* Orientation)
{
    FThornStartIkDesc const* Desc = static_cast<FThornStartIkDesc const*>(DescArg);
    int Slot = -1;
    int Index = 0;
    for (; Index < kAimSlots; ++Index)
    {
        if (Aims[Index].bActive != 0 && Aims[Index].Key == Desc->Key)
        {
            Slot = Index;
            break;
        }
    }

    FAimSlot& Aim = Aims[Slot];
    if (Aim.bStarted == 0)
        return GR_GENERIC;
    Aim.Target.Pos = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Position));
    Aim.Target.Orient = Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Orientation));
    return GR_OK;
}

// LAB_00452ab0 (geo facet, RET 8): IDeformable slot 19 with the position out of the script frame.
GENRESULT DACOM_API FThornDeformable::SetPosition(float* Position)
{
    Vector WorldPos = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Position));
    DALib::Deform->SetPosePos(Character->Lod.Instance, &WorldPos.x);
    return GR_OK;
}

// LAB_00452b00 (geo facet, RET 8): IDeformable slot 20.
GENRESULT DACOM_API FThornDeformable::SetOrientation(float* Orient9)
{
    Matrix WorldOrient = Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Orient9));
    DALib::Deform->SetPoseOri(Character->Lod.Instance, &WorldOrient);
    return GR_OK;
}

// LAB_00452bd0 (geo facet, RET 8).
GENRESULT DACOM_API FThornDeformable::GetPosition(float* OutPosition)
{
    void* const Instance = Character->GetEngineInstance();
    *reinterpret_cast<Vector*>(OutPosition) = Frame.trans_to_frame(InstancePosition(Instance));
    return GR_OK;
}

// LAB_00452c20 (geo facet, RET 8).
GENRESULT DACOM_API FThornDeformable::GetOrientation(float* OutOrient9)
{
    void* const Instance = Character->GetEngineInstance();
    *reinterpret_cast<Matrix*>(OutOrient9) = Frame.trans_to_frame(InstanceOrientation(Instance));
    return GR_OK;
}

// FUN_00452130 (slot 14). The actor name freed; every active motion's handle destroyed
// (IDeformable slot 10), its script released and the slot reset; every active aim stopped (slot
// 12, only if started) and deactivated (bStarted is left as it is); the character's light group,
// layer and render mode cleared when there is one -- and then FUN_00445540 is called on it with
// the owning script's +0x55 byte WITHOUT that check (0x004521e9).
void FThornDeformable::Stop()
{
    malloc_strfree(&ActorName);
    int Index = 0;
    for (; Index < kMotionSlots; ++Index)
    {
        FMotionSlot& Motion = Motions[Index];
        if (Motion.bActive == 0)
            continue;
        DALib::Deform->DestroyHandle(Character->Lod.Instance, Motion.Handle);
        GetCharacterInfoDb()->ReleaseScript(Motion.ScriptId);
        Motion.Handle = -1;
        Motion.ScriptId = 0;
        Motion.EventId = -1;
        Motion.bActive = 0;
    }
    for (Index = 0; Index < kAimSlots; ++Index)
    {
        FAimSlot& Aim = Aims[Index];
        if (Aim.bActive == 0)
            continue;
        if (Aim.bStarted != 0)
            DALib::Deform->StopAim(Character->Lod.Instance, Aim.Handle);
        Aim.bActive = 0;
    }
    if (Character != nullptr)
    {
        Character->LightGroup = 0;
        Character->Layer = 0;
        Character->RenderMode = 0;
    }
    Character->ResetScriptState(Owner->Field_0x55);
}

// LAB_00452df0 (thiscall, RET 0xc). A non-null name equal (DACOM stricmp) to the actor name hands
// out the character's +0x1b4 and the character itself.
bool FThornDeformable::FindActor(char const* InActorName, void** OutActorField, void** OutActor)
{
    if (InActorName == nullptr)
        return false;
    if (_stricmp(ActorName, InActorName) != 0)
        return false;
    *OutActorField = reinterpret_cast<void*>(static_cast<uintptr_t>(Character->Props.VoiceId));
    *OutActor = Character;
    return true;
}

// LAB_00452200 -- tail jump to the character's RoomObject slot 6.
void* FThornDeformable::GetEngineInstance()
{
    return Character->GetEngineInstance();
}

// LAB_00452ec0 -- no gate, unlike the Compound's.
void FThornDeformable::SetScene(void* Scene)
{
    Character->AmbientSource = Scene;
}

// LAB_00452ed0 -- no null check.
void FThornDeformable::SetFlags(unsigned char bVisible, unsigned char bLit, unsigned char bFog,
                                unsigned char bFlag14)
{
    Character->bVisible = bVisible;
    Character->bLit = bLit;
    Character->bFog = bFog;
    Character->Flag14 = bFlag14;
}

// FUN_00452e40.
Csys FThornDeformable::GetCharacterCsys() const
{
    void* const Instance = Character->GetEngineInstance();
    Csys Out;
    Out.Pos = InstancePosition(Instance);
    Out.Orient = InstanceOrientation(Instance);
    return Out;
}

// FUN_00452eb0.
void* FThornDeformable::GetDeformInstance() const
{
    return Character->Lod.Instance;
}
