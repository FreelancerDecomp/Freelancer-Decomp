// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU33 (06efe8e0-06f00670): the GameSupport controller.
// CreateInstance("GameSupport") -> new 8 -> FUN_06efeb10; vtable 06fb7d74
// (slot 0 LAB_06efec50 HandleMessage, slot 1 FUN_06efeb90 deleting dtor).

#include "ContentInternal.h"
#include "ContentMessageRouter.h"
#include "ContentPlayerState.h"
#include "ContentStaticIds.h"   // TU33 static IDs 06efe8e0..06efeac0
#include "ContentDefaultId.h"   // TU33 06efeae0 (CreateID("default") -> DAT_06fcf3e8)
#include "BaseDB.h"
#include "DockQueues.h"         // AllocateFormationId (FUN_06efeb00)
#include "Encounters.h"
#include "MissionObject.h"      // FNnPathLeg
#include "SpacePaths.h"         // BuildNnPath (FUN_06f5ea40)
#include "SpaceSupport.h"       // BuildBestPathLegs (FUN_06f5e910)
#include "SystemInfo.h"
#include "SystemConnections.h"  // InitSystemPathTables (TU41)
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"

#include "RandomMissionDB.h"   // GetShipMissionProperties (TU51)

#include <algorithm>
#include <cmath>
#include <list>
#include <new>
#include <vector>

namespace
{
    // Retail DAT_06fcf430: total time accumulated from GameSupport heartbeats.
    double GGameSupportElapsed = 0.0;

    struct FPlayerIdMessage
    {
        unsigned int PlayerId;
    };

    struct FBaseMessage
    {
        unsigned int BaseId;
        unsigned int PlayerId;
    };

    struct FPlayerSaveMessage
    {
        unsigned int PlayerId;
        FILE* File;
    };

    struct FPlayerLoadMessage
    {
        unsigned int PlayerId;
        INI_Reader* Reader;
    };

    struct FLocationMessage
    {
        unsigned int LocationId;
        unsigned int PlayerId;
    };

    struct FSystemMessage
    {
        unsigned int SystemId;
        unsigned int Field_0x4;                               // TODO: not read by GameSupport
        unsigned int PlayerId;
    };

    // {group, feeling} collected by FGroupFeelingEnumerator (8-byte stride).
    struct FGroupFeeling
    {
        unsigned int GroupId;
        float Feeling;
    };

    // Local of GameSupport msg 0x33: vtable 06fb7d7c (slot 0 LAB_06f00000),
    // +4 vector<FGroupFeeling>, +0x14 player reputation (pub::Player::GetRep out).
    struct FGroupFeelingEnumerator : pub::Reputation::Enumerator
    {
        std::vector<FGroupFeeling> Feelings;                  // +0x04
        int Reputation = 0;                                   // +0x14

        char Execute(unsigned int GroupId) override;
    };

    // LAB_06f00000 (06f00000-06f001f4, RET 4): feeling of the player's vibe towards
    // the group (0 when the call fails), push_back {group, feeling}, return 1.
    char FGroupFeelingEnumerator::Execute(unsigned int GroupId)
    {
        float Feeling;
        if (pub::Reputation::GetGroupFeelingsTowards(Reputation, GroupId, Feeling) != 0)
            Feeling = 0.0f;

        FGroupFeeling Entry;
        Entry.GroupId = GroupId;
        Entry.Feeling = Feeling;
        Feelings.push_back(Entry);
        return 1;
    }

    // Block handed to pub::Player::ReturnPlayerStats by msg 0x33 (06eff8d7-06effaec):
    // this 0x34-byte header, then ShipTypesKilledCount {ship type, count} pairs,
    // then GroupCount {group, feeling} pairs.
    struct FPlayerStatsBlock
    {
        int RandomMissionsCompleted;                          // +0x00 type 7 + types 0..5
        int RandomMissionsAborted;                            // +0x04 types 0..5
        int RandomMissionsFailed;                             // +0x08 types 0..5
        float TotalCashEarned;                                // +0x0c (float)(int)
        float TotalTimePlayed;                                // +0x10 (float)(int)
        int SystemsVisited;                                   // +0x14
        int BasesVisited;                                     // +0x18
        int HolesVisited;                                     // +0x1c
        unsigned int ShipTypesKilledCount;                    // +0x20
        int Rank;                                             // +0x24
        int Worth;                                            // +0x28 cash + (int)asset value
        int MoneyNeededToNextRank;                            // +0x2c
        int GroupCount;                                       // +0x30
    };
    static_assert(sizeof(FPlayerStatsBlock) == 0x34, "retail stats header is 0x34 bytes");

