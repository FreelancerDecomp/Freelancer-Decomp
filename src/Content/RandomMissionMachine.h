// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// TU61 -- the random-mission runtime machine (docs/re/content.md, TU map, lane CNT-RM). One machine per
// offer of a DestroyMission (FDestroyMission::CreateRuntimeMachines, FUN_06ef0100 -> the factory
// FUN_06f890f0). The machine is a TU57 machine group (FRmMachineGroup) holding three sub-machines
// that keep the records of what it spawned (solars, ships, loot), the mission objectives it shows,
// and the TU57 triggers of its comm sequences. Bodies in RandomMissionMachine.cpp.

#include "MissionObject.h"          // FMissionObjectiveList, FMissionObject, FMissionPlayer
#include "RandomMission.h"          // RmRefAddRef / RmRefRelease (FRmOffer) for TRmRef
#include "RandomMissionOffer.h"     // FRmOffer, FRmVignetteOffer, FRmLootSpawn, FRmSolarSpawn
#include "RandomMissionRuntime.h"   // FRmMachineGroup, FRmObjective, IRmTextParamSource
#include "common_net.h"
#include "common_tstring.h"
#include "x86math.h"

#include <list>
#include <map>
#include <vector>

class FContentNpcShip;              // NpcShip.h
class FRandomMissionObject;         // RandomMissionObject.h
struct FContentPlayerState;         // ContentPlayerState.h

// =======================================================================================
// Messages of the sub-machines. Every class shares the deleting dtor FUN_06f86dc0 (see
// FRmMachineFailedMessage); clone = StateMachineMessage::operator new, the exported copy ctor
// and the field copies. The message type is the caller's (the GRmMsgType* globals).
// =======================================================================================

// Vtable 06fbc804 (0x20 bytes, clone FUN_06f87cb0): a solar the solar machine tracked was
// destroyed (type DAT_06fd3b9c, 06f86a5a).
class FRmSolarMessage : public StateMachineMessage
{
public:
    FRmSolarMessage(unsigned int Type, unsigned int InObjectId);
    StateMachineMessage* clone() const override;

    unsigned int ObjectId;                          // +0x1c
};

// Vtable 06fbc80c (0x24 bytes, ctor FUN_06f86c80 (RET 0xc), clone FUN_06f86cb0): loot events
// (types DAT_06fd3b98 / b94 / b90 / b8c / b84 / b80).
class FRmLootMessage : public StateMachineMessage
{
public:
    FRmLootMessage(unsigned int Type, unsigned int InObjectId, unsigned int InValue);
    StateMachineMessage* clone() const override;

    unsigned int ObjectId;                          // +0x1c
    unsigned int Value;                             // +0x20 a player id, a count or 0
};

// Vtable 06fbc814 (0x28 bytes, clone FUN_06f86d20): the loot machine's message 0x27 answer
// (type DAT_06fd3b88, 06f87612-06f87645).
class FRmLootPlayerMessage : public StateMachineMessage
{
public:
    FRmLootPlayerMessage(unsigned int Type, unsigned int InObjectId, unsigned int InValue0,
                         unsigned int InValue1);
    StateMachineMessage* clone() const override;

    unsigned int ObjectId;                          // +0x1c
    unsigned int Value0;                            // +0x20 message 0x27 parms word 0
    unsigned int Value1;                            // +0x24 message 0x27 parms word 1
};

// Vtable 06fbc87c (0x24 bytes, clone FUN_06f87c40): ship events of the ship machine (types
// DAT_06fd3ba8 / ba4 / ba0, 06f87f05-06f88032).
class FRmShipMessage : public StateMachineMessage
{
public:
    FRmShipMessage(unsigned int Type, unsigned int InGroup, unsigned int InObjectId);
    StateMachineMessage* clone() const override;

    unsigned int Group;                             // +0x1c the ship record's +0x64
    unsigned int ObjectId;                          // +0x20
};

// Vtable 06fbc884 (0x20 bytes, ctor FUN_06f87d10 (RET 4) with type DAT_06fd3b78, clone
// FUN_06f87d40). Senders 06f8c174 (FUN_06f8beb0) and 06f8db3c (FUN_06f8c460).
class FRmCookieMessage : public StateMachineMessage
{
public:
    explicit FRmCookieMessage(unsigned int InCookie);
    StateMachineMessage* clone() const override;

