// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Effect base — Client/space/Effect.cpp (FUN_004f79a0 / 004f79f0 / 004f7a80 / 004f7d20,
// FUN_004f7bd0 kill, FUN_004f7db0 freeze-to-world, FUN_004f7ef0 world transform).
// Agent: cursor:equip-fx-factory.

#include "effect_base.h"

#include "beam_effect.h"
#include "effect_type_info.h"      // EffectTypeInfoByIndex (FUN_004fd560)
#include "effect_type_list.h"
#include "effect_vertex_batch.h"   // G_EffectMathEngine (DAT_00674f50)
#include "fl_fx_mesh.h"            // G_FLFxMeshFactory (DAT_00674cdc)
#include "space_camera.h"          // AsCamera (FUN_004f7b50)
#include "texture_shapes.h"        // G_EffectShapeDB (DAT_00674f24)

#include "DALib.h"
#include "alchemy.h"               // IFxMeshFactory (Release)
#include "common_cobject.h"        // CObject::get_velocity (vt+0x54)
#include "common_csys.h"           // Acquire3DMathEngine (FUN_005b73e0)
#include "common_misc.h"
#include "engine/engbase.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    // DAT_00674ccc — the Common.dll Heap pool for 0x84-byte effects, {ElementSize 0x84,
    // FreeList 0, MaxFreeCount -1, CurrentFreeCount 0}, stored by the static initializer
    // LAB_004f6fc0 (Heap ctor inlined there). FUN_004f79a0 / FUN_004f79c0 allocate and free
    // through it, FUN_004f7140 / FUN_004f8030 shrink and free into it.
    Heap G_EffectHeap(0x84u, -1);

    // DAT_00674cbc — a second {0x84, 0, -1, 0} Heap set up by the static initializer
    // LAB_004f6ff0. Nothing in the exe allocates from it (its only other reference is the Shrink
    // at 0x004f721c); kept because FUN_004f7140 shrinks it.
    Heap G_EffectHeapSecond(0x84u, -1);
}

void EffectSystemShutdown()
{
    // FUN_004f7140 (called from the client teardown at 0x005b228b).
    // 0x004f7143..0x004f71e9: every effect in every bucket is killed and released -- the same
    // loop as FUN_004f73c0.
    EffectTypeListKillAll();

    // 0x004f71ef..0x004f71ff: DAT_00674cdc (the FLFxMeshFactory FUN_004f70d0 creates) released
    // through its vt+8 and cleared.
    if (G_FLFxMeshFactory != nullptr)
    {
        G_FLFxMeshFactory->Release();
        G_FLFxMeshFactory = nullptr;
    }

    // 0x004f7205: FUN_00425a20 on DAT_00674f24.
    G_EffectShapeDB.Clear();

    // 0x004f720f: Heap::Shrink on DAT_00674ccc, then on DAT_00674cbc.
    G_EffectHeap.Shrink();
    G_EffectHeapSecond.Shrink();
}

void* EffectAlloc(unsigned Size)
{
    // FUN_004f79a0: size 0x84 → JMP Heap::Malloc(DAT_00674ccc); anything else → JMP malloc.
    if (Size != 0x84u)
        return malloc(Size);
    return G_EffectHeap.Malloc();
}

void EffectFree(void* Ptr, unsigned Size)
{
    // FUN_004f79c0: size 0x84 → Heap::Free(DAT_00674ccc, Ptr); anything else → free(Ptr).
    if (Size != 0x84u)
    {
        free(Ptr);
        return;
    }
    G_EffectHeap.Free(Ptr);
}

void FEffectBase::operator delete(void* Memory, size_t Size)
{
    EffectFree(Memory, (unsigned)Size);
}

FEffectBase::FEffectBase(unsigned TypeId)
{
    EffectBaseConstruct(this, TypeId);
}

void EffectBaseConstruct(FEffectBase* This, unsigned TypeId)
{
    // FUN_004f79f0 — listing dword writes through +0x68.
    if (This == nullptr)
        return;
    std::memset((unsigned char*)This + sizeof(void*), 0, sizeof(FEffectBase) - sizeof(void*));
    This->Priority0C = 1.0f;
    // Identity-ish transform diagonals (listing 0x3f800000 at [3]/[4]/[8]/[0xc]).
    This->LocalXform.Rotation.D[0][0] = 1.0f;
    This->LocalXform.Rotation.D[1][1] = 1.0f;
    This->LocalXform.Rotation.D[2][2] = 1.0f;
    This->EngineIndex = -1;
    This->RefCount = 1;
    This->EffectTypeId = TypeId;
    This->LinkPrev = nullptr;
    This->LinkNext = nullptr;
    This->LinkSelf = nullptr;
}

