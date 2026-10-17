// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU35 (06f03fa0-06f056d0): loot properties and mission offers. See
// LootProps.h.

#include "LootProps.h"
#include "BaseDB.h"
#include "BaseSupport.h"        // FBaseVisit, FBaseVendorOffer
#include "Cargo.h"              // GetVibeFeelingMask (FUN_06f35140)
#include "ContentPlayerState.h"
#include "ContentStaticIds.h"   // TU35 static IDs 06f03fa0..06f04180
#include "ContentDefaultId.h"   // TU35 06f041a0 (CreateID("default") -> DAT_06fd0590)
#include "RandomMission.h"
#include "RandomMissionDB.h"
#include "SystemInfo.h"         // FSystemInfo::SystemId
#include "common_goods.h"       // MissionVendorOfferDescription
#include "common_ini.h"
#include "common_server.h"      // pub::GF::MissionVendorOffer*, ReportWhyMissionVendorEmpty
#include "common_text.h"        // FmtStr
#include "common_weighted_vector.h" // weighted_vector<unsigned int> (vendor candidates)

#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    // Retail DAT_06fd05ec..DAT_06fd05f8: vector<FLootProps>, built by 06f04c20 and
    // freed at exit by 06f04c50.
    std::vector<FLootProps> GLootProps;

    // Retail DAT_06fd05e8 (06f04c70) and DAT_06fd05e4 (06f04c90).
    unsigned int const GIdDefaultCommodity = CreateID("default_commodity");
    unsigned int const GIdDefaultEquipment = CreateID("default_equipment");

    // Retail DAT_06fd05d4..DAT_06fd05e0: vector<FPhantomLoot>, built by 06f05090 and
    // freed at exit by 06f050c0.
    std::vector<FPhantomLoot> GPhantomLoots;

    // Shared body of FUN_06f05000 / FUN_06f05030 / FUN_06f05060: linear scan.
    FLootProps* FindLootPropsById(unsigned int Id)
    {
        for (int Index = 0; Index < static_cast<int>(GLootProps.size()); ++Index)
        {
            if (GLootProps[Index].Nickname.Id == Id)
                return &GLootProps[Index];
        }
        return nullptr;
    }
}

// FUN_06f04cb0 (06f04cb0-06f04ff8).
void __cdecl LoadLootProps(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("mLootProps"))
            continue;

        FLootProps Record;
        Record.Nickname.clear();
        Record.DropChance = 0.0f;
        for (int Index = 0; Index < 5; ++Index)
            Record.DropProperties[Index] = 0;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Record.Nickname.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("drop_properties"))
            {
                // FMUL float DAT_06fb8064 (0x3c23d70a = 0.01f), FSTP float.
                Record.DropChance = Reader->get_value_float(0) * 0.01f;
                Record.DropProperties[0] = Reader->get_value_int(1);
                Record.DropProperties[1] = Reader->get_value_int(2);
                Record.DropProperties[2] = Reader->get_value_int(3);
                Record.DropProperties[3] = Reader->get_value_int(4);
                Record.DropProperties[4] = Reader->get_value_int(5);
            }
        }

        // 06f04db1-06f04fdc: inlined vector insert at end.
        GLootProps.push_back(Record);
    }
}

// FUN_06f050e0 (06f050e0-06f05436).
void __cdecl LoadPhantomLoot(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("PhantomLoot"))
            continue;

        FPhantomLoot Record;
        Record.Nickname.clear();
        Record.ToughnessMin = 0;
        Record.ToughnessMax = 0;
        Record.PercentChance = 0.0f;
        Record.NumToDropMin = 0;
        Record.NumToDropMax = 0;

        while (Reader->read_value())
        {
            if (Reader->is_value("nickname"))
            {
                Record.Nickname.Id = CreateID(Reader->get_value_string());
            }
            else if (Reader->is_value("toughness_range"))
            {
                Record.ToughnessMin = Reader->get_value_int(0);
                Record.ToughnessMax = Reader->get_value_int(1);
                if (Record.ToughnessMax == -1)
                    Record.ToughnessMax = 0x7fffffff;
            }
            else if (Reader->is_value("percent_chance"))
            {
                Record.PercentChance = Reader->get_value_float(0) * 0.01f;
            }
            else if (Reader->is_value("num_to_drop"))
            {
                Record.NumToDropMin = Reader->get_value_int(0);
                Record.NumToDropMax = Reader->get_value_int(1);
            }
        }

        // 06f051fa-06f05418: inlined vector insert at end.
        GPhantomLoots.push_back(Record);
    }
}

