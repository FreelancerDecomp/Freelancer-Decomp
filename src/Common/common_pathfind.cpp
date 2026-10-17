// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- PathfindManager (common_pathfind.h): the per-ship AI path / obstacle state
// IBehaviorManager owns at +0x0C (retail run 0x062db3b0-0x062de160), together with the
// user-zone probe pair FUN_062db830 / FUN_062ddcd0 and the two oriented-box helpers
// FUN_062dba00 / FUN_062dbb90 that the probe and the zone shapes (common_zone.cpp) share.
//
// Split out of common_aisteer.cpp and common_stubs.cpp (CMN-SPLIT stage 1, 2026-10-14);
// the bodies are unchanged.

#include "common_pathfind.h"
#include "common_geometry_internal.h" // ClampPointToBoxSurface, MathTransposeTransformVia3DMathEngine
#include "common_aisteer.h"
#include "common_behavior.h"      // GetIfacePositionOrZero
#include "common_cequip.h"        // IObject::ThrustEquipEngine (PathfindManager ctor)
#include "common_cobjdb.h"        // IObjDB::GetZoneDb (SetPathfindObjectDb, FUN_062db7c0)
#include "common_csys.h"          // Acquire3DMathEngine, Matrix
#include "common_iface.h"         // IBehaviorManager, IObjRW, ObjRwFromWatchable
#include "common_misc.h"          // FastInvSqrt, SUBOBJ_ID_NONE
#include "common_systemmanager.h" // SystemManager (Owner->Sys)
#include "common_universe.h"      // Universe::ISystem::GetSphereProperties (PathfindManager::Update's zone probe), GetId
#include "x86math.h"              // I3DMathEngine::TransposeTransform (vtable +0x38)
#include "common_aisteer_internal.h" // CmnAiSteer::PathfindOwnerObject

#include <cmath>
#include <cstdlib>
#include <cstring>

using CmnAiSteer::PathfindOwnerObject;

// ============================================================================
// PathfindManager (common_pathfind.h)
// ============================================================================

void PathfindManager::ObstacleReaction::ResetState()
{
    // FUN_062db7a0 (Common.dll.txt 225775-225787), all 9 instructions, in retail's order.
    State = 3;
    LastObstacleTag = 0;
    Heading.z = 0.0f;   // [ECX+0x10]
    Heading.y = 0.0f;   // [ECX+0x0c]
    Heading.x = 0.0f;   // [ECX+0x08]
    bInReaction = false;
    bCloseEncounter = false;
}

PathfindManager::PathfindManager(IBehaviorManager* NewOwner)
    // Members are built in declaration order, which is retail's order for every member with
    // a constructor: Zone (FUN_062db740, .text:062db3da), DeflectionSlots (empty element
    // ctor, .text:062db3ef), Watcher1C8 (inline zero, .text:062db424), Watcher218
    // (FUN_062de190 with a null object == the default BaseWatcher, .text:062db472),
    // Avoidance (.text:062db4a6-062db4db).
{
    // FUN_062db3b0 (Common.dll.txt 225461-225695), all 224 instructions traced. The stores
    // below follow the listing's order; the member constructors above have already run.
    bUserZoneActive = false;                         // 062db3d7 (before Zone's ctor in retail)
    for (int Index = 0; Index < 8; ++Index)          // 062db3f4 REP STOSD x8
    {
        RecentObstacleIds[Index] = 0;
    }
    NextDeflectionSlot = 0;                          // 062db409
    Reaction.ResetState();                           // 062db413 FUN_062db7a0(this+0x16c)

    Watcher1C8.set(nullptr);                         // 062db42e BaseWatcher::set(0)
    Timer1E0 = 0.0f;                                 // 062db438
    Period1E4 = 1.0f;                                // 062db43e
    Timer1E8 = 0.0f;                                 // 062db44a
    Period1EC = 1.0f;                                // 062db450
    Timer20C = 0.0f;                                 // 062db462
    Period210 = 1.0f;                                // 062db468

    Field_214 = 0;                                   // 062db480
    Watcher218.set(nullptr);                         // 062db482 BaseWatcher::set(0)
    Field_220.z = 0.0f;                              // 062db487
    Field_220.y = 0.0f;
    Field_220.x = 0.0f;
    Field_22C.z = 0.0f;                              // 062db490
    Field_22C.y = 0.0f;
    Field_22C.x = 0.0f;
    Field_238 = false;                               // 062db499
    Field_23C = 1.0f;                                // 062db49c
    Field_240 = 0;                                   // 062db4a3

    CachedObstacleId = 0;                            // 062db4be
    bHasCachedObstacle = false;                      // 062db4c4

    Owner = NewOwner;                                // 062db4e4
    Field_1F0 = 0;                                   // 062db4ea
    Flag245 = 1;                                     // 062db4f0
    Field_248 = -1;                                  // 062db4f7
    Field_24C = 1;                                   // 062db501
    Field_250 = 0x7f0000;                            // 062db508
    Field_254 = 1;                                   // 062db512
    Field_064 = 0;                                   // 062db519
    Watcher1C8.set(nullptr);                         // 062db51c `if ([+0x1c8]) set_pointer(0)`
    Field_1D0.z = 0.0f;                              // 062db529
    Field_1D0.y = 0.0f;
    Field_1D0.x = 0.0f;
    Field_1DC = 0;                                   // 062db546
    Field_000 = 0;                                   // 062db54c
    MaxEngineSpeed = 80.0f;                          // 062db54e (0x42a00000)
    Field_1F8 = 0.0f;                                // 062db558
    Field_1FC = 0.0f;                                // 062db55e

    // 062db53b-062db57a: the owner object's get_type into OwnerType (result dropped).
    PathfindOwnerObject(Owner)->get_type(OwnerType);

    Avoidance.AttachOwner(Owner);                    // 062db589 FUN_062de220(this+0x268, Owner)

    Avoidance.PhysSystemId = 0;                      // 062db594
    CachedTargetPosition.z = 0.0f;                   // 062db5a1
    CachedTargetPosition.y = 0.0f;
    CachedTargetPosition.x = 0.0f;
    Period1E4 = 1.0f;                                // 062db5b3

    // 062db5bd-062db62d: three phase offsets, each `(float)rand() * (1/32768)
    // (DAT_063997d0 == 0x38000000) * Period`, rand() called in this order.
    Timer1E0 = static_cast<float>(std::rand()) * 3.0517578e-05f * Period1E4;
    Period1EC = 0.5f;                                // 062db5d7 (0x3f000000)
    Timer1E8 = static_cast<float>(std::rand()) * 3.0517578e-05f * Period1EC;
    Period210 = 0.1f;                                // 062db605 (0x3dcccccd)
    Timer20C = static_cast<float>(std::rand()) * 3.0517578e-05f * Period210;

    Field_244 = false;                               // 062db63e
    Field_004 = 0;                                   // 062db644
    Field_255 = false;                               // 062db647
    Field_258 = 0;                                   // 062db64d
    Field_25C = 0;                                   // 062db653
    BlendTimer = 0.0f;                               // 062db659
    bBlendEnabled = false;                           // 062db65f

    // 062db677: get_speed(1.0f, ThrustEquipEngine) (+0xd8; pushes 1 then 1.0f). FST keeps
    // it in MaxEngineSpeed; FCOMP -1.0f (DAT_06399404 == 0xbf800000) / TEST AH,0x44 / JP:
    // the jump to the two distance queries is taken on even C3|C2 parity -- not equal, or
    // unordered. Only an exact -1.0f zeroes both distances.
    float const Speed = PathfindOwnerObject(Owner)->get_speed(1.0f, IObject::ThrustEquipEngine);
    MaxEngineSpeed = Speed;
    if (Speed == -1.0f)
    {
        Field_1F8 = 0.0f;                            // 062db694
        Field_1FC = 0.0f;                            // 062db69a
    }
    else
    {
        // 062db6cb / 062db700: get_distance_travelled (+0xd0) with pushes
        // (Engine, 1000.0f (DAT_063a05b4), Speed or 300.0f (0x43960000), 0) -- the last
        // push, 0, is the first argument.
        Field_1F8 = PathfindOwnerObject(Owner)->get_distance_travelled(
            0.0f, Speed, 1000.0f, IObject::ThrustEquipEngine);
        Field_1FC = PathfindOwnerObject(Owner)->get_distance_travelled(
            0.0f, 300.0f, 1000.0f, IObject::ThrustEquipEngine);
    }

    BlendStart.x = 0.0f;                             // 062db711
    BlendStart.y = 1.0f;                             // 062db717 (EBP == 0x3f800000)
    BlendStart.z = 0.0f;                             // 062db71d
}

