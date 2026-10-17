// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "RandomMissionDB.h"        // TRmRef (FUN_06f9e770's by-value offer holder)
#include "RandomMissionRuntime.h"
#include "common_id.h"
#include "common_server.h"

#include <cstdio>
#include <list>
#include <map>
#include <vector>

class INI_Reader;
struct FmtStr;
struct FRmSolarFormation;
struct FSystemInfo;

// Random-mission generator (docs/re/content.md, TU map, lane CNT-RM): TU63 06f97360-06fa75c0.
// Ported: the generator root object (DAT_06fd3c78: ctor FUN_06f9d550 = all RandomMissions
// table loads, dtor FUN_06f9d970), the vignette node tree (VignetteParams.ini load
// FUN_06f9ecb0, link FUN_06f9dd50, difficulty windows FUN_06f9dad0), NpcRankToDiff /
// Diff2Money tables, the "all groups" enumerator, the voice-properties database, the solar
// formation pick, the offer walk (offer node, tree paths, path filters) and the building of
// the TU61 offer (BuildOffer, BuildOfferFromPath, the filters' CreateOffer; the offer
// classes are RandomMissionOffer.h).

// TU63 -- random pick among the solar formations (DAT_06fd0e34) whose mandatory slot types
// are all in SpawnTypes and whose mandatory + optional types cover SpawnTypes. Callers:
// FUN_06f9ac80 (06f9ad97), FUN_06f9bee0 (06f9bf98).
FRmSolarFormation* __cdecl PickSolarFormation(std::vector<int> const* SpawnTypes); // FUN_06f97d90

// TU63 FUN_06f98010 (cdecl): the zone shown as TARGET_ZONE for Position in System -- of the
// named zones within 1.0 of it (pub::System::InZones, at most 0x14) the one with the smallest
// radius; with none, the navmap grid cell (row + column * 8 + 1). Caller LAB_06f896f0.
unsigned int __cdecl GetRmZoneAtPosition(FSystemInfo const* System, Vector const* Position);

// ---------------------------------------------------------------------------------------
// TU63 -- NPC rank -> difficulty per formation size (RandomMissions\NpcRankToDiff.ini).
// DAT_06fd3cd4: map<NPC rank, difficulties>, static init LAB_06f98420, atexit FUN_06f98450.
// ---------------------------------------------------------------------------------------
extern std::map<int, std::vector<float>> GRmNpcRankToDifficulty;

void __cdecl LoadNpcRankToDifficulty(INI_Reader* Reader);                        // FUN_06f98540

// ---------------------------------------------------------------------------------------
// TU63 -- difficulty -> money (RandomMissions\Diff2Money.ini, read inline by the
// generator ctor). DAT_06fd3cec: map<difficulty, money>, static init LAB_06f98270,
// atexit FUN_06f982a0; unique insert FUN_06fa0860.
// ---------------------------------------------------------------------------------------
extern std::map<float, int> GRmDiff2Money;

unsigned int __cdecl RmDifficultyToMoney(float Difficulty);                      // FUN_06f98380

// TU44 FUN_06f35b50: keep the two leading decimal digits of an amount above 100 and round
// the third to 0 or 5 in the second place's unit (called by FUN_06f98380).
unsigned int __cdecl RoundRmMoney(unsigned int Amount);

// ---------------------------------------------------------------------------------------
// TU63 -- every reputation group, collected once for "offer_group = all" /
// "hostile_group = all" (FUN_06f998c0). Object DAT_06fd3cb8 (vtable 06fbcd4c, 0x18 bytes),
// ctor FUN_06f98bf0 (static init LAB_06f98bd0), dtor FUN_06f98c90 (atexit FUN_06f98c80);
// the ctor publishes its address in DAT_06fd3d8c, the dtor clears it.
// ---------------------------------------------------------------------------------------
struct FRmAllGroupsEnumerator : public pub::Reputation::Enumerator
{
    bool bEnumerated;                           // +0x04 set by the caller after EnumerateGroups
    std::vector<unsigned int> Groups;           // +0x08 (alloc byte) / +0x0c..+0x14

    FRmAllGroupsEnumerator();                   // FUN_06f98bf0
    ~FRmAllGroupsEnumerator();                  // FUN_06f98c90
    char Execute(unsigned int GroupId) override; // LAB_06f98c20
};

