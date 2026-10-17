// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU37 (06f05d00-06f0e000): the NPC guides. A guide is the long-running
// behaviour of an NPC ship (FContentNpcShip +0x178): it issues [ObjList] directives
// (MissionObjList.h) to the ship and reacts to the ship's events. CreateGuide builds one from
// a guide description: FGuideDesc (types 1-4, built by the encounter / mission / random
// mission code) or a [MetaBehavior] object (types 5 / 6, MB_GOTOGUIDE / MB_DOCKGUIDE, loaded
// by LoadMetaBehavior). Lane CNT-AI owns this file.
//
// No RTTI in Content: class names are chosen from what the classes do; the behavior names of
// ParseGuideBehavior give the type numbers (1 nothing, 2 trade, 3 patrol_path, 4 wander,
// 5 goto_guide, 6 dock_guide).

#include "ContentRefObject.h" // FContentRefObject (the ref-counted base 06fb43f8)
#include "RefHandle.h"        // TRefHandle
#include "common_pub.h"       // pub::AI::ContentCallback flee enums, Vector
#include "common_server_controller.h" // pub::Controller::EMessage (HandleEvent)
#include "common_tstring.h"   // TString

#include <list>
#include <vector>

class FContentGuide;
class INI_Reader;
class FContentNpcShip;    // NpcShip.h (TU43)
struct FContentBase;      // BaseDB.h (TU17)
struct FNnPathLeg;        // MissionObject.h (TU38; built by the TU53 path builder, SpacePaths.h)
struct FPatrolPath;       // PatrolPaths.h (the zone path of FSpawnPlan kind 7)

// FUN_06f07a80 (cdecl): "behavior = name" -> the guide type. Case-sensitive exact
// compares: trade 2, patrol_path 3, nothing 1, wander 4, goto_guide 5, dock_guide 6,
// patrol_guide 7; the empty string 0; anything else 1. Caller TU29 06ef615e.
int __cdecl ParseGuideBehavior(char const* Name);

// ---------------------------------------------------------------------------------------
// Guide descriptions
// ---------------------------------------------------------------------------------------

// The head shared by both kinds of guide description: the ref-counted base
// (FContentRefObject, vtable 06fb43f8; slots 0-2 of every description vtable are AddRef
// 06eacd40, Release 06eacd50, sdd 06f06eb0), then +0x08 Type and +0x0c Ship, written by
// FUN_06f061b0 / FUN_06f06200 and FUN_06f06530. CreateGuide reads only Type; the guide ctors
// read Ship and the derived members.
class FGuideDescBase : public FContentRefObject
{
public:
    int Type;                                    // +0x08 CreateGuide's switch (1..6)
    FContentNpcShip* Ship;                       // +0x0c the NPC ship ctor stores itself

    FGuideDescBase(int InType, FContentNpcShip* InShip)
        : Type(InType)
        , Ship(InShip)
    {
    }
};

// The member handle that holds one reference on a guide description: FContentNpcShipParams
// +0xfc (NpcShip.h). Retail keeps it as a ref-holding member: the copy is FUN_06f21fd0 (copy +
// AddRef when set), the destructor / unwind step FUN_06f28e10 (Release when set), run in the
// member order of FUN_06ef4bb0. A raw-pointer assignment does not touch the count: the users'
// inlined retail code (encounter spawn 06ef47c7.., mission spawn 06f167ad..) does its own
// AddRef / Release around it, as retail does.
using FGuideDescHandle = TRefHandle<FGuideDescBase>;
// The same holder for a guide (e.g. the mission ship's +0x68 guide).
using FContentGuideHandle = TRefHandle<FContentGuide>;

// The placement plan of one encounter spawn (0x38 bytes), FGuideDesc's +0x10 block: TU29
// FUN_06ef54e0 builds it on its stack (06ef57c7-06ef5832, the defaults below), TU53
// FUN_06f65f10 (PlaceSpawnFormation) fills it, TU29 FUN_06ef4eb0 copies it into the leader's
// guide description and TU53 FUN_06f63cc0 (the leader's trade targets) reads it.
struct FSpawnPlan
{
    // +0x00: how the formation arrives -- 0 not placed, 1 trade lane (FUN_06f65d20), 2 dock
    // launch (FUN_06f65900; TU58 CreateNpcShips launches from DockTarget), 7 cruise
    // (FUN_06f64800), 8 buzz (FUN_06f64fc0).
    int Kind = 0;
    unsigned int RingId = 0;            // +0x04 kind 1: the ring the ships leave the lane at
    unsigned int NextRingId = 0;        // +0x08 kind 1: the ring they were heading for
    unsigned int DockTarget = 0;        // +0x0c kind 2: the launching object
    Vector Position;                    // +0x10 kind 7 / 8: the leader's position (unset before)
    Vector Destination;                 // +0x1c kind 7 / 8: its first goto point (unset before)
    // +0x28: kind 7 along a zone path (FUN_06f64800, guide type 3): the path; +0x30 the point
    // index the ships head for, +0x34 the direction along the path (+1 / -1; FUN_06f61800).
    FPatrolPath* Path = nullptr;
    int PathPoint = -1;                 // +0x2c FUN_06f63cc0: the path point of the trade target
    int PathTargetPoint = -1;           // +0x30
    int PathDirection = 0;              // +0x34
};