    // GameSupport message 0x31 parms (0x28 bytes, read at 06eff207-06eff8d2).
    struct FBestPathRequest
    {
        unsigned int PlayerId;                                // +0x00 the ReturnBestPath target
        // +0x04: the client's request word (Server IServerImpl::RequestBestPath packs client
        // data +0x00 here); Content only echoes it into the reply header +0.
        unsigned int ClientCookie;
        unsigned int FromSystemId;                            // +0x08
        Vector FromPosition;                                  // +0x0c
        Vector ToPosition;                                    // +0x18
        unsigned int ToSystemId;                              // +0x24
    };
    static_assert(sizeof(FBestPathRequest) == 0x28, "retail msg 0x31 parms are 0x28 bytes");

    // Reply block of message 0x31 for pub::Player::ReturnBestPath: this 0xc-byte header, then
    // Count points.
    struct FBestPathHeader
    {
        unsigned int ClientCookie;                            // +0x00 = request +0x04
        int Count;                                            // +0x04
        // +0x08: same system -- set when BuildNnPath found no lane path (FUN_06f5e910 false,
        // SETE); across systems -- the failure flag (no route, a locked gate, a missing system
        // or a failed system path).
        bool bFlag8;
    };
    static_assert(sizeof(FBestPathHeader) == 0xc, "retail best path header is 0xc bytes");

    struct FBestPathPoint
    {
        Vector Position;                                      // +0x00
        unsigned int ObjectId;                                // +0x0c leg ring / connection
        unsigned int SystemId;                                // +0x10
    };
    static_assert(sizeof(FBestPathPoint) == 0x14, "retail best path point is 0x14 bytes");

    struct FGameSupport : IDLL
    {
        unsigned int ControllerId;                            // +4

        explicit FGameSupport(unsigned int InControllerId);
        ~FGameSupport() override;
        void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;

        // FUN_06f002e0 (message 5).
        static void OnPlayerLoad(FPlayerLoadMessage const* Message);
        // Message 0x33 body (06eff8d7-06effaec).
        static void OnPlayerStatsRequest(unsigned int const* PlayerId);
        // Message 0x36 body (06efeec1-06eff115).
        static void OnPlayerLaunch(unsigned int const* PlayerId);
        // Message 0x31 body (06eff207-06eff8d2).
        static void OnBestPathRequest(FBestPathRequest const* Request);
    };

    // The point loop shared by both halves of message 0x31 (06eff352-06eff410 and
    // 06eff6b8-06eff75f): per leg, its start point unless it equals the previous end point
    // (FCOMP x, y, z with TEST AH,0x44 / JP: any unequal or unordered component adds it) --
    // dropped again when it is the block's first point and has no object -- then its end
    // point. Last is the previous end point (all zero at the start); Index the block's point
    // count, which the caller caps. Returns the last point written (null when none).
    FBestPathPoint* AppendLegPoints(FBestPathPoint* Points, int* Index, Vector* Last,
                                    FNnPathLeg const* Leg, unsigned int SystemId)
    {
        if (!(Last->x == Leg->PointA.x && Last->y == Leg->PointA.y && Last->z == Leg->PointA.z))
        {
            FBestPathPoint* Start = &Points[*Index];
            Start->Position = Leg->PointA;
            Start->ObjectId = Leg->ObjectA;
            if (*Index != 0 || Leg->ObjectA != 0)
            {
                Start->SystemId = SystemId;
                ++*Index;
            }
        }
        FBestPathPoint* End = &Points[*Index];
        End->Position = Leg->PointB;
        End->SystemId = SystemId;
        *Last = End->Position;
        End->ObjectId = Leg->ObjectB;
        ++*Index;
        return End;
    }

