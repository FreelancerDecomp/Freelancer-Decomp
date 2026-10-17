// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CArchGroup, CArchGroupManager and CArchGrpTraverser (retail CArchGroupManager.cpp).
#include "common_cobject.h"
#include "x86math.h"

#include "common_id.h"
#include "common_physys.h"
#include "common_misc.h" // DamageList, DamageEntry

#include "DALib.h"
#include "dacom.h"

#include <cmath>
#include <list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const char kCArchGroupSource[] = "E:\\FL\\Scratch\\Source\\Common\\CArchGroupManager.cpp";
}

CArchGroup::CArchGroup(Archetype::CollisionGroup const& GroupArch, CEqObj* const OwnerObj)
{
    Owner = OwnerObj;
    Arch = &GroupArch;
    HitPts = (float)GroupArch.HitPts;
    Field10 = 1;
    State = 1;
    Fate = 0;
    DestroyedObj = nullptr;
    // Listing 062a0192-062a01ab: byte +0x20 = 0, dwords +0x24/+0x28/+0x2c/+0x30 = 0; the
    // padding +0x21..+0x23 is left alone.
    bBoundsValid = false;
    BoundRadius = 0.0f;
    BoundCenter.x = 0.0f;
    BoundCenter.y = 0.0f;
    BoundCenter.z = 0.0f;

    // The root instance of the group: the child of the owner's instance named by Arch's "obj".
    RootIndex = FindChild(OwnerObj->EngineInstanceId, GroupArch.ObjName);

    // .text:062a01cf: the ctor ends with LoadDamageObjs().
    LoadDamageObjs();
}

CArchGroup::~CArchGroup()
{
    // .text:062a01e0: CObject::Release on DestroyedObj (+0x1c) when set, then a tail JMP to
    // FreeDamageObjs.
    if (DestroyedObj != nullptr)
        DestroyedObj->Release();
    FreeDamageObjs();
}

void CArchGroup::LoadDamageObjs()
{
    // .text:062a0e00. Walks the parent list (Arch+0x40) and then the group list (Arch+0x4c);
    // each entry's archetype found by GetSimple is load()ed. The list heads are not tested
    // (the CollisionGroup ctor always allocates them).
    std::vector<Archetype::DamageObjInfo>::const_iterator It = Arch->DmgObjs.Items->begin();
    for (; It != Arch->DmgObjs.Items->end(); ++It)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(It->Arch.Id);
        if (Simple != nullptr)
            Simple->load();
    }
    for (It = Arch->GroupDmgObjs.Items->begin(); It != Arch->GroupDmgObjs.Items->end(); ++It)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(It->Arch.Id);
        if (Simple != nullptr)
            Simple->load();
    }
}

void CArchGroup::FreeDamageObjs()
{
    // .text:062a0e70. The same two walks as LoadDamageObjs, calling Archetype::Root::Release.
    std::vector<Archetype::DamageObjInfo>::const_iterator It = Arch->DmgObjs.Items->begin();
    for (; It != Arch->DmgObjs.Items->end(); ++It)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(It->Arch.Id);
        if (Simple != nullptr)
            Simple->Release();
    }
    for (It = Arch->GroupDmgObjs.Items->begin(); It != Arch->GroupDmgObjs.Items->end(); ++It)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(It->Arch.Id);
        if (Simple != nullptr)
            Simple->Release();
    }
}

void CArchGroup::AttachDamageObjs(CObject* Target, Archetype::DamageObjInfoList const& List) const
{
    // .text:062a0c10-062a0dfb. For each entry whose hardpoint exists on Target and is not yet
    // marked attached on the owner: mark it, load the archetype, create and open a CObject for
    // it, connect its "DpConnect" to the hardpoint and, when that succeeds, give it physics and
    // attach it to Target. Retail tests neither the list head nor GetSimpleByName's result.
    std::vector<Archetype::DamageObjInfo>::const_iterator It = List.Items->begin();
    for (; It != List.Items->end(); ++It)
    {
        Archetype::DamageObjInfo const& Info = *It;
        CacheString const& HpName = Info.Hardpoint;

        long Handle = -1;
        HardpointInfo HpInfo;
        if (!FindHardpoint_PS(Target->EngineInstanceId, Info.Hardpoint.Value, Handle, HpInfo))
            continue;
        if (Owner->is_damaged_obj_attached(HpName))
            continue;
        Owner->attaching_damaged_obj(HpName);

        Archetype::Root* Simple = Archetype::GetSimpleByName(Info.Arch);
        Simple->load();
        if (Simple->Companion != nullptr)
            Simple->Companion->Load(0);

        CObject* Obj = CObject::Alloc(CObject::ClassObject);
        Simple->create_instance(Obj);
        Obj->open(Simple);

        FHardpointKey ChildKey;
        ChildKey.Name = "DpConnect";
        ChildKey.Crc = DACOM_CRC::GetCRC32("DpConnect");
        FHardpointKey ParentKey;
        ParentKey.Name = Info.Hardpoint.Value;
        ParentKey.Crc = DACOM_CRC::GetCRC32(Info.Hardpoint.Value);
        if (DALib::Hardpoint->ConnectHardpoints((void*)Handle, &ParentKey,
                                                (void*)Obj->EngineInstanceId, &ChildKey) == 0)
        {
            DALib::Engine->UpdateInstance((void*)Obj->EngineInstanceId, 0, nullptr);

            PhySys::CreateParms Phy;
            Phy.Mass = Simple->Mass;
            Phy.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
            Phy.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
            Phy.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
            Phy.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
            Phy.CollisionGroupId = Owner->PhysicsSystemId;
            Phy.Type = 2;
            Phy.bFlag1c = 0;
            Phy.bEnableCollisions = 1;
            Phy.bHasInertia = 0;
            Phy.Inertia.x = 0.0f;
            Phy.Inertia.y = 0.0f;
            Phy.Inertia.z = 0.0f;
            strcpy(Phy.Name, PhySys::GetCollisionGroup(Target));
            PhySys::BuildIVP_Real(Obj, Phy);
            PhySys::Attach(Target, Obj);
        }
        Simple->Release();
    }
}

