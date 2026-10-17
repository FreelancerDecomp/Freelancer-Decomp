// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEquip, CInternalEquip, CExternalEquip, CEquipTraverser, CEquipManager -- split out of common_cequip.cpp (CMN-B2).
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
    const char kCExternalEquipSource[] = "E:\\FL\\Scratch\\Source\\Common\\CExternalEquip.cpp";
    const unsigned kInternalListMask = 0xc10003u;
    const unsigned kExternalListMask = 0xff3efffcu;
    const unsigned kExternalEquipMask = 0x1fe3u;
    const unsigned kInternalEquipMask = 0x1ff0000u;

    // .data DAT_063fc198: CEquip::IdentifyEquipment's 0x40-byte result buffer.
    char GIdentifyEquipmentBuf[0x40];

    // Retail keeps the list node in CEquipTraverser+0xc and steps node->next
    // (.text:0629b493 / 0629b4c7); the port keeps the CEquip* and rescans from begin() to step
    // past it -- the same element while the list is unchanged between calls. The payload null,
    // type-mask and IsDestroyed (vtbl+0x8, unless bIncludeDestroyed) tests are retail's
    // (.text:0629b4d6..0629b4fb).
    CEquip* NextMatching(std::list<CEquip*>* List, CEquip* After, unsigned TypeMask,
                         bool bIncludeDestroyed)
    {
        bool bSkip = After != nullptr;
        std::list<CEquip*>::iterator It = List->begin();
        for (; It != List->end(); ++It)
        {
            CEquip* Equip = *It;
            if (bSkip)
            {
                if (Equip == After)
                    bSkip = false;
                continue;
            }
            if (Equip == nullptr)
                continue;
            if ((TypeMask & Equip->GetType()) == 0)
                continue;
            if (!bIncludeDestroyed && Equip->IsDestroyed())
                continue;
            return Equip;
        }
        return nullptr;
    }

    // Mirror of NextMatching for CEquipManager::ReverseTraverse (Common.dll.c 0x3b530/0x3b540):
    // walks the list back-to-front instead of front-to-back.
    CEquip* PrevMatching(std::list<CEquip*>* List, CEquip* Before, unsigned TypeMask,
                        bool bIncludeDestroyed)
    {
        bool bSkip = Before != nullptr;
        std::list<CEquip*>::reverse_iterator It = List->rbegin();
        for (; It != List->rend(); ++It)
        {
            CEquip* Equip = *It;
            if (bSkip)
            {
                if (Equip == Before)
                    bSkip = false;
                continue;
            }
            if (Equip == nullptr)
                continue;
            if ((TypeMask & Equip->GetType()) == 0)
                continue;
            if (!bIncludeDestroyed && Equip->IsDestroyed())
                continue;
            return Equip;
        }
        return nullptr;
    }
}

namespace CmnCequip
{
    // The Watchable a BaseWatcher currently watches (null when the target died), as the raw
    // pointer the action classes then step back 8 bytes from to reach the IObjRW.
    void* WatchedTarget(BaseWatcher const& Watcher)
    {
        return (void*)Watcher.target();
    }
}

CEquip::CEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
               Archetype::Equipment const* ArchObj, bool bMissionFlag)
{
    Owner = OwnerObj;
    Id = EquipId;
    // Retail takes the archetype as const (PBU in every CE* constructor's mangling) and later
    // calls non-const members on it (load/Release/create_instance) through the same field.
    Arch = const_cast<Archetype::Equipment*>(ArchObj);
    bActive = 0;
    bDestroyed = 0;
    bMission = bMissionFlag ? 1 : 0;
    Type = TypeBits;
    NotifyA = nullptr;
    NotifyB = nullptr;
}

CEquip::CEquip()
{
    // Listing 0x26d60: Owner, Arch, Type and both notify sinks zeroed, Id = SUBOBJ_ID_NONE,
    // bActive (+0x10) and bMission (+0x12) cleared. bDestroyed (+0x11) is left untouched.
    Owner = nullptr;
    Id = SUBOBJ_ID_NONE;
    Arch = nullptr;
    bActive = 0;
    bMission = 0;
    Type = 0;
    NotifyA = nullptr;
    NotifyB = nullptr;
}

CEquip::~CEquip()
{
}

void* CEquip::operator new(size_t Size) noexcept
{
    // Listing 0x9d560: JMP msvcrt!malloc.
    return malloc(Size);
}

void CEquip::operator delete(void* Block)
{
    // Listing 0x9d570: JMP msvcrt!free.
    free(Block);
}

// Common.dll.c 0x3afd0 (ICF-folded with DamageList::set_inflictor_owner_player,
// which is unrelated -- both just store a pointer at a fixed offset).
void CEquip::ConnectServerEquip(INotify* NotifyParam)
{
    NotifyA = NotifyParam;
}

// Common.dll.c 0xbd950 (ICF-folded with CPlayerTradeOffer::SetMoneyOffered and
// EquipDesc::set_owner, which are unrelated -- all just store a value at a
// fixed offset).
void CEquip::ConnectClientEquip(INotify* NotifyParam)
{
    NotifyB = NotifyParam;
}

void CEquip::NotifyDisconnecting(INotify* NotifyParam)
{
    // Common.dll.c 0x3afe0: clears whichever of NotifyA/NotifyB matches.
    if (NotifyA == NotifyParam) {
        NotifyA = nullptr;
        return;
    }
    if (NotifyB == NotifyParam) {
        NotifyB = nullptr;
    }
}

