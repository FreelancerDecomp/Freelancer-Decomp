// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEqObj (retail CEqObj.cpp) and CEqObj::DockAnimInfo.
#include "common_cobject.h"
#include "x86math.h"

#include "common_cobjdb.h"
#include "common_goods.h"
#include "common_physys.h"
#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)
#include "common_iface.h"
#include "common_universe.h"
#include "common_misc.h"    // DamageList, DamageEntry

#include "DALib.h"

#include <cmath>
#include <list>
#include <new>      // placement new (get_equip_desc_list)
#include <string.h>

namespace
{
    const char kCEqObjSource[] = "E:\\FL\\Scratch\\Source\\Common\\CEqObj.cpp";
}

CEqObj::CEqObj(Class ObjectClass)
    : CSimple(ObjectClass)
{
    // Common.dll.c 0x49010. CEquipManager + CArchGroupManager::Init.
    EquipManager.Init(this);
    VibeId = -1;
    // 062a9097..062a90b5: the four part ids, the accessory count and the voice; the accessory
    // ids (+0x118..+0x134) are left unwritten, as retail does.
    SpaceCostume.PartIds[0] = 0;
    SpaceCostume.PartIds[1] = 0;
    SpaceCostume.PartIds[2] = 0;
    SpaceCostume.PartIds[3] = 0;
    SpaceCostume.AccessoryCount = 0;
    VoiceId = 0;
    CloakPercent = 0.0f;
    ArchGroups.Init(this);
    bWeaponsOffline = 0;
    DockingBaseId = 0;                                           // 062a90e8
    DockingPointBaseId = 0;                                      // 062a90ee
    // 062a90f4: the INotify listener starts out null. Only the client connects one
    // (Server.dll never calls CEqObj::connect(INotify*)), so a server-side CEqObj keeps
    // this null for life and CEqObj::notify (062a92c0) is a no-op for it.
    Notify = nullptr;                                            // 062a90f4
    bExplosionDmgCached = false;                                 // 062a90fa (byte write)
    ExplosionDmgRadius = 0.0f;                                   // 062a9100
    ExplosionDmgCenter.x = 0.0f;                                 // 062a9106
    ExplosionDmgCenter.y = 0.0f;                                 // 062a910c
    ExplosionDmgCenter.z = 0.0f;                                 // 062a9112
    // 062a9118..062a912a: DockingPoints (vector at +0x180) -- its default ctor.
    ControlExclusion = 0;                                        // 062a9130
    BehaviorMgr = nullptr;
    Power = 0.0f;
    MaxPower = 0.0f;
    BehaviorIndex = -1;
}

// Common.dll.c 0x272c0. Retail's version has zero callers anywhere in Common.dll,
// freelancer.exe, or Server.dll (only exported because CEqObj is a dllexport class) --
// ported as a plain member-wise copy of the base class plus every field below rather
// than reverse-engineering its byte-exact shuffling of ArchGroups/EquipManager/etc.
// through several other never-called helper functions.
CEqObj::CEqObj(CEqObj const& Other)
    : CSimple(Other)
    , EquipManager(Other.EquipManager)
    , VibeId(Other.VibeId)
    , SpaceCostume(Other.SpaceCostume)
    , VoiceId(Other.VoiceId)
    , CloakPercent(Other.CloakPercent)
    , ArchGroups(Other.ArchGroups)
    , bWeaponsOffline(Other.bWeaponsOffline)
    , DockingBaseId(Other.DockingBaseId)
    , DockingPointBaseId(Other.DockingPointBaseId)
    , Notify(Other.Notify)
    , bExplosionDmgCached(Other.bExplosionDmgCached)
    , ExplosionDmgRadius(Other.ExplosionDmgRadius)
    , ExplosionDmgCenter(Other.ExplosionDmgCenter)
    , DockingPoints(Other.DockingPoints)
    , ControlExclusion(Other.ControlExclusion)
    , BehaviorMgr(Other.BehaviorMgr)
    , Power(Other.Power)
    , MaxPower(Other.MaxPower)
    , BehaviorIndex(Other.BehaviorIndex)
{
}

CEqObj::~CEqObj()
{
    // .text:062a9190-062a9294.
    PhySys::SetCollisionState(this, false);                      // 062a91b8-062a91c4

    // 062a91cc-062a9205: default CEquipTraverser over every equip, DisableController
    // (CEquip vtable +0x50) on each.
    CEquipTraverser Trav;
    for (CEquip* Item = EquipManager.Traverse(Trav); Item != nullptr; Item = EquipManager.Traverse(Trav))
        Item->DisableController();

    // 062a9207-062a9221: ~IBehaviorManager (FUN_062d2530) + operator delete, then 0.
    if (BehaviorMgr != nullptr)
    {
        delete BehaviorMgr;
        BehaviorMgr = nullptr;
    }

    EquipManager.Clear();                                        // 062a9229
    ArchGroups.Clear();                                          // 062a9236
    // 062a923b-062a926e: DockingPoints' buffer freed, then ~CArchGroupManager,
    // ~CEquipManager and ~CSimple -- the member / base destructors.
}

void CEqObj::load_equip_and_cargo(EquipDescVector const& Items, bool bClear)
{
    if (bClear)
        clear_equip_and_cargo();

    EquipDesc* It = Items.Begin;
    EquipDesc* End = Items.End;
    for (; It != End; ++It)
        add_item(*It);
}

void CEqObj::clear_equip_and_cargo()
{
    EquipManager.Clear();
}

bool CEqObj::add_item(EquipDesc const& Desc)
{
    if (Desc.bEquipped)
        return add_equipped_item(Desc);
    return add_cargo_item(Desc);
}

bool CEqObj::add_equipped_item(EquipDesc const& Desc)
{
    // .text:062a9700..062a981b.
    Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc.ArchId);
    if (Arch == nullptr)
    {
        // 062a97e0..062a9813: the object's archetype nickname (asteroidarch() is the ICF
        // fold of `return [this+0x88]`, read without a null test) and the arch id.
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Object(%s) missing equipment archetype(%X)\n",
              kCEqObjSource, 0x127, Archetype->Nickname.get_string(), Desc.ArchId);
        return false;
    }

    // 062a971f..062a9743: virtual alloc_equip (vtable +0x104) with the raw mission byte.
    CEquip* const Equip = alloc_equip(Desc.Id, Arch, Desc.bMission);
    if (Equip == nullptr)
        return false;

    // 062a9749..062a9757: FLD Health; FMUL HitPts; FSTP dword -> SetHitPoints (+0x40).
    Equip->SetHitPoints(Desc.Health * Arch->HitPts);
    if (!Desc.is_internal())
    {
        CExternalEquip* const Ext = CExternalEquip::cast(Equip);
        if (Ext != nullptr)
        {
            Ext->Connect(Desc.Hardpoint);                              // vtable +0x5c
        }
        else
        {
            // 062a9783..062a97c0: object nickname, equipment nickname, hardpoint (raw).
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: Object(%s) tried to connect internal type(%s) to "
                  "hardpoint(%s). Ignorable.",
                  kCEqObjSource, 0x116, Archetype->Nickname.get_string(),
                  Arch->Nickname.get_string(), Desc.Hardpoint);
        }
    }
    EquipManager.AddNewEquipment(Equip);
    return true;
}

bool CEqObj::add_cargo_item(EquipDesc const& Desc)
{
    // .text:062a9820..062a9919.
    Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc.ArchId);
    if (Arch == nullptr)
    {
        // 062a98d0..062a9903: object nickname (no null test), arch id.
        FDUMP(0x100002,
              "%s(%d) : *** WARNING: Object(%s) missing equipment archetype(%X)\n",
              kCEqObjSource, 0x158, Archetype->Nickname.get_string(), Desc.ArchId);
        return false;
    }

    // 062a9851..062a98b2: new CECargo (0x2c), Init (vtable +0x5c) with Health, Count and a
    // reference to the descriptor's own hardpoint CacheString (+0x08), AddNewEquipment.
    CECargo* const Cargo = new CECargo(this, Desc.Id, Arch, Desc.bMission);
    Cargo->Init(Desc.Health, (unsigned)Desc.Count, Desc.get_hardpoint());
    EquipManager.AddNewEquipment(Cargo);
    return true;
}

bool CEqObj::sync_cargo(EquipDescList const& List)
{
    // .text:062a9920..062a9a80. First pass: every existing cargo equip (traverser mask
    // 0x10000) takes Count and HitPoints from the List entry with its Id, or Count 0.
    CEquipTraverser Trav(EquipTypeCargo);
    CEquip* Item = EquipManager.Traverse(Trav);
    while (Item != nullptr)
    {
        EquipDesc const* const Desc = List.find_equipment_item(Item->Id);
        CECargo* const Cargo = (CECargo*)Item;
        if (Desc != nullptr)
        {
            Cargo->SetCount((unsigned)Desc->Count);
            // 062a9988..062a99a0: GetMaxHitPoints (+0x44) FMUL Health, FSTP dword ->
            // SetHitPoints (+0x40).
            Item->SetHitPoints(Item->GetMaxHitPoints() * Desc->Health);
        }
        else
        {
            Cargo->SetCount(0);
        }
        Item = EquipManager.Traverse(Trav);
    }

    // 062a99be..062a9a67: second pass over the List nodes: every non-equipped entry whose Id
    // has no equip yet gets a new CECargo exactly as add_cargo_item builds one, except that
    // the GetEquipment result is used without a null test.
    for (std::list<EquipDesc>::const_iterator It = List.Items.begin(); It != List.Items.end(); ++It)
    {
        EquipDesc const& Desc = *It;
        if (Desc.bEquipped)
            continue;
        if (EquipManager.FindByID(Desc.Id) != nullptr)
            continue;
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc.ArchId);
        CECargo* const Cargo = new CECargo(this, Desc.Id, Arch, Desc.bMission);
        Cargo->Init(Desc.Health, (unsigned)Desc.Count, Desc.get_hardpoint());
        EquipManager.AddNewEquipment(Cargo);
    }
    return true;
}

