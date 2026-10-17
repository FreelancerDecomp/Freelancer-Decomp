// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "BehaviorCmds.h"    // FContentShipBase (TU50)
#include "ContentRefObject.h" // FContentRefObject (vtable 06fb43f8)

#include <cstdio>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Content.dll TU46 (06f46940-06f4a780): per-player content state. The retail
// object is 0x10c bytes (FUN_06f46b90), kept in the map DAT_06fd0bbc keyed by
// player id. Offsets below are the retail ones; containers are current std types
// (no retail ABI).

struct ID_String;   // common_id.h

// [mPlayer] "vnpc" record (vector at player +0xec, 0x10 stride): base, npc, and two values.
struct FVnpcRecord
{
    unsigned int Values[4];
};

// [mPlayer] "rumor" record (vector at player +0xfc, 8 stride).
struct FRumorRecord
{
    unsigned int Values[2];
};

// [mPlayer] "tlr_exception" record (vector at player +0x38, 8 stride).
struct FTlrException
{
    unsigned int Values[2];
};

// Retail player +0x48 (0x5c bytes): statistics block, saved by FUN_06f495f0 and
// loaded by FUN_06f49780.
struct FContentPlayerStats
{
    std::map<unsigned int, int> ShipTypesKilled;              // +0x00
    std::map<unsigned char, int> RandomMissionsCompleted;     // +0x14
    std::map<unsigned char, int> RandomMissionsAborted;       // +0x28
    std::map<unsigned char, int> RandomMissionsFailed;        // +0x3c
    float TotalCashEarned = 0.0f;                             // +0x50
    float TotalTimePlayed = 0.0f;                             // +0x54
    std::vector<unsigned int> SystemsVisited;                 // +0x58
    std::vector<unsigned int> BasesVisited;                   // +0x68
    std::vector<unsigned int> HolesVisited;                   // +0x78
};

// Retail FUN_06eb6be0: vtable 06fb4640 {FUN_06eacd40 AddRef, FUN_06eacd50 Release,
// FUN_06f06eb0 scalar deleting dtor} -- the FContentRefObject slots (+4 reference count =
// 1) -- and +8 reputation = -1.
struct FContentPlayerStateBase : public FContentRefObject
{
    int Reputation;                 // +0x08

    FContentPlayerStateBase();

    // FUN_06eb6bb0 (TU19, 21 callers): pub::Reputation::GetAffiliation(&Reputation, &Out);
    // returns Out (the call's status is ignored).
    unsigned int GetAffiliation() const;
    // FUN_06f33f80 (ICF-folded `MOV EAX,[ECX+8]; RET`): returns +8. The shared address
    // proves nothing about the receiver; it is this class's getter at FUN_06eb7080
    // (TU20, JMP with ECX = the +8 handle of the space object).
    int GetReputation() const;
};

// Parameter block of the player ship (GameSupport msg 0x36 local, ctor FUN_06efff70): the
// TU50 block (TU20 params, +0x44 0, +0x48/+0x4c -1, +0x50 vector, +0x60 list, +0x6c..+0x74
// 0) and the ship's space object id at +0x78. The +0x60 directive list
// (FContentShipParams::Directives) stays empty for the player.
struct FContentPlayerShipParams : public FContentShipParams
{
    unsigned int ShipId = 0;                                  // +0x78 pub::Player::GetShip
};

// Player ship controller owned by the player state at +0x10 (0x190 bytes). Created by
// GameSupport message 0x36 (player launch). Vtables 06fbb044 {FUN_06f472c0 deleting dtor,
// the TU50 slots 1-4} and 06fbb040 {FUN_06f57820} at +0x118.
struct FSystemInfo;   // SystemInfo.h (TU58), the value of DAT_06fd3a5c

class FContentPlayerShip : public FContentShipBase
{
public:
    // FUN_06f471d0 (RET 4): TU50 base, SpaceObjId = Params->ShipId, then the "PLAYER"
    // state graph through pub::AI::SubmitState.
    explicit FContentPlayerShip(FContentPlayerShipParams const* Params);
    // FUN_06f472e0 (called by the deleting dtor FUN_06f472c0).
    ~FContentPlayerShip() override;