// Common.dll.c 0x3b010: dispatches to both registered listeners' vtable slot 0
// (INotify::notify), in NotifyA-then-NotifyB order, each only if non-null.
void CEquip::Notify(INotify::Event Code, void* Param)
{
    if (NotifyA != nullptr)
        NotifyA->notify(Code, Param);
    if (NotifyB != nullptr)
        NotifyB->notify(Code, Param);
}

bool CEquip::IsActive() const
{
    return bActive != 0;
}

bool CEquip::IsDestroyed() const
{
    return bDestroyed != 0;
}

bool CEquip::IsFunctioning() const
{
    // .text:06286d20-06286d4c: vt+0x4 IsActive, vt+0x10 IsDisabled, vt+0x8 IsDestroyed;
    // true only for active, not disabled, not destroyed. vt+0x10 is slot 4 = IsDisabled
    // (ordinal 1762 ?IsDisabled@CEquip@@UBE_NXZ). Its base body 0629ba00 (`XOR AL,AL; RET`)
    // is ICF-folded with EnableController / IsControllerEnabled, but that fold only makes
    // them equal in CEquip's own table: CEThruster (0639d744) keeps slot 4 = 0629ba00 while
    // overriding IsControllerEnabled (slot 21, 0629d290 = [+0x5c], true once registered),
    // and CELauncher overrides IsDisabled. Calling IsControllerEnabled here made every
    // registered thruster "not functioning", so CEThruster::do_simulation_controller
    // returned at 0629d2e8 and the afterburner pushed no force (CMN-BUG10).
    if (IsActive()) {
        if (!IsDisabled() && !IsDestroyed())
            return true;
    }
    return false;
}

bool CEquip::Activate(bool bOn)
{
    // Common.dll.c 0x3aed0: on an actual 0->1 or 1->0 transition (not a same-state call),
    // retail notifies NotifyA/NotifyB with event 0xb (activating) or 0xc (deactivating) and
    // a null context, before flipping bActive.
    if (bOn) {
        if (bActive == 0) {
            Notify(INotify::EquipActivating, nullptr);
        }
        bActive = 1;
    } else {
        if (bActive != 0) {
            Notify(INotify::EquipDeactivating, nullptr);
        }
        bActive = 0;
    }
    return IsActive();
}

void CEquip::Destroy()
{
    // Common.dll.c 0x3af40: retail notifies NotifyA/NotifyB with event 0xa and this
    // equip's Id (zero-extended to a pointer-sized context value) after setting bDestroyed.
    bDestroyed = 1;
    Notify(INotify::EquipDestroying, (void*)(uintptr_t)Id);
}

float CEquip::GetMaxHitPoints() const
{
    // .text:0629af70: MOV EAX,[this+0xc]; FLD [EAX+0x1c]; RET -- no Arch null test.
    return Arch->HitPts;
}

void CEquip::SetHitPoints(float Hit)
{
    // Retail 0x8d730 (ICF-folded): RET 4 — empty function body.
    // Shared with ~18 other trivial functions via ICF folding.
    // Base CEquip ignores SetHitPoints; subclasses like CAttachedEquip override.
    (void)Hit;
}

bool CEquip::IsDisabled() const
{
    return false;
}

bool CEquip::EnableController()
{
    return false;
}

bool CEquip::DisableController()
{
    // Common.dll.c 0x3ba00
    return false;
}

bool CEquip::IsControllerEnabled() const
{
    return false;
}

bool CEquip::Update(float Dt, unsigned Flags)
{
    (void)Dt;
    (void)Flags;
    return !IsDestroyed();
}

float CEquip::GetHitPoints() const
{
    // Common.dll.c 0x3af80 / raw asm: a pure tail-jump to vtable+0x38 (GetMaxHitPoints),
    // no destroyed check at all -- confirmed via Common.dll.txt:110779-110780
    // ("MOV EAX,[this]; JMP [EAX+0x38]"). The previous version of this port added an
    // IsDestroyed() early-return that retail does not have; removed to match retail
    // exactly (subclasses that need a destroyed-aware GetHitPoints already override it,
    // e.g. CAttachedEquip::GetHitPoints, CECargo::GetHitPoints).
    return GetMaxHitPoints();
}

double CEquip::GetRelativeHealth() const
{
    // .text:0629af90-0629afc0: Max = vt+0x38 (FST float copy); FCOMP 0.0f, TEST AH,0x44 / JNP
    // -> 0.0f only on equal (NaN divides); else vt+0x3c GetHitPoints FDIV the float Max, the
    // quotient left unrounded in ST0.
    float const MaxHit = GetMaxHitPoints();
    if (MaxHit == 0.0f)
        return 0.0f;
    return (double)GetHitPoints() / (double)MaxHit;
}

bool CEquip::IsTemporary() const
{
    return bMission != 0;
}

