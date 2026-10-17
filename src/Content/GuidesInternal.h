// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU37: the concrete guide classes CreateGuide builds (Guides.h has the base, the
// descriptions and CreateGuide). Private to the guide sources (Guides.cpp, GotoGuides.cpp);
// the rest of Content only sees FContentGuide. Lane CNT-AI owns this file.
//
// No RTTI in Content: the names come from what the classes do. Retail vtables:
//   06fb81d8 FTargetGuide  (intermediate, never created on its own)
//   06fb8188 FNothingGuide (type 1, 0x28)      06fb8330 FTradeGuide (type 2, 0x34)
//   06fb84d0 FPatrolGuide  (type 3, 0x34)
//   06fb8138 FWanderGuide  (type 4, 0x3c)
//   06fb827c FLineGuide    (intermediate)      06fb8404 FGotoGuide (type 5, 0x1b0)
//   06fb8490 FDockGuide    (type 6, 0x1b8)
//   06fb839c FGuideRoute   (abstract route)    06fb8384 FTradeRoute (0x20)
//   06fb854c FPatrolRoute  (0x1c)              06fb8520 FCargoScanRoute (0x64)
//   06fb857c FGcsScanEvent (Gcs.h; the scanner's GCS events 0x11..0x18)

#include "Guides.h"
#include "Gcs.h"                  // FGcsScanEvent
#include "MissionObject.h"        // FNnPathLeg
#include "ObjListDirective.h"     // FMissionDirectiveList

#include <list>
#include <vector>

class FObjListDirective;
struct EquipDesc;                 // common_goods.h
struct FPatrolPath;               // PatrolPaths.h

// TU37 globals (Guides.cpp): DAT_06fd0648 (10000 squared; trade and patrol route flee),
// DAT_06fd0640 / DAT_06fd063c (the patrol scanner's attitude window).
extern float GGuideLaneEntryRangeSquared;
extern float GPatrolScanMinAttitude;
extern float GPatrolScanMaxAttitude;

// ---------------------------------------------------------------------------------------
// Target guide (retail vtable 06fb81d8): the two targets of the description.
// ---------------------------------------------------------------------------------------

class FTargetGuide : public FContentGuide
{
public:
    // The shared ctor prefix (CreateGuide 06f05f52-06f05f77 / 06f06051-06f06076, trade
    // FUN_06f06fe0 06f06ffd-06f0704e inline): base FUN_06f06560, vtable 06fb81d8,
    // Desc->GetTargets (FUN_06f062d0) into +0x14..+0x20.
    explicit FTargetGuide(FGuideDesc const* Desc);
    FTargetGuide(FTargetGuide const& Other) = default;

    // +0x14, 06ebf720: nothing.
    void Reset() override;
    // +0x30, LAB_06f06b20 (RET 0x10).
    bool RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                Vector& OutPosition, unsigned int& OutParam,
                                float& OutFloat) override;
    // +0x34, LAB_06f068f0 (RET 0x10): both ids; the bases as the base's rule (+0x1c only
    // when OutBase1 is given, +0x20 only when both are).
    void GetTargets(unsigned int* OutId1, unsigned int* OutId2, FContentBase** OutBase1,
                    FContentBase** OutBase2) override;

    // +0x3c, LAB_06f068d0: swaps +0x14 / +0x18 and +0x1c / +0x20.
    virtual void SwapTargets();
    // +0x40, FUN_06f06a50: a Delay directive of 20..30 s (rand) as the ship's only directive.
    virtual void IssueDelayDirective();
    // +0x44, FUN_06f069c0: a Launch directive (no setter) as the ship's only directive.
    virtual void IssueLaunchDirective();
    // +0x48, FUN_06f06920: a Dock directive at TargetId2 as the ship's only directive.
    virtual void IssueDockDirective();

    unsigned int TargetId1 = 0;                  // +0x14
    unsigned int TargetId2 = 0;                  // +0x18
    FContentBase* TargetBase1 = nullptr;         // +0x1c
    FContentBase* TargetBase2 = nullptr;         // +0x20
};

// ---------------------------------------------------------------------------------------
// Type 1 "nothing" (retail vtable 06fb8188, 0x28).
// ---------------------------------------------------------------------------------------