    // GameSupport message 0x31 (06eff207-06eff8d2): the best path between two points, sent with
    // pub::Player::ReturnBestPath.
    void FGameSupport::OnBestPathRequest(FBestPathRequest const* Request)
    {
        if (Request->FromSystemId == Request->ToSystemId)
        {
            // 06eff221-06eff234: no system record -> no reply.
            FSystemInfo* System = FindSystemInfo(&Request->FromSystemId);        // FUN_06f75f30
            if (System == nullptr)
                return;
            System->Load(false);                                                // FUN_06f75830

            // 06eff242-06eff296: FUN_06f5e910(player, system, from, to, &legs, 0x19, &500.0f
            // (0x43fa0000), 1).
            std::list<FNnPathLeg> Legs;                                         // [ESP+0x30]
            float const Range = 500.0f;
            bool const bPath = BuildBestPathLegs(Request->PlayerId, System, &Request->FromPosition,
                                                 &Request->ToPosition, &Legs, 0x19, &Range, 1);

            // 06eff29a-06eff2a9: new (legs * 0x28 + 0xc) -- two points per leg at most.
            std::vector<unsigned char> Data(Legs.size() * 0x28 + sizeof(FBestPathHeader));
            FBestPathHeader* Header = reinterpret_cast<FBestPathHeader*>(Data.data());
            FBestPathPoint* Points =
                reinterpret_cast<FBestPathPoint*>(Data.data() + sizeof(FBestPathHeader));
            int Count = 0;                                                      // EDI / EDX
            Vector Last;
            Last.x = 0.0f;
            Last.y = 0.0f;
            Last.z = 0.0f;
            for (std::list<FNnPathLeg>::const_iterator Leg = Legs.begin(); Leg != Legs.end(); ++Leg)
                AppendLegPoints(Points, &Count, &Last, &*Leg, Request->FromSystemId);

            // 06eff2d7-06eff307: header {request +4, count, !bPath}; size count * 0x14 + 0xc.
            Header->ClientCookie = Request->ClientCookie;
            Header->Count = Count;
            Header->bFlag8 = !bPath;
            pub::Player::ReturnBestPath(Request->PlayerId, Data.data(),
                                        Count * static_cast<int>(sizeof(FBestPathPoint)) +
                                            static_cast<int>(sizeof(FBestPathHeader)));
            // 06eff301: the block is freed before the unload; the legs after it.
            Data.clear();
            Data.shrink_to_fit();
            System->Unload();                                                   // FUN_06f75840
            return;
        }

        // 06eff426-06eff44c: a 0x7c8 block (header + 99 points) and the gate path (type 1)
        // between the systems (FUN_06f29d80).
        // The port allocates two points more than retail: the cap test sits only at the loop
        // top (06eff6b8 CMP ESI,0x63 / JGE), so a leg that starts at point 98 writes point 99,
        // 0x14 bytes past retail's block, and the reply then reads it. The send size is
        // retail's; only the overrun lands in owned memory.
        int const MaxPoints = 0x63;
        std::vector<unsigned char> Data(0x7c8 + 2 * sizeof(FBestPathPoint));
        FBestPathHeader* Header = reinterpret_cast<FBestPathHeader*>(Data.data());
        FBestPathPoint* Points =
            reinterpret_cast<FBestPathPoint*>(Data.data() + sizeof(FBestPathHeader));
        std::list<unsigned int> Route =
            GetSystemPath(Request->FromSystemId, Request->ToSystemId, 1);

        // 06eff450-06eff47b: the gates locked for the player (or everybody, FUN_06f6d2b0).
        std::vector<unsigned int> LockedGates;                                  // [ESP+0x48]
        CollectContentGateLocks(Request->PlayerId, LockedGates);

        // 06eff480-06eff4a2: fewer than two systems on the route -> {request +4, 0, 1}.
        if (Route.size() < 2)
        {
            Header->ClientCookie = Request->ClientCookie;
            Header->Count = 0;
            Header->bFlag8 = true;
            pub::Player::ReturnBestPath(Request->PlayerId, Data.data(),
                                        static_cast<int>(sizeof(FBestPathHeader)));
            return;
        }

        // 06eff4a7-06eff4c0: the walk starts at the route's second system.
        unsigned int CurrentSystemId = Request->FromSystemId;                   // [ESP+0x24]
        unsigned int PreviousSystemId = CurrentSystemId;                        // EBX
        std::list<unsigned int>::const_iterator Next = Route.begin();
        ++Next;                                                                 // [ESP+0x28]
        int Count = 0;                                                          // ESI
        bool bFailed = false;                                                   // [ESP+0x1b]
        for (;;)
        {
            // 06eff4c5-06eff4e0: the next system; past the route's end, the target system.
            unsigned int NextSystemId;                                          // [ESP+0x2c]
            if (Next != Route.end())
            {
                NextSystemId = *Next;
                ++Next;
            }
            else
            {
                NextSystemId = Request->ToSystemId;
            }

            // 06eff4e4-06eff4f7: no system record -> failure.
            FSystemInfo* System = FindSystemInfo(&CurrentSystemId);              // [ESP+0x14]
            if (System == nullptr)
            {
                bFailed = true;
                break;
            }
            System->Load(false);

            // 06eff505-06eff5aa: where the trip through this system starts: the request's
            // point, or the jump in from the previous system (FUN_06f29c10 type 1, its
            // position FUN_06f2a190). A locked entry gate is a failure; an earlier failure
            // also stops here (only tested when gates are locked).
            bool bExitIsConnection = false;                                     // [ESP+0x13]
            unsigned int ExitObjectId = 0;                                      // [ESP+0x20]
            Vector Start;                                                       // [ESP+0x70]
            if (CurrentSystemId == Request->FromSystemId)
            {
                Start = Request->FromPosition;
            }
            else
            {
                unsigned int const EntryObjectId =
                    GetNextHopConnectionObject(CurrentSystemId, PreviousSystemId, 1);
                Vector Position;                                                // [ESP+0xa0]
                GetConnectionPosition(CurrentSystemId, EntryObjectId, &Position);
                Start = Position;
                if (!LockedGates.empty())
                {
                    if (std::find(LockedGates.begin(), LockedGates.end(), EntryObjectId) !=
                        LockedGates.end())
                    {
                        bFailed = true;
                        break;
                    }
                    if (bFailed)
                        break;
                }
            }

            // 06eff5b0-06eff61b: where it ends: the request's point, or the jump out to the
            // next system.
            Vector End;                                                         // [ESP+0x64]
            if (CurrentSystemId == Request->ToSystemId)
            {
                End = Request->ToPosition;
            }
            else
            {
                ExitObjectId = GetNextHopConnectionObject(CurrentSystemId, NextSystemId, 1);
                Vector Position;                                                // [ESP+0xac]
                GetConnectionPosition(CurrentSystemId, ExitObjectId, &Position);
                End = Position;
                bExitIsConnection = true;
            }

            // 06eff620-06eff687: BuildNnPath(system, start, end, &legs, 0x19, &1.0f, 0);
            // false -> failure, and the walk goes on.
            std::list<FNnPathLeg> Legs;                                         // [ESP+0x30]
            float const Range = 1.0f;
            if (!BuildNnPath(System, &Start, &End, &Legs, 0x19, &Range, 0))
            {
                bFailed = true;
            }
            else
            {
                // 06eff68c-06eff775: the points while fewer than 99; the last one written in
                // this system gets the exit connection as its object.
                FBestPathPoint* LastPoint = nullptr;
                Vector Last;
                Last.x = 0.0f;
                Last.y = 0.0f;
                Last.z = 0.0f;
                for (std::list<FNnPathLeg>::const_iterator Leg = Legs.begin();
                     Leg != Legs.end() && Count < MaxPoints; ++Leg)
                {
                    LastPoint = AppendLegPoints(Points, &Count, &Last, &*Leg, CurrentSystemId);
                }
                if (LastPoint != nullptr && bExitIsConnection)
                    LastPoint->ObjectId = ExitObjectId;
            }

            // 06eff778-06eff7f0: unload; the target reached (current == next) ends the walk,
            // otherwise step on while fewer than 99 points.
            System->Unload();
            if (CurrentSystemId == NextSystemId)
                break;
            PreviousSystemId = CurrentSystemId;
            CurrentSystemId = NextSystemId;
            if (Count >= MaxPoints)
                break;
        }

        // 06eff82c-06eff860: a failure sends no points.
        if (bFailed)
            Count = 0;
        Header->ClientCookie = Request->ClientCookie;
        Header->bFlag8 = bFailed;
        Header->Count = Count;
        pub::Player::ReturnBestPath(Request->PlayerId, Data.data(),
                                    Count * static_cast<int>(sizeof(FBestPathPoint)) +
                                        static_cast<int>(sizeof(FBestPathHeader)));
    }