bool CEquip::CanDelete() const
{
    // Common.dll.c 0x3adc0: equipment attached to a solar (station/planet/etc, the only
    // retail CObject::Class whose bits satisfy (class & 0x303) == 0x303 -- ClassSolar
    // itself is literally 0x303) can never be deleted; everything else follows IsDestroyed().
    if (Owner != nullptr && (Owner->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar)
        return false;
    return IsDestroyed();
}

bool CEquip::IsLootable() const
{
    // .text:0629ade0..0629adff: lootable iff the archetype allows it (+0x6c, read with no Arch
    // test) AND it maps to a real sellable Good (Arch2Good(Arch+8) != 0).
    if (Arch->bLootable)
        return Arch2Good(Arch->get_id()) != 0;
    return false;
}

void CEquip::GetStatus(EquipStatus& OutStatus) const
{
    // .text:0629ae80..0629aec3: sets three independent bits of OutStatus.Flags (documented in
    // common_iface.h next to the EquipStatus struct): bit1 = IsDestroyed (vtbl+0x8), bit0 =
    // IsActive (vtbl+0x4), bit2 = IsDisabled (vtbl+0x10, slot 4). Order matches retail.
    if (IsDestroyed())
        OutStatus.Flags |= EquipStatusFlagDestroyed;
    else
        OutStatus.Flags &= ~EquipStatusFlagDestroyed;

    if (IsActive())
        OutStatus.Flags |= EquipStatusFlagActive;
    else
        OutStatus.Flags &= ~EquipStatusFlagActive;

    if (IsDisabled())
        OutStatus.Flags |= EquipStatusFlagDisabled;
    else
        OutStatus.Flags &= ~EquipStatusFlagDisabled;
}

bool CEquip::GetConnectionPosition(Vector* OutPos, Matrix* OutMatrix) const
{
    // Common.dll.c 0xcc0c0: base default is unconditional `return false` (params unused).
    // CExternalEquip::GetConnectionPosition (0x3ee00) overrides this with real joint/matrix
    // lookup logic that is not yet ported.
    (void)OutPos;
    (void)OutMatrix;
    return false;
}

void CEquip::NotifyArchGroupDestroyed(unsigned short SubObjId)
{
    // Common.dll.txt 0x8d730. This retail vtable slot (??_7CEquip@@6B@+0x1c) is ICF-folded
    // together with ~20 unrelated symbols (that fold's own XREF comment list names
    // CEquip::NotifyArchGroupDestroyed explicitly). The folded target's actual raw bytes are a
    // bare `RET 0x4`: a genuine empty no-op in retail's base implementation. Called by
    // CEqObj::destroy_equipment on every equip when a DamageEntry targets an arch-group
    // SubObjId; kept as a real virtual so a future override can hook it.
    (void)SubObjId;
}

bool CEquip::GetEquipDesc(EquipDesc& Desc) const
{
    // Common.dll.c 0x3ae10: bails out (returns false) if the equip is destroyed;
    // otherwise fills in the fields CEquip itself knows about. Ghidra mistypes every
    // field-fill call here (DamageList/CPlayerTradeOffer/CNudgeEngine make no sense for
    // an equipment descriptor) because they're all ICF-folded with EquipDesc's own
    // setters -- resolved via each callee's own tracker entry (raw asm Common.dll.txt
    // 110520-110572): set_id->set_id(Id), set_cause(float)->set_status(GetRelativeHealth()),
    // SetSourcePlayerID(Arch->get_id())->set_arch_id, SetOwner(1)->Count=1 (tracker id
    // 14300: this exact ICF-shared dword-store-at-+0x14 body's sibling aliases list
    // "EquipDesc::set_count"), set_destroyed(true)->set_equipped(true) (tracker id 13245:
    // sibling alias "EquipDesc::set_equipped"). The final raw byte copy (EquipDesc+0x18
    // = CEquip+0x12 bMission) is EquipDesc's own bMission field, confirmed by the mangled
    // symbol for every override in this family (`AAUEquipDesc@@`, not some other type) and
    // by CECargo::GetEquipDesc's identical +0x18 byte copy lining up with EquipDesc's
    // bMission rather than any Owner field -- this port previously filled in a distinct,
    // wrongly-laid-out "FEquipDesc" type here that does not match retail at all.
    // .text:0629ae10..0629ae6f in retail call order; Arch is read with no null test (0629ae44).
    if (IsDestroyed())
        return false;
    Desc.set_id(Id);
    Desc.set_status(GetRelativeHealth());
    Desc.set_arch_id(Arch->get_id());
    Desc.Count = 1;
    Desc.set_equipped(true);
    Desc.bMission = bMission;
    return true;
}

unsigned CEquip::GetType() const
{
    return Type;
}

unsigned short CEquip::GetID() const
{
    return Id;
}

Archetype::Equipment const* CEquip::EquipArch() const
{
    // Listing 0x3fb90: MOV EAX,[ECX+0xc]; RET.
    return Arch;
}

CEqObj* CEquip::GetOwner() const
{
    // Listing 0x71ad0: MOV EAX,[ECX+0x4]; RET.
    return Owner;
}

char const* CEquip::IdentifyEquipment() const
{
    // Listing 0x0629b040..0x0629b085: clear byte 0 of the 0x40-byte .data buffer DAT_063fc198,
    // then sprintf "ID(0x%X) EQ_ID(0x%X) EQ_ARCH(%s)" with [Owner+0xb0], the zero-extended
    // +0x08 id and Arch nickname; no null guards. Retail's sprintf can run past the 0x40 bytes
    // for a long nickname (UB into the next .data object); the port truncates at 0x40 instead.
    GIdentifyEquipmentBuf[0] = '\0';
    _snprintf(GIdentifyEquipmentBuf, sizeof(GIdentifyEquipmentBuf), "ID(0x%X) EQ_ID(0x%X) EQ_ARCH(%s)",
              Owner->Id, (unsigned)(Id), Arch->Nickname.get_string());
    GIdentifyEquipmentBuf[sizeof(GIdentifyEquipmentBuf) - 1] = '\0';
    return GIdentifyEquipmentBuf;
}

CInternalEquip::CInternalEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                               Archetype::Equipment const* ArchObj, bool bMissionFlag)
    : CEquip(TypeBits, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CInternalEquip::CInternalEquip(CInternalEquip const& Other)
    : CEquip(Other)
{
}

CInternalEquip::~CInternalEquip()
{
}

bool CInternalEquip::GetEquipDesc(EquipDesc& Desc) const
{
    // Common.dll.c 66784-66792 (listing 0x3f300): fill in the base descriptor, then flag it
    // as internal (retail EquipDesc::make_internal).
    bool Result = CEquip::GetEquipDesc(Desc);
    Desc.make_internal();
    return Result;
}

CInternalEquip* __cdecl CInternalEquip::cast(CEquip* Equip)
{
    // Common.dll.c 66799-66808 (0x3f320): null-check + type & 0x1ff0000 (all internal-equip
    // subtype bits: Cargo/Engine/Power/Scanner/Tractor/RepairDroid/InternalFX/TradeLane/Armor).
    if (Equip == nullptr || (Equip->GetType() & kInternalEquipMask) == 0)
        return nullptr;
    return (CInternalEquip*)(Equip);
}

CInternalEquip const* __cdecl CInternalEquip::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

CExternalEquip::CExternalEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                               Archetype::Equipment const* ArchObj, bool bMissionFlag)
    : CEquip(TypeBits, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    HardpointName.clear();
    ConnectionIndex = -1;
}