void CArchGroup::SetRelativeHealth(float Frac)
{
    // .text:062a0270: FILD [Arch+0x10] (exact), FMUL float Frac, FSTP float [this+0xc]; no null
    // test.
    HitPts = (float)((double)Arch->HitPts * Frac);
}

double CArchGroup::GetRelativeHealth() const
{
    // .text:062a0260: FLD float [this+0xc]; FIDIV dword [Arch+0x10]; RET -- no null test and no
    // zero test (a zero max gives the x87 masked-exception result, inf or NaN, as here). The
    // quotient is returned unrounded in ST0.
    return (double)HitPts / Arch->HitPts;
}

void CArchGroup::Kill(DamageEntry::SubObjFate FateValue)
{
    State = 2;
    Fate = (int)FateValue;
}

DamageEntry::SubObjFate CArchGroup::GetFate() const
{
    // .text:062a0160: plain field read, no guard. Fate is an int because Destroy also tests
    // values 4 and 5, which SubObjFate does not name.
    return (DamageEntry::SubObjFate)Fate;
}

void CArchGroup::Destroy()
{
    // .text:062a0450-062a0689. Only a separable group (Arch+0x14) still on a live root
    // instance does anything beyond State = 3.
    if (Arch->bSeparable && RootIndex != -1)
    {
        AttachDamageObjs(Owner, ParentDamageObjs());                      // 062a0477, Arch+0x40
        // 062a047c-062a048b: IEngine +0xd4 on the root instance; the result is not used.
        (void)DALib::Engine->GetInstanceParent((void*)RootIndex);

        if (Fate == 2)
        {
            // 062a049d-062a04b0: ComputeExplosiveImpulses(RootIndex, &ParentPush, &ChildPush,
            // &Point); ParentPush scaled by Arch+0x18, ChildPush by Arch+0x1c (float products).
            Vector ParentPush;
            Vector ChildPush;
            Vector Point;
            ComputeExplosiveImpulses(RootIndex, ParentPush, ChildPush, Point);
            ParentPush.x = ParentPush.x * Arch->ParentImpulse;
            ParentPush.y = ParentPush.y * Arch->ParentImpulse;
            ParentPush.z = ParentPush.z * Arch->ParentImpulse;
            ChildPush.x = ChildPush.x * Arch->ChildImpulse;
            ChildPush.y = ChildPush.y * Arch->ChildImpulse;
            ChildPush.z = ChildPush.z * Arch->ChildImpulse;

            // 062a04ea-062a05c5: the detach parameters. The default dampings are stored first
            // and then overwritten with zeros (062a057d-062a05b9); Mass = Arch+0x20; inertia
            // from Arch+0x28..+0x30 when Arch+0x24 is set, else zero; Name = itoa(Owner->Id,
            // radix 0x18).
            PhySys::DetachParms Parms;
            Parms.LinearDamping = 0.0f;
            Parms.AngularDamping.x = 0.0f;
            Parms.AngularDamping.y = 0.0f;
            Parms.AngularDamping.z = 0.0f;
            Parms.Name[0] = '\0';
            Parms.Mass = Arch->Mass;
            Parms.bHasInertia = 0;
            Parms.Inertia.x = 0.0f;
            Parms.Inertia.y = 0.0f;
            Parms.Inertia.z = 0.0f;
            if (Arch->bHasRotationInertia)
            {
                Parms.Inertia.x = Arch->RotationInertiaX;
                Parms.Inertia.y = Arch->RotationInertiaY;
                Parms.Inertia.z = Arch->RotationInertiaZ;
                Parms.bHasInertia = 1;
            }
            _itoa((int)Owner->Id, Parms.Name, 0x18);

            // 062a05d4: FUN_0628a050 splits the group's instance off into a debris object.
            DestroyedObj = DetachInstance(RootIndex, Parms);
            if (DestroyedObj != nullptr)
            {
                AttachDamageObjs(DestroyedObj, GroupDamageObjs());         // 062a05f1, Arch+0x4c
                if (DestroyedObj->BodyPtr != nullptr)
                    PhySys::AddImpulseAtPoint(DestroyedObj, ChildPush, Point); // 062a060b
                if (Owner->BodyPtr != nullptr)
                    PhySys::AddImpulseAtPoint(Owner, ParentPush, Point);       // 062a0627
                Owner->notify(INotify::Event_0x7, this);                   // 062a0636
            }
        }
        else if (Fate == 3 || Fate == 4 || Fate == 5)
        {
            Owner->notify(INotify::Event_0x8, this);                       // 062a0665
            DetachInstanceInPlace(RootIndex);                              // 062a066f
        }
        RootIndex = -1;                                                    // 062a063c / 062a0677
    }
    State = 3;                                                             // 062a0643 / 062a067e
}

bool CArchGroup::IsDead() const
{
    // Common.dll.c 0x40130: `return State == 2 || State == 3;` (killed or destroyed).
    return State == 2 || State == 3;
}

bool CArchGroup::IsDestroyed() const
{
    // .text:062a0150: CMP [this+0x14],3 / SETZ AL.
    return State == 3;
}