    unsigned int Cookie;                            // +0x1c
};

// Vtable 06fbc6d0 (0x20 bytes): a comm sequence conversation on its way to the runtime machine
// (type DAT_06fd3b74). Built on the stack by FRmOfferComponent::SendConversations (06f84f8a);
// the dtor FUN_06f87dc0 (deleting FUN_06f87da0) deletes the conversation through its slot 0,
// the clone FUN_06f87e10 clones it (slot 5). No null test in the clone: a failed new reads
// +0x1c of null.
class FRmConversationMessage : public StateMachineMessage
{
public:
    // 06f84f66-06f84f92: StateMachineMessage(DAT_06fd3b74) first (EH state 0), then +0x1c =
    // Source's slot 5 Clone (06f84f83), then the vptr (state 1).
    explicit FRmConversationMessage(FRmConversationBase* Source);
    // The shallow member copy inlined in FUN_06f87e10 (06f87e47-06f87e56, EH state 0 only);
    // clone() replaces +0x1c with its Clone afterwards, outside that state.
    FRmConversationMessage(FRmConversationMessage const& Other) = default;
    FRmConversationMessage& operator=(FRmConversationMessage const&) = delete;
    ~FRmConversationMessage() override;             // FUN_06f87dc0
    StateMachineMessage* clone() const override;    // FUN_06f87e10

    FRmConversationBase* Conversation;              // +0x1c owned
};

// =======================================================================================
// Records of the spawned objects (base vtable 06fbc7c8; derived 06fbc800 / 06fbc850 / 06fbc878
// share the deleting dtor FUN_06f869e0 = FUN_06f87bf0, a second copy of the base dtor).
// =======================================================================================

// TU61 -- record base (retail 0x60 bytes, vtable 06fbc7c8: [0] FUN_06f866e0 / dtor
// FUN_06f86700). ctor FUN_06f866a0. The copy ctor FUN_06f91310 and its inline copies
// (06f92440, 06f945f0) are the implicit member copy.
class FRmSpawnRecord
{
public:
    FRmSpawnRecord();                               // FUN_06f866a0
    virtual ~FRmSpawnRecord();                      // FUN_06f86700 / FUN_06f87bf0

    bool Field_0x04 = true;                         // +0x04 TODO: meaning (ctor 1)
    TString<0x30> Name;                             // +0x08 empty (Length 0, Data[0] 0)
    // +0x3c: 2 alive, 3 destroyed (message 0x39), 4 loot gone without a player (FUN_06f86fb0).
    int State = 2;
    unsigned int Flags = 0;                         // +0x40 bit 1: counted by FUN_06f87e90
    // +0x44: the attitude towards the players (FUN_06f8b930), -2.0 (DAT_06fbc2e8) = none.
    float Attitude = -2.0f;
    unsigned int ObjectId = 0;                      // +0x48 the SpaceObj id
    // +0x4c: 0 here; the ship records keep their NPC ship in it (FUN_06f8a820 06f8a8f6 reads
    // +0x04 / +0x14c / +0x154 / +0x1bc of it, FUN_06f8b930 its reputation).
    FContentNpcShip* NpcShip = nullptr;
    std::vector<FRmLootSpawn> Loot;                 // +0x50 (alloc byte) / +0x54..+0x5c
};

// TU61 -- solar record (retail 0x68 bytes, vtable 06fbc800). Built by FUN_06f926b0 (06f92ccd)
// and FUN_06f92ed0. The solar machine keeps them.
class FRmSolarRecord : public FRmSpawnRecord
{
public:
    int SolarType = -1;                             // +0x60 FRmSolarSpawn::SolarType (0 = big)
    int Reputation = -1;                            // +0x64 the pub::Reputation::Alloc handle
};

// TU61 -- ship record (retail 0x68 bytes, vtable 06fbc878). Built by FUN_06f915a0 (06f9198d);
// copy ctors FUN_06f92440 / FUN_06f945f0. The ship machine keeps them.
class FRmShipRecord : public FRmSpawnRecord
{
public:
    // +0x60: 2 the big ship, 1 a defensive ship (FUN_06f89e90 speaker kinds 3 / 4). TODO: the
    // other values.
    int Kind = 0;
    unsigned int Group = 0;                         // +0x64 the ship group's serial
};