// FUN_06f05000 (06f05000-06f05022).
FLootProps* __cdecl FindDefaultCommodityLootProps()
{
    return FindLootPropsById(GIdDefaultCommodity);
}

// FUN_06f05030 (06f05030-06f05052).
FLootProps* __cdecl FindDefaultEquipmentLootProps()
{
    return FindLootPropsById(GIdDefaultEquipment);
}

// FUN_06f05060 (06f05060-06f05082).
FLootProps* __cdecl FindLootProps(unsigned int const* Nickname)
{
    return FindLootPropsById(*Nickname);
}

// FUN_06f05440 (06f05440-06f05621). The append is the inlined VC6 insert(end, 1, record)
// (FUN_06eb6740 / FUN_06f38a50 / FUN_06eed270 and its reallocation).
void __cdecl CollectPhantomLoot(int const* Toughness, std::vector<FPhantomLoot const*>* Out)
{
    for (std::vector<FPhantomLoot>::const_iterator It = GPhantomLoots.begin();
         It != GPhantomLoots.end(); ++It)
    {
        // 06f05462-06f05474: [+4] > T or [+8] < T (JG / JL) -> skipped.
        if (It->ToughnessMin > *Toughness || It->ToughnessMax < *Toughness)
            continue;
        Out->insert(Out->end(), 1, &*It);
    }
}

// FUN_06f041c0 (06f041c0-06f042c2).
bool __cdecl CreateBarMissionOffer(FBaseNpcMission const* Mission, FContentPlayerState* Player,
                                   unsigned int BaseId, unsigned int Group,
                                   FBarMissionOfferData* Out)
{
    // 06f041cb-06f04216: Out->Type = the mission type (strlen clamped to 23).
    Out->Type = Mission->Type.Data;
    bool const bLoaderCreated = CreateRandomMissionLoader(Mission->Type.Data, &Out->Loader);

    // 06f04220-06f04247: {player, base, group, difficulty 0.0 (DAT_06fb8030)}.
    FRmMissionRequest Request;
    Request.PlayerId = Player->PlayerId;
    Request.OfferBase = BaseId;
    Request.OfferGroup = static_cast<int>(Group);
    Request.Difficulty = 0.0f;

    // 06f04250-06f04298: 2 ("no offer") is retried up to five attempts in all; 0, 1 and 3
    // end the loop.
    int Result = 0;
    for (int Attempt = 0; Attempt < 5; ++Attempt)
    {
        Request.Difficulty = PickRmMissionDifficulty(Player, Mission->MinDifficulty,
                                                     Mission->MaxDifficulty);
        // 06f04261-06f04273: FCOMP + TEST AH,0x44 / JNP -- equal to 0.0 -> false.
        if (0.0f == Request.Difficulty)
            return false;
        Result = Out->Loader->CreateMission(&Request, &Out->Mission);
        if (Result == 0 || Result == 1 || Result == 3)
            break;
    }

    // 06f0429a-06f042c2.
    if (!bLoaderCreated)
        return false;
    return Result == 0;
}

namespace
{
    // {[BaseFaction] faction, its weight}: the 8-byte candidate of FUN_06f042d0 /
    // FUN_06f04450 -- an element of Common's weighted_vector<unsigned int>, the type of the
    // candidate local of FUN_06f047e0 (its EH state 0, FuncInfo 06fc1990, unwinds through
    // the ??1?$weighted_vector@I@@QAE@XZ import). Every other use is inline VC6 vector code
    // on its pair vector (no weighted_vector member is called, not even Pick).
    typedef std::pair<unsigned int, float> FVendorFaction;

