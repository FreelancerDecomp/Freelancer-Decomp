// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RandomMissionDB.h"

#include "RandomMission.h"
#include "RandomMissionObject.h"
#include "ContentInternal.h"
#include "ContentMessageRouter.h"
#include "ContentPlayerState.h"
#include "ContentSort.h"        // ContentSort (VC6 std::sort, FUN_06f4a9f0's tail)
#include "ContentStageNames.h"
#include "ContentStaticIds.h"
#include "Cargo.h"              // PickFactionShipLineArch (TU44)
#include "DockQueues.h"         // PlaceFormationBlocks (TU53)
#include "EncounterParams.h"    // FEncounterShip
#include "Encounters.h"         // SetMatrixIdentity
#include "FactionProps.h"
#include "MissionObjList.h"     // CreateObjListDirective, FObjListGotoShip (TU40)
#include "NpcNames.h"           // FNpcNameRequest / BuildNpcName (TU42)
#include "NpcShip.h"            // FContentNpcShipParams (TU43)
#include "NpcShips.h"           // FNpcShipArch
#include "ObjListDirective.h"
#include "RandomEncounters.h"   // TU31 classes
#include "RefHandle.h"          // TRefHandle (FUN_06f28e10 holders)
#include "SpacePaths.h"         // BuildNnPath (FUN_06f5ea40)
#include "SystemConnections.h"
#include "SystemInfo.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"
#include "common_csys.h"        // Transform, Matrix (pub::SpaceObj::GetEmptyPos / GetLocation)
#include "server_cobject.h"     // pub::SpaceObj::IsPosEmpty / GetEmptyPos

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string.h>

namespace
{
    // FUN_06f95e00 and its inline copies: linear find, insert at end when absent.
    void AppendUnique(std::vector<unsigned int>* Values, unsigned int Value)
    {
        std::vector<unsigned int>::iterator It = std::find(Values->begin(), Values->end(), Value);
        if (It == Values->end())
            Values->insert(Values->end(), Value);
    }

    void AppendUniqueInt(std::vector<int>* Values, int Value)
    {
        std::vector<int>::iterator It = std::find(Values->begin(), Values->end(), Value);
        if (It == Values->end())
            Values->insert(Values->end(), Value);
    }
}

// ---------------------------------------------------------------------------------------
// Story stage names -- retail .data PTR_s_base_0_rank_06fce5d0 (see ContentStageNames.h).
// ---------------------------------------------------------------------------------------
FContentStageName const GContentStageNames[43] = {
    { "base_0_rank", 0x00 },
    { "mission_01a_loaded", 0x01 },
    { "mission_01a_accepted", 0x02 },
    { "mission_01b_loaded", 0x03 },
    { "mission_01b_accepted", 0x04 },
    { "freetime_01_02", 0x05 },
    { "mission_02_loaded", 0x07 },
    { "mission_02_accepted", 0x08 },
    { "freetime_02_03", 0x09 },
    { "mission_03_loaded", 0x0a },
    { "mission_03_accepted", 0x0b },
    { "freetime_03_04", 0x0c },
    { "mission_04_loaded", 0x0d },
    { "mission_04_accepted", 0x0e },
    { "freetime_04_05", 0x0f },
    { "mission_05_loaded", 0x10 },
    { "mission_05_accepted", 0x11 },
    { "freetime_05_06", 0x12 },
    { "mission_06_loaded", 0x13 },
    { "mission_06_accepted", 0x14 },
    { "freetime_06_07", 0x15 },
    { "mission_07_loaded", 0x16 },
    { "mission_07_accepted", 0x17 },
    { "freetime_07_08", 0x18 },
    { "mission_08_loaded", 0x19 },
    { "mission_08_accepted", 0x1a },
    { "freetime_08_09", 0x1b },
    { "mission_09_loaded", 0x1c },
    { "mission_09_accepted", 0x1d },
    { "freetime_09_10", 0x1e },
    { "mission_10_loaded", 0x1f },
    { "mission_10_accepted", 0x20 },
    { "freetime_10_11", 0x21 },
    { "mission_11_loaded", 0x22 },
    { "mission_11_accepted", 0x23 },
    { "freetime_11_12", 0x24 },
    { "mission_12_loaded", 0x25 },
    { "mission_12_accepted", 0x26 },
    { "freetime_12_13", 0x27 },
    { "mission_13_loaded", 0x28 },
    { "mission_13_accepted", 0x29 },
    { "mission_end", 0x2a },
    { nullptr, 0x00 },
};

// =======================================================================================
// TU47 -- rank -> difficulty
// =======================================================================================

// DAT_06fd0c38 (static init LAB_06f4a9a0, atexit FUN_06f4a9d0).
std::vector<FRmRankDifficulty> GRmRankDifficulties;

// FUN_06f4a9f0 (06f4a9f0-06f4ad27). [RankDiffDB] rank_diff = <stage name>, <difficulty>.
void __cdecl LoadRankDifficulties(INI_Reader* Reader)
{
    // 06f4aa89 writes the matched value into the stack slot of param_1 and 06f4aa90 reads
    // it back for every line: a name missing from the table therefore reuses the previous
    // line's stage, and the very first line reuses the INI_Reader pointer bits.
    int LastStage = static_cast<int>(reinterpret_cast<intptr_t>(Reader));

    while (Reader->read_header())
    {
        if (!Reader->is_header("RankDiffDB") || !Reader->read_value())
            continue;

        do
        {
            if (Reader->is_value("rank_diff"))
            {
                FRmRankDifficulty Record;
                Record.Stage = 0;
                Record.Difficulty = 0.0f;

                char const* StageName = Reader->get_value_string(0);
                // 06f4aa64-06f4aa85: all 43 entries (bound 0x158), null names skipped.
                for (int Index = 0; Index < 43; ++Index)
                {
                    if (GContentStageNames[Index].Name != nullptr &&
                        _stricmp(StageName, GContentStageNames[Index].Name) == 0)
                    {
                        LastStage = GContentStageNames[Index].Value;
                        break;
                    }
                }
                Record.Stage = LastStage;

                // 06f4aaa2-06f4aaf2: FCOMP 0.0 / TEST AH,0x41 / JNZ keeps the value when
                // 0.0 <= v; FCOMP 100.0 / TEST AH,0x5 / JP stores 100.0 when the clamped
                // value is not below 100 (or unordered); else 0.0 when v < 0, else v.
                float const Value = Reader->get_value_float(1);
                float const Floor = (Value < 0.0f) ? 0.0f : Value;
                if (!(Floor < 100.0f))
                    Record.Difficulty = 100.0f;
                else if (Value < 0.0f)
                    Record.Difficulty = 0.0f;
                else
                    Record.Difficulty = Value;

                // FUN_06f96770(end, 1, &Record) -- insert at end.
                GRmRankDifficulties.push_back(Record);
            }
        } while (Reader->read_value());
    }

    // 06f4ab2c-06f4ad27: VC6 std::sort (_Sort_0 with the first _Sort pass, the partition and
    // the insertion sort inlined; _Median FUN_06f4ece0, _Sort FUN_06f4eb70, _Unguarded_insert
    // FUN_06f4eca0), ordered by the int Stage only (CMP / JL on the first dword). ContentSort
    // runs the same steps, so records with an equal Stage keep retail's order.
    FRmRankDifficulty* const First = GRmRankDifficulties.data();
    ContentSort(First, First + GRmRankDifficulties.size(),
                [](FRmRankDifficulty const& Left, FRmRankDifficulty const& Right)
                {
                    return Left.Stage < Right.Stage;
                });
}

// FUN_06f4ad30: end = begin (records are trivially destructible).
void __cdecl ClearRankDifficulties()
{
    GRmRankDifficulties.clear();
}

namespace
{
    // The clamp of FUN_06f4ad60 (06f4adc1-06f4ae10, 06f4ae91-06f4aef4, 06f4aefc-06f4af4c):
    // Low = (0 > Value) ? 0 : Value (FCOMP + TEST AH,0x41 / JNZ); !(Low < 100) (TEST AH,0x5 /
    // JP, unordered included) gives 100; otherwise 0 when 0 > Value, else Value as a float.
    float ClampRmDifficulty(double Value)
    {
        double const Low = (0.0 > Value) ? 0.0 : Value;         // DAT_06fbb228 = 0.0f
        if (!(Low < 100.0))                                     // DAT_06fbb22c = 100.0f
            return 100.0f;
        if (0.0 > Value)
            return 0.0f;
        return static_cast<float>(Value);
    }

    // cos(rand() * 2^-15 (DAT_06fb4448) * DAT_06fb74e8), the double constant being
    // (double)(float)(pi / 2): 1 for rand() == 0, towards 0 for large rand().
    double RandomRmDifficultySpread()
    {
        return cos(static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) *
                   1.5707963705062866);
    }
}

// FUN_06f4ad60 (06f4ad60-06f4b022, cdecl). The result stays on the x87 stack (float10);
// callers store it as a float. 0.0 (DAT_06fbb224) means "no mission in this range" (single
// player only; TU35 FUN_06f041c0 tests it).
float __cdecl PickRmMissionDifficulty(FContentPlayerState const* Player, float MinDifficulty,
                                      float MaxDifficulty)
{
    double const RankStep = static_cast<double>(1.3689f);        // DAT_06fbb230
    if (!pub::SinglePlayer())
    {
        // 06f4ad72-06f4adbd: two times out of three the top grows by RankStep^(2, 4 or 6).
        if (rand() % 3 != 0)
        {
            int const Exponent = (rand() % 3) * 2 + 2;
            MaxDifficulty = static_cast<float>(pow(RankStep, static_cast<double>(Exponent)) *
                                               static_cast<double>(MaxDifficulty));
        }
        MaxDifficulty = ClampRmDifficulty(MaxDifficulty);
        // 06f4ae12-06f4ae41: Max - spread * (Max - Min); Min is not clamped.
        double const Spread = RandomRmDifficultySpread();
        return static_cast<float>(static_cast<double>(MaxDifficulty) -
                                  Spread * (static_cast<double>(MaxDifficulty) -
                                            static_cast<double>(MinDifficulty)));
    }

    // 06f4ae45-06f4aeb8: the difficulty of the player's stage -- the exact record, else the
    // last one below it (signed JGE), else the first; an empty table reads begin() (retail
    // does not test it).
    int const Stage = static_cast<int>(Player->MissionNumber);
    std::vector<FRmRankDifficulty>::const_iterator Previous = GRmRankDifficulties.begin();
    float StageDifficulty = 0.0f;
    bool bFound = false;
    for (std::vector<FRmRankDifficulty>::const_iterator It = GRmRankDifficulties.begin();
         It != GRmRankDifficulties.end(); ++It)
    {
        if (It->Stage == Stage)
        {
            StageDifficulty = It->Difficulty;
            bFound = true;
            break;
        }
        if (It->Stage > Stage)
            break;
        Previous = It;
    }
    if (!bFound)
        StageDifficulty = Previous->Difficulty;

    // 06f4ae79-06f4af4c: a window [stage / step^3, stage * step^3], each end clamped.
    double const Window = pow(RankStep, 3.0);                   // DAT_06fbb234 = 3.0f
    float const WindowLow = ClampRmDifficulty(static_cast<double>(StageDifficulty) / Window);
    float const WindowHigh = ClampRmDifficulty(static_cast<double>(StageDifficulty) * Window);

    // 06f4af4e-06f4af69: the window's top not above Min (TEST AH,0x41 / JP; unordered goes on)
    // -> 0.0.
    if (WindowHigh < MinDifficulty || WindowHigh == MinDifficulty)
        return 0.0f;

    if (WindowHigh >= MaxDifficulty)
    {
        // 06f4af79-06f4afd2 (TEST AH,0x1 / JNZ not taken: ordered and not below Max):
        // [max(Min, Max / step^3), Max].
        float Low = static_cast<float>(static_cast<double>(MaxDifficulty) / Window);
        if (MinDifficulty > Low)
            Low = MinDifficulty;
        double const Spread = RandomRmDifficultySpread();
        return static_cast<float>(static_cast<double>(MaxDifficulty) -
                                  Spread * (static_cast<double>(MaxDifficulty) -
                                            static_cast<double>(Low)));
    }

    // 06f4afd3-06f4b022: [max(Min, window low), window high] (TEST AH,0x5 / JP: only Low < Min
    // takes Min).
    float Low = WindowLow;
    if (Low < MinDifficulty)
        Low = MinDifficulty;
    double const Spread = RandomRmDifficultySpread();
    return static_cast<float>(static_cast<double>(WindowHigh) -
                              Spread * (static_cast<double>(WindowHigh) -
                                        static_cast<double>(Low)));
}

