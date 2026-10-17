// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll hardpoint lookups: FindHardpoint / FindHardpoint_OS / FindHardpoint_PS,
// HardpointToWorld and HardpointSummary. Split out of common_goods.cpp.

#include "common_goods.h"
#include "common_cobject.h" // CompoundInstanceAnalyze
#include "common_misc.h"
#include "DALib.h"
#include "x86math.h" // I3DMathEngine::MulTransform (ComposeTransform)

#include <cstring>
#include <new>

// common_dacom_stricmp.cpp: DACOM.DLL!stricmp (Common IAT PTR 0639902c).
int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    // The engine handle sentinel the whole hardpoint block compares against.
    long const kNoEngineHandle = -1;

    // FUN_0630e870 — the search shared by FindHardpoint and FindHardpoint_OS. Returns the
    // handle it matched on, which for a child hit is the child's, not the caller's.
    bool FindHardpointCore(long Handle, char const* Name, long* OutHandle, HardpointInfo* OutInfo)
    {
        // Retail FUN_0630e870 (Common.dll.txt 310643+): loads DALib::Engine and immediately
        // dereferences — no null checks on Engine, Hardpoint, Name, or the out pointers.
        // Callers in src/ (CEEngine nozzles, CETractor HpTractorSource) always pass stack
        // OutHandle/OutInfo and a non-null Name (CacheString / null-checked HpTractorSource);
        // Engine/Hardpoint are live once any CObject has an EngineInstanceId.
        IEngine* const Engine = DALib::Engine;

        // The archetype (part) handle of the instance; -1 when there is none.
        void* const Part = Engine->GetInstanceArchetype((void*)Handle);
        if ((long)Part == kNoEngineHandle)
            return false;

        // 0630e89e writes the name, 0630e8b0 the CRC just above it -- FHardpointKey.
        FHardpointKey Query;
        Query.Name = Name;
        Query.Crc = DACOM_CRC::GetCRC32(Name);

        bool const bFound = DALib::Hardpoint->GetHardpoint(Part, &Query, OutInfo) != 0;

        // 0630e8d4: the part is released whether or not the name matched.
        Engine->ReleaseHandle(Part);

        if (bFound)
        {
            *OutHandle = Handle;
            return true;
        }

        long Child = kNoEngineHandle;
        while (true)
        {
            Child = (long)Engine->GetRelatedInstance((void*)Handle, 1, (void*)Child);
            if (Child == kNoEngineHandle)
                return false;
            if (FindHardpointCore(Child, Name, OutHandle, OutInfo))
                return true;
        }
    }

    // Engine slot 40 (+0xa0) hands back the instance's transform: a 3x3 followed by a vector,
    // which is exactly the port's Transform.
    Transform const* EngineGetTransform(long Handle)
    {
        // Retail FindHardpoint_PS (0630e9c6) loads Engine and calls +0xa0 with no null gate.
        IEngine* const Engine = DALib::Engine;
        return (Transform const*)Engine->GetInstanceTransform((void*)Handle);
    }

    // 3DMathEngine slot 7 (+0x1c, MulTransform): Out = Parent * Local - what retail's
    // hardpoint functions call (0630e9fe, 0630eb19, 0630ec0c). Transform and FTransform have
    // the same layout.
    void ComposeTransform(Transform& Out, Transform const& Parent, Transform const& Local)
    {
        Acquire3DMathEngine()->MulTransform((FTransform&)Out, (FTransform const&)Parent,
                                            (FTransform const&)Local);
    }
}

bool __cdecl FindHardpoint(long Handle, char const* Name, long& OutHandle)
{
    // 0630e940-0630e95f: FUN_0630e870 with a 0x48-byte uninitialised stack scratch for the
    // info, which is thrown away.
    HardpointInfo Scratch;
    return FindHardpointCore(Handle, Name, &OutHandle, &Scratch);
}

bool __cdecl FindHardpoint_OS(long Handle, char const* Name, long& OutHandle,
                              HardpointInfo& OutInfo)
{
    // 0xaeb70 — the core, unmodified.
    return FindHardpointCore(Handle, Name, &OutHandle, &OutInfo);
}