    // FUN_06f47430 (RET 4): GameSupport msg 0x25 -- remember the target; a jump hole
    // (type & 0x80000) and its jump target go into the owner's holes_visited.
    void OnTargetChanged(unsigned int const* TargetId);
    // FUN_06f47590 (RET 4; caller SpaceSupport heartbeat 06f5d6ff): the hostile space objects
    // of the system within 2500 of the ship (see HostileContacts); entering / leaving that
    // state flips bInCombat and mutes / unmutes the system's NPC ships.
    void UpdateHostileContacts(float DeltaTime);
    // FUN_06f47850 (caller SpaceSupport msg 0x3b 06f5df3d): bInCombat = 0, CalmSince = -1.0,
    // HostileContactCount = 0.
    void ResetHostileState();
    // FUN_06f47840 (caller TU29 06ef5646 / 06ef566c / 06ef56b8): CalmSince.
    double GetCalmSince() const;
    // FUN_06f47870 (caller TU29 06ef562a): HostileContactCount.
    int GetHostileContactCount() const;

    unsigned int TargetId = 0;                                // +0x174
    // +0x178: the game time the last hostile contact was lost (FUN_06f47590 06f477d5); -1.0
    // (DAT_06fb7d70) while hostiles are near.
    double CalmSince = -1.0;
    // +0x180 list, node 0x14 (0xc value); the dtor frees the nodes without element dtors.
    // Writer FUN_06f47590: a hostile space object within 2500 of the ship.
    struct FHostileContact
    {
        FContentSpaceObj* Object;                             // +0x00
        float TimeNear;                                       // +0x04 seconds spent near
        float TimeSinceSeen;                                  // +0x08 reset to 0 when seen
    };
    std::list<FHostileContact> HostileContacts;
    // +0x18c: contacts added since the ship last became calm (0 from the ctor).
    int HostileContactCount = 0;
};

// TU17 base record (map DAT_06fcee70 value). Not ported yet; see BaseDB.h.
struct FContentBase;

struct FContentPlayerState : FContentPlayerStateBase
{
    unsigned int PlayerId;                                    // +0x0c
    FContentPlayerShip* ShipObject = nullptr;                 // +0x10 owned
    unsigned int LocationId = 0;                              // +0x14
    FSystemInfo* SystemRecord = nullptr;                      // +0x18 FUN_06f75f30 result
    FContentBase* BaseRecord = nullptr;                       // +0x1c
    // +0x20: 0 from the ctor (06f46bd4); GameSupport msg 0x39 (06efecfb) sets 1 when the
    // player's ship is destroyed (Server FObjectDestroyedEvent, ship id = event +0). No
    // reader identified in Content yet.
    unsigned char bShipDestroyed = 0;
    unsigned char bCanDock;                                   // +0x21
    std::vector<unsigned int> DockExceptions;                 // +0x24
    unsigned char bCanTradelane;                              // +0x34
    std::vector<FTlrException> TradelaneExceptions;           // +0x38
    FContentPlayerStats Stats;                                // +0x48
    unsigned int ValueD0;                                     // +0xd0 flags; bit 2 cleared when ValueD4 runs out
    float ValueD4;                                            // +0xd4 countdown, -1 = idle
    // Retail +0xd8: RandomMission controller id from FUN_06f483d0 Create.
    unsigned int ValueD8;
    unsigned int MissionNumber;                               // +0xdc
    // +0xe0: the next mission vendor offer id, 1 at first (FUN_06f46b90 06f46cb8); only
    // AllocateVendorOfferId (FUN_06f48c50) reads and writes it.
    unsigned int NextVendorOfferId;
    unsigned int ValueE4;                                   // +0xe4
    unsigned int ValueE8;                                     // +0xe8
    std::vector<FVnpcRecord> Vnpcs;                           // +0xec
    std::vector<FRumorRecord> Rumors;                         // +0xfc

