// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_lootbehavior.h"

#include "common_aisteer.h"        // FAiTarget
#include "common_archetype.h"      // Archetype::Ship::HoldSize, Archetype::Root::get_class_type
#include "common_cequip.h"         // ScanList
#include "common_cobject.h"        // CObject, CShip, CLoot, CEqObj::get_behavior_interface
#include "common_iface.h"          // IObjInspect, IObjAffect, IObjRW, IBehaviorManager, EquipmentVal
#include "common_misc.h"           // SUBOBJ_ID_NONE
#include "common_systemmanager.h"  // SystemManager -- what NavHelper points at

#include <cstring>

// LootBehavior -- see common_lootbehavior.h. Every method re-ported from
// EXE/Disasm/Common.dll.txt 2026-10-08 (claude-code:cmn-ai5).

// FUN_062e74a0 (item 1798) is SystemManagerTimerTable::FindVerifiedObject.

namespace
{
    IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // `MOV EAX,[Sys+8]; ... LEA ECX,[EAX-8]` -- the owner ship, null arm included.
    IObjRW* OwnShip(SystemManager* Sys)
    {
        return WatchedObject(Sys->Watcher);
    }

    // The inlined `cobject(); (+0x4c & Mask) == Mask ? obj : 0` filter.
    CObject const* CObjectOfClass(IObjInspect const* Obj, unsigned Mask)
    {
        CObject const* const C = Obj->cobject();
        if (C != nullptr && (static_cast<unsigned>(C->ObjectClass) & Mask) == Mask)
            return C;
        return nullptr;
    }

    // The cargo-hold check inlined at .text:062ce8a4-062ce8fe and 062ceb9e-062cebf8: the own
    // ship's CShip (null when it is not one -- retail then faults on [ECX+0x88]); with a
    // positive hold the free fraction, else 0.0; true when that exceeds 0.2 (double
    // .rdata:0639fa50, `TEST AH,0x41; JZ`).
    bool OwnHoldHasRoom(SystemManager* Sys)
    {
        CShip const* const Ship = static_cast<CShip const*>(CObjectOfClass(OwnShip(Sys), CObject::ClassShip));
        Archetype::Ship const* const Arch = static_cast<Archetype::Ship const*>(Ship->Archetype);
        double Fraction = 0.0;                                      // FLD DAT_06399408
        if (static_cast<double>(Arch->HoldSize) > 0.0)              // FCOMP double 0.0; `TEST AH,0x41; JNZ`
            Fraction = static_cast<double>(Ship->get_cargo_hold_remaining()) / Arch->HoldSize;
        return Fraction > 0.2;
    }

    // The 0x5f3759df distance at .text:062cea9b-062cead9 / 062cec90-062cecd0: (dz*dz + dx*dx)
    // + dy*dy on the x87 stack, FST for the bit pattern, DistSq * s * (1.5 - 0.5 * s*s*DistSq).
    double FastDistance(Vector const& A, Vector const& B)
    {
        double const Dx = static_cast<double>(A.x) - B.x;          // FLD; FSUB -- kept on the stack
        double const Dy = static_cast<double>(A.y) - B.y;
        double const Dz = static_cast<double>(A.z) - B.z;
        double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
        float const DistSqF = static_cast<float>(DistSq);
        int Bits;
        std::memcpy(&Bits, &DistSqF, sizeof(Bits));
        int const SeedBits = 0x5f3759df - (Bits >> 1);
        float Seed;
        std::memcpy(&Seed, &SeedBits, sizeof(Seed));
        return ((1.5f - ((static_cast<double>(Seed) * Seed) * DistSq) * 0.5f) * Seed) * DistSq;
    }