extern FRmAllGroupsEnumerator* GRmAllGroups;    // DAT_06fd3d8c

// ---------------------------------------------------------------------------------------
// TU63 -- difficulty window (retail 0xc bytes, vtable 06fbd3f0):
//   [0] scalar deleting dtor LAB_06f9cff0, [1] IsInRange LAB_06f9cfa0,
//   [2] Overlaps LAB_06f98cc0, [3] Parse LAB_06f98d10, [4] Set LAB_06f9cfd0.
// ctor(min, max) FUN_06f9d500, copy ctor FUN_06f9cf70.
// ---------------------------------------------------------------------------------------
struct FRmDifficultyWindow
{
    float Min;                                  // +0x04
    float Max;                                  // +0x08

    FRmDifficultyWindow();                      // inline at FUN_06f9efb0 (0, 0)
    FRmDifficultyWindow(float InMin, float InMax); // FUN_06f9d500
    virtual ~FRmDifficultyWindow();
    virtual bool IsInRange(float Difficulty) const;                   // LAB_06f9cfa0
    virtual bool Overlaps(FRmDifficultyWindow const* Other) const;     // LAB_06f98cc0
    virtual void Parse(INI_Reader* Reader);                            // LAB_06f98d10
    virtual void Set(float InMin, float InMax);                        // LAB_06f9cfd0

    bool Equals(FRmDifficultyWindow const* Other) const;              // FUN_06f98dc0
    void Add(FRmDifficultyWindow const* Other);                        // FUN_06f9dcb0
};

// TU63 -- list of difficulty windows (retail 0x10 bytes, vtable 06fbd50c: [0] scalar
// deleting dtor FUN_06f9dd30 / dtor FUN_06f9dcd0; ctor FUN_06f9ef80). The list nodes hold
// FRmDifficultyWindow by value (0x14-byte nodes, element dtor through its vtable).
struct FRmDifficultyWindowList
{
    std::list<FRmDifficultyWindow> Windows;     // +0x04 (alloc byte) / +0x08 head, +0x0c size

    virtual ~FRmDifficultyWindowList();
    void AddUnique(FRmDifficultyWindow const* Window);                 // FUN_06f98d40
    void PushBack(FRmDifficultyWindow const* Window);                  // FUN_06f9f860
};

// TU63 -- one "offer_text" line entry (retail 0x18 bytes).
struct FRmOfferTextEntry
{
    int StrId;                                  // +0x00
    int Form;                                   // +0x04 singular 1, plural 2, default 3
    std::vector<int> Params;                    // +0x08 (alloc byte) / +0x0c..+0x14
                                                //       GRmTextParamNames values

    FRmOfferTextEntry();
};

// TU63 -- "offer_text" value (retail 0x18 bytes, vtable 06fbcf08: [0] Parse FUN_06f99010,
// [1] IsEmpty LAB_06f99390, [2] Build FUN_06f993c0). No virtual dtor: the owning vector
// runs FUN_06fa55e0 -> FUN_06f99cd0.
class IRmTextParamSource;                       // RandomMissionOffer.h (the TU61 offers)
struct FRmOfferText
{
    bool bAppend;                               // +0x04 "append" 1, "replace" 0
    std::vector<FRmOfferTextEntry> Entries;     // +0x08 (alloc byte) / +0x0c..+0x14

    FRmOfferText();
    virtual void Parse(INI_Reader* Reader);     // FUN_06f99010
    virtual bool IsEmpty() const;               // LAB_06f99390
    // Slot 2, FUN_06f993c0 (RET 0x10): per entry whose Form passes (see the .cpp), a local
    // FmtStr is built (begin_mad_lib(StrId), Source slot 0 per parameter, end_mad_lib) and
    // appended to *Out (FmtStr::append_fmt_str). The two flags are the offer group's and the
    // hostile group's FFactionProps::bSingularNickname (the only caller, FUN_06f9e1f0
    // 06f9e231-06f9e249 / 06f9e42a-06f9e445).
    virtual void Build(bool bOfferGroupSingular, bool bHostileGroupSingular, FmtStr* Out,
                       IRmTextParamSource* Source);
};