class FNothingGuide final : public FTargetGuide
{
public:
    // CreateGuide case 1 (06f05f32-06f05fda).
    explicit FNothingGuide(FGuideDesc const* Desc);
    // Clone body (FUN_06f06ed0 06f06f0a-06f06f77).
    FNothingGuide(FNothingGuide const& Other);
    // FUN_06f06e00 (sdd 06f06de0): releases the directive.
    ~FNothingGuide() override;

    FContentGuide* Clone() override;                                   // FUN_06f06ed0
    void Start() override;                                             // FUN_06f06fa0
    void OnDirectivesDone(int ResultWord11c, int ResultWord118) override;   // LAB_06f0af30

    TRefHandle<FObjListDirective> DelayDirective;   // +0x24 ref-holding member
};

// ---------------------------------------------------------------------------------------
// Routes (retail abstract vtable 06fb839c; no destructor slot: the trade and patrol guides
// free their route with operator delete, FUN_06f07160 / 06f0ad2f). The patrol guide's scan
// worker (06fb8520) derives from the same base (its ctors store 06fb839c first, 06f0ab35 /
// 06f0c2ef) and adds a deleting dtor as its slot 6.
// ---------------------------------------------------------------------------------------

class FGuideRoute
{
public:
    // Slot 0 (purecall): a heap copy.
    virtual FGuideRoute* Clone() = 0;
    // Slot 1 (purecall). Trade route 1 (06ee9e10), patrol route 2 (06ee9660), scan worker 3
    // (06f5e540).
    virtual int GetType() = 0;
    // Slot 2 (purecall): the route's state for its guide (trade route 06f07110: 3 finished,
    // 1 running; patrol route 06f0bc00; scan worker 06f0abe0).
    virtual int GetStatus() = 0;
    // Slot 3, LAB_06f070f0 (RET 8): GetStatus(). Name from the override: the patrol guide's
    // HandleEvent (06f0aefd) forwards its message id and parms to the scan worker's slot 3
    // (06f0cbc0), which switches on the message id.
    virtual int HandleEvent(pub::Controller::EMessage MsgId, void const* Parms);
    // Slot 4 (purecall): the next directives into *Out; returns GetStatus().
    virtual int Update(std::list<FObjListDirective*>* Out) = 0;
    // Slot 5, LAB_06f07100 (RET 4): +4 = Ship.
    virtual void SetShip(FContentNpcShip* InShip);

    FContentNpcShip* Ship = nullptr;             // +0x04

protected:
    FGuideRoute() = default;
    FGuideRoute(FGuideRoute const& Other) = default;
    ~FGuideRoute() = default;
};

// The trade guide's route (retail 0x20, vtable 06fb8384): the way to Center (the trade
// target's GetRadius centre / GetLocation position) within Range.
class FTradeRoute final : public FGuideRoute
{
public:
    FTradeRoute() = default;
    FTradeRoute(FTradeRoute const& Other) = default;
    ~FTradeRoute() = default;

    FGuideRoute* Clone() override;                                     // LAB_06f07750
    int GetType() override;                                            // 06ee9e10
    int GetStatus() override;                                          // LAB_06f07110
    int Update(std::list<FObjListDirective*>* Out) override;           // LAB_06f077b0

    // FUN_06f07950 (RET 4): a 1 s Delay directive appended to Out when another member of the
    // ship's formation group (+0x154) is in its +0x120 state (HasField120).
    bool AddFormationWaitDirective(std::list<FObjListDirective*>* Out);

    int State = 0;                               // +0x08 0..4
    Vector Center = {};                          // +0x0c
    float Range = 0.0f;                          // +0x18 radius + 3000
    unsigned int Field_0x1c = 0;                 // +0x1c 0 in the ctor and Clone. TODO: reader
};

// ---------------------------------------------------------------------------------------
// Type 2 "trade" (retail vtable 06fb8330, 0x34).
// ---------------------------------------------------------------------------------------

class FTradeGuide final : public FTargetGuide
{
public:
    // FUN_06f06fe0 (RET 4).
    explicit FTradeGuide(FGuideDesc const* Desc);
    // Clone body (FUN_06f07180 06f071b4-06f0721e).
    FTradeGuide(FTradeGuide const& Other);
    // FUN_06f07160 (sdd 06f06fc0): the route freed (operator delete, no dtor call).
    ~FTradeGuide() override;