void FEffectBase::AddRef()
{
    FEffectBase* const This = this;
    // FUN_004f7a80: MOV EAX,[ECX+0x58] / INC / MOV [ECX+0x58],EAX.
    This->RefCount = This->RefCount + 1;
}

void EffectAddRef(FEffectBase* This)
{
    This->AddRef();
}

int FEffectBase::Release()
{
    FEffectBase* const This = this;
    // FUN_004f7a90: DEC [+0x58]; JG keeps it; otherwise `PUSH 1; CALL [vtable+0x20]`.
    int const Next = static_cast<int>(This->RefCount) - 1;
    This->RefCount = static_cast<unsigned>(Next);
    if (Next < 1)
    {
        delete This;
    }
    return Next;
}

int EffectRelease(FEffectBase* This)
{
    return This->Release();
}

unsigned char FEffectBase::Start(FEffectArch* Arch)
{
    FEffectBase* const This = this;
    // LAB_004f7ab0.
    if (Arch != nullptr)
    {
        // 0x004f7abb..0x004f7ac4: `TEST ECX,ECX / JG` on the type id -- signed.
        if (static_cast<int>(This->EffectTypeId) <= 0)
            This->EffectTypeId = *(unsigned const*)Arch;
        // 0x004f7ac7..0x004f7ada: FUN_004fd560 with retail's own null test.
        FEffectTypeInfo const* const Info =
            EffectTypeInfoByIndex(static_cast<int>(This->EffectTypeId));
        if (Info != nullptr)
            This->Priority0C = Info->Priority;
    }
    return 1;
}

unsigned char EffectBaseStart(FEffectBase* This, FEffectArch* Arch)
{
    return This->FEffectBase::Start(Arch);
}

void EffectBaseDestruct(FEffectBase* This)
{
    // 0x004f8dd6..0x004f8de2: only a non-empty watcher is reset (set_pointer(0)); set() makes
    // the same call once it sees the target differs.
    if (This->Watcher.target() != nullptr)
        This->Watcher.set(nullptr);
}

FEffectBase::~FEffectBase()
{
    EffectBaseDestruct(this);
}

void FEffectBase::SetIntensity(float /*Intensity*/)
{
    // Former slot +0x0c: an empty RET 4.
}

void FEffectBase::Attach(void* WatchableOwner, int EngineIndex, FTransform const* LocalXform)
{
    FEffectBase* const This = this;
    // FUN_004f7d20 (thiscall, RET 0xc). No null tests: the transform is copied with
    // REP MOVSD straight from the third argument.
    Watchable const* WatchablePtr = nullptr;
    if (WatchableOwner != nullptr)
        WatchablePtr =
            reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(WatchableOwner) + 8);
    if (WatchablePtr != This->Watcher.target())
        This->Watcher.set(WatchablePtr);
    This->EngineIndex = EngineIndex;
    This->LocalXform = *LocalXform;
    // +0x4c/+0x50/+0x54 zeroed as three dwords (listing FUN_004f7d20).
    std::memset(This->Pad4C, 0, sizeof(This->Pad4C));
}

void EffectAttach(FEffectBase* This, void* WatchableOwner, int EngineIndex,
                  FTransform const* LocalXform)
{
    This->Attach(WatchableOwner, EngineIndex, LocalXform);
}

void* EffectWatchedObject(FEffectBase const* This)
{
    // `MOV EAX,[this+0x44] / TEST / ADD EAX,-8 / TEST`: both an empty watcher and one holding
    // exactly 8 read as "no object".
    unsigned char const* const Held =
        reinterpret_cast<unsigned char const*>(This->Watcher.target());
    if (Held == nullptr)
        return nullptr;
    return const_cast<unsigned char*>(Held - 8);
}

namespace
{
    // DAT_00674f50, lazily filled by FUN_005b73e0 at every use site (0x004f7cbe, 0x004f7e9e,
    // 0x004f7f29, 0x004f863b).
    I3DMathEngine* EffectMathEngine()
    {
        if (G_EffectMathEngine == nullptr)
            G_EffectMathEngine = Acquire3DMathEngine();
        return G_EffectMathEngine;
    }

