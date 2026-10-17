// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ContentInternal.h"
#include "ContentMessageRouter.h"
#include "ContentPlayerState.h"
#include "PilotPersonalities.h"
#include "RandomEncounters.h"
#include "RandomMission.h"
#include "RandomMissionDB.h"
#include "RandomMissionGen.h"
#include "SystemInfo.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"
#include "common_misc.h" // SinglePlayer
#include "common_text.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <vector>

// The GameSupport / SpaceSupport / BaseSupport controllers live in GameSupport.cpp,
// SpaceSupport.cpp and BaseSupport.cpp; the player state core in PlayerState.cpp. The
// random-mission offer (TU61) is RandomMissionOffer.h / RandomMissionRuntime.cpp, the offer
// tree and its filters (TU63) RandomMissionGen.cpp.

namespace
{
    // One ptough_graph_pt element of DAT_06fd0bd0 (8 bytes): +0 experience (worth), +4 rank.
    struct FPlayerToughPt
    {
        int Experience;
        int Toughness;
    };

    // Retail DAT_06fd0bd0 vector<{exp, rank}> — filled by FUN_06f49c50, read by FUN_06f49440.
    std::vector<FPlayerToughPt> GPlayerToughnessScale;
}

namespace
{
    void AddVisitedUnique(std::vector<unsigned int>& Visited, unsigned int Id)
    {
        for (int Index = 0; Index < static_cast<int>(Visited.size()); ++Index)
        {
            if (Visited[Index] == Id)
                return;
        }
        Visited.push_back(Id);
    }

    // FRandomMissionData (TU27, 0x88; DestroyMission.cpp) and FRandomMissionLoader
    // (FUN_06f4d220; RandomMissionDB.cpp / DestroyMission.cpp) are declared in
    // RandomMission.h.
    // Parms of message 0x100b, broadcast (Flid 0) after a saved random mission is
    // re-created (FUN_06f483d0 06f486cf-06f486f7).
    struct FRandomMissionAttachMessage
    {
        unsigned int MissionControllerId;
        unsigned int PlayerId;
    };
}

namespace
{
    // Content.dll FUN_06f49780, called by FUN_06f483d0 for unhandled [mPlayer] keys.
    void LoadContentPlayerStatsValue(FContentPlayerStats& Stats, INI_Reader* Reader)
    {
        if (Reader->is_value("ship_type_killed"))
        {
            int Count = Reader->get_value_int(1);
            unsigned int ShipType = static_cast<unsigned int>(Reader->get_value_int(0));
            Stats.ShipTypesKilled.emplace(ShipType, Count);
            return;
        }
        if (Reader->is_value("rm_completed"))
        {
            int Count = Reader->get_value_int(1);
            unsigned char MissionType = static_cast<unsigned char>(Reader->get_value_int(0));
            Stats.RandomMissionsCompleted.emplace(MissionType, Count);
            return;
        }
        if (Reader->is_value("rm_aborted"))
        {
            int Count = Reader->get_value_int(1);
            unsigned char MissionType = static_cast<unsigned char>(Reader->get_value_int(0));
            Stats.RandomMissionsAborted.emplace(MissionType, Count);
            return;
        }
        if (Reader->is_value("rm_failed"))
        {
            int Count = Reader->get_value_int(1);
            unsigned char MissionType = static_cast<unsigned char>(Reader->get_value_int(0));
            Stats.RandomMissionsFailed.emplace(MissionType, Count);
            return;
        }
        if (Reader->is_value("total_cash_earned"))
        {
            Stats.TotalCashEarned = Reader->get_value_float(0);
            return;
        }
        if (Reader->is_value("total_time_played"))
        {
            Stats.TotalTimePlayed = Reader->get_value_float(0);
            return;
        }
        if (Reader->is_value("sys_visited"))
        {
            AddVisitedUnique(Stats.SystemsVisited,
                             static_cast<unsigned int>(Reader->get_value_int(0)));
            return;
        }
        if (Reader->is_value("base_visited"))
        {
            AddVisitedUnique(Stats.BasesVisited,
                             static_cast<unsigned int>(Reader->get_value_int(0)));
            return;
        }
        if (Reader->is_value("holes_visited"))
        {
            AddVisitedUnique(Stats.HolesVisited,
                             static_cast<unsigned int>(Reader->get_value_int(0)));
        }
    }
}

