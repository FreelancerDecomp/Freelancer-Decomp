// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU44 (06f34ef0-06f36f10): faction / NPC ship arch / cargo helpers.
// Lane CNT-POP (docs/re/content.md, TU map).
//

#include "Cargo.h"

#include "BaseDB.h"             // FContentBase, FBaseCommodity (TU17)
#include "ContentPlayerState.h" // FContentPlayerStateBase::GetAffiliation (FUN_06eb6bb0)
#include "ContentStaticIds.h"   // TU44's 16 static-init thunks 06f34ef0-06f350d0
#include "EncounterParams.h"    // FEncounterShip
#include "FactionProps.h"       // GetFactionProps, GetFactionGroups (TU48)
#include "Guides.h"             // FGuideDesc (TU37)
#include "NpcShip.h"            // FContentNpcShipParams (TU43)
#include "NpcShips.h"           // FNpcShipArch, GNpcShipArchs, FindNpcShipArch (TU42)
#include "RandomMissionDB.h"    // FindShipClass, GRmShipMissionProperties (TU51)
#include "common_server.h"      // pub::Reputation::GetReputation, pub::GetCargoHoldSize
#include "server_cobject.h"     // pub::SpaceObj::CargoDesc

#include <cstdlib>

namespace
{
    bool ContainsId(std::vector<unsigned int> const& Ids, unsigned int Id)
    {
        for (std::vector<unsigned int>::const_iterator It = Ids.begin(); It != Ids.end(); ++It)
        {
            if (*It == Id)
                return true;
        }
        return false;
    }

    // The unique append of FUN_06f35310 / FUN_06f35540 (06f353b2-06f353f1): a linear find,
    // FUN_06f937c0 insert(end, 1, id), then a re-find whose result is dropped.
    void AddUniqueId(std::vector<unsigned int>* Ids, unsigned int Id)
    {
        if (!ContainsId(*Ids, Id))
            Ids->insert(Ids->end(), 1, Id);
    }

    // The thresholds shared by FUN_06f350f0 / FUN_06f35140 / FUN_06f35190: FCOMP 0.2f
    // (DAT_06fb95c0), TEST AH,1, JNZ -> friendly only with C0 clear (an ordered not-less; a
    // NaN sets C0); FCOMP -0.2f (DAT_06fb95bc), TEST AH,0x41, JZ -> neutral only when greater.
    unsigned int FeelingMask(float Feeling)
    {
        if (Feeling >= 0.2f)
            return 1;
        if (Feeling > -0.2f)
            return 2;
        return 4;
    }
}

// FUN_06f350f0 (06f350f0-06f3513d, cdecl). A failed lookup leaves the local unset in
// retail; the port starts it at 0.
unsigned int __cdecl GetFactionFeelingMask(unsigned int FromGroup, unsigned int ToGroup)
{
    float Feeling = 0.0f;
    pub::Reputation::GetReputation(FromGroup, ToGroup, Feeling);
    return FeelingMask(Feeling);
}

// FUN_06f35140 (06f35140-06f3518d, cdecl): GetReputation(&arg2, &arg1, &out) -- the vibe is
// the second argument. The local is unset in retail on a failed lookup; the port starts it
// at 0.
unsigned int __cdecl GetVibeFeelingMask(unsigned int Group, int VibeHandle)
{
    float Feeling = 0.0f;
    pub::Reputation::GetReputation(VibeHandle, Group, Feeling);
    return FeelingMask(Feeling);
}