    // FUN_06f46b90. The retail dtor FUN_06f47060 only destroys the containers:
    // it does not delete ShipObject (no vtable call on +0x10).
    explicit FContentPlayerState(unsigned int InPlayerId);

    // Vtable 06fbb02c slot 1, LAB_06f479a0: --count only (the map DAT_06fd0bbc owns
    // the object; GameSupport msg 8 deletes it through slot 2 FUN_06f46ec0).
    unsigned int Release() override;

    // FUN_06f47ba0 (RET 8): can_dock and its exception list (Act_PlayerCanDock).
    void SetDockAccess(unsigned char bCan, std::vector<unsigned int> const* Exceptions);
    // FUN_06f47d60 (RET 8): can_tl and its exception pairs (Act_PlayerCanTradelane).
    void SetTradelaneAccess(unsigned char bCan, std::vector<FTlrException> const* Exceptions);
    // FUN_06f47ee0 (RET 8): the trade lane check of a lane request (TU55 FUN_06f6fcb0):
    // 0 allowed, 1 or 2 refused.
    int CheckTradelaneRoute(unsigned int RingA, unsigned int RingB);
    // FUN_06f47d10 (RET 4): can_dock inverted for the targets of its exception list.
    bool CanDockAt(unsigned int DockTargetId) const;
    // FUN_06f47880: delete the old ship object through its vtable, store the new one.
    void SetShipObject(FContentPlayerShip* NewShip);
    // FUN_06f478c0: return +0x18.
    FSystemInfo* GetSystemRecord() const;
    // FUN_06f47980: return +0x1c.
    FContentBase* GetBaseRecord() const;
    // FUN_06f478b0 (added by CNT-MS-OBJ6 for the TU38 NN path): the system record's id, 0
    // without one.
    unsigned int GetSystemId() const;
    // FUN_06f481e0: the ship object's SpaceObj id, 0 without a ship.
    unsigned int GetShipId() const;
    // FUN_06f47af0 (RET 4): the ship's position; docked, the position of the base
    // (pub::GF::GetBasePosition of the system and base ids, 0 without a record), else 0.
    Vector* GetPosition(Vector* Out) const;
    // FUN_06f478d0: system change (0 = left the system).
    void OnSystemChange(unsigned int SystemId);
    // FUN_06f479b0: base enter / exit (0 = exit).
    void OnBaseChange(unsigned int BaseId);
    // FUN_06f47b90: store +0x14.
    void SetLocation(unsigned int InLocationId);
    // FUN_06f49030: heartbeat — play time and the +0xd4 countdown.
    void UpdateTime(float DeltaTime);
    // FUN_06f481f0: write the [mPlayer] section.
    void Save(FILE* File);