    // FUN_06f042d0 (06f042d0-06f04449, cdecl): one candidate per [BaseFaction] of Base that
    // has a mission_type, minus those the player's reputation is hostile to.
    void __cdecl CollectVendorFactions(FContentBase* Base, FContentPlayerState* State,
                                       weighted_vector<unsigned int>* Factions)
    {
        std::vector<FVendorFaction>& Items = Factions->Items;

        // 06f042d9-06f0437d: reserve one slot per [BaseFaction] (the inlined VC6 reserve:
        // FUN_06ed2700 copy, FUN_06ea1490 empty destroy, the old block freed).
        Items.reserve(Base->Factions.size());

        // 06f04380-06f043dc: {+0x00 faction, +0x24 weight} of every faction whose
        // +0x14 mission_type vector is not empty (FUN_06f96770 insert(end, 1, pair)).
        for (std::vector<FBaseFaction>::iterator It = Base->Factions.begin();
             It != Base->Factions.end(); ++It)
        {
            if (!It->MissionTypes.empty())
                Items.insert(Items.end(), 1, FVendorFaction(It->Group, It->Weight));
        }

        // 06f043de-06f04440: FUN_06f35140(faction, FUN_06f33f80(State)) -- 1 (friendly) and
        // 2 (neutral) keep the candidate, anything else is erased in place (the following
        // elements move down one, end -= 8).
        std::vector<FVendorFaction>::iterator Faction = Items.begin();
        while (Faction != Items.end())
        {
            unsigned int const Mask = GetVibeFeelingMask(Faction->first, State->GetReputation());
            if (Mask == 1 || Mask == 2)
                ++Faction;
            else
                Faction = Items.erase(Faction);
        }
    }

    // FUN_06f04450 (06f04450-06f047d0, cdecl): up to Count random missions for Factions into
    // Out (the offers own their loader reference). Returns 0 when Out is not empty, else the
    // last failure: 2 no difficulty, 3 no objective zone (CreateMission 3), 4 any other
    // CreateMission failure; 0 also when nothing was tried.
    int __cdecl GenerateVendorMissions(std::vector<FBarMissionOfferData>* Out,
                                       FContentPlayerState* State, FContentBase* Base,
                                       weighted_vector<unsigned int> const* FactionList,
                                       int Count)
    {
        std::vector<FVendorFaction> const* Factions = &FactionList->Items;
        int Result = 0;                                                     // [ESP+0x04]

        // 06f04458-06f044e9: the inlined VC6 reserve; a negative Count allocates 0
        // (TEST EDI,EDI / JGE, else 0).
        Out->reserve(static_cast<size_t>(Count >= 0 ? Count : 0));

        // 06f044ec-06f04505: no candidate -> straight to the result.
        if (!Factions->empty())
        {
            // 06f0450b-06f0451b: 2 * Count attempts, at least 20 (unsigned JB).
            unsigned int MaxAttempts = 0x14;
            if (static_cast<unsigned int>(Count * 2) >= 0x14)
                MaxAttempts = static_cast<unsigned int>(Count * 2);
            unsigned int Attempt = 0;                                       // [ESP+0x58]

            // 06f0451f-06f04534: FUN_06f4d5e0 loads the player's system for the missions.
            RegisterRmDestSystem(State->GetSystemRecord()->SystemId);

            // 06f0453c-06f0473e: until Count offers are made, while Attempt <= MaxAttempts
            // (CMP / JA, unsigned).
            while (Count != 0 && Attempt <= MaxAttempts)
            {
                // 06f0455e-06f045d1: a faction by weight. The sum and the running total are
                // kept on the x87 stack (double); the roll is (float)(rand() * 1/32768 *
                // (float)sum). TEST AH,0x44 / JNP: a sum equal to 0.0 skips the roll. TEST
                // AH,0x41 / JNP: the first faction with roll <= running total is taken. No
                // roll, or none taken, leaves the pointer at end() and retail reads that
                // slot as a candidate (06f045d1 MOV EBP,ESI; 06f045d3 MOV ECX,[EBP]) -- the
                // stale copy an erase left there, or the bytes past the block; the port
                // reads the same slot.
                FVendorFaction const* Picked = Factions->data() + Factions->size();
                double FactionSum = 0.0;
                for (std::vector<FVendorFaction>::const_iterator It = Factions->begin();
                     It != Factions->end(); ++It)
                    FactionSum += It->second;
                if (FactionSum != 0.0)
                {
                    float const SumValue = static_cast<float>(FactionSum);    // FST [ESP+0x1c]
                    float const Roll = static_cast<float>(
                        static_cast<double>(rand()) * (1.0f / 32768.0f) * SumValue);
                    double Running = 0.0;
                    for (size_t Index = 0; Index < Factions->size(); ++Index)
                    {
                        Running += (*Factions)[Index].second;
                        if (Roll <= Running)
                        {
                            Picked = Factions->data() + Index;
                            break;
                        }
                    }
                }

                // 06f045d3-06f04664: FUN_06eab770, then a mission_type by its +0x24 weight
                // the same way; none -> the attempt is spent (06f04666 CMP ESI,[EDI+8]).
                FBaseFaction* Faction = Base->FindFaction(Picked->first);
                std::vector<FBaseMissionType>::iterator PickedType =
                    Faction->MissionTypes.end();
                double TypeSum = 0.0;
                for (std::vector<FBaseMissionType>::iterator It = Faction->MissionTypes.begin();
                     It != Faction->MissionTypes.end(); ++It)
                    TypeSum += It->Weight;
                if (TypeSum != 0.0)
                {
                    float const SumValue = static_cast<float>(TypeSum);       // FST [ESP+0x18]
                    float const Roll = static_cast<float>(
                        static_cast<double>(rand()) * (1.0f / 32768.0f) * SumValue);
                    double Running = 0.0;
                    for (std::vector<FBaseMissionType>::iterator It =
                             Faction->MissionTypes.begin();
                         It != Faction->MissionTypes.end(); ++It)
                    {
                        Running += It->Weight;
                        if (Roll <= Running)
                        {
                            PickedType = It;
                            break;
                        }
                    }
                }

                if (PickedType != Faction->MissionTypes.end())
                {
                    // 06f0466f-06f046a2: {loader 0, mission 0, the type (FUN_06ea9f40, at
                    // most 23 chars)}.
                    FBarMissionOfferData Offer;                             // [ESP+0x30]
                    Offer.Type = PickedType->Type.Data;

                    // 06f046a6-06f046ba: FUN_06f4d220; false -> the attempt is spent.
                    if (CreateRandomMissionLoader(Offer.Type.Data, &Offer.Loader))
                    {
                        // 06f046bc-06f046e2: {player, base, faction, 0.0 (DAT_06fb8030)}.
                        FRmMissionRequest Request;                          // [ESP+0x20]
                        Request.PlayerId = State->PlayerId;
                        Request.OfferBase = Base->BaseId;
                        Request.OfferGroup = static_cast<int>(Picked->first);
                        Request.Difficulty = 0.0f;
                        // 06f046e6-06f046f0: FUN_06f4ad60(State, min, max), FSTP float.
                        Request.Difficulty = PickRmMissionDifficulty(
                            State, PickedType->MinDifficulty, PickedType->MaxDifficulty);

                        bool bKept = false;
                        // 06f046f4-06f04706: FLD 0.0 / FCOMP difficulty / TEST AH,0x44 / JP
                        // -> not equal goes to CreateMission.
                        if (0.0f == Request.Difficulty)
                        {
                            // 06f04708-06f04710.
                            if (Result == 0)
                                Result = 2;
                        }
                        else
                        {
                            // 06f04776-06f047a7: slot 3; 3 -> Result 3, any other non-zero
                            // -> 4.
                            int const Code = Offer.Loader->CreateMission(&Request, &Offer.Mission);
                            bool const bCreated = Code == 0;
                            if (Code == 3)
                                Result = Code;
                            else if (Code != 0)
                                Result = 4;
                            if (bCreated)
                            {
                                // 06f047ad-06f047c3: FUN_06eae680 insert(end, 1, offer) --
                                // the loader reference moves into Out; one offer fewer to go.
                                Out->insert(Out->end(), 1, Offer);
                                --Count;
                                bKept = true;
                            }
                        }

                        if (!bKept)
                        {
                            // 06f04718-06f0472c: loader slot 5 (the mission), Release.
                            Offer.Loader->Slot05(Offer.Mission);
                            Offer.Loader->Release();
                        }
                    }
                }

                // 06f0472f-06f0473e.
                ++Attempt;
            }

            // 06f04744: FUN_06f4d650.
            ReleaseRmDestSystems();
        }

        // 06f04749-06f047d0: offers made -> 0, else the last failure.
        if (!Out->empty())
            return 0;
        return Result;
    }