void LoadPlayerToughnessScale(INI_Reader* Reader)
{
    // Content.dll FUN_06f49c50 (.text:06f49c50–06f49f8c, listing 287835–288111).
    int MaxExperience = 0;
    if (Reader->read_header())
    {
        for (;;)
        {
            if (Reader->is_header("PlayerToughnessScale"))
            {
                // Retail reallocates the VC6 vector to 0x29 slots when capacity is
                // below that; std::vector::reserve is the same capacity contract.
                if (GPlayerToughnessScale.capacity() < 0x29u)
                    GPlayerToughnessScale.reserve(0x29u);

                while (Reader->read_value())
                {
                    if (!Reader->is_value("ptough_graph_pt"))
                        continue;

                    int Experience = Reader->get_value_int(0);
                    int Toughness = Reader->get_value_int(1);
                    if (static_cast<unsigned int>(Experience) <
                        static_cast<unsigned int>(MaxExperience))
                        Experience = MaxExperience;
                    MaxExperience = Experience;

                    if (Toughness > 0x28)
                        Toughness = 0x28;
                    else if (Toughness < 1)
                        Toughness = 1;

                    FPlayerToughPt Point;
                    Point.Experience = Experience;
                    Point.Toughness = Toughness;
                    GPlayerToughnessScale.push_back(Point);
                }
                break;
            }

            if (!Reader->read_header())
                break;
        }
    }

    // Retail skips the extrapolation fill when SinglePlayer() is true.
    if (SinglePlayer())
        return;

    while (MaxExperience < 999999999)
    {
        // Signed /8 via CDQ;AND 7;ADD;SAR — matches C++ /8 for non-negative values.
        MaxExperience = MaxExperience + (MaxExperience / 8);

        FPlayerToughPt Point;
        Point.Experience = MaxExperience;
        Point.Toughness = static_cast<int>(GPlayerToughnessScale.size());
        GPlayerToughnessScale.push_back(Point);
    }
}

void RecalculateContentPlayerRank(FContentPlayerState* State)
{
    // Content.dll FUN_06f49440 (.text:06f49440–06f495e4, listing 286859–287013).
    int Cash = 0;
    float AssetValue = 0.0f;
    pub::Player::InspectCash(State->PlayerId, Cash);
    pub::Player::GetAssetValue(State->PlayerId, AssetValue);
    unsigned int Worth = static_cast<unsigned int>(
        static_cast<int>(static_cast<double>(Cash) + static_cast<double>(AssetValue)));

    float NextThreshold = 0.0f;
    unsigned int TableRank = 0;

    FPlayerToughPt const* Begin =
        GPlayerToughnessScale.empty() ? nullptr : &GPlayerToughnessScale[0];
    FPlayerToughPt const* End = Begin + GPlayerToughnessScale.size();
    FPlayerToughPt const* LastAtOrBelow = Begin;
    FPlayerToughPt const* Cursor = Begin;

    if (Begin != End)
    {
        while (Cursor != End)
        {
            if (static_cast<unsigned int>(Cursor->Experience) > Worth)
            {
                NextThreshold =
                    static_cast<float>(static_cast<unsigned int>(Cursor->Experience));
                break;
            }
            LastAtOrBelow = Cursor;
            ++Cursor;
        }

        if (LastAtOrBelow != Cursor)
            TableRank = static_cast<unsigned int>(LastAtOrBelow->Toughness);
        else
            TableRank = 0;
    }

    int ReputationId = State->Reputation;
    pub::Reputation::SetRank(ReputationId, static_cast<float>(TableRank));

    if (State->MissionNumber != 0x2a)
        return;

    int PlayerRank = 0;
    pub::Player::GetRank(State->PlayerId, PlayerRank);
    unsigned int PlayerRankU = static_cast<unsigned int>(PlayerRank);

    int MoneyNeeded = 0x7fffffff;
    if (TableRank < PlayerRankU)
    {
        unsigned int WantedRank = PlayerRankU + 1u;
        FPlayerToughPt const* Found = nullptr;
        for (FPlayerToughPt const* Entry = Begin; Entry != End; ++Entry)
        {
            if (static_cast<unsigned int>(Entry->Toughness) == WantedRank)
            {
                Found = Entry;
                break;
            }
        }
        if (Found != nullptr)
        {
            NextThreshold =
                static_cast<float>(static_cast<unsigned int>(Found->Experience));
            if (NextThreshold != 0.0f)
            {
                // 06f4958d: FILD qword worth, FSUBR float threshold - the
                // difference stays on the x87 stack (double, not float).
                double Remainder = static_cast<double>(NextThreshold) - static_cast<double>(Worth);
                if (Remainder < 1.0)
                    Remainder = 1.0;
                MoneyNeeded = static_cast<int>(Remainder);
            }
        }
    }
    else if (NextThreshold != 0.0f)
    {
        double Remainder = static_cast<double>(NextThreshold) - static_cast<double>(Worth);
        if (Remainder < 1.0)
            Remainder = 1.0;
        MoneyNeeded = static_cast<int>(Remainder);
    }

    pub::Player::SetMoneyNeededToNextRank(State->PlayerId, MoneyNeeded);
    if (TableRank > PlayerRankU)
        pub::Player::SetRank(State->PlayerId, static_cast<int>(TableRank));
}

