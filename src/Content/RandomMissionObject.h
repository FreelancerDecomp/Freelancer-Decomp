// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The random-mission controller objects (docs/re/content.md, TU map, lane CNT-RM):
//   TU47 (06f4b0d0-06f4d220)  FRandomMissionObject, the base of every random mission
//                             controller; bodies in RandomMissionDB.cpp.
//   TU27 (06eef3d0-06ef04d0)  FDestroyMission, the "DestroyMission" controller that
//                             CreateInstance builds (FUN_06eeec50 06eeed28); bodies in
//                             DestroyMission.cpp.
//
// Retail layout: the TU38 mission object (0x1b0 bytes, FMissionObject) followed by a Common
// StateMachineGroup at +0x1b0 (its StateMachine vtable at +0x1b0, its MessageHandler vtable at
// +0x1c0, 0x34 bytes). The port expresses that as the second base class. Retail has no RTTI in
// Content.dll; the class names are the port's.

#include "MissionObject.h"
#include "RandomMission.h"      // FRandomMissionData, TRmRef
#include "common_net.h"

#include <map>
#include <string>
#include <vector>

struct FContentPlayerState;
struct FRandomMissionData;
struct FSystemInfo;

// FUN_06ef00d0 (TU27, thiscall on the player state): the state has a ship object (+0x10).
// Callers DestroyMission 06eef922 / 06eef972 and TU61 06f8f99a.
bool RmPlayerHasShip(FContentPlayerState const* State);

// ---------------------------------------------------------------------------------------
// TU47 -- random mission object base (retail 0x240 bytes). Ctor FUN_06f4b0d0 (RET 8), dtor
// FUN_06f4b460, deleting dtor FUN_06f4b300. Vtables: 06fbb288 (45 slots), StateMachine side
// 06fbb26c (slot 2 = FUN_06f4bf30), MessageHandler side 06fbb258. Only DestroyMission
// derives from it (its ctor is the only caller of 06f4b0d0).
// ---------------------------------------------------------------------------------------
class FRandomMissionObject : public FMissionObject, public StateMachineGroup
{
public:
    // FUN_06f4b0d0 (RET 8): InData is the controller's create parms (the TU27 mission).
    FRandomMissionObject(unsigned int const* InControllerId, FRandomMissionData* InData);
    // FUN_06f4b460 (called by the deleting dtor FUN_06f4b300).
    ~FRandomMissionObject() override;

    // ---- TU38 slots this class replaces ----
    // Slot 2, LAB_06f4b650 (RET 4): the base record (FUN_06f1b6d0); a record whose slot 2 is
    // set gets slot 1(0).
    FMissionPlayer* GetOrCreatePlayer(unsigned int InPlayerId) override;
    // Slot 18, FUN_06f4c3b0 (RET 4): rebuild the player map / vector from the new player's group.
    void SetPlayer(unsigned int InPlayerId) override;
    // Slot 21, LAB_06f4b680 (RET 8): the message goes to the state machine group as an
    // SMControllerEvent; the TU38 body runs from Slot2.
    void HandleMissionMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override;
    // Slot 22 is 06f4cd40, a JMP to the TU38 body FUN_06f0fc40: no override.
    // Slot 23, FUN_06f4c9c0: victory music, completed statistics, reputation, then the TU38 body.
    void OnMissionSucceed() override;

