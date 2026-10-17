// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CAttachedEquip (retail CAttachedEquip.cpp) -- split out of common_cequip.cpp (CMN-B2).
// Declared in common_cequip.h; helpers shared with the sibling TUs are in
// common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine (MatrixToQuaternionViaMathEngine)
#include "common_fuses.h"    // WeaponModDB_GetShieldMod (CEShield::GetWeaponModifier)
#include "common_iface.h"    // EquipStatus, EquipStatusFlag* (CEquip::GetStatus)
#include "common_ini.h"
#include "common_misc.h"     // ComputeExplosiveImpulses (CAttachedEquip::Disconnect)
#include "common_phyarch.h"  // PhyArch::Part (CObject+0x58, CAttachedEquip::Connect)
#include "common_physys.h"
#include "common_spatial.h"  // ISpatialPartition (ScanList::add_in_range bucket query)
#include "common_timing.h"   // Timing::GetGlobalTime (CEShieldGenerator / CEShield)
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "../IVP/ivp_physics/ivp_real_object.h"
#include "../IVP/ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common_cequip_internal.h"

using namespace CmnCequip;

namespace
{
    const int kFateStay = 1;
    const int kFateDebris = 2;
}

CAttachedEquip::CAttachedEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                               Archetype::Equipment const* ArchObj, bool bMissionFlag)
    : CExternalEquip(TypeBits, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    Attachment = nullptr;
    Debris = nullptr;
    HitPts = 0.0f;
    Fate = kFateStay;
    bBoundsValid = 0;
    BoundRadius = 0.0f;
    BoundCenter.x = 0.0f;
    BoundCenter.y = 0.0f;
    BoundCenter.z = 0.0f;
    // .text:0629312e..06293147: Arch vt+0xc load() with no null test, then the non-virtual
    // CEquip::GetMaxHitPoints into +0x30. The pad bytes +0x39..+0x3b are not written.
    Arch->load();
    HitPts = CEquip::GetMaxHitPoints();
}

CAttachedEquip::CAttachedEquip(CAttachedEquip const& Other)
    : CExternalEquip(Other)
    , Attachment(Other.Attachment)
    , Debris(Other.Debris)
    , HitPts(Other.HitPts)
    , Fate(Other.Fate)
    , bBoundsValid(Other.bBoundsValid)
    , BoundRadius(Other.BoundRadius)
    , BoundCenter(Other.BoundCenter)
{
    // Common.dll.c 54938 (0x32a70). Raw field copy re-stamping the vtable at each base level
    // (CEquip::_vftable_, then CExternalEquip::_vftable_, then this class's own) -- ordinary
    // MSVC base-then-derived copy-ctor codegen, reproduced here via CExternalEquip(Other)'s
    // own (compiler-generated) memberwise copy plus this level's fields. No AddRef/refcount
    // bump on Attachment/Debris/Arch, matching retail exactly. Only the +0x38 byte is copied
    // (06292adf/06292ae2); the pad bytes +0x39..+0x3b are not.
}

CAttachedEquip::~CAttachedEquip()
{
    // Listing 0x06293160..0x062931f5.
    if (Debris != nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCAttachedEquipSource, 0x22,
              "Deleting equipment with un-retrieved debris object!");
        Debris->Release();
        Debris = nullptr;
    }
    if (Attachment != nullptr)
        delete Attachment;
    Arch->Release();
}

int CAttachedEquip::GetToughness() const
{
    // Listing 0x33200: MOV EAX,[ECX+0xc]; MOV EAX,[EAX+0x84]; RET -- no null check.
    return ((Archetype::AttachedEquipment const*)Arch)->Toughness;
}

bool CAttachedEquip::Update(float Dt, unsigned Flags)
{
    // .text:06293210..0629323b: destroyed (vt+8) with un-retrieved debris -> true; else
    // CExternalEquip::Update (itself a JMP to CEquip::Update).
    if (IsDestroyed() && Debris != nullptr)
        return true;
    return CExternalEquip::Update(Dt, Flags);
}

float CAttachedEquip::GetHitPoints() const
{
    return HitPts;
}

void CAttachedEquip::SetHitPoints(float Hit)
{
    HitPts = Hit;
}