void CArchGroup::GetCollisionGroupDesc(CollisionGroupDesc& Out) const
{
    // .text:062a0280..062a029a: Out word +0 = Arch->Id; FLD float HitPts, FIDIV dword
    // [Arch+0x10] (extended), FSTP float Out+4. No null test.
    Out.Id = Arch->Id;
    Out.Health = (float)((double)HitPts / Arch->HitPts);
}

CacheString const& CArchGroup::Name() const
{
    // .text:062a06b0: MOV EAX,[this+0x4]; ADD EAX,0x8; RET -- the CacheString that
    // CollisionGroup carries at +0x08 ("obj", declared there as its char const* value). No
    // null check on Arch in retail.
    return *(CacheString const*)&Arch->ObjName;
}

unsigned short CArchGroup::GetID() const
{
    // .text:062a0120: MOV EAX,[this+0x4]; MOV AX,[EAX+0x4]; RET.
    return Arch->Id;
}

long CArchGroup::GetRootIndex() const
{
    // Common.dll.c 0x402a0
    if (State == 3)
        return -1;
    return RootIndex;
}

bool CArchGroup::IsInstInGroup(long InstIndex) const
{
    // Common.dll.c 0x402b0
    if (State == 3)
        return false;
    return IsDescendant(RootIndex, InstIndex);
}

bool CArchGroup::GetVelocity(Vector& Out) const
{
    // .text:062a0300-062a0360. Out = 0; a destroyed group (State 3) or one without a root
    // instance returns false. Otherwise the velocity is that of the CObject riding on the
    // group's root instance: IEngine slot 32 (+0x80) GetInstanceUserData(RootIndex)
    // (062a0320-062a032e) feeds PhySys::GetVelocity (062a033a). No null test of the user data.
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (State == 3 || RootIndex == -1)
        return false;
    Out = PhySys::GetVelocity((CObject const*)DALib::Engine->GetInstanceUserData((void*)RootIndex));
    return true;
}

namespace
{
    // The 16-byte element retail push_backs at 062a0976 through the COMDAT
    // std::vector<T>::insert boilerplate at 062a0ee0: three floats of centre followed by one
    // float of radius (the refine loop at 062a0b62 walks it with stride 0x10 and reads
    // [p-8]/[p-4]/[p+0] as the centre and [p+4] as the radius).
    struct FArchGroupChildSphere
    {
        FVector Center;
        float Radius;
    };

    static_assert(sizeof(FArchGroupChildSphere) == 0x10, "CArchGroup child-sphere element stride 0x10");
}