// FUN_06f35190 (06f35190-06f35300, cdecl). The sums are float locals; the shares are
// (1.0f / Total) kept on the x87 stack times each sum, stored as floats.
void __cdecl GetHostileFactionShares(weighted_vector<unsigned int> const* Factions,
                                     unsigned int const* Group, float* OutHostileFrom,
                                     float* OutHostileTo)
{
    if (Factions->Items.empty())
    {
        *OutHostileFrom = 0.0f;
        *OutHostileTo = 0.0f;
        return;
    }
    float HostileFrom = 0.0f;       // ESP+0x08 (local_14)
    float HostileTo = 0.0f;         // ESP+0x0c (local_10)
    float Total = 0.0f;             // the reused argument slot
    for (std::vector<std::pair<unsigned int, float>>::const_iterator It = Factions->Items.begin();
         It != Factions->Items.end(); ++It)
    {
        // 06f351e2-06f35240: Group's feeling towards the faction.
        float Feeling = 0.0f;
        pub::Reputation::GetReputation(*Group, It->first, Feeling);
        if ((FeelingMask(Feeling) & 4) != 0)
            HostileFrom = HostileFrom + It->second;
        // 06f35244-06f352a2: the faction's feeling towards Group.
        float Back = 0.0f;
        pub::Reputation::GetReputation(It->first, *Group, Back);
        if ((FeelingMask(Back) & 4) != 0)
            HostileTo = HostileTo + It->second;
        Total = Total + It->second;
    }
    // 06f352c1-06f352e1: FLD 1.0f (DAT_06fb5810), FDIV Total.
    double const Scale = 1.0 / static_cast<double>(Total);
    *OutHostileFrom = static_cast<float>(static_cast<double>(HostileFrom) * Scale);
    *OutHostileTo = static_cast<float>(Scale * static_cast<double>(HostileTo));
}

// FUN_06f35310 (06f35310-06f353f3, cdecl).
void __cdecl GetNpcArchFactions(std::vector<unsigned int> const* NpcArchs,
                                std::vector<unsigned int>* Out)
{
    std::vector<unsigned int>* const Groups = GetFactionGroups();         // FUN_06f50760
    Out->clear();                                                          // 06f3531d-06f35342
    for (std::vector<unsigned int>::iterator Group = Groups->begin(); Group != Groups->end(); ++Group)
    {
        FFactionProps* const Props = GetFactionProps(&*Group);             // FUN_06f50650
        if (Props == nullptr)
            continue;
        // 06f35363-06f353b0: every arch must be in the faction's npc_ship list.
        bool bAll = true;
        for (std::vector<unsigned int>::const_iterator Arch = NpcArchs->begin();
             Arch != NpcArchs->end(); ++Arch)
        {
            if (!ContainsId(Props->NpcShips, *Arch))
            {
                bAll = false;
                break;
            }
        }
        if (bAll)
            AddUniqueId(Out, *Group);                                      // 06f353b2-06f353f1
    }
}

// FUN_06f36d80 (06f36d80-06f36ecf, cdecl, returns the vector by value; caller 06f3547c):
// the ids of First that are also in Second, each once, in First's order. The local result
// takes a VC6 set-like insert (FUN_06eae440 on a miss, FUN_06fa6e80 pair{it, false} on a
// hit); the returned copy is sized to the result.
static std::vector<unsigned int> IntersectIds(std::vector<unsigned int> const* First,
                                              std::vector<unsigned int> const* Second)
{
    std::vector<unsigned int> Result;
    for (std::vector<unsigned int>::const_iterator Id = First->begin(); Id != First->end(); ++Id)
    {
        if (!ContainsId(*Second, *Id))
            continue;
        AddUniqueId(&Result, *Id);
    }
    return Result;
}

// FUN_06f35400 (06f35400-06f3553c, cdecl). The faction's props and the arch are read
// without a test; the intersection is built and freed for every arch.
void __cdecl GetNpcClassFactions(std::vector<unsigned int> const* ClassIds,
                                 std::vector<unsigned int>* Out)
{
    std::vector<unsigned int>* const Groups = GetFactionGroups();         // FUN_06f50760
    Out->clear();                                                          // 06f3540c-06f35435
    for (std::vector<unsigned int>::iterator Group = Groups->begin(); Group != Groups->end(); ++Group)
    {
        FFactionProps* const Props = GetFactionProps(&*Group);             // FUN_06f50650
        for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
             Id != Props->NpcShips.end(); ++Id)
        {
            FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs); // FUN_06f32480
            bool const bShared = !IntersectIds(ClassIds, &Arch->NpcClasses).empty();
            if (bShared)
            {
                AddUniqueId(Out, *Group);                                  // 06f354cb-06f35519
                break;
            }
        }
    }
}