    FContentGuide* Clone() override;                                   // FUN_06f07180
    void Start() override;                                             // FUN_06f07240
    void Reset() override;                                             // LAB_06f061a0
    void OnDirectivesDone(int ResultWord11c, int ResultWord118) override;   // LAB_06f0af30
    void SetShip(FContentNpcShip* InShip) override;                    // LAB_06f07120
    bool RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                Vector& OutPosition, unsigned int& OutParam,
                                float& OutFloat) override;             // FUN_06f07470
    void SwapTargets() override;                                       // LAB_06f076c0

    // +0x4c, LAB_06f07140: +0x2c.
    virtual unsigned int GetTradeGood();
    // +0x50, LAB_06f07150: +0x30.
    virtual unsigned int GetReturnGood();

    int State = 0;                               // +0x24 0..3
    FTradeRoute* Route = nullptr;                // +0x28
    unsigned int TradeGood = 0;                  // +0x2c desc +0x4c
    unsigned int ReturnGood = 0;                 // +0x30 desc +0x50
};

// ---------------------------------------------------------------------------------------
// Type 3 "patrol_path" (retail vtable 06fb84d0, 0x34): the ship flies its encounter's zone
// path (FSpawnPlan::Path) point by point between the two targets (FPatrolRoute, 06fb854c) and
// meanwhile scans the cargo of the ships it passes (FCargoScanRoute, 06fb8520).
// ---------------------------------------------------------------------------------------

// The patrol guide's route (retail 0x1c, vtable 06fb854c = the route base slots + two).
class FPatrolRoute final : public FGuideRoute
{
public:
    FPatrolRoute() = default;
    FPatrolRoute(FPatrolRoute const& Other) = default;
    ~FPatrolRoute() = default;

    FGuideRoute* Clone() override;                                     // LAB_06f0bbb0
    int GetType() override;                                            // 06ee9660: 2
    int GetStatus() override;                                          // LAB_06f0bc00
    int Update(std::list<FObjListDirective*>* Out) override;           // LAB_06f0bfe0
    // Slot 6, LAB_06f0bc40 (RET 0x10): the patrol guide's request_flee_destination (06f0b261).
    virtual bool RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                        Vector& OutPosition, unsigned int& OutParam,
                                        float& OutFloat);
    // Slot 7, 06ebf6f0 (`RET 4`): nothing. No caller known; name unknown.
    virtual void Slot1c(unsigned int Unused);

    // FUN_06f0bf10: NextPoint one step on (turning at the path's ends, wrapping on a loop).
    void AdvancePoint();
    // FUN_06f0be60: the points left from NextPoint to EndPoint (0 unless State is 1 or 2).
    int GetPointsToGo() const;

    FPatrolPath* Path = nullptr;                 // +0x08 desc Plan.Path
    int Direction = 0;                           // +0x0c desc Plan.PathDirection (+1 / -1)
    int NextPoint = -1;                          // +0x10 desc Plan.PathTargetPoint
    int EndPoint = -1;                           // +0x14 desc Plan.PathPoint
    int State = 1;                               // +0x18 0..4
};

// The patrol guide's cargo scanner (retail 0x64, vtable 06fb8520): picks a ship near the
// patrol (FindScanTarget), trails it, scans its cargo (ActivateEquipment type 1) and asks for
// the faction's scan_for_cargo goods, then tractors / collects the jettisoned loot or turns
// hostile. Its conversations are GCS events of the types 0x11..0x18 (FGcsScanEvent).
// States (+0x14): 0 off, 1 idle, 2 closing in, 3 in range, 4 closing in after the "stand by"
// line, 5 waiting for that line, 6 scanning, 7 waiting for the demand line, 8 waiting for the
// player's answer, 0xa jettisoned, 0xb tractoring, 0xc collecting, 0xd hostile.
class FCargoScanRoute final : public FGuideRoute
{
public:
    // The patrol guide ctor's inline part (06f0ab23-06f0abab): Ship = Desc->Ship, the members
    // below, then SetShip(Ship) (FUN_06f0c3f0, called directly).
    explicit FCargoScanRoute(FContentNpcShip* InShip);
    // FUN_06f0c2c0 (RET 4; Clone 06f0c500).
    FCargoScanRoute(FCargoScanRoute const& Other);
    // Slot 6: scalar deleting dtor FUN_06f0acd0, body FUN_06f0c560.
    virtual ~FCargoScanRoute();