// FUN_062db7c0, Common.dll.txt 225799-225826 (21 insns), thiscall, `RET 4`. Sole caller is
// ConnectBehaviorManagerObjectDb (FUN_062d2830, .text:062d2879).
void SetPathfindObjectDb(PathfindManager* Self, IObjDB* ObjectDb)
{
    if (ObjectDb == nullptr)
    {
        Self->Avoidance.PhysSystemId = 0;           // .text:062db7f1
        return;
    }

    // .text:062db7cc-062db7e6: `[ObjectDb]->+0x08` (IObjDB::GetZoneDb), then slot 0 of the
    // object it returns -- the whole pair runs twice and only the second result is stored.
    // The zone database is the system's Universe::ISystem; its slot 0 is the system id (the
    // same chain CheckLauncherLineOfFireCached walks).
    Universe::ISystem const* ZoneDb = ObjectDb->GetZoneDb();
    (void)ZoneDb->GetId();
    ZoneDb = ObjectDb->GetZoneDb();
    Self->Avoidance.PhysSystemId = ZoneDb->GetId();
}

void PathfindManager::clear_user_zone()
{
    // 0x062db820 (Common.dll.txt 225852-225859): `MOV byte ptr [this+0x8],0; RET`.
    bUserZoneActive = false;
}

void PathfindManager::submit_user_zone(UserZone const& NewZone)
{
    // 0x062db800 (Common.dll.txt 225827-225843): byte [this+0x8] = 1, then
    // `LEA EDI,[this+0xc]; MOV ECX,0x16; REP MOVSD` -- the 0x58-byte zone copied whole.
    bUserZoneActive = true;
    std::memcpy(&Zone, &NewZone, sizeof(UserZone));
}

bool PathfindManager::get_user_zone(UserZone& OutZone)
{
    // 0x062c5ac0 (Common.dll.txt 191497-191519): when byte [this+0x8] is set, the 0x16-dword
    // copy from this+0xc into OutZone and true; otherwise OutZone is untouched and false.
    if (bUserZoneActive)
    {
        std::memcpy(&OutZone, &Zone, sizeof(UserZone));
        return true;
    }
    return false;
}

void PathfindManager::SetCruiseLock(int Value, unsigned int Mask)
{
    // FUN_062dd3d0 (Common.dll.txt 228376-228515), all 108 instructions traced.
    unsigned char const Byte = static_cast<unsigned char>(Value);   // 062dd3f2 MOV CL,byte
    if ((Mask & 2) != 0)                                            // 062dd3ec TEST AL,0x2 / JZ
        Field_254 = Byte;                                           // 062dd3f8
    unsigned int const Bit0 = Mask & 1;                             // 062dd3fe AND EAX,0x1
    if (Bit0 != 0)
        Field_24C = Byte;                                           // 062dd403
    if (Byte != 0)                                                  // 062dd40b CMP CL,BL / JNZ
        return;

    // 062dd413-062dd43d: bit 0 given and Field_214 bit 0 set -> FUN_062e8ce0 on the owner's
    // ThrustManager (IBehaviorManager+0x08 -> SystemManager, +0x18) with a zeroed stack Vector.
    if (Bit0 != 0 && (Field_214 & 1) != 0)
    {
        Vector Zero;
        Zero.x = 0.0f;                                              // 062dd426
        Zero.y = 0.0f;                                              // 062dd42a
        Zero.z = 0.0f;                                              // 062dd42e
        Owner->Sys->Thrust.SetNudgeDirection(Zero);                 // 062dd43d
    }

    // 062dd442-062dd55d: the +0x214 record is assigned from a default-constructed
    // temporary. The temporary's watcher is zeroed and BaseWatcher::set(0) on it is a no-op,
    // so its two `if (watched) set_pointer(0)` branches (062dd472, 062dd556) are never
    // taken. The assignment, field by field:
    Field_214 = 0;                                                  // 062dd4c8
    // 062dd4d6-062dd4f1: the watcher half -- EDX = source target (0 here), `CMP EDX,[ECX];
    // JZ; set_pointer(EDX)`, i.e. BaseWatcher::set(nullptr).
    Watcher218.set(nullptr);
    Field_220.x = 0.0f;                                             // 062dd506
    Field_220.y = 0.0f;                                             // 062dd50c
    Field_220.z = 0.0f;                                             // 062dd513
    Field_22C.x = 0.0f;                                             // 062dd520
    Field_22C.y = 0.0f;                                             // 062dd526
    Field_22C.z = 0.0f;                                             // 062dd52d
    Field_238 = false;                                              // 062dd538
    Field_23C = 1.0f;                                               // 062dd542 (0x3f800000)
    Field_240 = 0;                                                  // 062dd548
}

