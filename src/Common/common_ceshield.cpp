// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEShieldGenerator, CEShield, CEShieldIntruderChecker (retail CEShield.cpp) -- split out of common_cequip.cpp (CMN-B2).
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
#include "common_physys.h"
#include "common_spatial.h"  // ISpatialPartition (ScanList::add_in_range bucket query)
#include "common_timing.h"   // Timing::GetGlobalTime (CEShieldGenerator / CEShield)
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "../IVP/ivp_physics/ivp_real_object.h"
#include "../IVP/ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)

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
    const char kCEShieldSource[] = "E:\\FL\\Scratch\\Source\\Common\\CEShield.cpp";
}

CEShieldGenerator::CEShieldGenerator(CEqObj* OwnerObj, unsigned short EquipId,
                                     Archetype::ShieldGenerator const* ArchObj, bool bMissionFlag)
    : CAttachedEquip(EquipTypeShieldGen, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // .text:0629cd30-0629cda1: CAttachedEquip(0x200, ...); +0x4c = +0x50 = 0 (+0x54 is not
    // written); +0x58 = Timing::GetGlobalTime(); then the direct, non-virtual
    // CEquip::Activate(true) (0629cd8b).
    LinkedShield = nullptr;
    Opaque50Value = 0.0f;
    LastUpdateTime = Timing::GetGlobalTime();
    CEquip::Activate(true);
}

// Common.dll.c 63483-63521 (0x3bc50): field-by-field copy across the CEquip/CExternalEquip/
// CAttachedEquip layers (delegated to CAttachedEquip's own copy ctor here), then LinkedShield
// (raw pointer copy, NOT re-linked into the target CEShield's generator list -- matches
// CEShield's own copy ctor's identical quirk) plus this class's own scalar fields.
CEShieldGenerator::CEShieldGenerator(CEShieldGenerator const& Other)
    : CAttachedEquip(Other)
{
    // .text:0629bce8-0629bcfe: +0x4c, +0x50, then +0x58/+0x5c; the +0x54 gap is not copied.
    LinkedShield = Other.LinkedShield;
    Opaque50Value = Other.Opaque50Value;
    LastUpdateTime = Other.LastUpdateTime;
}

CEShieldGenerator::~CEShieldGenerator()
{
    // Common.dll.c 64443-64465 (0x3cdb0): unlink from the shield, then explicitly call
    // CExternalEquip::Destroy() (CEquip::Destroy() + Disconnect()) before the implicit
    // CAttachedEquip::~CAttachedEquip() base-class teardown -- the previously ported version of
    // this destructor was missing the CExternalEquip::Destroy() call entirely.
    if (LinkedShield != nullptr)
    {
        LinkedShield->RemGen(this);
        LinkedShield = nullptr;
    }
    CExternalEquip::Destroy();
}

bool CEShieldGenerator::IsFunctioning() const
{
    // .text:0629ce20-0629ce4f: same shape as CEquip::IsFunctioning() -- vt+0x4 IsActive,
    // vt+0x10 IsDisabled (slot 4, not IsControllerEnabled), vt+0x8 IsDestroyed -- plus the
    // linked shield at +0x4c non-null.
    if (IsActive() && !IsDisabled() && !IsDestroyed() && LinkedShield != nullptr)
        return true;
    return false;
}