// =======================================================================================
// TU47 -- random mission data base (RandomMission.h)
// =======================================================================================

// FUN_06ef0570 (TU27 COMDAT, callers FUN_06f4d370 and the EH funclets 06fabf7e/06fabfa3):
// ~FmtStr at +0x24, then the two vptrs go back to FContentRefObject (06fb43f8) and
// CreateParms (06fb4338) -- the base dtors of the port. The deleting dtor FUN_06f4d370 and
// the this-4 thunk FUN_06f4ed50 are the compiler's.
FRmMissionDataBase::~FRmMissionDataBase()
{
}

// FUN_06f4d390 (RET 4): eight lines in this order; the floats go out as doubles.
void FRmMissionDataBase::Save(FILE* File)
{
    fprintf(File, "offer_group = %d\n", OfferGroup);
    fprintf(File, "offer_base = %u\n", OfferBase);
    fprintf(File, "offer_ids = %d\n", OfferIds);
    fprintf(File, "reward = %d\n", Reward);
    fprintf(File, "rep_reward = %g\n", static_cast<double>(RepReward));
    fprintf(File, "rep_hit = %g\n", static_cast<double>(RepHit));
    fprintf(File, "difficulty = %g\n", static_cast<double>(Difficulty));
    fprintf(File, "jump_pref = %d\n", JumpPref);
}

// FUN_06f4d420 (RET 4): the first matching key is stored and the function returns.
void FRmMissionDataBase::LoadValue(INI_Reader* Reader)
{
    if (Reader->is_value("offer_group"))
    {
        OfferGroup = Reader->get_value_int(0);
        return;
    }
    if (Reader->is_value("offer_base"))
    {
        OfferBase = static_cast<unsigned int>(Reader->get_value_int(0));
        return;
    }
    if (Reader->is_value("offer_ids"))
    {
        OfferIds = Reader->get_value_int(0);
        return;
    }
    if (Reader->is_value("reward"))
    {
        Reward = Reader->get_value_int(0);
        return;
    }
    if (Reader->is_value("rep_reward"))
    {
        RepReward = Reader->get_value_float(0);
        return;
    }
    if (Reader->is_value("rep_hit"))
    {
        RepHit = Reader->get_value_float(0);
        return;
    }
    if (Reader->is_value("difficulty"))
    {
        Difficulty = Reader->get_value_float(0);
        return;
    }
    if (Reader->is_value("jump_pref"))
        JumpPref = Reader->get_value_int(0);
}

// =======================================================================================
// TU47 -- the random-mission loader (RandomMission.h)
// =======================================================================================

// FUN_06f4d220 (06f4d220-06f4d255): operator new(0xc); on success count 1, mission 0,
// vtable 06fbb35c, *OutLoader = it; on failure *OutLoader = 0. Returns 1 either way.
bool __cdecl CreateRandomMissionLoader(char const* /*MissionType*/,
                                       FRandomMissionLoader** OutLoader)
{
    *OutLoader = new (std::nothrow) FRandomMissionLoader();
    return true;
}

// FUN_06f4d280 (06f4d280-06f4d2d0): a held mission is released through its interface at
// +0x04 and cleared, then the vptr goes back to the ref-counted base (06fb43f8). The
// deleting dtor FUN_06f4d260 is the compiler's.
FRandomMissionLoader::~FRandomMissionLoader()
{
    if (Mission != nullptr)
    {
        Mission->Release();
        Mission = nullptr;
    }
}

// FUN_06ebf6f0: RET 4, no body (ICF-shared empty slot).
void FRandomMissionLoader::Slot05(FRandomMissionData* /*Mission*/)
{
}

// =======================================================================================
// TU47 -- systems loaded for random missions
// =======================================================================================

std::list<unsigned int> GRmDestSystems;   // DAT_06fd0c2c (head) / DAT_06fd0c30 (size)

// FUN_06f4d5e0 -- callers 06ef13ab/06ef15d6/06ef183a/06ef1d1f (TU27), 06f04534 (TU34),
// 06f9f6de (TU63). A system already in the list is left alone; otherwise it is appended
// and FindSystemInfo(&SystemId)->Load(false) (06f4d63c-06f4d648, no null check).
void __cdecl RegisterRmDestSystem(unsigned int SystemId)
{
    for (std::list<unsigned int>::iterator It = GRmDestSystems.begin();
         It != GRmDestSystems.end(); ++It)
    {
        if (*It == SystemId)
            return;
    }
    GRmDestSystems.push_back(SystemId);
    FindSystemInfo(&SystemId)->Load(false);
}

// FUN_06f4d650 -- callers 06eb3098 (BasePop), 06f04744 (TU34). For every listed system in
// order: FindSystemInfo(&id)->Unload() (06f4d664-06f4d66e), then unlink and free the node.
void __cdecl ReleaseRmDestSystems()
{
    std::list<unsigned int>::iterator It = GRmDestSystems.begin();
    while (It != GRmDestSystems.end())
    {
        FindSystemInfo(&*It)->Unload();
        It = GRmDestSystems.erase(It);
    }
}

// =======================================================================================
// TU49 -- random-mission bonus loot
// =======================================================================================

// DAT_06fd0d2c (static init LAB_06f56040, atexit FUN_06f56070).
std::map<unsigned int, std::vector<FRmBonusLootEntry>> GRmBonusLoot;

// DAT_06fd0d84.
float GRmBonusLootDropChance = 0.0f;

namespace
{
    // .rdata DAT_06fbb9b8 -- the faction key used for "all".
    unsigned int const GRmBonusLootAllFactions = 0xffffffffu;

    // FUN_06f564d0 candidate: {record pointer, weight copied from record+0x14}.
    struct FRmBonusLootCandidate
    {
        FRmBonusLootEntry const* Record;
        float Weight;
    };

    // FUN_06f564d0 inner loop over one faction list: DifficultyMin <= d and d <= Max
    // (FCOMP / TEST AH,0x41 / JP skips when greater or unordered), appended at end.
    void CollectBonusLootCandidates(std::map<unsigned int, std::vector<FRmBonusLootEntry>>::iterator It,
                                    float Difficulty,
                                    std::vector<FRmBonusLootCandidate>* Candidates)
    {
        if (It == GRmBonusLoot.end())
            return;
        std::vector<FRmBonusLootEntry> const& Records = It->second;
        for (std::vector<FRmBonusLootEntry>::const_iterator Record = Records.begin();
             Record != Records.end(); ++Record)
        {
            if ((Record->DifficultyMin < Difficulty || Record->DifficultyMin == Difficulty) &&
                (Difficulty < Record->DifficultyMax || Difficulty == Record->DifficultyMax))
            {
                FRmBonusLootCandidate Candidate;
                Candidate.Record = &*Record;
                Candidate.Weight = Record->Weight;
                Candidates->push_back(Candidate);
            }
        }
    }
}

// FUN_06f56150: FLD DAT_06fd0d84.
float __cdecl GetRmBonusLootDropChance()
{
    return GRmBonusLootDropChance;
}

// FUN_06f56160 (06f56160-06f564c9): values of one [RMBonusLoot] section (the header is
// already read by the caller).
void __cdecl LoadRmBonusLootSection(INI_Reader* Reader)
{
    FRmBonusLootEntry Record;
    Record.Archetype = 0;
    Record.NumToDropMin = 0;
    Record.NumToDropMax = 0;
    Record.DifficultyMin = 0.0f;
    Record.DifficultyMax = 0.0f;
    Record.Weight = 0.0f;
    std::vector<unsigned int> Factions;
    // uStack_70: written only by GetReputationGroup, lives across the section.
    unsigned int GroupId = 0;

    while (Reader->read_value())
    {
        if (Reader->is_value("archetype"))
        {
            pub::GetEquipmentID(Record.Archetype, Reader->get_value_string(0));
        }
        else if (Reader->is_value("num_to_drop"))
        {
            Record.NumToDropMin = Reader->get_value_int(0);
            if (Reader->is_value_empty(1))
                Record.NumToDropMax = Record.NumToDropMin;
            else
                Record.NumToDropMax = Reader->get_value_int(1);
        }
        else if (Reader->is_value("faction"))
        {
            if (_stricmp(Reader->get_value_string(0), "all") == 0)
            {
                AppendUnique(&Factions, GRmBonusLootAllFactions);
            }
            else
            {
                unsigned int ValueIndex = 0;
                while (!Reader->is_value_empty(ValueIndex))
                {
                    pub::Reputation::GetReputationGroup(GroupId,
                                                        Reader->get_value_string(ValueIndex));
                    AppendUnique(&Factions, GroupId);
                    ++ValueIndex;
                }
            }
        }
        else if (Reader->is_value("difficulty_range"))
        {
            Record.DifficultyMin = Reader->get_value_float(0);
            Record.DifficultyMax = Reader->get_value_float(1);
        }
        else if (Reader->is_value("weight"))
        {
            Record.Weight = static_cast<float>(Reader->get_value_int(0));
        }
    }

    // 06f56400-06f564a9: FUN_06f56780 inserts {faction, empty vector} (or finds the
    // existing entry), FUN_06f56900 appends the record at the end of its list.
    for (std::vector<unsigned int>::const_iterator It = Factions.begin(); It != Factions.end();
         ++It)
    {
        std::pair<std::map<unsigned int, std::vector<FRmBonusLootEntry>>::iterator, bool> Inserted =
            GRmBonusLoot.insert(std::map<unsigned int, std::vector<FRmBonusLootEntry>>::value_type(
                *It, std::vector<FRmBonusLootEntry>()));
        Inserted.first->second.push_back(Record);
    }
}