// TU63 -- "objective_text" values (retail 0x18 bytes, vtable 06fbd510: [0] Parse
// FUN_06f99560). map<objective, text> at +0x04 (ctor FUN_06fa25f0, insert FUN_06fa0590,
// signed key compare).
struct FRmObjectiveText
{
    std::map<int, FRmParamText> Texts;          // +0x04

    virtual char Parse(INI_Reader* Reader, unsigned int Index);        // FUN_06f99560
};

// TU63 -- one line of a comm sequence (retail 0x48 bytes; vector insert FUN_06ed5080).
struct FRmCommLine
{
    int Speaker;                                // +0x00 GRmCommTargetNames value, default 1
    unsigned int Length;                        // +0x04
    char Text[0x40];                            // +0x08 at most 0x3f characters

    FRmCommLine();
};

// TU63 -- "comm_sequence" value (retail 0x24 bytes; element dtor FUN_06f971c0 ->
// FUN_06f956a0).
struct FRmCommSequence
{
    int Trigger;                                // +0x00 GRmCommTriggerNames value
    int Target;                                 // +0x04 GRmCommTargetNames value, default 1
    float Field_0x08;                           // +0x08 value 2 TODO: meaning unknown
    // +0x0c / +0x10 values 3 / 4: the conversation's delay is MinDelay + (MaxDelay - MinDelay)
    // * rand() / 32768 (FUN_06f9e770 06f9e82c-06f9e864).
    float MinDelay;                             // +0x0c
    float MaxDelay;                             // +0x10
    std::vector<FRmCommLine> Lines;             // +0x14 (alloc byte) / +0x18..+0x20

    FRmCommSequence();
    void Parse(INI_Reader* Reader);             // FUN_06f98df0
};

// TU63 -- per-node parameters (retail 0xd0 bytes, vtable 06fbd554: [0] scalar deleting
// dtor FUN_06f9f300 / dtor FUN_06f9f100, [1] ParseKey FUN_06f998c0). ctor FUN_06f9efb0,
// copy ctor FUN_06fa0050 (member-wise).
struct FRmVignetteParams
{
    float Weight;                               // +0x04 0 in the ctor; 0 -> 1.0 after a node
    std::vector<FRmOfferText> OfferTexts;       // +0x08 (alloc byte) / +0x0c..+0x14
    FRmObjectiveText ObjectiveText;             // +0x18
    FRmParamText FailureText;                   // +0x30
    FRmParamText RewardText;                    // +0x60
    FRmDifficultyWindow Difficulty;             // +0x90
    std::vector<unsigned int> OfferGroups;      // +0x9c (alloc byte) / +0xa0..+0xa8
    std::vector<unsigned int> HostileGroups;    // +0xac (alloc byte) / +0xb0..+0xb8
    std::vector<FRmCommSequence> CommSequences; // +0xbc (alloc byte) / +0xc0..+0xc8
    unsigned int AllowableZoneTypes;            // +0xcc open 1, field 2, exclusion 4;
                                                //       default 7

    FRmVignetteParams();                        // FUN_06f9efb0
    virtual ~FRmVignetteParams();               // FUN_06f9f100
    virtual char ParseKey(INI_Reader* Reader);  // FUN_06f998c0
};

// TU63 -- one [DecisionNode] / [DocumentationNode] / [DataNode] of VignetteParams.ini
// (retail 0x114 bytes, vtable 06fbd550: [0] Load LAB_06f99e40 -- no virtual dtor).
// Default ctor inline at FUN_06f9ecb0 / FUN_06f9fc20; copy ctor FUN_06f9fec0.
struct FRmVignetteNode
{
    ID_String Nickname;                         // +0x04 CreateID("nickname"/"documentation")
    unsigned int NodeId;                        // +0x08 -1
    std::vector<unsigned int> Parents;          // +0x0c (alloc byte) / +0x10..+0x18
                                                //       filled by FUN_06f9dd50
    std::vector<unsigned int> Children;         // +0x1c (alloc byte) / +0x20..+0x28
    bool bImplemented;                          // +0x2c 1
    FRmDifficultyWindowList Windows;            // +0x30 filled by FUN_06f9dad0
    FRmVignetteParams Params;                   // +0x40
    float FirstChildChance;                     // +0x110 -1.0: chance of the first child
                                                //       on a path (FUN_06f9d070); -1 means
                                                //       1 / child count. No loader writes it.

