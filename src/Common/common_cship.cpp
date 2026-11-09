// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CShip (retail CShip.cpp), FAnimScriptRef and the CShip-only helpers.
#include "common_cobject.h"
#include "common_cobject_internal.h" // CmnCobject::RotateWorldToObjectSpace, GetObjectDbCollisionGroup
#include "x86math.h"

#include "common_cobjdb.h"
#include "common_goods.h"
#include "common_physys.h"
#include "common_spatial.h" // ISpatialPartition, FSpatialQueryResult (CShip::update)
#include "common_net.h"
#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)
#include "common_iface.h"
#include "common_universe.h"

#include "DALib.h"

#include <cmath>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using CmnCobject::GetObjectDbCollisionGroup;
using CmnCobject::RotateWorldToObjectSpace;

namespace
{
    const char kCShipSource[] = "E:\\FL\\Scratch\\Source\\Common\\CShip.cpp";
}




CShip::CShip()
    : CEqObj(ClassShip)
    , PhySys::Controller()
{
    // .text:062b0670..062b09ad. CEqObj(0x503) and Controller(+0x1a4) run in the init list.
    // FormationLeader (+0x1b8), FollowLeader (+0x1e0) and Target (+0x220) are zeroed by their
    // default ctors (.text:062b06b3/b9, 062b06da/e0, 062b0724/2a); the three vectors start
    // empty (.text:062b06cf..062b06d7, 062b06f0..062b06f9, 062b0700..062b0712); BayAnim = -1.
    GroupId = 0;                      // 062b06ad
    FormationSpeed = 0.0f;            // 062b06c9
    bJumpedOut = 0;                   // 062b0718
    AtmosphereSolar = nullptr;        // 062b071e
    // Retail leaves +0x21c unwritten (no store between 062b071e and 062b0724); the port
    // zeroes it for a deterministic value.
    GroupNameId = 0;
    SubTargetIndex = SUBOBJ_ID_NONE;  // 062b0730..062b0736
    // 062b07f7..062b0803 / 062b0810..062b081c: FormationOffset and FollowOffset zeroed.
    FormationOffset.x = 0.0f;
    FormationOffset.y = 0.0f;
    FormationOffset.z = 0.0f;
    FollowOffset.x = 0.0f;
    FollowOffset.y = 0.0f;
    FollowOffset.z = 0.0f;
    Steering.SetOwner(this);          // 062b0822 (CNudgeEngine::SetOwner by ICF)
    NudgeEngine.SetOwner(this);       // 062b082e
    // 062b0833 `MOV [ESI+0x294],ESI`: StrafeEngine (+0x280) +0x14 = this, SetOwner inlined.
    // (An older note here claimed retail leaves the strafe engine's owner null; the store
    // is in the listing.)
    StrafeEngine.SetOwner(this);
    Throttle = 0.0f;                  // 062b0777..062b07a1: +0x29c..+0x2b8
    ThrustPower = 0.0f;
    MaxThrustPower = 0.0f;
    BayDoorCounter = 0;
    BayState = 0;
    TradelaneSpeed = 0.0f;
    bUsingTradelane = 0;              // 062b07a7
    bIsPlayer = 0;                    // 062b07ad
    AngularDragFactor = 1.0f;         // 062b07b3
    // GunStats (+0x2c8) is zeroed by its own ShipGunStats() member ctor, as retail's inlined
    // construction at .text:062b07c9-062b07d8.
    GunStatsDirty = 1;                // 062b07bd

    // 062b0839..062b0906 / 062b0909..062b0989: the two IObjRW-watcher vectors grow to 16 when
    // their capacity is below 16 (inline reserve: new 0x80, copy-construct, destroy, free);
    // 062b098c..062b0994: FUN_062b5100(16), the CSimple-watcher vector's reserve.
    FormationFollowers.reserve(16);
    FollowFollowers.reserve(16);
    Targeters.reserve(16);
}

FAnimScriptRef::~FAnimScriptRef()
{
    // FUN_0629abd0 (.text:0629abd0..0629abff); inlined in ~CShip at .text:062b0ada..062b0b0b.
    Release();
}

void FAnimScriptRef::Release()
{
    if (Index != -1)                                               // .text:0629abd5
    {
        DALib::Anim->StopPlayback(Index);                          // .text:0629abe5 slot 0x38
        DALib::Anim->DestroyScript((void*)Index); // .text:0629abf5 slot 0x28
        Index = -1;                                                // .text:0629abf8
    }
}

// Common.dll.c 46575 (retail 0x27bb0). Copy constructor -- confirmed via list_references
// to have zero callers anywhere in Common.dll, Server.dll, or freelancer.exe (only exported
// because CShip is a dllexport class), the same situation as CEqObj::CEqObj(CEqObj const&)
// above. Retail inlines CSimple's copy plus a manual field-by-field re-implementation of
// everything CEqObj::CEqObj(CEqObj const&) already does (EquipManager/Vibe*/Rank*/ArchGroups/
// BehaviorMgr/Power/etc.) and PhySys::Controller's own copy ctor (the this+0x1a4..0x1b4
// vtable-stamping dance at Common.dll.c 46625-46730) rather than delegating to them --
// confirmed by tracer notes on FUN_06288080 (item 722, ArchGroups' CArchGroupManager copy)
// and FUN_0628e9c0 (item 796, a std::vector<T> member copy), whose only two callers are
// CShip::CShip and CSolar::CSolar. Ported here by delegating to the already-ported
// CEqObj(CEqObj const&) and PhySys::Controller(Controller const&) copy ctors instead of
// re-inlining their bodies, then copying every CShip-own field below in declaration order.
//
// CShip-own part, .text:06287d4b..06288051: FollowLeader (06287e14..06287e41) and Target
// (06287f24..06287f59) are watchers re-watching the other ship's owner; the three vectors are
// copied exact-size (operator_new(n * 8), then FUN_0628ef80 / FUN_0628efc0 copy-construct
// every element with FUN_062904d0 / FUN_06290530 -- the TWatcher copy ctor), which is what
// std::vector's copy ctor does. Everything else is a plain copy, +0x22c included.
CShip::CShip(CShip const& Other)
    : CEqObj(Other)
    , PhySys::Controller(Other)
    // Sub-objects retail copy-constructs directly (Common.dll.c 46707-46709): use their
    // own copy ctors in the init list rather than default-constructing then overwriting.
    // CSteering/CNudgeEngine/CStrafeEngine's copy ctors already replicate retail's field
    // set exactly, including NOT retargeting OwnerShip to `this` (matching
    // CSteering::CSteering(this+0x230, Other+0x230) etc. verbatim -- see those classes'
    // own copy-ctor comments in common_csteering.cpp). ActionDB has no user copy ctor, so its
    // implicit one is a plain Head/Tail copy, matching retail's raw two-dword copy exactly.
    //
    // FormationLeader, .text:06287d57..06287d86: both words zeroed, then the other ship's
    // watched pointer W: W != 0 and W - 8 != 0 -> set_pointer(W). That is
    // BaseWatcher(IObjRW* Leader) with Leader = W - 8 (null when W is 0 or 8).
    , FormationLeader(Other.FormationLeader)         // TWatcher copy: TWatcher(Other.get())
    , FormationFollowers(Other.FormationFollowers)   // 06287db3..06287e11
    , FollowLeader(Other.FollowLeader)               // 06287e14..06287e41
    , FollowFollowers(Other.FollowFollowers)         // 06287e62..06287eae
    , Targeters(Other.Targeters)                     // 06287eb1..06287efd
    , Target(Other.Target)                           // 06287f24..06287f59
    , BayAnim(Other.BayAnim)                         // 06287f72..06287f7f
    , Steering(Other.Steering)
    , NudgeEngine(Other.NudgeEngine)
    , StrafeEngine(Other.StrafeEngine)
    , Actions(Other.Actions)
{
    GroupId = Other.GroupId; // 0x06287d4b / 0x06287d51
    FormationOffset = Other.FormationOffset;          // 06287d8b..06287da4
    FormationSpeed = Other.FormationSpeed;            // 06287da7..06287dad
    FollowOffset = Other.FollowOffset;                // 06287e46..06287e5f
    bJumpedOut = Other.bJumpedOut;                    // 06287f00..06287f06
    AtmosphereSolar = Other.AtmosphereSolar;          // 06287f0c..06287f12
    GroupNameId = Other.GroupNameId;                  // 06287f18..06287f1e
    SubTargetIndex = Other.SubTargetIndex;            // 06287f5e..06287f6b

    Throttle = Other.Throttle;
    ThrustPower = Other.ThrustPower;
    MaxThrustPower = Other.MaxThrustPower;
    BayDoorCounter = Other.BayDoorCounter;
    BayState = Other.BayState;
    TradelaneSpeed = Other.TradelaneSpeed;
    bUsingTradelane = Other.bUsingTradelane;
    bIsPlayer = Other.bIsPlayer;
    AngularDragFactor = Other.AngularDragFactor;
    GunStatsDirty = Other.GunStatsDirty;
    GunStats = Other.GunStats;
}

CShip::~CShip()
{
    // .text:062b09b0..062b0c33. The body is Actions.free() (062b09f2, ActionDB::free shares
    // ~ActionDB's address) and CShip::flush_animations inlined (062b09f7..062b0a3e; called
    // by name here, so no virtual dispatch). Everything after is member destruction in
    // reverse declaration order, which the compiler generates: ~ActionDB (062b0a4a), the
    // strafe / nudge / steering controllers (062b0a4f..062b0ad5), BayAnim's release
    // (062b0ada..062b0b0b), Target (062b0b11), Targeters (062b0b2c..062b0b68), FollowFollowers
    // (062b0b6e..062b0b9d), FollowLeader (062b0ba3..062b0bb2), FormationFollowers
    // (062b0bb7..062b0bec), FormationLeader (062b0bef..062b0bfe), ~Controller (062b0c0d) and
    // ~CEqObj (062b0c1c).
    Actions.free();
    CShip::flush_animations();
}

void CShip::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // .text:062b2850..062b2a10. Parms: PhySys default linear / angular damping kept (unlike
    // CGuided), Type 2, +0x1c = 0, bEnableCollisions written 1 then 0 (062b28fe), Name =
    // _itoa(Id +0xb0, radix 24), mass = archetype Mass (+0x20, no null test), system id from
    // IObjDB; archetype byte +0x38 set -> bHasInertia 1 and Inertia = archetype +0x2c..+0x34,
    // else +0x26 / +0x28..+0x30 stay 0. The bytes retail leaves unwritten are zero here.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Phy.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Phy.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Phy.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Phy.Type = 2;
    _itoa((int)Id, Phy.Name, 0x18);
    Phy.Mass = Archetype->Mass;
    Phy.CollisionGroupId = GetObjectDbCollisionGroup(ObjectDb);
    Phy.bEnableCollisions = 0;
    if (Archetype->bHasRotationInertia)
    {
        Phy.bHasInertia = 1;
        Phy.Inertia.x = Archetype->RotationInertiaX;
        Phy.Inertia.y = Archetype->RotationInertiaY;
        Phy.Inertia.z = Archetype->RotationInertiaZ;
    }

    PhySys::BuildIVP_Real(this, Phy);

    // 062b292e / 062b29e2: no body (+0x54) -> FDUMP the CShip.cpp:0x5c7 error with the
    // archetype's DaArchetype (+0x0c) and return without touching velocity.
    if (BodyPtr == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: CShip::init_physics(): Failed to create ivp_real for %s. Expect a crash.",
              "E:\\FL\\Scratch\\Source\\Common\\CShip.cpp", 0x5c7, Archetype->DaArchetype.Value);
        return;
    }

    // 062b293c..062b29d2: AddToVelocity, then the world angular velocity through the
    // transposed orientation (TransformMatrix) into AddToAngularVelocityOS.
    PhySys::AddToVelocity(this, Velocity);
    PhySys::AddToAngularVelocityOS(this, RotateWorldToObjectSpace(Orientation, AngularVelocity));
}

void CShip::init(CreateParms const& Parms)
{
    // .text:062b2690..062b2843.
    CEqObj::init(Parms);                                              // 062b269e

    GroupNameId = Parms.GroupNameId;                                  // 062b26a3 (+0xec)
    bUsingTradelane = Parms.bUsingTradelane;                          // 062b26b5 (+0xf4)

    // 062b26bb..062b26e6: the base this ship sits in (Universe::get_base of +0x160, which
    // CEqObj::init has just filled) learns the current system: IObjDB slot 0x08
    // (GetZoneDb, the system) -> ISystem slot 0x00 (its id) -> IBase slot 0x58 (set system).
    Universe::IBase* const Base = Universe::get_base(DockingBaseId);
    if (Base != nullptr)
    {
        Universe::ISystem const* const System = ObjectDb->GetZoneDb();
        Base->SetSystem(System->GetId());
    }

    enable_controllers();                                             // 062b26ed vtable +0x60

    // 062b26f0..062b271b: the bay door animation, when the archetype names one (+0xdc; no
    // null test on the string, only on its first byte): Anim slot 0x24 PlayScript(arch
    // script +0x44, EngineInstanceId, name, 0, 0) -> BayAnim. No null test on Anim.
    Archetype::Ship const* const ShipArch = shiparch();
    if (ShipArch->BayDoorAnim.Value[0] != '\0')
    {
        unsigned long const PlayId = DALib::Anim->PlayScript(AsOpaqueDword(Archetype->AnimIndex),
                                                             AsOpaqueDword(EngineInstanceId),
                                                             ShipArch->BayDoorAnim.Value,
                                                             nullptr, nullptr);
        BayAnim.Index = (int)PlayId;
    }

    // 062b2721..062b277a: every gun (traverser 0x80, walk ends at the first entry
    // CEGun::cast rejects) whose projectile archetype (+0x4c) does not require ammo (+0x78)
    // is activated (vtable +0x30, true). No null test on the projectile.
    CEquipTraverser GunWalk(EquipTypeGun);
    for (CEGun* Gun = CEGun::cast(EquipManager.Traverse(GunWalk)); Gun != nullptr;
         Gun = CEGun::cast(EquipManager.Traverse(GunWalk)))
    {
        Archetype::Projectile const* const Proj =
            (Archetype::Projectile const*)Gun->Projectile;
        if (!Proj->bRequiresAmmo)
            Gun->Activate(true);
    }

    // 062b277c..062b27d5: every power plant (traverser 0x40000) adds 0 when destroyed
    // (CEquip slot 2) else its archetype's thrust capacity (+0x78) to +0x2a4, one FSTP dword
    // per add. The ctor zeroed +0x2a4; this does not reset it.
    CEquipTraverser PowerWalk(EquipTypePower);
    for (CEquip* Power = EquipManager.Traverse(PowerWalk); Power != nullptr;
         Power = EquipManager.Traverse(PowerWalk))
    {
        float const Capacity = Power->IsDestroyed()
            ? 0.0f : ((Archetype::Power const*)Power->Arch)->ThrustCapacity;
        MaxThrustPower = (float)((double)Capacity + MaxThrustPower);
    }
    ThrustPower = MaxThrustPower;                                     // 062b27db..062b27e9

    Steering.Activate(true);                                          // 062b27ef (CNudgeEngine::Activate by ICF)
    NudgeEngine.Activate(true);                                       // 062b27fc

    // 062b2801..062b2810: +0x160 = Parms +0x5c, then init_docking_points with it.
    DockingBaseId = Parms.DockingCount;
    init_docking_points(Parms.DockingCount);

    Reputation::Vibe::SetRank(VibeId, Parms.Rank);                    // 062b2823 (+0xf0)

    bIsPlayer = (OwnerPlayer != 0) ? 1 : 0;                           // 062b2828..062b2837
}