void CArchGroup::GetBoundingSphere(float& OutRadius, Vector& OutCenter) const
{
    // .text:062a07a0-062a0c03 (Ghidra's .c for it is corrupt; ported from the listing). An
    // AABB in the root instance's parent frame over the root's extent sphere and the extent
    // sphere of every descendant instance of this group; its midpoint is the centre, half its
    // widest span the first radius, which then grows to cover the root sphere and every child
    // sphere; the centre is finally mapped into the root instance's local frame. Terms retail
    // keeps on the FPU stack are double; each FSTP dword is a float rounding here.
    // DAT_0639c13c = 0x3F000000 = 0.5f.

    // 062a07bf-062a07cf: a destroyed group enumerates from the invalid handle.
    long const RootInst = (State == 3) ? -1 : RootIndex;

    // 062a07d8-062a07e6: IEngine GetInstanceExtent(RootInst, 1, &Extent, &Center). Retail does
    // not pre-set the outputs; every IInstanceTransform::GetExtra (and the invalid-instance
    // path) writes both, so the initial values here are never seen.
    float RootExtent = 0.0f;
    FVector RootLocal = { 0.0f, 0.0f, 0.0f };
    DALib::Engine->GetInstanceExtent((void*)RootInst, 1, &RootExtent, &RootLocal.X);

    // 062a07f5-062a080d: the engine's root transform is copied into the frame (REP MOVSD 12
    // dwords); the tail reuses the copy.
    FTransform const RootXform =
        *(FTransform const*)DALib::Engine->GetInstanceTransform((void*)RootInst);
    I3DMathEngine* const Math = Acquire3DMathEngine();

    // 062a0831: TransformXform (+0x28) into the root's parent frame.
    FVector RootCenter;
    Math->TransformXform(RootCenter, RootXform, RootLocal);

    // 062a0834-062a08a4: seed the AABB from the transformed root centre +/- its extent, each
    // FLD/FSUB|FADD/FSTP dword.
    float MinX = (float)((double)RootCenter.X - RootExtent);
    float MaxX = (float)((double)RootCenter.X + RootExtent);
    float MinY = (float)((double)RootCenter.Y - RootExtent);
    float MaxY = (float)((double)RootCenter.Y + RootExtent);
    float MinZ = (float)((double)RootCenter.Z - RootExtent);
    float MaxZ = (float)((double)RootCenter.Z + RootExtent);

    // The child spheres the refine pass at 062a0b62 walks (a std::vector in retail's frame).
    std::vector<FArchGroupChildSphere> ChildSpheres;

    // 062a08b1 / 062a0a33: IEngine GetRelatedInstance(RootInst, 0, Cursor), seeded with the
    // invalid handle and ended by it.
    for (long Child = (long)DALib::Engine->GetRelatedInstance((void*)RootInst, 0,
                                                              G_InvalidEngineHandle);
         Child != -1;
         Child = (long)DALib::Engine->GetRelatedInstance((void*)RootInst, 0, (void*)Child))
    {
        // 062a08d0-062a08e3: only instances the owner maps to this group (CEqObj vtable +0xf4
        // inst_to_subobj_id, a 16-bit compare with Arch->Id).
        if (Owner->inst_to_subobj_id(Child) != Arch->Id)
            continue;

        // 062a08ef-062a0909: the radius output is pre-zeroed, the centre output is not.
        float ChildExtent = 0.0f;
        FVector ChildLocal;
        DALib::Engine->GetInstanceExtent((void*)Child, 1, &ChildExtent, &ChildLocal.X);

        // 062a0918-062a0943: each child through its own instance transform (no copy).
        FTransform const* const ChildXform =
            (FTransform const*)DALib::Engine->GetInstanceTransform((void*)Child);
        FVector ChildCenter;
        Math->TransformXform(ChildCenter, *ChildXform, ChildLocal);

        // 062a0976: the push_back comes before the AABB folds.
        FArchGroupChildSphere Entry;
        Entry.Center = ChildCenter;
        Entry.Radius = ChildExtent;
        ChildSpheres.push_back(Entry);

        // 062a097b-062a0a27, six folds on the unrounded candidate. Min: FCOMP + TEST AH,0x05
        // + JNP taken (Min < candidate) keeps Min; equal, greater or unordered store it. Max:
        // FCOMP + TEST AH,0x41 + JZ taken (Max > candidate) keeps Max, otherwise stores.
        double const CandMinX = (double)ChildCenter.X - ChildExtent;
        if (!(MinX < CandMinX))
            MinX = (float)CandMinX;
        double const CandMaxX = (double)ChildCenter.X + ChildExtent;
        if (!(MaxX > CandMaxX))
            MaxX = (float)CandMaxX;
        double const CandMinY = (double)ChildCenter.Y - ChildExtent;
        if (!(MinY < CandMinY))
            MinY = (float)CandMinY;
        double const CandMaxY = (double)ChildCenter.Y + ChildExtent;
        if (!(MaxY > CandMaxY))
            MaxY = (float)CandMaxY;
        double const CandMinZ = (double)ChildCenter.Z - ChildExtent;
        if (!(MinZ < CandMinZ))
            MinZ = (float)CandMinZ;
        double const CandMaxZ = (double)ChildCenter.Z + ChildExtent;
        if (!(MaxZ > CandMaxZ))
            MaxZ = (float)CandMaxZ;
    }

    // 062a0a49-062a0aab: centre = AABB midpoint (sum * 0.5f, DAT_0639c13c), FSTP dword into
    // the out-param. Retail keeps the X/Y sums unrounded but stores the Z sum as a float
    // (062a0a6a) before halving it.
    OutCenter.x = (float)(((double)MaxX + MinX) * 0.5);
    OutCenter.y = (float)(((double)MaxY + MinY) * 0.5);
    float const SumZ = (float)((double)MaxZ + MinZ);
    OutCenter.z = (float)((double)SumZ * 0.5);

    // 062a0a9f-062a0b0c: first radius = half the widest span. SpanX stays unrounded, SpanY and
    // SpanZ are FSTP dword. FCOM + TEST AH,0x41 + JNZ takes SpanY unless SpanX is strictly
    // greater (NaN takes SpanY); the winner likewise against SpanZ. The unrounded half is
    // stored as a float and kept on the stack for the next compare.
    double const SpanX = (double)MaxX - MinX;
    float const SpanY = (float)((double)MaxY - MinY);
    float const SpanZ = (float)((double)MaxZ - MinZ);
    double MaxSpan = (SpanX > SpanY) ? SpanX : (double)SpanY;
    if (!(MaxSpan > SpanZ))
        MaxSpan = SpanZ;
    double const HalfSpan = MaxSpan * 0.5;
    OutRadius = (float)HalfSpan;

    // 062a0b0e-062a0b51: grow to the distance from the stored centre to the transformed root
    // centre plus the root extent; the square is summed (dz*dz + dy*dy) + dx*dx. FCOMP against
    // the unrounded half + TEST AH,0x41 + JNZ keeps the half unless strictly greater.
    {
        double const Dx = (double)OutCenter.x - RootCenter.X;
        double const Dy = (double)OutCenter.y - RootCenter.Y;
        double const Dz = (double)OutCenter.z - RootCenter.Z;
        double const Reach = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) + RootExtent;
        if (Reach > HalfSpan)
            OutRadius = (float)Reach;
    }

    // 062a0b53-062a0ba4: grow to cover every child sphere: FCOM dword OutRadius + TEST
    // AH,0x41 + JNZ keeps the radius unless the unrounded reach is strictly greater.
    std::vector<FArchGroupChildSphere>::const_iterator It = ChildSpheres.begin();
    for (; It != ChildSpheres.end(); ++It)
    {
        double const Dx = (double)It->Center.X - OutCenter.x;
        double const Dy = (double)It->Center.Y - OutCenter.y;
        double const Dz = (double)It->Center.Z - OutCenter.z;
        double const Reach = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) + It->Radius;
        if (Reach > OutRadius)
            OutRadius = (float)Reach;
    }

    // 062a0ba6-062a0be0: InverseTransform (+0x3c) of the centre through the copied root
    // transform into the root instance's local frame; the radius is not transformed.
    FVector const ParentCenter = { OutCenter.x, OutCenter.y, OutCenter.z };
    FVector LocalCenter;
    Math->InverseTransform(LocalCenter, RootXform, ParentCenter);
    OutCenter.x = LocalCenter.X;
    OutCenter.y = LocalCenter.Y;
    OutCenter.z = LocalCenter.Z;
}