// Guide description of the types 1-4 (0x64 bytes, vtable 06fb8224 = the three base slots).
// Built by the encounter / mission code (default ctor FUN_06f061b0, caller TU29 06ef4803;
// FUN_06f06200 (Type, Ship), callers RM 06f839c6 / 06f8a931) and handed to the NPC ship
// through its parameters (+0xfc); CreateGuide builds the guide from it.
struct FGuideDesc : public FGuideDescBase
{
    // +0x10..+0x47: the placement plan of the encounter spawn (TU29 FUN_06ef4eb0 copies the
    // spawner's plan here whole, REP MOVSD 0xe; TU53 FUN_06f63cc0 reads it). The ctors set
    // the same defaults as the spawner's local (+0x10..+0x1c 0, +0x38 0, +0x3c / +0x40 -1,
    // +0x44 0; +0x20..+0x37 unset).
    FSpawnPlan Plan;
    // +0x48: the wander guide's zone (CreateGuide case 4 copies it to the guide's +0x2c; the
    // guide reads the zone id at +0 of it). Written by TU29 06ef4862.
    unsigned int const* WanderZone = nullptr;
    // +0x4c / +0x50: the goods the ship trades from the first target and back (TU44 cargo
    // generation 06f36b88-06f36b9c). The trade guide (FUN_06f06fe0) copies them to its
    // +0x2c / +0x30 and swaps them together with the two targets.
    unsigned int TradeGood = 0;
    unsigned int ReturnGood = 0;
    // +0x54 / +0x58: two object ids, +0x5c / +0x60: the bases reached from them
    // (FUN_06f06260 through FUN_06f06300). The guides of types 1-4 copy them as their two
    // targets (+0x14..+0x20).
    unsigned int TargetId1 = 0;                  // +0x54
    unsigned int TargetId2 = 0;                  // +0x58
    FContentBase* TargetBase1 = nullptr;         // +0x5c
    FContentBase* TargetBase2 = nullptr;         // +0x60

    // FUN_06f061b0.
    FGuideDesc()
        : FGuideDescBase(0, nullptr)
    {
    }

    // FUN_06f06200 (RET 8): the default values with Type and Ship.
    FGuideDesc(int InType, FContentNpcShip* InShip)
        : FGuideDescBase(InType, InShip)
    {
    }

    // FUN_06f062d0 (RET 0x10): *OutId1 = +0x54, *OutId2 = +0x58; the bases only when their
    // out-pointer is given (the +0x60 one only after a given +0x5c one).
    void GetTargets(unsigned int* OutId1, unsigned int* OutId2, FContentBase** OutBase1,
                    FContentBase** OutBase2) const
    {
        *OutId1 = TargetId1;
        *OutId2 = TargetId2;
        if (OutBase1 == nullptr)
            return;
        *OutBase1 = TargetBase1;
        if (OutBase2 == nullptr)
            return;
        *OutBase2 = TargetBase2;
    }

    // FUN_06f06260 (RET 0x10; caller TU53 FUN_06f63cc0 06f647e5, added by CNT-GS9): the two
    // target ids and their bases (FindTargetBase; ClassIds goes to it by value).
    void SetTargets(unsigned int const* Id1, unsigned int const* Id2,
                    std::vector<unsigned int> const* ClassIds, int const* Rep);
    // FUN_06f06300 (RET 0x18): the base of target *ObjectId -- through a jump gate / hole
    // (type 0x40) a random base of the destination system accepting *Rep's affiliation
    // (TU53 IsBaseOfAffiliation), else the object's docking target; FindContentBaseInfo of
    // it. ClassIds is passed by value and not read.
    FContentBase* FindTargetBase(unsigned int const* ObjectId,
                                 std::vector<unsigned int> ClassIds, int const* Rep);
};

