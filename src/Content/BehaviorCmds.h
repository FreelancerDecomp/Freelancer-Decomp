// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU50 (06f56f50-06f57cb0): the ship base class shared by NPC ships (TU43,
// ctor FUN_06f336a0, 0x5b0 bytes) and the player ship (TU46, ctor FUN_06f471d0, 0x190
// bytes). It derives from the TU20 space object (NpcBase.h) and from pub::AI::DirectiveCallback
// at +0x118, and runs a list of [ObjList] directives (ObjListDirective.h).
//
// Retail: ctor FUN_06f571a0 (RET 4), dtor body FUN_06f33d20 (in TU43's range), size 0x174.
// Primary vtable 06fbba08:
//   +0x00 06f572f0 scalar deleting dtor
//   +0x04 06ea1480 returns false (TU43 overrides with 06ea1460 = true); name unknown
//   +0x08 06f33cf0 returns byte +0x11c
//   +0x0c 06f57c30 returns &ActiveDirective (+0x16c)
//   +0x10 06f57c40 releases ActiveDirective
// Secondary vtable at +0x118: base 06fbba1c = {purecall}, final 06fbba04 = {06f57820}.
// The player ship's vtables (06fbb040/06fbb044) reuse every slot but the dtor.

#include "NpcBase.h"
#include "ObjListDirective.h"   // FObjListDirective, FMissionDirectiveList (+0x158)
#include "common_pub.h"   // pub::AI::BaseOp

#include <list>
#include <utility>
#include <vector>

struct FFormation;         // Encounters.h (TU31 formations.ini record, FUN_06efd1a0)

// Parameter block read by FUN_06f571a0 (retail 0x78 bytes). No constructor of its own: the
// two derived blocks initialise it inline — the NPC ship's FUN_06f33500 (TU43, NpcShip.h)
// and the player ship's FUN_06efff70 (GameSupport) — both with TU20 parameters at +0, then
// +0x44 = 0, +0x48 = -1, +0x4c = -1, +0x50 vector<uint> (FUN_06ead6a0), +0x60 list of
// directives (FUN_06f1d4c0), +0x6c/+0x70/+0x74 = 0.
struct FContentShipParams : public FContentSpaceObjParams
{
    FFormation* Formation = nullptr;       // +0x44 -> ship +0x150
    unsigned int Field_0x48 = 0xffffffffu; // +0x48 -> ship +0x14c
    unsigned int Field_0x4c = 0xffffffffu; // +0x4c -> ship +0x154
    std::vector<unsigned int> ClassIds;    // +0x50 -> ship +0x128
    // +0x60: the directives the NPC ship starts with (FUN_06f336a0 06f33bb1 hands it to
    // SetDirectives), one reference held on each. Retail's member is the ref-holding list
    // class: its out-of-line copy ctor FUN_06ef7e30 is what the implicit NpcShipParams copies
    // 06ef7bd0 / 06f6a880 call for +0x60 (TU40 06f23a30 and 06fa4c80 use it too).
    FMissionDirectiveList Directives;
    unsigned int ControllerId = 0;         // +0x6c -> ship +0x170
    int Priority = 0;                      // +0x70 -> ship +0x168
    unsigned int ShipArchId = 0;           // +0x74 -> ship +0x138

    FContentShipParams() = default;
    // FUN_06f73680 (RET 4; caller TU58 06f7330d) is the implicit copy: the TU20 part (handle
    // AddRef'ed), +0x44..+0x4c, the ClassIds copy, the FMissionDirectiveList copy (FUN_06ef7e30
    // inlined: each directive AddRef'ed), +0x6c..+0x74.
    FContentShipParams(FContentShipParams const& Other) = default;
    FContentShipParams& operator=(FContentShipParams const&) = delete;
    // FUN_06ef4ab0 (callers 06ef5289, 06efc171, 06efc280, 06f735ec) is the implicit dtor:
    // ~FMissionDirectiveList inlined (Release, slot 1, on every directive, erase, the inner
    // list's head FUN_06f15350 = EH state 2), ClassIds freed, then FUN_06f28e10 on +0.
    ~FContentShipParams() = default;
};