CEquip* CEqObj::alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch,
                            bool bMissionFlag)
{
    // .text:062a9a90..062a9e9f: switch on Arch->get_class_type() (vtable +0x10, no null
    // test), byte table 062a9edc over 9..0x22. Each case news its CE* class (the
    // CEAttachedFX case inlines that ctor: CExternalEquip(2, ...) + vtable stamp).
    int const ClassType = (int)Arch->get_class_type();

    switch (ClassType)
    {
    case Archetype::AClassCargoPod:
        return new CECargoPod(this, EquipId, (Archetype::CargoPod const*)Arch, bMissionFlag);
    case Archetype::AClassPower:
        return new CEPower(this, EquipId, (Archetype::Power const*)Arch, bMissionFlag);
    case Archetype::AClassShieldGenerator:
        return new CEShieldGenerator(this, EquipId, (Archetype::ShieldGenerator const*)Arch, bMissionFlag);
    case Archetype::AClassGun:
        return new CEGun(this, EquipId, (Archetype::Gun const*)Arch, bMissionFlag);
    case Archetype::AClassMineDropper:
        return new CEMineDropper(this, EquipId, (Archetype::MineDropper const*)Arch, bMissionFlag);
    case Archetype::AClassCounterMeasureDropper:
        return new CECounterMeasureDropper(this, EquipId, (Archetype::CounterMeasureDropper const*)Arch, bMissionFlag);
    case Archetype::AClassScanner:
        return new CEScanner(this, EquipId, (Archetype::Scanner const*)Arch, bMissionFlag);
    case Archetype::AClassLight:
        return new CELightEquip(this, EquipId, (Archetype::Light const*)Arch, bMissionFlag);
    case Archetype::AClassAttachedFX:
        return new CEAttachedFX(this, EquipId, (Archetype::AttachedFXEquip const*)Arch, bMissionFlag);
    case Archetype::AClassInternalFX:
        return new CEInternalFX(this, EquipId, (Archetype::InternalFXEquip const*)Arch, bMissionFlag);
    case Archetype::AClassRepairDroid:
        return new CERepairDroid(this, EquipId, (Archetype::RepairDroid const*)Arch, bMissionFlag);
    case Archetype::AClassCloakingDevice:
        return new CECloakingDevice(this, EquipId, (Archetype::CloakingDevice const*)Arch, bMissionFlag);
    case Archetype::AClassArmor:
        return new CEArmor(this, EquipId, (Archetype::Armor const*)Arch, bMissionFlag);
    default:
        break;
    }

    // 062a9e64..062a9e89: every other class -- warning with Arch's nickname, return 0.
    FDUMP(0x100002,
          "%s(%d) : *** WARNING: alloc_equip: Archetype[%s] not supported/equippable!",
          kCEqObjSource, 0x1cf, Arch->Nickname.get_string());
    return nullptr;
}

void CEqObj::link_shields()
{
    // .text:062abbe0..062abd43. Walks the archetype's shield links (retail vector begin/end
    // at Archetype+0x68/+0x6c, the port's heap vector allocated by the EqObj ctor), each
    // link's generator hardpoints in order; no null test on Archetype.
    Archetype::EqObj const* const Eq = eqobjarch();
    std::vector<Archetype::ShieldLink> const& Links = *Eq->ShieldLinks;
    for (std::vector<Archetype::ShieldLink>::const_iterator LinkIt = Links.begin(); LinkIt != Links.end(); ++LinkIt)
    {
        Archetype::ShieldLink const& Link = *LinkIt;
        CEShield* Shield = nullptr;                                    // 062abc1e XOR ESI,ESI
        for (std::vector<CacheString>::const_iterator HpIt = Link.GeneratorHps.begin();
             HpIt != Link.GeneratorHps.end(); ++HpIt)
        {
            CEShieldGenerator* const Gen = CEShieldGenerator::cast(EquipManager.FindByHardpoint(*HpIt));
            if (Gen == nullptr || Gen->LinkedShield != nullptr)        // +0x4c
                continue;
            if (Shield == nullptr)
            {
                Archetype::Equipment* const ShieldArch = Archetype::GetEquipmentByName(Link.ShieldArch);
                if (ShieldArch == nullptr)
                {
                    // 062abd06..062abd2e: shield arch nickname, hardpoint (raw); the whole
                    // function returns.
                    FDUMP(0x100001,
                          "%s(%d) : *** ERROR: Failed to create shield(%s) on hardpoint(%s). "
                          "(Ignorable)",
                          kCEqObjSource, 0x579, Link.ShieldArch.get_string(), Link.Hardpoint.Value);
                    return;
                }
                Shield = new CEShield(this, Link.EquipId, (Archetype::Shield const*)ShieldArch, false);
                Shield->Connect(Link.Hardpoint.Value);
                EquipManager.AddNewEquipment(Shield);
            }
            Gen->LinkShield(Shield);
        }
    }
}

void CEqObj::load_arch_groups(std::list<CollisionGroupDesc> const& Descs)
{
    // .text:062a9420..062a94e8. For every description: group archetype =
    // Archetype(+0x88, no null test)->get_group_by_id(word Id +0x8) -- the result goes to the
    // CArchGroup ctor untested; new CArchGroup(group, this) (0x34 bytes); SetRelativeHealth
    // (Health +0xc); Health FCOMP 0.0 / TEST AH,0x44 / JP: only equal -> Kill(3); then
    // ArchGroups (+0x144).AddNewArchGroup. Finally DestroyDeadGroups.
    Archetype::EqObj* const Eq = eqobjarch();
    for (std::list<CollisionGroupDesc>::const_iterator It = Descs.begin(); It != Descs.end(); ++It)
    {
        Archetype::CollisionGroup const* const GroupArch = Eq->get_group_by_id(It->Id);
        CArchGroup* const Group = new CArchGroup(*GroupArch, this);
        Group->SetRelativeHealth(It->Health);
        if (It->Health == 0.0f)
            Group->Kill(DamageEntry::SubObjFate::HealthProxy);
        ArchGroups.AddNewArchGroup(Group);
    }
    ArchGroups.DestroyDeadGroups();
}

void CEqObj::clear_arch_groups()
{
    // Common.dll.c 0x494f0: pure forwarding wrapper around ArchGroups.Clear().
    ArchGroups.Clear();
}

void CEqObj::get_collision_group_description(std::list<CollisionGroupDesc>& OutDescs) const
{
    // Common.dll.c 0x49500: walk every live arch group (fresh CArchGrpTraverser) and
    // append one CollisionGroupDesc per group to the caller's list.
    CArchGrpTraverser Traverser;
    for (CArchGroup const* Group = ArchGroups.Traverse(Traverser); Group != nullptr;
         Group = ArchGroups.Traverse(Traverser))
    {
        CollisionGroupDesc Desc;
        Group->GetCollisionGroupDesc(Desc);
        OutDescs.push_back(Desc);
    }
}