bool CEShieldGenerator::Update(float Dt, unsigned Flags)
{
    // Listing 0x0629ce60..0x0629cf24. The passed Dt is replaced (in its own argument slot) by
    // the real elapsed time from two Timing::GetGlobalTime() reads.
    (void)Dt;
    float RealDt = (float)(Timing::GetGlobalTime() - LastUpdateTime);
    LastUpdateTime = Timing::GetGlobalTime();

    if (IsFunctioning())   // vt+0xc
    {
        // Linked shield's relative health (vt+0x44) FCOMP 1.0, TEST AH,0x5 + JP: below 1.0
        // falls through to "recharging", not below / unordered does not.
        bool bRecharging = LinkedShield->GetRelativeHealth() < 1.0f;
        Archetype::ShieldGenerator const* GenArch = (Archetype::ShieldGenerator const*)(Arch);
        // Kept on the x87 stack in retail.
        double Draw = (double)(RealDt) * GenArch->ConstantPowerDraw;   // Arch+0x98
        if (bRecharging)
            Draw += (double)(RealDt) * GenArch->RebuildPowerDraw;       // Arch+0x9c
        // FCOMP Draw vs Owner->Power (+0x198), TEST AH,0x41 + JP: greater or unordered skips.
        double Power = Owner->Power;
        if (Draw <= Power)
        {
            Owner->Power = (float)(Power - Draw);
            if (bRecharging)
            {
                // vt+0x3c GetHitPoints + RealDt * Arch+0x90 -> vt+0x40 SetHitPoints.
                float NewHitPoints = (float)(
                    (double)(RealDt) * GenArch->RegenerationRate + LinkedShield->GetHitPoints());
                LinkedShield->SetHitPoints(NewHitPoints);
            }
        }
    }
    return CAttachedEquip::Update(RealDt, Flags);
}

void CEShieldGenerator::Destroy()
{
    // Common.dll.c 64535-64545 (0x3cf30): same unlink-then-CExternalEquip::Destroy() shape as
    // the destructor above, callable independently of destruction.
    if (LinkedShield != nullptr)
    {
        LinkedShield->RemGen(this);
        LinkedShield = nullptr;
    }
    CExternalEquip::Destroy();
}

void CEShieldGenerator::LinkShield(CEShield* Shield)
{
    // .text:0629cf50-0629cf5d: +0x4c = Shield, then tail-jump CEShield::AddGen(this) on it --
    // no null test.
    LinkedShield = Shield;
    Shield->AddGen(this);
}

bool CEShieldGenerator::IsLinked() const
{
    // Common.dll.c 0x3bc40: return *(int*)(this+0x4c) != 0;
    return LinkedShield != nullptr;
}

void CEShieldGenerator::UnLinkShield()
{
    // Common.dll.c 0x3cf70: if still linked, remove this generator from the shield's
    // generator list, then clear our own back-pointer. Same shape as the destructor's
    // teardown (~CEShieldGenerator), but callable independently of destruction.
    if (LinkedShield != nullptr)
    {
        LinkedShield->RemGen(this);
        LinkedShield = nullptr;
    }
}

CEShieldGenerator* __cdecl CEShieldGenerator::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeShieldGen)
        return nullptr;
    return (CEShieldGenerator*)(Equip);
}

CEShieldGenerator const* __cdecl CEShieldGenerator::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

CEShield::CEShield(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Shield const* ArchObj,
                   bool bMissionFlag)
    : CAttachedEquip(EquipTypeShield, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // Common.dll.c 0x3bd70 (raw .txt 112814-112966). Retail's own field inits, in order.
    bOnline = 0;
    CurrentHitPoints = 0.0f;
    OfflineEndTime = 0.0;
    MissionFlagCopy = bMissionFlag ? 1 : 0;
    Gens = new std::vector<CEShieldGenerator*>();
    DominantGenArch = nullptr;
    OfflineThreshold = 0.0f;
    OfflineRebuildTime = 0.0f;
    MaxHitPoints = 0.0f;
    IntruderChecker = nullptr;

    // Retail re-writes the inherited CAttachedEquip::Fate field (default 1) to 3 right after
    // the vtable is stamped to CEShield's own -- reproduced as-is, semantics not identified.
    Fate = 3;

    // Retail's ctor tail is CEquip::Activate(this, true) immediately followed by a manual
    // inline copy of CEShield::Activate(true)'s own body (IsActive() check, GetGlobalTime/
    // GetRelativeHealth/intruder-idle gate, Attachment->EnableCollisions toggle, Notify) --
    // i.e. the compiler inlined the ctor's call to the virtual Activate(true), which in a ctor
    // always statically resolves to this class's own override. Calling Activate(true) here
    // reproduces that exactly without duplicating its logic a second time.
    Activate(true);
}