// TU61 -- loot record (retail 0x7c bytes, vtable 06fbc850). Built by FUN_06f90f60 (06f911c3).
// The loot machine keeps them.
class FRmLootRecord : public FRmSpawnRecord
{
public:
    // +0x60: the object the loot was spawned next to (FRmLootSpawn::Info.OwnerObjId,
    // FUN_06f90f60 06f91153); FUN_06f877c0 filters on it.
    unsigned int ParentId = 0;
    int Units = 0;                                  // +0x64 the loot's units left (msg 0x18)
    int Kind = 0;                                   // +0x68 FRmLootSpawn::Kind (2 critical)
    int Field_0x6c = 0;                             // +0x6c FRmLootSpawn::Field_0x100
    unsigned int HolderId = 0;                      // +0x70 the player holding it, 0 = none
    unsigned short CargoSlot = 0;                   // +0x74 SUBOBJ_ID_NONE / msg 0x18 word
    // +0x78: the good (msg 0x18 parms +0xc; GetArchetypeID turns it back into loot).
    unsigned int GoodId = 0;
};

// TU61 -- list of records (retail 0x10 bytes, vtable 06fbc7cc: [0] FUN_06f86800 / dtor
// FUN_06f867a0 (frees the nodes only), [1] DeleteAll FUN_06f86820, [2] FindById FUN_06f86850,
// [3] FindByName FUN_06f86880). ctor FUN_06f90b10 (inline in the sub-machine ctors). The
// queries of the sub-machines return a copy of a local list (non-owning; 06f86bbe-06f86c06).
class FRmRecordList
{
public:
    FRmRecordList() = default;                      // FUN_06f90b10
    virtual ~FRmRecordList() = default;             // FUN_06f867a0
    // Slot 1, FUN_06f86820: every record deleted through its slot 0 (the nodes stay).
    virtual void DeleteAll();
    // Slot 2, FUN_06f86850 (RET 4): the first record with ObjectId == Id; 0 never matches.
    virtual FRmSpawnRecord* FindById(unsigned int Id);
    // Slot 3, FUN_06f86880 (RET 0x34): the first record whose name equals InName (strcmp).
    // Retail first tests &record->Name.Data for null, which never is.
    virtual FRmSpawnRecord* FindByName(TString<0x30> InName);

    std::list<FRmSpawnRecord*> Records;             // +0x04 (alloc byte) / +0x08 head / +0x0c size
};

// =======================================================================================
// Sub-machines (retail 0x30 bytes). Abstract base vtable 06fbc7a4 ([0] FUN_06f86680 (dtor
// FUN_06f92e80: ~MessageHandler, ~StateMachine), [1] process, [2] purecall, [3]
// handle_state_change; MessageHandler side 06fbc7b4). The base part is inline in the runtime
// machine ctor (06f89b01-06f89b1a): the owner's handler id, then StateMachineMessageHandler.
// =======================================================================================
class FRmSubMachine : public StateMachineMessageHandler
{
public:
    explicit FRmSubMachine(unsigned int InOwnerId);

    // The runtime machine's sends through a sub-machine (e.g. 06f9127a-06f9128d: the sub-
    // machine's MessageHandler slot 2, send_immediate_message, to its OwnerId); public so the
    // runtime machine can reach the protected MessageHandler slot.
    void SendToOwner(StateMachineMessage* Message);

    unsigned int OwnerId;                         // +0x1c the runtime machine's handler id
};

// TU61 -- the solar machine (vtable 06fbc7dc: [0] FUN_06f86900 / dtor FUN_06f86920, [2]
// LAB_06f86a00; MessageHandler side 06fbc7ec). The runtime machine's +0x58.
class FRmSolarMachine : public FRmSubMachine
{
public:
    explicit FRmSolarMachine(unsigned int InOwnerId);
    // FUN_06f86920: Records.DeleteAll (slot 1), then the members and the bases.
    ~FRmSolarMachine() override;

    // LAB_06f86a00 (RET 8): state 0, message 0x39 (destroyed) of a record -> State 3 and
    // FRmSolarMessage(DAT_06fd3b9c) to the owner.
    bool Slot2(SMEventData const* Event, int const& State) override;

    // FUN_06f86ae0 (RET 0x10): the records with Kind (-1 = any) == SolarType, State (0 = any)
    // == State and Mask (0 = any) & Flags.
    FRmRecordList Query(int Kind, int InState, unsigned int Mask);

    FRmRecordList Records;                          // +0x20 (vtable store 06fbc7cc)
};