// FUN_06f35540 (06f35540-06f35636, cdecl). The faction's props are read without a test.
void __cdecl GetShipLineFactions(FEncounterShip const* Ship, std::vector<unsigned int>* Out)
{
    std::vector<unsigned int>* const Groups = GetFactionGroups();
    Out->clear();                                                          // 06f3554a-06f3557a
    for (std::vector<unsigned int>::iterator Group = Groups->begin(); Group != Groups->end(); ++Group)
    {
        FFactionProps* const Props = GetFactionProps(&*Group);
        for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
             Id != Props->NpcShips.end(); ++Id)
        {
            FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs); // FUN_06f32480
            if (NpcArchFitsShipLine(Ship, Arch))                           // FUN_06f35640
            {
                AddUniqueId(Out, *Group);
                break;
            }
        }
    }
}

// FUN_06f35640 (06f35640-06f35739, cdecl). Arch is read without a test.
bool __cdecl NpcArchFitsShipLine(FEncounterShip const* Ship, FNpcShipArch const* Arch)
{
    for (std::vector<unsigned int>::const_iterator Class = Ship->ShipClasses.begin();
         Class != Ship->ShipClasses.end(); ++Class)
    {
        std::vector<unsigned int>* const Members = FindShipClass(&*Class);   // FUN_06f58080
        if (Members == nullptr)
        {
            // 06f3566d-06f356d0: the arch's own npc classes, then the ship's mission
            // properties (DAT_06fd0dc8 keyed by the arch's ship, FUN_06f7b0f0 find).
            if (ContainsId(Arch->NpcClasses, *Class))
                continue;
            std::map<unsigned int, std::vector<unsigned int>>::const_iterator Properties =
                GRmShipMissionProperties.find(Arch->ShipArchetype);
            if (Properties != GRmShipMissionProperties.end() &&
                ContainsId(Properties->second, *Class))
                continue;
            return false;
        }
        // 06f356da-06f35717: one member of the ship class among the arch's npc classes.
        bool bMember = false;
        for (std::vector<unsigned int>::iterator Member = Members->begin();
             Member != Members->end(); ++Member)
        {
            if (ContainsId(Arch->NpcClasses, *Member))
            {
                bMember = true;
                break;
            }
        }
        if (!bMember)
            return false;
    }
    return true;
}

// FUN_06f35740 (06f35740-06f357cb, cdecl): the arch is looked up again for every class id;
// a match ends the class loop of that arch only.
bool __cdecl FactionHasNpcClass(unsigned int Group, std::vector<unsigned int> const* ClassIds)
{
    bool bResult = false;
    FFactionProps* const Props = GetFactionProps(&Group);
    if (Props == nullptr)
        return bResult;
    for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
         Id != Props->NpcShips.end(); ++Id)
    {
        for (std::vector<unsigned int>::const_iterator Class = ClassIds->begin();
             Class != ClassIds->end(); ++Class)
        {
            FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs);
            if (Arch != nullptr && ContainsId(Arch->NpcClasses, *Class))
            {
                bResult = true;
                break;
            }
        }
    }
    return bResult;
}

// FUN_06f357d0 (06f357d0-06f35883, cdecl). The faction's props are read without a test.
void __cdecl GetFactionShipLineArchs(unsigned int Group, FEncounterShip const* Ship,
                                     std::list<FNpcShipArch*>* Out)
{
    Out->clear();                                                          // 06f357d8-06f357f4
    FFactionProps* const Props = GetFactionProps(&Group);
    for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
         Id != Props->NpcShips.end(); ++Id)
    {
        FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs);
        if (NpcArchFitsShipLine(Ship, Arch))
            Out->push_back(Arch);                                          // 06f35835-06f35869
    }
}

