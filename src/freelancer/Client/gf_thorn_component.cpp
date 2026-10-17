// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "gf_thorn_component.h"

#include "DALib.h"
#include "common_goods.h"   // HardpointSummary
#include "common_misc.h"    // malloc_strcpy / malloc_strfree
#include "gf_script_object.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace
{
    char const* const GScriptBaseFile = "E:\\FL\\Scratch\\Source\\Client\\Gf\\ScriptBase.cpp";

    // The QueryInterface table at 0x005cca20: {IID, facet offset}, null-terminated. An offset
    // with the high bit set would name a field holding the facet pointer (LAB_004500d0
    // 0x00450141 JNS); none of retail's nine entries uses it.
    struct FThornFacetEntry
    {
        char const* Iid;
        unsigned long Offset;
    };

    FThornFacetEntry const G_ThornFacetTable[] = {
        {Iids::CompoundControl, 0x00},     {Iids::SoundControl, 0x04},
        {Iids::IKControl, 0x08},           {Iids::GeoTransformControl, 0x0c},
        {Iids::CameraControl, 0x10},       {Iids::D3DLightControl, 0x14},
        {Iids::PSysControl, 0x18},         {Iids::FogControl, 0x1c},
        {Iids::ReverbControl, 0x20},       {nullptr, 0},
    };

    long InstanceHandle(void* Instance)
    {
        return static_cast<long>(reinterpret_cast<intptr_t>(Instance));
    }

    void* InstancePointer(long Instance)
    {
        return reinterpret_cast<void*>(static_cast<intptr_t>(Instance));
    }
}

// FUN_0044fbe0. 0x0044fc25..0x0044fca0: +0x24 -1, +0x28 1, the nine vptrs, 0xc dwords of the
// frame to +0x2c, +0x5c Owner, +0x60 Desc+0x00, malloc_strcpy(+0x64, Desc+0x38), +0x68 the low
// bit of Desc+0x04. Name is not cleared first: malloc_strcpy overwrites it.
FThornComponent::FThornComponent(Csys const& ScriptFrame, FGfScript* InOwner,
                                 FThornEntitySnap const* Desc)
    : EntityIndex(-1),
      RefCount(1),
      Frame(ScriptFrame),
      Owner(InOwner),
      Type(Desc->Type)
{
    malloc_strcpy(&Name, Desc->Name);
    bReference = static_cast<unsigned char>(Desc->Flags & 1);
}

// FUN_0044fd80 / FUN_0044fd10.
FThornComponent::~FThornComponent()
{
    malloc_strfree(&Name);
}