// TU61 -- the ship machine (vtable 06fbc854: [0] FUN_06f87970 / dtor FUN_06f87990, [2]
// LAB_06f87e90; MessageHandler side 06fbc864). The runtime machine's +0x5c.
class FRmShipMachine : public FRmSubMachine
{
public:
    explicit FRmShipMachine(unsigned int InOwnerId);
    ~FRmShipMachine() override;                     // FUN_06f87990

    // LAB_06f87e90 (RET 8): state 0, message 0x39 of a record -> State 3, the group's
    // messages.
    bool Slot2(SMEventData const* Event, int const& State) override;

    // FUN_06f87a50 (RET 0x10): Kind (0 = any), State (0 = any), Mask (0 = any).
    FRmRecordList Query(int Kind, int InState, unsigned int Mask);
    // FUN_06f880c0 (RET 8): the records of Group (-1 gives an empty list).
    FRmRecordList QueryGroup(unsigned int Group);

    FRmRecordList Records;                          // +0x20
};

// TU61 -- the loot machine (vtable 06fbc82c: [0] FUN_06f86ed0 / dtor FUN_06f86ef0, [2]
// LAB_06f86fb0; MessageHandler side 06fbc83c). The runtime machine's +0x60.
class FRmLootMachine : public FRmSubMachine
{
public:
    explicit FRmLootMachine(unsigned int InOwnerId);
    ~FRmLootMachine() override;                     // FUN_06f86ef0

    // LAB_06f86fb0 (RET 8): state 0; messages DAT_06fd3b6c, 0x39, 0x18, 0x27, 0x3d.
    bool Slot2(SMEventData const* Event, int const& State) override;

    // FUN_06f877c0 (RET 0x18): every argument 0 = any; compared in the retail order Field60
    // (+0x60), HolderId (+0x70), Field6c (+0x6c), Kind (+0x68), State (+0x3c).
    FRmRecordList Query(int Field6c, int Kind, unsigned int Field60, int InState,
                        unsigned int InHolderId);

    FRmRecordList Records;                          // +0x20
};

// =======================================================================================
// TU61 triggers (FRmTrigger, RandomMissionRuntime.h, plus +0x40..; 12 slots: the base's 11 and
// a new slot 11). Built only by FRmVignetteMachine (FUN_06f8beb0, FUN_06f8c460) and added to
// it with FRmMachineGroup::AddStateMachine. No ctor, dtor or MessageHandler slot of their own:
// slot 0 is FUN_06f86680 (ICF with the sub-machine base) and the MessageHandler side is
// 06fbca2c / 06fbca70 ([0] LAB_06f94cb0, the this - 0x10 thunk).
// =======================================================================================

// Vtable 06fbca40 (0x48 bytes; MessageHandler side 06fbca2c). Fires once at most Threshold
// records of the list are alive.
class FRmShipCountTrigger : public FRmTrigger
{
public:
    // Inline at 06f8c059-06f8c0a5 / 06f8c0de-06f8c131 / 06f8dc5a-06f8dccc: FUN_06f70ff0
    // (bOneShot 1, RearmDelay 1.0), +0x40 = InRecords, +0x44 = InThreshold, the vtables.
    FRmShipCountTrigger(unsigned int InCookie, unsigned int InOwnerId, FRmRecordList* InRecords,
                        int InThreshold);

    // Slot 6, LAB_06f92dd0 (RET 8): in state 1, on an enter event or controller message 0x39,
    // slot 11 <= Threshold -> state 2; true for both, false for anything else.
    bool OnArmed(SMEventData const* Event, int const& State) override;
    // Slot 11, LAB_06f92e50: the records with Field_0x04 set and State 2.
    virtual int CountAlive();

    FRmRecordList* Records;                         // +0x40 the ship machine's list
    int Threshold;                                  // +0x44
};

// Vtable 06fbca84 (operator new(0x50) at 06f8dbb1, +0x4c never written; MessageHandler side
// 06fbca70). Fires on the health of the records of one ship kind.
class FRmShipHealthTrigger : public FRmTrigger
{
public:
    // Inline at 06f8dbb1-06f8dc2b: FUN_06f70ff0 (bOneShot 1, RearmDelay 1.0), +0x40 =
    // InRecords, +0x44 = InThreshold, +0x48 = InKind, the vtables.
    FRmShipHealthTrigger(unsigned int InCookie, unsigned int InOwnerId, FRmRecordList* InRecords,
                         int InThreshold, int InKind);