CEquip* CShip::alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch,
                           bool bMissionFlag)
{
    // .text:062b0c40..062b0d0e: Arch (no null test) get_class_type (vtable +0x10): 0xc engine
    // (operator new 0x58), 0xf thruster (0x64), 0x16 tractor (0x60); anything else -- or a
    // null allocation, which modern new never returns -- goes to CEqObj::alloc_equip.
    int const ClassType = (int)Arch->get_class_type();
    if (ClassType == Archetype::AClassEngine)
        return new CEEngine(this, EquipId, (Archetype::Engine const*)Arch, bMissionFlag);
    if (ClassType == Archetype::AClassThruster)
        return new CEThruster(this, EquipId, (Archetype::Thruster const*)Arch, bMissionFlag);
    if (ClassType == Archetype::AClassTractor)
        return new CETractor(this, EquipId, (Archetype::Tractor const*)Arch, bMissionFlag);
    return CEqObj::alloc_equip(EquipId, Arch, bMissionFlag);
}

bool CShip::is_cruise_active() const
{
    CEquip* Equip = const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine));
    CEEngine* Engine = CEEngine::cast(Equip);
    if (Engine == nullptr)
        return false;
    return Engine->bCruiseActive != 0;
}

bool CShip::is_using_tradelane() const
{
    return bUsingTradelane != 0;
}


unsigned CShip::get_name() const
{
    // .text:062b5f70 (shared with CSimple via ICF): MOV EAX,[this+0x88]; MOV EAX,[EAX+0x14]
    // -- Archetype->IdsName, no null test.
    return (unsigned)Archetype->IdsName;
}

bool CShip::any_thrusters_on() const
{
    // Listing 0x51660: traverse thrusters (0x400), return true if any IsActive()
    CEquipTraverser Walk(EquipTypeThruster);
    CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->IsActive())
            return true;
        Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    }
    return false;
}

bool CShip::get_tgt_lead_fire_pos(Vector& OutPos) const
{
    // .text:062b1480..062b14a8: W = [+0x220]; W != 0 -> forward W - 8, else forward 0. No
    // second null test on W - 8 here, unlike most readers -- get() gives the same value.
    IObjRW* const TargetObj = Target.get();
    if (TargetObj != nullptr)
    {
        IObjInspect const* const TargetInspect = TargetObj;
        return get_tgt_lead_fire_pos(TargetInspect, OutPos);
    }
    return get_tgt_lead_fire_pos((IObjInspect const*)nullptr, OutPos);
}

unsigned short CShip::get_sub_target() const
{
    // Listing 0x52300: MOV AX, [this+0x228]
    return SubTargetIndex;
}

IObjRW* CShip::get_target() const
{
    // .text:062b23c0..062b23d0: W = [+0x220]; W != 0 -> W - 8, else 0.
    return Target.get();
}

void CShip::set_target(IObjRW* NewTarget)
{
    // .text:062b23e0..062b2490.
    if (Target.get() == NewTarget)                                    // 062b23e4..062b2402
        return;

    // 062b2408..062b2444: the old target, when it is a ship, forgets this ship as targeter.
    IObjRW* const OldTarget = Target.get();
    if (OldTarget != nullptr)
    {
        CObject* const OldObj = const_cast<CObject*>(OldTarget->cobject()); // slot +0x150
        if (OldObj != nullptr && (OldObj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            ((CShip*)OldObj)->notify_set_target(this, false);
    }

    // 062b2449..062b2471: the new target, when it is a ship, learns this ship as targeter.
    if (NewTarget != nullptr)
    {
        CObject* const NewObj = const_cast<CObject*>(NewTarget->cobject());
        if (NewObj != nullptr && (NewObj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            ((CShip*)NewObj)->notify_set_target(this, true);
    }

    Target.watch(NewTarget);                                          // 062b2476..062b2488
}

Vector CShip::get_nudge_vec() const
{
    // Listing 0x51700: copies Vector from NudgeEngine (+0x258) + 0x1c = +0x274
    return NudgeEngine.GetNudgeVec();
}

StrafeDir CShip::get_strafe_dir() const
{
    // Listing 0x51740: reads dword from StrafeEngine (+0x280) + 0x18 = +0x298
    return (StrafeDir)StrafeEngine.GetStrafe();
}

ShipGunStats const& CShip::get_gun_stats() const
{
    // Listing 0x514f0: checks GunStatsDirty at +0x2c4; if set, re-traverses EquipTypeGun
    // (0x80) equips and rebuilds the cache, then clears the flag.
    if (GunStatsDirty != 0)
    {
        GunStatsDirty = 0;
        GunStats.ActiveGunCount = 0;
        GunStats.AvgMuzzleVelocity = 0.0f;
        GunStats.MaxMunitionRange = 0.0f;
        GunStats.CmDropperCount = 0;
        GunStats.CmMaxMunitionRange = 0.0f;

        bool const bPlayerShip = bIsPlayer != 0; // 062b152c..062b153b: snapshot before traversal.
        CEquipTraverser Walk(EquipTypeGun);
        CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
        while (Equip != nullptr)
        {
            // vcall+0xc == IsFunctioning(). Non-player ships (AI) skip their own
            // auto-turret guns here; player ships and non-turret guns always count.
            if (Equip->IsFunctioning())
            {
                Archetype::Gun const* GunArch = (Archetype::Gun const*)Equip->EquipArch();
                if (bPlayerShip || !GunArch->bAutoTurret)
                {
                    CEGun* const Gun = (CEGun*)Equip;
                    // 062b1586 calls 062969c0 (MOV EAX,[ECX+0x4c]; RET): the munition,
                    // unlike 0629fb90 above, which returns the gun archetype at +0x0c.
                    // 062b158b tests Munition::Seeker (+0x90), not Launcher::RefireDelay.
                    if (Gun->MunitionArch()->Seeker == 0)
                    {
                        ++GunStats.ActiveGunCount;
                        float const Range = Gun->GetMunitionRange();
                        if (GunStats.MaxMunitionRange < Range)
                            GunStats.MaxMunitionRange = Range;
                        GunStats.AvgMuzzleVelocity += GunArch->MuzzleVelocity;
                    }
                    else
                    {
                        ++GunStats.CmDropperCount;
                        float const Range = Gun->GetMunitionRange();
                        if (GunStats.CmMaxMunitionRange < Range)
                            GunStats.CmMaxMunitionRange = Range;
                    }
                }
            }
            Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
        }

        if (GunStats.ActiveGunCount != 0)
            GunStats.AvgMuzzleVelocity /= (float)(GunStats.ActiveGunCount);
        else
            GunStats.AvgMuzzleVelocity = 0.0f;
    }

    return GunStats;
}

bool CShip::get_farthest_active_gun_range(float& OutRange) const
{
    // Listing 0x514b0: calls get_gun_stats; prefers the active-gun range, falls back to
    // the CM-type range when there are no active guns, else reports no range.
    ShipGunStats const& Stats = get_gun_stats();
    if (Stats.ActiveGunCount != 0)
    {
        OutRange = Stats.MaxMunitionRange;
        return true;
    }
    if (Stats.CmDropperCount != 0)
    {
        OutRange = Stats.CmMaxMunitionRange;
        return true;
    }
    OutRange = 0.0f;
    return false;
}

double CShip::get_cargo_hold_remaining() const
{
    // .text:062b3040..062b30e7. used = 0.0f; every equip of mask 0x1ff0000: Type (+0x14) ==
    // 0x10000 (cargo) -> used = float(FILD qword {count +0x28, 0} * Arch->Volume + used), else
    // used = float(used + Arch->Volume). FLD HoldSize (shiparch +0x100, no null test); FLD
    // used; FCOMP; TEST AH,0x41 / JP taken (used > hold, or unordered) -> 0.0, otherwise
    // hold - used, returned unrounded in ST0 (062b30d4 FSUB; RET).
    float Used = 0.0f;
    CEquipTraverser Walk(0x1ff0000);
    for (CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk); Equip != nullptr;
         Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk))
    {
        if (Equip->Type == EquipTypeCargo)
            Used = (float)((double)(((CECargo const*)Equip)->Count) *
                                      Equip->Arch->Volume + Used);
        else
            Used = (float)((double)(Used) + Equip->Arch->Volume);
    }

    float const Hold = shiparch()->HoldSize;
    if (Used <= Hold)
        return (double)Hold - Used;
    return 0.0f;
}

int CShip::get_repair_kit_capacity_remaining() const
{
    // .text:062b30f0..062b3172: Limit = shiparch(+0x88)->NanobotLimit (+0x140, no null test);
    // -1 -> -1. Walk the cargo (traverser mask 0x10000); the FIRST item whose Arch(+0xc)->
    // get_class_type() (+0x10) is 0x1a (repair kit) answers: its count [+0x28] > Limit
    // (signed JG) -> 0, else Limit - count. No such item -> Limit.
    Archetype::Ship const* const ShipArch = shiparch();
    int const Limit = ShipArch->NanobotLimit;
    if (Limit == -1)
        return Limit;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->Arch->get_class_type() == Archetype::AClassRepairKit)
        {
            int const Count = (int)((CECargo*)Equip)->Count;
            if (Count > Limit)
                return 0;
            return Limit - Count;
        }
        Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    }
    return Limit;
}

int CShip::get_shield_battery_capacity_remaining() const
{
    // .text:062b3180..062b3202: same shape as get_repair_kit_capacity_remaining with
    // ShieldBatteryLimit (+0x144) and class 0x1b (shield battery).
    Archetype::Ship const* const ShipArch = shiparch();
    int const Limit = ShipArch->ShieldBatteryLimit;
    if (Limit == -1)
        return Limit;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->Arch->get_class_type() == Archetype::AClassShieldBattery)
        {
            int const Count = (int)((CECargo*)Equip)->Count;
            if (Count > Limit)
                return 0;
            return Limit - Count;
        }
        Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    }
    return Limit;
}

int CShip::get_ammo_capacity_remaining(Archetype::Equipment const* Arch) const
{
    // Common.dll.c 0x53210: thin overload, forwards *(Arch+8) i.e. Arch->get_id() (Nickname.Id).
    return get_ammo_capacity_remaining(Arch->get_id());
}

int CShip::get_ammo_capacity_remaining(unsigned EquipArchId) const
{
    // .text:062b3220..062b32d8. Limit = the global MAX_PLAYER_AMMO (DAT_063ed1d4, read from
    // the constants INI by 06304c71 -- not a literal 50). Id 0 -> -1. Walk the cargo (mask
    // 0x10000); the FIRST item whose Arch(+0xc)->Nickname id (+0x8) equals the id answers:
    // class (vtable +0x10) not 0x1f/0x20/0x21 -> -1; count (+0x28) > Limit (signed JG) -> 0,
    // else Limit - count. No such item -> Limit.
    int const Limit = MAX_PLAYER_AMMO;
    if (EquipArchId == 0)
        return -1;

    CEquipTraverser Walk(EquipTypeCargo);
    for (CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk); Equip != nullptr;
         Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk))
    {
        if (Equip->Arch->Nickname.Id != EquipArchId)
            continue;

        Archetype::AClassType const ClassType = Equip->Arch->get_class_type();
        if (ClassType != Archetype::AClassMunition &&
            ClassType != Archetype::AClassMine &&
            ClassType != Archetype::AClassCounterMeasure)
            return -1;

        int const Count = (int)((CECargo const*)Equip)->Count;
        if (Count > Limit)
            return 0;
        return Limit - Count;
    }
    return Limit;
}

int CShip::get_space_for_cargo_type(Archetype::Equipment const* CargoArch) const
{
    // .text:062b32e0..062b360c (no null test on CargoArch or the archetype).
    // 062b32e8..062b3307: Volume (+0x5c) FST dword; result starts at 0x7fffffff; FCOMP 0.0 /
    // TEST AH,0x41 / JNZ skips the volume step when Volume <= 0 or unordered -- the class
    // switch below still runs.
    int SpaceForType = 0x7fffffff;
    float const ItemVolume = CargoArch->Volume;
    if (ItemVolume > 0.0f)
    {
        // 062b330d..062b3380: used = 0.0f; every equip of mask 0x1ff0000: Type (+0x14) ==
        // 0x10000 (cargo) -> used = float(FILD qword {count +0x28, 0} * Arch->Volume + used),
        // else used = float(used + Arch->Volume).
        float Used = 0.0f;
        CEquipTraverser Walk(0x1ff0000);
        for (CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk); Equip != nullptr;
             Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk))
        {
            if (Equip->Type == EquipTypeCargo)
                Used = (float)((double)(((CECargo const*)Equip)->Count) *
                                          Equip->Arch->Volume + Used);
            else
                Used = (float)((double)(Used) + Equip->Arch->Volume);
        }

        // 062b3382..062b33c1: FLD HoldSize (+0x100); FLD used; FCOMP; TEST AH,0x41 / JP taken
        // (neither or both of C0/C3: used > hold, or unordered) -> 0.0, otherwise hold - used
        // (unrounded); FDIV Volume (dword), FSTP qword, floor, ftol.
        double const Hold = shiparch()->HoldSize;
        double const Remaining = (Used <= Hold) ? (Hold - Used) : 0.0;
        SpaceForType = (int)std::floor(Remaining / ItemVolume);
    }

    // 062b33c5..062b33d8: switch on CargoArch->get_class_type() - 0x1a over 0..7 (table
    // 062b3610): 0x1a repair kit and 0x1b shield battery take the smaller of the result and
    // the inlined get_*_capacity_remaining (retail recomputes it for the return when it is
    // smaller); 0x1f..0x21 (munition, mine, countermeasure) the smaller of the result and
    // get_ammo_capacity_remaining(Nickname id +0x08) (called a second time for the return);
    // everything else returns the result.
    switch (CargoArch->get_class_type())
    {
    case Archetype::AClassRepairKit:
    {
        int const KitRemaining = get_repair_kit_capacity_remaining();
        if (SpaceForType < KitRemaining)
            return SpaceForType;
        return get_repair_kit_capacity_remaining();
    }
    case Archetype::AClassShieldBattery:
    {
        int const BatteryRemaining = get_shield_battery_capacity_remaining();
        if (SpaceForType < BatteryRemaining)
            return SpaceForType;
        return get_shield_battery_capacity_remaining();
    }
    case Archetype::AClassMunition:
    case Archetype::AClassMine:
    case Archetype::AClassCounterMeasure:
        if (SpaceForType >= get_ammo_capacity_remaining(CargoArch->Nickname.Id))
            SpaceForType = get_ammo_capacity_remaining(CargoArch->Nickname.Id);
        return SpaceForType;
    default:
        return SpaceForType;
    }
}