void CEqObj::get_equip_desc_list(EquipDescVector& OutList) const
{
    // .text:062a9f00-062aa18a. One descriptor (062a9f09) is reused for every equip.
    EquipDesc Desc;

    // 062a9f12-062a9fcb: the inlined VC6 reserve(size() + ExternalCount (+0xf0) +
    // InternalCount (+0xfc)). Capacity below that (unsigned JNC) -> new buffer of exactly
    // that many elements, copy-construct the live ones (FUN_0626c1e0), free the old one.
    unsigned const OldSize = (OutList.Begin != nullptr) ? (unsigned)(OutList.End - OutList.Begin) : 0;
    unsigned const Wanted = EquipManager.InternalCount + EquipManager.ExternalCount + OldSize;
    unsigned const Capacity = (OutList.Begin != nullptr) ? (unsigned)(OutList.Cap - OutList.Begin) : 0;
    if (Capacity < Wanted)
    {
        int AllocCount = (int)Wanted;
        if (AllocCount < 0) // 062a9f58 JGE (the allocator's own clamp)
            AllocCount = 0;
        EquipDesc* const NewBuf = (EquipDesc*)::operator new(AllocCount * sizeof(EquipDesc));
        EquipDesc* Dst = NewBuf;
        for (EquipDesc const* Src = OutList.Begin; Src != OutList.End; ++Src, ++Dst)
            new (Dst) EquipDesc(*Src);
        ::operator delete(OutList.Begin);
        OutList.Cap = NewBuf + Wanted;
        OutList.End = NewBuf + ((OutList.Begin != nullptr) ? (OutList.End - OutList.Begin) : 0);
        OutList.Begin = NewBuf;
    }

    // 062a9fce-062aa17d: default traverser over every equip; vt+0x28 GetEquipDesc(Desc) true
    // -> the inlined VC6 insert(end(), 1, Desc).
    CEquipManager& MutableEquipManager = const_cast<CEquipManager&>(EquipManager);
    CEquipTraverser Walk;
    for (CEquip const* Eq = MutableEquipManager.Traverse(Walk); Eq != nullptr;
         Eq = MutableEquipManager.Traverse(Walk))
    {
        if (!Eq->GetEquipDesc(Desc))
            continue;
        if ((unsigned)(OutList.Cap - OutList.End) < 1)
        {
            // 062aa018-062aa0ba: grow to size() + max(size(), 1), copy the old elements,
            // construct Desc after them, free the old buffer.
            unsigned const Size = (OutList.Begin != nullptr) ? (unsigned)(OutList.End - OutList.Begin) : 0;
            unsigned const Grown = Size + (Size > 1 ? Size : 1);
            int AllocCount = (int)Grown;
            if (AllocCount < 0) // 062aa041 JNS
                AllocCount = 0;
            EquipDesc* const NewBuf = (EquipDesc*)::operator new(AllocCount * sizeof(EquipDesc));
            EquipDesc* Dst = NewBuf;
            for (EquipDesc const* Src = OutList.Begin; Src != OutList.End; ++Src, ++Dst)
                new (Dst) EquipDesc(*Src);
            new (Dst) EquipDesc(Desc);
            ::operator delete(OutList.Begin);
            OutList.Cap = NewBuf + Grown;
            OutList.End = NewBuf + (Size + 1);
            OutList.Begin = NewBuf;
        }
        else
        {
            // 062aa0bf-062aa167: room left -- construct Desc at End, End + 1.
            new (OutList.End) EquipDesc(Desc);
            ++OutList.End;
        }
    }
}

void CEqObj::init_docking_points(unsigned int BaseId)
{
    // .text:062aadc0..062ab0f4.
    DockingPointBaseId = BaseId;                                       // 062aadcb [this+0x164]

    // 062aadd1..062aae70: reserve room for one DockAnimInfo per archetype docking sphere
    // (reallocate to exactly that count when the capacity is smaller). The vector is NOT
    // cleared: the entries are appended. No null test on the archetype.
    Archetype::EqObj const* const Arch = eqobjarch();
    DockingPoints.reserve(Arch->DockingSpheres.size());

    // 062aae73..062ab0e6: per sphere (0x10-byte records, the end re-read every pass) a
    // DockAnimInfo {bActive 0, channel, 0.0f, &sphere} is pushed at the end. A non-empty
    // animation name (record +0x08) is started with DALib::Anim slot 9 (+0x24)
    // PlayScript(Arch->AnimIndex (+0x44), this->EngineInstanceId, name, 0, 0); a -1 result
    // is reported. An empty name gives channel -1 without a call.
    for (std::vector<Archetype::EqObj::DockingSphere>::const_iterator It = Arch->DockingSpheres.begin();
         It != Arch->DockingSpheres.end(); ++It)
    {
        Archetype::EqObj::DockingSphere const& Sphere = *It;
        int AnimChannel = -1;
        if (Sphere.AnimName.c_str()[0] != '\0')
        {
            AnimChannel = DALib::Anim->PlayScript(AsOpaqueDword(Arch->get_script_index()),
                                                 AsOpaqueDword(this->EngineInstanceId),
                                                 Sphere.AnimName.c_str(), nullptr, nullptr);
            if (AnimChannel == -1)
            {
                // 062aaedf..062aaf13: the name and the object archetype's nickname.
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: failed to find animation (%s) in object (%s)\n",
                      "E:\\FL\\Scratch\\Source\\Common\\CEqObj.cpp",
                      0x359,
                      Sphere.AnimName.c_str(),
                      this->Archetype->Nickname.get_string());
            }
        }

        DockAnimInfo Entry;                                            // 062aae79..062aae91
        Entry.bActive = false;
        Entry.AnimChannel = AnimChannel;
        Entry.LastDuration = 0.0f;
        Entry.ArchPoint = &Sphere;
        DockingPoints.push_back(Entry);
    }
}

void CEqObj::init(CreateParms const& Parms)
{
    CSimple::init(Parms);

    VibeId = (int)Parms.VibeId;
    SpaceCostume.PartIds[0] = Parms.SpaceCostume.PartIds[0];
    SpaceCostume.PartIds[1] = Parms.SpaceCostume.PartIds[1];
    SpaceCostume.PartIds[2] = Parms.SpaceCostume.PartIds[2];
    SpaceCostume.PartIds[3] = Parms.SpaceCostume.PartIds[3];

    // Listing does not Vibe::Alloc here. Player/NPC Alloc is a separate Common helper.

    // .text:062a931b..062a9340: the accessory count (+0x138) = Parms+0x94 stored as is (no
    // clamp); when > 0 (signed JLE), AccessoryIds[i] (+0x118) = Parms' (+0x74) while i < the
    // stored count. Retail has no bound: a count of 9 or 10 rewrites +0x138 / +0x13c with the
    // same values this function stores there anyway, anything larger copies the creation
    // parameters' std containers over CEqObj fields (very strange; not reproducible over the
    // port's types), so the copy stops at the 8-entry array.
    SpaceCostume.AccessoryCount = Parms.SpaceCostume.AccessoryCount;
    int const AccessoryCount = (int)SpaceCostume.AccessoryCount;
    for (int AccessoryIndex = 0; AccessoryIndex < AccessoryCount && AccessoryIndex < 8; ++AccessoryIndex)
        SpaceCostume.AccessoryIds[AccessoryIndex] = Parms.SpaceCostume.AccessoryIds[AccessoryIndex];

    VoiceId = Parms.VoiceId;                          // 062a9341 (+0x98 -> +0x13c)

    load_arch_groups(Parms.ArchGroupDescs);           // 062a9356 (+0xac)

    load_equip_and_cargo(Parms.Equipment, true);      // 062a9368 virtual +0xa4 (+0x9c, true)

    // 062a936e..062a9388: any shield generator (FindFirst 0x200) -> virtual link_shields.
    if (EquipManager.FindFirst(EquipTypeShieldGen) != nullptr)
        link_shields();

    // 062a938e..062a9416: MaxPower (+0x19c) = 0, then for every equip of mask 0x40000:
    // MaxPower = float((IsDestroyed() (vtable +0x8) ? 0.0f : Arch->Capacity (+0x70)) +
    // MaxPower); finally Power (+0x198) = MaxPower. No cast and no class test.
    MaxPower = 0.0f;
    CEquipTraverser PowerWalk(EquipTypePower);
    for (CEquip* PowerEquip = EquipManager.Traverse(PowerWalk); PowerEquip != nullptr;
         PowerEquip = EquipManager.Traverse(PowerWalk))
    {
        float const Add = PowerEquip->IsDestroyed()
                              ? 0.0f
                              : ((Archetype::Power const*)PowerEquip->Arch)->Capacity;
        MaxPower = Add + MaxPower;
    }
    Power = MaxPower;
}

CEqObj::DockAnimInfo::DockAnimInfo()
{
    // .text:06287270: byte +0 = 0, dword +4 = -1, +8 = 0, +0xc = 0. Bytes +1..+3 untouched.
    bActive = false;
    AnimChannel = -1;
    LastDuration = 0.0f;
    ArchPoint = nullptr;
}

bool CEqObj::is_targetable() const
{
    // .text:06287240: TEST [this+0xa0],1 / JZ false; FLD [this+0x140] / FCOMP 1.0f /
    // FNSTSW / TEST AH,1 / JZ false. C0 is clear only for CloakPercent >= 1.0; less AND
    // unordered (NaN) set C0 -> true.
    if ((FlagsA0 & 1) == 0)
        return false;
    return !(CloakPercent >= 1.0f);
}

Archetype::EqObj* CEqObj::eqobjarch() const
{
    // 0x06262d50: `MOV EAX,[this+0x88]; RET` (ICF fold).
    return (Archetype::EqObj*)Archetype;
}

bool CEqObj::is_damaged_obj_attached(CacheString const& Name) const
{
    // .text:062a9590: lower_bound (FUN_062fcfb0) on the set at this+0xbc; the result is
    // end() unless its key equals Name; returns result != end().
    return AttachedDamageObjs.find(Name) != AttachedDamageObjs.end();
}

void CEqObj::attaching_damaged_obj(CacheString const& Name)
{
    // .text:062a95f0: the set's unique insert (descent comparing the Value pointer as
    // unsigned, then _Insert FUN_0628fd80 unless an equal key is already present).
    AttachedDamageObjs.insert(Name);
}

unsigned CEqObj::get_name() const
{
    // Common.dll.c 0x4a420: NOT an archetype lookup (that was a copy-paste of
    // CSimple::get_name and wrong for CEqObj). Retail calls
    // Reputation::Vibe::GetName(this+0x104 /*VibeId*/, &fmtA, &fmtB, &wideName) and
    // returns fmtA's mad-lib id (FmtStr+0x04). Matches the identical pattern already
    // used by CShip::get_pilot_name (common_cship.cpp) for the same Vibe::GetName call.
    FmtStr NameFmtA;
    FmtStr NameFmtB;
    wchar_t const* WideName = nullptr;
    Reputation::Vibe::GetName(VibeId, NameFmtA, NameFmtB, WideName);
    return NameFmtA.MadLibId;
}

