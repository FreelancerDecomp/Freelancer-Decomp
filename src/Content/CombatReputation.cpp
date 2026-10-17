// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU56 (06f6fd90-06f70dd0): reputation reactions to combat. See
// CombatReputation.h.

#include "CombatReputation.h"
#include "ContentStaticIds.h"     // TU56's 16 static-init thunks 06f6fd90-06f6ff70
#include "BehaviorCmds.h"         // FContentShipBase (FormationId, HasField120, Slot08)
#include "ContentInternal.h"      // GSpaceSupportControllerId (DAT_06fd1c74)
#include "ContentPlayerState.h"   // FindContentPlayerStateByShip (FUN_06f49bc0), ...
#include "FactionProps.h"         // ApplyFactionReputationChange (FUN_06f50c50), GetFactionEventRate
#include "NpcBase.h"              // FContentSpaceObj, FSpaceObjAttacker (TU20)
#include "NpcShip.h"              // FContentNpcShip::CreationKind (+0x1b4)
#include "Populators.h"           // GSpacePopAttackClamp (DAT_06fd1c78)
#include "SpacePaths.h"           // FastLength (the 0x5f3759df length)
#include "SystemInfo.h"           // FSystemInfo (TU58), GetSystemInfos, FindSystemInfo
#include "common_ini.h"           // Vector
#include "common_server.h"
#include "common_server_controller.h"
#include "server_gameobject.h"    // FSubObjDamageEvent

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <vector>

namespace
{
    // One entry of the heartbeat's scan table (0xe4 bytes in retail): one object found
    // around a player. +0x20..+0x5f of the retail entry are never referenced (raw listing:
    // no label or access between DAT_06fd1d74 and DAT_06fd1db8) and are not kept.
    struct FNpcReputationScanEntry
    {
        unsigned char bValid;          // +0x00 (ctor 1)
        unsigned int SpaceObjId;       // +0x04 (ctor 0)
        Vector Position;               // +0x08 pub::SpaceObj::GetLocation (not initialised)
        unsigned int Type;             // +0x14 (ctor 0) pub::SpaceObj::GetType
        unsigned int Affiliation;      // +0x18 (ctor -1)
        int Reputation;                // +0x1c (ctor -1)
        unsigned char bPlayer;         // +0x60 (ctor 0) the ship of a player of the system
        float Attitudes[32];           // +0x64 (ctor 0) towards the entry of the same index

        // FUN_06f70530 (the _initterm entry 06fce038), per element: +4 = 0, +0x14 = 0,
        // +0x18 = +0x1c = -1, byte +0 = 1, byte +0x60 = 0, then the 32 attitudes = 0.
        FNpcReputationScanEntry()
            : bValid(1)
            , SpaceObjId(0)
            , Type(0)
            , Affiliation(0xffffffffu)
            , Reputation(-1)
            , bPlayer(0)
        {
            for (int Index = 0; Index < 32; ++Index)
                Attitudes[Index] = 0.0f;
        }
    };

    // DAT_06fd1d58: 32 entries, filled per player by FUN_06f70580.
    FNpcReputationScanEntry GNpcReputationScan[32];

    // DAT_06fd1d50: the attitude FUN_06f70580 sets, -0.6f copied from DAT_06fbbd7c by the
    // _initterm entry LAB_06f70570 (06fce03c).
    float GNpcAllyHostileAttitude = -0.6f;

    // Message 0x1005 sent to controller 0 by FUN_06f6ff90 (a stack block at ESP+0x3c).
    struct FDamageReputationMsg
    {
        unsigned int bProvoked;        // +0x00 1 at hostility 50 / damage causes 5 and 6, 0 at 25
        unsigned int AttackerId;       // +0x04 the event's InflictorId
        unsigned int VictimId;         // +0x08 the event's ObjId (or its solar parent)
        unsigned int SystemId;         // +0x0c the system's +0x48
    };

    // Message 0x1003 sent to the SpaceSupport controller by FUN_06f70580.
    struct FNpcHostilityMsg
    {
        unsigned int ObjectId;         // +0x00 the object whose attitude changed
        unsigned int OtherId;          // +0x04
        unsigned int SystemId;         // +0x08
    };

