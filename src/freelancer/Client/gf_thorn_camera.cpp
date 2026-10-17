// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_thorn_camera.h"

#include "dacom.h"
#include "gf_script.h"
#include "room_camera.h"

#include <cstdint>
#include <cstring>

namespace
{
    char const* const GScriptCameraFile = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptCamera.cpp";
    char const* const GInvalidCameraReference =
        "%s(%d) : *** ERROR: Using invalid camera reference %s\n";

    GENRESULT const kThornReferenceMissing = static_cast<GENRESULT>(-1);

    // The pool slot is indexed without a range check by every body below (`SHL reg,6;
    // ADD reg,DAT_00668b00`). A slot of -1 -- Allocate found the 256 slots full -- would reach
    // the 0x40 bytes before the pool, exactly as retail does.
    FGfCameraSlot& PoolSlotAt(int Slot)
    {
        return G_GfCameraPool.Slots[Slot];
    }

    // The head of LAB_00450880 / LAB_00450940: to world through the script frame, into the slot,
    // FUN_00556cb0 when this slot is DAT_0066d100.
    void StorePoolPosition(Csys const& Frame, int Slot, float const* Local3)
    {
        Vector const World = Frame.trans_from_frame(*reinterpret_cast<Vector const*>(Local3));
        FGfCameraSlot& Target = PoolSlotAt(Slot);
        Target.Pos[0] = World.x;
        Target.Pos[1] = World.y;
        Target.Pos[2] = World.z;
        if (Slot == G_GfCameraPool.ActiveIndex)
            RoomCameraSetPosition(Target.Camera, Target.Pos);
    }

    // LAB_004508e0 / the tail of LAB_00450940: the same for the orientation (REP MOVSD 9),
    // FUN_00556cc0.
    void StorePoolOrientation(Csys const& Frame, int Slot, float const* Local9)
    {
        Matrix const World = Frame.trans_from_frame(*reinterpret_cast<Matrix const*>(Local9));
        FGfCameraSlot& Target = PoolSlotAt(Slot);
        memcpy(Target.Orient, &World, sizeof(Target.Orient));
        if (Slot == G_GfCameraPool.ActiveIndex)
            RoomCameraSetOrientation(Target.Camera, Target.Orient);
    }

    void LoadPoolPosition(Csys const& Frame, int Slot, float* Out3)
    {
        FGfCameraSlot const& Source = PoolSlotAt(Slot);
        Vector const Local = Frame.trans_to_frame(*reinterpret_cast<Vector const*>(Source.Pos));
        Out3[0] = Local.x;
        Out3[1] = Local.y;
        Out3[2] = Local.z;
    }

    void LoadPoolOrientation(Csys const& Frame, int Slot, float* Out9)
    {
        FGfCameraSlot const& Source = PoolSlotAt(Slot);
        Matrix const Local = Frame.trans_to_frame(*reinterpret_cast<Matrix const*>(Source.Orient));
        memcpy(Out9, &Local, sizeof(float) * 9);
    }

    GENRESULT ReportInvalidReference(char const* Name, int Line)
    {
        FDUMP(0x100001, GInvalidCameraReference, GScriptCameraFile, Line, Name);
        return kThornReferenceMissing;
    }
}

// ---------------------------------------------------------------------------------------
// Thorn_Camera
// ---------------------------------------------------------------------------------------