void PathfindManager::Update(float Dt)
{
    // FUN_062dd760 (Common.dll.txt 228721-228908), all 173 instructions traced. Clock sums
    // stay on the x87 stack for the compare (double here); the stored timers are floats.
    if ((OwnerType & 0x7f0000) == 0)                                // 062dd767
    {
        return;
    }

    // 062dd778-062dd7a3: EBX = the owner's CurrentBehaviorIndex, read before the probe.
    // Timer1E0 += Dt; the unrounded sum against Period1E4 (TEST AH,0x41 / JNZ skips <=, ==
    // and unordered).
    int const BehaviorIndex = Owner->CurrentBehaviorIndex;
    double const ZoneClock = static_cast<double>(Dt) + Timer1E0;
    float const ZoneClockStored = static_cast<float>(ZoneClock);
    Timer1E0 = ZoneClockStored;
    if (ZoneClock > Period1E4)
    {
        // 062dd7a9-062dd7b8: fmod of the stored clock.
        Timer1E0 = static_cast<float>(std::fmod(static_cast<double>(ZoneClockStored),
                                                static_cast<double>(Period1E4)));

        // 062dd7be-062dd812: zone properties of a 20.0f sphere around the owner --
        // get_position (slot 0), then ObjectDb->GetZoneDb() (+0x08), Universe::ISystem
        // slot 8 (+0x20) on {position, 20.0f}.
        Vector const& Position = PathfindOwnerObject(Owner)->get_position();
        Universe::ISystem const* const System = Owner->ObjectDb->GetZoneDb();
        Geometry::Sphere Probe;
        Probe.Center = Position;
        Probe.Radius = 20.0f;                                       // 0x41a00000
        unsigned const ZoneProps = System->GetSphereProperties(&Probe);

        // 062dd815-062dd832: Field_004 = the properties, Field_000 = 1 when any of 0x7000 is
        // set (TEST AH,0x70), | 8 while the avoidance holds a contact.
        Field_004 = static_cast<int>(ZoneProps);
        Field_000 = 0;
        if ((ZoneProps & 0x7000) != 0)
        {
            Field_000 = 1;
        }
        if (Avoidance.HasContact == 1)
        {
            Field_000 |= 8;
        }
    }

    // 062dd835-062dd886: behaviours 6, 8, 0x13 and none drop the obstacle state -- the five
    // stores of FUN_062db780 inlined in its order, then FUN_062db7a0 on +0x16c.
    if (BehaviorIndex == 6 || BehaviorIndex == 8 || BehaviorIndex == 0x13 || BehaviorIndex == -1)
    {
        Avoidance.ResetContact();
        Reaction.ResetState();
        return;
    }

    // 062dd889-062dd8c7: EBX = Field_214 before the refresh; Timer20C against Period210.
    int const PreviousRecord = Field_214;
    double const RecordClock = static_cast<double>(Dt) + Timer20C;
    float const RecordClockStored = static_cast<float>(RecordClock);
    Timer20C = RecordClockStored;
    if (RecordClock > Period210)
    {
        Timer20C = static_cast<float>(std::fmod(static_cast<double>(RecordClockStored),
                                                static_cast<double>(Period210)));
        RefreshAvoidance();                                         // 062dd8c7 FUN_062dd580
    }

    // 062dd8cc-062dd92c: a live nudge record is blended into the ThrustManager
    // (FUN_062e8e60) when Field_244 allows; one that has just ended hands it a zero vector
    // (FUN_062e8ce0).
    if ((Field_214 & 1) != 0)
    {
        if (Field_244)
        {
            Owner->Sys->Thrust.BlendNudgeDirection(Field_220, Dt);
        }
    }
    else if ((PreviousRecord & 1) != 0)
    {
        Vector Zero;
        Zero.x = 0.0f;                                              // 062dd90e
        Zero.y = 0.0f;
        Zero.z = 0.0f;
        Owner->Sys->Thrust.SetNudgeDirection(Zero);
    }

    // 062dd931-062dd947
    if (Flag245 == 0 || Field_254 == 0)
    {
        return;
    }

    UpdateHeadingBlend(Dt);                                         // 062dd950 FUN_062dc590

    // 062dd955-062dd9c1: the avoidance runs on its own clock and is handed its period as
    // its Dt; the period becomes 0.1f (DAT_0639d20c) while it is active or has a contact,
    // 0.5f otherwise.
    double const AvoidClock = static_cast<double>(Dt) + Timer1E8;
    float const AvoidClockStored = static_cast<float>(AvoidClock);
    Timer1E8 = AvoidClockStored;
    if (AvoidClock > Period1EC)
    {
        Timer1E8 = static_cast<float>(std::fmod(static_cast<double>(AvoidClockStored),
                                                static_cast<double>(Period1EC)));
        Avoidance.Update(Period1EC);                                // 062dd99a FUN_062de490
        if (Avoidance.Active != 0 || Avoidance.HasContact != 0)
        {
            Period1EC = 0.1f;
        }
        else
        {
            Period1EC = 0.5f;
        }
    }

    // 062dd9c7-062dd9f2: is_player (+0xbc) on the owner object, result dropped; no null test.
    (void)PathfindOwnerObject(Owner)->is_player();
}

void PathfindManager::RefreshAvoidance()
{
    // FUN_062dd580 (Common.dll.txt 228536-228706), all 155 instructions traced.
    // 062dd588-062dd5e1: the record cleared, then SelectAvoidancePoint into +0x22c / +0x23c.
    Watcher218.set(nullptr);                                        // `if (watched) set_pointer(0)`
    Field_214 = 0;
    Field_22C.z = 0.0f;
    Field_22C.y = 0.0f;
    Field_22C.x = 0.0f;
    Field_220.z = 0.0f;
    Field_220.y = 0.0f;
    Field_220.x = 0.0f;
    Field_23C = 1.0f;
    Field_240 = 0;
    int Kind;
    if (SelectAvoidancePoint(&Field_22C, &Field_23C, &Kind) == 0)
    {
        // 062dd5ea-062dd651: a point record.
        Field_238 = Avoidance.HasContact;
        Watcher218.set(nullptr);
        Field_214 = 2;
        Field_240 = Kind;
        // get_center_of_mass (slot 5, +0x14) into a local that is never read.
        (void)PathfindOwnerObject(Owner)->get_center_of_mass();
        return;
    }

    // 062dd652-062dd67d: only owner types in 0x130000 look for an obstacle to nudge around.
    if ((OwnerType & 0x130000) == 0)
    {
        return;
    }
    IObjRW* Obstacle = nullptr;
    Vector Direction;
    if (SelectObstacleDirection(&Direction, &Obstacle, static_cast<unsigned>(Field_064)) != 0)
    {
        return;
    }

    // 062dd683-062dd6b3: watch the obstacle (its Watchable at +8, 0 for none); Field_240 = 4.
    Watcher218.set(Obstacle != nullptr ? static_cast<Watchable const*>(Obstacle) : nullptr);
    Field_240 = 4;

    // 062dd6b3-062dd6f3: the owner's cruise state (is_cruise_active, slot 24 +0x60) and the
    // watched obstacle's type (get_type, +0x88) when one is watched.
    bool bCruise = false;
    PathfindOwnerObject(Owner)->is_cruise_active(bCruise);
    unsigned ObstacleType = 0;
    if (Watcher218.target() != nullptr
        && ObjRwFromWatchable(const_cast<Watchable*>(Watcher218.target())) != nullptr)
    {
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher218.target()))->get_type(ObstacleType);
    }

    // 062dd6f9-062dd74e: Field_244 = !cruise || obstacle type in 0x7f0000; when clear, the
    // direction goes straight to the ThrustManager (FUN_062e8ce0). Field_220 = direction,
    // Field_214 = 1.
    SystemManager* const Sys = Owner->Sys;
    bool const bBlend = (!bCruise || (ObstacleType & 0x7f0000) != 0);
    Field_244 = bBlend;
    if (!bBlend)
    {
        Sys->Thrust.SetNudgeDirection(Direction);
    }
    Field_220 = Direction;
    Field_214 = 1;
}