CExternalEquip::CExternalEquip(CExternalEquip const& Other)
    : CEquip(Other)
    , HardpointName(Other.HardpointName)
    , ConnectionIndex(Other.ConnectionIndex)
{
    // Common.dll.c 54839 (0x32800): plain field-by-field copy re-stamping the vtable at
    // each base level (CEquip::_vftable_ then this class's own) -- ordinary MSVC
    // base-then-derived copy-ctor codegen, reproduced here via CEquip(Other)'s own
    // (compiler-generated) memberwise copy plus this level's two fields.
}

CExternalEquip::~CExternalEquip()
{
}

bool CExternalEquip::GetEquipDesc(EquipDesc& Desc) const
{
    // Common.dll.c 66330-66339 (listing 0x3ed30): fill in the base descriptor, then stamp
    // this equip's hardpoint name over whatever the base left there.
    bool Result = CEquip::GetEquipDesc(Desc);
    Desc.set_hardpoint(HardpointName);
    return Result;
}

bool CExternalEquip::IsDestroyed() const
{
    // Common.dll.c 66343-66356 (0x3ed60). Gates on IsConnected() [vtable+0x58]. An unconnected
    // equip is treated as destroyed; otherwise returns the plain CEquip::bDestroyed flag.
    if (!IsConnected())
        return true;
    return bDestroyed != 0;
}

bool CExternalEquip::IsConnected() const
{
    // Listing 0x3ed80: GetParentConnector(false) != -1.
    return GetParentConnector(false) != kInvalidEngineInstance;
}

long CExternalEquip::GetParentConnector(bool bSearch) const
{
    // Listing 0x0629eed0..0x0629ef53. false -> +0x24. true -> exported FindHardpoint(owner
    // instance, +0x20 name, &Hit) with Hit preset to -1; a miss warns and returns Hit as left.
    if (!bSearch)
        return ConnectionIndex;
    long HitInst = -1;
    if (!FindHardpoint(Owner->EngineInstanceId, HardpointName.Value, HitInst))
    {
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: CExternalEquip: Object (%s) missing hardpoint (%s) for item (%s)\n",
              kCExternalEquipSource, 0x8e, Owner->Archetype->Nickname.get_string(),
              HardpointName.Value, Arch->Nickname.get_string());
    }
    return HitInst;
}

bool CExternalEquip::GetHardPointInfo(HardpointInfo& Info) const
{
    // Common.dll.c 66517-66538 (raw 0629ef60-0629efdd, listing 0x3ef60). Unlike FindHardpointOs
    // above (the shared recursive search), this is a flat, single-level query: it converts this
    // equip's own ConnectionIndex to an engine handle (IEngine+0x64), looks up HardpointName
    // against that handle only (IHardpoint+0x0C, no child recursion on a miss), then
    // unconditionally releases the handle (IEngine+0x3C) regardless of whether the lookup hit --
    // all three steps confirmed against the raw instruction trace, which also confirmed retail
    // does NOT null/validity-check the intermediate handle before using it (matched here).
    // .text:0629ef60..0629efdd: vtbl+0x64 GetParentConnector(false) is called twice (test, then
    // the argument); DALib::Engine / DALib::Hardpoint are used with no null test.
    if (GetParentConnector(false) == kInvalidEngineInstance)
        return false;

    void* Handle = DALib::Engine->GetInstanceArchetype((void*)(GetParentConnector(false)));

    FHardpointKey Key;
    Key.Name = HardpointName.Value;
    Key.Crc = DACOM_CRC::GetCRC32(HardpointName.Value);
    unsigned long Hit = DALib::Hardpoint->GetHardpoint(Handle, &Key, &Info);

    DALib::Engine->ReleaseHandle(Handle);

    return Hit != 0;
}