// Common.dll.txt 0xc75d0: `*(int*)(this+0x34) = param_1`.
void CAttachedEquip::SetFate(DamageEntry::SubObjFate FateValue)
{
    Fate = (int)(FateValue);
}

bool CAttachedEquip::IsConnected() const
{
    if (!CExternalEquip::IsConnected() || Attachment == nullptr)
        return false;
    return Attachment->GetRootIndex() != -1;
}

bool CAttachedEquip::Connect(char const* HpName)
{
    // .text:062932a0..06293378. No null tests: the CRC of HpName, the parent connector's user
    // data (IEngine+0x80 of [this+0x24]) and its part (+0x58) are used as they come.
    if (!CExternalEquip::Connect(HpName))                                    // 062932a9
        return false;

    unsigned long const HpCrc = DACOM_CRC::GetCRC32(HpName);                 // 062932b7
    CObject* const Parent = (CObject*)(
        DALib::Engine->GetInstanceUserData((void*)(ConnectionIndex)));   // 062932d0
    // 062932d6..062932f6: a parent part (+0x58) whose hardpoint id list (std::find over the
    // vector at part+0x24/+0x28) holds the CRC makes this a non-physical attachment.
    PhyArch::Part const* const Part = Parent->PhyPart;
    if (Part != nullptr &&
        std::find(Part->HardpointIds.begin(), Part->HardpointIds.end(), HpCrc) !=
            Part->HardpointIds.end())
    {
        Attachment = new CNonPhysAttachment();                               // 062932f8..0629331e
    }
    else
    {
        Attachment = new CPhysAttachment();                                  // 06293340..0629335d
    }
    return Attachment->Connect(this);                                        // vt+0x10
}

void CAttachedEquip::Disconnect()
{
    // Listing 0x06293380..0x062934d1.
    if (IsConnected())
    {
        if (Fate == kFateDebris)
        {
            // 0x0629339f: ComputeExplosiveImpulses(GetRootIndex(), &ParentPush, &ChildPush, &Point)
            // (the three out-pointers are pushed before the vtable+0x84 call and stay on the stack
            // as the helper's arguments 2..4).
            Vector ParentPush;
            Vector ChildPush;
            Vector Point;
            ComputeExplosiveImpulses(GetRootIndex(), ParentPush, ChildPush, Point);
            Archetype::AttachedEquipment const* AttachedArch =
                (Archetype::AttachedEquipment const*)(Arch);
            ParentPush.x = ParentPush.x * AttachedArch->ParentImpulse; // Arch+0x7c
            ParentPush.y = ParentPush.y * AttachedArch->ParentImpulse;
            ParentPush.z = ParentPush.z * AttachedArch->ParentImpulse;
            ChildPush.x = ChildPush.x * AttachedArch->ChildImpulse;    // Arch+0x78
            ChildPush.y = ChildPush.y * AttachedArch->ChildImpulse;
            ChildPush.z = ChildPush.z * AttachedArch->ChildImpulse;
            Debris = Attachment->Disconnect(this, true);
            if (Debris != nullptr)
            {
                if (Debris->BodyPtr != nullptr)
                    PhySys::AddImpulseAtPoint(Debris, ChildPush, Point);
                Owner->notify(INotify::Event_0x5, this);
            }
            if (Owner->BodyPtr != nullptr)
                PhySys::AddImpulseAtPoint(Owner, ParentPush, Point);
        }
        else if (Fate != kFateStay)
        {
            Owner->notify(INotify::Event_0x6, this);
            Attachment->Disconnect(this, false);
            if (Fate != 3 && Fate != 4)
                FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCAttachedEquipSource, 0xd1,
                      "Disconnecting equipment with FATE_UNKNOWN, using FATE_DISAPPEAR");
        }
        if (Attachment != nullptr)
            delete Attachment;
        Attachment = nullptr;
    }
    CExternalEquip::Disconnect();
}

long CAttachedEquip::GetRootIndex() const
{
    if (!IsConnected())
        return -1;
    return Attachment->GetRootIndex();
}