    // GameSupport message 0x36 (06efeec1-06eff115): build the player ship.
    void FGameSupport::OnPlayerLaunch(unsigned int const* PlayerId)
    {
        FContentPlayerShipParams Params;                                        // FUN_06efff70
        Params.ShipId = 0;
        pub::Player::GetShip(*PlayerId, Params.ShipId);
        unsigned int ShipArch;
        pub::Player::GetShipID(*PlayerId, ShipArch);
        Params.ShipArchId = ShipArch;
        GetShipMissionProperties(ShipArch, &Params.ClassIds);                   // FUN_06f587d0

        // 06efef1b-06efef4b: the reputation handle takes the player state (release the
        // old one, AddRef the new one).
        FContentPlayerState* State = FindContentPlayerState(*PlayerId);          // FUN_06f49b00
        if (Params.ReputationHandle != State)
        {
            if (Params.ReputationHandle != nullptr)
                Params.ReputationHandle->Release();
            Params.ReputationHandle = State;
            if (State != nullptr)
                State->AddRef();
        }
        Params.System = State->GetSystemRecord();                               // FUN_06f478c0

        // 06efef66-06eff08f: fighters and freighters get their class id once.
        unsigned int Type;
        pub::SpaceObj::GetType(Params.ShipId, Type);
        unsigned int const* ClassId = nullptr;
        if ((Type & 0x10000) != 0)
            ClassId = &GIdFighter;                                              // DAT_06fcf400
        else if ((Type & 0x20000) != 0)
            ClassId = &GIdFreighter;                                            // DAT_06fcf3fc
        if (ClassId != nullptr &&
            std::find(Params.ClassIds.begin(), Params.ClassIds.end(), *ClassId) == Params.ClassIds.end())
        {
            Params.ClassIds.insert(Params.ClassIds.end(), *ClassId);           // FUN_06eae440
        }

        State->SetShipObject(new FContentPlayerShip(&Params));                   // FUN_06f471d0, FUN_06f47880
    }