int PathfindManager::SelectAvoidancePoint(Vector* OutPoint, float* OutScale, int* OutKind)
{
    // FUN_062dda10 (Common.dll.txt 228937-229152), all 208 instructions traced.
    if (Flag245 == 0 || Field_254 == 0)                             // 062dda17-062dda2e
    {
        return 2;
    }

    // 062dda34-062dda5c: EDI = the owner object; the user-zone exit direction first.
    IObjRW* const Ship = PathfindOwnerObject(Owner);
    Vector ExitDirection;
    if (GetUserZoneApproachDirection(&ExitDirection) == 0)
    {
        // 062dda62-062ddb00: kind 2, the point 100 units along the exit direction from the
        // owner (get_orientation +0x10, folded with get_transform). *OutScale untouched.
        *OutKind = 2;
        Field_25C = 2;
        Transform const& Xform = Ship->get_transform();
        Vector const Reach{ ExitDirection.x * 100.0f, ExitDirection.y * 100.0f, ExitDirection.z * 100.0f };
        *OutPoint = Vector{ Reach.x + Xform.Translation.x, Reach.y + Xform.Translation.y,
                            Reach.z + Xform.Translation.z };
        return 0;
    }

    if (Reaction.State != 3)
    {
        // 062ddb10-062ddb91: kind 3, the point 100 units along the reaction heading;
        // x and y are summed unrounded, z is rounded first (FSTP to a local).
        IObjRW* const Self = PathfindOwnerObject(Owner);
        Transform const& Xform = Self->get_transform();
        double const ReachX = static_cast<double>(Reaction.Heading.x) * 100.0f;
        double const ReachY = static_cast<double>(Reaction.Heading.y) * 100.0f;
        float const ReachZ = static_cast<float>(static_cast<double>(Reaction.Heading.z) * 100.0f);
        OutPoint->x = static_cast<float>(ReachX + Xform.Translation.x);
        OutPoint->y = static_cast<float>(ReachY + Xform.Translation.y);
        OutPoint->z = ReachZ + Xform.Translation.z;

        // 062ddb91-062ddb9a: FUN_062e5c00(owner, point, 0, 0) into *OutScale.
        *OutScale = static_cast<float>(EstimateTurnCircleApproachCost(Self, *OutPoint, 0.0f, 0.0f));

        // 062ddb9c-062ddbef: in a close encounter, a heading less than 0.93f (DAT_063a05d0;
        // TEST AH,0x5 / JP skips >=, unordered) off the owner's forward (-column 2) zeroes it.
        if (Reaction.bCloseEncounter)
        {
            float const ForwardX = -Xform.Rotation.E[0][2];
            double const Dot = (static_cast<double>(-Xform.Rotation.E[2][2]) * Reaction.Heading.z
                                + static_cast<double>(-Xform.Rotation.E[1][2]) * Reaction.Heading.y)
                               + static_cast<double>(ForwardX) * Reaction.Heading.x;
            if (Dot < 0.93f)
            {
                *OutScale = 0.0f;
            }
        }

        // 062ddbf5-062ddc3e: kind 3; a point within 1 of the origin ((x*x + y*y) + z*z <=
        // 1.0, double, TEST AH,0x41 / JNZ) still answers 2.
        *OutKind = 3;
        Field_25C = 3;
        double const LengthSq = static_cast<double>(OutPoint->x) * OutPoint->x
                                + static_cast<double>(OutPoint->y) * OutPoint->y
                                + static_cast<double>(OutPoint->z) * OutPoint->z;
        if (!(LengthSq > 1.0))
        {
            return 2;
        }
        return 0;
    }

    // 062ddc41-062ddcb5: the avoidance point, unless its contact is the excluded Field_064.
    Vector Point;
    unsigned ObjectId;
    if (Avoidance.GetAvoidPoint(&Point, &ObjectId) && ObjectId != static_cast<unsigned>(Field_064))
    {
        *OutPoint = Point;
        *OutScale = Avoidance.GetCloseRateScale();                 // 062ddc7f FUN_062df2d0
        *OutKind = 1;
        Field_25C = 1;
        return 0;
    }
    *OutKind = 0;
    Field_25C = 0;
    return 2;
}

int PathfindManager::SelectObstacleDirection(Vector* OutDirection, IObjRW** OutObstacle, unsigned ExcludeId)
{
    // FUN_062de050 (Common.dll.txt 229513-229603), all 89 instructions traced.
    if (Flag245 == 0 || Field_24C == 0)                             // 062de058-062de06f
    {
        return 2;
    }

    // 062de075-062de0a1: zero the direction, Field_255 = 0, then the closest obstacle on
    // the SystemManager's timer table (FUN_062e7a30) excluding ExcludeId, type mask
    // Field_248.
    SystemManager* const Sys = Owner->Sys;
    OutDirection->z = 0.0f;
    OutDirection->y = 0.0f;
    OutDirection->x = 0.0f;
    Field_255 = false;
    IObjInspect* const Obstacle =
        FindClosestObstacleToAvoid(&Sys->Timers, ExcludeId, static_cast<unsigned>(Field_248));
    if (Obstacle != nullptr)
    {
        // 062de0ac-062de0f5: the shape-biased direction around it (FUN_062e55b0 on
        // Sys+0x14fc); Field_258 = its get_id (+0x20).
        Vector Direction;
        *OutDirection = *ComputeShapeBiasedAvoidDirection(&Direction, &Sys->SelfTarget, Obstacle);
        *OutObstacle = static_cast<IObjRW*>(Obstacle);
        Field_255 = true;
        Field_258 = static_cast<int>(Obstacle->get_id());
        return 0;
    }

    // 062de0f8-062de14b: otherwise the avoidance direction and its contact, looked up by id
    // in the object database (IObjDB::FindById, +0x04).
    unsigned ObjectId = 0;
    if (Avoidance.GetAvoidDirection(OutDirection, &ObjectId))
    {
        *OutObstacle = Owner->ObjectDb->FindById(&ObjectId);
        Field_255 = true;
        Field_258 = static_cast<int>(ObjectId);
        return 0;
    }
    return 2;
}