// FUN_06f566a0 (06f566a0-06f5677b). [Global] BonusLootDropChance / [RMBonusLoot].
void __cdecl LoadRmLootInfo(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (Reader->is_header("Global"))
        {
            while (Reader->read_value())
            {
                if (!Reader->is_value("BonusLootDropChance"))
                    continue;

                // 06f566f5-06f56740: stored first, then v <= 0 (or unordered) -> 0.0,
                // v >= 1 -> 1.0 (TEST AH,0x41 / JNZ, TEST AH,0x5 / JP, TEST AH,0x41 / JZ).
                GRmBonusLootDropChance = Reader->get_value_float(0);
                if (!(GRmBonusLootDropChance > 0.0f))
                    GRmBonusLootDropChance = 0.0f;
                else if (!(GRmBonusLootDropChance < 1.0f))
                    GRmBonusLootDropChance = 1.0f;
            }
        }
        else if (Reader->is_header("RMBonusLoot"))
        {
            LoadRmBonusLootSection(Reader);
        }
    }
}

// FUN_06f564d0 (06f564d0-06f56690): weighted pick among the "all" records and then the
// faction's records whose difficulty range contains Difficulty.
FRmBonusLootEntry const* __cdecl PickRmBonusLoot(unsigned int Faction, float Difficulty)
{
    FRmBonusLootEntry const* Picked = nullptr;
    std::vector<FRmBonusLootCandidate> Candidates;

    std::map<unsigned int, std::vector<FRmBonusLootEntry>>::iterator AllIt =
        GRmBonusLoot.find(GRmBonusLootAllFactions);
    std::map<unsigned int, std::vector<FRmBonusLootEntry>>::iterator FactionIt =
        GRmBonusLoot.find(Faction);
    CollectBonusLootCandidates(AllIt, Difficulty, &Candidates);
    CollectBonusLootCandidates(FactionIt, Difficulty, &Candidates);

    if (!Candidates.empty())
    {
        // x87: the sum stays on the FPU stack, FST to float for the target.
        double Sum = 0.0;
        for (std::vector<FRmBonusLootCandidate>::const_iterator It = Candidates.begin();
             It != Candidates.end(); ++It)
        {
            Sum = Sum + It->Weight;
        }
        float const Total = static_cast<float>(Sum);

        // FCOMP sum,0.0 / TEST AH,0x44 / JNP: a zero total skips the draw. Retail then
        // dereferences the end iterator (reads past the candidate buffer); the port has no
        // defined value to read there and returns null for that case.
        if (Sum != 0.0)
        {
            float const Target = static_cast<float>(
                static_cast<double>(std::rand()) * 3.0517578125e-05 * Total);
            double Accumulated = 0.0;
            // Loop end without a hit also dereferences end in retail; unreachable because
            // Target < Total.
            for (std::vector<FRmBonusLootCandidate>::const_iterator It = Candidates.begin();
                 It != Candidates.end(); ++It)
            {
                Accumulated = Accumulated + It->Weight;
                // TEST AH,0x41 / JNP: stop at the first Target <= Accumulated.
                if (Target < Accumulated || Target == Accumulated)
                {
                    Picked = It->Record;
                    break;
                }
            }
        }
    }
    return Picked;
}

// =======================================================================================
// TU51 -- ship classes, ship mission properties, solar formations
// =======================================================================================

// .data PTR_s_big_solar_06fce748 (4 entries): read by TU51 FUN_06f58f30 (through
// FUN_06f59550) and inline by TU62 FUN_06f95040.
FRmNamedValue const GRmSolarTypes[4] = {
    { "big_solar", 0 },
    { "defensive_solar", 1 },
    { "killable_neutral", 2 },
    { "background_solar", 3 },
};

namespace
{
    // FUN_06f59550 (thiscall on a {char const*, int} table): stricmp over Count entries,
    // null names skipped; on a hit writes the value and returns true, else leaves *OutValue.
    bool LookupNamedValue(FRmNamedValue const* Table, char const* Name, int* OutValue,
                          unsigned int Count)
    {
        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            if (Table[Index].Name != nullptr && _stricmp(Name, Table[Index].Name) == 0)
            {
                *OutValue = Table[Index].Value;
                return true;
            }
        }
        return false;
    }

    // .data PTR_s_big_solar_formation_06fce728 (4 entries).
    FRmNamedValue const GRmSolarFormationTypes[4] = {
        { "big_solar_formation", 0 },
        { "battle_background_formation", 1 },
        { "defensive_outpost_formation", 2 },
        { "loot_group_formation", 3 },
    };

    // DAT_06fd0e30 -- ships\shiparch.ini is read once.
    bool GbRmShipMissionPropertiesLoaded = false;

    // DAT_06fd0e34 (static init LAB_06f58e10, atexit FUN_06f58e40).
    std::map<unsigned int, FRmSolarFormation> GRmSolarFormations;

    // DAT_06fd0e48 -- this TU's cached 3DMathEngine (FUN_06fa8fa0 on first use).
    I3DMathEngine* GRmSolarFormationMath = nullptr;
}

// DAT_06fd0ddc (static init LAB_06f57eb0, atexit FUN_06f57ee0).
std::map<unsigned int, std::vector<unsigned int>> GRmShipClasses;

// DAT_06fd0dc8 (static init LAB_06f583c0, atexit FUN_06f583f0).
std::map<unsigned int, std::vector<unsigned int>> GRmShipMissionProperties;

// FUN_06f58190 (06f58190-06f583bd). [ShipClass] nickname / member.
void __cdecl LoadShipClasses(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("ShipClass"))
            continue;

        std::vector<unsigned int> Members;
        ID_String ClassId;
        ClassId.clear();

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                ClassId.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("member"))
            {
                unsigned int MemberId = CreateID(Reader->get_value_string());
                std::map<unsigned int, std::vector<unsigned int>>::iterator Found =
                    GRmShipClasses.find(MemberId);
                if (Found != GRmShipClasses.end())
                {
                    // 06f58284-06f582a6: a member naming an earlier class expands to its
                    // members, each appended once (FUN_06f95e00).
                    std::vector<unsigned int> const& Nested = Found->second;
                    for (std::vector<unsigned int>::const_iterator It = Nested.begin();
                         It != Nested.end(); ++It)
                    {
                        AppendUnique(&Members, *It);
                    }
                }
                else
                {
                    // 06f582aa-06f5831e: second get_value_string + CreateID, appended once.
                    AppendUnique(&Members, CreateID(Reader->get_value_string()));
                }
            }
        }

        // 06f58335-06f5835f: FUN_06f58870 is map insert-unique; an existing class keeps its
        // first definition.
        GRmShipClasses.insert(
            std::map<unsigned int, std::vector<unsigned int>>::value_type(ClassId.Id, Members));
    }
}

// FUN_06f57fc0: erase every node of DAT_06fd0ddc (called from FUN_06efe870).
void __cdecl ClearShipClasses()
{
    GRmShipClasses.clear();
}

// FUN_06f58080: FUN_06fa7200 lower_bound + exact key; hit -> node value, miss -> null.
std::vector<unsigned int>* __cdecl FindShipClass(unsigned int const* ShipClassId)
{
    std::map<unsigned int, std::vector<unsigned int>>::iterator It =
        GRmShipClasses.find(*ShipClassId);
    if (It == GRmShipClasses.end())
        return nullptr;
    return &It->second;
}

// FUN_06f580d0: in-order walk; the first class whose member list equals Members element by
// element (same size, same order) writes its key and returns true.
bool __cdecl FindShipClassByMembers(unsigned int* OutShipClassId,
                                    std::vector<unsigned int> const& Members)
{
    for (std::map<unsigned int, std::vector<unsigned int>>::const_iterator It =
             GRmShipClasses.begin();
         It != GRmShipClasses.end(); ++It)
    {
        if (It->second == Members)
        {
            *OutShipClassId = It->first;
            return true;
        }
    }
    return false;
}

// FUN_06f584d0 (06f584d0-06f587c9). [Ship] nickname / mission_property; runs once.
void __cdecl LoadShipMissionProperties(INI_Reader* Reader)
{
    if (GbRmShipMissionPropertiesLoaded)
        return;

    while (Reader->read_header())
    {
        if (!Reader->is_header("Ship"))
            continue;

        std::vector<unsigned int> Properties;
        unsigned int ShipArchId = 0;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                pub::GetShipID(ShipArchId, Reader->get_value_string());
            }
            else if (Reader->is_value("mission_property"))
            {
                // Retail keys: "can_use_berths", "can_use_med_moors" (stored as the
                // can_use_medium_moors id), "can_use_large_moors"; anything else ignored.
                if (_stricmp(Reader->get_value_string(), "can_use_berths") == 0)
                    AppendUnique(&Properties, GIdCanUseBerths);
                else if (_stricmp(Reader->get_value_string(), "can_use_med_moors") == 0)
                    AppendUnique(&Properties, GIdCanUseMediumMoors);
                else if (_stricmp(Reader->get_value_string(), "can_use_large_moors") == 0)
                    AppendUnique(&Properties, GIdCanUseLargeMoors);
            }
        }

        GRmShipMissionProperties.insert(
            std::map<unsigned int, std::vector<unsigned int>>::value_type(ShipArchId, Properties));
    }

    // 06f587b3: set after the header loop, also when the file had no header at all.
    GbRmShipMissionPropertiesLoaded = true;
}

// FUN_06f587d0: unknown ship -> false; else append each property once and return true.
bool __cdecl GetShipMissionProperties(unsigned int ShipArchId,
                                      std::vector<unsigned int>* InOutProperties)
{
    std::map<unsigned int, std::vector<unsigned int>>::iterator Found =
        GRmShipMissionProperties.find(ShipArchId);
    if (Found == GRmShipMissionProperties.end())
        return false;

    std::vector<unsigned int> const& Properties = Found->second;
    for (std::vector<unsigned int>::const_iterator It = Properties.begin();
         It != Properties.end(); ++It)
    {
        AppendUnique(InOutProperties, *It);
    }
    return true;
}

// FUN_06f58f20.
std::map<unsigned int, FRmSolarFormation>* __cdecl GetSolarFormations()
{
    return &GRmSolarFormations;
}