bool CAttachedEquip::GetVelocity(Vector& Out) const
{
    // Common.dll.c 55580 (0x334e0). Connected guard, then Attachment->GetPhysicsOwner()->
    // get_velocity() (CObject's own vtable+0x54 vcall, already ported as CObject::get_velocity()).
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (!IsConnected())
        return false;
    CObject* Owner = Attachment->GetPhysicsOwner();
    if (Owner == nullptr)
        return false;
    Out = Owner->get_velocity();
    return true;
}

void CAttachedEquip::ComputeBoundingSphere(float& OutRadius, Vector& OutCenter) const
{
    // Listing 0x06293740..0x0629376f: IEngine::GetInstanceExtent (+0x88) of the root instance.
    // The three arguments (0, &OutRadius, &OutCenter) are pushed before the 0-arg
    // GetRootIndex() vcall (+0x84) and stay on the stack for the engine call.
    long Root = GetRootIndex();
    DALib::Engine->GetInstanceExtent((void*)(Root), 0, &OutRadius,
                                     &OutCenter.x);
}

bool CAttachedEquip::GetCenterOfMass(Vector& Out) const
{
    // Listing 0x06293570..0x06293645. Zero Out; when connected, fill the bound cache once
    // (vtable+0x8c), then transform the local center by the root instance's world transform:
    // IEngine::GetInstanceTransform(Attachment->GetRootIndex()) (+0xa0) and
    // I3DMathEngine::TransformXform (+0x28, the per-TU cached singleton DAT_063fc114).
    Vector World;
    World.x = 0.0f;
    World.y = 0.0f;
    World.z = 0.0f;
    Out = World;
    if (!IsConnected())
        return false;
    if (!bBoundsValid)
    {
        bBoundsValid = 1;
        ComputeBoundingSphere(BoundRadius, BoundCenter);
    }
    long Root = Attachment->GetRootIndex();
    float* Xform = DALib::Engine->GetInstanceTransform((void*)(Root));
    Acquire3DMathEngine()->TransformXform((FVector&)(World),
                                          *(FTransform const*)Xform,
                                          (FVector const&)(BoundCenter));
    Out = World;
    return true;
}

bool CAttachedEquip::GetRadius(float& OutRadius) const
{
    // Common.dll.c 55654-55670 (raw 06293650, listing 0x33650). Same IsConnected guard + lazy
    // bBoundsValid cache as GetCenterOfMass above (sharing the same ComputeBoundingSphere
    // cache-populate call), returning the cached BoundRadius (local-space magnitude, not
    // re-transformed -- retail does not re-transform the radius, only the center).
    if (!IsConnected())
        return false;
    if (!bBoundsValid)
    {
        bBoundsValid = 1;
        ComputeBoundingSphere(BoundRadius, BoundCenter);
    }
    OutRadius = BoundRadius;
    return true;
}

CObject* CAttachedEquip::GetPhysicsOwner() const
{
    if (!IsConnected())
        return nullptr;
    return Attachment->GetPhysicsOwner();
}

bool CAttachedEquip::IsInstOnEquip(long InstIndex) const
{
    // Common.dll.c 0x336a0. See header comment: retail calls a 0-arg self-virtual twice at the
    // same vtable slot returning a long, compared/used exactly like GetRootIndex() would be.
    // .text:062936a0..062936e3: vt+0x84 GetRootIndex is called again for IsDescendant.
    if (InstIndex == -1)
        return false;
    if (InstIndex == GetRootIndex())
        return true;
    return IsDescendant(GetRootIndex(), InstIndex);
}

CObject* CAttachedEquip::RetrieveDebrisObject()
{
    CObject* Out = Debris;
    Debris = nullptr;
    return Out;
}

CAttachedEquip* __cdecl CAttachedEquip::cast(CEquip* Equip)
{
    if (Equip == nullptr || (Equip->GetType() & kAttachedEquipMask) == 0)
        return nullptr;
    return (CAttachedEquip*)(Equip);
}

CAttachedEquip const* __cdecl CAttachedEquip::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

// ============================================================
// Archetype accessors and small retail members restored by name
// ============================================================

// Every leaf *Arch accessor is one ICF-folded body at listing 0x3fb90 (MOV EAX,[ECX+0xc]; RET):
// the base CEquip::Arch field, typed as the leaf archetype.
Archetype::AttachedEquipment const* CAttachedEquip::AttachedEquipArch() const
{
    return (Archetype::AttachedEquipment const*)(Arch);
}