wchar_t const* CShip::get_pilot_name() const
{
    // Ghidra @ 0x53630: reputation vibe name lookup. Retail discards GetName's int
    // return value and always hands back whatever local_40 (DisplayNameOut) ended up
    // as -- nullptr when the vibe lookup fails, never an empty-string fallback.
    wchar_t const* WideName = nullptr;
    FmtStr Fmt1;
    FmtStr Fmt2;
    Reputation::Vibe::GetName(this->VibeId, Fmt1, Fmt2, WideName);
    return WideName;
}

bool CShip::is_launching() const
{
    // Ghidra @ 0x51A00: ActionDB lookup for "Launch"
    return Actions.find("Launch") != nullptr;
}

bool CShip::is_jumping() const
{
    // Ghidra @ 0x51A20: ActionDB lookup for "JumpIn"
    return Actions.find("JumpIn") != nullptr;
}

bool CShip::is_enemy(IObjInspect* OtherInspect)
{
    // 062b1b76..062b1b95: cobject() (+0x150), then narrow to CEqObj.
    // Caller 00544a70..00544a8e rejects null scanner entries before this call.
    CObject const* const Delegate = OtherInspect->cobject();
    if (Delegate == nullptr)
        return false;
    if ((Delegate->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return false;

    // 062b1b97..062b1bba: own vibe is copied to the argument slot, the other's
    // to the first local. The pushes pass OTHER -> THIS, not THIS -> OTHER.
    // Reputation is directional: the contact must be hostile towards this ship.
    int const ThisVibeId = VibeId;
    int const OtherVibeId = ((CEqObj const*)Delegate)->VibeId;
    float Attitude;
    int const Result = Reputation::Vibe::AttitudeTowards(OtherVibeId, ThisVibeId, Attitude);
    if (Result != 0)
        return false;

    // 062b1bca..062b1bd5: FCOMP -0.6f (BF19999A), TEST AH,0x41 + JP
    // rejects greater/unordered; less/equal falls through to true.
    return Attitude <= -0.6f;
}

unsigned CShip::get_group_name() const
{
    // Listing 0x52640: if GroupNameId != 0 return it, else call Reputation::Vibe::GetAffiliation and Reputation::get_name
    if (GroupNameId != 0)
        return GroupNameId;
    unsigned Affiliation = 0;
    int Result = Reputation::Vibe::GetAffiliation(VibeId, Affiliation, false);
    if (Result == 0)
        return Reputation::get_name(Affiliation);
    return 0;
}

int CShip::get_targeted_enemy_count()
{
    // .text:062b24a0..062b24cc: count the Targeters slots whose CSimple (W - 0x9c) is live.
    int Count = 0;
    for (std::vector<FSimpleWatcher>::const_iterator It = Targeters.begin(); It != Targeters.end(); ++It)
    {
        if (It->get() != nullptr)
            ++Count;
    }
    return Count;
}

IObjRW* CShip::get_follow_leader()
{
    // .text:062b4ee0..062b4ef0: W = [+0x1e0]; W != 0 -> W - 8, else 0.
    return FollowLeader.get();
}

int CShip::get_formation_follower_count()
{
    // .text:062b4a90..062b4ab9: count the FormationFollowers slots whose owner is live.
    int Count = 0;
    for (std::vector<FObjRWWatcher>::const_iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        if (It->get() != nullptr)
            ++Count;
    }
    return Count;
}

int CShip::get_formation_followers(IObjRW** const OutFollowers, unsigned int MaxCount)
{
    // .text:062b4ac0..062b4b05: every live follower is counted; it is stored only while the
    // count is below MaxCount (062b4aed CMP EAX,EBX / JNC skips the store). No null test
    // on OutFollowers.
    unsigned Count = 0;
    for (std::vector<FObjRWWatcher>::const_iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower != nullptr)
        {
            if (Count < MaxCount)
                OutFollowers[Count] = Follower;
            ++Count;
        }
    }
    return (int)Count;
}

FORMATION_RTYPE CShip::add_formation_follower(IObjRW* Follower)
{
    // Common.dll.c 0x54810 (86302-86382). BUG FIX: retail only succeeds when
    // FormationLeader already resolves to `this` (self-leader) -- if FormationLeader
    // is unset (nullptr) or the tombstone sentinel (void*)8, or resolves to some OTHER
    // ship's Id, retail falls straight through to `return FORMATION_RTYPE_NOT_LEADER`
    // without touching the follower list at all. The previous port instead treated a
    // null/never-established FormationLeader as an implicit pass, letting any ship add
    // followers before ever being made a leader.
    // .text:062b482b..062b4860: W = [this+0x1b8] (the leader watcher); W == 0 or W - 8 == 0
    // -> not a leader; else the leader's IObjRW (W - 8) vt+0x20 get_id() against this+0xb0.
    // (An earlier revision read `Id` off W - 8 as if it were a CSimple -- it is the leader's
    // IObjRW, so the call goes through get_id.)
    IObjRW* const Leader = FormationLeader.get();
    if (Leader == nullptr)
    {
        return FORMATION_RTYPE_NOT_LEADER;
    }
    if (Leader->get_id() != Id)
    {
        return FORMATION_RTYPE_NOT_LEADER;
    }

    // 062b4866..062b48b2: walk the slots. The first cleared slot takes the follower
    // (062b4955..062b4961: `Follower + 8 != [slot] -> set_pointer`); a live slot with the same
    // get_id() means the follower is already there. Either way, on to the speed refresh.
    // A null Follower skips the walk (062b486f) and is appended as an empty watcher.
    bool bPlaced = false;
    if (Follower != nullptr)
    {
        for (std::vector<FObjRWWatcher>::iterator It = FormationFollowers.begin();
             It != FormationFollowers.end(); ++It)
        {
            IObjRW* const Existing = It->get();
            if (Existing == nullptr)
            {
                It->watch(Follower);
                bPlaced = true;
                break;
            }
            if (Existing->get_id() == Follower->get_id())
            {
                bPlaced = true;
                break;
            }
        }
    }

    if (!bPlaced)
    {
        // 062b48b4..062b4906: a temporary watcher on Follower, appended at _Last through
        // FUN_062b5200 (vector::_Insert_n(_Last, 1, Temp)), then destroyed.
        FObjRWWatcher const Temp(Follower);
        FormationFollowers.push_back(Temp);
    }

    // 062b4910..062b4979: recalculate_formation_speed's body, inlined.
    recalculate_formation_speed();
    return FORMATION_RTYPE_OK;
}

FORMATION_RTYPE CShip::add_follow_follower(IObjRW* Follower)
{
    // .text:062b4d10..062b4e16. Follower is dereferenced without a null test (062b4d2d).
    if (Follower->get_id() == Id)                                     // 062b4d34..062b4d3d
        return FORMATION_RTYPE_SELF;

    // 062b4d55..062b4d99: the first cleared slot takes the follower (062b4e0a..062b4e16);
    // a live slot with the same get_id() means it is already there.
    for (std::vector<FObjRWWatcher>::iterator It = FollowFollowers.begin();
         It != FollowFollowers.end(); ++It)
    {
        IObjRW* const Existing = It->get();
        if (Existing == nullptr)
        {
            It->watch(Follower);
            return FORMATION_RTYPE_OK;
        }
        if (Existing->get_id() == Follower->get_id())
            return FORMATION_RTYPE_OK;
    }

    // 062b4d9b..062b4de7: a temporary watcher on Follower + 8 (tested for 0 only after the
    // +8), appended through FUN_062b5200, then destroyed.
    FObjRWWatcher const Temp(Follower);
    FollowFollowers.push_back(Temp);
    return FORMATION_RTYPE_OK;
}

bool CShip::generate_follow_offset(IObjInspect const* Inspect, Transform& OutOffset) const
{
    // .text:062b4f10..062b50fd.
    if (Inspect == nullptr)                                           // 062b4f18 / 062b4f27
        return false;

    // 062b4f2d: get_id() is called once up front with the result dropped, then again per slot.
    (void)Inspect->get_id();

    // 062b4f30..062b4f73: Index counts every slot, cleared ones included, from 1.
    int Index = 1;
    std::vector<FObjRWWatcher>::const_iterator It = FollowFollowers.begin();
    for (; It != FollowFollowers.end(); ++It)
    {
        IObjRW* const Existing = It->get();
        if (Existing != nullptr && Existing->get_id() == Inspect->get_id())
            break;
        ++Index;
    }
    if (It == FollowFollowers.end())
        return false;                                                 // 062b4f75

    // 062b4f81..062b4fc9: Slot = Index - 1; Ring = Slot / 4 + 1 and Quad = Slot % 4, both
    // signed C division (CDQ / AND 3 / SAR 2; AND 0x80000003 with the JNS fix-up).
    int const Slot = Index - 1;
    int const Ring = Slot / 4 + 1;
    int const Quad = Slot % 4;

    // 062b4f9c..062b4fbe: FILD Ring, each product rounded once by FSTP dword.
    Vector Offset;
    Offset.x = (float)((double)(50.0f) * Ring);  // DAT_0639e6d4 = 50.0f
    Offset.y = (float)((double)(0.0f) * Ring);   // DAT_06399408 = 0.0f
    Offset.z = (float)(Ring * (double)(20.0f));  // DAT_0639b21c = 20.0f

    // 062b4fcd..062b502a: A = Quad * pi(float, DAT_0639d208) * 0.5(double, DAT_0639df88) kept
    // on the FPU; FST dword stores A rounded, FCOS runs on the unrounded A, FSIN on the
    // reloaded rounded one. Rows {cos, -sin, 0}, {sin, cos, 0}, {0, 0, 1}.
    double const Angle = (double)(Quad) * (double)(3.1415927f) * 0.5;
    float const AngleRounded = (float)(Angle);
    float const Cos = static_cast<float>(std::cos(static_cast<double>(Angle)));
    float const Sin = static_cast<float>(std::sin(static_cast<double>(AngleRounded)));
    Matrix Spin;
    Spin.E[0][0] = Cos;
    Spin.E[0][1] = -Sin;
    Spin.E[0][2] = 0.0f;
    Spin.E[1][0] = Sin;
    Spin.E[1][1] = Cos;
    Spin.E[1][2] = 0.0f;
    Spin.E[2][0] = 0.0f;
    Spin.E[2][1] = 0.0f;
    Spin.E[2][2] = 1.0f;

    // 062b503a..062b506c: I3DMathEngine slot 0x2c TransformMatrix(Rotated, Spin, Offset), the
    // result stored into OutOffset.Translation.
    Vector Rotated;
    Acquire3DMathEngine()->TransformMatrix((FVector&)Rotated, (FMatrix const&)Spin, (FVector const&)Offset);
    OutOffset.Translation = Rotated;

    // 062b506f..062b507b: this->vtable+0x64 = get_physical_radius_r(Center); the radius
    // left on the FPU is popped unused.
    Vector Center;
    (void)get_physical_radius_r(Center);

    // 062b5082..062b50ac: the ship's own orientation (+0x08, 9 dwords) copied, then
    // TransformMatrix(Rotated, Orientation, OutOffset.Translation).
    Matrix const Orient = Orientation;
    Acquire3DMathEngine()->TransformMatrix((FVector&)Rotated, (FMatrix const&)Orient,
                                           (FVector const&)OutOffset.Translation);

    // 062b50af..062b50f6: Translation = Rotated + Center, each sum rounded by FSTP dword;
    // Rotation = the orientation copy.
    OutOffset.Translation.x = (float)((double)(Rotated.x) + Center.x);
    OutOffset.Translation.y = (float)((double)(Rotated.y) + Center.y);
    OutOffset.Translation.z = (float)((double)(Rotated.z) + Center.z);
    OutOffset.Rotation = Orient;
    return true;
}

unsigned CShip::get_tradelane_entrance_ring()
{
    // .text:062b1810..062b1832: find("LaneAction"); null, or IAction == 0x10 (a null
    // LaneAction after -0x10), -> 0; else tail jump to FUN_062c2320: EntranceRing (+0x20)
    // null -> 0, else its get_id() (IObjInspect slot 8, +0x20).
    IAction* const Action = Actions.find("LaneAction");
    if (Action == nullptr || Action == (IAction*)0x10)
        return 0;
    LaneAction const* const Lane = static_cast<LaneAction const*>(Action);
    if (Lane->EntranceRing == nullptr)
        return 0;
    return Lane->EntranceRing->get_id();
}

unsigned CShip::get_tradelane_exit_ring()
{
    // .text:062b1840..062b1862: find("LaneAction"); null, or IAction == 0x10 (a null
    // LaneAction after -0x10), -> 0; else tail jump to FUN_062c2330, which reads
    // LaneAction +0x28 -- EntranceRingXform.Ring, NOT ExitRing (+0x24) -- and returns its
    // get_id() (IObjInspect slot 8, +0x20), or 0 when it is null. BuildRingTransform stores
    // the entrance ring there, so retail answers the ENTRANCE ring's id here (very strange:
    // most likely +0x28 typed for +0x5c ExitRingXform.Ring); reproduced as retail.
    IAction* const Action = Actions.find("LaneAction");
    if (Action == nullptr || Action == (IAction*)0x10)
        return 0;
    LaneAction const* const Lane = static_cast<LaneAction const*>(Action);
    if (Lane->EntranceRingXform.Ring == nullptr)
        return 0;
    return Lane->EntranceRingXform.Ring->get_id();
}

float CShip::get_tradelane_speed()
{
    // Listing 0x517c0: checks bUsingTradelane (+0x2bc), returns TradelaneSpeed at +0x2B8
    if (bUsingTradelane != 0)
        return TradelaneSpeed;
    return 0.0f;
}

int CShip::go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                        IObjRW* TradelaneObj, bool bFastExit, float Speed)
{
    // .text:062b1960..062b19f1: +0x2bc = 1; operator_new(0xfc) + LaneAction ctor 062c1220; the
    // IAction base (+0x10, or 0) goes to ActionDB::add with no null test (062b19c1..062b19db);
    // returns 0.
    bUsingTradelane = 1;
    Actions.add(new LaneAction(EntranceRing, ExitRing, TradelaneObj, bFastExit, Speed));
    return 0;
}

void CShip::airlock_in(Vector const& Pos, Matrix const& Orient)
{
    // Common.dll.c 85118 (listing 0x52ff0). Real bug fix: the final step is retail's
    // vtable+0x7c call, which is beam_object (confirmed via this port's own CSimple vtable
    // dump -- see CSimple::beam_object) -- i.e. PhySys::BeamR, which repositions this object
    // AND recursively replays the transform onto every attached child/subpart
    // (PhySys_BeamR_Children in common_physys.cpp). Calling set_position()+set_orientation()
    // instead (as this did previously) skips that child-object propagation entirely.
    Throttle = 1.0f;
    CEquip* EngineEquip = const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine));
    if (EngineEquip != nullptr)
    {
        if (!EngineEquip->IsActive())
        {
            EngineEquip->Activate(true);
        }
    }
    beam_object(Pos, Orient, false);
}