    FRmVignetteNode();
    virtual void Load(INI_Reader* Reader);      // LAB_06f99e40
    bool ParseCommonKey(INI_Reader* Reader);    // FUN_06f99d60
};

// ---------------------------------------------------------------------------------------
// TU63 -- offer request (retail 0x34 bytes, ctor FUN_06f9cac0). Filled by the random
// mission (FUN_06ef09c0) and checked by FRandomMissionGenerator::BuildOffer.
// ---------------------------------------------------------------------------------------
struct FRmOfferQuery
{
    int OfferGroup;                             // +0x00 -1
    unsigned int EnemyGroup;                    // +0x04 -1
    int Reward;                                 // +0x08 0 (signed compare in 06f9f714)
    float RepReward;                            // +0x0c 0
    float RepHit;                               // +0x10 0
    unsigned int OfferBase;                     // +0x14 0
    float Difficulty;                           // +0x18 DAT_06fbcbc0 (0.0)
    unsigned int ObjectiveZone;                 // +0x1c 0
    Vector ObjectivePos;                        // +0x20 0
    unsigned int PlayerId;                      // +0x2c 0
    unsigned int DestSys;                       // +0x30 0

    FRmOfferQuery();                            // FUN_06f9cac0
};

// ---------------------------------------------------------------------------------------
// TU63 -- one decision path from the root of the vignette tree (retail 0x14 bytes, vtable
// 06fb7750: [0] Save LAB_06f99ea0, [1] Load FUN_06f99f10; built inline at 06ef0658,
// 06f9cd95, 06fa4ff5). One byte per decision: 1 = first child, 0 = second child. The random
// mission keeps one at +0x68 and saves it as "tree_path = TFFT...".
// ---------------------------------------------------------------------------------------
struct FRmTreePath
{
    std::vector<char> Bits;                     // +0x04 (alloc byte) / +0x08..+0x10

    virtual void Save(FILE* File);              // LAB_06f99ea0
    virtual void Load(INI_Reader* Reader);      // FUN_06f99f10
};

// TU61 random-mission offers (RandomMissionOffer.h): the base offer and the vignette offer
// the destroy-solars and assassinate filters create.
class FRmOffer;
class FRmVignetteOffer;
class FRmSolarComponent;
class FRmShipComponent;
class FRmTargetShipComponent;
struct FRmKillableSolar;
struct FRmSolarSpawn;

// TU63 -- the formation size a ship block gets (the three dwords FUN_06f98780 writes at the
// block's +0x08).
struct FRmShipSize
{
    float Difficulty;                           // +0x00 the NpcRankToDiff entry of Count
    int Level;                                  // +0x04 the NPC level
    int Count;                                  // +0x08 the number of ships; 0 when none
};

// TU63 -- a ship block (0x20-byte stack record of FUN_06f9a700 / FUN_06f9b790), read by
// FUN_06f979b0. Field meanings are open: the names say what writes and reads them.
struct FRmShipBlock
{
    unsigned char Field_0x00;                   // -> ship group +0x00 (pk flag)
    // FUN_06f979b0: non-zero -> the first ship's +0xd8 = 2, the others' 0, and the first
    // ship's +0xdc = 2 (its random name off, +0xd4 = the caller's last argument).
    unsigned char Field_0x01;
    unsigned char Field_0x02;                   // FUN_06f979b0: +0xd8 = 2 for every ship; pk flag
    unsigned char Field_0x03;                   // -> ship group +0x02
    // FUN_06f979b0: non-zero -> the first ship carries a critical loot (FUN_06f95cd0 of
    // Field_0x14); 0 -> a bonus loot when the drop chance roll succeeds.
    unsigned char Field_0x04;
    FRmShipSize Size;                           // +0x08 FUN_06f98780
    int Field_0x14;                             // FUN_06f979b0 critical loot (FUN_06f95cd0)
    int Field_0x18;                             // -> ship group +0x04
    int Field_0x1c;                             // -> ship group +0x08
};

// TU63 FUN_06f98780 (cdecl): the formation size for Difficulty among the class_fighter
// npc_ship archs of Faction -- per NPC level 1..25 the NpcRankToDiff count whose difficulty
// is closest to Difficulty, then a weighted pick by count (single-player weights
// DAT_06fbcc58, multiplayer DAT_06fbcc7c). Nothing picked: only Out->Count = 0.
void __cdecl PickRmShipFormationSize(FRmShipSize* Out, float Difficulty, unsigned int Faction);