// FUN_06f35890 (06f35890-06f35b41, cdecl).
FNpcShipArch* __cdecl PickFactionShipLineArch(unsigned int Group, FEncounterShip const* Ship,
                                              int Level)
{
    std::list<FNpcShipArch*> Candidates;
    FNpcShipArch* Closest = nullptr;                                       // ESP+0x14
    if (GetFactionProps(&Group) != nullptr)
    {
        // 06f358e9-06f3594a: the props are looked up a second time for the loop.
        Candidates.clear();
        FFactionProps* const Props = GetFactionProps(&Group);
        for (std::vector<unsigned int>::iterator Id = Props->NpcShips.begin();
             Id != Props->NpcShips.end(); ++Id)
        {
            FNpcShipArch* const Arch = FindNpcShipArch(&*Id, &GNpcShipArchs);
            if (NpcArchFitsShipLine(Ship, Arch))
                Candidates.push_back(Arch);                                // FUN_06ecfc30
        }

        // 06f35950-06f359df: with a level, drop archs more than one level off it and keep the
        // closest one (strictly closer replaces it: CMP / JGE).
        std::list<FNpcShipArch*>::iterator It = Candidates.begin();
        while (It != Candidates.end())
        {
            if (Level <= 0)
            {
                ++It;
                continue;
            }
            int const ArchLevel = (*It)->GetLevel();                       // FUN_06f32910
            bool const bKeep = ArchLevel >= Level - 1 && ArchLevel <= Level + 1;
            if (Closest == nullptr || abs(Level - ArchLevel) < abs(Level - Closest->GetLevel()))
                Closest = *It;
            if (bKeep)
                ++It;
            else
                It = Candidates.erase(It);
        }

        // 06f359e6-06f35a07: nothing in range -> the closest one is the only candidate.
        if (Closest != nullptr && Candidates.empty())
            Candidates.push_back(Closest);                                 // FUN_06f1e390
    }

    // 06f35a0c-06f35a6c: a random candidate -- index 0 for a single one, else
    // (rand() * count) / 0x8000 (signed, truncated).
    int const Count = static_cast<int>(Candidates.size());
    if (Count == 0)
        return Closest;
    int Index = 0;
    if (Count - 1 != 0)
        Index = (rand() * Count) / 0x8000;
    std::list<FNpcShipArch*>::iterator Picked = Candidates.begin();
    for (int Step = Index; Step != 0; --Step)                              // unsigned JBE / DEC
        ++Picked;
    if (Picked == Candidates.end())
        return Closest;
    return *Picked;
}

// ---------------------------------------------------------------------------------------
// Cargo generation (06f35bf0-06f36d30).
// ---------------------------------------------------------------------------------------

// FUN_06f36cf0 (06f36cf0-06f36d09, RET 8).
FCargoPick::FCargoPick(FBaseCommodity const* Entry, float const* InWeight)
    : GoodId(Entry->GoodId)
    , Count(Entry->Count)
    , Weight(*InWeight)
{
}

namespace
{
    // MSVCRT ftol: truncation to a 64-bit integer, the low dword kept.
    unsigned int FtolLow(double Value)
    {
        return static_cast<unsigned int>(static_cast<long long>(Value));
    }

    // The unsigned count loaded with FILD qword {count, 0} (count 0 -> 1), stored as a float.
    float CargoWeightOf(unsigned int Count)
    {
        unsigned int const Weight = Count == 0 ? 1u : Count;
        return static_cast<float>(static_cast<double>(Weight));
    }

    // The intersection GenerateCargo builds twice (06f3621a-06f362a5, 06f36450-06f364e6):
    // copies of both lists (FUN_06f52f60 / FUN_06f55880, not written to), then for every
    // market entry (outer) each pick of the same good (inner) is appended through
    // FUN_06f38170 insert(end, pick). EH map 06fc3c88: the picks copy is built first
    // (states 4/5, 10/11), the market copy second (states 6/12); both are RAII locals and the
    // loops walk the copies.
    void MatchCargoPicks(std::vector<FBaseCommodity> const& Market,
                         std::vector<FCargoPick> const& Picks, std::vector<FCargoPick>* Out)
    {
        std::vector<FCargoPick> const PicksCopy(Picks);                    // FUN_06f52f60
        std::vector<FBaseCommodity> const MarketCopy(Market);              // FUN_06f55880
        for (std::vector<FBaseCommodity>::const_iterator Entry = MarketCopy.begin();
             Entry != MarketCopy.end(); ++Entry)
        {
            for (std::vector<FCargoPick>::const_iterator Pick = PicksCopy.begin();
                 Pick != PicksCopy.end(); ++Pick)
            {
                if (Entry->GoodId == Pick->GoodId)
                    Out->insert(Out->end(), *Pick);
            }
        }
    }
}