float CShip::close_bay()
{
    // .text:062b2c10..062b2d3c.
    BayDoorCounter -= 1;                                              // 062b2c1c DEC
    if (BayDoorCounter != 0)
    {
        // 062b2d21..062b2d35: other open requests remain -> the stored duration, returned
        // straight off the FPU (no store). No Anim / handle test.
        return DALib::Anim->GetStoredDuration(BayAnim.Index);
    }

    // 062b2c28..062b2c36: only an opening (3) or open (2) bay closes.
    if (BayState != 3 && BayState != 2)
        return 0.0f;
    if (BayAnim.Index == -1)                                          // 062b2c3c..062b2c45
    {
        BayState = 0;                                                 // 062b2d12
        return 0.0f;
    }

    BayState = 1;                                                     // 062b2c65
    // 062b2c4b..062b2c7c: Anim slot 0x2c EvalPlayback(handle, 1, -2.0f, 1.0f, 0, 1.0f, 1.0f, 0).
    DALib::Anim->EvalPlayback(BayAnim.Index, 1, -2.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                              AsOpaqueDword(1.0f), nullptr);
    AnimDB::Add(BayAnim.Index);                                       // 062b2c86
    // 062b2c8b..062b2ca3: the stored duration (slot 0x40), FSTP dword.
    float const Result = DALib::Anim->GetStoredDuration(BayAnim.Index);

    // 062b2ca7..062b2cc1: with an INotify (+0x168), notify(2, &bOpen) (vtable +0xc8,
    // CEqObj::notify) with bOpen = false.
    if (Notify != nullptr)
    {
        unsigned char bOpen = 0;
        notify(INotify::BayStateChanged, &bOpen);
    }

    // 062b2cc7..062b2d07: every equipment of type 0x100 is activated (vtable +0x30, true).
    CEquipTraverser Walk(0x100);
    for (CEquip* Equip = EquipManager.Traverse(Walk); Equip != nullptr;
         Equip = EquipManager.Traverse(Walk))
    {
        Equip->Activate(true);
    }
    return Result;
}

void CShip::jump_in(IObjRW* JumpObj, Vector const& Pos, Matrix const& Orient, float Speed)
{
    // .text:062b2d40..062b2f06: operator_new(0x68) + the JumpInAction ctor (inlined in retail,
    // 062b2d7c..062b2edb); the action (or 0) goes to ActionDB::add with no null test
    // (062b2ede..062b2ef1).
    Actions.add(new JumpInAction(JumpObj, Pos, Orient, Speed));
}

void CShip::collect_force_components(float ThrottleVal, ThrustEquipType ThrustType,
                                     float& OutForceX, float& OutForceY) const
{
    // .text:062b4540..062b46f0. Both outputs are zeroed first; every accumulation is an
    // x87 FADD into the output followed by FSTP dword.
    Archetype::Ship const* const ShipArch = shiparch();
    OutForceX = 0.0f;
    OutForceY = 0.0f;

    if (ThrustType == ThrustEquipEngine)
    {
        // 062b4566: first engine (FindFirst 0x20000) -> thrust(throttle) and linear drag.
        CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(0x20000)));
        if (Engine != nullptr)
        {
            OutForceX = (float)((double)(Engine->GetThrust(ThrottleVal)) + OutForceX);
            OutForceY = (float)((double)(Engine->GetLinearDrag()) + OutForceY);
        }
    }
    else if (ThrustType == ThrustEquipThruster)
    {
        // 062b45a9: the engine as above when present, then every thruster of the traverser
        // mask 0x400 -- the walk stops at the first entry CEThruster::cast rejects.
        CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(0x20000)));
        if (Engine != nullptr)
        {
            OutForceX = (float)((double)(Engine->GetThrust(ThrottleVal)) + OutForceX);
            OutForceY = (float)((double)(Engine->GetLinearDrag()) + OutForceY);
        }

        CEquipTraverser Trav(0x400);
        for (CEThruster* Thruster = CEThruster::cast(const_cast<CEquipManager&>(EquipManager).Traverse(Trav));
             Thruster != nullptr;
             Thruster = CEThruster::cast(const_cast<CEquipManager&>(EquipManager).Traverse(Trav)))
        {
            OutForceX = (float)((double)(Thruster->GetThrust()) + OutForceX);
        }
    }
    else if (ThrustType == ThrustEquipCruise)
    {
        // 062b463f: cruise thrust(throttle) and cruise drag of the first engine.
        CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(0x20000)));
        if (Engine != nullptr)
        {
            OutForceX = (float)((double)(Engine->GetCruiseThrust(ThrottleVal)) + OutForceX);
            OutForceY = (float)((double)(Engine->GetCruiseDrag()) + OutForceY);
        }
    }
    else if (ThrustType == ThrustEquipTradelane)
    {
        // 062b467c: force = float(throttle * NudgeForce (+0x120, unrounded) + force), then the
        // engine's linear drag.
        OutForceX = (float)((double)(ThrottleVal) * ShipArch->NudgeForce + OutForceX);
        CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(0x20000)));
        if (Engine != nullptr)
            OutForceY = (float)((double)(Engine->GetLinearDrag()) + OutForceY);
    }

    // 062b46b8: drag = float(LinearDrag (+0x104) + drag), also kept in the throttle slot;
    // with a zone (+0xa4): drag = float(zone slot +0x58 * ScannerInterference (+0xa8) *
    // that copy + drag).
    OutForceY = (float)((double)(ShipArch->LinearDrag) + OutForceY);
    float const BaseDrag = OutForceY;
    if (ZoneAtA4 != nullptr)
        OutForceY = (float)((double)(ZoneAtA4->GetDragModifier()) * ScannerInterference * BaseDrag +
                                       OutForceY);   // ZONE 0633ad70
}

void CShip::collect_angular_force_components(unsigned Axis, float ThrottleVal,
                                             float& OutTorqueX, float& OutTorqueY) const
{
    // .text:062b4700..062b478c. OutTorqueX = torque(axis, throttle) (FSTP dword); the three
    // drags factor (+0x2c0) * shiparch(+0x88, no null test)->AngularDrag x/y/z are formed
    // (each FSTP dword) and the one for axis 0/1/2 goes to OutTorqueY; any other axis leaves
    // OutTorqueY untouched (no zeroing).
    OutTorqueX = Steering.GetAxisSteeringTorque(Axis, ThrottleVal);
    Archetype::Ship const* const ShipArch = shiparch();
    float const DragX = AngularDragFactor * ShipArch->AngularDragX;
    float const DragY = AngularDragFactor * ShipArch->AngularDragY;
    float const DragZ = AngularDragFactor * ShipArch->AngularDragZ;
    if (Axis == 0)
        OutTorqueY = DragX;
    else if (Axis == 1)
        OutTorqueY = DragY;
    else if (Axis == 2)
        OutTorqueY = DragZ;
}

double CShip::get_linear_drag() const
{
    // .text:062b3730..062b37a8. Drag = shiparch(+0x88, no null test)->LinearDrag (+0x104)
    // as a float local; the first engine (FindFirst 0x20000, CEEngine::cast) that IsActive()
    // (vtable +0x4) adds GetCruiseDrag() when its byte +0x54 is set, else GetLinearDrag()
    // (FADD, FSTP dword). With a zone (+0xa4): zone slot +0x58 FMUL drag, left unrounded in
    // ST0 (hence the double return: the three CEEngine callers keep it on the FPU);
    // otherwise FLD of the local.
    Archetype::Ship const* const ShipArch = shiparch();
    float TotalDrag = ShipArch->LinearDrag;

    CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(0x20000)));
    if (Engine != nullptr && Engine->IsActive())
    {
        if (Engine->bCruiseActive != 0)
            TotalDrag = (float)((double)(Engine->GetCruiseDrag()) + TotalDrag);
        else
            TotalDrag = (float)((double)(Engine->GetLinearDrag()) + TotalDrag);
    }

    if (ZoneAtA4 != nullptr)
        return (double)ZoneAtA4->GetDragModifier() * TotalDrag;   // ZONE 0633ad70, 062b379a-062b379d

    return TotalDrag;
}

Vector CShip::get_angular_drag() const
{
    // .text:062b37b0..062b37ea: factor (+0x2c0) FMUL shiparch(+0x88, no null test)->
    // AngularDrag x/y/z (+0x108/+0x10c/+0x110), each FSTP dword.
    Vector OutDrag;
    Archetype::Ship const* const ShipArch = shiparch();
    OutDrag.x = AngularDragFactor * ShipArch->AngularDragX;
    OutDrag.y = AngularDragFactor * ShipArch->AngularDragY;
    OutDrag.z = AngularDragFactor * ShipArch->AngularDragZ;
    return OutDrag;
}

float CShip::get_time_to_accelerate(float ForceOverMass, float Mass, float Drag,
                                    float FromSpeed, float ToSpeed) const
{
    // .text:062b42f0..062b4431 (x87; double below stands for the unrounded stack value).
    // 062b42f0..062b434b: FromSpeed * ToSpeed (unrounded), FromSpeed and ToSpeed each FCOMP
    // 0.0 / TEST AH,0x5 / JP: only "less than" (C0 alone) returns 0; NaN goes on.
    if ((double)(FromSpeed) * ToSpeed < 0.0)
        return 0.0f;
    if (FromSpeed < 0.0f)
        return 0.0f;
    if (ToSpeed < 0.0f)
        return 0.0f;

    // 062b434e: param_1 = float(ForceOverMass * Mass) (FSTP into the argument slot).
    float const Force = ForceOverMass * Mass;

    // 062b435a..062b437f: Drag FCOMP 0.0 / TEST AH,0x44 / JP -- equal (C3 alone) takes the
    // no-drag branch, NaN does not. (ToSpeed - FromSpeed) * (Mass / Force), no zero test.
    if (Drag == 0.0f)
        return (float)(((double)(ToSpeed) - FromSpeed) *
                                  ((double)(Mass) / Force));

    // 062b4382..062b43e0: terminal = Force / Drag (unrounded), param_1 = float(terminal).
    // terminal > 0 (TEST AH,0x41 / JNZ not taken): cap = float(terminal) * 0.99f; ToSpeed > cap
    // (TEST AH,0x41 / JNZ not taken) stores float(cap). Otherwise float(terminal) FCOMP 0.0 /
    // TEST AH,0x41 / JP: exactly one of C0/C3 (<= 0, not NaN) clamps ToSpeed < 0.01 (double
    // 0639e6b0, TEST AH,0x5 / JP) to 0.01f (0x3c23d70a).
    double const Terminal = (double)(Force) / Drag;
    float const TerminalF = (float)(Terminal);
    if (Terminal > 0.0)
    {
        double const Cap = (double)(TerminalF) * 0.99f;
        if (ToSpeed > Cap)
            ToSpeed = (float)(Cap);
    }
    else if (TerminalF <= 0.0f)
    {
        if (ToSpeed < 0.01)
            ToSpeed = 0.01f;
    }

    // 062b43e2..062b43f9: gap = FromSpeed - float(terminal) (unrounded), param_4 =
    // float(gap); gap FCOMP 0.0 / TEST AH,0x44 / JNP: equal returns 0, NaN goes on.
    double const Gap = (double)(FromSpeed) - TerminalF;
    if (Gap == 0.0)
        return 0.0f;
    float const GapF = (float)(Gap);

    // 062b43fd..062b4426: ratio = (ToSpeed - float(terminal)) / float(gap); FCOM 0.0 / TEST
    // AH,0x41 / JNZ: <= 0 or NaN returns 0; else -(ln(ratio) * Mass / Drag) (FLDLN2/FYL2X).
    double const Ratio = ((double)(ToSpeed) - TerminalF) / GapF;
    if (!(Ratio > 0.0))
        return 0.0f;
    return (float)(-(std::log(Ratio) * Mass / Drag));
}

float CShip::get_distance_travelled(float ForceOverMass, float Mass, float Drag,
                                    float FromSpeed, float Time) const
{
    // .text:062b4440..062b44ed. Time (param_5) < 0 (TEST AH,0x5 / JNP: C0 alone) -> 0;
    // Drag < 0.01f -> 0; FromSpeed (param_4) < 0 stores 0.0; Time > 1000 (TEST AH,0x41 / JNZ
    // not taken) stores 1000. NaN passes every test.
    if (Time < 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    if (FromSpeed < 0.0f)
        FromSpeed = 0.0f;
    if (Time > 1000.0f)
        Time = 1000.0f;

    // 062b449b..062b44eb: terminal = (Mass / Drag) * ForceOverMass; decay = exp(((-1 / Mass) *
    // Drag) * Time) (FLDL2E/F2XM1/FSCALE); terminal * Time + ((1 - decay) * ((FromSpeed -
    // terminal) * Mass)) / Drag, all on the x87 stack.
    double const Terminal = (double)(Mass) / Drag * ForceOverMass;
    double const Decay = std::exp(-1.0 / Mass * Drag * Time);
    return (float)(Terminal * Time + ((1.0 - Decay) * ((FromSpeed - Terminal) * Mass)) / Drag);
}

float CShip::get_initial_speed_to_coast_distance(float A, float B, float C) const
{
    // .text:062b44f0..062b452f. A FCOMP 0.0 / TEST AH,0x41 / JNP: <= 0 returns 0 (NaN goes
    // on); C < 0.01f (TEST AH,0x5 / JNP) -> 0; B < 0 -> 0; else (A / B) * C.
    if (A <= 0.0f)
        return 0.0f;
    if (C < 0.01f)
        return 0.0f;
    if (B < 0.0f)
        return 0.0f;
    return (float)((double)(A) / B * C);
}

void CShip::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:0628f050: `RET 0x8` -- the whole body. Both arguments are ignored.
    (void)EventSim;
    (void)CoreList;
}

namespace
{
// Inlined at the head of every CShip angular query (e.g. 062b3ce0..062b3d0e): the moment of
// inertia from PhySys when the object has a body (+0x54), else a zero vector.
Vector ShipMomentOfInertia(CShip const* Ship)
{
    if (Ship->BodyPtr != nullptr)
        return PhySys::GetMomentOfInertia(Ship);
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    return Zero;
}
}