// ============================================================================
// THE AI PROXIMITY-VOLUME PROBE FAMILY -- items 1674 (FUN_062dbb90), 1673
// (FUN_062dba00), 1672 (FUN_062db830), 1686 (FUN_062ddcd0).
// PORTED FOR REAL 2026-09-17 (claude:common-dnt-audit), from the raw listing
// only: the Ghidra pseudo-C for FUN_062dbb90 is self-evidently broken (item
// 1674's own note records the uninitialized-array-write artifact) and the
// tracker notes for 1672/1673/1686 all pre-date the receiver being resolved.
//
// WHY THEY ARE PORTABLE NOW -- BOTH STATED BLOCKERS WERE WRONG.
//   (a) Items 1672/1686 claimed "owning embedded sub-object (element+0xc within
//       the still-unbuilt 0x88-stride AI-list-element) is a Wave-4/FULLPORT-
//       DEFERRED[PUB] prerequisite". That reads a "+0xc" out of the wrong
//       object. The receiver is NOT inside the 0x88-stride list element -- it
//       cannot be, since these functions dereference `this+0x1c0`, which is
//       past the end of a 0x88-byte record. Traced from the raw bytes at
//       FUN_062ed780's own call site:
//         Common.dll.txt:254066-254068  MOV EAX,[ESI+0x4]     ; SystemManager
//                                       MOV ECX,[EAX+0x4]     ; IBehaviorManager
//                                       MOV EDX,[ECX+0xc]     ; <-- the receiver
//         Common.dll.txt:254080         MOV [ESP+0x18],EDX    ; frame+0x18
//         Common.dll.txt:254425-254428  LEA ECX,[ESP+0x60] / PUSH ECX /
//                                       MOV ECX,[ESP+0x1c]   ; == frame+0x18
//                                       CALL FUN_062ddcd0
//         Common.dll.txt:254689-254694  ... MOV ECX,[ESP+0x20] ; == frame+0x18
//                                       CALL FUN_062db830
//       (ESP is at the frame base at both call sites: SUB ESP,0x11c + 4 pushes
//       == 0x12c, which is exactly the bias Ghidra's own `local_114+0x12c`
//       annotation at 254080 uses, and the only intervening pushes are the
//       out-param ones counted above.) So BOTH calls take the same receiver,
//       `*(void**)(IBehaviorManager + 0x0c)`. Independently corroborated by a
//       sibling lane: common_dodgeidea.cpp (split out of common_aisteer.cpp) records
//       `thunk_FUN_062ddcd0(ECX = *(Manager+0xc))` for the SteeringTask
//       siblings' own call sites, derived without reference to this cluster.
//       IBehaviorManager is ported (common_iface.h), so the receiver is
//       reachable today; nothing about the 0x88-stride element is involved.
//   (b) The "0x062d2000-0x062ea000 DO-NOT-TOUCH range" cited against them is
//       retired -- see the cluster header comment above for the census.
//
// THE RECEIVER'S LAYOUT, as established by these four bodies plus the two
// FUN_062ed780 call sites. The receiver `*(IBehaviorManager+0x0c)` is the
// PathfindManager (common_pathfind.h: IBehaviorManager::Pathfind; +0x08 is its
// bUserZoneActive, +0x0C its UserZone, +0x1C0 its Owner), so the two probes are
// PathfindManager members (CMN-SPLIT 2026-10-14). The bodies still address the
// fields as byte offsets on `this`; the offsets below match common_pathfind.h:
//   +0x008 bool   Active        -- probe disabled when clear
//   +0x00C int    Mode          -- 0 = point+radius sphere, 1 = oriented box,
//                                  2 = live-interface target, >=3 = inert
//   +0x010 int    Invert        -- dword, tested as a whole (not a byte)
//   +0x014 Vector TargetPoint   -- mode 0's target; mode 2 caches into it
//   +0x020 float  Radius        -- mode 0's sphere radius / mode 0's distance bias
//   +0x024 Matrix BoxRotation  \
//   +0x048 Vector BoxCenter     |- a 0x3C-byte oriented box, MATRIX-FIRST order
//   +0x054 Vector BoxHalfExtent/   (deliberately NOT common_ini.h's OrientedBox,
//                                   which puts Center first -- see below)
//   +0x060 Zone.TargetKey      -- an object id; its ADDRESS goes to IObjDB::FindById
//   +0x1C0 IBehaviorManager* Owner -- Owner+0x0F0 is a Watchable* (object at -8),
//                                 Owner+0x0EC is IBehaviorManager::ObjectDb (IObjDB*)
// The box's field order is pinned by FUN_062dba00's own reads: it is called
// with ECX = This+0x24 (Common.dll.txt:229264, 229312, 225974) and reads
// [ECX+0x30]/[ECX+0x34]/[ECX+0x38], i.e. This+0x54..0x5C, as the half-extents;
// while FUN_06288800 is called with This+0x48 as the point to subtract
// (Common.dll.txt:229294, 225957) and FUN_062dbb90 with This+0x24 as the
// matrix. Matrix at +0x00, Center at +0x24, HalfExtent at +0x30, total 0x3C.
//
// Shared leaf helpers used below and named in the raw listing:
//   FUN_06288800(Out, A, B)  -- __cdecl Vec3Sub: *Out = *A - *B.
//   FUN_0628b030(Out, ByVal) -- __thiscall Vec3Assign (Vector passed by value
//                               in 12 bytes of stack).
// Both are trivial and are written out inline here rather than called through
// a forward declaration, matching this file's existing practice for them.
// ============================================================================

