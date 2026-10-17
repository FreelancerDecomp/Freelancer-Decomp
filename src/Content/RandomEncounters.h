// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "BaseDB.h"
#include "ContentRefObject.h"   // FContentRefObject: the ref-counted base, vtable 06fb43f8
#include "common_id.h"
#include "common_server_controller.h" // pub::Controller::EMessage (Accept)
#include "common_tstring.h"
#include "x86math.h"            // FTransform

#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <utility>
#include <vector>

class INI_Reader;
class FObjListDirective;
struct FContentPlayerState;
struct FFormation;              // Encounters.h (TU31 formations.ini record)
struct FSystemInfo;             // SystemInfo.h (TU58)
struct FSystemZone;             // SystemInfo.h (TU64)

// =======================================================================================
// TU31 (06efb550-06efc9a7) -- the parameters of a random encounter ([RandomEncounter] /
// [MsnRandEnc]) and the factories of its two halves: the parameter object ("TLParams" /
// "SPParams") and the populator ("TLPzr" / "SPPzr") that reads the section into it and
// decides when it spawns. The TL ("TL_Attack", trade-lane attack) classes live in TU60
// (06f81120-06f83ec0), the SP ("SP_Attack") classes in TU59 (06f80410-06f81120); their
// bodies are in RandomEncounters.cpp, the TU31 base in RandomMissionDB.cpp. Retail has no RTTI
// in Content.dll; the class names are the port's.
// =======================================================================================

// +0x28 element (list node 0x18): "formation_position" -- a spawn point and its weight.
struct FRandEncPosition
{
    Vector Position;                // +0x00
    float Weight;                   // +0x0c summed and picked in FUN_06efc2d0
};

// FRandEncFormation +0x18 element (list node 0x1c): one ship-class choice of a formation.
struct FRandEncShipChoice
{
    std::vector<unsigned int> ShipClasses;  // +0x00 (alloc byte) / +0x04..+0x0c; dtor FUN_06eec8d0
    float Weight;                           // +0x10 summed and picked in FUN_06efba30
};

// +0x44 element (list node 0x30, value 0x28; erase FUN_06efc910, value dtor FUN_06efc950):
// one "formation" line of the section.
struct FRandEncFormation
{
    FFormation* Formation = nullptr;        // +0x00 -> FUN_06f66c80 (PlaceFormationBlocks)
    ID_String Nickname;                     // +0x04 dumped with get_string
    // +0x08: "formation" value 1 as read (dumped); the parse copies it into Weight (+0x24).
    // TODO: whether anything but Dump reads +0x08 (the TL populator).
    float Field_0x08 = 0.0f;
    int MinCount = 0;                       // +0x0c ship count range (FUN_06efba30 06efbb63)
    int MaxCount = 0;                       // +0x10
    int Count = 0;                          // +0x14 the count picked for the current spawn
    std::list<FRandEncShipChoice> ShipChoices;  // +0x18 (head +0x1c, size +0x20)
    float Weight = 0.0f;                    // +0x24 summed and picked in FUN_06efba30
};

// ---------------------------------------------------------------------------------------
// Parameter object base (retail 0xa0). Ctor FUN_06efb550, dtor FUN_06efb710, deleting dtor
// FUN_06efb690; vtable 06fb7ae4 {AddRef, Release, 06efb690, purecall, FUN_06efc2d0,
// purecall}. The derived TL (0xbc, vtable 06fbc1a4) and SP (0xc4, vtable 06fbbfbc) objects
// add plain values only, so their dtors (06f81c20 / 06f80550) are this dtor inlined.
// ---------------------------------------------------------------------------------------
class FRandEncParams : public FContentRefObject
{
public:
    // FUN_06efb550.
    FRandEncParams();
    // FUN_06efb710: the three lists freed, vptr back to 06fb43f8.
    ~FRandEncParams() override;

    // Slot 3 (purecall in the base): TL 3 (LAB_06f5e540), SP 2 (LAB_06ee9660) -- the type
    // codes of FUN_06efb8e0.
    virtual int GetType() = 0;
    // Slot 4, FUN_06efc2d0 (RET 4): spawn the encounter around the target object (+0x88).
    // The base ignores bForced; the TL override (FUN_06f83af0) spawns at its trade lane unless
    // it is set. Called by the populators and by FUN_06f148f0 (TU38) with 1.
    virtual void Spawn(bool bForced);
    // Slot 5 (purecall in the base): the values as INI text (TL FUN_06f81d50, SP FUN_06f80f10).
    virtual void Dump(FILE* File) = 0;