    // Statistics readers used by GameSupport msg 0x33 (player stats request).
    // FUN_06f49210 / FUN_06f49270 / FUN_06f492d0: count for one random-mission type, 0 if absent.
    int GetRandomMissionsCompleted(unsigned char MissionType) const;
    int GetRandomMissionsAborted(unsigned char MissionType) const;
    int GetRandomMissionsFailed(unsigned char MissionType) const;
    // FUN_06f49330 / FUN_06f49350 / FUN_06f49370: visited-list sizes.
    int GetBasesVisitedCount() const;
    int GetHolesVisitedCount() const;
    int GetSystemsVisitedCount() const;
    // FUN_06f49390 / FUN_06f493a0: ftol of the float totals.
    int GetTotalCashEarned() const;
    int GetTotalTimePlayed() const;
    // FUN_06f493b0: number of ship types killed.
    unsigned int GetShipTypesKilledCount() const;
    // FUN_06f493c0: {ship type, count} pairs in key order.
    void CopyShipTypesKilled(unsigned int* Out) const;
    // FUN_06f48d80 (thiscall, RET 4): Values[1] of the first Rumors record whose
    // Values[0] is Id, 0 when none.
    unsigned int FindRumor(unsigned int Id) const;
    // FUN_06f48db0 (thiscall, RET 4; callers TU19 06eb4c69, TU17 06ea8dcf): ++Values[1] of
    // the first Rumors record for Id, else a new record {Id, 1} at the end.
    void AddRumor(unsigned int Id);
    // FUN_06f48ca0 (thiscall, RET 8; caller TU19 06eb4c41): a bar character was talked to.
    // NpcId 0 -> nothing. The first Vnpcs record {BaseId, NpcId}: none, or Values[2] == 0
    // -> a new record {BaseId, NpcId, 1, 0} at the end; else Values[3] != 1 -> 0, and
    // ++Values[2].
    void RecordVnpcTalk(unsigned int BaseId, unsigned int NpcId);
    // FUN_06f487d0 (thiscall; callers TU19 06eb5143, TU17 06ea8e58): +0xd8, else the
    // first non-zero +0xd8 among the group members that have a state, else 0.
    unsigned int GetGroupLiveMission() const;
    // FUN_06f48c50 (thiscall; only caller TU35 FUN_06f047e0 06f0498f): returns +0xe0 and
    // post-increments it -- the id of a new mission vendor offer.
    unsigned int AllocateVendorOfferId();
    // FUN_06f48070 (thiscall): pub::Player::InspectCash + pub::Player::GetAssetValue of +0xc,
    // left on the FPU as (double)cash + asset value. Callers: SpaceSupport loot FUN_06f5cfa0
    // 06f5d0f2; TU15 Story (FStory inlines its own copy of the sum).
    double GetWorth() const;

    // FUN_06f48030 (RET 4; only caller Act_AdjAcct FUN_06ec7ef0 06ec7f10): the player id
    // (+0xc) copied to a local, then pub::Player::AdjustCash(local, Amount).
    void AdjustCash(int Amount);

    // Mission end (added by CNT-MS-OBJ5 for the TU38 slots 23/24).
    // FUN_06f48050 (RET 4): pub::Player::RewardGroup(+0xc, Amount).
    void RewardGroup(int Amount);
    // FUN_06f480c0: pub::Player::GetGroupSize(+0xc) into a local preset to 1.
    unsigned int GetGroupSize() const;
    // FUN_06f487c0: +0xd8, the live mission controller.
    unsigned int GetLiveMission() const;
    // FUN_06f48a20 (RET 0xc): +0xec records in state 1 take VnpcState; then the player (or,
    // with bGroup, every group member that has a state) leaves its mission.
    void EndLiveMission(unsigned int Unused, int VnpcState, bool bGroup);

    // Random-mission support (added by CNT-RM8 for the TU47 mission object).
    // FUN_06f471a0: return +0xd0.
    unsigned int GetFlags() const;
    // FUN_06f471b0 (RET 4): +0xd0 = Flags.
    void SetFlags(unsigned int Flags);
    // FUN_06f471c0 (RET 4): +0xd4 = Countdown.
    void SetCountdown(float Countdown);
    // FUN_06f480e0 (RET 4): *Out = pub::Player::GetName(+0xc); returns Out.
    std::wstring* GetName(std::wstring* Out) const;
    // FUN_06f48190 (RET 4): true for the player himself, else pub::Player::IsGroupMember.
    bool IsGroupMember(unsigned int OtherPlayerId) const;
    // FUN_06f481c0 (RET 4): pub::Player::GetGroupMembers(+0xc, *Out).
    void GetGroupMembers(std::vector<unsigned int>* Out) const;
    // FUN_06f48ba0 (RET 0x10): +0xd8 = *ControllerId, message 0x100b {controller, player} to
    // every controller, then pub::Player::SetMsnID(+0xc, +0xd8, SubId or +0xc, bNotify, Arg).
    void JoinMission(unsigned int const* ControllerId, unsigned int SubId, bool bNotify,
                     unsigned int NotifyArg);
    // FUN_06f49090 / FUN_06f49110 / FUN_06f49190 (RET 4): ++ the per-type count of the
    // statistics map +0x5c (completed) / +0x70 (aborted) / +0x84 (failed); absent -> 1.
    void CountRandomMissionCompleted(unsigned char MissionType);
    void CountRandomMissionAborted(unsigned char MissionType);
    void CountRandomMissionFailed(unsigned char MissionType);