    FGuideRoute* Clone() override;                                     // FUN_06f0c500
    int GetType() override;                                            // 06f5e540: 3
    int GetStatus() override;                                          // LAB_06f0abe0
    int HandleEvent(pub::Controller::EMessage MsgId, void const* Parms) override;            // LAB_06f0cbc0
    // LAB_06f0dbd0 (RET 4): Out's directives released, Out = the scanner's directives (each
    // AddRef'ed), the scanner's own released and erased; returns GetStatus().
    int Update(std::list<FObjListDirective*>* Out) override;
    void SetShip(FContentNpcShip* InShip) override;                    // FUN_06f0c3f0

    // Slot 7, LAB_06f0ac00 (RET 4): +0x60.
    virtual void SetScanEnabled(bool bEnabled);
    // Slot 8, FUN_06f0ac10.
    virtual void Reset();
    // Slot 9, FUN_06f0ca10 (RET 8): a TractorBeam directive (0x19) at TargetId appended to
    // List (BaseOp Field_08 0).
    virtual void AddTractorBeamDirective(unsigned int TargetId, std::list<FObjListDirective*>* List);
    // Slot 10, FUN_06f0cad0 (RET 8): a GotoShip directive (3) at TargetId (range 50) appended
    // to List (BaseOp Field_08 0), then pub::AI::submit_forced_target(ship, TargetId).
    virtual void AddGotoShipDirective(unsigned int TargetId, std::list<FObjListDirective*>* List);

    // FUN_06f0c6b0: picks TargetId among the ships within 2500 of the scanner (see the .cpp).
    bool FindScanTarget();
    // The message 0x20 loop of LAB_06f0cbc0 (06f0cc88-06f0d49d, 06f0d4a2-06f0d627).
    void HandleTimer(float const* DeltaSeconds);
    // A GCS scan event (FGcsScanEvent, vtable 06fb857c) of Type from the ship to TargetId,
    // CommSpeakerId = the ship's id, then GGcsConversations.QueueConversation (FUN_06ee8d70;
    // the float result is dropped). E.g. 06f0d4a2-06f0d4f0.
    void QueueScanEvent(int Type, unsigned int LootTakerId, unsigned int GoodId,
                        unsigned int Response);

    FMissionDirectiveList Directives;            // +0x08 the directives Update hands out
    int State = 0;                               // +0x14 see above
    // +0x18: SetShip: the scanner's cargo scan range (pub::SpaceObj::GetScannerRange's second
    // value), 500 once scanning is possible.
    int ScanRange = -1;
    int CargoCount = -1;                         // +0x1c the found cargo's count (EquipDesc +0x14)
    unsigned int TargetId = 0;                   // +0x20 the scanned ship
    unsigned int CommSpeakerId = 0;              // +0x24 the speaker of the last queued event
    unsigned int CargoGoodId = 0;                // +0x28 the found cargo's good
    // +0x2c: the found cargo in the scan's local EquipDescVector (freed at the end of state 6;
    // not read afterwards).
    EquipDesc const* Cargo = nullptr;
    std::vector<unsigned int> ScannedIds;        // +0x30 (allocator byte; +0x34..+0x3c)
    std::vector<unsigned int> LootIds;           // +0x40 the jettisoned cargo (+0x44..+0x4c)
    float AnswerTimer = 0.0f;                    // +0x50 state 8
    unsigned int Field_0x54 = 0;                 // +0x54 0, copied. TODO: no reader found
    float ScanChance = 0.0f;                     // +0x58 SetShip: personality or faction scan_chance
    float LootTimer = -1.0f;                     // +0x5c 3.1 s before the loot is collected; -1 off
    bool bScanEnabled = true;                    // +0x60 SetScanEnabled
    bool bScanAnnounce = true;                   // +0x61 the faction's scan_announce
};

// Type 3 "patrol_path" guide.
class FPatrolGuide final : public FTargetGuide
{
public:
    // FUN_06f0aa40 (RET 4).
    explicit FPatrolGuide(FGuideDesc const* Desc);
    // Clone body (FUN_06f0adb0 06f0adea-06f0ae66).
    FPatrolGuide(FPatrolGuide const& Other);
    // FUN_06f0acf0 (sdd 06f0aa20).
    ~FPatrolGuide() override;