    // FUN_06efba30 (RET 0x1c; callers Spawn 06efc6e8 and TU38 FUN_06f145d0 06f1466e): create
    // one weighted-picked formation of the encounter at Placement, through the player's system.
    // Count -1 picks the ship count from the formation's range. A null Choices picks a
    // weighted ship choice per ship, else ship i takes the classes of Choices' i-th element.
    // A null Names numbers the ships "rand_enc_ship_%d", else each ship takes (and removes)
    // the front name. Only the first ship gets Directives. Priority -> ship params +0x70,
    // DockTarget -> FSystemInfo::CreateNpcShips. False when the player has no system, no
    // formation is picked or no npc_ship arch fits a ship.
    // Choices / Names are the TU38 +0x8c record's lists at +0x3c / +0x48: the snapshot writer
    // FUN_06f130b0 (MissionSave.cpp, CNT-MS-OBJ11) builds Choices as {npc +0x128 class ids,
    // 1.0f} and Names as the npcs' TString<0x30> nicknames -- the types declared here.
    bool SpawnFormation(FTransform const* Placement, int Count,
                        std::list<FObjListDirective*> const* Directives,
                        std::list<FRandEncShipChoice> const* Choices,
                        std::list<TString<0x30>>* Names, int Priority, unsigned int DockTarget);

    // +0x08: ctor 0; TU58 FUN_06f782e0 (06f78a9c) writes the trade lane's system.
    FSystemInfo* System = nullptr;
    // +0x0c: the controller id given to every ship (-> ship params +0x6c). Retail's ctor
    // leaves it unset; the port starts it at 0.
    unsigned int ControllerId = 0;
    // +0x10: the player the encounter belongs to (FUN_06f478c0 on it gives the system).
    // Retail's ctor leaves it unset; the port starts it at null.
    FContentPlayerState* Player = nullptr;
    int AttackerRep = -1;                   // +0x14 "attacker_rep" -- the ships' faction group
    ID_String PilotId;                      // +0x18 "pilot_id" (cleared)
    std::list<unsigned int> Labels;         // +0x1c "label" ids -> ship params +0x118
    std::list<FRandEncPosition> Positions;  // +0x28 "formation_position"
    int ShipsSpawned = 0;                   // +0x34 ships created by the last Spawn
    int FormationsToSpawn = 0;              // +0x38 formations Spawn creates (0 afterwards)
    int NumFormsMin = 0;                    // +0x3c "num_forms" (0: not given)
    int NumFormsMax = 0;                    // +0x40 (0: the number of formations, set by Dump)
    std::list<FRandEncFormation> Formations;    // +0x44 (size +0x4c)
    TString<0x30> TargetShipName;           // +0x50 "target_ship_name" (+0x54 chars)
    // +0x84: the object the leader's goto directive heads for (Spawn: FObjListGotoShip::
    // SetTarget(&GotoTargetId, 200, -1)); both populators' Reset zero it.
    unsigned int GotoTargetId = 0;
    // +0x88: the object the encounter spawns around (pub::SpaceObj::GetLocation in Spawn).
    // Retail's ctor leaves it unset; the port starts it at 0.
    unsigned int TargetObjectId = 0;
    // +0x8c "arrival_obj" (a number or a nickname, SP parse 06f81858): SpawnFormation's
    // DockTarget in Spawn.
    unsigned int DockTarget = 0;
    float SpawnAngle = 1.5707964f;          // +0x90 (0x3fc90fdb) -> PlaceFacingAnchor heading
    float SpawnDistance = 2500.0f;          // +0x94 -> PlaceFacingAnchor range
    // +0x98 "radius" (ftol): the clearance of the spawn point (pub::SpaceObj::IsPosEmpty /
    // GetEmptyPos radius, as a float); 0 or less skips the test.
    int EmptyPosRadius = 7;
    int ShipNameCounter = 0;                // +0x9c the %d of "rand_enc_ship_%d"
};

