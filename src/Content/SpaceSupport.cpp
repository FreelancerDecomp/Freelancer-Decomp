// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU53 (06f5c760-06f6b840): the SpaceSupport controller and the
// locked-gate table. CreateInstance("SpaceSupport") -> new 4 -> FUN_06f5cd10;
// vtable 06fbbc08 (slot 0 FUN_06f5d560 HandleMessage, slot 1 FUN_06f5cd80
// deleting dtor = IDLL::~IDLL only).

#include "SpaceSupport.h"

#include "CombatReputation.h"   // TU56: OnDamageReputation / ApplyKillReputation / OnHeartbeatReputation
#include "ContentInternal.h"
#include "ContentMessageRouter.h"
#include "ContentPlayerState.h"
#include "ContentSort.h"        // ContentSort (the VC6 std::sort FUN_06f6ab00)
#include "DockQueues.h"
#include "FactionProps.h"       // HandleFactionNameTimer (FUN_06f513c0)
#include "Gcs.h"                // GGcsHandlers, GGcsConversations, the category setters
#include "Guides.h"             // FContentGuide::SetShip (slot +0x20)
#include "LootProps.h"          // FindLootProps & co., CollectPhantomLoot (TU35)
#include "MissionObject.h"      // FNnPathLeg
#include "NpcShip.h"
#include "SpacePaths.h"         // BuildNnPath (FUN_06f5ea40)
#include "Populators.h"         // FSpacePopExclusionSphere
#include "RandomMissionDB.h"    // GetShipMissionProperties (TU51 FUN_06f587d0)
#include "SystemInfo.h"
#include "TradelaneRequests.h"  // HandleTradelaneRequest / Release (TU55, msgs 0x14 / 0x15)
#include "common_goods.h"       // EquipDesc
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"
#include "server_cobject.h"     // pub::SpaceObj::Destroy, DestroyType
#include "server_gameobject.h"  // FObjectDestroyedEvent, FSubObjDamageEvent, FEquipLootEvent

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <vector>

// Retail DAT_06fd1c74: the SpaceSupport controller id (the object itself is
// only a vtable). Declared in ContentInternal.h; the message router sets its
// timers on it.
unsigned int GSpaceSupportControllerId = 0;

// DAT_06fd1b9c (LAB_06f5cc60 / FUN_06f5cca0). See SpaceSupport.h.
std::list<FContentNpcShip*> GSpaceSupportGuidedShips;

namespace
{
    // Retail DAT_06fce770 (.data, only read): heartbeat period.
    float GSpaceSupportHeartbeat = 1.0f;

    // Retail DAT_06fd1c2c, static init LAB_06f5c960: the next module-wide unique id
    // (DAT_06fce76c post-increment). Only read by the timer case (06f5d72f): that timer
    // cancels the player's music. GContentNextUniqueId is constant-initialised, so it is
    // ready before this dynamic initialiser runs.
    unsigned int GSpaceSupportMusicTimerId = GContentNextUniqueId++;

    struct FGateLock
    {
        unsigned int GateId;
        std::vector<unsigned int> PlayerIds;
    };

    // Retail DAT_06fd1ca8-06fd1cb4: an ordered vector of {gate, players} (0x14 stride).
    std::vector<FGateLock> GContentGateLocks;

    bool ContainsId(std::vector<unsigned int> const& Values, unsigned int Value)
    {
        for (int Index = 0; Index < static_cast<int>(Values.size()); ++Index)
        {
            if (Values[Index] == Value)
                return true;
        }
        return false;
    }

    struct FSpaceSupport : IDLL
    {
        explicit FSpaceSupport(unsigned int InControllerId);
        void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;

        // FUN_06f5cda0.
        void InitGcs();
        // Case 0x39 of HandleMessage (06f5d8ca-06f5dc1d).
        void OnObjectDestroyed(FObjectDestroyedEvent* Event);
        // FUN_06f5cfa0 (RET 0x10; this is not read): the loot of ShipId in Items --
        // duplicates merged, counts scaled by the worth of the players near the ship, a
        // random health, and with bAddPhantomLoot the [PhantomLoot] of its rank.
        void PrepareLoot(FSystemInfo* System, unsigned int ShipId, std::vector<EquipDesc>* Items,
                         bool bAddPhantomLoot);
        // FUN_06f68960 (RET 4; this is not read; caller msg 0x36 06f5e10d in multiplayer):
        // every solar of the system takes the launching player's feeling towards its
        // affiliation as its attitude towards him when the two differ by more than 0.01.
        void SyncSolarAttitudes(unsigned int const* Msg);
        // FUN_06f5e680 (RET 8; this and System are not read; caller OnObjectDestroyed
        // 06f5da20 for a destroyed NPC ship): a destroyed formation leader hands its
        // formation, directives and guide to a new leader (true), or with no NPC wingman
        // left frees the player wingmen (false). False for a wingman or no ship.
        bool PromoteFormationLeader(FSystemInfo* System, FContentNpcShip* Ship);
        // FUN_06f5e550 (RET 4; this is not read): the new leader among Ships -- NPC ships
        // only (vtable slot 1), a LARGE_SHIPS member over any other, then the higher
        // reputation rank (the first wins a tie); it is moved to the front of Ships.
        FContentShipBase* PickFormationLeader(std::list<FContentShipBase*>* Ships);
    };