// FUN_06f58f30 (06f58f30-06f592bd). [SolarFormation] nickname / type / pos.
void __cdecl LoadSolarFormations(INI_Reader* Reader)
{
    // [ESP+0x13] and [ESP+0x3c] live for the whole call: a "pos" line whose first value is
    // neither "mandatory" nor "optional", or whose solar type is unknown, reuses the value
    // of the previous "pos" line. Retail starts both uninitialized; the port starts at 0.
    unsigned char bMandatory = 0;
    int SolarType = 0;

    while (Reader->read_header())
    {
        if (!Reader->is_header("SolarFormation"))
            continue;

        // 06f58f85-06f58faf: the section record [ESP+0x24] {ID_String::clear'ed nickname, type -1,
        // empty slot vector} is the one local with a dtor (unwind state 0, FUN_06eb7d10); the
        // slots are appended to it directly. The nickname is kept in an ID_String (no dtor) and
        // copied into the record before the insert.
        ID_String Nickname;
        Nickname.clear();
        FRmSolarFormation Formation;
        Formation.Nickname = 0;
        Formation.Type = -1;
        std::vector<FRmSolarFormationSlot>& Slots = Formation.Slots;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Nickname.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("type"))
            {
                int Type = 0;
                if (LookupNamedValue(GRmSolarFormationTypes, Reader->get_value_string(), &Type, 4))
                    Formation.Type = Type;
            }
            else if (Reader->is_value("pos"))
            {
                FRmSolarFormationSlot Slot;
                // FUN_06efc7a0: identity.
                Slot.Orientation.D[0][0] = 1.0f;
                Slot.Orientation.D[0][1] = 0.0f;
                Slot.Orientation.D[0][2] = 0.0f;
                Slot.Orientation.D[1][0] = 0.0f;
                Slot.Orientation.D[1][1] = 1.0f;
                Slot.Orientation.D[1][2] = 0.0f;
                Slot.Orientation.D[2][0] = 0.0f;
                Slot.Orientation.D[2][1] = 0.0f;
                Slot.Orientation.D[2][2] = 1.0f;
                Slot.Position.X = 0.0f;
                Slot.Position.Y = 0.0f;
                Slot.Position.Z = 0.0f;

                if (_stricmp(Reader->get_value_string(0), "mandatory") == 0)
                    bMandatory = 1;
                else if (_stricmp(Reader->get_value_string(0), "optional") == 0)
                    bMandatory = 0;

                LookupNamedValue(GRmSolarTypes, Reader->get_value_string(1), &SolarType, 4);

                float const X = Reader->get_value_float(2);
                float const Y = Reader->get_value_float(3);
                float const Z = Reader->get_value_float(4);
                Slot.Position.X = X;
                Slot.Position.Y = Y;
                Slot.Position.Z = Z;

                if (!Reader->is_value_empty(5))
                {
                    // 06f59115-06f591cf: quaternion (W, X, Y, Z) = values 5..8, normalized
                    // on x87 (sum of squares v8,v7,v6,v5 and 1/sqrt kept unrounded; each
                    // product rounded to float), then 3DMathEngine QuaternionToMatrix.
                    FQuaternion Quat;
                    Quat.W = Reader->get_value_float(5);
                    Quat.X = Reader->get_value_float(6);
                    Quat.Y = Reader->get_value_float(7);
                    float const LastZ = Reader->get_value_float(8);
                    double const Sum = static_cast<double>(LastZ) * LastZ +
                                       static_cast<double>(Quat.Y) * Quat.Y +
                                       static_cast<double>(Quat.X) * Quat.X +
                                       static_cast<double>(Quat.W) * Quat.W;
                    double const InvLength = 1.0 / std::sqrt(Sum);
                    Quat.W = static_cast<float>(Quat.W * InvLength);
                    Quat.X = static_cast<float>(Quat.X * InvLength);
                    Quat.Y = static_cast<float>(Quat.Y * InvLength);
                    Quat.Z = static_cast<float>(InvLength * LastZ);

                    if (GRmSolarFormationMath == nullptr)
                        GRmSolarFormationMath = ContentAcquire3DMathEngine();
                    FMatrix Rotation;
                    GRmSolarFormationMath->QuaternionToMatrix(Rotation, Quat);
                    Slot.Orientation = Rotation;
                }

                Slot.bMandatory = bMandatory;
                Slot.SolarType = SolarType;
                // FUN_06f59830(end, 1, &Slot) -- insert at end.
                Slots.push_back(Slot);
            }
        }

        Formation.Nickname = Nickname.Id;
        // 06f59222-06f5925e: the pair {key, record copy} (unwind state 1, FUN_06f592d0) goes to
        // FUN_06f59620, map insert-unique keyed by the nickname; the pair dies first (06f59268),
        // then the record (06f59289).
        GRmSolarFormations.insert(
            std::map<unsigned int, FRmSolarFormation>::value_type(Nickname.Id, Formation));
    }
}

// FUN_06f592f0: every optional slot (byte 0) contributes its solar type once.
void FRmSolarFormation::CollectOptionalSlotTypes(std::vector<int>* InOutTypes) const
{
    int Count = static_cast<int>(Slots.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Slots[Index].bMandatory == 0)
            AppendUniqueInt(InOutTypes, Slots[Index].SolarType);
    }
}

// FUN_06f593a0: every mandatory slot (byte != 0) contributes its solar type once.
void FRmSolarFormation::CollectMandatorySlotTypes(std::vector<int>* InOutTypes) const
{
    int Count = static_cast<int>(Slots.size());
    for (int Index = 0; Index < Count; ++Index)
    {
        if (Slots[Index].bMandatory != 0)
            AppendUniqueInt(InOutTypes, Slots[Index].SolarType);
    }
}

// FUN_06f59450: (end - begin) / 0x38, 0 for an unallocated vector.
int FRmSolarFormation::GetSlotCount() const
{
    return static_cast<int>(Slots.size());
}

// FUN_06f59480: slot pointer when Index < count (unsigned compare), else null.
FRmSolarFormationSlot* FRmSolarFormation::GetSlot(unsigned int Index)
{
    if (Index < static_cast<unsigned int>(Slots.size()))
        return &Slots[Index];
    return nullptr;
}

// ---------------------------------------------------------------------------------------
// TU47 -- the random mission object base (FRandomMissionObject, RandomMissionObject.h).
// ---------------------------------------------------------------------------------------

namespace
{
    // DAT_06fd0c8c: live random mission objects (ctor ++ at 06f4b218, dtor -- at 06f4b49c).
    // Retail never reads it.
    int GRmMissionObjectCount = 0;

    // .rdata DAT_06fb5810 (1.0f): a position counts as set when one axis squared exceeds it.
    float const GRmPositionAxisSq = 1.0f;
    // .rdata DAT_06fb8570 (1.0e6f): squared reach of the first path objective (1000 units).
    double const GRmPathReachSq = 1000000.0;
    // .rdata DAT_06fbb23c (0.25f): +0x14 of the victory / failure music.
    float const GRmMusicFade = 0.25f;

    // The three FLD/FMUL/FCOMP 1.0f tests of FUN_06f4b870 (06f4b8d7-06f4b91a) and
    // FUN_06f4b9e0 (06f4ba38-06f4ba7b): TEST AH,0x41 + JP takes the greater (or unordered)
    // side, so any axis whose square is not <= 1.0f makes the position usable.
    bool RmHasPosition(Vector const& Position)
    {
        return !(Position.x * Position.x <= GRmPositionAxisSq) ||
               !(Position.y * Position.y <= GRmPositionAxisSq) ||
               !(Position.z * Position.z <= GRmPositionAxisSq);
    }

    // FUN_06f4d200 (cdecl): (rand() * Range) >> 15, the signed division idiom (CDQ, AND
    // EDX,0x7fff, ADD, SAR 15).
    int RmRandomBelow(int Range)
    {
        int const Product = rand() * Range;
        return (Product + ((Product >> 31) & 0x7fff)) >> 15;
    }

    // The music local of FUN_06f4c6f0 / FUN_06f4c9c0 (06f4c6f9-06f4c744): +0..+8 zero,
    // +0xc the nickname id, +0x10 byte 1, +0x14 DAT_06fbb23c.
    pub::Audio::Tryptich RmMissionMusic(char const* Nickname)
    {
        pub::Audio::Tryptich Music;
        Music.Field0 = 0;
        Music.Field4 = 0;
        Music.Field8 = 0;
        Music.FieldC = pub::GetNicknameId(Nickname);
        Music.Field10 = 1;
        std::memcpy(&Music.Field14, &GRmMusicFade, sizeof(float));
        return Music;
    }
}

// FUN_06f4b0d0 (06f4b0d0-06f4b2fc, RET 8). The TU38 base (FUN_06f0e3e0), the group
// (StateMachineGroup ctor with DAT_06fcf254), the +0x1e4 multimap, the create parms
// (AddRef when non-null), the vector / map / names and the zeroed scalars; then the vtables,
// +0x68 = the mission description, heartbeat 1.0, the player (slot 18 body called directly)
// and the live count. Every player of the group gets a record (the TU38 slot 2 body
// FUN_06f1b6d0, called directly) whose slot 2 flag is cleared and a placeholder objective
// (FUN_06f1cee0); then two mission messages to the offering player: an empty FmtStr as type
// 0xa and the description as type 8, both broadcast.
FRandomMissionObject::FRandomMissionObject(unsigned int const* InControllerId,
                                           FRandomMissionData* InData)
    : FMissionObject(InControllerId, InData)
    , StateMachineGroup(GMessageRouter)
    , Data(InData)
{
    Field_0x68 = Data->Description;
    HeartbeatInterval = 1.0f;
    pub::Controller::SetHeartbeatInterval(ControllerId, 1.0f);
    FRandomMissionObject::SetPlayer(Data->PlayerId);
    ++GRmMissionObjectCount;
    for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
    {
        FMissionPlayer* Record = FMissionObject::GetOrCreatePlayer(PlayerStates[Index]->PlayerId);
        if (Record->GetField25())
            Record->SetField25(false);
        Record->SendPlaceholderObjective();
    }
    {
        // 06f4b279-06f4b2d4: the empty FmtStr (unwind state 7) is destroyed right after the
        // first message, before the second one (06f4b2e3).
        FmtStr Empty;
        pub::Player::DisplayMissionMessage(Data->PlayerId, Empty,
                                           static_cast<MissionMessageType>(0xa), true);
    }
    pub::Player::DisplayMissionMessage(Data->PlayerId, Field_0x68, static_cast<MissionMessageType>(8),
                                       true);
}

// FUN_06f4b460 (06f4b460-06f4b649). Retail tears down +0x224, +0x20c and +0x1fc first, then
// releases the parms (+0x1f8 holder, FUN_06fa0330 inlined), then +0x1e4, the group and the
// TU38 base (FUN_06f0ed00). The port does the count in the body; the members follow in
// reverse declaration order (the holder after +0x1fc, before +0x1e4), then the bases.
FRandomMissionObject::~FRandomMissionObject()
{
    --GRmMissionObjectCount;
}

// LAB_06f4b650 (06f4b650-06f4b674, RET 4). No null test on the record.
FMissionPlayer* FRandomMissionObject::GetOrCreatePlayer(unsigned int InPlayerId)
{
    FMissionPlayer* Record = FMissionObject::GetOrCreatePlayer(InPlayerId);
    if (Record->GetField25())
        Record->SetField25(false);
    return Record;
}

// FUN_06f4c3b0 (06f4c3b0-06f4c602, RET 4): the map (FUN_06f4d710 over begin..end) and the
// vector (FUN_06f6a640) are emptied, the TU38 slot 18 body FUN_06f1ca50 runs; player 0 clears
// +0xc and +8 and stops. Otherwise every member of the new player's group (FUN_06f481c0 on
// +8, no null test) is mapped to its state (FUN_06f003e0 insert), appended to the vector
// (FUN_06f937c0) and its name counted (AddPlayerName; GetName on the state, no null test).
void FRandomMissionObject::SetPlayer(unsigned int InPlayerId)
{
    PlayerStateMap.clear();
    PlayerStates.clear();
    FMissionObject::SetPlayer(InPlayerId);
    if (InPlayerId == 0)
    {
        PlayerId = 0;
        PlayerState = nullptr;
        return;
    }
    std::vector<unsigned int> Members;
    PlayerState->GetGroupMembers(&Members);
    for (int Index = 0; Index < static_cast<int>(Members.size()); ++Index)
    {
        FContentPlayerState* State = FindContentPlayerState(Members[Index]);
        PlayerStateMap.insert(
            std::map<unsigned int, FContentPlayerState*>::value_type(Members[Index], State));
        PlayerStates.push_back(State);
        // 06f4c4c8 / 06f4c5be: the name local (unwind state 1) lives for this pass only.
        std::wstring Name;
        State->GetName(&Name);
        AddPlayerName(Name);
    }
}

