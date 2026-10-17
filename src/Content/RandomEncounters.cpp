// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "RandomEncounters.h"

#include "Cargo.h"          // GetNpcClassFactions, GetVibeFeelingMask, FactionHasNpcClass
#include "ContentInternal.h"    // ContentAcquire3DMathEngine
#include "ContentPlayerState.h"
#include "ContentStaticIds.h"
#include "DockQueues.h"     // PlaceFormationBlocks (FUN_06f66c80)
#include "EncounterParams.h"    // FEncounterShip
#include "Encounters.h"     // FindFormation (FUN_06efd1a0), SetMatrixIdentity
#include "Guides.h"         // FGuideDesc, FSpawnPlan
#include "MissionObject.h"  // FMissionDirectiveList
#include "NpcNames.h"       // PickNpcNameGender, BuildNpcName, CreateNpcNameReputation
#include "NpcShip.h"        // FContentNpcShipParams, ApplyNpcShipArch, ApplyNpcShipFaction
#include "NpcShips.h"       // FNpcShipArch
#include "PatrolPaths.h"    // FPatrolPath::GetZoneSegments
#include "RefHandle.h"      // TRefHandle (FUN_06f28e10 holders)
#include "MissionCnd.h"     // GetObjectDistanceSq (FUN_06f242e0)
#include "MissionObjList.h" // CreateObjListDirective, FObjListDelay (TU40)
#include "ObjListDirective.h"
#include "RandomMissionDB.h"
#include "SpaceSupport.h"   // SetLeaderGuideTargets (TU53 FUN_06f63cc0)
#include "SystemInfo.h"     // FSystemInfo, FSystemZone
#include "common_csys.h"    // Matrix (pub::SpaceObj::GetLocation)
#include "common_ini.h"
#include "common_server.h"
#include "common_weighted_vector.h"
#include "msvcrt_compat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string.h>

// =======================================================================================
// TU62 -- killable solars and vignette critical loot
// =======================================================================================

std::map<unsigned int, FRmKillableSolar> GRmKillableSolars;   // DAT_06fd3c20
std::vector<FRmCriticalLoot> GRmCriticalLoot;                  // DAT_06fd3c10

std::pair<std::vector<unsigned int>::iterator, bool> __cdecl RmInsertUnique(
    std::vector<unsigned int>* Values, unsigned int const& Value)
{
    // 06f95e03-06f95e21: linear find over [begin, end).
    std::vector<unsigned int>::iterator It = std::find(Values->begin(), Values->end(), Value);
    if (It != Values->end())
        return std::make_pair(It, false);                     // 06f95e6b
    // 06f95e25 FUN_06f937c0(end, 1, &Value), then the same find again (06f95e30-06f95e49);
    // it always hits the element just appended.
    Values->insert(Values->end(), Value);
    return std::make_pair(Values->end() - 1, true);
}

// FUN_06f95040 -- RandomMissions\KillableSolars.ini, called by the generator ctor 06f9d794.
void __cdecl LoadKillableSolars(INI_Reader* Reader)
{
    // [ESP+0x5c] -- GetMaxHitPoints out-slot; one frame slot for every section, so a failed
    // lookup compares against the previous section's value (retail: indeterminate before the
    // first successful call; the port starts from 0).
    int MaxHitPoints = 0;

    while (Reader->read_header())
    {
        if (!Reader->is_header("KillableSolar"))
            continue;

        // 06f9508b-06f950d9: ID_String::clear, SolarType -1, zeroes, PilotChoices 1.
        FRmKillableSolar Entry;
        Entry.Nickname.clear();
        Entry.SolarType = -1;
        Entry.StringId = 0;
        Entry.bAnyFaction = 0;
        Entry.ArchetypeId = 0;
        Entry.LoadoutId = 0;
        Entry.HitpointsMin = 0.0f;
        Entry.HitpointsMax = 0.0f;
        Entry.DifficultyMin = 0.0f;
        Entry.DifficultyMax = 0.0f;
        Entry.ZoneTypeMask = 0;
        Entry.PilotChoices = 1;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Entry.Nickname.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("solar_type"))
            {
                // 06f95132-06f95162: inline walk of PTR_s_big_solar_06fce748 (4 rows), null
                // names skipped, DACOM stricmp(value, name); miss leaves SolarType.
                char const* Value = Reader->get_value_string();
                for (unsigned int Index = 0; Index < 4; ++Index)
                {
                    if (GRmSolarTypes[Index].Name != nullptr &&
                        _stricmp(Value, GRmSolarTypes[Index].Name) == 0)
                    {
                        Entry.SolarType = GRmSolarTypes[Index].Value;
                        break;
                    }
                }
            }
            else if (Reader->is_value("string_id"))
            {
                Entry.StringId = Reader->get_value_int(0);
            }
            else if (Reader->is_value("faction"))
            {
                if (_stricmp(Reader->get_value_string(), "all") == 0)
                {
                    Entry.bAnyFaction = 1;
                }
                else
                {
                    // 06f952c5-06f9537e: every value -> reputation group, appended unique.
                    Entry.bAnyFaction = 0;
                    for (unsigned int Index = 0; !Reader->is_value_empty(Index); ++Index)
                    {
                        unsigned int GroupId;
                        pub::Reputation::GetReputationGroup(GroupId, Reader->get_value_string(Index));
                        RmInsertUnique(&Entry.Factions, GroupId);
                    }
                }
            }
            else if (Reader->is_value("archetype"))
            {
                Entry.ArchetypeId = pub::GetNicknameId(Reader->get_value_string());
            }
            else if (Reader->is_value("loadout"))
            {
                pub::GetLoadoutID(Entry.LoadoutId, Reader->get_value_string());
            }
            else if (Reader->is_value("hitpoints"))
            {
                // 06f953dd-06f9541f: FILD int -> float; a missing second value copies the first.
                Entry.HitpointsMin = static_cast<float>(Reader->get_value_int(0));
                if (Reader->is_value_empty(1))
                    Entry.HitpointsMax = Entry.HitpointsMin;
                else
                    Entry.HitpointsMax = static_cast<float>(Reader->get_value_int(1));
            }
            else if (Reader->is_value("difficulty"))
            {
                Entry.DifficultyMin = Reader->get_value_float(0);
                Entry.DifficultyMax = Reader->get_value_float(1);
            }
            else if (Reader->is_value("only_allowed_in_zone_type"))
            {
                Entry.ZoneTypeMask = 0;
                for (unsigned int Index = 0; !Reader->is_value_empty(Index); ++Index)
                {
                    char const* Value = Reader->get_value_string(Index);
                    if (_stricmp(Value, "rock") == 0)
                        Entry.ZoneTypeMask |= 0x40;
                    else if (_stricmp(Value, "debris") == 0)
                        Entry.ZoneTypeMask |= 0x80;
                    else if (_stricmp(Value, "ice") == 0)
                        Entry.ZoneTypeMask |= 0x100;
                    else if (_stricmp(Value, "lava") == 0)
                        Entry.ZoneTypeMask |= 0x200;
                    else if (_stricmp(Value, "crystal") == 0)
                        Entry.ZoneTypeMask |= 0x800;
                    // 06f95517: a last stricmp against "all" whose result is discarded.
                }
            }
            else if (Reader->is_value("pilot_choices"))
            {
                Entry.PilotChoices = Reader->get_value_int(0);
            }
        }

        // 06f95179-06f9522d: reject incomplete or out-of-range entries.
        if (Entry.Nickname.IsEmpty())
            continue;
        if (Entry.SolarType == -1)
            continue;
        if (!Entry.bAnyFaction && Entry.Factions.empty())
            continue;
        if (Entry.ArchetypeId == 0 || Entry.LoadoutId == 0)
            continue;
        if (!(Entry.HitpointsMin > 0.0f))                         // TEST AH,0x41 / JNZ
            continue;
        if (!(Entry.HitpointsMax >= Entry.HitpointsMin))          // TEST AH,0x1 / JNZ
            continue;
        if (!(Entry.DifficultyMin >= 0.0f))                       // TEST AH,0x1 / JNZ
            continue;
        if (!(Entry.DifficultyMin <= Entry.DifficultyMax))        // TEST AH,0x41 / JP
            continue;
        if (!(Entry.DifficultyMax <= 100.0f))                     // TEST AH,0x41 / JP
            continue;

        // 06f95233-06f95263: clamp both hit point bounds to the archetype maximum when the
        // upper bound exceeds it (FILD int vs float on the x87 stack, FST/FSTP float).
        pub::GetMaxHitPoints(Entry.ArchetypeId, MaxHitPoints);
        if (static_cast<double>(Entry.HitpointsMax) > static_cast<double>(MaxHitPoints))
        {
            Entry.HitpointsMin = static_cast<float>(MaxHitPoints);
            Entry.HitpointsMax = static_cast<float>(MaxHitPoints);
        }

        // 06f95561-06f9560b: FUN_06f95ef0 map insert-unique keyed by the nickname id.
        GRmKillableSolars.insert(std::make_pair(Entry.Nickname.Id, Entry));
    }
}