float CShip::get_time_to_accelerate(float Throttle, float FromSpeed, float ToSpeed,
                                    ThrustEquipType ThrustType) const
{
    // .text:062b37f0..062b38f9. FromSpeed < 0 and ToSpeed < 0 (TEST AH,0x5 / JP) store 0;
    // Throttle > 1 (TEST AH,0x41 / JNZ not taken) -> 1, else < -1 -> -1; NaN passes.
    if (FromSpeed < 0.0f)
        FromSpeed = 0.0f;
    if (ToSpeed < 0.0f)
        ToSpeed = 0.0f;
    if (Throttle > 1.0f)
        Throttle = 1.0f;
    else if (Throttle < -1.0f)
        Throttle = -1.0f;

    // 062b385c..062b387c: FromSpeed and Throttle both == 0 (TEST AH,0x44: C3 alone) -> 0.
    if (FromSpeed == 0.0f && Throttle == 0.0f)
        return 0.0f;

    // 062b387e..062b3894: mass = get_mass() (vtable +0x6c), FST dword; <= 0.01 (double) or
    // NaN -> 0.
    float const Mass = get_mass();
    if (!(Mass > 0.01))
        return 0.0f;

    // 062b3896..062b38e3: collect_force_components(throttle, type, &force, &drag) into zeroed
    // locals, then the private overload with float(force / mass).
    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(Throttle, ThrustType, Force, Drag);
    return get_time_to_accelerate(Force / Mass, Mass, Drag, FromSpeed, ToSpeed);
}

float CShip::get_time_to_accelerate_angularly(float PitchThrottle, float YawThrottle,
                                              float FromAngVel, float ToAngVel) const
{
    // .text:062b3ce0..062b3dab (no archetype test, no zero-inertia guard).
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();

    // pitch = float(torque(0) / I.x); yaw = float(torque(1) / I.y) (both FSTP dword);
    // drag = float(factor (+0x2c0) * AngularDragY (+0x10c)).
    float const PitchAccel = Steering.GetAxisSteeringTorque(0, PitchThrottle) / Inertia.x;
    float const YawAccel = Steering.GetAxisSteeringTorque(1, YawThrottle) / Inertia.y;
    float const Drag = AngularDragFactor * ShipArch->AngularDragY;

    // 062b3d67..062b3d9e: inertia = float(sqrt(I.y^2 + I.x^2)), accel = float(sqrt(yaw^2 +
    // pitch^2)), sums and roots on the x87 stack.
    float const CombinedInertia = (float)(
        std::sqrt((double)(Inertia.y) * Inertia.y + (double)(Inertia.x) * Inertia.x));
    float const CombinedAccel = (float)(
        std::sqrt((double)(YawAccel) * YawAccel + (double)(PitchAccel) * PitchAccel));
    return get_time_to_accelerate(CombinedAccel, CombinedInertia, Drag, FromAngVel, ToAngVel);
}

float CShip::get_time_to_accelerate_angularly(float Throttle, float FromAngVel, float ToAngVel) const
{
    // .text:062b3db0..062b3e36: roll = float(torque(2) / I.z), drag = float(factor *
    // AngularDragZ (+0x110)); private overload (roll, I.z, drag, from, to).
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();
    float const RollAccel = Steering.GetAxisSteeringTorque(2, Throttle) / Inertia.z;
    float const Drag = AngularDragFactor * ShipArch->AngularDragZ;
    return get_time_to_accelerate(RollAccel, Inertia.z, Drag, FromAngVel, ToAngVel);
}