float CEqObj::get_cloak_percentage() const
{
    return CloakPercent;
}

int CEqObj::is_cloaked() const
{
    // Common.dll.c:45895-45903. Same threshold CELauncher::get_fire_error checks (common_celauncher.cpp,
    // CloakPercent >= 1.0f -> FireOwnerBlocked): CloakPercent is a 0.0-1.0 cloak-fade fraction
    // (see get_cloak_percentage above); >= 1.0 means fully cloaked.
    return (CloakPercent >= 1.0f) ? 1 : 0;
}


float CEqObj::attitude_towards(CEqObj const* Other) const
{
    // Common.dll.c 0x4a640. Reputation-based attitude towards another CEqObj.
    // Disasm lines 140778-140793:
    //   LEA EAX, [ESP+4] (local float)
    //   PUSH EAX (OutAttitude ptr)
    //   ADD EDX, 0x104 (Other->VibeId)
    //   PUSH EDX (IdB ptr)
    //   ADD ECX, 0x104 (this->VibeId)
    //   PUSH ECX (IdA ptr)
    //   CALL Reputation::Vibe::AttitudeTowards
    //   TEST EAX, EAX; JZ success
    //   FLD 0.0f; RET (failure в†’ return 0.0f)
    //   success: FLD [ESP+4]; RET (return OutAttitude)
    // No null test on Other (.text:062aa640..062aa670); retail's out slot is the reused
    // argument slot, read only on success.
    float Attitude = 0.0f;
    int const Result = Reputation::Vibe::AttitudeTowards(this->VibeId, Other->VibeId, Attitude);
    if (Result != 0)
        return 0.0f;
    return Attitude;
}

void CEqObj::attitude_towards_symmetrical(float& OutAtt, CEqObj const* Other,
                                          float& OutReverse) const
{
    // Common.dll.c 0x4a600. Calls Reputation::Vibe::AttitudeTowardsSymmetrical.
    // Disasm lines 140750-140763:
    //   ADD EAX, 0x104 (this->VibeId ptr)
    //   PUSH EAX
    //   ADD ECX, 0x104 (Other->VibeId ptr)
    //   PUSH ECX
    //   CALL AttitudeTowardsSymmetrical(this+0x104, Other+0x104, &OutAtt, &OutReverse)
    //   TEST EAX; JZ success
    //   MOV [EDI], 0 (OutReverse = 0); MOV [ESI], 0 (OutAtt = 0)
    // .text:062aa600..062aa637: no null test on Other and no pre-zeroing; both outputs are
    // zeroed only when the call fails.
    int const Result = Reputation::Vibe::AttitudeTowardsSymmetrical(
        this->VibeId, Other->VibeId, OutAtt, OutReverse);
    if (Result != 0)
    {
        OutAtt = 0.0f;
        OutReverse = 0.0f;
    }
}

void CEqObj::disconnect(IObjRW* Watcher)
{
    // .text:062aa3d0-062aa415. When the behavior manager exists and the object its +0xF0
    // watcher holds (Watchable - 8, or 0) is Watcher, destroy it (FUN_062d2530), free it and
    // clear +0x194. The second `TEST ESI,ESI` at .text:062aa3f5 is the delete-expression's
    // own null test on a pointer already known non-null.
    if (BehaviorMgr == nullptr)
        return;
    if (ObjRwFromWatchable(BehaviorMgr->WatchablePtr) != Watcher)
        return;
    delete BehaviorMgr;
    BehaviorMgr = nullptr;
}

void CEqObj::unmake_physical()
{
    // .text:062aa2f0-062aa350. CEquipTraverser(0x100) (shields); the walk runs while
    // CEShield::cast of the next equip is non-null, tearing each shield's intruder checker
    // down; then the base.
    CEquipTraverser Trav(0x100);
    for (CEShield* Shield = CEShield::cast(EquipManager.Traverse(Trav)); Shield != nullptr;
         Shield = CEShield::cast(EquipManager.Traverse(Trav)))
    {
        Shield->DestroyIntruderChecker();                        // 062aa326
    }
    CSimple::unmake_physical();                                  // 062aa346
}

void CEqObj::remake_physical(PhySys::CreateParms const& Parms, float Radius)
{
    // .text:062aa360-062aa3c8. The base first, then the same shield walk re-attaching each
    // intruder checker.
    CObject::remake_physical(Parms, Radius);                     // 062aa372
    CEquipTraverser Trav(0x100);
    for (CEShield* Shield = CEShield::cast(EquipManager.Traverse(Trav)); Shield != nullptr;
         Shield = CEShield::cast(EquipManager.Traverse(Trav)))
    {
        Shield->AttachIntruderChecker();                         // 062aa3a6
    }
}

int CEqObj::update(float Dt, unsigned Flags)
{
    // .text:062aa520-062aa5f1.
    CObject::update(Dt, Flags);                                  // 062aa532

    // 062aa537-062aa55f: the flag byte (reusing the Flags stack slot) is set only when
    // bWeaponsOffline and get_relative_health() <= 0.0 (FCOMP double 0.0 at 0639c430,
    // TEST AH,0x41 + JNP: exactly one of C0 / C3 set -> less or equal keeps 1; greater or
    // unordered -> 0).
    bool bWeaponsOfflineDestroyed = false;
    if (bWeaponsOffline != 0)
    {
        double const RelHealth = CSimple::get_relative_health();
        bWeaponsOfflineDestroyed = (RelHealth <= 0.0);
    }

    // 062aa55f-062aa59b: CEquipTraverser(0xff3efffc), Update(Dt, Flags) (vtable +0x24).
    CEquipTraverser Trav(-0xc10004);
    CEquip* Item = EquipManager.Traverse(Trav);
    while (Item != nullptr)
    {
        Item->Update(Dt, Flags);
        Item = EquipManager.Traverse(Trav);
    }

    // 062aa59d-062aa5de: BehaviorMgr present, not destroyed, Flags & 8 (dword 0639e530 = 8)
    // clear and ControlExclusion bit 0 clear -> FUN_062d2b10(Dt) on the manager. The two
    // calls around it (062aa5c3 / 062aa5d9, PUSH 1 / PUSH 0) go to 062a8920, a bare RET.
    if (BehaviorMgr != nullptr && !bWeaponsOfflineDestroyed && (Flags & 8) == 0 &&
        (ControlExclusion & 1) == 0)
    {
        BehaviorMgr->UpdateBehaviorTick(Dt);
    }

    update_docking_animations(Dt);                               // 062aa5e4
    return 0;
}

ObjActivateResult CEqObj::activate(bool bOn, unsigned DockIndex)
{
    // .text:062ab130. DockIndex -1 fails; so does an index outside the ARCHETYPE's docking
    // spheres (Archetype+0x84 vector, 0x10-byte elements, begin == null counts as empty) --
    // retail bounds against the archetype, not against this+0x184. DockingPoints is built 1:1
    // from those spheres by init_docking_points. Then the byte at DockingPoints[i] is swapped.
    if (DockIndex == 0xffffffffu)
        return ObjActivateFailed;
    Archetype::EqObj const* Arch = eqobjarch();
    if (DockIndex >= Arch->DockingSpheres.size())
        return ObjActivateFailed;
    bool WasOn = DockingPoints[DockIndex].bActive;
    DockingPoints[DockIndex].bActive = bOn;
    return (bOn == WasOn) ? ObjActivateNoChange : ObjActivateChanged;
}

bool CEqObj::get_activate_state(std::vector<bool>& OutStates)
{
    // Common.dll.c 0x4b190 (78762-78843). Appends bActive for every DockingPoints entry
    // (the byte CEqObj::activate writes) and returns true if any
    // docking point is currently active. Retail hand-inlines std::vector<bool>'s
    // bit-packed push_back/growth machinery; the port's std::vector<bool> does the
    // same thing through its normal push_back.
    bool bAnyActive = false;
    for (DockAnimInfo const& Entry : DockingPoints)
    {
        bool bActive = Entry.bActive;
        OutStates.push_back(bActive);
        if (bActive)
            bAnyActive = true;
    }
    return bAnyActive;
}

bool CEqObj::get_sub_obj_velocity(unsigned short SubObjId, Vector& Out) const
{
    // .text:062ab330-062ab3ea. Out is first set to this object's own velocity (vtable +0x54,
    // 062ab33a-062ab35c) on every path; a sub-object query writes a temporary that is copied
    // into Out only on success.
    Out = get_velocity();
    Vector Local;
    if (SubObjectID::IsEquipID(SubObjId))                        // 062ab35f
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return false;
        CExternalEquip const* External = CExternalEquip::cast(Eq);  // 062ab37c
        if (External == nullptr)
            return false;
        if (!External->GetVelocity(Local))                       // vtable +0x6c, 062ab391
            return false;
    }
    else
    {
        if (!SubObjectID::IsArchGroupID(SubObjId))               // 062ab396
            return false;
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return false;
        if (!Group->GetVelocity(Local))                          // 062ab3b9
            return false;
    }
    Out = Local;                                                 // 062ab3c2-062ab3d6
    return true;
}