// FUN_06f956c0 -- random killable solar for (faction, solar type, zone mask, difficulty);
// callers 06f9ae50 / 06f9c012 (TU63).
FRmKillableSolar* __cdecl PickKillableSolar(unsigned int Faction, int SolarType,
                                            unsigned int ZoneTypeMask, float Difficulty)
{
    // 8-byte candidate {entry, difficulty}; the float half is written and never read.
    struct FCandidate
    {
        FRmKillableSolar* Entry;
        float Difficulty;
    };
    std::vector<FCandidate> Candidates;

    for (std::map<unsigned int, FRmKillableSolar>::iterator It = GRmKillableSolars.begin();
         It != GRmKillableSolars.end(); ++It)
    {
        FRmKillableSolar& Entry = It->second;
        if (!Entry.bAnyFaction &&
            std::find(Entry.Factions.begin(), Entry.Factions.end(), Faction) == Entry.Factions.end())
            continue;
        if (Entry.SolarType != SolarType)
            continue;
        if (!(Entry.DifficultyMin <= Difficulty))                 // TEST AH,0x41 / JP
            continue;
        if (!(Difficulty <= Entry.DifficultyMax))                 // TEST AH,0x41 / JP
            continue;
        if ((Entry.ZoneTypeMask | ZoneTypeMask) != ZoneTypeMask)
            continue;
        FCandidate Candidate;
        Candidate.Entry = &Entry;
        Candidate.Difficulty = Difficulty;
        Candidates.insert(Candidates.end(), Candidate);           // FUN_06f96770(end, 1, &c)
    }

    if (Candidates.empty())
        return nullptr;
    // 06f957be-06f957e7: one candidate -> index 0 without calling rand; otherwise
    // (rand() * Count) / 0x8000 (CDQ / AND EDX,0x7fff / SAR 15).
    int Count = static_cast<int>(Candidates.size());
    int Index = 0;
    if (Count - 1 != 0)
        Index = (rand() * Count) / 0x8000;
    return Candidates[Index].Entry;
}

// FUN_06f95890 -- RandomMissions\VignetteCriticalLoot.ini, called by the generator ctor
// 06f9d7bd.
void __cdecl LoadVignetteCriticalLoot(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("critical_loot"))
            continue;

        // 06f958d8-06f9596b: push_back of a fresh record, then fill the new back() element.
        // Retail leaves WhyDesired and ArchetypeId of the temporary unset (stack contents);
        // the port zeroes them. The temporary dies right after the insert (unwind map
        // FuncInfo_06fc6c20: state -1 at 06f95941, before the value loop).
        {
            FRmCriticalLoot Fresh;
            Fresh.WhyDesired[0] = 0;
            Fresh.WhyDesired[1] = 0;
            Fresh.Name = 0;
            Fresh.ArchetypeId = 0;
            Fresh.bAnyFaction = 0;
            GRmCriticalLoot.insert(GRmCriticalLoot.end(), Fresh);   // FUN_06f969c0(end, 1, &rec)
        }
        FRmCriticalLoot* Record = &GRmCriticalLoot.back();

        while (Reader->read_value())
        {
            if (Reader->is_value("faction"))
            {
                if (_stricmp(Reader->get_value_string(), "all") == 0)
                {
                    Record->bAnyFaction = 1;
                }
                else
                {
                    Record->bAnyFaction = 0;
                    for (unsigned int Index = 0; !Reader->is_value_empty(Index); ++Index)
                    {
                        unsigned int GroupId;
                        pub::Reputation::GetReputationGroup(GroupId, Reader->get_value_string(Index));
                        RmInsertUnique(&Record->Factions, GroupId);
                    }
                }
            }
            else if (Reader->is_value("archetype"))
            {
                pub::GetEquipmentID(Record->ArchetypeId, Reader->get_value_string(0));
            }
            else if (Reader->is_value("name"))
            {
                Record->Name = Reader->get_value_int(0);
            }
            else if (Reader->is_value("why_desired"))
            {
                Record->WhyDesired[0] = Reader->get_value_int(0);
                Record->WhyDesired[1] = Reader->get_value_int(1);
            }
            else if (Reader->is_value("type"))
            {
                // 06f95ae7-06f95c5a: destroy 1, tractor 2, return 3, each appended unique;
                // other words ignored.
                for (unsigned int Index = 0; !Reader->is_value_empty(Index); ++Index)
                {
                    if (_stricmp(Reader->get_value_string(Index), "destroy") == 0)
                        RmInsertUnique(&Record->Types, 1);
                    else if (_stricmp(Reader->get_value_string(Index), "tractor") == 0)
                        RmInsertUnique(&Record->Types, 2);
                    else if (_stricmp(Reader->get_value_string(Index), "return") == 0)
                        RmInsertUnique(&Record->Types, 3);
                }
            }
        }
    }
}

// FUN_06f95cd0 -- random critical loot record for (faction, type); caller 06f97bd1 (TU63).
FRmCriticalLoot* __cdecl PickCriticalLoot(unsigned int Faction, unsigned int Type)
{
    std::vector<FRmCriticalLoot*> Candidates;
    for (std::vector<FRmCriticalLoot>::iterator It = GRmCriticalLoot.begin();
         It != GRmCriticalLoot.end(); ++It)
    {
        if (!It->bAnyFaction &&
            std::find(It->Factions.begin(), It->Factions.end(), Faction) == It->Factions.end())
            continue;
        if (std::find(It->Types.begin(), It->Types.end(), Type) == It->Types.end())
            continue;
        Candidates.insert(Candidates.end(), &*It);               // FUN_06f937c0(end, 1, &p)
    }

    if (Candidates.empty())
        return nullptr;
    int Count = static_cast<int>(Candidates.size());
    int Index = 0;
    if (Count - 1 != 0)
        Index = (rand() * Count) / 0x8000;
    return Candidates[Index];
}

// =======================================================================================
// TU59 / TU60 -- the SP_Attack and TL_Attack parameter objects and populators (the TU31 base is
// in RandomMissionDB.cpp)
// =======================================================================================