float CShip::get_distance_travelled(float Throttle, float FromSpeed, float Time,
                                    ThrustEquipType ThrustType) const
{
    // .text:062b3900..062b3a9a. Time < 0 (TEST AH,0x5 / JNP: C0 alone) -> 0; FromSpeed < 0
    // stores 0; Throttle clamped to [-1, 1] as in 062b37f0.
    if (Time < 0.0f)
        return 0.0f;
    if (FromSpeed < 0.0f)
        FromSpeed = 0.0f;
    if (Throttle > 1.0f)
        Throttle = 1.0f;
    else if (Throttle < -1.0f)
        Throttle = -1.0f;

    // 062b3968..062b397e: mass = get_mass(), FST dword; <= 0.01 (double) or NaN -> 0.
    float const Mass = get_mass();
    if (!(Mass > 0.01))
        return 0.0f;

    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(Throttle, ThrustType, Force, Drag);

    // 062b39af..062b3a39: the private 062b4440 inlined, except for the decay exponent order.
    // accel = float(force / mass); Time < 0 -> 0 (again); drag < 0.01f -> 0; speed = max(
    // FromSpeed, 0); t = Time > 1000 ? 1000 : Time (kept on the stack).
    float const Accel = Force / Mass;
    if (Time < 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    float const StartSpeed = (FromSpeed < 0.0f) ? 0.0f : FromSpeed;
    double const T = (Time > 1000.0f) ? 1000.0 : (double)(Time);

    // 062b3a39..062b3a86: terminal = (mass / drag) * accel; decay = exp(((-1 / mass) * t) *
    // drag); terminal * t + ((1 - decay) * ((speed - terminal) * mass)) / drag.
    double const Terminal = (double)(Mass) / Drag * Accel;
    double const Decay = std::exp(-1.0 / Mass * T * Drag);
    return (float)(((1.0 - Decay) * ((StartSpeed - Terminal) * Mass)) / Drag + Terminal * T);
}

float CShip::get_initial_speed_to_coast_distance(float Speed, bool bStop) const
{
    // .text:062b3aa0..062b3b60. Not a call to the private 062b44f0: the formula here is
    // (drag / mass) * speed.
    // Speed FCOMP 0.0 / TEST AH,0x41 / JNP: <= 0 returns 0, NaN goes on.
    if (Speed <= 0.0f)
        return 0.0f;

    // 062b3abb..062b3acf: mass = get_mass(), FST dword; <= 0.01 (double) or NaN -> 0.
    float const Mass = get_mass();
    if (!(Mass > 0.01))
        return 0.0f;

    // 062b3ad5..062b3b0b: collect_force_components(0.0f, bStop ? 1 : 0, &force, &drag).
    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(0.0f, bStop ? ThrustEquipEngine : ThrustEquipNormal, Force, Drag);

    // 062b3b10..062b3b4c: Speed <= 0 -> 0 (again); drag < 0.01f -> 0; mass < 0 -> 0; else
    // (drag / mass) * Speed.
    if (Speed <= 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    if (Mass < 0.0f)
        return 0.0f;
    return (float)((double)(Drag) / Mass * Speed);
}

float CShip::get_speed(float ThrottleVal, ThrustEquipType ThrustType) const
{
    // .text:062b3c30..062b3cd3. Throttle clamped to [-1, 1]; get_mass() <= 0.01 (double)
    // or NaN -> 0; collect_force_components(throttle, type, &force, &drag); drag <= 0.001f
    // (TEST AH,0x41 / JNZ) or NaN -> 0; else force / drag.
    if (ThrottleVal > 1.0f)
        ThrottleVal = 1.0f;
    else if (ThrottleVal < -1.0f)
        ThrottleVal = -1.0f;

    if (!(get_mass() > 0.01))
        return 0.0f;

    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(ThrottleVal, ThrustType, Force, Drag);
    if (!(Drag > 0.001f))
        return 0.0f;
    return Force / Drag;
}

float CShip::get_projected_throttle(float Speed, ThrustEquipType ThrustType) const
{
    // .text:062b3b70..062b3c25. Direction 1.0f, replaced by -1.0f when Speed FCOMP 0.0 /
    // TEST AH,0x41 / JZ not taken (<= 0 or NaN).
    float Direction = 1.0f;
    if (!(Speed > 0.0f))
        Direction = -1.0f;
    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(Direction, ThrustType, Force, Drag);

    // 062b3bc5..062b3c25: drag FCOMP 0.0 / TEST AH,0x1 / JNZ: < 0 or NaN -> 0; |force| <=
    // (double)0.01f (0639e6c0) or NaN -> 0; r = |Speed| * drag / force (unrounded); FCOM
    // 1.0f / TEST AH,0x41 / JNZ: <= 1 or NaN returns r, else 1.0f.
    if (!(Drag >= 0.0f))
        return 0.0f;
    if (!(std::fabs((double)(Force)) > (double)(0.01f)))
        return 0.0f;
    double const Result = std::fabs((double)(Speed)) * Drag / Force;
    if (!(Result > 1.0))
        return (float)(Result);
    return 1.0f;
}

float CShip::get_projected_axis_throttle_XY(float Speed) const
{
    // .text:062b40e0..062b419e (no archetype test, no zero-inertia guard). pitch =
    // float(torque(0, 1.0f) / I.x); drag = float(factor * AngularDragY (+0x10c)); yaw =
    // torque(1, 1.0f) / I.y kept unrounded; s = sqrt(yaw^2 + pitch^2); |s| <= (double)0.01f
    // (0639e6c0) or NaN -> 0; else (drag * Speed) / (s * I.y).
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();
    float const PitchAccel = Steering.GetAxisSteeringTorque(0, 1.0f) / Inertia.x;
    double const YawAccel = (double)(Steering.GetAxisSteeringTorque(1, 1.0f)) / Inertia.y;
    float const Drag = AngularDragFactor * ShipArch->AngularDragY;
    double const Combined = std::sqrt(YawAccel * YawAccel + (double)(PitchAccel) * PitchAccel);
    if (!(std::fabs(Combined) > (double)(0.01f)))
        return 0.0f;
    return (float)((double)(Drag) * Speed / (Combined * Inertia.y));
}

float CShip::get_projected_axis_throttle_Z(float Speed) const
{
    // .text:062b41b0..062b4246 (no archetype test, no zero-inertia guard). drag =
    // float(factor * AngularDragZ (+0x110)); roll = torque(2, 1.0f) / I.z kept unrounded;
    // |roll| <= (double)0.01f or NaN -> 0; else (drag * Speed) / (I.z * roll).
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();
    double const RollAccel = (double)(Steering.GetAxisSteeringTorque(2, 1.0f)) / Inertia.z;
    float const Drag = AngularDragFactor * ShipArch->AngularDragZ;
    if (!(std::fabs(RollAccel) > (double)(0.01f)))
        return 0.0f;
    return (float)((double)(Drag) * Speed / ((double)(Inertia.z) * RollAccel));
}

float CShip::get_angular_speed_XY(float MaxRollRate, float MaxPitchRate) const
{
    // .text:062b4250..062b42b6 (no archetype test). pitch = float(torque(0) / (factor *
    // AngularDragX (+0x108))), FSTP dword; yaw = torque(1) / (factor * AngularDragY) kept
    // unrounded; sqrt(yaw^2 + pitch^2) on the x87 stack.
    Archetype::Ship const* const ShipArch = shiparch();
    float const PitchSpeed = (float)((double)(Steering.GetAxisSteeringTorque(0, MaxRollRate)) /
                                                ((double)(AngularDragFactor) * ShipArch->AngularDragX));
    double const YawSpeed = (double)(Steering.GetAxisSteeringTorque(1, MaxPitchRate)) /
                            ((double)(AngularDragFactor) * ShipArch->AngularDragY);
    return (float)(std::sqrt(YawSpeed * YawSpeed + (double)(PitchSpeed) * PitchSpeed));
}

float CShip::get_angular_speed_Z(float MaxYawRate) const
{
    // .text:062b42c0..062b42ea (no archetype test): torque(2) / (factor * AngularDragZ),
    // the product unrounded.
    Archetype::Ship const* const ShipArch = shiparch();
    return (float)((double)(Steering.GetAxisSteeringTorque(2, MaxYawRate)) /
                              ((double)(AngularDragFactor) * ShipArch->AngularDragZ));
}

float CShip::get_angular_distance_travelled(float PitchThrottle, float YawThrottle,
                                            float FromAngVel, float Time) const
{
    // .text:062b3e40..062b3fa5 (no archetype test, no zero-inertia guard).
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();

    // pitch = float(torque(0) / I.x); yaw = torque(1) / I.y kept unrounded; drag =
    // float(factor * AngularDragY (+0x10c)); accel = float(sqrt(yaw^2 + pitch^2)); inertia =
    // float(sqrt(I.y^2 + I.x^2)).
    float const PitchAccel = Steering.GetAxisSteeringTorque(0, PitchThrottle) / Inertia.x;
    double const YawAccel = (double)(Steering.GetAxisSteeringTorque(1, YawThrottle)) / Inertia.y;
    float const Drag = AngularDragFactor * ShipArch->AngularDragY;
    float const CombinedAccel = (float)(
        std::sqrt(YawAccel * YawAccel + (double)(PitchAccel) * PitchAccel));
    float const CombinedInertia = (float)(
        std::sqrt((double)(Inertia.y) * Inertia.y + (double)(Inertia.x) * Inertia.x));

    // 062b3efc..062b3f54: Time < 0 (TEST AH,0x5 / JNP: C0 alone) -> 0; drag < 0.01f -> 0;
    // speed = FromAngVel < 0 ? 0 : FromAngVel; t = Time > 1000 ? 1000 : Time. NaN passes.
    if (Time < 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    double const StartSpeed = (FromAngVel < 0.0f) ? 0.0 : (double)(FromAngVel);
    double const T = (Time > 1000.0f) ? 1000.0 : (double)(Time);

    // 062b3f54..062b3f9e: terminal = (accel / drag) * inertia; decay = exp(((-1 / inertia) *
    // t) * drag); terminal * t + ((1 - decay) * ((speed - terminal) * inertia)) / drag.
    double const Terminal = (double)(CombinedAccel) / Drag * CombinedInertia;
    double const Decay = std::exp(-1.0 / CombinedInertia * T * Drag);
    return (float)(Terminal * T + ((1.0 - Decay) * ((StartSpeed - Terminal) * CombinedInertia)) / Drag);
}

float CShip::get_angular_distance_travelled(float Throttle, float FromAngVel, float Time) const
{
    // .text:062b3fb0..062b40d1 (no archetype test, no zero-inertia guard). roll =
    // float(torque(2) / I.z); drag = float(factor * AngularDragZ (+0x110)).
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();
    float const RollAccel = Steering.GetAxisSteeringTorque(2, Throttle) / Inertia.z;
    float const Drag = AngularDragFactor * ShipArch->AngularDragZ;

    // 062b4028..062b4080: Time < 0 -> 0; drag < 0.01f -> 0; speed = max(FromAngVel, 0);
    // t = min(Time, 1000) (NaN passes as in the 4-param overload).
    if (Time < 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    double const StartSpeed = (FromAngVel < 0.0f) ? 0.0 : (double)(FromAngVel);
    double const T = (Time > 1000.0f) ? 1000.0 : (double)(Time);

    // 062b4080..062b40ca: terminal = (roll / drag) * I.z; decay = exp(((-1 / I.z) * drag) *
    // t) -- note the factor order differs from the 4-param overload; terminal * t +
    // ((1 - decay) * ((speed - terminal) * I.z)) / drag.
    double const Terminal = (double)(RollAccel) / Drag * Inertia.z;
    double const Decay = std::exp(-1.0 / Inertia.z * Drag * T);
    return (float)(Terminal * T + ((1.0 - Decay) * ((StartSpeed - Terminal) * Inertia.z)) / Drag);
}

float CShip::get_throttle() const
{
    // Listing 0x27b60: FLD [this+0x29C]
    return Throttle;
}

Vector const& CShip::get_axis_throttle() const
{
    // Listing 0x27b70: LEA EAX, [this+0x24C] = Steering+0x1C
    return Steering.GetAxisThrottle();
}

float CShip::get_max_bank_angle() const
{
    // .text:06287ba0..06287bac: FLD [shiparch(+0x88, no null test) + 0x12c] (MaxBankAngle).
    return shiparch()->MaxBankAngle;
}

float CShip::get_thrust_power() const
{
    // Listing 0x28240: FLD [this+0x2a0]; RET
    return ThrustPower;
}

float CShip::get_max_thrust_power() const
{
    // Listing 0x28260: FLD [this+0x2a4]; RET
    return MaxThrustPower;
}

float CShip::get_thrust_power_ratio() const
{
    // Listing 0x28270: if max <= 0 return 0.0f, else return current / max
    if (MaxThrustPower <= 0.0f)
        return 0.0f;
    return ThrustPower / MaxThrustPower;
}

enum BayState CShip::get_bay_state() const
{
    // Listing 0x27b50: reads dword at [this+0x2AC] (BayState),
    // ?get_bay_state@CShip@@QBE?AW4BayState@@XZ. The member of the same name shadows the
    // enum inside CShip, hence the elaborated type specifier.
    return (enum BayState)BayState;
}

void CShip::VerifyTarget()
{
    // .text:062b1190..062b128a. No target -> the sub-target is reset (062b127b).
    IObjRW* const TargetObj = Target.get();
    if (TargetObj == nullptr)
    {
        SubTargetIndex = SUBOBJ_ID_NONE;
        return;
    }
    if (SubTargetIndex == SUBOBJ_ID_NONE)                             // 062b11ac..062b11ba
        return;

    // 062b11c0..062b1245: the target's object (slot 0x150 cobject) must be a CEqObj
    // (class & 0x103); then CEqObj::sub_obj_id_to_inst (vtable +0xf8) on the sub-target.
    // Otherwise the instance is -1. Retail fetches cobject() twice; the second fetch is the
    // same call on the same watcher.
    long SubInst = -1;
    CObject const* const Obj = TargetObj->cobject();
    if (Obj != nullptr && (Obj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        CObject const* const EqObj = Target.get()->cobject();
        CEqObj const* const Eq = (EqObj != nullptr && (EqObj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
            ? (CEqObj const*)EqObj : nullptr;
        SubInst = Eq->sub_obj_id_to_inst(SubTargetIndex);
    }

    // 062b1248..062b1272: IsDescendant(target's IObjInspect::get_index() (slot 0x1c), SubInst);
    // false -> the sub-target is reset.
    if (!IsDescendant(Target.get()->get_index(), SubInst))
        SubTargetIndex = SUBOBJ_ID_NONE;
}

bool CShip::get_tgt_lead_fire_pos(IObjInspect const* TargetInspect, Vector& OutPos) const
{
    // .text:062b1290..062b1471.
    if (TargetInspect == nullptr)
        return false;

    bool bLead = false;                                               // [ESP+0x54]
    ShipGunStats const& Stats = get_gun_stats();
    float RangeSq = 0.0f;                                             // [ESP+0x10]

    if (Stats.ActiveGunCount > 0)
    {
        // 062b12c6..062b130b: the target's velocity (IObjInspect slot 1) and its aim point:
        // the sub-target's centre of mass (slot 6) when that answers 0, else the whole
        // object's (slot 5).
        Vector const TargetVelocity = TargetInspect->get_velocity();
        Vector TargetPos;
        if (TargetInspect->get_sub_obj_center_of_mass(SubTargetIndex, TargetPos) != 0)
            TargetPos = TargetInspect->get_center_of_mass();

        // 062b130f..062b1373: RelPos = TargetPos - WorldPosition and RelVel = target velocity
        // - this ship's get_velocity() (vtable +0x54), each component rounded by FSTP dword;
        // Time starts at 0.
        Vector RelPos;
        RelPos.x = (float)((double)(TargetPos.x) - WorldPosition.x);
        RelPos.y = (float)((double)(TargetPos.y) - WorldPosition.y);
        RelPos.z = (float)((double)(TargetPos.z) - WorldPosition.z);
        Vector const ShipVelocity = get_velocity();
        Vector RelVel;
        RelVel.x = (float)((double)(TargetVelocity.x) - ShipVelocity.x);
        RelVel.y = (float)((double)(TargetVelocity.y) - ShipVelocity.y);
        RelVel.z = (float)((double)(TargetVelocity.z) - ShipVelocity.z);
        float Time = 0.0f;

        // 062b1377..062b13d7: CEGun::ComputeTimeToTgt(RelPos, RelVel, average muzzle
        // velocity, Time); on success OutPos = TargetPos + RelVel * Time. x and y keep the
        // product on the FPU and round the sum; z rounds the product first.
        if (CEGun::ComputeTimeToTgt(RelPos, RelVel, Stats.AvgMuzzleVelocity, Time))
        {
            float const LeadZ = (float)((double)(RelVel.z) * Time);
            OutPos.x = (float)((double)(RelVel.x) * Time + TargetPos.x);
            OutPos.y = (float)((double)(RelVel.y) * Time + TargetPos.y);
            OutPos.z = (float)((double)(LeadZ) + TargetPos.z);
            bLead = true;
        }

        // 062b13df..062b13e8: RangeSq = MaxMunitionRange squared, FSTP dword.
        RangeSq = (float)((double)(Stats.MaxMunitionRange) * Stats.MaxMunitionRange);
    }
    else if (Stats.CmDropperCount > 0)
    {
        // 062b144a..062b1458: no active gun but countermeasure droppers: their range squared.
        RangeSq = (float)((double)(Stats.CmMaxMunitionRange) * Stats.CmMaxMunitionRange);
    }

    // 062b13f0..062b1406: without a lead the aim point is the target's position (slot 0).
    if (!bLead)
        OutPos = TargetInspect->get_position();

    // 062b1409..062b143b: `FCOMP RangeSq; TEST AH,5; JP -> false`: in range only when the
    // distance squared (dz*dz + dy*dy + dx*dx, on the FPU) is strictly below RangeSq.
    double const Dx = (double)(OutPos.x) - WorldPosition.x;
    double const Dy = (double)(OutPos.y) - WorldPosition.y;
    double const Dz = (double)(OutPos.z) - WorldPosition.z;
    double const DistSq = Dz * Dz + Dy * Dy + Dx * Dx;
    return DistSq < RangeSq;
}

bool CShip::check_formation_follower_status(float MaxDistance)
{
    // .text:062b4bf0..062b4d0b. For every live formation follower (any IObjRW, no class test):
    for (std::vector<FObjRWWatcher>::const_iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower == nullptr)
            continue;

        // 062b4c25..062b4c44: its formation offset into a zeroed local (IObjInspect slot 0xac).
        Vector Offset;
        Offset.x = 0.0f;
        Offset.y = 0.0f;
        Offset.z = 0.0f;
        Follower->get_formation_offset(Offset);

        // 062b4c4a..062b4c6b: I3DMathEngine slot 0x28 TransformXform(Slot, this+8, Offset) --
        // this ship's Orientation and WorldPosition read as one Transform.
        Vector Slot;
        Acquire3DMathEngine()->TransformXform((FVector&)Slot, (FTransform const&)Orientation,
                                              (FVector const&)Offset);

        // 062b4c6e..062b4caa: D = follower position (IObjInspect slot 0) - Slot, kept on the
        // FPU; S = dz*dz + dy*dy + dx*dx (that order).
        Vector const& FollowerPos = Follower->get_position();
        double const Dx = (double)(FollowerPos.x) - Slot.x;
        double const Dy = (double)(FollowerPos.y) - Slot.y;
        double const Dz = (double)(FollowerPos.z) - Slot.z;
        double const S = Dz * Dz + Dy * Dy + Dx * Dx;

        // 062b4cae..062b4cd8: the 0x5f3759df inverse square root on S rounded by FST dword,
        // one Newton step against the unrounded S (0.5 = DAT_0639c13c, 1.5 = DAT_0639e6d0),
        // times S again: an approximate distance.
        float const SRounded = (float)(S);
        int Bits;
        memcpy(&Bits, &SRounded, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Y;
        memcpy(&Y, &Bits, sizeof(Y));
        double Dist = (double)(Y) * Y;
        Dist *= S;
        Dist *= (double)(0.5f);
        Dist = (double)(1.5f) - Dist;
        Dist *= Y;
        Dist *= S;

        // 062b4cda..062b4ce5: `FCOMP MaxDistance; TEST AH,0x41; JZ -> return false` -- C3 and
        // C0 both clear means Dist > MaxDistance.
        if (Dist > MaxDistance)
            return false;
    }
    return true;
}

void CShip::flush_animations()
{
    // .text:062b0dd0..062b0e1d: AnimDB::Rem unconditionally, then the script release only
    // for a live handle (062b0de8 CMP -1 / JZ), then a tail jump to CEqObj::flush_animations.
    AnimDB::Rem(BayAnim.Index);                                           // 062b0dda
    if (BayAnim.Index != -1)
    {
        DALib::Anim->StopPlayback(BayAnim.Index);                         // 062b0df9 slot 0x38
        DALib::Anim->DestroyScript((void*)BayAnim.Index); // 062b0e0d slot 0x28
        BayAnim.Index = -1;                                               // 062b0e10
    }

    CEqObj::flush_animations();
}

void CShip::disable_controllers()
{
    // Common.dll.c 0x50d30
    CEqObj::disable_controllers();
    Steering.DisableController();
    NudgeEngine.DisableController();
    StrafeEngine.DisableController();
    PhySys::Controller::UnRegister(this);
}

void CShip::enable_controllers()
{
    // Common.dll.c 0x50d80
    CEqObj::enable_controllers();
    Steering.EnableController();
    NudgeEngine.EnableController();
    StrafeEngine.EnableController();
    PhySys::Controller::Register(this, this);
}

// ============================================================================
// CShip::update вЂ” main game loop update for ships
// Listing 0x50e30: calls ActionDB::update, CSimple::update_zones, CEqObj::update,
// then computes damping via engine drag and moment of inertia
// ============================================================================

int CShip::update(float Dt, unsigned Flags)
{
    // .text:062b0e30..062b1182. Always returns 0; CEqObj::update's result is dropped.
    if ((Flags & CObject::NO_ACTIONS) == 0)                           // 062b0e3a..062b0e5e
        Actions.update(Dt);
    CSimple::update_zones(Dt, Flags);                                 // 062b0e67
    CEqObj::update(Dt, Flags);                                        // 062b0e70

    if ((Flags & CObject::NO_DYNAMICS) == 0)                          // 062b0e75
    {
        PhySys::Wakeup(this);                                         // 062b0e82

        // 062b0e87..062b0e96: the archetype's linear drag (+0x104), kept as a float in the
        // Dt argument slot. No null test on the archetype.
        Archetype::Ship const* const ShipArch = shiparch();
        float Drag = ShipArch->LinearDrag;

        // 062b0e9a..062b0edd: an active engine (CEquip slot 1 IsActive) adds its cruise drag
        // when cruising (+0x54), else its linear drag; one FSTP dword. GetCruiseDrag leaves
        // its sum unrounded in ST0 (062b0ecd -> FADD 062b0ed9), so the local is double.
        CEEngine* const Engine =
            CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine)));
        if (Engine != nullptr && Engine->IsActive())
        {
            double const EngineDrag = Engine->bCruiseActive != 0 ? Engine->GetCruiseDrag()
                                                                 : Engine->GetLinearDrag();
            Drag = (float)(EngineDrag + Drag);
        }

        // 062b0ee1..062b0ef4: the zone's drag modifier (IZone slot +0x58) scales it.
        if (ZoneAtA4 != nullptr)
            Drag = (float)((double)(ZoneAtA4->GetDragModifier()) * Drag);

        // 062b0ef8..062b0f15: linear damping = Drag / get_mass() (vtable +0x6c), FDIVR, one
        // FSTP dword; no zero test on the mass.
        float const LinearDamping = (float)((double)(Drag) / get_mass());

        // 062b0f19..062b0f36: the archetype's angular drag (+0x108..+0x110) times
        // AngularDragFactor (+0x2c0), each product rounded into a Vector (FUN_0628b030).
        Vector AngularDrag;
        AngularDrag.x = (float)((double)(AngularDragFactor) * ShipArch->AngularDragX);
        AngularDrag.y = (float)((double)(AngularDragFactor) * ShipArch->AngularDragY);
        AngularDrag.z = (float)((double)(AngularDragFactor) * ShipArch->AngularDragZ);

        // 062b0f3b..062b0f75: divided per axis by the moment of inertia (no zero test), each
        // quotient rounded, then PhySys::SetDamping(this, LinearDamping, AngularDamping).
        Vector const MomentOfInertia = PhySys::GetMomentOfInertia(this);
        Vector AngularDamping;
        AngularDamping.x = (float)((double)(AngularDrag.x) / MomentOfInertia.x);
        AngularDamping.y = (float)((double)(AngularDrag.y) / MomentOfInertia.y);
        AngularDamping.z = (float)((double)(AngularDrag.z) / MomentOfInertia.z);
        PhySys::SetDamping(this, LinearDamping, AngularDamping);
    }

    // 062b0f7d..062b0ff1: the bay door animation. Opening (3): GetScriptDuration (slot 0x14)
    // rounded by FSTP dword, then GetStoredDuration (slot 0x40); `FCOMP; TEST AH,1; JNZ` keeps
    // the state while Stored < Script or the compare is unordered (C0 set), so the bay is open
    // (2) only when Stored >= Script. Closing (1):
    // `FCOMP 0.0; TEST AH,0x41; JP` -> Stored <= 0 means closed (0). No null test on Anim.
    if (BayState == 3)
    {
        float const ScriptDuration =
            DALib::Anim->GetScriptDuration((void*)BayAnim.Index);
        float const StoredDuration = DALib::Anim->GetStoredDuration(BayAnim.Index);
        if (StoredDuration >= ScriptDuration)
            BayState = 2;
    }
    else if (BayState == 1)
    {
        float const StoredDuration = DALib::Anim->GetStoredDuration(BayAnim.Index);
        if (StoredDuration <= 0.0f)
            BayState = 0;
    }

    // 062b0ffb..062b10d1: the atmosphere the ship is inside.
    AtmosphereSolar = nullptr;

    // ObjectDb (+0xac) and this object's collision state required; the ObjectDb's spatial
    // bucket for mask 0xfdff (IObjDB slot 0x0c) is queried with the sphere {WorldPosition,
    // 14000 (0x465ac000)} into a {0x800, 0, items} block (bucket slot 0x1c QuerySphere).
    if (ObjectDb != nullptr && PhySys::GetCollisionState(this))
    {
        ISpatialPartition* const Bucket = (ISpatialPartition*)ObjectDb->GetBucket(0xfdff);
        if (Bucket != nullptr)
        {
            Geometry::Sphere Query;
            Query.Center = WorldPosition;
            Query.Radius = 14000.0f;
            FSpatialQueryResult Found;
            Found.Capacity = 0x800;
            Found.Count = 0;
            // 062b1073..062b1080: an unsigned zero result or an empty list ends the search.
            if ((unsigned)Bucket->QuerySphere(&Query, &Found) != 0)
            {
                for (unsigned i = 0; i < Found.Count; ++i)
                {
                    // 062b1086..062b10ab: cobject() (slot 0x150) narrowed to a CSolar by
                    // class & 0x303 (0 otherwise). 062b10ab..062b10b1: get_atmosphere_immersion
                    // is called on the narrowed pointer with no null test -- bucket 0xfdff
                    // holds solars only; a non-solar entry faults on [0+0x1ac] as in retail.
                    IObjRW* const Candidate = (IObjRW*)Found.Items[i];
                    CObject const* const Obj = Candidate->cobject();
                    CSolar* const Solar = (Obj != nullptr && (Obj->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar)
                        ? (CSolar*)Obj : nullptr;
                    // 062b10b1..062b10d1: `FCOMP 0.0; TEST AH,0x41; JZ` -> immersion > 0
                    // takes this solar and ends the walk.
                    if (Solar->get_atmosphere_immersion(WorldPosition) > 0.0f)
                    {
                        AtmosphereSolar = Solar;
                        break;
                    }
                }
            }
        }
    }

    // 062b10d7..062b1170: a target that is no longer targetable (IObjInspect slot 0x34
    // is_targetable false) is dropped: when its object is a ship it forgets this targeter
    // (notify_set_target(this, false)), the watcher is cleared (`[+0x220] != 0 ->
    // set_pointer(0)`), and the sub-target becomes SUBOBJ_ID_NONE.
    if (Target.get() != nullptr && !Target.get()->is_targetable())
    {
        if (Target.get() != nullptr)
        {
            CObject* const TargetObj = const_cast<CObject*>(Target.get()->cobject());
            if (TargetObj != nullptr && (TargetObj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
                ((CShip*)TargetObj)->notify_set_target(this, false);
            Target.set(nullptr);
        }
        SubTargetIndex = SUBOBJ_ID_NONE;
    }

    return 0;
}

// ============================================================================
// CShip::open_bay вЂ” opens ship bay doors
// Listing 0x52aa0: increments BayDoorCounter, checks BayState, triggers animation
// ============================================================================

double CShip::open_bay()
{
    // .text:062b2aa0..062b2c0c.
    int const OldCounter = BayDoorCounter;                            // 062b2aac..062b2ab7
    BayDoorCounter = OldCounter + 1;
    if (OldCounter != 0)
    {
        // 062b2bd4..062b2c04: already requested -> GetScriptDuration (slot 0x14) minus the
        // stored duration (slot 0x40, called first and rounded by FSTP dword); the
        // difference is returned off the FPU unrounded. No Anim / handle test.
        float const Stored = DALib::Anim->GetStoredDuration(BayAnim.Index);
        float const Script = DALib::Anim->GetScriptDuration((void*)BayAnim.Index);
        return (double)Script - Stored;
    }

    // 062b2ac3..062b2ad0: only a closing (1) or closed (0) bay opens.
    if (BayState != 1 && BayState != 0)
        return 0.0f;
    if (BayAnim.Index == -1)                                          // 062b2ad6..062b2adf
    {
        BayState = 2;                                                 // 062b2bc5
        return 0.0f;
    }

    BayState = 3;                                                     // 062b2aff
    // 062b2ae5..062b2b17: Anim slot 0x2c EvalPlayback(handle, 0, -2.0f, 1.0f, 0, 1.0f, 1.0f, 0).
    DALib::Anim->EvalPlayback(BayAnim.Index, 0, -2.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                              AsOpaqueDword(1.0f), nullptr);
    AnimDB::Add(BayAnim.Index);                                       // 062b2b21
    // 062b2b26..062b2b63: Script - Stored (Stored rounded first), FSTP dword.
    float const Stored = DALib::Anim->GetStoredDuration(BayAnim.Index);
    float const Script = DALib::Anim->GetScriptDuration((void*)BayAnim.Index);
    float const Result = (float)((double)(Script) - Stored);

    // 062b2b5b..062b2b79: with an INotify (+0x168), notify(2, &bOpen) (vtable +0xc8,
    // CEqObj::notify) with bOpen = true.
    if (Notify != nullptr)
    {
        unsigned char bOpen = 1;
        notify(INotify::BayStateChanged, &bOpen);
    }

    // 062b2b7f..062b2bba: every equipment of type 0x100 (shields) is deactivated (vtable
    // +0x30 Activate, false).
    CEquipTraverser Walk(0x100);
    for (CEquip* Equip = EquipManager.Traverse(Walk); Equip != nullptr;
         Equip = EquipManager.Traverse(Walk))
    {
        Equip->Activate(false);
    }
    return Result;
}

// ============================================================================
// CShip::notify_set_target вЂ” notifies ship about target change
// Listing 0x524d0: checks TypeBits for ship type, updates target tracking
// ============================================================================

void CShip::notify_set_target(CShip* TargetShip, bool bIsTarget)
{
    // .text:062b24d0..062b2633. TargetShip is the ship that started / stopped targeting this
    // one; it is dereferenced without a null test (062b24ee).
    if ((TargetShip->TypeBits & 0x30000) == 0)                        // 062b24ee / 062b2501
        return;
    unsigned const TargeterId = TargetShip->Id;                        // 062b24f8 EBP

    if (bIsTarget)
    {
        // 062b2513..062b253f: the first cleared slot takes the targeter (062b25c1..062b25ce:
        // `Ship + 0x9c != [slot] -> set_pointer`). No duplicate check.
        for (std::vector<FSimpleWatcher>::iterator It = Targeters.begin(); It != Targeters.end(); ++It)
        {
            if (It->get() == nullptr)
            {
                It->watch(TargetShip);
                return;
            }
        }
        // 062b2541..062b25a8: a temporary watcher on Ship + 0x9c appended through
        // FUN_062b54f0 (vector::_Insert_n(_Last, 1, Temp)), then destroyed.
        FSimpleWatcher const Temp(TargetShip);
        Targeters.push_back(Temp);
        return;
    }

    // 062b25e7..062b2620: every live slot whose CSimple Id (+0xb0) equals the targeter's is
    // cleared with set_pointer(0).
    for (std::vector<FSimpleWatcher>::iterator It = Targeters.begin(); It != Targeters.end(); ++It)
    {
        CSimple* const Existing = It->get();
        if (Existing != nullptr && Existing->Id == TargeterId)
            It->set(nullptr);
    }
}

// ============================================================================
// CShip::launch вЂ” launches ship from dock/base
// Listing 0x52a20: creates LaunchAction via Async::Action::operator_new(0x78)
// ============================================================================

void CShip::launch(IObjRW* DockObj, CEqObj* BaseObj, int LaunchType)
{
    // Common.dll.c / raw asm 062b2a20-062b2a95. BUG FIX: the previous port set
    // BayState=1 (PhySys "launching") and BayDoorCounter=0 here, but the raw asm
    // for this function never touches either field (0x2a8/0x2ac) -- those writes
    // had no basis in retail and are removed.
    LaunchAction* Action = new LaunchAction(DockObj, BaseObj, LaunchType);
    Actions.add(Action);
    // Common.dll.c / raw asm 062b2a6a-062b2a80: retail unconditionally calls
    // FUN_062aff80(Action) here (thiscall, receiver = the just-constructed
    // LaunchAction pointer, or nullptr on allocation failure -- ActionDB::add is
    // also called unconditionally with that same possibly-null pointer), the
    // launch action's synchronous "Start" step. Ported as LaunchAction::Start()
    // (common_launchaction.cpp); see that function and the LaunchAction ctor for the
    // resolved field-offset puzzle (DockObjWatcher +0x1c, BaseObj +0x24 -- both
    // shifted 4 bytes by a third LaunchAction base, pub::AI::DirectiveCallback,
    // that a prior pass had missed).
    Action->Start();
}

// Common.dll.c 0xced40 (listing only, no C decompilation): `XOR EAX,EAX; RET 8` -- always
// returns 0, ignores both parameters.
int CShip::get_status(char* p1, int p2) const
{
    (void)p1;
    (void)p2;
    return 0;
}

// ============================================================================
// CShip::is_player вЂ” checks if this ship is player-controlled
// ============================================================================

bool CShip::is_player() const
{
    return bIsPlayer != 0;
}

// ============================================================================
// CShip setter methods вЂ” physics and state setters
// ============================================================================

void CShip::set_throttle(float ThrottleValue)
{
    // .text:062b16c0-062b16f6, RET 0x4: Throttle (+0x29c) = ThrottleValue; the first Engine
    // equip (FindFirst(0x20000)), when present and NOT active (CEquip slot 1, vt+0x4,
    // IsActive), is activated (vt+0x30, Activate(1)).
    Throttle = ThrottleValue;

    CEquip* EngineEquip = const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine));
    if (EngineEquip != nullptr && !EngineEquip->IsActive())
    {
        EngineEquip->Activate(true);
    }
}

void CShip::set_axis_throttle(Vector const& AxisThrottleValue)
{
    // Listing: sets axis throttle vector at [this+0x24c] = Steering+0x1c
    Steering.SetAxisThrottle(AxisThrottleValue);
}

void CShip::set_nudge_vec(Vector const& NudgeValue)
{
    // Listing: sets nudge vector at [this+0x274] = NudgeEngine+0x1c
    NudgeEngine.SetNudgeVec(NudgeValue);
}

void CShip::set_strafe_dir(StrafeDir StrafeDirValue)
{
    // Listing: sets strafe direction enum at [this+0x298] = StrafeEngine+0x18
    StrafeEngine.SetStrafe((StrafeDir)StrafeDirValue);
}

void CShip::set_angular_drag_factor(float Factor)
{
    // Listing 0x27b90: sets angular drag factor at [this+0x2c0]
    AngularDragFactor = Factor;
}

void CShip::set_thrust_power(float Power)
{
    // Listing 0x28250: sets thrust power at [this+0x2a0]
    ThrustPower = Power;
}

void CShip::set_sub_target(unsigned short SubTargetId)
{
    // Listing 0x522f0: sets sub-target index at [this+0x228]
    SubTargetIndex = SubTargetId;
}

void CShip::set_gun_stats_dirty()
{
    // Listing: sets GunStatsDirty flag at [this+0x2c4]
    GunStatsDirty = 1;
}

// ============================================================================
// CShip tradelane methods
// ============================================================================

void CShip::use_tradelane(bool bUseTradelane)
{
    // Listing 0x517a0: simply sets is_using_tradelane flag at [this+0x2bc]
    bUsingTradelane = bUseTradelane ? 1 : 0;
}

void CShip::stop_tradelane()
{
    // Retail 0x517e0. BUG FIX: missing `!= (IAction*)0x10` sentinel guard (see
    // get_tradelane_entrance_ring/exit_ring and request_exit_tradelane for the same check).
    IAction* Lane = Actions.find("LaneAction");
    if (Lane != nullptr && Lane != (IAction*)0x10)
    {
        // IAction is the second base of LaneAction (after PhySys::Controller which is 0x10 bytes).
        LaneAction* LaneAct = static_cast<LaneAction*>(Lane);
        bUsingTradelane = 0; // is_using_tradelane = false
        // BUG FIX: retail (Common.dll.c:83891, FUN_062c2350(1)) calls stop with param2=1,
        // not 0. LaneAction::stop's param2==0 branch does an extra behavior-dispatch vcall
        // (vtable slot 26) on the tradelane target that must NOT run from stop_tradelane.
        LaneAct->stop(1);
    }
}

extern COMMON_API IBehaviorManager* __cdecl GetBehaviorManager(IObjRW* Obj);

void CShip::request_exit_tradelane()
{
    // .text:062b1870..062b195d. ActionDB (+0x2b0) find("LaneAction"); a null IAction, or a
    // null LaneAction after the -0x10 base adjustment, returns.
    // (static_cast of a non-null pointer cannot yield null in C++, so retail's second test
    // -- IAction* == 0x10 -- is spelled out on the address.)
    IAction* const Lane = Actions.find("LaneAction");
    if (Lane == nullptr)
        return;
    if (Lane == (IAction*)0x10)
        return;
    static_cast<LaneAction*>(Lane)->request_exit();   // FUN_062c2300

    // 062b18a2..062b1950: every follower watcher (+0x1f8.. +0x1fc, stride 8) with a live
    // IObjRW whose GetBehaviorManager is non-null: FDUMP the CShip.cpp:0x3c5 warning, then
    // IBehaviorManager::Slot00({4, &payload}) with the case-4 tradelane payload {+0 = 0,
    // +1 = 1, +2 = 0, +4 = 0, +8 = 0, +0xc = this ship's id (+0xb0), +0x10 = 0, +0x14 = 0}
    // (+3 never written).
    for (std::vector<FObjRWWatcher>::const_iterator It = FollowFollowers.begin(); It != FollowFollowers.end(); ++It)
    {
        IObjRW* const FollowerRW = It->get();
        if (FollowerRW == nullptr)
            continue;
        IBehaviorManager* const BehMgr = GetBehaviorManager(FollowerRW);
        if (BehMgr == nullptr)
            continue;

        FBehaviorTradelaneEvent Payload;
        Payload.bEnteringLane = 0;
        Payload.bExitRequested = 1;
        Payload.bFastExit = 0;
        Payload.Speed = 0.0f;
        Payload.Field_0x08 = 0;
        Payload.ShipId = Id;
        Payload.EntranceId = 0;
        Payload.ExitId = 0;
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", "E:\\FL\\Scratch\\Source\\Common\\CShip.cpp", 0x3c5,
              "request_exit_tradelane");

        FBehaviorManagerEvent Event;
        Event.Type = 4;
        Event.Data = &Payload;
        BehMgr->Slot00(&Event);
    }
}

void CShip::set_tradelane_speed(float Speed)
{
    // Listing: sets tradelane speed at [this+0x2b8]
    TradelaneSpeed = Speed;
}

Vector const& CShip::get_follow_offset()
{
    // .text:062b4f00: LEA EAX,[this+0x1e8] / RET.
    return FollowOffset;
}

void CShip::set_follow_offset(Vector const& Offset)
{
    // Listing 0x54ec0: copies 3 dwords (Vector) to [this+0x1e8]
    FollowOffset.x = Offset.x;
    FollowOffset.y = Offset.y;
    FollowOffset.z = Offset.z;
}

// ============================================================================
// CShip formation methods
// ============================================================================

void CShip::set_follow_leader(IObjRW* Leader)
{
    // .text:062b4e90..062b4eb2: W = Leader ? Leader + 8 : 0; W != [+0x1e0] -> tail jump to
    // BaseWatcher::set_pointer(W).
    FollowLeader.watch(Leader);
}

FORMATION_RTYPE CShip::remove_follow_follower(IObjRW* Follower)
{
    // .text:062b4e20..062b4e8c: a null Follower or no slot whose live owner has the same
    // get_id() -> NOT_FOUND; the matching slot is cleared (`[slot] != 0 -> set_pointer(0)`),
    // never erased.
    if (Follower != nullptr)
    {
        for (std::vector<FObjRWWatcher>::iterator It = FollowFollowers.begin();
             It != FollowFollowers.end(); ++It)
        {
            IObjRW* const Existing = It->get();
            if (Existing != nullptr && Existing->get_id() == Follower->get_id())
            {
                It->set(nullptr);
                return FORMATION_RTYPE_OK;
            }
        }
    }
    return FORMATION_RTYPE_NOT_FOUND;
}

FORMATION_RTYPE CShip::remove_formation_follower(IObjRW* Follower)
{
    // .text:062b49b0..062b4a7e: as remove_follow_follower on FormationFollowers; after the
    // slot is cleared the recalculate_formation_speed body runs inline (062b4a16..062b4a72).
    if (Follower != nullptr)
    {
        for (std::vector<FObjRWWatcher>::iterator It = FormationFollowers.begin();
             It != FormationFollowers.end(); ++It)
        {
            IObjRW* const Existing = It->get();
            if (Existing != nullptr && Existing->get_id() == Follower->get_id())
            {
                It->set(nullptr);
                recalculate_formation_speed();
                return FORMATION_RTYPE_OK;
            }
        }
    }
    return FORMATION_RTYPE_NOT_FOUND;
}

void CShip::recalculate_formation_speed()
{
    // .text:062b4790..062b4809. Min starts at 0x4cbebc20 (1.0e8f). Every live follower's
    // IObjInspect::get_speed(1.0f, 1) (slot 0xd8, any object class) is compared:
    // `FLD Min; FCOMP Speed; TEST AH,5; JNP keep` -- JNP is taken only for C0 alone, i.e.
    // Min < Speed keeps Min; Min >= Speed or unordered stores Speed (FSTP dword).
    float MinSpeed = 1.0e8f;
    for (std::vector<FObjRWWatcher>::const_iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower != nullptr)
        {
            float const Speed = Follower->get_speed(1.0f, IObject::ThrustEquipEngine);
            if (!(MinSpeed < Speed))
                MinSpeed = Speed;
        }
    }
    FormationSpeed = MinSpeed;
}

namespace
{
    // CEScanner::Scans.Entries stores real BaseWatcher nodes (common_cequip.h); this
    // recovers the raw Watchable* a slot currently watches as a void*, matching the
    // "+8 back to CObject" pointer arithmetic these CShip target-cycling methods use.
    void* ScanEntryWatched(BaseWatcher const& Entry)
    {
        return (void*)Entry.target();
    }

    // `W = [entry]; W != 0 -> W - 8`: the IObjRW a scanner slot watches (its Watchable is
    // IObjRW's third base, at +8), or null.
    IObjRW* ScanEntryOwner(BaseWatcher const& Entry)
    {
        return ObjRwFromWatchable(ScanEntryWatched(Entry));
    }

    // The candidate test next_enemy / previous_enemy share (.text:062b1d47..062b1db0 and
    // its copies): the object's cobject() (slot 0x150) is a CEqObj (class & 0x103), and
    // Reputation::Vibe::AttitudeTowards(its vibe +0x104, the ship's vibe) succeeds with an
    // attitude <= -0.6f (DAT_0639e548; `FCOMP; TEST AH,0x41; JP` skips greater and unordered).
    bool IsHostileEqObj(IObjRW* Object, int MyVibe)
    {
        CObject const* const Obj = Object->cobject();
        if (Obj == nullptr || (Obj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
            return false;
        int const TheirVibe = ((CEqObj const*)Obj)->VibeId;
        int const OwnVibe = MyVibe;
        float Attitude = 0.0f;
        if (Reputation::Vibe::AttitudeTowards(TheirVibe, OwnVibe, Attitude) != 0)
            return false;
        return Attitude <= -0.6f;
    }

    // Each difference rounded by FSTP dword into a Vector (FUN_0628b030 = Vector(x, y, z)),
    // then FSQRT(y*y + z*z + x*x) left on the FPU.
    double RoundedDistance(Vector const& To, Vector const& From)
    {
        float const X = (float)((double)(To.x) - From.x);
        float const Y = (float)((double)(To.y) - From.y);
        float const Z = (float)((double)(To.z) - From.z);
        double const Sum = (double)(Y) * Y + (double)(Z) * Z +
                           (double)(X) * X;
        return sqrt(Sum);
    }

    // The second passes keep the differences on the FPU: FSQRT((dx*dx + dz*dz) + dy*dy).
    double UnroundedDistance(Vector const& To, Vector const& From)
    {
        double const Dx = (double)(To.x) - From.x;
        double const Dy = (double)(To.y) - From.y;
        double const Dz = (double)(To.z) - From.z;
        return sqrt((Dx * Dx + Dz * Dz) + Dy * Dy);
    }
}

IObjRW* CShip::next_enemy()
{
    // .text:062b1bf0..062b1f63. Distances are measured from this ship's WorldPosition
    // (+0x2c..+0x34). Floats: 1e9 = 0x4e6e6b28, -1 = 0xbf800000.
    IObjRW* Best = nullptr;                                           // [ESP+0x10]
    float MinAbove = 1.0e9f;                                          // [ESP+0x1c]
    float CurrentDist = -1.0f;                                        // [ESP+0x18]

    // 062b1bf9..062b1cf9: the current target's distance, when it is a hostile CEqObj.
    IObjRW* const TargetObj = Target.get();
    if (TargetObj != nullptr && IsHostileEqObj(TargetObj, VibeId))
        CurrentDist = (float)(RoundedDistance(Target.get()->get_position(), WorldPosition));

    // 062b1cfd..062b1d29. A missing scanner (062b1d15 JZ) skips the first pass only.
    CEScanner const* const Scanner =
        (CEScanner const*)EquipManager.FindFirst(EquipTypeScanner);

    // 062b1d1b..062b1e48: the nearest hostile, live (is_dying() slot 0x38 false) scanned
    // object farther away than the current target. `FCOM MinAbove; TEST AH,5; JP` needs
    // Dist < MinAbove; `FCOM CurrentDist; TEST AH,0x41; JNZ` needs Dist > CurrentDist.
    if (Scanner != nullptr)
    {
        for (int i = 0; i < Scanner->Scans.Count; ++i)
        {
            IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[i]);
            if (Candidate == nullptr || !IsHostileEqObj(Candidate, VibeId) || Candidate->is_dying())
                continue;
            double const Dist = RoundedDistance(Candidate->get_position(), WorldPosition);
            if (Dist < MinAbove && Dist > CurrentDist)
            {
                MinAbove = (float)Dist;
                Best = Candidate;
            }
        }
        if (Best != nullptr)
            return Best;
    }

    // 062b1e4e..062b1f4e: otherwise the nearest hostile, live scanned object at all, the
    // distance taken from unrounded differences (`FCOM Nearest; TEST AH,5; JP` -> Dist < Nearest).
    // Reached with a null scanner too: retail then reads its +0x824 (062b1e4e) and faults.
    float Nearest = 1.0e9f;                                           // [ESP+0x20]
    for (int i = 0; i < Scanner->Scans.Count; ++i)
    {
        IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[i]);
        if (Candidate == nullptr || !IsHostileEqObj(Candidate, VibeId) || Candidate->is_dying())
            continue;
        double const Dist = UnroundedDistance(Candidate->get_position(), WorldPosition);
        if (Dist < Nearest)
        {
            Nearest = (float)(Dist);
            Best = Candidate;
        }
    }
    return Best;
}

IObjRW* CShip::previous_enemy()
{
    // .text:062b1f70..062b22e3: next_enemy (062b1bf0) mirrored.
    IObjRW* Best = nullptr;                                           // [ESP+0x10]
    float MaxBelow = -1.0f;                                           // [ESP+0x1c]
    float CurrentDist = 1.0e9f;                                       // [ESP+0x18]

    // 062b1f79..062b2079: the current target's distance, when it is a hostile CEqObj.
    IObjRW* const TargetObj = Target.get();
    if (TargetObj != nullptr && IsHostileEqObj(TargetObj, VibeId))
        CurrentDist = (float)(RoundedDistance(Target.get()->get_position(), WorldPosition));

    // 062b207d..062b20a9: a missing scanner (062b2095 JZ) skips the first pass only.
    CEScanner const* const Scanner =
        (CEScanner const*)EquipManager.FindFirst(EquipTypeScanner);

    // 062b20b0..062b21c8: the farthest hostile, live scanned object nearer than the current
    // target. `FCOM MaxBelow; TEST AH,0x41; JNZ` needs Dist > MaxBelow; `FCOM CurrentDist;
    // TEST AH,5; JP` needs Dist < CurrentDist.
    if (Scanner != nullptr)
    {
        for (int i = 0; i < Scanner->Scans.Count; ++i)
        {
            IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[i]);
            if (Candidate == nullptr || !IsHostileEqObj(Candidate, VibeId) || Candidate->is_dying())
                continue;
            double const Dist = RoundedDistance(Candidate->get_position(), WorldPosition);
            if (Dist > MaxBelow && Dist < CurrentDist)
            {
                MaxBelow = (float)Dist;
                Best = Candidate;
            }
        }
        if (Best != nullptr)
            return Best;
    }

    // 062b21ce..062b22ce: otherwise the farthest hostile, live scanned object at all,
    // unrounded differences (`FCOM Farthest; TEST AH,0x41; JNZ` -> Dist > Farthest).
    // Reached with a null scanner too: retail then reads its +0x824 (062b21ce) and faults.
    float Farthest = -1.0f;                                           // [ESP+0x20]
    for (int i = 0; i < Scanner->Scans.Count; ++i)
    {
        IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[i]);
        if (Candidate == nullptr || !IsHostileEqObj(Candidate, VibeId) || Candidate->is_dying())
            continue;
        double const Dist = UnroundedDistance(Candidate->get_position(), WorldPosition);
        if (Dist > Farthest)
        {
            Farthest = (float)(Dist);
            Best = Candidate;
        }
    }
    return Best;
}

IObjRW* CShip::next_target(bool bReverse)
{
    // .text:062b1a40..062b1b66. The scanner is EquipManager.FindFirst(0x80000) used as a
    // CEScanner without a cast check (the type filter is the find itself).
    CEScanner const* const Scanner =
        (CEScanner const*)EquipManager.FindFirst(EquipTypeScanner);
    if (Scanner == nullptr)                                           // 062b1a59
        return nullptr;
    int const Count = Scanner->Scans.Count;                           // +0x824
    if (Count < 1)                                                    // 062b1a67 JL
        return nullptr;

    // 062b1a70..062b1aba: the slot that holds the current target's owner, -1 if none.
    int Current = -1;
    if (Target.get() != nullptr)
    {
        for (int i = 0; i < Count; ++i)
        {
            if (Target.get() == ScanEntryOwner(Scanner->Scans.Entries[i]))
            {
                Current = i;
                break;
            }
        }
    }

    // 062b1abe..062b1b4a: Count steps of +1 (bReverse false) or -1, wrapping below 0 to
    // Count - 1 and at Count to 0; the current slot, an empty slot and a slot whose object
    // has TypeBits 0 or 0x20000000 are passed over (the loop goes on, it does not stop).
    int const Step = bReverse ? -1 : 1;
    int Index = Current;
    for (int Steps = 0; Steps < Count; ++Steps)
    {
        Index += Step;
        if (Index < 0)
            Index = Count - 1;
        else if (Index >= Count)
            Index = 0;
        if (Index < 0 || Index >= Count || Index == Current)
            continue;

        IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[Index]);
        if (Candidate == nullptr)
            continue;
        // 062b1b06..062b1b45: cobject() (slot 0x150) narrowed by class & 3 to a CSimple, and
        // its TypeBits (+0xe0), read from the narrowed pointer with no null test: a slot whose
        // object is not a CSimple faults in retail (062b1b34 XOR EAX,EAX; 062b1b36 [EAX+0xe0]).
        CObject const* const Obj = Candidate->cobject();
        CSimple const* const Simple = (Obj != nullptr && (Obj->ObjectClass & 3) == 3)
            ? (CSimple const*)Obj : nullptr;
        if (Simple->TypeBits == 0 || Simple->TypeBits == 0x20000000)
            continue;
        // 062b1b56..062b1b5f: the slot is read once more; W - 8 returned.
        return ScanEntryOwner(Scanner->Scans.Entries[Index]);
    }
    return nullptr;
}