// FUN_06f35bf0 (06f35bf0-06f35da1, cdecl). DAT_06fb95c8 = 0.8 (double). The mass out-param
// of pub::GetGoodProperties is a stack slot the function never reads.
void __cdecl FitCargoToHold(unsigned int ShipArchId, std::vector<FCargoPick>* Cargo)
{
    unsigned int HoldSize = 0;                                             // local_2c
    pub::GetCargoHoldSize(ShipArchId, HoldSize);                           // 06f35c0d
    // 06f35c13-06f35c30: FILD qword (unsigned), FMUL 0.8, ftol.
    unsigned int FreeSpace = FtolLow(static_cast<double>(HoldSize) * 0.8);

    // 06f35c41-06f35ca8: the summed weights (ftol after every float add) and the volume the
    // listed counts already take.
    unsigned int TotalWeight = 0;
    for (std::vector<FCargoPick>::iterator It = Cargo->begin(); It != Cargo->end(); ++It)
    {
        TotalWeight = FtolLow(static_cast<double>(TotalWeight) + It->Weight);
        if (It->Count == 0)                                                // CMP / JBE
            continue;
        float Volume = 0.0f;
        float Mass;
        pub::GetGoodProperties(It->GoodId, Volume, Mass);                  // 06f35c6f
        unsigned int const Used = FtolLow(static_cast<double>(It->Count) * Volume);
        // 06f35c90-06f35c9c: CMP / JC -> subtract only when below the free space.
        if (Used < FreeSpace)
            FreeSpace -= Used;
        else
            FreeSpace = 0;
    }

    // 06f35cae-06f35d94: each pick's share of the free space by weight becomes its count.
    std::vector<FCargoPick>::iterator It = Cargo->begin();
    while (It != Cargo->end())
    {
        // 06f35cc0-06f35cd2: FCOMP 0.0f, TEST AH,0x41, JNZ -> weight not greater than 0 (or
        // unordered) keeps the count; so does a zero total.
        if (It->Weight > 0.0f && TotalWeight != 0)
        {
            // 06f35cd4-06f35d06: weight / total (FDIVR), FIMUL the free space as an int.
            float const Share = static_cast<float>(static_cast<double>(It->Weight)
                                                   / static_cast<double>(TotalWeight)
                                                   * static_cast<int>(FreeSpace));
            float Volume = 0.0f;
            float Mass;
            pub::GetGoodProperties(It->GoodId, Volume, Mass);              // 06f35d0a
            // 06f35d10-06f35d45: volume > 0 (TEST AH,0x41, JNZ) and not greater than the share
            // (TEST AH,0x41, JP) -> share / volume, else 0.
            if (Volume > 0.0f && Volume <= Share)
                It->Count = FtolLow(static_cast<double>(Share) / Volume);
            else
                It->Count = 0;
        }
        // 06f35d48-06f35d8e: a pick without a count is erased in place (the tail moved down).
        if (It->Count != 0)
            ++It;
        else
            It = Cargo->erase(It);
    }
}

// FUN_06f35db0 (06f35db0-06f36048, cdecl): VC6 insert(end, 1, pick) with its inlined
// reallocation for every match (FUN_06f36ef0 / FUN_06f6f720 / FUN_06f6f760).
void __cdecl FilterCargoPicks(std::vector<unsigned int> const* Goods,
                              std::vector<FBaseCommodity> const* Market,
                              std::vector<FCargoPick>* Out)
{
    for (std::vector<unsigned int>::const_iterator Good = Goods->begin(); Good != Goods->end(); ++Good)
    {
        for (std::vector<FBaseCommodity>::const_iterator Entry = Market->begin();
             Entry != Market->end(); ++Entry)
        {
            if (Entry->GoodId != *Good)
                continue;
            float const Weight = CargoWeightOf(Entry->Count);
            FCargoPick const Pick(&*Entry, &Weight);                       // FUN_06f36cf0
            Out->insert(Out->end(), 1, Pick);
        }
    }
}