namespace
{
    // The "formation" lines of both dumps (06f81da4-06f81e3f, 06f80fda-06f8107b): Header with
    // (nickname, +0x08, min, max), then ", %u, %f" (ship class id FUN_06f580d0, weight) per
    // ship choice, then "\n". bNameAsNumber: the TL format prints get_string()'s pointer with
    // %u.
    void DumpRandEncFormations(FILE* File, FRandEncParams* Params, char const* Header,
                               bool bNameAsNumber)
    {
        for (std::list<FRandEncFormation>::iterator It = Params->Formations.begin();
             It != Params->Formations.end(); ++It)
        {
            char const* const Name = It->Nickname.get_string();
            if (bNameAsNumber)
                std::fprintf(File, Header,
                             static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(Name)),
                             static_cast<double>(It->Field_0x08), It->MinCount, It->MaxCount);
            else
                std::fprintf(File, Header, Name, static_cast<double>(It->Field_0x08),
                             It->MinCount, It->MaxCount);
            for (std::list<FRandEncShipChoice>::iterator Choice = It->ShipChoices.begin();
                 Choice != It->ShipChoices.end(); ++Choice)
            {
                ID_String ClassId;
                ClassId.clear();
                FindShipClassByMembers(&ClassId.Id, Choice->ShipClasses);
                std::fprintf(File, ", %u, %f", ClassId.Id, static_cast<double>(Choice->Weight));
            }
            std::fprintf(File, "\n");
        }
    }

    // DAT_06fb6060: degrees to radians (float).
    float const GRandEncDegToRad = 0.017453292f;

    // DAT_06fd3b34: TU60's own cached math engine (FUN_06fa8fa0 on first use; the TU31 one is
    // DAT_06fcf384, the TU53 one DAT_06fd1c70 -- one cache per TU, the same engine).
    I3DMathEngine* GRandEncLaneMathEngine = nullptr;

    // DAT_06fb7b40: the turn of a spawned formation towards its target, (double)(float)pi
    // (shared with TU31).
    double const GRandEncLaneTurnAngle = 3.1415927410125732;

    // FUN_06f83ec0 (cdecl): *Out = TransformMatrix(*Matrix, *Source) through a local (math
    // engine slot +0x2c).
    void TransformByMatrix(FVector* Out, FMatrix const* Matrix, FVector const* Source)
    {
        if (GRandEncLaneMathEngine == nullptr)
            GRandEncLaneMathEngine = ContentAcquire3DMathEngine();
        FVector Result;
        GRandEncLaneMathEngine->TransformMatrix(Result, *Matrix, *Source);
        *Out = Result;
    }

    // FUN_06f83f10 (cdecl): *Out = MulMatrix(*A, *B) through a local (slot +0x20), copied as
    // 9 dwords.
    void MultiplyMatrices(FMatrix* Out, FMatrix const* A, FMatrix const* B)
    {
        if (GRandEncLaneMathEngine == nullptr)
            GRandEncLaneMathEngine = ContentAcquire3DMathEngine();
        FMatrix Result;
        GRandEncLaneMathEngine->MulMatrix(Result, *A, *B);
        *Out = Result;
    }

    // FUN_06f24370 (cdecl; only caller the SP Accept 06f80841): the squared distance from
    // the object to Point (by value), -1.0f (DAT_06fb5e84) when pub::SpaceObj::GetLocation
    // fails. Summed on the x87 stack as (dz^2 + dy^2) + dx^2 and returned unrounded.
    double GetObjectDistanceSqTo(unsigned int const* ObjectId, Vector Point)
    {
        Vector Position;
        Matrix Orientation;
        if (pub::SpaceObj::GetLocation(*ObjectId, Position, Orientation) != 0)
            return -1.0;
        double const Dx = static_cast<double>(Position.x) - static_cast<double>(Point.x);
        double const Dy = static_cast<double>(Position.y) - static_cast<double>(Point.y);
        double const Dz = static_cast<double>(Position.z) - static_cast<double>(Point.z);
        return (Dz * Dz + Dy * Dy) + Dx * Dx;
    }

    // One default spawn point of the SP Accept (06f80932-06f80a89): y = (rand() * 201) >> 15
    // loaded as a qword with a zero high dword (FILD) and stored as float; pushed with
    // FUN_06f7ae20 at the end of +0x28.
    void PushDefaultSpawnPoint(FRandEncParams* Params, float X, float Z, float Weight)
    {
        int const Y = std::rand() * 0xc9 / 32768;
        FRandEncPosition Point;
        Point.Position.x = X;
        Point.Position.y = static_cast<float>(static_cast<long long>(static_cast<unsigned int>(Y)));
        Point.Position.z = Z;
        Point.Weight = Weight;
        Params->Positions.push_back(Point);
    }

    // The stretch of the lane the player travelled, inlined twice in the TL Accept
    // (06f81ff3-06f82125 and 06f82224-06f82333): the first ring equal to +0xb0 (the exit)
    // gets pub::SpaceObj::GetTradelaneNextAndPrev (the result is not tested; retail's two
    // out locals are uninitialised, the port's start at 0). Its "next" being +0xac (the
    // entry) means travel towards the lane's start: +0xa8 = -1 and the rings before the exit
    // stay (copy/erase FUN_06f6a380 + FUN_06ea1490), without the first; otherwise +0xa8 = 1
    // and the rings from the exit on stay, without the last (FUN_06f83f60). The second
    // erase needs a non-empty vector. No exit ring on the lane: nothing changes.
    void TrimTravelledRings(FRandEncTLParams* Lane, std::vector<FTradeLaneRing>* Rings)
    {
        std::vector<FTradeLaneRing>::iterator Exit = Rings->begin();
        while (Exit != Rings->end() && Exit->ObjectId != Lane->ExitRing)
            ++Exit;
        if (Exit == Rings->end())
            return;
        unsigned int Next = 0;
        unsigned int Prev = 0;
        pub::SpaceObj::GetTradelaneNextAndPrev(Exit->ObjectId, Next, Prev);
        if (Next == Lane->EntryRing)
        {
            Lane->Direction = -1;
            Rings->erase(Exit, Rings->end());
            if (!Rings->empty())
                Rings->erase(Rings->begin());
        }
        else
        {
            Lane->Direction = 1;
            Rings->erase(Rings->begin(), Exit);
            if (!Rings->empty())
                Rings->erase(Rings->end() - 1);
        }
    }

    // One default spawn point of the TL Accept (06f82345-06f82541): x as given, y 0, z =
    // Direction * ZScale (IMUL, FILD, FSTP float); pushed with FUN_06f7ae20.
    void PushLaneSpawnPoint(FRandEncTLParams* Lane, float X, int ZScale, float Weight)
    {
        FRandEncPosition Point;
        Point.Position.x = X;
        Point.Position.y = 0.0f;
        Point.Position.z = static_cast<float>(Lane->Direction * ZScale);
        Point.Weight = Weight;
        Lane->Positions.push_back(Point);
    }

    // The attacker filter of the SP Accept (06f80c95-06f80ced / 06f80d49-06f80da0): a copy
    // of Populated (FUN_06f53080), Allowed cleared (FUN_06fa8370 over [begin, end)), then
    // every pair whose group is hostile to Vibe (GetVibeFeelingMask & 4) is appended
    // (FUN_06f81960); the copy is destroyed (FUN_06f15350).
    void CollectHostilePairs(std::list<std::pair<unsigned int, float>> const* Populated,
                             int Vibe, std::list<std::pair<unsigned int, float>>* Allowed)
    {
        std::list<std::pair<unsigned int, float>> Copy(*Populated);
        Allowed->clear();
        for (std::list<std::pair<unsigned int, float>>::iterator It = Copy.begin();
             It != Copy.end(); ++It)
        {
            if ((GetVibeFeelingMask(It->first, Vibe) & 4) != 0)
                Allowed->push_back(*It);
        }
    }

    // The formation values shared by both parses (SP "formation" 06f81365-06f8163e, TL
    // "formation" / "saved_formation" 06f82ece-06f8323a); the caller has set the nickname id
    // and the formation (FUN_06efd1a0). Value 1 float, values 2 / 3 the ship count range, then
    // from value 4 pairs of (ship class name, weight) while is_value_empty is false: a known,
    // non-empty ship class (FUN_06f58080 on CreateID of the name) adds a choice {its members,
    // weight}. SP copies the name through a 0x40 string first (FUN_06ea9f40); TL skips a pair
    // whose weight value is empty. The copy pushed into +0x44 (FUN_06f79c40 / FUN_06f83fb0 +
    // FUN_06f7d6d0) gets +0x24 = value 1; the local's inner list is then erased.
    void ParseRandEncFormationValues(INI_Reader* Reader, FRandEncParams* Params,
                                     FRandEncFormation* Formation, bool bLaneRules)
    {
        Formation->Field_0x08 = Reader->get_value_float(1);
        Formation->MinCount = Reader->get_value_int(2);
        Formation->MaxCount = Reader->get_value_int(3);
        for (unsigned int Index = 4; !Reader->is_value_empty(Index); Index += 2)
        {
            unsigned int ClassId = 0;
            if (bLaneRules)
            {
                ClassId = CreateID(Reader->get_value_string(Index));
            }
            else
            {
                TString<0x40> ClassName;
                ClassName = Reader->get_value_string(Index);
                ClassId = CreateID(ClassName.Data);
            }
            std::vector<unsigned int>* const Members = FindShipClass(&ClassId);
            if (Members == nullptr || Members->empty())
                continue;
            if (bLaneRules && Reader->is_value_empty(Index + 1))
                continue;
            FRandEncShipChoice Choice;
            Choice.Weight = Reader->get_value_float(Index + 1);
            Choice.ShipClasses = *Members;
            Formation->ShipChoices.push_back(Choice);
        }
        FRandEncFormation Copy(*Formation);
        Copy.Weight = Formation->Field_0x08;
        Params->Formations.push_back(Copy);
    }

    // The "num_forms" line of both dumps (06f81e47-06f81e68, 06f81083-06f810a4): only with a
    // minimum; a zero maximum becomes the number of formations first.
    void DumpRandEncNumForms(FILE* File, FRandEncParams* Params)
    {
        if (Params->NumFormsMin == 0)
            return;
        if (Params->NumFormsMax == 0)
            Params->NumFormsMax = static_cast<int>(Params->Formations.size());
        std::fprintf(File, "num_forms = %d, %d\n", Params->NumFormsMin, Params->NumFormsMax);
    }
}

// LAB_06f5e540: MOV EAX,3.
int FRandEncTLParams::GetType()
{
    return 3;
}