// The +0x118 secondary base is pub::AI::DirectiveCallback (common_pub.h): one pure slot with
// two arguments, dispatched by Common's IBehaviorManager when a queued AI directive ends
// (retail base vtable 06fbba1c = {purecall}). TU50 implements it with FUN_06f57820, TU43
// with 06f33f90.
class FContentShipBase : public FContentSpaceObj, public pub::AI::DirectiveCallback
{
public:
    // FUN_06f571a0 (RET 4).
    explicit FContentShipBase(FContentShipParams const* Params);
    // FUN_06f33d20 (called by the sdd 06f572f0).
    ~FContentShipBase() override;

    // Slot 1 (06ea1480): XOR AL,AL. FContentSpaceObj +0x04.
    bool Slot04() override;
    // Slot 2 (06f33cf0): byte +0x11c.
    virtual bool Slot08();
    // Slot 3 (06f57c30): &ActiveDirective.
    virtual FObjListDirective** GetActiveDirectiveSlot();
    // Slot 4 (06f57c40): Release and clear ActiveDirective.
    virtual void ReleaseActiveDirective();

    // DirectiveCallback slot 0 (06f57820): if the directive list is not empty, advance it.
    void OnDirectiveComplete(int ResultWord11c, int ResultWord118) override;

    // FUN_06f33e40 (in TU43's range; callers SpaceSupport 06f5d2fc, 06f5e5eb, 06f5e5f6 on
    // ships that passed IsInShipClass): pub::Reputation::GetRank of the handle's
    // reputation, truncated to int (ftol).
    int GetReputationRank() const;
    // FUN_06f57150: pub::GetCargoHoldSize(ShipArchId).
    unsigned int GetCargoHoldSize() const;
    // FUN_06f57170: pub::SpaceObj::GetCargoSpaceOccupied(SpaceObjId).
    unsigned int GetCargoSpaceOccupied() const;
    // FUN_06f57310 (RET 8).
    void AddDamage(unsigned int const* AttackerId, float Amount);
    // FUN_06f57500 (RET 8).
    void GetTopDamagers(unsigned int* OutTopPlayer, unsigned int* OutTopAny) const;
    // FUN_06f575a0 (RET 4).
    bool HasClassId(unsigned int const* ClassId) const;
    // FUN_06f575d0 (RET 4): any member of the TU51 ship class is in ClassIds.
    bool IsInShipClass(unsigned int const* ShipClassId) const;
    // FUN_06f57640: the first member of this ship's formation (FUN_06f66220) when it is a
    // wingman (+0x14c != 0, +0x154 != -1), else this ship.
    FContentShipBase* GetFormationLeader();
    // FUN_06f576c0 (RET 0xc): +0x154 FormationId = InFormationId, +0x14c FormationSlot =
    // InFormationSlot, +0x150 Formation = InFormation.
    void SetFormation(unsigned int InFormationId, unsigned int InFormationSlot,
                      FFormation* InFormation);
    // FUN_06f576f0 (RET 8): TradeLaneEntryRing = *Value120, TradeLaneEndRing = *Value124.
    // GameSupport msg 0x16 (06efee22 / 06efee5f) sets {entry ring, last or first ring of the
    // lane in the travel direction}; msg 0x17 (06efed3f) clears both to 0 (DAT_06fb7d60).
    void SetField120(unsigned int const* Value120, unsigned int const* Value124);
    // FUN_06f57710: TradeLaneEntryRing != 0, i.e. the ship is travelling a trade lane.
    bool HasField120() const;
    // FUN_06f57720 (RET 8): *Out120 = TradeLaneEntryRing, *Out124 = TradeLaneEndRing
    // (readers: SpaceSupport 06f5d959 -> RemoveTradeLaneShip; TU58 06f73184 / 06f73260).
    void GetField120(unsigned int* Out120, unsigned int* Out124) const;
    // FUN_06f57740: cancel the AI directive, release and drop every queued directive.
    void CancelDirectives();
    // FUN_06f577d0 (RET 4): push_back + AddRef.
    void AddDirective(FObjListDirective* Directive);
    // FUN_06f57840 (RET 8): replace the directive list and the priority, then start it.
    void SetDirectives(std::list<FObjListDirective*> const* NewList, int InPriority);
    // FUN_06f57b00: drop the current directive and start the next one.
    void AdvanceDirective();
    // FUN_06f57c00: CurrentDirective = end(); AdvanceDirective().
    void RestartDirectives();
    // FUN_06f579c0 (RET 4; callers SpaceSupport 06f5e7d5, TU53 06f6da9e; added by CNT-GS8):
    // cancel the AI directive, take Leader's directive list (shared, AddRef'ed) and start it;
    // a leading Dock directive is pointed at this ship's own docking terminal when it is in
    // the docking list.
    void FollowDirectivesOf(FContentShipBase* Leader);
    // FUN_06f14400 (RET 4, in TU38's range; callers TU38 06f134f6 / 06f137fc / 06f13b36 /
    // 06f13f2a, TU53 06f6db5c; added by CNT-GS8): a copy of the directive list, each
    // directive AddRef'ed (the caller releases them).
    std::list<FObjListDirective*> CopyDirectives() const;
    // FUN_06f57c20.
    bool HasNoDirectives() const;
    // FUN_06f57c60 (RET 4): pub::AI::SubmitDirective(SpaceObjId, Op) == 1.
    bool SubmitDirective(pub::AI::BaseOp* Op);
    // FUN_06f57c80.
    int GetPriority() const;
    // FUN_06f57c90 (RET 4).
    void SetPriority(int const* InPriority);