struct FRmShipGroup;
struct FRmLootSpawn;
struct FNpcNameParams;

// TU63 FUN_06f97580 (cdecl): Size.Count ships of GroupId into Group->Ships, at most one per
// slot of the faction's "fighters" formation -- a class_fighter / "D<level>" npc_ship arch,
// named "Vignette_ship_%X". The first ship gets Loot (when given) and the First* values, the
// others the Other* values. bMatchLevel rejects a level 0 or an arch more than one level off.
bool __cdecl BuildRmShips(FRmShipGroup* Group, unsigned int GroupId, FRmShipSize Size,
                          int FirstField_0xd8, int OtherField_0xd8, int FirstField_0xdc,
                          int OtherField_0xdc, FRmLootSpawn const* Loot, FNpcNameParams* TargetName,
                          bool bMatchLevel);

// TU63 FUN_06f979b0 (cdecl): a new ship group of the block's size in Component (the offer
// group's when bFriendly, else the hostile group's) at *Position, with an optional loot for
// its first ship; the result of FUN_06f97580.
bool __cdecl BuildRmShipGroup(FRmOfferQuery const* Query, FRmShipComponent* Component,
                              FRmShipBlock const* Block, Vector const* Position,
                              bool bFriendly, FNpcNameParams* TargetName);

// ---------------------------------------------------------------------------------------
// TU63 -- path filter. FRandomMissionGenerator::BuildOfferFromPath (FUN_06f9e9b0) starts
// with a base filter, feeds it every node of the chosen path and swaps it for the object
// Specialize returns once a mission type was recognised; the last filter creates the
// offer. Filters have no virtual destructor: retail frees them with plain operator delete
// (FUN_06fa8fe0), so every filter class stays trivially destructible.
// Slots: [0] Slot00, [1] HasMissionType, [2] ApplyNode, [3] Specialize, [4] CreateOffer,
// [5] SupportsTextParam.
// ---------------------------------------------------------------------------------------
struct FRmPathFilter                            // retail 0x0c, vtable 06fbcf6c
{
    bool bRandomMission;                        // +0x04 node "random_mission"
    bool bDestroySolars;                        // +0x05 node "destroy_solars_mission"
    bool bAssassinate;                          // +0x06 node "assassinate_mission"
    bool bDestroyShips;                         // +0x07 node "destroy_ships_mission"
    bool bLootItem;                             // +0x08 node "loot_item_mission"

    FRmPathFilter();                            // inline at 06f9e9e0
    virtual int Slot00() const;                 // FUN_06ea1450: 0. TODO: name unknown
    virtual bool HasMissionType() const;        // FUN_06f9a170
    virtual bool ApplyNode(FRmVignetteNode const* Node, char Bit);    // FUN_06f9a1a0
    virtual FRmPathFilter* Specialize() const;  // FUN_06f9a270
    virtual bool CreateOffer(FRmOfferQuery const* Query, FRmOffer** OutOffer); // FUN_06f9a410
    virtual bool SupportsTextParam(int Param) const;                  // LAB_06f9a430
};

// Destroy-solars filter (retail 0x1c, vtable 06fbd034; built by FUN_06f9a270 with the base
// flags copied and its own flags cleared). Each flag is set by the path node of that name.
struct FRmDestroySolarsFilter : FRmPathFilter
{
    bool bBigSolar;                             // +0x0c "big_solar"
    bool bPkBigSolar;                           // +0x0d "pk_big_solar"
    bool bDefensiveSolars;                      // +0x0e "defensive_solars"
    bool bPkDefensiveSolars;                    // +0x0f "pk_defensive_solars"
    bool bCavalry;                              // +0x10 "cavalry"
    bool bHostileShips;                         // +0x11 "hostile_ships"
    bool bPkHostileShips;                       // +0x12 "pk_hostile_ships"
    bool bHostileShipsAtInstallation;           // +0x13 "hostile_ships_at_installation"
    bool bHostileShipsAfter30S;                 // +0x14 "hostile_ships_after_30_s"
    bool bHostileWaves;                         // +0x15 "hostile_waves"
    bool bFriendlyShips;                        // +0x16 "friendly_ships"
    bool bFriendlyShipsAtInstallation;          // +0x17 "friendly_ships_at_installation"
    bool bFriendlyShipsAfter30S;                // +0x18 "friendly_ships_after_30_s"
    bool bFriendlyWaves;                        // +0x19 "friendly_waves"