// 06f4c4c8-06f4c5b3 / 06f4cb76-06f4cc30: the name (FUN_06f480e0) is looked up in +0x224
// (std::operator== per element); when absent, +0x220 grows if fewer than 8 names are listed
// (an unallocated vector counts as fewer), and the name is inserted at the end unless a second
// lookup (FUN_06f4daa0) finds it (FUN_06f4e220).
void FRandomMissionObject::AddPlayerName(std::wstring const& Name)
{
    if (std::find(PlayerNames.begin(), PlayerNames.end(), Name) != PlayerNames.end())
        return;
    if (PlayerNames.size() < 8)
        ++PlayerCount;
    if (std::find(PlayerNames.begin(), PlayerNames.end(), Name) == PlayerNames.end())
        PlayerNames.insert(PlayerNames.end(), Name);
}

// LAB_06f4b680 (06f4b680-06f4b6b6, RET 8): a stack SMControllerEvent {4, MessageId, Parms}
// goes to the group's slot 1 (StateMachineGroup::process). The TU38 body is reached from Slot2.
void FRandomMissionObject::HandleMissionMessage(pub::Controller::EMessage MessageId,
                                                void const* MessageParms)
{
    // SMControllerEvent (common_net.h, Common.dll's export) keeps the message id as a plain int
    // in its +0x08 field (named ControllerId there); the readers in Slot2 cast it back.
    SMControllerEvent Event(static_cast<int>(MessageId), MessageParms);
    process(&Event);
}

// FUN_06f4c9c0 (06f4c9c0-06f4ca91): "music_victory" to every player with a ship after the
// completed count (FUN_06f49090 with +0x80), slot 44 with the offer group's event rate 1,
// then the TU38 body FUN_06f0fda0. No flag reset here (unlike slot 35).
void FRandomMissionObject::OnMissionSucceed()
{
    pub::Audio::Tryptich const Music = RmMissionMusic("music_victory");
    for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
    {
        FContentPlayerState* State = PlayerStates[Index];
        State->CountRandomMissionCompleted(Field_0x80);
        if (State->ShipObject != nullptr)
            pub::Audio::SetMusic(State->PlayerId, Music);
    }
    unsigned int const Group = static_cast<unsigned int>(Data->OfferGroup);
    float const Rate = GetFactionEventRate(Group, 1);
    ChangeGroupReputationAlways(Group, Rate);
    FMissionObject::OnMissionSucceed();
}

// LAB_06f4b6c0.
std::vector<FContentPlayerState*>* FRandomMissionObject::GetPlayerStates()
{
    return &PlayerStates;
}

// LAB_06f4b6d0.
std::map<unsigned int, FContentPlayerState*>* FRandomMissionObject::GetPlayerStateMap()
{
    return &PlayerStateMap;
}

// LAB_06f4b7b0 (06f4b7b0-06f4b86a, RET 8). A single listed name leaves *bChanged as it is.
// Otherwise the first type 8 objective of the player's list (slot 4 per node) is brought to
// +0x220 - 1 (only while +0x220 > 0; Type |= 0x50, slot 3, *bChanged = 1); without one a new
// type 8 (FUN_06f1cc40, no null test) gets Type 0x58, shown, ids 0x331a, the same value and
// slot 3, and *bChanged = 1.
bool FRandomMissionObject::UpdatePlayerCountObjective(FMissionPlayer* Player, bool* bChanged)
{
    if (PlayerNames.size() == 1)
        return *bChanged;
    std::list<FMissionObjective*>& List = Player->Objectives.Objectives;
    std::list<FMissionObjective*>::iterator It = List.begin();
    for (; It != List.end(); ++It)
    {
        if ((*It)->GetType() == 8)
            break;
    }
    if (It != List.end())
    {
        FNNObjectiveType8* Existing = static_cast<FNNObjectiveType8*>(*It);
        if (PlayerCount > 0 && Existing->Value != static_cast<unsigned int>(PlayerCount - 1))
        {
            Existing->Value = static_cast<unsigned int>(PlayerCount - 1);
            Existing->Objective.Type |= 0x50;
            Existing->BuildDescription();
            *bChanged = true;
        }
        return *bChanged;
    }
    FNNObjectiveType8* Created = static_cast<FNNObjectiveType8*>(Player->Objectives.Add(8, this));
    Created->Objective.Type = 0x58;
    Created->bHidden = 0;
    Created->Ids = 0x331a;
    Created->Value = static_cast<unsigned int>(PlayerCount - 1);
    Created->BuildDescription();
    *bChanged = true;
    return *bChanged;
}

// LAB_06f4bd00: DEC, store, return the new value.
int FRandomMissionObject::DecrementPlayerCount()
{
    --PlayerCount;
    return PlayerCount;
}

// LAB_06f4bd10: INC, store, return the new value.
int FRandomMissionObject::IncrementPlayerCount()
{
    ++PlayerCount;
    return PlayerCount;
}

// LAB_06f4bd20.
int FRandomMissionObject::GetPlayerCount()
{
    return PlayerCount;
}

// FUN_06f4c6f0 (06f4c6f0-06f4c82b, RET 4). Every player: flags 0, the aborted (Reason 1) or
// failed count with +0x80, "music_failure" when he has a ship. Then slot 43 with the offer
// group's event rate 3 (aborted) or 2 and empathy for any reason but 1, the objectives cleared
// (FUN_06f1b3d0), slot 22, and when the mission's player is still in this mission
// (FUN_06f487c0 == +4) FUN_06f48a20(+4, 3, group).
void FRandomMissionObject::FailMission(int Reason)
{
    pub::Audio::Tryptich const Music = RmMissionMusic("music_failure");
    for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
    {
        FContentPlayerState* State = PlayerStates[Index];
        State->SetFlags(0);
        if (Reason == 1)
            State->CountRandomMissionAborted(Field_0x80);
        else
            State->CountRandomMissionFailed(Field_0x80);
        if (State->ShipObject != nullptr)
            pub::Audio::SetMusic(State->PlayerId, Music);
    }
    unsigned int const Group = static_cast<unsigned int>(Data->OfferGroup);
    float const Rate = GetFactionEventRate(Group, Reason == 1 ? 3 : 2);
    ChangeGroupReputation(Group, Rate, Reason != 1);
    ClearObjectives();
    EndMission();
    FContentPlayerState* Owner = FindContentPlayerState(PlayerId);
    if (Owner != nullptr && Owner->GetLiveMission() == ControllerId)
        Owner->EndLiveMission(ControllerId, 3, true);
}

// FUN_06f4c830 (06f4c830-06f4c9ba, RET 8). Slot 41 (not forced, empathy) with the offer
// group's event rate 3 (Reason 1) or 2, slot 5, the player erased from the map (retail
// FUN_06f20e00 erases the find result without an end test; the port erases by key), the
// vector rebuilt from the map values in key order, the TU38 slot 18 body FUN_06f1ca50 with the
// first remaining player or 0, the leaving player's flags 0 when he has a state. Reason 1:
// aborted count and FUN_06f48a20(+4, 4, 0); 0xe: nothing more; else failed count and
// FUN_06f48a20(+4, 3, 0). Retail does not test the state before those calls.
void FRandomMissionObject::RemoveMissionPlayer(unsigned int InPlayerId, int Reason)
{
    unsigned int const Group = static_cast<unsigned int>(Data->OfferGroup);
    float const Rate = GetFactionEventRate(Group, Reason == 1 ? 3 : 2);
    ChangePlayerReputation(InPlayerId, Group, Rate, false, true);
    RemovePlayerById(InPlayerId);
    PlayerStateMap.erase(InPlayerId);
    PlayerStates.clear();
    for (std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerStateMap.begin();
         It != PlayerStateMap.end(); ++It)
        PlayerStates.push_back(It->second);
    if (!PlayerStateMap.empty())
        FMissionObject::SetPlayer(PlayerStateMap.begin()->first);
    else
        FMissionObject::SetPlayer(0);
    FContentPlayerState* State = FindContentPlayerState(InPlayerId);
    if (State != nullptr)
        State->SetFlags(0);
    if (Reason == 1)
    {
        State->CountRandomMissionAborted(Field_0x80);
        State->EndLiveMission(ControllerId, 4, false);
        return;
    }
    if (Reason == 0xe)
        return;
    State->CountRandomMissionFailed(Field_0x80);
    State->EndLiveMission(ControllerId, 3, false);
}

// FUN_06f4b6e0 (06f4b6e0-06f4b713): slot 38 per state of +0x1fc.
void FRandomMissionObject::RefreshAllObjectives()
{
    for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
        RefreshObjectives(PlayerStates[Index]->PlayerId);
}

// LAB_06f4b720 (06f4b720-06f4b79f, RET 4): slot 2; no record -> nothing. Slot 40, slot 39 and,
// in multiplayer only, slot 31 each report a change; any change resends (FUN_06f1d110).
void FRandomMissionObject::RefreshObjectives(unsigned int InPlayerId)
{
    FMissionPlayer* Record = GetOrCreatePlayer(InPlayerId);
    if (Record == nullptr)
        return;
    bool bPathChanged = false;
    bool bDockedChanged = false;
    bool bCountChanged = false;
    UpdatePathObjectives(Record, &bPathChanged);
    UpdateDockedObjective(Record, &bDockedChanged);
    if (!pub::SinglePlayer())
        UpdatePlayerCountObjective(Record, &bCountChanged);
    if (bDockedChanged || bPathChanged || bCountChanged)
        Record->SendObjectives();
}

// LAB_06f4b870 (06f4b870-06f4b9dc, RET 8). *bChanged = 0. Without a ship (player state +0x10):
// a first objective with ids 0x3319 is kept; else the player's position (FUN_06f47af0) must be
// set and a new type 2 objective (FUN_06f19cd0, no null test) -- shown, ids 0x3319, Type 0x10,
// the player's system (FUN_06f478b0), 0, the position -- goes to the front of the list and
// *bChanged = 1. With a ship: a first objective with ids 0x3319 is popped (FUN_06f1ccb0),
// *bChanged = 1. A missing player state returns 0.
bool FRandomMissionObject::UpdateDockedObjective(FMissionPlayer* Player, bool* bChanged)
{
    *bChanged = false;
    FMissionObjective* Front = Player->Objectives.GetFront();
    if (Player->PlayerState->ShipObject == nullptr)
    {
        if (Front != nullptr && Front->Ids == 0x3319)
            return *bChanged;
        FContentPlayerState* State = FindContentPlayerState(Player->PlayerId);
        if (State == nullptr)
            return false;
        Vector Position;
        State->GetPosition(&Position);
        if (!RmHasPosition(Position))
            return false;
        FMissionObjective* Created = nullptr;
        CreateMissionObjective(2, this, &Created);
        Created->bHidden = 0;
        Created->Ids = 0x3319;
        Created->Objective.Type = 0x10;
        FNNObjectiveNavMarker* Marker = static_cast<FNNObjectiveNavMarker*>(Created);
        Marker->Marker.Data[0] = State->GetSystemId();
        Marker->Marker.Data[1] = 0;
        std::memcpy(&Marker->Marker.Data[2], &Position, sizeof(Vector));
        Player->Objectives.Objectives.push_front(Created);
        *bChanged = true;
        return *bChanged;
    }
    if (Front != nullptr && Front->Ids == 0x3319)
    {
        Player->Objectives.PopFront();
        *bChanged = true;
    }
    return *bChanged;
}