CEShield::CEShield(CEShield const& Other)
    : CAttachedEquip(Other)
{
    // Common.dll.c 0x3ba70 (raw .txt 112482-112616). Own fields past CAttachedEquip's 0x4c
    // bytes, plus a fresh heap copy of the generator list (retail copies the inline VC6 vector
    // element-by-element without re-linking each CEShieldGenerator::LinkedShield back to this
    // new object -- matching CEShieldGenerator's own copy ctor's identical quirk).
    bOnline = Other.bOnline;
    CurrentHitPoints = Other.CurrentHitPoints;
    OfflineEndTime = Other.OfflineEndTime;
    MissionFlagCopy = Other.MissionFlagCopy;
    Gens = new std::vector<CEShieldGenerator*>(*Other.Gens);
    DominantGenArch = Other.DominantGenArch;
    OfflineThreshold = Other.OfflineThreshold;
    OfflineRebuildTime = Other.OfflineRebuildTime;
    MaxHitPoints = Other.MaxHitPoints;
    IntruderChecker = Other.IntruderChecker;
}

CEShield::~CEShield()
{
    // Listing 0x0629bf00..0x0629bfd4: the body is CEShield::Destroy() inlined (a virtual call
    // in a destructor binds statically), then the generator vector's own destructor, then
    // ~CAttachedEquip.
    CEShield::Destroy();
    delete Gens;
    Gens = nullptr;
}

void CEShield::Destroy()
{
    // Listing 0x0629c110..0x0629c17f: unlink every generator (Gen+0x4c = 0, no null test),
    // erase the generator vector, delete the intruder checker (inlined destructor), then
    // CExternalEquip::Destroy.
    unsigned Index = 0;
    for (; Index < Gens->size(); ++Index)
        (*Gens)[Index]->LinkedShield = nullptr;
    Gens->clear();
    if (IntruderChecker != nullptr)
    {
        // The inlined ~CEShieldIntruderChecker (0629c15b..0629c16a) calls DestroyPhantom only
        // when the phantom is ALREADY null (CMP [EDI+8],0 + JNZ skips a live one), passing 0.
        if (IntruderChecker->Phantom == nullptr)
            PhySys::DestroyPhantom(nullptr);
        delete IntruderChecker;
        // Very strange, but retail: +0x80 is left dangling (0629c172-0629c17f), so a later
        // ~CEShield would delete it a second time. Unreachable -- no code in Common.dll ever
        // creates a checker -- and reproduced as is.
    }
    CExternalEquip::Destroy();
}

bool CEShield::GetEquipDesc(EquipDesc& Desc) const
{
    // Common.dll.c 63324-63329 (listing 0x3ba10, `?GetEquipDesc@CEShield@@UBE_NAAUEquipDesc@@@Z`):
    // the raw asm at this symbol's address doesn't correspond to an isolated function body --
    // ICF appears to have folded this override onto shared "return false" stub code -- but the
    // pseudocode is unambiguous and self-consistent: retail's CEShield::GetEquipDesc is an
    // unconditional `return false`, unlike every sibling override, which forwards through
    // CEquip::GetEquipDesc.
    (void)Desc;
    return false;
}

bool CEShield::IsFunctioning() const
{
    // .text:0629bfe0-0629c00f: vt+0x4 IsActive, vt+0x10 IsDisabled (slot 4, not
    // IsControllerEnabled), vt+0x8 IsDestroyed, then the byte bOnline (+0x4c).
    return IsActive() && !IsDisabled() && !IsDestroyed() && bOnline != 0;
}