    FRmDestroySolarsFilter();                   // inline at 06f9a2aa / 06f9a6aa
    bool HasMissionType() const override;       // FUN_06f06190: bCavalry
    bool ApplyNode(FRmVignetteNode const* Node, char Bit) override;   // LAB_06f9a470
    FRmPathFilter* Specialize() const override; // FUN_06f9a690
    bool CreateOffer(FRmOfferQuery const* Query, FRmOffer** OutOffer) override; // FUN_06f9a700
    bool SupportsTextParam(int Param) const override;                 // FUN_06f9abe0
    // Slot 6 (+0x18), new in this class (vtables 06fbd034 and 06fbd164, raw dword after
    // slot 5): LAB_06ee9e10 = MOV EAX,1; RET. TODO: name -- no +0x18 call on a filter found
    // yet in TU63 (06f9a700 / 06f9ac80 / 06f9af40 / 06f9e1f0 checked).
    virtual int Slot06() const;

    // FUN_06f9ac80 (thiscall, RET 8): the solars of the offer -- position, activation, the
    // solar formation and a KillableSolars entry per solar type; false when either is
    // missing.
    bool FillSolarSpawns(FRmOfferQuery const* Query, FRmSolarComponent* Solars) const;
    // FUN_06f9af40 (thiscall, RET 0x18): one solar per matching formation slot and entry
    // until the entries' scaled difficulties reach Difficulty.
    void SpawnSolars(FRmOfferQuery const* Query, unsigned int EnemyGroup, float Difficulty,
                     FRmSolarFormation* Formation,
                     std::vector<std::pair<FRmKillableSolar*, float>> const* Entries,
                     std::list<FRmSolarSpawn>* OutSolars) const;
};

// Destroy-solars filter after a "cavalry" node (retail 0x1c, vtable 06fbd164; built by
// FUN_06f9a690 with every flag cleared): slots 1/2 are FUN_06ea1480 / FUN_06f9a6f0 (always
// false), so no later node changes it; 3/4/5 are the destroy-solars ones.
struct FRmCavalryFilter : FRmDestroySolarsFilter
{
    bool HasMissionType() const override;       // FUN_06ea1480
    bool ApplyNode(FRmVignetteNode const* Node, char Bit) override;   // FUN_06f9a6f0
};

// Assassinate filter (retail 0x28, vtable 06fbd01c; built by FUN_06f9a270 with the base
// flags copied, ctor FUN_06f9b390 clears +0x0c..+0x26). Flags[i] is set by the path node
// named GRmAssassinateNodeNames[i] (LAB_06f9b3f0).
struct FRmAssassinateFilter : FRmPathFilter
{
    bool Flags[27];                             // +0x0c..+0x26

    FRmAssassinateFilter();                     // FUN_06f9b390
    int Slot00() const override;                // LAB_06f9a400: Flags[0] ? 2 : 3
    bool HasMissionType() const override;       // FUN_06ea1480
    bool ApplyNode(FRmVignetteNode const* Node, char Bit) override;   // FUN_06f9b3f0
    FRmPathFilter* Specialize() const override; // FUN_06ea1450: null
    bool CreateOffer(FRmOfferQuery const* Query, FRmOffer** OutOffer) override; // FUN_06f9b790
    bool SupportsTextParam(int Param) const override;                 // FUN_06f9ba50