    // The body FUN_004f7db0 runs once it knows the index is set and the watcher holds an
    // object; FUN_004f7bd0 (0x004f7c4a..0x004f7d03) and LAB_004f84d0 (0x004f85c7..0x004f867f)
    // carry the same instructions inlined.
    void EffectBakeInstance(FEffectBase* This)
    {
        void* const Instance = reinterpret_cast<void*>(static_cast<intptr_t>(This->EngineIndex));

        // IEngine +0x80, then CObject vt+0x54 into a stack Vector, copied to +0x4c.
        float* const Velocity = reinterpret_cast<float*>(This->Pad4C);
        CObject* const Object = static_cast<CObject*>(DALib::Engine->GetInstanceUserData(Instance));
        if (Object != nullptr)
        {
            Vector const V = Object->get_velocity();
            Velocity[0] = V.x;
            Velocity[1] = V.y;
            Velocity[2] = V.z;
        }
        else
        {
            Velocity[2] = 0.0f;   // +0x54, +0x50, +0x4c: retail's store order
            Velocity[1] = 0.0f;
            Velocity[0] = 0.0f;
        }

        // The watcher and the index are tested again (the object may have been the one the
        // velocity call just touched); only then is the instance transform multiplied in.
        FTransform Product;
        FTransform const* Source = &This->LocalXform;
        if (EffectWatchedObject(This) != nullptr && This->EngineIndex != -1)
        {
            FTransform const* const InstanceXform = reinterpret_cast<FTransform const*>(
                DALib::Engine->GetInstanceTransform(
                    reinterpret_cast<void*>(static_cast<intptr_t>(This->EngineIndex))));
            EffectMathEngine()->MulTransform(Product, *InstanceXform, This->LocalXform);
            Source = &Product;
        }

        // Two 0xc-dword copies through a stack temporary: source -> temp -> +0x10.
        FTransform const Temp = *Source;
        This->LocalXform = Temp;
        This->EngineIndex = -1;
    }
}

void EffectFreezeToWorld(FEffectBase* This)
{
    // FUN_004f7db0.
    if (This->EngineIndex == -1)
        return;

    if (EffectWatchedObject(This) == nullptr)
    {
        // 0x004f7dd4: FUN_004f7af0 inlined (link unless linked or dead, RefCount++).
        EffectTypeListLink(This);
        // 0x004f7e17: `CALL FUN_004f7db0` on the same object. The index is still set and the
        // watcher still empty, so retail recurses until the stack runs out if this is reached;
        // reproduced as written.
        EffectFreezeToWorld(This);
        This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
        return;
    }

    EffectBakeInstance(This);
}

void FEffectBase::Kill()
{
    FEffectBase* const This = this;
    // FUN_004f7bd0.
    EffectTypeListLink(This);      // 0x004f7bd6..0x004f7c18 inlined

    if (This->EngineIndex != -1)
    {
        if (EffectWatchedObject(This) == nullptr)
        {
            // 0x004f7c37: `CALL FUN_004f7bd0` on itself, then the flag. Same unbounded recursion
            // as FUN_004f7db0's; reproduced as written.
            EffectKill(This);
            This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);
            return;
        }
        EffectBakeInstance(This);
    }

    This->Flags08 = static_cast<unsigned char>(This->Flags08 | 2u);   // 0x004f7d04
}

void EffectKill(FEffectBase* This)
{
    This->FEffectBase::Kill();
}

void EffectGetWorldTransform(FEffectBase const* This, FTransform* Out)
{
    // FUN_004f7ef0.
    if (EffectWatchedObject(This) != nullptr && This->EngineIndex != -1)
    {
        FTransform const* const InstanceXform = reinterpret_cast<FTransform const*>(
            DALib::Engine->GetInstanceTransform(
                reinterpret_cast<void*>(static_cast<intptr_t>(This->EngineIndex))));
        FTransform Product;
        EffectMathEngine()->MulTransform(Product, *InstanceXform, This->LocalXform);
        *Out = Product;
        return;
    }
    *Out = This->LocalXform;
}

unsigned char EffectInCameraView(FEffectBase* This, FSpaceCamera* Cam)
{
    // FUN_004f7b50 (RET 4). 0x004f7b53..0x004f7b5f: the effect's own slot +8 -- world
    // position into a local, radius left on the FPU and stored as a float.
    FVector WorldPos;
    float const Radius = This->GetWorldPos(&WorldPos);

    // 0x004f7b63..0x004f7b90: the camera's slot +0x40 (view transform), then I3DMathEngine
    // +0x28 (DAT_00674f50, lazily FUN_005b73e0) into a second local.
    ICamera* const CamIface = AsCamera(Cam);
    FTransform const* const ViewXform =
        reinterpret_cast<FTransform const*>(CamIface->GetViewTransform());
    FVector ViewPos;
    EffectMathEngine()->TransformXform(ViewPos, *ViewXform, WorldPos);

    // 0x004f7b93..0x004f7bc4: copied into a third local, the camera's slot +0x3c with it and
    // the radius; CMP EAX,3 / SETGE.
    FVector Centre = ViewPos;
    int const Class = CamIface->ClassifySphere(&Centre.X, Radius);
    return static_cast<unsigned char>(Class >= 3 ? 1 : 0);
}