bool CEShield::CanActivate() const
{
    // Listing 0x0629ca50..0x0629ca96.
    // FCOMP double [+0x58], TEST AH,0x1 + JNZ: below or unordered -> false.
    if (!(Timing::GetGlobalTime() >= OfflineEndTime))
        return false;
    // FCOMP [+0x74], TEST AH,0x41 + JNZ: less, equal or unordered -> false.
    if (!(GetRelativeHealth() > OfflineThreshold))
        return false;
    // An existing intruder checker must report no intruders (+0xc == 0).
    if (IntruderChecker != nullptr && IntruderChecker->IntruderCount != 0)
        return false;
    return true;
}

bool CEShield::Activate_Internal(bool bOn)
{
    // Listing 0x0629cad0..0x0629cb49.
    if (bOnline != (unsigned char)(bOn))
    {
        if (Attachment != nullptr)
        {
            Attachment->EnableCollisions(bOn);   // vt+0xc
            if (IntruderChecker != nullptr)
                PhySys::SetCollisionStatePhantom(IntruderChecker->Phantom, !bOn);
            bOnline = bOn ? 1 : 0;
            Notify(INotify::ShieldOfflineStateChanged, &bOnline);
            return bOnline != 0;
        }
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCEShieldSource, 0x195,
              "Tried to change change activation state on non-connected shield.");
    }
    return bOnline != 0;
}

bool CEShield::Activate(bool bOn)
{
    // Common.dll.c 0x3c190.
    CEquip::Activate(bOn);
    if (IsActive())
    {
        if (CanActivate())
            Activate_Internal(true);
        return true;
    }
    if (bOnline != 0)
        Activate_Internal(false);
    return false;
}

bool CEShield::Update(float Dt, unsigned Flags)
{
    // Common.dll.c 0x3c020.
    if (!IsFunctioning() && IsActive() && CanActivate())
    {
        Notify(INotify::ShieldCanReactivate, nullptr);
        Activate_Internal(true);
    }
    return CAttachedEquip::Update(Dt, Flags);
}

void CEShield::SetHitPoints(float Hit)
{
    // Common.dll.c 0x3c2d0.
    if (MaxHitPoints < Hit)
        Hit = MaxHitPoints;
    CurrentHitPoints = Hit;

    if (IsFunctioning())
    {
        if (GetRelativeHealth() <= OfflineThreshold)
        {
            Notify(INotify::ShieldWentOffline, nullptr);
            OfflineEndTime = Timing::GetGlobalTime() + OfflineRebuildTime;
            Activate_Internal(false);
        }
    }
    else if (IsActive() && CanActivate())
    {
        Notify(INotify::ShieldCanReactivate, nullptr);
        Activate_Internal(true);
    }
}

float CEShield::GetHitPoints() const
{
    // Common.dll.c 63344-63352 (COMDAT-folded with CEEngine::GetCruiseChargeTimeElapsed).
    return CurrentHitPoints;
}