// FUN_06f83af0 (06f83af0-06f83eaf, RET 4): pub::SpaceObj::DrainShields on the ring (+0xb4,
// result not read), then with bForced the base body (06f83b2d). Otherwise: +0x9c = 0, the
// location of +0x88 (result not read), a copy of the spawn points (FUN_06efc7d0), the turn
// matrix of DAT_06fb7b40, an identity placement (FUN_06efc7a0) and one "Delay" directive
// (factory type 1) of 25 s with Op +0x08 = 1 in a local list with its own reference. Per
// formation to spawn: a weighted point of the copy (RandEncPickWeighted) turned by the
// ring's orientation (FUN_06f83ec0) and added to its position (FUN_06f0bb80) gives the
// placement's translation; the point leaves the copy; the rotation is turn * orientation
// (FUN_06f83f10); SpawnLaneFormation(-1, the list, null, null). Afterwards +0x38 = 0, the
// directive holder is released, the list torn down (FUN_06ed2a00), the copy freed.
void FRandEncTLParams::Spawn(bool bForced)
{
    pub::SpaceObj::DrainShields(TargetTradelane);
    if (bForced)
    {
        FRandEncParams::Spawn(true);
        return;
    }

    // 06f83b37-06f83b69. Retail leaves the location unset when GetLocation fails; the port
    // starts it at zero.
    Vector TargetPosition = {0.0f, 0.0f, 0.0f};
    Matrix TargetRotation;
    std::memset(&TargetRotation, 0, sizeof(TargetRotation));
    ShipNameCounter = 0;
    pub::SpaceObj::GetLocation(TargetObjectId, TargetPosition, TargetRotation);
    std::list<FRandEncPosition> FreePositions(Positions);
    FMatrix TargetMatrix;
    static_assert(sizeof(Matrix) == sizeof(FMatrix), "Matrix is 3x3 floats");
    std::memcpy(&TargetMatrix, &TargetRotation, sizeof(FMatrix));

    // 06f83b6e-06f83bb1: rows {cos, 0, sin}, {0, 1, 0}, {-sin, 0, cos}.
    float const TurnCos = static_cast<float>(std::cos(GRandEncLaneTurnAngle));
    float const TurnSin = static_cast<float>(std::sin(GRandEncLaneTurnAngle));
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

    // 06f83bb5-06f83bd1
    FTransform Placement;
    SetMatrixIdentity(&Placement.Rotation);
    Placement.Translation.X = 0.0f;
    Placement.Translation.Y = 0.0f;
    Placement.Translation.Z = 0.0f;

    // 06f83bba-06f83c43: the list, then the directive holder (FUN_06f28e10 on it while still
    // empty, a no-op).
    FMissionDirectiveList LeaderDirectives;
    TRefHandle<FObjListDirective> Leader;
    CreateObjListDirective(1, &Leader.Pointer);
    FObjListDelay* const Delay = static_cast<FObjListDelay*>(Leader.Pointer);
    Delay->SetDelay(25.0f);                                                // FUN_06f27940
    Delay->Op.Priority = 1;                                                // +0x114
    LeaderDirectives.Items.push_back(Leader.Pointer);                      // FUN_06f1e390
    Leader->AddRef();

    for (int Index = 0; Index < FormationsToSpawn; ++Index)
    {
        // 06f83c53-06f83d37. With nothing picked (an empty copy or a zero sum) retail goes on
        // with the list head's uninitialised value and frees the head itself (undefined);
        // the port skips that formation.
        std::list<FRandEncPosition>::iterator const Picked =
            RandEncPickWeighted(&FreePositions, &FRandEncPosition::Weight);
        if (Picked == FreePositions.end())
            continue;

        // 06f83d39-06f83d93
        FVector PointOffset;
        FVector const Point = {Picked->Position.x, Picked->Position.y, Picked->Position.z};
        TransformByMatrix(&PointOffset, &TargetMatrix, &Point);
        Placement.Translation.X = TargetPosition.x + PointOffset.X;       // FUN_06f0bb80
        Placement.Translation.Y = TargetPosition.y + PointOffset.Y;
        Placement.Translation.Z = TargetPosition.z + PointOffset.Z;

        // 06f83d82-06f83dcb: the node unlinked and freed, size--.
        FreePositions.erase(Picked);

        // 06f83dbf-06f83e0a
        FMatrix Rotation;
        MultiplyMatrices(&Rotation, &Turn, &TargetMatrix);
        Placement.Rotation = Rotation;
        SpawnLaneFormation(&Placement, -1, &LeaderDirectives.Items, nullptr, nullptr);
    }

    // 06f83e1e-06f83e36: +0x38 = 0, the holder released inline and cleared.
    FormationsToSpawn = 0;
    if (Leader.Pointer != nullptr)
    {
        Leader.Pointer->Release();
        Leader.Pointer = nullptr;
    }
}