// "TLParams" (retail 0xbc, ctor FUN_06f81bb0): the trade-lane attack.
class FRandEncTLParams : public FRandEncParams
{
public:
    // FUN_06f81bb0.
    FRandEncTLParams() = default;

    int GetType() override;                 // LAB_06f5e540: 3
    // FUN_06f83af0 (RET 4): DrainShields on the ring; bForced -> the base body, else every
    // formation to spawn goes to a weighted spawn point turned by the ring's orientation,
    // with a 25 s delay directive for its leader (SpawnLaneFormation).
    void Spawn(bool bForced) override;
    void Dump(FILE* File) override;         // FUN_06f81d50

    // FUN_06f832b0 (RET 0x14; only caller Spawn 06f83e0a): the lane counterpart of
    // SpawnFormation -- a weighted formation, its ships through the attacker faction's
    // npc_ship archs (a ship without an arch is skipped), named pilots, then the leader's
    // guide description (type 3) walking the path of the ring's zone, and the ships created in
    // +8's system. Always true.
    bool SpawnLaneFormation(FTransform const* Placement, int Count,
                            std::list<FObjListDirective*> const* Directives,
                            std::list<FRandEncShipChoice> const* Choices,
                            std::list<TString<0x30>>* Names);

    // +0xa0 "tl_attack_chance": the percentage a lane exit rolls against (Accept message
    // 0x16); a failed roll adds 15, message 0x3b restores +0xa4 while it is 50 or less.
    int TlAttackChance = 20;
    int TlAttackChanceReadIn = 20;          // +0xa4 "tl_attack_chance_read_in"
    // +0xa8: the player's travel direction along the lane, +1 or -1 (Accept 0x16:
    // GetTradelaneNextAndPrev of the exit ring leads back to the entry ring -> -1). The
    // spawn points' z (Accept 06f82345-06f82541) scale with it; TU58 06f789fa writes it too.
    int Direction = 1;
    unsigned int EntryRing = 0;             // +0xac message 0x16 [1], the ring entered
    unsigned int ExitRing = 0;              // +0xb0 message 0x16 [2], the ring left
    // +0xb4 "target_tradelane": the ring attacked (DrainShields in Spawn); Accept picks a
    // random ring of the travelled stretch when it is 0; Reset clears it.
    unsigned int TargetTradelane = 0;
    // +0xb8: the smallest zone around the attacked ring (FSystemInfo::FindSmallestZoneAt in
    // Accept 06f825de); TU58 06f78a04 writes its own zone there.
    FSystemZone* TargetZone = nullptr;
};

// "SPParams" (retail 0xc4, ctor FUN_06f804d0): the attack in open space.
class FRandEncSPParams : public FRandEncParams
{
public:
    // FUN_06f804d0.
    FRandEncSPParams() = default;

    int GetType() override;                 // LAB_06ee9660: 2
    void Dump(FILE* File) override;         // FUN_06f80f10

    // +0xa0 "activation_type": 0 time_cumulative, 1 location, 2 time.
    int ActivationType = 0;
    Vector ActivationLocation = {0.0f, 0.0f, 0.0f};    // +0xa4 location
    int ActivationRange = 6250000;          // +0xb0 location (0x5f5e10), dumped as %d
    // +0xb4 / +0xb8 "time": Dump prints [1] (+0xb8, 180 by default) before [0] (+0xb4, 300).
    float ActivationTime[2] = {300.0f, 180.0f};
    // +0xbc: the time_cumulative threshold; "time" picks it from [ActivationTime[1],
    // ActivationTime[0]] (Parse, and Accept whenever it is 0).
    float CumulativeTime = 0.0f;
    // +0xc0: the smallest zone around the target when the attacker faction was last picked
    // (FSystemInfo::FindSmallestZoneAt in Accept 06f80894). Retail's ctor leaves it unset.
    FSystemZone* TargetZone = nullptr;
};