bool CArchGroup::GetCenterOfMass(Vector& Out) const
{
    // Listing 0x40360, read in full: 062a0360 (SUB ESP,0xc) .. 062a03f8 (RET 0x4).
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    // 062a0376-062a0386: two separate guards, each returning AL=0 with Out left zeroed.
    if (State == 3 || RootIndex == -1)
        return false;

    // 062a0388-062a039f: the cache is filled exactly once, and retail sets the flag BEFORE
    // making the call (062a0392 stores BL=1 into +0x20 at 062a039c, one instruction ahead of
    // the CALL) -- so a re-entrant GetBoundingSphere could never recurse through here.
    if (!bBoundsValid)
    {
        bBoundsValid = true;
        GetBoundingSphere(BoundRadius, BoundCenter);
    }

    // 062a03a4-062a03d8: BoundCenter is cached in the root instance's LOCAL frame (that is
    // what GetBoundingSphere's closing InverseTransform leaves behind), so it has to be
    // pushed back out through the root's transform here: IEngine +0xa0 on the root index
    // read before the cache fill (EBP), then the math singleton (DAT_063fc26c, created on
    // first use) slot +0x28 TransformXform(&World, xform, &BoundCenter). No null tests.
    FTransform const* const Xform =
        (FTransform const*)DALib::Engine->GetInstanceTransform((void*)RootIndex);
    I3DMathEngine* const Math = Acquire3DMathEngine();

    FVector World;
    Math->TransformXform(World, *Xform, (FVector const&)BoundCenter);
    Out.x = World.X;
    Out.y = World.Y;
    Out.z = World.Z;
    // 062a03ec: AL = BL = 1 unconditionally on this path.
    return true;
}

bool CArchGroup::GetRadius(float& Out) const
{
    // Listing 0x40400, read in full: 062a0400 (PUSH ESI) .. 062a0444 (RET 0x4).
    Out = 0.0f;
    if (State == 3 || RootIndex == -1)
        return false;

    if (!bBoundsValid)
    {
        bBoundsValid = true;
        GetBoundingSphere(BoundRadius, BoundCenter);
    }

    // 062a043a: the cached radius is handed back as-is -- no transform, no rescale.
    Out = BoundRadius;
    return true;
}

int CArchGroup::InstDepthInGroup(long InstIndex) const
{
    // Common.dll.c 0x40690
    if (State == 3)
        return -1;
    return Hierarchy::GetDepth(RootIndex, InstIndex);
}

double CArchGroup::GetMaxHitPoints() const
{
    // .text:062a0200: MOV EAX,[this+4]; FILD [EAX+0x10]; RET -- no null test. The exact FILD
    // value is returned in ST0.
    return Arch->HitPts;
}

float CArchGroup::GetHitPoints() const
{
    return HitPts;
}

void CArchGroup::SetHitPoints(float NewHitPts)
{
    // .text:062a0210-062a024e. The old value stays on the FPU while HitPts is overwritten.
    // New > 0.0f (FCOMP + TEST AH,0x41 + JNZ out on less/equal/unordered) and New < Old
    // (FCOMP + TEST AH,0x5 + JP out on greater/equal/unordered) -> Owner->notify(9, this)
    // (CEqObj vtable +0xc8).
    float const OldHitPts = HitPts;
    HitPts = NewHitPts;
    if (NewHitPts > 0.0f && NewHitPts < OldHitPts)
        Owner->notify(INotify::Event_0x9, this);
}

unsigned CArchGroup::GetCollisionGroupType() const
{
    // .text:062a06c0: MOV EAX,[this+4]; MOV EAX,[EAX+0xc]; RET -- no null test.
    return Arch->Type;
}

// Common.dll.c 0x402f0
CObject* CArchGroup::RetreiveDestroyedObj()
{
    CObject* Obj = DestroyedObj;
    DestroyedObj = nullptr;
    return Obj;
}

// The CArchGroup accessors .text:062a06d0..062a0796 are all `MOV EAX,[this+4]` (Arch) and one
// load or LEA off it, without a null test.

// .text:062a06d0: byte [Arch+0x14].
bool CArchGroup::Separable() const
{
    return Arch->bSeparable;
}

// .text:062a06e0: byte [Arch+0x15].
bool CArchGroup::DestroysParent() const
{
    return Arch->bDestroyParent;
}

// .text:062a06f0: byte [Arch+0x16].
bool CArchGroup::IsRootHealthProxy() const
{
    return Arch->bRootHealthProxy;
}

// .text:062a0710: LEA [Arch+0x38].
ID_String const& CArchGroup::DebrisTypeName() const
{
    return Arch->DebrisType;
}

// .text:062a0720: FLD [Arch+0x18].
float CArchGroup::ExplosionParentImpulse() const
{
    return Arch->ParentImpulse;
}

// .text:062a0730: FLD [Arch+0x1c].
float CArchGroup::ExplosionGroupImpulse() const
{
    return Arch->ChildImpulse;
}

// .text:062a0770: [Arch+0x64].
unsigned CArchGroup::LinkedEquipmentType() const
{
    return Arch->LinkedEquipType;
}

// .text:062a0780: FLD [Arch+0x68].
float CArchGroup::LinkedEquipmentDamage() const
{
    return Arch->LinkedEquipHealth;
}

// .text:062a0790: FLD [Arch+0x3c].
float CArchGroup::ExplosionResistance() const
{
    return Arch->ExplosionResistance;
}

Archetype::DamageObjInfoList const& CArchGroup::ParentDamageObjs() const
{
    // .text:062a0740: LEA [Arch+0x40], no null test.
    return Arch->DmgObjs;
}

Archetype::DamageObjInfoList const& CArchGroup::GroupDamageObjs() const
{
    // .text:062a0750: LEA [Arch+0x4c].
    return Arch->GroupDmgObjs;
}

Archetype::FuseIgnitionList const& CArchGroup::FuseIgnitions() const
{
    // .text:062a0760: LEA [Arch+0x58].
    return Arch->Fuses;
}