bool CEqObj::get_sub_obj_center_of_mass(unsigned short SubObjId, Vector& Out) const
{
    // Listing 0x4b3f0, re-read in full this pass: 062ab3f0 .. 062ab4b8.
    //
    // This body used to be a bare `return false;` on the strength of two claims that have
    // both since gone stale: (a) "CExternalEquip vtable-slot ambiguity with no CAttachedEquip
    // fallback" -- CExternalEquip::GetCenterOfMass IS the +0x70 virtual and is declared in
    // common_cequip.h, with CAttachedEquip overriding it; and (b) "CArchGroup::GetCenterOfMass
    // doesn't exist in this port" -- it does now (this file, listing 0x40360). Both branches
    // are wired for real.
    //
    // Note retail leaves Out untouched on every failure path (BL is zeroed at 062ab3fd and
    // LAB_062ab4b2 just returns AL=BL), so the unconditional zero-fill this used to do was
    // itself a deviation and is gone.
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return false;
        CExternalEquip* External = CExternalEquip::cast(const_cast<CEquip*>(Eq));
        if (External == nullptr)
            return false;
        Vector Local{ 0.0f, 0.0f, 0.0f };
        if (!External->GetCenterOfMass(Local))
            return false;
        Out = Local;
        return true;
    }
    if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return false;
        Vector Local{ 0.0f, 0.0f, 0.0f };
        if (!Group->GetCenterOfMass(Local))
            return false;
        Out = Local;
        return true;
    }
    return false;
}

bool CEqObj::get_sub_obj_max_hit_pts(unsigned short SubObjId, float& Out) const
{
    // Common.dll.c 0x4b820 (79147-79179).
    Out = 0.0f;
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return false;
        Out = Eq->GetMaxHitPoints();
        return true;
    }
    if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return false;
        Out = (float)Group->GetMaxHitPoints();                        // 062ab886 FSTP float
        return true;
    }
    return false;
}

bool CEqObj::get_sub_obj_relative_health(unsigned short SubObjId, float& Out) const
{
    // Common.dll.c 0x4b8a0 (79185-79223).
    Out = 0.0f;
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return false;
        Out = (float)Eq->GetRelativeHealth();                         // 062ab8d4 FSTP float
        return true;
    }
    if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return false;
        Out = (float)Group->GetRelativeHealth();                      // 062ab90a FSTP float
        return true;
    }
    if (SubObjId == 1)
    {
        Out = (float)CSimple::get_relative_health();                  // 062ab924 FSTP float
        return true;
    }
    return false;
}

long CEqObj::sub_obj_id_to_inst(unsigned short SubObjId) const
{
    // Common.dll.c 0x4b990 (79252-79291). See class-comment on this override's declaration
    // re: reusing CAttachedEquip::GetRootIndex() for the retail vtable+0x84 slot.
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return -1;
        CAttachedEquip* const Attached = CAttachedEquip::cast(const_cast<CEquip*>(Eq));
        if (Attached == nullptr)
        {
            // 062ab9e0..062aba0a.
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCEqObjSource, 0x4ee,
                  "Tried to get inst for non-attached equipment.");
            return -1;
        }
        // vtable +0x84 = ??_7CAttachedEquip slot 0639c6a0 = CAttachedEquip::GetRootIndex.
        return Attached->GetRootIndex();
    }
    if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return -1;
        return Group->GetRootIndex();
    }
    if (SubObjId == 1)
        return EngineInstanceId;
    return -1;
}

IBehaviorManager* CEqObj::create_behavior_interface(IObjRW* Owner, int GraphId)
{
    // Common.dll.c 0x4c240 (Common.dll.txt 143889-143932). .text:062ac259-062ac298: when
    // +0x194 is null, operator_new(0x1cc) + FUN_062d2220(Owner) (IBehaviorManager's ctor
    // takes the IObjRW* argument, .text:062ac280-062ac287). Both paths then run
    // FUN_062d2830(this+0xAC) at .text:062ac29e-062ac2ab and set_state_graph at .text:062ac2bd.
    if (BehaviorMgr == nullptr)
        BehaviorMgr = new IBehaviorManager(Owner);
    ConnectBehaviorManagerObjectDb(BehaviorMgr, ObjectDb);
    BehaviorMgr->set_state_graph(GraphId, false);
    return BehaviorMgr;
}

void CEqObj::enable_controllers()
{
    // Common.dll.c 0x4a250 / raw 062aa250-062aa290. Retail calls vtable+0x4c on every
    // equip via CEquipManager::Traverse(all types). BUG FIX: this used to index *this
    // port's own* freshly-built vtable by that raw retail byte offset (Vt[0x4c/4]), but
    // CEquip's actual declared virtual order here puts EnableController/DisableController
    // at slots 9/10 (byte 0x24/0x28) -- 0x4c/0x50 land on GetStatus/GetConnectionPosition
    // instead (counted from the class body in common_cequip.h: ~CEquip, IsActive,
    // IsDestroyed, IsFunctioning, Activate, Destroy, GetMaxHitPoints, SetHitPoints,
    // IsDisabled, EnableController, DisableController, IsControllerEnabled, Update,
    // GetHitPoints, GetRelativeHealth, IsTemporary, GetEquipDesc, CanDelete, IsLootable,
    // GetStatus(0x4c), GetConnectionPosition(0x50)). Retail's own vtable order differs
    // from this port's declaration order, so raw slot-offset dispatch on a locally
    // compiled object is invalid here -- calling the named virtual directly is both
    // correct and immune to future reordering.
    CEquipTraverser Trav; // default ctor already sets TypeMask=0xFFFFFFFF (all types)
    for (CEquip* Eq = EquipManager.Traverse(Trav); Eq != nullptr; Eq = EquipManager.Traverse(Trav))
        Eq->EnableController();
}

void CEqObj::disable_controllers()
{
    // Common.dll.c 0x4a2a0 / raw 062aa2a0-062aa2e0. See enable_controllers() above for why
    // this calls the named virtual instead of indexing the port's own vtable by retail's
    // 0x50 byte offset (which lands on GetConnectionPosition in this port's declared order).
    CEquipTraverser Trav;
    for (CEquip* Eq = EquipManager.Traverse(Trav); Eq != nullptr; Eq = EquipManager.Traverse(Trav))
        Eq->DisableController();
}

bool CEqObj::launch_pos(Vector& OutPos, Matrix& OutOrient, int HardpointIndex) const
{
    // Common.dll.c 0x4a980, traced from the raw asm (062aa980-062aab42) rather than the
    // decompiled C, whose scratch-stack variable names for the quaternion/matrix buffers
    // (local_70/local_64/local_54/local_48/afStack_30) overlap in memory in ways Ghidra
    // named inconsistently -- the data flow below (build a quaternion, turn it into a
    // matrix, compose it onto the hardpoint's orientation) is unambiguous regardless.
    //
    // Fast path: HardpointIndex selects one of the archetype's docking/launch points.
    // Retail resolves it against the archetype's DockingSpheres array (this+0x88 ->
    // Archetype::Root, +0x88/+0x8c begin/end of a 0x10-byte-stride vector, name at +4).
    // STALE COMMENT FIXED (D50, 2026-09-09): EqObj::read() DOES populate DockingSpheres
    // today (common_archetype_ships.cpp's "docking_sphere" case) -- this branch is
    // live, not dormant. Also consumed by CEqObj::init_docking_points (DockingPoints,
    // populated 1:1 from Arch->DockingSpheres) and CEqObj::dock_terminal_valid.
    // 062aa98b-062aa9b6: index != -1, then the archetype (+0x88, no null test) DockingSpheres
    // vector: an empty one (begin == null) or an index not below its size (unsigned JNC)
    // takes the fallback.
    Archetype::EqObj* Arch = eqobjarch();
    if (HardpointIndex != -1 && (unsigned)HardpointIndex < (unsigned)Arch->DockingSpheres.size())
    {
        // 062aa9bc-062aa9de: HardpointPosition(instance, sphere+0x04 name, &OutPos, &OutOrient).
        char const* HpName = Arch->DockingSpheres[(unsigned)HardpointIndex].HardpointName.c_str();
        HardpointPosition(this->EngineInstanceId, HpName, &OutPos, &OutOrient);

        // 062aa9e3-062aaa3b: Col1 = column 1 of the hardpoint orientation (float copies);
        // W = (float)cos(A), A = DAT_0639e2d8 (double, float pi/2); the scale
        // sin(A) / sqrt((z*z + x*x) + y*y) stays on the FPU (double) and is stored as the
        // float argument of FUN_06288830 (Vector * float). No zero-length test.
        double const kSpreadAngle = 1.5707963705062866;
        Vector const Col1{ OutOrient.E[0][1], OutOrient.E[1][1], OutOrient.E[2][1] };
        float const W = static_cast<float>(std::cos(static_cast<double>(kSpreadAngle)));
        double const Len = sqrt(((double)Col1.z * Col1.z + (double)Col1.x * Col1.x) + (double)Col1.y * Col1.y);
        float const Scale = (float)(std::sin(static_cast<double>(kSpreadAngle)) / Len);
        struct FQuat
        {
            float W, X, Y, Z;
        };
        FQuat const Quat{ W, Scale * Col1.x, Scale * Col1.y, Scale * Col1.z };

        // 062aaa55-062aaac1: the cached 3DMathEngine (no null test): QuaternionToMatrix
        // (+0x54) into a temporary, then MulMatrix (+0x20)(tmp, Spread, OutOrient) copied back
        // into OutOrient.
        I3DMathEngine* Math = Acquire3DMathEngine();
        Matrix SpreadMatrix;
        Math->QuaternionToMatrix((FMatrix&)SpreadMatrix, (FQuaternion const&)Quat);
        Matrix Product;
        Math->MulMatrix((FMatrix&)Product, (FMatrix const&)SpreadMatrix, (FMatrix const&)OutOrient);
        OutOrient = Product;
        return true;
    }

    // 062aaace-062aab3c: OutPos = +0x2c, OutOrient = +0x08 (9 dwords), then rows 0 and 2 of
    // every column rotated by A = DAT_0639e2d0 (double, float pi): c = cos(A), s = sin(A)
    // stay on the FPU, each element is computed in double and rounded once at the FSTP:
    // row0 = s*row2 + c*row0, row2 = c*row2 - s*row0 (row0 read before its store).
    OutPos = this->WorldPosition;
    OutOrient = this->Orientation;

    double const kFallbackAngle = 3.1415927410125732;
    double const CosA = std::cos(static_cast<double>(kFallbackAngle));
    double const SinA = std::sin(static_cast<double>(kFallbackAngle));
    for (int Col = 0; Col < 3; ++Col)
    {
        double const Row0 = OutOrient.E[0][Col];
        double const Row2 = OutOrient.E[2][Col];
        OutOrient.E[0][Col] = (float)(SinA * Row2 + Row0 * CosA);
        OutOrient.E[2][Col] = (float)(CosA * Row2 - Row0 * SinA);
    }
    return false;
}