// ---------------------------------------------------------------------------------------
// Populators ("Pzr", 0xc bytes, built inline by FUN_06efb980): read the section into the
// parameter object (slot 4) and decide from the controller messages when it spawns (slot 3).
// Both vtables are {AddRef, Release, FUN_06f06eb0 (empty deleting dtor), Accept, Parse,
// Reset}.
// ---------------------------------------------------------------------------------------
class FRandEncPopulator : public FContentRefObject
{
public:
    // Slot 3 (RET 0xc): a controller message; true when the encounter triggers (FUN_06f148f0
    // then calls Params' Spawn(1) and disables the encounter). Message 0x37 -> Reset.
    virtual bool Accept(FRandEncParams* Params, pub::Controller::EMessage MessageId,
                        void const* MessageParms) = 0;
    // Slot 4 (RET 8): the [RandomEncounter] / [MsnRandEnc] section of Reader into Params.
    virtual void Parse(INI_Reader* Reader, FRandEncParams* Params) = 0;
    // Slot 5 (RET 4): Accept calls it on message 0x37.
    virtual void Reset(FRandEncParams* Params) = 0;
};

// "TLPzr", vtable 06fb7b14.
class FRandEncTLPopulator : public FRandEncPopulator
{
public:
    // FUN_06f81ed0: 0x16 (a ship left a trade lane: {ship, entry ring, exit ring, system})
    // arms the attack on a ring of the player's lane; 0x20 fires it once the player's ship
    // is within 10000 of that ring; 0x3b restores the attack chance.
    bool Accept(FRandEncParams* Params, pub::Controller::EMessage MessageId,
                void const* MessageParms) override;
    // FUN_06f82b90.
    void Parse(INI_Reader* Reader, FRandEncParams* Params) override;
    // LAB_06f83280.
    void Reset(FRandEncParams* Params) override;

    // +0x08: an attack is armed (Accept 0x16 06f829ff); 0x20 then tests the distance.
    // Reset clears it.
    bool bAttackArmed = false;
};

// "SPPzr", vtable 06fb7afc.
class FRandEncSPPopulator : public FRandEncPopulator
{
public:
    // FUN_06f80680: on 0x20 (MessageParms = the elapsed seconds, float) the activation test,
    // then the formation / position defaults and, without an attacker_rep, the attacker
    // faction picked from the target zone's population.
    bool Accept(FRandEncParams* Params, pub::Controller::EMessage MessageId,
                void const* MessageParms) override;
    // FUN_06f81120.
    void Parse(INI_Reader* Reader, FRandEncParams* Params) override;
    // LAB_06f818d0.
    void Reset(FRandEncParams* Params) override;

    // +0x08: seconds accumulated towards the time activation (Accept); Reset clears it.
    float ElapsedTime = 0.0f;
};

// The weighted pick inlined in TU31 (06efba67-06efbb4c, 06efbd7c-06efbe68, 06efc423-06efc4fa)
// and TU60 (TL Accept 06f82640-06f82715, TL Spawn 06f83c53-06f83d37): the weights are summed
// into a float; a zero sum (TEST AH,0x44 + JP falls through only on equal) picks nothing;
// otherwise R = Sum * (rand() * DAT_06fb4448 (1/32768)) as a float, and the first element
// whose running float sum reaches R (TEST AH,0x41 + JNP: taken when R < sum or R == sum, an
// unordered compare keeps walking) is picked; none -> end. Where retail then uses the list's
// head node (an empty list, a zero sum, a walk past the end), it reads the sentinel's
// uninitialised value; every caller in the port stops on end() instead.
template <typename TElement>
typename std::list<TElement>::iterator RandEncPickWeighted(std::list<TElement>* Elements,
                                                           float TElement::*Weight)
{
    float Sum = 0.0f;
    for (typename std::list<TElement>::iterator It = Elements->begin(); It != Elements->end();
         ++It)
        Sum = Sum + (*It).*Weight;
    if (Sum == 0.0f)
        return Elements->end();
    double const Scaled = static_cast<double>(std::rand()) * static_cast<double>(3.0517578125e-05f);
    float const Pick = static_cast<float>(static_cast<double>(Sum) * Scaled);
    Sum = 0.0f;
    for (typename std::list<TElement>::iterator It = Elements->begin(); It != Elements->end();
         ++It)
    {
        Sum = Sum + (*It).*Weight;
        if (Pick < Sum || Pick == Sum)
            return It;
    }
    return Elements->end();
}