    void SendDamageReputation(unsigned int bProvoked, FSubObjDamageEvent const& Event,
                              FSystemInfo const* System)
    {
        FDamageReputationMsg Msg;
        Msg.bProvoked = bProvoked;
        Msg.AttackerId = Event.InflictorId;
        Msg.VictimId = Event.ObjId;
        Msg.SystemId = System->SystemId;
        unsigned int const Controller = 0;
        pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::DamageReputation, &Msg);
    }

    // The distance FUN_06f70580 inlines twice (06f70924-06f70986, 06f70a34-06f70a95): the x87
    // sum (dz*dz + dy*dy) + dx*dx of the float differences, then the 0x5f3759df estimate.
    double ScanDistance(Vector const& From, Vector const& To)
    {
        double const Dx = static_cast<double>(From.x) - To.x;
        double const Dy = static_cast<double>(From.y) - To.y;
        double const Dz = static_cast<double>(From.z) - To.z;
        return FastLength((Dz * Dz + Dy * Dy) + Dx * Dx);
    }
}

// FUN_06f6ff90 (06f6ff90-06f70471).
void __cdecl HandleDamageReputation(FSubObjDamageEvent const* InEvent)
{
    // 06f6ff9b-06f6ffa4: the nine dwords copied.
    FSubObjDamageEvent Event = *InEvent;

    // 06f6ffa6-06f6ffd8: causes 2..7 only.
    int const Cause = static_cast<int>(Event.Cause);
    bool bCause = false;
    if (Cause == 2)
        bCause = true;
    if (Cause == 3)
        bCause = true;
    if (Cause == 4)
        bCause = true;
    if (Cause == 5)
        bCause = true;
    if (Cause == 6)
        bCause = true;
    if (Cause != 7 && !bCause)
        return;

    // 06f6ffde-06f6fffb: the attacker is a player's ship and not the victim itself.
    if (FindContentPlayerStateByShip(&Event.InflictorId) == nullptr)
        return;
    if (Event.InflictorId == Event.ObjId)
        return;

    // 06f70001-06f7002a: a solar part stands for its parent.
    unsigned int Parent = 0;
    if (pub::SpaceObj::GetSolarParent(Event.ObjId, Parent) == 0)
        Event.ObjId = Parent;
    unsigned int VictimType = 0;
    if (Event.ObjId != 0)
    {
        pub::SpaceObj::GetType(Event.ObjId, VictimType);
        if ((VictimType & 0x7fb110) == 0)
            return;
    }

    // 06f70050-06f700a3.
    int AttackerRep = -1;
    pub::SpaceObj::GetRep(Event.InflictorId, AttackerRep);
    unsigned int AttackerAffiliation = 0xffffffffu;
    pub::Reputation::GetAffiliation(AttackerRep, AttackerAffiliation);
    int VictimRep = -1;
    pub::SpaceObj::GetRep(Event.ObjId, VictimRep);
    unsigned int VictimAffiliation = 0xffffffffu;
    pub::Reputation::GetAffiliation(VictimRep, VictimAffiliation);

    // 06f700a6-06f700de: type 0x2000 at no health hit on equipment type 0x800 (FCOMP 0.0,
    // TEST AH,0x41, JP skip: below or equal goes on): -0.1 (0xbdcccccd) with empathy.
    if (VictimType == 0x2000 && Event.RelativeHealth <= 0.0f && Event.EquipType == 0x800)
        ApplyFactionReputationChange(AttackerRep, -0.1f, VictimAffiliation, true);

    // 06f700e1-06f700ff.
    if (AttackerRep == -1)
        return;
    if (VictimRep == -1)
        return;
    if (AttackerAffiliation == 0xffffffffu && VictimAffiliation == 0xffffffffu)
        return;

    // 06f70105-06f70179: both attitudes above -0.6 (TEST AH,0x41 JNP: below or equal
    // returns), the victim's below 1.0 (TEST AH,1 JZ: an unordered compare goes on).
    float AttackerAttitude = 0.0f;
    pub::Reputation::GetAttitude(AttackerRep, VictimRep, AttackerAttitude);
    float VictimAttitude = 0.0f;
    pub::Reputation::GetAttitude(VictimRep, AttackerRep, VictimAttitude);
    if (AttackerAttitude <= -0.6f)
        return;
    if (VictimAttitude <= -0.6f)
        return;
    if (VictimAttitude >= 1.0f)
        return;

    // 06f7017f-06f701b9: the system is not null-tested (as retail).
    double Now = 0.0;
    pub::GetRwTime(Now);
    FSystemInfo* System = FindSystemInfo(&Event.SystemId);
    FContentSpaceObj* AttackerShip = System->FindShipOrSolar(Event.InflictorId);
    if (AttackerShip == nullptr)
        return;

    // 06f701bf-06f701ed: in single player nothing while the player runs a mission and the
    // player ship's slot 2 says so.
    if (pub::SinglePlayer())
    {
        FContentPlayerState* First = GetFirstContentPlayerState();
        if (First != nullptr && First->GetLiveMission() != 0 && First->ShipObject != nullptr
            && First->ShipObject->Slot08())
        {
            return;
        }
    }

    // 06f701f3-06f7020a: the attacker ship's slot for this victim.
    FSpaceObjAttacker* Slot = nullptr;
    if (!AttackerShip->FindOrAllocAttacker(Event.ObjId, &Slot))
        return;

    // 06f70210-06f70220, 06f70419-06f70467: causes 5 and 6 provoke at once.
    if (Cause == 5 || Cause == 6)
    {
        SendDamageReputation(1, Event, System);
        pub::Reputation::SetAttitude(VictimRep, AttackerRep, -1.0f);
        return;
    }

    // 06f70226-06f70279: the time since the previous hit; a new victim restarts the slot.
    double Elapsed = Now - Slot->LastTime;
    Slot->LastTime = static_cast<float>(Now);
    if (Slot->AttackerId != Event.ObjId)
    {
        Slot->AttackerId = Event.ObjId;
        Slot->FirstTime = static_cast<float>(Now);
        Slot->HostilityTime = static_cast<float>(Now);
        Elapsed = 0.0;
    }
    if (Elapsed > 1.0)
        Slot->HostilityTime = static_cast<float>(Now);

    // 06f70280-06f702bc: +10 at most once a second, plus 50 times the relative damage.
    if (Now - Slot->HostilityTime > 1.0)
    {
        Slot->HostilityTime = static_cast<float>(Now);
        Slot->Hostility += 10.0f;
    }
    Slot->Hostility =
        static_cast<float>(static_cast<double>(Event.RelativeDamage) * 50.0 + Slot->Hostility);

    // 06f702c3-06f703b9: 50 provokes the victim and its formation (TEST AH,1 JNZ: below or
    // unordered goes to the 25 test).
    if (Slot->Hostility >= 50.0f && Slot->bProvoked == 0)
    {
        Slot->bProvoked = 1;
        SendDamageReputation(1, Event, System);
        pub::Reputation::SetAttitude(VictimRep, AttackerRep, -1.0f);
        FContentSpaceObj* Victim = System->FindSpaceObj(Event.ObjId);
        if (Victim == nullptr)
            return;
        unsigned int const Group = static_cast<FContentShipBase*>(Victim)->FormationId;
        if (Group == 0xffffffffu)
            return;
        for (std::list<FContentSpaceObj*>::iterator It = System->SpaceObjs.begin();
             It != System->SpaceObjs.end(); ++It)
        {
            if (static_cast<FContentShipBase*>(*It)->FormationId != Group)
                continue;
            int MemberRep = -1;
            pub::SpaceObj::GetRep((*It)->SpaceObjId, MemberRep);
            pub::Reputation::SetAttitude(MemberRep, AttackerRep, -1.0f);
        }
        return;
    }

    // 06f703ba-06f70418: above 25 (TEST AH,0x41 JNZ: below or equal returns) warns once.
    if (!(Slot->Hostility > 25.0f))
        return;
    if (Slot->bWarned != 0)
        return;
    Slot->bWarned = 1;
    SendDamageReputation(0, Event, System);
}