bool CEqObj::get_explosion_dmg_bounding_sphere(float& OutRadius, Vector& OutCenter) const
{
    // Common.dll.c 0x4bd50. Lazily computes and caches a LOCAL-space (instance-relative)
    // bounding sphere the first time this is called, then re-transforms the cached local
    // center into the object's CURRENT world transform on every call (only the shape is
    // invariant; the object can move between calls). EngineObject's Orientation+WorldPosition
    // overlay a Transform (see the static_assert in common_cobject.h), matching retail's use
    // of this+8 as a Transform* here.
    if (!bExplosionDmgCached)                                    // 062abd56 byte test
    {
        bExplosionDmgCached = true;                              // 062abd70
        compute_explosion_dmg_bounding_sphere(ExplosionDmgRadius, ExplosionDmgCenter);
    }
    OutRadius = ExplosionDmgRadius;                              // 062abd7c-062abd86

    // 062abd88-062abdc6: the cached 3D math engine (lazily acquired, never null-tested),
    // slot +0x28 transforms the local center by this+8 into a stack temporary, which is
    // then copied to OutCenter.
    Vector World;
    Acquire3DMathEngine()->TransformXform((FVector&)World, (FTransform const&)Orientation,
                                          (FVector const&)ExplosionDmgCenter);
    OutCenter = World;
    return true;                                                 // 062abdc9
}

void CEqObj::compute_explosion_dmg_bounding_sphere(float& OutRadius, Vector& OutCenter) const
{
    // .text:062abde0..062ac220. Everything is gathered in WORLD space and only the final
    // centre goes back to object space. No null tests (DALib::Engine, the math engine).
    struct FChildSphere
    {
        Vector Center;   // +0x00 world centre of a root-group child
        float Radius;    // +0x0c its extent
    };

    I3DMathEngine* const Math = Acquire3DMathEngine();     // DAT_063fc2e4 cache
    FTransform const& Xform = (FTransform const&)Orientation;   // this+0x08
    void* const Instance = (void*)EngineInstanceId;

    // 062abe03..062abebe: root extent (IEngine +0x88, flags 1) -> radius R0 and local centre;
    // TransformXform (+0x28) by this+0x08 -> world centre W0; the AABB starts as W0 +- R0
    // (each bound FSTP dword).
    float RootRadius;
    Vector RootLocal;
    DALib::Engine->GetInstanceExtent(Instance, 1, &RootRadius, (float*)&RootLocal);
    Vector RootWorld;
    Math->TransformXform((FVector&)RootWorld, Xform, (FVector const&)RootLocal);
    float MinX = RootWorld.x - RootRadius;
    float MaxX = RootWorld.x + RootRadius;
    float MinY = RootWorld.y - RootRadius;
    float MaxY = RootWorld.y + RootRadius;
    float MinZ = RootWorld.z - RootRadius;
    float MaxZ = RootWorld.z + RootRadius;

    // 062abec2..062ac051: every related instance (IEngine +0xd8 (instance, 0, cursor), from
    // -1 until -1) whose inst_to_subobj_id (vtable +0xf4) is SUBOBJ_ID_ROOT: its extent and
    // its own transform (IEngine +0xa0) give a world centre W and radius r; {W, r} is
    // appended to a local vector (FUN_062a0ee0 insert-at-end); each bound takes W -+ r
    // (unrounded) unless it already lies strictly beyond it -- TEST AH,0x5 / JNP keeps a
    // min only when min < value, TEST AH,0x41 / JZ keeps a max only when max > value, NaN
    // replaces.
    std::vector<FChildSphere> Children;
    for (long Child = (long)DALib::Engine->GetRelatedInstance(Instance, 0, G_InvalidEngineHandle);
         Child != -1;
         Child = (long)DALib::Engine->GetRelatedInstance(Instance, 0, (void*)Child))
    {
        if (inst_to_subobj_id(Child) != SUBOBJ_ID_ROOT)
            continue;

        float ChildRadius;
        Vector ChildLocal;
        DALib::Engine->GetInstanceExtent((void*)Child, 1, &ChildRadius, (float*)&ChildLocal);
        FTransform const& ChildXform =
            *(FTransform const*)DALib::Engine->GetInstanceTransform((void*)Child);
        FChildSphere Sphere;
        Math->TransformXform((FVector&)Sphere.Center, ChildXform, (FVector const&)ChildLocal);
        Sphere.Radius = ChildRadius;
        Children.push_back(Sphere);

        double const LowX = (double)(Sphere.Center.x) - Sphere.Radius;
        if (!(MinX < LowX))
            MinX = (float)(LowX);
        double const HighX = (double)(Sphere.Center.x) + Sphere.Radius;
        if (!(MaxX > HighX))
            MaxX = (float)(HighX);
        double const LowY = (double)(Sphere.Center.y) - Sphere.Radius;
        if (!(MinY < LowY))
            MinY = (float)(LowY);
        double const HighY = (double)(Sphere.Center.y) + Sphere.Radius;
        if (!(MaxY > HighY))
            MaxY = (float)(HighY);
        double const LowZ = (double)(Sphere.Center.z) - Sphere.Radius;
        if (!(MinZ < LowZ))
            MinZ = (float)(LowZ);
        double const HighZ = (double)(Sphere.Center.z) + Sphere.Radius;
        if (!(MaxZ > HighZ))
            MaxZ = (float)(HighZ);
    }

    // 062ac057..062ac0b9: centre = AABB midpoint (x and y sums kept unrounded, the z sum
    // FSTP dword first), each * 0.5f, written to OutCenter.
    double const SumX = (double)(MaxX) + MinX;
    double const SumY = (double)(MaxY) + MinY;
    float const SumZ = (float)((double)(MaxZ) + MinZ);
    OutCenter.x = (float)(SumX * 0.5f);
    OutCenter.y = (float)(SumY * 0.5f);
    OutCenter.z = (float)((double)(SumZ) * 0.5f);

    // 062ac0ad..062ac129: widest span. wx unrounded, wy and wz FSTP dword; best = wx > wy ?
    // wx : wy, then best > wz ? best : wz (TEST AH,0x41 compares, NaN takes the right-hand
    // value); half = widest * 0.5f; OutRadius = float(half).
    double const WidthX = (double)(MaxX) - MinX;
    float const WidthY = (float)((double)(MaxY) - MinY);
    float const WidthZ = (float)((double)(MaxZ) - MinZ);
    double const BestXY = (WidthX > WidthY) ? WidthX : (double)(WidthY);
    double const Widest = (BestXY > WidthZ) ? BestXY : (double)(WidthZ);
    double const Half = Widest * 0.5f;
    OutRadius = (float)(Half);

    // 062ac12b..062ac174: reach = |centre - W0| + R0 (sqrt((dz^2 + dy^2) + dx^2) on the x87
    // stack); reach > half (TEST AH,0x41 / JNZ not taken) -> OutRadius = float(reach).
    {
        double const Dx = (double)(OutCenter.x) - RootWorld.x;
        double const Dy = (double)(OutCenter.y) - RootWorld.y;
        double const Dz = (double)(OutCenter.z) - RootWorld.z;
        double const Reach = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) + RootRadius;
        if (Reach > Half)
            OutRadius = (float)(Reach);
    }

    // 062ac176..062ac1c7: each collected child: reach = |W - centre| + r; reach > OutRadius
    // (FCOM dword / TEST AH,0x41 / JNZ not taken) -> OutRadius = float(reach).
    for (std::vector<FChildSphere>::const_iterator It = Children.begin(); It != Children.end(); ++It)
    {
        double const Dx = (double)(It->Center.x) - OutCenter.x;
        double const Dy = (double)(It->Center.y) - OutCenter.y;
        double const Dz = (double)(It->Center.z) - OutCenter.z;
        double const Reach = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) + It->Radius;
        if (Reach > OutRadius)
            OutRadius = (float)(Reach);
    }

    // 062ac1c9..062ac203: OutCenter back to object space with InverseTransform (+0x3c) by
    // this+0x08 (into a temporary, then copied); the vector's buffer is freed (FUN_06391d5a).
    Vector LocalCenter;
    Math->InverseTransform((FVector&)LocalCenter, Xform, (FVector const&)OutCenter);
    OutCenter = LocalCenter;
}