    // FUN_062ecd90 (.text:062ecd90-062ece39, RET 4): `this` is the SystemManager+0x1498
    // tractor record {Owner1498, Watcher149C, Unknown14A4 = tractor equipment id}. Point the
    // tractor at Target through the owner ship's IObjAffect::slot_40(&Id, Target, -1).
    bool TractorActivate(SystemManager* Sys, IObjRW* Target)
    {
        if (Target == nullptr)                                      // 062ecd98
            return false;
        unsigned short const* const Id = reinterpret_cast<unsigned short const*>(&Sys->Unknown14A4);
        if (*Id == SUBOBJ_ID_NONE)                                  // 062ecda6
            return false;
        IObjRW* const Current = WatchedObject(Sys->Watcher149C);
        if (Current != nullptr && Current->get_id() == Target->get_id())   // 062ecdc7-062ecde4
            return false;
        Sys->Watcher149C.set(reinterpret_cast<Watchable const*>(reinterpret_cast<char*>(Target) + 8));   // 062ecdea
        IObjRW* const Owner = WatchedObject(Sys->Owner1498->Watcher);      // 062ece08-062ece1a
        int const Result = static_cast<IObjAffect*>(Owner)->slot_40(Id, WatchedObject(Sys->Watcher149C), -1);
        return Result == 0;                                         // 062ece26
    }

    // FUN_062ece40 (.text:062ece40-062ecea3, RET 0): release the tractor through
    // IObjAffect::slot_4c(&Id, false, -1); true when that returned 0 (NEG/SBB/INC).
    bool TractorRelease(SystemManager* Sys)
    {
        if (WatchedObject(Sys->Watcher149C) == nullptr)             // 062ece43-062ece53
            return false;
        unsigned short const* const Id = reinterpret_cast<unsigned short const*>(&Sys->Unknown14A4);
        if (*Id == SUBOBJ_ID_NONE)                                  // 062ece5a
            return false;
        Sys->Watcher149C.set(nullptr);                              // 062ece6f-062ece76
        IObjRW* const Owner = WatchedObject(Sys->Owner1498->Watcher);
        return static_cast<IObjAffect*>(Owner)->slot_4c(Id, false, -1) == 0;   // 062ece98
    }
}

// ----------------------------------------------------------------------------
// FUN_062ce400 (.text:062ce400-062ce48f).
// ----------------------------------------------------------------------------
LootBehavior::LootBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContext(nullptr)
    , Opaque18{}
    , DeltaTime(0.0f)
    , BehaviorMgrPtr(nullptr)
    , NavHelper(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , Move()                        // 062ce431 FUN_062d4e60
    , Face()                        // 062ce441 FUN_062d6750
    , bHasTractor164(0)             // not written by retail's ctor; SetContext writes it
    , Pad165{}
    , TractorReach168(0.0f)         // not written by retail's ctor; SetContext writes it
    , Flag16c(1)                    // 062ce461
    , Pad16d{}
    , Field170(13.0f)               // 062ce457 0x41500000
    , Target{}                      // 062ce446-062ce44e watcher zeroed, 062ce47b bDirected = 0
{
    ConstructBase(InTypeId);        // 062ce423 FUN_062d2020
}

// FUN_062ce4c0 (FUN_062ce4a0 is the scalar-deleting thunk).
LootBehavior::~LootBehavior()
{
}

// ----------------------------------------------------------------------------
// Slot 1, LAB_062ce550 (.text:062ce550-062ce653).
// ----------------------------------------------------------------------------
bool LootBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bOk = BindContext(Owner, Manager, Sys);              // 062ce565 FUN_062d2090
    if (bOk)
    {
        Move.SetContext(OwnerContext, NavHelper, Manager);          // 062ce581
        Face.SetContext(OwnerContext, NavHelper, Manager);          // 062ce598
    }

    // LAB_062ce59a -- runs whether or not the binder accepted the context.
    Target.Watcher.set(nullptr);                                    // 062ce59a-062ce5ac
    Target.bDirected = 0;                                           // 062ce5b4
    bHasTractor164 = 0;                                             // 062ce5bb
    TractorReach168 = 0.0f;                                         // 062ce5c2

    // 062ce5cc-062ce649: retail reuses its own argument slots: the second one becomes the
    // equipment id (seeded SUBOBJ_ID_NONE) and the first the EquipmentVal value in front of
    // it, i.e. one EquipmentVal{Value, EquipId}.
    EquipmentVal Tractor;
    Tractor.Value = 0.0f;
    Tractor.EquipId = SUBOBJ_ID_NONE;
    Tractor.Pad = 0;
    IObjRW* const Ship = OwnShip(NavHelper);
    if (Ship->find_equipment(&Tractor.EquipId, 1, 0x100000) == 0)  // [+0x114], tractor class
    {
        bHasTractor164 = 1;                                         // 062ce606
        if (OwnShip(NavHelper)->get_equipment_val(Tractor, Tractor.EquipId,
                                                  static_cast<EquipmentValType>(5), 0.0f) == 0)   // [+0x11c]
        {
            // 062ce638-062ce643: FMUL double 0.75 (.rdata:0639fe28), FSTP float.
            TractorReach168 = static_cast<float>(static_cast<double>(Tractor.Value) * 0.75);
        }
    }
    return bOk;
}

