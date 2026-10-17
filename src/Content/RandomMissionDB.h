// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "x86math.h"

#include <list>
#include <map>
#include <vector>

class INI_Reader;

// Random-mission static databases (docs/re/content.md, TU map, lane CNT-RM):
//   TU47 06f4a780-06f4ed80  rank -> difficulty table (missions\RankDiff.ini)
//   TU51 06f57cb0-06f5a2e0  ship classes (missions\shipclasses.ini), per-ship mission
//                           properties (ships\shiparch.ini), solar formations
//                           (RandomMissions\SolarFormations.ini)
// Every loader takes an already opened INI_Reader, exactly as retail: the caller runs
// OpenContentDataIni (FUN_06f24280) and closes the reader afterwards.

// ---------------------------------------------------------------------------------------
// The FUN_06fa0330 holder: retail's releasing-handle dtor for objects whose ref-counted
// interface sits at +4 -- the random mission (FRmMissionDataBase) and the TU61 offer: if set,
// (p + 4)->Release(), then null. Users: the TU47 mission object's +0x1f8 (Unwind@06faefe5 /
// 06faf070), the DestroyMission +0x240 (Unwind@06fabe08 / 06fabe68), the offer local of
// FUN_06ef09c0 (Unwind@06fac000). Holders released through FUN_06f28e10 (interface at +0,
// e.g. an [ObjList] directive) use the shared TRefHandle (RefHandle.h) instead.
// RmRefAddRef / RmRefRelease overloads next to each held type (RandomMission.h) pick its +4
// interface (found by argument-dependent lookup at instantiation).
// ---------------------------------------------------------------------------------------
template <typename T>
class TRmRef
{
public:
    TRmRef() = default;
    // Store, then AddRef when non-null (the member inits 06f4b144-06f4b152 and
    // 06eef409-06eef417).
    explicit TRmRef(T* InObject)
        : Object(InObject)
    {
        if (Object != nullptr)
            RmRefAddRef(Object);
    }
    // Copy + AddRef when set: the by-value holder argument of FUN_06f9e770 (06f9ec46-06f9ec61),
    // released by the callee.
    TRmRef(TRmRef const& Other)
        : Object(Other.Object)
    {
        if (Object != nullptr)
            RmRefAddRef(Object);
    }
    TRmRef& operator=(TRmRef const&) = delete;
    // FUN_06fa0330.
    ~TRmRef()
    {
        Reset();
    }

    // FUN_06fa0330 called directly (e.g. FUN_06ef1c70 06ef1c85 on the loader's mission).
    void Reset()
    {
        if (Object != nullptr)
        {
            RmRefRelease(Object);
            Object = nullptr;
        }
    }

    // The holder's address as an out-parameter: the callee stores a pointer whose reference
    // the holder then owns (no AddRef), e.g. BuildOffer(&Query, &Offer, ...) in FUN_06ef09c0.
    T** GetAddress()
    {
        return &Object;
    }

    T* Get() const
    {
        return Object;
    }

    T* operator->() const
    {
        return Object;
    }

private:
    T* Object = nullptr;
};

// {name, value} row of the retail .data name tables (PTR_s_big_solar_06fce748 and the
// other 4-entry tables of TU51/TU62).
struct FRmNamedValue
{
    char const* Name;
    int Value;
};

// .data PTR_s_big_solar_06fce748: big_solar 0, defensive_solar 1, killable_neutral 2,
// background_solar 3. One retail object, shared by TU51 and TU62.
extern FRmNamedValue const GRmSolarTypes[4];

// ---------------------------------------------------------------------------------------
// TU47 -- rank -> difficulty (DAT_06fd0c38: VC6 vector of 8-byte {int, float} records,
// static init LAB_06f4a9a0, atexit FUN_06f4a9d0).
// ---------------------------------------------------------------------------------------
struct FRmRankDifficulty
{
    int Stage;          // +0x0 story stage number (GContentStageNames value)
    float Difficulty;   // +0x4 clamped to [0, 100]
};

extern std::vector<FRmRankDifficulty> GRmRankDifficulties;

void __cdecl LoadRankDifficulties(INI_Reader* Reader);   // FUN_06f4a9f0
void __cdecl ClearRankDifficulties();                     // FUN_06f4ad30

struct FContentPlayerState;

// FUN_06f4ad60: a random difficulty in [MinDifficulty, MaxDifficulty] for Player's offer
// (TU35 FUN_06f041c0). Multiplayer: the top may grow by 1.3689^(2|4|6) and is clamped to
// [0, 100]. Single player: narrowed to the window around the difficulty of the player's
// stage (MissionNumber in GRmRankDifficulties); 0.0 when the window lies at or below
// MinDifficulty.
float __cdecl PickRmMissionDifficulty(FContentPlayerState const* Player, float MinDifficulty,
                                      float MaxDifficulty);