// Common.dll.txt:226246-226282 (item 1674, FUN_062dbb90, 25 insns). __cdecl
// (Vector* Out, Matrix const* M, Vector const* Src) -- three plain stack args,
// no ECX, and the caller cleans (Common.dll.txt:229307 / 225969 clean 0x18 for
// this call plus the preceding Vec3Sub together).
//
// Body: lazily fetches the 3DMathEngine singleton into its own static cache
// (.data:DAT_063FC994, initialised from FUN_06391CF0 == Acquire3DMathEngine --
// retail keeps one such cache slot per call site; DAT_063FCA58 is another),
// then invokes engine vtable slot +0x38 as
//   slot(Engine, &Scratch, M, Src)
// with callee cleanup (there is no ADD ESP after the CALL at 226272, and the
// subsequent [ESP+0x10] read at 226273 only resolves to `Out` if 16 bytes were
// popped by the callee -- i.e. the slot is __stdcall, which is what
// DACOM_API/x86math.h already declares it as), and finally copies the three
// floats out of Scratch into *Out. Slot +0x38 is I3DMathEngine::
// TransposeTransform (x86math.h: +0x0C Inverse, +0x10 Scale, +0x14 Det,
// +0x18 MulQuat, +0x1C MulTransform, +0x20 MulMatrix, +0x24 TransformQuat,
// +0x28 TransformXform, +0x2C TransformMatrix, +0x30 Rotate, +0x34
// InverseRotate, +0x38 TransposeTransform) -- i.e. this rotates a world-space
// delta into the box's local frame, which is exactly how both callers use it.
//
// The bounce through `Scratch` is retail's, not an artifact: the engine writes
// into its own stack temp and only then is it copied to *Out, so aliasing
// between Out and Src is harmless. Reproduced faithfully.
//
// LINKAGE 2026-09-24 (claude-code:w6-zone): no longer file-static -- the zone shapes in
// common_zone.cpp call FUN_062dbb90 directly (06339812, 06339be1, 06339cf2 and more), so it
// is declared in common_geometry_internal.h (formerly common_stubs.h). The null guard that stood here is gone: retail has none
// (226259's TEST is only the lazy-init gate of the engine cache).
void MathTransposeTransformVia3DMathEngine(Vector* Out, Matrix const* M, Vector const* Src)
{
    I3DMathEngine* const Engine = Acquire3DMathEngine();

    Vector Scratch;
    Engine->TransposeTransform(*reinterpret_cast<FVector*>(&Scratch),
                               *reinterpret_cast<FMatrix const*>(M),
                               *reinterpret_cast<FVector const*>(Src));
    *Out = Scratch;
}

// Common.dll.txt:226083-226241 (item 1673, FUN_062dba00, 138 insns).
// `bool __thiscall(OrientedBox* Box, Vector const* Src, Vector* Dst)`, RET 8.
// `Box` here is the probe object's +0x24 sub-object, so the half-extents it
// reads at Box+0x30/+0x34/+0x38 are the probe's +0x54/+0x58/+0x5C.
//
// Per axis i (226103-226179): Overshoot[i] = fabs(Src[i]) - HalfExtent[i]; if
// Overshoot[i] >= 0 then Dst[i] = _copysign(HalfExtent[i], Src[i]) and the
// "was outside" flag is latched, otherwise Dst[i] = Src[i] verbatim. The
// FCOMP/TEST AH,0x1/JNZ trio at 226110-226113 tests C0 only, so the >= 0 case
// (equality included) is the clamping one.
//
// If no axis was outside, the point is strictly inside the box and retail does
// NOT leave it alone: it snaps the single axis with the LARGEST Overshoot
// (i.e. the nearest face, all three values being negative) onto that face
// (226180-226234). The comparison chain is exactly
//   if (Ox >= Oy) { if (Ox >= Oz) snap X; else snap Z; }
//   else          { if (Oy >= Oz) snap Y; else snap Z; }
// (226183/226188 and 226208 each TEST AH,0x1, i.e. branch away only on a
// strict <). That interior case still returns FALSE -- every one of the three
// interior exits loads AL from the latched flag, which is 0 on this path
// (226197, 226217, 226232), never from a fresh 1. So the return value means
// "the input was outside the box", while Dst always comes back sitting exactly
// on the box surface. Both callers rely on that pair of facts.
//
// LINKAGE 2026-09-24 (claude-code:w6-zone): no longer file-static; the zone box shape
// (common_zone.cpp) calls FUN_062dba00 directly. Declared in common_geometry_internal.h
// (formerly common_stubs.h).
bool ClampPointToBoxSurface(unsigned char* Box, Vector const* Src, Vector* Dst)
{
    float const* const HalfExtent = reinterpret_cast<float const*>(Box + 0x30);
    float const* const In = reinterpret_cast<float const*>(Src);
    float* const OutAxis = reinterpret_cast<float*>(Dst);

    float Overshoot[3];
    bool bWasOutside = false;

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Overshoot[Axis] = std::fabs(In[Axis]) - HalfExtent[Axis];
        if (Overshoot[Axis] >= 0.0f)
        {
            OutAxis[Axis] = static_cast<float>(_copysign(HalfExtent[Axis], In[Axis]));
            bWasOutside = true;
        }
        else
        {
            OutAxis[Axis] = In[Axis];
        }
    }

    if (!bWasOutside)
    {
        int NearestAxis = 2;
        if (Overshoot[0] >= Overshoot[1])
        {
            if (Overshoot[0] >= Overshoot[2])
                NearestAxis = 0;
        }
        else if (Overshoot[1] >= Overshoot[2])
        {
            NearestAxis = 1;
        }
        OutAxis[NearestAxis] = static_cast<float>(_copysign(HalfExtent[NearestAxis], In[NearestAxis]));
    }

    return bWasOutside;
}

// Both user-zone probes below start the same way (Common.dll.txt:225899-225912 and
// 229197-229210): the owner position is GetIfacePositionOrZero (FUN_062e60e0) of the
// watched object behind Owner+0xF0 (PathfindOwnerObject: raw ? raw - 8 : 0). Mode 2's
// "target interface" at Owner+0xEC is IBehaviorManager::ObjectDb: slot +0x04 is
// IObjDB::FindById(&Zone.TargetKey) (no ADD ESP after the CALL -- thiscall, callee
// pops), and slot 0 of the found object is IObjInspect::get_position.