IBehaviorManager* CEqObj::get_behavior_interface()
{
    // Common.dll.c 0x4c230
    return BehaviorMgr;
}

void CEqObj::set_control_exclusion(unsigned int Flags)
{
    // Common.dll.c 0x4c2e0
    ControlExclusion = Flags;
}

bool CEqObj::is_control_excluded(unsigned int Flags) const
{
    return (ControlExclusion & Flags) != 0;
}

unsigned CEqObj::get_base() const
{
    return DockingBaseId;
}

bool CEqObj::is_base() const
{
    // Common.dll.c 0x27670: return *(int*)(this+0x160) != 0;
    return DockingBaseId != 0;
}

unsigned int const& CEqObj::get_dock_target() const
{
    return DockingPointBaseId;
}

bool CEqObj::is_dock() const
{
    // Common.dll.c 0x27680: return *(int*)(this+0x164) != 0;
    return DockingPointBaseId != 0;
}

float CEqObj::get_power() const
{
    return Power;
}

// Common.dll.c 0x276c0
void CEqObj::set_power(float NewPower)
{
    Power = NewPower;
}

float CEqObj::get_max_power() const
{
    return MaxPower;
}

float CEqObj::get_power_ratio() const
{
    // Common.dll.c 0x276e0: `if (MaxPower<0.0 != MaxPower==0.0) return 0.0;` is the
    // decompiler's split-flag rendering of `if (MaxPower <= 0.0f) return 0.0f;`
    // (true when MaxPower<0 OR MaxPower==0, false only when MaxPower>0).
    if (MaxPower <= 0.0f)
        return 0.0f;
    return Power / MaxPower;
}

int CEqObj::get_vibe() const
{
    return VibeId;
}

int CEqObj::enumerate_sub_objs() const
{
    // Common.dll.c 0x4ba80: return *(int*)(this+0x158) + *(int*)(this+0xfc) + *(int*)(this+0xf0);
    // this+0xf0/this+0xfc land inside the embedded CEquipManager (starts at this+0xe4):
    // EquipManager+0x0c == ExternalCount, EquipManager+0x18 == InternalCount. this+0x158 lands
    // inside the embedded CArchGroupManager (starts at this+0x144): ArchGroups+0x14 == Count.
    return (int)ArchGroups.Count + (int)EquipManager.InternalCount + (int)EquipManager.ExternalCount;
}

unsigned CEqObj::get_base_name() const
{
    // .text:062ab100..062ab120: (+0x160 & 0x3fffffff) == 0 -> 0; else tail jump to
    // Universe::get_base(+0x160) (no null test) vtable +0x08 -- IBase slot 2, the base's
    // strid_name (+0x0c), not its nickname id.
    if ((DockingBaseId & 0x3fffffff) == 0)
        return 0;
    return (unsigned)Universe::get_base(DockingBaseId)->GetStridName();
}

float CEqObj::get_total_hit_pts() const
{
    // Common.dll.c 0x4b4c0: this object's own hit points plus every EquipTypeMask(0x1ee0)
    // equip's GetHitPoints() plus every non-Killed/Destroyed arch group's GetHitPoints().
    float Total = get_hit_pts();

    CEquipManager& MutableEquipManager = const_cast<CEquipManager&>(EquipManager);
    CEquipTraverser Trav(0x1ee0);
    for (CEquip const* Eq = MutableEquipManager.Traverse(Trav); Eq != nullptr; Eq = MutableEquipManager.Traverse(Trav))
        Total += Eq->GetHitPoints();

    CArchGrpTraverser GroupTrav;
    for (CArchGroup const* Group = ArchGroups.Traverse(GroupTrav); Group != nullptr;
         Group = ArchGroups.Traverse(GroupTrav))
    {
        if (Group->State != 2 && Group->State != 3)
            Total += Group->GetHitPoints();
    }
    return Total;
}

float CEqObj::get_total_max_hit_pts() const
{
    // Common.dll.c 0x4b570: same shape as get_total_hit_pts() but with the Max variants.
    float Total = get_max_hit_pts();

    CEquipManager& MutableEquipManager = const_cast<CEquipManager&>(EquipManager);
    CEquipTraverser Trav(0x1ee0);
    for (CEquip const* Eq = MutableEquipManager.Traverse(Trav); Eq != nullptr; Eq = MutableEquipManager.Traverse(Trav))
        Total += Eq->GetMaxHitPoints();

    CArchGrpTraverser GroupTrav;
    for (CArchGroup const* Group = ArchGroups.Traverse(GroupTrav); Group != nullptr;
         Group = ArchGroups.Traverse(GroupTrav))
    {
        if (Group->State != 2 && Group->State != 3)
            Total = (float)(Total + Group->GetMaxHitPoints()); // 062ab5f4 FADD float, FSTP float
    }
    return Total;
}

float CEqObj::get_total_relative_health() const
{
    // .text:062ab620..062ab716: both totals accumulated in one pass (each FADD + FSTP dword);
    // then Max FCOMP 0.0 / TEST AH,0x41 / JP: divide when neither C0 nor C3 (Max > 0) or both
    // (NaN); Max < 0 or == 0 returns 0.0f. Hit / Max is returned in ST0.
    float Hit = get_hit_pts();
    float Max = get_max_hit_pts();

    CEquipManager& MutableEquipManager = const_cast<CEquipManager&>(EquipManager);
    CEquipTraverser Trav(0x1ee0);
    for (CEquip const* Eq = MutableEquipManager.Traverse(Trav); Eq != nullptr; Eq = MutableEquipManager.Traverse(Trav))
    {
        Hit += Eq->GetHitPoints();
        Max += Eq->GetMaxHitPoints();
    }

    CArchGrpTraverser GroupTrav;
    for (CArchGroup const* Group = ArchGroups.Traverse(GroupTrav); Group != nullptr;
         Group = ArchGroups.Traverse(GroupTrav))
    {
        if (Group->State != 2 && Group->State != 3)
        {
            Hit += Group->GetHitPoints();
            Max = (float)(Max + Group->GetMaxHitPoints()); // 062ab6d3 FADD float, FSTP float
        }
    }
    if (Max < 0.0f || Max == 0.0f)
        return 0.0f;
    return Hit / Max;
}

bool CEqObj::get_sub_obj_hit_pts(unsigned short SubObjId, float& OutHitPts) const
{
    // Common.dll.c 0x4b720.
    OutHitPts = 0.0f;
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq != nullptr)
        {
            OutHitPts = Eq->GetHitPoints();
            return true;
        }
    }
    else if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup const* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group != nullptr)
        {
            OutHitPts = Group->GetHitPoints();
            return true;
        }
    }
    return false;
}

bool CEqObj::set_sub_obj_hit_pts(unsigned short SubObjId, float HitPts)
{
    // Common.dll.c 0x4b7a0.
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip* Eq = EquipManager.FindByID(SubObjId);
        if (Eq != nullptr)
        {
            Eq->SetHitPoints(HitPts);
            return true;
        }
    }
    else if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = ArchGroups.FindByID(SubObjId);
        if (Group != nullptr)
        {
            Group->SetHitPoints(HitPts);
            return true;
        }
    }
    return false;
}

unsigned short CEqObj::inst_to_subobj_id(long Instance) const
{
    // Common.dll.c 0x4b940: the root engine instance always maps to SubObjId 1; otherwise
    // try the equip manager's then the arch-group manager's own InstToSubObjID, falling
    // back to 1 if neither claims the instance.
    if (Instance != EngineInstanceId)
    {
        unsigned short SubObjId = EquipManager.InstToSubObjID(Instance);
        if (SubObjId != 0)
            return SubObjId;
        SubObjId = ArchGroups.InstToSubObjID(Instance);
        if (SubObjId != 0)
            return SubObjId;
    }
    return 1;
}

void CEqObj::flush_animations()
{
    // Common.dll.c 0x4ad30 (78559-78577). Walks DockingPoints (each entry is a 0x10-byte
    // retail record: Flags/AnimChannel/LastDuration/ArchPoint -- AnimChannel is "-1 if none").
    // For every live handle: AnimDB::Rem(handle) then Anim_exref's DestroyScript (vtable
    // slot 10 / +0x28) -- NOT StopPlayback (+0x38); unlike CShip::flush_animations's
    // BayAnim.Index release and FAnimScriptRef::Release, retail does not call
    // StopPlayback here. Finally resets the vector to empty (retail sets end=begin,
    // i.e. size 0 keeping capacity -- matches vector::clear()).
    // .text:062aad30..062aad6f: no null test on DALib::Anim. 062aad71..: erase(begin, end)
    // (the element copy loop over an empty tail, then end = begin).
    for (std::vector<DockAnimInfo>::iterator It = DockingPoints.begin(); It != DockingPoints.end(); ++It)
    {
        if (It->AnimChannel != -1)
        {
            AnimDB::Rem(It->AnimChannel);
            DALib::Anim->DestroyScript((void*)It->AnimChannel);
        }
    }
    DockingPoints.clear();
}