// FUN_004507a0. 0x004507b4 base ctor, 0x004507b9..0x004507f0 the nine vptrs (the compiler's),
// 0x004507f7 +0x6c = -1.
FThornCamera::FThornCamera(Csys const& ScriptFrame, FGfScript* InOwner,
                           FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      PoolSlot(-1)
{
}

// FUN_00450830. 0x0045083f..0x00450847: PUSH bCanBeActive, PUSH Camera, PUSH Priority, ECX =
// the pool; 0x0045084c the answer into +0x6c.
void FThornCamera::Register(FRoomCamera* Camera, int Priority, unsigned char bCanBeActive)
{
    PoolSlot = G_GfCameraPool.Allocate(Priority, Camera, bCanBeActive);
}

// LAB_00450940 (identity, RET 8). The position (Transform + 0x24) first -- trans_from_frame,
// slot +0x04, FUN_00556cb0 when active (0x0045094f..0x00450998) -- then the rotation
// (Transform + 0), slot +0x10, FUN_00556cc0 when active (0x0045099b..0x004509da). 0.
GENRESULT DACOM_API FThornCamera::SetTransform(float const* Transform12)
{
    StorePoolPosition(Frame, PoolSlot, Transform12 + 9);
    StorePoolOrientation(Frame, PoolSlot, Transform12);
    return GR_OK;
}

// LAB_00450aa0 (identity, RET 8). trans_to_frame of the slot position into Out + 0x24
// (0x00450aaa..0x00450af3), then of the slot orientation into Out + 0 (0x00450af6..0x00450b28).
GENRESULT DACOM_API FThornCamera::GetTransform(float* OutTransform12)
{
    LoadPoolPosition(Frame, PoolSlot, OutTransform12 + 9);
    LoadPoolOrientation(Frame, PoolSlot, OutTransform12);
    return GR_OK;
}

// LAB_00450880 (geo facet, RET 8). Facet +0x20 = the frame, +0x60 = the slot.
GENRESULT DACOM_API FThornCamera::SetPosition(float* Position)
{
    StorePoolPosition(Frame, PoolSlot, Position);
    return GR_OK;
}

// LAB_004508e0 (geo facet, RET 8).
GENRESULT DACOM_API FThornCamera::SetOrientation(float* Orient9)
{
    StorePoolOrientation(Frame, PoolSlot, Orient9);
    return GR_OK;
}

// LAB_004509f0 (geo facet, RET 8). The slot position copied to the stack, trans_to_frame, out.
GENRESULT DACOM_API FThornCamera::GetPosition(float* OutPosition)
{
    LoadPoolPosition(Frame, PoolSlot, OutPosition);
    return GR_OK;
}

// LAB_00450a50 (geo facet, RET 8).
GENRESULT DACOM_API FThornCamera::GetOrientation(float* OutOrient9)
{
    LoadPoolOrientation(Frame, PoolSlot, OutOrient9);
    return GR_OK;
}

// LAB_00450b40 (camera facet, RET 8). Facet +0x5c = the slot. Slot +0x34, then FUN_00556ce0
// with the value when the slot is DAT_0066d100.
GENRESULT DACOM_API FThornCamera::SetFov(const float* Fov)
{
    FGfCameraSlot& Slot = PoolSlotAt(PoolSlot);
    float Value;
    std::memcpy(&Value, Fov, sizeof(Value));
    std::memcpy(&Slot.HalfFovXDeg, &Value, sizeof(Value));
    if (PoolSlot == G_GfCameraPool.ActiveIndex)
        RoomCameraSetHalfFovXValue(Slot.Camera, &Value);
    return GR_OK;
}

// LAB_00450b80 -- slot +0x38, FUN_00556e00.
GENRESULT DACOM_API FThornCamera::SetNear(const float* Near)
{
    FGfCameraSlot& Slot = PoolSlotAt(PoolSlot);
    float Value;
    std::memcpy(&Value, Near, sizeof(Value));
    std::memcpy(&Slot.ZNear, &Value, sizeof(Value));
    if (PoolSlot == G_GfCameraPool.ActiveIndex)
        RoomCameraSetZNearValue(Slot.Camera, &Value);
    return GR_OK;
}

// LAB_00450bc0 -- slot +0x3c, FUN_00556f10.
GENRESULT DACOM_API FThornCamera::SetFar(const float* Far)
{
    FGfCameraSlot& Slot = PoolSlotAt(PoolSlot);
    float Value;
    std::memcpy(&Value, Far, sizeof(Value));
    std::memcpy(&Slot.ZFar, &Value, sizeof(Value));
    if (PoolSlot == G_GfCameraPool.ActiveIndex)
        RoomCameraSetZFarValue(Slot.Camera, &Value);
    return GR_OK;
}

// LAB_00450c00 / LAB_00450c20 / LAB_00450c40 -- FLD slot +0x34 / +0x38 / +0x3c, FSTP [Out], 0.
GENRESULT DACOM_API FThornCamera::GetFov(float* OutFov)
{
    *OutFov = PoolSlotAt(PoolSlot).HalfFovXDeg;
    return GR_OK;
}

GENRESULT DACOM_API FThornCamera::GetNear(float* OutNear)
{
    *OutNear = PoolSlotAt(PoolSlot).ZNear;
    return GR_OK;
}

GENRESULT DACOM_API FThornCamera::GetFar(float* OutFar)
{
    *OutFar = PoolSlotAt(PoolSlot).ZFar;
    return GR_OK;
}

// LAB_00450860 -- G_GfCameraPool.Free(+0x6c). +0x6c keeps its value.
void FThornCamera::Stop()
{
    G_GfCameraPool.Free(PoolSlot);
}

// FUN_00450870 -- OR EAX,-1 (shared by /OPT:ICF with every "-1" getter of the exe).
void* FThornCamera::GetEngineInstance()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
}