// ----------------------------------------------------------------------------
// Slot 3, LAB_062cd5d0: `JMP FUN_062d20d0`.
// ----------------------------------------------------------------------------
void LootBehavior::Slot3()
{
    Behavior::Slot3();
}

// ----------------------------------------------------------------------------
// Slot 4, FUN_062ce660 (.text:062ce660-062ce6aa).
// ----------------------------------------------------------------------------
void LootBehavior::Slot4()
{
    Target.Watcher.set(nullptr);                                    // 062ce663-062ce675
    Target.bDirected = 0;                                           // 062ce680
    Move.Commit();                                                  // 062ce687
    Face.Commit();                                                  // 062ce696
    (void)TractorRelease(NavHelper);                                // 062ce6a2 FUN_062ece40(Sys+0x1498)
    Behavior::Slot4();                                              // 062ce6aa JMP FUN_062d20f0
}

// ----------------------------------------------------------------------------
// Slot 5, FUN_062ce6b0 (.text:062ce6b0-062ce73a).
// ----------------------------------------------------------------------------
int LootBehavior::Slot5(float Unused)
{
    (void)Unused;
    // 062ce6b3-062ce6c8: only with a tractor and the TargetManager in attack state 2.
    if (bHasTractor164 != 0 && NavHelper->Targeting.AttackState == 2)
    {
        if (WatchedObject(Target.Watcher) == nullptr)               // 062ce6ca-062ce6df
            (void)AcquireLootTarget(&Target);                       // 062ce6e4 FUN_062ce870
        if (WatchedObject(Target.Watcher) != nullptr)               // 062ce6e9-062ce6f4
        {
            ModeSelector0xc = Flag29 ? 3 : 0;                       // 062ce6f6-062ce70b
            return ModeSelector0xc;
        }
    }
    ModeSelector0xc = Flag29 ? 3 : 2;                               // LAB_062ce718
    return ModeSelector0xc;
}

// ----------------------------------------------------------------------------
// FUN_062ce740 (.text:062ce740-062ce85e, RET 4).
// ----------------------------------------------------------------------------
bool LootBehavior::IsLootUnclaimed(IObjRW* Loot)
{
    // 062ce743-062ce776: the loot's CLoot (null when it is not one -- retail then reads
    // [0+0xf0]); more than one unit is never contested.
    CLoot const* const LootObj = static_cast<CLoot const*>(CObjectOfClass(Loot, CObject::ClassLoot));
    if (LootObj->Units > 1)                                         // `CMP [EAX+0xf0],1; JA`
        return true;

    // 062ce77c-062ce79b: every ship-class object in the owner's scan list (FUN_062e74e0).
    ScanList const* List = nullptr;
    (void)NavHelper->Timers.GetOwnScanList(List, 0x7f0000);
    if (List == nullptr)
        return false;

    for (int Index = 0; static_cast<unsigned>(Index) < static_cast<unsigned>(List->Count); ++Index)   // `JBE` / `JC`
    {
        IObjRW* const Other = WatchedObject(List->Entries[Index]);
        if (Other == OwnShip(NavHelper))                            // 062ce7c1-062ce7d6
            continue;
        unsigned OtherType = 0;                                     // not pre-set in retail
        Other->get_type(OtherType);                                 // 062ce7e1 [+0x88], result unread
        if ((OtherType & 0x7f0000u) == 0)
            continue;
        CObject const* const OtherShip = CObjectOfClass(Other, CObject::ClassShip);   // 062ce7f1-062ce80e
        if (OtherShip == nullptr)
            continue;
        IBehaviorManager* const Manager =
            const_cast<CEqObj*>(static_cast<CEqObj const*>(OtherShip))->get_behavior_interface();   // 062ce812
        if (Manager == nullptr)
            continue;
        if (Manager->CurrentBehaviorIndex != 0x10)                  // 062ce81b: currently looting
            continue;
        // 062ce824-062ce83e: its LootBehavior (dispatch slot 16, +0x1b4) chases this loot.
        LootBehavior const* const OtherLoot = static_cast<LootBehavior const*>(Manager->BehaviorDispatchTable[16]);
        if (WatchedObject(OtherLoot->Target.Watcher) == Loot)
            return false;
    }
    return true;                                                    // LAB_062ce857
}