    // The reset of one FmtStr inlined three times in FUN_06f04ab0: FmtStr::destroy, then all
    // 0x18 bytes zeroed (REP STOSD x6).
    void ResetVendorFmtStr(FmtStr& Str)
    {
        Str.destroy();
        Str.Head = nullptr;
        Str.MadLibId = 0;
        memset(Str.ValueIndex, 0, sizeof(Str.ValueIndex));
    }

    // FUN_06f04ab0 (06f04ab0-06f04b03, thiscall; the three FmtStr default ctors
    // FUN_06eac680 run before it): +0x00..+0x0c zeroed, each FmtStr reset, +0x58/+0x5c 0.
    void InitMissionVendorOfferDescription(MissionVendorOfferDescription* Desc)
    {
        Desc->BaseId = 0;
        Desc->TargetId = 0;
        Desc->SystemId = 0;
        Desc->MissionClass = 0;
        ResetVendorFmtStr(Desc->Title);
        ResetVendorFmtStr(Desc->Description);
        ResetVendorFmtStr(Desc->Briefing);
        Desc->IconTextureId = 0;
        Desc->Filter = 0;
    }
}

// FUN_06f047e0 (06f047e0-06f04a21).
void __cdecl BuildMissionVendorOffers(FContentBase* Base, FBaseVisit* Visit,
                                      FContentPlayerState* State)
{
    // 06f04801-06f0484b: the candidate factions; none -> reason 1 and nothing else.
    // weighted_vector<unsigned int> (EH state 0 -> the Common ~weighted_vector import); the
    // normal path frees it inline (06f04a02 operator delete).
    weighted_vector<unsigned int> Factions;                                 // [ESP+0x10]
    CollectVendorFactions(Base, State, &Factions);
    if (Factions.Items.empty())
    {
        pub::GF::ReportWhyMissionVendorEmpty(State->PlayerId, static_cast<MVEmptyReason>(1));
        return;
    }

    // 06f04861-06f048ab: num_offers[0] + rand() * (num_offers[1] - num_offers[0] + 1) /
    // 32768 (IMUL, CDQ, AND EDX,0x7fff, SAR 0xf -- a signed division).
    std::vector<FBarMissionOfferData> Offers;                               // [ESP+0x28]
    int const Range = Base->VendorOffersMax - Base->VendorOffersMin + 1;
    int const Count = rand() * Range / 32768 + Base->VendorOffersMin;
    int const Reason = GenerateVendorMissions(&Offers, State, Base, &Factions, Count);

    // 06f048b0-06f048d9: the candidates are cleared (FUN_06ea1490, end = begin), then the
    // reason is reported whatever it is.
    Factions.Items.clear();
    pub::GF::ReportWhyMissionVendorEmpty(State->PlayerId, static_cast<MVEmptyReason>(Reason));

    // 06f048e0-06f049e9: one vendor offer per generated mission.
    for (std::vector<FBarMissionOfferData>::iterator Offer = Offers.begin();
         Offer != Offers.end(); ++Offer)
    {
        // 06f04930-06f0497e: three FmtStr ctors (EH states 2..4), FUN_06f04ab0 (state 5);
        // its dtor ~MissionVendorOfferDescription = FUN_06f04b10 (common_goods.h) runs at the
        // end of the pass (06f049cf-06f049db, back to EH state 1) and on unwind.
        MissionVendorOfferDescription Desc;                                 // [ESP+0x64]
        InitMissionVendorOfferDescription(&Desc);

        // 06f04971-06f04986: loader slot 6 fills the description from the mission.
        Offer->Loader->GetOfferDescription(Offer->Mission, &Desc);
        // 06f04989-06f04998: +0x08 = 2 (the kind BaseSupport msg 0x2c reads back as 2 =
        // vendor offer), +0x04 = the player's next offer id (FUN_06f48c50). The
        // common_goods.h names of both fields (SystemId, TargetId) are the Server port's.
        Desc.SystemId = 2;
        Desc.TargetId = State->AllocateVendorOfferId();

        // 06f04994-06f049c5: {handle, offer id, FUN_06f04a30 empty data} then FUN_06eb2060
        // copies the offer in (the loader reference moves; nothing is AddRef'd and the
        // local has no dtor).
        FBaseVendorOffer Record;                                            // [ESP+0x38]
        Record.VendorHandle = pub::GF::MissionVendorOfferCreate(Desc);
        Record.OfferId = Desc.TargetId;
        Record.Data = *Offer;

        // 06f049c1-06f049ca: FUN_06f04b90 = list push_back (new 0x34 node, 0xb-dword copy).
        Visit->VendorOffers.push_back(Record);
    }

    // 06f049f3-06f04a02: both blocks freed with operator delete, no element dtors.
}

// FUN_06f04a40 (06f04a40-06f04aad). Neither loop tests the loader for null.
void __cdecl ClearMissionVendorOffers(FBaseVisit* Visit)
{
    for (std::list<FBaseVendorOffer>::iterator It = Visit->VendorOffers.begin();
         It != Visit->VendorOffers.end(); ++It)
    {
        pub::GF::MissionVendorOfferDestroy(&It->VendorHandle);             // 06f04a5a
        It->Data.Loader->Slot05(It->Data.Mission);                         // +0x14
        It->Data.Loader->Release();                                        // +0x04
    }
    // 06f04a7a-06f04aa8: unlink and free every node, --size.
    Visit->VendorOffers.clear();
}