    // Slot 6, FUN_06f92ed0 (RET 8): in state 1: an enter event -> state 2 when slot 11 is not
    // -1 and <= Threshold (true); controller message 0xd of a record of Kind -> state 2 with
    // that record as the activator when the percent drop is below Threshold (true).
    bool OnArmed(SMEventData const* Event, int const& State) override;
    // Slot 11, LAB_06f92ff0: the lowest health percent (pub::SpaceObj::GetHealth, ftol of
    // current / max * 100) of the alive records (Field_0x04, State 2) of Kind; -1 without one.
    virtual int GetLowestHealth();

    FRmRecordList* Records;                         // +0x40 the ship machine's list
    int Threshold;                                  // +0x44
    int Kind;                                       // +0x48 FRmShipRecord::Kind (2)
};

// The comm sequence triggers (GRmCommTriggerNames, VignetteParams.ini) the vignette machine
// passes to FRmOfferComponent::SendConversations. Names are the retail strings.
enum ERmCommTrigger
{
    RmCommInSpaceInSystem = 1,                      // IN_SPACE_IN_SYSTEM
    RmCommInSpaceNotInSystem = 2,                   // IN_SPACE_NOT_IN_SYSTEM
    RmCommPreBattleSpawnRange = 3,                  // PLAYER_IN_PRE_BATTLE_SPAWN_RANGE
    RmCommKilledAllGoingToMain = 4,                 // SUCCESS_KILLED_ALL_GOING_TO_MAIN
    RmCommNotKilledAllGoingToMain = 5,              // SUCCESS_NOT_KILLED_ALL_GOING_TO_MAIN
    RmCommMainBattleSolarSpawnRange = 6,            // PLAYER_IN_MAIN_BATTLE_SOLAR_SPAWN_RANGE
    RmCommMainBattleShipSpawnRange = 7,             // PLAYER_IN_MAIN_BATTLE_SHIP_SPAWN_RANGE
    RmCommFirstFriendlySpawn = 8,                   // FIRST_FRIENDLY_SPAWN
    RmCommFirstDefensiveShipSpawn = 9,              // FIRST_DEFENSIVE_SHIP_SPAWN
    RmCommFirstBigShipSpawn = 10,                   // FIRST_BIG_SHIP_SPAWN
    RmCommLeftCombatArea = 11,                      // LEFT_COMBAT_AREA
    RmCommReenteredCombatArea = 12,                 // REENTERED_COMBAT_AREA
    RmCommGlobalSuccess = 13,                       // GLOBAL_SUCCESS
    RmCommGlobalFailCombatRange = 14,               // GLOBAL_FAIL_COMBAT_RANGE
    RmCommGlobalFailDestroyedCriticalLoot = 15,     // GLOBAL_FAIL_DESTROYED_CRITICAL_LOOT
    RmCommGlobalFailOther = 16,                     // GLOBAL_FAIL_OTHER
};

// =======================================================================================
// TU61 -- runtime machine base (retail 0xa8 bytes). Vtable 06fbc9b8 (28 slots, [27] purecall);
// MessageHandler side 06fbc9a0; the text-parameter source at +0x54 (vtable 06fbc99c, purecall;
// ctor phase 06fbca28). ctor FUN_06f89a00 (RET 4), dtor FUN_06f89cc0 (deleting FUN_06f89ca0).
// Only FRmVignetteMachine derives from it.
// =======================================================================================
class FRmRuntimeMachine : public FRmMachineGroup, public IRmTextParamSource
{
public:
    // FUN_06f89a00 (RET 4).
    explicit FRmRuntimeMachine(FRmOffer* InOffer);
    // FUN_06f89cc0: the offer released, the objective vector freed, the objective list (with
    // its objectives), then ~FRmMachineGroup (which removes the sub-machines by id).
    ~FRmRuntimeMachine() override;