// ---------------------------------------------------------------------------------------
// Thorn_CameraReference -- every forwarder: +0x6c null -> FDUMP(0x100001, "Using invalid
// camera reference", ScriptCamera.cpp, line, Name) and -1; otherwise a tail JMP into the
// target's same facet slot with the target's facet as `this`.
// ---------------------------------------------------------------------------------------

// FUN_00450c60. Base ctor, vptrs, 0x00450cb7 +0x6c = 0.
FThornCameraReference::FThornCameraReference(Csys const& ScriptFrame, FGfScript* InOwner,
                                             FThornEntitySnap const* Desc)
    : FThornComponent(ScriptFrame, InOwner, Desc),
      Target(nullptr)
{
}

// LAB_00450dc0 -- identity slot 11 (+0x2c), line 0x226.
GENRESULT DACOM_API FThornCameraReference::SetTransform(float const* Transform12)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x226);
    return Target->SetTransform(Transform12);
}

// LAB_00450ea0 -- identity slot 12 (+0x30), line 0x24d.
GENRESULT DACOM_API FThornCameraReference::GetTransform(float* OutTransform12)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x24d);
    return Target->GetTransform(OutTransform12);
}

// LAB_00450d20 -- geo slot 3 (target + 0xc, +0xc), line 0x20c.
GENRESULT DACOM_API FThornCameraReference::SetPosition(float* Position)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x20c);
    return Target->SetPosition(Position);
}

// LAB_00450d70 -- geo slot 4, line 0x219.
GENRESULT DACOM_API FThornCameraReference::SetOrientation(float* Orient9)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x219);
    return Target->SetOrientation(Orient9);
}

// LAB_00450e00 -- geo slot 5, line 0x233.
GENRESULT DACOM_API FThornCameraReference::GetPosition(float* OutPosition)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x233);
    return Target->GetPosition(OutPosition);
}

// LAB_00450e50 -- geo slot 6, line 0x240.
GENRESULT DACOM_API FThornCameraReference::GetOrientation(float* OutOrient9)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x240);
    return Target->GetOrientation(OutOrient9);
}

// LAB_00450ee0 -- camera slot 3 (target + 0x10, +0xc), line 0x25a.
GENRESULT DACOM_API FThornCameraReference::SetFov(const float* Fov)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x25a);
    return Target->SetFov(Fov);
}

// LAB_00450f30 -- camera slot 6, line 0x267.
GENRESULT DACOM_API FThornCameraReference::SetNear(const float* Near)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x267);
    return Target->SetNear(Near);
}

// LAB_00450f80 -- camera slot 7, line 0x274.
GENRESULT DACOM_API FThornCameraReference::SetFar(const float* Far)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x274);
    return Target->SetFar(Far);
}

// LAB_00450fd0 -- camera slot 8, line 0x281.
GENRESULT DACOM_API FThornCameraReference::GetFov(float* OutFov)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x281);
    return Target->GetFov(OutFov);
}

// LAB_00451020 -- camera slot 11, line 0x28e.
GENRESULT DACOM_API FThornCameraReference::GetNear(float* OutNear)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x28e);
    return Target->GetNear(OutNear);
}

// LAB_00451070 -- camera slot 12, line 0x29b.
GENRESULT DACOM_API FThornCameraReference::GetFar(float* OutFar)
{
    if (Target == nullptr)
        return ReportInvalidReference(Name, 0x29b);
    return Target->GetFar(OutFar);
}

// LAB_00450cd0 -- +0x6c = 0 (shared with the light, marker and sound references).
void FThornCameraReference::Stop()
{
    Target = nullptr;
}

// LAB_00450ce0 -- the target's identity slot 17, or FDUMP line 0x1ff and -1.
void* FThornCameraReference::GetEngineInstance()
{
    if (Target == nullptr)
    {
        ReportInvalidReference(Name, 0x1ff);
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    return Target->GetEngineInstance();
}