bool CEShield::Connect(char const* HpName)
{
    // Common.dll.c 0x3c470 (listing 113580-113702).
    if (!CAttachedEquip::Connect(HpName))
        return false;

    // Listing 0629c48b-0629c4cd, no Attachment or owner test: PhysicsOwner =
    // Attachment->GetPhysicsOwner() (vtable+8), RootIndex = Attachment->GetRootIndex()
    // (vtable+4), then a hand-inlined scan of PhysicsOwner's PartMap ([+0x68, +0x6c), step 0xc)
    // for the entry whose EngineInstanceId matches RootIndex, its PartId (or 0 if absent)
    // flagged as the shield's part -- exactly CObject::inst_to_part() feeding
    // CObject::flag_part_as_shield(). Then Attachment->EnableCollisions(bOnline) (vtable+0xc).
    CObject* PhysicsOwner = Attachment->GetPhysicsOwner();
    PhysicsOwner->flag_part_as_shield(PhysicsOwner->inst_to_part(Attachment->GetRootIndex()));
    Attachment->EnableCollisions(bOnline != 0);

    if (IntruderChecker != nullptr)
    {
        // .text:0629c4d0..0629c4f8: FUN_0629cb70 on the checker, then its phantom's collision
        // state = !bOnline (the same two steps AttachIntruderChecker makes).
        IntruderChecker->Attach(this);
        PhySys::SetCollisionStatePhantom(IntruderChecker->Phantom, bOnline == 0);
    }

    // Listing 0629c4fb-0629c58b: broadcasts the newly-connected shield's physical sphere
    // (center + radius) to the engine as the root instance's "extent", the same instance-extent
    // channel other systems query for coarse collision/LOS bounds. Sequence, traced
    // instruction-by-instruction (the Ghidra .c for this tail is not trusted -- see below):
    // PhysicsOwner->get_physical_radius_r(WorldCenter) [CObject vtable+0x64] gets the radius and
    // a world-space center; IEngine::GetInstanceTransform(Root) [IEngine vtable+0xa0, same slot
    // as CExternalEquip::GetConnectionPosition's XformPtr fetch above] gets the root instance's
    // world transform; I3DMathEngine::InverseTransform(LocalCenter, *Xform, WorldCenter)
    // [x86math.h vtable+0x3c] brings WorldCenter into the root instance's local space; then
    // IEngine::SetInstanceExtent(Root, 1, Radius, &LocalCenter) [IEngine vtable+0x84, matches
    // engbase.h's documented slot 33] publishes it.
    //
    // A prior pass flagged the 3rd SetInstanceExtent arg (raw disasm dword ptr [ESP+0x2c] at
    // 0629c580, Ghidra's "unaff_EBX") as uncomputed caller-context garbage and left this whole
    // tail unported. Re-traced the full ESP ledger from the FSTP at 0629c519 (which stores
    // get_physical_radius_r's float return to [ESP+0x28]) through every intervening
    // PUSH/CALL/POP up to the 0629c580 read: the two intermediate DACOM calls
    // (GetInstanceTransform, InverseTransform) are both self-cleaning (stdcall) and net zero
    // extra stack depth by the time each returns, and the single PUSH at 0629c57b (&LocalCenter)
    // accounts for exactly the +4 needed to turn [ESP+0x28] into [ESP+0x2c] -- i.e. it IS the
    // Radius local from this same function, not garbage. Corrected and ported below.
    // 0629c4fb-0629c585, no null tests: Root first (vtable+4), then the physics owner
    // (vtable+8); its vtable+0x64 radius is stored as float (FSTP 0629c519); the math engine
    // comes from the lazily filled per-TU cache DAT_063fc204 (FUN_06391cf0 =
    // Acquire3DMathEngine).
    long const Root = Attachment->GetRootIndex();
    CObject* RadiusOwner = Attachment->GetPhysicsOwner();
    Vector WorldCenter;
    float const Radius = RadiusOwner->get_physical_radius_r(WorldCenter);
    FTransform const* InstXform = (FTransform const*)DALib::Engine->GetInstanceTransform((void*)Root);
    I3DMathEngine* Math = Acquire3DMathEngine();
    Vector LocalCenter;
    Math->InverseTransform((FVector&)LocalCenter, *InstXform, (FVector const&)WorldCenter); // +0x3c
    DALib::Engine->SetInstanceExtent((void*)Root, 1, Radius, (float const*)&LocalCenter);   // +0x84

    return true;
}

void CEShield::Disconnect()
{
    // Listing 0x0629c5a0..0x0629c5cc: DestroyIntruderChecker inlined, then tail-jump to
    // CAttachedEquip::Disconnect.
    DestroyIntruderChecker();
    CAttachedEquip::Disconnect();
}

void CEShield::AttachIntruderChecker()
{
    // Listing 0x0629c950..0x0629c97f.
    if (IntruderChecker != nullptr)
    {
        IntruderChecker->Attach(this);   // FUN_0629cb70
        PhySys::SetCollisionStatePhantom(IntruderChecker->Phantom, bOnline == 0);
    }
}