// ----------------------------------------------------------------------------
// FUN_062ce870 (.text:062ce870-062ceb40, RET 4).
// ----------------------------------------------------------------------------
bool LootBehavior::AcquireLootTarget(FLootTarget* Slot)
{
    SystemManager* const Sys = NavHelper;
    pub::AI::Personality::JobStruct const& Job = Sys->PersonalityData.JobParams;
    if (Job.LootPreference == 0)                                    // 062ce87c [Sys+0x18fc]: loot flags
        return false;
    if (!OwnHoldHasRoom(Sys))                                       // 062ce896-062ce8fe
        return false;

    // LAB_062ce90c: the own position (ExternalIface transform slot 4, +0x24 = Position).
    Vector const& OwnPos = Sys->SelfTarget.Iface->get_transform().Translation;   // [+0x10] +0x24

    IObjRW* Best = nullptr;                                         // [ESP+0x20]
    float BestDistance = 3.40282347e+38f;                           // 0x7f7fffff
    ScanList const* List = nullptr;
    (void)Sys->Timers.GetOwnScanList(List, 0x10000000);   // 062ce948, loot class
    if (List == nullptr)
        return false;                                               // 062ce953

    for (int Index = 0; static_cast<unsigned>(Index) < static_cast<unsigned>(List->Count); ++Index)
    {
        IObjRW* const Candidate = WatchedObject(List->Entries[Index]);
        unsigned CandidateType = 0;                                 // not pre-set in retail
        Candidate->get_type(CandidateType);                         // 062ce98c [+0x88]
        if ((CandidateType & 0x10000000u) == 0)
            continue;

        CLoot const* const Loot = static_cast<CLoot const*>(CObjectOfClass(Candidate, CObject::ClassLoot));   // 062ce9a0-062ce9c3
        if (Loot->bCanAiTractor == 0)                               // 062ce9c5 [+0xfc]
            break;

        int const Kind = static_cast<int>(Loot->ContentsArch->get_class_type());   // 062ce9db [+0x10]
        if (Kind == 0xa)
        {
            // 062ce9e3-062cea3c: commodities need loot flag bit 0 and, when the personality
            // lists goods (+0x1904 count, +0x1908 ids), the candidate's good among them.
            if ((Job.LootPreference & 1) == 0)
                break;
            int const ListCount = Job.ScanForCargoCount;
            if (ListCount > 0)
            {
                unsigned GoodId = 0;                                // 062cea06
                if (Candidate->get_good_id(GoodId) == 0)            // [+0x24]
                {
                    int GoodIndex = 0;
                    while (Job.ScanForCargo[GoodIndex] != GoodId)   // LAB_062cea30
                    {
                        ++GoodIndex;
                        if (GoodIndex >= ListCount)
                            goto Done;                              // 062cea3c JMP LAB_062ceb0f
                    }
                }
            }
        }
        else if (Kind == 0x1a || Kind == 0x1b)
        {
            if ((Job.LootPreference & 4) == 0)                      // LAB_062cea4b
                break;
        }

        // LAB_062cea61.
        if (Loot->Units == 0)                                       // [+0xf0]
            break;
        if (!IsLootUnclaimed(Candidate))                            // 062cea72 FUN_062ce740
            continue;

        // 062cea7b-062ceaf0: the nearest wins (`FCOM best; TEST AH,0x5; JP` keeps >=).
        Vector const& LootPos = Candidate->get_transform().Translation;   // [+0x10] +0x24
        double const Distance = FastDistance(LootPos, OwnPos);
        if (Distance < BestDistance)
        {
            BestDistance = static_cast<float>(Distance);
            Best = Candidate;
        }
    }

Done:
    // LAB_062ceb0f: the slot watches the winner (or nothing), undirected.
    Slot->Watcher.set(Best != nullptr ? reinterpret_cast<Watchable const*>(reinterpret_cast<char*>(Best) + 8)
                                      : nullptr);
    Slot->bDirected = 0;                                            // 062ceb2f
    return Best != nullptr;
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062ceb50 (.text:062ceb50-062cedc7).
// ----------------------------------------------------------------------------
int LootBehavior::Slot7()
{
    if (OwnerContext == nullptr)                                    // 062ceb56
    {
        UpdateResult10 = 3;
        return 3;
    }
    IObjRW* const Loot = WatchedObject(Target.Watcher);             // 062ceb6c-062ceb85 EBX
    if (Loot == nullptr)
    {
        UpdateResult10 = 2;                                         // LAB_062cec0d
        return 2;
    }

    SystemManager* const Sys = NavHelper;
    if (!OwnHoldHasRoom(Sys))                                       // 062ceb8b-062cebf8
    {
        Target.Watcher.set(nullptr);                                // 062cebfa-062cec04
        Target.bDirected = 0;
        UpdateResult10 = 2;
        return 2;
    }

    // LAB_062cec1b: the FAiTarget type refresh (get_type, then +0x7d = 1).
    FAiTarget* const Nav = &Sys->SelfTarget;
    if (Nav->bFlagsValid == 0)
    {
        Nav->Iface->get_type(Nav->CachedFlags);
        Nav->bFlagsValid = 1;
    }
    // 062cec44-062cec53: `AND ECX,0x10000; OR ECX,0x20000; JNZ LAB_062ced2c` -- always taken.
    // The fall-through at 062cec59-062ced2a (a FastDistance gate on TractorReach168, then
    // Move.Commit and FUN_062ecd90, or the release exit) is unreachable in retail and is
    // written below behind the same constant test.
    if ((((Nav->CachedFlags & 0x10000u) | 0x20000u)) == 0)
    {
        IObjRW* const Ship = OwnShip(Sys);
        double const Distance = FastDistance(Ship->get_position(), Loot->get_position());   // [EBP] / [EBX] slot 0
        if (Distance < TractorReach168)                             // `TEST AH,0x5; JP`
        {
            Move.Commit();
            if (!TractorActivate(Sys, Loot))
            {
                UpdateResult10 = 1;                                 // LAB_062cedba
                return 1;
            }
        }
        Target.Watcher.set(nullptr);                                // LAB_062ced01
        Target.bDirected = 0;
        UpdateResult10 = 2;
        return 2;
    }

    // LAB_062ced2c: fly within tractor reach, then face it and tractor it in.
    Move.SetWatchObject(Loot);                                      // 062ced32 FUN_062d5760
    Move.SetEnabled(true);                                          // 062ced3b FUN_062d5a10
    Move.SetRange3c(TractorReach168);                               // 062ced49 .text:062a66b0
    if (Move.Update(DeltaTime) == 1)                                // 062ced56
    {
        Move.Commit();                                              // 062ced62
        Face.SetWatchObject(Loot);                                  // 062ced6e FUN_062d7510
        if (Face.Update(DeltaTime) == 1 && TractorActivate(Sys, Loot))   // 062ced7b / 062ced8d
            return 2;                                               // 062ced99: +0x10 not written
    }
    else
    {
        Face.SetWatchObject(Loot);                                  // LAB_062ceda1
        Face.Update(DeltaTime);
    }
    UpdateResult10 = 1;                                             // LAB_062cedba
    return 1;
}

// ----------------------------------------------------------------------------
// Slot 10, FUN_062cedd0 (.text:062cedd0-062cee3f).
// ----------------------------------------------------------------------------
bool LootBehavior::Slot10(void* DirectiveOp)
{
    unsigned char* const Op = static_cast<unsigned char*>(DirectiveOp);
    void* const Found = NavHelper->Timers.FindVerifiedObject((unsigned const*)(Op + 0xc));   // 062cede3
    Target.Watcher.set(Found != nullptr ? reinterpret_cast<Watchable const*>(static_cast<char*>(Found) + 8)
                                        : nullptr);                 // 062cedf9-062cee00
    if (WatchedObject(Target.Watcher) != nullptr)                   // 062cee05-062cee10
    {
        Target.bDirected = 1;                                       // 062cee1b
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        return true;
    }
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);           // LAB_062cee29
    Flag16c = Op[0x10];                                             // 062cee35
    return false;
}