    // FUN_06f68960 (06f68960-06f68ab9). Msg = {system, player, ...} of message 0x36.
    void FSpaceSupport::SyncSolarAttitudes(unsigned int const* Msg)
    {
        // 06f68969-06f6898e: the player's ship; none -> nothing.
        unsigned int ShipId = 0;                                            // [ESP+0x1c]
        pub::Player::GetShip(Msg[1], ShipId);
        if (ShipId == 0)
            return;

        // 06f68994-06f689c3: up to 1000 objects of the system, any distance from the origin
        // (radius FLT_MAX 0x7f7fffff, type mask 0xfdff).
        unsigned int Objects[0x3e8];                                        // [ESP+0x34]
        unsigned int Count = 0;                                             // [ESP+0x14]
        Vector Origin;                                                      // [ESP+0x28]
        Origin.x = 0.0f;
        Origin.y = 0.0f;
        Origin.z = 0.0f;
        pub::System::ScanObjects(Msg[0], Objects, 0x3e8, Origin, 3.40282347e+38f, 0xfdff, Count);

        // 06f689c9-06f689f9: the ship's reputation (-1 first); a failed call, no
        // reputation or no object ends it.
        int PlayerRep = -1;                                                 // [ESP+0xc]
        if (pub::SpaceObj::GetRep(ShipId, PlayerRep) != 0)
            return;
        if (PlayerRep == -1 || Count == 0)
            return;

        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            // 06f68a10-06f68a40: the solar's reputation and its affiliation (both -1 first);
            // either call failing skips the object.
            int SolarRep = -1;                                              // [ESP+0x10]
            unsigned int Affiliation = 0xffffffffu;                         // [ESP+0x20]
            if (pub::SpaceObj::GetSolarRep(Objects[Index], SolarRep) != 0)
                continue;
            if (pub::Reputation::GetAffiliation(SolarRep, Affiliation) != 0)
                continue;

            // 06f68a42-06f68a6a: its attitude towards the player and the player's feeling
            // towards its affiliation (both 0 first; results ignored).
            float Attitude = 0.0f;                                          // [ESP+0x24]
            float Feeling = 0.0f;                                           // [ESP+0x18]
            pub::Reputation::GetAttitude(SolarRep, PlayerRep, Attitude);
            pub::Reputation::GetReputation(PlayerRep, Affiliation, Feeling);

            // 06f68a70-06f68a88: FABS(attitude - feeling) FCOMP double 0.01 (DAT_06fbbb88),
            // TEST AH,0x41 / JNE skips not-greater and unordered.
            if (std::fabs(static_cast<double>(Attitude) - static_cast<double>(Feeling)) > 0.01)
                pub::Reputation::SetAttitude(SolarRep, PlayerRep, Feeling);
        }
    }

    // Retail DAT_06fd1b98, static init LAB_06f5cf70 (MOV [06fd1b98],0x459c4000): the radius
    // of the players whose worth scales a loot drop (FUN_06f5cfa0 06f5d0ab).
    float GSpaceSupportLootRadius = 5000.0f;

    // FUN_06f5e550 (06f5e550-06f5e676).
    FContentShipBase* FSpaceSupport::PickFormationLeader(std::list<FContentShipBase*>* Ships)
    {
        unsigned int const LargeShips = CreateID("LARGE_SHIPS");             // [ESP+0x14]
        FContentShipBase* Best = nullptr;                                   // [ESP+0x10]
        std::list<FContentShipBase*>::iterator BestIt = Ships->end();       // EBX
        for (std::list<FContentShipBase*>::iterator It = Ships->begin(); It != Ships->end();
             ++It)
        {
            FContentShipBase* Candidate = (*It)->Slot04() ? *It : nullptr;
            if (Best == nullptr)
            {
                if (Candidate != nullptr)
                {
                    Best = Candidate;
                    BestIt = It;
                }
                continue;
            }
            if (Candidate == nullptr)
                continue;

            // 06f5e5af-06f5e607: a large ship beats a non-large one; otherwise (both large or
            // neither) the candidate needs a strictly higher rank (CMP / JGE keeps Best).
            bool bCompareRanks = false;
            if (Candidate->IsInShipClass(&LargeShips))                      // FUN_06f575d0
            {
                if (!Best->IsInShipClass(&LargeShips))
                {
                    Best = Candidate;
                    BestIt = It;
                    continue;
                }
                bCompareRanks = true;
            }
            else if (!Best->IsInShipClass(&LargeShips))
            {
                bCompareRanks = true;
            }
            if (bCompareRanks)
            {
                int const CandidateRank = Candidate->GetReputationRank();   // FUN_06f33e40
                if (Best->GetReputationRank() < CandidateRank)
                {
                    Best = Candidate;
                    BestIt = It;
                }
            }
        }

        // 06f5e61c-06f5e66b: the chosen node is unlinked and freed (--size), the leader
        // pushed at the front. Retail unlinks whatever EBX holds; with no NPC ship it would
        // be the list object itself -- the only caller always has one.
        Ships->erase(BestIt);
        Ships->push_front(Best);
        return Best;
    }

    // FUN_06f5e680 (06f5e680-06f5e8ff).
    bool FSpaceSupport::PromoteFormationLeader(FSystemInfo* /*System*/, FContentNpcShip* Ship)
    {
        // 06f5e69c-06f5e6bb: no ship, or a wingman (+0x14c != 0 and +0x154 != -1) -> false.
        if (Ship == nullptr)
            return false;
        if (Ship->FormationSlot != 0 && Ship->FormationId != 0xffffffffu)
            return false;

        // 06f5e6c1-06f5e705: the formation's members (EH states 0..2: the vector, then the two
        // lists).
        std::vector<FContentShipBase*> Members;                             // [ESP+0x3c]
        std::list<FContentShipBase*> NpcWingmen;                            // [ESP+0x24]
        std::list<FContentShipBase*> PlayerWingmen;                         // [ESP+0x30]
        CollectFormationMembers(&Members, Ship->FormationId);               // FUN_06f66220

        // 06f5e70a-06f5e778: the wingmen, split by whether a player flies them
        // (FUN_06f49bc0 on the SpaceObj id), FUN_06f1e390 push_back.
        for (std::vector<FContentShipBase*>::iterator It = Members.begin(); It != Members.end();
             ++It)
        {
            FContentShipBase* Member = *It;
            if (Member->FormationSlot == 0 || Member->FormationId == 0xffffffffu)
                continue;
            unsigned int MemberId = Member->SpaceObjId;                     // [ESP+0x14]
            if (FindContentPlayerStateByShip(&MemberId) == nullptr)
                NpcWingmen.push_back(Member);
            else
                PlayerWingmen.push_back(Member);
        }

        if (NpcWingmen.empty())
        {
            // 06f5e83d-06f5e884: no NPC wingman -- every player wingman (looked up again in
            // its system, FUN_06eea860 + FUN_06f73930) still in a formation stops docking
            // behind the leader and leaves the formation (-1, 0, null).
            for (std::list<FContentShipBase*>::iterator It = PlayerWingmen.begin();
                 It != PlayerWingmen.end(); ++It)
            {
                FContentShipBase* Wingman =
                    static_cast<FContentShipBase*>((*It)->GetSystem()->FindShip((*It)->SpaceObjId));
                if (Wingman == nullptr || Wingman->FormationId == 0xffffffffu)
                    continue;
                CancelDockingBehindLeader(Wingman, Wingman->FormationId);  // FUN_06f6ee40
                Wingman->SetFormation(0xffffffffu, 0, nullptr);              // FUN_06f576c0
            }
            return false;
        }

        // 06f5e788-06f5e7ad: the player wingmen go after the NPC ones.
        for (std::list<FContentShipBase*>::iterator It = PlayerWingmen.begin();
             It != PlayerWingmen.end(); ++It)
            NpcWingmen.push_back(*It);

        // 06f5e7af-06f5e7e6: the new leader (FUN_06f5e550), a new formation of the same
        // layout (FUN_06f66ec0(&ships, +0x150, 0)), the old leader's directives
        // (FUN_06f579c0) and its guide (+0x178, FUN_06f34b70(guide, 1, 0)).
        FContentShipBase* NewLeader = PickFormationLeader(&NpcWingmen);
        FormFormation(&NpcWingmen, Ship->Formation, false);
        NewLeader->FollowDirectivesOf(Ship);
        static_cast<FContentNpcShip*>(NewLeader)->SetGuide(Ship->GetGuide(), true, false);

        // 06f5e7eb-06f5e802: _SendMessage(&0 (DAT_06fbbb78), 0x1009, {new leader, old leader}).
        struct FLeaderChangedMsg
        {
            FContentShipBase* NewLeader;
            FContentShipBase* OldLeader;
        };
        FLeaderChangedMsg Msg;                                              // [ESP+0x18]
        Msg.NewLeader = NewLeader;
        Msg.OldLeader = Ship;
        unsigned int const ControllerId = 0;
        pub::Controller::_SendMessage(ControllerId, pub::Controller::EMessage::FormationLeaderChanged,
                                      &Msg);
        return true;
    }

    // FUN_06f5cd10 (06f5cd10-06f5cd71).
    FSpaceSupport::FSpaceSupport(unsigned int InControllerId)
    {
        GSpaceSupportControllerId = InControllerId;
        pub::Controller::SetHeartbeatInterval(GSpaceSupportControllerId, GSpaceSupportHeartbeat);
        InitGcs();
    }

    // FUN_06f5cda0 (06f5cda0-06f5cf44): the GCS handlers 0, 1, 2 (FUN_06ee9740 with a local
    // int), the category intervals (FUN_06eeba50; 12.0f DAT_06fbbbb0, 30.0f DAT_06fbbbb4,
    // 3.0f DAT_06fbbbb8, twice that computed as FLD + FADD ST0,ST0, category 7 a pushed 0),
    // the default interval (FUN_06eeba30, the float 15.0f DAT_06fbbbbc widened), the chances
    // (FUN_06eebb00) and the speaker / listener categories (FUN_06eebbb0 / FUN_06eebc60).
    void FSpaceSupport::InitGcs()
    {
        int Type = 0;
        GGcsHandlers.AddHandler(&Type);
        Type = 1;
        GGcsHandlers.AddHandler(&Type);
        Type = 2;
        GGcsHandlers.AddHandler(&Type);

        SetGcsCategoryInterval(2, 12.0f);
        SetGcsCategoryInterval(0x19, 30.0f);
        SetGcsDefaultInterval(static_cast<double>(15.0f));
        SetGcsCategoryChance(7, 0.4f);
        SetGcsCategoryChance(6, 0.05f);
        SetGcsCategoryInterval(6, 3.0f + 3.0f);
        SetGcsCategoryInterval(7, 0.0f);
        SetGcsCategoryInterval(8, 3.0f + 3.0f);
        SetGcsCategoryInterval(9, 3.0f);
        SetGcsCategoryInterval(0xa, 3.0f);
        SetGcsCategoryInterval(0xb, 3.0f);
        SetGcsCategoryInterval(0xd, 3.0f);
        SetGcsCategoryInterval(0xe, 3.0f);
        SetGcsCategoryInterval(0xf, 3.0f);
        SetGcsCategoryInterval(0x10, 3.0f);

        int const Categories[] = {6, 7, 8, 0xa, 0xb, 0xd, 0xe, 0xf, 0x10};
        for (int Index = 0; Index < static_cast<int>(sizeof(Categories) / sizeof(Categories[0]));
             ++Index)
        {
            EnableGcsSpeakerCategory(Categories[Index]);
            EnableGcsListenerCategory(Categories[Index]);
        }
    }

    // FUN_06f5cfa0 (06f5cfa0-06f5d550). Callers: case 0x13 06f5d7f6 (one item, no phantom
    // loot) and OnObjectDestroyed 06f5d9c7 (the dropped equipment, phantom loot on).
    void FSpaceSupport::PrepareLoot(FSystemInfo* System, unsigned int ShipId,
                                    std::vector<EquipDesc>* Items, bool bAddPhantomLoot)
    {
        // 06f5cfbe-06f5cfd0: FUN_06f6ab00 = the VC6 std::sort of vector<EquipDesc> with
        // LAB_06f5cf80 (CMP [A+4],[B+4]; SBB; NEG: A.ArchId < B.ArchId, unsigned).
        ContentSort(Items->data(), Items->data() + Items->size(),
                    [](EquipDesc const& A, EquipDesc const& B) { return A.ArchId < B.ArchId; });

        // 06f5cfd5-06f5d072: runs of one arch id fold into their first item -- set_count(own
        // + next) and the next erased (copy-down, Common's vector<EquipDesc>::_Destroy of the
        // last slot, end -= 0x20) -- then the walk resumes at the first different item.
        std::vector<EquipDesc>::iterator Current = Items->begin();
        while (Current != Items->end())
        {
            std::vector<EquipDesc>::iterator Next = Current + 1;
            while (Next != Items->end() && Next->ArchId == Current->ArchId)
            {
                Current->set_count(Current->Count + Next->Count);
                Next = Items->erase(Next);
            }
            Current = Next;
        }

        // 06f5d074-06f5d08c: FUN_06f73930 (the result of FindSystemInfo is not tested by
        // either caller); no ship -> done.
        FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(ShipId));
        if (Ship == nullptr)
            return;

        // 06f5d092-06f5d0c3: the players within DAT_06fd1b98 of the ship (FUN_06eb70f0,
        // FUN_06f72ea0).
        std::vector<FContentPlayerState*> Players;                          // [ESP+0x28]
        Vector Position;
        Ship->GetPosition(&Position);
        System->FindPlayersNear(&Position, &GSpaceSupportLootRadius, &Players);

        // 06f5d0c8-06f5d108: their worth summed in a float (FUN_06f48070, FADD float + FSTP
        // float per player) in the reused Items argument slot [ESP+0x70].
        float Worth = 0.0f;
        for (std::vector<FContentPlayerState*>::iterator It = Players.begin();
             It != Players.end(); ++It)
            Worth = static_cast<float>((*It)->GetWorth() + static_cast<double>(Worth));

        if (!Items->empty())
        {
            // 06f5d116-06f5d11f: ftol (truncation).
            int const WorthValue = static_cast<int>(Worth);                 // [ESP+0x20]

            // The two out-params live in reused argument slots and keep their value when the
            // pub:: call does not write it: GoodId starts as ShipId ([ESP+0x6c]), the
            // commodity flag as the low byte of the float sum ([ESP+0x70]).
            unsigned int GoodId = ShipId;
            unsigned int WorthBits = 0;
            std::memcpy(&WorthBits, &Worth, sizeof(WorthBits));
            bool bCommodity = (WorthBits & 0xff) != 0;

            for (std::vector<EquipDesc>::iterator Item = Items->begin(); Item != Items->end();
                 ++Item)
            {
                // 06f5d123-06f5d171: the loot properties of the item's good, else the
                // default commodity / equipment record (not null-tested).
                unsigned int const ArchId = Item->ArchId;                   // [ESP+0x68]
                pub::TranslateArchToGood(ArchId, GoodId);
                pub::IsCommodity(GoodId, bCommodity);
                FLootProps* Props = FindLootProps(&GoodId);                 // FUN_06f05060
                if (Props == nullptr)
                {
                    if (bCommodity)
                        Props = FindDefaultCommodityLootProps();            // FUN_06f05000
                    else
                        Props = FindDefaultEquipmentLootProps();            // FUN_06f05030
                }

                int const Count = Item->Count;                              // [ESP+0x10]
                int NewCount = 0;                                           // EDI
                // 06f5d17f-06f5d185: worth below drop_properties[1] (signed JL) -> 0.
                if (WorthValue >= Props->DropProperties[0])
                {
                    // 06f5d18b-06f5d1d1: the excess worth, per drop_properties[2] (IDIV),
                    // capped at the count, rounded (floor(x + 0.5 DAT_06fb74e0), ftol); a
                    // divisor 0 keeps the count; no excess gives 0.
                    int const Excess = WorthValue - Props->DropProperties[0];
                    int Scaled = 0;                                         // EAX
                    if (Excess > 0)
                    {
                        Scaled = Count;
                        if (Props->DropProperties[1] != 0)
                        {
                            int Capped = Excess / Props->DropProperties[1];
                            if (Capped >= Count)
                                Capped = Count;
                            Scaled = static_cast<int>(floor(static_cast<double>(Capped) + 0.5));
                        }
                    }

                    // 06f5d1d6-06f5d1f5: + drop_properties[3], at most drop_properties[4],
                    // then at most the count.
                    int Bounded = Props->DropProperties[3];
                    if (Props->DropProperties[2] + Scaled < Props->DropProperties[3])
                        Bounded = Scaled + Props->DropProperties[2];
                    if (Bounded >= Count)
                        Bounded = Count;

                    // 06f5d1f9-06f5d23e: x = (float)(n * drop chance); floor(x) items, and one
                    // more when rand()/32768 < the float fraction (FCOMP, TEST AH,5, JP skips
                    // not-less and unordered).
                    float const Expected = static_cast<float>(
                        static_cast<double>(Bounded) * static_cast<double>(Props->DropChance));
                    NewCount = static_cast<int>(floor(static_cast<double>(Expected)));
                    float const Fraction = static_cast<float>(
                        static_cast<double>(Expected) - floor(static_cast<double>(Expected)));
                    if (static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) <
                        static_cast<double>(Fraction))
                        ++NewCount;

                    // 06f5d262-06f5d269: at most drop_properties[5].
                    if (NewCount >= Props->DropProperties[4])
                        NewCount = Props->DropProperties[4];
                }

                // 06f5d26b-06f5d29a: health 0.5 + rand()/32768 * 0.3 (DAT_06fbbc10 is the
                // double of 0.3f, DAT_06fb74e0 0.5), then the count.
                float const Status = static_cast<float>(
                    static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) *
                        static_cast<double>(0.3f) +
                    0.5);
                Item->set_status(Status);
                Item->set_count(NewCount);
            }
        }

        // 06f5d2b2-06f5d2d9: phantom loot only when asked for, for a ship whose vtable slot 1
        // is true, and whose affiliation (FUN_06eb7070) is not 0x62e8.
        if (bAddPhantomLoot && Ship->Slot04() && Ship->GetAffiliation() != 0x62e8)
        {
            // 06f5d2df-06f5d30f: the records for the ship's reputation rank (FUN_06f33e40,
            // kept in the reused flag slot [ESP+0x74]; FUN_06f05440).
            std::vector<FPhantomLoot const*> Phantoms;                     // [ESP+0x10]
            int const Rank = Ship->GetReputationRank();
            CollectPhantomLoot(&Rank, &Phantoms);
            for (std::vector<FPhantomLoot const*>::iterator It = Phantoms.begin();
                 It != Phantoms.end(); ++It)
            {
                // 06f5d330-06f5d358: rand()/32768 < percent_chance (TEST AH,5 / JP skips).
                float const Chance = (*It)->PercentChance;
                if (!(static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) <
                      static_cast<double>(Chance)))
                    continue;

                // 06f5d35e-06f5d39a: a new item of the record's nickname, count
                // num_to_drop[0] + rand() * (range + 1) / 32768 (signed SAR).
                EquipDesc Desc;
                Desc.set_arch_id((*It)->Nickname.Id);
                int const Range = (*It)->NumToDropMax - (*It)->NumToDropMin + 1;
                Desc.set_count(rand() * Range / 32768 + (*It)->NumToDropMin);

                // 06f5d3a0-06f5d50e: the inlined VC6 insert(end, 1, Desc).
                Items->push_back(Desc);
            }
        }
    }

    // The dword at Msg[Index] read as a float (retail FLD / PUSH of the parms slot).
    float MsgFloat(unsigned int const* Msg, int Index)
    {
        float Value;
        std::memcpy(&Value, &Msg[Index], sizeof(Value));
        return Value;
    }

    // The inline search of GDockingShips by ship id (node +0xc) in cases 0x11, 0x12, 0x27,
    // 0x28, 0x3a: the first entry, or end().
    std::list<FDockQueueEntry>::iterator FindDockingShip(unsigned int ShipId)
    {
        return FindDockQueueEntry(&GDockingShips, ShipId);
    }

    // The inline map erase of cases 0x37 / 0x3a (FUN_06f694b0 over begin..end).
    void EraseAllDockLaunchQueues()
    {
        GDockLaunchQueues.erase(GDockLaunchQueues.begin(), GDockLaunchQueues.end());
    }

    // The inline vector erase of case 0x39 (06f5db56-06f5dc1d): the first terminal state of
    // this target is removed (the tail copied down, end -= 0xc).
    void EraseFirstTerminalOf(std::vector<FDockTerminal>* Terminals, unsigned int DockTargetId)
    {
        for (std::vector<FDockTerminal>::iterator It = Terminals->begin(); It != Terminals->end();
             ++It)
        {
            if (It->DockTargetId == DockTargetId)
            {
                Terminals->erase(It);
                return;
            }
        }
    }

    // FUN_06f5d560 (06f5d560-06f5e44b, RET 8). Parms is the message's dword array (Msg).
    //   06f5d58d-06f5d5d0: ids 0xd, 0x1f, 0x20, 0x2d, 0x2e, 0x38, 0x39, 0x3e, 0x1001..0x1005,
    //     0x1009, 0x100e go to the GCS set DAT_06fd1bf0 (FUN_06ee9900(id, parms)).
    //   06f5d5d5-06f5d5eb: 0xb, 0x39, 0x3e -> the NPC ship event list (FUN_06f683e0).
    //   06f5d5f0-06f5d615: the main switch (0x1005 -> 06f5e38e, 0x1010 -> 06f5e40c, else
    //     id - 0xb over the byte table 06f5e504 into the jump table 06f5e4a0).
    //   06f5d822-06f5e433 (tail, every id; 0x1005 / 0x1010 enter at 06f5d840, which only
    //     skips tests they cannot pass): 0x3c / 0x3d to the GCS set; id !=
    //     0x1007 -> ApplyDockTerminalStates (FUN_06f6bc20); every non-null ship of
    //     GSpaceSupportGuidedShips forwards (id, parms) to its guide (FUN_06f34c30); then the
    //     null entries are erased.
    void FSpaceSupport::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
    {
        using pub::Controller::EMessage;
        unsigned int const* Msg = static_cast<unsigned int const*>(Parms);

        // 06f5d58d-06f5d5d0 (byte tables 06f5e458 for 0xd..0x39, 06f5e490 for 0x1001..0x100e,
        // plus the JZ for 0x3e).
        switch (MessageId)
        {
        case EMessage::ObjectDamaged:
        case EMessage::Hail:
        case EMessage::Heartbeat:
        case EMessage::PhantomEnter:
        case EMessage::PhantomExit:
        case EMessage::DeathFuseLit:
        case EMessage::ObjectDestroyed:
        case EMessage::TimerExpired:
        case EMessage::CombatState:
        case EMessage::CombatDrift:
        case EMessage::NpcTurnedHostile:
        case EMessage::FleeRequest:
        case EMessage::DamageReputation:
        case EMessage::FormationLeaderChanged:
        case EMessage::ShipInSights:
            GGcsHandlers.HandleMessage(MessageId, Parms);                     // FUN_06ee9900
            break;
        default:
            break;
        }

        if (MessageId == EMessage::CommComplete || MessageId == EMessage::ObjectDestroyed ||
            MessageId == EMessage::TimerExpired)
            GNpcShipEvents.HandleMessage(MessageId, Parms);                   // FUN_06f683e0

        switch (MessageId)
        {
        case EMessage::CommComplete:
        {
            // 06f5dc22: a comm line ended {sender?, system, Arg1, Key}.
            FSystemInfo* System = FindSystemInfo(&Msg[1]);
            if (System != nullptr)
                System->SendPendingComms(Msg[2], Msg[0], Msg[3]);             // FUN_06f72980
            GGcsConversations.OnMessage(Parms);                               // FUN_06ee8bd0
            break;
        }
        case EMessage::ObjectDamaged:
        {
            // 06f5d763: sub-object damage (FSubObjDamageEvent).
            FSubObjDamageEvent const* Event = static_cast<FSubObjDamageEvent const*>(Parms);
            // 06f5d763-06f5d766: FUN_06f70490(parms) -> TU56 FUN_06f6ff90.
            OnDamageReputation(Event);
            FSystemInfo* System = FindSystemInfo(&Event->SystemId);           // not null-tested
            FContentShipBase* Ship =
                static_cast<FContentShipBase*>(System->FindShip(Event->ObjId));
            if (Ship != nullptr)
                Ship->AddDamage(&Event->InflictorId, Event->Damage);          // FUN_06f57310
            break;
        }
        case EMessage::DockCancel:
            // 06f5e023: {ship, pass-through}.
            CancelShipDocking(Msg[0], Msg[1], true);                          // FUN_06f6e810
            break;
        case EMessage::DockComplete:
        {
            // 06f5dd9d: {ship, ?, system}.
            FSystemInfo* System = FindSystemInfo(&Msg[2]);
            if (System == nullptr)
                break;
            unsigned int SystemId = System->SystemId;
            BreakFormationAtDock(&SystemId, &Msg[0], true, false);            // FUN_06f6d920
            SetDockEquipment(Msg[0], false, false, nullptr);                  // FUN_06f6dff0
            std::list<FDockQueueEntry>::iterator It = FindDockingShip(Msg[0]);
            if (It == GDockingShips.end())
                break;
            FinishDocking(&It);                                               // FUN_06f6e9d0
            UpdateDockQueues();                                               // FUN_06f6c250
            break;
        }
        case EMessage::DockRequest:
        {
            // 06f5dfe5: {ship, ...}.
            std::list<FDockQueueEntry>::iterator It = FindDockingShip(Msg[0]);
            if (It != GDockingShips.end())
                FinishDocking(&It);
            // 06f5e016-06f5e019: PUSH EBP (parms); MOV ECX,EBX (this, not read); CALL
            // FUN_06f6e110 -- the dock request; its DOCK_HOST_RESPONSE is not used here.
            HandleDockRequest(static_cast<FDockRequest const*>(Parms));
            break;
        }
        case EMessage::EquipDropLoot:
        {
            // 06f5d79e: equipment about to drop as loot (FEquipLootEvent; Server copies Desc
            // back and loots that). Not inflicted by a player's ship -> the count becomes 0.
            FEquipLootEvent* Event =
                static_cast<FEquipLootEvent*>(const_cast<void*>(Parms));
            if (FindContentPlayerStateByShip(&Event->InflictorId) == nullptr)
            {
                Event->Desc.set_count(0);                                     // 06f5d8bf
                break;
            }
            // 06f5d7b4-06f5d815: a one-element vector<EquipDesc> copy of the item goes
            // through FUN_06f5cfa0(FindSystemInfo(&SystemId), ObjId, &vector, 0) and its first
            // element is copied back (REP MOVSD 8 dwords).
            std::vector<EquipDesc> Items;
            Items.insert(Items.begin(), 1, Event->Desc);
            PrepareLoot(FindSystemInfo(&Event->SystemId), Event->ObjId, &Items, false);
            Event->Desc = Items[0];
            break;
        }
        case EMessage::TradelaneRequest:
            // 06f5e07b-06f5e083: PUSH EBP (parms); MOV ECX,EBX (this, not read);
            // CALL FUN_06f6fcb0; JMP 06f5d822.
            HandleTradelaneRequest(static_cast<FTradelaneRequest const*>(Parms));
            break;
        case EMessage::TradelaneRelease:
            // 06f5e088-06f5e090: PUSH EBP; MOV ECX,EBX; CALL FUN_06f6fc90; JMP 06f5d822.
            HandleTradelaneRelease(static_cast<FTradelaneRequest const*>(Parms));
            break;
        case EMessage::FormationRequest:
        {
            // 06f5dc55-06f5dca4: formation request {ship, leader, system}.
            FSystemInfo* System = FindSystemInfo(&Msg[2]);                    // FUN_06f75f30
            int const Result = HandleFormationRequest(&Msg[0], &Msg[1], System); // FUN_06f66690
            if (Result == 0)
            {
                // 06f5dc72-06f5dc8f: the ship (FUN_06f73930, not null-tested) leaves its old
                // formation state, is told 0, and follows a docking leader.
                FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(Msg[0]));
                ResetFormationState(Ship);                                    // FUN_06f671a0
                pub::SpaceObj::FormationResponse(Msg[0], static_cast<FORMATION_RTYPE>(0));
                FollowLeaderIntoDock(Ship);                                   // FUN_06f6ed10
            }
            else
            {
                pub::SpaceObj::FormationResponse(Msg[0], static_cast<FORMATION_RTYPE>(Result));
            }
            break;
        }
        case EMessage::FormationLeave:
        {
            // 06f5dcac: leave formation {ship, ?, system}.
            FSystemInfo* System = FindSystemInfo(&Msg[2]);                    // not null-tested
            FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(Msg[0]));
            if (Ship != nullptr && Ship->FormationId != 0xffffffffu)
            {
                CancelDockingBehindLeader(Ship, Ship->FormationId);           // FUN_06f6ee40
                Ship->SetFormation(0xffffffffu, 0, nullptr);                   // FUN_06f576c0
            }
            ResetFormationState(static_cast<FContentShipBase*>(System->FindShip(Msg[0])));
            if (!pub::SinglePlayer())
                break;
            FContentPlayerState* Player = FindContentPlayerStateByShip(&Msg[0]);
            if (Player != nullptr && Player->ValueE4 == 10)                   // FUN_06f48c10
                Player->SetValueE4(0);                                        // FUN_06f48c20
            break;
        }
        case EMessage::Heartbeat:
        {
            // 06f5d61c: the heartbeat. Every system runs its NPC ship lives, then every
            // system drops its flagged objects (both with the heartbeat period
            // DAT_06fce770, not the elapsed time).
            std::map<unsigned int, FSystemInfo>* Systems = GetSystemInfos();  // FUN_06f75f20
            for (std::map<unsigned int, FSystemInfo>::iterator It = Systems->begin();
                 It != Systems->end(); ++It)
            {
                It->second.UpdateNpcShipLives(GSpaceSupportHeartbeat);        // FUN_06f73fa0
            }
            for (std::map<unsigned int, FSystemInfo>::iterator It = Systems->begin();
                 It != Systems->end(); ++It)
            {
                It->second.DestroyFlaggedObjectsAwayFromPlayers(GSpaceSupportHeartbeat); // FUN_06f73dd0
            }
            UpdateDockQueues();                                               // FUN_06f6c250
            LaunchQueuedShips();                                              // FUN_06f607e0
            // 06f5d6bf-06f5d6c6: FUN_06f70480(DAT_06fce770) -> TU56 FUN_06f70580.
            OnHeartbeatReputation(GSpaceSupportHeartbeat);
            float DeltaTime = MsgFloat(Msg, 0);
            std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& Players =
                GetContentPlayerStates();
            for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator It =
                     Players.begin();
                 It != Players.end(); ++It)
            {
                FContentPlayerShip* Ship = It->second->ShipObject;
                if (Ship != nullptr)
                    Ship->UpdateHostileContacts(DeltaTime);                   // FUN_06f47590
            }
            GGcsConversations.Update(DeltaTime);                              // FUN_06ee8c50
            break;
        }
        case EMessage::Msg0x25:
        {
            // 06f5de18: {object, ?, system}: an object of the system's list is destroyed.
            FSystemInfo* System = FindSystemInfo(&Msg[2]);
            if (System == nullptr || System->FindSpaceObj(Msg[0]) == nullptr) // FUN_06f73990
                break;
            pub::SpaceObj::Destroy(Msg[0], static_cast<DestroyType>(0));
            break;
        }
        case EMessage::Msg0x27:
        {
            // 06f5dd40: {ship, ?, ?, byte cancel}.
            if (static_cast<unsigned char>(Msg[3]) != 0)
            {
                CancelShipDocking(Msg[0], 0, false);
                break;
            }
            std::list<FDockQueueEntry>::iterator It = FindDockingShip(Msg[0]);
            if (It == GDockingShips.end())
                break;
            It->bField_0x10 = true;
            // 06f5dd7d-06f5dd8b: FUN_06f6e530(node +0x10 target, node +0x14 index, 0).
            MarkDockTerminal(It->DockTargetId, It->DockIndex, false);
            UpdateDockQueues();
            break;
        }
        case EMessage::LaunchComplete:
        {
            // 06f5e03a: {?, ship}.
            std::list<FDockQueueEntry>::iterator It = FindDockingShip(Msg[1]);
            if (It != GDockingShips.end())
                FinishDocking(&It);
            break;
        }
        case EMessage::PlayerManeuver:
        {
            // 06f5e360: {player, E8 value, E4 value}.
            FContentPlayerState* Player = FindContentPlayerState(Msg[0]);     // FUN_06f49b00
            if (Player == nullptr)
                break;
            Player->SetValueE4(Msg[2]);
            Player->SetValueE8(Msg[1]);
            break;
        }
        case EMessage::FreeTerminalRequest:
        {
            // 06f5e251: free terminal request {player, dock target}. The player's ship arch,
            // its mission properties (TU51 FUN_06f587d0), a terminal for them (index -1 in
            // multiplayer, 0 in single player; no position) -> ReportFreeTerminal on 4.
            unsigned int ShipArchId;
            pub::Player::GetShipID(Msg[0], ShipArchId);
            std::vector<unsigned int> ClassIds;
            if (GetShipMissionProperties(ShipArchId, &ClassIds))
            {
                int DockIndex = pub::SinglePlayer() ? 0 : -1;
                if (ChooseDockTerminal(&ClassIds, &Msg[1], nullptr, &DockIndex) == DOCK_HOST_RESPONSE_4)
                    pub::ReportFreeTerminal(Msg[1], DockIndex);
            }
            break;
        }
        case EMessage::SpaceEnter:
        {
            // 06f5e100: player launch {system, player, dock target, terminal}.
            // 06f5e100-06f5e10d: multiplayer only, FUN_06f68960(parms).
            if (!pub::SinglePlayer())
                SyncSolarAttitudes(Msg);
            unsigned int TargetType;
            if (pub::SpaceObj::GetType(Msg[2], TargetType) != 0)
                break;
            if ((TargetType & 0x600de0) == 0)
                break;
            FDockQueueEntry Entry;                                            // FUN_06f6bbf0
            Entry.SystemId = Msg[0];
            Entry.DockTargetId = Msg[2];
            Entry.DockIndex = static_cast<int>(Msg[3]);
            pub::Player::GetShip(Msg[1], Entry.ShipId);
            Entry.bField_0x10 = true;
            GDockingShips.push_back(Entry);
            if ((TargetType & 0x200100) != 0)
            {
                // 06f5e1bb-06f5e1de: the dock hail handler keeps quiet about the ship for 30 s.
                int const HandlerType = 0;
                void* const AnyPhantom = nullptr;                              // DAT_06fbbb98
                static_cast<FGcsDockHailHandler*>(GetGcsHandler(&HandlerType))
                    ->SuppressHail(&Entry.ShipId, &AnyPhantom, 0x1e);           // FUN_06ee9b70
            }
            // 06f5e1e3-06f5e24a: the type 3 launch event (FindContentPlayerState(Msg[1]) is
            // looked up there and its result dropped).
            FGcsLaunchEvent Event;
            Event.System = FindSystemInfo(&Msg[0]);
            FindContentPlayerState(Msg[1]);                                   // FUN_06f49b00
            pub::SpaceObj::TerminalInfo Info;
            pub::SpaceObj::GetTerminalInfo(Entry.DockTargetId, Entry.DockIndex, Info);
            Event.Speaker = Entry.DockTargetId;
            Event.TerminalType = Info.Value;
            Event.Listener = Entry.ShipId;
            Event.DockIndex = Entry.DockIndex;
            GGcsConversations.QueueConversation(&Event);                      // FUN_06ee8d70
            break;
        }
        case EMessage::SpaceExit:
        {
            // 06f5e095: {system, player}: no system or an empty one drops every launch queue.
            if ((Msg[0] & 0x3fffffffu) == 0 || !IsContentSystemOccupied(Msg[0]))
                EraseAllDockLaunchQueues();                                   // FUN_06f694b0
            FSystemInfo* System = FindSystemInfo(&Msg[0]);
            if (System != nullptr)
                System->OnPlayerChanged(FindContentPlayerState(Msg[1]));      // FUN_06f72c90
            break;
        }
        case EMessage::ObjectDestroyed:
            OnObjectDestroyed(static_cast<FObjectDestroyedEvent*>(const_cast<void*>(Parms)));
            break;
        case EMessage::SystemExit:
        {
            // 06f5de53: system exit {system, dock target, player}.
            if ((Msg[0] & 0x3fffffffu) == 0 || !IsContentSystemOccupied(Msg[0]))
                EraseAllDockLaunchQueues();
            FSystemInfo* System = FindSystemInfo(&Msg[0]);
            if (System == nullptr)
                break;
            FContentPlayerState* Player = FindContentPlayerState(Msg[2]);     // not null-tested
            if (Player->ShipObject != nullptr)
            {
                // 06f5deba-06f5def2: the docking entry of the ship is finished unless its
                // SYSTEM (node +8 = entry +0, MOV ECX,[EAX+8]; CMP ECX,[EBP+4]) is Msg[1].
                std::list<FDockQueueEntry>::iterator It =
                    FindDockingShip(Player->ShipObject->SpaceObjId);
                if (It != GDockingShips.end() && It->SystemId != Msg[1])
                    FinishDocking(&It);
            }
            System->RemovePlayer(Player);                                     // FUN_06f72c30
            break;
        }
        case EMessage::SystemEnter:
        {
            // 06f5df07: system enter {system, ?, player, arrival object}.
            FSystemInfo* System = FindSystemInfo(&Msg[0]);
            if (System != nullptr)
            {
                FContentPlayerState* Player = FindContentPlayerState(Msg[2]);
                System->AddPlayer(Player);                                    // FUN_06f72a20
                if (Player->ShipObject != nullptr)
                {
                    Player->ShipObject->bScanTarget = 0;
                    Player->ShipObject->ResetHostileState();                  // FUN_06f47850
                }
            }
            // 06f5df42-06f5dfe0: arriving through an airlock (type & 0x400) -> the player's
            // ship enters the docking list as launched ({system, ship, arrival, 0, +0x10 1}).
            if (Msg[3] == 0)
                break;
            unsigned int ArrivalType;
            if (pub::SpaceObj::GetType(Msg[3], ArrivalType) != 0 || (ArrivalType & 0x400) == 0)
                break;
            FDockQueueEntry Entry;
            Entry.SystemId = Msg[0];
            Entry.DockTargetId = Msg[3];
            Entry.DockIndex = 0;
            pub::Player::GetShip(Msg[2], Entry.ShipId);
            Entry.bField_0x10 = true;
            GDockingShips.push_back(Entry);                                   // FUN_06f690a0
            break;
        }
        case EMessage::SystemLoaded:
            // 06f5e34b: FindSystemInfo(&Msg[0]) (not null-tested) -> full load.
            FindSystemInfo(&Msg[0])->LoadFull();                              // FUN_06f75940
            break;
        case EMessage::SystemUnloaded:
            // 06f5e301: full unload, then every applied terminal state is forgotten.
            FindSystemInfo(&Msg[0])->UnloadFull();                            // FUN_06f75bc0
            GDockTerminalsApplied.clear();
            break;
        case EMessage::TimerExpired:
        {
            // 06f5d72c-06f5d75e: the music timer cancels the player's music; any other
            // timer goes to the faction name queues (FUN_06f513c0) and the message router
            // (DAT_06fcf254 vtbl[+0x14]).
            pub::Controller::TimerExpired const* Timer =
                static_cast<pub::Controller::TimerExpired const*>(Parms);
            if (Timer->Param1 == GSpaceSupportMusicTimerId)
            {
                pub::Audio::CancelMusic(Timer->Param2);
                break;
            }
            HandleFactionNameTimer(Timer);
            GMessageRouter->OnTimerExpired(Timer);
            break;
        }
        case EMessage::DamageReputation:
        {
            // 06f5e38e: {kind, ship, other, system}: kind 1 and a player's ship sharing its
            // formation group with the other ship -> the ship leaves the formation.
            FSystemInfo* System = FindSystemInfo(&Msg[3]);
            if (System == nullptr || Msg[0] != 1)
                break;
            FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(Msg[1]));
            FContentShipBase* Other = static_cast<FContentShipBase*>(System->FindShip(Msg[2]));
            if (Ship == nullptr || Other == nullptr)
                break;
            if (FindContentPlayerStateByShip(&Msg[1]) == nullptr)
                break;
            if (Ship->FormationId == Other->FormationId)
                RemoveFromFormation(Ship);                                    // FUN_06f67090
            break;
        }
        case EMessage::NpcShipCreated:
            // 06f5e40c: a new NPC ship (&ship).
            ResetFormationState(*static_cast<FContentNpcShip* const*>(Parms)); // FUN_06f671a0
            break;
        default:
            break;
        }

        // 06f5d822-06f5d833.
        if (MessageId == EMessage::SystemLoaded || MessageId == EMessage::SystemUnloaded)
            GGcsHandlers.HandleMessage(MessageId, Parms);                     // FUN_06ee9900
        if (MessageId != EMessage::JumpgateAct)
            ApplyDockTerminalStates();                                        // FUN_06f6bc20

        for (std::list<FContentNpcShip*>::iterator It = GSpaceSupportGuidedShips.begin();
             It != GSpaceSupportGuidedShips.end(); ++It)
        {
            if (*It != nullptr)
                (*It)->ForwardToGuide(MessageId, Parms);                      // FUN_06f34c30
        }
        std::list<FContentNpcShip*>::iterator It = GSpaceSupportGuidedShips.begin();
        while (It != GSpaceSupportGuidedShips.end())
        {
            if (*It != nullptr)
                ++It;
            else
                It = GSpaceSupportGuidedShips.erase(It);
        }
    }

    // Case 0x39 of FUN_06f5d560 (06f5d8ca-06f5dc1d): an object was destroyed
    // (FObjectDestroyedEvent, server_gameobject.h).
    void FSpaceSupport::OnObjectDestroyed(FObjectDestroyedEvent* Event)
    {
        unsigned int ObjectId = Event->ObjId;

        // 06f5d8d7-06f5d910: players whose +0xe8 holds the object clear it.
        std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& Players =
            GetContentPlayerStates();
        for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator It =
                 Players.begin();
             It != Players.end(); ++It)
        {
            if (It->second->ValueE8 == ObjectId)                              // FUN_06f48c30
                It->second->SetValueE8(0);                                    // FUN_06f48c40
        }

        FSystemInfo* System = FindSystemInfo(&Event->SystemId);
        FContentShipBase* Ship = nullptr;
        if (System != nullptr)
            Ship = static_cast<FContentShipBase*>(System->FindShip(ObjectId));
        if (Ship != nullptr)
        {
            if (Ship->HasField120())                                          // FUN_06f57710
            {
                unsigned int Id120 = 0;
                unsigned int Id124 = 0;
                Ship->GetField120(&Id120, &Id124);                            // FUN_06f57720
                System->RemoveTradeLaneShip(&ObjectId, &Id120);               // 06f5d96a FUN_06f78dd0
            }

            FContentNpcShip* NpcShip = nullptr;
            bool bGuideKept = false;                                          // local_a1
            if (Ship->Slot04())
                NpcShip = static_cast<FContentNpcShip*>(Ship);

            if (Event->Kind == 1)
            {
                unsigned int TopPlayer = 0;
                unsigned int TopAny = 0;
                Ship->GetTopDamagers(&TopPlayer, &TopAny);                    // FUN_06f57500
                if (TopPlayer != 0 && TopPlayer == TopAny)
                {
                    // 06f5d9b4-06f5d9c7: the loot drop, phantom loot on.
                    PrepareLoot(System, Ship->SpaceObjId, &Event->DroppedEquip, true);
                }
                else
                {
                    // 06f5d9ce-06f5d9f1: nobody but a player earned the loot -- the dropped
                    // equipment is emptied (copy-down of the empty range + _Destroy).
                    Event->DroppedEquip.clear();
                }
                // 06f5d9fc-06f5da11: FUN_06f704a0(Ship, TopPlayer ? &TopPlayer :
                // &Event->KillerId) -- the TU56 reputation hit.
                ApplyKillReputation(Ship, TopPlayer != 0 ? &TopPlayer : &Event->KillerId);
                if (NpcShip != nullptr)
                    bGuideKept = PromoteFormationLeader(System, NpcShip);    // 06f5da20
                FContentPlayerState* Killer = FindContentPlayerStateByShip(&Event->KillerId);
                if (Killer != nullptr)
                    Killer->CountShipTypeKilled(Ship->ShipArchId);            // FUN_06f48fb0
            }

            // 06f5da51-06f5da74: an NPC ship's guide is told (slot +0x20, 0) unless kept.
            if (NpcShip != nullptr && !bGuideKept && NpcShip->GetGuide() != nullptr)
                NpcShip->Guide->SetShip(nullptr);                             // FUN_06f34ca0
        }

        // 06f5da77-06f5dac6: a killed object owned by a player -> "music_death" for him.
        if (Event->OwnerPlayer != 0 && Event->Kind == 1)
        {
            pub::Audio::Tryptich Track;
            Track.Field0 = 0;
            Track.Field4 = 0;
            Track.Field8 = 0;
            Track.FieldC = pub::GetNicknameId("music_death");
            Track.Field10 = 1;
            float Fade = -1.0f;
            std::memcpy(&Track.Field14, &Fade, sizeof(Fade));
            pub::Audio::SetMusic(Event->OwnerPlayer, Track);
        }

        // 06f5dac9-06f5db11: map erase(key) (FUN_06f69c50 equal_range, FUN_06f6a3b0 count,
        // FUN_06f694b0 range erase).
        GDockLaunchQueues.erase(ObjectId);
        RemoveContentNpcGateLock(&ObjectId);                                  // FUN_06f6d220
        unsigned int const AllPlayers = 0;                                    // DAT_06fbbb74
        UnregisterContentGateLock(&AllPlayers, &ObjectId);                    // FUN_06f6d5d0
        if (System != nullptr)
        {
            System->RemoveSpaceObj(ObjectId, true);                           // FUN_06f74310
            System->RemoveMissionObject(ObjectId);                            // FUN_06f74620
        }
        CancelShipDocking(ObjectId, 0, false);                                // FUN_06f6e810
        EraseFirstTerminalOf(&GDockTerminals, ObjectId);
        EraseFirstTerminalOf(&GDockTerminalsApplied, ObjectId);
    }
}