void CEShield::DestroyIntruderChecker()
{
    // Listing 0x0629c980..0x0629c9a6.
    CEShieldIntruderChecker* Checker = IntruderChecker;
    if (Checker != nullptr)
    {
        Checker->IntruderCount = -1;
        PhySys::DestroyPhantom(Checker->Phantom);
        Checker->Phantom = nullptr;
    }
}

void CEShieldIntruderChecker::added(CObject* Obj)
{
    // .text:0629cd10: INC dword [ECX+0xc]; RET 4.
    (void)Obj;
    ++IntruderCount;
}

void CEShieldIntruderChecker::deleted(CObject* Obj)
{
    // .text:0629cd20: DEC dword [ECX+0xc]; RET 4.
    (void)Obj;
    --IntruderCount;
}

void CEShieldIntruderChecker::Attach(CEShield* Shield)
{
    // FUN_0629cb70 (.text:0629cb70..0629ccff).
    if (Phantom != nullptr)
    {
        IntruderCount = -1;
        PhySys::DestroyPhantom(Phantom);
        Phantom = nullptr;
    }
    if (Shield == nullptr)
        return;
    CObject* Body = Shield->GetPhysicsOwner();   // CAttachedEquip::GetPhysicsOwner (direct)
    if (Body == nullptr)
        return;

    // 0x0629cbb3..0x0629cc5f: PhantomParms {system id, 0, 1, collision group name, connection
    // position, connection orientation (identity before the call), listener = this}.
    PhySys::PhantomParms Parms;
    Parms.SystemId = 0;
    Parms.Flag04 = 0;
    Parms.Flag05 = 0;
    Parms.Position.x = 0.0f;
    Parms.Position.y = 0.0f;
    Parms.Position.z = 0.0f;
    MatrixSetIdentity(Parms.Orientation);
    Parms.Listener = nullptr;
    char const* Group = PhySys::GetCollisionGroup(Body);
    unsigned Byte = 0;
    do
    {
        Parms.Name[Byte] = Group[Byte];   // inline strcpy, unbounded as in retail
    } while (Group[Byte++] != '\0');
    Parms.SystemId = Body->PhysicsSystemId;   // Body+0x50
    Shield->GetConnectionPosition(&Parms.Position, &Parms.Orientation);   // vt+0x48
    Parms.Listener = this;
    Parms.Flag05 = 1;

    // Part id = the CRC half of IEngine::GetInstanceName(GetRootIndex()) (+0x68), looked up on
    // the shield's archetype.
    FEngineName Name;
    DALib::Engine->GetInstanceName(&Name, (void*)Shield->GetRootIndex());   // vt+0x84
    PhyArch::Part const* Part = Shield->Arch->get_phy_part(Name.Crc);
    if (Part != nullptr)
    {
        PhySys::CreatePhantom(Parms, *Part, Phantom);
        PhySys::AttachPhantom(Body, Phantom);
        IntruderCount = 0;
        return;
    }
    FDUMP(0x100001,
          "%s(%d) : *** ERROR: Couldn't find shield geometry for shield intruder-checker: %s.",
          kCEShieldSource, 0x1e9, Body->Archetype->Nickname.get_string());
}

void CEShield::ShieldBatteryBoost(float Amount)
{
    // .text:0629caa0-0629cac5: FLD MaxHitPoints FCOMP 0.0, TEST AH,0x41 / JNZ: not greater
    // (or unordered) -> return. Else OfflineEndTime = 0 and tail-jump vt+0x40 SetHitPoints with
    // the argument unchanged -- the caller passes the new absolute hit points.
    if (MaxHitPoints > 0.0f)
    {
        OfflineEndTime = 0.0;
        SetHitPoints(Amount);
    }
}