    // SpaceSupport support (CNT-GS8).
    // FUN_06f48c20 (RET 4): +0xe4 = Value (SpaceSupport msg 0x1a 06f5dd36 with 0, msg 0x2f
    // 06f5e379 with parms[2]).
    void SetValueE4(unsigned int Value);
    // FUN_06f48c40 (RET 4): +0xe8 = Value (SpaceSupport msg 0x39 06f5d8f8 with 0 when it
    // holds the destroyed object, msg 0x2f 06f5e384 with parms[1]).
    void SetValueE8(unsigned int Value);
    // FUN_06f48c10 (callers SpaceSupport 06f5dd24, TU37 goto guide 06f09224 / 06f0958a; added
    // by CNT-AI6): +0xe4. With GetValueE8 the goto guide reads the pair that SpaceSupport msg
    // 0x2f stores (parms[2], parms[1]).
    unsigned int GetValueE4() const;
    // FUN_06f48c30 (callers TU37 06f0921c / 06f09582, SpaceSupport 06f5d8e4; added by
    // CNT-AI6): +0xe8.
    unsigned int GetValueE8() const;
    // FUN_06f48fb0 (RET 4; caller SpaceSupport msg 0x39 06f5da46 with the killed ship's
    // arch): ++ ShipTypesKilled[ShipArchId], a new entry starting at 1 (FUN_06f003e0).
    void CountShipTypeKilled(unsigned int ShipArchId);
    // FUN_06f488b0 (RET 0x14; callers TU2 06ea2327, 06ea243e, TU17 06ea8e99, TU19 06eb51aa,
    // TU21 06eba1b4 / 06ebb123): the vnpc record {VnpcOwner, *Vnpc} (+0xec) gets its last
    // value 1 (VnpcOwner 0 or an empty Vnpc: none); then every group member with a state
    // joins ControllerId (param 1, EBX at 06f4895a) as JoinMission does (+0xd8, message
    // 0x100b to controller 0 -- retail reuses param 1's slot for that 0 --,
    // pub::Player::SetMsnID with SubId = this player's id, or the member's when that is 0).
    void JoinMissionAsGroup(unsigned int ControllerId, unsigned int VnpcOwner,
                            ID_String const* Vnpc, bool bNotify, unsigned int NotifyArg);
    // FUN_06f48770 (RET 0xc; caller TU34 FUN_06f011f0 06f012a1): the first vnpc record
    // {BaseId, *NpcId} (+0xec) -> *OutValue = its Values[3], true; false (OutValue untouched)
    // when there is none.
    bool FindVnpcValue3(unsigned int BaseId, unsigned int const* NpcId,
                        unsigned int* OutValue) const;
    // FUN_06f48c60 (RET 8; caller TU34 FUN_06f00b90 06f00cc4): Values[2] of the first vnpc
    // record {BaseId, NpcId} (+0xec), 0 when there is none.
    unsigned int GetVnpcValue2(unsigned int BaseId, unsigned int NpcId) const;
};

class INI_Reader;