// FUN_06f616e0 (06f616e0-06f617f2). Per sphere (0x10 stride): the offset and its squared
// length stay on the x87 stack (z*z + y*y + x*x; double here), the length is the square times
// the 0x5f3759df estimate refined once (0.5f DAT_06fb605c, 1.5f DAT_06fb6058) from the
// square stored as float; FCOMP radius, TEST AH,5, JP: below -> inside, else (equal,
// greater, unordered) outside. Counts compared with the vector size ((last-first)>>4, a null
// first counts as 0) or with 0 (SETG).
bool __cdecl TestExclusionSpheres(float const* Position,
                                  std::vector<FSpacePopExclusionSphere> const* Spheres,
                                  bool const* bAll, bool const* bInside)
{
    if (Spheres == nullptr)
        return true;

    int Inside = 0;
    int Outside = 0;
    for (std::vector<FSpacePopExclusionSphere>::const_iterator It = Spheres->begin();
         It != Spheres->end(); ++It)
    {
        double DX = static_cast<double>(Position[0]) - It->X;
        double DY = static_cast<double>(Position[1]) - It->Y;
        double DZ = static_cast<double>(Position[2]) - It->Z;
        double Square = DZ * DZ + DY * DY + DX * DX;
        float StoredSquare = static_cast<float>(Square);
        int Bits;
        std::memcpy(&Bits, &StoredSquare, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Guess;
        std::memcpy(&Guess, &Bits, sizeof(Guess));
        double Distance =
            (1.5f - static_cast<double>(Guess) * Guess * Square * 0.5f) * Guess * Square;
        if (Distance < It->Radius)
            ++Inside;
        else
            ++Outside;
    }

    int Count = static_cast<int>(Spheres->size());
    if (*bAll)
        return *bInside ? Inside == Count : Outside == Count;
    return *bInside ? Inside > 0 : Outside > 0;
}

// FUN_06f6d400 (06f6d400-06f6d5ca).
void RegisterContentGateLock(unsigned int PlayerId, unsigned int GateId)
{
    for (int GateIndex = 0; GateIndex < static_cast<int>(GContentGateLocks.size()); ++GateIndex)
    {
        FGateLock& Gate = GContentGateLocks[GateIndex];
        if (Gate.GateId != GateId)
            continue;

        if (!ContainsId(Gate.PlayerIds, PlayerId))
            Gate.PlayerIds.push_back(PlayerId);
        return;
    }

    // 06f6d4de-06f6d5ca: a temporary player list {PlayerId} (local_30, EH state 0, unwind
    // 06fafdc0), the record copy-constructed from it (local_20 {GateId, copy}, FUN_06eb35b0,
    // state 1, unwind 06fafdc8), appended by copy (FUN_06f6f0d0); then the record, then the
    // temporary are destroyed.
    std::vector<unsigned int> Players;
    Players.push_back(PlayerId);                                            // FUN_06eb5770
    FGateLock Gate = { GateId, Players };
    GContentGateLocks.push_back(Gate);
}

// FUN_06f6d2b0 (06f6d2b0-06f6d3f5).
void CollectContentGateLocks(unsigned int PlayerId, std::vector<unsigned int>& OutGates)
{
    for (int GateIndex = 0; GateIndex < static_cast<int>(GContentGateLocks.size()); ++GateIndex)
    {
        FGateLock const& Gate = GContentGateLocks[GateIndex];
        if (ContainsId(Gate.PlayerIds, PlayerId) || ContainsId(Gate.PlayerIds, 0))
        {
            if (!ContainsId(OutGates, Gate.GateId))
                OutGates.push_back(Gate.GateId);
        }
    }
}

// FUN_06f6ce90 (06f6ce90-06f6cf12, cdecl): the first lock of GateId (none -> false); true when
// its players hold 0 (06f6cec3) or *PlayerId (06f6cef0). Callers TU27 06ef138c, TU41
// 06f2a123 / 06f2a540 / 06f2ac00 / 06f2afb0 / 06f2b670 (player 0), TU53 06f668d2.
bool IsContentGateLocked(unsigned int const* PlayerId, unsigned int const* GateId)
{
    for (int GateIndex = 0; GateIndex < static_cast<int>(GContentGateLocks.size()); ++GateIndex)
    {
        FGateLock const& Gate = GContentGateLocks[GateIndex];
        if (Gate.GateId != *GateId)
            continue;
        if (ContainsId(Gate.PlayerIds, 0))
            return true;
        return ContainsId(Gate.PlayerIds, *PlayerId);
    }
    return false;
}

// DAT_06fd1cbc: gates locked for NPC traffic, unique ids in insertion order.
namespace
{
    std::vector<unsigned int> GContentNpcGateLocks;
}

// FUN_06f6d1c0 (06f6d1c0-06f6d212): insert-unique at the end; the returned pair is dropped.
void RegisterContentNpcGateLock(unsigned int const* GateId)
{
    if (!ContainsId(GContentNpcGateLocks, *GateId))
        GContentNpcGateLocks.insert(GContentNpcGateLocks.end(), 1, *GateId);      // FUN_06f937c0
}

// FUN_06f6d280 (06f6d280-06f6d2ab).
bool IsContentNpcGateLocked(unsigned int const* GateId)
{
    return ContainsId(GContentNpcGateLocks, *GateId);
}

// FUN_06f6d220 (06f6d220-06f6d26f).
void RemoveContentNpcGateLock(unsigned int const* GateId)
{
    for (std::vector<unsigned int>::iterator It = GContentNpcGateLocks.begin();
         It != GContentNpcGateLocks.end(); ++It)
    {
        if (*It == *GateId)
        {
            GContentNpcGateLocks.erase(It);
            return;
        }
    }
}

namespace
{
    // The first Value of Ids is erased (FUN_06f69a10 at the std::find result).
    void EraseFirstId(std::vector<unsigned int>* Ids, unsigned int Value)
    {
        for (std::vector<unsigned int>::iterator It = Ids->begin(); It != Ids->end(); ++It)
        {
            if (*It == Value)
            {
                Ids->erase(It);
                return;
            }
        }
    }

    // FUN_06f6d5d0's per-lock step (06f6d674-06f6d6ee and 06f6d75b-06f6d7d3): the player is
    // erased; in single player a listed player 0 is erased too (the ContainsId(0) test, then
    // std::find of DAT_06fbbcf0 = 0). Returns whether the lock has no players left (a null
    // first pointer or a zero size).
    bool DropPlayerFromLock(FGateLock* Lock, unsigned int PlayerId)
    {
        EraseFirstId(&Lock->PlayerIds, PlayerId);
        if (pub::SinglePlayer() && ContainsId(Lock->PlayerIds, 0))
            EraseFirstId(&Lock->PlayerIds, 0);
        return Lock->PlayerIds.empty();
    }
}

// FUN_06f6d5d0 (06f6d5d0-06f6d813). The lock vector erases copy the tail down
// (FUN_06f6f810 / FUN_06f7f7f0) and destroy the last element (FUN_06f38140).
void UnregisterContentGateLock(unsigned int const* PlayerId, unsigned int const* GateId)
{
    if (*PlayerId == 0)
    {
        for (std::vector<FGateLock>::iterator It = GContentGateLocks.begin();
             It != GContentGateLocks.end(); ++It)
        {
            if (It->GateId == *GateId)
            {
                It->PlayerIds.clear();                                        // FUN_06f69a10 loop
                GContentGateLocks.erase(It);
                return;
            }
        }
        return;
    }

    if (*GateId == 0)
    {
        std::vector<FGateLock>::iterator It = GContentGateLocks.begin();
        while (It != GContentGateLocks.end())
        {
            if (DropPlayerFromLock(&*It, *PlayerId))
                It = GContentGateLocks.erase(It);
            else
                ++It;
        }
        return;
    }

    for (std::vector<FGateLock>::iterator It = GContentGateLocks.begin();
         It != GContentGateLocks.end(); ++It)
    {
        if (It->GateId != *GateId)
            continue;
        if (DropPlayerFromLock(&*It, *PlayerId))
            GContentGateLocks.erase(It);
        return;
    }
}

// FUN_06f6cf20 (06f6cf20-06f6d032): ship 0 -> false. A player's ship: the IsContentGateLocked
// test inlined (06f6cf3f-06f6cfb3). Otherwise the gate's system (GetSystem into a local
// preset to 0, result ignored; id & 0x3fffffff == 0 -> false) and its SystemInfo (null ->
// false); a ship found there that is not SpacePop's (FUN_06f33e80) -> false; then the gate
// in the NPC list.
bool IsGateLockedForShip(unsigned int ShipId, unsigned int GateId)
{
    if (ShipId == 0)
        return false;

    FContentPlayerState* Player = FindContentPlayerStateByShip(&ShipId);
    if (Player != nullptr)
        return IsContentGateLocked(&Player->PlayerId, &GateId);

    unsigned int SystemId = 0;
    pub::SpaceObj::GetSystem(GateId, SystemId);
    if ((SystemId & 0x3fffffffu) == 0)
        return false;
    FSystemInfo* System = FindSystemInfo(&SystemId);
    if (System == nullptr)
        return false;
    FContentSpaceObj* Object = System->FindShip(ShipId);
    if (Object != nullptr && !static_cast<FContentNpcShip*>(Object)->IsSpacePopShip())
        return false;
    return ContainsId(GContentNpcGateLocks, GateId);
}

// FUN_06f67cd0 (06f67cd0-06f67d7e): [locked_gates] of a save; locked_gate is registered
// for player 0 (.rdata 06fbbb74 = 0), npc_locked_gate for NPCs.
void LoadContentGateLocks(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("locked_gates"))
            continue;
        while (Reader->read_value())
        {
            if (Reader->is_value("locked_gate"))
            {
                unsigned int GateId = static_cast<unsigned int>(Reader->get_value_int(0));
                RegisterContentGateLock(0, GateId);                            // FUN_06f6d400
            }
            else if (Reader->is_value("npc_locked_gate"))
            {
                unsigned int GateId = static_cast<unsigned int>(Reader->get_value_int(0));
                RegisterContentNpcGateLock(&GateId);                            // FUN_06f6d1c0
            }
        }
    }
}