    // ---- slots 7..27 ----
    // [7] LAB_06ebf720 (RET): nothing. The mission's EndMission calls it (FUN_06ef0480).
    virtual void Shutdown();
    // [8] FUN_06f89dc0: the system of the first objective that has one (slot 9), else the
    // offer's system id.
    virtual unsigned int GetSystemId();
    // [9] FUN_06f89e30 (RET 0x34): Solars->Records.FindByName.
    virtual FRmSpawnRecord* FindSolarByName(TString<0x30> InName);
    // [10] FUN_06f89e60 (RET 0x34): Ships->Records.FindByName.
    virtual FRmSpawnRecord* FindShipByName(TString<0x30> InName);
    // [11] LAB_06f89e90 (RET 0xc): fill the speaker (bSpeaker) or the listeners of Line from its
    // kinds (FRmCommLine::Speaker / FRmCommSequence::Target).
    virtual bool ResolveLine(bool bSpeaker, FRmLine* Line, FContentPlayerState* Player);
    // [12] LAB_06f8a590: false in states 5, 6 and 7 (the mission save asks it).
    virtual bool CanSave();
    // [13] LAB_06f71100 (ICF, the TU57 trigger body): state 0 and an enter event -> state 1.
    virtual bool OnState0(SMEventData const* Event, int const& State);
    // [14] LAB_06f8a610 (RET 8): state 1 and GRmMsgStartMachine for this state -> state 2.
    virtual bool OnState1(SMEventData const* Event, int const& State);
    // [15] LAB_06f8a670 (RET 8): state 2 and an enter event -> state 3.
    virtual bool OnState2(SMEventData const* Event, int const& State);
    // [16] FUN_06f8b160: slot 18 for every mission player in this machine's system; then
    // bObjectivesChanged = 0.
    virtual void RefreshObjectives();
    // [17] FUN_06f8b250: FMissionPlayer::SendPlaceholderObjective for every mission player.
    virtual void SendPlaceholderObjectives();
    // [18] FUN_06f8b330 (RET 4): rebuild one player's objective list.
    virtual void UpdatePlayerObjectives(unsigned int InPlayerId);
    // [19] FUN_06f8b620 (RET 8): the path objectives towards the first objective.
    virtual bool UpdatePathObjectives(FMissionPlayer* Player, bool* bChanged);
    // [20] FUN_06f8bac0 (RET 4): the player's attitudes (slot 21) and the type 3 objectives
    // rebuilt for it.
    virtual void OnPlayerJoined(unsigned int InPlayerId);
    // [21] FUN_06f8b930 (RET 4): every ship / solar attitude towards Player.
    virtual void SetPlayerAttitudes(FContentPlayerState* Player);
    // [22] FUN_06f92680 (RET 4): slot 23 for every solar of Solars.
    virtual void SpawnSolars(std::list<FRmSolarSpawn>* Solars);
    // [23] FUN_06f926b0 (RET 4): create one solar and its record.
    virtual bool SpawnSolar(FRmSolarSpawn* Solar);
    // [24] FUN_06f915a0 (RET 8): create a ship group and its records. bNotFirst: the group is
    // not the first of its component (FUN_06f8beb0 06f8bf0b-06f8bf1a, SETNZ of group != begin).
    virtual bool SpawnShipGroup(FRmShipGroup* Group, bool bNotFirst);
    // [25] FUN_06f90f60 (RET 4): create one loot and its record.
    virtual bool SpawnLoot(FRmLootSpawn* Loot);
    // [26] FUN_06f90f20 (RET 4): slot 25 for every loot of Record, parented to it.
    virtual void SpawnRecordLoot(FRmSpawnRecord* Record);
    // [27] purecall: the mission succeeded (FRmVignetteMachine::OnSucceeded).
    virtual void OnSucceeded() = 0;

    // FUN_06f8a570: the mission's player states (mission slot 29).
    std::vector<FContentPlayerState*>* GetMissionPlayers();
    // FUN_06f8a580: the mission's player map (mission slot 30).
    std::map<unsigned int, FContentPlayerState*>* GetMissionPlayerMap();

protected:
    // StateMachine slot 2, LAB_06f8a5c0 (RET 8): states 0..2 -> slots 13..15; always false.
    bool Slot2(SMEventData const* Event, int const& State) override;

    // FUN_06f8ade0 (RET 8): Objective's mission objective appended (and the objective kept).
    void AddObjective(int Key, FRmObjective* Objective);
    // FUN_06f8ae70 (RET 0xc): the same inserted before Where.
    void InsertObjective(int Key, FRmObjective* Objective,
                         std::vector<FRmObjective*>::iterator Where);
    // FUN_06f8af40 (RET 4): the objective of Key rebuilt in place; false without one.
    bool RebuildObjective(int Key);
    // FUN_06f8b030 (RET 4): the objective of Key dropped; false without one.
    bool RemoveObjective(int Key);
    // FUN_06f8b110 (RET 4): the objective of Key, or end.
    std::vector<FRmObjective*>::iterator FindObjective(int Key);