void CEShield::AddGen(CEShieldGenerator* Gen)
{
    // .text:0629c5e0-0629c776 (Ghidra's decompilation of this function is corrupted; ported
    // from the listing). No Gen or Gen->Arch test.
    Archetype::ShieldGenerator* GenArch = (Archetype::ShieldGenerator*)Gen->Arch;

    // 0629c5ea-0629c611: no dominant yet, or its Toughness (+0x84, signed CMP / JGE) below the
    // new one -> the new generator's archetype.
    if (DominantGenArch == nullptr || DominantGenArch->Toughness < GenArch->Toughness)
        DominantGenArch = GenArch;

    // 0629c614-0629c646: FILD size; FMUL threshold; FADD Arch+0xa0; FIDIV (size + 1) -- one
    // x87 expression, rounded once by the FSTP float.
    int const ExistingCount = (int)Gens->size();
    OfflineThreshold = (float)(((double)ExistingCount * (double)OfflineThreshold +
                                (double)GenArch->OfflineThreshold) /
                               (double)(ExistingCount + 1));
    // 0629c64b-0629c661: FLD Arch+0xa4 FCOMP [+0x78], TEST AH,0x41 / JNZ: only a strictly
    // greater (ordered) rebuild time replaces the current one.
    if (OfflineRebuildTime < GenArch->OfflineRebuildTime)
        OfflineRebuildTime = GenArch->OfflineRebuildTime;
    // 0629c664-0629c67b: MaxCapacity + MaxHitPoints, one FSTP.
    MaxHitPoints = GenArch->MaxCapacity + MaxHitPoints;

    // 0629c67e-0629c68b: vt+0x40 SetHitPoints(vt+0x3c GetHitPoints() + MaxCapacity) -- both
    // CEShield's own overrides (may bring the shield back online).
    SetHitPoints(GetHitPoints() + GenArch->MaxCapacity);

    // 0629c68e-0629c806: the inlined vector insert at end().
    Gens->push_back(Gen);

    // Retail's trailing 2-zero-arg virtual call after push_back resolves through this class's
    // own vtable+0x24 slot, which is CEShield::Update (not CEquip::Update, as an earlier pass
    // assumed) -- a real side effect that may reactivate a shield that was starved for
    // generators.
    Update(0.0f, 0);
}

void CEShield::RemGen(CEShieldGenerator* Gen)
{
    // Common.dll.c 0x3c820: not just an erase -- retail resets DominantGenArch/OfflineThreshold/
    // OfflineRebuildTime/MaxHitPoints to zero and rebuilds them from every remaining generator
    // (the aggregates aren't simply reversible since OfflineThreshold is an average).
    // .text:0629c820-0629c94a. No Gen, element or element Arch test anywhere.
    Archetype::ShieldGenerator* OldDominantGenArch = DominantGenArch;

    DominantGenArch = nullptr;
    OfflineThreshold = 0.0f;
    OfflineRebuildTime = 0.0f;
    MaxHitPoints = 0.0f;

    for (std::vector<CEShieldGenerator*>::iterator It = Gens->begin(); It != Gens->end(); )
    {
        if (*It == Gen)
        {
            // Retail checks every slot unconditionally (no "already removed" latch), so a
            // duplicate pointer in the list -- never expected in practice -- would be removed
            // more than once too; reproduced as-is rather than adding a guard retail lacks.
            It = Gens->erase(It);
            continue;
        }

        // 0629c879-0629c8c8: the element's Arch (+0xc) read directly; each float sum is
        // rounded by its own FSTP.
        Archetype::ShieldGenerator* RemArch = (Archetype::ShieldGenerator*)(*It)->Arch;
        if (DominantGenArch == nullptr || DominantGenArch->Toughness < RemArch->Toughness)
            DominantGenArch = RemArch;
        OfflineThreshold = RemArch->OfflineThreshold + OfflineThreshold;  // running sum; averaged below
        if (OfflineRebuildTime < RemArch->OfflineRebuildTime)
            OfflineRebuildTime = RemArch->OfflineRebuildTime;
        MaxHitPoints = RemArch->MaxCapacity + MaxHitPoints;
        ++It;
    }

    // Common.dll.c 64143-64145: fires a HUD-relevant equipment-change notification when the
    // dominant generator's archetype identity actually changed. Retail requires the new
    // DominantGenArch to be non-null and compares Nickname (Root+0x08, an interned ID_String)
    // rather than the raw archetype pointer -- two CEShieldGenerator instances of the same
    // equipment type share one Archetype::ShieldGenerator object in practice, but this is what
    // retail actually tests.
    // 0629c8d1-0629c8e9: the old dominant archetype is dereferenced without a null test (it
    // cannot be null while a generator remains: every AddGen sets it).
    if (DominantGenArch != nullptr && DominantGenArch->Nickname.Id != OldDominantGenArch->Nickname.Id)
    {
        Notify(INotify::ShieldGeneratorSetChanged, nullptr);
    }

    // 0629c8ee-0629c920: non-empty -> FILD qword size; FDIVR threshold; FSTP float.
    unsigned const RemainingCount = (unsigned)Gens->size();
    if (RemainingCount != 0)
        OfflineThreshold = (float)((double)OfflineThreshold / (double)RemainingCount);

    // Retail also calls SetHitPoints(0) when MaxHitPoints drops to exactly 0, and unconditionally
    // tail-calls this class's own Update(0,0) afterwards -- both real side effects under the
    // corrected layout (see AddGen's matching fix).
    if (MaxHitPoints == 0.0f)
        SetHitPoints(0.0f);

    Update(0.0f, 0);
}