// FUN_06f67bc0 (06f67bc0-06f67ccd).
void SaveContentGateLocks(FILE* File)
{
    std::vector<unsigned int> Gates;
    CollectContentGateLocks(0, Gates);                                         // FUN_06f6d2b0
    if (!GContentNpcGateLocks.empty() || !Gates.empty())
        fprintf(File, "[%s]\n", "locked_gates");
    for (std::vector<unsigned int>::iterator It = GContentNpcGateLocks.begin();
         It != GContentNpcGateLocks.end(); ++It)
    {
        fprintf(File, "%s = %u\n", "npc_locked_gate", *It);
    }
    for (std::vector<unsigned int>::iterator It = Gates.begin(); It != Gates.end(); ++It)
        fprintf(File, "%s = %u\n", "locked_gate", *It);
}

// FUN_06f5e910 (06f5e910-06f5ea1b).
bool __cdecl BuildBestPathLegs(unsigned int /*Unused*/, FSystemInfo* System, Vector const* From,
                               Vector const* To, std::list<FNnPathLeg>* Legs, int Flags,
                               float const* Range, int Unused7)
{
    // 06f5e913-06f5e946: the result is kept in the Unused7 argument slot.
    bool const bPath = BuildNnPath(System, From, To, Legs, Flags, Range, Unused7);
    if (bPath)
        return bPath;

    // 06f5e94e-06f5e992: every node erased (FUN_06f68e30), --size each time.
    Legs->clear();

    // 06f5e994-06f5ea0e: {kind 1, From, To, 0, 0} pushed at the end (new 0x2c node, 9-dword
    // copy, ++size).
    FNnPathLeg Leg;
    Leg.Kind = 1;
    Leg.PointA = *From;
    Leg.PointB = *To;
    Leg.ObjectA = 0;
    Leg.ObjectB = 0;
    Legs->push_back(Leg);
    return bPath;
}

IDLL* CreateSpaceSupport(char const* /*Name*/, unsigned int ControllerId, void const* /*Parms*/)
{
    return new (std::nothrow) FSpaceSupport(ControllerId);
}