// FUN_06f4b9e0 (06f4b9e0-06f4bcf0, RET 8). Slot 8 is the mission's system; 0 -> *bChanged = 0.
// The player needs a state (else 0) and a set position (else 0), then a system record
// that is not already the mission's (else *bChanged = 0). A ship with FUN_06f57710 set: once
// per trip (+0x24 of the record), a first type 5 objective within 1000 units
// (squared distance, TEST AH,0x5 + JP: greater / unordered keep it) is popped and
// *bChanged = 1. Otherwise +0x24 = 0 and the path is rebuilt towards the next jump of the
// route (FUN_06f29c10 with slot 9, FUN_06f2a190): FUN_06f5ea40 fills the legs and slot 11
// lays them out (first ids 0x330e + FUN_06f4d200(5), leg ids 1, target ids 0x3318, the jump
// object, types 0x5a / 9 / 0x1a, at the front), its result in *bChanged.
bool FRandomMissionObject::UpdatePathObjectives(FMissionPlayer* Player, bool* bChanged)
{
    unsigned int const TargetSystem = static_cast<unsigned int>(Slot08());
    if (TargetSystem == 0)
    {
        *bChanged = false;
        return false;
    }
    FContentPlayerState* State = FindContentPlayerState(Player->PlayerId);
    if (State == nullptr)
        return false;
    Vector Position;
    State->GetPosition(&Position);
    if (!RmHasPosition(Position))
        return false;
    FMissionObjectiveList* List = &Player->Objectives;
    FSystemInfo* System = State->GetSystemRecord();
    if (System == nullptr || System->SystemId == TargetSystem)
    {
        *bChanged = false;
        return false;
    }
    FContentPlayerShip* Ship = State->ShipObject;
    if (Ship != nullptr && Ship->HasField120())
    {
        if (Player->Field_0x24 != 0)
            return *bChanged;
        Player->Field_0x24 = 1;
        if (List->Objectives.empty())
            return *bChanged;
        FMissionObjective* Front = List->GetFront();
        if (Front->GetType() != 5)
            return *bChanged;
        Vector Now;
        State->GetPosition(&Now);
        FNNObjectiveType5 const* Leg = static_cast<FNNObjectiveType5 const*>(Front);
        Vector LegPoint;
        std::memcpy(&LegPoint, &Leg->Marker.Data[2], sizeof(Vector));
        // FUN_06eb7810: each difference is stored as a float; the sum y*y + z*z + x*x stays
        // on the x87 stack.
        float const DeltaX = Now.x - LegPoint.x;
        float const DeltaY = Now.y - LegPoint.y;
        float const DeltaZ = Now.z - LegPoint.z;
        double const DistanceSq = static_cast<double>(DeltaY) * DeltaY +
                                  static_cast<double>(DeltaZ) * DeltaZ +
                                  static_cast<double>(DeltaX) * DeltaX;
        if (!(DistanceSq <= GRmPathReachSq))
            return *bChanged;
        List->PopFront();
        *bChanged = true;
        return true;
    }
    Player->Field_0x24 = 0;
    unsigned int const JumpObject =
        GetNextHopConnectionObject(System->SystemId, TargetSystem, Slot09());
    // Retail's stack slot keeps whatever it held when FUN_06f2a190 finds no connection.
    Vector JumpPosition = {0.0f, 0.0f, 0.0f};
    GetConnectionPosition(System->SystemId, JumpObject, &JumpPosition);
    std::list<FNnPathLeg> Legs;
    // 06f4bbcd-06f4bbff: FUN_06f5ea40 with the range local ([ESP+0x7c]) zeroed; the result is
    // not read.
    float const NoRange = 0.0f;
    BuildNnPath(System, &Position, &JumpPosition, &Legs, 0x19, &NoRange, 1);
    *bChanged = BuildPathObjectives(System, &Legs, List, 0x330e + RmRandomBelow(5), 1, 0x3318,
                                    JumpObject, 0x5a, 9, 0x1a, nullptr);
    return *bChanged;
}

// LAB_06f4cd50 (06f4cd50-06f4cf0c, RET 0x14). Unless bForce, a player name / group pair
// already recorded (FUN_06f4d090) stops here. The pair {GetName, Group} goes into +0x1e4
// (FUN_06f4d7c0), then the player's reputation (pub::Player::GetRep) takes Delta towards Group
// (FUN_06f50c50 with bEmpathy).
void FRandomMissionObject::ChangePlayerReputation(unsigned int InPlayerId, unsigned int Group,
                                                  float Delta, bool bForce, bool bEmpathy)
{
    if (!bForce && HasReputationEntry(InPlayerId, Group))
        return;
    std::wstring Name;
    pub::Player::GetName(InPlayerId, Name);
    // 06f4cdc0-06f4ce67: a pair {Name, Group} (unwind state 1) lives to the end of the function
    // (its string is freed at 06f4ce8f, after FUN_06f50c50); the insert takes a converted
    // pair<const wstring, uint> copy (state 2) that dies right after FUN_06f4d7c0.
    std::pair<std::wstring, unsigned int> const Entry(Name, Group);
    ReputationChanges.insert(std::multimap<std::wstring, unsigned int>::value_type(Entry));
    // Retail reads the result into the bForce argument slot.
    int Reputation = 0;
    pub::Player::GetRep(InPlayerId, Reputation);
    ApplyFactionReputationChange(Reputation, Delta, Group, bEmpathy);
}

// LAB_06f4cf50 (06f4cf50-06f4cf7a, RET 0xc): FUN_06f50c50(GetRep, Delta, Group, 1).
void FRandomMissionObject::ChangePlayerReputationAlways(unsigned int InPlayerId, unsigned int Group,
                                                        float Delta)
{
    int Reputation = 0;
    pub::Player::GetRep(InPlayerId, Reputation);
    ApplyFactionReputationChange(Reputation, Delta, Group, true);
}

// FUN_06f4cf80 (06f4cf80-06f4d002, RET 0xc): slot 41 (id, Group, Delta, 0, bEmpathy) per
// map entry in key order.
void FRandomMissionObject::ChangeGroupReputation(unsigned int Group, float Delta, bool bEmpathy)
{
    for (std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerStateMap.begin();
         It != PlayerStateMap.end(); ++It)
        ChangePlayerReputation(It->first, Group, Delta, false, bEmpathy);
}

// FUN_06f4d010 (06f4d010-06f4d08d, RET 8): slot 42 (id, Group, Delta) per map entry.
void FRandomMissionObject::ChangeGroupReputationAlways(unsigned int Group, float Delta)
{
    for (std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerStateMap.begin();
         It != PlayerStateMap.end(); ++It)
        ChangePlayerReputationAlways(It->first, Group, Delta);
}

// FUN_06f4d090 (06f4d090-06f4d1fa, RET 8). The name's first entry (FUN_06f4da10: lower_bound
// plus key test) starts a walk that runs to the END of the multimap, not of the name's range:
// any entry from there on with value Group answers true.
bool FRandomMissionObject::HasReputationEntry(unsigned int InPlayerId, unsigned int Group)
{
    std::wstring Name;
    pub::Player::GetName(InPlayerId, Name);
    std::multimap<std::wstring, unsigned int>::iterator It = ReputationChanges.lower_bound(Name);
    if (It != ReputationChanges.end() && Name < It->first)
        It = ReputationChanges.end();
    for (; It != ReputationChanges.end(); ++It)
    {
        if (It->second == Group)
            return true;
    }
    return false;
}

// FUN_06f4be60 (06f4be60-06f4bf1e, RET 8). A mapped player only. In the mission's system
// (+0x1f8 +0x10) his flags get 0xc, in the offer base's system (pub::GetSystem into the
// argument slot, so a failed lookup compares equal) 4, elsewhere 0xc is cleared -- each only
// while he has a ship.
void FRandomMissionObject::UpdatePlayerSystemFlags(unsigned int InPlayerId, unsigned int SystemId)
{
    std::map<unsigned int, FContentPlayerState*>::iterator Entry = PlayerStateMap.find(InPlayerId);
    if (Entry == PlayerStateMap.end())
        return;
    FContentPlayerState* State = Entry->second;
    if (SystemId == Data->DestSys)
    {
        if (State->ShipObject != nullptr)
            State->SetFlags(State->GetFlags() | 0xc);
        return;
    }
    unsigned int OfferSystem = SystemId;
    pub::GetSystem(OfferSystem, Data->OfferBase);
    if (SystemId == OfferSystem)
    {
        if (State->ShipObject != nullptr)
            State->SetFlags(State->GetFlags() | 4);
        return;
    }
    if (State->ShipObject != nullptr)
        State->SetFlags(State->GetFlags() & 0xfffffff3u);
}

// FUN_06f4bd30 (06f4bd30-06f4be5c, RET 4). The player's state (FUN_06f49b00, no null test)
// becomes +8 when he is the mission's player; he joins the mission (FUN_06f48ba0 with his own
// id as the sub id), his map value is set (insert then assign), the vector is rebuilt from
// FUN_06f49b00 of every key, and an existing record (slot 3) takes the state (+0x20) and is
// dropped (slot 4).
void FRandomMissionObject::RejoinPlayer(unsigned int InPlayerId)
{
    FContentPlayerState* State = FindContentPlayerState(InPlayerId);
    if (PlayerId == InPlayerId)
        PlayerState = State;
    State->JoinMission(&ControllerId, State->PlayerId, false, 0);
    PlayerStateMap.insert(std::map<unsigned int, FContentPlayerState*>::value_type(InPlayerId, nullptr))
        .first->second = State;
    PlayerStates.clear();
    for (std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerStateMap.begin();
         It != PlayerStateMap.end(); ++It)
        PlayerStates.push_back(FindContentPlayerState(It->first));
    FMissionPlayer* Record = FindPlayer(InPlayerId);
    if (Record != nullptr)
    {
        Record->PlayerState = State;
        RemovePlayer(Record);
    }
}

// FUN_06f4c610 (06f4c610-06f4c6e3, RET 4). A mapped player whose group has more than one
// member leaves the group (pub::Player::RemoveFromGroup); alone, the mission is aborted
// (slot 35 with 1).
void FRandomMissionObject::OnPlayerLeftGroup(unsigned int const* InPlayerId)
{
    std::map<unsigned int, FContentPlayerState*>::iterator Entry = PlayerStateMap.find(*InPlayerId);
    if (Entry == PlayerStateMap.end())
        return;
    std::vector<unsigned int> Members;
    Entry->second->GetGroupMembers(&Members);
    if (Members.size() > 1)
        pub::Player::RemoveFromGroup(*InPlayerId);
    else
        FailMission(1);
}