    FContentGuide* Clone() override;                                   // FUN_06f0adb0
    void Start() override;                                             // LAB_06f0af40
    void Reset() override;                                             // LAB_06f0aa10
    void OnDirectivesDone(int ResultWord11c, int ResultWord118) override;   // LAB_06f0af30
    void HandleEvent(pub::Controller::EMessage MsgId, void const* Parms) override;           // FUN_06f0aee0
    void SetShip(FContentNpcShip* InShip) override;                    // FUN_06f0ad60
    void OnScanStateChange(bool bScanning) override;                   // LAB_06f0aeb0
    bool RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                Vector& OutPosition, unsigned int& OutParam,
                                float& OutFloat) override;             // FUN_06f0b1e0

    // +0x4c, LAB_06f0b2d0: Route->GetPointsToGo() (tail jump). GCS FUN_06edae70 (the patrol
    // status line) reads it.
    virtual int GetPathPointsToGo();

    bool bUndocked = true;                       // +0x24 0 from the dock directive to the launch
    bool bScanState = true;                      // +0x25 the ship's scan_state_change
    int State = 0;                               // +0x28 0..5
    FPatrolRoute* Route = nullptr;               // +0x2c
    FCargoScanRoute* Scanner = nullptr;          // +0x30
};

// ---------------------------------------------------------------------------------------
// Type 4 "wander" (retail vtable 06fb8138, 0x3c).
// ---------------------------------------------------------------------------------------

class FWanderGuide final : public FTargetGuide
{
public:
    // CreateGuide case 4 (06f06035-06f060c4).
    explicit FWanderGuide(FGuideDesc const* Desc);
    // Clone body (FUN_06f0b350 06f0b384-06f0b3f7).
    FWanderGuide(FWanderGuide const& Other);
    // FUN_06f0b300 (sdd 06f0b2e0): releases the directive.
    ~FWanderGuide() override;

    FContentGuide* Clone() override;                                   // FUN_06f0b350
    void Start() override;                                             // LAB_06f0b410
    void Reset() override;                                             // LAB_06f061a0
    void OnDirectivesDone(int ResultWord11c, int ResultWord118) override;   // LAB_06f0af30

    int State = 0;                               // +0x24 set to 1 by Start
    // +0x28: the directive Start hands out; a ref-holding member (CreateGuide's unwind state 10,
    // funclet 06facde2: FUN_06f28e10 on +0x28).
    TRefHandle<FObjListDirective> Directive;
    unsigned int const* Zone = nullptr;          // +0x2c desc +0x48; *Zone = the zone id
    float MinRange = 1500.0f;                    // +0x30 DAT_06fb8114
    float MaxRange = 3000.0f;                    // +0x34 DAT_06fb8118
    float Height = 500.0f;                       // +0x38 DAT_06fb811c: y of the search centre
};

// ---------------------------------------------------------------------------------------
// [MetaBehavior] guides (retail intermediate vtable 06fb827c = the base slots).
// ---------------------------------------------------------------------------------------

class FLineGuide : public FContentGuide
{
public:
    // FUN_06f07ef0 06f07f17-06f07f29: base FUN_06f06560, Line = the description's line
    // (0x41 dwords), vtable 06fb827c.
    explicit FLineGuide(FMetaBehavior const* Desc);
    FLineGuide(FLineGuide const& Other) = default;

    TString<0x100> Line;                         // +0x14
};

// Type 5 "goto_guide" (retail vtable 06fb8404, 0x1b0).
class FGotoGuide : public FLineGuide
{
public:
    // FUN_06f07ef0 (RET 4).
    explicit FGotoGuide(FMbGotoGuide const* Desc);
    // Clone body (FUN_06f08140 06f0817d-06f08367).
    FGotoGuide(FGotoGuide const& Other);
    // FUN_06f08390 (sdd 06f08120).
    ~FGotoGuide() override;