    // ---- slots 29..44 ----
    // Slot 29, LAB_06f4b6c0: &+0x1fc.
    virtual std::vector<FContentPlayerState*>* GetPlayerStates();
    // Slot 30, LAB_06f4b6d0: &+0x20c.
    virtual std::map<unsigned int, FContentPlayerState*>* GetPlayerStateMap();
    // Slot 31, LAB_06f4b7b0 (RET 8): the type 8 "other players" objective of Player.
    virtual bool UpdatePlayerCountObjective(FMissionPlayer* Player, bool* bChanged);
    // Slots 32..34, LAB_06f4bd00 / LAB_06f4bd10 / LAB_06f4bd20: --, ++ and read +0x220.
    virtual int DecrementPlayerCount();
    virtual int IncrementPlayerCount();
    virtual int GetPlayerCount();
    // Slot 35, FUN_06f4c6f0 (RET 4): the mission failed (Reason 1 = aborted).
    virtual void FailMission(int Reason);
    // Slot 36, FUN_06f4c830 (RET 8): InPlayerId leaves the mission.
    virtual void RemoveMissionPlayer(unsigned int InPlayerId, int Reason);
    // Slot 37, FUN_06f4b6e0: slot 38 for every player.
    virtual void RefreshAllObjectives();
    // Slot 38, LAB_06f4b720 (RET 4): slots 40, 39 and (multiplayer) 31, then resend.
    virtual void RefreshObjectives(unsigned int InPlayerId);
    // Slot 39, LAB_06f4b870 (RET 8): the type 2 "return to space" objective of a docked player.
    virtual bool UpdateDockedObjective(FMissionPlayer* Player, bool* bChanged);
    // Slot 40, FUN_06f4b9e0 (RET 8): the path objectives to the mission's system (slot 8).
    virtual bool UpdatePathObjectives(FMissionPlayer* Player, bool* bChanged);
    // Slot 41, LAB_06f4cd50 (RET 0x14): reputation change towards Group, once per player name
    // and group unless bForce.
    virtual void ChangePlayerReputation(unsigned int InPlayerId, unsigned int Group, float Delta,
                                        bool bForce, bool bEmpathy);
    // Slot 42, LAB_06f4cf50 (RET 0xc): reputation change with empathy, always.
    virtual void ChangePlayerReputationAlways(unsigned int InPlayerId, unsigned int Group,
                                              float Delta);
    // Slot 43, FUN_06f4cf80 (RET 0xc): slot 41 (bForce false) for every player of the map.
    virtual void ChangeGroupReputation(unsigned int Group, float Delta, bool bEmpathy);
    // Slot 44, FUN_06f4d010 (RET 8): slot 42 for every player of the map.
    virtual void ChangeGroupReputationAlways(unsigned int Group, float Delta);

protected:
    // StateMachine slot 2, FUN_06f4bf30 (RET 8): the "any state" handler of the controller
    // messages. Called directly by the DestroyMission override (06eef740) with State -1.
    bool Slot2(SMEventData const* Event, int const& State) override;

    // FUN_06f4be60 (RET 8): the +0xd0 mission flags of a mapped player for SystemId.
    void UpdatePlayerSystemFlags(unsigned int InPlayerId, unsigned int SystemId);
    // FUN_06f4bd30 (RET 4): message 5 for a mapped player (rejoin).
    void RejoinPlayer(unsigned int InPlayerId);
    // FUN_06f4c610 (RET 4): message 0x24 (group left).
    void OnPlayerLeftGroup(unsigned int const* InPlayerId);
    // FUN_06f4caa0 (RET 4): message 0xa (a group member joins).
    void AddGroupMember(unsigned int InPlayerId);
    // FUN_06f4d090 (RET 8): a +0x1e4 entry at or after the player's name holds Group.
    bool HasReputationEntry(unsigned int InPlayerId, unsigned int Group);
    // The unique-name insert inlined in FUN_06f4c3b0 and FUN_06f4caa0 (06f4c4c8-06f4c5b3 /
    // 06f4cb76-06f4cc30): +0x220 grows while fewer than 8 names are listed. The name
    // (FUN_06f480e0) is the caller's local: retail frees it at the end of each SetPlayer loop
    // pass (06f4c5be) but only at the very end of AddGroupMember (06f4ccea).
    void AddPlayerName(std::wstring const& Name);

    // +0x1e4 multimap (ctor FUN_06f4df60 with the multi flag 1): player name -> group whose
    // reputation slot 41 already changed.
    std::multimap<std::wstring, unsigned int> ReputationChanges;
    // +0x1f8: the controller's create parms, one reference in a holder (AddRef in the ctor
    // 06f4b14c; the holder dtor FUN_06fa0330 runs after +0x1fc, inlined at 06f4b54a-06f4b565).
    TRmRef<FRandomMissionData> Data;
    // +0x1fc: the player states of the map below, in key order (rebuilt on every change).
    std::vector<FContentPlayerState*> PlayerStates;
    // +0x20c map (ctor FUN_06f4dae0): player id -> player state.
    std::map<unsigned int, FContentPlayerState*> PlayerStateMap;
    // +0x220: players counted for the type 8 objective (see AddPlayerName).
    int PlayerCount = 0;
    // +0x224: the names seen (vector of wstring, elements destroyed by FUN_06f4eaf0).
    std::vector<std::wstring> PlayerNames;
    // +0x234: seconds since the start (Slot2, message 0x20); -1.0f once it reached the
    // mission's +0x48 (360.0f), after which the +0xd0 system flags are no longer updated.
    // Ctor 0.
    float ElapsedTime = 0.0f;
    // +0x23c: fail reason stored by the DestroyMission state machine (06eefe9d), ctor 0.
    int FailReason = 0;
};