// Common.dll.txt:225872-226066 (item 1672, FUN_062db830, 161 insns).
// Retail signature `bool __thiscall(This, Vector const* ProbePoint, float*
// /*unused*/)` -- RET 8, and the single call site
// (Common.dll.txt:254689-254694) pushes &<a float scratch at frame+0x50> first
// and This+0xC's Vector second, so the low/first argument is the point and the
// high/second one is a float out-param the callee never touches. The dead
// second argument is dropped rather than carried as a fake parameter.
// PathfindManager::UserZoneContainsPoint (formerly AiVolumeProbeContainsPoint,
// a file-static over the raw receiver; retail 0x062db830).
//
// Asks "is ProbePoint inside this probe's volume?".
//   * !Active -> true unconditionally (226062-226066: AL=1). A disabled probe
//     admits everything; that is retail, not a stub.
//   * The owner position is fetched first, on every mode (225899-225912).
//   * Mode 0 (225 226018-226025): the volume is the sphere at TargetPoint.
//   * Mode 1 (225952-226006): oriented box. Local = TransposeTransform(
//     BoxRotation, ProbePoint - BoxCenter); Clamped = ClampPointToBoxSurface(
//     Local); SignedSq = |Local - Clamped|^2, negated when the point was NOT
//     outside (226000-226001, TEST BL,BL / JNZ skips the FCHS). Inside iff
//     SignedSq < 0 -- the TEST AH,0x5 / JNP at 226005-226006 branches to the
//     "inside" label on odd parity of C0|C2, i.e. on C0 alone, i.e. on
//     ST0 < 0.0f. When the point is interior the clamp still moves one axis,
//     so SignedSq is strictly negative and the test is not degenerate.
//   * Mode 2 (225922-225951): resolves the live target through the owner's
//     interface and then falls into the SAME sphere test as mode 0 using that
//     target (225951 jumps straight to LAB_062db996). A missing interface or a
//     missing target returns false outright (225926, 225937).
//   * Mode >= 3 (225921): no test at all, result stays false.
//   * Finally, when Invert is non-zero the result is flipped
//     (226010-226017 vs 226055-226061).
bool PathfindManager::UserZoneContainsPoint(Vector const* ProbePoint)
{
    if (!bUserZoneActive)                                   // byte [+0x08]
        return true;

    Vector OwnerPos;
    GetIfacePositionOrZero(&OwnerPos, PathfindOwnerObject(Owner));   // 062db841-062db85f

    bool bInside = false;

    // Set by mode 0 and mode 2; consumed by the shared sphere test.
    Vector SphereTarget;
    bool bHaveSphereTarget = false;

    if (Zone.Mode == 0)                                     // [+0x0C]
    {
        SphereTarget = Zone.TargetPoint;                    // [+0x14]
        bHaveSphereTarget = true;
    }
    else if (Zone.Mode == 1)
    {
        // FUN_062dba00 takes the box as the 0x3C-byte block at +0x24 (rotation, then
        // centre at +0x24, half extents at +0x30 of that block).
        unsigned char* const Box = reinterpret_cast<unsigned char*>(&Zone.BoxRotation);
        Vector const* const BoxCenter = &Zone.BoxCenter;    // [+0x48]

        Vector Delta;
        Delta.x = ProbePoint->x - BoxCenter->x;
        Delta.y = ProbePoint->y - BoxCenter->y;
        Delta.z = ProbePoint->z - BoxCenter->z;

        Vector Local;
        MathTransposeTransformVia3DMathEngine(&Local, &Zone.BoxRotation, &Delta);

        Vector Clamped;
        bool const bWasOutside = ClampPointToBoxSurface(Box, &Local, &Clamped);

        // 062db909-062db935: the three differences are FSTP'd to float (Vec3Assign);
        // 062db93a-062db958: (Dy*Dy + Dz*Dz) + Dx*Dx stays unrounded on the x87 stack.
        float const Dx = Local.x - Clamped.x;
        float const Dy = Local.y - Clamped.y;
        float const Dz = Local.z - Clamped.z;
        double SignedSq = ((double)Dy * Dy + (double)Dz * Dz) + (double)Dx * Dx;
        if (!bWasOutside)
            SignedSq = -SignedSq;

        bInside = (SignedSq < 0.0f);
    }
    else if (Zone.Mode == 2)
    {
        // 062db87f-062db8c8: Owner->ObjectDb (+0xEC) FindById (slot +0x04) on
        // &Zone.TargetKey (+0x60), then the found object's get_position (slot 0).
        IObjDB* const ObjectDb = Owner->ObjectDb;
        if (ObjectDb == nullptr)
            return false;

        IObjRW* const Found = ObjectDb->FindById(&Zone.TargetKey);
        if (Found == nullptr)
            return false;

        SphereTarget = Found->get_position();
        bHaveSphereTarget = true;
    }

    if (bHaveSphereTarget)
    {
        // 062db996-062db9aa: the differences stay on the x87 stack (unrounded).
        double const Dx = (double)OwnerPos.x - SphereTarget.x;
        double const Dy = (double)OwnerPos.y - SphereTarget.y;
        double const Dz = (double)OwnerPos.z - SphereTarget.z;
        double const Radius = Zone.Radius;                  // [+0x20]
        // 062db9b1-062db9d4: (dz*dz + dy*dy) + dx*dx and Radius*Radius, both unrounded;
        // FCOMPP (ST0 = R*R) with TEST AH,0x41 / JNZ to "outside" on not-greater or
        // unordered, so equality counts as outside -- a strict R^2 > DistSq is required.
        bInside = (Radius * Radius > (Dz * Dz + Dy * Dy) + Dx * Dx);
    }

    if (Zone.Invert != 0)                                   // [+0x10]
        return !bInside;
    return bInside;
}