// .text:062a0700: LEA [Arch+0x34], no null test.
ID_String const& CArchGroup::ExplosionTypeName() const
{
    return Arch->SeparationExplosion;
}

CArchGroupManager::CArchGroupManager()
    : Owner(nullptr)
    , TreeAllocator(0)
    , TreeCompare(0)
    , Groups(nullptr)
    , TreeNilSlot(0)
    , bTreeMulti(false)
    , Count(0)
{
    // .text:06280b60: retail stores uninitialised stack bytes into +0x04/+0x05; zero here.
    Groups = new std::map<unsigned short, CArchGroup*>();
}

CArchGroupManager::~CArchGroupManager()
{
    // .text:062a1230..062a1374: retail loads Count (+0x14) first, deletes every non-null
    // CArchGroup value (~CArchGroup + operator delete), erases every node (FUN_062a1c90) --
    // the same steps as Clear() -- then warns when the saved Count was nonzero, and
    // finally runs the map's own destructor (erase again, free the head). The port's
    // Groups is a std::map held by pointer (class comment), so `delete Groups` is that
    // last step.
    unsigned OldCount = Count;
    Clear();
    if (OldCount != 0)                                                 // 062a12a8 CMP EBP,0
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
              kCArchGroupSource, 0x12, "CArchGroupList: Destructing with ArchGroups still present");
    }
    delete Groups;
    Groups = nullptr;
    Count = 0;
}

// .text:06280bd0. Owner, the allocator / key_compare bytes and _Multi are copied, a fresh
// tree is built and Other's nodes are copied into it (the CArchGroup* values shallowly, as
// std::map's copy does), then the size.
CArchGroupManager::CArchGroupManager(CArchGroupManager const& Other)
    : Owner(Other.Owner)
    , TreeAllocator(Other.TreeAllocator)
    , TreeCompare(Other.TreeCompare)
    , Groups(new std::map<unsigned short, CArchGroup*>(*Other.Groups))
    , TreeNilSlot(0)
    , bTreeMulti(Other.bTreeMulti)
    , Count(Other.Count)
{
}

// .text:06280d30. Owner is copied first, even on self-assignment (the guard compares the
// map sub-objects). Otherwise the old nodes are freed without deleting their CArchGroup
// values, the key_compare byte is copied (not the allocator byte or _Multi), Other's nodes
// are copied, then the size.
CArchGroupManager& CArchGroupManager::operator=(CArchGroupManager const& Other)
{
    Owner = Other.Owner;
    if (this != &Other)
    {
        Groups->clear();
        TreeCompare = Other.TreeCompare;
        *Groups = *Other.Groups;
        Count = Other.Count;
    }
    return *this;
}

bool CArchGroupManager::Init(CEqObj* OwnerObj)
{
    if (Owner == nullptr)
        Owner = OwnerObj;
    return Owner != nullptr;
}

bool CArchGroupManager::AddNewArchGroup(CArchGroup* Group)
{
    // .text:062a17f0..062a18a8. The key is Group->Arch->Id (+4 -> +4 word, GetID inlined),
    // read without any null test. lower_bound (FUN_062a2250) + `key < node.key` is find().
    unsigned short const GroupId = Group->GetID();
    if (Groups->find(GroupId) != Groups->end())
    {
        // 062a1878..062a18a8: warning, return false. Retail does NOT free the rejected
        // group; the caller's allocation stays alive (leaks), as retail.
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: CArchGroupList: Insertion failed: Group SUBOBJ_ID[%d] already on list",
              kCArchGroupSource, 0xce, (unsigned)GroupId);
        return false;
    }
    // 062a1841..062a186a: insert(pair(key, 0)) (FUN_062a1db0), then node value = Group.
    // Retail's Count (+0x14) is the map's own size; the port keeps it in step.
    (*Groups)[GroupId] = Group;
    Count = (unsigned)Groups->size();
    return true;
}

int CArchGroupManager::Clear()
{
    // .text:062a1680..062a179b: save Count (+0x14), delete every non-null value
    // (~CArchGroup + operator delete), erase every node (the inlined VC6
    // erase(begin(), end())), return the saved Count.
    int const OldCount = (int)Count;
    for (std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin(); It != Groups->end(); ++It)
    {
        CArchGroup* const Group = It->second;
        if (Group != nullptr)                                          // 062a16a3 TEST EDI,EDI
            delete Group;
    }
    Groups->clear();
    Count = 0;
    return OldCount;
}

int CArchGroupManager::NumLiving() const
{
    // Common.dll.c 69248-69266 (0x417a0): walk every group via Traverse() (a fresh
    // traverser), counting each whose State isn't Killed(2) or Destroyed(3).
    int LivingCount = 0;
    CArchGrpTraverser Traverser;
    for (CArchGroup const* Group = Traverse(Traverser); Group != nullptr; Group = Traverse(Traverser))
    {
        if (Group->State != 2 && Group->State != 3)
            ++LivingCount;
    }
    return LivingCount;
}

bool CArchGroupManager::DeleteArchGroup(unsigned short GroupId)
{
    // Common.dll.c 69316-69349 (0x418b0). Unlike DestroyDeadGroups/DestroyGroups (Destroy()
    // in place, entry stays in the map), this really erases the entry: explicitly
    // destructs+frees the CArchGroup object (retail: explicit ~CArchGroup + free-list call)
    // then removes the map entry, keeping Count in sync (same pattern as AddNewArchGroup).
    // .text:062a18b0..062a1956: lower_bound (FUN_062a1fc0) + `key < node.key` is find().
    std::map<unsigned short, CArchGroup*>::iterator It = Groups->find(GroupId);
    if (It == Groups->end())
    {
        // 062a1926..062a1956: warning, return false.
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: CArchGroupList: Removal failed: Group SUBOBJ_ID[%d] not on list",
              kCArchGroupSource, 0xe7, (unsigned)GroupId);
        return false;
    }

    // 062a18f7..062a1916: delete the value when non-null, then erase the node (FUN_062be400).
    CArchGroup* const Group = It->second;
    if (Group != nullptr)
        delete Group;
    Groups->erase(It);
    Count = (unsigned)Groups->size();
    return true;
}