// ---------------------------------------------------------------------------------------
// TU47 -- systems loaded for random missions. DAT_06fd0c2c/DAT_06fd0c30: std::list of
// system ids (head, size), static init at 06f4d540, atexit at 06f4d570. Each listed system
// holds one FSystemInfo::Load(false) reference until ReleaseRmDestSystems.
// ---------------------------------------------------------------------------------------
extern std::list<unsigned int> GRmDestSystems;

void __cdecl RegisterRmDestSystem(unsigned int SystemId);  // FUN_06f4d5e0
void __cdecl ReleaseRmDestSystems();                       // FUN_06f4d650

// ---------------------------------------------------------------------------------------
// TU49 -- random-mission bonus loot (RandomMissions\RMLootInfo.ini).
// DAT_06fd0d2c: map<reputation group id (0xffffffff = "all"), records>, static init
// LAB_06f56040, atexit FUN_06f56070. DAT_06fd0d84: [Global] BonusLootDropChance.
// ---------------------------------------------------------------------------------------
struct FRmBonusLootEntry            // retail 0x18 bytes
{
    unsigned int Archetype;     // +0x00 pub::GetEquipmentID
    int NumToDropMin;           // +0x04
    int NumToDropMax;           // +0x08 = min when the second value is empty
    float DifficultyMin;        // +0x0c
    float DifficultyMax;        // +0x10
    float Weight;               // +0x14 get_value_int converted to float
};

extern std::map<unsigned int, std::vector<FRmBonusLootEntry>> GRmBonusLoot;
extern float GRmBonusLootDropChance;

void __cdecl LoadRmLootInfo(INI_Reader* Reader);                                   // FUN_06f566a0
void __cdecl LoadRmBonusLootSection(INI_Reader* Reader);                           // FUN_06f56160
float __cdecl GetRmBonusLootDropChance();                                          // FUN_06f56150
FRmBonusLootEntry const* __cdecl PickRmBonusLoot(unsigned int Faction, float Difficulty); // FUN_06f564d0

// ---------------------------------------------------------------------------------------
// TU51 -- ship classes (DAT_06fd0ddc: map<class id, member ids>, static init
// LAB_06f57eb0, atexit FUN_06f57ee0).
// ---------------------------------------------------------------------------------------
extern std::map<unsigned int, std::vector<unsigned int>> GRmShipClasses;

void __cdecl LoadShipClasses(INI_Reader* Reader);                                  // FUN_06f58190
void __cdecl ClearShipClasses();                                                   // FUN_06f57fc0
std::vector<unsigned int>* __cdecl FindShipClass(unsigned int const* ShipClassId);  // FUN_06f58080
bool __cdecl FindShipClassByMembers(unsigned int* OutShipClassId,
                                    std::vector<unsigned int> const& Members);     // FUN_06f580d0

// ---------------------------------------------------------------------------------------
// TU51 -- ship mission properties (DAT_06fd0dc8: map<ship arch id, property ids>,
// load-once flag DAT_06fd0e30, static init LAB_06f583c0, atexit FUN_06f583f0).
// ---------------------------------------------------------------------------------------
extern std::map<unsigned int, std::vector<unsigned int>> GRmShipMissionProperties;

void __cdecl LoadShipMissionProperties(INI_Reader* Reader);                       // FUN_06f584d0
bool __cdecl GetShipMissionProperties(unsigned int ShipArchId,
                                      std::vector<unsigned int>* InOutProperties); // FUN_06f587d0

// ---------------------------------------------------------------------------------------
// TU51 -- solar formations (DAT_06fd0e34: map<nickname id, formation>, static init
// LAB_06f58e10, atexit FUN_06f58e40).
// ---------------------------------------------------------------------------------------
struct FRmSolarFormationSlot    // retail 0x38 bytes
{
    unsigned char bMandatory;   // +0x00 "mandatory" = 1, "optional" = 0
    FMatrix Orientation;        // +0x04 identity unless a quaternion follows the position
    FVector Position;           // +0x28
    int SolarType;              // +0x34 index in {big_solar, defensive_solar,
                                //       killable_neutral, background_solar}
};

struct FRmSolarFormation        // map value, retail 0x14 bytes
{
    unsigned int Nickname;      // +0x00
    int Type;                   // +0x04 index in {big_solar_formation,
                                //       battle_background_formation,
                                //       defensive_outpost_formation, loot_group_formation},
                                //       -1 when "type" is absent or unknown
    std::vector<FRmSolarFormationSlot> Slots;   // +0x08 (alloc byte) / +0x0c..+0x14

    void CollectOptionalSlotTypes(std::vector<int>* InOutTypes) const;   // FUN_06f592f0
    void CollectMandatorySlotTypes(std::vector<int>* InOutTypes) const;  // FUN_06f593a0
    int GetSlotCount() const;                                            // FUN_06f59450
    FRmSolarFormationSlot* GetSlot(unsigned int Index);                  // FUN_06f59480
};

std::map<unsigned int, FRmSolarFormation>* __cdecl GetSolarFormations();          // FUN_06f58f20
void __cdecl LoadSolarFormations(INI_Reader* Reader);                              // FUN_06f58f30