// FUN_06f4caa0 (06f4caa0-06f4cd37, RET 4). An unmapped player who is in the first player's
// group (FUN_06f48190) is mapped to his state, appended, named, joins the mission (sub id 0),
// gets a record (slot 2) with the placeholder objective, and the two mission messages
// (0xa empty, 8 the description, not broadcast).
void FRandomMissionObject::AddGroupMember(unsigned int InPlayerId)
{
    if (PlayerStateMap.find(InPlayerId) != PlayerStateMap.end())
        return;
    if (PlayerStates.empty())
        return;
    if (!PlayerStates[0]->IsGroupMember(InPlayerId))
        return;
    FContentPlayerState* Found = FindContentPlayerState(InPlayerId);
    std::map<unsigned int, FContentPlayerState*>::iterator Entry =
        PlayerStateMap.insert(std::map<unsigned int, FContentPlayerState*>::value_type(InPlayerId, Found))
            .first;
    FContentPlayerState* State = Entry->second;
    PlayerStates.push_back(State);
    // 06f4cb76 / 06f4ccea: the name local (unwind state 0) lives to the end of the function.
    std::wstring Name;
    State->GetName(&Name);
    AddPlayerName(Name);
    State->JoinMission(&ControllerId, 0, false, 0);
    GetOrCreatePlayer(State->PlayerId)->SendPlaceholderObjective();
    unsigned int const Target = State->PlayerId;
    {
        // 06f4ccb9-06f4cccb: the empty FmtStr (unwind state 1) dies before the second message.
        FmtStr Empty;
        pub::Player::DisplayMissionMessage(Target, Empty, static_cast<MissionMessageType>(0xa),
                                           false);
    }
    pub::Player::DisplayMissionMessage(Target, Field_0x68, static_cast<MissionMessageType>(8), false);
}

// FUN_06f4bf30 (06f4bf30-06f4c3aa, RET 8). Only with players (+0x200 non-empty) and for the
// "any state" call (*State < 0); controller events only (type 4), every one of them answered
// true. 0x3b / 0x36: system flags while +0x234 <= +0x48 (TEST AH,0x41 + JP: greater or
// unordered skip); 0x36 first resets a mapped player's objectives (slot 2, FUN_06f1cea0, slot
// 38). 0x20: unless single player without a ship, +0x234 (while not -1.0f) grows by the
// frame time until it reaches +0x48 (TEST AH,0x1 + JNZ: less or unordered add), then becomes
// -1.0f and every player's flag 4 is cleared; then the TU38 body. 0x24: FUN_06f4c610. 0x39:
// a mapped player lowers +0x220 (slot 32); at 0 the mission fails (slot 35: 2 single player,
// 3 multiplayer). 0x1009: swallowed. 5: a mapped player rejoins (FUN_06f4bd30). 0xa:
// FUN_06f4caa0. 8 / 9: a mapped player leaves (slot 36 with 0xe / 1), then the TU38 body.
// Anything else: the TU38 body FUN_06f0f150.
bool FRandomMissionObject::Slot2(SMEventData const* Event, int const& State)
{
    if (PlayerStates.empty())
        return false;
    if (State >= 0)
        return false;
    using pub::Controller::EMessage;
    // A type 4 event's +0x08 int is the controller message id (see HandleMissionMessage); it is
    // read, and cast back to EMessage, only after the type test.
    SMControllerEvent const* Message = static_cast<SMControllerEvent const*>(Event);
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::SystemEnter)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Message->DataPtr);
        if (ElapsedTime <= Data->FlagTime)
            UpdatePlayerSystemFlags(Parms[2], Parms[0]);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::SpaceEnter)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Parms[1]) != PlayerStateMap.end())
        {
            GetOrCreatePlayer(Parms[1])->ClearObjectives();
            RefreshObjectives(Parms[1]);
        }
        if (ElapsedTime <= Data->FlagTime)
            UpdatePlayerSystemFlags(Parms[1], Parms[0]);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::Heartbeat)
    {
        bool bCount = false;
        if (pub::SinglePlayer() && PlayerState->ShipObject != nullptr)
            bCount = true;
        else if (!pub::SinglePlayer())
            bCount = true;
        if (bCount && ElapsedTime != -1.0f)
        {
            if (!(ElapsedTime >= Data->FlagTime))
            {
                ElapsedTime += *static_cast<float const*>(Message->DataPtr);
            }
            else
            {
                ElapsedTime = -1.0f;
                for (int Index = 0; Index < static_cast<int>(PlayerStates.size()); ++Index)
                    PlayerStates[Index]->SetFlags(PlayerStates[Index]->GetFlags() & 0xfffffffbu);
            }
        }
        FMissionObject::HandleMissionMessage(static_cast<EMessage>(Message->ControllerId),
                                             Message->DataPtr);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::AbortMission)
    {
        OnPlayerLeftGroup(static_cast<unsigned int const*>(Message->DataPtr));
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::ObjectDestroyed)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Parms[3]) == PlayerStateMap.end())
            return true;
        if (DecrementPlayerCount() > 0)
            return true;
        if (pub::SinglePlayer())
            FailMission(2);
        else
            FailMission(3);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::FormationLeaderChanged)
        return true;
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::PlayerLoad)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Parms[0]) == PlayerStateMap.end())
            return true;
        RejoinPlayer(Parms[0]);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::GroupMemberJoined)
    {
        AddGroupMember(*static_cast<unsigned int const*>(Message->DataPtr));
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::PlayerLogout)
    {
        unsigned int const Leaving = *static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Leaving) != PlayerStateMap.end())
            RemoveMissionPlayer(Leaving, 0xe);
        FMissionObject::HandleMissionMessage(static_cast<EMessage>(Message->ControllerId),
                                             Message->DataPtr);
        return true;
    }
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Message->ControllerId) == EMessage::GroupMemberLeft)
    {
        unsigned int const Leaving = *static_cast<unsigned int const*>(Message->DataPtr);
        if (PlayerStateMap.find(Leaving) != PlayerStateMap.end())
            RemoveMissionPlayer(Leaving, 1);
        FMissionObject::HandleMissionMessage(static_cast<EMessage>(Message->ControllerId),
                                             Message->DataPtr);
        return true;
    }
    if (Event->get_event_type() == 4)
    {
        FMissionObject::HandleMissionMessage(static_cast<EMessage>(Message->ControllerId),
                                             Message->DataPtr);
        return true;
    }
    return false;
}

// =======================================================================================
// TU31 -- random encounter parameters and the factories of their two halves
// =======================================================================================

namespace
{
    // DAT_06fcf384: this TU's math engine, created on first use (FUN_06fa8fa0).
    I3DMathEngine* GRandEncMathEngine = nullptr;

    // DAT_06fb7b40: the turn of a spawned formation towards its target, (double)(float)pi.
    double const GRandEncTurnAngle = 3.1415927410125732;

    // The weighted pick inlined three times in TU31 -- RandEncPickWeighted (RandomEncounters.h).
    template <typename TElement>
    typename std::list<TElement>::iterator PickWeighted(std::list<TElement>* Elements,
                                                        float TElement::*Weight)
    {
        return RandEncPickWeighted(Elements, Weight);
    }
}

// FUN_06efb550 (06efb550-06efb68b). The +0x18 ID_String is cleared twice and the (empty) +0x1c
// list emptied after the vptr is set (06efb643-06efb675); +0x0c, +0x10 and +0x88 are left
// unset in retail (the port starts them at 0 / null).
FRandEncParams::FRandEncParams()
{
    PilotId.clear();
}

// FUN_06efb710 (06efb710-06efb82a): +0x44 (FUN_06efc910 per element, the element's own list
// freed by FUN_06efc950), +0x28 and +0x1c (FUN_06ecfe80 per element) erased and their heads
// freed -- the member lists' destructors.
FRandEncParams::~FRandEncParams()
{
}

// FUN_06efc2d0 (06efc2d0-06efc795, RET 4). bForced is not read. ShipNameCounter and
// ShipsSpawned restart at 0; the target's location (pub::SpaceObj::GetLocation on +0x88) and a
// copy of the formation positions (FUN_06efc7d0) are taken, the 180-degree turn matrix is built
// (cos / sin of DAT_06fb7b40), and one "GotoShip" directive (factory type 3) aimed at +0x84
// (SetTarget(&GotoTargetId, 200, -1), Op +0x58 = 1, +0x5a = 0) goes into a local list with
// its own reference. Then per formation to spawn: a weighted position is taken out of the copy
// (its value is not read), the spawn point is PlaceFacingAnchor(target, +0x90, +0x94); with a
// player system and a positive +0x98 the point is checked by pub::SpaceObj::IsPosEmpty and
// moved by GetEmptyPos(POS_SEL_3D_VOLUME) when occupied; the placement's rotation becomes
// turn * target rotation (math engine slot 8, MulMatrix) and SpawnFormation(-1, the list, null,
// null, 0, +0x8c) creates the formation. Afterwards +0x38 = 0 and the local references go.
void FRandEncParams::Spawn(bool bForced)
{
    (void)bForced;
    ShipNameCounter = 0;
    ShipsSpawned = 0;
    Vector TargetPosition;
    Matrix TargetRotation;
    pub::SpaceObj::GetLocation(TargetObjectId, TargetPosition, TargetRotation);
    std::list<FRandEncPosition> FreePositions(Positions);

    // 06efc324-06efc36a: rows {cos, 0, sin}, {0, 1, 0}, {-sin, 0, cos}.
    float const TurnCos = static_cast<float>(std::cos(GRandEncTurnAngle));
    float const TurnSin = static_cast<float>(std::sin(GRandEncTurnAngle));
    FMatrix Turn;
    Turn.D[0][0] = TurnCos;
    Turn.D[0][1] = 0.0f;
    Turn.D[0][2] = TurnSin;
    Turn.D[1][0] = 0.0f;
    Turn.D[1][1] = 1.0f;
    Turn.D[1][2] = 0.0f;
    Turn.D[2][0] = -TurnSin;
    Turn.D[2][1] = 0.0f;
    Turn.D[2][2] = TurnCos;

    // 06efc373-06efc410
    // The list, then the directive holder (FUN_06f28e10 run on it while still empty at
    // 06efc3a6, a no-op); the holder is released first at the end.
    FMissionDirectiveList LeaderDirectives;
    TRefHandle<FObjListDirective> Leader;
    CreateObjListDirective(3, &Leader.Pointer);
    FObjListGotoShip* const Goto = static_cast<FObjListGotoShip*>(Leader.Pointer);
    Goto->SetTarget(&GotoTargetId, 200.0f, -1.0f);
    Goto->Op.Field_5A = 0;
    Goto->Op.Field_58 = 1;
    LeaderDirectives.Items.push_back(Leader.Pointer);
    Leader->AddRef();

    for (int Index = 0; Index < FormationsToSpawn; ++Index)
    {
        std::list<FRandEncPosition>::iterator const Picked =
            PickWeighted(&FreePositions, &FRandEncPosition::Weight);

        // 06efc4fc-06efc589: the target transform {rotation, position} and the spawn point.
        FTransform Target;
        std::memcpy(&Target.Rotation, &TargetRotation, sizeof(FMatrix));
        Target.Translation.X = TargetPosition.x;
        Target.Translation.Y = TargetPosition.y;
        Target.Translation.Z = TargetPosition.z;
        FTransform Around;
        SetMatrixIdentity(&Around.Rotation);
        Around.Translation.X = 0.0f;
        Around.Translation.Y = 0.0f;
        Around.Translation.Z = 0.0f;
        PlaceFacingAnchor(&Target, &SpawnAngle, &SpawnDistance, &Around);  // FUN_06f243d0

        // 06efc58e-06efc638
        FSystemInfo* const System = Player->GetSystemRecord();
        if (System != nullptr && EmptyPosRadius > 0)
        {
            unsigned int const SystemId = System->SystemId;
            float const Radius = static_cast<float>(EmptyPosRadius);
            Vector Point;
            Point.x = Around.Translation.X;
            Point.y = Around.Translation.Y;
            Point.z = Around.Translation.Z;
            bool bEmpty = true;
            pub::SpaceObj::IsPosEmpty(SystemId, Point, Radius, bEmpty);
            if (!bEmpty)
            {
                static_assert(sizeof(Transform) == sizeof(FTransform), "Transform is 0x30");
                Transform AroundTransform;
                std::memcpy(&AroundTransform, &Around, sizeof(Transform));
                pub::SpaceObj::PosSelectionType const Selection =
                    pub::SpaceObj::POS_SEL_3D_VOLUME;
                pub::SpaceObj::GetEmptyPos(SystemId, AroundTransform, Radius, Radius, Selection,
                                           Point);
                Around.Translation.X = Point.x;
                Around.Translation.Y = Point.y;
                Around.Translation.Z = Point.z;
            }
        }

        // 06efc63b-06efc6e8. Retail unlinks the picked node unconditionally; with nothing
        // picked that is the list's own head (undefined), which the port does not erase.
        FTransform Placement = Around;
        if (Picked != FreePositions.end())
            FreePositions.erase(Picked);
        if (GRandEncMathEngine == nullptr)
            GRandEncMathEngine = ContentAcquire3DMathEngine();
        GRandEncMathEngine->MulMatrix(Placement.Rotation, Turn, Target.Rotation);
        SpawnFormation(&Placement, -1, &LeaderDirectives.Items, nullptr, nullptr, 0, DockTarget);
    }
    // 06efc705-06efc71d: the holder released inline and cleared.
    FormationsToSpawn = 0;
    if (Leader.Pointer != nullptr)
    {
        Leader.Pointer->Release();
        Leader.Pointer = nullptr;
    }
}