// FUN_06f832b0 (06f832b0-06f83acd, RET 0x14).
bool FRandEncTLParams::SpawnLaneFormation(FTransform const* Placement, int Count,
                                          std::list<FObjListDirective*> const* Directives,
                                          std::list<FRandEncShipChoice> const* Choices,
                                          std::list<TString<0x30>>* Names)
{
    // 06f832d1-06f833b0. With nothing picked retail writes the count into the list head's
    // uninitialised value and spawns from it (undefined); the port creates nothing.
    std::list<FRandEncFormation>::iterator const Picked =
        RandEncPickWeighted(&Formations, &FRandEncFormation::Weight);
    if (Picked == Formations.end())
        return true;
    FRandEncFormation& Formation = *Picked;

    // 06f833b2-06f833e5: Count -1 -> min + (rand() * range) >> 15.
    if (Count == -1)
    {
        int const Min = Formation.MinCount;
        int const Max = Formation.MaxCount;
        Count = std::rand() * (Max - Min + 1) / 32768 + Min;
    }
    Formation.Count = Count;

    std::list<FContentNpcShipParams> Blocks;                               // FUN_06ef6b80
    for (int Index = 0; Index < Formation.Count; ++Index)
    {
        // 06f83418-06f8345e: the block (FUN_06f33500, EH state 1), then a name block at
        // [ESP+0xd8] (two FmtStr, +0x30 = 0, +0x34 = -1; EH states 2/3, dtor FUN_06ef4e60)
        // that is never read. It dies at the end of the iteration after the push_back
        // (06f837fa-06f8380b), or before the block on the no-arch skip (06f8384e-06f8385f).
        FContentNpcShipParams Params;
        FNpcNameParams UnusedName;

        // 06f8345e-06f834d3
        if (Names == nullptr)
        {
            char Nickname[0x60];
            std::sprintf(Nickname, "rand_enc_ship_%d", ShipNameCounter);
            ++ShipNameCounter;
            Params.Nickname = Nickname;                                    // FUN_06ed0160
        }
        else
        {
            Params.Nickname = Names->front().Data;
            Names->pop_front();                                            // FUN_06ecfe80
        }

        // 06f834d8-06f83552: controller, placement, +0x104 = DAT_06fbc18c (24.0f), labels.
        Params.ControllerId = ControllerId;
        std::memcpy(&Params.Orientation, &Placement->Rotation, sizeof(FMatrix));
        Params.Position.x = Placement->Translation.X;
        Params.Position.y = Placement->Translation.Y;
        Params.Position.z = Placement->Translation.Z;
        Params.Field_0x104 = 24.0f;
        for (std::list<unsigned int>::iterator Label = Labels.begin(); Label != Labels.end();
             ++Label)
            Params.Field_0x118.push_back(*Label);                          // FUN_06f1e390

        // 06f83554-06f83645: the ship classes -- a weighted ship choice (retail reads the
        // list head's value when nothing is picked; the port leaves them empty) or Choices'
        // Index-th element -- passed straight to FUN_06f35890 (only its classes are read).
        FEncounterShip Line;
        if (Choices == nullptr)
        {
            std::list<FRandEncShipChoice>::iterator const Choice =
                RandEncPickWeighted(&Formation.ShipChoices, &FRandEncShipChoice::Weight);
            if (Choice != Formation.ShipChoices.end())
                Line.ShipClasses = Choice->ShipClasses;
        }
        else
        {
            std::list<FRandEncShipChoice>::const_iterator Choice = Choices->begin();
            for (int Step = 0; Step < Index; ++Step)
                ++Choice;
            Line.ShipClasses = Choice->ShipClasses;
        }

        // 06f83648-06f8365b: no arch -> this ship is skipped (06f83847).
        FNpcShipArch* const Arch =
            PickFactionShipLineArch(static_cast<unsigned int>(AttackerRep), &Line, -1);
        if (Arch == nullptr)
            continue;

        // 06f83661-06f8373c: a named pilot of the attacker faction; the arch with the
        // encounter's pilot (+0x18); the job's scan chance 1.0 (+0x4f4); the name reputation.
        // The name block [ESP+0xa0] (EH states 4/5) dies right after the level store, before
        // the leader directives (06f83756-06f83767), hence its own scope.
        {
            FNpcNameParams Name;
            PickNpcNameGender(static_cast<unsigned int>(AttackerRep), &Name);  // FUN_06f319b0
            FNpcNameRequest Request;                                           // FUN_06f31650
            if (BuildNpcName(static_cast<unsigned int>(AttackerRep), &Request, &Name, true))
            {
                ApplyNpcShipFaction(static_cast<unsigned int>(AttackerRep), Name.Gender, &Params);
                ApplyNpcShipArch(&Params, Arch, &PilotId, nullptr);
                Params.Personality.JobParams.ScanChance = 1.0f;
                if (Params.ReputationHandle != nullptr)
                {
                    Params.ReputationHandle->Release();                        // FUN_06f28e10
                    Params.ReputationHandle = nullptr;
                }
                CreateNpcNameReputation(&Name, &Params.ReputationHandle);      // FUN_06f31910
            }
            Params.Level = Arch->Level;                                        // arch +0x24
        }

        // 06f83771-06f837d6: the leader gets the directives (FUN_06ed2a00 on the fresh block,
        // the assign FUN_06f93490, slot 0 AddRef on each).
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

    // 06f838d3-06f83953: a random segment of the ring zone's path (FUN_06fa8130,
    // FUN_06f37ea0; its result is not read) and the placement plan (defaults FUN_06f83ad0).
    // Retail indexes the segments without an emptiness test and the first block without
    // one either.
    FPatrolPath* const Path = TargetZone->GetPath();
    std::vector<FPatrolPathSegment> Segments;
    Path->GetZoneSegments(TargetZone->ZoneId, &Segments);
    int const SegmentCount = static_cast<int>(Segments.size());
    FPatrolPathSegment const& Segment = Segments[std::rand() * SegmentCount / 32768];
    FSpawnPlan Plan;
    Plan.Kind = 0;
    Plan.RingId = 0;
    Plan.NextRingId = 0;
    Plan.DockTarget = 0;
    Plan.Path = Path;

    // 06f8395f-06f83996: the direction (rand() * 2) >> 15 -> 0: -1, 1: +1 (NEG / SBB / AND 2 /
    // DEC), and the point it heads for.
    Plan.PathDirection = ((std::rand() * 2 / 32768) != 0) ? 1 : -1;
    if (Plan.PathDirection == 1)
        Plan.PathTargetPoint = Segment.SecondPoint;
    else
        Plan.PathTargetPoint = Segment.FirstPoint;

    // 06f8399d-06f83a26: a new type 3 guide description (FUN_06f06200) held by the first
    // block (its own reference from new is never dropped), the plan copied into it
    // (REP MOVSD 0xe) and the ring's zone.
    FContentNpcShipParams& Leader = Blocks.front();
    FGuideDesc* const Desc = new FGuideDesc(3, nullptr);
    if (Leader.GuideDesc.Pointer != Desc)
    {
        if (Leader.GuideDesc.Pointer != nullptr)
        {
            Leader.GuideDesc.Pointer->Release();
            Leader.GuideDesc.Pointer = nullptr;
        }
        Leader.GuideDesc.Pointer = Desc;
        if (Desc != nullptr)
            Desc->AddRef();
    }
    static_cast<FGuideDesc*>(Leader.GuideDesc.Pointer)->Plan = Plan;
    static_cast<FGuideDesc*>(Leader.GuideDesc.Pointer)->WanderZone = &TargetZone->ZoneId;

    // 06f83a29-06f83a38: TU53 FUN_06f63cc0(System (+0x8), &Desc->Plan (+0x10), the leader
    // block) -- the two trade targets of the leader's guide.
    SetLeaderGuideTargets(System, &static_cast<FGuideDesc*>(Leader.GuideDesc.Pointer)->Plan,
                          &Leader);

    // 06f83a3d-06f83a66
    PlaceFormationBlocks(&Blocks, Formation.Formation, Placement);
    System->CreateNpcShips(&Blocks, 0, nullptr);
    return true;
}

// FUN_06f81d50 (06f81d50-06f81ec4, RET 4).
void FRandEncTLParams::Dump(FILE* File)
{
    std::fprintf(File, "encounter_type = %s\n", "TL_Attack");
    std::fprintf(File, "tl_attack_chance = %d\n", TlAttackChance);
    std::fprintf(File, "tl_attack_chance_read_in = %d\n", TlAttackChanceReadIn);
    if (!Formations.empty())
        DumpRandEncFormations(File, this, "saved_formation = %u, %f, %d, %d", true);
    DumpRandEncNumForms(File, this);
    if (TargetTradelane != 0)
        std::fprintf(File, "target_tradelane = %u\n", TargetTradelane);
    if (TargetShipName.Length != 0)
        std::fprintf(File, "target_ship_name = %s\n", TargetShipName.Data);
    if (AttackerRep != -1)
        std::fprintf(File, "attacker_rep = %d\n", AttackerRep);
    if (PilotId.Id != 0)
        std::fprintf(File, "pilot_id = %u\n", PilotId.Id);
}

// LAB_06ee9660: MOV EAX,2.
int FRandEncSPParams::GetType()
{
    return 2;
}

// FUN_06f80f10 (06f80f10-06f8111b, RET 4). "time" prints +0xb8 before +0xb4.
void FRandEncSPParams::Dump(FILE* File)
{
    std::fprintf(File, "encounter_type = %s\n", "SP_Attack");
    if (ActivationType == 2)
        std::fprintf(File, "activation_type = time, %f, %f\n",
                     static_cast<double>(ActivationTime[1]), static_cast<double>(ActivationTime[0]));
    else if (ActivationType == 1)
        std::fprintf(File, "activation_type = location, %f, %f, %f, %d\n",
                     static_cast<double>(ActivationLocation.x),
                     static_cast<double>(ActivationLocation.y),
                     static_cast<double>(ActivationLocation.z), ActivationRange);
    else
        std::fprintf(File, "activation_type = time_cumulative, %f\n",
                     static_cast<double>(CumulativeTime));
    if (TargetShipName.Length != 0)
        std::fprintf(File, "target_ship_name = %s\n", TargetShipName.Data);
    if (!Formations.empty())
        DumpRandEncFormations(File, this, "formation = %s, %f, %d, %d", false);
    DumpRandEncNumForms(File, this);
    for (std::list<FRandEncPosition>::iterator It = Positions.begin(); It != Positions.end(); ++It)
        std::fprintf(File, "formation_position = %f, %f, %f\n",
                     static_cast<double>(It->Position.x), static_cast<double>(It->Position.y),
                     static_cast<double>(It->Position.z));
    if (AttackerRep != -1)
        std::fprintf(File, "attacker_rep = %d\n", AttackerRep);
    if (PilotId.Id != 0)
        std::fprintf(File, "pilot_id = %u\n", PilotId.Id);
}

// FUN_06f81ed0 (06f81ed0-06f82b7a, RET 0xc). The result is the local byte [ESP+0x1b]; only
// message 0x20 ever sets it.
bool FRandEncTLPopulator::Accept(FRandEncParams* Params, pub::Controller::EMessage MessageId,
                                 void const* MessageParms)
{
    FRandEncTLParams* const Lane = static_cast<FRandEncTLParams*>(Params);
    bool bTrigger = false;

    if (MessageId == pub::Controller::EMessage::SpaceExit)
    {
        // 06f81f04-06f81f11: slot 5 through the vtable.
        Reset(Params);
        return bTrigger;
    }

    if (MessageId == pub::Controller::EMessage::TradelaneEnter)
    {
        // A ship entered a trade lane: {ship, entry ring, exit ring, system}.
        if (bAttackArmed)
            return bTrigger;
        unsigned int const* const Parms = static_cast<unsigned int const*>(MessageParms);
        std::vector<FTradeLaneRing> Rings;                  // [ESP+0x1c], FUN_06ead6a0

        // 06f81f38-06f81f64: only the encounter player's ship.
        FContentPlayerShip* const Ship = Lane->Player->ShipObject;
        if (Ship == nullptr || Parms[0] != Ship->SpaceObjId)
            return bTrigger;

        if (Lane->TargetTradelane == 0)
        {
            // 06f81f7e-06f81fa6: the attack chance, rand() * 101 >> 15 against +0xa0; a
            // failed roll raises the chance by 15.
            int const Roll = std::rand() * 0x65 / 32768;
            if (Roll > Lane->TlAttackChance)
            {
                Lane->TlAttackChance += 15;
                return bTrigger;
            }
            // 06f81fab-06f81fd6
            Lane->EntryRing = Parms[1];
            Lane->ExitRing = Parms[2];
            FSystemInfo* const System = FindSystemInfo(&Parms[3]);
            Lane->System = System;
            System->GetTradeLaneRingsByRing(&Lane->EntryRing, &Rings);     // FUN_06f77bd0
            TrimTravelledRings(Lane, &Rings);

            // 06f82129-06f821d1: a random ring of the stretch (FUN_06f2fb70 is the size; its
            // zero branch 06f8215b is unreachable after the emptiness test).
            if (Rings.empty())
                return bTrigger;
            int const Count = static_cast<int>(Rings.size());
            int Index = 0;
            if (Count - 1 != 0)
                Index = std::rand() * Count / 32768;
            Lane->TargetTradelane = Rings[Index].ObjectId;
            Lane->TargetObjectId = Lane->TargetTradelane;
        }
        else
        {
            // 06f821d6-06f82333: a scripted ring keeps its id; the direction is still derived
            // from the lane of that ring (the trimmed rings are not read afterwards).
            Lane->TargetObjectId = Lane->TargetTradelane;
            Lane->EntryRing = Parms[1];
            Lane->ExitRing = Parms[2];
            FSystemInfo* const System = FindSystemInfo(&Parms[3]);
            Lane->System = System;
            System->GetTradeLaneRingsByRing(&Lane->TargetTradelane, &Rings);
            TrimTravelledRings(Lane, &Rings);
        }

        // 06f82338-06f82541: six default spawn points along the travel direction.
        if (Lane->Positions.size() == 0)
        {
            PushLaneSpawnPoint(Lane, 0.0f, 3000, 80.0f);
            PushLaneSpawnPoint(Lane, 1250.0f, 4000, 2.5f);
            PushLaneSpawnPoint(Lane, -1250.0f, 4000, 2.5f);
            PushLaneSpawnPoint(Lane, 2000.0f, 5000, 5.0f);
            PushLaneSpawnPoint(Lane, -2000.0f, 5000, 5.0f);
            PushLaneSpawnPoint(Lane, 0.0f, 6750, 5.0f);
        }

        // 06f82546-06f825a2
        if (Lane->PilotId.IsEmpty())
            Lane->PilotId.Id = CreateID("pilot_0");
        if (Lane->FormationsToSpawn == 0)
        {
            if (Lane->NumFormsMin == 0)
                Lane->NumFormsMin = 1;
            if (Lane->NumFormsMax == 0)
                Lane->NumFormsMax = 3;
            int const Max = Lane->NumFormsMax;
            int const Min = Lane->NumFormsMin;
            Lane->FormationsToSpawn = std::rand() * (Max - Min + 1) / 32768 + Min;
        }

        if (Lane->AttackerRep == -1)
        {
            // 06f825b3-06f82610: the zone at the attacked ring (GetLocation's result is not
            // tested) must be populated.
            Vector Position;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(Lane->TargetTradelane, Position, Orientation);
            Lane->TargetZone = Lane->System->FindSmallestZoneAt(&Position);
            if (Lane->TargetZone == nullptr)
                return false;
            weighted_vector<unsigned int> const* const Population =
                Lane->TargetZone->GetPopulationFactions();
            if (Population == nullptr)
                return false;

            // 06f82615-06f827db: per formation to spawn, a weighted formation; the factions
            // flying any of its ship classes (GetNpcClassFactions), each once (FUN_06eb5f30 +
            // FUN_06eb5770).
            std::vector<unsigned int> ClassFactions;
            for (int Index = 0; Index < Lane->FormationsToSpawn; ++Index)
            {
                std::list<FRandEncFormation>::iterator const Formation =
                    RandEncPickWeighted(&Lane->Formations, &FRandEncFormation::Weight);
                if (Formation == Lane->Formations.end())
                    continue;
                for (std::list<FRandEncShipChoice>::iterator Choice =
                         Formation->ShipChoices.begin();
                     Choice != Formation->ShipChoices.end(); ++Choice)
                {
                    std::vector<unsigned int> Factions;
                    GetNpcClassFactions(&Choice->ShipClasses, &Factions);
                    for (std::vector<unsigned int>::iterator It = Factions.begin();
                         It != Factions.end(); ++It)
                    {
                        if (std::find(ClassFactions.begin(), ClassFactions.end(), *It) ==
                            ClassFactions.end())
                            ClassFactions.insert(ClassFactions.end(), *It);
                    }
                }
            }

            // 06f827e7-06f82870: the zone's population pairs of those factions.
            std::list<std::pair<unsigned int, float>> Populated;    // [ESP+0x78]
            std::list<std::pair<unsigned int, float>> Allowed;      // [ESP+0x2c]
            {
                weighted_vector<unsigned int> const PopulationCopy(*Population);
                for (std::vector<std::pair<unsigned int, float>>::const_iterator It =
                         PopulationCopy.Items.begin();
                     It != PopulationCopy.Items.end(); ++It)
                {
                    if (std::find(ClassFactions.begin(), ClassFactions.end(), It->first) !=
                        ClassFactions.end())
                        Populated.push_back(*It);
                }
            }

            // 06f82875-06f828fb: those hostile to the player (FUN_06f33f80).
            int const Vibe = Lane->Player->GetReputation();
            CollectHostilePairs(&Populated, Vibe, &Allowed);

            // 06f82900-06f82a60: none -> the ring is dropped (+0xb4 = 0) and nothing is
            // armed; else a random attacker and the ship choices it cannot fly erased.
            if (Allowed.size() == 0)
            {
                Lane->TargetTradelane = 0;
                return false;
            }
            int const Count = static_cast<int>(Allowed.size());
            int Pick = 0;
            if (Count - 1 != 0)
                Pick = std::rand() * Count / 32768;
            std::list<std::pair<unsigned int, float>>::iterator Picked = Allowed.begin();
            for (unsigned int Step = static_cast<unsigned int>(Pick); Step != 0; --Step)
                ++Picked;
            Lane->AttackerRep = static_cast<int>(Picked->first);

            for (std::list<FRandEncFormation>::iterator Formation = Lane->Formations.begin();
                 Formation != Lane->Formations.end(); ++Formation)
            {
                std::list<FRandEncShipChoice>::iterator Choice = Formation->ShipChoices.begin();
                while (Choice != Formation->ShipChoices.end())
                {
                    if (FactionHasNpcClass(static_cast<unsigned int>(Lane->AttackerRep),
                                           &Choice->ShipClasses))
                        ++Choice;
                    else
                        Choice = Formation->ShipChoices.erase(Choice);  // FUN_06f81940 + free
                }
            }
        }

        // 06f829f2-06f82a27
        bAttackArmed = true;
        return false;
    }

    if (MessageId == pub::Controller::EMessage::Heartbeat)
    {
        // 06f82ab9-06f82af4: armed -> fire once the player's ship is within 10000 of the ring:
        // the squared distance (FUN_06f242e0, -1.0 when a location fails) through ftol (the
        // low dword of the truncated 64-bit value) against 100000000 (JG, signed).
        if (bAttackArmed)
        {
            unsigned int ShipId = Lane->Player->ShipObject->SpaceObjId;
            double const DistanceSq = GetObjectDistanceSq(&Lane->TargetTradelane, &ShipId);
            int const Truncated = static_cast<int>(
                static_cast<unsigned int>(static_cast<long long>(DistanceSq)));
            if (Truncated <= 100000000)
                bTrigger = true;
        }
        // 06f82af9-06f82b40: without a target (+0x84 masked with 0x3fffffff) the named ship
        // (FindObjectIdByName on +8 with the controller id) or else the player's ship.
        if ((Lane->GotoTargetId & 0x3fffffff) == 0)
        {
            if (Lane->TargetShipName.Length == 0)
            {
                Lane->GotoTargetId = Lane->Player->ShipObject->SpaceObjId;
            }
            else
            {
                unsigned int const TargetId =
                    Lane->System->FindObjectIdByName(&Lane->TargetShipName, Lane->ControllerId);
                if (TargetId != 0)
                    Lane->GotoTargetId = TargetId;
                else
                    Lane->GotoTargetId = Lane->Player->ShipObject->SpaceObjId;
            }
        }
        return bTrigger;
    }

    if (MessageId == pub::Controller::EMessage::SystemEnter)
    {
        // 06f82b42-06f82b5d: JG 0x32 skips.
        if (Lane->TlAttackChance <= 0x32)
            Lane->TlAttackChance = Lane->TlAttackChanceReadIn;
    }
    return bTrigger;
}

// FUN_06f82b90 (06f82b90-06f83276, RET 8). Same frame as the SP parse (state saved and
// restored, [RandomEncounter] / [MsnRandEnc] with a value), with the lane keys.
void FRandEncTLPopulator::Parse(INI_Reader* Reader, FRandEncParams* Params)
{
    FRandEncTLParams* const Lane = static_cast<FRandEncTLParams*>(Params);
    INI_Reader::State Saved;
    Reader->get_state(Saved);
    if ((Reader->is_header("RandomEncounter") || Reader->is_header("MsnRandEnc")) &&
        Reader->read_value())
    {
        do
        {
            if (Reader->is_value("target_tradelane_name"))
            {
                Lane->TargetTradelane = pub::GetNicknameId(Reader->get_value_string(0));
            }
            else if (Reader->is_value("target_tradelane"))
            {
                Lane->TargetTradelane = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("target_ship_name"))
            {
                Lane->TargetShipName = Reader->get_value_string(0);         // FUN_06ea9f40 0x30
            }
            else if (Reader->is_value("attacker_rep_name"))
            {
                unsigned int Group = static_cast<unsigned int>(Lane->AttackerRep);
                pub::Reputation::GetReputationGroup(Group, Reader->get_value_string(0));
                Lane->AttackerRep = static_cast<int>(Group);
            }
            else if (Reader->is_value("attacker_rep"))
            {
                Lane->AttackerRep = Reader->get_value_int(0);
            }
            else if (Reader->is_value("pilot"))
            {
                Lane->PilotId.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("pilot_id"))
            {
                Lane->PilotId.Id = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("tl_attack_chance"))
            {
                // 06f82d33-06f82d47: both chances from value 0.
                Lane->TlAttackChanceReadIn = Reader->get_value_int(0);
                Lane->TlAttackChance = Reader->get_value_int(0);
            }
            else if (Reader->is_value("tl_attack_chance_read_in"))
            {
                Lane->TlAttackChanceReadIn = Reader->get_value_int(0);
            }
            else if (Reader->is_value("formation") || Reader->is_value("saved_formation"))
            {
                // 06f82ece-06f830e5: "formation" names both by value 0; "saved_formation" gives
                // the ids as numbers (value 0 for both).
                FRandEncFormation Formation;
                Formation.Nickname.clear();
                if (Reader->is_value("formation"))
                {
                    Formation.Nickname.Id = CreateID(Reader->get_value_string(0));
                    unsigned int const FormationId = CreateID(Reader->get_value_string(0));
                    Formation.Formation = FindFormation(&FormationId);
                }
                else
                {
                    Formation.Nickname.Id = static_cast<unsigned int>(Reader->get_value_int(0));
                    unsigned int const FormationId =
                        static_cast<unsigned int>(Reader->get_value_int(0));
                    Formation.Formation = FindFormation(&FormationId);
                }
                ParseRandEncFormationValues(Reader, Lane, &Formation, true);
            }
            else if (Reader->is_value("orientation"))
            {
                Lane->SpawnAngle = static_cast<float>(
                    static_cast<double>(Reader->get_value_float(0)) * GRandEncDegToRad);
                if (!Reader->is_value_empty(1))
                    Lane->SpawnDistance = Reader->get_value_float(1);
            }
            else if (Reader->is_value("radius"))
            {
                Lane->EmptyPosRadius = static_cast<int>(Reader->get_value_float(0));    // ftol
            }
            else if (Reader->is_value("label"))
            {
                Lane->Labels.push_back(CreateID(Reader->get_value_string(0)));
            }
            else if (Reader->is_value("num_forms"))
            {
                Lane->NumFormsMin = Reader->get_value_int(0);
                Lane->NumFormsMax = Reader->get_value_int(1);
                Lane->FormationsToSpawn =
                    std::rand() * (Lane->NumFormsMax - Lane->NumFormsMin + 1) / 32768 +
                    Lane->NumFormsMin;
            }
        } while (Reader->read_value());
    }
    Reader->set_state(Saved);
}

// LAB_06f83280 (RET 4): +8 = 0; Params +0x84 = 0, +0xa0 = +0xa4, +0xb4 = 0.
void FRandEncTLPopulator::Reset(FRandEncParams* Params)
{
    FRandEncTLParams* const Lane = static_cast<FRandEncTLParams*>(Params);
    bAttackArmed = false;
    Lane->GotoTargetId = 0;
    Lane->TlAttackChance = Lane->TlAttackChanceReadIn;
    Lane->TargetTradelane = 0;
}

// FUN_06f80680 (06f80680-06f80f04, RET 0xc). The result is the local byte [ESP+0x13].
bool FRandEncSPPopulator::Accept(FRandEncParams* Params, pub::Controller::EMessage MessageId,
                                 void const* MessageParms)
{
    FRandEncSPParams* const Space = static_cast<FRandEncSPParams*>(Params);
    bool bTrigger = false;

    if (MessageId != pub::Controller::EMessage::Heartbeat)
    {
        // 06f806b6-06f806ce: 0x37 -> slot 5 through the vtable.
        if (MessageId == pub::Controller::EMessage::SpaceExit)
            Reset(Params);
        return bTrigger;
    }

    // 06f806d3-06f80714: a player with a ship object and a system; the ship's HasField120
    // (FUN_06f57710) stops the encounter.
    FContentPlayerState* const Player = Space->Player;
    if (Player == nullptr)
        return bTrigger;
    FContentPlayerShip* const Ship = Player->ShipObject;
    FSystemInfo* const PlayerSystem = Player->GetSystemRecord();
    if (Player->ShipObject == nullptr || PlayerSystem == nullptr || Ship == nullptr)
        return bTrigger;
    if (Ship->HasField120())
        return bTrigger;

    // 06f8071a-06f80766: +8 = the player's system; without a target (+0x84, masked with
    // 0x3fffffff) the named ship (FindObjectIdByName with the controller id) or else the
    // player's ship becomes both +0x84 and +0x88.
    FSystemInfo* const System = Player->GetSystemRecord();
    Space->System = System;
    if ((Space->GotoTargetId & 0x3fffffff) == 0)
    {
        unsigned int TargetId;
        if (Space->TargetShipName.Length == 0)
        {
            TargetId = Player->ShipObject->SpaceObjId;
        }
        else
        {
            TargetId = System->FindObjectIdByName(&Space->TargetShipName, Space->ControllerId);
            if (TargetId == 0)
                TargetId = Player->ShipObject->SpaceObjId;
        }
        Space->GotoTargetId = TargetId;
        Space->TargetObjectId = TargetId;
    }

    // 06f8076c-06f80856: the activation. An unset type becomes "time".
    if (Space->ActivationType == 0)
        Space->ActivationType = 2;
    if (Space->ActivationType == 2)
    {
        // 06f80795-06f807ec: FCOMP DAT_06fb444c (0.0) + TEST AH,0x44 + JP -- only an exact 0
        // falls through and picks the threshold between the ftol'd bounds ([0] high, [1] low).
        if (Space->CumulativeTime == 0.0f)
        {
            int const High = static_cast<int>(Space->ActivationTime[0]);
            int const Low = static_cast<int>(Space->ActivationTime[1]);
            int const Picked = std::rand() * (High - Low + 1) / 32768 + Low;
            Space->CumulativeTime = static_cast<float>(
                static_cast<long long>(static_cast<unsigned int>(Picked)));
        }
        // 06f807f2-06f8081e: FCOMP + TEST AH,0x1 + JNZ -- below the threshold (or unordered)
        // the message's seconds are added; otherwise both are zeroed and the encounter fires.
        if (!(ElapsedTime >= Space->CumulativeTime))
        {
            ElapsedTime = ElapsedTime + *static_cast<float const*>(MessageParms);
        }
        else
        {
            Space->CumulativeTime = 0.0f;
            ElapsedTime = 0.0f;
            bTrigger = true;
        }
    }
    else if (Space->ActivationType == 1)
    {
        // 06f80825-06f80856: FILD range, FCOMPP + TEST AH,0x1 + JNZ -- fires unless the range
        // is below the squared distance (or unordered).
        double const DistanceSq =
            GetObjectDistanceSqTo(&Space->GotoTargetId, Space->ActivationLocation);
        if (static_cast<double>(Space->ActivationRange) >= DistanceSq)
            bTrigger = true;
    }

    // 06f8085d-06f808b7: without an attacker_rep the target's zone must be populated; this
    // runs whether or not the activation fired.
    weighted_vector<unsigned int> const* Population = nullptr;
    if (Space->AttackerRep == -1)
    {
        Vector Position;
        Matrix Orientation;
        if (pub::SpaceObj::GetLocation(Space->GotoTargetId, Position, Orientation) != 0)
            return false;
        Space->TargetZone = Space->System->FindSmallestZoneAt(&Position);
        if (Space->TargetZone == nullptr)
            return false;
        Population = Space->TargetZone->GetPopulationFactions();
        if (Population == nullptr)
            return false;
    }
    if (!bTrigger)
        return bTrigger;

    // 06f808c8-06f808e5
    if (Space->PilotId.IsEmpty())
        Space->PilotId.Id = CreateID("pilot_0");

    // 06f808e7-06f80924: the formation count, the range defaulting to 1..3.
    if (Space->FormationsToSpawn == 0)
    {
        if (Space->NumFormsMin == 0)
            Space->NumFormsMin = 1;
        if (Space->NumFormsMax == 0)
            Space->NumFormsMax = 3;
        int const Max = Space->NumFormsMax;
        int const Min = Space->NumFormsMin;
        Space->FormationsToSpawn = std::rand() * (Max - Min + 1) / 32768 + Min;
    }

    // 06f80929-06f80a89: three default spawn points behind the target.
    if (Space->Positions.size() == 0)
    {
        PushDefaultSpawnPoint(Space, 0.0f, -3000.0f, 80.0f);
        PushDefaultSpawnPoint(Space, 2000.0f, -3500.0f, 2.5f);
        PushDefaultSpawnPoint(Space, -2000.0f, -3500.0f, 2.5f);
    }

    if (Space->AttackerRep != -1)
        return bTrigger;

    // 06f80a98-06f80b7b: every faction that flies one of the ship classes of any formation
    // (GetNpcClassFactions per ship choice), each once (FUN_06eb5f30 find + FUN_06f937c0).
    std::vector<unsigned int> ClassFactions;
    for (std::list<FRandEncFormation>::iterator Formation = Space->Formations.begin();
         Formation != Space->Formations.end(); ++Formation)
    {
        for (std::list<FRandEncShipChoice>::iterator Choice = Formation->ShipChoices.begin();
             Choice != Formation->ShipChoices.end(); ++Choice)
        {
            std::vector<unsigned int> Factions;
            GetNpcClassFactions(&Choice->ShipClasses, &Factions);
            for (std::vector<unsigned int>::iterator It = Factions.begin(); It != Factions.end();
                 ++It)
            {
                if (std::find(ClassFactions.begin(), ClassFactions.end(), *It) ==
                    ClassFactions.end())
                    ClassFactions.insert(ClassFactions.end(), 1, *It);
            }
        }
    }

    // 06f80b81-06f80c36: the zone's population pairs whose faction is in that set, in
    // population order (a copy of the weighted vector is walked, FUN_06f1e440 appends).
    std::list<std::pair<unsigned int, float>> Populated;
    std::list<std::pair<unsigned int, float>> Allowed;
    {
        weighted_vector<unsigned int> const PopulationCopy(*Population);
        for (std::vector<std::pair<unsigned int, float>>::const_iterator It =
                 PopulationCopy.Items.begin();
             It != PopulationCopy.Items.end(); ++It)
        {
            if (std::find(ClassFactions.begin(), ClassFactions.end(), It->first) !=
                ClassFactions.end())
                Populated.push_back(*It);
        }
    }

    // 06f80c3b-06f80da2: those hostile to the target -- the player's reputation
    // (FUN_06f33f80) when the target is the player's ship, else pub::SpaceObj::GetRep (its
    // result is not tested; the port's GetRep writes 0 before any failure).
    if (Player->ShipObject->SpaceObjId == Space->GotoTargetId)
    {
        int const Vibe = Player->GetReputation();
        CollectHostilePairs(&Populated, Vibe, &Allowed);
    }
    else
    {
        int Vibe = 0;
        pub::SpaceObj::GetRep(Space->GotoTargetId, Vibe);
        CollectHostilePairs(&Populated, Vibe, &Allowed);
    }

    // 06f80db3-06f80e71: none -> no spawn; else a random one becomes the attacker and every
    // ship choice that faction cannot fly (FactionHasNpcClass) is erased from its formation.
    if (Allowed.size() == 0)
    {
        bTrigger = false;
        return bTrigger;
    }
    int const Count = static_cast<int>(Allowed.size());
    int Index = 0;
    if (Count - 1 != 0)
        Index = std::rand() * Count / 32768;
    std::list<std::pair<unsigned int, float>>::iterator Picked = Allowed.begin();
    for (unsigned int Step = static_cast<unsigned int>(Index); Step != 0; --Step)
        ++Picked;
    Space->AttackerRep = static_cast<int>(Picked->first);

    for (std::list<FRandEncFormation>::iterator Formation = Space->Formations.begin();
         Formation != Space->Formations.end(); ++Formation)
    {
        std::list<FRandEncShipChoice>::iterator Choice = Formation->ShipChoices.begin();
        while (Choice != Formation->ShipChoices.end())
        {
            if (FactionHasNpcClass(static_cast<unsigned int>(Space->AttackerRep),
                                   &Choice->ShipClasses))
                ++Choice;
            else
                Choice = Formation->ShipChoices.erase(Choice);    // FUN_06f81940 + free
        }
    }
    return bTrigger;
}

// FUN_06f81120 (06f81120-06f818c6, RET 8). The reader's state is saved first and restored at
// the end (get_state / set_state); only a [RandomEncounter] or [MsnRandEnc] section with a
// value is read.
void FRandEncSPPopulator::Parse(INI_Reader* Reader, FRandEncParams* Params)
{
    FRandEncSPParams* const Space = static_cast<FRandEncSPParams*>(Params);
    INI_Reader::State Saved;
    Reader->get_state(Saved);
    if ((Reader->is_header("RandomEncounter") || Reader->is_header("MsnRandEnc")) &&
        Reader->read_value())
    {
        do
        {
            if (Reader->is_value("label"))
            {
                Space->Labels.push_back(CreateID(Reader->get_value_string(0)));
            }
            else if (Reader->is_value("activation_type"))
            {
                // 06f811ee-06f8130f: only while no activation was read (+0xa0 == 0).
                char const* const Type = Reader->get_value_string(0);
                if (_stricmp(Type, "location") == 0)
                {
                    if (Space->ActivationType == 0)
                    {
                        Space->ActivationLocation.x = Reader->get_value_float(1);
                        Space->ActivationLocation.y = Reader->get_value_float(2);
                        Space->ActivationLocation.z = Reader->get_value_float(3);
                        double const Range = Reader->get_value_float(4);
                        Space->ActivationRange =
                            static_cast<int>(Reader->get_value_float(4) * Range);   // ftol
                        Space->ActivationType = 1;
                    }
                }
                else if (_stricmp(Type, "time") == 0 && Space->ActivationType == 0)
                {
                    // 06f8129f-06f81309: [1] = value 1, [0] = value 2; the start time is a
                    // (rand() * range) >> 15 pick between their truncations, loaded as a qword
                    // with a zero high dword (FILD).
                    Space->ActivationTime[1] = Reader->get_value_float(1);
                    Space->ActivationTime[0] = Reader->get_value_float(2);
                    int const High = static_cast<int>(Space->ActivationTime[0]);
                    int const Low = static_cast<int>(Space->ActivationTime[1]);
                    int const Picked = std::rand() * (High - Low + 1) / 32768 + Low;
                    Space->ActivationType = 2;
                    Space->CumulativeTime = static_cast<float>(
                        static_cast<long long>(static_cast<unsigned int>(Picked)));
                }
            }
            else if (Reader->is_value("target_ship_name"))
            {
                Space->TargetShipName = Reader->get_value_string(0);        // FUN_06ea9f40 0x30
            }
            else if (Reader->is_value("formation"))
            {
                // 06f81365-06f813d6: value 0 is both the nickname and the formation.
                FRandEncFormation Formation;
                Formation.Nickname.clear();
                Formation.Nickname.Id = CreateID(Reader->get_value_string(0));
                unsigned int const FormationId = CreateID(Reader->get_value_string(0));
                Formation.Formation = FindFormation(&FormationId);
                ParseRandEncFormationValues(Reader, Space, &Formation, false);
            }
            else if (Reader->is_value("formation_position"))
            {
                // 06f8165a-06f816e7: weight 1.0f.
                FRandEncPosition Position;
                Position.Position.x = Reader->get_value_float(0);
                Position.Position.y = Reader->get_value_float(1);
                Position.Position.z = Reader->get_value_float(2);
                Position.Weight = 1.0f;
                Space->Positions.push_back(Position);
            }
            else if (Reader->is_value("orientation"))
            {
                // 06f81700-06f8172f: degrees (DAT_06fb6060) and an optional distance.
                Space->SpawnAngle = static_cast<float>(
                    static_cast<double>(Reader->get_value_float(0)) * GRandEncDegToRad);
                if (!Reader->is_value_empty(1))
                    Space->SpawnDistance = Reader->get_value_float(1);
            }
            else if (Reader->is_value("radius"))
            {
                Space->EmptyPosRadius = static_cast<int>(Reader->get_value_float(0));   // ftol
            }
            else if (Reader->is_value("attacker_rep_name"))
            {
                unsigned int Group = static_cast<unsigned int>(Space->AttackerRep);
                pub::Reputation::GetReputationGroup(Group, Reader->get_value_string(0));
                Space->AttackerRep = static_cast<int>(Group);
            }
            else if (Reader->is_value("attacker_rep"))
            {
                Space->AttackerRep = Reader->get_value_int(0);
            }
            else if (Reader->is_value("pilot"))
            {
                Space->PilotId.Id = CreateID(Reader->get_value_string(0));
            }
            else if (Reader->is_value("pilot_id"))
            {
                Space->PilotId.Id = static_cast<unsigned int>(Reader->get_value_int(0));
            }
            else if (Reader->is_value("num_forms"))
            {
                // 06f81808-06f81844
                Space->NumFormsMin = Reader->get_value_int(0);
                Space->NumFormsMax = Reader->get_value_int(1);
                Space->FormationsToSpawn =
                    std::rand() * (Space->NumFormsMax - Space->NumFormsMin + 1) / 32768 +
                    Space->NumFormsMin;
            }
            else if (Reader->is_value("arrival_obj"))
            {
                // 06f81858-06f81888: a number as is, else a nickname.
                if (MsvcrtAtoi(Reader->get_value_string(0)) != 0)
                    Space->DockTarget = static_cast<unsigned int>(Reader->get_value_int(0));
                else
                    Space->DockTarget = pub::GetNicknameId(Reader->get_value_string(0));
            }
        } while (Reader->read_value());
    }
    Reader->set_state(Saved);
}

// LAB_06f818d0 (RET 4): Params +0x84 = 0, +0xbc = 0; +8 = 0.
void FRandEncSPPopulator::Reset(FRandEncParams* Params)
{
    FRandEncSPParams* const Space = static_cast<FRandEncSPParams*>(Params);
    Space->GotoTargetId = 0;
    Space->CumulativeTime = 0.0f;
    ElapsedTime = 0.0f;
}