    FContentGuide* Clone() override;                                   // FUN_06f08140
    void Start() override;                                             // FUN_06f099f0
    void Reset() override;                                             // LAB_06f087e0
    void OnDirectivesDone(int ResultWord11c, int ResultWord118) override;   // 06ea1490
    void HandleEvent(pub::Controller::EMessage MsgId, void const* Parms) override;           // FUN_06f08c70
    void SetShip(FContentNpcShip* InShip) override;                    // LAB_06f07de0
    bool RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                Vector& OutPosition, unsigned int& OutParam,
                                float& OutFloat) override;             // LAB_06f084d0
    void RefreshTarget() override;                                     // FUN_06f07ec0

    // State change used by every handler: State = NewState (the old one kept in PrevState)
    // and a restart requested, unless State already is NewState.
    void SetState(int NewState);
    // The 0x20 (timer) part of FUN_06f08c70 (06f09168-06f0998a).
    void HandleTimer(float DeltaSeconds, FNnPathLeg const& Leg, double ApproachSquared);
    // The common exit of FUN_06f08c70 (06f0998e-06f0999f): Start() (slot +0x10) when a restart
    // was requested.
    void FinishEvent();
    // FUN_06f08800 (RET 8).
    bool IsTargetOnPath(int Kind, unsigned int ObjectId);
    // FUN_06f08930.
    bool FindLaneEntryNearTarget();
    // FUN_06f08a80.
    bool FindLegPointNearTarget();
    // The first leg of Path (retail copies the head's first node unconditionally; an empty
    // Path yields the zeroed leg here, see the .cpp).
    FNnPathLeg GetFirstLeg() const;

    Vector FirstPosition = {};                   // +0x118 desc +0x124 (SetGuide follow offset)
    Vector Destination = {};                     // +0x124 desc +0x130
    float Range = 0.0f;                          // +0x130 desc +0x13c
    TString<0x30> TargetName;                    // +0x134 desc +0x140
    unsigned int TargetId = 0;                   // +0x168 FindObjectIdByName(TargetName)
    unsigned char bCruise = 0;                   // +0x16c desc +0x174 -> SetGuide bCruise
    unsigned char bNoCruise = 0;                 // +0x16d desc +0x175 -> SetGuide bNoCruise
    unsigned char Field_0x16e = 1;               // +0x16e desc +0x176 -> SetGuide FieldA0
    // +0x170: the last directive handed out; a ref-holding member (the ctor / Clone / dtor
    // unwind maps release it with FUN_06f28e10, after +0x180 and +0x1a0).
    TRefHandle<FObjListDirective> Directive;
    int State = 1;                               // +0x174 1..10 (dock guide also 12, 13)
    int PrevState = 0;                           // +0x178
    int Priority = 0;                            // +0x17c desc +0x114 -> BaseOp Field_08
    std::list<FNnPathLeg> Path;                  // +0x180
    Vector Waypoint = {};                        // +0x18c (not set by the ctor in retail)
    float Timer = 0.0f;                          // +0x198 state 3 nag timer
    int NagCount = 0;                            // +0x19c state 3 "get going" count
    // +0x1a0: the first ids of the 0x1001 (combat state) messages of type 0 that name the
    // target or the ship; types 3 / 4 and message 0x39 remove them again.
    std::list<unsigned int> CombatIds;
    bool bRestart = false;                       // +0x1ac Start acts only when set
    bool bNodeEndSent = false;                   // +0x1ad "at node end" line sent
};

// Type 6 "dock_guide" (retail vtable 06fb8490, 0x1b8).
class FDockGuide final : public FGotoGuide
{
public:
    // FUN_06f0a270 (RET 4).
    explicit FDockGuide(FMbDockGuide const* Desc);
    // Clone body (FUN_06f0a3b0 06f0a3ed-06f0a5b6).
    FDockGuide(FDockGuide const& Other);
    // FUN_06f0a140 (sdd 06f0a120): the goto guide's dtor body, repeated.
    ~FDockGuide() override = default;

    FContentGuide* Clone() override;                                   // FUN_06f0a3b0
    void Start() override;                                             // LAB_06f0a830
    void HandleEvent(pub::Controller::EMessage MsgId, void const* Parms) override;           // LAB_06f0a620
    void RefreshTarget() override;                                     // LAB_06f0a5d0

    unsigned int DockTargetId = 0;               // +0x1b0 desc +0x178
    bool bDockRestart = false;                   // +0x1b4 Start issues the dock directive
    bool bDocking = false;                       // +0x1b5 the goto part is finished
};

// Directive helpers shared by the guide sources.

// FUN_06f0de80 (RET 4): smart-pointer assignment: when Slot differs from New, Slot is
// released and set to New, which is AddRef'ed.
void AssignDirective(FObjListDirective** Slot, FObjListDirective* New);
// The "only directive" tail (06f06978-06f069a8 and siblings): CancelDirectives, AddDirective,
// RestartDirectives on the ship.
void IssueOnlyDirective(FContentNpcShip* Ship, FObjListDirective* Directive);