// [MetaBehavior] object (base vtable 06fb8230 = the three head slots + Parse; ctor part
// FUN_06f06530 (Type, Ship) with +0x10 Line empty). The [MetaBehavior] section loader
// LoadMetaBehavior builds one per MB_GOTOGUIDE / MB_DOCKGUIDE value; the mission hands it to
// CreateGuide as the description of a guide of type 5 / 6.
class FMetaBehavior : public FGuideDescBase
{
public:
    TString<0x100> Line;                         // +0x10 the whole value line

    FMetaBehavior(int InType, FContentNpcShip* InShip)
        : FGuideDescBase(InType, InShip)
    {
        Line.Length = 0;
        Line.Data[0] = '\0';
    }

    // Slot 3 (+0x0c), base 06ebf6f0: nothing.
    virtual void Parse(INI_Reader* Reader)
    {
        (void)Reader;
    }
};

// MB_GOTOGUIDE (0x178, vtable 06fb82d8 {.., sdd 06f07c10, Parse 06f07c30}; ctor FUN_06f07b60,
// inlined in the loader; dtor FUN_06f06840). Values: name, x1 y1 z1, x2 y2 z2, int, [bool],
// [NORMAL | ALWAYS_EXECUTE].
class FMbGotoGuide : public FMetaBehavior
{
public:
    // FUN_06f07b60: head (Type 5, Ship 0), Priority 0, Legs empty, TargetName empty, the
    // three flags 0 / 0 / 1.
    FMbGotoGuide();
    // FUN_06f06840: frees Legs.
    ~FMbGotoGuide() override;

    // FUN_06f07c30 (RET 4).
    void Parse(INI_Reader* Reader) override;

    // +0x114: the priority the goto guide gives its Guide directives (their BaseOp Field_08):
    // "NORMAL" 1, "ALWAYS_EXECUTE" 0, else 0 from the ctor.
    int Priority = 0;
    std::list<FNnPathLeg> Legs;                  // +0x118 copied into the guide's path
    // +0x124 / +0x130: the ctor does not write them (the dock description never sets +0x130;
    // the dock guide replaces the copied value with its target's position). The port starts
    // them at 0 instead of heap garbage.
    Vector FirstPosition = {};                   // +0x124 values 1-3; guide +0x118
    Vector Destination = {};                     // +0x130 values 4-6 (goto only)
    float Range = 0.0f;                          // +0x13c value 7 (int); dock guide: value 5
    TString<0x30> TargetName;                    // +0x140 value 0
    unsigned char Field_0x174 = 0;               // +0x174 guide +0x16c. TODO: writer
    unsigned char Field_0x175 = 0;               // +0x175 guide +0x16d. TODO: writer
    unsigned char Field_0x176 = 1;               // +0x176 value 8 (bool); guide +0x16e
};

// MB_DOCKGUIDE (0x17c, vtable 06fb82b8 {.., sdd 06f07c10, Parse 06f0a030}; the loader runs
// FUN_06f07b60 and sets this vtable). Values: name, x y z, dock target nickname, float.
class FMbDockGuide : public FMbGotoGuide
{
public:
    FMbDockGuide() = default;

    // FUN_06f0a030 (RET 4).
    void Parse(INI_Reader* Reader) override;

    unsigned int DockTargetId = 0;               // +0x178 value 4 (GetNicknameId)
};

// FUN_06f065e0 (cdecl; callers TU38 06f107ea, 06f18455): one [MetaBehavior] section. Per value:
// "nickname" -> *OutNickname = CreateID(value); "system" -> ignored; MB_GOTOGUIDE /
// MB_DOCKGUIDE (stricmp on the value name) -> a new object (Type 5 / 6, Line = the whole
// line, then Parse) appended to List, which holds its one reference. Other values are
// skipped. Unlike LoadObjList, *OutNickname is not reset first.
void __cdecl LoadMetaBehavior(INI_Reader* Reader, std::list<FMetaBehavior*>* List,
                              unsigned int* OutNickname);

// ---------------------------------------------------------------------------------------
// Guide base
// ---------------------------------------------------------------------------------------

// Guide base (retail vtable 06fb8240, 15 slots, 0x14 bytes; ctor FUN_06f06560). The NPC ship
// owns one at +0x178 (refcounted) and forwards its AI events to it.
class FContentGuide : public FContentRefObject
{
public:
    FContentNpcShip* Ship;                       // +0x08 desc +0x0c; SetShip
    int Type;                                    // +0x0c desc +0x08; GetType
    bool bDone = false;                          // +0x10 IsDone