bool CExternalEquip::GetConnectionPosition(Vector* OutPos, Matrix* OutMatrix) const
{
    // Common.dll.c 66423-66478 (raw 0629ee00-0629eecb, listing 0x3ee00). Traced instruction-by-
    // instruction from the .txt listing -- the decompiled C for this function is corrupted
    // (spurious unaff_EBX/EBP/ESI locals, implausible auStack_* sizes) and was not trusted.
    // Sequence: IsConnected() [vtable+0x58], then GetHardPointInfo(Buf) [vtable+0x68] into a
    // local HardpointInfo, then the connector instance's world Transform via IEngine+0xA0
    // (same slot as common_hardpointsummary.cpp's EngineGetTransform / EngBase's EngineGetInstanceTransform),
    // then the 3DMathEngine singleton's slot 0x28 (FnTransformXform, already established at
    // common_ceqobj.cpp's get_explosion_dmg_bounding_sphere: world = Xform * Buf.Position) and
    // slot 0x20 (FnMulMatrix, already established at the former common_cobject.cpp's
    // get_docking_point_offset -- CEqObj code, now common_ceqobj.cpp: world = A * B) fill OutPos/OutMatrix respectively -- the matrix
    // call's "A" operand is the Transform's own leading 3x3 read as a plain Matrix (Transform's
    // Rotation field sits at offset 0, so this is a safe reinterpret, not a layout guess).
    // Return value is exactly GetHardPointInfo's own result; OutPos/OutMatrix are each optional
    // and only filled when non-null (retail never null-checks the Transform pointer itself
    // before using it, so neither does this port).
    // .text:0629ee00..0629eecb: DALib::Engine and the cached 3DMathEngine (DAT_063fc23c,
    // FUN_06391cf0 on first use) are used with no null test; each result goes into a stack
    // temporary and is then copied out. Buf is not initialised (GetHardPointInfo fills it).
    if (!IsConnected())
        return false;

    HardpointInfo Buf;
    if (!GetHardPointInfo(Buf))
        return false;

    Transform const* XformPtr = (Transform const*)(
        DALib::Engine->GetInstanceTransform((void*)(ConnectionIndex)));

    if (OutPos != nullptr)
    {
        FVector Pos;
        Acquire3DMathEngine()->TransformXform(Pos, *(FTransform const*)XformPtr,
                                              (FVector const&)(Buf.Position)); // +0x28
        *(FVector*)OutPos = Pos;
    }

    if (OutMatrix != nullptr)
    {
        FMatrix Rot;
        Acquire3DMathEngine()->MulMatrix(Rot, *(FMatrix const*)XformPtr,
                                         (FMatrix const&)(Buf.Orient)); // +0x20
        *(FMatrix*)OutMatrix = Rot;
    }

    return true;
}

bool CExternalEquip::GetVelocity(Vector& Out) const
{
    // Common.dll.c 66542-66563 (raw 0629efe0, listing 0x3efe0). Zero Out, then when connected
    // [vtable+0x58] copy Owner->get_velocity() over it (no null-check on Owner, matching retail --
    // Owner is always set by the CEquip ctor). Retail hardcodes the return to false regardless of
    // outcome; reproduced as-is (a genuine retail quirk, not a porting bug).
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (IsConnected())
        Out = Owner->get_velocity();
    return false;
}

bool CExternalEquip::GetCenterOfMass(Vector& Out) const
{
    // Common.dll.c 66567-66590 (raw 0629f030, listing 0x3f030). Zero Out, then when connected
    // [vtable+0x58] fetch this equip's own ConnectionIndex (GetParentConnector(false),
    // vtable+0x64) and query DALib::Engine's own vtable+0x90 (world-position getter, same slot as
    // common_misc.cpp's ComputeExplosiveImpulses / this file's own GetConnectionPosition's +0xa0
    // sibling) for that connector instance's world position. Retail hardcodes the return to false
    // regardless of outcome, matching GetVelocity above.
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    // .text:0629f05b..0629f07d: DALib::Engine used with no null test.
    if (IsConnected())
    {
        long const ConnIdx = GetParentConnector(false);
        float const* Pos = DALib::Engine->GetInstancePosition((void*)(ConnIdx));
        Out.x = Pos[0];
        Out.y = Pos[1];
        Out.z = Pos[2];
    }
    return false;
}

bool CExternalEquip::Connect(char const* HpName)
{
    // Listing 0x3eda0: StringAlloc, GetParentConnector(true) → +0x24.
    HardpointName.Value = StringAlloc(HpName, false);
    ConnectionIndex = (int)(GetParentConnector(true));
    if (ConnectionIndex == kInvalidEngineInstance)
    {
        Disconnect();
        return false;
    }
    return true;
}

void CExternalEquip::Disconnect()
{
    ConnectionIndex = (int)(kInvalidEngineInstance);
}

void CExternalEquip::Destroy()
{
    CEquip::Destroy();
    Disconnect();
}

CacheString CExternalEquip::GetParentHPName() const
{
    // Common.dll.c 0x327f0: struct-return copy of HardpointName.
    return HardpointName;
}