// Common.dll.txt:229167-229485 (item 1686, FUN_062ddcd0, 283 insns).
// `int __thiscall(This, Vector* OutDir)`, RET 4 (OutDir at entry+4, read back
// at 229444/229459 as [ESP+0x58] with the frame 0x54 deep).
// Returns 0 on success with a unit approach direction written to *OutDir, or 2
// ("not applicable") on every rejection. Ghidra exposes it as
// `thunk_FUN_062ddcd0`; the four other callers (Common.dll.txt:219794, 221278,
// 222312, 228962) reach it through that thunk.
//
// Constants, read out of .rdata rather than assumed:
//   DAT_06399408 = 00 00 00 00                   -> 0.0f
//   DAT_0639A1D0 = 00 00 80 3F                   -> 1.0f
//   DAT_0639C430 = eight zero bytes              -> 0.0 (double)
//
//   * !Active -> 2 (229193-229196).
//   * Owner position first, same as the containment probe (229197-229210).
//   * Mode 0 (229379-229434): Dist = |OwnerPos - TargetPoint|; the gate value
//     is Dist - Radius (229399, FSUB [EDI+0xC] with EDI = This+0x14); the
//     direction is normalize(TargetPoint - OwnerPos).
//   * Mode 1 (229242-229378): oriented box. Retail runs the SAME
//     Vec3Sub/TransposeTransform/ClampPointToBoxSurface/Vec3Sub chain TWICE --
//     once at 229245-229283 purely to test the sign, and again verbatim at
//     229294-229336 to take the square root -- with identical inputs both
//     times. Reproduced as one computation plus a comment rather than a
//     literal double evaluation, since the second run cannot differ.
//     The gate value is 0.0f when the (signed) squared distance is not > 0.0
//     (229291: the JP at 229290 is taken on even parity of C0|C3, i.e. when
//     neither is set, i.e. when ST0 > 0.0 -- so the sqrt branch runs on
//     strictly positive only), otherwise sqrt of it. NOTE the asymmetry with
//     mode 0: here the probe point fed to the box is the OWNER's own position
//     (229296 passes local_48, the position filled at 229210), not an external
//     point -- this function asks "how far am *I* from the volume", which is
//     why it takes no point argument at all. The direction is
//     normalize(BoxCenter - OwnerPos) (229338-229375, reading This+0x48/+0x4C/
//     +0x50 == BoxCenter).
//   * Mode 2 (229220-229241): refreshes This->TargetPoint from the owner's
//     interface -- note this WRITES the cache, unlike the containment probe --
//     then joins mode 0's path at LAB_062DDF04. Missing interface or missing
//     target -> 2.
//   * Mode >= 3 (229219): jumps straight to the gate with the 0.0f that was
//     loaded at 229211 still in ST0, and 0.0 fails both polarities below, so
//     an out-of-range mode always returns 2. OutDir is left untouched in that
//     case (retail would have written the raw owner position; it never gets
//     there).
//   * The gate (229435-229478): compare the value against 0.0.
//       Invert == 0: reject on C0|C3 via TEST AH,0x41/JNZ, i.e. require
//                    Value > 0.0, and write *OutDir = Dir.
//       Invert != 0: TEST AH,0x5/JP rejects on EVEN parity of C0|C2, i.e. when
//                    C0 is clear, i.e. require Value < 0.0, and write
//                    *OutDir = -Dir (three FCHS at 229460/229465/229470).
//
// PathfindManager::GetUserZoneApproachDirection (formerly the free function
// AiVolumeProbeApproachDirection(This, OutDir), retail 0x062ddcd0 + thunk
// 0x062de040; CMN-SPLIT 2026-10-14). Callers: SelectAvoidancePoint,
// DodgeIdea's reroll (common_dodgeidea.cpp), BreakAwayIdea (common_breakawayidea.cpp) / the
// 062d952c idea (DodgeMissileIdea, common_dodgemissileidea.cpp) and TargetManager::UpdateAttackTargetOverride.
int PathfindManager::GetUserZoneApproachDirection(Vector* OutDir)
{
    if (!bUserZoneActive)                                   // byte [+0x08]
        return 2;

    Vector OwnerPos;
    GetIfacePositionOrZero(&OwnerPos, PathfindOwnerObject(Owner));   // 062ddce2-062ddd00

    int const Mode = Zone.Mode;                             // [+0x0C]

    // 062ddd05: FLD 0.0f -- the gate value lives on the x87 stack (double here) until
    // the final FCOMP at 062ddfb7.
    double GateValue = 0.0f;
    Vector Dir;
    Dir.x = 0.0f;
    Dir.y = 0.0f;
    Dir.z = 0.0f;

    if (Mode == 2)
    {
        // 062ddd24-062ddd61: Owner->ObjectDb (+0xEC) FindById (slot +0x04) on
        // &Zone.TargetKey (+0x60); the found object's get_position (slot 0) is written
        // into the cached TargetPoint (+0x14).
        IObjDB* const ObjectDb = Owner->ObjectDb;
        if (ObjectDb == nullptr)
            return 2;

        IObjRW* const Found = ObjectDb->FindById(&Zone.TargetKey);
        if (Found == nullptr)
            return 2;

        Zone.TargetPoint = Found->get_position();
    }

    if (Mode == 0 || Mode == 2)
    {
        Vector const* const Target = &Zone.TargetPoint;

        // 062ddf04-062ddf38: V = OwnerPos - Target through FUN_06288800 (float
        // stores); gate = FSQRT((Vy*Vy + Vz*Vz) + Vx*Vx) - Radius, left unrounded.
        float const Bx = OwnerPos.x - Target->x;
        float const By = OwnerPos.y - Target->y;
        float const Bz = OwnerPos.z - Target->z;
        float const Radius = Zone.Radius;                   // [+0x20]
        GateValue = std::sqrt(((double)By * By + (double)Bz * Bz) + (double)Bx * Bx) - Radius;

        // 062ddf3b-062ddfb5: Dir = Target - OwnerPos (float stores), then
        // 1 / FSQRT((Dz*Dz + Dy*Dy) + Dx*Dx) unrounded (FDIVR 1.0f, no zero guard),
        // each component FSTP'd.
        Dir.x = Target->x - OwnerPos.x;
        Dir.y = Target->y - OwnerPos.y;
        Dir.z = Target->z - OwnerPos.z;
        double const InvLen = 1.0 / std::sqrt(((double)Dir.z * Dir.z + (double)Dir.y * Dir.y) + (double)Dir.x * Dir.x);
        Dir.x = (float)(Dir.x * InvLen);
        Dir.y = (float)(Dir.y * InvLen);
        Dir.z = (float)(Dir.z * InvLen);
    }
    else if (Mode == 1)
    {
        unsigned char* const Box = reinterpret_cast<unsigned char*>(&Zone.BoxRotation);   // [+0x24]
        Vector const* const BoxCenter = &Zone.BoxCenter;    // [+0x48]

        Vector Delta;
        Delta.x = OwnerPos.x - BoxCenter->x;
        Delta.y = OwnerPos.y - BoxCenter->y;
        Delta.z = OwnerPos.z - BoxCenter->z;

        Vector Local;
        MathTransposeTransformVia3DMathEngine(&Local, &Zone.BoxRotation, &Delta);

        Vector Clamped;
        bool const bWasOutside = ClampPointToBoxSurface(Box, &Local, &Clamped);

        // 062dddb7-062ddddf: D = Local - Clamped (FUN_06288800, float stores);
        // (Dy*Dy + Dz*Dz) + Dx*Dx unrounded, negated when the point was inside.
        float const Dx = Local.x - Clamped.x;
        float const Dy = Local.y - Clamped.y;
        float const Dz = Local.z - Clamped.z;
        double SignedSq = ((double)Dy * Dy + (double)Dz * Dz) + (double)Dx * Dx;
        if (!bWasOutside)
            SignedSq = -SignedSq;

        // 062ddde1-062dde6c: `FCOMP 0.0; TEST AH,0x41; JP` takes the sqrt arm on greater
        // (or unordered -- a NaN gate is rejected below either way); otherwise the float
        // gate local is 0. Retail's duplicate second evaluation of everything above
        // (062dddf8-062dde68) is bit-identical to the first; only its FSQRT, FSTP'd to
        // float, is kept here.
        GateValue = (SignedSq > 0.0f) ? (float)std::sqrt(SignedSq) : 0.0f;

        // 062dde70-062ddef3: Dir = BoxCenter - OwnerPos (Vec3Assign, float stores), then
        // 1 / FSQRT((Dx*Dx + Dy*Dy) + Dz*Dz) unrounded (no zero guard), FSTP each.
        Dir.x = BoxCenter->x - OwnerPos.x;
        Dir.y = BoxCenter->y - OwnerPos.y;
        Dir.z = BoxCenter->z - OwnerPos.z;
        double const InvLen = 1.0 / std::sqrt(((double)Dir.x * Dir.x + (double)Dir.y * Dir.y) + (double)Dir.z * Dir.z);
        Dir.x = (float)(Dir.x * InvLen);
        Dir.y = (float)(Dir.y * InvLen);
        Dir.z = (float)(Dir.z * InvLen);
    }

    if (Zone.Invert == 0)                                   // [+0x10]
    {
        if (!(GateValue > 0.0f))
            return 2;
        *OutDir = Dir;
        return 0;
    }

    if (!(GateValue < 0.0f))
        return 2;
    OutDir->x = -Dir.x;
    OutDir->y = -Dir.y;
    OutDir->z = -Dir.z;
    return 0;
}