// FUN_06f70490 (06f70490-06f7049b).
void __cdecl OnDamageReputation(FSubObjDamageEvent const* Event)
{
    HandleDamageReputation(Event);
}

// FUN_06f704a0 (06f704a0-06f70523).
void __cdecl ApplyKillReputation(FContentSpaceObj const* Ship, unsigned int const* KillerId)
{
    // 06f704a1: the random-population attack clamp.
    if (GSpacePopAttackClamp == 1)
        return;
    FContentPlayerState* Killer = FindContentPlayerStateByShip(KillerId);
    if (Killer == nullptr)
        return;
    // 06f704be-06f704db: retail passes uninitialised slots; both calls always write them.
    int ShipRep;
    pub::SpaceObj::GetRep(Ship->SpaceObjId, ShipRep);
    unsigned int Affiliation;
    pub::Reputation::GetAffiliation(ShipRep, Affiliation);
    // 06f704e1-06f70503: event 0; FCOMP with 0.0, TEST AH,0x44 JNP: only an equal compare
    // returns.
    float const Rate = GetFactionEventRate(Affiliation, 0);
    if (Rate == 0.0f)
        return;
    // 06f70505-06f70519: the killer's reputation (FUN_06f33f80 = +8) with empathy.
    ApplyFactionReputationChange(Killer->GetReputation(), Rate, Affiliation, true);
}