    // +0x11c (slot 2 reads it): the ship is in combat -- NPC: combat_state_notification
    // (06f34090) stores Type == 0; player: set while hostile contacts exist (06f47804), cleared
    // when none are left (06f477c9). A byte in retail.
    unsigned char bInCombat = 0;
    // +0x11d: the ship (formation leader) is claimed as a patrol cargo-scan target (06f0c5ac
    // sets 1, 06f0ac2e clears; SpaceSupport 06f5df33 clears it for a player entering a
    // system). A byte in retail.
    unsigned char bScanTarget = 0;
    unsigned int TradeLaneEntryRing = 0;                          // +0x120 0 = not in a lane
    unsigned int TradeLaneEndRing = 0;                            // +0x124
    std::vector<unsigned int> ClassIds;                           // +0x128
    unsigned int ShipArchId;                                      // +0x138
    std::vector<std::pair<unsigned int, float>> Damage;           // +0x13c
    // +0x14c: the ship's slot in Formation, 0 = the leader (params +0x48; written only by
    // SetFormation 06f576c0; TU53 06f671a0 passes it to FFormation::GetSlotPosition,
    // FUN_06efcb20; CollectFormationMembers 06f66220 sorts the members by it).
    unsigned int FormationSlot;
    // +0x150: the formation the ship flies in (params +0x44; TU53 06f671a0 calls
    // FUN_06efcb20 on it).
    FFormation* Formation;
    // +0x154: the id of the formation group the ship flies in, -1 = none (params +0x4c; ids
    // come from AllocateFormationId FUN_06efeb00 through SetFormation 06f576c0; equal on every
    // member, CollectFormationMembers 06f66220 gathers a group by it).
    unsigned int FormationId;
    // +0x158: the queued directives, one reference held on each. A ref-owning member: retail's
    // unwind maps tear it down with FUN_06ecfa90 (Release each + erase; ~FContentShipBase
    // FuncInfo 06fc3b30 state 3, ~FContentNpcShip 06fc3b8c state 6), after the dtor body's
    // ActiveDirective release and before Damage / ClassIds / the base.
    FMissionDirectiveList Directives;                             // +0x158
    // +0x164: retail leaves it unset in the ctor (CancelDirectives / RestartDirectives /
    // the list setters store end() before advancing). The port starts it at end() so the
    // iterator is never singular.
    std::list<FObjListDirective*>::iterator CurrentDirective;
    int Priority;                                                 // +0x168
    FObjListDirective* ActiveDirective = nullptr;                 // +0x16c
    // +0x170: the owning controller (mission, SpacePop, ...): ShipInfo::ControllerId of the
    // created ship (TU43 06f33904) and the target of the NPC ship's 0x1006/0x100a/0x1011
    // messages.
    unsigned int ControllerId;
};