// LAB_004500d0 (RET 0xc).
GENRESULT DACOM_API FThornComponent::QueryInterface(const char* Iid, void** Out)
{
    unsigned char* Base = reinterpret_cast<unsigned char*>(static_cast<ICompoundControl*>(this));
    int Index = 0;
    for (; G_ThornFacetTable[Index].Iid != nullptr; ++Index)
    {
        if (std::strcmp(G_ThornFacetTable[Index].Iid, Iid) != 0)
            continue;

        unsigned long const Offset = G_ThornFacetTable[Index].Offset;
        IDAComponent* Facet = nullptr;
        if ((Offset & 0x80000000u) != 0)
            Facet = *reinterpret_cast<IDAComponent**>(Base + (Offset & 0x7fffffffu));
        else
            Facet = reinterpret_cast<IDAComponent*>(Base + Offset);
        Facet->AddRef();
        *Out = Facet;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// LAB_00450180.
unsigned long DACOM_API FThornComponent::AddRef()
{
    ++RefCount;
    return RefCount;
}

// LAB_00450190. 0x00450197 `TEST EAX,EAX; JBE` -- a count of 0 is not decremented. At zero the
// count goes back to 1 and FUN_005b7e1d (operator delete) frees the block: the destructor does
// NOT run, so the name copy (and a derived class's resources) is never released on this path.
// The scalar deleting destructor (slot 13) is the path that does run it.
unsigned long DACOM_API FThornComponent::Release()
{
    if (RefCount > 0)
        --RefCount;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    ::operator delete(static_cast<void*>(this));
    return 0;
}

// --- defaults -------------------------------------------------------------------------------
// LAB_00454190: MOV EAX,0xfffffff9; RET 8. LAB_0044fcc0 / fcd0 / fce0 / fcf0: the same answer
// with RET 4 / 0x10 / 0xc / 0x14. LAB_00451370: XOR EAX,EAX; RET 8.

GENRESULT DACOM_API FThornComponent::StartMotion(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::StopMotion(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetFloorHeight(float) { return GR_OK; }
GENRESULT DACOM_API FThornComponent::AdvanceTime(int) { return GR_OK; }
GENRESULT DACOM_API FThornComponent::ConnectHardpoints(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::DisconnectHardpoints(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetHardpointXform(int, char const*, float*)
{
    return kThornSlotNotImplemented;
}

// LAB_0044fcb0: *(dword*)Out = 0; RET 8.
GENRESULT DACOM_API FThornComponent::GetFloorHeight(float* OutHeight)
{
    *OutHeight = 0.0f;
    return GR_OK;
}

GENRESULT DACOM_API FThornComponent::SetTransform(float const*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetTransform(float*) { return kThornSlotNotImplemented; }

GENRESULT DACOM_API FThornComponent::StartSound(void*, int) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::StopSound(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetAttenuation(float) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetPan(float) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetAttenuation(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetPan(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetAudioProps(float const*, int) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetAudioProps(float*, int) { return kThornSlotNotImplemented; }

GENRESULT DACOM_API FThornComponent::StartIk(void*, float*, float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::StopIk(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::UpdateIk(void*, float*, float*) { return kThornSlotNotImplemented; }

GENRESULT DACOM_API FThornComponent::SetPosition(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetOrientation(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetPosition(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetOrientation(float*) { return kThornSlotNotImplemented; }

GENRESULT DACOM_API FThornComponent::SetFov(const float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::CameraSlot4(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetAspect(const float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetNear(const float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetFar(const float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetFov(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::CameraSlot9(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetAspect(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetNear(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetFar(float*) { return kThornSlotNotImplemented; }

GENRESULT DACOM_API FThornComponent::SetOn(int) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetLight(void const*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetDirection(float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetOn(int*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetLight(void*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetDirection(float*) { return kThornSlotNotImplemented; }

GENRESULT DACOM_API FThornComponent::Start() { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::StopEffect() { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::Tick(int) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetSparam(float) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetSparam(float*) { return kThornSlotNotImplemented; }

GENRESULT DACOM_API FThornComponent::SetOn(unsigned char) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetOn(unsigned char*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetColor(int const*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetColor(int*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetRange(float, float, float) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetRangeValue(const float*, const float*, const float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetRange(float*, float*, float*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetTableMode(unsigned char, unsigned int)
{
    return kThornSlotNotImplemented;
}
GENRESULT DACOM_API FThornComponent::GetTableMode(unsigned char*, unsigned int*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::SetPacked(unsigned char const*) { return kThornSlotNotImplemented; }
GENRESULT DACOM_API FThornComponent::GetPacked(unsigned char*) { return kThornSlotNotImplemented; }

GENRESULT DACOM_API FThornComponent::SetReverb(unsigned int, float, float, float)
{
    return kThornSlotNotImplemented;
}
GENRESULT DACOM_API FThornComponent::GetReverb(float*, float*, float*, float*)
{
    return kThornSlotNotImplemented;
}

// --- the identity tail's base bodies --------------------------------------------------------
void FThornComponent::Stop() {}
void FThornComponent::SetSpawnTable(FThornComponent**, int) {}
bool FThornComponent::FindActor(char const*, void**, void**) { return false; }
void FThornComponent::DrawEffects(ICamera*) {}
void FThornComponent::SetScene(void*) {}
void FThornComponent::SetFlags(unsigned char, unsigned char, unsigned char, unsigned char) {}
void FThornComponent::RefreshSound() {}
bool FThornComponent::IsBackgroundSound() { return false; }

// FUN_00450070. 0x00450070..0x004500be: a reference entity never matches; otherwise the type must
// be equal and the name equal under the inlined exact strcmp.
bool FThornComponent::MatchesEntity(int InType, char const* InName) const
{
    if (bReference != 0)
        return false;
    if (Type != InType)
        return false;
    return std::strcmp(Name, InName) == 0;
}

// FUN_0044fdd0. The two summaries are locals with destructors (EH states 0 / 1, unwind
// LAB_005b9de2), built A then B; both are analyzed, keyed and searched A then B, and destroy()ed
// A then B before the destructors run (B, then A).
void FThornComponent::ConnectEntityHardpoints(int IndexA, char const* HpA, int IndexB,
                                              char const* HpB)
{
    FThornComponent* EntityA = Owner->Spawned[IndexA];
    FThornComponent* EntityB = Owner->Spawned[IndexB];
    if (EntityA == nullptr || EntityB == nullptr)
        return;

    HardpointSummary SummaryA;
    HardpointSummary SummaryB;
    SummaryA.analyze_instance(InstanceHandle(EntityA->GetEngineInstance()));
    SummaryB.analyze_instance(InstanceHandle(EntityB->GetEngineInstance()));

    FHardpointKey KeyA;
    KeyA.Name = HpA;
    KeyA.Crc = DACOM_CRC::GetCRC32(HpA);
    FHardpointKey KeyB;
    KeyB.Name = HpB;
    KeyB.Crc = DACOM_CRC::GetCRC32(HpB);

    long const InstanceA = SummaryA.find_name_instance(HpA);
    long const InstanceB = SummaryB.find_name_instance(HpB);
    if (DALib::Hardpoint->ConnectHardpoints(InstancePointer(InstanceB), &KeyB,
                                            InstancePointer(InstanceA), &KeyA) != 0)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Cannot connect hardpoint %s on entity %s to hardpoint %s on "
              "entity %s",
              GScriptBaseFile, 0x5e, HpB, EntityB->Name, HpA, EntityA->Name);
    }

    SummaryA.destroy();
    SummaryB.destroy();
}

// FUN_0044ff30. Same shape; the joint goes away through DALib::Engine slot 48 (+0xc0) with
// (instance B, instance A), and anything but 1 is reported.
void FThornComponent::DisconnectEntityHardpoints(int IndexA, char const* HpA, int IndexB,
                                                 char const* HpB)
{
    FThornComponent* EntityA = Owner->Spawned[IndexA];
    FThornComponent* EntityB = Owner->Spawned[IndexB];
    if (EntityA == nullptr || EntityB == nullptr)
        return;

    HardpointSummary SummaryA;
    HardpointSummary SummaryB;
    SummaryA.analyze_instance(InstanceHandle(EntityA->GetEngineInstance()));
    SummaryB.analyze_instance(InstanceHandle(EntityB->GetEngineInstance()));

    long const InstanceA = SummaryA.find_name_instance(HpA);
    long const InstanceB = SummaryB.find_name_instance(HpB);
    if (DALib::Engine->DestroyJoint(InstancePointer(InstanceB), InstancePointer(InstanceA)) != 1)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Cannot disconnect hardpoint %s on entity %s from hardpoint %s "
              "on entity %s",
              GScriptBaseFile, 0x81, HpB, EntityB->Name, HpA, EntityA->Name);
    }

    SummaryA.destroy();
    SummaryB.destroy();
}