void LoadContentPlayerState(FContentPlayerState* State, INI_Reader* Reader)
{
    // Content.dll FUN_06f483d0: both collections are empty after construction,
    // and the retail loader erases their full old ranges before scanning.
    State->Vnpcs.clear();
    State->Rumors.clear();

    INI_Reader::State SavedState;
    Reader->get_state(SavedState);
    while (Reader->read_header())
    {
        if (Reader->is_header("mPlayer"))
        {
            while (Reader->read_value())
            {
                if (Reader->is_value("locked_gate"))
                {
                    RegisterContentGateLock(State->PlayerId,
                                            static_cast<unsigned int>(Reader->get_value_int(0)));
                }
                else if (Reader->is_value("vnpc"))
                {
                    FVnpcRecord Record;
                    Record.Values[1] = static_cast<unsigned int>(Reader->get_value_int(1));
                    Record.Values[3] = static_cast<unsigned int>(Reader->get_value_int(3));
                    Record.Values[2] = static_cast<unsigned int>(Reader->get_value_int(2));
                    Record.Values[0] = static_cast<unsigned int>(Reader->get_value_int(0));
                    State->Vnpcs.push_back(Record);
                }
                else if (Reader->is_value("can_dock"))
                {
                    State->bCanDock = Reader->get_value_bool(0) ? 1 : 0;
                }
                else if (Reader->is_value("dock_exception"))
                {
                    State->DockExceptions.push_back(
                        static_cast<unsigned int>(Reader->get_value_int(0)));
                }
                else if (Reader->is_value("can_tl"))
                {
                    State->bCanTradelane = Reader->get_value_bool(0) ? 1 : 0;
                }
                else if (Reader->is_value("tlr_exception"))
                {
                    FTlrException Record = {};
                    Record.Values[0] = static_cast<unsigned int>(Reader->get_value_int(0));
                    Record.Values[1] = static_cast<unsigned int>(Reader->get_value_int(1));
                    State->TradelaneExceptions.push_back(Record);
                }
                else if (Reader->is_value("rumor"))
                {
                    FRumorRecord Record;
                    Record.Values[1] = static_cast<unsigned int>(Reader->get_value_int(1));
                    Record.Values[0] = static_cast<unsigned int>(Reader->get_value_int(0));
                    State->Rumors.push_back(Record);
                }
                else
                {
                    LoadContentPlayerStatsValue(State->Stats, Reader);
                }
            }
        }
        else if (Reader->is_header("RandomMission"))
        {
            // Listing 0x06f4863f–0x06f4872e: type string is copied then ignored by
            // FUN_06f4d220; Load / Create / 0x100b / SetMsnID follow on success.
            while (Reader->read_value())
            {
                if (!Reader->is_value("type"))
                    continue;

                char MissionType[0x3c];
                std::strncpy(MissionType, Reader->get_value_string(0), sizeof(MissionType));

                // FUN_06f4d220 always produces a loader; retail calls through it
                // without a null check.
                FRandomMissionLoader* Loader = nullptr;
                CreateRandomMissionLoader(MissionType, &Loader);

                FRandomMissionData* Mission = nullptr;
                if (Loader->Load(Reader, &Mission, State->PlayerId))
                {
                    unsigned int ControllerId = pub::Controller::Create(
                        "content.dll", MissionType, Mission, pub::Controller::ABOVE_NORMAL);
                    State->ValueD8 = ControllerId;

                    // 06f486cf-06f486f7: _SendMessage(&0 (all controllers), 0x100b,
                    // {mission controller, player}).
                    FRandomMissionAttachMessage AttachMessage;
                    AttachMessage.MissionControllerId = ControllerId;
                    AttachMessage.PlayerId = State->PlayerId;
                    unsigned int BroadcastFlid = 0;
                    pub::Controller::_SendMessage(BroadcastFlid,
                                                  pub::Controller::EMessage::PlayerMissionChanged,
                                                  &AttachMessage);
                    pub::Player::SetMsnID(State->PlayerId, ControllerId, State->PlayerId, false,
                                         0);
                }

                Loader->Release();
            }
        }
    }

    // FUN_06f49440 recalculates rank and the next-rank cash threshold here.
    RecalculateContentPlayerRank(State);
    Reader->set_state(SavedState);
}

void SetContentPlayerMissionNumber(FContentPlayerState* State, unsigned int MissionNumber,
                                   unsigned int bAdvanceRank)
{
    if ((bAdvanceRank & 0xffu) != 0)
    {
        switch (MissionNumber)
        {
        case 3:
        case 5:
        case 7:
        case 9:
        case 10:
        case 12:
        case 13:
        case 15:
        case 16:
        case 18:
        case 19:
        case 22:
        case 24:
        case 25:
        case 27:
        case 28:
        case 31:
        case 42:
        {
            int Rank;
            pub::Player::GetRank(State->PlayerId, Rank);
            pub::Player::SetRank(State->PlayerId, Rank + 1);
            break;
        }
        default:
            break;
        }
    }

    State->MissionNumber = MissionNumber;
}