CEShield* __cdecl CEShield::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeShield)
        return nullptr;
    return (CEShield*)(Equip);
}

CEShield const* __cdecl CEShield::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

float CEShield::GetMaxHitPoints() const
{
    // Common.dll.c 0x3ba20
    return MaxHitPoints;
}

float CEShield::GetOfflineThreshold() const
{
    // Common.dll.c 0x3ba40
    return OfflineThreshold;
}

float CEShield::GetOfflineRebuildTime() const
{
    // Common.dll.c 0x3ba50
    return OfflineRebuildTime;
}

Archetype::ShieldGenerator const* CEShield::ShieldGenArch() const
{
    // Common.dll.c 0x3ba60: returns the dominant generator's archetype (highest-Toughness
    // linked CEShieldGenerator, see AddGen at 0x3c5e0), not any particular CEShieldGenerator*.
    return DominantGenArch;
}

double CEShield::GetWeaponModifier(ID_String const& WeaponType) const
{
    // .text:0629c9b0-0629ca3e: Sum (float local) += WeaponModDB_GetShieldMod(WeaponType,
    // Gen->Arch->ShieldType (+0x8c)) per generator (FUN_06341c70 returns an exact FLD float;
    // FADD + FSTP float per iteration; no Gen / Arch test). Then FILD qword size; FCOM 0.0,
    // TEST AH,0x44 / JNP: zero -> 1.0f (.rdata:0639a1d0); else FDIVR -> Sum / size left
    // unrounded in ST0, hence double.
    float Sum = 0.0f;
    unsigned const Count = (unsigned)Gens->size();
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        Archetype::ShieldGenerator const* GenArch = (*Gens)[Index]->ShieldGenArch();
        Sum = WeaponModDB_GetShieldMod(WeaponType.Id, GenArch->ShieldType.Id) + Sum;
    }
    if (Count == 0)
        return 1.0f;
    return (double)Sum / (double)Count;
}

Archetype::ShieldGenerator const* CEShieldGenerator::ShieldGenArch() const
{
    return (Archetype::ShieldGenerator const*)(Arch);
}

Archetype::Shield const* CEShield::ShieldArch() const
{
    return (Archetype::Shield const*)(Arch);
}

bool CEShield::IsLootable() const
{
    // Listing 0x3ba00 (ICF-shared): XOR AL,AL; RET.
    return false;
}

void CEShield::SetFate(DamageEntry::SubObjFate FateValue)
{
    // Listing 0x8d730 (ICF-shared): RET 0x4.
    (void)FateValue;
}