bool CArchGroupManager::DestroyDeadGroups()
{
    // .text:062a1a50..062a1ab1. Every node's value is dereferenced without a null test.
    // `state == 3 -> skip; state == 2 || state == 3 -> destroy` is state == 2. The owner's
    // flush_animations (vtable +0xcc, no null test), then DestroyLowerGroups, then Destroy
    // on the value re-read from the node.
    for (std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin(); It != Groups->end(); ++It)
    {
        if (It->second->State != 2)
            continue;
        Owner->flush_animations();
        CArchGroup* const Group = It->second;
        DestroyLowerGroups(*Group);
        Group->Destroy();
    }
    return true;
}

bool CArchGroupManager::DestroyGroups(DamageList const& List, bool Param2)
{
    // .text:062a1960..062a1a41. Param2 is read once (062a19ce) to choose between two pushes
    // of the same EAX (the entry's fate) -- both paths push the same value, so it has no
    // effect.
    (void)Param2;

    // 062a1968..062a19e3: for each damage entry, IsArchGroupID(sub id) first, then a
    // non-zero fate; find the group (FUN_062a1d40); a found, non-null, not Killed(2)/
    // Destroyed(3) group is Kill()ed with the entry's fate.
    for (std::list<DamageEntry>::const_iterator EntryIt = List.Entries.begin(); EntryIt != List.Entries.end(); ++EntryIt)
    {
        if (!SubObjectID::IsArchGroupID(EntryIt->SubObjId))
            continue;
        if (EntryIt->Fate == DamageEntry::SubObjFate::None)
            continue;
        std::map<unsigned short, CArchGroup*>::iterator Found = Groups->find(EntryIt->SubObjId);
        if (Found == Groups->end())
            continue;
        CArchGroup* const Group = Found->second;
        if (Group == nullptr || Group->State == 2 || Group->State == 3)
            continue;
        Group->Kill(EntryIt->Fate);
    }

    // 062a19e5..062a1a39: the DestroyDeadGroups sweep, inlined.
    for (std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin(); It != Groups->end(); ++It)
    {
        if (It->second->State != 2)
            continue;
        Owner->flush_animations();
        CArchGroup* const Group = It->second;
        DestroyLowerGroups(*Group);
        Group->Destroy();
    }
    return true;
}

void CArchGroupManager::DestroyDeadGroup(CArchGroup& Group)
{
    // Common.dll.c 0x62a1c10
    DestroyLowerGroups(Group);
    Group.Destroy();
}

void CArchGroupManager::DestroyLowerGroups(CArchGroup const& DeadGroup)
{
    // .text:062a1ac0..062a1c0d. Walks every OTHER group (Arch id word differs, 062a1af0..
    // 062a1b01; values dereferenced without a null test); one that sits under DeadGroup's
    // instance (IsInstInGroup(Entry->GetRootIndex())) is either Killed/Destroyed -- recurse
    // into its own lower groups, then Destroy() it -- or still alive, an "UnDead lower
    // group" that retail logs and deletes.
    std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin();
    while (It != Groups->end())
    {
        CArchGroup* const Entry = It->second;
        if (DeadGroup.GetID() != Entry->GetID() && DeadGroup.IsInstInGroup(Entry->GetRootIndex()))
        {
            if (Entry->State == 2 || Entry->State == 3)
            {
                DestroyLowerGroups(*Entry);
                Entry->Destroy();
            }
            else
            {
                // 062a1b44..062a1bab: both warnings; names in push order DeadGroup, Entry;
                // then root index and id.
                char const* const EntryName = Entry->Name().Value;
                char const* const DeadName = DeadGroup.Name().Value;
                FDUMP(0x100002,
                      "%s(%d) : *** WARNING: DestroyLowerGroups(%s): Deleting UnDead lower group[%s].",
                      kCArchGroupSource, 0x146, DeadName, EntryName);
                unsigned const EntryId = Entry->GetID();
                long const EntryRoot = Entry->GetRootIndex();
                FDUMP(0x100002, "%s(%d) : *** WARNING: Dead DUMP: Inst:%d\t ID:%d",
                      kCArchGroupSource, 0x148, EntryRoot, EntryId);

                // 062a1bac..062a1bec: the iterator is first decremented (FUN_0626ba00, tree
                // _Dec), the value deleted (null-tested), the node erased (FUN_062be400); the
                // loop's own increment (FUN_063429f0, _Inc) then runs from the decremented
                // position, which normally lands on the node after the erased one.
                // Very strange, reproduced: when the erased node was the first one, _Dec
                // yields the head, and _Inc from the head gives the minimum of the rightmost
                // node's subtree, so every node in between is skipped. The port resumes at
                // the rightmost node; retail resumes at the rightmost node's left child
                // instead when it has one (tree shape, not visible through std::map). When
                // the erased first node is the root (a two-node map), _Dec returns the node
                // itself and _Inc then reads the freed node (retail UB); its stale right link
                // leads to the rightmost node as well. DeadGroup's own node is still in the
                // map, so the map is not empty after the erase.
                bool const bWasFirst = (It == Groups->begin());
                std::map<unsigned short, CArchGroup*>::iterator const Erased = It;
                if (!bWasFirst)
                    --It;
                if (Entry != nullptr)
                    delete Entry;
                Groups->erase(Erased);
                Count = (unsigned)Groups->size();
                if (bWasFirst)
                {
                    It = Groups->end();
                    --It;
                    continue;
                }
            }
        }
        ++It;
    }
}