bool __cdecl FindHardpoint_PS(long Handle, char const* Name, long& OutHandle,
                              HardpointInfo& OutInfo)
{
    // 0630e960-0630eb61. FUN_0630e870 finds the hardpoint (a miss is false); when it sits on
    // Handle itself the object-space info is returned as is. Otherwise the hardpoint frame L
    // is lifted to world through the found instance's transform (Engine +0xa0, MulTransform:
    // W = T_found * L, written back into OutInfo and into L), then brought into Handle's
    // space with the inverse of Handle's transform, built by hand: v = TransposeTransform
    // (+0x38) of T_handle's rotation applied to its translation, Inv = {transpose of the
    // rotation (FUN_06284f70 fed column-wise), -v (FUN_0628b030)} (FUN_0630f140), and
    // OutInfo = Inv * W (MulTransform). No null checks of either engine transform.
    if (!FindHardpointCore(Handle, Name, &OutHandle, &OutInfo))
        return false;
    if (OutHandle == Handle)
        return true;

    Transform Local;
    Local.Rotation = OutInfo.Orient;
    Local.Translation = OutInfo.Position;

    Transform const* const Found = EngineGetTransform(OutHandle);
    Transform World;
    ComposeTransform(World, *Found, Local);
    OutInfo.Position = World.Translation;
    OutInfo.Orient = World.Rotation;
    Local = World;

    Transform const* const Space = EngineGetTransform(Handle);
    FVector Rotated;
    Acquire3DMathEngine()->TransposeTransform(Rotated, (FMatrix const&)Space->Rotation,
                                              (FVector const&)Space->Translation);
    Vector NegRotated;
    Vec3Assign(&NegRotated, -Rotated.X, -Rotated.Y, -Rotated.Z);
    Matrix const& R = Space->Rotation;
    Matrix const Transposed(R.E[0][0], R.E[1][0], R.E[2][0],
                            R.E[0][1], R.E[1][1], R.E[2][1],
                            R.E[0][2], R.E[1][2], R.E[2][2]);
    Transform Inverse;
    Inverse.Rotation = Transposed;
    Inverse.Translation = NegRotated;

    Transform Result;
    ComposeTransform(Result, Inverse, Local);
    OutInfo.Position = Result.Translation;
    OutInfo.Orient = Result.Rotation;
    return true;
}

void __cdecl HardpointToWorld(long Handle, HardpointInfo& Info)
{
    // 0630eba0-0630ec36: L = {Info.Orient, Info.Position}; T = Engine +0xa0 (no null check);
    // W = T * L (3DMathEngine +0x1c); Info.Position = W.Translation, Info.Orient = W.Rotation.
    Transform const* const Parent = EngineGetTransform(Handle);

    Transform Local;
    Local.Rotation = Info.Orient;
    Local.Translation = Info.Position;

    Transform World;
    ComposeTransform(World, *Parent, Local);
    Info.Position = World.Translation;
    Info.Orient = World.Rotation;
}

// ---------------------------------------------------------------------------
// HardpointSummary
// ---------------------------------------------------------------------------

// 063345c0: vftable, +4..+0x10 = 0, +0x14/+0x18 = -1, +0x1c = 0.
HardpointSummary::HardpointSummary()
    : Count(0)
    , Names(nullptr)
    , Infos(nullptr)
    , Instances(nullptr)
    , FillInstance(-1)
    , FillPart((void*)-1)
    , FillCursor(0)
{
}

// 06273150: dwords +4..+0x1c copied as they are (the three array pointers are shared, so
// destroying both copies frees them twice -- retail's own hazard), then the vftable.
HardpointSummary::HardpointSummary(HardpointSummary const& Other)
    : Count(Other.Count)
    , Names(Other.Names)
    , Infos(Other.Infos)
    , Instances(Other.Instances)
    , FillInstance(Other.FillInstance)
    , FillPart(Other.FillPart)
    , FillCursor(Other.FillCursor)
{
}

// 063345f0: vftable store, then destroy()'s body inlined.
HardpointSummary::~HardpointSummary()
{
    destroy();
}

// 06273190: dwords +4..+0x1c copied (no vftable store, no self test, nothing freed).
HardpointSummary& HardpointSummary::operator=(HardpointSummary const& Other)
{
    Count = Other.Count;
    Names = Other.Names;
    Infos = Other.Infos;
    Instances = Other.Instances;
    FillInstance = Other.FillInstance;
    FillPart = Other.FillPart;
    FillCursor = Other.FillCursor;
    return *this;
}

// 06334660: +4..+0x10 = 0, +0x14/+0x18 = -1, +0x1c = 0 (nothing freed).
void HardpointSummary::clear()
{
    Count = 0;
    Names = nullptr;
    Infos = nullptr;
    Instances = nullptr;
    FillInstance = -1;
    FillPart = (void*)-1;
    FillCursor = 0;
}

// 06334680: malloc_strfree on each of the Count names, operator delete on the three arrays
// (no null tests -- delete of null is a no-op, and Count > 0 implies the name array exists),
// then the clear() stores.
void HardpointSummary::destroy()
{
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        malloc_strfree(const_cast<char**>(&Names[Index]));
    delete[] Names;
    Names = nullptr;
    operator delete(Infos);
    Infos = nullptr;
    delete[] Instances;
    clear();
}

int HardpointSummary::get_count() const
{
    return Count;
}

// 063346f0: Names[Index], unchecked.
char const* HardpointSummary::get_name(int Index) const
{
    return Names[Index];
}

// 06334700: Infos + Index * 0x48, unchecked.
HardpointInfo const& HardpointSummary::get_info(int Index) const
{
    return Infos[Index];
}

// 06334710: Instances[Index], unchecked.
long HardpointSummary::get_instance(int Index) const
{
    return Instances[Index];
}

// 06334720: whole scan with MSVCRT _stricmp, the LAST match wins; -1 when none.
int HardpointSummary::find_name(char const* Name) const
{
    int Result = -1;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        if (_stricmp(Names[Index], Name) == 0)
            Result = Index;
    }
    return Result;
}