    // FUN_06efeb10 (06efeb10-06efeb89).
    FGameSupport::FGameSupport(unsigned int InControllerId)
        : ControllerId(InControllerId)
    {
        pub::Controller::SetHeartbeatInterval(ControllerId, 1.0f);
        // 06efeb50-06efeb69: new 0x24 + FUN_06f058d0; the router ctor publishes
        // itself in GMessageRouter (DAT_06fcf254). The pointer is not kept here.
        new FContentMessageRouter;
        LoadContentGameTables();
    }

    // FUN_06efebb0 (06efebb0-06efec06).
    FGameSupport::~FGameSupport()
    {
        // 06efebdb: FUN_06efe870.
        UnloadContentGameTables();
        // 06efebe0-06efebee: DAT_06fcf254->vtbl[+0x10](1) = virtual deleting dtor.
        if (GMessageRouter != nullptr)
            delete GMessageRouter;
    }

    // FUN_06f002e0 (06f002e0-06f003d6).
    void FGameSupport::OnPlayerLoad(FPlayerLoadMessage const* Message)
    {
        FContentPlayerState* State = ReplaceContentPlayerState(Message->PlayerId);
        Message->Reader->reset();
        LoadContentPlayerState(State, Message->Reader);
    }

    // GameSupport message 0x33 (06eff8d7-06effaec): build the statistics block and
    // send it with pub::Player::ReturnPlayerStats. Parms = &player id.
    void FGameSupport::OnPlayerStatsRequest(unsigned int const* PlayerId)
    {
        FContentPlayerState* State = FindContentPlayerState(*PlayerId);
        if (State == nullptr)
            return;

        FGroupFeelingEnumerator Groups;
        pub::Player::GetRep(*PlayerId, Groups.Reputation);
        pub::Reputation::EnumerateGroups(Groups);
        int GroupCount = static_cast<int>(Groups.Feelings.size());

        unsigned int ShipTypeCount = State->GetShipTypesKilledCount();
        int GroupsOffset = static_cast<int>(ShipTypeCount * 8 + 0x34);
        int BlockSize = GroupsOffset + GroupCount * 8;
        unsigned char* Data = static_cast<unsigned char*>(operator new(BlockSize));
        FPlayerStatsBlock* Block = reinterpret_cast<FPlayerStatsBlock*>(Data);

        State->CopyShipTypesKilled(reinterpret_cast<unsigned int*>(Data + 0x34));
        Block->RandomMissionsCompleted = 0;
        Block->RandomMissionsAborted = 0;
        Block->RandomMissionsFailed = 0;
        Block->RandomMissionsCompleted += State->GetRandomMissionsCompleted(7);
        for (unsigned char MissionType = 0; MissionType < 6; ++MissionType)
        {
            Block->RandomMissionsCompleted += State->GetRandomMissionsCompleted(MissionType);
            Block->RandomMissionsAborted += State->GetRandomMissionsAborted(MissionType);
            Block->RandomMissionsFailed += State->GetRandomMissionsFailed(MissionType);
        }
        Block->BasesVisited = State->GetBasesVisitedCount();
        Block->HolesVisited = State->GetHolesVisitedCount();
        Block->SystemsVisited = State->GetSystemsVisitedCount();
        Block->TotalCashEarned = static_cast<float>(State->GetTotalCashEarned());
        Block->TotalTimePlayed = static_cast<float>(State->GetTotalTimePlayed());
        Block->ShipTypesKilledCount = ShipTypeCount;

        if (pub::SinglePlayer())
        {
            pub::Player::GetRank(*PlayerId, Block->Rank);
        }
        else
        {
            float Rank;
            pub::Reputation::GetRank(Groups.Reputation, Rank);
            Block->Rank = static_cast<int>(Rank);
        }

        int Cash;
        pub::Player::InspectCash(*PlayerId, Cash);
        float AssetValue = 0.0f;
        pub::Player::GetAssetValue(*PlayerId, AssetValue);
        Block->Worth = Cash + static_cast<int>(AssetValue);
        pub::Player::GetMoneyNeededToNextRank(*PlayerId, Block->MoneyNeededToNextRank);

        Block->GroupCount = GroupCount;
        FGroupFeeling* GroupOut = reinterpret_cast<FGroupFeeling*>(Data + GroupsOffset);
        for (int Index = 0; Index < GroupCount; ++Index)
            GroupOut[Index] = Groups.Feelings[Index];

        pub::Player::ReturnPlayerStats(*PlayerId, Data, BlockSize);
        operator delete(Data);
    }