bool CShip::leaving_system_update(unsigned& SystemId)
{
    // .text:062b4b10..062b4bec. SystemId names the object that leaves; every slot of the three
    // vectors watching it is cleared and the result says whether any matched.
    bool bFound = false;

    // 062b4b1a..062b4b62: formation followers by IObjInspect::get_id() (slot 0x20). BL is set
    // on a match before the `[slot] != 0 -> set_pointer(0)` release.
    for (std::vector<FObjRWWatcher>::iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower != nullptr && Follower->get_id() == SystemId)
        {
            bFound = true;
            It->set(nullptr);
        }
    }

    // 062b4b64..062b4ba4: follow followers, the same test.
    for (std::vector<FObjRWWatcher>::iterator It = FollowFollowers.begin();
         It != FollowFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower != nullptr && Follower->get_id() == SystemId)
        {
            It->set(nullptr);
            bFound = true;
        }
    }

    // 062b4ba6..062b4be4: targeters by the CSimple's Id (+0xb0), released with an
    // unconditional set_pointer(0) on the live slot.
    for (std::vector<FSimpleWatcher>::iterator It = Targeters.begin(); It != Targeters.end(); ++It)
    {
        CSimple* const Targeter = It->get();
        if (Targeter != nullptr && Targeter->Id == SystemId)
        {
            It->set(nullptr);
            bFound = true;
        }
    }

    return bFound;
}