void CEqObj::update_docking_animations(float Dt)
{
    // .text:062aab50..062aad29. Dt (the stack argument) is never read. For every docking
    // point (+0x184 .. +0x188, stride 0x10) whose channel (+0x4) is not -1: D = float(Anim
    // slot 0x40 GetStoredDuration(channel)), P = Anim slot 0x14 (channel) -- the same id, see
    // the note on slot 0x14 in CShip::open_bay. No null test on DALib::Anim. Every path that
    // reaches a live entry ends with LastDuration (+0x8) = D.
    (void)Dt;

    for (std::vector<DockAnimInfo>::iterator It = DockingPoints.begin(); It != DockingPoints.end(); ++It)
    {
        DockAnimInfo& Entry = *It;
        if (Entry.AnimChannel == -1)
            continue;

        float const Duration = DALib::Anim->GetStoredDuration(Entry.AnimChannel);
        float const Position = DALib::Anim->GetScriptDuration((void*)Entry.AnimChannel);

        if (Entry.bActive)
        {
            // 062aabac..062aac5a: D < P (TEST AH,0x5 / JP: C0 alone; NaN skips). D == 0
            // (TEST AH,0x44 / JP: C3 alone) -> notify(0, &flag = 1) (vtable +0xc8, no Notify
            // test here), EvalPlayback(channel, 0, 0.0f, 1.0f, 0, 1.0f, 1.0f, 0), AnimDB::Add;
            // otherwise D <= LastDuration (TEST AH,0x41 / JP: exactly one of C0/C3) ->
            // EvalPlayback(channel, 0, -2.0f, ...).
            if (Duration < Position)
            {
                if (Duration == 0.0f)
                {
                    unsigned char Flag = 1;
                    notify(INotify::DockAnimEnded, &Flag);
                    DALib::Anim->EvalPlayback(Entry.AnimChannel, 0, 0.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                                              AsOpaqueDword(1.0f), nullptr);
                    AnimDB::Add(Entry.AnimChannel);
                }
                else if (Duration <= Entry.LastDuration)
                {
                    DALib::Anim->EvalPlayback(Entry.AnimChannel, 0, -2.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                                              AsOpaqueDword(1.0f), nullptr);
                }
            }
        }
        else if (Duration > 0.0f)
        {
            // 062aac5f..062aad08: D > 0 (TEST AH,0x41 / JNZ). D >= P (TEST AH,0x1 / JNZ not
            // taken; NaN takes the other side) -> notify(0, &flag = 0), EvalPlayback(channel,
            // 1, -1.0f, ...), AnimDB::Add; otherwise D >= LastDuration (TEST AH,0x1) ->
            // EvalPlayback(channel, 1, -2.0f, ...).
            if (Duration >= Position)
            {
                unsigned char Flag = 0;
                notify(INotify::DockAnimEnded, &Flag);
                DALib::Anim->EvalPlayback(Entry.AnimChannel, 1, -1.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                                          AsOpaqueDword(1.0f), nullptr);
                AnimDB::Add(Entry.AnimChannel);
            }
            else if (Duration >= Entry.LastDuration)
            {
                DALib::Anim->EvalPlayback(Entry.AnimChannel, 1, -2.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                                          AsOpaqueDword(1.0f), nullptr);
            }
        }

        Entry.LastDuration = Duration;
    }
}

void CEqObj::destroy_equipment(DamageList const& Damage, bool bParam2)
{
    // Common.dll.c 79321-79380 (raw asm 0x062abaa0-0x062abbc7). Walks Damage.Entries (retail's
    // raw VC6 std::list, ported here as std::list<DamageEntry> -- see DamageList's class
    // comment in common_misc.h). Skips entries whose Fate == SubObjFate::None, same idiom as
    // CArchGroupManager::DestroyGroups.
    for (DamageEntry const& Entry : Damage.Entries)
    {
        if (Entry.Fate == DamageEntry::SubObjFate::None)
            continue;

        if (SubObjectID::IsEquipID(Entry.SubObjId))
        {
            CEquip* Equip = EquipManager.FindByID(Entry.SubObjId);
            if (Equip == nullptr)
                continue;

            // 062abafe-062abb22: DamageList+0x10 == 0x14 (a cause value the port's DamageCause
            // enum does not name) -> this vt+0xc8 = CEqObj::notify (CEqObj vftable 0639be5c +
            // 0xc8 = 0639bf24) with event 4 for a cargo equip (Type +0x14 == 0x10000), else 3,
            // and the zero-extended SubObjId as the context.
            if ((int)Damage.Cause == 0x14)
                notify((INotify::Event)(Equip->Type == 0x10000 ? 4 : 3), (void*)(unsigned)Entry.SubObjId);

            CAttachedEquip* Attached = CAttachedEquip::cast(Equip);
            if (Attached != nullptr)
            {
                if (Entry.Fate == DamageEntry::SubObjFate::Separated && !bParam2)
                {
                    // Raw asm 062abb54-062abb63: SetFate(HealthProxy) + Destroy(), then skips
                    // the second (unconditional) Destroy() call below via a goto.
                    Attached->SetFate(DamageEntry::SubObjFate::HealthProxy);
                    Equip->Destroy();
                    continue;
                }
                Attached->SetFate(Entry.Fate);
            }
            Equip->Destroy();
        }
        else if (SubObjectID::IsArchGroupID(Entry.SubObjId))
        {
            // Retail vtable+0x1c call on every traversed equip is a confirmed empty no-op in
            // the base CEquip implementation (see CEquip::NotifyArchGroupDestroyed's own
            // comment for the ICF-fold trace) -- ported faithfully as a real (if currently
            // inert) virtual call rather than skipped, so a future override still gets invoked.
            CEquipTraverser Walk;
            CEquip* Item = EquipManager.Traverse(Walk);
            while (Item != nullptr)
            {
                Item->NotifyArchGroupDestroyed(Entry.SubObjId);
                Item = EquipManager.Traverse(Walk);
            }
        }
    }
}

void CEqObj::destroy_sub_objs(DamageList const& List, bool Param2)
{
    // Common.dll.c 143037-143062 (raw asm 0x062aba50-0x062aba74, not separately decompiled --
    // thin enough to trace directly from the listing). Forwards to the private
    // destroy_equipment() helper, then CArchGroupManager::DestroyGroups.
    destroy_equipment(List, Param2);
    ArchGroups.DestroyGroups(List, Param2);
}

// Common.dll.c/raw 0x4a190 (?connect@CEqObj@@UAEXPAUIObjDB@@@Z). A distinct override from
// the INotify* connect() below (different retail address/vtable slot).
void CEqObj::connect(IObjDB* ObjDb)
{
    CSimple::connect(ObjDb);
    // .text:062aa19d-062aa1b2: when +0x194 is set, tail-jump to FUN_062d2830(this+0xAC) on
    // the behavior manager.
    if (BehaviorMgr != nullptr)
        ConnectBehaviorManagerObjectDb(BehaviorMgr, ObjectDb);
}

// Common.dll.c/raw 0x4a1c0 (?disconnect@CEqObj@@UAEXPAUIObjDB@@@Z).
void CEqObj::disconnect(IObjDB* ObjDb)
{
    if (ObjectDb != nullptr && ObjectDb == ObjDb)
    {
        // .text:062aa1d7-062aa1e3: FUN_062d2830(0) on the behavior manager, when set.
        if (BehaviorMgr != nullptr)
            ConnectBehaviorManagerObjectDb(BehaviorMgr, nullptr);
        CSimple::disconnect(ObjDb);

        // Retail invalidates the first scanner's in-flight scan/cargo results once the
        // object database link is gone (raw disasm 062aa1f0-062aa23c): reset Scans.Count to
        // empty (capacity stays 256) and collapse the cargo-scan vector to empty without
        // freeing its storage.
        // FindFirst(0x80000) already yields only scanners; retail tests the result for null
        // (.text:062aa202) and uses it directly, with no type re-check.
        CEquip* Eq = const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeScanner));
        if (Eq != nullptr)
        {
            CEScanner* const Scanner = (CEScanner*)Eq;
            Scanner->Scans.reset();
            Scanner->CargoVecEnd = Scanner->CargoVecBegin;
        }
    }
}

// Common.dll.c 0x492a0
void CEqObj::connect(INotify* NotifyParam)
{
    Notify = NotifyParam;
}

void CEqObj::disconnect(INotify* NotifyParam)
{
    Notify = nullptr;
}

void CEqObj::notify(INotify::Event EventParam, void* Context)
{
    // Common.dll.c 0x492c0: `if (Notify) Notify->vtbl[0](EventParam, Context);` -- the same
    // INotify::notify vtable-slot-0 dispatch shape as CEquip::Notify (Common.dll.c 62500),
    // just with a single listener instead of two. Ghidra's own decompilation of this
    // particular call site couldn't recover the pushed arguments ("Could not recover
    // jumptable" / "Treating indirect jump as call", showing a bare `()`), but the mangled
    // signature (`?notify@CEqObj@@UAEXW4Event@INotify@@PAX@Z`) and every other INotify call
    // site agree on (Event, void*) through slot 0.
    if (Notify != nullptr)
        Notify->notify(EventParam, Context);
}