    // LAB_06efec50 (06efec50-06effecd). Messages above 0x41 and the unlisted
    // ids in 0..0x41 fall through to the common return.
    void FGameSupport::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
    {
        using pub::Controller::EMessage;
        switch (MessageId)
        {
        case EMessage::PlayerWorthChanged:
        {
            // 06effb34
            FPlayerIdMessage const* Message = static_cast<FPlayerIdMessage const*>(Parms);
            RecalculateContentPlayerRank(FindContentPlayerState(Message->PlayerId));
            break;
        }
        case EMessage::BaseEnter:
        {
            // 06eff17e: base enter.
            FBaseMessage const* Message = static_cast<FBaseMessage const*>(Parms);
            FindContentPlayerState(Message->PlayerId)->OnBaseChange(Message->BaseId);
            FContentBase* Base = FindContentBaseInfo(&Message->BaseId);
            if (Base != nullptr)
            {
                Base->Load(nullptr);                            // 06eff1b2 FUN_06eaa820
                Base->OnPlayerEnter(Message->PlayerId);         // 06eff1bc FUN_06eaa3e0
            }
            break;
        }
        case EMessage::BaseExit:
        {
            // 06eff1c6: base exit.
            FBaseMessage const* Message = static_cast<FBaseMessage const*>(Parms);
            FContentBase* Base = FindContentBaseInfo(&Message->BaseId);
            FContentPlayerState* State = FindContentPlayerState(Message->PlayerId);
            if (State != nullptr)
            {
                if (Base != nullptr)
                    Base->OnPlayerExit(Message->PlayerId);      // 06eff1f4 FUN_06eaa790
                State->OnBaseChange(0);
            }
            break;
        }
        case EMessage::PlayerSave:
        {
            // 06effb50
            FPlayerSaveMessage const* Message = static_cast<FPlayerSaveMessage const*>(Parms);
            FindContentPlayerState(Message->PlayerId)->Save(Message->File);
            break;
        }
        case EMessage::PlayerLoad:
            // 06effb70
            OnPlayerLoad(static_cast<FPlayerLoadMessage const*>(Parms));
            break;
        case EMessage::Heartbeat:
        {
            // 06efec8a: heartbeat, parms = float elapsed.
            float DeltaTime = *static_cast<float const*>(Parms);
            GGameSupportElapsed = GGameSupportElapsed + DeltaTime;
            UpdateAllContentPlayerTimes(DeltaTime);
            break;
        }
        case EMessage::LocationEnter:
        {
            // 06effb12: location enter.
            FLocationMessage const* Message = static_cast<FLocationMessage const*>(Parms);
            FindContentPlayerState(Message->PlayerId)->SetLocation(Message->LocationId);
            break;
        }
        case EMessage::LocationExit:
        {
            // 06effaf1: location exit.
            FLocationMessage const* Message = static_cast<FLocationMessage const*>(Parms);
            FindContentPlayerState(Message->PlayerId)->SetLocation(0);
            break;
        }
        case EMessage::SpaceExit:
        {
            // 06eff11a
            FLocationMessage const* Message = static_cast<FLocationMessage const*>(Parms);
            FindContentPlayerState(Message->PlayerId)->SetShipObject(nullptr);
            break;
        }
        case EMessage::ObjectDestroyed:
        {
            // 06efecd8: parms = ship id.
            FContentPlayerState* State =
                FindContentPlayerStateByShip(static_cast<unsigned int const*>(Parms));
            if (State != nullptr)
            {
                State->SetShipObject(nullptr);
                State->bShipDestroyed = 1;
            }
            break;
        }
        case EMessage::SystemExit:
        {
            // 06eff15d: system exit.
            FSystemMessage const* Message = static_cast<FSystemMessage const*>(Parms);
            FindContentPlayerState(Message->PlayerId)->OnSystemChange(0);
            break;
        }
        case EMessage::PlayerLogout:
            // 06effb82: logout, parms = &player id.
            RemoveContentPlayerState(*static_cast<unsigned int const*>(Parms));
            break;
        case EMessage::RequestPlayerStats:
            // 06eff8d7: player stats request.
            OnPlayerStatsRequest(static_cast<unsigned int const*>(Parms));
            break;
        case EMessage::SystemEnter:
        {
            // 06eff13b: system enter.
            FSystemMessage const* Message = static_cast<FSystemMessage const*>(Parms);
            FindContentPlayerState(Message->PlayerId)->OnSystemChange(Message->SystemId);
            break;
        }
        case EMessage::LoadGame:
        {
            // 06effe5e: load a save, parms = INI_Reader*.
            INI_Reader* Reader = static_cast<INI_Reader*>(const_cast<void*>(Parms));
            LoadDynBaseInfo(Reader);                            // FUN_06eabf40
            Reader->reset();
            LoadContentGateLocks(Reader);                       // FUN_06f67cd0
            Reader->reset();
            LoadDynSysInfo(Reader);                             // FUN_06f76200
            Reader->reset();
            InitSystemPathTables();                             // 06effe92 FUN_06f29a90
            break;
        }
        case EMessage::SaveGame:
        {
            // 06effe99: write a save, parms = FILE*.
            FILE* File = static_cast<FILE*>(const_cast<void*>(Parms));
            SaveContentBases(File);                             // FUN_06eac090
            SaveContentGateLocks(File);                         // FUN_06f67bc0
            VisitSystemInfos();                                 // FUN_06f76350
            break;
        }
        case EMessage::TradelaneEnter:
        {
            // 06efed55-06efee93: a ship enters a trade lane; parms = {ship, entry ring, target
            // ring, system}.
            unsigned int const* Message = static_cast<unsigned int const*>(Parms);
            FSystemInfo* System = FindSystemInfo(&Message[3]);                  // FUN_06f75f30
            FContentShipBase* Ship =
                static_cast<FContentShipBase*>(System->FindShip(Message[0]));   // FUN_06f73930
            if (Ship == nullptr)
                break;
            FTradeLane* Lane;
            System->FindTradeLaneByRing(&Message[1], &Lane);                    // FUN_06f77e00
            // 06efed96-06efee08: the indices of the entry and the target ring (the scan
            // stops once both are known).
            std::vector<FTradeLaneRing> const& Rings = Lane->Rings;
            unsigned int EntryIndex = 0xffffffffu;
            unsigned int TargetIndex = 0xffffffffu;
            for (unsigned int Index = 0; Index < Rings.size(); ++Index)
            {
                unsigned int const RingId = Rings[Index].ObjectId;
                if (RingId == Message[1])
                {
                    EntryIndex = Index;
                    if (TargetIndex != 0xffffffffu)
                        break;
                }
                else if (RingId == Message[2])
                {
                    TargetIndex = Index;
                    if (EntryIndex != 0xffffffffu)
                        break;
                }
            }
            unsigned int ShipId = Ship->SpaceObjId;
            // 06efee0a-06efee12: JAE (unsigned) -- forward when the entry comes first.
            if (EntryIndex < TargetIndex)
            {
                Ship->SetField120(&Message[1], &Rings.back().ObjectId);         // FUN_06f576f0
                int const Direction = 1;
                System->AddTradeLaneShip(&Message[1], &Direction, &ShipId);     // FUN_06f78110
            }
            else
            {
                Ship->SetField120(&Message[1], &Rings.front().ObjectId);
                int const Direction = -1;
                System->AddTradeLaneShip(&Message[1], &Direction, &ShipId);
            }
            break;
        }
        case EMessage::TradelaneExit:
        {
            // 06efed04-06efed50: a ship leaves a trade lane; parms = {ship, ring, ?, system}.
            unsigned int const* Message = static_cast<unsigned int const*>(Parms);
            FSystemInfo* System = FindSystemInfo(&Message[3]);                  // FUN_06f75f30
            FContentShipBase* Ship =
                static_cast<FContentShipBase*>(System->FindShip(Message[0]));   // FUN_06f73930
            if (Ship == nullptr || System == nullptr)
                break;
            unsigned int const NoRing = 0;                                      // DAT_06fb7d60
            Ship->SetField120(&NoRing, &NoRing);                                // FUN_06f576f0
            System->RemoveTradeLaneShip(&Message[0], &Message[1]);              // FUN_06f78dd0
            break;
        }
        case EMessage::Msg0x25:
        {
            // 06efee98: parms = {ship, target}.
            unsigned int const* Message = static_cast<unsigned int const*>(Parms);
            FContentPlayerState* State = FindContentPlayerStateByShip(Message);   // FUN_06f49bc0
            if (State != nullptr)
                State->ShipObject->OnTargetChanged(Message + 1);                  // FUN_06f47430
            break;
        }
        case EMessage::SpaceEnter:
            // 06efeec1: player launch. 06efeed6-06efeee4: MOV EDX,[parms]; LEA ESI,[EDX+4] --
            // the player id is the SECOND dword (GetShip, GetShipID and FUN_06f49b00 all get
            // ESI), like messages 1 and 0x37.
            OnPlayerLaunch(static_cast<unsigned int const*>(Parms) + 1);
            break;
        case EMessage::RequestBestPath:
            // 06eff207: best path request.
            OnBestPathRequest(static_cast<FBestPathRequest const*>(Parms));
            break;
        default:
            break;
        }
    }
}

namespace
{
    // Retail DAT_06fcf42c (.data, zero at load): the last formation id handed out.
    unsigned int GLastFormationId = 0;
}

// FUN_06efeb00 (06efeb00-06efeb0b): pre-increment of DAT_06fcf42c. Declared in DockQueues.h.
unsigned int __cdecl AllocateFormationId()
{
    return ++GLastFormationId;
}

// FUN_06efec10 (06efec10-06efec16): FLD double DAT_06fcf430.
double __cdecl GetContentGameTime()
{
    return GGameSupportElapsed;
}

// FUN_06efec20 (06efec20-06efec4e): either time equal to the -1.0 sentinel (raw
// compare of both dwords with 0 / 0xbff00000) -> -1.0 (DAT_06fb78f0); else FABS(*A - *B).
double __cdecl GetContentTimeDistance(double const* A, double const* B)
{
    if (*A == -1.0 || *B == -1.0)
        return -1.0;
    return std::fabs(*A - *B);
}

IDLL* CreateGameSupport(char const* /*Name*/, unsigned int ControllerId, void const* /*Parms*/)
{
    return new (std::nothrow) FGameSupport(ControllerId);
}