    // FUN_06f9c100 (thiscall, RET 0x10): the target's ship groups at a random point of a ring
    // around the objective (pre-battle or main-battle wave, the target group itself carrying
    // TargetName); +0x50 / +0x54 of the component are set at the end.
    bool FillTargetShips(FRmOfferQuery const* Query, FRmTargetShipComponent* TargetShips,
                         float Share, FNpcNameParams* TargetName) const;
    // FUN_06f9c410 (thiscall, RET 0x14): the defensive solars (FUN_06f9bee0) and the hostile /
    // friendly ship groups of the main battle at the objective.
    bool FillShipGroups(FRmOfferQuery const* Query, FRmShipComponent* Ships, float Share,
                        FRmSolarComponent* Solars, FNpcNameParams* TargetName) const;
    // FUN_06f9bee0 (thiscall, RET 8): 1..3 defensive solars (type 1) at the objective; false
    // without a formation or a KillableSolars entry.
    bool FillDefensiveSolars(FRmOfferQuery const* Query, FRmSolarComponent* Solars) const;
    // FUN_06f9bb00 (thiscall, RET 0x18): FRmDestroySolarsFilter::SpawnSolars (FUN_06f9af40)
    // without the pk modes -- every solar's +0x4d8 is 0.
    void SpawnSolars(FRmOfferQuery const* Query, unsigned int EnemyGroup, float Difficulty,
                     FRmSolarFormation* Formation,
                     std::vector<std::pair<FRmKillableSolar*, float>> const* Entries,
                     std::list<FRmSolarSpawn>* OutSolars) const;
};

extern char const* const GRmAssassinateNodeNames[27];

// Destroy-ships filter (retail 0x0c, vtable 06fbd004) and loot-item filter (retail 0x0c,
// vtable 06fbcfec): both built by FUN_06f9a270 with every flag cleared. Slots 1 and 2 are
// FUN_06ea1480 / FUN_06f9a6f0 (always false); 0, 3, 4 and 5 are the base ones. Their
// offers are created by the base CreateOffer, i.e. never.
struct FRmDestroyShipsFilter : FRmPathFilter
{
    bool HasMissionType() const override;       // FUN_06ea1480
    bool ApplyNode(FRmVignetteNode const* Node, char Bit) override;   // FUN_06f9a6f0
};

struct FRmLootItemFilter : FRmPathFilter
{
    bool HasMissionType() const override;       // FUN_06ea1480
    bool ApplyNode(FRmVignetteNode const* Node, char Bit) override;   // FUN_06f9a6f0
};

// ---------------------------------------------------------------------------------------
// TU63 -- the random-mission generator root. One static instance DAT_06fd3c78 (static init
// LAB_06f9d520, atexit LAB_06f9d540); its address is published in DAT_06fcf25c.
// vtable 06fbd4fc: [0] scalar deleting dtor FUN_06f9d880, [1] Load FUN_06f9ecb0
// (VignetteParams.ini), [2] Unload LAB_06f9f600, [3] BuildOffer FUN_06f9f6d0 (build an
// offer; caller FUN_06ef09c0 at 06ef0a5c).
// ---------------------------------------------------------------------------------------
struct FRandomMissionGenerator;

// TU63 -- the generator's publishing sub-object (no storage). Its construction is the first
// thing FUN_06f9d550 does (06f9d57a: DAT_06fcf25c = this, before any member); its destructor
// FUN_06f9fc10 (DAT_06fcf25c = 0) is inlined last into FUN_06f9d970
// (06f9da41, after the members) and called by the unwind funclets 06fb19e9 / 06fb1a72 when
// the ctor or dtor throws.
struct FRmGeneratorRegistration
{
    explicit FRmGeneratorRegistration(FRandomMissionGenerator* Generator);
    ~FRmGeneratorRegistration();                // FUN_06f9fc10
};

struct FRandomMissionGenerator : private FRmGeneratorRegistration
{
    bool bLoaded;                               // +0x04 set at the end of Load
    bool bLinked;                               // +0x05 set at the end of FUN_06f9dd50
    unsigned int RootNodeId;                    // +0x08 -1 in ctor/dtor; FUN_06f9dd50
    std::vector<unsigned int> LeafNodeIds;      // +0x0c (alloc byte) / +0x10..+0x18
                                                //       implemented nodes without children
    std::map<unsigned int, FRmVignetteNode> Nodes; // +0x1c map<node id, node>
                                                //       (insert FUN_06fa0e70, unsigned keys)

    FRandomMissionGenerator();                  // FUN_06f9d550
    virtual ~FRandomMissionGenerator();         // FUN_06f9d970 / sdtor FUN_06f9d880
    virtual void Load(INI_Reader* Reader);      // FUN_06f9ecb0
    // Slot 2, LAB_06f9f600: +0x04 / +0x05 = 0, RootNodeId = -1, the leaf ids and the node map
    // emptied (the map's inline clear: FUN_06fa38c0 subtree erase with the node dtor
    // FUN_06fa58a0, or the one-by-one erase FUN_06fa3470 path).
    virtual void Unload();
    // Slot 3, FUN_06f9f6d0 (called through the vtable by FUN_06ef09c0 at 06ef0a5c).
    virtual bool BuildOffer(FRmOfferQuery const* Query, FRmOffer** OutOffer,
                            FRmTreePath* OutPath);