// FUN_06efb830 (cdecl; callers TU38 06f11d75 / 06f11dd9 / 06f1802b / 06f18090): "TLParams" or
// "SPParams" (stricmp) -> a new object in *Out, else null. True when *Out is non-null.
bool __cdecl CreateRandEncParams(char const* Name, FRandEncParams** Out);
// FUN_06efb8e0 (cdecl; caller TU58 06f78951): the same by GetType code, 2 SP, 3 TL.
bool __cdecl CreateRandEncParamsByType(int Type, FRandEncParams** Out);
// FUN_06efb980 (cdecl): "TLPzr" or "SPPzr" -> a new populator in *Out, else null.
bool __cdecl CreateRandEncPopulator(char const* Name, FRandEncPopulator** Out);
// Random-mission encounters (docs/re/content.md, TU map, lane CNT-RM):
//   TU62 06f94d10-06f97360  killable solars (RandomMissions\KillableSolars.ini) and vignette
//                           critical loot (RandomMissions\VignetteCriticalLoot.ini).
// Both loaders take an already opened INI_Reader; the generator ctor FUN_06f9d550 opens and
// closes it.

// ---------------------------------------------------------------------------------------
// TU62 -- killable solars. DAT_06fd3c20: map<nickname id, entry>, static init LAB_06f94f30
// (map ctor FUN_06f95e80), atexit FUN_06f94f60.
// ---------------------------------------------------------------------------------------
struct FRmKillableSolar             // map value, retail 0x40 bytes
{
    ID_String Nickname;             // +0x00 CreateID("nickname")
    int SolarType;                  // +0x04 GRmSolarTypes value, -1 when absent/unknown
    int StringId;                   // +0x08 "string_id"
    unsigned char bAnyFaction;      // +0x0c "faction = all"
    std::vector<unsigned int> Factions; // +0x10 (alloc byte) / +0x14..+0x1c reputation groups
    unsigned int ArchetypeId;       // +0x20 pub::GetNicknameId("archetype")
    unsigned int LoadoutId;         // +0x24 pub::GetLoadoutID("loadout")
    float HitpointsMin;             // +0x28
    float HitpointsMax;             // +0x2c
    float DifficultyMin;            // +0x30
    float DifficultyMax;            // +0x34
    unsigned int ZoneTypeMask;      // +0x38 rock 0x40, debris 0x80, ice 0x100, lava 0x200,
                                    //       crystal 0x800
    int PilotChoices;               // +0x3c default 1
};

extern std::map<unsigned int, FRmKillableSolar> GRmKillableSolars;

void __cdecl LoadKillableSolars(INI_Reader* Reader);                                 // FUN_06f95040
FRmKillableSolar* __cdecl PickKillableSolar(unsigned int Faction, int SolarType,
                                            unsigned int ZoneTypeMask, float Difficulty); // FUN_06f956c0

// ---------------------------------------------------------------------------------------
// TU62 -- vignette critical loot. DAT_06fd3c10: vector of 0x34-byte records, static init
// LAB_06f95810, atexit FUN_06f95840 (record dtor FUN_06f95ca0).
// ---------------------------------------------------------------------------------------
struct FRmCriticalLoot              // retail 0x34 bytes
{
    int WhyDesired[2];              // +0x00 / +0x04 "why_desired" (two ints)
    int Name;                       // +0x08 "name" (int)
    unsigned int ArchetypeId;       // +0x0c pub::GetEquipmentID("archetype")
    std::vector<unsigned int> Types; // +0x10 (alloc byte) / +0x14..+0x1c "type": destroy 1,
                                    //       tractor 2, return 3
    unsigned char bAnyFaction;      // +0x20 "faction = all"
    std::vector<unsigned int> Factions; // +0x24 (alloc byte) / +0x28..+0x30
};

extern std::vector<FRmCriticalLoot> GRmCriticalLoot;

void __cdecl LoadVignetteCriticalLoot(INI_Reader* Reader);                           // FUN_06f95890
FRmCriticalLoot* __cdecl PickCriticalLoot(unsigned int Faction, unsigned int Type);           // FUN_06f95cd0

// FUN_06f95e00: vector<uint> insert-unique -- linear find; when absent insert at end and
// return {position, true}, else {found, false}. Shared COMDAT, called from TU27/46/51/62/63.
std::pair<std::vector<unsigned int>::iterator, bool> __cdecl RmInsertUnique(
    std::vector<unsigned int>* Values, unsigned int const& Value);