Archetype::Ship const* CShip::shiparch() const
{
    // 0x06262d50: `MOV EAX,[this+0x88]; RET` (ICF fold). No null check in retail.
    return (Archetype::Ship const*)Archetype;
}

// JETTISON_CLEARANCE_TIME constant (retail DAT_0639e574 = 4.0, not 5.0)
float const CShip::JETTISON_CLEARANCE_TIME = 4.0f;

// ============================================================================
// CShip::get_renderable_subtarget вЂ” returns renderable subtarget index
// Listing 0x52310: reads Target at +0x220, then subtarget index at +0x228
// ============================================================================

long CShip::get_renderable_subtarget() const
{
    // .text:062b2310..062b23b4: no target, SubTargetIndex == SUBOBJ_ID_NONE, or a target
    // object (slot 0x150 cobject) that is not a CEqObj (class & 0x103) -> -1. Otherwise
    // cobject() again (same watcher, same call) and its CEqObj::sub_obj_id_to_inst (vtable
    // +0xf8) of the sub-target.
    IObjRW* const TargetObj = Target.get();
    if (TargetObj == nullptr || SubTargetIndex == SUBOBJ_ID_NONE)
        return -1;
    CObject const* const Obj = TargetObj->cobject();
    if (Obj == nullptr || (Obj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -1;
    CObject const* const EqObj = Target.get()->cobject();
    CEqObj const* const Eq = (EqObj != nullptr && (EqObj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
        ? (CEqObj const*)EqObj : nullptr;
    return Eq->sub_obj_id_to_inst(SubTargetIndex);
}