bool CExternalEquip::Update(float Dt, unsigned Flags)
{
    // Common.dll.c 0x3ed20. Pure passthrough to CEquip::Update.
    return CEquip::Update(Dt, Flags);
}

CExternalEquip* __cdecl CExternalEquip::cast(CEquip* Equip)
{
    if (Equip == nullptr || (Equip->GetType() & kExternalEquipMask) == 0)
        return nullptr;
    return (CExternalEquip*)(Equip);
}

CExternalEquip const* __cdecl CExternalEquip::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

// The three ctors (0629b970 / 0629b990 / 0629b9b0) write +0, byte +4 and +8 only. Retail
// leaves +0xc unwritten; the port zeroes Current (it is only read in states 1 and 3, after
// Traverse has set it).
CEquipTraverser::CEquipTraverser()
{
    TypeMask = 0xFFFFFFFFu;
    bIncludeDestroyed = 0;
    State = 0;
    Current = nullptr;
}

CEquipTraverser::CEquipTraverser(int TypeMaskValue)
{
    TypeMask = (unsigned)TypeMaskValue;
    bIncludeDestroyed = 0;
    State = 0;
    Current = nullptr;
}

CEquipTraverser::CEquipTraverser(int TypeMaskValue, bool bIncludeDestroyedValue)
{
    TypeMask = (unsigned)TypeMaskValue;
    bIncludeDestroyed = bIncludeDestroyedValue ? 1 : 0;
    State = 0;
    Current = nullptr;
}

void CEquipTraverser::Restart()
{
    State = 0;
}


CEquipManager::CEquipManager()
    : Owner(nullptr)
    , ExternalAllocator(0)
    , External(nullptr)
    , ExternalCount(0)
    , InternalAllocator(0)
    , Internal(nullptr)
    , InternalCount(0)
    , DecayingCargoCount(0)
{
    External = new std::list<CEquip*>();
    Internal = new std::list<CEquip*>();
}

CEquipManager::CEquipManager(CEquipManager const& Other)
    : Owner(Other.Owner)
    , ExternalAllocator(Other.ExternalAllocator)
    , External(new std::list<CEquip*>(*Other.External))
    , ExternalCount(Other.ExternalCount)
    , InternalAllocator(Other.InternalAllocator)
    , Internal(new std::list<CEquip*>(*Other.Internal))
    , InternalCount(Other.InternalCount)
    , DecayingCargoCount(Other.DecayingCargoCount)
{
}

CEquipManager& CEquipManager::operator=(CEquipManager const& Other)
{
    // Common.dll.c 0x270d0: reuses nodes in place while both lists overlap, then either erases
    // the destination's remaining tail or appends the source's remaining tail -- exactly what
    // std::list<T>::operator= already does, so delegating to it is a faithful high-level port.
    // .text:062870d0..062871f2: +0 copied, each list assigned unless it is the same object
    // (the +4/+0x10 allocator bytes are not copied; the counts follow the list sizes), +0x1c
    // copied. A self-assignment is therefore a no-op, as the early return below.
    if (this == &Other)
        return *this;

    Owner = Other.Owner;
    *External = *Other.External;
    ExternalCount = Other.ExternalCount;
    *Internal = *Other.Internal;
    InternalCount = Other.InternalCount;
    DecayingCargoCount = Other.DecayingCargoCount;
    return *this;
}

CEquipManager::~CEquipManager()
{
    Clear();
    delete External;
    External = nullptr;
    ExternalCount = 0;
    delete Internal;
    Internal = nullptr;
    InternalCount = 0;
}

bool CEquipManager::Init(CEqObj* OwnerObj)
{
    if (Owner == nullptr)
        Owner = OwnerObj;
    return Owner != nullptr;
}

bool CEquipManager::AddNewEquipment(CEquip* Equip)
{
    // .text:0629b210..0629b2a7: no Equip null test; type & 0xc10003 picks the internal list
    // (+0x10), else the external one (+0x04); a cargo stack (type == 0x10000) whose
    // GetDecayDamagePerSecond() is not 0.0 (FCOMP, TEST AH,0x44 / JNP skips only on equal)
    // bumps +0x1c. Always true.
    if ((Equip->GetType() & kInternalListMask) == 0)
    {
        External->push_back(Equip);
        ExternalCount += 1;
    }
    else
    {
        Internal->push_back(Equip);
        InternalCount += 1;
    }

    if (Equip->GetType() == EquipTypeCargo &&
        ((CECargo*)Equip)->GetDecayDamagePerSecond() != 0.0f)
        DecayingCargoCount += 1;
    return true;
}

void CEquipManager::Clear()
{
    // Listing 0x3b2b0 inlines the static Clear(list&) below for Internal, then External, then
    // zeroes +0x1c. The lists are owned by the manager (allocated in every constructor).
    Clear(*Internal);
    InternalCount = 0;
    Clear(*External);
    ExternalCount = 0;
    DecayingCargoCount = 0;
}

void __cdecl CEquipManager::Clear(std::list<CEquip*>& List)
{
    // Listing 0x3b860: null each slot and delete its equipment (scalar deleting destructor),
    // then unlink every node.
    std::list<CEquip*>::iterator It = List.begin();
    for (; It != List.end(); ++It)
    {
        CEquip* Equip = *It;
        *It = nullptr;
        delete Equip;
    }
    List.clear();
}