    // The +0x64 position in Objectives of the ActiveObjectives entry Where (the inline walk of
    // 06f8ae80-06f8aee3: one more when a type 5 objective is in the list).
    std::list<FMissionObjective*>::iterator ObjectiveListPosition(
        std::vector<FRmObjective*>::iterator Where);

    FRmSolarMachine* Solars = nullptr;              // +0x58 (owned by the group)
    FRmShipMachine* Ships = nullptr;                // +0x5c (owned by the group)
    FRmLootMachine* Loot = nullptr;                 // +0x60 (owned by the group)
    // +0x64: the mission objectives built from ActiveObjectives (slot 6), in the same order
    // after an optional run of type 5 path objectives at the front.
    FMissionObjectiveList Objectives;
    bool bObjectivesChanged = false;                // +0x74
    std::vector<FRmObjective*> ActiveObjectives;    // +0x78 (alloc byte) / +0x7c..+0x84
    // +0x88: the mission (FRmOffer::Mission, written by FUN_06ef0100 before the factory).
    FRandomMissionObject* Mission;
    // +0x8c: the offer, one reference (AddRef in the ctor; released by FUN_06fa0330 in the
    // dtor and on the ctor's exception path, unwind 06fb1007).
    TRmRef<FRmOffer> Offer;
    Vector Position = {};                           // +0x90 the centre of the mission area
    // +0x9c: the combat area radius: 3500.0 (DAT_06fbc2c8) from the ctor and states 3 / 11,
    // GRmVignetteOuterRadius (9500.0) from the exit of state 9. The area triggers use it +- 250.
    float AreaRadius = 3500.0f;
    // +0xa0 / +0xa1 / +0xa2: the comm sequences FIRST_DEFENSIVE_SHIP_SPAWN (9),
    // FIRST_BIG_SHIP_SPAWN (10) and FIRST_FRIENDLY_SPAWN (8) were sent (FUN_06f8beb0).
    unsigned char bDefensiveShipSpawnSent = 0;
    unsigned char bBigShipSpawnSent = 0;
    unsigned char bFriendlySpawnSent = 0;
    // +0xa3: a player reached the 10000 range of the target or the main battle (an exclusion
    // sphere was added, 06f8efbc); message 0x27 (06f8c7c3) fails the mission only after it.
    unsigned char bAreaReached = 0;
    // +0xa4: the fail reason the failed state sends (2/3 player killed, 4..10 critical loot,
    // 0xb/0xc left the combat area, 13 docked).
    int FailReason = 0;
};

// TU61 -- DAT_06fce778: 30.0, set to 45.0 (DAT_06fbc2dc) by the first runtime machine built
// outside single player (06f89ad0-06f89ae0). TODO: readers.
extern float GRmMultiplayerDelay;

// =======================================================================================
// TU61 -- the vignette runtime machine (retail 0xb4 bytes). Vtable 06fbc928 (29 slots),
// MessageHandler side 06fbc910, text-parameter source 06fbc90c ([0] FUN_06f8b140). Built only
// by the factory FUN_06f890f0; dtor FUN_06f8a6d0 (deleting FUN_06f8a6b0).
// =======================================================================================
class FRmVignetteMachine : public FRmRuntimeMachine
{
public:
    // 06f8913b-06f89173: the base, +0xa8 = 0, +0xac = -1.0, +0xb0 = the offer (AddRef).
    explicit FRmVignetteMachine(FRmVignetteOffer* InOffer);
    // FUN_06f8a6d0: +0xb0 released, then the base dtor (inlined).
    ~FRmVignetteMachine() override;

    // FUN_06f8b140 (RET 8): the vignette offer's text parameters.
    bool GetTextParam(int Param, FmtStr* Out) override;