// FUN_06f36c60 (06f36c60-06f36cea). DAT_06fb4448 = 1 / 32768 (float). The total is the x87
// sum from 0.0f kept as a float; the roll is rand() * (1/32768) * total, kept as a float; the
// first pick whose running sum reaches the roll (FCOMP, TEST AH,0x41, JNP: not greater) wins.
// Retail returns end() for an empty list, a zero total (TEST AH,0x44, JNP) or a roll past
// the last sum, and every caller reads the good at the result. Every caller passes a
// non-empty list whose weights are at least 1 (CargoWeightOf; 1.0f for the pick), and the
// roll stays below the total, so that end() is never reached; the port returns the last pick
// there instead of reading past the buffer.
FCargoPick const* __cdecl PickWeightedCargo(std::vector<FCargoPick> const* Picks)
{
    double Sum = 0.0;
    for (std::vector<FCargoPick>::const_iterator It = Picks->begin(); It != Picks->end(); ++It)
        Sum += It->Weight;
    float const Total = static_cast<float>(Sum);
    if (Picks->empty() || Total == 0.0f)
        return &Picks->back();

    float const Roll = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05f * Total);
    double Running = 0.0;
    for (std::vector<FCargoPick>::const_iterator It = Picks->begin(); It != Picks->end(); ++It)
    {
        Running += It->Weight;
        if (Roll <= Running)
            return &*It;
    }
    return &Picks->back();
}