// FUN_06f70580 (06f70580-06f70dc4).
void __cdecl UpdateNpcReputations(float /*Unused*/)
{
    // 06f70587-06f70593: NEG/SBB/INC = !SinglePlayer().
    bool const bMultiplayer = !pub::SinglePlayer();
    unsigned int Found[32];
    std::map<unsigned int, FSystemInfo>* Systems = GetSystemInfos();
    for (std::map<unsigned int, FSystemInfo>::iterator SystemIt = Systems->begin();
         SystemIt != Systems->end(); ++SystemIt)
    {
        FSystemInfo* System = &SystemIt->second;
        std::vector<FContentPlayerState*>* Players = System->GetPlayers();
        // 06f705c3-06f705dc: no storage or no element.
        if (Players->empty())
            continue;
        // 06f705e2: once per system.
        unsigned int Count = 0;
        for (std::vector<FContentPlayerState*>::iterator PlayerIt = Players->begin();
             PlayerIt != Players->end(); ++PlayerIt)
        {
            FContentPlayerShip* PlayerShip = (*PlayerIt)->ShipObject;
            if (PlayerShip == nullptr)
                continue;

            // 06f70611-06f70669: up to 32 objects (type mask 0x7f31e0) within 5000.
            Vector Position;
            PlayerShip->GetPosition(&Position);
            unsigned int const SystemId = System->SystemId;
            pub::System::ScanObjects(SystemId, Found, 0x20, Position, 5000.0f, 0x7f31e0, Count);
            std::vector<unsigned int>* MissionObjects = System->GetMissionObjects();

            // 06f70677-06f707f8: fill the table.
            for (unsigned int Index = 0; Index < Count; ++Index)
            {
                FNpcReputationScanEntry& Entry = GNpcReputationScan[Index];
                Entry.Type = 0;
                Entry.Affiliation = 0xffffffffu;
                Entry.Reputation = -1;
                Entry.bValid = 1;
                Entry.bPlayer = 0;
                for (int Slot = 0; Slot < 32; ++Slot)
                    Entry.Attitudes[Slot] = 0.0f;
                Entry.SpaceObjId = Found[Index];
                for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
                     It != Players->end(); ++It)
                {
                    FContentPlayerShip* Ship = (*It)->ShipObject;
                    if (Ship != nullptr && Ship->SpaceObjId == Entry.SpaceObjId)
                        Entry.bPlayer = 1;
                }
                if (Entry.SpaceObjId == 0)
                    continue;

                // 06f706f7-06f7072b: NPC ships of the system in creation kinds 0 / 1 (single
                // player) or with +0x120 set are left out.
                bool bExcluded = false;
                FContentSpaceObj* Object = System->FindSpaceObj(Entry.SpaceObjId);
                if (Object != nullptr)
                {
                    FContentNpcShip* Npc = static_cast<FContentNpcShip*>(Object);
                    if (!bMultiplayer && (Npc->CreationKind == 0 || Npc->CreationKind == 1))
                        bExcluded = true;
                    if (Npc->HasField120())
                        bExcluded = true;
                }
                // 06f7072d-06f70753: in single player the mission objects too.
                if (!bMultiplayer
                    && std::find(MissionObjects->begin(), MissionObjects->end(), Found[Index])
                           != MissionObjects->end())
                {
                    Entry.bValid = 0;
                    continue;
                }
                if (bExcluded)
                {
                    Entry.bValid = 0;
                    continue;
                }

                // 06f7075d-06f707d6: place, type, reputation (a solar's first), affiliation.
                Matrix Orientation;
                int const LocationResult =
                    pub::SpaceObj::GetLocation(Entry.SpaceObjId, Entry.Position, Orientation);
                Entry.Type = 0;
                pub::SpaceObj::GetType(Entry.SpaceObjId, Entry.Type);
                int RepResult = 0;
                if ((Entry.Type & 0xfdff) != 0)
                {
                    RepResult = pub::SpaceObj::GetSolarRep(Entry.SpaceObjId, Entry.Reputation);
                    if (RepResult != 0)
                        RepResult = pub::SpaceObj::GetRep(Entry.SpaceObjId, Entry.Reputation);
                }
                else
                {
                    RepResult = pub::SpaceObj::GetRep(Entry.SpaceObjId, Entry.Reputation);
                }
                int const AffiliationResult =
                    pub::Reputation::GetAffiliation(Entry.Reputation, Entry.Affiliation);
                if (LocationResult != 0 || RepResult != 0 || AffiliationResult != 0)
                {
                    Entry.SpaceObjId = 0;
                    Entry.bValid = 0;
                }
            }

            // 06f707fe-06f70892: every valid entry's attitude towards every other one.
            for (unsigned int A = 0; A < Count; ++A)
            {
                FNpcReputationScanEntry& EntryA = GNpcReputationScan[A];
                if (EntryA.bValid == 0 || EntryA.SpaceObjId == 0)
                    continue;
                for (unsigned int B = 0; B < Count; ++B)
                {
                    FNpcReputationScanEntry& EntryB = GNpcReputationScan[B];
                    if (EntryB.bValid == 0 || EntryB.SpaceObjId == 0)
                        continue;
                    if (A == B)
                        EntryA.Attitudes[B] = 0.0f;
                    else
                        pub::Reputation::GetAttitude(EntryA.Reputation, EntryB.Reputation,
                                                     EntryA.Attitudes[B]);
                }
            }

            // 06f70898-06f70d80: A, liking B within 5000, turns hostile when B (or C) is
            // hostile to C (or B) within 5000 of B.
            for (unsigned int A = 0; A < Count; ++A)
            {
                FNpcReputationScanEntry& EntryA = GNpcReputationScan[A];
                if (EntryA.bValid == 0 || EntryA.SpaceObjId == 0
                    || EntryA.Affiliation == 0xffffffffu)
                {
                    continue;
                }
                for (unsigned int B = 0; B < Count; ++B)
                {
                    if (A == B)
                        continue;
                    FNpcReputationScanEntry& EntryB = GNpcReputationScan[B];
                    if (EntryB.bValid == 0 || EntryB.SpaceObjId == 0)
                        continue;
                    // 06f70924-06f70989: FCOMP 5000, TEST AH,0x41 JZ: farther skips.
                    if (ScanDistance(EntryA.Position, EntryB.Position) > 5000.0)
                        continue;
                    // 06f7098f-06f709a8: TEST AH,5 JNP: below 0.6 skips.
                    float const AttitudeAB = EntryA.Attitudes[B];
                    if (AttitudeAB < 0.6f)
                        continue;
                    for (unsigned int C = 0; C < Count; ++C)
                    {
                        if (B == C)
                            continue;
                        FNpcReputationScanEntry& EntryC = GNpcReputationScan[C];
                        if (EntryC.bValid == 0 || EntryC.SpaceObjId == 0)
                            continue;
                        if (EntryB.bPlayer != 0 && EntryC.bPlayer != 0)
                            continue;
                        if (EntryB.Affiliation == 0xffffffffu && EntryC.Affiliation == 0xffffffffu)
                            continue;
                        // 06f70a17-06f70a2e: the sign bit of the low type byte.
                        if ((EntryA.Type & 0x80) != 0)
                        {
                            if ((EntryB.Type & 0x80) != 0)
                                continue;
                            if ((EntryC.Type & 0x80) != 0)
                                continue;
                        }
                        if (ScanDistance(EntryB.Position, EntryC.Position) > 5000.0)
                            continue;
                        // 06f70a9e-06f70ab8: TEST AH,5 JNP: below -0.6 skips.
                        float const AttitudeAC = EntryA.Attitudes[C];
                        if (AttitudeAC < -0.6f)
                            continue;
                        // 06f70abe-06f70b03: C hostile to B (B to C when C has no affiliation);
                        // TEST AH,0x41: below or equal.
                        float const AttitudeBC = EntryB.Attitudes[C];
                        float const AttitudeCB = EntryC.Attitudes[B];
                        bool const bHostileCB = AttitudeCB <= -0.6f;
                        if (EntryC.Affiliation == 0xffffffffu)
                        {
                            if (!(AttitudeBC <= -0.6f))
                                continue;
                        }
                        else if (!bHostileCB)
                        {
                            continue;
                        }
                        // 06f70b09-06f70b37: TEST AH,1: not below (an unordered compare is
                        // "below").
                        bool const bLikesB = AttitudeAB >= 0.6f;
                        bool const bLikesC = AttitudeAC >= 0.6f;

                        FNpcHostilityMsg Msg;
                        if (EntryB.Affiliation == 0xffffffffu)
                        {
                            // 06f70b40-06f70c1b.
                            if (!bLikesC)
                                continue;
                            float BReputation = 0.0f;
                            pub::Reputation::GetReputation(EntryB.Reputation, EntryC.Affiliation,
                                                           BReputation);
                            // FABS, FLD 0.01 (double), FADD ST0,ST0, FCOMPP, TEST AH,0x41.
                            double const Gap =
                                std::fabs(static_cast<double>(BReputation) - AttitudeCB);
                            bool const bMatches = 0.01 + 0.01 > Gap;
                            if (bMatches)
                            {
                                // TEST AH,1 JZ: 1.0 or above (ordered) skips.
                                if (AttitudeAC >= 1.0f)
                                    continue;
                                pub::Reputation::SetAttitude(EntryA.Reputation, EntryC.Reputation,
                                                             GNpcAllyHostileAttitude);
                                Msg.ObjectId = EntryA.SpaceObjId;
                                Msg.OtherId = EntryB.SpaceObjId;
                            }
                            else
                            {
                                if (AttitudeAB >= 1.0f)
                                    continue;
                                pub::Reputation::SetAttitude(EntryA.Reputation, EntryB.Reputation,
                                                             GNpcAllyHostileAttitude);
                                Msg.ObjectId = EntryA.SpaceObjId;
                                Msg.OtherId = EntryC.SpaceObjId;
                            }
                        }
                        else if (bLikesB)
                        {
                            // 06f70c24-06f70c95.
                            if (bLikesC)
                                continue;
                            if (AttitudeAC >= 1.0f)
                                continue;
                            if (!(AttitudeAC <= -0.6f))
                                continue;
                            pub::Reputation::SetAttitude(EntryA.Reputation, EntryC.Reputation,
                                                         GNpcAllyHostileAttitude);
                            Msg.ObjectId = EntryA.SpaceObjId;
                            Msg.OtherId = EntryB.SpaceObjId;
                        }
                        else
                        {
                            // 06f70c97-06f70cea.
                            if (AttitudeAB >= 1.0f)
                                continue;
                            if (!(AttitudeAB <= -0.6f))
                                continue;
                            pub::Reputation::SetAttitude(EntryA.Reputation, EntryB.Reputation,
                                                         GNpcAllyHostileAttitude);
                            Msg.ObjectId = EntryA.SpaceObjId;
                            Msg.OtherId = EntryB.SpaceObjId;
                        }
                        Msg.SystemId = System->SystemId;
                        pub::Controller::_SendMessage(GSpaceSupportControllerId,
                                                      pub::Controller::EMessage::NpcTurnedHostile, &Msg);
                    }
                }
            }
        }
    }
}

// FUN_06f70480 (06f70480-06f7048d): MOV [ESP+4],EAX of its own argument, JMP FUN_06f70580.
void __cdecl OnHeartbeatReputation(float HeartbeatPeriod)
{
    UpdateNpcReputations(HeartbeatPeriod);
}