// ---------------------------------------------------------------------------------------
// TU27 -- the "DestroyMission" controller (retail 0x29c bytes). Ctor FUN_06eef3d0 (RET 8),
// dtor FUN_06eef550, deleting dtor FUN_06eef530. Vtables: 06fb7658 (46 slots; replaces 1, 8,
// 9, 12, 22, 23, 35, 36 and adds 45), StateMachine side 06fb7638 (slot 2 = LAB_06eef740),
// MessageHandler side 06fb7624.
// ---------------------------------------------------------------------------------------
class FDestroyMission : public FRandomMissionObject
{
public:
    // FUN_06eef3d0 (RET 8).
    FDestroyMission(unsigned int const* InControllerId, FRandomMissionData* InData);
    // FUN_06eef550.
    ~FDestroyMission() override;

    // Slot 8, LAB_06eef6e0: the first runtime machine's slot 8, else the system id of +0x244.
    int Slot08() override;
    // Slot 9, LAB_06eef730: the jump preference of the mission (+0x240 +0x50).
    int Slot09() override;
    // Slot 12: LAB_06ea1480 (XOR AL,AL; RET) where the TU47 table has LAB_06ea1460.
    bool Slot12() override;
    // Slot 22, FUN_06ef0480: slot 7 of every +0x28c machine, then the TU38 body.
    void EndMission() override;
    // Slot 23, LAB_06ef0460: +0x10 = the mission reward, then the TU47 body.
    void OnMissionSucceed() override;
    // Slot 35, FUN_06ef02a0 (RET 4): the reason text to the first player, then the TU47 body.
    void FailMission(int Reason) override;
    // Slot 36, FUN_06ef0390 (RET 8): the reason text to the player, then the TU47 body.
    void RemoveMissionPlayer(unsigned int InPlayerId, int Reason) override;
    // Slot 45, LAB_06ef01d0 (RET 4): the resource id of a fail reason (0..14), else 1.
    virtual unsigned int GetReasonIds(int Reason);

protected:
    // StateMachine slot 2, LAB_06eef740 (RET 8): states 0 (brief), 1 (launch the runtime
    // machines), 2 (running), 3 (succeeded), 4 (failed) and the "any state" messages.
    bool Slot2(SMEventData const* Event, int const& State) override;

    // FUN_06ef0100: a runtime machine per offer of the mission, added to the group; the new
    // handler ids go to +0x26c.
    void CreateRuntimeMachines();

    // +0x240: one reference in a holder (AddRef in the ctor 06eef411; the holder dtor
    // FUN_06fa0330 runs after the +0x248 list, inlined at 06eef693-06eef6ad).
    TRmRef<FRandomMissionData> MissionData;
    FSystemInfo* DestSystem = nullptr;                  // +0x244 FindSystemInfo(&DestSys)
    // +0x248: the owned objective list (FUN_06f1ca70, then the derived vptr 06fb478c at
    // 06eef447); its +0x10 Owner (+0x258) = 0 at 06eef44e.
    FMissionOwnedObjectiveList Objectives;
    // +0x25c: set by message DAT_06fd3bac (06eef8af). Retail's ctor leaves it as heap
    // garbage; the port starts it at 0. TODO: meaning from its readers.
    unsigned char Field_0x25c = 0;
    std::vector<unsigned int> PendingMachines;          // +0x26c ids not started yet
    std::vector<unsigned int> StartingMachines;         // +0x27c ids sent DAT_06fd3bbc
    std::vector<unsigned int> RunningMachines;          // +0x28c ids that answered DAT_06fd3bb8
};