    // [7] LAB_06f8a820: free the zones, wake the ships, release the solars, destroy the
    // critical loot.
    void Shutdown() override;
    // [27] LAB_06f8bb80: the reward text to the first player; the players near the mission
    // area get flags 4 and 120 seconds.
    void OnSucceeded() override;
    // [28] FUN_06f8ab10 (RET 4): every mark of this machine's objects removed for Players,
    // and the critical loot taken from the players holding it.
    virtual void UnmarkObjects(std::vector<FContentPlayerState*> const* Players);

protected:
    // StateMachine slot 2, FUN_06f8c460 (RET 8; RandomMissionVignette.cpp): the "any state"
    // messages (State < 0), states 0..2 through slots 13..15 (false), then the vignette states
    // 3 start, 8 pre-battle, 9 target fight, 10 approach, 11 main battle, 12 critical loot,
    // 5 succeeded, 6 failed, 7 done.
    bool Slot2(SMEventData const* Event, int const& State) override;

private:
    // The pieces of FUN_06f8c460, one per state (RandomMissionVignette.cpp).
    bool HandleAnyState(SMEventData const* Event);          // 06f8c4e8-06f8d3ba
    bool HandleStart(SMEventData const* Event);             // state 3, 06f8d3fc-06f8d6ea
    bool HandlePreBattle(SMEventData const* Event);         // state 8, 06f8d6ed-06f8d9b2
    bool HandleTargetBattle(SMEventData const* Event);      // state 9, 06f8d9b5-06f8e5b6
    bool HandleApproachMain(SMEventData const* Event);      // state 10, 06f8e5b9-06f8f0f3
    bool HandleMainBattle(SMEventData const* Event);        // state 11, 06f8f0f6-06f8f895
    bool HandleCriticalLoot(SMEventData const* Event);      // state 12, 06f8f89a-06f9059a
    bool HandleSucceeded(SMEventData const* Event);         // state 5, 06f9059f-06f90770
    bool HandleFailed(SMEventData const* Event);            // state 6, 06f90773-06f9095b
    bool HandleDone(SMEventData const* Event);              // state 7, 06f9095e-06f90a32

    // FUN_06f8bc70: objectives 6 and 7 dropped; every alive flagged ship loses its flags and
    // its mark for every mission player.
    void ClearShipTargets();
    // FUN_06f8bd90: the same for objectives 4 / 5 and the solars.
    void ClearSolarTargets();
    // FUN_06f8beb0 (RET 0xc): spawn the ship group Serial of Component (slot 24) unless it was
    // spawned, mark its targets (MarkGroupTargets), send the first-spawn comm sequences, and
    // arm the triggers that spawn the next group (cookie Cookie).
    void SpawnGroupWithTriggers(unsigned int Serial, FRmShipComponent* Component,
                                unsigned int Cookie);
    // FUN_06f8c200 (RET 4): after the flagged ships of a kind died, objective 7 (big ships) or
    // 6 (defensive ships) is dropped.
    void OnFlaggedShipsLost(unsigned int ObjectId);
    // FUN_06f8c330: one of the objectives 7, 4, 6, 5 is active.
    bool HasTargetObjectives();
    // FUN_06f8c3c0 (RET 8): IN_SPACE_IN_SYSTEM or IN_SPACE_NOT_IN_SYSTEM for a mission player
    // with a ship.
    void SendSystemConversations(unsigned int InPlayerId, unsigned int SystemId);
    // FUN_06f90b70 (RET 8): the target objective (6 / 7) of a spawned group and the friendly
    // objective 8.
    void MarkGroupTargets(FRmRecordList* Members, FRmObjectiveMap* Map);

    // +0xa8: 0 from the factory (06f8913b). TODO: meaning (no reader in FUN_06f8c460).
    unsigned int Field_0xa8 = 0;
    // +0xac: the game time the players left the combat area (FUN_06efec10, stored as float),
    // -1.0 while they are in it (06f8d068 / 06f8d19f / 06f8e2fb / 06f8ed46).
    float LeftAreaTime = -1.0f;
    TRmRef<FRmVignetteOffer> VignetteOffer;         // +0xb0 one reference
};

// TU61 -- DAT_06fd3bc0 (static init LAB_06f84280): 3500.0 (DAT_06fbc2c8) + 6000.0
// (DAT_06fbc2b4); the combat area radius from the main battle on (FUN_06f8c460 06f8e43c).
extern float GRmVignetteOuterRadius;

// FUN_06f890f0 (cdecl): a runtime machine for Offer when its machine type is 1..3 (always a
// vignette machine), else none; *Out = the machine (null when none). True when one was built.
bool __cdecl CreateRmRuntimeMachine(FRmOffer* Offer, FRmRuntimeMachine** Out);