int CEquipManager::CleanUp()
{
    // Listing 0x3b380: CleanUp(Internal list) then CleanUp(External list), summed. In retail
    // the counts are the lists' own size fields, which the private overload decrements as it
    // unlinks; this port keeps them as separate fields and adjusts them here.
    int InternalRemoved = CleanUp(*Internal);
    int ExternalRemoved = CleanUp(*External);
    InternalCount -= (unsigned)(InternalRemoved);
    ExternalCount -= (unsigned)(ExternalRemoved);
    return InternalRemoved + ExternalRemoved;
}

int CEquipManager::CleanUp(std::list<CEquip*>& List)
{
    // Listing 0x3b8c0: for each item whose CanDelete() (vtable+0x18) holds -- a decaying cargo
    // stack (Type == 0x10000 and GetDecayDamagePerSecond() != 0) first decrements
    // DecayingCargoCount -- unlink the node, then delete the equipment. Returns the count.
    int Removed = 0;
    std::list<CEquip*>::iterator It = List.begin();
    while (It != List.end())
    {
        CEquip* Equip = *It;
        if (!Equip->CanDelete())
        {
            ++It;
            continue;
        }
        if (Equip->GetType() == EquipTypeCargo &&
            ((CECargo*)Equip)->GetDecayDamagePerSecond() != 0.0f)
            DecayingCargoCount -= 1;
        It = List.erase(It);
        delete Equip;
        ++Removed;
    }
    return Removed;
}

int CEquipManager::Size() const
{
    return (int)(InternalCount + ExternalCount);
}

bool CEquipManager::HasDecayingCargo() const
{
    // Common.dll.c 0x26fa0: return 0 < *(int*)(this+0x1c);
    return DecayingCargoCount > 0;
}

CEquip* CEquipManager::FindFirst(unsigned TypeMask)
{
    // Listing 0x3b720 (both overloads): a {TypeMask, false, 0} traverser and one Traverse.
    CEquipTraverser Walk((int)(TypeMask));
    return Traverse(Walk);
}

CEquip const* CEquipManager::Traverse(CEquipTraverser& Walk) const
{
    // Listing 0x3b430: JMP to the non-const Traverse.
    return const_cast<CEquipManager*>(this)->Traverse(Walk);
}

CEquip const* CEquipManager::ReverseTraverse(CEquipTraverser& Walk) const
{
    // Listing 0x3b530: JMP to the non-const ReverseTraverse.
    return const_cast<CEquipManager*>(this)->ReverseTraverse(Walk);
}

CEquip const* CEquipManager::FindFirst(unsigned TypeMask) const
{
    // Traverse walks the manager's own lists and is not const; retail's FindFirst is declared
    // const anyway (QBE), so the cast is retail's own const-correctness gap, not ours.
    CEquipTraverser Walk((int)(TypeMask));
    return const_cast<CEquipManager*>(this)->Traverse(Walk);
}

CEquip* CEquipManager::Traverse(CEquipTraverser& Walk)
{
    if (ExternalCount == 0 && InternalCount == 0)
        return nullptr;

    for (;;)
    {
        switch (Walk.State)
        {
        case 0:
            if (ExternalCount == 0 || (Walk.TypeMask & kExternalListMask) == 0)
            {
                Walk.State = 2;
                Walk.Current = nullptr;
                continue;
            }
            Walk.State = 1;
            Walk.Current = nullptr;
            break;
        case 1:
            break;
        case 2:
            if (InternalCount == 0 || (Walk.TypeMask & kInternalListMask) == 0)
            {
                Walk.State = 0;
                Walk.Current = nullptr;
                return nullptr;
            }
            Walk.State = 3;
            Walk.Current = nullptr;
            break;
        case 3:
            break;
        default:
            // .text:0629b466 JA -> 0629b509: State = 0, null.
            Walk.State = 0;
            return nullptr;
        }

        std::list<CEquip*>* List = (Walk.State == 1) ? External : Internal;
        CEquip* Found = NextMatching(List, Walk.Current, Walk.TypeMask,
                                     Walk.bIncludeDestroyed != 0);
        if (Found != nullptr)
        {
            Walk.Current = Found;
            return Found;
        }

        if (Walk.State == 1)
        {
            Walk.State = 2;
            Walk.Current = nullptr;
            continue;
        }
        Walk.State = 0;
        Walk.Current = nullptr;
        return nullptr;
    }
}

CEquip* CEquipManager::ReverseTraverse(CEquipTraverser& Walk)
{
    // Common.dll.c 0x3b540: retail's own guard is OR (not Traverse's AND) -- reproduced as-is.
    if (ExternalCount == 0 || InternalCount == 0)
        return nullptr;

    for (;;)
    {
        switch (Walk.State)
        {
        case 0:
            if (InternalCount == 0 || (Walk.TypeMask & kInternalListMask) == 0)
            {
                Walk.State = 2;
                Walk.Current = nullptr;
                continue;
            }
            Walk.State = 3;
            Walk.Current = nullptr;
            break;
        case 3:
            break;
        case 2:
            if (ExternalCount == 0 || (Walk.TypeMask & kExternalListMask) == 0)
            {
                Walk.State = 0;
                Walk.Current = nullptr;
                return nullptr;
            }
            Walk.State = 1;
            Walk.Current = nullptr;
            break;
        case 1:
            break;
        default:
            // .text:0629b56a JA -> 0629b615: State = 0, null.
            Walk.State = 0;
            return nullptr;
        }

        std::list<CEquip*>* List = (Walk.State == 3) ? Internal : External;
        CEquip* Found = PrevMatching(List, Walk.Current, Walk.TypeMask,
                                     Walk.bIncludeDestroyed != 0);
        if (Found != nullptr)
        {
            Walk.Current = Found;
            return Found;
        }

        if (Walk.State == 3)
        {
            Walk.State = 2;
            Walk.Current = nullptr;
            continue;
        }
        Walk.State = 0;
        Walk.Current = nullptr;
        return nullptr;
    }
}