    // FUN_06f06560 (RET 4): refcounted base (FContentRefObject, vtable 06fb43f8, count 1),
    // Ship / Type from the description, bDone 0, vtable 06fb8240 (slots 0-2 = the base's
    // AddRef 06eacd40, Release 06eacd50, sdd 06f06eb0).
    explicit FContentGuide(FGuideDescBase const* Desc)
        : Ship(Desc->Ship)
        , Type(Desc->Type)
    {
    }
    // The Clone slots start from a member-wise copy of the base (06f06f0a-06f06f25 and the
    // other Clone bodies copy the reference count too, so the copy starts with the source's
    // count).
    FContentGuide(FContentGuide const& Other) = default;

    // Slot 2 (06f06eb0).
    ~FContentGuide() override = default;
    // +0x0c purecall. Name from the bodies: every override is operator new(own size) plus a
    // member-wise copy of this (06f06ed0, 06f07180, 06f0adb0, 06f0b350, 06f08140, 06f0a3b0).
    virtual FContentGuide* Clone() = 0;
    // +0x10 purecall. Name from the bodies: every override hands directives to the ship
    // (CancelDirectives + AddDirective + RestartDirectives 06f06fa0 / 06f0bb14, SetDirectives
    // 06f072db / 06f09f5f); the NPC ship calls it when it has no directives (06f33c74,
    // 06f34b59, 06f34be1).
    virtual void Start() = 0;
    // +0x14 purecall. Name from the bodies: they only zero the state (06f061a0 +0x24 = 0,
    // 06f0aa10 +0x28 = 0, 06f087e0 state 1 / previous 0); FUN_06f34b70's third argument.
    virtual void Reset() = 0;
    // +0x18 purecall. Name from the caller: the ship's DirectiveCallback forwards here when
    // its own directive list is empty (06f33fbe); 06f0af30 restarts (Start).
    virtual void OnDirectivesDone(int ResultWord11c, int ResultWord118) = 0;
    // +0x1c, 06ea1490 (`RET 8`). Name from the caller: SpaceSupport's message id and parms,
    // forwarded by FUN_06f34c30; the goto guide switches on message ids (06f08d0f).
    virtual void HandleEvent(pub::Controller::EMessage MsgId, void const* Parms)
    {
        (void)MsgId;
        (void)Parms;
    }
    // +0x20, 06ebf700: +8 = Ship.
    virtual void SetShip(FContentNpcShip* InShip)
    {
        Ship = InShip;
    }
    // +0x24, 06f06190: byte +0x10.
    virtual bool IsDone()
    {
        return bDone;
    }
    // +0x28, 06fa8130: +0xc.
    virtual int GetType()
    {
        return Type;
    }
    // +0x2c, 06ebf6f0 (`RET 4`): the ship's scan_state_change.
    virtual void OnScanStateChange(bool bScanning)
    {
        (void)bScanning;
    }
    // +0x30, 06f06590 (`XOR AL,AL; RET 0x10`): the ship's request_flee_destination.
    virtual bool RequestFleeDestination(pub::AI::ContentCallback::FleeDestinationType& OutType,
                                        Vector& OutPosition, unsigned int& OutParam,
                                        float& OutFloat)
    {
        (void)OutType;
        (void)OutPosition;
        (void)OutParam;
        (void)OutFloat;
        return false;
    }
    // +0x34, 06f065a0 (RET 0x10): *OutId2 = *OutId1 = 0, *OutBase1 / *OutBase2 = 0 when
    // given (each tested on its own).
    virtual void GetTargets(unsigned int* OutId1, unsigned int* OutId2, FContentBase** OutBase1,
                            FContentBase** OutBase2)
    {
        *OutId2 = 0;
        *OutId1 = 0;
        if (OutBase1 != nullptr)
            *OutBase1 = nullptr;
        if (OutBase2 != nullptr)
            *OutBase2 = nullptr;
    }
    // +0x38, 06ebf720 (`RET`). Name from the overrides: the goto / dock guides recompute
    // their target id from its name here (06f07ec0, 06f0a5d0: FindObjectIdByName).
    virtual void RefreshTarget()
    {
    }
};

// FUN_06f05f00 (cdecl; callers NPC ship 06f33c58 / 06f34b3b, mission 06f10acf): build the
// guide of Desc->Type (1..6) and store it in *InOutGuide; the previous guide is released
// (any other type leaves *InOutGuide null). Returns *InOutGuide != null.
bool __cdecl CreateGuide(FGuideDescBase* Desc, FContentGuide** InOutGuide);