// 06334770: the same scan (MSVCRT _stricmp, last match wins); Instances[match] or -1.
long HardpointSummary::find_name_instance(char const* Name) const
{
    int Result = -1;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        if (_stricmp(Names[Index], Name) == 0)
            Result = Index;
    }
    if (Result == -1)
        return -1;
    return Instances[Result];
}

// 063347c0: the same scan with DACOM.DLL!stricmp; &Infos[last match] or null.
HardpointInfo const* HardpointSummary::get_info_by_name(char const* Name) const
{
    HardpointInfo const* Result = nullptr;
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        if (CallDacomStricmp(Names[Index], Name) == 0)
            Result = &Infos[Index];
    }
    return Result;
}

// 06334810: ++Context->Count.
void HardpointSummary::hardpoint_callback1(char const* Name, void* Context)
{
    (void)Name;
    HardpointSummary* const Self = (HardpointSummary*)Context;
    Self->Count++;
}

// 06334820: Part = DALib::Engine slot 25 (+0x64) on the instance; DALib::Hardpoint slot 5
// (+0x14) enumerates the part's names into hardpoint_callback1; Engine slot 15 (+0x3c)
// releases the part. No null tests.
void HardpointSummary::count_hardpoints_callback(long Instance, void* Context)
{
    void* const Part = DALib::Engine->GetInstanceArchetype((void*)Instance);
    DALib::Hardpoint->EnumerateHardpoints(&hardpoint_callback1, Part, Context);
    DALib::Engine->ReleaseHandle(Part);
}

// 06334860: Count = 0, then CompoundInstanceAnalyze with count_hardpoints_callback.
void HardpointSummary::count_hardpoints(long Instance)
{
    Count = 0;
    CompoundInstanceAnalyze(Instance, count_hardpoints_callback, this);
}

// 06334880: malloc_strcpy(&Names[FillCursor], Name); {Name, CRC32(Name)} looked up with
// DALib::Hardpoint slot 3 (+0x0c) on FillPart into a local 0x48-byte blob, which is copied
// (0x12 dwords) into Infos[FillCursor] only when the low byte of the result is non-zero
// (TEST AL,AL); Instances[FillCursor] = FillInstance; ++FillCursor. No bounds or null tests.
void HardpointSummary::hardpoint_callback2(char const* Name, void* Context)
{
    HardpointSummary* const Self = (HardpointSummary*)Context;
    malloc_strcpy(const_cast<char**>(&Self->Names[Self->FillCursor]), Name);

    FHardpointKey Query;
    Query.Name = Name;
    Query.Crc = DACOM_CRC::GetCRC32(Name);
    HardpointInfo Found;
    unsigned long const Hit = DALib::Hardpoint->GetHardpoint(Self->FillPart, &Query, &Found);
    if ((unsigned char)Hit != 0)
        memcpy(&Self->Infos[Self->FillCursor], &Found, sizeof(HardpointInfo));

    Self->Instances[Self->FillCursor] = Self->FillInstance;
    Self->FillCursor++;
}

// 06334900: Part = Engine slot 25 on the instance; FillInstance = instance, FillPart = part;
// Hardpoint slot 5 enumerates the part's names into hardpoint_callback2; Engine slot 15
// releases the part. No null tests.
void HardpointSummary::fill_hardpoints_callback(long Instance, void* Context)
{
    void* const Part = DALib::Engine->GetInstanceArchetype((void*)Instance);
    HardpointSummary* const Self = (HardpointSummary*)Context;
    Self->FillInstance = Instance;
    Self->FillPart = Part;
    DALib::Hardpoint->EnumerateHardpoints(&hardpoint_callback2, Part, Context);
    DALib::Engine->ReleaseHandle(Part);
}

// 06334950: FillCursor = 0, then CompoundInstanceAnalyze with fill_hardpoints_callback.
void HardpointSummary::fill_hardpoints(long Instance)
{
    FillCursor = 0;
    CompoundInstanceAnalyze(Instance, fill_hardpoints_callback, this);
}

// 06334970: destroy() inlined; Instance == -1 stops there. Otherwise count_hardpoints and
// fill_hardpoints inlined around three operator new arrays (Count*4, Count*0x48, Count*4;
// Infos left uninitialised), Names[i] = 0 and Instances[i] = -1 for every slot. Both paths end
// with FillCursor = 0, FillInstance = FillPart = -1 and return Count. The Infos block stays
// a raw allocation: retail leaves it uninitialised.
int HardpointSummary::analyze_instance(long Instance)
{
    destroy();

    if (Instance != -1)
    {
        count_hardpoints(Instance);

        Names = new char const*[Count];
        Infos = (HardpointInfo*)operator new((size_t)Count * sizeof(HardpointInfo));
        Instances = new long[Count];

        int Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            Names[Index] = nullptr;
            Instances[Index] = -1;
        }

        fill_hardpoints(Instance);
    }

    FillCursor = 0;
    FillInstance = -1;
    FillPart = (void*)-1;
    return Count;
}

// 06334a60: Instances[i] = -1 for every slot.
void HardpointSummary::expire_instance()
{
    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
        Instances[Index] = -1;
}