    void Link();                                                      // FUN_06f9dd50
    void DisableChildren(std::vector<unsigned int> const* ChildIds);  // FUN_06f9da60
    void PropagateDifficultyWindows(unsigned int NodeId);             // FUN_06f9dad0

    // Offer construction helpers of BuildOffer (slot 3 above).
    unsigned int PickOfferNode(FRmOfferQuery const* Query);           // FUN_06f9cb00
    bool ChooseTreePath(FRmTreePath* OutPath, unsigned int OfferNodeId, float Difficulty,
                        int OfferGroup);                              // FUN_06f9d2b0
    void CollectTreePaths(std::vector<FRmTreePath>* OutPaths, unsigned int NodeId,
                          FRmDifficultyWindow Window, int OfferGroup); // FUN_06f9cd00
    FRmTreePath* PickTreePath(std::vector<FRmTreePath>* Paths);       // FUN_06f9d070
    bool BuildOfferFromPath(FRmTreePath const* Path, FRmOfferQuery const* Query,
                            FRmOffer** OutOffer);                     // FUN_06f9e9b0
    // FUN_06f9e770 (RET 8): the comm sequences of every node on Path go into the vignette
    // offer's General component; the holder argument is released on return.
    void BuildCommSequences(FRmTreePath const* Path, TRmRef<FRmOffer> Offer);
    // FUN_06f9e1f0 (RET 0x14; caller FUN_06f9e9b0 06f9ec23): the offer text, the reward and
    // failure texts and the objective texts of every node on Path, then the objectives
    // (FUN_06f9dee0). Source is the caller's *OutOffer read before the call.
    void BuildOfferTexts(FRmOfferQuery const* Query, FRmTreePath const* Path,
                         FRmPathFilter* Filter, FRmOffer* Source, FRmOffer** OutOffer);
    // FUN_06f9dee0 (RET 0x10; caller FUN_06f9e1f0 06f9e524): one TU61 objective per objective
    // text into the vignette offer's component maps. this and Filter are not read.
    void BuildObjectives(FRmOfferQuery const* Query, FRmOffer** OutOffer,
                         FRmObjectiveText const* Texts, FRmPathFilter* Filter);
};

extern FRandomMissionGenerator* GRandomMissionGenerator;  // DAT_06fcf25c

// ---------------------------------------------------------------------------------------
// TU63 -- voice properties (missions\voice_properties.ini and the audio\voices_*.ini files).
// DAT_06fd3d90: vector of 0x1c-byte records, static init LAB_06fa66e0, atexit FUN_06fa6710
// (record dtor FUN_06fa6d20).
// ---------------------------------------------------------------------------------------
struct FVoiceProperties                             // retail 0x1c bytes
{
    unsigned int VoiceId;                           // +0x00 pub::GetVoiceID("voice")
    unsigned int Flags;                             // +0x04 gender: male 1, female 2;
                                                    //       supports_roles: combat_lev_1 0x4000,
                                                    //       combat_lev_2 0x4, combat_lev_3 0xc004,
                                                    //       scan 0x8, hail_hailee 0x10,
                                                    //       hail_dock 0x20, dockrequest_dock 0x40,
                                                    //       dockrequest_docker 0x80, behavior 0x100,
                                                    //       misc_chatter 0x200, fidget 0x400,
                                                    //       combat_dock 0x800,
                                                    //       random_mission_ship 0x1000,
                                                    //       mission_commission 0x2000
    std::map<unsigned int, int> PermutationCounts;  // +0x08 nickname id -> permutation count

    int GetPermutationCount(unsigned int const* NicknameId) const;              // FUN_06fa6de0
    bool SupportsRoles(unsigned int Mask) const;                                // FUN_06fa6e30
};

extern std::vector<FVoiceProperties> GVoiceProperties;

void __cdecl LoadVoiceProperties(INI_Reader* Reader);                            // FUN_06fa6760
FVoiceProperties* __cdecl FindVoiceProperties(unsigned int const* VoiceId);      // FUN_06fa6e50