// FUN_06f36050 (06f36050-06f36c5c, cdecl). The loop at 06f360c6 / 06f36c39 runs once (the
// counter is incremented first and compared with 1). The weighted picks of 06f362e3,
// 06f36524, 06f3669b, 06f36747 and 06f367ea are PickWeightedCargo inlined; the pick loops
// 06f36141-06f361c5, 06f363b1-06f3643d and 06f36601-06f36685 are FilterCargoPicks inlined.
// EH states 0..0x12 are the vectors below (RAII here).
void __cdecl GenerateCargo(FSystemInfo* Unused, FContentNpcShipParams* Params,
                           std::vector<FCargoPick>* Out)
{
    (void)Unused;
    Out->clear();                                                          // FUN_06f36d30
    // +0xfc taken as an FGuideDesc without a type check (06f3608c-06f360bd: MOV ECX,[EAX+0xfc];
    // CALL FUN_06f062d0), as retail does: this path only runs for encounter ships.
    FGuideDesc* const Desc = static_cast<FGuideDesc*>(Params->GuideDesc.Pointer);  // +0xfc
    unsigned int Id54;
    unsigned int Id58;
    FContentBase* FromBase = nullptr;                                      // local_188 (+0x5c)
    FContentBase* ToBase = nullptr;                                        // local_190 (+0x60)
    Desc->GetTargets(&Id54, &Id58, &FromBase, &ToBase);                    // FUN_06f062d0

    int Attempt = 0;
    do
    {
        ++Attempt;
        unsigned int Faction = Params->ReputationHandle->GetAffiliation();  // FUN_06eb6bb0
        FFactionProps* const Props = GetFactionProps(&Faction);           // FUN_06f50650
        bool bPicked = false;                                              // local_1a5
        unsigned int Good = 0;                                             // local_1a0 -> +0x4c
        unsigned int ReturnGood = 0;                                       // local_194 -> +0x50

        if (FromBase != nullptr)
        {
            // 06f3610e-06f361c5: the faction's goods FromBase sells.
            std::vector<FBaseCommodity> FromSale;
            FromBase->GetCommoditiesForSale(&FromSale);                    // 06f3612e
            std::vector<FCargoPick> FromSalePicks;
            FilterCargoPicks(&Props->MarketGoods, &FromSale, &FromSalePicks);
            // 06f361c7-06f361f0: the goods ToBase asks for (empty without ToBase).
            std::vector<FBaseCommodity> ToDemand;
            if (ToBase != nullptr)
                ToBase->GetCommoditiesInDemand(&ToDemand);                 // 06f361f0
            // 06f361f5-06f36371: bought here, wanted there.
            std::vector<FCargoPick> Trade;
            MatchCargoPicks(ToDemand, FromSalePicks, &Trade);
            if (!Trade.empty())
            {
                bPicked = true;
                Good = PickWeightedCargo(&Trade)->GoodId;
            }
            else
            {
                // 06f36376-06f365b1: the faction's goods FromBase asks for, wanted at ToBase.
                std::vector<FBaseCommodity> FromDemand;
                FromBase->GetCommoditiesInDemand(&FromDemand);             // 06f3639b
                std::vector<FCargoPick> FromDemandPicks;
                FilterCargoPicks(&Props->MarketGoods, &FromDemand, &FromDemandPicks);
                std::vector<FCargoPick> Resale;
                MatchCargoPicks(ToDemand, FromDemandPicks, &Resale);
                if (!Resale.empty())
                {
                    bPicked = true;
                    Good = PickWeightedCargo(&Resale)->GoodId;
                }
                else
                {
                    // 06f365b6-06f36731: what ToBase sells -- the return load.
                    std::vector<FBaseCommodity> ToSale;
                    std::vector<FCargoPick> ToSalePicks;
                    bool bReturnPicked = false;
                    if (ToBase != nullptr)
                    {
                        ToBase->GetCommoditiesForSale(&ToSale);           // 06f365fc
                        FilterCargoPicks(&Props->MarketGoods, &ToSale, &ToSalePicks);
                        if (!ToSalePicks.empty())
                        {
                            bPicked = true;
                            ReturnGood = PickWeightedCargo(&ToSalePicks)->GoodId;
                            bReturnPicked = true;
                        }
                    }
                    // 06f36736-06f36881: else anything FromBase sells, else anything it asks
                    // for.
                    if (!bReturnPicked)
                    {
                        if (!FromSalePicks.empty())
                        {
                            bPicked = true;
                            Good = PickWeightedCargo(&FromSalePicks)->GoodId;
                        }
                        else if (!FromDemandPicks.empty())
                        {
                            bPicked = true;
                            Good = PickWeightedCargo(&FromDemandPicks)->GoodId;
                        }
                    }
                }
            }
        }
        else if (ToBase != nullptr)
        {
            // 06f369c5-06f36b39: only a destination -- its goods for sale, else its demand.
            std::vector<FBaseCommodity> ToSale;
            ToBase->GetCommoditiesForSale(&ToSale);                        // 06f369f8
            std::vector<FCargoPick> ToSalePicks;
            FilterCargoPicks(&Props->MarketGoods, &ToSale, &ToSalePicks);  // 06f36a2a
            if (!ToSalePicks.empty())
            {
                bPicked = true;
                ReturnGood = PickWeightedCargo(&ToSalePicks)->GoodId;      // 06f36a4e
            }
            else
            {
                std::vector<FBaseCommodity> ToDemand;
                ToBase->GetCommoditiesInDemand(&ToDemand);                 // 06f36a92
                std::vector<FCargoPick> ToDemandPicks;
                FilterCargoPicks(&Props->MarketGoods, &ToDemand, &ToDemandPicks);  // 06f36abc
                if (!ToDemandPicks.empty())
                {
                    bPicked = true;
                    Good = PickWeightedCargo(&ToDemandPicks)->GoodId;      // 06f36ae0
                }
            }
        }

        if (!bPicked)
        {
            // 06f36b44-06f36b84: a random good of the faction; none -> nothing is written.
            if (Props->MarketGoods.empty())
                continue;
            int const Count = static_cast<int>(Props->MarketGoods.size()); // FUN_06fa3db0
            Good = Props->MarketGoods[(rand() * Count) / 0x8000];
        }

        // 06f36b88-06f36b9c: the guide description keeps both goods.
        Desc->TradeGood = Good;                                            // +0x4c
        Desc->ReturnGood = ReturnGood;                                     // +0x50
        if (Good == 0)
            continue;

        // 06f36ba5-06f36c37: one unit of weight 1.0f, fitted to the hold, then appended to
        // the ship's cargo (FUN_06f537a0 insert(end, {good, count})).
        FCargoPick const Pick(Good, 1, 1.0f);
        Out->insert(Out->end(), Pick);                                     // FUN_06f38170
        FitCargoToHold(Params->ShipArchId, Out);                           // 06f36bfd
        for (std::vector<FCargoPick>::const_iterator It = Out->begin(); It != Out->end(); ++It)
        {
            pub::SpaceObj::CargoDesc const Entry = {It->GoodId, It->Count};
            Params->Cargo.insert(Params->Cargo.end(), Entry);
        }
    } while (Attempt < 1);
}