CArchGroup* CArchGroupManager::FindByID(unsigned short GroupId)
{
    // .text:062a1540..062a1593: lower_bound (FUN_062a2250) + `key < node.key` is find();
    // found -> the node's value, else 0.
    std::map<unsigned short, CArchGroup*>::iterator It = Groups->find(GroupId);
    if (It == Groups->end())
        return nullptr;
    return It->second;
}

CArchGroup const* CArchGroupManager::FindByID(unsigned short GroupId) const
{
    // .text:062a1540 serves both overloads.
    return const_cast<CArchGroupManager*>(this)->FindByID(GroupId);
}

int CArchGroupManager::Size() const
{
    // .text:06263bb0: MOV EAX,[this+0x14]; RET (ICF fold).
    return (int)Count;
}

bool CArchGroupManager::IsOnMap(unsigned short GroupId, bool CheckDestroyed) const
{
    // .text:062a1460..062a14d4. Same tree-find as FindByID; not found -> false. When
    // CheckDestroyed, whether the found group's State == 3 (Destroyed; the value is read
    // without a null test, 062a14ac); otherwise true.
    std::map<unsigned short, CArchGroup*>::const_iterator It = Groups->find(GroupId);
    if (It == Groups->end())
        return false;
    if (CheckDestroyed)
        return It->second->State == 3;
    return true;
}

CArchGroup* CArchGroupManager::FindByName(CacheString const& Name)
{
    // .text:062a14e0..062a1536. Linear in-order scan while no match is found: the value's
    // Name() first dword (CacheString.Value, pointer identity of interned strings) equal to
    // Name's and State not Killed/Destroyed (2/3) -> that group. No null test on the value.
    CArchGroup* Result = nullptr;
    for (std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin();
         It != Groups->end() && Result == nullptr; ++It)
    {
        CArchGroup* const Group = It->second;
        if (Group->Name().Value == Name.Value && Group->State != 2 && Group->State != 3)
            Result = Group;
    }
    return Result;
}

CArchGroup const* CArchGroupManager::FindByName(CacheString const& Name) const
{
    return const_cast<CArchGroupManager*>(this)->FindByName(Name);
}

// Common.dll.c 0x20ec0
CArchGrpTraverser::CArchGrpTraverser()
    : AtStart(true)
{
}

// Common.dll.c 0x20ed0
void CArchGrpTraverser::Restart()
{
    AtStart = true;
}

CArchGroup* CArchGroupManager::Traverse(CArchGrpTraverser& Traverser)
{
    // .text:062a15a0: AtStart set -> Count (+0x14) == 0 returns 0 leaving AtStart set;
    // otherwise AtStart = 0, position = leftmost node, return its value. AtStart clear ->
    // _Tree::_Inc (FUN_063429f0) the position; at the head, AtStart = 1 and return 0;
    // else return the node's value. The port's position is the current key, so the
    // in-order successor is upper_bound(key) (differs only if the current node was erased
    // between calls, where retail increments a freed node).
    if (!Traverser.AtStart)
    {
        std::map<unsigned short, CArchGroup*>::iterator Next = Groups->upper_bound(Traverser.CurrentKey);
        if (Next != Groups->end())
        {
            Traverser.CurrentKey = Next->first;
            return Next->second;
        }
        Traverser.AtStart = true;
    }
    else if (Count != 0)
    {
        Traverser.AtStart = false;
        std::map<unsigned short, CArchGroup*>::iterator First = Groups->begin();
        Traverser.CurrentKey = First->first;
        return First->second;
    }
    return nullptr;
}

// Retail's const overload (?Traverse@CArchGroupManager@@QBE...) at 0x62a15f0 is a plain
// JMP thunk tail-calling the non-const Traverse at 0x62a15a0 (Common.dll.txt:123999).
CArchGroup const* CArchGroupManager::Traverse(CArchGrpTraverser& Traverser) const
{
    return const_cast<CArchGroupManager*>(this)->Traverse(Traverser);
}

unsigned short CArchGroupManager::InstToSubObjID(long InstIndex) const
{
    // .text:062a1600..062a166f. Result starts as SUBOBJ_ID_NONE (062a160a); InstIndex == -1
    // returns it. Otherwise the in-order walk runs while the node is not the head AND the best
    // depth is still > 0 (062a1627 TEST ESI,ESI; JLE exit): a depth that is not -1 and below
    // the best becomes the best, and the value's Arch->Id (inlined GetID, 062a1643..064b) the
    // result. No null test on the value.
    unsigned short Result = SUBOBJ_ID_NONE;
    if (InstIndex == -1)
        return Result;

    int BestDepth = 0x7fff;
    for (std::map<unsigned short, CArchGroup*>::const_iterator It = Groups->begin();
         It != Groups->end() && BestDepth > 0; ++It)
    {
        CArchGroup const* const Group = It->second;
        int const Depth = Group->InstDepthInGroup(InstIndex);
        if (Depth != -1 && Depth < BestDepth)
        {
            BestDepth = Depth;
            Result = Group->GetID();
        }
    }
    return Result;
}

// CArchGroupManager::MAX_GROUP_DEPTH -- export ordinal 1835, .rdata 0x0639d9dc = 0x7fff.
// Exported but never read anywhere in Common.dll; ported for symbol parity.
int const CArchGroupManager::MAX_GROUP_DEPTH = 0x7FFF;