FContentPlayerState* FindContentPlayerState(unsigned int PlayerId);
// FUN_06f49c30: the first player state in key order, or null (Act_DockRequest).
FContentPlayerState* GetFirstContentPlayerState();
// FUN_06f49bc0: the player whose ship object carries this id, or null.
FContentPlayerState* FindContentPlayerStateByShip(unsigned int const* ShipId);
// FUN_06f49b50 (cdecl): the first player state in key order whose live mission (+0xd8) is
// *ControllerId, or null. Caller TU58 FSystemInfo::FindObjectIdByName (06f73af9).
FContentPlayerState* FindContentPlayerStateByLiveMission(unsigned int const* ControllerId);
// The map DAT_06fd0bbc itself (node key +0xc, value +0x10). Retail TU53 walks it inline
// (FUN_06f66220 06f662a4, FUN_06f67660 06f676c3); the port hands the walkers the map.
std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& GetContentPlayerStates();
// FUN_06f49f90 (cdecl, by value; callers SpaceSupport msg 0x37 06f5e0b0 / 0x3a 06f5de5e):
// the system's SystemInfo (not null-tested) has players.
bool IsContentSystemOccupied(unsigned int SystemId);
// FUN_06f002e0 body: drop the old state (virtual delete) and insert a new one.
FContentPlayerState* ReplaceContentPlayerState(unsigned int PlayerId);
// GameSupport msg 8 (logout): delete and erase the player's state, if any.
void RemoveContentPlayerState(unsigned int PlayerId);
// Walk of DAT_06fd0bbc in key order (GameSupport heartbeat).
void UpdateAllContentPlayerTimes(float DeltaTime);
void LoadContentPlayerState(FContentPlayerState* State, INI_Reader* Reader);
void SetContentPlayerMissionNumber(FContentPlayerState* State, unsigned int MissionNumber,
                                   unsigned int bAdvanceRank);
// FUN_06f495f0: write the statistics block.
void SaveContentPlayerStats(FContentPlayerStats const& Stats, FILE* File);

// Content.dll FUN_06f49440 — rank / next-rank cash from the PTough scale table.
void RecalculateContentPlayerRank(FContentPlayerState* State);
// Content.dll FUN_06f49c50 — load [PlayerToughnessScale] / ptough_graph_pt.
void LoadPlayerToughnessScale(INI_Reader* Reader);

// TU53 locked gates (DAT_06fd1ca8, ordered {gate, players}); live in SpaceSupport.cpp.
// FUN_06f6d400.
void RegisterContentGateLock(unsigned int PlayerId, unsigned int GateId);
// FUN_06f6d2b0: gates locked for this player or for player 0, in table order, unique.
void CollectContentGateLocks(unsigned int PlayerId, std::vector<unsigned int>& OutGates);
// FUN_06f6ce90: the gate is locked for this player or for player 0.
bool IsContentGateLocked(unsigned int const* PlayerId, unsigned int const* GateId);
// FUN_06f6d1c0: NPC gate lock (DAT_06fd1cbc), unique.
void RegisterContentNpcGateLock(unsigned int const* GateId);
// FUN_06f6d280 (cdecl; callers TU58 06f723f4, 06f72789): *GateId is in the NPC lock list.
bool IsContentNpcGateLocked(unsigned int const* GateId);
// FUN_06f6d220 (cdecl; callers TU21 06ec5f3f, SpaceSupport msg 0x39 06f5db16): the first NPC
// lock of *GateId is removed.
void RemoveContentNpcGateLock(unsigned int const* GateId);
// FUN_06f6d5d0 (cdecl; callers TU21 06ec5ecd / 06ec5edc / 06ec5f4e / 06ec5f5d, SpaceSupport
// msg 0x39 06f5db25 with player 0): *PlayerId 0 drops the whole lock of *GateId; *GateId 0
// drops the player from every lock; otherwise from the lock of *GateId. In single player the
// player-0 entry of a touched lock goes too, and a lock left without players is removed.
void UnregisterContentGateLock(unsigned int const* PlayerId, unsigned int const* GateId);
// FUN_06f6cf20 (cdecl, by value; caller TU53 FUN_06f6c920 06f6c953): a player's ship asks
// IsContentGateLocked; an NPC ship only when it belongs to SpacePop (or is not a known
// ship of the gate's system) and the gate is in the NPC list.
bool IsGateLockedForShip(unsigned int ShipId, unsigned int GateId);
// FUN_06f67cd0: [locked_gates] of a save (GameSupport msg 0x40).
void LoadContentGateLocks(INI_Reader* Reader);
// FUN_06f67bc0: write [locked_gates] (GameSupport msg 0x41).
void SaveContentGateLocks(FILE* File);