// FUN_06efba30 (06efba30-06efc2c1, RET 0x1c). Per ship: an FContentNpcShipParams (FUN_06f33500)
// named from Names or "rand_enc_ship_%d", +0x6c = ControllerId, placed at Placement, +0x104 =
// 24.0f when (DockTarget & 0x3fffffff) == 0 else -1.0f (DAT_06fb7ae0 / DAT_06fb7adc), +0x70 =
// Priority, the labels appended to +0x118. The ship classes (a weighted choice or Choices'
// Index-th element) are copied (FUN_06eb6560); their ship class id (FUN_06f580d0) forms a
// one-id line that is tried first, the classes themselves second (FUN_06f35890 with level
// -1); no arch -> the blocks are dropped and false. The pilot name: PickNpcNameGender,
// BuildNpcName (a default request, bNamedLeader 1), then the faction costume / voice
// (FUN_06f34a80 with the gender), the arch (FUN_06f348f0) and a new name reputation in +0.
// The arch's level goes to +0x500 either way; the first ship gets Directives (each AddRef'ed).
// Retail also builds an unused name block ([ESP+0xd4], held as UnusedName: it has EH states)
// and an unused default name request ([ESP+0x10c], FUN_06f31650; no destructor, no EH
// state, not reproduced).
bool FRandEncParams::SpawnFormation(FTransform const* Placement, int Count,
                                    std::list<FObjListDirective*> const* Directives,
                                    std::list<FRandEncShipChoice> const* Choices,
                                    std::list<TString<0x30>>* Names, int Priority,
                                    unsigned int DockTarget)
{
    FSystemInfo* const System = Player->GetSystemRecord();
    if (System == nullptr)
        return false;
    std::list<FRandEncFormation>::iterator const Picked =
        PickWeighted(&Formations, &FRandEncFormation::Weight);
    if (Picked == Formations.end())
        return false;
    FRandEncFormation& Formation = *Picked;
    if (Count == -1)
    {
        // 06efbb63-06efbb87: (rand() * range) / 32768 truncated (CDQ / AND 0x7fff / SAR 0xf).
        int const Range = Formation.MaxCount - Formation.MinCount + 1;
        Formation.Count = std::rand() * Range / 32768 + Formation.MinCount;
    }
    else
    {
        Formation.Count = Count;
    }

    std::list<FContentNpcShipParams> Blocks;
    ShipsSpawned += Formation.Count;
    bool const bNoDockTarget = (DockTarget & 0x3fffffffu) == 0;
    for (int Index = 0; Index < Formation.Count; ++Index)
    {
        FContentNpcShipParams Params;
        // 06efbbf1-06efbc38: a name block at [ESP+0xd4] (two FmtStr, +0x30 = 0, +0x34 = -1;
        // EH states 2/3, dtor FUN_06ef4e60) is built right after the ship block and dies at
        // the end of the iteration after the class lines (06efc10b-06efc11c). Never read.
        FNpcNameParams UnusedName;
        if (Names == nullptr)
        {
            char Nickname[0x60];
            std::sprintf(Nickname, "rand_enc_ship_%d", ShipNameCounter);
            ++ShipNameCounter;
            Params.Nickname = Nickname;
        }
        else
        {
            Params.Nickname = Names->front().Data;
            Names->pop_front();
        }
        Params.ControllerId = ControllerId;
        std::memcpy(&Params.Orientation, &Placement->Rotation, sizeof(FMatrix));
        Params.Position.x = Placement->Translation.X;
        Params.Position.y = Placement->Translation.Y;
        Params.Position.z = Placement->Translation.Z;
        Params.Field_0x104 = bNoDockTarget ? 24.0f : -1.0f;
        Params.Priority = Priority;
        for (std::list<unsigned int>::iterator Label = Labels.begin(); Label != Labels.end();
             ++Label)
            Params.Field_0x118.push_back(*Label);

        // 06efbd57-06efbe8d. Retail reads the classes of the list's head when nothing is
        // picked (undefined); the port leaves them empty.
        FEncounterShip MemberLine;
        if (Choices == nullptr)
        {
            std::list<FRandEncShipChoice>::iterator const Choice =
                PickWeighted(&Formation.ShipChoices, &FRandEncShipChoice::Weight);
            if (Choice != Formation.ShipChoices.end())
                MemberLine.ShipClasses = Choice->ShipClasses;
        }
        else
        {
            std::list<FRandEncShipChoice>::const_iterator Choice = Choices->begin();
            for (int Step = 0; Step < Index; ++Step)
                ++Choice;
            MemberLine.ShipClasses = Choice->ShipClasses;
        }
        ID_String ClassId;
        ClassId.clear();
        FindShipClassByMembers(&ClassId.Id, MemberLine.ShipClasses);
        FEncounterShip ClassLine;
        if (std::find(ClassLine.ShipClasses.begin(), ClassLine.ShipClasses.end(), ClassId.Id) ==
            ClassLine.ShipClasses.end())
            ClassLine.ShipClasses.insert(ClassLine.ShipClasses.end(), ClassId.Id);

        // 06efbf21-06efbf52
        FNpcShipArch* Arch = PickFactionShipLineArch(static_cast<unsigned int>(AttackerRep),
                                                     &ClassLine, -1);
        if (Arch == nullptr)
            Arch = PickFactionShipLineArch(static_cast<unsigned int>(AttackerRep), &MemberLine, -1);
        if (Arch == nullptr)
            return false;

        // 06efbf58-06efc055: the name block [ESP+0x9c] (EH states 6/7/8) is destroyed right
        // after the level store, before the leader's directives and the push_back.
        {
            FNpcNameParams Name;
            PickNpcNameGender(static_cast<unsigned int>(AttackerRep), &Name);
            FNpcNameRequest Request;
            if (BuildNpcName(static_cast<unsigned int>(AttackerRep), &Request, &Name, true))
            {
                ApplyNpcShipFaction(static_cast<unsigned int>(AttackerRep), Name.Gender,
                                    &Params);
                ApplyNpcShipArch(&Params, Arch, nullptr, nullptr);
                if (Params.ReputationHandle != nullptr)
                    Params.ReputationHandle->Release();                     // FUN_06f28e10
                Params.ReputationHandle = nullptr;
                CreateNpcNameReputation(&Name, &Params.ReputationHandle);
            }
            Params.Level = Arch->Level;
        }

        // 06efc057-06efc0c2: the leader's directives (FUN_06ed2a00 on the fresh block, the
        // assign FUN_06f93490, then slot 0 AddRef on each).
        if (Index == 0)
        {
            for (std::list<FObjListDirective*>::const_iterator It = Directives->begin();
                 It != Directives->end(); ++It)
            {
                Params.Directives.Items.push_back(*It);
                (*It)->AddRef();
            }
        }
        Blocks.push_back(Params);                                          // FUN_06efc860
    }

    // 06efc18e-06efc1c2
    PlaceFormationBlocks(&Blocks, Formation.Formation, Placement);
    System->CreateNpcShips(&Blocks, DockTarget, nullptr);
    return true;
}

// FUN_06efb830 (06efb830-06efb8dc).
bool __cdecl CreateRandEncParams(char const* Name, FRandEncParams** Out)
{
    FRandEncParams* Created = nullptr;
    if (_stricmp(Name, "TLParams") == 0)
        Created = new FRandEncTLParams();                                  // new 0xbc, 06f81bb0
    else if (_stricmp(Name, "SPParams") == 0)
        Created = new FRandEncSPParams();                                  // new 0xc4, 06f804d0
    *Out = Created;
    return Created != nullptr;
}

// FUN_06efb8e0 (06efb8e0-06efb974): *Out = null first; 2 -> SP, 3 -> TL, else null.
bool __cdecl CreateRandEncParamsByType(int Type, FRandEncParams** Out)
{
    *Out = nullptr;
    if (Type == 2)
        *Out = new FRandEncSPParams();
    else if (Type == 3)
        *Out = new FRandEncTLParams();
    return *Out != nullptr;
}

// FUN_06efb980 (06efb980-06efba28): the 0xc objects are built inline (vtable, count 1, +8 0).
bool __cdecl CreateRandEncPopulator(char const* Name, FRandEncPopulator** Out)
{
    FRandEncPopulator* Created = nullptr;
    if (_stricmp(Name, "TLPzr") == 0)
        Created = new FRandEncTLPopulator();
    else if (_stricmp(Name, "SPPzr") == 0)
        Created = new FRandEncSPPopulator();
    *Out = Created;
    return Created != nullptr;
}