CExternalEquip* CEquipManager::FindByHardpoint(CacheString const& Hardpoint)
{
    // Listing 0x3b640: traverse with mask 0x1fe3 (every external type) and return the first
    // equip whose GetParentHPName() (vtable+0x60) matches Hardpoint by stricmp; null once the
    // traversal runs out. Retail neither casts nor null-checks the names.
    CEquipTraverser Walk((int)(kExternalEquipMask));
    for (;;)
    {
        CEquip* Equip = Traverse(Walk);
        if (Equip == nullptr)
            return nullptr;
        CExternalEquip* Ext = (CExternalEquip*)(Equip);
        if (CallDacomStricmp(Ext->GetParentHPName().Value, Hardpoint.Value) == 0)   // 0629b697
            return Ext;
    }
}

CExternalEquip const* CEquipManager::FindByHardpoint(CacheString const& Hardpoint) const
{
    // Listing 0x3b640: the same body as the non-const overload.
    return const_cast<CEquipManager*>(this)->FindByHardpoint(Hardpoint);
}

CEquip* CEquipManager::FindByID(unsigned short EquipId)
{
    // .text:0629b6d0..0629b713: external list, then internal list; the payload's +0x08 word is
    // compared with no null test.
    std::list<CEquip*>::iterator It = External->begin();
    for (; It != External->end(); ++It)
    {
        if ((*It)->Id == EquipId)
            return *It;
    }
    for (It = Internal->begin(); It != Internal->end(); ++It)
    {
        if ((*It)->Id == EquipId)
            return *It;
    }
    return nullptr;
}

CEquip const* CEquipManager::FindByID(unsigned short EquipId) const
{
    // Common.dll.c 0x3b6c0
    return const_cast<CEquipManager*>(this)->FindByID(EquipId);
}

unsigned short CEquipManager::InstToSubObjID(long Instance) const
{
    // .text:0629b3a0..0629b428: -1 -> SUBOBJ_ID_NONE; otherwise a {0x1fe0, false} traversal
    // (CAttachedEquip types only) asking each equip vtbl+0x80 = CAttachedEquip::IsInstOnEquip
    // (Instance) -- root instance or any descendant of it; the first hit's Id, else
    // SUBOBJ_ID_NONE. The equip is not cast-checked (the mask guarantees the type).
    if (Instance == -1)
        return SUBOBJ_ID_NONE;

    CEquipTraverser Walk((int)(kAttachedEquipMask));
    CEquip* Equip = const_cast<CEquipManager*>(this)->Traverse(Walk);
    while (Equip != nullptr)
    {
        if (((CAttachedEquip*)Equip)->IsInstOnEquip(Instance))
            return Equip->Id;
        Equip = const_cast<CEquipManager*>(this)->Traverse(Walk);
    }
    return SUBOBJ_ID_NONE;
}

bool CEquipManager::VerifyListSync(EquipDescList const& Other) const
{
    // Common.dll.c 0x3b750, phase 1: every desc in Other must have a live matching equip.
    for (EquipDesc const& Desc : Other.Items)
    {
        CEquip const* Equip = FindByID(Desc.Id);
        if (Equip == nullptr)
            return false;
        // 0629b788: Arch (+0xc) read with no null test, its +8 id against desc+4.
        if (Desc.ArchId != Equip->Arch->Nickname.Id)
            return false;
        // 0629b797..0629b7b6: |Health - GetRelativeHealth()| on the FPU stack, FCOMP the double
        // 0.001 (.rdata:0639c9f0), TEST AH,0x5 / JP -> false when not less or unordered.
        double const HealthDiff =
            fabs((double)Desc.Health - Equip->GetRelativeHealth());
        if (!(HealthDiff < 0.001))
            return false;
        // Retail computes this stricmp but never checks its result -- reproduced verbatim.
        CExternalEquip const* Ext = CExternalEquip::cast(const_cast<CEquip*>(Equip));
        if (Ext != nullptr)
            CallDacomStricmp(Desc.Hardpoint, Ext->GetParentHPName().Value); // vtbl+0x60 0629b7d2, DACOM stricmp 0629b7dc
    }

    // Phase 2: every live equip that reports a GetEquipDesc() must appear somewhere in Other.
    CEquipTraverser Walk((int)(0xFFFFFFFFu));
    for (;;)
    {
        CEquip const* Equip = const_cast<CEquipManager*>(this)->Traverse(Walk);
        if (Equip == nullptr)
            return true;
        EquipDesc TempDesc;
        bool bHasDesc = Equip->GetEquipDesc(TempDesc);
        if (bHasDesc && Other.find_equipment_item(Equip->Id) == nullptr)
            return false;
    }
}
